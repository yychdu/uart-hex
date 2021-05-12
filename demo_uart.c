#include <stdio.h>
#include "string.h"
#include "iot_debug.h"
#include "iot_uart.h"
#include "iot_os.h"

HANDLE uart_task_handle;

#define uart_print iot_debug_print
#define UART_PORT1 OPENAT_UART_1
#define UART_PORT2 OPENAT_UART_2
#define UART_USB   OPENAT_UART_USB
#define UART_RECV_TIMEOUT (5 * 1000) // 2S

typedef enum
{
    UART_RECV_MSG = 1,

}TASK_MSG_ID;

typedef struct
{
    TASK_MSG_ID id;
    UINT32 len;
    char *param;
}TASK_MSG;

char HexChar(char c)
{

	if ((c >= '0') && (c <= '9'))
		return c-'0';//16进制中的，字符0-9转化成10进制，还是0-9
	else if ((c >= 'A') && (c <= 'F'))
		return c-'A'+10;//16进制中的A-F，分别对应着11-16
	else if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10; //16进制中的a-f，分别对应也是11-16，不区分大小写
	else
        return 0x10;   // 其他返回0x10
}

int Str2Hex(char* str, char *data)

{
	int t, t1;
	int rlen = 0, len = strlen(str);
	if (len == 1)
	{
		char h = str[0];
		t = HexChar(h);
		data[0] = (char)t;
		rlen++;
	}
	for (int i = 0; i < len;)
	{
		char l, h = str[i];//高八位和低八位
		i++;
		if (h == ' ')
            continue;
		if (i >= len)
			break;
		l = str[i];
		// if (str[i] == '0' && str[i - 1] == '0')
		// {
		// 	data[rlen++] = 0;//0 也是 '\0'到这里就结束了
		// 	i++;
		// 	continue;
		// }
		t = HexChar(h);
		t1 = HexChar(l);
		if ((t == 0x10) || (t1 == 0x10)) //判断为非法的16进制数
			break;
        else
			t = t * 16 + t1;
		i++;
		data[rlen] = (char)t;
		rlen++;
	}
	return rlen;
}

 
VOID uart_msg_send(HANDLE hTask, TASK_MSG_ID id, VOID *param, UINT32 len)
{
    TASK_MSG *msg = NULL;

    msg = (TASK_MSG *)iot_os_malloc(sizeof(TASK_MSG));
    msg->id = id;
    msg->param = param;
    msg->len = len;

    iot_os_send_message(hTask, msg);
}

//中断方式读串口1数据
//注: 中断中有复杂的逻辑,要发送消息到task中处理
void uart_recv_handle(T_AMOPENAT_UART_MESSAGE* evt)
{
	INT8 *recv_buff = NULL;
    int32 recv_len;
    int32 dataLen = evt->param.dataLen;
	if(dataLen)
	{
		recv_buff = iot_os_malloc(dataLen);
		if(recv_buff == NULL)
		{
			iot_debug_print("uart_recv_handle_0 recv_buff malloc fail %d", dataLen);
		}	
		switch(evt->evtId)
		{
		    case OPENAT_DRV_EVT_UART_RX_DATA_IND:

		        recv_len = iot_uart_read(UART_PORT2, (UINT8*)recv_buff, dataLen , UART_RECV_TIMEOUT);
		        iot_debug_print("uart_recv_handle_1:recv_len %d", recv_len);
				uart_msg_send(uart_task_handle, UART_RECV_MSG, recv_buff, recv_len);
		        break;

		    case OPENAT_DRV_EVT_UART_TX_DONE_IND:
		        iot_debug_print("uart_recv_handle_2 OPENAT_DRV_EVT_UART_TX_DONE_IND");
		        break;
		    default:
		        break;
		}
	}
}

VOID uart_write(VOID)
{
	
    char write_buff[] = "01 03 00 00 00 02 C4 0B";
	char trans_buff[100] = {0};
	int32 write_len = Str2Hex(write_buff, trans_buff);
	iot_uart_write(UART_PORT2, (UINT8 *)trans_buff, 8);
	iot_debug_print("[uart]send ok");
}

VOID uart_open(VOID)
{
    BOOL err;
    T_AMOPENAT_UART_PARAM uartCfg;
    
    memset(&uartCfg, 0, sizeof(T_AMOPENAT_UART_PARAM));
    uartCfg.baud = OPENAT_UART_BAUD_9600; //波特率
    uartCfg.dataBits = 8;   //数据位
    uartCfg.stopBits = 1; // 停止位
    uartCfg.parity = OPENAT_UART_NO_PARITY; // 无校验
    uartCfg.flowControl = OPENAT_UART_FLOWCONTROL_NONE; //无流控
    uartCfg.txDoneReport = TRUE; // 设置TURE可以在回调函数中收到OPENAT_DRV_EVT_UART_TX_DONE_IND
    uartCfg.uartMsgHande = uart_recv_handle; //回调函数

    // 配置uart1 使用中断方式读数据
    err = iot_uart_open(UART_PORT2, &uartCfg);
	iot_debug_print("[uart] uart_open_2 err: %d", err);

	uartCfg.txDoneReport = FALSE;
	uartCfg.uartMsgHande = NULL;
	err = iot_uart_open(UART_USB, &uartCfg);
	iot_debug_print("[uart] uart_open_usb err: %d", err);
}

VOID uart_close(VOID)
{
    iot_uart_close(UART_PORT2);
    iot_uart_close(UART_USB);
    iot_debug_print("[uart] uart_close_1");
}

VOID uart_init(VOID)
{   
    uart_open(); // 打开串口1和串口2 (串口1中断方式读数据, 串口2轮训方式读数据)
}

static VOID uart_task_main(PVOID pParameter)
{
	TASK_MSG *msg = NULL;
	bool flag = TRUE;
	char res[1024] = "";
	char *message = res;
	char *head = res;
	float temp, Hum;
	while(1)
	{
		if (flag)
		{
			int i = 10;
			while (--i)
				{
					uart_write();
					iot_os_sleep(1000);
				}
			flag = FALSE;
		}
		iot_os_wait_message(uart_task_handle, (PVOID*)&msg);
		switch(msg->id)
	    {
	        case UART_RECV_MSG:
				memset(res, 0, sizeof(res));
				message = head;
				// iot_debug_print("[uart] uart_task_main_1 recv_len %s", msg->param);
				for (int i = 0; i < 8; i++)
				{
					sprintf(message,"%02x ", msg->param[i]);
					message += 2;
				}
				*message = '\0';
				iot_debug_print("[uart] recv_msg %s", head);
				temp = (HexChar(head[6])) * 16 * 16 * 16 + (HexChar(head[7])) * 16 * 16 + (HexChar(head[8]))* 16 + HexChar(head[9]);
				temp /= 100;
				Hum = HexChar(head[10]) * 16 * 16 * 16 + HexChar(head[11]) * 16 * 16 + HexChar(head[12] - 0)* 16 + HexChar(head[13]);
				Hum /= 100;
				iot_debug_print("[uart] temp = %.2f, Hum = %.2f", temp, Hum);
				break;
	        default:
	            break;
	    }

	    if(msg)
	    {
	        if(msg->param)
	        {
	            iot_os_free(msg->param);
	            msg->param = NULL;
	        }
	        iot_os_free(msg);
	        msg = NULL;
			iot_debug_print("[uart] uart_task_main_2 uart free");
	    }
		uart_write(); //串口2 写数据
	}
}

static VOID usb_task_main(PVOID param)
{
	INT8 *recv_buff = iot_os_malloc(32);
	CHAR buff[64];
	UINT32 len = 0;
	while (1)
	{
		len = iot_uart_read(UART_USB, (UINT8 *)recv_buff, 32, UART_RECV_TIMEOUT);
		if (len == 0)
		{
			iot_os_sleep(10);
		}
		else
		{
			snprintf(buff, len, "%s", recv_buff);
			iot_debug_print("[uart] usb_task_main %s", buff);
		}
	}
}

int appimg_enter(void *param)
{    
    iot_debug_print("[uart] appimg_enter");
	iot_vat_send_cmd("AT^TRACECTRL=0,1,1\r\n", sizeof("AT^TRACECTRL=0,1,1\r\n"));
    uart_init();
	uart_task_handle =  iot_os_create_task(uart_task_main, NULL, 4096, 1, OPENAT_OS_CREATE_DEFAULT, "uart_task");
	// iot_os_create_task(usb_task_main, NULL, 4096, 1, OPENAT_OS_CREATE_DEFAULT, "usb_task");

    return 0;
}

void appimg_exit(void)
{
    iot_debug_print("[uart] appimg_exit");
}


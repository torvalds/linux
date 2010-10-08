/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/
/*!
	\file	spibusdrv.c

	\brief	spi bus driver module

	This file contains implementation of the spi bus driver.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include "smscoreapi.h"
#include "smsdbg_prn.h"
#include "smsspicommon.h"
#include "smsspiphy.h"
#include <linux/spi/spi.h>
#if 	SIANO_HALFDUPLEX
#include <linux/kthread.h>//hzb@20100902
#endif

#define ANDROID_2_6_25
#ifdef ANDROID_2_6_25
#include <linux/workqueue.h>
#endif

#define DRV_NAME	"siano1186"


#define SMS_INTR_PIN			19  /* 0 for nova sip, 26 for vega in the default, 19 in the reality */
#define TX_BUFFER_SIZE			0x200
#if 0
#define RX_BUFFER_SIZE			(0x1000 + SPI_PACKET_SIZE + 0x100)
#define NUM_RX_BUFFERS			 64 // change to 128
#else
#define RX_BUFFER_SIZE			(0x10000 + SPI_PACKET_SIZE + 0x100)
#define NUM_RX_BUFFERS			 4 // change to 128
#endif


u32 g_Sms_Int_Counter=0;
u32 g_Sms_MsgFound_Counter=0;

extern unsigned  long   u_irq_count;

struct _spi_device_st {
	struct _spi_dev dev;
	void *phy_dev;

	struct completion write_operation;
	struct list_head tx_queue;
	int allocatedPackets;
	int padding_allowed;
	char *rxbuf;
	dma_addr_t rxbuf_phy_addr;

	struct smscore_device_t *coredev;
	struct list_head txqueue;
	char *txbuf;
	dma_addr_t txbuf_phy_addr;
};

struct _smsspi_txmsg {
	struct list_head node;	/*! internal management */
	void *buffer;
	size_t size;
	int alignment;
	int add_preamble;
	struct completion completion;
	void (*prewrite) (void *);
	void (*postwrite) (void *);
};

struct _spi_device_st *spi_dev;

static int spi_resume_fail = 0 ;
static int spi_suspended   = 0 ;



static void spi_worker_thread(void *arg);

#if SIANO_HALFDUPLEX
int g_IsTokenOwned=false;
int g_IsTokenEnable=false;
struct semaphore 	HalfDuplexSemaphore;
struct task_struct	*SPI_Thread;
static int SPI_Thread_IsStop=0;
#define MSG_HDR_FLAG_STATIC_MSG		0x0001	// Message is dynamic when this bit is '0'
#else
static DECLARE_WORK(spi_work_queue, (void *)spi_worker_thread);
#endif

static u8 smsspi_preamble[] = { 0xa5, 0x5a, 0xe7, 0x7e };

// to support dma 16byte burst size
static u8 smsspi_startup[] = { 0,0,0,0,0,0,0,0,0, 0, 0xde, 0xc1, 0xa5, 0x51, 0xf1, 0xed };

//static u32 default_type = SMS_NOVA_A0;
static u32 default_type = SMS_VEGA;
static u32 intr_pin = SMS_INTR_PIN;

module_param(default_type, int, 0644);
MODULE_PARM_DESC(default_type, "default board type.");

module_param(intr_pin, int, 0644);
MODULE_PARM_DESC(intr_pin, "interrupt pin number.");

/******************************************/

void spilog_panic_print(void)
{
    if(spi_dev)
    {
        printk("spidev rxbuf_phy_addr =[0x%x]\n",spi_dev->rxbuf_phy_addr) ;
        printk("spidev txbufphy_addr  =[0x%x]\n",spi_dev->txbuf_phy_addr) ;
        printk("spidev TX_BUFFER_SIZE = [0x%x]\n",TX_BUFFER_SIZE) ;
    }
}

static void spi_worker_thread(void *arg)
{
	struct _spi_device_st *spi_device = spi_dev;
	struct _smsspi_txmsg *msg = NULL;
	struct _spi_msg txmsg;
	int i=0;


#if SIANO_HALFDUPLEX
	static u8 s_SpiTokenMsgBuf[256] = {0};
	const u8 g_PreambleBytes[4] = { 0xa5, 0x5a, 0xe7, 0x7e};
	struct SmsMsgHdr_ST s_SpiTokenSendMsg = {MSG_SMS_SPI_HALFDUPLEX_TOKEN_HOST_TO_DEVICE, 0, 11, sizeof(struct SmsMsgHdr_ST), MSG_HDR_FLAG_STATIC_MSG};

	memcpy( s_SpiTokenMsgBuf, g_PreambleBytes, sizeof(g_PreambleBytes) );
	memcpy( &s_SpiTokenMsgBuf[sizeof(g_PreambleBytes)], &s_SpiTokenSendMsg, sizeof(s_SpiTokenSendMsg) );

	PDEBUG("worker start\n");
	
	do {
		mdelay(3);
		if (g_IsTokenEnable){
			if (g_IsTokenOwned){
				if (!msg && !list_empty(&spi_device->txqueue))
					msg = (struct _smsspi_txmsg *)list_entry(spi_device->txqueue.next, struct _smsspi_txmsg, node);

				if (!msg) {
					// TX queue empty - give up token
					sms_debug("TX queue empty - give up token\n");
					g_IsTokenOwned = false;
					txmsg.len = 256;
					txmsg.buf = s_SpiTokenMsgBuf;
					txmsg.buf_phy_addr = 0;//zzf spi_device->txbuf_phy_addr;
					smsspi_common_transfer_msg(&spi_device->dev,&txmsg, 0);
				} else {
					sms_debug("msg is not null\n");
					if (msg->add_preamble) {// need to add preamble
						txmsg.len = min(msg->size + sizeof(smsspi_preamble),(size_t) TX_BUFFER_SIZE);
						txmsg.buf = spi_device->txbuf;
						txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
						memcpy(txmsg.buf, smsspi_preamble, sizeof(smsspi_preamble));
						memcpy(&txmsg.buf[sizeof(smsspi_preamble)],msg->buffer,txmsg.len - sizeof(smsspi_preamble));
						msg->add_preamble = 0;
						msg->buffer = (char*)msg->buffer + txmsg.len - sizeof(smsspi_preamble);
						msg->size -= txmsg.len - sizeof(smsspi_preamble);
						/* zero out the rest of aligned buffer */
						memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
						if(spi_resume_fail||spi_suspended) 
						{
							sms_err(KERN_EMERG " SMS1180: spi failed\n");
						} else {
							smsspi_common_transfer_msg(&spi_device->dev, &txmsg, 1);
						}
					} else {// donot need to add preamble
						txmsg.len = min(msg->size, (size_t) TX_BUFFER_SIZE);
						txmsg.buf = spi_device->txbuf;
						txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
						memcpy(txmsg.buf, msg->buffer, txmsg.len);
				
						msg->buffer = (char*)msg->buffer + txmsg.len;
						msg->size -= txmsg.len;
						/* zero out the rest of aligned buffer */
						memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
						if(spi_resume_fail||spi_suspended){
							sms_err(KERN_EMERG " SMS1180: spi failed\n");
						} else {
							smsspi_common_transfer_msg(&spi_device->dev,&txmsg, 0);
						}
					}
				} 

			} else {
				if(0)//spi_resume_fail||spi_suspended) 
				{
					sms_err(KERN_EMERG " SMS1180: spi failed\n") ;	  
				} else {
					sms_debug(KERN_EMERG "[SMS]spi_worker_thread token enable wait HalfDuplexSemaphore\n") ;
					if (SPI_Thread_IsStop)
						return -EINTR;
					if (down_interruptible(&HalfDuplexSemaphore))
						return -EINTR;
					sms_debug(KERN_EMERG "[SMS]spi_worker_thread token enable get HalfDuplexSemaphore\n") ;
					smsspi_common_transfer_msg(&spi_device->dev, NULL, 1);
				}
			}
		}else {
			sms_debug(KERN_EMERG "[SMS]spi_worker_thread token disable wait HalfDuplexSemaphore\n") ;
			if (SPI_Thread_IsStop)
					return -EINTR;
			if (down_interruptible(&HalfDuplexSemaphore))
					return -EINTR;
			sms_debug(KERN_EMERG "[SMS]spi_worker_thread token disable get HalfDuplexSemaphore\n") ;
			if (!msg && !list_empty(&spi_device->txqueue))
				msg = (struct _smsspi_txmsg *)list_entry(spi_device->txqueue.next, struct _smsspi_txmsg, node);
			if (msg) {
				if (msg->add_preamble) {// need to add preamble
					txmsg.len = min(msg->size + sizeof(smsspi_preamble),(size_t) TX_BUFFER_SIZE);
					txmsg.buf = spi_device->txbuf;
					txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
					memcpy(txmsg.buf, smsspi_preamble, sizeof(smsspi_preamble));
					memcpy(&txmsg.buf[sizeof(smsspi_preamble)],msg->buffer,txmsg.len - sizeof(smsspi_preamble));
					msg->add_preamble = 0;
					msg->buffer = (char*)msg->buffer + txmsg.len - sizeof(smsspi_preamble);
					msg->size -= txmsg.len - sizeof(smsspi_preamble);
					/* zero out the rest of aligned buffer */
					memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
					if(spi_resume_fail||spi_suspended) 
					{
						sms_err(KERN_EMERG " SMS1180: spi failed\n");
					} else {
						smsspi_common_transfer_msg(&spi_device->dev, &txmsg, 1);
					}
				} else {// donot need to add preamble
					txmsg.len = min(msg->size, (size_t) TX_BUFFER_SIZE);
					txmsg.buf = spi_device->txbuf;
					txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
					memcpy(txmsg.buf, msg->buffer, txmsg.len);
			
					msg->buffer = (char*)msg->buffer + txmsg.len;
					msg->size -= txmsg.len;
					/* zero out the rest of aligned buffer */
					memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
					if(spi_resume_fail||spi_suspended) 
					{
						sms_err(KERN_EMERG " SMS1180: spi failed\n");
					} else {
						smsspi_common_transfer_msg(&spi_device->dev,&txmsg, 0);
					}
				}
			} else {
				if(0)//spi_resume_fail||spi_suspended) 
				{
					sms_err(KERN_EMERG " SMS1180: spi failed\n") ;	  
				} else {
					smsspi_common_transfer_msg(&spi_device->dev, NULL, 1);
				}
			}

		}
			/* if there was write, have we finished ? */
		if (msg && !msg->size) {
			/* call postwrite call back */
			if (msg->postwrite)
				msg->postwrite(spi_device);

			list_del(&msg->node);
			complete(&msg->completion);
			msg = NULL;
		}
		/* if there was read, did we read anything ? */


		//check if we lost msg, if so, recover
		if(g_Sms_MsgFound_Counter < g_Sms_Int_Counter){
		//	sms_err("we lost msg, probably becouse dma time out\n");
			//for(i=0; i<16; i++)
			{
				//smsspi_common_transfer_msg(&spi_device->dev, NULL, 1);
			}
			g_Sms_MsgFound_Counter = g_Sms_Int_Counter;
		}
	}while(1);
#else
	PDEBUG("worker start\n");
	do{
        	mdelay(6);
        /* do we have a msg to write ? */
		if (!msg && !list_empty(&spi_device->txqueue))
			msg = (struct _smsspi_txmsg *)list_entry(spi_device->txqueue.next, struct _smsspi_txmsg, node);
        if (msg) {
			if (msg->add_preamble) {// need to add preamble
				txmsg.len = min(msg->size + sizeof(smsspi_preamble),(size_t) TX_BUFFER_SIZE);
				txmsg.buf = spi_device->txbuf;
				txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
				memcpy(txmsg.buf, smsspi_preamble, sizeof(smsspi_preamble));
				memcpy(&txmsg.buf[sizeof(smsspi_preamble)],msg->buffer,txmsg.len - sizeof(smsspi_preamble));
				msg->add_preamble = 0;
				msg->buffer = (char*)msg->buffer + txmsg.len - sizeof(smsspi_preamble);
				msg->size -= txmsg.len - sizeof(smsspi_preamble);
				/* zero out the rest of aligned buffer */
				memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
                if(spi_resume_fail||spi_suspended) 
                {
                    sms_err(KERN_EMERG " SMS1180: spi failed\n");
                } else {
                    smsspi_common_transfer_msg(&spi_device->dev, &txmsg, 1);
                }
			} else {// donot need to add preamble
				txmsg.len = min(msg->size, (size_t) TX_BUFFER_SIZE);
				txmsg.buf = spi_device->txbuf;
				txmsg.buf_phy_addr = spi_device->txbuf_phy_addr;
				memcpy(txmsg.buf, msg->buffer, txmsg.len);

				msg->buffer = (char*)msg->buffer + txmsg.len;
				msg->size -= txmsg.len;
				/* zero out the rest of aligned buffer */
				memset(&txmsg.buf[txmsg.len], 0, TX_BUFFER_SIZE - txmsg.len);
                if(spi_resume_fail||spi_suspended) 
                {
                    sms_err(KERN_EMERG " SMS1180: spi failed\n");
                } else {
                    smsspi_common_transfer_msg(&spi_device->dev,&txmsg, 0);
                }
			}
		} else {
            if(spi_resume_fail||spi_suspended) 
            {
                sms_err(KERN_EMERG " SMS1180: spi failed\n") ;     
            } else {
                smsspi_common_transfer_msg(&spi_device->dev, NULL, 1);
            }
        }

		/* if there was write, have we finished ? */
		if (msg && !msg->size) {
			/* call postwrite call back */
			if (msg->postwrite)
				msg->postwrite(spi_device);

			list_del(&msg->node);
			complete(&msg->completion);
			msg = NULL;
		}
		/* if there was read, did we read anything ? */


		//check if we lost msg, if so, recover
		if(g_Sms_MsgFound_Counter < g_Sms_Int_Counter)
		{
			sms_err("we lost msg, probably becouse dma time out\n");
			//for(i=0; i<16; i++)
			{
				//smsspi_common_transfer_msg(&spi_device->dev, NULL, 1);
			}
			g_Sms_MsgFound_Counter = g_Sms_Int_Counter;
		}
	} while (!list_empty(&spi_device->txqueue) || msg);
#endif
}

unsigned  long u_msgres_count =0;
static void msg_found(void *context, void *buf, int offset, int len)
{
	struct _spi_device_st *spi_device = (struct _spi_device_st *) context;
	struct smscore_buffer_t *cb =
	    (struct smscore_buffer_t
	     *)(container_of(buf, struct smscore_buffer_t, p));

    g_Sms_MsgFound_Counter++;
    u_msgres_count ++;
    
    sms_debug("Msg_found count = %d\n", u_msgres_count);
    //printk("Msg_found count = %d\n", u_msgres_count);

    if(len > RX_BUFFER_SIZE || offset >RX_BUFFER_SIZE )
    {
        sms_debug("SMS1180: msg rx over,len=0x%x,offset=0x%x\n",len,offset ) ;
        sms_debug("SMS1180: cb->p = [0x%x]\n",(unsigned int) cb->p) ;
        sms_debug("SMS1180: cb->phys=[0x%x]\n",(unsigned int) cb->phys) ;
    } 
    
	cb->offset = offset;
	cb->size = len;

	smscore_onresponse(spi_device->coredev, cb);


}

static void smsspi_int_handler(void *context)
{
	g_Sms_Int_Counter++;
    
    if(spi_resume_fail||spi_suspended) 
    {
        sms_err(KERN_EMERG " SMS1180: spi failed\n") ;
        return ;                       
    }
#if SIANO_HALFDUPLEX
	up(&HalfDuplexSemaphore);
	sms_debug(KERN_EMERG "[SMS]smsspi_int_handler send HalfDuplexSemaphore@intr\n") ;
#else
	schedule_work(&spi_work_queue);
#endif
}



static int smsspi_queue_message_and_wait(struct _spi_device_st *spi_device,
					 struct _smsspi_txmsg *msg)
{
    init_completion(&msg->completion);
	list_add_tail(&msg->node, &spi_device->txqueue);
#if SIANO_HALFDUPLEX
	if(!g_IsTokenEnable){
		sms_debug(KERN_EMERG "[SMS]smsspi_queue_message_and_wait token disable send HalfDuplexSemaphore@writemsg\n") ;
		up(&HalfDuplexSemaphore);
	} else {
		sms_debug(KERN_EMERG "[SMS]smsspi_queue_message_and_wait send HalfDuplexSemaphore\n") ;
	}
#else
	schedule_work(&spi_work_queue);
#endif
	wait_for_completion(&msg->completion);
	return 0;
}


static int smsspi_SetIntLine(void *context)
{
	struct _Msg {
		struct SmsMsgHdr_ST hdr;
		u32 data[3];
	} Msg = {
		{
		MSG_SMS_SPI_INT_LINE_SET_REQ, 0, HIF_TASK,
			    sizeof(struct _Msg), 0}, {
		0, intr_pin, 1000}
	};
	struct _smsspi_txmsg msg;

	PDEBUG("Sending SPI Set Interrupt command sequence\n");

	msg.buffer = &Msg;
	msg.size = sizeof(Msg);
	msg.alignment = SPI_PACKET_SIZE;
	msg.add_preamble = 1;
	msg.prewrite = NULL;
	msg.postwrite = NULL;	/* smsspiphy_restore_clock; */
	smsspi_queue_message_and_wait(context, &msg);
	return 0;
}


static int smsspi_preload(void *context)
{
	struct _smsspi_txmsg msg;
	struct _spi_device_st *spi_device = (struct _spi_device_st *) context;
    int ret;

	prepareForFWDnl(spi_device->phy_dev);
	PDEBUG("Sending SPI init sequence\n");


	msg.buffer = smsspi_startup;
	msg.size = sizeof(smsspi_startup);
	msg.alignment = 4;
	msg.add_preamble = 0;
	msg.prewrite = NULL;	/* smsspiphy_reduce_clock; */
	msg.postwrite = NULL;

    printk(KERN_EMERG "smsmdtv: call smsspi_queue_message_and_wait\n") ;
	smsspi_queue_message_and_wait(context, &msg);

	ret = smsspi_SetIntLine(context);
        sms_info("smsspi_preload set int line ret = 0x%x",ret);
    //return ret;
	return 0;

}


static int smsspi_postload(void *context)
{
	struct _Msg {
		struct SmsMsgHdr_ST hdr;
		u32 data[1];
	} Msg = {
		{
		MSG_SMS_SET_PERIODIC_STATS_REQ, 0, HIF_TASK,
			    sizeof(struct _Msg), 0}, {
		1}
	};
	struct _spi_device_st *spi_device = (struct _spi_device_st *) context;
	struct _smsspi_txmsg msg;

	sms_debug("Sending Period Statistics Req\n");
    
    //This function just speed up the SPI clock
	fwDnlComplete(spi_device->phy_dev, 0);
	msg.buffer = &Msg;
	msg.size = sizeof(Msg);
	msg.alignment = SPI_PACKET_SIZE;
	msg.add_preamble = 1;
	msg.prewrite = NULL;
	msg.postwrite = NULL;	/* smsspiphy_restore_clock; */

	g_Sms_Int_Counter=0;
	g_Sms_Int_Counter=0;

	u_irq_count = 0;

	return 0;
}


static int smsspi_write(void *context, void *txbuf, size_t len)
{
	struct _smsspi_txmsg msg;

	msg.buffer = txbuf;
	msg.size = len;
	msg.prewrite = NULL;
	msg.postwrite = NULL;

	if (len > 0x1000) {
		/* The FW is the only long message. Do not add preamble,
		and do not padd it */
		msg.alignment = 4;
		msg.add_preamble = 0;
		msg.prewrite = smschipreset;
	} else {
		msg.alignment = SPI_PACKET_SIZE;
		msg.add_preamble = 1;
	}

	return smsspi_queue_message_and_wait(context, &msg);
}

struct _rx_buffer_st *allocate_rx_buf(void *context, int size)
{
	struct smscore_buffer_t *buf;
	struct _spi_device_st *spi_device = (struct _spi_device_st *) context;
	if (size > RX_BUFFER_SIZE) {
		PERROR("Requested size is bigger than max buffer size.\n");
		return NULL;
	}
	buf = smscore_getbuffer(spi_device->coredev);
//	printk("smsmdtv: Recieved Rx buf %p physical 0x%x (contained in %p)\n", buf->p,
//	       buf->phys, buf);

	/* note: this is not mistake! the rx_buffer_st is identical to part of
	   smscore_buffer_t and we return the address of the start of the
	   identical part */

//  smscore_getbuffer return null, lets also return null
	if(NULL == buf)
	{
		return NULL;
	}

	return (struct _rx_buffer_st *) &buf->p;
}

static void free_rx_buf(void *context, struct _rx_buffer_st *buf)
{
	struct _spi_device_st *spi_device = (struct _spi_device_st *) context;
	struct smscore_buffer_t *cb =
	    (struct smscore_buffer_t
	     *)(container_of(((void *)buf), struct smscore_buffer_t, p));
//	printk("smsmdtv: buffer %p is released.\n", cb);
	smscore_putbuffer(spi_device->coredev, cb);
}

/*! Release device STUB

\param[in]	dev:		device control block
\return		void
*/
static void smsspi_release(struct device *dev)
{
	PDEBUG("nothing to do\n");
	/* Nothing to release */
}

static int smsspi_driver_probe(struct platform_device *pdev)
{
    PDEBUG("smsspi_probe\n") ;
    return 0 ;
}

extern void smschar_reset_device(void) ;
extern void smschar_set_suspend(int suspend_on);

extern int sms_suspend_count ;

#if 0   //hzb rockchip@20100525 
static struct platform_device smsspi_device = {
	.name = "smsspi",
	.id = 1,
	.dev = {
		.release = smsspi_release,
		},
};
#endif

static struct platform_driver smsspi_driver = {
    .probe   = smsspi_driver_probe,

    .driver  = {
         .name = "smsspi",
    },       
};

void smsspi_poweron(void)
{
    int ret=0;
    ret = smsspibus_ssp_resume(spi_dev->phy_dev) ;
    if( ret== -1)
    {
       sms_err(KERN_INFO "smsspibus_ssp_resume failed\n") ;

	}
}


void smsspi_off(void)
{
    smschar_reset_device() ;

    smsspibus_ssp_suspend(spi_dev->phy_dev) ;
}


static int siano1186_probe( struct spi_device *Smsdevice)
{
    struct smsdevice_params_t params;
    int ret;
    struct _spi_device_st *spi_device;
    struct _spi_dev_cb_st common_cb;

    sms_debug(KERN_INFO "siano1186_probe\n") ;

    spi_device =
    kmalloc(sizeof(struct _spi_device_st), GFP_KERNEL);
    if(!spi_device)
    {
        sms_err("spi_device is null smsspi_register\n") ;
        return 0;
    }
    spi_dev = spi_device;

    INIT_LIST_HEAD(&spi_device->txqueue);

    spi_device->txbuf = dma_alloc_coherent(NULL, max(TX_BUFFER_SIZE,PAGE_SIZE),&spi_device->txbuf_phy_addr, GFP_KERNEL | GFP_DMA);

    if (!spi_device->txbuf) {
        sms_err(KERN_INFO "%s dma_alloc_coherent(...) failed\n", __func__);
        ret = -ENOMEM;
        goto txbuf_error;
    }
    
    sms_debug(KERN_INFO "smsmdtv: spi_device->txbuf = 0x%x  spi_device->txbuf_phy_addr= 0x%x\n",
            (unsigned int)spi_device->txbuf, spi_device->txbuf_phy_addr);

    spi_device->phy_dev =  smsspiphy_init(Smsdevice, smsspi_int_handler, spi_device);

    if (spi_device->phy_dev == 0) {
        sms_err(KERN_INFO "%s smsspiphy_init(...) failed\n", __func__);
        goto phy_error;
    }

    common_cb.allocate_rx_buf = allocate_rx_buf;
    common_cb.free_rx_buf = free_rx_buf;
    common_cb.msg_found_cb = msg_found;
    common_cb.transfer_data_cb = smsspibus_xfer;

    ret =  smsspicommon_init(&spi_device->dev, spi_device, spi_device->phy_dev, &common_cb);
    if (ret) {
        sms_err(KERN_INFO "%s smsspiphy_init(...) failed\n", __func__);
        goto common_error;
    }

    /* register in smscore */
    memset(&params, 0, sizeof(params));
    params.context = spi_device;
    params.device = &Smsdevice->dev;
    params.buffer_size = RX_BUFFER_SIZE;
    params.num_buffers = NUM_RX_BUFFERS;
    params.flags = SMS_DEVICE_NOT_READY;
    params.sendrequest_handler = smsspi_write;
    strcpy(params.devpath, "spi");
    params.device_type = default_type;

    if (0) {
        /* device family */
        /* params.setmode_handler = smsspi_setmode; */
    } else {
        params.flags =
        SMS_DEVICE_FAMILY2 | SMS_DEVICE_NOT_READY |
        SMS_ROM_NO_RESPONSE;
        params.preload_handler = smsspi_preload;
        params.postload_handler = smsspi_postload;
    }

#if SIANO_HALFDUPLEX
	g_IsTokenOwned = false;
	init_MUTEX_LOCKED(&HalfDuplexSemaphore);
	SPI_Thread = kthread_run(spi_worker_thread,NULL,"cmmb_spi_thread");
	SPI_Thread_IsStop = 0;
#endif


    ret = smscore_register_device(&params, &spi_device->coredev);
    if (ret < 0) {
        sms_err(KERN_INFO "%s smscore_register_device(...) failed\n", __func__);
        goto reg_device_error;
    }

    ret = smscore_start_device(spi_device->coredev);
    if (ret < 0) {
        sms_err(KERN_INFO "%s smscore_start_device(...) failed\n", __func__);
        goto start_device_error;
    }
    spi_resume_fail = 0 ;
    spi_suspended = 0 ;

    sms_info(KERN_INFO "siano1186_probe exiting\n") ;
   
    PDEBUG("exiting\n");
    return 0;

start_device_error:
    smscore_unregister_device(spi_device->coredev);

reg_device_error:

common_error:
    smsspiphy_deinit(spi_device->phy_dev);

phy_error:
    
#if 0  //spi buff kmalloc  
    kfree(spi_device->txbuf);
#else
    dma_free_coherent(NULL, TX_BUFFER_SIZE, spi_device->txbuf,spi_device->txbuf_phy_addr);
#endif
   
txbuf_error:
    kfree(spi_device);
    sms_err("exiting error %d\n", ret);

    return ret;
}


void smsspi_remove(void)
{
	struct _spi_device_st *spi_device = spi_dev;
	sms_info(KERN_INFO "smsmdtv: in smsspi_unregister\n") ;
        int ret;
#if SIANO_HALFDUPLEX
	SPI_Thread_IsStop = 1;
  up(&HalfDuplexSemaphore);
  sms_info("stop kthread \n");
	ret = kthread_stop(SPI_Thread);
	sms_info("stop kthread ret = 0x%x\n",ret);
#endif
	/* stop interrupts */
	smsspiphy_deinit(spi_device->phy_dev);
	
	smscore_unregister_device(spi_device->coredev);

	dma_free_coherent(NULL, TX_BUFFER_SIZE, spi_device->txbuf,spi_device->txbuf_phy_addr);
	
	kfree(spi_device);

	sms_info("smsspi_remove exiting\n");
}

static struct spi_driver siano1186_driver = {
	.driver = {
		   .name = DRV_NAME,
		   .bus = &spi_bus_type,
		   .owner = THIS_MODULE,
		   },
	.probe = siano1186_probe,
	.remove = __devexit_p(smsspi_remove),
};

int smsspi_register(void)
{
    sms_info(KERN_INFO "smsmdtv: in smsspi_register\n") ;
    spi_register_driver(&siano1186_driver); 
}

void smsspi_unregister(void)
{
	spi_unregister_driver(&siano1186_driver);
}

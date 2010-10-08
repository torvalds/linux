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
//#define PXA_310_LV

#include <linux/kernel.h>
#include <asm/irq.h>
//#include <asm/hardware.h>

#include <linux/init.h>
#include <linux/module.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <asm/dma.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "smsdbg_prn.h"
#include <linux/spi/spi.h>
#include <mach/gpio.h>
#include "smscoreapi.h"
#include <linux/notifier.h>

#include <mach/iomux.h>
#include "smsspiphy.h"
//#include <mach/cmmb_io.h>
//#define CMMB_1186_SPIIRQ RK2818_PIN_PE1  //This Pin is SDK Board GPIOPortE_Pin1 
//#define CMMB_1186_PWR_EN   GPIOPortH_Pin7//This Pin is SDK Board GPIOPortE_Pin1 

#if 0
//define the gpio used 
#define CMMB_1186_SPIIRQ		 RK2818_PIN_PA6  //This Pin is SDK Board GPIOPortA_Pin6 
#define CMMB_1186_POWER_DOWN	 FPGA_PIO2_09 
#define CMMB_1186_POWER_ENABLE	 FPGA_PIO4_03
#define CMMB_1186_POWER_RESET	 FPGA_PIO2_06
#endif

/*! macro to align the divider to the proper offset in the register bits */
#define CLOCK_DIVIDER(i)((i-1)<<8)	/* 1-4096 */


#define SPI_PACKET_SIZE 256


unsigned  long u_irq_count =0;

static unsigned int dma_rxbuf_phy_addr ;
static unsigned int dma_txbuf_phy_addr ;

static int     rx_dma_channel =0 ;
static int     tx_dma_channel =0 ;
static volatile int     dma_len = 0 ;
static volatile int     tx_len  = 0 ;

static struct ssp_dev*  panic_sspdev = NULL ;
static struct cmmb_io_def_s* cmmb_io_ctrl =NULL;

extern void smscore_panic_print(void);
extern void spilog_panic_print(void) ;
static void chip_powerdown();
extern void smschar_reset_device(void);

static int sms_panic_handler(struct notifier_block *this,
			      unsigned long         event,
			      void                  *unused) 
{
    static int panic_event_handled = 0;
    if(!panic_event_handled)
    {
       smscore_panic_print() ;
       spilog_panic_print() ; 
       sms_debug("last tx_len = %d\n",tx_len) ;
       sms_debug("last DMA len = %d\n",dma_len) ;

       panic_event_handled =1 ; 
    }
    return NOTIFY_OK ;
}

static struct notifier_block sms_panic_notifier = {
	.notifier_call	= sms_panic_handler,
	.next		= NULL,
	.priority	= 150	/* priority: INT_MAX >= x >= 0 */
};




/*!  GPIO functions for PXA3xx
*/
// obsolete
void pxa3xx_gpio_set_rising_edge_detect (int gpio_id, int dir)
{
#if 0
	;
#endif
}

void pxa3xx_gpio_set_direction(int gpio_id , int dir)
{
#if 0
	;
#endif
}
//////////////////////////////////////////////////////////

/* physical layer variables */
/*! global bus data */
struct spiphy_dev_s {
	//struct ssp_dev sspdev;	/*!< ssp port configuration */
	struct completion transfer_in_process;
    struct spi_device *Smsdevice; 
	void (*interruptHandler) (void *);
	void *intr_context;
	struct device *dev;	/*!< device model stuff */
	int rx_dma_channel;
	int tx_dma_channel;
	int rx_buf_len;
	int tx_buf_len;
};




/*!
invert the endianness of a single 32it integer

\param[in]		u: word to invert

\return		the inverted word
*/
static inline u32 invert_bo(u32 u)
{
	return ((u & 0xff) << 24) | ((u & 0xff00) << 8) | ((u & 0xff0000) >> 8)
		| ((u & 0xff000000) >> 24);
}

/*!
invert the endianness of a data buffer

\param[in]		buf: buffer to invert
\param[in]		len: buffer length

\return		the inverted word
*/

static int invert_endianness(char *buf, int len)
{
	int i;
	u32 *ptr = (u32 *) buf;

	len = (len + 3) / 4;
	for (i = 0; i < len; i++, ptr++)
		*ptr = invert_bo(*ptr);

	return 4 * ((len + 3) & (~3));
}

/*! Map DMA buffers when request starts

\return	error status
*/
static unsigned long dma_map_buf(struct spiphy_dev_s *spiphy_dev, char *buf,
		int len, int direction)
{
	unsigned long phyaddr;	/* map dma buffers */
	if (!buf) {
		PERROR(" NULL buffers to map\n");
		return 0;
	}
	/* map buffer */
/*
	phyaddr = dma_map_single(spiphy_dev->dev, buf, len, direction);
	if (dma_mapping_error(phyaddr)) {
		PERROR("exiting  with error\n");
		return 0;
	}
*/
	return phyaddr;
}

static irqreturn_t spibus_interrupt(int irq, void *context)
{
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
    
	u_irq_count ++;
    
//	PDEBUG("INT counter = %d\n", u_irq_count);
//	printk("cmmb siano 1186 int\n");
        sms_info("spibus_interrupt %d\n", u_irq_count);
    
	if (spiphy_dev->interruptHandler)
		spiphy_dev->interruptHandler(spiphy_dev->intr_context);
    
	return IRQ_HANDLED;

}

/*!	DMA controller callback - called only on BUS error condition

\param[in]	channel: DMA channel with error
\param[in]	data: Unused
\param[in]	regs: Unused
\return		void
*/

//extern dma_addr_t common_buf_end ;

static void spibus_dma_handler(int channel, void *context)
{
#if 0

#endif   
}

void smsspibus_xfer(void *context, unsigned char *txbuf,
		    unsigned long txbuf_phy_addr, unsigned char *rxbuf,
		    unsigned long rxbuf_phy_addr, int len)
{
    struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
    unsigned char *temp = NULL;
    int ret;

#if SIANO_HALFDUPLEX
	if(txbuf)
	{
	 //  sms_debug("tx_buf:%x,%x,%x,%x,%x,%x", txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4],txbuf[5]);
	    sms_debug("rxbuf 4, 5,6,7,8, 9,10,11=%x,%x,%x,%x",rxbuf[4],rxbuf[5],rxbuf[6],rxbuf[7]);
       sms_debug(",%x,%x,%x,%x\n",rxbuf[8],rxbuf[9],rxbuf[10],rxbuf[11]);
	   ret = spi_write(spiphy_dev->Smsdevice, txbuf, len);
	} else {
		if ((rxbuf)&&(len != 16))
			ret = spi_read(spiphy_dev->Smsdevice, rxbuf, len);
	}
#else
    if(txbuf)
    {
       //sms_debug("tx_buf:%x,%x,%x,%x,%x,%x", txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4],txbuf[5]);
       ret = spi_write(spiphy_dev->Smsdevice, txbuf, len);
    }
     
    if ((rxbuf)&&(len != 16))
        ret = spi_read(spiphy_dev->Smsdevice, rxbuf, len);
#endif

   //sms_debug("rxbuf 4, 5,6,7,8, 9,10,11=%x,%x,%x,%x",rxbuf[4],rxbuf[5],rxbuf[6],rxbuf[7]);
     //  sms_debug(",%x,%x,%x,%x\n",rxbuf[8],rxbuf[9],rxbuf[10],rxbuf[11]);
    //printk("len=%x,rxbuf 4, 5,8,9Mlen=%x,%x,%x,%x,%x,%x\n",len,rxbuf[4],rxbuf[5],rxbuf[8],rxbuf[9],rxbuf[13],rxbuf[12]);

}

void smschipreset(void *context)
{

}

static struct ssp_state  sms_ssp_state ;

void smsspibus_ssp_suspend(void* context )
{
	struct spiphy_dev_s *spiphy_dev ;

	sms_info("entering smsspibus_ssp_suspend\n");
	
	if(!context)
	{
		sms_info("smsspibus_ssp_suspend context NULL \n") ;
		return ;
	}
	spiphy_dev = (struct spiphy_dev_s *) context;

 // free_irq(gpio_to_irq(CMMB_1186_SPIIRQ), NULL);

	chip_powerdown();
    
}
static void chip_poweron()
{
#if 0    
#ifdef CONFIG_MACH_LC6830_PHONE_BOARD_1_0
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO4), 1);
	mdelay(100);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO6), 1);
	mdelay(1);
#elif defined CONFIG_MACH_LC6830_PHONE_BOARD_1_1
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO6), 1);
	mdelay(50);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO8), 1);
	mdelay(200);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO4), 1);
	mdelay(1);
#endif
#else

#endif

//1186 cmmb power on
//set the SPI CS mode , zyc
	//rk2818_mux_api_set(GPIOB4_SPI0CS0_MMC0D4_NAME,1);
	if(cmmb_io_ctrl)
	{

		gpio_direction_output(cmmb_io_ctrl->cmmb_pw_rst,0);
		gpio_direction_output(cmmb_io_ctrl->cmmb_pw_dwn,0);

	//	GPIOSetPinDirection(CMMB_1186_POWER_ENABLE,1);
	      mdelay(100);
		gpio_direction_output(cmmb_io_ctrl->cmmb_pw_en,1);
	//	gpio_set_value(CMMB_1186_POWER_ENABLE,GPIO_HIGH);
		mdelay(200);
	//	gpio_set_value(CMMB_1186_POWER_DOWN,GPIO_HIGH);
		gpio_direction_output(cmmb_io_ctrl->cmmb_pw_dwn,1);

		mdelay(200);
	//	gpio_set_value(CMMB_1186_POWER_RESET,GPIO_HIGH);
		gpio_direction_output(cmmb_io_ctrl->cmmb_pw_rst,1);

		mdelay(200);
	  
		printk("cmmb chip_poweron !!!!\n");
		}
}

static void chip_powerdown()
{
#if 0    //hzb test
#ifdef CONFIG_MACH_LC6830_PHONE_BOARD_1_0
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO4), 0);
	mdelay(50);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO6), 0);
#elif defined CONFIG_MACH_LC6830_PHONE_BOARD_1_1
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO4), 0);
	mdelay(100);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO8), 0);
	mdelay(100);
	gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO6), 0);
	mdelay(1);
#endif
#else


#endif

//1186 cmmb power down
#if 1
//	GPIOSetPinDirection(CMMB_1186_POWER_ENABLE,1);
if(cmmb_io_ctrl)
{
	gpio_direction_output(cmmb_io_ctrl->cmmb_pw_rst,0);
	
	mdelay(200);
	gpio_direction_output(cmmb_io_ctrl->cmmb_pw_dwn,0);
	
	gpio_direction_output(cmmb_io_ctrl->cmmb_pw_en,0);
//	gpio_set_value(CMMB_1186_POWER_RESET,GPIO_LOW);
//	gpio_set_value(CMMB_1186_POWER_DOWN,GPIO_LOW);
//	gpio_set_value(CMMB_1186_POWER_ENABLE,GPIO_LOW);
	//mdelay(00);
//set the CS0 as gpio mode 

//	rk2818_mux_api_set(GPIOB4_SPI0CS0_MMC0D4_NAME,0);
//	gpio_direction_output(GPIOB4_SPI0CS0_MMC0D4_NAME,0);

	printk("cmmb chip_powerdown !!!!\n");
}

#endif
//for test
	//chip_poweron();
}

int smsspibus_ssp_resume(void* context) 
{
    int ret;
    struct spiphy_dev_s *spiphy_dev ;
    u32 mode = 0, flags = 0, psp_flags = 0, speed = 0;
    printk("entering smsspibus_ssp_resume\n");

    if(!context){
        PERROR("smsspibus_ssp_resume context NULL \n");
        return -1;
    }
    spiphy_dev = (struct spiphy_dev_s *) context;
    chip_poweron();
    //free_irq(gpio_to_irq(CMMB_1186_SPIIRQ), spiphy_dev);
    //printk("siano 1186 request irq\n");
    //gpio_pull_updown(CMMB_1186_SPIIRQ,GPIOPullDown);
    //ret = request_gpio_irq(CMMB_1186_SPIIRQ, (pFunc)spibus_interrupt, GPIOEdgelRising, spiphy_dev);       
    //request_irq(gpio_to_irq(CMMB_1186_SPIIRQ),spibus_interrupt,IRQF_TRIGGER_RISING,NULL,spiphy_dev);
    if(ret<0){
        printk("siano1186 request irq failed !!\n");
        ret = -EBUSY;
        goto fail1;
    }
    return 0 ;
fail1:
	  free_irq(gpio_to_irq(cmmb_io_ctrl->cmmb_irq), NULL);
    return -1 ;
}

//zyc
static void request_cmmb_gpio()
{
#if 0
	int ret;
	ret = gpio_request(CMMB_1186_POWER_RESET, NULL);
	if (ret) {
	printk("%s:failed to request CMMB_1186_POWER_RESET\n",__FUNCTION__);
	//return ret;
	}

	ret = gpio_request(CMMB_1186_POWER_DOWN, NULL);
	if (ret) {
	printk("%s:failed to request CMMB_1186_POWER_DOWN\n",__FUNCTION__);
	//return ret;
	}
	

	ret = gpio_request(CMMB_1186_POWER_ENABLE, NULL);
	if (ret) {
	printk("%s:failed to request CMMB_1186_POWER_ENABLE\n",__FUNCTION__);
	//return ret;
	}

	rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, 0);
   	ret = gpio_request(CMMB_1186_SPIIRQ,"cmmb irq");
	if (ret) {
		//dev_err(&pdev->dev, "failed to request play key gpio\n");
		//goto free_gpio;
		printk("gpio request error\n");
	}

    //ret = request_gpio_irq(CMMB_1186_SPIIRQ, spibus_interrupt, GPIOEdgelRising, spiphy_dev);//
    gpio_pull_updown(CMMB_1186_SPIIRQ,GPIOPullUp);
	printk("leave the request_cmmb_gpio\n");
	#endif
#if 1
		int ret;
	
	if(cmmb_io_ctrl)
		{
			if(cmmb_io_ctrl->io_init_mux)
			cmmb_io_ctrl->io_init_mux();
			else
				printk("cmmb_io_ctrl->io_init_mux is null !!!!!!!\n");
			ret = gpio_request(cmmb_io_ctrl->cmmb_pw_rst, NULL);
			if (ret) {
			printk("%s:failed to request CMMB_1186_POWER_RESET\n",__FUNCTION__);
			//return ret;
			}

			ret = gpio_request(cmmb_io_ctrl->cmmb_pw_dwn, NULL);
			if (ret) {
			printk("%s:failed to request CMMB_1186_POWER_DOWN\n",__FUNCTION__);
			//return ret;
			}
			

			ret = gpio_request(cmmb_io_ctrl->cmmb_pw_en, NULL);
			if (ret) {
			printk("%s:failed to request CMMB_1186_POWER_ENABLE\n",__FUNCTION__);
			//return ret;
			}

		//	rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, 0);
		   	ret = gpio_request(cmmb_io_ctrl->cmmb_irq,"cmmb irq");
			if (ret) {
				//dev_err(&pdev->dev, "failed to request play key gpio\n");
				//goto free_gpio;
				printk("gpio request error\n");
			}

		    //ret = request_gpio_irq(CMMB_1186_SPIIRQ, spibus_interrupt, GPIOEdgelRising, spiphy_dev);//
		    gpio_pull_updown(cmmb_io_ctrl->cmmb_irq,GPIOPullUp);
			printk("leave the request_cmmb_gpio\n");
		}
#endif
}

static void release_cmmb_gpio()
{
#if 1
	if(cmmb_io_ctrl)
		{
		gpio_free(cmmb_io_ctrl->cmmb_pw_rst);
		gpio_free(cmmb_io_ctrl->cmmb_pw_dwn);
		gpio_free(cmmb_io_ctrl->cmmb_pw_en);
		gpio_free(cmmb_io_ctrl->cmmb_irq);
		cmmb_io_ctrl = NULL;
		printk("leave the release_cmmb_gpio\n");
		}
#endif

}

void *smsspiphy_init(void *context, void (*smsspi_interruptHandler) (void *),
		     void *intr_context)
{
	int ret;
	struct spiphy_dev_s *spiphy_dev;
	u32 mode = 0, flags = 0, psp_flags = 0, speed = 0;
	int error;
	cmmb_io_ctrl = ((struct spi_device*)context)->dev.platform_data;

    sms_debug("smsspiphy_init\n");
    
	spiphy_dev = kmalloc(sizeof(struct spiphy_dev_s), GFP_KERNEL);
    if(!spiphy_dev )
    {
		sms_err("spiphy_dev is null in smsspiphy_init\n") ;
        return NULL;
	}
	
	request_cmmb_gpio();
	
	chip_powerdown();
	spiphy_dev->interruptHandler = smsspi_interruptHandler;
	spiphy_dev->intr_context = intr_context;
  spiphy_dev->Smsdevice = (struct spi_device*)context;
    
    //gpio_pull_updown(CMMB_1186_SPIIRQ, IRQT_FALLING);
    //设置CMMB 中断脚IOMUX	
 //申请GPIO放到   request_cmmb_gpio
 #if 0
	rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, 0);
   	error = gpio_request(CMMB_1186_SPIIRQ,"cmmb irq");
	if (error) {
		//dev_err(&pdev->dev, "failed to request play key gpio\n");
		//goto free_gpio;
		printk("gpio request error\n");
	}

    //ret = request_gpio_irq(CMMB_1186_SPIIRQ, spibus_interrupt, GPIOEdgelRising, spiphy_dev);//
    gpio_pull_updown(CMMB_1186_SPIIRQ,GPIOPullUp);
#endif

    //ret = request_gpio_irq(CMMB_-rwxrwxrwx 1 root root     8 2010-09-20 17:43 built-in.o
    //-rwxrwxrwx 1 root root  6927 2010-09-19 10:42 compat.h
    //-rwxrwxrwx 1 root root  1748 2010-09-21 15:06 Kconfig
    //-rwxrwxrwx 1 root root  2518 2010-09-19 10:42 Makefile
    //-rwxrwxrwx 1 root root    37 2010-09-21 20:27 modules.order
    //-rwxrwxrwx 1 root root  9890 2010-09-19 10:42 sms-cards.c
    //-rwxrwxrwx 1 root root  2752 2010-09-19 10:42 sms-cards.h
    //-rwxrwxrwx 1 root root  5416 2010-09-21 19:47 sms-cards.o
    //-rwxrwxrwx 1 root root 20493 2010-09-21 19:46 smschar.c
    //-rwxrwxrwx 1 root root  1916 2010-09-19 10:42 smscharioctl.h
    //-rwxrwxrwx 1 root root 12440 2010-09-21 19:47 smschar.o
    //-rwxrwxrwx 1 root root 53173 2010-09-21 19:46 smscoreapi.c
    //-rwxrwxrwx 1 root root 16701 2010-09-21 19:46 smscoreapi.h
    //-rwxrwxrwx 1 root root 25516 2010-09-21 19:47 smscoreapi.o
    //-rwxrwxrwx 1 root root  1982 2010-09-19 10:42 smsdbg_prn.h
    //-rwxrwxrwx 1 root root  2409 2010-09-19 10:42 smsendian.c
    //-rwxrwxrwx 1 root root  1100 2010-09-19 10:42 smsendian.h
    //-rwxrwxrwx 1 root root  1140 2010-09-21 19:47 smsendian.o
    //-rwxrwxrwx 1 root root 58990 2010-09-21 19:48 smsmdtv.ko
    //-rwxrwxrwx 1 root root  1578 2010-09-19 16:15 smsmdtv.mod.c
    //-rwxrwxrwx 1 root root  2984 2010-09-20 17:43 smsmdtv.mod.o
    //-rwxrwxrwx 1 root root 56673 2010-09-21 19:47 smsmdtv.o
    //-rwxrwxrwx 1 root root 11950 2010-09-21 19:46 smsspicommon.c
    //-rwxrwxrwx 1 root root  2496 2010-09-19 10:42 smsspicommon.h
    //-rwxrwxrwx 1 root root  3800 2010-09-21 19:47 smsspicommon.o
    //-rwxrwxrwx 1 root root 23441 2010-09-21 19:46 smsspilog.c
    //-rwxrwxrwx 1 root root 12260 2010-09-21 19:47 smsspilog.o
    //-rwxrwxrwx 1 root root  1512 2010-09-19 10:42 smsspiphy.h
    //-rwxrwxrwx 1 root root 20394 2010-09-17 11:22 smsspiphy_pxa.c
    //-rwxrwxrwx 1 root root 11895 2010-09-21 19:46 smsspiphy_rk.c
    //-rwxrwxrwx 1 root root  5480 2010-09-21 19:47 smsspiphy_rk.o
    //root@zyc-desktop:/usr/android_source/android_cmmb_dev/kernel/kernel/drivers/cmmb/siano# 
    //
    //1186_SPIIRQ, (pFunc)spibus_interrupt, GPIOEdgelRising, spiphy_dev);       

    request_irq(gpio_to_irq(cmmb_io_ctrl->cmmb_irq),spibus_interrupt,IRQF_TRIGGER_RISING,"inno_irq",spiphy_dev);


    if(ret<0){
        printk("siano 1186 request irq failed !!\n");
        ret = -EBUSY;
        goto fail1;
    }
    
    atomic_notifier_chain_register(&panic_notifier_list,&sms_panic_notifier);
    //panic_sspdev =  &(spiphy_dev->sspdev) ;
        
	PDEBUG("exiting\n");
    
	return spiphy_dev;
    
fail1:
	free_irq(gpio_to_irq(cmmb_io_ctrl->cmmb_irq), spiphy_dev);
	return 0;
}

int smsspiphy_deinit(void *context)
{
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
	PDEBUG("entering\n");

	printk("entering smsspiphy_deinit\n");

        panic_sspdev = NULL;
        atomic_notifier_chain_unregister(&panic_notifier_list,
						 &sms_panic_notifier);
        chip_powerdown();
	sms_info("exiting\n");
	free_irq(gpio_to_irq(cmmb_io_ctrl->cmmb_irq), spiphy_dev);
//	gpio_free(CMMB_1186_SPIIRQ);
	release_cmmb_gpio();

	return 0;
}

void smsspiphy_set_config(struct spiphy_dev_s *spiphy_dev, int clock_divider)
{
	;
}

void prepareForFWDnl(void *context)
{
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
	smsspiphy_set_config(spiphy_dev, 3);
	msleep(100);
}

void fwDnlComplete(void *context, int App)
{
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
	smsspiphy_set_config(spiphy_dev, 1);
	msleep(100);
}

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
#include <asm/hardware.h>

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
#include <asm/arch/gpio.h>
#include "smscoreapi.h"


#define CMMB_1186_SPIIRQ GPIOPortE_Pin1//This Pin is SDK Board GPIOPortE_Pin1 


#define SSP_PORT 1
#define SSP_CKEN CKEN_SSP1
#define SMS_IRQ_GPIO MFP_PIN_GPIO5

#if (SSP_PORT == 1)
#define SDCMR_RX DRCMRRXSSDR
#define SDCMR_TX DRCMRTXSSDR
#else
#if (SSP_PORT == 2)
#define SDCMR_RX DRCMR15
#define SDCMR_TX DRCMR16
#else
#if (SSP_PORT == 3)
#define SDCMR_RX DRCMR66
#define SDCMR_TX DRCMR67
#else
#if (SSP_PORT == 4)
#define SDCMR_RX DRCMRRXSADR
#define SDCMR_TX DRCMRTXSADR
#endif
#endif
#endif
#endif


/* Macros defining physical layer behaviour*/
#ifdef PXA_310_LV
#define CLOCK_FACTOR 1
//#define CLOCK_FACTOR 2
#else /*PXA_310_LV */
#define CLOCK_FACTOR 2
#endif /*PXA_310_LV */

/* Macros for coding reuse */

/*! macro to align the divider to the proper offset in the register bits */
#define CLOCK_DIVIDER(i)((i-1)<<8)	/* 1-4096 */

/*! DMA related macros */
#define DMA_INT_MASK (DCSR_ENDINTR | DCSR_STARTINTR | DCSR_BUSERR)
#define RESET_DMA_CHANNEL (DCSR_NODESC | DMA_INT_MASK)

#define SSP_TIMEOUT_SCALE (769)
#define SSP_TIMEOUT(x) ((x*10000)/SSP_TIMEOUT_SCALE)

#define SPI_PACKET_SIZE 256



// in android platform 2.6.25 , need to check the Reg bit by bit later
#define GSDR(x) __REG2(0x40e00400, ((x) & 0x60) >> 3)
#define GCDR(x) __REG2(0x40300420, ((x) & 0x60) >> 3)

#define GSRER(x) __REG2(0x40e00440, ((x) & 0x60) >> 3)
#define GCRER(x) __REG2(0x40e00460, ((x) & 0x60) >> 3)


#define GPIO_DIR_IN 0

#define SSCR0_P1	__REG(0x41000000)  /* SSP Port 1 Control Register 0 */
#define SSCR1_P1	__REG(0x41000004)  /* SSP Port 1 Control Register 1 */
#define SSSR_P1		__REG(0x41000008)  /* SSP Port 1 Status Register */
#define SSITR_P1	__REG(0x4100000C)  /* SSP Port 1 Interrupt Test Register */
#define SSDR_P1		__REG(0x41000010)  /* (Write / Read) SSP Port 1 Data Write Register/SSP Data Read Register */

unsigned  long u_irq_count =0;


/**********************************************************************/
//to support dma 16byte burst size
// change SPI TS according to marvel recomendations

#define DMA_BURST_SIZE  	DCMD_BURST16      //DCMD_BURST8 
#define SPI_RX_FIFO_RFT 	SSCR1_RxTresh(4) //SSCR1_RxTresh(1) 
#define SPI_TX_FIFO_TFT		SSCR1_TxTresh(3) //SSCR1_TxTresh(1) 


/**********************************************************************/

#include <linux/notifier.h>

static unsigned int dma_rxbuf_phy_addr ;
static unsigned int dma_txbuf_phy_addr ;

static int     rx_dma_channel =0 ;
static int     tx_dma_channel =0 ;
static volatile int     dma_len = 0 ;
static volatile int     tx_len  = 0 ;

static struct ssp_dev*  panic_sspdev = NULL ;


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
       printk("last tx_len = %d\n",tx_len) ;
       printk("last DMA len = %d\n",dma_len) ;
#if 0       
       printk("rxbuf_addr=[0x%x],Rx DSADR=[0x%x] DTADR=[0x%x] DCSR=[0x%x] DCMD=[0x%x]\n",
                             dma_rxbuf_phy_addr, DSADR(rx_dma_channel),DTADR(rx_dma_channel), 
                             DCSR(rx_dma_channel),DCMD(rx_dma_channel) );

       printk("txbuf_addr=[0x%x],Tx DSADR=[0x%x] DTADR=[0x%x] DCSR[0x%x] DCMD=[0x%x]\n", 
                             dma_txbuf_phy_addr, DSADR(tx_dma_channel),DTADR(tx_dma_channel),
                             DCSR(tx_dma_channel),DCMD(tx_dma_channel) );
       if(panic_sspdev)
       {
           printk("SSCR0 =[0x%x]\n",__raw_readl(panic_sspdev->ssp->mmio_base + SSCR0)) ;
           printk("SSCR1 =[0x%x]\n",__raw_readl(panic_sspdev->ssp->mmio_base + SSCR1)) ;
           printk("SSTO  =[0x%x]\n",__raw_readl(panic_sspdev->ssp->mmio_base + SSTO)) ;
           printk("SSPSP =[0x%x]\n",__raw_readl(panic_sspdev->ssp->mmio_base + SSPSP)) ;
           printk("SSSR  =[0x%x]\n",__raw_readl(panic_sspdev->ssp->mmio_base + SSSR)) ;
       }
#endif

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
    unsigned  long flags;
 	int gpio = mfp_to_gpio(gpio_id);

//	if (gpio >= GPIO_EXP_START)
//	{
//		return 0;
//	}
//	spin_lock_irqsave(&gpio_spin_lock, flags);
	local_irq_save(flags);
        
        if ( dir == GPIO_DIR_IN)
		GCRER(gpio) =1u << (gpio& 0x1f);
	else
		GSRER(gpio) =1u << (gpio& 0x1f);
 	local_irq_restore(flags);
#endif
}

void pxa3xx_gpio_set_direction(int gpio_id , int dir)
{
#if 0
    unsigned long flags;
	int gpio = mfp_to_gpio(gpio_id);

	local_irq_save(flags);
        
        if ( dir == GPIO_DIR_IN)
		GCDR(gpio) =1u << (gpio& 0x1f);
	else
		GSDR(gpio) =1u << (gpio& 0x1f);
 	local_irq_restore(flags);
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
	unsigned long phyaddr;
	/* map dma buffers */
	if (!buf) {
		PERROR(" NULL buffers to map\n");
		return 0;
	}
	/* map buffer */
	phyaddr = dma_map_single(spiphy_dev->dev, buf, len, direction);
	if (dma_mapping_error(phyaddr)) {
		PERROR("exiting  with error\n");
		return 0;
	}
	return phyaddr;
}

static irqreturn_t spibus_interrupt(int irq, void *context)
{
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
    
	u_irq_count ++;
    
	PDEBUG("INT counter = %d\n", u_irq_count);

    sms_debug("spibus_interrupt\n");
    
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
    struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
	u32 irq_status = DCSR(channel) & DMA_INT_MASK;

//	printk( "recieved interrupt from dma channel %d irq status %x.\n",
//	       channel, irq_status);
	if (irq_status & DCSR_BUSERR) {
		printk(KERN_EMERG "bus error!!! resetting channel %d\n", channel);

		DCSR(spiphy_dev->rx_dma_channel) = RESET_DMA_CHANNEL;
		DCSR(spiphy_dev->tx_dma_channel) = RESET_DMA_CHANNEL;
	}
	DCSR(spiphy_dev->rx_dma_channel) = RESET_DMA_CHANNEL;
	complete(&spiphy_dev->transfer_in_process);
#endif   
}

void smsspibus_xfer(void *context, unsigned char *txbuf,
		    unsigned long txbuf_phy_addr, unsigned char *rxbuf,
		    unsigned long rxbuf_phy_addr, int len)
{
    struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
    unsigned char *temp = NULL;
    int ret;

    //printk(KERN_INFO "smsspibus_xfer \n");
    //printk(KERN_INFO "smsspibus_xfer txbuf = 0x%x\n",txbuf);
    //printk(KERN_INFO "smsspibus_xfer rxbuf = 0x%x\n",rxbuf);
    //printk(KERN_INFO "smsspibus_xfer len=0x%x\n",len);

    //while (1)
    //{
    //mdelay(100);    
#if 1
    if (txbuf)
    {
       // sms_debug("spi write buf[4] = 0x%x buf[5] = 0x%x\n", txbuf[4],txbuf[5]);
        ret = spi_write(spiphy_dev->Smsdevice, txbuf, len);
    }
   
#if 0    
    else
    {
        temp = kmalloc(len,GFP_KERNEL);
        if (temp == NULL)
        {
            sms_err("sms spi write temp malloc fail");
            return -ENOMEM;
        }
        memset(temp,0xFF,len);
        sms_debug("sms spi write temp buf");
        ret = spi_write(spiphy_dev->Smsdevice, temp, len);
    }
#endif    
//    if (ret)
 //       sms_err(KERN_INFO "smsspibus_xfer spi write ret=0x%x\n",ret);

//      memset (rxbuf, 0xff, 256);

        
    if ((rxbuf)&&(len != 16))
        ret = spi_read(spiphy_dev->Smsdevice, rxbuf, len);
    
    //sms_debug("sms spi read buf=0x%x\n",rxbuf[0]);
    //sms_debug("sms spi read buf=0x%x\n",rxbuf[1]);
    //sms_debug("sms spi read buf=0x%x\n",rxbuf[2]);
    //sms_debug("sms spi read buf=0x%x\n",rxbuf[3]);
//        printk(KERN_INFO "after sms spi read buf[0]=0x%x\n",rxbuf[0]);
       
  //      printk(KERN_INFO "sms spi read buf[8]=0x%x\n",rxbuf[8]);
    //    printk(KERN_INFO "sms spi read buf[9]=0x%x\n",rxbuf[9]);
    //}
#else
    spi_write_then_read(spiphy_dev->Smsdevice,txbuf,len,rxbuf,len);
#endif
    //spi_write_then_read(struct spi_device *spi, const u8 *txbuf, unsigned n_tx, u8 *rxbuf, unsigned n_rx);
#if 0   //hzb 0524
    /* DMA burst is 8 bytes, therefore we need tmp buffer that size. */
	// to support dma 16byte burst size
	unsigned long tmp[4];
	//unsigned long tmp[2];

	unsigned long txdma;
	struct spiphy_dev_s *spiphy_dev = (struct spiphy_dev_s *) context;
        unsigned long expire;
	int res =0;

        expire =  msecs_to_jiffies(200) ;
	/* program the controller */
	if (txbuf)
	{
//		PDEBUG("tx \n");
		invert_endianness(txbuf, len);
	}
//	else
//		PDEBUG("rx \n");
	tmp[0] = -1;
	tmp[1] = -1;
	tmp[2] = -1;
	tmp[3] = -1;

	/* map RX buffer */

	if (!txbuf)
        {
		txdma =
		    dma_map_buf(spiphy_dev, (char *)tmp, sizeof(tmp),
				DMA_TO_DEVICE);
                tx_len = 0 ;
        }
	else
        {
		txdma = txbuf_phy_addr;
                tx_len = len ;
        }

	init_completion(&spiphy_dev->transfer_in_process);
	/* configure DMA Controller */
	DCSR(spiphy_dev->rx_dma_channel) = RESET_DMA_CHANNEL;
	DSADR(spiphy_dev->rx_dma_channel) = __PREG(SSDR_P(SSP_PORT));
	DTADR(spiphy_dev->rx_dma_channel) = rxbuf_phy_addr;


        // to support dma 16byte burst size
	DCMD(spiphy_dev->rx_dma_channel) = DCMD_INCTRGADDR | DCMD_FLOWSRC
	    | DCMD_WIDTH4 | DCMD_ENDIRQEN | DMA_BURST_SIZE | len;



        rx_dma_channel = spiphy_dev->rx_dma_channel ;
        dma_rxbuf_phy_addr = (unsigned int) rxbuf_phy_addr ;
        dma_len = len ;
//	PDEBUG("rx channel=%d, src=0x%x, dst=0x%x, cmd=0x%x\n",
//	       spiphy_dev->rx_dma_channel, __PREG(SSDR_P(SSP_PORT)),
//	       (unsigned int)rxbuf_phy_addr, DCMD(spiphy_dev->rx_dma_channel));
	spiphy_dev->rx_buf_len = len;

	DCSR(spiphy_dev->tx_dma_channel) = RESET_DMA_CHANNEL;
	DTADR(spiphy_dev->tx_dma_channel) = __PREG(SSDR_P(SSP_PORT));
       
      
	DSADR(spiphy_dev->tx_dma_channel) = txdma;
       
        tx_dma_channel = spiphy_dev->tx_dma_channel;
        dma_txbuf_phy_addr = (unsigned int) txdma ;

	if (txbuf) {
		DCMD(spiphy_dev->tx_dma_channel) =
		    DCMD_INCSRCADDR | DCMD_FLOWTRG | DCMD_WIDTH4
		    /* | DCMD_ENDIRQEN */  | DMA_BURST_SIZE  | len;
		spiphy_dev->tx_buf_len = len;
	} else {
		DCMD(spiphy_dev->tx_dma_channel) = DCMD_FLOWTRG
		    | DCMD_WIDTH4 /* | DCMD_ENDIRQEN */  |DMA_BURST_SIZE  | len;
		spiphy_dev->tx_buf_len = 4;
	}

#if 0
       printk("Tx DSADR=[0x%x],DTADR=[0x%x],DCMD=[0x%x],len =[0x%x]\n",
                DSADR(spiphy_dev->tx_dma_channel),DTADR(spiphy_dev->tx_dma_channel),DCMD(spiphy_dev->tx_dma_channel),len) ;
#endif 
//	PDEBUG("tx channel=%d, src=0x%x, dst=0x%x, cmd=0x%x\n",
//	       spiphy_dev->tx_dma_channel, (unsigned int)txdma,
//	       __PREG(SSDR_P(SSP_PORT)), DCMD(spiphy_dev->tx_dma_channel));
	/* DALGN - DMA ALIGNMENT REG. */
	if (rxbuf_phy_addr & 0x7)
		DALGN |= (1 << spiphy_dev->rx_dma_channel);
	else
		DALGN &= ~(1 << spiphy_dev->rx_dma_channel);
	if (txdma & 0x7)
		DALGN |= (1 << spiphy_dev->tx_dma_channel);
	else
		DALGN &= ~(1 << spiphy_dev->tx_dma_channel);

	/* Start DMA controller */
 //printk( "smsmdtv: Start DMA controller\n");   
	DCSR(spiphy_dev->rx_dma_channel) |= DCSR_RUN;
	DCSR(spiphy_dev->tx_dma_channel) |= DCSR_RUN;
//	printk(  "DMA running. wait for completion.\n");

//printk( "smsmdtv: before wait_for_completion_timeout\n");
	res = wait_for_completion_timeout(&spiphy_dev->transfer_in_process,expire);       
 	if(!res)
	{
             printk( "smsmdtv DMA timeout, res=0x%x len =%d\n", res, len);
             printk( "smsmdtv DMA reg, 0x%x %x \n",DCSR(spiphy_dev->rx_dma_channel), DCSR(spiphy_dev->tx_dma_channel)  );
             DCSR(spiphy_dev->rx_dma_channel) = RESET_DMA_CHANNEL;
	     DCSR(spiphy_dev->tx_dma_channel) = RESET_DMA_CHANNEL;
	     
             complete(&spiphy_dev->transfer_in_process);
        }
//	else
	{
		//printk( "DMA complete.\n");
		invert_endianness(rxbuf, len);

		if (!txbuf)
			PDEBUG("rx[4]:0x%x;[6]:0x%x \n", rxbuf[4], rxbuf[6]);
	}
//printk( "smsmdtv: Xfer end\n");
#endif //hzb 0524
}

void smschipreset(void *context)
{

}

static struct ssp_state  sms_ssp_state ;

void smsspibus_ssp_suspend(void* context )
{
    struct spiphy_dev_s *spiphy_dev ;
    printk("entering smsspibus_ssp_suspend\n");
    if(!context)
    {
        PERROR("smsspibus_ssp_suspend context NULL \n") ;
        return ;
    }
    spiphy_dev = (struct spiphy_dev_s *) context;

    //ssp_flush(&(spiphy_dev->sspdev)) ;
    //ssp_save_state(&(spiphy_dev->sspdev) , &sms_ssp_state) ;
    //ssp_disable(&(spiphy_dev->sspdev));
    //ssp_exit(&spiphy_dev->sspdev);
    free_irq(CMMB_1186_SPIIRQ, spiphy_dev);

    	/*  release DMA resources */
    //if (spiphy_dev->rx_dma_channel >= 0)
 	//pxa_free_dma(spiphy_dev->rx_dma_channel);

    //if (spiphy_dev->tx_dma_channel >= 0)
	//pxa_free_dma(spiphy_dev->tx_dma_channel);
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
#endif
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
#endif
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
    //ret = ssp_init(&spiphy_dev->sspdev, SSP_PORT, 0);
    //if (ret) {
        //PERROR("ssp_init failed. error %d", ret);
    //}
    //GPIOPullUpDown(CMMB_1186_SPIIRQ, IRQT_FALLING);
    //err = request_gpio_irq(PT2046_PENIRQ,xpt2046_ts_interrupt,GPIOEdgelFalling,ts_dev);   
    //ret = request_gpio_irq(CMMB_1186_SPIIRQ, (pFunc)spibus_interrupt, GPIOEdgelRising, spiphy_dev);       
    if(ret<0){
        printk("siano1186 request irq failed !!\n");
        ret = -EBUSY;
        goto fail1;
    }
    return 0 ;
fail1:
	free_irq(CMMB_1186_SPIIRQ,NULL);
    return -1 ;
}


void *smsspiphy_init(void *context, void (*smsspi_interruptHandler) (void *),
		     void *intr_context)
{
	int ret;
	struct spiphy_dev_s *spiphy_dev;
	u32 mode = 0, flags = 0, psp_flags = 0, speed = 0;

    sms_debug("smsspiphy_init\n");
    
	spiphy_dev = kmalloc(sizeof(struct spiphy_dev_s), GFP_KERNEL);
    if(!spiphy_dev )
    {
        printk("spiphy_dev is null in smsspiphy_init\n") ;
        return NULL;
	}
	chip_powerdown();
	spiphy_dev->interruptHandler = smsspi_interruptHandler;
	spiphy_dev->intr_context = intr_context;
    spiphy_dev->Smsdevice = (struct spi_device*)context;
    
    GPIOPullUpDown(CMMB_1186_SPIIRQ, IRQT_FALLING);

    ret = request_gpio_irq(CMMB_1186_SPIIRQ, spibus_interrupt, GPIOEdgelRising, spiphy_dev);//
    
    if(ret<0){
        printk("siano 1186 request irq failed !!\n");
        ret = -EBUSY;
        goto fail1;
    }
    
    atomic_notifier_chain_register(&panic_notifier_list,&sms_panic_notifier);
    //panic_sspdev =  &(spiphy_dev->sspdev) ;
        
	PDEBUG("exiting\n");
    
	return spiphy_dev;
    
error_irq:
	//if (spiphy_dev->tx_dma_channel >= 0)
		//pxa_free_dma(spiphy_dev->tx_dma_channel);

error_txdma:
	//if (spiphy_dev->rx_dma_channel >= 0)
		//pxa_free_dma(spiphy_dev->rx_dma_channel);

error_rxdma:
//	ssp_exit(&spiphy_dev->sspdev);
error_sspinit:
	//PDEBUG("exiting on error\n");
	printk("exiting on error\n");
fail1:
    free_irq(CMMB_1186_SPIIRQ,NULL);
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
	PDEBUG("exiting\n");
	return 0;
}

void smsspiphy_set_config(struct spiphy_dev_s *spiphy_dev, int clock_divider)
{
	//u32 mode, flags, speed, psp_flags = 0;
	//ssp_disable(&spiphy_dev->sspdev);
	/* clock divisor for this mode. */
	//speed = CLOCK_DIVIDER(clock_divider);
	/* 32bit words in the fifo */
	//mode = SSCR0_Motorola | SSCR0_DataSize(16) | SSCR0_EDSS;
	//flags = SPI_RX_FIFO_RFT |SPI_TX_FIFO_TFT | SSCR1_TSRE |
		 //SSCR1_RSRE | SSCR1_RIE | SSCR1_TRAIL;	/* | SSCR1_TIE */
	//ssp_config(&spiphy_dev->sspdev, mode, flags, psp_flags, speed);
	//ssp_enable(&spiphy_dev->sspdev);
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

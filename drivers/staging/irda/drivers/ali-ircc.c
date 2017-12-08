/*********************************************************************
 *                
 * Filename:      ali-ircc.h
 * Version:       0.5
 * Description:   Driver for the ALI M1535D and M1543C FIR Controller
 * Status:        Experimental.
 * Author:        Benjamin Kong <benjamin_kong@ali.com.tw>
 * Created at:    2000/10/16 03:46PM
 * Modified at:   2001/1/3 02:55PM
 * Modified by:   Benjamin Kong <benjamin_kong@ali.com.tw>
 * Modified at:   2003/11/6 and support for ALi south-bridge chipsets M1563
 * Modified by:   Clear Zhang <clear_zhang@ali.com.tw>
 * 
 *     Copyright (c) 2000 Benjamin Kong <benjamin_kong@ali.com.tw>
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 ********************************************************************/

#include <linux/module.h>
#include <linux/gfp.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/rtnetlink.h>
#include <linux/serial_reg.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include "ali-ircc.h"

#define CHIP_IO_EXTENT 8
#define BROKEN_DONGLE_ID

#define ALI_IRCC_DRIVER_NAME "ali-ircc"

/* Power Management */
static int ali_ircc_suspend(struct platform_device *dev, pm_message_t state);
static int ali_ircc_resume(struct platform_device *dev);

static struct platform_driver ali_ircc_driver = {
	.suspend	= ali_ircc_suspend,
	.resume		= ali_ircc_resume,
	.driver		= {
		.name	= ALI_IRCC_DRIVER_NAME,
	},
};

/* Module parameters */
static int qos_mtt_bits = 0x07;  /* 1 ms or more */

/* Use BIOS settions by default, but user may supply module parameters */
static unsigned int io[]  = { ~0, ~0, ~0, ~0 };
static unsigned int irq[] = { 0, 0, 0, 0 };
static unsigned int dma[] = { 0, 0, 0, 0 };

static int  ali_ircc_probe_53(ali_chip_t *chip, chipio_t *info);
static int  ali_ircc_init_43(ali_chip_t *chip, chipio_t *info);
static int  ali_ircc_init_53(ali_chip_t *chip, chipio_t *info);

/* These are the currently known ALi south-bridge chipsets, the only one difference
 * is that M1543C doesn't support HP HDSL-3600
 */
static ali_chip_t chips[] =
{
	{ "M1543", { 0x3f0, 0x370 }, 0x51, 0x23, 0x20, 0x43, ali_ircc_probe_53, ali_ircc_init_43 },
	{ "M1535", { 0x3f0, 0x370 }, 0x51, 0x23, 0x20, 0x53, ali_ircc_probe_53, ali_ircc_init_53 },
	{ "M1563", { 0x3f0, 0x370 }, 0x51, 0x23, 0x20, 0x63, ali_ircc_probe_53, ali_ircc_init_53 },
	{ NULL }
};

/* Max 4 instances for now */
static struct ali_ircc_cb *dev_self[] = { NULL, NULL, NULL, NULL };

/* Dongle Types */
static char *dongle_types[] = {
	"TFDS6000",
	"HP HSDL-3600",
	"HP HSDL-1100",	
	"No dongle connected",
};

/* Some prototypes */
static int  ali_ircc_open(int i, chipio_t *info);

static int  ali_ircc_close(struct ali_ircc_cb *self);

static int  ali_ircc_setup(chipio_t *info);
static int  ali_ircc_is_receiving(struct ali_ircc_cb *self);
static int  ali_ircc_net_open(struct net_device *dev);
static int  ali_ircc_net_close(struct net_device *dev);
static int  ali_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void ali_ircc_change_speed(struct ali_ircc_cb *self, __u32 baud);

/* SIR function */
static netdev_tx_t ali_ircc_sir_hard_xmit(struct sk_buff *skb,
						struct net_device *dev);
static irqreturn_t ali_ircc_sir_interrupt(struct ali_ircc_cb *self);
static void ali_ircc_sir_receive(struct ali_ircc_cb *self);
static void ali_ircc_sir_write_wakeup(struct ali_ircc_cb *self);
static int  ali_ircc_sir_write(int iobase, int fifo_size, __u8 *buf, int len);
static void ali_ircc_sir_change_speed(struct ali_ircc_cb *priv, __u32 speed);

/* FIR function */
static netdev_tx_t  ali_ircc_fir_hard_xmit(struct sk_buff *skb,
						 struct net_device *dev);
static void ali_ircc_fir_change_speed(struct ali_ircc_cb *priv, __u32 speed);
static irqreturn_t ali_ircc_fir_interrupt(struct ali_ircc_cb *self);
static int  ali_ircc_dma_receive(struct ali_ircc_cb *self); 
static int  ali_ircc_dma_receive_complete(struct ali_ircc_cb *self);
static int  ali_ircc_dma_xmit_complete(struct ali_ircc_cb *self);
static void ali_ircc_dma_xmit(struct ali_ircc_cb *self);

/* My Function */
static int  ali_ircc_read_dongle_id (int i, chipio_t *info);
static void ali_ircc_change_dongle_speed(struct ali_ircc_cb *priv, int speed);

/* ALi chip function */
static void SIR2FIR(int iobase);
static void FIR2SIR(int iobase);
static void SetCOMInterrupts(struct ali_ircc_cb *self , unsigned char enable);

/*
 * Function ali_ircc_init ()
 *
 *    Initialize chip. Find out whay kinds of chips we are dealing with
 *    and their configuration registers address
 */
static int __init ali_ircc_init(void)
{
	ali_chip_t *chip;
	chipio_t info;
	int ret;
	int cfg, cfg_base;
	int reg, revision;
	int i = 0;
	
	ret = platform_driver_register(&ali_ircc_driver);
        if (ret) {
		net_err_ratelimited("%s, Can't register driver!\n",
				    ALI_IRCC_DRIVER_NAME);
                return ret;
        }

	ret = -ENODEV;
	
	/* Probe for all the ALi chipsets we know about */
	for (chip= chips; chip->name; chip++, i++) 
	{
		pr_debug("%s(), Probing for %s ...\n", __func__, chip->name);
				
		/* Try all config registers for this chip */
		for (cfg=0; cfg<2; cfg++)
		{
			cfg_base = chip->cfg[cfg];
			if (!cfg_base)
				continue;
				
			memset(&info, 0, sizeof(chipio_t));
			info.cfg_base = cfg_base;
			info.fir_base = io[i];
			info.dma = dma[i];
			info.irq = irq[i];
			
			
			/* Enter Configuration */
			outb(chip->entr1, cfg_base);
			outb(chip->entr2, cfg_base);
			
			/* Select Logical Device 5 Registers (UART2) */
			outb(0x07, cfg_base);
			outb(0x05, cfg_base+1);
			
			/* Read Chip Identification Register */
			outb(chip->cid_index, cfg_base);	
			reg = inb(cfg_base+1);	
				
			if (reg == chip->cid_value)
			{
				pr_debug("%s(), Chip found at 0x%03x\n",
					 __func__, cfg_base);
					   
				outb(0x1F, cfg_base);
				revision = inb(cfg_base+1);
				pr_debug("%s(), Found %s chip, revision=%d\n",
					 __func__, chip->name, revision);
				
				/* 
				 * If the user supplies the base address, then
				 * we init the chip, if not we probe the values
				 * set by the BIOS
				 */				
				if (io[i] < 2000)
				{
					chip->init(chip, &info);
				}
				else
				{
					chip->probe(chip, &info);	
				}
				
				if (ali_ircc_open(i, &info) == 0)
					ret = 0;
				i++;				
			}
			else
			{
				pr_debug("%s(), No %s chip at 0x%03x\n",
					 __func__, chip->name, cfg_base);
			}
			/* Exit configuration */
			outb(0xbb, cfg_base);
		}
	}		
		
	if (ret)
		platform_driver_unregister(&ali_ircc_driver);

	return ret;
}

/*
 * Function ali_ircc_cleanup ()
 *
 *    Close all configured chips
 *
 */
static void __exit ali_ircc_cleanup(void)
{
	int i;

	for (i=0; i < ARRAY_SIZE(dev_self); i++) {
		if (dev_self[i])
			ali_ircc_close(dev_self[i]);
	}
	
	platform_driver_unregister(&ali_ircc_driver);

}

static const struct net_device_ops ali_ircc_sir_ops = {
	.ndo_open       = ali_ircc_net_open,
	.ndo_stop       = ali_ircc_net_close,
	.ndo_start_xmit = ali_ircc_sir_hard_xmit,
	.ndo_do_ioctl   = ali_ircc_net_ioctl,
};

static const struct net_device_ops ali_ircc_fir_ops = {
	.ndo_open       = ali_ircc_net_open,
	.ndo_stop       = ali_ircc_net_close,
	.ndo_start_xmit = ali_ircc_fir_hard_xmit,
	.ndo_do_ioctl   = ali_ircc_net_ioctl,
};

/*
 * Function ali_ircc_open (int i, chipio_t *inf)
 *
 *    Open driver instance
 *
 */
static int ali_ircc_open(int i, chipio_t *info)
{
	struct net_device *dev;
	struct ali_ircc_cb *self;
	int dongle_id;
	int err;
			
	if (i >= ARRAY_SIZE(dev_self)) {
		net_err_ratelimited("%s(), maximum number of supported chips reached!\n",
				    __func__);
		return -ENOMEM;
	}
	
	/* Set FIR FIFO and DMA Threshold */
	if ((ali_ircc_setup(info)) == -1)
		return -1;
		
	dev = alloc_irdadev(sizeof(*self));
	if (dev == NULL) {
		net_err_ratelimited("%s(), can't allocate memory for control block!\n",
				    __func__);
		return -ENOMEM;
	}

	self = netdev_priv(dev);
	self->netdev = dev;
	spin_lock_init(&self->lock);
   
	/* Need to store self somewhere */
	dev_self[i] = self;
	self->index = i;

	/* Initialize IO */
	self->io.cfg_base  = info->cfg_base;	/* In ali_ircc_probe_53 assign 		*/
	self->io.fir_base  = info->fir_base;	/* info->sir_base = info->fir_base 	*/
	self->io.sir_base  = info->sir_base; 	/* ALi SIR and FIR use the same address */
        self->io.irq       = info->irq;
        self->io.fir_ext   = CHIP_IO_EXTENT;
        self->io.dma       = info->dma;
        self->io.fifo_size = 16;		/* SIR: 16, FIR: 32 Benjamin 2000/11/1 */
	
	/* Reserve the ioports that we need */
	if (!request_region(self->io.fir_base, self->io.fir_ext,
			    ALI_IRCC_DRIVER_NAME)) {
		net_warn_ratelimited("%s(), can't get iobase of 0x%03x\n",
				     __func__, self->io.fir_base);
		err = -ENODEV;
		goto err_out1;
	}

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	/* The only value we must override it the baudrate */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8); // benjamin 2000/11/8 05:27PM
			
	self->qos.min_turn_time.bits = qos_mtt_bits;
			
	irda_qos_bits_to_value(&self->qos);
	
	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 14384; 
	self->tx_buff.truesize = 14384;

	/* Allocate memory if needed */
	self->rx_buff.head =
		dma_zalloc_coherent(NULL, self->rx_buff.truesize,
				    &self->rx_buff_dma, GFP_KERNEL);
	if (self->rx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out2;
	}
	
	self->tx_buff.head =
		dma_zalloc_coherent(NULL, self->tx_buff.truesize,
				    &self->tx_buff_dma, GFP_KERNEL);
	if (self->tx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out3;
	}

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;
	
	/* Reset Tx queue info */
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;

	/* Override the network functions we need to use */
	dev->netdev_ops = &ali_ircc_sir_ops;

	err = register_netdev(dev);
	if (err) {
		net_err_ratelimited("%s(), register_netdev() failed!\n",
				    __func__);
		goto err_out4;
	}
	net_info_ratelimited("IrDA: Registered device %s\n", dev->name);

	/* Check dongle id */
	dongle_id = ali_ircc_read_dongle_id(i, info);
	net_info_ratelimited("%s(), %s, Found dongle: %s\n",
			     __func__, ALI_IRCC_DRIVER_NAME,
			     dongle_types[dongle_id]);
		
	self->io.dongle_id = dongle_id;

	
	return 0;

 err_out4:
	dma_free_coherent(NULL, self->tx_buff.truesize,
			  self->tx_buff.head, self->tx_buff_dma);
 err_out3:
	dma_free_coherent(NULL, self->rx_buff.truesize,
			  self->rx_buff.head, self->rx_buff_dma);
 err_out2:
	release_region(self->io.fir_base, self->io.fir_ext);
 err_out1:
	dev_self[i] = NULL;
	free_netdev(dev);
	return err;
}


/*
 * Function ali_ircc_close (self)
 *
 *    Close driver instance
 *
 */
static int __exit ali_ircc_close(struct ali_ircc_cb *self)
{
	int iobase;

	IRDA_ASSERT(self != NULL, return -1;);

        iobase = self->io.fir_base;

	/* Remove netdevice */
	unregister_netdev(self->netdev);

	/* Release the PORT that this driver is using */
	pr_debug("%s(), Releasing Region %03x\n", __func__, self->io.fir_base);
	release_region(self->io.fir_base, self->io.fir_ext);

	if (self->tx_buff.head)
		dma_free_coherent(NULL, self->tx_buff.truesize,
				  self->tx_buff.head, self->tx_buff_dma);
	
	if (self->rx_buff.head)
		dma_free_coherent(NULL, self->rx_buff.truesize,
				  self->rx_buff.head, self->rx_buff_dma);

	dev_self[self->index] = NULL;
	free_netdev(self->netdev);
	
	
	return 0;
}

/*
 * Function ali_ircc_init_43 (chip, info)
 *
 *    Initialize the ALi M1543 chip. 
 */
static int ali_ircc_init_43(ali_chip_t *chip, chipio_t *info) 
{
	/* All controller information like I/O address, DMA channel, IRQ
	 * are set by BIOS
	 */
	
	return 0;
}

/*
 * Function ali_ircc_init_53 (chip, info)
 *
 *    Initialize the ALi M1535 chip. 
 */
static int ali_ircc_init_53(ali_chip_t *chip, chipio_t *info) 
{
	/* All controller information like I/O address, DMA channel, IRQ
	 * are set by BIOS
	 */
	
	return 0;
}

/*
 * Function ali_ircc_probe_53 (chip, info)
 *    	
 *	Probes for the ALi M1535D or M1535
 */
static int ali_ircc_probe_53(ali_chip_t *chip, chipio_t *info)
{
	int cfg_base = info->cfg_base;
	int hi, low, reg;
	
	
	/* Enter Configuration */
	outb(chip->entr1, cfg_base);
	outb(chip->entr2, cfg_base);
	
	/* Select Logical Device 5 Registers (UART2) */
	outb(0x07, cfg_base);
	outb(0x05, cfg_base+1);
	
	/* Read address control register */
	outb(0x60, cfg_base);
	hi = inb(cfg_base+1);	
	outb(0x61, cfg_base);
	low = inb(cfg_base+1);
	info->fir_base = (hi<<8) + low;
	
	info->sir_base = info->fir_base;
	
	pr_debug("%s(), probing fir_base=0x%03x\n", __func__, info->fir_base);
		
	/* Read IRQ control register */
	outb(0x70, cfg_base);
	reg = inb(cfg_base+1);
	info->irq = reg & 0x0f;
	pr_debug("%s(), probing irq=%d\n", __func__, info->irq);
	
	/* Read DMA channel */
	outb(0x74, cfg_base);
	reg = inb(cfg_base+1);
	info->dma = reg & 0x07;
	
	if(info->dma == 0x04)
		net_warn_ratelimited("%s(), No DMA channel assigned !\n",
				     __func__);
	else
		pr_debug("%s(), probing dma=%d\n", __func__, info->dma);
	
	/* Read Enabled Status */
	outb(0x30, cfg_base);
	reg = inb(cfg_base+1);
	info->enabled = (reg & 0x80) && (reg & 0x01);
	pr_debug("%s(), probing enabled=%d\n", __func__, info->enabled);
	
	/* Read Power Status */
	outb(0x22, cfg_base);
	reg = inb(cfg_base+1);
	info->suspended = (reg & 0x20);
	pr_debug("%s(), probing suspended=%d\n", __func__, info->suspended);
	
	/* Exit configuration */
	outb(0xbb, cfg_base);
		
	
	return 0;	
}

/*
 * Function ali_ircc_setup (info)
 *
 *    	Set FIR FIFO and DMA Threshold
 *	Returns non-negative on success.
 *
 */
static int ali_ircc_setup(chipio_t *info)
{
	unsigned char tmp;
	int version;
	int iobase = info->fir_base;
	
	
	/* Locking comments :
	 * Most operations here need to be protected. We are called before
	 * the device instance is created in ali_ircc_open(), therefore 
	 * nobody can bother us - Jean II */

	/* Switch to FIR space */
	SIR2FIR(iobase);
	
	/* Master Reset */
	outb(0x40, iobase+FIR_MCR); // benjamin 2000/11/30 11:45AM
	
	/* Read FIR ID Version Register */
	switch_bank(iobase, BANK3);
	version = inb(iobase+FIR_ID_VR);
	
	/* Should be 0x00 in the M1535/M1535D */
	if(version != 0x00)
	{
		net_err_ratelimited("%s, Wrong chip version %02x\n",
				    ALI_IRCC_DRIVER_NAME, version);
		return -1;
	}
	
	/* Set FIR FIFO Threshold Register */
	switch_bank(iobase, BANK1);
	outb(RX_FIFO_Threshold, iobase+FIR_FIFO_TR);
	
	/* Set FIR DMA Threshold Register */
	outb(RX_DMA_Threshold, iobase+FIR_DMA_TR);
	
	/* CRC enable */
	switch_bank(iobase, BANK2);
	outb(inb(iobase+FIR_IRDA_CR) | IRDA_CR_CRC, iobase+FIR_IRDA_CR);
	
	/* NDIS driver set TX Length here BANK2 Alias 3, Alias4*/
	
	/* Switch to Bank 0 */
	switch_bank(iobase, BANK0);
	
	tmp = inb(iobase+FIR_LCR_B);
	tmp &=~0x20; // disable SIP
	tmp |= 0x80; // these two steps make RX mode
	tmp &= 0xbf;	
	outb(tmp, iobase+FIR_LCR_B);
		
	/* Disable Interrupt */
	outb(0x00, iobase+FIR_IER);
	
	
	/* Switch to SIR space */
	FIR2SIR(iobase);
	
	net_info_ratelimited("%s, driver loaded (Benjamin Kong)\n",
			     ALI_IRCC_DRIVER_NAME);
	
	/* Enable receive interrupts */ 
	// outb(UART_IER_RDI, iobase+UART_IER); //benjamin 2000/11/23 01:25PM
	// Turn on the interrupts in ali_ircc_net_open
	
	
	return 0;
}

/*
 * Function ali_ircc_read_dongle_id (int index, info)
 *
 * Try to read dongle identification. This procedure needs to be executed
 * once after power-on/reset. It also needs to be used whenever you suspect
 * that the user may have plugged/unplugged the IrDA Dongle.
 */
static int ali_ircc_read_dongle_id (int i, chipio_t *info)
{
	int dongle_id, reg;
	int cfg_base = info->cfg_base;
	
		
	/* Enter Configuration */
	outb(chips[i].entr1, cfg_base);
	outb(chips[i].entr2, cfg_base);
	
	/* Select Logical Device 5 Registers (UART2) */
	outb(0x07, cfg_base);
	outb(0x05, cfg_base+1);
	
	/* Read Dongle ID */
	outb(0xf0, cfg_base);
	reg = inb(cfg_base+1);	
	dongle_id = ((reg>>6)&0x02) | ((reg>>5)&0x01);
	pr_debug("%s(), probing dongle_id=%d, dongle_types=%s\n",
		 __func__, dongle_id, dongle_types[dongle_id]);
	
	/* Exit configuration */
	outb(0xbb, cfg_base);
			
	
	return dongle_id;
}

/*
 * Function ali_ircc_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static irqreturn_t ali_ircc_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct ali_ircc_cb *self;
	int ret;
		
		
	self = netdev_priv(dev);
	
	spin_lock(&self->lock);
	
	/* Dispatch interrupt handler for the current speed */
	if (self->io.speed > 115200)
		ret = ali_ircc_fir_interrupt(self);
	else
		ret = ali_ircc_sir_interrupt(self);
		
	spin_unlock(&self->lock);
	
	return ret;
}
/*
 * Function ali_ircc_fir_interrupt(irq, struct ali_ircc_cb *self)
 *
 *    Handle MIR/FIR interrupt
 *
 */
static irqreturn_t ali_ircc_fir_interrupt(struct ali_ircc_cb *self)
{
	__u8 eir, OldMessageCount;
	int iobase, tmp;
	
	
	iobase = self->io.fir_base;
	
	switch_bank(iobase, BANK0);	
	self->InterruptID = inb(iobase+FIR_IIR);		
	self->BusStatus = inb(iobase+FIR_BSR);	
	
	OldMessageCount = (self->LineStatus + 1) & 0x07;
	self->LineStatus = inb(iobase+FIR_LSR);	
	//self->ier = inb(iobase+FIR_IER); 		2000/12/1 04:32PM
	eir = self->InterruptID & self->ier; /* Mask out the interesting ones */ 
	
	pr_debug("%s(), self->InterruptID = %x\n", __func__, self->InterruptID);
	pr_debug("%s(), self->LineStatus = %x\n", __func__, self->LineStatus);
	pr_debug("%s(), self->ier = %x\n", __func__, self->ier);
	pr_debug("%s(), eir = %x\n", __func__, eir);
	
	/* Disable interrupts */
	 SetCOMInterrupts(self, FALSE);
	
	/* Tx or Rx Interrupt */
	
	if (eir & IIR_EOM) 
	{		
		if (self->io.direction == IO_XMIT) /* TX */
		{
			pr_debug("%s(), ******* IIR_EOM (Tx) *******\n",
				 __func__);
			
			if(ali_ircc_dma_xmit_complete(self))
			{
				if (irda_device_txqueue_empty(self->netdev)) 
				{
					/* Prepare for receive */
					ali_ircc_dma_receive(self);					
					self->ier = IER_EOM;									
				}
			}
			else
			{
				self->ier = IER_EOM; 					
			}
									
		}	
		else /* RX */
		{
			pr_debug("%s(), ******* IIR_EOM (Rx) *******\n",
				 __func__);
			
			if(OldMessageCount > ((self->LineStatus+1) & 0x07))
			{
				self->rcvFramesOverflow = TRUE;	
				pr_debug("%s(), ******* self->rcvFramesOverflow = TRUE ********\n",
					 __func__);
			}
						
			if (ali_ircc_dma_receive_complete(self))
			{
				pr_debug("%s(), ******* receive complete ********\n",
					 __func__);
				
				self->ier = IER_EOM;				
			}
			else
			{
				pr_debug("%s(), ******* Not receive complete ********\n",
					 __func__);
				
				self->ier = IER_EOM | IER_TIMER;								
			}	
		
		}		
	}
	/* Timer Interrupt */
	else if (eir & IIR_TIMER)
	{	
		if(OldMessageCount > ((self->LineStatus+1) & 0x07))
		{
			self->rcvFramesOverflow = TRUE;	
			pr_debug("%s(), ******* self->rcvFramesOverflow = TRUE *******\n",
				 __func__);
		}
		/* Disable Timer */
		switch_bank(iobase, BANK1);
		tmp = inb(iobase+FIR_CR);
		outb( tmp& ~CR_TIMER_EN, iobase+FIR_CR);
		
		/* Check if this is a Tx timer interrupt */
		if (self->io.direction == IO_XMIT)
		{
			ali_ircc_dma_xmit(self);
			
			/* Interrupt on EOM */
			self->ier = IER_EOM;
									
		}
		else /* Rx */
		{
			if(ali_ircc_dma_receive_complete(self)) 
			{
				self->ier = IER_EOM;
			}
			else
			{
				self->ier = IER_EOM | IER_TIMER;
			}	
		}		
	}
	
	/* Restore Interrupt */	
	SetCOMInterrupts(self, TRUE);	
		
	return IRQ_RETVAL(eir);
}

/*
 * Function ali_ircc_sir_interrupt (irq, self, eir)
 *
 *    Handle SIR interrupt
 *
 */
static irqreturn_t ali_ircc_sir_interrupt(struct ali_ircc_cb *self)
{
	int iobase;
	int iir, lsr;
	
	
	iobase = self->io.sir_base;

	iir = inb(iobase+UART_IIR) & UART_IIR_ID;
	if (iir) {	
		/* Clear interrupt */
		lsr = inb(iobase+UART_LSR);

		pr_debug("%s(), iir=%02x, lsr=%02x, iobase=%#x\n",
			 __func__, iir, lsr, iobase);

		switch (iir) 
		{
			case UART_IIR_RLSI:
				pr_debug("%s(), RLSI\n", __func__);
				break;
			case UART_IIR_RDI:
				/* Receive interrupt */
				ali_ircc_sir_receive(self);
				break;
			case UART_IIR_THRI:
				if (lsr & UART_LSR_THRE)
				{
					/* Transmitter ready for data */
					ali_ircc_sir_write_wakeup(self);				
				}				
				break;
			default:
				pr_debug("%s(), unhandled IIR=%#x\n",
					 __func__, iir);
				break;
		} 
		
	}
	
	
	return IRQ_RETVAL(iir);
}


/*
 * Function ali_ircc_sir_receive (self)
 *
 *    Receive one frame from the infrared port
 *
 */
static void ali_ircc_sir_receive(struct ali_ircc_cb *self) 
{
	int boguscount = 0;
	int iobase;
	
	IRDA_ASSERT(self != NULL, return;);

	iobase = self->io.sir_base;

	/*  
	 * Receive all characters in Rx FIFO, unwrap and unstuff them. 
         * async_unwrap_char will deliver all found frames  
	 */
	do {
		async_unwrap_char(self->netdev, &self->netdev->stats, &self->rx_buff,
				  inb(iobase+UART_RX));

		/* Make sure we don't stay here too long */
		if (boguscount++ > 32) {
			pr_debug("%s(), breaking!\n", __func__);
			break;
		}
	} while (inb(iobase+UART_LSR) & UART_LSR_DR);	
	
}

/*
 * Function ali_ircc_sir_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 */
static void ali_ircc_sir_write_wakeup(struct ali_ircc_cb *self)
{
	int actual = 0;
	int iobase;	

	IRDA_ASSERT(self != NULL, return;);

	
	iobase = self->io.sir_base;

	/* Finished with frame?  */
	if (self->tx_buff.len > 0)  
	{
		/* Write data left in transmit buffer */
		actual = ali_ircc_sir_write(iobase, self->io.fifo_size, 
				      self->tx_buff.data, self->tx_buff.len);
		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
	} 
	else 
	{
		if (self->new_speed) 
		{
			/* We must wait until all data are gone */
			while(!(inb(iobase+UART_LSR) & UART_LSR_TEMT))
				pr_debug("%s(), UART_LSR_THRE\n", __func__);
			
			pr_debug("%s(), Changing speed! self->new_speed = %d\n",
				 __func__, self->new_speed);
			ali_ircc_change_speed(self, self->new_speed);
			self->new_speed = 0;			
			
			// benjamin 2000/11/10 06:32PM
			if (self->io.speed > 115200)
			{
				pr_debug("%s(), ali_ircc_change_speed from UART_LSR_TEMT\n",
					 __func__);
					
				self->ier = IER_EOM;
				// SetCOMInterrupts(self, TRUE);							
				return;							
			}
		}
		else
		{
			netif_wake_queue(self->netdev);	
		}
			
		self->netdev->stats.tx_packets++;
		
		/* Turn on receive interrupts */
		outb(UART_IER_RDI, iobase+UART_IER);
	}
		
}

static void ali_ircc_change_speed(struct ali_ircc_cb *self, __u32 baud)
{
	struct net_device *dev = self->netdev;
	int iobase;
	
	
	pr_debug("%s(), setting speed = %d\n", __func__, baud);
	
	/* This function *must* be called with irq off and spin-lock.
	 * - Jean II */

	iobase = self->io.fir_base;
	
	SetCOMInterrupts(self, FALSE); // 2000/11/24 11:43AM
	
	/* Go to MIR, FIR Speed */
	if (baud > 115200)
	{
		
					
		ali_ircc_fir_change_speed(self, baud);			
		
		/* Install FIR xmit handler*/
		dev->netdev_ops = &ali_ircc_fir_ops;
				
		/* Enable Interuupt */
		self->ier = IER_EOM; // benjamin 2000/11/20 07:24PM					
				
		/* Be ready for incoming frames */
		ali_ircc_dma_receive(self);	// benajmin 2000/11/8 07:46PM not complete
	}	
	/* Go to SIR Speed */
	else
	{
		ali_ircc_sir_change_speed(self, baud);
				
		/* Install SIR xmit handler*/
		dev->netdev_ops = &ali_ircc_sir_ops;
	}
	
		
	SetCOMInterrupts(self, TRUE);	// 2000/11/24 11:43AM
		
	netif_wake_queue(self->netdev);	
	
}

static void ali_ircc_fir_change_speed(struct ali_ircc_cb *priv, __u32 baud)
{
		
	int iobase; 
	struct ali_ircc_cb *self = priv;
	struct net_device *dev;

		
	IRDA_ASSERT(self != NULL, return;);

	dev = self->netdev;
	iobase = self->io.fir_base;
	
	pr_debug("%s(), self->io.speed = %d, change to speed = %d\n",
		 __func__, self->io.speed, baud);
	
	/* Come from SIR speed */
	if(self->io.speed <=115200)
	{
		SIR2FIR(iobase);
	}
		
	/* Update accounting for new speed */
	self->io.speed = baud;
		
	// Set Dongle Speed mode
	ali_ircc_change_dongle_speed(self, baud);
		
}

/*
 * Function ali_sir_change_speed (self, speed)
 *
 *    Set speed of IrDA port to specified baudrate
 *
 */
static void ali_ircc_sir_change_speed(struct ali_ircc_cb *priv, __u32 speed)
{
	struct ali_ircc_cb *self = priv;
	int iobase; 
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	
	pr_debug("%s(), Setting speed to: %d\n", __func__, speed);

	IRDA_ASSERT(self != NULL, return;);

	iobase = self->io.sir_base;
	
	/* Come from MIR or FIR speed */
	if(self->io.speed >115200)
	{	
		// Set Dongle Speed mode first
		ali_ircc_change_dongle_speed(self, speed);
			
		FIR2SIR(iobase);
	}
		
	// Clear Line and Auxiluary status registers 2000/11/24 11:47AM
		
	inb(iobase+UART_LSR);
	inb(iobase+UART_SCR);
		
	/* Update accounting for new speed */
	self->io.speed = speed;

	divisor = 115200/speed;
	
	fcr = UART_FCR_ENABLE_FIFO;

	/* 
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough. 
	 */
	if (self->io.speed < 38400)
		fcr |= UART_FCR_TRIGGER_1;
	else 
		fcr |= UART_FCR_TRIGGER_14;
        
	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;
	
	outb(UART_LCR_DLAB | lcr, iobase+UART_LCR); /* Set DLAB */
	outb(divisor & 0xff,      iobase+UART_DLL); /* Set speed */
	outb(divisor >> 8,	  iobase+UART_DLM);
	outb(lcr,		  iobase+UART_LCR); /* Set 8N1	*/
	outb(fcr,		  iobase+UART_FCR); /* Enable FIFO's */

	/* without this, the connection will be broken after come back from FIR speed,
	   but with this, the SIR connection is harder to established */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase+UART_MCR);
}

static void ali_ircc_change_dongle_speed(struct ali_ircc_cb *priv, int speed)
{
	
	struct ali_ircc_cb *self = priv;
	int iobase,dongle_id;
	int tmp = 0;
			
	
	iobase = self->io.fir_base; 	/* or iobase = self->io.sir_base; */
	dongle_id = self->io.dongle_id;
	
	/* We are already locked, no need to do it again */
		
	pr_debug("%s(), Set Speed for %s , Speed = %d\n",
		 __func__, dongle_types[dongle_id], speed);
	
	switch_bank(iobase, BANK2);
	tmp = inb(iobase+FIR_IRDA_CR);
		
	/* IBM type dongle */
	if(dongle_id == 0)
	{				
		if(speed == 4000000)
		{
			//	      __ __	
			// SD/MODE __|     |__ __
			//               __ __ 
			// IRTX    __ __|     |__
			//         T1 T2 T3 T4 T5
			
			tmp &=  ~IRDA_CR_HDLC;		// HDLC=0
			tmp |= IRDA_CR_CRC;	   	// CRC=1
			
			switch_bank(iobase, BANK2);
			outb(tmp, iobase+FIR_IRDA_CR);
			
      			// T1 -> SD/MODE:0 IRTX:0
      			tmp &= ~0x09;
      			tmp |= 0x02;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// T2 -> SD/MODE:1 IRTX:0
      			tmp &= ~0x01;
      			tmp |= 0x0a;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// T3 -> SD/MODE:1 IRTX:1
      			tmp |= 0x0b;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// T4 -> SD/MODE:0 IRTX:1
      			tmp &= ~0x08;
      			tmp |= 0x03;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// T5 -> SD/MODE:0 IRTX:0
      			tmp &= ~0x09;
      			tmp |= 0x02;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// reset -> Normal TX output Signal
      			outb(tmp & ~0x02, iobase+FIR_IRDA_CR);      			
		}
		else /* speed <=1152000 */
		{	
			//	      __	
			// SD/MODE __|  |__
			//
			// IRTX    ________
			//         T1 T2 T3  
			
			/* MIR 115200, 57600 */
			if (speed==1152000)
			{
				tmp |= 0xA0;	   //HDLC=1, 1.152Mbps=1
      			}
      			else
      			{
				tmp &=~0x80;	   //HDLC 0.576Mbps
				tmp |= 0x20;	   //HDLC=1,
      			}			
      			
      			tmp |= IRDA_CR_CRC;	   	// CRC=1
      			
      			switch_bank(iobase, BANK2);
      			outb(tmp, iobase+FIR_IRDA_CR);
						
			/* MIR 115200, 57600 */	
						
			//switch_bank(iobase, BANK2);			
			// T1 -> SD/MODE:0 IRTX:0
      			tmp &= ~0x09;
      			tmp |= 0x02;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// T2 -> SD/MODE:1 IRTX:0
      			tmp &= ~0x01;     
      			tmp |= 0x0a;      
      			outb(tmp, iobase+FIR_IRDA_CR);
      			
      			// T3 -> SD/MODE:0 IRTX:0
      			tmp &= ~0x09;
      			tmp |= 0x02;
      			outb(tmp, iobase+FIR_IRDA_CR);
      			udelay(2);
      			
      			// reset -> Normal TX output Signal
      			outb(tmp & ~0x02, iobase+FIR_IRDA_CR);      						
		}		
	}
	else if (dongle_id == 1) /* HP HDSL-3600 */
	{
		switch(speed)
		{
		case 4000000:
			tmp &=  ~IRDA_CR_HDLC;	// HDLC=0
			break;	
			
		case 1152000:
			tmp |= 0xA0;	   	// HDLC=1, 1.152Mbps=1
      			break;
      			
      		case 576000:
      			tmp &=~0x80;	   	// HDLC 0.576Mbps
			tmp |= 0x20;	   	// HDLC=1,
			break;
      		}			
			
		tmp |= IRDA_CR_CRC;	   	// CRC=1
			
		switch_bank(iobase, BANK2);
      		outb(tmp, iobase+FIR_IRDA_CR);		
	}
	else /* HP HDSL-1100 */
	{
		if(speed <= 115200) /* SIR */
		{
			
			tmp &= ~IRDA_CR_FIR_SIN;	// HP sin select = 0
			
			switch_bank(iobase, BANK2);
      			outb(tmp, iobase+FIR_IRDA_CR);			
		}
		else /* MIR FIR */
		{	
			
			switch(speed)
			{
			case 4000000:
				tmp &=  ~IRDA_CR_HDLC;	// HDLC=0
				break;	
			
			case 1152000:
				tmp |= 0xA0;	   	// HDLC=1, 1.152Mbps=1
      				break;
      			
      			case 576000:
      				tmp &=~0x80;	   	// HDLC 0.576Mbps
				tmp |= 0x20;	   	// HDLC=1,
				break;
      			}			
			
			tmp |= IRDA_CR_CRC;	   	// CRC=1
			tmp |= IRDA_CR_FIR_SIN;		// HP sin select = 1
			
			switch_bank(iobase, BANK2);
      			outb(tmp, iobase+FIR_IRDA_CR);			
		}
	}
			
	switch_bank(iobase, BANK0);
	
}

/*
 * Function ali_ircc_sir_write (driver)
 *
 *    Fill Tx FIFO with transmit data
 *
 */
static int ali_ircc_sir_write(int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;
	
		
	/* Tx FIFO should be empty! */
	if (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		pr_debug("%s(), failed, fifo not empty!\n", __func__);
		return 0;
	}
        
	/* Fill FIFO with current frame */
	while ((fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual], iobase+UART_TX);

		actual++;
	}
	
	return actual;
}

/*
 * Function ali_ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int ali_ircc_net_open(struct net_device *dev)
{
	struct ali_ircc_cb *self;
	int iobase;
	char hwname[32];
		
	
	IRDA_ASSERT(dev != NULL, return -1;);
	
	self = netdev_priv(dev);
	
	IRDA_ASSERT(self != NULL, return 0;);
	
	iobase = self->io.fir_base;
	
	/* Request IRQ and install Interrupt Handler */
	if (request_irq(self->io.irq, ali_ircc_interrupt, 0, dev->name, dev)) 
	{
		net_warn_ratelimited("%s, unable to allocate irq=%d\n",
				     ALI_IRCC_DRIVER_NAME, self->io.irq);
		return -EAGAIN;
	}
	
	/*
	 * Always allocate the DMA channel after the IRQ, and clean up on 
	 * failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		net_warn_ratelimited("%s, unable to allocate dma=%d\n",
				     ALI_IRCC_DRIVER_NAME, self->io.dma);
		free_irq(self->io.irq, dev);
		return -EAGAIN;
	}
	
	/* Turn on interrups */
	outb(UART_IER_RDI , iobase+UART_IER);

	/* Ready to play! */
	netif_start_queue(dev); //benjamin by irport
	
	/* Give self a hardware name */
	sprintf(hwname, "ALI-FIR @ 0x%03x", self->io.fir_base);

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(dev, &self->qos, hwname);
		
	
	return 0;
}

/*
 * Function ali_ircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int ali_ircc_net_close(struct net_device *dev)
{	

	struct ali_ircc_cb *self;
	//int iobase;
			
		
	IRDA_ASSERT(dev != NULL, return -1;);

	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

	/* Stop device */
	netif_stop_queue(dev);
	
	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;
		
	disable_dma(self->io.dma);

	/* Disable interrupts */
	SetCOMInterrupts(self, FALSE);
	       
	free_irq(self->io.irq, dev);
	free_dma(self->io.dma);

	
	return 0;
}

/*
 * Function ali_ircc_fir_hard_xmit (skb, dev)
 *
 *    Transmit the frame
 *
 */
static netdev_tx_t ali_ircc_fir_hard_xmit(struct sk_buff *skb,
						struct net_device *dev)
{
	struct ali_ircc_cb *self;
	unsigned long flags;
	int iobase;
	__u32 speed;
	int mtt, diff;
	
	
	self = netdev_priv(dev);
	iobase = self->io.fir_base;

	netif_stop_queue(dev);
	
	/* Make sure tests *& speed change are atomic */
	spin_lock_irqsave(&self->lock, flags);
	
	/* Note : you should make sure that speed changes are not going
	 * to corrupt any outgoing frame. Look at nsc-ircc for the gory
	 * details - Jean II */

	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		/* Check for empty frame */
		if (!skb->len) {
			ali_ircc_change_speed(self, speed); 
			netif_trans_update(dev);
			spin_unlock_irqrestore(&self->lock, flags);
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		} else
			self->new_speed = speed;
	}

	/* Register and copy this frame to DMA memory */
	self->tx_fifo.queue[self->tx_fifo.free].start = self->tx_fifo.tail;
	self->tx_fifo.queue[self->tx_fifo.free].len = skb->len;
	self->tx_fifo.tail += skb->len;

	dev->stats.tx_bytes += skb->len;

	skb_copy_from_linear_data(skb, self->tx_fifo.queue[self->tx_fifo.free].start,
		      skb->len);
	self->tx_fifo.len++;
	self->tx_fifo.free++;

	/* Start transmit only if there is currently no transmit going on */
	if (self->tx_fifo.len == 1) 
	{
		/* Check if we must wait the min turn time or not */
		mtt = irda_get_mtt(skb);
				
		if (mtt) 
		{
			/* Check how much time we have used already */
			diff = ktime_us_delta(ktime_get(), self->stamp);
			/* self->stamp is set from ali_ircc_dma_receive_complete() */
							
			pr_debug("%s(), ******* diff = %d *******\n",
				 __func__, diff);

			/* Check if the mtt is larger than the time we have
			 * already used by all the protocol processing
			 */
			if (mtt > diff)
			{				
				mtt -= diff;
								
				/* 
				 * Use timer if delay larger than 1000 us, and
				 * use udelay for smaller values which should
				 * be acceptable
				 */
				if (mtt > 500) 
				{
					/* Adjust for timer resolution */
					mtt = (mtt+250) / 500; 	/* 4 discard, 5 get advanced, Let's round off */
					
					pr_debug("%s(), ************** mtt = %d ***********\n",
						 __func__, mtt);
					
					/* Setup timer */
					if (mtt == 1) /* 500 us */
					{
						switch_bank(iobase, BANK1);
						outb(TIMER_IIR_500, iobase+FIR_TIMER_IIR);
					}	
					else if (mtt == 2) /* 1 ms */
					{
						switch_bank(iobase, BANK1);
						outb(TIMER_IIR_1ms, iobase+FIR_TIMER_IIR);
					}					
					else /* > 2ms -> 4ms */
					{
						switch_bank(iobase, BANK1);
						outb(TIMER_IIR_2ms, iobase+FIR_TIMER_IIR);
					}
					
					
					/* Start timer */
					outb(inb(iobase+FIR_CR) | CR_TIMER_EN, iobase+FIR_CR);
					self->io.direction = IO_XMIT;
					
					/* Enable timer interrupt */
					self->ier = IER_TIMER;
					SetCOMInterrupts(self, TRUE);					
					
					/* Timer will take care of the rest */
					goto out; 
				} 
				else
					udelay(mtt);
			} // if (if (mtt > diff)
		}// if (mtt) 
				
		/* Enable EOM interrupt */
		self->ier = IER_EOM;
		SetCOMInterrupts(self, TRUE);
		
		/* Transmit frame */
		ali_ircc_dma_xmit(self);
	} // if (self->tx_fifo.len == 1) 
	
 out:
 	
	/* Not busy transmitting anymore if window is not full */
	if (self->tx_fifo.free < MAX_TX_WINDOW)
		netif_wake_queue(self->netdev);
	
	/* Restore bank register */
	switch_bank(iobase, BANK0);

	netif_trans_update(dev);
	spin_unlock_irqrestore(&self->lock, flags);
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;	
}


static void ali_ircc_dma_xmit(struct ali_ircc_cb *self)
{
	int iobase, tmp;
	unsigned char FIFO_OPTI, Hi, Lo;
	
	
	
	iobase = self->io.fir_base;
	
	/* FIFO threshold , this method comes from NDIS5 code */
	
	if(self->tx_fifo.queue[self->tx_fifo.ptr].len < TX_FIFO_Threshold)
		FIFO_OPTI = self->tx_fifo.queue[self->tx_fifo.ptr].len-1;
	else
		FIFO_OPTI = TX_FIFO_Threshold;
	
	/* Disable DMA */
	switch_bank(iobase, BANK1);
	outb(inb(iobase+FIR_CR) & ~CR_DMA_EN, iobase+FIR_CR);
	
	self->io.direction = IO_XMIT;
	
	irda_setup_dma(self->io.dma, 
		       ((u8 *)self->tx_fifo.queue[self->tx_fifo.ptr].start -
			self->tx_buff.head) + self->tx_buff_dma,
		       self->tx_fifo.queue[self->tx_fifo.ptr].len, 
		       DMA_TX_MODE);
		
	/* Reset Tx FIFO */
	switch_bank(iobase, BANK0);
	outb(LCR_A_FIFO_RESET, iobase+FIR_LCR_A);
	
	/* Set Tx FIFO threshold */
	if (self->fifo_opti_buf!=FIFO_OPTI) 
	{
		switch_bank(iobase, BANK1);
	    	outb(FIFO_OPTI, iobase+FIR_FIFO_TR) ;
	    	self->fifo_opti_buf=FIFO_OPTI;
	}
	
	/* Set Tx DMA threshold */
	switch_bank(iobase, BANK1);
	outb(TX_DMA_Threshold, iobase+FIR_DMA_TR);
	
	/* Set max Tx frame size */
	Hi = (self->tx_fifo.queue[self->tx_fifo.ptr].len >> 8) & 0x0f;
	Lo = self->tx_fifo.queue[self->tx_fifo.ptr].len & 0xff;
	switch_bank(iobase, BANK2);
	outb(Hi, iobase+FIR_TX_DSR_HI);
	outb(Lo, iobase+FIR_TX_DSR_LO);
	
	/* Disable SIP , Disable Brick Wall (we don't support in TX mode), Change to TX mode */
	switch_bank(iobase, BANK0);	
	tmp = inb(iobase+FIR_LCR_B);
	tmp &= ~0x20; // Disable SIP
	outb(((unsigned char)(tmp & 0x3f) | LCR_B_TX_MODE) & ~LCR_B_BW, iobase+FIR_LCR_B);
	pr_debug("%s(), *** Change to TX mode: FIR_LCR_B = 0x%x ***\n",
		 __func__, inb(iobase + FIR_LCR_B));
	
	outb(0, iobase+FIR_LSR);
			
	/* Enable DMA and Burst Mode */
	switch_bank(iobase, BANK1);
	outb(inb(iobase+FIR_CR) | CR_DMA_EN | CR_DMA_BURST, iobase+FIR_CR);
	
	switch_bank(iobase, BANK0); 
	
}

static int  ali_ircc_dma_xmit_complete(struct ali_ircc_cb *self)
{
	int iobase;
	int ret = TRUE;
	
	
	iobase = self->io.fir_base;
	
	/* Disable DMA */
	switch_bank(iobase, BANK1);
	outb(inb(iobase+FIR_CR) & ~CR_DMA_EN, iobase+FIR_CR);
	
	/* Check for underrun! */
	switch_bank(iobase, BANK0);
	if((inb(iobase+FIR_LSR) & LSR_FRAME_ABORT) == LSR_FRAME_ABORT)
	
	{
		net_err_ratelimited("%s(), ********* LSR_FRAME_ABORT *********\n",
				    __func__);
		self->netdev->stats.tx_errors++;
		self->netdev->stats.tx_fifo_errors++;
	}
	else 
	{
		self->netdev->stats.tx_packets++;
	}

	/* Check if we need to change the speed */
	if (self->new_speed) 
	{
		ali_ircc_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	/* Finished with this frame, so prepare for next */
	self->tx_fifo.ptr++;
	self->tx_fifo.len--;

	/* Any frames to be sent back-to-back? */
	if (self->tx_fifo.len) 
	{
		ali_ircc_dma_xmit(self);
		
		/* Not finished yet! */
		ret = FALSE;
	} 
	else 
	{	/* Reset Tx FIFO info */
		self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
		self->tx_fifo.tail = self->tx_buff.head;
	}

	/* Make sure we have room for more frames */
	if (self->tx_fifo.free < MAX_TX_WINDOW) {
		/* Not busy transmitting anymore */
		/* Tell the network layer, that we can accept more frames */
		netif_wake_queue(self->netdev);
	}
		
	switch_bank(iobase, BANK0); 
	
	return ret;
}

/*
 * Function ali_ircc_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int ali_ircc_dma_receive(struct ali_ircc_cb *self) 
{
	int iobase, tmp;
	
	
	iobase = self->io.fir_base;
	
	/* Reset Tx FIFO info */
	self->tx_fifo.len = self->tx_fifo.ptr = self->tx_fifo.free = 0;
	self->tx_fifo.tail = self->tx_buff.head;
		
	/* Disable DMA */
	switch_bank(iobase, BANK1);
	outb(inb(iobase+FIR_CR) & ~CR_DMA_EN, iobase+FIR_CR);
	
	/* Reset Message Count */
	switch_bank(iobase, BANK0);
	outb(0x07, iobase+FIR_LSR);
		
	self->rcvFramesOverflow = FALSE;	
	
	self->LineStatus = inb(iobase+FIR_LSR) ;
	
	/* Reset Rx FIFO info */
	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;
		
	/* Reset Rx FIFO */
	// switch_bank(iobase, BANK0);
	outb(LCR_A_FIFO_RESET, iobase+FIR_LCR_A); 
	
	self->st_fifo.len = self->st_fifo.pending_bytes = 0;
	self->st_fifo.tail = self->st_fifo.head = 0;
		
	irda_setup_dma(self->io.dma, self->rx_buff_dma, self->rx_buff.truesize,
		       DMA_RX_MODE);
	 
	/* Set Receive Mode,Brick Wall */
	//switch_bank(iobase, BANK0);
	tmp = inb(iobase+FIR_LCR_B);
	outb((unsigned char)(tmp &0x3f) | LCR_B_RX_MODE | LCR_B_BW , iobase + FIR_LCR_B); // 2000/12/1 05:16PM
	pr_debug("%s(), *** Change To RX mode: FIR_LCR_B = 0x%x ***\n",
		 __func__, inb(iobase + FIR_LCR_B));
			
	/* Set Rx Threshold */
	switch_bank(iobase, BANK1);
	outb(RX_FIFO_Threshold, iobase+FIR_FIFO_TR);
	outb(RX_DMA_Threshold, iobase+FIR_DMA_TR);
		
	/* Enable DMA and Burst Mode */
	// switch_bank(iobase, BANK1);
	outb(CR_DMA_EN | CR_DMA_BURST, iobase+FIR_CR);
				
	switch_bank(iobase, BANK0); 
	return 0;
}

static int  ali_ircc_dma_receive_complete(struct ali_ircc_cb *self)
{
	struct st_fifo *st_fifo;
	struct sk_buff *skb;
	__u8 status, MessageCount;
	int len, i, iobase, val;	

	st_fifo = &self->st_fifo;		
	iobase = self->io.fir_base;	
		
	switch_bank(iobase, BANK0);
	MessageCount = inb(iobase+ FIR_LSR)&0x07;
	
	if (MessageCount > 0)	
		pr_debug("%s(), Message count = %d\n", __func__, MessageCount);
		
	for (i=0; i<=MessageCount; i++)
	{
		/* Bank 0 */
		switch_bank(iobase, BANK0);
		status = inb(iobase+FIR_LSR);
		
		switch_bank(iobase, BANK2);
		len = inb(iobase+FIR_RX_DSR_HI) & 0x0f;
		len = len << 8; 
		len |= inb(iobase+FIR_RX_DSR_LO);
		
		pr_debug("%s(), RX Length = 0x%.2x,\n", __func__ , len);
		pr_debug("%s(), RX Status = 0x%.2x,\n", __func__ , status);
		
		if (st_fifo->tail >= MAX_RX_WINDOW) {
			pr_debug("%s(), window is full!\n", __func__);
			continue;
		}
			
		st_fifo->entries[st_fifo->tail].status = status;
		st_fifo->entries[st_fifo->tail].len = len;
		st_fifo->pending_bytes += len;
		st_fifo->tail++;
		st_fifo->len++;
	}
			
	for (i=0; i<=MessageCount; i++)
	{	
		/* Get first entry */
		status = st_fifo->entries[st_fifo->head].status;
		len    = st_fifo->entries[st_fifo->head].len;
		st_fifo->pending_bytes -= len;
		st_fifo->head++;
		st_fifo->len--;			
		
		/* Check for errors */
		if ((status & 0xd8) || self->rcvFramesOverflow || (len==0)) 		
		{
			pr_debug("%s(), ************* RX Errors ************\n",
				 __func__);
			
			/* Skip frame */
			self->netdev->stats.rx_errors++;
			
			self->rx_buff.data += len;
			
			if (status & LSR_FIFO_UR) 
			{
				self->netdev->stats.rx_frame_errors++;
				pr_debug("%s(), ************* FIFO Errors ************\n",
					 __func__);
			}	
			if (status & LSR_FRAME_ERROR)
			{
				self->netdev->stats.rx_frame_errors++;
				pr_debug("%s(), ************* FRAME Errors ************\n",
					 __func__);
			}
							
			if (status & LSR_CRC_ERROR) 
			{
				self->netdev->stats.rx_crc_errors++;
				pr_debug("%s(), ************* CRC Errors ************\n",
					 __func__);
			}
			
			if(self->rcvFramesOverflow)
			{
				self->netdev->stats.rx_frame_errors++;
				pr_debug("%s(), ************* Overran DMA buffer ************\n",
					 __func__);
			}
			if(len == 0)
			{
				self->netdev->stats.rx_frame_errors++;
				pr_debug("%s(), ********** Receive Frame Size = 0 *********\n",
					 __func__);
			}
		}	 
		else 
		{
			
			if (st_fifo->pending_bytes < 32) 
			{
				switch_bank(iobase, BANK0);
				val = inb(iobase+FIR_BSR);	
				if ((val& BSR_FIFO_NOT_EMPTY)== 0x80) 
				{
					pr_debug("%s(), ************* BSR_FIFO_NOT_EMPTY ************\n",
						 __func__);
					
					/* Put this entry back in fifo */
					st_fifo->head--;
					st_fifo->len++;
					st_fifo->pending_bytes += len;
					st_fifo->entries[st_fifo->head].status = status;
					st_fifo->entries[st_fifo->head].len = len;
						
					/*  
		 			* DMA not finished yet, so try again 
		 			* later, set timer value, resolution 
		 			* 500 us 
		 			*/
					 
					switch_bank(iobase, BANK1);
					outb(TIMER_IIR_500, iobase+FIR_TIMER_IIR); // 2001/1/2 05:07PM
					
					/* Enable Timer */
					outb(inb(iobase+FIR_CR) | CR_TIMER_EN, iobase+FIR_CR);
						
					return FALSE; /* I'll be back! */
				}
			}		
			
			/* 
			 * Remember the time we received this frame, so we can
			 * reduce the min turn time a bit since we will know
			 * how much time we have used for protocol processing
			 */
			self->stamp = ktime_get();

			skb = dev_alloc_skb(len+1);
			if (!skb) {
				self->netdev->stats.rx_dropped++;

				return FALSE;
			}
			
			/* Make sure IP header gets aligned */
			skb_reserve(skb, 1); 
			
			/* Copy frame without CRC, CRC is removed by hardware*/
			skb_put(skb, len);
			skb_copy_to_linear_data(skb, self->rx_buff.data, len);

			/* Move to next frame */
			self->rx_buff.data += len;
			self->netdev->stats.rx_bytes += len;
			self->netdev->stats.rx_packets++;

			skb->dev = self->netdev;
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
		}
	}
	
	switch_bank(iobase, BANK0);	
		
	return TRUE;
}



/*
 * Function ali_ircc_sir_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static netdev_tx_t ali_ircc_sir_hard_xmit(struct sk_buff *skb,
						struct net_device *dev)
{
	struct ali_ircc_cb *self;
	unsigned long flags;
	int iobase;
	__u32 speed;
	
	
	IRDA_ASSERT(dev != NULL, return NETDEV_TX_OK;);
	
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return NETDEV_TX_OK;);

	iobase = self->io.sir_base;

	netif_stop_queue(dev);
	
	/* Make sure tests *& speed change are atomic */
	spin_lock_irqsave(&self->lock, flags);

	/* Note : you should make sure that speed changes are not going
	 * to corrupt any outgoing frame. Look at nsc-ircc for the gory
	 * details - Jean II */

	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		/* Check for empty frame */
		if (!skb->len) {
			ali_ircc_change_speed(self, speed); 
			netif_trans_update(dev);
			spin_unlock_irqrestore(&self->lock, flags);
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		} else
			self->new_speed = speed;
	}

	/* Init tx buffer */
	self->tx_buff.data = self->tx_buff.head;

        /* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
					   self->tx_buff.truesize);
	
	self->netdev->stats.tx_bytes += self->tx_buff.len;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, iobase+UART_IER); 

	netif_trans_update(dev);
	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);
	
	
	return NETDEV_TX_OK;	
}


/*
 * Function ali_ircc_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int ali_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct ali_ircc_cb *self;
	unsigned long flags;
	int ret = 0;
	
	
	IRDA_ASSERT(dev != NULL, return -1;);

	self = netdev_priv(dev);

	IRDA_ASSERT(self != NULL, return -1;);

	pr_debug("%s(), %s, (cmd=0x%X)\n", __func__ , dev->name, cmd);
	
	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		pr_debug("%s(), SIOCSBANDWIDTH\n", __func__);
		/*
		 * This function will also be used by IrLAP to change the
		 * speed, so we still must allow for speed change within
		 * interrupt context.
		 */
		if (!in_interrupt() && !capable(CAP_NET_ADMIN))
			return -EPERM;
		
		spin_lock_irqsave(&self->lock, flags);
		ali_ircc_change_speed(self, irq->ifr_baudrate);		
		spin_unlock_irqrestore(&self->lock, flags);
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		pr_debug("%s(), SIOCSMEDIABUSY\n", __func__);
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		pr_debug("%s(), SIOCGRECEIVING\n", __func__);
		/* This is protected */
		irq->ifr_receiving = ali_ircc_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	
	
	return ret;
}

/*
 * Function ali_ircc_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int ali_ircc_is_receiving(struct ali_ircc_cb *self)
{
	unsigned long flags;
	int status = FALSE;
	int iobase;		
	
	
	IRDA_ASSERT(self != NULL, return FALSE;);

	spin_lock_irqsave(&self->lock, flags);

	if (self->io.speed > 115200) 
	{
		iobase = self->io.fir_base;
		
		switch_bank(iobase, BANK1);
		if((inb(iobase+FIR_FIFO_FR) & 0x3f) != 0) 		
		{
			/* We are receiving something */
			pr_debug("%s(), We are receiving something\n",
				 __func__);
			status = TRUE;
		}
		switch_bank(iobase, BANK0);		
	} 
	else
	{ 
		status = (self->rx_buff.state != OUTSIDE_FRAME);
	}
	
	spin_unlock_irqrestore(&self->lock, flags);
	
	
	return status;
}

static int ali_ircc_suspend(struct platform_device *dev, pm_message_t state)
{
	struct ali_ircc_cb *self = platform_get_drvdata(dev);
	
	net_info_ratelimited("%s, Suspending\n", ALI_IRCC_DRIVER_NAME);

	if (self->io.suspended)
		return 0;

	ali_ircc_net_close(self->netdev);

	self->io.suspended = 1;
	
	return 0;
}

static int ali_ircc_resume(struct platform_device *dev)
{
	struct ali_ircc_cb *self = platform_get_drvdata(dev);
	
	if (!self->io.suspended)
		return 0;
	
	ali_ircc_net_open(self->netdev);
	
	net_info_ratelimited("%s, Waking up\n", ALI_IRCC_DRIVER_NAME);

	self->io.suspended = 0;

	return 0;
}

/* ALi Chip Function */

static void SetCOMInterrupts(struct ali_ircc_cb *self , unsigned char enable)
{
	
	unsigned char newMask;
	
	int iobase = self->io.fir_base; /* or sir_base */

	pr_debug("%s(), -------- Start -------- ( Enable = %d )\n",
		 __func__, enable);
	
	/* Enable the interrupt which we wish to */
	if (enable){
		if (self->io.direction == IO_XMIT)
		{
			if (self->io.speed > 115200) /* FIR, MIR */
			{
				newMask = self->ier;
			}
			else /* SIR */
			{
				newMask = UART_IER_THRI | UART_IER_RDI;
			}
		}
		else {
			if (self->io.speed > 115200) /* FIR, MIR */
			{
				newMask = self->ier;
			}
			else /* SIR */
			{
				newMask = UART_IER_RDI;
			}
		}
	}
	else /* Disable all the interrupts */
	{
		newMask = 0x00;

	}

	//SIR and FIR has different registers
	if (self->io.speed > 115200)
	{	
		switch_bank(iobase, BANK0);
		outb(newMask, iobase+FIR_IER);
	}
	else
		outb(newMask, iobase+UART_IER);
		
}

static void SIR2FIR(int iobase)
{
	//unsigned char tmp;
		
	
	/* Already protected (change_speed() or setup()), no need to lock.
	 * Jean II */
	
	outb(0x28, iobase+UART_MCR);
	outb(0x68, iobase+UART_MCR);
	outb(0x88, iobase+UART_MCR);		
	
	outb(0x60, iobase+FIR_MCR); 	/*  Master Reset */
	outb(0x20, iobase+FIR_MCR); 	/*  Master Interrupt Enable */
	
	//tmp = inb(iobase+FIR_LCR_B);	/* SIP enable */
	//tmp |= 0x20;
	//outb(tmp, iobase+FIR_LCR_B);	
	
}

static void FIR2SIR(int iobase)
{
	unsigned char val;
	
	
	/* Already protected (change_speed() or setup()), no need to lock.
	 * Jean II */
	
	outb(0x20, iobase+FIR_MCR); 	/* IRQ to low */
	outb(0x00, iobase+UART_IER); 	
		
	outb(0xA0, iobase+FIR_MCR); 	/* Don't set master reset */
	outb(0x00, iobase+UART_FCR);
	outb(0x07, iobase+UART_FCR);		
	
	val = inb(iobase+UART_RX);
	val = inb(iobase+UART_LSR);
	val = inb(iobase+UART_MSR);
	
}

MODULE_AUTHOR("Benjamin Kong <benjamin_kong@ali.com.tw>");
MODULE_DESCRIPTION("ALi FIR Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" ALI_IRCC_DRIVER_NAME);


module_param_hw_array(io, int, ioport, NULL, 0);
MODULE_PARM_DESC(io, "Base I/O addresses");
module_param_hw_array(irq, int, irq, NULL, 0);
MODULE_PARM_DESC(irq, "IRQ lines");
module_param_hw_array(dma, int, dma, NULL, 0);
MODULE_PARM_DESC(dma, "DMA channels");

module_init(ali_ircc_init);
module_exit(ali_ircc_cleanup);

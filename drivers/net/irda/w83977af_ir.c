/*********************************************************************
 *                
 * Filename:      w83977af_ir.c
 * Version:       1.0
 * Description:   FIR driver for the Winbond W83977AF Super I/O chip
 * Status:        Experimental.
 * Author:        Paul VanderSpek
 * Created at:    Wed Nov  4 11:46:16 1998
 * Modified at:   Fri Jan 28 12:10:59 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998-1999 Rebel.com
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Paul VanderSpek nor Rebel.com admit liability nor provide
 *     warranty for any of this software. This material is provided "AS-IS"
 *     and at no charge.
 *     
 *     If you find bugs in this file, its very likely that the same bug
 *     will also be in pc87108.c since the implementations are quite
 *     similar.
 *
 *     Notice that all functions that needs to access the chip in _any_
 *     way, must save BSR register on entry, and restore it on exit. 
 *     It is _very_ important to follow this policy!
 *
 *         __u8 bank;
 *     
 *         bank = inb( iobase+BSR);
 *  
 *         do_your_stuff_here();
 *
 *         outb( bank, iobase+BSR);
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>
#include "w83977af.h"
#include "w83977af_ir.h"

#ifdef  CONFIG_ARCH_NETWINDER            /* Adjust to NetWinder differences */
#undef  CONFIG_NETWINDER_TX_DMA_PROBLEMS /* Not needed */
#define CONFIG_NETWINDER_RX_DMA_PROBLEMS /* Must have this one! */
#endif
#undef  CONFIG_USE_INTERNAL_TIMER  /* Just cannot make that timer work */
#define CONFIG_USE_W977_PNP        /* Currently needed */
#define PIO_MAX_SPEED       115200 

static char *driver_name = "w83977af_ir";
static int  qos_mtt_bits = 0x07;   /* 1 ms or more */

#define CHIP_IO_EXTENT 8

static unsigned int io[] = { 0x180, ~0, ~0, ~0 };
#ifdef CONFIG_ARCH_NETWINDER             /* Adjust to NetWinder differences */
static unsigned int irq[] = { 6, 0, 0, 0 };
#else
static unsigned int irq[] = { 11, 0, 0, 0 };
#endif
static unsigned int dma[] = { 1, 0, 0, 0 };
static unsigned int efbase[] = { W977_EFIO_BASE, W977_EFIO2_BASE };
static unsigned int efio = W977_EFIO_BASE;

static struct w83977af_ir *dev_self[] = { NULL, NULL, NULL, NULL};

/* Some prototypes */
static int  w83977af_open(int i, unsigned int iobase, unsigned int irq, 
                          unsigned int dma);
static int  w83977af_close(struct w83977af_ir *self);
static int  w83977af_probe(int iobase, int irq, int dma);
static int  w83977af_dma_receive(struct w83977af_ir *self); 
static int  w83977af_dma_receive_complete(struct w83977af_ir *self);
static int  w83977af_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static int  w83977af_pio_write(int iobase, __u8 *buf, int len, int fifo_size);
static void w83977af_dma_write(struct w83977af_ir *self, int iobase);
static void w83977af_change_speed(struct w83977af_ir *self, __u32 speed);
static int  w83977af_is_receiving(struct w83977af_ir *self);

static int  w83977af_net_open(struct net_device *dev);
static int  w83977af_net_close(struct net_device *dev);
static int  w83977af_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

/*
 * Function w83977af_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
static int __init w83977af_init(void)
{
        int i;

	IRDA_DEBUG(0, "%s()\n", __func__ );

	for (i=0; i < ARRAY_SIZE(dev_self) && io[i] < 2000; i++) {
		if (w83977af_open(i, io[i], irq[i], dma[i]) == 0)
			return 0;
	}
	return -ENODEV;
}

/*
 * Function w83977af_cleanup ()
 *
 *    Close all configured chips
 *
 */
static void __exit w83977af_cleanup(void)
{
	int i;

        IRDA_DEBUG(4, "%s()\n", __func__ );

	for (i=0; i < ARRAY_SIZE(dev_self); i++) {
		if (dev_self[i])
			w83977af_close(dev_self[i]);
	}
}

static const struct net_device_ops w83977_netdev_ops = {
	.ndo_open       = w83977af_net_open,
	.ndo_stop       = w83977af_net_close,
	.ndo_start_xmit = w83977af_hard_xmit,
	.ndo_do_ioctl   = w83977af_net_ioctl,
};

/*
 * Function w83977af_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int w83977af_open(int i, unsigned int iobase, unsigned int irq,
			 unsigned int dma)
{
	struct net_device *dev;
        struct w83977af_ir *self;
	int err;

	IRDA_DEBUG(0, "%s()\n", __func__ );

	/* Lock the port that we need */
	if (!request_region(iobase, CHIP_IO_EXTENT, driver_name)) {
		IRDA_DEBUG(0, "%s(), can't get iobase of 0x%03x\n",
		      __func__ , iobase);
		return -ENODEV;
	}

	if (w83977af_probe(iobase, irq, dma) == -1) {
		err = -1;
		goto err_out;
	}
	/*
	 *  Allocate new instance of the driver
	 */
	dev = alloc_irdadev(sizeof(struct w83977af_ir));
	if (dev == NULL) {
		printk( KERN_ERR "IrDA: Can't allocate memory for "
			"IrDA control block!\n");
		err = -ENOMEM;
		goto err_out;
	}

	self = netdev_priv(dev);
	spin_lock_init(&self->lock);
   

	/* Initialize IO */
	self->io.fir_base   = iobase;
        self->io.irq       = irq;
        self->io.fir_ext   = CHIP_IO_EXTENT;
        self->io.dma       = dma;
        self->io.fifo_size = 32;

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	/* The only value we must override it the baudrate */

	/* FIXME: The HP HDLS-1100 does not support 1152000! */
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);

	/* The HP HDLS-1100 needs 1 ms according to the specs */
	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);
	
	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 14384; 
	self->tx_buff.truesize = 4000;
	
	/* Allocate memory if needed */
	self->rx_buff.head =
		dma_alloc_coherent(NULL, self->rx_buff.truesize,
				   &self->rx_buff_dma, GFP_KERNEL);
	if (self->rx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out1;
	}

	memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	
	self->tx_buff.head =
		dma_alloc_coherent(NULL, self->tx_buff.truesize,
				   &self->tx_buff_dma, GFP_KERNEL);
	if (self->tx_buff.head == NULL) {
		err = -ENOMEM;
		goto err_out2;
	}
	memset(self->tx_buff.head, 0, self->tx_buff.truesize);

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;
	self->netdev = dev;

	dev->netdev_ops	= &w83977_netdev_ops;

	err = register_netdev(dev);
	if (err) {
		IRDA_ERROR("%s(), register_netdevice() failed!\n", __func__);
		goto err_out3;
	}
	IRDA_MESSAGE("IrDA: Registered device %s\n", dev->name);

	/* Need to store self somewhere */
	dev_self[i] = self;
	
	return 0;
err_out3:
	dma_free_coherent(NULL, self->tx_buff.truesize,
			  self->tx_buff.head, self->tx_buff_dma);
err_out2:	
	dma_free_coherent(NULL, self->rx_buff.truesize,
			  self->rx_buff.head, self->rx_buff_dma);
err_out1:
	free_netdev(dev);
err_out:
	release_region(iobase, CHIP_IO_EXTENT);
	return err;
}

/*
 * Function w83977af_close (self)
 *
 *    Close driver instance
 *
 */
static int w83977af_close(struct w83977af_ir *self)
{
	int iobase;

	IRDA_DEBUG(0, "%s()\n", __func__ );

        iobase = self->io.fir_base;

#ifdef CONFIG_USE_W977_PNP
	/* enter PnP configuration mode */
	w977_efm_enter(efio);

	w977_select_device(W977_DEVICE_IR, efio);

	/* Deactivate device */
	w977_write_reg(0x30, 0x00, efio);

	w977_efm_exit(efio);
#endif /* CONFIG_USE_W977_PNP */

	/* Remove netdevice */
	unregister_netdev(self->netdev);

	/* Release the PORT that this driver is using */
	IRDA_DEBUG(0 , "%s(), Releasing Region %03x\n", 
	      __func__ , self->io.fir_base);
	release_region(self->io.fir_base, self->io.fir_ext);

	if (self->tx_buff.head)
		dma_free_coherent(NULL, self->tx_buff.truesize,
				  self->tx_buff.head, self->tx_buff_dma);
	
	if (self->rx_buff.head)
		dma_free_coherent(NULL, self->rx_buff.truesize,
				  self->rx_buff.head, self->rx_buff_dma);

	free_netdev(self->netdev);

	return 0;
}

static int w83977af_probe(int iobase, int irq, int dma)
{
  	int version;
	int i;
  	
 	for (i=0; i < 2; i++) {
		IRDA_DEBUG( 0, "%s()\n", __func__ );
#ifdef CONFIG_USE_W977_PNP
 		/* Enter PnP configuration mode */
		w977_efm_enter(efbase[i]);
  
 		w977_select_device(W977_DEVICE_IR, efbase[i]);
  
 		/* Configure PnP port, IRQ, and DMA channel */
 		w977_write_reg(0x60, (iobase >> 8) & 0xff, efbase[i]);
 		w977_write_reg(0x61, (iobase) & 0xff, efbase[i]);
  
 		w977_write_reg(0x70, irq, efbase[i]);
#ifdef CONFIG_ARCH_NETWINDER
		/* Netwinder uses 1 higher than Linux */
 		w977_write_reg(0x74, dma+1, efbase[i]);
#else
 		w977_write_reg(0x74, dma, efbase[i]);   
#endif /*CONFIG_ARCH_NETWINDER */
 		w977_write_reg(0x75, 0x04, efbase[i]);  /* Disable Tx DMA */
  	
 		/* Set append hardware CRC, enable IR bank selection */	
 		w977_write_reg(0xf0, APEDCRC|ENBNKSEL, efbase[i]);
  
 		/* Activate device */
 		w977_write_reg(0x30, 0x01, efbase[i]);
  
 		w977_efm_exit(efbase[i]);
#endif /* CONFIG_USE_W977_PNP */
  		/* Disable Advanced mode */
  		switch_bank(iobase, SET2);
  		outb(iobase+2, 0x00);  
 
 		/* Turn on UART (global) interrupts */
 		switch_bank(iobase, SET0);
  		outb(HCR_EN_IRQ, iobase+HCR);
  	
  		/* Switch to advanced mode */
  		switch_bank(iobase, SET2);
  		outb(inb(iobase+ADCR1) | ADCR1_ADV_SL, iobase+ADCR1);
  
  		/* Set default IR-mode */
  		switch_bank(iobase, SET0);
  		outb(HCR_SIR, iobase+HCR);
  
  		/* Read the Advanced IR ID */
  		switch_bank(iobase, SET3);
  		version = inb(iobase+AUID);
  	
  		/* Should be 0x1? */
  		if (0x10 == (version & 0xf0)) {
 			efio = efbase[i];
 
 			/* Set FIFO size to 32 */
 			switch_bank(iobase, SET2);
 			outb(ADCR2_RXFS32|ADCR2_TXFS32, iobase+ADCR2);	
 	
 			/* Set FIFO threshold to TX17, RX16 */
 			switch_bank(iobase, SET0);	
 			outb(UFR_RXTL|UFR_TXTL|UFR_TXF_RST|UFR_RXF_RST|
			     UFR_EN_FIFO,iobase+UFR);
 
 			/* Receiver frame length */
 			switch_bank(iobase, SET4);
			outb(2048 & 0xff, iobase+6);
			outb((2048 >> 8) & 0x1f, iobase+7);

			/* 
			 * Init HP HSDL-1100 transceiver. 
			 * 
			 * Set IRX_MSL since we have 2 * receive paths IRRX, 
			 * and IRRXH. Clear IRSL0D since we want IRSL0 * to 
			 * be a input pin used for IRRXH 
			 *
			 *   IRRX  pin 37 connected to receiver 
			 *   IRTX  pin 38 connected to transmitter
			 *   FIRRX pin 39 connected to receiver      (IRSL0) 
			 *   CIRRX pin 40 connected to pin 37
			 */
			switch_bank(iobase, SET7);
			outb(0x40, iobase+7);
			
			IRDA_MESSAGE("W83977AF (IR) driver loaded. "
				     "Version: 0x%02x\n", version);
			
			return 0;
		} else {
			/* Try next extented function register address */
			IRDA_DEBUG( 0, "%s(), Wrong chip version", __func__ );
		}
  	}   	
	return -1;
}

static void w83977af_change_speed(struct w83977af_ir *self, __u32 speed)
{
	int ir_mode = HCR_SIR;
	int iobase; 
	__u8 set;

	iobase = self->io.fir_base;

	/* Update accounting for new speed */
	self->io.speed = speed;

	/* Save current bank */
	set = inb(iobase+SSR);

	/* Disable interrupts */
	switch_bank(iobase, SET0);
	outb(0, iobase+ICR);

	/* Select Set 2 */
	switch_bank(iobase, SET2);
	outb(0x00, iobase+ABHL);

	switch (speed) {
	case 9600:   outb(0x0c, iobase+ABLL); break;
	case 19200:  outb(0x06, iobase+ABLL); break;
	case 38400:  outb(0x03, iobase+ABLL); break;
	case 57600:  outb(0x02, iobase+ABLL); break;
	case 115200: outb(0x01, iobase+ABLL); break;
	case 576000:
		ir_mode = HCR_MIR_576;
		IRDA_DEBUG(0, "%s(), handling baud of 576000\n", __func__ );
		break;
	case 1152000:
		ir_mode = HCR_MIR_1152;
		IRDA_DEBUG(0, "%s(), handling baud of 1152000\n", __func__ );
		break;
	case 4000000:
		ir_mode = HCR_FIR;
		IRDA_DEBUG(0, "%s(), handling baud of 4000000\n", __func__ );
		break;
	default:
		ir_mode = HCR_FIR;
		IRDA_DEBUG(0, "%s(), unknown baud rate of %d\n", __func__ , speed);
		break;
	}

	/* Set speed mode */
	switch_bank(iobase, SET0);
	outb(ir_mode, iobase+HCR);

	/* set FIFO size to 32 */
	switch_bank(iobase, SET2);
	outb(ADCR2_RXFS32|ADCR2_TXFS32, iobase+ADCR2);	
	
	/* set FIFO threshold to TX17, RX16 */
	switch_bank(iobase, SET0);
	outb(0x00, iobase+UFR);        /* Reset */
	outb(UFR_EN_FIFO, iobase+UFR); /* First we must enable FIFO */
	outb(0xa7, iobase+UFR);

	netif_wake_queue(self->netdev);
		
	/* Enable some interrupts so we can receive frames */
	switch_bank(iobase, SET0);
	if (speed > PIO_MAX_SPEED) {
		outb(ICR_EFSFI, iobase+ICR);
		w83977af_dma_receive(self);
	} else
		outb(ICR_ERBRI, iobase+ICR);
    	
	/* Restore SSR */
	outb(set, iobase+SSR);
}

/*
 * Function w83977af_hard_xmit (skb, dev)
 *
 *    Sets up a DMA transfer to send the current frame.
 *
 */
static int w83977af_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct w83977af_ir *self;
	__s32 speed;
	int iobase;
	__u8 set;
	int mtt;
	
	self = netdev_priv(dev);

	iobase = self->io.fir_base;

	IRDA_DEBUG(4, "%s(%ld), skb->len=%d\n", __func__ , jiffies,
		   (int) skb->len);
	
	/* Lock transmit buffer */
	netif_stop_queue(dev);
	
	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		/* Check for empty frame */
		if (!skb->len) {
			w83977af_change_speed(self, speed); 
			dev->trans_start = jiffies;
			dev_kfree_skb(skb);
			return 0;
		} else
			self->new_speed = speed;
	}

	/* Save current set */
	set = inb(iobase+SSR);
	
	/* Decide if we should use PIO or DMA transfer */
	if (self->io.speed > PIO_MAX_SPEED) {
		self->tx_buff.data = self->tx_buff.head;
		skb_copy_from_linear_data(skb, self->tx_buff.data, skb->len);
		self->tx_buff.len = skb->len;
		
		mtt = irda_get_mtt(skb);
#ifdef CONFIG_USE_INTERNAL_TIMER
	        if (mtt > 50) {
			/* Adjust for timer resolution */
			mtt /= 1000+1;

			/* Setup timer */
			switch_bank(iobase, SET4);
			outb(mtt & 0xff, iobase+TMRL);
			outb((mtt >> 8) & 0x0f, iobase+TMRH);
			
			/* Start timer */
			outb(IR_MSL_EN_TMR, iobase+IR_MSL);
			self->io.direction = IO_XMIT;
			
			/* Enable timer interrupt */
			switch_bank(iobase, SET0);
			outb(ICR_ETMRI, iobase+ICR);
		} else {
#endif
			IRDA_DEBUG(4, "%s(%ld), mtt=%d\n", __func__ , jiffies, mtt);
			if (mtt)
				udelay(mtt);

			/* Enable DMA interrupt */
			switch_bank(iobase, SET0);
	 		outb(ICR_EDMAI, iobase+ICR);
	     		w83977af_dma_write(self, iobase);
#ifdef CONFIG_USE_INTERNAL_TIMER
		}
#endif
	} else {
		self->tx_buff.data = self->tx_buff.head;
		self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
						   self->tx_buff.truesize);
		
		/* Add interrupt on tx low level (will fire immediately) */
		switch_bank(iobase, SET0);
		outb(ICR_ETXTHI, iobase+ICR);
	}
	dev->trans_start = jiffies;
	dev_kfree_skb(skb);

	/* Restore set register */
	outb(set, iobase+SSR);

	return 0;
}

/*
 * Function w83977af_dma_write (self, iobase)
 *
 *    Send frame using DMA
 *
 */
static void w83977af_dma_write(struct w83977af_ir *self, int iobase)
{
	__u8 set;
#ifdef CONFIG_NETWINDER_TX_DMA_PROBLEMS
	unsigned long flags;
	__u8 hcr;
#endif
        IRDA_DEBUG(4, "%s(), len=%d\n", __func__ , self->tx_buff.len);

	/* Save current set */
	set = inb(iobase+SSR);

	/* Disable DMA */
	switch_bank(iobase, SET0);
	outb(inb(iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);

	/* Choose transmit DMA channel  */ 
	switch_bank(iobase, SET2);
	outb(ADCR1_D_CHSW|/*ADCR1_DMA_F|*/ADCR1_ADV_SL, iobase+ADCR1);
#ifdef CONFIG_NETWINDER_TX_DMA_PROBLEMS
	spin_lock_irqsave(&self->lock, flags);

	disable_dma(self->io.dma);
	clear_dma_ff(self->io.dma);
	set_dma_mode(self->io.dma, DMA_MODE_READ);
	set_dma_addr(self->io.dma, self->tx_buff_dma);
	set_dma_count(self->io.dma, self->tx_buff.len);
#else
	irda_setup_dma(self->io.dma, self->tx_buff_dma, self->tx_buff.len,
		       DMA_MODE_WRITE);	
#endif
	self->io.direction = IO_XMIT;
	
	/* Enable DMA */
 	switch_bank(iobase, SET0);
#ifdef CONFIG_NETWINDER_TX_DMA_PROBLEMS
	hcr = inb(iobase+HCR);
	outb(hcr | HCR_EN_DMA, iobase+HCR);
	enable_dma(self->io.dma);
	spin_unlock_irqrestore(&self->lock, flags);
#else	
	outb(inb(iobase+HCR) | HCR_EN_DMA | HCR_TX_WT, iobase+HCR);
#endif

	/* Restore set register */
	outb(set, iobase+SSR);
}

/*
 * Function w83977af_pio_write (iobase, buf, len, fifo_size)
 *
 *    
 *
 */
static int w83977af_pio_write(int iobase, __u8 *buf, int len, int fifo_size)
{
	int actual = 0;
	__u8 set;
	
	IRDA_DEBUG(4, "%s()\n", __func__ );

	/* Save current bank */
	set = inb(iobase+SSR);

	switch_bank(iobase, SET0);
	if (!(inb_p(iobase+USR) & USR_TSRE)) {
		IRDA_DEBUG(4,
			   "%s(), warning, FIFO not empty yet!\n", __func__  );

		fifo_size -= 17;
		IRDA_DEBUG(4, "%s(), %d bytes left in tx fifo\n", 
			   __func__ , fifo_size);
	}

	/* Fill FIFO with current frame */
	while ((fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual++], iobase+TBR);
	}
        
	IRDA_DEBUG(4, "%s(), fifo_size %d ; %d sent of %d\n", 
		   __func__ , fifo_size, actual, len);

	/* Restore bank */
	outb(set, iobase+SSR);

	return actual;
}

/*
 * Function w83977af_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. So do the necessary things
 *
 *    
 */
static void w83977af_dma_xmit_complete(struct w83977af_ir *self)
{
	int iobase;
	__u8 set;

	IRDA_DEBUG(4, "%s(%ld)\n", __func__ , jiffies);

	IRDA_ASSERT(self != NULL, return;);

	iobase = self->io.fir_base;

	/* Save current set */
	set = inb(iobase+SSR);

	/* Disable DMA */
	switch_bank(iobase, SET0);
	outb(inb(iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);
	
	/* Check for underrrun! */
	if (inb(iobase+AUDR) & AUDR_UNDR) {
		IRDA_DEBUG(0, "%s(), Transmit underrun!\n", __func__ );
		
		self->netdev->stats.tx_errors++;
		self->netdev->stats.tx_fifo_errors++;

		/* Clear bit, by writing 1 to it */
		outb(AUDR_UNDR, iobase+AUDR);
	} else
		self->netdev->stats.tx_packets++;

	
	if (self->new_speed) {
		w83977af_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	/* Unlock tx_buff and request another frame */
	/* Tell the network layer, that we want more frames */
	netif_wake_queue(self->netdev);
	
	/* Restore set */
	outb(set, iobase+SSR);
}

/*
 * Function w83977af_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int w83977af_dma_receive(struct w83977af_ir *self)
{
	int iobase;
	__u8 set;
#ifdef CONFIG_NETWINDER_RX_DMA_PROBLEMS
	unsigned long flags;
	__u8 hcr;
#endif
	IRDA_ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(4, "%s\n", __func__ );

	iobase= self->io.fir_base;

	/* Save current set */
	set = inb(iobase+SSR);

	/* Disable DMA */
	switch_bank(iobase, SET0);
	outb(inb(iobase+HCR) & ~HCR_EN_DMA, iobase+HCR);

	/* Choose DMA Rx, DMA Fairness, and Advanced mode */
	switch_bank(iobase, SET2);
	outb((inb(iobase+ADCR1) & ~ADCR1_D_CHSW)/*|ADCR1_DMA_F*/|ADCR1_ADV_SL,
	     iobase+ADCR1);

	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;

#ifdef CONFIG_NETWINDER_RX_DMA_PROBLEMS
	spin_lock_irqsave(&self->lock, flags);

	disable_dma(self->io.dma);
	clear_dma_ff(self->io.dma);
	set_dma_mode(self->io.dma, DMA_MODE_READ);
	set_dma_addr(self->io.dma, self->rx_buff_dma);
	set_dma_count(self->io.dma, self->rx_buff.truesize);
#else
	irda_setup_dma(self->io.dma, self->rx_buff_dma, self->rx_buff.truesize,
		       DMA_MODE_READ);
#endif
	/* 
	 * Reset Rx FIFO. This will also flush the ST_FIFO, it's very 
	 * important that we don't reset the Tx FIFO since it might not
	 * be finished transmitting yet
	 */
	switch_bank(iobase, SET0);
	outb(UFR_RXTL|UFR_TXTL|UFR_RXF_RST|UFR_EN_FIFO, iobase+UFR);
	self->st_fifo.len = self->st_fifo.tail = self->st_fifo.head = 0;
	
	/* Enable DMA */
	switch_bank(iobase, SET0);
#ifdef CONFIG_NETWINDER_RX_DMA_PROBLEMS
	hcr = inb(iobase+HCR);
	outb(hcr | HCR_EN_DMA, iobase+HCR);
	enable_dma(self->io.dma);
	spin_unlock_irqrestore(&self->lock, flags);
#else	
	outb(inb(iobase+HCR) | HCR_EN_DMA, iobase+HCR);
#endif
	/* Restore set */
	outb(set, iobase+SSR);

	return 0;
}

/*
 * Function w83977af_receive_complete (self)
 *
 *    Finished with receiving a frame
 *
 */
static int w83977af_dma_receive_complete(struct w83977af_ir *self)
{
	struct sk_buff *skb;
	struct st_fifo *st_fifo;
	int len;
	int iobase;
	__u8 set;
	__u8 status;

	IRDA_DEBUG(4, "%s\n", __func__ );

	st_fifo = &self->st_fifo;

	iobase = self->io.fir_base;

	/* Save current set */
	set = inb(iobase+SSR);
	
	iobase = self->io.fir_base;

	/* Read status FIFO */
	switch_bank(iobase, SET5);
	while ((status = inb(iobase+FS_FO)) & FS_FO_FSFDR) {
		st_fifo->entries[st_fifo->tail].status = status;
		
		st_fifo->entries[st_fifo->tail].len  = inb(iobase+RFLFL);
		st_fifo->entries[st_fifo->tail].len |= inb(iobase+RFLFH) << 8;
		
		st_fifo->tail++;
		st_fifo->len++;
	}
	
	while (st_fifo->len) {
		/* Get first entry */
		status = st_fifo->entries[st_fifo->head].status;
		len    = st_fifo->entries[st_fifo->head].len;
		st_fifo->head++;
		st_fifo->len--;

		/* Check for errors */
		if (status & FS_FO_ERR_MSK) {
			if (status & FS_FO_LST_FR) {
				/* Add number of lost frames to stats */
				self->netdev->stats.rx_errors += len;
			} else {
				/* Skip frame */
				self->netdev->stats.rx_errors++;
				
				self->rx_buff.data += len;
				
				if (status & FS_FO_MX_LEX)
					self->netdev->stats.rx_length_errors++;
				
				if (status & FS_FO_PHY_ERR) 
					self->netdev->stats.rx_frame_errors++;
				
				if (status & FS_FO_CRC_ERR) 
					self->netdev->stats.rx_crc_errors++;
			}
			/* The errors below can be reported in both cases */
			if (status & FS_FO_RX_OV)
				self->netdev->stats.rx_fifo_errors++;
			
			if (status & FS_FO_FSF_OV)
				self->netdev->stats.rx_fifo_errors++;
			
		} else {
			/* Check if we have transferred all data to memory */
			switch_bank(iobase, SET0);
			if (inb(iobase+USR) & USR_RDR) {
#ifdef CONFIG_USE_INTERNAL_TIMER
				/* Put this entry back in fifo */
				st_fifo->head--;
				st_fifo->len++;
				st_fifo->entries[st_fifo->head].status = status;
				st_fifo->entries[st_fifo->head].len = len;
				
				/* Restore set register */
				outb(set, iobase+SSR);
			
				return FALSE; 	/* I'll be back! */
#else
				udelay(80); /* Should be enough!? */
#endif
			}
						
			skb = dev_alloc_skb(len+1);
			if (skb == NULL)  {
				printk(KERN_INFO
				       "%s(), memory squeeze, dropping frame.\n", __func__);
				/* Restore set register */
				outb(set, iobase+SSR);

				return FALSE;
			}
			
			/*  Align to 20 bytes */
			skb_reserve(skb, 1); 
			
			/* Copy frame without CRC */
			if (self->io.speed < 4000000) {
				skb_put(skb, len-2);
				skb_copy_to_linear_data(skb,
							self->rx_buff.data,
							len - 2);
			} else {
				skb_put(skb, len-4);
				skb_copy_to_linear_data(skb,
							self->rx_buff.data,
							len - 4);
			}

			/* Move to next frame */
			self->rx_buff.data += len;
			self->netdev->stats.rx_packets++;
			
			skb->dev = self->netdev;
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
		}
	}
	/* Restore set register */
	outb(set, iobase+SSR);

	return TRUE;
}

/*
 * Function pc87108_pio_receive (self)
 *
 *    Receive all data in receiver FIFO
 *
 */
static void w83977af_pio_receive(struct w83977af_ir *self) 
{
	__u8 byte = 0x00;
	int iobase;

	IRDA_DEBUG(4, "%s()\n", __func__ );

	IRDA_ASSERT(self != NULL, return;);
	
	iobase = self->io.fir_base;
	
	/*  Receive all characters in Rx FIFO */
	do {
		byte = inb(iobase+RBR);
		async_unwrap_char(self->netdev, &self->netdev->stats, &self->rx_buff,
				  byte);
	} while (inb(iobase+USR) & USR_RDR); /* Data available */	
}

/*
 * Function w83977af_sir_interrupt (self, eir)
 *
 *    Handle SIR interrupt
 *
 */
static __u8 w83977af_sir_interrupt(struct w83977af_ir *self, int isr)
{
	int actual;
	__u8 new_icr = 0;
	__u8 set;
	int iobase;

	IRDA_DEBUG(4, "%s(), isr=%#x\n", __func__ , isr);
	
	iobase = self->io.fir_base;
	/* Transmit FIFO low on data */
	if (isr & ISR_TXTH_I) {
		/* Write data left in transmit buffer */
		actual = w83977af_pio_write(self->io.fir_base, 
					    self->tx_buff.data, 
					    self->tx_buff.len, 
					    self->io.fifo_size);

		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
		
		self->io.direction = IO_XMIT;

		/* Check if finished */
		if (self->tx_buff.len > 0) {
			new_icr |= ICR_ETXTHI;
		} else {
			set = inb(iobase+SSR);
			switch_bank(iobase, SET0);
			outb(AUDR_SFEND, iobase+AUDR);
			outb(set, iobase+SSR); 

			self->netdev->stats.tx_packets++;

			/* Feed me more packets */
			netif_wake_queue(self->netdev);
			new_icr |= ICR_ETBREI;
		}
	}
	/* Check if transmission has completed */
	if (isr & ISR_TXEMP_I) {		
		/* Check if we need to change the speed? */
		if (self->new_speed) {
			IRDA_DEBUG(2,
				   "%s(), Changing speed!\n", __func__ );
			w83977af_change_speed(self, self->new_speed);
			self->new_speed = 0;
		}

		/* Turn around and get ready to receive some data */
		self->io.direction = IO_RECV;
		new_icr |= ICR_ERBRI;
	}

	/* Rx FIFO threshold or timeout */
	if (isr & ISR_RXTH_I) {
		w83977af_pio_receive(self);

		/* Keep receiving */
		new_icr |= ICR_ERBRI;
	}
	return new_icr;
}

/*
 * Function pc87108_fir_interrupt (self, eir)
 *
 *    Handle MIR/FIR interrupt
 *
 */
static __u8 w83977af_fir_interrupt(struct w83977af_ir *self, int isr)
{
	__u8 new_icr = 0;
	__u8 set;
	int iobase;

	iobase = self->io.fir_base;
	set = inb(iobase+SSR);
	
	/* End of frame detected in FIFO */
	if (isr & (ISR_FEND_I|ISR_FSF_I)) {
		if (w83977af_dma_receive_complete(self)) {
			
			/* Wait for next status FIFO interrupt */
			new_icr |= ICR_EFSFI;
		} else {
			/* DMA not finished yet */

			/* Set timer value, resolution 1 ms */
			switch_bank(iobase, SET4);
			outb(0x01, iobase+TMRL); /* 1 ms */
			outb(0x00, iobase+TMRH);

			/* Start timer */
			outb(IR_MSL_EN_TMR, iobase+IR_MSL);

			new_icr |= ICR_ETMRI;
		}
	}
	/* Timer finished */
	if (isr & ISR_TMR_I) {
		/* Disable timer */
		switch_bank(iobase, SET4);
		outb(0, iobase+IR_MSL);

		/* Clear timer event */
		/* switch_bank(iobase, SET0); */
/* 		outb(ASCR_CTE, iobase+ASCR); */

		/* Check if this is a TX timer interrupt */
		if (self->io.direction == IO_XMIT) {
			w83977af_dma_write(self, iobase);

			new_icr |= ICR_EDMAI;
		} else {
			/* Check if DMA has now finished */
			w83977af_dma_receive_complete(self);

			new_icr |= ICR_EFSFI;
		}
	}	
	/* Finished with DMA */
	if (isr & ISR_DMA_I) {
		w83977af_dma_xmit_complete(self);

		/* Check if there are more frames to be transmitted */
		/* if (irda_device_txqueue_empty(self)) { */
		
		/* Prepare for receive 
		 * 
		 * ** Netwinder Tx DMA likes that we do this anyway **
		 */
		w83977af_dma_receive(self);
		new_icr = ICR_EFSFI;
	       /* } */
	}
	
	/* Restore set */
	outb(set, iobase+SSR);

	return new_icr;
}

/*
 * Function w83977af_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static irqreturn_t w83977af_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct w83977af_ir *self;
	__u8 set, icr, isr;
	int iobase;

	self = netdev_priv(dev);

	iobase = self->io.fir_base;

	/* Save current bank */
	set = inb(iobase+SSR);
	switch_bank(iobase, SET0);
	
	icr = inb(iobase+ICR); 
	isr = inb(iobase+ISR) & icr; /* Mask out the interesting ones */ 

	outb(0, iobase+ICR); /* Disable interrupts */
	
	if (isr) {
		/* Dispatch interrupt handler for the current speed */
		if (self->io.speed > PIO_MAX_SPEED )
			icr = w83977af_fir_interrupt(self, isr);
		else
			icr = w83977af_sir_interrupt(self, isr);
	}

	outb(icr, iobase+ICR);    /* Restore (new) interrupts */
	outb(set, iobase+SSR);    /* Restore bank register */
	return IRQ_RETVAL(isr);
}

/*
 * Function w83977af_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int w83977af_is_receiving(struct w83977af_ir *self)
{
	int status = FALSE;
	int iobase;
	__u8 set;

	IRDA_ASSERT(self != NULL, return FALSE;);

	if (self->io.speed > 115200) {
		iobase = self->io.fir_base;

		/* Check if rx FIFO is not empty */
		set = inb(iobase+SSR);
		switch_bank(iobase, SET2);
		if ((inb(iobase+RXFDTH) & 0x3f) != 0) {
			/* We are receiving something */
			status =  TRUE;
		}
		outb(set, iobase+SSR);
	} else 
		status = (self->rx_buff.state != OUTSIDE_FRAME);
	
	return status;
}

/*
 * Function w83977af_net_open (dev)
 *
 *    Start the device
 *
 */
static int w83977af_net_open(struct net_device *dev)
{
	struct w83977af_ir *self;
	int iobase;
	char hwname[32];
	__u8 set;
	
	IRDA_DEBUG(0, "%s()\n", __func__ );
	
	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	
	IRDA_ASSERT(self != NULL, return 0;);
	
	iobase = self->io.fir_base;

	if (request_irq(self->io.irq, w83977af_interrupt, 0, dev->name, 
			(void *) dev)) {
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		free_irq(self->io.irq, self);
		return -EAGAIN;
	}
		
	/* Save current set */
	set = inb(iobase+SSR);

 	/* Enable some interrupts so we can receive frames again */
 	switch_bank(iobase, SET0);
 	if (self->io.speed > 115200) {
 		outb(ICR_EFSFI, iobase+ICR);
 		w83977af_dma_receive(self);
 	} else
 		outb(ICR_ERBRI, iobase+ICR);

	/* Restore bank register */
	outb(set, iobase+SSR);

	/* Ready to play! */
	netif_start_queue(dev);
	
	/* Give self a hardware name */
	sprintf(hwname, "w83977af @ 0x%03x", self->io.fir_base);

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(dev, &self->qos, hwname);

	return 0;
}

/*
 * Function w83977af_net_close (dev)
 *
 *    Stop the device
 *
 */
static int w83977af_net_close(struct net_device *dev)
{
	struct w83977af_ir *self;
	int iobase;
	__u8 set;

	IRDA_DEBUG(0, "%s()\n", __func__ );

	IRDA_ASSERT(dev != NULL, return -1;);
	
	self = netdev_priv(dev);
	
	IRDA_ASSERT(self != NULL, return 0;);
	
	iobase = self->io.fir_base;

	/* Stop device */
	netif_stop_queue(dev);
	
	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	disable_dma(self->io.dma);

	/* Save current set */
	set = inb(iobase+SSR);
	
	/* Disable interrupts */
	switch_bank(iobase, SET0);
	outb(0, iobase+ICR); 

	free_irq(self->io.irq, dev);
	free_dma(self->io.dma);

	/* Restore bank register */
	outb(set, iobase+SSR);

	return 0;
}

/*
 * Function w83977af_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int w83977af_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct w83977af_ir *self;
	unsigned long flags;
	int ret = 0;

	IRDA_ASSERT(dev != NULL, return -1;);

	self = netdev_priv(dev);

	IRDA_ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(2, "%s(), %s, (cmd=0x%X)\n", __func__ , dev->name, cmd);
	
	spin_lock_irqsave(&self->lock, flags);

	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		w83977af_change_speed(self, irq->ifr_baudrate);
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = w83977af_is_receiving(self);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
out:
	spin_unlock_irqrestore(&self->lock, flags);
	return ret;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Winbond W83977AF IrDA Device Driver");
MODULE_LICENSE("GPL");


module_param(qos_mtt_bits, int, 0);
MODULE_PARM_DESC(qos_mtt_bits, "Mimimum Turn Time");
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "Base I/O addresses");
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "IRQ lines");

/*
 * Function init_module (void)
 *
 *    
 *
 */
module_init(w83977af_init);

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
module_exit(w83977af_cleanup);

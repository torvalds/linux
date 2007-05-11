/*
 * linux/drivers/net/irda/pxaficp_ir.c
 *
 * Based on sa1100_ir.c by Russell King
 *
 * Changes copyright (C) 2003-2005 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Infra-red driver (SIR/FIR) for the PXA2xx embedded microprocessor
 *
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>

#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/delay.h>
#include <asm/hardware.h>
#include <asm/arch/irda.h>
#include <asm/arch/pxa-regs.h>

#ifdef CONFIG_MACH_MAINSTONE
#include <asm/arch/mainstone.h>
#endif

#define IrSR_RXPL_NEG_IS_ZERO (1<<4)
#define IrSR_RXPL_POS_IS_ZERO 0x0
#define IrSR_TXPL_NEG_IS_ZERO (1<<3)
#define IrSR_TXPL_POS_IS_ZERO 0x0
#define IrSR_XMODE_PULSE_1_6  (1<<2)
#define IrSR_XMODE_PULSE_3_16 0x0
#define IrSR_RCVEIR_IR_MODE   (1<<1)
#define IrSR_RCVEIR_UART_MODE 0x0
#define IrSR_XMITIR_IR_MODE   (1<<0)
#define IrSR_XMITIR_UART_MODE 0x0

#define IrSR_IR_RECEIVE_ON (\
                IrSR_RXPL_NEG_IS_ZERO | \
                IrSR_TXPL_POS_IS_ZERO | \
                IrSR_XMODE_PULSE_3_16 | \
                IrSR_RCVEIR_IR_MODE   | \
                IrSR_XMITIR_UART_MODE)

#define IrSR_IR_TRANSMIT_ON (\
                IrSR_RXPL_NEG_IS_ZERO | \
                IrSR_TXPL_POS_IS_ZERO | \
                IrSR_XMODE_PULSE_3_16 | \
                IrSR_RCVEIR_UART_MODE | \
                IrSR_XMITIR_IR_MODE)

struct pxa_irda {
	int			speed;
	int			newspeed;
	unsigned long		last_oscr;

	unsigned char		*dma_rx_buff;
	unsigned char		*dma_tx_buff;
	dma_addr_t		dma_rx_buff_phy;
	dma_addr_t		dma_tx_buff_phy;
	unsigned int		dma_tx_buff_len;
	int			txdma;
	int			rxdma;

	struct net_device_stats	stats;
	struct irlap_cb		*irlap;
	struct qos_info		qos;

	iobuff_t		tx_buff;
	iobuff_t		rx_buff;

	struct device		*dev;
	struct pxaficp_platform_data *pdata;
};


#define IS_FIR(si)		((si)->speed >= 4000000)
#define IRDA_FRAME_SIZE_LIMIT	2047

inline static void pxa_irda_fir_dma_rx_start(struct pxa_irda *si)
{
	DCSR(si->rxdma)  = DCSR_NODESC;
	DSADR(si->rxdma) = __PREG(ICDR);
	DTADR(si->rxdma) = si->dma_rx_buff_phy;
	DCMD(si->rxdma) = DCMD_INCTRGADDR | DCMD_FLOWSRC |  DCMD_WIDTH1 | DCMD_BURST32 | IRDA_FRAME_SIZE_LIMIT;
	DCSR(si->rxdma) |= DCSR_RUN;
}

inline static void pxa_irda_fir_dma_tx_start(struct pxa_irda *si)
{
	DCSR(si->txdma)  = DCSR_NODESC;
	DSADR(si->txdma) = si->dma_tx_buff_phy;
	DTADR(si->txdma) = __PREG(ICDR);
	DCMD(si->txdma) = DCMD_INCSRCADDR | DCMD_FLOWTRG |  DCMD_ENDIRQEN | DCMD_WIDTH1 | DCMD_BURST32 | si->dma_tx_buff_len;
	DCSR(si->txdma) |= DCSR_RUN;
}

/*
 * Set the IrDA communications speed.
 */
static int pxa_irda_set_speed(struct pxa_irda *si, int speed)
{
	unsigned long flags;
	unsigned int divisor;

	switch (speed) {
	case 9600:	case 19200:	case 38400:
	case 57600:	case 115200:

		/* refer to PXA250/210 Developer's Manual 10-7 */
		/*  BaudRate = 14.7456 MHz / (16*Divisor) */
		divisor = 14745600 / (16 * speed);

		local_irq_save(flags);

		if (IS_FIR(si)) {
			/* stop RX DMA */
			DCSR(si->rxdma) &= ~DCSR_RUN;
			/* disable FICP */
			ICCR0 = 0;
			pxa_set_cken(CKEN_FICP, 0);

			/* set board transceiver to SIR mode */
			si->pdata->transceiver_mode(si->dev, IR_SIRMODE);

			/* configure GPIO46/47 */
			pxa_gpio_mode(GPIO46_STRXD_MD);
			pxa_gpio_mode(GPIO47_STTXD_MD);

			/* enable the STUART clock */
			pxa_set_cken(CKEN_STUART, 1);
		}

		/* disable STUART first */
		STIER = 0;

		/* access DLL & DLH */
		STLCR |= LCR_DLAB;
		STDLL = divisor & 0xff;
		STDLH = divisor >> 8;
		STLCR &= ~LCR_DLAB;

		si->speed = speed;
		STISR = IrSR_IR_RECEIVE_ON | IrSR_XMODE_PULSE_1_6;
		STIER = IER_UUE | IER_RLSE | IER_RAVIE | IER_RTIOE;

		local_irq_restore(flags);
		break;

	case 4000000:
		local_irq_save(flags);

		/* disable STUART */
		STIER = 0;
		STISR = 0;
		pxa_set_cken(CKEN_STUART, 0);

		/* disable FICP first */
		ICCR0 = 0;

		/* set board transceiver to FIR mode */
		si->pdata->transceiver_mode(si->dev, IR_FIRMODE);

		/* configure GPIO46/47 */
		pxa_gpio_mode(GPIO46_ICPRXD_MD);
		pxa_gpio_mode(GPIO47_ICPTXD_MD);

		/* enable the FICP clock */
		pxa_set_cken(CKEN_FICP, 1);

		si->speed = speed;
		pxa_irda_fir_dma_rx_start(si);
		ICCR0 = ICCR0_ITR | ICCR0_RXE;

		local_irq_restore(flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* SIR interrupt service routine. */
static irqreturn_t pxa_irda_sir_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct pxa_irda *si = netdev_priv(dev);
	int iir, lsr, data;

	iir = STIIR;

	switch  (iir & 0x0F) {
	case 0x06: /* Receiver Line Status */
	  	lsr = STLSR;
		while (lsr & LSR_FIFOE) {
			data = STRBR;
			if (lsr & (LSR_OE | LSR_PE | LSR_FE | LSR_BI)) {
				printk(KERN_DEBUG "pxa_ir: sir receiving error\n");
				si->stats.rx_errors++;
				if (lsr & LSR_FE)
					si->stats.rx_frame_errors++;
				if (lsr & LSR_OE)
					si->stats.rx_fifo_errors++;
			} else {
				si->stats.rx_bytes++;
				async_unwrap_char(dev, &si->stats, &si->rx_buff, data);
			}
			lsr = STLSR;
		}
		dev->last_rx = jiffies;
		si->last_oscr = OSCR;
		break;

	case 0x04: /* Received Data Available */
	  	   /* forth through */

	case 0x0C: /* Character Timeout Indication */
	  	do  {
		    si->stats.rx_bytes++;
	            async_unwrap_char(dev, &si->stats, &si->rx_buff, STRBR);
	  	} while (STLSR & LSR_DR);
	  	dev->last_rx = jiffies;
		si->last_oscr = OSCR;
	  	break;

	case 0x02: /* Transmit FIFO Data Request */
	    	while ((si->tx_buff.len) && (STLSR & LSR_TDRQ)) {
	    		STTHR = *si->tx_buff.data++;
			si->tx_buff.len -= 1;
	    	}

		if (si->tx_buff.len == 0) {
			si->stats.tx_packets++;
			si->stats.tx_bytes += si->tx_buff.data -
					      si->tx_buff.head;

                        /* We need to ensure that the transmitter has finished. */
			while ((STLSR & LSR_TEMT) == 0)
				cpu_relax();
			si->last_oscr = OSCR;

			/*
		 	* Ok, we've finished transmitting.  Now enable
		 	* the receiver.  Sometimes we get a receive IRQ
		 	* immediately after a transmit...
		 	*/
			if (si->newspeed) {
				pxa_irda_set_speed(si, si->newspeed);
				si->newspeed = 0;
			} else {
				/* enable IR Receiver, disable IR Transmitter */
				STISR = IrSR_IR_RECEIVE_ON | IrSR_XMODE_PULSE_1_6;
				/* enable STUART and receive interrupts */
				STIER = IER_UUE | IER_RLSE | IER_RAVIE | IER_RTIOE;
			}
			/* I'm hungry! */
			netif_wake_queue(dev);
		}
		break;
	}

	return IRQ_HANDLED;
}

/* FIR Receive DMA interrupt handler */
static void pxa_irda_fir_dma_rx_irq(int channel, void *data)
{
	int dcsr = DCSR(channel);

	DCSR(channel) = dcsr & ~DCSR_RUN;

	printk(KERN_DEBUG "pxa_ir: fir rx dma bus error %#x\n", dcsr);
}

/* FIR Transmit DMA interrupt handler */
static void pxa_irda_fir_dma_tx_irq(int channel, void *data)
{
	struct net_device *dev = data;
	struct pxa_irda *si = netdev_priv(dev);
	int dcsr;

	dcsr = DCSR(channel);
	DCSR(channel) = dcsr & ~DCSR_RUN;

	if (dcsr & DCSR_ENDINTR)  {
		si->stats.tx_packets++;
		si->stats.tx_bytes += si->dma_tx_buff_len;
	} else {
		si->stats.tx_errors++;
	}

	while (ICSR1 & ICSR1_TBY)
		cpu_relax();
	si->last_oscr = OSCR;

	/*
	 * HACK: It looks like the TBY bit is dropped too soon.
	 * Without this delay things break.
	 */
	udelay(120);

	if (si->newspeed) {
		pxa_irda_set_speed(si, si->newspeed);
		si->newspeed = 0;
	} else {
		int i = 64;

		ICCR0 = 0;
		pxa_irda_fir_dma_rx_start(si);
		while ((ICSR1 & ICSR1_RNE) && i--)
			(void)ICDR;
		ICCR0 = ICCR0_ITR | ICCR0_RXE;

		if (i < 0)
			printk(KERN_ERR "pxa_ir: cannot clear Rx FIFO!\n");
	}
	netif_wake_queue(dev);
}

/* EIF(Error in FIFO/End in Frame) handler for FIR */
static void pxa_irda_fir_irq_eif(struct pxa_irda *si, struct net_device *dev, int icsr0)
{
	unsigned int len, stat, data;

	/* Get the current data position. */
	len = DTADR(si->rxdma) - si->dma_rx_buff_phy;

	do {
		/* Read Status, and then Data. 	 */
		stat = ICSR1;
		rmb();
		data = ICDR;

		if (stat & (ICSR1_CRE | ICSR1_ROR)) {
			si->stats.rx_errors++;
			if (stat & ICSR1_CRE) {
				printk(KERN_DEBUG "pxa_ir: fir receive CRC error\n");
				si->stats.rx_crc_errors++;
			}
			if (stat & ICSR1_ROR) {
				printk(KERN_DEBUG "pxa_ir: fir receive overrun\n");
				si->stats.rx_over_errors++;
			}
		} else	{
			si->dma_rx_buff[len++] = data;
		}
		/* If we hit the end of frame, there's no point in continuing. */
		if (stat & ICSR1_EOF)
			break;
	} while (ICSR0 & ICSR0_EIF);

	if (stat & ICSR1_EOF) {
		/* end of frame. */
		struct sk_buff *skb;

		if (icsr0 & ICSR0_FRE) {
			printk(KERN_ERR "pxa_ir: dropping erroneous frame\n");
			si->stats.rx_dropped++;
			return;
		}

		skb = alloc_skb(len+1,GFP_ATOMIC);
		if (!skb)  {
			printk(KERN_ERR "pxa_ir: fir out of memory for receive skb\n");
			si->stats.rx_dropped++;
			return;
		}

		/* Align IP header to 20 bytes  */
		skb_reserve(skb, 1);
		skb_copy_to_linear_data(skb, si->dma_rx_buff, len);
		skb_put(skb, len);

		/* Feed it to IrLAP  */
		skb->dev = dev;
		skb_reset_mac_header(skb);
		skb->protocol = htons(ETH_P_IRDA);
		netif_rx(skb);

		si->stats.rx_packets++;
		si->stats.rx_bytes += len;

		dev->last_rx = jiffies;
	}
}

/* FIR interrupt handler */
static irqreturn_t pxa_irda_fir_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct pxa_irda *si = netdev_priv(dev);
	int icsr0, i = 64;

	/* stop RX DMA */
	DCSR(si->rxdma) &= ~DCSR_RUN;
	si->last_oscr = OSCR;
	icsr0 = ICSR0;

	if (icsr0 & (ICSR0_FRE | ICSR0_RAB)) {
		if (icsr0 & ICSR0_FRE) {
		        printk(KERN_DEBUG "pxa_ir: fir receive frame error\n");
			si->stats.rx_frame_errors++;
		} else {
			printk(KERN_DEBUG "pxa_ir: fir receive abort\n");
			si->stats.rx_errors++;
		}
		ICSR0 = icsr0 & (ICSR0_FRE | ICSR0_RAB);
	}

	if (icsr0 & ICSR0_EIF) {
		/* An error in FIFO occured, or there is a end of frame */
		pxa_irda_fir_irq_eif(si, dev, icsr0);
	}

	ICCR0 = 0;
	pxa_irda_fir_dma_rx_start(si);
	while ((ICSR1 & ICSR1_RNE) && i--)
		(void)ICDR;
	ICCR0 = ICCR0_ITR | ICCR0_RXE;

	if (i < 0)
		printk(KERN_ERR "pxa_ir: cannot clear Rx FIFO!\n");

	return IRQ_HANDLED;
}

/* hard_xmit interface of irda device */
static int pxa_irda_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pxa_irda *si = netdev_priv(dev);
	int speed = irda_get_next_speed(skb);

	/*
	 * Does this packet contain a request to change the interface
	 * speed?  If so, remember it until we complete the transmission
	 * of this frame.
	 */
	if (speed != si->speed && speed != -1)
		si->newspeed = speed;

	/*
	 * If this is an empty frame, we can bypass a lot.
	 */
	if (skb->len == 0) {
		if (si->newspeed) {
			si->newspeed = 0;
			pxa_irda_set_speed(si, speed);
		}
		dev_kfree_skb(skb);
		return 0;
	}

	netif_stop_queue(dev);

	if (!IS_FIR(si)) {
		si->tx_buff.data = si->tx_buff.head;
		si->tx_buff.len  = async_wrap_skb(skb, si->tx_buff.data, si->tx_buff.truesize);

		/* Disable STUART interrupts and switch to transmit mode. */
		STIER = 0;
		STISR = IrSR_IR_TRANSMIT_ON | IrSR_XMODE_PULSE_1_6;

		/* enable STUART and transmit interrupts */
		STIER = IER_UUE | IER_TIE;
	} else {
		unsigned long mtt = irda_get_mtt(skb);

		si->dma_tx_buff_len = skb->len;
		skb_copy_from_linear_data(skb, si->dma_tx_buff, skb->len);

		if (mtt)
			while ((unsigned)(OSCR - si->last_oscr)/4 < mtt)
				cpu_relax();

		/* stop RX DMA,  disable FICP */
		DCSR(si->rxdma) &= ~DCSR_RUN;
		ICCR0 = 0;

		pxa_irda_fir_dma_tx_start(si);
		ICCR0 = ICCR0_ITR | ICCR0_TXE;
	}

	dev_kfree_skb(skb);
	dev->trans_start = jiffies;
	return 0;
}

static int pxa_irda_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
{
	struct if_irda_req *rq = (struct if_irda_req *)ifreq;
	struct pxa_irda *si = netdev_priv(dev);
	int ret;

	switch (cmd) {
	case SIOCSBANDWIDTH:
		ret = -EPERM;
		if (capable(CAP_NET_ADMIN)) {
			/*
			 * We are unable to set the speed if the
			 * device is not running.
			 */
			if (netif_running(dev)) {
				ret = pxa_irda_set_speed(si,
						rq->ifr_baudrate);
			} else {
				printk(KERN_INFO "pxa_ir: SIOCSBANDWIDTH: !netif_running\n");
				ret = 0;
			}
		}
		break;

	case SIOCSMEDIABUSY:
		ret = -EPERM;
		if (capable(CAP_NET_ADMIN)) {
			irda_device_set_media_busy(dev, TRUE);
			ret = 0;
		}
		break;

	case SIOCGRECEIVING:
		ret = 0;
		rq->ifr_receiving = IS_FIR(si) ? 0
					: si->rx_buff.state != OUTSIDE_FRAME;
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static struct net_device_stats *pxa_irda_stats(struct net_device *dev)
{
	struct pxa_irda *si = netdev_priv(dev);
	return &si->stats;
}

static void pxa_irda_startup(struct pxa_irda *si)
{
	/* Disable STUART interrupts */
	STIER = 0;
	/* enable STUART interrupt to the processor */
	STMCR = MCR_OUT2;
	/* configure SIR frame format: StartBit - Data 7 ... Data 0 - Stop Bit */
	STLCR = LCR_WLS0 | LCR_WLS1;
	/* enable FIFO, we use FIFO to improve performance */
	STFCR = FCR_TRFIFOE | FCR_ITL_32;

	/* disable FICP */
	ICCR0 = 0;
	/* configure FICP ICCR2 */
	ICCR2 = ICCR2_TXP | ICCR2_TRIG_32;

	/* configure DMAC */
	DRCMR17 = si->rxdma | DRCMR_MAPVLD;
	DRCMR18 = si->txdma | DRCMR_MAPVLD;

	/* force SIR reinitialization */
	si->speed = 4000000;
	pxa_irda_set_speed(si, 9600);

	printk(KERN_DEBUG "pxa_ir: irda startup\n");
}

static void pxa_irda_shutdown(struct pxa_irda *si)
{
	unsigned long flags;

	local_irq_save(flags);

	/* disable STUART and interrupt */
	STIER = 0;
	/* disable STUART SIR mode */
	STISR = 0;
	/* disable the STUART clock */
	pxa_set_cken(CKEN_STUART, 0);

	/* disable DMA */
	DCSR(si->txdma) &= ~DCSR_RUN;
	DCSR(si->rxdma) &= ~DCSR_RUN;
	/* disable FICP */
	ICCR0 = 0;
	/* disable the FICP clock */
	pxa_set_cken(CKEN_FICP, 0);

	DRCMR17 = 0;
	DRCMR18 = 0;

	local_irq_restore(flags);

	/* power off board transceiver */
	si->pdata->transceiver_mode(si->dev, IR_OFF);

	printk(KERN_DEBUG "pxa_ir: irda shutdown\n");
}

static int pxa_irda_start(struct net_device *dev)
{
	struct pxa_irda *si = netdev_priv(dev);
	int err;

	si->speed = 9600;

	err = request_irq(IRQ_STUART, pxa_irda_sir_irq, 0, dev->name, dev);
	if (err)
		goto err_irq1;

	err = request_irq(IRQ_ICP, pxa_irda_fir_irq, 0, dev->name, dev);
	if (err)
		goto err_irq2;

	/*
	 * The interrupt must remain disabled for now.
	 */
	disable_irq(IRQ_STUART);
	disable_irq(IRQ_ICP);

	err = -EBUSY;
	si->rxdma = pxa_request_dma("FICP_RX",DMA_PRIO_LOW, pxa_irda_fir_dma_rx_irq, dev);
	if (si->rxdma < 0)
		goto err_rx_dma;

	si->txdma = pxa_request_dma("FICP_TX",DMA_PRIO_LOW, pxa_irda_fir_dma_tx_irq, dev);
	if (si->txdma < 0)
		goto err_tx_dma;

	err = -ENOMEM;
	si->dma_rx_buff = dma_alloc_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT,
					     &si->dma_rx_buff_phy, GFP_KERNEL );
	if (!si->dma_rx_buff)
		goto err_dma_rx_buff;

	si->dma_tx_buff = dma_alloc_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT,
					     &si->dma_tx_buff_phy, GFP_KERNEL );
	if (!si->dma_tx_buff)
		goto err_dma_tx_buff;

	/* Setup the serial port for the initial speed. */
	pxa_irda_startup(si);

	/*
	 * Open a new IrLAP layer instance.
	 */
	si->irlap = irlap_open(dev, &si->qos, "pxa");
	err = -ENOMEM;
	if (!si->irlap)
		goto err_irlap;

	/*
	 * Now enable the interrupt and start the queue
	 */
	enable_irq(IRQ_STUART);
	enable_irq(IRQ_ICP);
	netif_start_queue(dev);

	printk(KERN_DEBUG "pxa_ir: irda driver opened\n");

	return 0;

err_irlap:
	pxa_irda_shutdown(si);
	dma_free_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT, si->dma_tx_buff, si->dma_tx_buff_phy);
err_dma_tx_buff:
	dma_free_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT, si->dma_rx_buff, si->dma_rx_buff_phy);
err_dma_rx_buff:
	pxa_free_dma(si->txdma);
err_tx_dma:
	pxa_free_dma(si->rxdma);
err_rx_dma:
	free_irq(IRQ_ICP, dev);
err_irq2:
	free_irq(IRQ_STUART, dev);
err_irq1:

	return err;
}

static int pxa_irda_stop(struct net_device *dev)
{
	struct pxa_irda *si = netdev_priv(dev);

	netif_stop_queue(dev);

	pxa_irda_shutdown(si);

	/* Stop IrLAP */
	if (si->irlap) {
		irlap_close(si->irlap);
		si->irlap = NULL;
	}

	free_irq(IRQ_STUART, dev);
	free_irq(IRQ_ICP, dev);

	pxa_free_dma(si->rxdma);
	pxa_free_dma(si->txdma);

	if (si->dma_rx_buff)
		dma_free_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT, si->dma_tx_buff, si->dma_tx_buff_phy);
	if (si->dma_tx_buff)
		dma_free_coherent(si->dev, IRDA_FRAME_SIZE_LIMIT, si->dma_rx_buff, si->dma_rx_buff_phy);

	printk(KERN_DEBUG "pxa_ir: irda driver closed\n");
	return 0;
}

static int pxa_irda_suspend(struct platform_device *_dev, pm_message_t state)
{
	struct net_device *dev = platform_get_drvdata(_dev);
	struct pxa_irda *si;

	if (dev && netif_running(dev)) {
		si = netdev_priv(dev);
		netif_device_detach(dev);
		pxa_irda_shutdown(si);
	}

	return 0;
}

static int pxa_irda_resume(struct platform_device *_dev)
{
	struct net_device *dev = platform_get_drvdata(_dev);
	struct pxa_irda *si;

	if (dev && netif_running(dev)) {
		si = netdev_priv(dev);
		pxa_irda_startup(si);
		netif_device_attach(dev);
		netif_wake_queue(dev);
	}

	return 0;
}


static int pxa_irda_init_iobuf(iobuff_t *io, int size)
{
	io->head = kmalloc(size, GFP_KERNEL | GFP_DMA);
	if (io->head != NULL) {
		io->truesize = size;
		io->in_frame = FALSE;
		io->state    = OUTSIDE_FRAME;
		io->data     = io->head;
	}
	return io->head ? 0 : -ENOMEM;
}

static int pxa_irda_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct pxa_irda *si;
	unsigned int baudrate_mask;
	int err;

	if (!pdev->dev.platform_data)
		return -ENODEV;

	err = request_mem_region(__PREG(STUART), 0x24, "IrDA") ? 0 : -EBUSY;
	if (err)
		goto err_mem_1;

	err = request_mem_region(__PREG(FICP), 0x1c, "IrDA") ? 0 : -EBUSY;
	if (err)
		goto err_mem_2;

	dev = alloc_irdadev(sizeof(struct pxa_irda));
	if (!dev)
		goto err_mem_3;

	si = netdev_priv(dev);
	si->dev = &pdev->dev;
	si->pdata = pdev->dev.platform_data;

	/*
	 * Initialise the SIR buffers
	 */
	err = pxa_irda_init_iobuf(&si->rx_buff, 14384);
	if (err)
		goto err_mem_4;
	err = pxa_irda_init_iobuf(&si->tx_buff, 4000);
	if (err)
		goto err_mem_5;

	dev->hard_start_xmit	= pxa_irda_hard_xmit;
	dev->open		= pxa_irda_start;
	dev->stop		= pxa_irda_stop;
	dev->do_ioctl		= pxa_irda_ioctl;
	dev->get_stats		= pxa_irda_stats;

	irda_init_max_qos_capabilies(&si->qos);

	baudrate_mask = 0;
	if (si->pdata->transceiver_cap & IR_SIRMODE)
		baudrate_mask |= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	if (si->pdata->transceiver_cap & IR_FIRMODE)
		baudrate_mask |= IR_4000000 << 8;

	si->qos.baud_rate.bits &= baudrate_mask;
	si->qos.min_turn_time.bits = 7;  /* 1ms or more */

	irda_qos_bits_to_value(&si->qos);

	err = register_netdev(dev);

	if (err == 0)
		dev_set_drvdata(&pdev->dev, dev);

	if (err) {
		kfree(si->tx_buff.head);
err_mem_5:
		kfree(si->rx_buff.head);
err_mem_4:
		free_netdev(dev);
err_mem_3:
		release_mem_region(__PREG(FICP), 0x1c);
err_mem_2:
		release_mem_region(__PREG(STUART), 0x24);
	}
err_mem_1:
	return err;
}

static int pxa_irda_remove(struct platform_device *_dev)
{
	struct net_device *dev = platform_get_drvdata(_dev);

	if (dev) {
		struct pxa_irda *si = netdev_priv(dev);
		unregister_netdev(dev);
		kfree(si->tx_buff.head);
		kfree(si->rx_buff.head);
		free_netdev(dev);
	}

	release_mem_region(__PREG(STUART), 0x24);
	release_mem_region(__PREG(FICP), 0x1c);

	return 0;
}

static struct platform_driver pxa_ir_driver = {
	.driver         = {
		.name   = "pxa2xx-ir",
	},
	.probe		= pxa_irda_probe,
	.remove		= pxa_irda_remove,
	.suspend	= pxa_irda_suspend,
	.resume		= pxa_irda_resume,
};

static int __init pxa_irda_init(void)
{
	return platform_driver_register(&pxa_ir_driver);
}

static void __exit pxa_irda_exit(void)
{
	platform_driver_unregister(&pxa_ir_driver);
}

module_init(pxa_irda_init);
module_exit(pxa_irda_exit);

MODULE_LICENSE("GPL");

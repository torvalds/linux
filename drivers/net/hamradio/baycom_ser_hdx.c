/*****************************************************************************/

/*
 *	baycom_ser_hdx.c  -- baycom ser12 halfduplex radio modem driver.
 *
 *	Copyright (C) 1996-2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 *
 *  Supported modems
 *
 *  ser12:  This is a very simple 1200 baud AFSK modem. The modem consists only
 *          of a modulator/demodulator chip, usually a TI TCM3105. The computer
 *          is responsible for regenerating the receiver bit clock, as well as
 *          for handling the HDLC protocol. The modem connects to a serial port,
 *          hence the name. Since the serial port is not used as an async serial
 *          port, the kernel driver for serial ports cannot be used, and this
 *          driver only supports standard serial hardware (8250, 16450, 16550A)
 *
 *
 *  Command line options (insmod command line)
 *
 *  mode     ser12    hardware DCD
 *           ser12*   software DCD
 *           ser12@   hardware/software DCD, i.e. no explicit DCD signal but hardware
 *                    mutes audio input to the modem
 *           ser12+   hardware DCD, inverted signal at DCD pin
 *  iobase   base address of the port; common values are 0x3f8, 0x2f8, 0x3e8, 0x2e8
 *  irq      interrupt line of the port; common values are 4,3
 *
 *
 *  History:
 *   0.1  26.06.1996  Adapted from baycom.c and made network driver interface
 *        18.10.1996  Changed to new user space access routines (copy_{to,from}_user)
 *   0.3  26.04.1997  init code/data tagged
 *   0.4  08.07.1997  alternative ser12 decoding algorithm (uses delta CTS ints)
 *   0.5  11.11.1997  ser12/par96 split into separate files
 *   0.6  14.04.1998  cleanups
 *   0.7  03.08.1999  adapt to Linus' new __setup/__initcall
 *   0.8  10.08.1999  use module_init/module_exit
 *   0.9  12.02.2000  adapted to softnet driver interface
 *   0.10 03.07.2000  fix interface name handling
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/jiffies.h>

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_ser_hdx";
static const char bc_drvinfo[] = KERN_INFO "baycom_ser_hdx: (C) 1996-2000 Thomas Sailer, HB9JNX/AE4WA\n"
KERN_INFO "baycom_ser_hdx: version 0.10 compiled " __TIME__ " " __DATE__ "\n";

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

static struct net_device *baycom_device[NR_PORTS];

/* --------------------------------------------------------------------- */

#define RBR(iobase) (iobase+0)
#define THR(iobase) (iobase+0)
#define IER(iobase) (iobase+1)
#define IIR(iobase) (iobase+2)
#define FCR(iobase) (iobase+2)
#define LCR(iobase) (iobase+3)
#define MCR(iobase) (iobase+4)
#define LSR(iobase) (iobase+5)
#define MSR(iobase) (iobase+6)
#define SCR(iobase) (iobase+7)
#define DLL(iobase) (iobase+0)
#define DLM(iobase) (iobase+1)

#define SER12_EXTENT 8

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board.
 */

struct baycom_state {
	struct hdlcdrv_state hdrv;

	int opt_dcd;

	struct modem_state {
		short arb_divider;
		unsigned char flags;
		unsigned int shreg;
		struct modem_state_ser12 {
			unsigned char tx_bit;
			int dcd_sum0, dcd_sum1, dcd_sum2;
			unsigned char last_sample;
			unsigned char last_rxbit;
			unsigned int dcd_shreg;
			unsigned int dcd_time;
			unsigned int bit_pll;
			unsigned char interm_sample;
		} ser12;
	} modem;

#ifdef BAYCOM_DEBUG
	struct debug_vals {
		unsigned long last_jiffies;
		unsigned cur_intcnt;
		unsigned last_intcnt;
		int cur_pllcorr;
		int last_pllcorr;
	} debug_vals;
#endif /* BAYCOM_DEBUG */
};

/* --------------------------------------------------------------------- */

static inline void baycom_int_freq(struct baycom_state *bc)
{
#ifdef BAYCOM_DEBUG
	unsigned long cur_jiffies = jiffies;
	/*
	 * measure the interrupt frequency
	 */
	bc->debug_vals.cur_intcnt++;
	if (time_after_eq(cur_jiffies, bc->debug_vals.last_jiffies + HZ)) {
		bc->debug_vals.last_jiffies = cur_jiffies;
		bc->debug_vals.last_intcnt = bc->debug_vals.cur_intcnt;
		bc->debug_vals.cur_intcnt = 0;
		bc->debug_vals.last_pllcorr = bc->debug_vals.cur_pllcorr;
		bc->debug_vals.cur_pllcorr = 0;
	}
#endif /* BAYCOM_DEBUG */
}

/* --------------------------------------------------------------------- */
/*
 * ===================== SER12 specific routines =========================
 */

static inline void ser12_set_divisor(struct net_device *dev,
				     unsigned char divisor)
{
	outb(0x81, LCR(dev->base_addr));	/* DLAB = 1 */
	outb(divisor, DLL(dev->base_addr));
	outb(0, DLM(dev->base_addr));
	outb(0x01, LCR(dev->base_addr));	/* word length = 6 */
	/*
	 * make sure the next interrupt is generated;
	 * 0 must be used to power the modem; the modem draws its
	 * power from the TxD line
	 */
	outb(0x00, THR(dev->base_addr));
	/*
	 * it is important not to set the divider while transmitting;
	 * this reportedly makes some UARTs generating interrupts
	 * in the hundredthousands per second region
	 * Reported by: Ignacio.Arenaza@studi.epfl.ch (Ignacio Arenaza Nuno)
	 */
}

/* --------------------------------------------------------------------- */

/*
 * must call the TX arbitrator every 10ms
 */
#define SER12_ARB_DIVIDER(bc)  (bc->opt_dcd ? 24 : 36)
			       
#define SER12_DCD_INTERVAL(bc) (bc->opt_dcd ? 12 : 240)

static inline void ser12_tx(struct net_device *dev, struct baycom_state *bc)
{
	/* one interrupt per channel bit */
	ser12_set_divisor(dev, 12);
	/*
	 * first output the last bit (!) then call HDLC transmitter,
	 * since this may take quite long
	 */
	outb(0x0e | (!!bc->modem.ser12.tx_bit), MCR(dev->base_addr));
	if (bc->modem.shreg <= 1)
		bc->modem.shreg = 0x10000 | hdlcdrv_getbits(&bc->hdrv);
	bc->modem.ser12.tx_bit = !(bc->modem.ser12.tx_bit ^
				   (bc->modem.shreg & 1));
	bc->modem.shreg >>= 1;
}

/* --------------------------------------------------------------------- */

static inline void ser12_rx(struct net_device *dev, struct baycom_state *bc)
{
	unsigned char cur_s;
	/*
	 * do demodulator
	 */
	cur_s = inb(MSR(dev->base_addr)) & 0x10;	/* the CTS line */
	hdlcdrv_channelbit(&bc->hdrv, cur_s);
	bc->modem.ser12.dcd_shreg = (bc->modem.ser12.dcd_shreg << 1) |
		(cur_s != bc->modem.ser12.last_sample);
	bc->modem.ser12.last_sample = cur_s;
	if(bc->modem.ser12.dcd_shreg & 1) {
		if (!bc->opt_dcd) {
			unsigned int dcdspos, dcdsneg;

			dcdspos = dcdsneg = 0;
			dcdspos += ((bc->modem.ser12.dcd_shreg >> 1) & 1);
			if (!(bc->modem.ser12.dcd_shreg & 0x7ffffffe))
				dcdspos += 2;
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 2) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 3) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 4) & 1);

			bc->modem.ser12.dcd_sum0 += 16*dcdspos - dcdsneg;
		} else
			bc->modem.ser12.dcd_sum0--;
	}
	if(!bc->modem.ser12.dcd_time) {
		hdlcdrv_setdcd(&bc->hdrv, (bc->modem.ser12.dcd_sum0 +
					   bc->modem.ser12.dcd_sum1 +
					   bc->modem.ser12.dcd_sum2) < 0);
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		/* offset to ensure DCD off on silent input */
		bc->modem.ser12.dcd_sum0 = 2;
		bc->modem.ser12.dcd_time = SER12_DCD_INTERVAL(bc);
	}
	bc->modem.ser12.dcd_time--;
	if (!bc->opt_dcd) {
		/*
		 * PLL code for the improved software DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(dev, 4);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 7) {
			case 1: /* transition too late */
				ser12_set_divisor(dev, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 4:	/* transition too early */
				ser12_set_divisor(dev, 3);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(dev, 4);
				break;
			}
			bc->modem.shreg >>= 1;
			if (bc->modem.ser12.last_sample ==
			    bc->modem.ser12.last_rxbit)
				bc->modem.shreg |= 0x10000;
			bc->modem.ser12.last_rxbit =
				bc->modem.ser12.last_sample;
		}
		if (++bc->modem.ser12.interm_sample >= 3)
			bc->modem.ser12.interm_sample = 0;
		/*
		 * DCD stuff
		 */
		if (bc->modem.ser12.dcd_shreg & 1) {
			unsigned int dcdspos, dcdsneg;

			dcdspos = dcdsneg = 0;
			dcdspos += ((bc->modem.ser12.dcd_shreg >> 1) & 1);
			dcdspos += (!(bc->modem.ser12.dcd_shreg & 0x7ffffffe))
				<< 1;
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 2) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 3) & 1);
			dcdsneg += ((bc->modem.ser12.dcd_shreg >> 4) & 1);

			bc->modem.ser12.dcd_sum0 += 16*dcdspos - dcdsneg;
		}
	} else {
		/*
		 * PLL algorithm for the hardware squelch DCD algorithm
		 */
		if (bc->modem.ser12.interm_sample) {
			/*
			 * intermediate sample; set timing correction to normal
			 */
			ser12_set_divisor(dev, 6);
		} else {
			/*
			 * do PLL correction and call HDLC receiver
			 */
			switch (bc->modem.ser12.dcd_shreg & 3) {
			case 1: /* transition too late */
				ser12_set_divisor(dev, 7);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr++;
#endif /* BAYCOM_DEBUG */
				break;
			case 2:	/* transition too early */
				ser12_set_divisor(dev, 5);
#ifdef BAYCOM_DEBUG
				bc->debug_vals.cur_pllcorr--;
#endif /* BAYCOM_DEBUG */
				break;
			default:
				ser12_set_divisor(dev, 6);
				break;
			}
			bc->modem.shreg >>= 1;
			if (bc->modem.ser12.last_sample ==
			    bc->modem.ser12.last_rxbit)
				bc->modem.shreg |= 0x10000;
			bc->modem.ser12.last_rxbit =
				bc->modem.ser12.last_sample;
		}
		bc->modem.ser12.interm_sample = !bc->modem.ser12.interm_sample;
		/*
		 * DCD stuff
		 */
		bc->modem.ser12.dcd_sum0 -= (bc->modem.ser12.dcd_shreg & 1);
	}
	outb(0x0d, MCR(dev->base_addr));		/* transmitter off */
	if (bc->modem.shreg & 1) {
		hdlcdrv_putbits(&bc->hdrv, bc->modem.shreg >> 1);
		bc->modem.shreg = 0x10000;
	}
	if(!bc->modem.ser12.dcd_time) {
		if (bc->opt_dcd & 1) 
			hdlcdrv_setdcd(&bc->hdrv, !((inb(MSR(dev->base_addr)) ^ bc->opt_dcd) & 0x80));
		else
			hdlcdrv_setdcd(&bc->hdrv, (bc->modem.ser12.dcd_sum0 +
						   bc->modem.ser12.dcd_sum1 +
						   bc->modem.ser12.dcd_sum2) < 0);
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		/* offset to ensure DCD off on silent input */
		bc->modem.ser12.dcd_sum0 = 2;
		bc->modem.ser12.dcd_time = SER12_DCD_INTERVAL(bc);
	}
	bc->modem.ser12.dcd_time--;
}

/* --------------------------------------------------------------------- */

static irqreturn_t ser12_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct baycom_state *bc = netdev_priv(dev);
	unsigned char iir;

	if (!dev || !bc || bc->hdrv.magic != HDLCDRV_MAGIC)
		return IRQ_NONE;
	/* fast way out */
	if ((iir = inb(IIR(dev->base_addr))) & 1)
		return IRQ_NONE;
	baycom_int_freq(bc);
	do {
		switch (iir & 6) {
		case 6:
			inb(LSR(dev->base_addr));
			break;
			
		case 4:
			inb(RBR(dev->base_addr));
			break;
			
		case 2:
			/*
			 * check if transmitter active
			 */
			if (hdlcdrv_ptt(&bc->hdrv))
				ser12_tx(dev, bc);
			else {
				ser12_rx(dev, bc);
				bc->modem.arb_divider--;
			}
			outb(0x00, THR(dev->base_addr));
			break;
			
		default:
			inb(MSR(dev->base_addr));
			break;
		}
		iir = inb(IIR(dev->base_addr));
	} while (!(iir & 1));
	if (bc->modem.arb_divider <= 0) {
		bc->modem.arb_divider = SER12_ARB_DIVIDER(bc);
		local_irq_enable();
		hdlcdrv_arbitrate(dev, &bc->hdrv);
	}
	local_irq_enable();
	hdlcdrv_transmitter(dev, &bc->hdrv);
	hdlcdrv_receiver(dev, &bc->hdrv);
	local_irq_disable();
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

enum uart { c_uart_unknown, c_uart_8250,
	    c_uart_16450, c_uart_16550, c_uart_16550A};
static const char *uart_str[] = { 
	"unknown", "8250", "16450", "16550", "16550A" 
};

static enum uart ser12_check_uart(unsigned int iobase)
{
	unsigned char b1,b2,b3;
	enum uart u;
	enum uart uart_tab[] =
		{ c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A };

	b1 = inb(MCR(iobase));
	outb(b1 | 0x10, MCR(iobase));	/* loopback mode */
	b2 = inb(MSR(iobase));
	outb(0x1a, MCR(iobase));
	b3 = inb(MSR(iobase)) & 0xf0;
	outb(b1, MCR(iobase));			/* restore old values */
	outb(b2, MSR(iobase));
	if (b3 != 0x90)
		return c_uart_unknown;
	inb(RBR(iobase));
	inb(RBR(iobase));
	outb(0x01, FCR(iobase));		/* enable FIFOs */
	u = uart_tab[(inb(IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, SCR(iobase));
		b1 = inb(SCR(iobase));
		outb(0xa5, SCR(iobase));
		b2 = inb(SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5))
			u = c_uart_8250;
	}
	return u;
}

/* --------------------------------------------------------------------- */

static int ser12_open(struct net_device *dev)
{
	struct baycom_state *bc = netdev_priv(dev);
	enum uart u;

	if (!dev || !bc)
		return -ENXIO;
	if (!dev->base_addr || dev->base_addr > 0x1000-SER12_EXTENT ||
	    dev->irq < 2 || dev->irq > 15)
		return -ENXIO;
	if (!request_region(dev->base_addr, SER12_EXTENT, "baycom_ser12"))
		return -EACCES;
	memset(&bc->modem, 0, sizeof(bc->modem));
	bc->hdrv.par.bitrate = 1200;
	if ((u = ser12_check_uart(dev->base_addr)) == c_uart_unknown) {
		release_region(dev->base_addr, SER12_EXTENT);       
		return -EIO;
	}
	outb(0, FCR(dev->base_addr));  /* disable FIFOs */
	outb(0x0d, MCR(dev->base_addr));
	outb(0, IER(dev->base_addr));
	if (request_irq(dev->irq, ser12_interrupt, SA_INTERRUPT | SA_SHIRQ,
			"baycom_ser12", dev)) {
		release_region(dev->base_addr, SER12_EXTENT);       
		return -EBUSY;
	}
	/*
	 * enable transmitter empty interrupt
	 */
	outb(2, IER(dev->base_addr));
	/*
	 * set the SIO to 6 Bits/character and 19200 or 28800 baud, so that
	 * we get exactly (hopefully) 2 or 3 interrupts per radio symbol,
	 * depending on the usage of the software DCD routine
	 */
	ser12_set_divisor(dev, bc->opt_dcd ? 6 : 4);
	printk(KERN_INFO "%s: ser12 at iobase 0x%lx irq %u uart %s\n", 
	       bc_drvname, dev->base_addr, dev->irq, uart_str[u]);
	return 0;
}

/* --------------------------------------------------------------------- */

static int ser12_close(struct net_device *dev)
{
	struct baycom_state *bc = netdev_priv(dev);

	if (!dev || !bc)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	outb(0, IER(dev->base_addr));
	outb(1, MCR(dev->base_addr));
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, SER12_EXTENT);
	printk(KERN_INFO "%s: close ser12 at iobase 0x%lx irq %u\n",
	       bc_drvname, dev->base_addr, dev->irq);
	return 0;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== hdlcdrv driver interface =========================
 */

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct net_device *dev, struct ifreq *ifr,
			struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

static struct hdlcdrv_ops ser12_ops = {
	.drvname = bc_drvname,
	.drvinfo = bc_drvinfo,
	.open    = ser12_open,
	.close   = ser12_close,
	.ioctl   = baycom_ioctl,
};

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	if (strchr(modestr, '*'))
		bc->opt_dcd = 0;
	else if (strchr(modestr, '+'))
		bc->opt_dcd = -1;
	else if (strchr(modestr, '@'))
		bc->opt_dcd = -2;
	else
		bc->opt_dcd = 1;
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct net_device *dev, struct ifreq *ifr,
			struct hdlcdrv_ioctl *hi, int cmd)
{
	struct baycom_state *bc;
	struct baycom_ioctl bi;

	if (!dev)
		return -EINVAL;

	bc = netdev_priv(dev);
	BUG_ON(bc->hdrv.magic != HDLCDRV_MAGIC);

	if (cmd != SIOCDEVPRIVATE)
		return -ENOIOCTLCMD;
	switch (hi->cmd) {
	default:
		break;

	case HDLCDRVCTL_GETMODE:
		strcpy(hi->data.modename, "ser12");
		if (bc->opt_dcd <= 0)
			strcat(hi->data.modename, (!bc->opt_dcd) ? "*" : (bc->opt_dcd == -2) ? "@" : "+");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (netif_running(dev) || !capable(CAP_NET_ADMIN))
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return baycom_setmode(bc, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		strcpy(hi->data.modename, "ser12");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_MODEMPARMASK:
		return HDLCDRV_PARMASK_IOBASE | HDLCDRV_PARMASK_IRQ;

	}

	if (copy_from_user(&bi, ifr->ifr_data, sizeof(bi)))
		return -EFAULT;
	switch (bi.cmd) {
	default:
		return -ENOIOCTLCMD;

#ifdef BAYCOM_DEBUG
	case BAYCOMCTL_GETDEBUG:
		bi.data.dbg.debug1 = bc->hdrv.ptt_keyed;
		bi.data.dbg.debug2 = bc->debug_vals.last_intcnt;
		bi.data.dbg.debug3 = bc->debug_vals.last_pllcorr;
		break;
#endif /* BAYCOM_DEBUG */

	}
	if (copy_to_user(ifr->ifr_data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

/*
 * command line settable parameters
 */
static char *mode[NR_PORTS] = { "ser12*", };
static int iobase[NR_PORTS] = { 0x3f8, };
static int irq[NR_PORTS] = { 4, };

module_param_array(mode, charp, NULL, 0);
MODULE_PARM_DESC(mode, "baycom operating mode; * for software DCD");
module_param_array(iobase, int, NULL, 0);
MODULE_PARM_DESC(iobase, "baycom io base address");
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "baycom irq number");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom ser12 half duplex amateur radio modem driver");
MODULE_LICENSE("GPL");

/* --------------------------------------------------------------------- */

static int __init init_baycomserhdx(void)
{
	int i, found = 0;
	char set_hw = 1;

	printk(bc_drvinfo);
	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev;
		struct baycom_state *bc;
		char ifname[IFNAMSIZ];

		sprintf(ifname, "bcsh%d", i);

		if (!mode[i])
			set_hw = 0;
		if (!set_hw)
			iobase[i] = irq[i] = 0;

		dev = hdlcdrv_register(&ser12_ops, 
				       sizeof(struct baycom_state),
				       ifname, iobase[i], irq[i], 0);
		if (IS_ERR(dev)) 
			break;

		bc = netdev_priv(dev);
		if (set_hw && baycom_setmode(bc, mode[i]))
			set_hw = 0;
		found++;
		baycom_device[i] = dev;
	}

	if (!found)
		return -ENXIO;
	return 0;
}

static void __exit cleanup_baycomserhdx(void)
{
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = baycom_device[i];

		if (dev)
			hdlcdrv_unregister(dev);
	}
}

module_init(init_baycomserhdx);
module_exit(cleanup_baycomserhdx);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/*
 * format: baycom_ser_hdx=io,irq,mode
 * mode: ser12    hardware DCD
 *       ser12*   software DCD
 *       ser12@   hardware/software DCD, i.e. no explicit DCD signal but hardware
 *                mutes audio input to the modem
 *       ser12+   hardware DCD, inverted signal at DCD pin
 */

static int __init baycom_ser_hdx_setup(char *str)
{
        static unsigned nr_dev;
	int ints[3];

        if (nr_dev >= NR_PORTS)
                return 0;
	str = get_options(str, 3, ints);
	if (ints[0] < 2)
		return 0;
	mode[nr_dev] = str;
	iobase[nr_dev] = ints[1];
	irq[nr_dev] = ints[2];
	nr_dev++;
	return 1;
}

__setup("baycom_ser_hdx=", baycom_ser_hdx_setup);

#endif /* MODULE */
/* --------------------------------------------------------------------- */

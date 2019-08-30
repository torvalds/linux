// SPDX-License-Identifier: GPL-2.0-or-later
/*****************************************************************************/

/*
 *	baycom_ser_fdx.c  -- baycom ser12 fullduplex radio modem driver.
 *
 *	Copyright (C) 1996-2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
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
 *          This modem usually draws its supply current out of the otherwise unused
 *          TXD pin of the serial port. Thus a contiguous stream of 0x00-bytes
 *          is transmitted to achieve a positive supply voltage.
 *
 *  hsk:    This is a 4800 baud FSK modem, designed for TNC use. It works fine
 *          in 'baycom-mode' :-)  In contrast to the TCM3105 modem, power is
 *          externally supplied. So there's no need to provide the 0x00-byte-stream
 *          when receiving or idle, which drastically reduces interrupt load.
 *
 *  Command line options (insmod command line)
 *
 *  mode     ser#    hardware DCD
 *           ser#*   software DCD
 *           ser#+   hardware DCD, inverted signal at DCD pin
 *           '#' denotes the baud rate / 100, eg. ser12* is '1200 baud, soft DCD'
 *  iobase   base address of the port; common values are 0x3f8, 0x2f8, 0x3e8, 0x2e8
 *  baud     baud rate (between 300 and 4800)
 *  irq      interrupt line of the port; common values are 4,3
 *
 *  History:
 *   0.1  26.06.1996  Adapted from baycom.c and made network driver interface
 *        18.10.1996  Changed to new user space access routines (copy_{to,from}_user)
 *   0.3  26.04.1997  init code/data tagged
 *   0.4  08.07.1997  alternative ser12 decoding algorithm (uses delta CTS ints)
 *   0.5  11.11.1997  ser12/par96 split into separate files
 *   0.6  24.01.1998  Thorsten Kranzkowski, dl8bcu and Thomas Sailer:
 *                    reduced interrupt load in transmit case
 *                    reworked receiver
 *   0.7  03.08.1999  adapt to Linus' new __setup/__initcall
 *   0.8  10.08.1999  use module_init/module_exit
 *   0.9  12.02.2000  adapted to softnet driver interface
 *   0.10 03.07.2000  fix interface name handling
 */

/*****************************************************************************/

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/jiffies.h>
#include <linux/time64.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_ser_fdx";
static const char bc_drvinfo[] = KERN_INFO "baycom_ser_fdx: (C) 1996-2000 Thomas Sailer, HB9JNX/AE4WA\n"
"baycom_ser_fdx: version 0.10\n";

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

	unsigned int baud, baud_us, baud_arbdiv, baud_uartdiv, baud_dcdtimeout;
	int opt_dcd;

	struct modem_state {
		unsigned char flags;
		unsigned char ptt;
		unsigned int shreg;
		struct modem_state_ser12 {
			unsigned char tx_bit;
			unsigned char last_rxbit;
			int dcd_sum0, dcd_sum1, dcd_sum2;
			int dcd_time;
			unsigned int pll_time;
			unsigned int txshreg;
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

/* --------------------------------------------------------------------- */

static inline void ser12_set_divisor(struct net_device *dev,
                                     unsigned int divisor)
{
        outb(0x81, LCR(dev->base_addr));        /* DLAB = 1 */
        outb(divisor, DLL(dev->base_addr));
        outb(divisor >> 8, DLM(dev->base_addr));
        outb(0x01, LCR(dev->base_addr));        /* word length = 6 */
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

static __inline__ void ser12_rx(struct net_device *dev, struct baycom_state *bc, struct timespec64 *ts, unsigned char curs)
{
	int timediff;
	int bdus8 = bc->baud_us >> 3;
	int bdus4 = bc->baud_us >> 2;
	int bdus2 = bc->baud_us >> 1;

	timediff = 1000000 + ts->tv_nsec / NSEC_PER_USEC -
					bc->modem.ser12.pll_time;
	while (timediff >= 500000)
		timediff -= 1000000;
	while (timediff >= bdus2) {
		timediff -= bc->baud_us;
		bc->modem.ser12.pll_time += bc->baud_us;
		bc->modem.ser12.dcd_time--;
		/* first check if there is room to add a bit */
		if (bc->modem.shreg & 1) {
			hdlcdrv_putbits(&bc->hdrv, (bc->modem.shreg >> 1) ^ 0xffff);
			bc->modem.shreg = 0x10000;
		}
		/* add a one bit */
		bc->modem.shreg >>= 1;
	}
	if (bc->modem.ser12.dcd_time <= 0) {
		if (!bc->opt_dcd)
			hdlcdrv_setdcd(&bc->hdrv, (bc->modem.ser12.dcd_sum0 + 
						   bc->modem.ser12.dcd_sum1 + 
						   bc->modem.ser12.dcd_sum2) < 0);
		bc->modem.ser12.dcd_sum2 = bc->modem.ser12.dcd_sum1;
		bc->modem.ser12.dcd_sum1 = bc->modem.ser12.dcd_sum0;
		bc->modem.ser12.dcd_sum0 = 2; /* slight bias */
		bc->modem.ser12.dcd_time += 120;
	}
	if (bc->modem.ser12.last_rxbit != curs) {
		bc->modem.ser12.last_rxbit = curs;
		bc->modem.shreg |= 0x10000;
		/* adjust the PLL */
		if (timediff > 0)
			bc->modem.ser12.pll_time += bdus8;
		else
			bc->modem.ser12.pll_time += 1000000 - bdus8;
		/* update DCD */
		if (abs(timediff) > bdus4)
			bc->modem.ser12.dcd_sum0 += 4;
		else
			bc->modem.ser12.dcd_sum0--;
#ifdef BAYCOM_DEBUG
		bc->debug_vals.cur_pllcorr = timediff;
#endif /* BAYCOM_DEBUG */
	}
	while (bc->modem.ser12.pll_time >= 1000000)
		bc->modem.ser12.pll_time -= 1000000;
}

/* --------------------------------------------------------------------- */

static irqreturn_t ser12_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct baycom_state *bc = netdev_priv(dev);
	struct timespec64 ts;
	unsigned char iir, msr;
	unsigned int txcount = 0;

	if (!bc || bc->hdrv.magic != HDLCDRV_MAGIC)
		return IRQ_NONE;
	/* fast way out for shared irq */
	if ((iir = inb(IIR(dev->base_addr))) & 1) 	
		return IRQ_NONE;
	/* get current time */
	ktime_get_ts64(&ts);
	msr = inb(MSR(dev->base_addr));
	/* delta DCD */
	if ((msr & 8) && bc->opt_dcd)
		hdlcdrv_setdcd(&bc->hdrv, !((msr ^ bc->opt_dcd) & 0x80));
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
			 * make sure the next interrupt is generated;
			 * 0 must be used to power the modem; the modem draws its
			 * power from the TxD line
			 */
			outb(0x00, THR(dev->base_addr));
			baycom_int_freq(bc);
			txcount++;
			/*
			 * first output the last bit (!) then call HDLC transmitter,
			 * since this may take quite long
			 */
			if (bc->modem.ptt)
				outb(0x0e | (!!bc->modem.ser12.tx_bit), MCR(dev->base_addr));
			else
				outb(0x0d, MCR(dev->base_addr));       /* transmitter off */
			break;
			
		default:
			msr = inb(MSR(dev->base_addr));
			/* delta DCD */
			if ((msr & 8) && bc->opt_dcd) 
				hdlcdrv_setdcd(&bc->hdrv, !((msr ^ bc->opt_dcd) & 0x80));
			break;
		}
		iir = inb(IIR(dev->base_addr));
	} while (!(iir & 1));
	ser12_rx(dev, bc, &ts, msr & 0x10); /* CTS */
	if (bc->modem.ptt && txcount) {
		if (bc->modem.ser12.txshreg <= 1) {
			bc->modem.ser12.txshreg = 0x10000 | hdlcdrv_getbits(&bc->hdrv);
			if (!hdlcdrv_ptt(&bc->hdrv)) {
				ser12_set_divisor(dev, 115200/100/8);
				bc->modem.ptt = 0;
				goto end_transmit;
			}
		}
		bc->modem.ser12.tx_bit = !(bc->modem.ser12.tx_bit ^ (bc->modem.ser12.txshreg & 1));
		bc->modem.ser12.txshreg >>= 1;
	}
 end_transmit:
	local_irq_enable();
	if (!bc->modem.ptt && txcount) {
		hdlcdrv_arbitrate(dev, &bc->hdrv);
		if (hdlcdrv_ptt(&bc->hdrv)) {
			ser12_set_divisor(dev, bc->baud_uartdiv);
			bc->modem.ser12.txshreg = 1;
			bc->modem.ptt = 1;
		}
	}
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
	if (!dev->base_addr || dev->base_addr > 0xffff-SER12_EXTENT ||
	    dev->irq < 2 || dev->irq > nr_irqs) {
		printk(KERN_INFO "baycom_ser_fdx: invalid portnumber (max %u) "
				"or irq (2 <= irq <= %d)\n",
				0xffff-SER12_EXTENT, nr_irqs);
		return -ENXIO;
	}
	if (bc->baud < 300 || bc->baud > 4800) {
		printk(KERN_INFO "baycom_ser_fdx: invalid baudrate "
				"(300...4800)\n");
		return -EINVAL;
	}
	if (!request_region(dev->base_addr, SER12_EXTENT, "baycom_ser_fdx")) {
		printk(KERN_WARNING "BAYCOM_SER_FSX: I/O port 0x%04lx busy\n",
		       dev->base_addr);
		return -EACCES;
	}
	memset(&bc->modem, 0, sizeof(bc->modem));
	bc->hdrv.par.bitrate = bc->baud;
	bc->baud_us = 1000000/bc->baud;
	bc->baud_uartdiv = (115200/8)/bc->baud;
	if ((u = ser12_check_uart(dev->base_addr)) == c_uart_unknown){
		release_region(dev->base_addr, SER12_EXTENT);
		return -EIO;
	}
	outb(0, FCR(dev->base_addr));  /* disable FIFOs */
	outb(0x0d, MCR(dev->base_addr));
	outb(0, IER(dev->base_addr));
	if (request_irq(dev->irq, ser12_interrupt, IRQF_SHARED,
			"baycom_ser_fdx", dev)) {
		release_region(dev->base_addr, SER12_EXTENT);
		return -EBUSY;
	}
	/*
	 * set the SIO to 6 Bits/character; during receive,
	 * the baud rate is set to produce 100 ints/sec
	 * to feed the channel arbitration process,
	 * during transmit to baud ints/sec to run
	 * the transmitter
	 */
	ser12_set_divisor(dev, 115200/100/8);
	/*
	 * enable transmitter empty interrupt and modem status interrupt
	 */
	outb(0x0a, IER(dev->base_addr));
	/*
	 * make sure the next interrupt is generated;
	 * 0 must be used to power the modem; the modem draws its
	 * power from the TxD line
	 */
	outb(0x00, THR(dev->base_addr));
	hdlcdrv_setdcd(&bc->hdrv, 0);
	printk(KERN_INFO "%s: ser_fdx at iobase 0x%lx irq %u baud %u uart %s\n",
	       bc_drvname, dev->base_addr, dev->irq, bc->baud, uart_str[u]);
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
	printk(KERN_INFO "%s: close ser_fdx at iobase 0x%lx irq %u\n",
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

static const struct hdlcdrv_ops ser12_ops = {
	.drvname = bc_drvname,
	.drvinfo = bc_drvinfo,
	.open    = ser12_open,
	.close   = ser12_close,
	.ioctl   = baycom_ioctl,
};

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	unsigned int baud;

	if (!strncmp(modestr, "ser", 3)) {
		baud = simple_strtoul(modestr+3, NULL, 10);
		if (baud >= 3 && baud <= 48)
			bc->baud = baud*100;
	}
	if (strchr(modestr, '*'))
		bc->opt_dcd = 0;
	else if (strchr(modestr, '+'))
		bc->opt_dcd = -1;
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
		sprintf(hi->data.modename, "ser%u", bc->baud / 100);
		if (bc->opt_dcd <= 0)
			strcat(hi->data.modename, (!bc->opt_dcd) ? "*" : "+");
		if (copy_to_user(ifr->ifr_data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (netif_running(dev) || !capable(CAP_NET_ADMIN))
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return baycom_setmode(bc, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		strcpy(hi->data.modename, "ser12,ser3,ser24");
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
static int baud[NR_PORTS] = { [0 ... NR_PORTS-1] = 1200 };

module_param_array(mode, charp, NULL, 0);
MODULE_PARM_DESC(mode, "baycom operating mode; * for software DCD");
module_param_hw_array(iobase, int, ioport, NULL, 0);
MODULE_PARM_DESC(iobase, "baycom io base address");
module_param_hw_array(irq, int, irq, NULL, 0);
MODULE_PARM_DESC(irq, "baycom irq number");
module_param_array(baud, int, NULL, 0);
MODULE_PARM_DESC(baud, "baycom baud rate (300 to 4800)");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom ser12 full duplex amateur radio modem driver");
MODULE_LICENSE("GPL");

/* --------------------------------------------------------------------- */

static int __init init_baycomserfdx(void)
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

		sprintf(ifname, "bcsf%d", i);

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
		bc->baud = baud[i];
		found++;
		baycom_device[i] = dev;
	}

	if (!found)
		return -ENXIO;
	return 0;
}

static void __exit cleanup_baycomserfdx(void)
{
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = baycom_device[i];
		if (dev) 
			hdlcdrv_unregister(dev);
	}
}

module_init(init_baycomserfdx);
module_exit(cleanup_baycomserfdx);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/*
 * format: baycom_ser_fdx=io,irq,mode
 * mode: ser#    hardware DCD
 *       ser#*   software DCD
 *       ser#+   hardware DCD, inverted signal at DCD pin
 * '#' denotes the baud rate / 100, eg. ser12* is '1200 baud, soft DCD'
 */

static int __init baycom_ser_fdx_setup(char *str)
{
        static unsigned nr_dev;
        int ints[4];

        if (nr_dev >= NR_PORTS)
                return 0;
        str = get_options(str, 4, ints);
        if (ints[0] < 2)
                return 0;
        mode[nr_dev] = str;
        iobase[nr_dev] = ints[1];
        irq[nr_dev] = ints[2];
	if (ints[0] >= 3)
		baud[nr_dev] = ints[3];
	nr_dev++;
	return 1;
}

__setup("baycom_ser_fdx=", baycom_ser_fdx_setup);

#endif /* MODULE */
/* --------------------------------------------------------------------- */

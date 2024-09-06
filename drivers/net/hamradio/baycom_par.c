// SPDX-License-Identifier: GPL-2.0-or-later
/*****************************************************************************/

/*
 *	baycom_par.c  -- baycom par96 and picpar radio modem driver.
 *
 *	Copyright (C) 1996-2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 *  Supported modems
 *
 *  par96:  This is a modem for 9600 baud FSK compatible to the G3RUH standard.
 *          The modem does all the filtering and regenerates the receiver clock.
 *          Data is transferred from and to the PC via a shift register.
 *          The shift register is filled with 16 bits and an interrupt is
 *          signalled. The PC then empties the shift register in a burst. This
 *          modem connects to the parallel port, hence the name. The modem
 *          leaves the implementation of the HDLC protocol and the scrambler
 *          polynomial to the PC. This modem is no longer available (at least
 *          from Baycom) and has been replaced by the PICPAR modem (see below).
 *          You may however still build one from the schematics published in
 *          cq-DL :-).
 *
 *  picpar: This is a redesign of the par96 modem by Henning Rech, DF9IC. The
 *          modem is protocol compatible to par96, but uses only three low
 *          power ICs and can therefore be fed from the parallel port and
 *          does not require an additional power supply. It features
 *          built in DCD circuitry. The driver should therefore be configured
 *          for hardware DCD.
 *
 *  Command line options (insmod command line)
 *
 *  mode     driver mode string. Valid choices are par96 and picpar.
 *  iobase   base address of the port; common values are 0x378, 0x278, 0x3bc
 *
 *  History:
 *   0.1  26.06.1996  Adapted from baycom.c and made network driver interface
 *        18.10.1996  Changed to new user space access routines (copy_{to,from}_user)
 *   0.3  26.04.1997  init code/data tagged
 *   0.4  08.07.1997  alternative ser12 decoding algorithm (uses delta CTS ints)
 *   0.5  11.11.1997  split into separate files for ser12/par96
 *   0.6  03.08.1999  adapt to Linus' new __setup/__initcall
 *                    removed some pre-2.2 kernel compatibility cruft
 *   0.7  10.08.1999  Check if parport can do SPP and is safe to access during interrupt contexts
 *   0.8  12.02.2000  adapted to softnet driver interface
 *                    removed direct parport access, uses parport driver methods
 *   0.9  03.07.2000  fix interface name handling
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/hdlcdrv.h>
#include <linux/baycom.h>
#include <linux/parport.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <linux/uaccess.h>

/* --------------------------------------------------------------------- */

#define BAYCOM_DEBUG

/*
 * modem options; bit mask
 */
#define BAYCOM_OPTIONS_SOFTDCD  1

/* --------------------------------------------------------------------- */

static const char bc_drvname[] = "baycom_par";
static const char bc_drvinfo[] = KERN_INFO "baycom_par: (C) 1996-2000 Thomas Sailer, HB9JNX/AE4WA\n"
"baycom_par: version 0.9\n";

/* --------------------------------------------------------------------- */

#define NR_PORTS 4

static struct net_device *baycom_device[NR_PORTS];

/* --------------------------------------------------------------------- */

#define PAR96_BURSTBITS 16
#define PAR96_BURST     4
#define PAR96_PTT       2
#define PAR96_TXBIT     1
#define PAR96_ACK       0x40
#define PAR96_RXBIT     0x20
#define PAR96_DCD       0x10
#define PAR97_POWER     0xf8

/* ---------------------------------------------------------------------- */
/*
 * Information that need to be kept for each board.
 */

struct baycom_state {
	struct hdlcdrv_state hdrv;

	struct pardevice *pdev;
	unsigned int options;

	struct modem_state {
		short arb_divider;
		unsigned char flags;
		unsigned int shreg;
		struct modem_state_par96 {
			int dcd_count;
			unsigned int dcd_shreg;
			unsigned long descram;
			unsigned long scram;
		} par96;
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
 * ===================== PAR96 specific routines =========================
 */

#define PAR96_DESCRAM_TAP1 0x20000
#define PAR96_DESCRAM_TAP2 0x01000
#define PAR96_DESCRAM_TAP3 0x00001

#define PAR96_DESCRAM_TAPSH1 17
#define PAR96_DESCRAM_TAPSH2 12
#define PAR96_DESCRAM_TAPSH3 0

#define PAR96_SCRAM_TAP1 0x20000 /* X^17 */
#define PAR96_SCRAM_TAPN 0x00021 /* X^0+X^5 */

/* --------------------------------------------------------------------- */

static inline void par96_tx(struct net_device *dev, struct baycom_state *bc)
{
	int i;
	unsigned int data = hdlcdrv_getbits(&bc->hdrv);
	struct parport *pp = bc->pdev->port;

	for(i = 0; i < PAR96_BURSTBITS; i++, data >>= 1) {
		unsigned char val = PAR97_POWER;
		bc->modem.par96.scram = ((bc->modem.par96.scram << 1) |
					 (bc->modem.par96.scram & 1));
		if (!(data & 1))
			bc->modem.par96.scram ^= 1;
		if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 1))
			bc->modem.par96.scram ^=
				(PAR96_SCRAM_TAPN << 1);
		if (bc->modem.par96.scram & (PAR96_SCRAM_TAP1 << 2))
			val |= PAR96_TXBIT;
		pp->ops->write_data(pp, val);
		pp->ops->write_data(pp, val | PAR96_BURST);
	}
}

/* --------------------------------------------------------------------- */

static inline void par96_rx(struct net_device *dev, struct baycom_state *bc)
{
	int i;
	unsigned int data, mask, mask2, descx;
	struct parport *pp = bc->pdev->port;

	/*
	 * do receiver; differential decode and descramble on the fly
	 */
	for(data = i = 0; i < PAR96_BURSTBITS; i++) {
		bc->modem.par96.descram = (bc->modem.par96.descram << 1);
		if (pp->ops->read_status(pp) & PAR96_RXBIT)
			bc->modem.par96.descram |= 1;
		descx = bc->modem.par96.descram ^
			(bc->modem.par96.descram >> 1);
		/* now the diff decoded data is inverted in descram */
		pp->ops->write_data(pp, PAR97_POWER | PAR96_PTT);
		descx ^= ((descx >> PAR96_DESCRAM_TAPSH1) ^
			  (descx >> PAR96_DESCRAM_TAPSH2));
		data >>= 1;
		if (!(descx & 1))
			data |= 0x8000;
		pp->ops->write_data(pp, PAR97_POWER | PAR96_PTT | PAR96_BURST);
	}
	hdlcdrv_putbits(&bc->hdrv, data);
	/*
	 * do DCD algorithm
	 */
	if (bc->options & BAYCOM_OPTIONS_SOFTDCD) {
		bc->modem.par96.dcd_shreg = (bc->modem.par96.dcd_shreg >> 16)
			| (data << 16);
		/* search for flags and set the dcd counter appropriately */
		for(mask = 0x1fe00, mask2 = 0xfc00, i = 0;
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if ((bc->modem.par96.dcd_shreg & mask) == mask2)
				bc->modem.par96.dcd_count = HDLCDRV_MAXFLEN+4;
		/* check for abort/noise sequences */
		for(mask = 0x1fe00, mask2 = 0x1fe00, i = 0;
		    i < PAR96_BURSTBITS; i++, mask <<= 1, mask2 <<= 1)
			if (((bc->modem.par96.dcd_shreg & mask) == mask2) &&
			    (bc->modem.par96.dcd_count >= 0))
				bc->modem.par96.dcd_count -= HDLCDRV_MAXFLEN-10;
		/* decrement and set the dcd variable */
		if (bc->modem.par96.dcd_count >= 0)
			bc->modem.par96.dcd_count -= 2;
		hdlcdrv_setdcd(&bc->hdrv, bc->modem.par96.dcd_count > 0);
	} else {
		hdlcdrv_setdcd(&bc->hdrv, !!(pp->ops->read_status(pp) & PAR96_DCD));
	}
}

/* --------------------------------------------------------------------- */

static void par96_interrupt(void *dev_id)
{
	struct net_device *dev = dev_id;
	struct baycom_state *bc = netdev_priv(dev);

	baycom_int_freq(bc);
	/*
	 * check if transmitter active
	 */
	if (hdlcdrv_ptt(&bc->hdrv))
		par96_tx(dev, bc);
	else {
		par96_rx(dev, bc);
		if (--bc->modem.arb_divider <= 0) {
			bc->modem.arb_divider = 6;
			local_irq_enable();
			hdlcdrv_arbitrate(dev, &bc->hdrv);
		}
	}
	local_irq_enable();
	hdlcdrv_transmitter(dev, &bc->hdrv);
	hdlcdrv_receiver(dev, &bc->hdrv);
        local_irq_disable();
}

/* --------------------------------------------------------------------- */

static void par96_wakeup(void *handle)
{
        struct net_device *dev = (struct net_device *)handle;
	struct baycom_state *bc = netdev_priv(dev);

	printk(KERN_DEBUG "baycom_par: %s: why am I being woken up?\n", dev->name);
	if (!parport_claim(bc->pdev))
		printk(KERN_DEBUG "baycom_par: %s: I'm broken.\n", dev->name);
}

/* --------------------------------------------------------------------- */

static int par96_open(struct net_device *dev)
{
	struct baycom_state *bc = netdev_priv(dev);
	struct pardev_cb par_cb;
	struct parport *pp;
	int i;

	if (!dev || !bc)
		return -ENXIO;
	pp = parport_find_base(dev->base_addr);
	if (!pp) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx unknown\n", dev->base_addr);
		return -ENXIO;
	}
	if (pp->irq < 0) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx has no irq\n", pp->base);
		parport_put_port(pp);
		return -ENXIO;
	}
	if ((~pp->modes) & (PARPORT_MODE_PCSPP | PARPORT_MODE_SAFEININT)) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx cannot be used\n", pp->base);
		parport_put_port(pp);
		return -ENXIO;
	}
	memset(&bc->modem, 0, sizeof(bc->modem));
	bc->hdrv.par.bitrate = 9600;
	memset(&par_cb, 0, sizeof(par_cb));
	par_cb.wakeup = par96_wakeup;
	par_cb.irq_func = par96_interrupt;
	par_cb.private = (void *)dev;
	par_cb.flags = PARPORT_DEV_EXCL;
	for (i = 0; i < NR_PORTS; i++)
		if (baycom_device[i] == dev)
			break;

	if (i == NR_PORTS) {
		pr_err("%s: no device found\n", bc_drvname);
		parport_put_port(pp);
		return -ENODEV;
	}
	bc->pdev = parport_register_dev_model(pp, dev->name, &par_cb, i);
	parport_put_port(pp);
	if (!bc->pdev) {
		printk(KERN_ERR "baycom_par: cannot register parport at 0x%lx\n", dev->base_addr);
		return -ENXIO;
	}
	if (parport_claim(bc->pdev)) {
		printk(KERN_ERR "baycom_par: parport at 0x%lx busy\n", pp->base);
		parport_unregister_device(bc->pdev);
		return -EBUSY;
	}
	pp = bc->pdev->port;
	dev->irq = pp->irq;
	pp->ops->data_forward(pp);
        bc->hdrv.par.bitrate = 9600;
	pp->ops->write_data(pp, PAR96_PTT | PAR97_POWER); /* switch off PTT */
	pp->ops->enable_irq(pp);
	printk(KERN_INFO "%s: par96 at iobase 0x%lx irq %u options 0x%x\n",
	       bc_drvname, dev->base_addr, dev->irq, bc->options);
	return 0;
}

/* --------------------------------------------------------------------- */

static int par96_close(struct net_device *dev)
{
	struct baycom_state *bc = netdev_priv(dev);
	struct parport *pp;

	if (!dev || !bc)
		return -EINVAL;
	pp = bc->pdev->port;
	/* disable interrupt */
	pp->ops->disable_irq(pp);
	/* switch off PTT */
	pp->ops->write_data(pp, PAR96_PTT | PAR97_POWER);
	parport_release(bc->pdev);
	parport_unregister_device(bc->pdev);
	printk(KERN_INFO "%s: close par96 at iobase 0x%lx irq %u\n",
	       bc_drvname, dev->base_addr, dev->irq);
	return 0;
}

/* --------------------------------------------------------------------- */
/*
 * ===================== hdlcdrv driver interface =========================
 */

static int baycom_ioctl(struct net_device *dev, void __user *data,
			struct hdlcdrv_ioctl *hi, int cmd);

/* --------------------------------------------------------------------- */

static const struct hdlcdrv_ops par96_ops = {
	.drvname = bc_drvname,
	.drvinfo = bc_drvinfo,
	.open    = par96_open,
	.close   = par96_close,
	.ioctl   = baycom_ioctl
};

/* --------------------------------------------------------------------- */

static int baycom_setmode(struct baycom_state *bc, const char *modestr)
{
	if (!strncmp(modestr, "picpar", 6))
		bc->options = 0;
	else if (!strncmp(modestr, "par96", 5))
		bc->options = BAYCOM_OPTIONS_SOFTDCD;
	else
		bc->options = !!strchr(modestr, '*');
	return 0;
}

/* --------------------------------------------------------------------- */

static int baycom_ioctl(struct net_device *dev, void __user *data,
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
		strcpy(hi->data.modename, bc->options ? "par96" : "picpar");
		if (copy_to_user(data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_SETMODE:
		if (netif_running(dev) || !capable(CAP_NET_ADMIN))
			return -EACCES;
		hi->data.modename[sizeof(hi->data.modename)-1] = '\0';
		return baycom_setmode(bc, hi->data.modename);

	case HDLCDRVCTL_MODELIST:
		strcpy(hi->data.modename, "par96,picpar");
		if (copy_to_user(data, hi, sizeof(struct hdlcdrv_ioctl)))
			return -EFAULT;
		return 0;

	case HDLCDRVCTL_MODEMPARMASK:
		return HDLCDRV_PARMASK_IOBASE;

	}

	if (copy_from_user(&bi, data, sizeof(bi)))
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
	if (copy_to_user(data, &bi, sizeof(bi)))
		return -EFAULT;
	return 0;

}

/* --------------------------------------------------------------------- */

/*
 * command line settable parameters
 */
static char *mode[NR_PORTS] = { "picpar", };
static int iobase[NR_PORTS] = { 0x378, };

module_param_array(mode, charp, NULL, 0);
MODULE_PARM_DESC(mode, "baycom operating mode; eg. par96 or picpar");
module_param_hw_array(iobase, int, ioport, NULL, 0);
MODULE_PARM_DESC(iobase, "baycom io base address");

MODULE_AUTHOR("Thomas M. Sailer, sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu");
MODULE_DESCRIPTION("Baycom par96 and picpar amateur radio modem driver");
MODULE_LICENSE("GPL");

/* --------------------------------------------------------------------- */

static int baycom_par_probe(struct pardevice *par_dev)
{
	struct device_driver *drv = par_dev->dev.driver;
	int len = strlen(drv->name);

	if (strncmp(par_dev->name, drv->name, len))
		return -ENODEV;

	return 0;
}

static struct parport_driver baycom_par_driver = {
	.name = "bcp",
	.probe = baycom_par_probe,
};

static int __init init_baycompar(void)
{
	int i, found = 0, ret;
	char set_hw = 1;

	printk(bc_drvinfo);

	ret = parport_register_driver(&baycom_par_driver);
	if (ret)
		return ret;

	/*
	 * register net devices
	 */
	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev;
		struct baycom_state *bc;
		char ifname[IFNAMSIZ];

		sprintf(ifname, "bcp%d", i);

		if (!mode[i])
			set_hw = 0;
		if (!set_hw)
			iobase[i] = 0;

		dev = hdlcdrv_register(&par96_ops,
				       sizeof(struct baycom_state),
				       ifname, iobase[i], 0, 0);
		if (IS_ERR(dev)) 
			break;

		bc = netdev_priv(dev);
		if (set_hw && baycom_setmode(bc, mode[i]))
			set_hw = 0;
		found++;
		baycom_device[i] = dev;
	}

	if (!found) {
		parport_unregister_driver(&baycom_par_driver);
		return -ENXIO;
	}
	return 0;
}

static void __exit cleanup_baycompar(void)
{
	int i;

	for(i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = baycom_device[i];

		if (dev)
			hdlcdrv_unregister(dev);
	}
	parport_unregister_driver(&baycom_par_driver);
}

module_init(init_baycompar);
module_exit(cleanup_baycompar);

/* --------------------------------------------------------------------- */

#ifndef MODULE

/*
 * format: baycom_par=io,mode
 * mode: par96,picpar
 */

static int __init baycom_par_setup(char *str)
{
        static unsigned nr_dev;
	int ints[2];

        if (nr_dev >= NR_PORTS)
                return 0;
        str = get_options(str, 2, ints);
        if (ints[0] < 1)
                return 0;
        mode[nr_dev] = str;
        iobase[nr_dev] = ints[1];
	nr_dev++;
	return 1;
}

__setup("baycom_par=", baycom_par_setup);

#endif /* MODULE */
/* --------------------------------------------------------------------- */

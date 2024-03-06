/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Low-level parallel-support for PC-style hardware integrated in the
 *	LASI-Controller (on GSC-Bus) for HP-PARISC Workstations
 *
 *	(C) 1999-2001 by Helge Deller <deller@gmx.de>
 *
 * based on parport_pc.c by
 * 	    Grant Guenther <grant@torque.net>
 * 	    Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell
 *          Andrea Arcangeli
 */

#ifndef	__DRIVERS_PARPORT_PARPORT_GSC_H
#define	__DRIVERS_PARPORT_PARPORT_GSC_H

#include <asm/io.h>
#include <linux/delay.h>

#undef	DEBUG_PARPORT	/* undefine for production */
#define DELAY_TIME 	0

#if DELAY_TIME == 0
#define parport_readb	gsc_readb
#define parport_writeb	gsc_writeb
#else
static __inline__ unsigned char parport_readb( unsigned long port )
{
    udelay(DELAY_TIME);
    return gsc_readb(port);
}

static __inline__ void parport_writeb( unsigned char value, unsigned long port )
{
    gsc_writeb(value,port);
    udelay(DELAY_TIME);
}
#endif

/* --- register definitions ------------------------------- */

#define EPPDATA(p)  ((p)->base    + 0x4)
#define EPPADDR(p)  ((p)->base    + 0x3)
#define CONTROL(p)  ((p)->base    + 0x2)
#define STATUS(p)   ((p)->base    + 0x1)
#define DATA(p)     ((p)->base    + 0x0)

struct parport_gsc_private {
	/* Contents of CTR. */
	unsigned char ctr;

	/* Bitmask of writable CTR bits. */
	unsigned char ctr_writable;

	/* Number of bytes per portword. */
	int pword;

	/* Not used yet. */
	int readIntrThreshold;
	int writeIntrThreshold;

	/* buffer suitable for DMA, if DMA enabled */
	struct pci_dev *dev;
};

static inline void parport_gsc_write_data(struct parport *p, unsigned char d)
{
#ifdef DEBUG_PARPORT
	printk(KERN_DEBUG "%s(%p,0x%02x)\n", __func__, p, d);
#endif
	parport_writeb(d, DATA(p));
}

static inline unsigned char parport_gsc_read_data(struct parport *p)
{
	unsigned char val = parport_readb (DATA (p));
#ifdef DEBUG_PARPORT
	printk(KERN_DEBUG "%s(%p) = 0x%02x\n", __func__, p, val);
#endif
	return val;
}

/* __parport_gsc_frob_control differs from parport_gsc_frob_control in that
 * it doesn't do any extra masking. */
static inline unsigned char __parport_gsc_frob_control(struct parport *p,
							unsigned char mask,
							unsigned char val)
{
	struct parport_gsc_private *priv = p->physport->private_data;
	unsigned char ctr = priv->ctr;
#ifdef DEBUG_PARPORT
	printk(KERN_DEBUG "%s(%02x,%02x): %02x -> %02x\n",
	       __func__, mask, val,
	       ctr, ((ctr & ~mask) ^ val) & priv->ctr_writable);
#endif
	ctr = (ctr & ~mask) ^ val;
	ctr &= priv->ctr_writable; /* only write writable bits. */
	parport_writeb (ctr, CONTROL (p));
	priv->ctr = ctr;	/* Update soft copy */
	return ctr;
}

static inline void parport_gsc_data_reverse(struct parport *p)
{
	__parport_gsc_frob_control (p, 0x20, 0x20);
}

static inline void parport_gsc_data_forward(struct parport *p)
{
	__parport_gsc_frob_control (p, 0x20, 0x00);
}

static inline void parport_gsc_write_control(struct parport *p,
						 unsigned char d)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);

	/* Take this out when drivers have adapted to newer interface. */
	if (d & 0x20) {
		printk(KERN_DEBUG "%s (%s): use data_reverse for this!\n",
		       p->name, p->cad->name);
		parport_gsc_data_reverse (p);
	}

	__parport_gsc_frob_control (p, wm, d & wm);
}

static inline unsigned char parport_gsc_read_control(struct parport *p)
{
	const unsigned char rm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);
	const struct parport_gsc_private *priv = p->physport->private_data;
	return priv->ctr & rm; /* Use soft copy */
}

static inline unsigned char parport_gsc_frob_control(struct parport *p,
							unsigned char mask,
							unsigned char val)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);

	/* Take this out when drivers have adapted to newer interface. */
	if (mask & 0x20) {
		printk(KERN_DEBUG "%s (%s): use data_%s for this!\n",
		       p->name, p->cad->name,
		       (val & 0x20) ? "reverse" : "forward");
		if (val & 0x20)
			parport_gsc_data_reverse (p);
		else
			parport_gsc_data_forward (p);
	}

	/* Restrict mask and val to control lines. */
	mask &= wm;
	val &= wm;

	return __parport_gsc_frob_control (p, mask, val);
}

static inline unsigned char parport_gsc_read_status(struct parport *p)
{
	return parport_readb (STATUS(p));
}

static inline void parport_gsc_disable_irq(struct parport *p)
{
	__parport_gsc_frob_control (p, 0x10, 0x00);
}

static inline void parport_gsc_enable_irq(struct parport *p)
{
	__parport_gsc_frob_control (p, 0x10, 0x10);
}

extern void parport_gsc_release_resources(struct parport *p);

extern int parport_gsc_claim_resources(struct parport *p);

extern void parport_gsc_init_state(struct pardevice *, struct parport_state *s);

extern void parport_gsc_save_state(struct parport *p, struct parport_state *s);

extern void parport_gsc_restore_state(struct parport *p, struct parport_state *s);

extern void parport_gsc_inc_use_count(void);

extern void parport_gsc_dec_use_count(void);

#endif	/* __DRIVERS_PARPORT_PARPORT_GSC_H */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1992-1997, 2000-2003 Silicon Graphics, Inc.
 * Copyright (C) 2004 Christoph Hellwig.
 *
 * Support functions for the HUB ASIC - mostly PIO mapping related.
 */

#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/mmzone.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/hub.h>


static int force_fire_and_forget = 1;

/**
 * hub_pio_map	-  establish a HUB PIO mapping
 *
 * @hub:	hub to perform PIO mapping on
 * @widget:	widget ID to perform PIO mapping for
 * @xtalk_addr: xtalk_address that needs to be mapped
 * @size:	size of the PIO mapping
 *
 **/
unsigned long hub_pio_map(nasid_t nasid, xwidgetnum_t widget,
			  unsigned long xtalk_addr, size_t size)
{
	unsigned i;

	/* use small-window mapping if possible */
	if ((xtalk_addr % SWIN_SIZE) + size <= SWIN_SIZE)
		return NODE_SWIN_BASE(nasid, widget) + (xtalk_addr % SWIN_SIZE);

	if ((xtalk_addr % BWIN_SIZE) + size > BWIN_SIZE) {
		printk(KERN_WARNING "PIO mapping at hub %d widget %d addr 0x%lx"
				" too big (%ld)\n",
				nasid, widget, xtalk_addr, size);
		return 0;
	}

	xtalk_addr &= ~(BWIN_SIZE-1);
	for (i = 0; i < HUB_NUM_BIG_WINDOW; i++) {
		if (test_and_set_bit(i, hub_data(nasid)->h_bigwin_used))
			continue;

		/*
		 * The code below does a PIO write to setup an ITTE entry.
		 *
		 * We need to prevent other CPUs from seeing our updated
		 * memory shadow of the ITTE (in the piomap) until the ITTE
		 * entry is actually set up; otherwise, another CPU might
		 * attempt a PIO prematurely.
		 *
		 * Also, the only way we can know that an entry has been
		 * received  by the hub and can be used by future PIO reads/
		 * writes is by reading back the ITTE entry after writing it.
		 *
		 * For these two reasons, we PIO read back the ITTE entry
		 * after we write it.
		 */
		IIO_ITTE_PUT(nasid, i, HUB_PIO_MAP_TO_MEM, widget, xtalk_addr);
		__raw_readq(IIO_ITTE_GET(nasid, i));

		return NODE_BWIN_BASE(nasid, widget) + (xtalk_addr % BWIN_SIZE);
	}

	printk(KERN_WARNING "unable to establish PIO mapping for at"
			" hub %d widget %d addr 0x%lx\n",
			nasid, widget, xtalk_addr);
	return 0;
}


/*
 * hub_setup_prb(nasid, prbnum, credits, conveyor)
 *
 *	Put a PRB into fire-and-forget mode if conveyor isn't set.  Otherwise,
 *	put it into conveyor belt mode with the specified number of credits.
 */
static void hub_setup_prb(nasid_t nasid, int prbnum, int credits)
{
	iprb_t prb;
	int prb_offset;

	/*
	 * Get the current register value.
	 */
	prb_offset = IIO_IOPRB(prbnum);
	prb.iprb_regval = REMOTE_HUB_L(nasid, prb_offset);

	/*
	 * Clear out some fields.
	 */
	prb.iprb_ovflow = 1;
	prb.iprb_bnakctr = 0;
	prb.iprb_anakctr = 0;

	/*
	 * Enable or disable fire-and-forget mode.
	 */
	prb.iprb_ff = force_fire_and_forget ? 1 : 0;

	/*
	 * Set the appropriate number of PIO credits for the widget.
	 */
	prb.iprb_xtalkctr = credits;

	/*
	 * Store the new value to the register.
	 */
	REMOTE_HUB_S(nasid, prb_offset, prb.iprb_regval);
}

/**
 * hub_set_piomode  -  set pio mode for a given hub
 *
 * @nasid:	physical node ID for the hub in question
 *
 * Put the hub into either "PIO conveyor belt" mode or "fire-and-forget" mode.
 * To do this, we have to make absolutely sure that no PIOs are in progress
 * so we turn off access to all widgets for the duration of the function.
 *
 * XXX - This code should really check what kind of widget we're talking
 * to.	Bridges can only handle three requests, but XG will do more.
 * How many can crossbow handle to widget 0?  We're assuming 1.
 *
 * XXX - There is a bug in the crossbow that link reset PIOs do not
 * return write responses.  The easiest solution to this problem is to
 * leave widget 0 (xbow) in fire-and-forget mode at all times.	This
 * only affects pio's to xbow registers, which should be rare.
 **/
static void hub_set_piomode(nasid_t nasid)
{
	u64 ii_iowa;
	hubii_wcr_t ii_wcr;
	unsigned i;

	ii_iowa = REMOTE_HUB_L(nasid, IIO_OUTWIDGET_ACCESS);
	REMOTE_HUB_S(nasid, IIO_OUTWIDGET_ACCESS, 0);

	ii_wcr.wcr_reg_value = REMOTE_HUB_L(nasid, IIO_WCR);

	if (ii_wcr.iwcr_dir_con) {
		/*
		 * Assume a bridge here.
		 */
		hub_setup_prb(nasid, 0, 3);
	} else {
		/*
		 * Assume a crossbow here.
		 */
		hub_setup_prb(nasid, 0, 1);
	}

	/*
	 * XXX - Here's where we should take the widget type into
	 * when account assigning credits.
	 */
	for (i = HUB_WIDGET_ID_MIN; i <= HUB_WIDGET_ID_MAX; i++)
		hub_setup_prb(nasid, i, 3);

	REMOTE_HUB_S(nasid, IIO_OUTWIDGET_ACCESS, ii_iowa);
}

/*
 * hub_pio_init	 -  PIO-related hub initialization
 *
 * @hub:	hubinfo structure for our hub
 */
void hub_pio_init(nasid_t nasid)
{
	unsigned i;

	/* initialize big window piomaps for this hub */
	bitmap_zero(hub_data(nasid)->h_bigwin_used, HUB_NUM_BIG_WINDOW);
	for (i = 0; i < HUB_NUM_BIG_WINDOW; i++)
		IIO_ITTE_DISABLE(nasid, i);

	hub_set_piomode(nasid);
}

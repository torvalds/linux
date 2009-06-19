/*
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silcon Graphics, Inc.
 * Copyright (C) 2004 Christoph Hellwig.
 *	Released under GPL v2.
 *
 * Generic XTALK initialization code
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <asm/sn/types.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/hub.h>
#include <asm/pci/bridge.h>
#include <asm/xtalk/xtalk.h>


#define XBOW_WIDGET_PART_NUM    0x0
#define XXBOW_WIDGET_PART_NUM   0xd000  /* Xbow in Xbridge */
#define BASE_XBOW_PORT  	8     /* Lowest external port */

extern int bridge_probe(nasid_t nasid, int widget, int masterwid);

static int __cpuinit probe_one_port(nasid_t nasid, int widget, int masterwid)
{
	widgetreg_t 		widget_id;
	xwidget_part_num_t	partnum;

	widget_id = *(volatile widgetreg_t *)
		(RAW_NODE_SWIN_BASE(nasid, widget) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	printk(KERN_INFO "Cpu %d, Nasid 0x%x, widget 0x%x (partnum 0x%x) is ",
			smp_processor_id(), nasid, widget, partnum);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
	case XBRIDGE_WIDGET_PART_NUM:
		bridge_probe(nasid, widget, masterwid);
		break;
	default:
		break;
	}

	return 0;
}

static int __cpuinit xbow_probe(nasid_t nasid)
{
	lboard_t *brd;
	klxbow_t *xbow_p;
	unsigned masterwid, i;

	printk("is xbow\n");

	/*
	 * found xbow, so may have multiple bridges
	 * need to probe xbow
	 */
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_MIDPLANE8);
	if (!brd)
		return -ENODEV;

	xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW);
	if (!xbow_p)
		return -ENODEV;

	/*
	 * Okay, here's a xbow. Lets arbitrate and find
	 * out if we should initialize it. Set enabled
	 * hub connected at highest or lowest widget as
	 * master.
	 */
#ifdef WIDGET_A
	i = HUB_WIDGET_ID_MAX + 1;
	do {
		i--;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#else
	i = HUB_WIDGET_ID_MIN - 1;
	do {
		i++;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#endif

	masterwid = i;
	if (nasid != XBOW_PORT_NASID(xbow_p, i))
		return 1;

	for (i = HUB_WIDGET_ID_MIN; i <= HUB_WIDGET_ID_MAX; i++) {
		if (XBOW_PORT_IS_ENABLED(xbow_p, i) &&
		    XBOW_PORT_TYPE_IO(xbow_p, i))
			probe_one_port(nasid, i, masterwid);
	}

	return 0;
}

void __cpuinit xtalk_probe_node(cnodeid_t nid)
{
	volatile u64 		hubreg;
	nasid_t	 		nasid;
	xwidget_part_num_t	partnum;
	widgetreg_t 		widget_id;

	nasid = COMPACT_TO_NASID_NODEID(nid);
	hubreg = REMOTE_HUB_L(nasid, IIO_LLP_CSR);

	/* check whether the link is up */
	if (!(hubreg & IIO_LLP_CSR_IS_UP))
		return;

	widget_id = *(volatile widgetreg_t *)
                       (RAW_NODE_SWIN_BASE(nasid, 0x0) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	printk(KERN_INFO "Cpu %d, Nasid 0x%x: partnum 0x%x is ",
			smp_processor_id(), nasid, partnum);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
		bridge_probe(nasid, 0x8, 0xa);
		break;
	case XBOW_WIDGET_PART_NUM:
	case XXBOW_WIDGET_PART_NUM:
		xbow_probe(nasid);
		break;
	default:
		printk(" unknown widget??\n");
		break;
	}
}

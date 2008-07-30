/* central.c: Central FHC driver for Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997, 1999 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/page.h>
#include <asm/fhc.h>
#include <asm/starfire.h>

static struct linux_central *central_bus = NULL;
static struct linux_fhc *fhc_list = NULL;

#define IS_CENTRAL_FHC(__fhc)	((__fhc) == central_bus->child)

static void central_probe_failure(int line)
{
	prom_printf("CENTRAL: Critical device probe failure at central.c:%d\n",
		    line);
	prom_halt();
}

static void central_ranges_init(struct linux_central *central)
{
	struct device_node *dp = central->prom_node;
	const void *pval;
	int len;
	
	central->num_central_ranges = 0;
	pval = of_get_property(dp, "ranges", &len);
	if (pval) {
		memcpy(central->central_ranges, pval, len);
		central->num_central_ranges =
			(len / sizeof(struct linux_prom_ranges));
	}
}

static void fhc_ranges_init(struct linux_fhc *fhc)
{
	struct device_node *dp = fhc->prom_node;
	const void *pval;
	int len;
	
	fhc->num_fhc_ranges = 0;
	pval = of_get_property(dp, "ranges", &len);
	if (pval) {
		memcpy(fhc->fhc_ranges, pval, len);
		fhc->num_fhc_ranges =
			(len / sizeof(struct linux_prom_ranges));
	}
}

/* Range application routines are exported to various drivers,
 * so do not __init this.
 */
static void adjust_regs(struct linux_prom_registers *regp, int nregs,
			struct linux_prom_ranges *rangep, int nranges)
{
	int regc, rngc;

	for (regc = 0; regc < nregs; regc++) {
		for (rngc = 0; rngc < nranges; rngc++)
			if (regp[regc].which_io == rangep[rngc].ot_child_space)
				break; /* Fount it */
		if (rngc == nranges) /* oops */
			central_probe_failure(__LINE__);
		regp[regc].which_io = rangep[rngc].ot_parent_space;
		regp[regc].phys_addr -= rangep[rngc].ot_child_base;
		regp[regc].phys_addr += rangep[rngc].ot_parent_base;
	}
}

/* Apply probed fhc ranges to registers passed, if no ranges return. */
static void apply_fhc_ranges(struct linux_fhc *fhc,
			     struct linux_prom_registers *regs,
			     int nregs)
{
	if (fhc->num_fhc_ranges)
		adjust_regs(regs, nregs, fhc->fhc_ranges,
			    fhc->num_fhc_ranges);
}

/* Apply probed central ranges to registers passed, if no ranges return. */
static void apply_central_ranges(struct linux_central *central,
				 struct linux_prom_registers *regs, int nregs)
{
	if (central->num_central_ranges)
		adjust_regs(regs, nregs, central->central_ranges,
			    central->num_central_ranges);
}

static void * __init central_alloc_bootmem(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret != NULL)
		memset(ret, 0, size);

	return ret;
}

static unsigned long prom_reg_to_paddr(struct linux_prom_registers *r)
{
	unsigned long ret = ((unsigned long) r->which_io) << 32;

	return ret | (unsigned long) r->phys_addr;
}

static void __init probe_other_fhcs(void)
{
	struct device_node *dp;
	const struct linux_prom64_registers *fpregs;

	for_each_node_by_name(dp, "fhc") {
		struct linux_fhc *fhc;
		int board;
		u32 tmp;

		if (dp->parent &&
		    dp->parent->parent != NULL)
			continue;

		fhc = (struct linux_fhc *)
			central_alloc_bootmem(sizeof(struct linux_fhc));
		if (fhc == NULL)
			central_probe_failure(__LINE__);

		/* Link it into the FHC chain. */
		fhc->next = fhc_list;
		fhc_list = fhc;

		/* Toplevel FHCs have no parent. */
		fhc->parent = NULL;
		
		fhc->prom_node = dp;
		fhc_ranges_init(fhc);

		/* Non-central FHC's have 64-bit OBP format registers. */
		fpregs = of_get_property(dp, "reg", NULL);
		if (!fpregs)
			central_probe_failure(__LINE__);

		/* Only central FHC needs special ranges applied. */
		fhc->fhc_regs.pregs = fpregs[0].phys_addr;
		fhc->fhc_regs.ireg = fpregs[1].phys_addr;
		fhc->fhc_regs.ffregs = fpregs[2].phys_addr;
		fhc->fhc_regs.sregs = fpregs[3].phys_addr;
		fhc->fhc_regs.uregs = fpregs[4].phys_addr;
		fhc->fhc_regs.tregs = fpregs[5].phys_addr;

		board = of_getintprop_default(dp, "board#", -1);
		fhc->board = board;

		tmp = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_JCTRL);
		if ((tmp & FHC_JTAG_CTRL_MENAB) != 0)
			fhc->jtag_master = 1;
		else
			fhc->jtag_master = 0;

		tmp = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_ID);
		printk("FHC(board %d): Version[%x] PartID[%x] Manuf[%x] %s\n",
		       board,
		       (tmp & FHC_ID_VERS) >> 28,
		       (tmp & FHC_ID_PARTID) >> 12,
		       (tmp & FHC_ID_MANUF) >> 1,
		       (fhc->jtag_master ? "(JTAG Master)" : ""));
		
		/* This bit must be set in all non-central FHC's in
		 * the system.  When it is clear, this identifies
		 * the central board.
		 */
		tmp = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
		tmp |= FHC_CONTROL_IXIST;
		upa_writel(tmp, fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
	}
}

static void probe_clock_board(struct linux_central *central,
			      struct linux_fhc *fhc,
			      struct device_node *fp)
{
	struct device_node *dp;
	struct linux_prom_registers cregs[3];
	const struct linux_prom_registers *pr;
	int nslots, tmp, nregs;

	dp = fp->child;
	while (dp) {
		if (!strcmp(dp->name, "clock-board"))
			break;
		dp = dp->sibling;
	}
	if (!dp)
		central_probe_failure(__LINE__);

	pr = of_get_property(dp, "reg", &nregs);
	if (!pr)
		central_probe_failure(__LINE__);

	memcpy(cregs, pr, nregs);
	nregs /= sizeof(struct linux_prom_registers);

	apply_fhc_ranges(fhc, &cregs[0], nregs);
	apply_central_ranges(central, &cregs[0], nregs);
	central->cfreg = prom_reg_to_paddr(&cregs[0]);
	central->clkregs = prom_reg_to_paddr(&cregs[1]);

	if (nregs == 2)
		central->clkver = 0UL;
	else
		central->clkver = prom_reg_to_paddr(&cregs[2]);

	tmp = upa_readb(central->clkregs + CLOCK_STAT1);
	tmp &= 0xc0;
	switch(tmp) {
	case 0x40:
		nslots = 16;
		break;
	case 0xc0:
		nslots = 8;
		break;
	case 0x80:
		if (central->clkver != 0UL &&
		   upa_readb(central->clkver) != 0) {
			if ((upa_readb(central->clkver) & 0x80) != 0)
				nslots = 4;
			else
				nslots = 5;
			break;
		}
	default:
		nslots = 4;
		break;
	};
	central->slots = nslots;
	printk("CENTRAL: Detected %d slot Enterprise system. cfreg[%02x] cver[%02x]\n",
	       central->slots, upa_readb(central->cfreg),
	       (central->clkver ? upa_readb(central->clkver) : 0x00));
}

static void ZAP(unsigned long iclr, unsigned long imap)
{
	u32 imap_tmp;

	upa_writel(0, iclr);
	upa_readl(iclr);
	imap_tmp = upa_readl(imap);
	imap_tmp &= ~(0x80000000);
	upa_writel(imap_tmp, imap);
	upa_readl(imap);
}

static void init_all_fhc_hw(void)
{
	struct linux_fhc *fhc;

	for (fhc = fhc_list; fhc != NULL; fhc = fhc->next) {
		u32 tmp;

		/* Clear all of the interrupt mapping registers
		 * just in case OBP left them in a foul state.
		 */
		ZAP(fhc->fhc_regs.ffregs + FHC_FFREGS_ICLR,
		    fhc->fhc_regs.ffregs + FHC_FFREGS_IMAP);
		ZAP(fhc->fhc_regs.sregs + FHC_SREGS_ICLR,
		    fhc->fhc_regs.sregs + FHC_SREGS_IMAP);
		ZAP(fhc->fhc_regs.uregs + FHC_UREGS_ICLR,
		    fhc->fhc_regs.uregs + FHC_UREGS_IMAP);
		ZAP(fhc->fhc_regs.tregs + FHC_TREGS_ICLR,
		    fhc->fhc_regs.tregs + FHC_TREGS_IMAP);

		/* Setup FHC control register. */
		tmp = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_CTRL);

		/* All non-central boards have this bit set. */
		if (! IS_CENTRAL_FHC(fhc))
			tmp |= FHC_CONTROL_IXIST;

		/* For all FHCs, clear the firmware synchronization
		 * line and both low power mode enables.
		 */
		tmp &= ~(FHC_CONTROL_AOFF | FHC_CONTROL_BOFF |
			 FHC_CONTROL_SLINE);

		upa_writel(tmp, fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
		upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
	}

}

void __init central_probe(void)
{
	struct linux_prom_registers fpregs[6];
	const struct linux_prom_registers *pr;
	struct linux_fhc *fhc;
	struct device_node *dp, *fp;
	int err;

	dp = of_find_node_by_name(NULL, "central");
	if (!dp) {
		if (this_is_starfire)
			starfire_cpu_setup();
		return;
	}

	/* Ok we got one, grab some memory for software state. */
	central_bus = (struct linux_central *)
		central_alloc_bootmem(sizeof(struct linux_central));
	if (central_bus == NULL)
		central_probe_failure(__LINE__);

	fhc = (struct linux_fhc *)
		central_alloc_bootmem(sizeof(struct linux_fhc));
	if (fhc == NULL)
		central_probe_failure(__LINE__);

	/* First init central. */
	central_bus->child = fhc;
	central_bus->prom_node = dp;
	central_ranges_init(central_bus);

	/* And then central's FHC. */
	fhc->next = fhc_list;
	fhc_list = fhc;

	fhc->parent = central_bus;
	fp = dp->child;
	while (fp) {
		if (!strcmp(fp->name, "fhc"))
			break;
		fp = fp->sibling;
	}
	if (!fp)
		central_probe_failure(__LINE__);

	fhc->prom_node = fp;
	fhc_ranges_init(fhc);

	/* Now, map in FHC register set. */
	pr = of_get_property(fp, "reg", NULL);
	if (!pr)
		central_probe_failure(__LINE__);
	memcpy(fpregs, pr, sizeof(fpregs));

	apply_central_ranges(central_bus, &fpregs[0], 6);
	
	fhc->fhc_regs.pregs = prom_reg_to_paddr(&fpregs[0]);
	fhc->fhc_regs.ireg = prom_reg_to_paddr(&fpregs[1]);
	fhc->fhc_regs.ffregs = prom_reg_to_paddr(&fpregs[2]);
	fhc->fhc_regs.sregs = prom_reg_to_paddr(&fpregs[3]);
	fhc->fhc_regs.uregs = prom_reg_to_paddr(&fpregs[4]);
	fhc->fhc_regs.tregs = prom_reg_to_paddr(&fpregs[5]);

	/* Obtain board number from board status register, Central's
	 * FHC lacks "board#" property.
	 */
	err = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_BSR);
	fhc->board = (((err >> 16) & 0x01) |
		      ((err >> 12) & 0x0e));

	fhc->jtag_master = 0;

	/* Attach the clock board registers for CENTRAL. */
	probe_clock_board(central_bus, fhc, fp);

	err = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_ID);
	printk("FHC(board %d): Version[%x] PartID[%x] Manuf[%x] (CENTRAL)\n",
	       fhc->board,
	       ((err & FHC_ID_VERS) >> 28),
	       ((err & FHC_ID_PARTID) >> 12),
	       ((err & FHC_ID_MANUF) >> 1));

	probe_other_fhcs();

	init_all_fhc_hw();
}

static inline void fhc_ledblink(struct linux_fhc *fhc, int on)
{
	u32 tmp;

	tmp = upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_CTRL);

	/* NOTE: reverse logic on this bit */
	if (on)
		tmp &= ~(FHC_CONTROL_RLED);
	else
		tmp |= FHC_CONTROL_RLED;
	tmp &= ~(FHC_CONTROL_AOFF | FHC_CONTROL_BOFF | FHC_CONTROL_SLINE);

	upa_writel(tmp, fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
	upa_readl(fhc->fhc_regs.pregs + FHC_PREGS_CTRL);
}

static inline void central_ledblink(struct linux_central *central, int on)
{
	u8 tmp;

	tmp = upa_readb(central->clkregs + CLOCK_CTRL);

	/* NOTE: reverse logic on this bit */
	if (on)
		tmp &= ~(CLOCK_CTRL_RLED);
	else
		tmp |= CLOCK_CTRL_RLED;

	upa_writeb(tmp, central->clkregs + CLOCK_CTRL);
	upa_readb(central->clkregs + CLOCK_CTRL);
}

static struct timer_list sftimer;
static int led_state;

static void sunfire_timer(unsigned long __ignored)
{
	struct linux_fhc *fhc;

	central_ledblink(central_bus, led_state);
	for (fhc = fhc_list; fhc != NULL; fhc = fhc->next)
		if (! IS_CENTRAL_FHC(fhc))
			fhc_ledblink(fhc, led_state);
	led_state = ! led_state;
	sftimer.expires = jiffies + (HZ >> 1);
	add_timer(&sftimer);
}

/* After PCI/SBUS busses have been probed, this is called to perform
 * final initialization of all FireHose Controllers in the system.
 */
void firetruck_init(void)
{
	struct linux_central *central = central_bus;
	u8 ctrl;

	/* No central bus, nothing to do. */
	if (central == NULL)
		return;

	/* OBP leaves it on, turn it off so clock board timer LED
	 * is in sync with FHC ones.
	 */
	ctrl = upa_readb(central->clkregs + CLOCK_CTRL);
	ctrl &= ~(CLOCK_CTRL_RLED);
	upa_writeb(ctrl, central->clkregs + CLOCK_CTRL);

	led_state = 0;
	init_timer(&sftimer);
	sftimer.data = 0;
	sftimer.function = &sunfire_timer;
	sftimer.expires = jiffies + (HZ >> 1);
	add_timer(&sftimer);
}

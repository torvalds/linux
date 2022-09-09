// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/timex.h>
#include <linux/clocksource.h>
#include <linux/io.h>

/* IBM Summit (EXA) Cyclone counter code*/
#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ 100000000

int use_cyclone;
void __init cyclone_setup(void)
{
	use_cyclone = 1;
}

static void __iomem *cyclone_mc;

static u64 read_cyclone(struct clocksource *cs)
{
	return (u64)readq((void __iomem *)cyclone_mc);
}

static struct clocksource clocksource_cyclone = {
        .name           = "cyclone",
        .rating         = 300,
        .read           = read_cyclone,
        .mask           = (1LL << 40) - 1,
        .flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

int __init init_cyclone_clock(void)
{
	u64 __iomem *reg;
	u64 base;	/* saved cyclone base address */
	u64 offset;	/* offset from pageaddr to cyclone_timer register */
	int i;
	u32 __iomem *cyclone_timer;	/* Cyclone MPMC0 register */

	if (!use_cyclone)
		return 0;

	printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

	/* find base address */
	offset = (CYCLONE_CBAR_ADDR);
	reg = ioremap(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR"
				" register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	base = readq(reg);
	iounmap(reg);
	if(!base){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR"
				" value.\n");
		use_cyclone = 0;
		return -ENODEV;
	}

	/* setup PMCC */
	offset = (base + CYCLONE_PMCC_OFFSET);
	reg = ioremap(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid PMCC"
				" register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* setup MPCS */
	offset = (base + CYCLONE_MPCS_OFFSET);
	reg = ioremap(offset, sizeof(u64));
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid MPCS"
				" register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* map in cyclone_timer */
	offset = (base + CYCLONE_MPMC_OFFSET);
	cyclone_timer = ioremap(offset, sizeof(u32));
	if(!cyclone_timer){
		printk(KERN_ERR "Summit chipset: Could not find valid MPMC"
				" register.\n");
		use_cyclone = 0;
		return -ENODEV;
	}

	/*quick test to make sure its ticking*/
	for(i=0; i<3; i++){
		u32 old = readl(cyclone_timer);
		int stall = 100;
		while(stall--) barrier();
		if(readl(cyclone_timer) == old){
			printk(KERN_ERR "Summit chipset: Counter not counting!"
					" DISABLED\n");
			iounmap(cyclone_timer);
			cyclone_timer = NULL;
			use_cyclone = 0;
			return -ENODEV;
		}
	}
	/* initialize last tick */
	cyclone_mc = cyclone_timer;
	clocksource_cyclone.archdata.fsys_mmio = cyclone_timer;
	clocksource_register_hz(&clocksource_cyclone, CYCLONE_TIMER_FREQ);

	return 0;
}

__initcall(init_cyclone_clock);

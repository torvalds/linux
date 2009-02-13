#include <linux/clocksource.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/timex.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/io.h>

#include <asm/mach_timer.h>

#define CYCLONE_CBAR_ADDR	0xFEB00CD0	/* base address ptr */
#define CYCLONE_PMCC_OFFSET	0x51A0		/* offset to control register */
#define CYCLONE_MPCS_OFFSET	0x51A8		/* offset to select register */
#define CYCLONE_MPMC_OFFSET	0x51D0		/* offset to count register */
#define CYCLONE_TIMER_FREQ	99780000	/* 100Mhz, but not really */
#define CYCLONE_TIMER_MASK	CLOCKSOURCE_MASK(32) /* 32 bit mask */

int use_cyclone = 0;
static void __iomem *cyclone_ptr;

static cycle_t read_cyclone(void)
{
	return (cycle_t)readl(cyclone_ptr);
}

static struct clocksource clocksource_cyclone = {
	.name		= "cyclone",
	.rating		= 250,
	.read		= read_cyclone,
	.mask		= CYCLONE_TIMER_MASK,
	.mult		= 10,
	.shift		= 0,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init init_cyclone_clocksource(void)
{
	unsigned long base;	/* saved value from CBAR */
	unsigned long offset;
	u32 __iomem* volatile cyclone_timer;	/* Cyclone MPMC0 register */
	u32 __iomem* reg;
	int i;

	/* make sure we're on a summit box: */
	if (!use_cyclone)
		return -ENODEV;

	printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

	/* find base address: */
	offset = CYCLONE_CBAR_ADDR;
	reg = ioremap_nocache(offset, sizeof(reg));
	if (!reg) {
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR register.\n");
		return -ENODEV;
	}
	/* even on 64bit systems, this is only 32bits: */
	base = readl(reg);
	if (!base) {
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR value.\n");
		return -ENODEV;
	}
	iounmap(reg);

	/* setup PMCC: */
	offset = base + CYCLONE_PMCC_OFFSET;
	reg = ioremap_nocache(offset, sizeof(reg));
	if (!reg) {
		printk(KERN_ERR "Summit chipset: Could not find valid PMCC register.\n");
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* setup MPCS: */
	offset = base + CYCLONE_MPCS_OFFSET;
	reg = ioremap_nocache(offset, sizeof(reg));
	if (!reg) {
		printk(KERN_ERR "Summit chipset: Could not find valid MPCS register.\n");
		return -ENODEV;
	}
	writel(0x00000001,reg);
	iounmap(reg);

	/* map in cyclone_timer: */
	offset = base + CYCLONE_MPMC_OFFSET;
	cyclone_timer = ioremap_nocache(offset, sizeof(u64));
	if (!cyclone_timer) {
		printk(KERN_ERR "Summit chipset: Could not find valid MPMC register.\n");
		return -ENODEV;
	}

	/* quick test to make sure its ticking: */
	for (i = 0; i < 3; i++){
		u32 old = readl(cyclone_timer);
		int stall = 100;

		while (stall--)
			barrier();

		if (readl(cyclone_timer) == old) {
			printk(KERN_ERR "Summit chipset: Counter not counting! DISABLED\n");
			iounmap(cyclone_timer);
			cyclone_timer = NULL;
			return -ENODEV;
		}
	}
	cyclone_ptr = cyclone_timer;

	/* sort out mult/shift values: */
	clocksource_cyclone.shift = 22;
	clocksource_cyclone.mult = clocksource_hz2mult(CYCLONE_TIMER_FREQ,
						clocksource_cyclone.shift);

	return clocksource_register(&clocksource_cyclone);
}

arch_initcall(init_cyclone_clocksource);

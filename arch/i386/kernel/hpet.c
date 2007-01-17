#include <linux/clocksource.h>
#include <linux/errno.h>
#include <linux/hpet.h>
#include <linux/init.h>

#include <asm/hpet.h>
#include <asm/io.h>

#define HPET_MASK	CLOCKSOURCE_MASK(32)
#define HPET_SHIFT	22

/* FSEC = 10^-15 NSEC = 10^-9 */
#define FSEC_PER_NSEC	1000000

static void *hpet_ptr;

static cycle_t read_hpet(void)
{
	return (cycle_t)readl(hpet_ptr);
}

static struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= HPET_MASK,
	.mult		= 0, /* set below */
	.shift		= HPET_SHIFT,
	.is_continuous	= 1,
};

static int __init init_hpet_clocksource(void)
{
	unsigned long hpet_period;
	void __iomem* hpet_base;
	u64 tmp;
	int err;

	if (!is_hpet_enabled())
		return -ENODEV;

	/* calculate the hpet address: */
	hpet_base =
		(void __iomem*)ioremap_nocache(hpet_address, HPET_MMAP_SIZE);
	hpet_ptr = hpet_base + HPET_COUNTER;

	/* calculate the frequency: */
	hpet_period = readl(hpet_base + HPET_PERIOD);

	/*
	 * hpet period is in femto seconds per cycle
	 * so we need to convert this to ns/cyc units
	 * aproximated by mult/2^shift
	 *
	 *  fsec/cyc * 1nsec/1000000fsec = nsec/cyc = mult/2^shift
	 *  fsec/cyc * 1ns/1000000fsec * 2^shift = mult
	 *  fsec/cyc * 2^shift * 1nsec/1000000fsec = mult
	 *  (fsec/cyc << shift)/1000000 = mult
	 *  (hpet_period << shift)/FSEC_PER_NSEC = mult
	 */
	tmp = (u64)hpet_period << HPET_SHIFT;
	do_div(tmp, FSEC_PER_NSEC);
	clocksource_hpet.mult = (u32)tmp;

	err = clocksource_register(&clocksource_hpet);
	if (err)
		iounmap(hpet_base);

	return err;
}

module_init(init_hpet_clocksource);

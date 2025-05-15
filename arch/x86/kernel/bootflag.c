// SPDX-License-Identifier: GPL-2.0
/*
 *	Implement 'Simple Boot Flag Specification 2.0'
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <asm/io.h>

#include <linux/mc146818rtc.h>

#define SBF_RESERVED (0x78)
#define SBF_PNPOS    (1<<0)
#define SBF_BOOTING  (1<<1)
#define SBF_DIAG     (1<<2)
#define SBF_PARITY   (1<<7)

int sbf_port __initdata = -1;	/* set via acpi_boot_init() */

static void __init sbf_write(u8 v)
{
	unsigned long flags;

	if (sbf_port != -1) {
		if (!parity8(v))
			v ^= SBF_PARITY;

		printk(KERN_INFO "Simple Boot Flag at 0x%x set to 0x%x\n",
			sbf_port, v);

		spin_lock_irqsave(&rtc_lock, flags);
		CMOS_WRITE(v, sbf_port);
		spin_unlock_irqrestore(&rtc_lock, flags);
	}
}

static u8 __init sbf_read(void)
{
	unsigned long flags;
	u8 v;

	if (sbf_port == -1)
		return 0;

	spin_lock_irqsave(&rtc_lock, flags);
	v = CMOS_READ(sbf_port);
	spin_unlock_irqrestore(&rtc_lock, flags);

	return v;
}

static bool __init sbf_value_valid(u8 v)
{
	if (v & SBF_RESERVED)		/* Reserved bits */
		return false;
	if (!parity8(v))
		return false;

	return true;
}

static int __init sbf_init(void)
{
	u8 v;

	if (sbf_port == -1)
		return 0;

	v = sbf_read();
	if (!sbf_value_valid(v)) {
		printk(KERN_WARNING "Simple Boot Flag value 0x%x read from "
			"CMOS RAM was invalid\n", v);
	}

	v &= ~SBF_RESERVED;
	v &= ~SBF_BOOTING;
	v &= ~SBF_DIAG;
#if defined(CONFIG_ISAPNP)
	v |= SBF_PNPOS;
#endif
	sbf_write(v);

	return 0;
}
arch_initcall(sbf_init);

/*
 * A clocksource for Linux running on HyperV.
 *
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>
#include <asm/hypervisor.h>

#define HV_CLOCK_SHIFT	22

static cycle_t read_hv_clock(struct clocksource *arg)
{
	cycle_t current_tick;
	/*
	 * Read the partition counter to get the current tick count. This count
	 * is set to 0 when the partition is created and is incremented in
	 * 100 nanosecond units.
	 */
	rdmsrl(HV_X64_MSR_TIME_REF_COUNT, current_tick);
	return current_tick;
}

static struct clocksource hyperv_cs = {
	.name           = "hyperv_clocksource",
	.rating         = 400, /* use this when running on Hyperv*/
	.read           = read_hv_clock,
	.mask           = CLOCKSOURCE_MASK(64),
	/*
	 * The time ref counter in HyperV is in 100ns units.
	 * The definition of mult is:
	 * mult/2^shift = ns/cyc = 100
	 * mult = (100 << shift)
	 */
	.mult           = (100 << HV_CLOCK_SHIFT),
	.shift          = HV_CLOCK_SHIFT,
};

static const struct dmi_system_id __initconst
hv_timesource_dmi_table[] __maybe_unused  = {
	{
		.ident = "Hyper-V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_BOARD_NAME, "Virtual Machine"),
		},
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, hv_timesource_dmi_table);

static const struct pci_device_id __initconst
hv_timesource_pci_table[] __maybe_unused = {
	{ PCI_DEVICE(0x1414, 0x5353) }, /* VGA compatible controller */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hv_timesource_pci_table);


static int __init init_hv_clocksource(void)
{
	if ((x86_hyper != &x86_hyper_ms_hyperv) ||
		!(ms_hyperv.features & HV_X64_MSR_TIME_REF_COUNT_AVAILABLE))
		return -ENODEV;

	if (!dmi_check_system(hv_timesource_dmi_table))
		return -ENODEV;

	pr_info("Registering HyperV clock source\n");
	return clocksource_register(&hyperv_cs);
}

module_init(init_hv_clocksource);
MODULE_DESCRIPTION("HyperV based clocksource");
MODULE_AUTHOR("K. Y. Srinivasan <ksrinivasan@novell.com>");
MODULE_LICENSE("GPL");

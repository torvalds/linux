// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright (C) 2020 VMware, Inc., Palo Alto, CA., USA
 *
 * PTP clock driver for VMware precision clock virtual device.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptp_clock_kernel.h>
#include <asm/hypervisor.h>
#include <asm/vmware.h>

#define VMWARE_MAGIC 0x564D5868
#define VMWARE_CMD_PCLK(nr) ((nr << 16) | 97)
#define VMWARE_CMD_PCLK_GETTIME VMWARE_CMD_PCLK(0)

static struct acpi_device *ptp_vmw_acpi_device;
static struct ptp_clock *ptp_vmw_clock;


static int ptp_vmw_pclk_read(u64 *ns)
{
	u32 ret, nsec_hi, nsec_lo, unused1, unused2, unused3;

	asm volatile (VMWARE_HYPERCALL :
		"=a"(ret), "=b"(nsec_hi), "=c"(nsec_lo), "=d"(unused1),
		"=S"(unused2), "=D"(unused3) :
		"a"(VMWARE_MAGIC), "b"(0),
		"c"(VMWARE_CMD_PCLK_GETTIME), "d"(0) :
		"memory");

	if (ret == 0)
		*ns = ((u64)nsec_hi << 32) | nsec_lo;
	return ret;
}

/*
 * PTP clock ops.
 */

static int ptp_vmw_adjtime(struct ptp_clock_info *info, s64 delta)
{
	return -EOPNOTSUPP;
}

static int ptp_vmw_adjfreq(struct ptp_clock_info *info, s32 delta)
{
	return -EOPNOTSUPP;
}

static int ptp_vmw_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	u64 ns;

	if (ptp_vmw_pclk_read(&ns) != 0)
		return -EIO;
	*ts = ns_to_timespec64(ns);
	return 0;
}

static int ptp_vmw_settime(struct ptp_clock_info *info,
			  const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static int ptp_vmw_enable(struct ptp_clock_info *info,
			 struct ptp_clock_request *request, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info ptp_vmw_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ptp_vmw",
	.max_adj	= 0,
	.adjtime	= ptp_vmw_adjtime,
	.adjfreq	= ptp_vmw_adjfreq,
	.gettime64	= ptp_vmw_gettime,
	.settime64	= ptp_vmw_settime,
	.enable		= ptp_vmw_enable,
};

/*
 * ACPI driver ops for VMware "precision clock" virtual device.
 */

static int ptp_vmw_acpi_add(struct acpi_device *device)
{
	ptp_vmw_clock = ptp_clock_register(&ptp_vmw_clock_info, NULL);
	if (IS_ERR(ptp_vmw_clock)) {
		pr_err("failed to register ptp clock\n");
		return PTR_ERR(ptp_vmw_clock);
	}

	ptp_vmw_acpi_device = device;
	return 0;
}

static int ptp_vmw_acpi_remove(struct acpi_device *device)
{
	ptp_clock_unregister(ptp_vmw_clock);
	return 0;
}

static const struct acpi_device_id ptp_vmw_acpi_device_ids[] = {
	{ "VMW0005", 0 },
	{ "", 0 },
};

MODULE_DEVICE_TABLE(acpi, ptp_vmw_acpi_device_ids);

static struct acpi_driver ptp_vmw_acpi_driver = {
	.name = "ptp_vmw",
	.ids = ptp_vmw_acpi_device_ids,
	.ops = {
		.add = ptp_vmw_acpi_add,
		.remove	= ptp_vmw_acpi_remove
	},
	.owner	= THIS_MODULE
};

static int __init ptp_vmw_init(void)
{
	if (x86_hyper_type != X86_HYPER_VMWARE)
		return -1;
	return acpi_bus_register_driver(&ptp_vmw_acpi_driver);
}

static void __exit ptp_vmw_exit(void)
{
	acpi_bus_unregister_driver(&ptp_vmw_acpi_driver);
}

module_init(ptp_vmw_init);
module_exit(ptp_vmw_exit);

MODULE_DESCRIPTION("VMware virtual PTP clock driver");
MODULE_AUTHOR("VMware, Inc.");
MODULE_LICENSE("Dual BSD/GPL");

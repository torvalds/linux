// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gunyah: " fmt

#include <linux/arm-smccc.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/gunyah.h>
#include "hcall_ctrl.h"

#define QC_HYP_SMCCC_CALL_UID                                                  \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,              \
			   ARM_SMCCC_OWNER_VENDOR_HYP, 0x3f01)
#define QC_HYP_SMCCC_REVISION                                                  \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,              \
			   ARM_SMCCC_OWNER_VENDOR_HYP, 0xff03)

#define QC_HYP_UID0 0x19bd54bd
#define QC_HYP_UID1 0x0b37571b
#define QC_HYP_UID2 0x946f609b
#define QC_HYP_UID3 0x54539de6

#define QC_HYP1_UID0 0xbd54bd19
#define QC_HYP1_UID1 0x1b57370b
#define QC_HYP1_UID2 0x9b606f94
#define QC_HYP1_UID3 0xe69d5354

/* Use */
#undef GH_API_INFO_API_VERSION
#undef GH_API_INFO_BIG_ENDIAN
#undef GH_API_INFO_IS_64BIT
#undef GH_API_INFO_VARIANT

#define GH_API_INFO_API_VERSION(x)	(((x) >> 0) & 0x3fff)
#define GH_API_INFO_BIG_ENDIAN(x)	(((x) >> 14) & 1)
#define GH_API_INFO_IS_64BIT(x)		(((x) >> 15) & 1)
#define GH_API_INFO_VARIANT(x)		(((x) >> 56) & 0xff)

#define GH_IDENTIFY_PARTITION_CSPACE(x)	(((x) >> 0) & 1)
#define GH_IDENTIFY_DOORBELL(x)		(((x) >> 1) & 1)
#define GH_IDENTIFY_MSGQUEUE(x)		(((x) >> 2) & 1)
#define GH_IDENTIFY_VIC(x)		(((x) >> 3) & 1)
#define GH_IDENTIFY_VPM(x)		(((x) >> 4) & 1)
#define GH_IDENTIFY_VCPU(x)		(((x) >> 5) & 1)
#define GH_IDENTIFY_MEMEXTENT(x)	(((x) >> 6) & 1)
#define GH_IDENTIFY_TRACE_CTRL(x)	(((x) >> 7) & 1)
#define GH_IDENTIFY_ROOTVM_CHANNEL(x)	(((x) >> 16) & 1)
#define GH_IDENTIFY_SCHEDULER(x)	(((x) >> 28) & 0xf)

static bool qc_hyp_calls;
static struct gh_hcall_hyp_identify_resp gunyah_api;

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buffer)
{
	return scnprintf(buffer, PAGE_SIZE, "gunyah\n");
}
static struct kobj_attribute type_attr = __ATTR_RO(type);

static ssize_t api_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buffer)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n",
		(int)GH_API_INFO_API_VERSION(gunyah_api.api_info));
}
static struct kobj_attribute api_attr = __ATTR_RO(api);

static ssize_t variant_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buffer)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n",
		(int)GH_API_INFO_VARIANT(gunyah_api.api_info));
}
static struct kobj_attribute variant_attr = __ATTR_RO(variant);

static struct attribute *version_attrs[] = { &api_attr.attr,
					     &variant_attr.attr, NULL };

static const struct attribute_group version_group = {
	.name = "version",
	.attrs = version_attrs,
};

static int __init gh_sysfs_register(void)
{
	int ret;

	ret = sysfs_create_file(hypervisor_kobj, &type_attr.attr);
	if (ret)
		return ret;

	return sysfs_create_group(hypervisor_kobj, &version_group);
}

static void __exit gh_sysfs_unregister(void)
{
	sysfs_remove_file(hypervisor_kobj, &type_attr.attr);
	sysfs_remove_group(hypervisor_kobj, &version_group);
}

#if defined(CONFIG_DEBUG_FS)

#define QC_HYP_SMCCC_UART_DISABLE                                              \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,              \
			   ARM_SMCCC_OWNER_VENDOR_HYP, 0x0)
#define QC_HYP_SMCCC_UART_ENABLE                                              \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,              \
			   ARM_SMCCC_OWNER_VENDOR_HYP, 0xc)
#define ENABLE 1
#define DISABLE 0

static struct dentry *gh_dbgfs_dir;
static int hyp_uart_enable;

static void gh_control_hyp_uart(int val)
{
	switch (val) {
	case ENABLE:
	if (!hyp_uart_enable) {
		hyp_uart_enable = val;
		pr_info("Gunyah: enabling HYP UART\n");
		arm_smccc_1_1_smc(QC_HYP_SMCCC_UART_ENABLE, NULL);
	} else {
		pr_info("Gunyah: HYP UART already enabled\n");
	}
	break;
	case DISABLE:
	if (hyp_uart_enable) {
		hyp_uart_enable = val;
		pr_info("Gunyah: disabling HYP UART\n");
		arm_smccc_1_1_smc(QC_HYP_SMCCC_UART_DISABLE, NULL);
	} else {
		pr_info("Gunyah: HYP UART already disabled\n");
	}
	break;
	default:
		pr_info("Gunyah: supported values disable(0)/enable(1)\n");
	}
}

static int gh_dbgfs_trace_class_set(void *data, u64 val)
{
	return gh_error_remap(gh_hcall_trace_update_class_flags(val, 0, NULL));
}

static int gh_dbgfs_trace_class_clear(void *data, u64 val)
{
	return gh_error_remap(gh_hcall_trace_update_class_flags(0, val, NULL));
}

static int gh_dbgfs_trace_class_get(void *data, u64 *val)
{
	*val = 0;
	return gh_error_remap(gh_hcall_trace_update_class_flags(0, 0, val));
}

static int gh_dbgfs_hyp_uart_set(void *data, u64 val)
{
	gh_control_hyp_uart(val);
	return 0;
}

static int gh_dbgfs_hyp_uart_get(void *data, u64 *val)
{
	*val = hyp_uart_enable;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gh_dbgfs_trace_class_set_fops,
			 gh_dbgfs_trace_class_get,
			 gh_dbgfs_trace_class_set,
			 "0x%llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(gh_dbgfs_trace_class_clear_fops,
			 gh_dbgfs_trace_class_get,
			 gh_dbgfs_trace_class_clear,
			 "0x%llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(gh_dbgfs_hyp_uart_ctrl_fops,
			 gh_dbgfs_hyp_uart_get,
			 gh_dbgfs_hyp_uart_set,
			 "0x%llx\n");

static int __init gh_dbgfs_register(void)
{
	struct dentry *dentry;

	gh_dbgfs_dir = debugfs_create_dir("gunyah", NULL);
	if (IS_ERR_OR_NULL(gh_dbgfs_dir))
		return PTR_ERR(gh_dbgfs_dir);

	if (GH_IDENTIFY_TRACE_CTRL(gunyah_api.flags[0])) {
		dentry = debugfs_create_file("trace_set", 0600, gh_dbgfs_dir,
					NULL, &gh_dbgfs_trace_class_set_fops);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);

		dentry = debugfs_create_file("trace_clear", 0600, gh_dbgfs_dir,
					NULL, &gh_dbgfs_trace_class_clear_fops);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);

		dentry = debugfs_create_file("hyp_uart_ctrl", 0600, gh_dbgfs_dir,
					NULL, &gh_dbgfs_hyp_uart_ctrl_fops);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}

	return 0;
}

static void __exit gh_dbgfs_unregister(void)
{
	debugfs_remove_recursive(gh_dbgfs_dir);
}
#else /* !defined (CONFIG_DEBUG_FS) */
static inline int gh_dbgfs_register(void) { return 0; }
static inline int gh_dbgfs_unregister(void) { return 0; }
#endif

static int __init gh_ctrl_init(void)
{
	int ret;
	struct device_node *hyp;
	struct arm_smccc_res res;

	hyp = of_find_node_by_path("/hypervisor");

	if (!hyp || (!of_device_is_compatible(hyp, "qcom,gunyah-hypervisor") &&
		     !of_device_is_compatible(hyp, "qcom,haven-hypervisor"))) {
		pr_err("gunyah-hypervisor or haven-hypervisor node not present\n");
		return 0;
	}

	(void)gh_hcall_hyp_identify(&gunyah_api);

	if (GH_API_INFO_API_VERSION(gunyah_api.api_info) != 1) {
		pr_err("unknown version\n");
		return 0;
	}

	/* Check for ARM SMCCC VENDOR_HYP service calls by UID. */
	arm_smccc_1_1_smc(QC_HYP_SMCCC_CALL_UID, &res);
	if ((res.a0 == QC_HYP_UID0) && (res.a1 == QC_HYP_UID1) &&
	    (res.a2 == QC_HYP_UID2) && (res.a3 == QC_HYP_UID3))
		qc_hyp_calls = true;
	else if ((res.a0 == QC_HYP1_UID0) && (res.a1 == QC_HYP1_UID1) &&
	    (res.a2 == QC_HYP1_UID2) && (res.a3 == QC_HYP1_UID3))
		qc_hyp_calls = true;

	if (qc_hyp_calls) {
		ret = gh_sysfs_register();
		if (ret)
			return ret;

		ret = gh_dbgfs_register();
		if (ret)
			pr_warn("failed to register dbgfs: %d\n", ret);
	} else {
		pr_info("Gunyah: no QC HYP interface detected\n");
	}

	return 0;
}
module_init(gh_ctrl_init);

static void __exit gh_ctrl_exit(void)
{
	gh_sysfs_unregister();
	gh_dbgfs_unregister();
}
module_exit(gh_ctrl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah Hypervisor Control Driver");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Arm Limited
 */

#define pr_fmt(fmt) "SMCCC: SOC_ID: " fmt

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#define SMCCC_SOC_ID_JEP106_BANK_IDX_MASK	GENMASK(30, 24)
/*
 * As per the SMC Calling Convention specification v1.2 (ARM DEN 0028C)
 * Section 7.4 SMCCC_ARCH_SOC_ID bits[23:16] are JEP-106 identification
 * code with parity bit for the SiP. We can drop the parity bit.
 */
#define SMCCC_SOC_ID_JEP106_ID_CODE_MASK	GENMASK(22, 16)
#define SMCCC_SOC_ID_IMP_DEF_SOC_ID_MASK	GENMASK(15, 0)

#define JEP106_BANK_CONT_CODE(x)	\
	(u8)(FIELD_GET(SMCCC_SOC_ID_JEP106_BANK_IDX_MASK, (x)))
#define JEP106_ID_CODE(x)	\
	(u8)(FIELD_GET(SMCCC_SOC_ID_JEP106_ID_CODE_MASK, (x)))
#define IMP_DEF_SOC_ID(x)	\
	(u16)(FIELD_GET(SMCCC_SOC_ID_IMP_DEF_SOC_ID_MASK, (x)))

static struct soc_device *soc_dev;
static struct soc_device_attribute *soc_dev_attr;

static int __init smccc_soc_init(void)
{
	int soc_id_rev, soc_id_version;
	static char soc_id_str[20], soc_id_rev_str[12];
	static char soc_id_jep106_id_str[12];

	if (arm_smccc_get_version() < ARM_SMCCC_VERSION_1_2)
		return 0;

	soc_id_version = arm_smccc_get_soc_id_version();
	if (soc_id_version == SMCCC_RET_NOT_SUPPORTED) {
		pr_info("ARCH_SOC_ID not implemented, skipping ....\n");
		return 0;
	}

	if (soc_id_version < 0) {
		pr_err("Invalid SoC Version: %x\n", soc_id_version);
		return -EINVAL;
	}

	soc_id_rev = arm_smccc_get_soc_id_revision();
	if (soc_id_rev < 0) {
		pr_err("Invalid SoC Revision: %x\n", soc_id_rev);
		return -EINVAL;
	}

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	sprintf(soc_id_rev_str, "0x%08x", soc_id_rev);
	sprintf(soc_id_jep106_id_str, "jep106:%02x%02x",
		JEP106_BANK_CONT_CODE(soc_id_version),
		JEP106_ID_CODE(soc_id_version));
	sprintf(soc_id_str, "%s:%04x", soc_id_jep106_id_str,
		IMP_DEF_SOC_ID(soc_id_version));

	soc_dev_attr->soc_id = soc_id_str;
	soc_dev_attr->revision = soc_id_rev_str;
	soc_dev_attr->family = soc_id_jep106_id_str;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	pr_info("ID = %s Revision = %s\n", soc_dev_attr->soc_id,
		soc_dev_attr->revision);

	return 0;
}
module_init(smccc_soc_init);

static void __exit smccc_soc_exit(void)
{
	if (soc_dev)
		soc_device_unregister(soc_dev);
	kfree(soc_dev_attr);
}
module_exit(smccc_soc_exit);

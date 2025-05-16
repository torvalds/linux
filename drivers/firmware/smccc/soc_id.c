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

#ifdef CONFIG_ARM64

static char __ro_after_init smccc_soc_id_name[136] = "";

static inline void str_fragment_from_reg(char *dst, unsigned long reg)
{
	dst[0] = (reg >> 0)  & 0xff;
	dst[1] = (reg >> 8)  & 0xff;
	dst[2] = (reg >> 16) & 0xff;
	dst[3] = (reg >> 24) & 0xff;
	dst[4] = (reg >> 32) & 0xff;
	dst[5] = (reg >> 40) & 0xff;
	dst[6] = (reg >> 48) & 0xff;
	dst[7] = (reg >> 56) & 0xff;
}

static char __init *smccc_soc_name_init(void)
{
	struct arm_smccc_1_2_regs args;
	struct arm_smccc_1_2_regs res;
	size_t len;

	/*
	 * Issue Number 1.6 of the Arm SMC Calling Convention
	 * specification introduces an optional "name" string
	 * to the ARM_SMCCC_ARCH_SOC_ID function.  Fetch it if
	 * available.
	 */
	args.a0 = ARM_SMCCC_ARCH_SOC_ID;
	args.a1 = 2;    /* SOC_ID name */
	arm_smccc_1_2_invoke(&args, &res);

	if ((u32)res.a0 == 0) {
		/*
		 * Copy res.a1..res.a17 to the smccc_soc_id_name string
		 * 8 bytes at a time.  As per Issue 1.6 of the Arm SMC
		 * Calling Convention, the string will be NUL terminated
		 * and padded, from the end of the string to the end of the
		 * 136 byte buffer, with NULs.
		 */
		str_fragment_from_reg(smccc_soc_id_name + 8 * 0, res.a1);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 1, res.a2);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 2, res.a3);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 3, res.a4);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 4, res.a5);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 5, res.a6);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 6, res.a7);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 7, res.a8);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 8, res.a9);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 9, res.a10);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 10, res.a11);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 11, res.a12);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 12, res.a13);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 13, res.a14);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 14, res.a15);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 15, res.a16);
		str_fragment_from_reg(smccc_soc_id_name + 8 * 16, res.a17);

		len = strnlen(smccc_soc_id_name, sizeof(smccc_soc_id_name));
		if (len) {
			if (len == sizeof(smccc_soc_id_name))
				pr_warn(FW_BUG "Ignoring improperly formatted name\n");
			else
				return smccc_soc_id_name;
		}
	}

	return NULL;
}

#else

static char __init *smccc_soc_name_init(void)
{
	return NULL;
}

#endif

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
	soc_dev_attr->machine = smccc_soc_name_init();

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

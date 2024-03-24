// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common crypto library for storage encryption.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/crypto-qti-common.h>
#include <linux/module.h>
#include "crypto-qti-ice-regs.h"
#include "crypto-qti-platform.h"

static int ice_check_fuse_setting(void __iomem *ice_mmio)
{
	uint32_t regval;
	uint32_t version, major, minor;

	version = ice_readl(ice_mmio, ICE_REGS_VERSION);
	major = (version & ICE_CORE_MAJOR_REV_MASK) >>
			ICE_CORE_MAJOR_REV;
	minor = (version & ICE_CORE_MINOR_REV_MASK) >>
			ICE_CORE_MINOR_REV;

	//Check fuse setting is not supported on ICE 3.2 onwards
	if ((major == 0x03) && (minor >= 0x02))
		return 0;
	regval = ice_readl(ice_mmio, ICE_REGS_FUSE_SETTING);
	regval &= (ICE_FUSE_SETTING_MASK |
		ICE_FORCE_HW_KEY0_SETTING_MASK |
		ICE_FORCE_HW_KEY1_SETTING_MASK);

	if (regval) {
		pr_err("%s: error: ICE_ERROR_HW_DISABLE_FUSE_BLOWN\n",
				__func__);
		return -EPERM;
	}
	return 0;
}

static int ice_check_version(void __iomem *ice_mmio)
{
	uint32_t version, major, minor, step;

	version = ice_readl(ice_mmio, ICE_REGS_VERSION);
	major = (version & ICE_CORE_MAJOR_REV_MASK) >> ICE_CORE_MAJOR_REV;
	minor = (version & ICE_CORE_MINOR_REV_MASK) >> ICE_CORE_MINOR_REV;
	step = (version & ICE_CORE_STEP_REV_MASK) >> ICE_CORE_STEP_REV;

	if (major < ICE_CORE_CURRENT_MAJOR_VERSION) {
		pr_err("%s: Unknown ICE device at %lu, rev %d.%d.%d\n",
			__func__, (unsigned long)ice_mmio,
				major, minor, step);
		return -ENODEV;
	}

	return 0;
}

int crypto_qti_init_crypto(void *mmio_data)
{
	int err = 0;
	void __iomem *ice_mmio = (void __iomem *) mmio_data;

	err = ice_check_version(ice_mmio);
	if (err) {
		pr_err("%s: check version failed, err %d\n", __func__, err);
		return err;
	}

	err = ice_check_fuse_setting(ice_mmio);
	if (err)
		pr_err("%s: check fuse failed, err %d\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(crypto_qti_init_crypto);

static void ice_low_power_and_optimization_enable(void __iomem *ice_mmio)
{
	uint32_t regval;

	regval = ice_readl(ice_mmio, ICE_REGS_ADVANCED_CONTROL);
	/* Enable low power mode sequence
	 * [0]-0,[1]-0,[2]-0,[3]-7,[4]-0,[5]-0,[6]-0,[7]-0,
	 * Enable CONFIG_CLK_GATING, STREAM2_CLK_GATING and STREAM1_CLK_GATING
	 */
	regval |= 0x7000;
	/* Optimization enable sequence
	 */
	regval |= 0xD807100;
	ice_writel(ice_mmio, regval, ICE_REGS_ADVANCED_CONTROL);
	/*
	 * Memory barrier - to ensure write completion before next transaction
	 */
	wmb();
}

static int ice_wait_bist_status(void __iomem *ice_mmio)
{
	int count;
	uint32_t regval;

	for (count = 0; count < QTI_ICE_MAX_BIST_CHECK_COUNT; count++) {
		regval = ice_readl(ice_mmio, ICE_REGS_BIST_STATUS);
		if (!(regval & ICE_BIST_STATUS_MASK))
			break;
		udelay(50);
	}

	if (regval) {
		pr_err("%s: wait bist status failed, reg %d\n",
				__func__, regval);
		return -ETIMEDOUT;
	}

	return 0;
}

int crypto_qti_enable(void *mmio_data)
{
	int err = 0;
	void __iomem *ice_mmio = (void __iomem *) mmio_data;

	ice_low_power_and_optimization_enable(ice_mmio);
	err = ice_wait_bist_status(ice_mmio);
	if (err)
		return err;

	return err;
}
EXPORT_SYMBOL(crypto_qti_enable);

void crypto_qti_disable(void)
{
	crypto_qti_disable_platform();
}
EXPORT_SYMBOL(crypto_qti_disable);

int crypto_qti_resume(void *mmio_data)
{
	void __iomem *ice_mmio = (void __iomem *) mmio_data;

	return ice_wait_bist_status(ice_mmio);
}
EXPORT_SYMBOL(crypto_qti_resume);

static void ice_dump_test_bus(void __iomem *ice_mmio)
{
	uint32_t regval = 0x1;
	uint32_t val;
	uint8_t bus_selector;
	uint8_t stream_selector;

	pr_err("ICE TEST BUS DUMP:\n");

	for (bus_selector = 0; bus_selector <= 0xF;  bus_selector++) {
		regval = 0x1;	/* enable test bus */
		regval |= bus_selector << 28;
		if (bus_selector == 0xD)
			continue;
		ice_writel(ice_mmio, regval, ICE_REGS_TEST_BUS_CONTROL);
		/*
		 * make sure test bus selector is written before reading
		 * the test bus register
		 */
		wmb();
		val = ice_readl(ice_mmio, ICE_REGS_TEST_BUS_REG);
		pr_err("ICE_TEST_BUS_CONTROL: 0x%08x | ICE_TEST_BUS_REG: 0x%08x\n",
			regval, val);
	}

	pr_err("ICE TEST BUS DUMP (ICE_STREAM1_DATAPATH_TEST_BUS):\n");
	for (stream_selector = 0; stream_selector <= 0xF; stream_selector++) {
		regval = 0xD0000001;	/* enable stream test bus */
		regval |= stream_selector << 16;
		ice_writel(ice_mmio, regval, ICE_REGS_TEST_BUS_CONTROL);
		/*
		 * make sure test bus selector is written before reading
		 * the test bus register
		 */
		wmb();
		val = ice_readl(ice_mmio, ICE_REGS_TEST_BUS_REG);
		pr_err("ICE_TEST_BUS_CONTROL: 0x%08x | ICE_TEST_BUS_REG: 0x%08x\n",
			regval, val);
	}
}

static void ice_dump_config_regs(void __iomem *ice_mmio)
{
	int i = 0;
	uint32_t version = 0;
	uint32_t  major = 0;
	uint32_t minor = 0;

	version = ice_readl(ice_mmio, ICE_REGS_VERSION);
	major = (version & ICE_CORE_MAJOR_REV_MASK) >> ICE_CORE_MAJOR_REV;
	minor = (version & ICE_CORE_MINOR_REV_MASK) >> ICE_CORE_MINOR_REV;

	if (((major == 3) && (minor >= 2)) || (major > 3)) {
		for (i = 0; i < 64; i++) {
			pr_err("ICE_CRYPTOCFG_r_16 slot %d: 0x%08x\n", i,
				ice_readl(ice_mmio, ICE_LUT_KEYS_CRYPTOCFG_R_16 +
				ICE_LUT_KEYS_CRYPTOCFG_OFFSET*i));
			pr_err("ICE_CRYPTOCFG_r_17 slot %d: 0x%08x\n", i,
				ice_readl(ice_mmio, ICE_LUT_KEYS_CRYPTOCFG_R_17 +
				ICE_LUT_KEYS_CRYPTOCFG_OFFSET*i));
		}
	} else {
		for (i = 0; i < 32; i++) {
			pr_err("ICE_CRYPTOCFG_r_16 slot %d: 0x%08x\n", i,
				ice_readl(ice_mmio, ICE_LUT_KEYS_SW_CRYPTOCFG_R_16 +
				ICE_LUT_KEYS_CRYPTOCFG_OFFSET*i));
			pr_err("ICE_CRYPTOCFG_r_17 slot %d: 0x%08x\n", i,
				ice_readl(ice_mmio, ICE_LUT_KEYS_SW_CRYPTOCFG_R_17 +
				ICE_LUT_KEYS_CRYPTOCFG_OFFSET*i));
		}
	}
}

int crypto_qti_debug(const struct ice_mmio_data *mmio_data)
{
	void __iomem *ice_mmio =  mmio_data->ice_base_mmio;

	pr_err("%s: Dumping ICE registers\n", __func__);
	pr_err("ICE Control: 0x%08x | ICE Reset: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_CONTROL),
		ice_readl(ice_mmio, ICE_REGS_RESET));

	pr_err("ICE Version: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_VERSION));

	pr_err("ICE Param1: 0x%08x | ICE Param2:  0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_PARAMETERS_1),
		ice_readl(ice_mmio, ICE_REGS_PARAMETERS_2));

	pr_err("ICE Param3: 0x%08x | ICE Param4:  0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_PARAMETERS_3),
		ice_readl(ice_mmio, ICE_REGS_PARAMETERS_4));

	pr_err("ICE Param5: 0x%08x | ICE IRQ STTS:  0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_PARAMETERS_5),
		ice_readl(ice_mmio, ICE_REGS_NON_SEC_IRQ_STTS));

	pr_err("ICE IRQ MASK: 0x%08x | ICE IRQ CLR:	0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_NON_SEC_IRQ_MASK),
		ice_readl(ice_mmio, ICE_REGS_NON_SEC_IRQ_CLR));

	pr_err("ICE INVALID CCFG ERR STTS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_INVALID_CCFG_ERR_STTS));

	pr_err("ICE GENERAL ERR STTS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_GENERAL_ERR_STTS));

	pr_err("ICE BIST Sts: 0x%08x | ICE Bypass Sts:  0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_BIST_STATUS),
		ice_readl(ice_mmio, ICE_REGS_BYPASS_STATUS));

	pr_err("ICE ADV CTRL: 0x%08x | ICE ENDIAN SWAP:	0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_ADVANCED_CONTROL),
		ice_readl(ice_mmio, ICE_REGS_ENDIAN_SWAP));

	pr_err("ICE_STM1_ERR_SYND1: 0x%08x | ICE_STM2_ERR_SYND1: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_ERROR_SYNDROME1),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_ERROR_SYNDROME1));

	pr_err("ICE_STM1_ERR_SYND2: 0x%08x | ICE_STM2_ERR_SYND2: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_ERROR_SYNDROME2),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_ERROR_SYNDROME2));

	pr_err("ICE_STM1_ERR_SYND3: 0x%08x | ICE_STM2_ERR_SYND3: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_ERROR_SYNDROME3),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_ERROR_SYNDROME3));

	pr_err("ICE_STM1_COUNTER1: 0x%08x | ICE_STM1_COUNTER2: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS1),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS2));

	pr_err("ICE_STM1_COUNTER3: 0x%08x | ICE_STM1_COUNTER4: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS3),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS4));

	pr_err("ICE_STM2_COUNTER1: 0x%08x | ICE_STM2_COUNTER2: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS1),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS2));

	pr_err("ICE_STM2_COUNTER3: 0x%08x | ICE_STM2_COUNTER4: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS3),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS4));

	pr_err("ICE_STM1_CTR5_MSB: 0x%08x | ICE_STM1_CTR5_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS5_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS5_LSB));

	pr_err("ICE_STM1_CTR6_MSB: 0x%08x | ICE_STM1_CTR6_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS6_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS6_LSB));

	pr_err("ICE_STM1_CTR7_MSB: 0x%08x | ICE_STM1_CTR7_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS7_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS7_LSB));

	pr_err("ICE_STM1_CTR8_MSB: 0x%08x | ICE_STM1_CTR8_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS8_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS8_LSB));

	pr_err("ICE_STM1_CTR9_MSB: 0x%08x | ICE_STM1_CTR9_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS9_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM1_COUNTERS9_LSB));

	pr_err("ICE_STM2_CTR5_MSB: 0x%08x | ICE_STM2_CTR5_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS5_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS5_LSB));

	pr_err("ICE_STM2_CTR6_MSB: 0x%08x | ICE_STM2_CTR6_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS6_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS6_LSB));

	pr_err("ICE_STM2_CTR7_MSB: 0x%08x | ICE_STM2_CTR7_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS7_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS7_LSB));

	pr_err("ICE_STM2_CTR8_MSB: 0x%08x | ICE_STM2_CTR8_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS8_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS8_LSB));

	pr_err("ICE_STM2_CTR9_MSB: 0x%08x | ICE_STM2_CTR9_LSB: 0x%08x\n",
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS9_MSB),
		ice_readl(ice_mmio, ICE_REGS_STREAM2_COUNTERS9_LSB));

	pr_err("ICE_STREAM1_HWKM_RD_ERR_STS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_STREAM1_HWKM_RD_ERR_STTS));

	pr_err("ICE_STREAM2_HWKM_RD_ERR_STS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_STREAM2_HWKM_RD_ERR_STTS));

	pr_err("ICE_CONFIG_HWKM_WR_ERR_STS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_CONFIG_HWKM_WR_ERR_STTS));

	pr_err("ICE_AES_SHARE_CONTROL: 0x%08x\n",
		ice_readl(ice_mmio, ICE_AES_SHARE_CONTROL));

	pr_err("ICE_AES_CORE_STTS: 0x%08x\n",
		ice_readl(ice_mmio, ICE_AES_CORE_STTS));

	pr_err("ICE_AES_CORE_DISABLE: 0x%08x\n",
		ice_readl(ice_mmio, ICE_AES_CORE_DISABLE));

	ice_dump_config_regs(ice_mmio);

	ice_dump_test_bus(ice_mmio);

	return 0;
}
EXPORT_SYMBOL(crypto_qti_debug);

int crypto_qti_keyslot_program(const struct ice_mmio_data *mmio_data,
			       const struct blk_crypto_key *key,
			       unsigned int slot,
			       u8 data_unit_mask, int capid, int storage_type)
{
	int err = 0;

	err = crypto_qti_program_key(mmio_data, key, slot,
				data_unit_mask, capid, storage_type);
	if (err) {
		pr_err("%s: program key failed with error %d\n", __func__, err);
		err = crypto_qti_invalidate_key(mmio_data, slot, storage_type);
		if (err) {
			pr_err("%s: invalidate key failed with error %d\n",
				__func__, err);
			return err;
		}
	}

	return err;
}
EXPORT_SYMBOL(crypto_qti_keyslot_program);

int crypto_qti_keyslot_evict(const struct ice_mmio_data *mmio_data,
							 unsigned int slot, int storage_type)
{
	int err = 0;

	err = crypto_qti_invalidate_key(mmio_data, slot, storage_type);
	if (err) {
		pr_err("%s: invalidate key failed with error %d\n",
			__func__, err);
	}

	return err;
}
EXPORT_SYMBOL(crypto_qti_keyslot_evict);

int crypto_qti_derive_raw_secret(const struct ice_mmio_data *mmio_data, const u8 *wrapped_key,
				 unsigned int wrapped_key_size, u8 *secret,
				 unsigned int secret_size)
{
	int err = 0;

	if (wrapped_key_size <= RAW_SECRET_SIZE) {
		pr_err("%s: Invalid wrapped_key_size: %u\n",
				__func__, wrapped_key_size);
		err = -EINVAL;
		return err;
	}
	if (secret_size != RAW_SECRET_SIZE) {
		pr_err("%s: Invalid secret size: %u\n", __func__, secret_size);
		err = -EINVAL;
		return err;
	}

	if (wrapped_key_size > 64)
		err = crypto_qti_derive_raw_secret_platform(mmio_data, wrapped_key,
				wrapped_key_size, secret, secret_size);
	else
		memcpy(secret, wrapped_key, secret_size);

	return err;
}
EXPORT_SYMBOL(crypto_qti_derive_raw_secret);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Common crypto library for storage encryption");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2017, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "spm_driver.h"

#define MSM_SPM_PMIC_STATE_IDLE  0

enum {
	MSM_SPM_DEBUG_SHADOW = 1U << 0,
	MSM_SPM_DEBUG_VCTL = 1U << 1,
};

static int msm_spm_debug_mask;
module_param_named(
	debug_mask, msm_spm_debug_mask, int, 0664
);

struct saw2_data {
	const char *ver_name;
	uint32_t major;
	uint32_t minor;
	uint32_t *spm_reg_offset_ptr;
};

static uint32_t msm_spm_reg_offsets_saw2_v2_1[MSM_SPM_REG_NR] = {
	[MSM_SPM_REG_SAW_SECURE]		= 0x00,
	[MSM_SPM_REG_SAW_ID]			= 0x04,
	[MSM_SPM_REG_SAW_CFG]			= 0x08,
	[MSM_SPM_REG_SAW_SPM_STS]		= 0x0C,
	[MSM_SPM_REG_SAW_AVS_STS]		= 0x10,
	[MSM_SPM_REG_SAW_PMIC_STS]		= 0x14,
	[MSM_SPM_REG_SAW_RST]			= 0x18,
	[MSM_SPM_REG_SAW_VCTL]			= 0x1C,
	[MSM_SPM_REG_SAW_AVS_CTL]		= 0x20,
	[MSM_SPM_REG_SAW_AVS_LIMIT]		= 0x24,
	[MSM_SPM_REG_SAW_AVS_DLY]		= 0x28,
	[MSM_SPM_REG_SAW_AVS_HYSTERESIS]	= 0x2C,
	[MSM_SPM_REG_SAW_SPM_CTL]		= 0x30,
	[MSM_SPM_REG_SAW_SPM_DLY]		= 0x34,
	[MSM_SPM_REG_SAW_PMIC_DATA_0]		= 0x40,
	[MSM_SPM_REG_SAW_PMIC_DATA_1]		= 0x44,
	[MSM_SPM_REG_SAW_PMIC_DATA_2]		= 0x48,
	[MSM_SPM_REG_SAW_PMIC_DATA_3]		= 0x4C,
	[MSM_SPM_REG_SAW_PMIC_DATA_4]		= 0x50,
	[MSM_SPM_REG_SAW_PMIC_DATA_5]		= 0x54,
	[MSM_SPM_REG_SAW_PMIC_DATA_6]		= 0x58,
	[MSM_SPM_REG_SAW_PMIC_DATA_7]		= 0x5C,
	[MSM_SPM_REG_SAW_SEQ_ENTRY]		= 0x80,
	[MSM_SPM_REG_SAW_VERSION]		= 0xFD0,
};

static uint32_t msm_spm_reg_offsets_saw2_v3_0[MSM_SPM_REG_NR] = {
	[MSM_SPM_REG_SAW_SECURE]		= 0x00,
	[MSM_SPM_REG_SAW_ID]			= 0x04,
	[MSM_SPM_REG_SAW_CFG]			= 0x08,
	[MSM_SPM_REG_SAW_SPM_STS]		= 0x0C,
	[MSM_SPM_REG_SAW_AVS_STS]		= 0x10,
	[MSM_SPM_REG_SAW_PMIC_STS]		= 0x14,
	[MSM_SPM_REG_SAW_RST]			= 0x18,
	[MSM_SPM_REG_SAW_VCTL]			= 0x1C,
	[MSM_SPM_REG_SAW_AVS_CTL]		= 0x20,
	[MSM_SPM_REG_SAW_AVS_LIMIT]		= 0x24,
	[MSM_SPM_REG_SAW_AVS_DLY]		= 0x28,
	[MSM_SPM_REG_SAW_AVS_HYSTERESIS]	= 0x2C,
	[MSM_SPM_REG_SAW_SPM_CTL]		= 0x30,
	[MSM_SPM_REG_SAW_SPM_DLY]		= 0x34,
	[MSM_SPM_REG_SAW_STS2]			= 0x38,
	[MSM_SPM_REG_SAW_PMIC_DATA_0]		= 0x40,
	[MSM_SPM_REG_SAW_PMIC_DATA_1]		= 0x44,
	[MSM_SPM_REG_SAW_PMIC_DATA_2]		= 0x48,
	[MSM_SPM_REG_SAW_PMIC_DATA_3]		= 0x4C,
	[MSM_SPM_REG_SAW_PMIC_DATA_4]		= 0x50,
	[MSM_SPM_REG_SAW_PMIC_DATA_5]		= 0x54,
	[MSM_SPM_REG_SAW_PMIC_DATA_6]		= 0x58,
	[MSM_SPM_REG_SAW_PMIC_DATA_7]		= 0x5C,
	[MSM_SPM_REG_SAW_SEQ_ENTRY]		= 0x400,
	[MSM_SPM_REG_SAW_VERSION]		= 0xFD0,
};

static uint32_t msm_spm_reg_offsets_saw2_v4_1[MSM_SPM_REG_NR] = {
	[MSM_SPM_REG_SAW_SECURE]		= 0xC00,
	[MSM_SPM_REG_SAW_ID]			= 0xC04,
	[MSM_SPM_REG_SAW_STS2]			= 0xC10,
	[MSM_SPM_REG_SAW_SPM_STS]		= 0xC0C,
	[MSM_SPM_REG_SAW_AVS_STS]		= 0xC14,
	[MSM_SPM_REG_SAW_PMIC_STS]		= 0xC18,
	[MSM_SPM_REG_SAW_RST]			= 0xC1C,
	[MSM_SPM_REG_SAW_VCTL]			= 0x900,
	[MSM_SPM_REG_SAW_AVS_CTL]		= 0x904,
	[MSM_SPM_REG_SAW_AVS_LIMIT]		= 0x908,
	[MSM_SPM_REG_SAW_AVS_DLY]		= 0x90C,
	[MSM_SPM_REG_SAW_SPM_CTL]		= 0x0,
	[MSM_SPM_REG_SAW_SPM_DLY]		= 0x4,
	[MSM_SPM_REG_SAW_CFG]			= 0x0C,
	[MSM_SPM_REG_SAW_PMIC_DATA_0]		= 0x40,
	[MSM_SPM_REG_SAW_PMIC_DATA_1]		= 0x44,
	[MSM_SPM_REG_SAW_PMIC_DATA_2]		= 0x48,
	[MSM_SPM_REG_SAW_PMIC_DATA_3]		= 0x4C,
	[MSM_SPM_REG_SAW_PMIC_DATA_4]		= 0x50,
	[MSM_SPM_REG_SAW_PMIC_DATA_5]		= 0x54,
	[MSM_SPM_REG_SAW_SEQ_ENTRY]		= 0x400,
	[MSM_SPM_REG_SAW_VERSION]		= 0xFD0,
};

static struct saw2_data saw2_info[] = {
	[0] = {
		"SAW_v2.1",
		0x2,
		0x1,
		msm_spm_reg_offsets_saw2_v2_1,
	},
	[1] = {
		"SAW_v2.3",
		0x3,
		0x0,
		msm_spm_reg_offsets_saw2_v3_0,
	},
	[2] = {
		"SAW_v3.0",
		0x1,
		0x0,
		msm_spm_reg_offsets_saw2_v3_0,
	},
	[3] = {
		"SAW_v4.0",
		0x4,
		0x1,
		msm_spm_reg_offsets_saw2_v4_1,
	},
};

static uint32_t num_pmic_data;

static void msm_spm_drv_flush_shadow(struct msm_spm_driver_data *dev,
		unsigned int reg_index)
{
	if (!dev || reg_index >= MSM_SPM_REG_NR)
		return;

	__raw_writel(dev->reg_shadow[reg_index],
		dev->reg_base_addr + dev->reg_offsets[reg_index]);
}

static void msm_spm_drv_load_shadow(struct msm_spm_driver_data *dev,
		unsigned int reg_index)
{
	if (!dev || reg_index >= MSM_SPM_REG_NR)
		return;

	dev->reg_shadow[reg_index] =
		__raw_readl(dev->reg_base_addr +
				dev->reg_offsets[reg_index]);
}

static inline uint32_t msm_spm_drv_get_num_spm_entry(
		struct msm_spm_driver_data *dev)
{
	if (!dev)
		return -ENODEV;

	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_ID);
	return (dev->reg_shadow[MSM_SPM_REG_SAW_ID] >> 24) & 0xFF;
}

static inline void msm_spm_drv_set_start_addr(
		struct msm_spm_driver_data *dev, uint32_t ctl)
{
	dev->reg_shadow[MSM_SPM_REG_SAW_SPM_CTL] = ctl;
}

static inline bool msm_spm_pmic_arb_present(struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_ID);
	return (dev->reg_shadow[MSM_SPM_REG_SAW_ID] >> 2) & 0x1;
}

static inline void msm_spm_drv_set_vctl2(struct msm_spm_driver_data *dev,
				uint32_t vlevel, uint32_t vctl_port)
{
	unsigned int pmic_data = 0;

	pmic_data |= vlevel;
	pmic_data |= (vctl_port & 0x7) << 16;

	dev->reg_shadow[MSM_SPM_REG_SAW_VCTL] &= ~0x700FF;
	dev->reg_shadow[MSM_SPM_REG_SAW_VCTL] |= pmic_data;

	dev->reg_shadow[MSM_SPM_REG_SAW_PMIC_DATA_3] &= ~0x700FF;
	dev->reg_shadow[MSM_SPM_REG_SAW_PMIC_DATA_3] |= pmic_data;

	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_VCTL);
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_PMIC_DATA_3);
}

static inline uint32_t msm_spm_drv_get_num_pmic_data(
		struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_ID);
	mb(); /* Ensure we flush */
	return (dev->reg_shadow[MSM_SPM_REG_SAW_ID] >> 4) & 0x7;
}

static inline uint32_t msm_spm_drv_get_sts_pmic_state(
		struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_PMIC_STS);
	return (dev->reg_shadow[MSM_SPM_REG_SAW_PMIC_STS] >> 16) &
				0x03;
}

uint32_t msm_spm_drv_get_sts_curr_pmic_data(
		struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_PMIC_STS);
	return dev->reg_shadow[MSM_SPM_REG_SAW_PMIC_STS] & 0x300FF;
}

static inline void msm_spm_drv_get_saw2_ver(struct msm_spm_driver_data *dev,
		uint32_t *major, uint32_t *minor)
{
	uint32_t val = 0;

	dev->reg_shadow[MSM_SPM_REG_SAW_VERSION] =
			__raw_readl(dev->reg_base_addr + dev->ver_reg);

	val = dev->reg_shadow[MSM_SPM_REG_SAW_VERSION];

	*major = (val >> 28) & 0xF;
	*minor = (val >> 16) & 0xFFF;
}

inline int msm_spm_drv_set_spm_enable(
		struct msm_spm_driver_data *dev, bool enable)
{
	uint32_t value = enable ? 0x01 : 0x00;

	if (!dev)
		return -EINVAL;

	if ((dev->reg_shadow[MSM_SPM_REG_SAW_SPM_CTL] & 0x01) ^ value) {

		dev->reg_shadow[MSM_SPM_REG_SAW_SPM_CTL] &= ~0x1;
		dev->reg_shadow[MSM_SPM_REG_SAW_SPM_CTL] |= value;

		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_SPM_CTL);
		wmb(); /* Ensure we flush */
	}
	return 0;
}

int msm_spm_drv_get_avs_enable(struct msm_spm_driver_data *dev)
{
	if (!dev)
		return -EINVAL;

	return dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] & 0x01;
}

int msm_spm_drv_set_avs_enable(struct msm_spm_driver_data *dev,
		 bool enable)
{
	uint32_t value = enable ? 0x1 : 0x0;

	if (!dev)
		return -EINVAL;

	if ((dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] & 0x1) ^ value) {
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] &= ~0x1;
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= value;

		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
	}

	return 0;
}

int msm_spm_drv_set_avs_limit(struct msm_spm_driver_data *dev,
		uint32_t min_lvl, uint32_t max_lvl)
{
	uint32_t value = (max_lvl & 0xff) << 16 | (min_lvl & 0xff);

	if (!dev)
		return -EINVAL;

	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_LIMIT] = value;

	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_LIMIT);

	return 0;
}

static int msm_spm_drv_avs_irq_mask(enum msm_spm_avs_irq irq)
{
	switch (irq) {
	case MSM_SPM_AVS_IRQ_MIN:
		return BIT(1);
	case MSM_SPM_AVS_IRQ_MAX:
		return BIT(2);
	default:
		return -EINVAL;
	}
}

int msm_spm_drv_set_avs_irq_enable(struct msm_spm_driver_data *dev,
		enum msm_spm_avs_irq irq, bool enable)
{
	int mask = msm_spm_drv_avs_irq_mask(irq);
	uint32_t value;

	if (!dev)
		return -EINVAL;
	else if (mask < 0)
		return mask;

	value = enable ? mask : 0;

	if ((dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] & mask) ^ value) {
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] &= ~mask;
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= value;
		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
	}

	return 0;
}

int msm_spm_drv_avs_clear_irq(struct msm_spm_driver_data *dev,
		enum msm_spm_avs_irq irq)
{
	int mask = msm_spm_drv_avs_irq_mask(irq);

	if (!dev)
		return -EINVAL;
	else if (mask < 0)
		return mask;

	if (dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] & mask) {
		/*
		 * The interrupt status is cleared by disabling and then
		 * re-enabling the interrupt.
		 */
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] &= ~mask;
		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
		dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= mask;
		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
	}

	return 0;
}

void msm_spm_drv_flush_seq_entry(struct msm_spm_driver_data *dev)
{
	int i;
	int num_spm_entry = msm_spm_drv_get_num_spm_entry(dev);

	if (!dev) {
		__WARN();
		return;
	}

	for (i = 0; i < num_spm_entry; i++) {
		__raw_writel(dev->reg_seq_entry_shadow[i],
			dev->reg_base_addr
			+ dev->reg_offsets[MSM_SPM_REG_SAW_SEQ_ENTRY]
			+ 4 * i);
	}
	mb(); /* Ensure we flush */
}

void dump_regs(struct msm_spm_driver_data *dev, int cpu)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_SPM_STS);
	mb(); /* Ensure we flush */
	pr_err("CPU%d: spm register MSM_SPM_REG_SAW_SPM_STS: 0x%x\n", cpu,
			dev->reg_shadow[MSM_SPM_REG_SAW_SPM_STS]);
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_SPM_CTL);
	mb(); /* Ensure we flush */
	pr_err("CPU%d: spm register MSM_SPM_REG_SAW_SPM_CTL: 0x%x\n", cpu,
			dev->reg_shadow[MSM_SPM_REG_SAW_SPM_CTL]);
}

int msm_spm_drv_write_seq_data(struct msm_spm_driver_data *dev,
		uint8_t *cmd, uint32_t *offset)
{
	uint32_t cmd_w;
	uint32_t offset_w = *offset / 4;
	uint8_t last_cmd;

	if (!cmd)
		return -EINVAL;

	while (1) {
		int i;

		cmd_w = 0;
		last_cmd = 0;
		cmd_w = dev->reg_seq_entry_shadow[offset_w];

		for (i = (*offset % 4); i < 4; i++) {
			last_cmd = *(cmd++);
			cmd_w |=  last_cmd << (i * 8);
			(*offset)++;
			if (last_cmd == 0x0f)
				break;
		}

		dev->reg_seq_entry_shadow[offset_w++] = cmd_w;
		if (last_cmd == 0x0f)
			break;
	}

	return 0;
}

int msm_spm_drv_set_low_power_mode(struct msm_spm_driver_data *dev,
		uint32_t ctl)
{

	/* SPM is configured to reset start address to zero after end of Program
	 */
	if (!dev)
		return -EINVAL;

	msm_spm_drv_set_start_addr(dev, ctl);

	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_SPM_CTL);
	wmb(); /* Ensure we flush */

	if (msm_spm_debug_mask & MSM_SPM_DEBUG_SHADOW) {
		int i;

		for (i = 0; i < MSM_SPM_REG_NR; i++)
			pr_info("%s: reg %02x = 0x%08x\n", __func__,
				dev->reg_offsets[i], dev->reg_shadow[i]);
	}
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_SPM_STS);

	return 0;
}

uint32_t msm_spm_drv_get_vdd(struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_PMIC_STS);
	return dev->reg_shadow[MSM_SPM_REG_SAW_PMIC_STS] & 0xFF;
}

#ifdef CONFIG_MSM_AVS_HW
static bool msm_spm_drv_is_avs_enabled(struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
	return dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] & BIT(0);
}

static void msm_spm_drv_disable_avs(struct msm_spm_driver_data *dev)
{
	msm_spm_drv_load_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] &= ~BIT(27);
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
}

static void msm_spm_drv_enable_avs(struct msm_spm_driver_data *dev)
{
	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= BIT(27);
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
}

static void msm_spm_drv_set_avs_vlevel(struct msm_spm_driver_data *dev,
		unsigned int vlevel)
{
	vlevel &= 0x3f;
	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] &= ~0x7efc00;
	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= ((vlevel - 4) << 10);
	dev->reg_shadow[MSM_SPM_REG_SAW_AVS_CTL] |= (vlevel << 17);
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_AVS_CTL);
}

#else
static bool msm_spm_drv_is_avs_enabled(struct msm_spm_driver_data *dev)
{
	return false;
}

static void msm_spm_drv_disable_avs(struct msm_spm_driver_data *dev) { }

static void msm_spm_drv_enable_avs(struct msm_spm_driver_data *dev) { }

static void msm_spm_drv_set_avs_vlevel(struct msm_spm_driver_data *dev,
		unsigned int vlevel)
{
}
#endif

static inline int msm_spm_drv_validate_data(struct msm_spm_driver_data *dev,
					unsigned int vlevel, int vctl_port)
{
	int timeout_us = dev->vctl_timeout_us;
	uint32_t new_level;

	/* Confirm the voltage we set was what hardware sent and
	 * FSM is idle.
	 */
	do {
		udelay(1);
		new_level = msm_spm_drv_get_sts_curr_pmic_data(dev);

		/**
		 * VCTL_PORT has to be 0, for vlevel to be updated.
		 * If port is not 0, check for PMIC_STATE only.
		 */

		if (((new_level & 0x30000) == MSM_SPM_PMIC_STATE_IDLE) &&
				(vctl_port || ((new_level & 0xFF) == vlevel)))
			break;
	} while (--timeout_us);

	if (!timeout_us) {
		pr_err("Wrong level %#x\n", new_level);
		return -EIO;
	}

	if (msm_spm_debug_mask & MSM_SPM_DEBUG_VCTL)
		pr_info("%s: done, remaining timeout %u us\n",
			__func__, timeout_us);

	return 0;
}

int msm_spm_drv_set_vdd(struct msm_spm_driver_data *dev, unsigned int vlevel)
{
	uint32_t vlevel_set = vlevel;
	bool avs_enabled;
	int ret = 0;

	if (!dev)
		return -EINVAL;

	avs_enabled  = msm_spm_drv_is_avs_enabled(dev);

	if (!msm_spm_pmic_arb_present(dev))
		return -ENODEV;

	if (msm_spm_debug_mask & MSM_SPM_DEBUG_VCTL)
		pr_info("%s: requesting vlevel %#x\n", __func__, vlevel);

	if (avs_enabled)
		msm_spm_drv_disable_avs(dev);

	if (dev->vctl_port_ub >= 0) {
		/**
		 * VCTL can send 8bit voltage level at once.
		 * Send lower 8bit first, vlevel change happens
		 * when upper 8bit is sent.
		 */
		vlevel = vlevel_set & 0xFF;
	}

	/* Kick the state machine back to idle */
	dev->reg_shadow[MSM_SPM_REG_SAW_RST] = 1;
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_RST);

	msm_spm_drv_set_vctl2(dev, vlevel, dev->vctl_port);

	ret = msm_spm_drv_validate_data(dev, vlevel, dev->vctl_port);
	if (ret)
		goto set_vdd_bail;

	if (dev->vctl_port_ub >= 0) {
		/* Send upper 8bit of voltage level */
		vlevel = (vlevel_set >> 8) & 0xFF;

		/* Kick the state machine back to idle */
		dev->reg_shadow[MSM_SPM_REG_SAW_RST] = 1;
		msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_RST);

		/*
		 * Steps for sending for vctl port other than '0'
		 * Write VCTL register with pmic data and address index
		 * Perform system barrier
		 * Wait for 1us
		 * Read PMIC_STS register to make sure operation is complete
		 */
		msm_spm_drv_set_vctl2(dev, vlevel, dev->vctl_port_ub);

		mb(); /* To make sure data is sent before checking status */

		ret = msm_spm_drv_validate_data(dev, vlevel, dev->vctl_port_ub);
		if (ret)
			goto set_vdd_bail;
	}

	/* Set AVS min/max */
	if (avs_enabled) {
		msm_spm_drv_set_avs_vlevel(dev, vlevel_set);
		msm_spm_drv_enable_avs(dev);
	}

	return ret;

set_vdd_bail:
	if (avs_enabled)
		msm_spm_drv_enable_avs(dev);

	pr_err("%s: failed %#x vlevel setting in timeout %uus\n",
			__func__, vlevel_set, dev->vctl_timeout_us);
	return -EIO;
}

static int msm_spm_drv_get_pmic_port(struct msm_spm_driver_data *dev,
		enum msm_spm_pmic_port port)
{
	int index = -1;

	switch (port) {
	case MSM_SPM_PMIC_VCTL_PORT:
		index = dev->vctl_port;
		break;
	case MSM_SPM_PMIC_PHASE_PORT:
		index = dev->phase_port;
		break;
	case MSM_SPM_PMIC_PFM_PORT:
		index = dev->pfm_port;
		break;
	default:
		break;
	}

	return index;
}

int msm_spm_drv_set_pmic_data(struct msm_spm_driver_data *dev,
		enum msm_spm_pmic_port port, unsigned int data)
{
	unsigned int pmic_data = 0;
	unsigned int timeout_us = 0;
	int index = 0;

	if (!msm_spm_pmic_arb_present(dev))
		return -ENODEV;

	index = msm_spm_drv_get_pmic_port(dev, port);
	if (index < 0)
		return -ENODEV;

	pmic_data |= data & 0xFF;
	pmic_data |= (index & 0x7) << 16;

	dev->reg_shadow[MSM_SPM_REG_SAW_VCTL] &= ~0x700FF;
	dev->reg_shadow[MSM_SPM_REG_SAW_VCTL] |= pmic_data;
	msm_spm_drv_flush_shadow(dev, MSM_SPM_REG_SAW_VCTL);
	mb(); /* Ensure we flush */

	timeout_us = dev->vctl_timeout_us;
	/**
	 * Confirm the pmic data set was what hardware sent by
	 * checking the PMIC FSM state.
	 * We cannot use the sts_pmic_data and check it against
	 * the value like we do fot set_vdd, since the PMIC_STS
	 * is only updated for SAW_VCTL sent with port index 0.
	 */
	do {
		if (msm_spm_drv_get_sts_pmic_state(dev) ==
				MSM_SPM_PMIC_STATE_IDLE)
			break;
		udelay(1);
	} while (--timeout_us);

	if (!timeout_us) {
		pr_err("%s: failed, remaining timeout %u us, data %d\n",
				__func__, timeout_us, data);
		return -EIO;
	}

	return 0;
}

void msm_spm_drv_reinit(struct msm_spm_driver_data *dev, bool seq_write)
{
	int i;

	if (seq_write)
		msm_spm_drv_flush_seq_entry(dev);

	for (i = 0; i < MSM_SPM_REG_SAW_PMIC_DATA_0 + num_pmic_data; i++)
		msm_spm_drv_load_shadow(dev, i);

	for (i = MSM_SPM_REG_NR_INITIALIZE + 1; i < MSM_SPM_REG_NR; i++)
		msm_spm_drv_load_shadow(dev, i);
}

int msm_spm_drv_reg_init(struct msm_spm_driver_data *dev,
		struct msm_spm_platform_data *data)
{
	int i;
	bool found = false;

	dev->ver_reg = data->ver_reg;
	dev->reg_base_addr = data->reg_base_addr;
	msm_spm_drv_get_saw2_ver(dev, &dev->major, &dev->minor);
	for (i = 0; i < ARRAY_SIZE(saw2_info); i++)
		if (dev->major == saw2_info[i].major &&
			dev->minor == saw2_info[i].minor) {
			pr_debug("%s: Version found\n",
					saw2_info[i].ver_name);
			dev->reg_offsets = saw2_info[i].spm_reg_offset_ptr;
			found = true;
			break;
		}

	if (!found) {
		pr_err("%s: No SAW version found\n", __func__);
		WARN_ON(!found);
	}
	return 0;
}

void msm_spm_drv_upd_reg_shadow(struct msm_spm_driver_data *dev, int id,
		int val)
{
	dev->reg_shadow[id] = val;
	msm_spm_drv_flush_shadow(dev, id);
	/* Complete the above writes before other accesses */
	mb();
}

int msm_spm_drv_init(struct msm_spm_driver_data *dev,
		struct msm_spm_platform_data *data)
{
	int num_spm_entry;

	if (!dev || !data)
		return -ENODEV;

	dev->vctl_port = data->vctl_port;
	dev->vctl_port_ub = data->vctl_port_ub;
	dev->phase_port = data->phase_port;
	dev->pfm_port = data->pfm_port;
	dev->reg_base_addr = data->reg_base_addr;
	memcpy(dev->reg_shadow, data->reg_init_values,
			sizeof(data->reg_init_values));

	dev->vctl_timeout_us = data->vctl_timeout_us;


	if (!num_pmic_data)
		num_pmic_data = msm_spm_drv_get_num_pmic_data(dev);

	num_spm_entry = msm_spm_drv_get_num_spm_entry(dev);

	dev->reg_seq_entry_shadow =
		kcalloc(num_spm_entry, sizeof(*dev->reg_seq_entry_shadow),
				GFP_KERNEL);

	if (!dev->reg_seq_entry_shadow)
		return -ENOMEM;

	return 0;
}

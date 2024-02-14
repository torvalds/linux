// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments Ethernet Switch media-access-controller (MAC) submodule/
 * Ethernet MAC Sliver (CPGMAC_SL)
 *
 * Copyright (C) 2019 Texas Instruments
 *
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "cpsw_sl.h"

#define CPSW_SL_REG_NOTUSED U16_MAX

static const u16 cpsw_sl_reg_map_cpsw[] = {
	[CPSW_SL_IDVER] = 0x00,
	[CPSW_SL_MACCONTROL] = 0x04,
	[CPSW_SL_MACSTATUS] = 0x08,
	[CPSW_SL_SOFT_RESET] = 0x0c,
	[CPSW_SL_RX_MAXLEN] = 0x10,
	[CPSW_SL_BOFFTEST] = 0x14,
	[CPSW_SL_RX_PAUSE] = 0x18,
	[CPSW_SL_TX_PAUSE] = 0x1c,
	[CPSW_SL_EMCONTROL] = 0x20,
	[CPSW_SL_RX_PRI_MAP] = 0x24,
	[CPSW_SL_TX_GAP] = 0x28,
};

static const u16 cpsw_sl_reg_map_66ak2hk[] = {
	[CPSW_SL_IDVER] = 0x00,
	[CPSW_SL_MACCONTROL] = 0x04,
	[CPSW_SL_MACSTATUS] = 0x08,
	[CPSW_SL_SOFT_RESET] = 0x0c,
	[CPSW_SL_RX_MAXLEN] = 0x10,
	[CPSW_SL_BOFFTEST] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_RX_PAUSE] = 0x18,
	[CPSW_SL_TX_PAUSE] = 0x1c,
	[CPSW_SL_EMCONTROL] = 0x20,
	[CPSW_SL_RX_PRI_MAP] = 0x24,
	[CPSW_SL_TX_GAP] = CPSW_SL_REG_NOTUSED,
};

static const u16 cpsw_sl_reg_map_66ak2x_xgbe[] = {
	[CPSW_SL_IDVER] = 0x00,
	[CPSW_SL_MACCONTROL] = 0x04,
	[CPSW_SL_MACSTATUS] = 0x08,
	[CPSW_SL_SOFT_RESET] = 0x0c,
	[CPSW_SL_RX_MAXLEN] = 0x10,
	[CPSW_SL_BOFFTEST] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_RX_PAUSE] = 0x18,
	[CPSW_SL_TX_PAUSE] = 0x1c,
	[CPSW_SL_EMCONTROL] = 0x20,
	[CPSW_SL_RX_PRI_MAP] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_TX_GAP] = 0x28,
};

static const u16 cpsw_sl_reg_map_66ak2elg_am65[] = {
	[CPSW_SL_IDVER] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_MACCONTROL] = 0x00,
	[CPSW_SL_MACSTATUS] = 0x04,
	[CPSW_SL_SOFT_RESET] = 0x08,
	[CPSW_SL_RX_MAXLEN] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_BOFFTEST] = 0x0c,
	[CPSW_SL_RX_PAUSE] = 0x10,
	[CPSW_SL_TX_PAUSE] = 0x40,
	[CPSW_SL_EMCONTROL] = 0x70,
	[CPSW_SL_RX_PRI_MAP] = CPSW_SL_REG_NOTUSED,
	[CPSW_SL_TX_GAP] = 0x74,
};

#define CPSW_SL_SOFT_RESET_BIT		BIT(0)

#define CPSW_SL_STATUS_PN_IDLE		BIT(31)
#define CPSW_SL_AM65_STATUS_PN_E_IDLE	BIT(30)
#define CPSW_SL_AM65_STATUS_PN_P_IDLE	BIT(29)
#define CPSW_SL_AM65_STATUS_PN_TX_IDLE	BIT(28)

#define CPSW_SL_STATUS_IDLE_MASK_BASE (CPSW_SL_STATUS_PN_IDLE)

#define CPSW_SL_STATUS_IDLE_MASK_K3 \
	(CPSW_SL_STATUS_IDLE_MASK_BASE | CPSW_SL_AM65_STATUS_PN_E_IDLE | \
	 CPSW_SL_AM65_STATUS_PN_P_IDLE | CPSW_SL_AM65_STATUS_PN_TX_IDLE)

#define CPSW_SL_CTL_FUNC_BASE \
	(CPSW_SL_CTL_FULLDUPLEX |\
	CPSW_SL_CTL_LOOPBACK |\
	CPSW_SL_CTL_RX_FLOW_EN |\
	CPSW_SL_CTL_TX_FLOW_EN |\
	CPSW_SL_CTL_GMII_EN |\
	CPSW_SL_CTL_TX_PACE |\
	CPSW_SL_CTL_GIG |\
	CPSW_SL_CTL_CMD_IDLE |\
	CPSW_SL_CTL_IFCTL_A |\
	CPSW_SL_CTL_IFCTL_B |\
	CPSW_SL_CTL_GIG_FORCE |\
	CPSW_SL_CTL_EXT_EN |\
	CPSW_SL_CTL_RX_CEF_EN |\
	CPSW_SL_CTL_RX_CSF_EN |\
	CPSW_SL_CTL_RX_CMF_EN)

struct cpsw_sl {
	struct device *dev;
	void __iomem *sl_base;
	const u16 *regs;
	u32 control_features;
	u32 idle_mask;
};

struct cpsw_sl_dev_id {
	const char *device_id;
	const u16 *regs;
	const u32 control_features;
	const u32 regs_offset;
	const u32 idle_mask;
};

static const struct cpsw_sl_dev_id cpsw_sl_id_match[] = {
	{
		.device_id = "cpsw",
		.regs = cpsw_sl_reg_map_cpsw,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_MTEST |
				    CPSW_SL_CTL_TX_SHORT_GAP_EN |
				    CPSW_SL_CTL_TX_SG_LIM_EN,
		.idle_mask = CPSW_SL_STATUS_IDLE_MASK_BASE,
	},
	{
		.device_id = "66ak2hk",
		.regs = cpsw_sl_reg_map_66ak2hk,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_TX_SHORT_GAP_EN,
		.idle_mask = CPSW_SL_STATUS_IDLE_MASK_BASE,
	},
	{
		.device_id = "66ak2x_xgbe",
		.regs = cpsw_sl_reg_map_66ak2x_xgbe,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_XGIG |
				    CPSW_SL_CTL_TX_SHORT_GAP_EN |
				    CPSW_SL_CTL_CRC_TYPE |
				    CPSW_SL_CTL_XGMII_EN,
		.idle_mask = CPSW_SL_STATUS_IDLE_MASK_BASE,
	},
	{
		.device_id = "66ak2el",
		.regs = cpsw_sl_reg_map_66ak2elg_am65,
		.regs_offset = 0x330,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_MTEST |
				    CPSW_SL_CTL_TX_SHORT_GAP_EN |
				    CPSW_SL_CTL_CRC_TYPE |
				    CPSW_SL_CTL_EXT_EN_RX_FLO |
				    CPSW_SL_CTL_EXT_EN_TX_FLO |
				    CPSW_SL_CTL_TX_SG_LIM_EN,
		.idle_mask = CPSW_SL_STATUS_IDLE_MASK_BASE,
	},
	{
		.device_id = "66ak2g",
		.regs = cpsw_sl_reg_map_66ak2elg_am65,
		.regs_offset = 0x330,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_MTEST |
				    CPSW_SL_CTL_CRC_TYPE |
				    CPSW_SL_CTL_EXT_EN_RX_FLO |
				    CPSW_SL_CTL_EXT_EN_TX_FLO,
	},
	{
		.device_id = "am65",
		.regs = cpsw_sl_reg_map_66ak2elg_am65,
		.regs_offset = 0x330,
		.control_features = CPSW_SL_CTL_FUNC_BASE |
				    CPSW_SL_CTL_MTEST |
				    CPSW_SL_CTL_XGIG |
				    CPSW_SL_CTL_TX_SHORT_GAP_EN |
				    CPSW_SL_CTL_CRC_TYPE |
				    CPSW_SL_CTL_XGMII_EN |
				    CPSW_SL_CTL_EXT_EN_RX_FLO |
				    CPSW_SL_CTL_EXT_EN_TX_FLO |
				    CPSW_SL_CTL_TX_SG_LIM_EN |
				    CPSW_SL_CTL_EXT_EN_XGIG,
		.idle_mask = CPSW_SL_STATUS_IDLE_MASK_K3,
	},
	{ },
};

u32 cpsw_sl_reg_read(struct cpsw_sl *sl, enum cpsw_sl_regs reg)
{
	int val;

	if (sl->regs[reg] == CPSW_SL_REG_NOTUSED) {
		dev_err(sl->dev, "cpsw_sl: not sup r reg: %04X\n",
			sl->regs[reg]);
		return 0;
	}

	val = readl(sl->sl_base + sl->regs[reg]);
	dev_dbg(sl->dev, "cpsw_sl: reg: %04X r 0x%08X\n", sl->regs[reg], val);
	return val;
}

void cpsw_sl_reg_write(struct cpsw_sl *sl, enum cpsw_sl_regs reg, u32 val)
{
	if (sl->regs[reg] == CPSW_SL_REG_NOTUSED) {
		dev_err(sl->dev, "cpsw_sl: not sup w reg: %04X\n",
			sl->regs[reg]);
		return;
	}

	dev_dbg(sl->dev, "cpsw_sl: reg: %04X w 0x%08X\n", sl->regs[reg], val);
	writel(val, sl->sl_base + sl->regs[reg]);
}

static const struct cpsw_sl_dev_id *cpsw_sl_match_id(
		const struct cpsw_sl_dev_id *id,
		const char *device_id)
{
	if (!id || !device_id)
		return NULL;

	while (id->device_id) {
		if (strcmp(device_id, id->device_id) == 0)
			return id;
		id++;
	}
	return NULL;
}

struct cpsw_sl *cpsw_sl_get(const char *device_id, struct device *dev,
			    void __iomem *sl_base)
{
	const struct cpsw_sl_dev_id *sl_dev_id;
	struct cpsw_sl *sl;

	sl = devm_kzalloc(dev, sizeof(struct cpsw_sl), GFP_KERNEL);
	if (!sl)
		return ERR_PTR(-ENOMEM);
	sl->dev = dev;
	sl->sl_base = sl_base;

	sl_dev_id = cpsw_sl_match_id(cpsw_sl_id_match, device_id);
	if (!sl_dev_id) {
		dev_err(sl->dev, "cpsw_sl: dev_id %s not found.\n", device_id);
		return ERR_PTR(-EINVAL);
	}
	sl->regs = sl_dev_id->regs;
	sl->control_features = sl_dev_id->control_features;
	sl->idle_mask = sl_dev_id->idle_mask;
	sl->sl_base += sl_dev_id->regs_offset;

	return sl;
}

void cpsw_sl_reset(struct cpsw_sl *sl, unsigned long tmo)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(tmo);

	/* Set the soft reset bit */
	cpsw_sl_reg_write(sl, CPSW_SL_SOFT_RESET, CPSW_SL_SOFT_RESET_BIT);

	/* Wait for the bit to clear */
	do {
		usleep_range(100, 200);
	} while ((cpsw_sl_reg_read(sl, CPSW_SL_SOFT_RESET) &
		  CPSW_SL_SOFT_RESET_BIT) &&
		  time_after(timeout, jiffies));

	if (cpsw_sl_reg_read(sl, CPSW_SL_SOFT_RESET) & CPSW_SL_SOFT_RESET_BIT)
		dev_err(sl->dev, "cpsw_sl failed to soft-reset.\n");
}

u32 cpsw_sl_ctl_set(struct cpsw_sl *sl, u32 ctl_funcs)
{
	u32 val;

	if (ctl_funcs & ~sl->control_features) {
		dev_err(sl->dev, "cpsw_sl: unsupported func 0x%08X\n",
			ctl_funcs & (~sl->control_features));
		return -EINVAL;
	}

	val = cpsw_sl_reg_read(sl, CPSW_SL_MACCONTROL);
	val |= ctl_funcs;
	cpsw_sl_reg_write(sl, CPSW_SL_MACCONTROL, val);

	return 0;
}

u32 cpsw_sl_ctl_clr(struct cpsw_sl *sl, u32 ctl_funcs)
{
	u32 val;

	if (ctl_funcs & ~sl->control_features) {
		dev_err(sl->dev, "cpsw_sl: unsupported func 0x%08X\n",
			ctl_funcs & (~sl->control_features));
		return -EINVAL;
	}

	val = cpsw_sl_reg_read(sl, CPSW_SL_MACCONTROL);
	val &= ~ctl_funcs;
	cpsw_sl_reg_write(sl, CPSW_SL_MACCONTROL, val);

	return 0;
}

void cpsw_sl_ctl_reset(struct cpsw_sl *sl)
{
	cpsw_sl_reg_write(sl, CPSW_SL_MACCONTROL, 0);
}

int cpsw_sl_wait_for_idle(struct cpsw_sl *sl, unsigned long tmo)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(tmo);

	do {
		usleep_range(100, 200);
	} while (!(cpsw_sl_reg_read(sl, CPSW_SL_MACSTATUS) &
		  sl->idle_mask) && time_after(timeout, jiffies));

	if (!(cpsw_sl_reg_read(sl, CPSW_SL_MACSTATUS) & sl->idle_mask)) {
		dev_err(sl->dev, "cpsw_sl failed to soft-reset.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

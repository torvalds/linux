// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "SCHG-FLASH: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/printk.h>
#include <linux/pmic-voter.h>
#include "smb5-lib.h"
#include "schgm-flash.h"

#define IS_BETWEEN(left, right, value) \
		(((left) >= (right) && (left) >= (value) \
			&& (value) >= (right)) \
		|| ((left) <= (right) && (left) <= (value) \
			&& (value) <= (right)))

irqreturn_t smb5_schgm_flash_default_irq_handler(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;

	pr_debug("IRQ: %s\n", irq_data->name);

	return IRQ_HANDLED;
}

irqreturn_t smb5_schgm_flash_ilim2_irq_handler(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc;

	rc = smblib_write(chg, SCHGM_FLASH_S2_LATCH_RESET_CMD_REG,
				FLASH_S2_LATCH_RESET_BIT);
	if (rc < 0)
		pr_err("Couldn't reset S2_LATCH reset rc=%d\n", rc);

	return IRQ_HANDLED;
}

irqreturn_t smb5_schgm_flash_state_change_irq_handler(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc;
	u8 reg;

	rc = smblib_read(chg, SCHGM_FLASH_STATUS_3_REG, &reg);
	if (rc < 0)
		pr_err("Couldn't read flash status_3 rc=%d\n", rc);
	else
		pr_debug("Flash status changed state=[%x]\n",
					(reg && FLASH_STATE_MASK));

	return IRQ_HANDLED;
}

#define FIXED_MODE		0
#define ADAPTIVE_MODE		1
static void schgm_flash_parse_dt(struct smb_charger *chg)
{
	struct device_node *node = chg->dev->of_node;
	u32 val;
	int rc;

	chg->flash_derating_soc = -EINVAL;
	rc = of_property_read_u32(node, "qcom,flash-derating-soc", &val);
	if (!rc) {
		if (IS_BETWEEN(0, 100, val))
			chg->flash_derating_soc = (val * 255) / 100;
	}

	chg->flash_disable_soc = -EINVAL;
	rc = of_property_read_u32(node, "qcom,flash-disable-soc", &val);
	if (!rc) {
		if (IS_BETWEEN(0, 100, val))
			chg->flash_disable_soc = (val * 255) / 100;
	}

	chg->headroom_mode = -EINVAL;
	rc = of_property_read_u32(node, "qcom,headroom-mode", &val);
	if (!rc) {
		if (IS_BETWEEN(FIXED_MODE, ADAPTIVE_MODE, val))
			chg->headroom_mode = val;
	}
}

bool is_flash_active(struct smb_charger *chg)
{
	return chg->flash_active ? true : false;
}

int schgm_flash_get_vreg_ok(struct smb_charger *chg, int *val)
{
	int rc, vreg_state;
	u8 stat = 0;

	if (!chg->flash_init_done)
		return -EPERM;

	rc = smblib_read(chg, SCHGM_FLASH_STATUS_2_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read FLASH STATUS_2 rc=%d\n", rc);
		return rc;
	}
	vreg_state = !!(stat & VREG_OK_BIT);

	/* If VREG_OK is not set check for flash error */
	if (!vreg_state) {
		rc = smblib_read(chg, SCHGM_FLASH_STATUS_3_REG, &stat);
		if (rc < 0) {
			pr_err("Couldn't read FLASH_STATUS_3 rc=%d\n", rc);
			return rc;
		}
		if ((stat & FLASH_STATE_MASK) == FLASH_ERROR_VAL) {
			vreg_state = -EFAULT;
			rc = smblib_read(chg, SCHGM_FLASH_STATUS_5_REG,
					&stat);
			if (rc < 0) {
				pr_err("Couldn't read FLASH_STATUS_5 rc=%d\n",
						rc);
				return rc;
			}
			pr_debug("Flash error: status=%x\n", stat);
		}
	}

	/*
	 * val can be one of the following:
	 * 1		- VREG_OK is set.
	 * 0		- VREG_OK is 0 but no Flash error.
	 * -EFAULT	- Flash Error is set.
	 */
	*val = vreg_state;

	return 0;
}

void schgm_flash_torch_priority(struct smb_charger *chg, enum torch_mode mode)
{
	int rc;
	u8 reg;

	/*
	 * If torch is configured in default BOOST mode, skip any update in the
	 * mode configuration.
	 */
	if (chg->headroom_mode == FIXED_MODE)
		return;

	if ((mode != TORCH_BOOST_MODE) && (mode != TORCH_BUCK_MODE))
		return;

	reg = mode;
	rc = smblib_masked_write(chg, SCHGM_TORCH_PRIORITY_CONTROL_REG,
					TORCH_PRIORITY_CONTROL_BIT, reg);
	if (rc < 0)
		pr_err("Couldn't configure Torch priority control rc=%d\n",
				rc);

	pr_debug("Torch priority changed to: %d\n", mode);
}

int schgm_flash_init(struct smb_charger *chg)
{
	int rc;
	u8 reg;

	schgm_flash_parse_dt(chg);

	if (chg->flash_derating_soc != -EINVAL) {
		rc = smblib_write(chg, SCHGM_SOC_BASED_FLASH_DERATE_TH_CFG_REG,
					chg->flash_derating_soc);
		if (rc < 0) {
			pr_err("Couldn't configure SOC for flash derating rc=%d\n",
					rc);
			return rc;
		}
	}

	if (chg->flash_disable_soc != -EINVAL) {
		rc = smblib_write(chg, SCHGM_SOC_BASED_FLASH_DISABLE_TH_CFG_REG,
					chg->flash_disable_soc);
		if (rc < 0) {
			pr_err("Couldn't configure SOC for flash disable rc=%d\n",
					rc);
			return rc;
		}
	}

	if (chg->headroom_mode != -EINVAL) {
		/*
		 * configure headroom management policy for
		 * flash and torch mode.
		 */
		reg = (chg->headroom_mode == FIXED_MODE)
					? FORCE_FLASH_BOOST_5V_BIT : 0;
		rc = smblib_write(chg, SCHGM_FORCE_BOOST_CONTROL, reg);
		if (rc < 0) {
			pr_err("Couldn't write force boost control reg rc=%d\n",
					rc);
			return rc;
		}

		reg = (chg->headroom_mode == FIXED_MODE)
					? TORCH_PRIORITY_CONTROL_BIT : 0;
		rc = smblib_write(chg, SCHGM_TORCH_PRIORITY_CONTROL_REG, reg);
		if (rc < 0) {
			pr_err("Couldn't force 5V boost in torch mode rc=%d\n",
					rc);
			return rc;
		}
	}

	if ((chg->flash_derating_soc != -EINVAL)
				|| (chg->flash_disable_soc != -EINVAL)) {
		/* Check if SOC based derating/disable is enabled */
		rc = smblib_read(chg, SCHGM_FLASH_CONTROL_REG, &reg);
		if (rc < 0) {
			pr_err("Couldn't read flash control reg rc=%d\n", rc);
			return rc;
		}
		if (!(reg & SOC_LOW_FOR_FLASH_EN_BIT))
			pr_warn("Soc based flash derating not enabled\n");
	}

	chg->flash_init_done = true;

	return 0;
}

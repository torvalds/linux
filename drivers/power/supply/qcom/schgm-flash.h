/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SCHGM_FLASH_H__
#define __SCHGM_FLASH_H__

#include <linux/bitops.h>

#define SCHGM_FLASH_BASE			0xA600

#define SCHGM_FLASH_STATUS_2_REG		(SCHGM_FLASH_BASE + 0x07)
#define VREG_OK_BIT				BIT(4)

#define SCHGM_FLASH_STATUS_3_REG		(SCHGM_FLASH_BASE + 0x08)
#define FLASH_STATE_MASK			GENMASK(2, 0)
#define FLASH_ERROR_VAL				0x7

#define SCHGM_FLASH_INT_RT_STS_REG		(SCHGM_FLASH_BASE + 0x10)

#define SCHGM_FLASH_STATUS_5_REG		(SCHGM_FLASH_BASE + 0x0B)

#define SCHGM_FORCE_BOOST_CONTROL		(SCHGM_FLASH_BASE + 0x41)
#define FORCE_FLASH_BOOST_5V_BIT		BIT(0)

#define SCHGM_FLASH_S2_LATCH_RESET_CMD_REG	(SCHGM_FLASH_BASE + 0x44)
#define FLASH_S2_LATCH_RESET_BIT		BIT(0)

#define SCHGM_FLASH_CONTROL_REG			(SCHGM_FLASH_BASE + 0x60)
#define SOC_LOW_FOR_FLASH_EN_BIT		BIT(7)

#define SCHGM_TORCH_PRIORITY_CONTROL_REG	(SCHGM_FLASH_BASE + 0x63)
#define TORCH_PRIORITY_CONTROL_BIT		BIT(0)

#define SCHGM_SOC_BASED_FLASH_DERATE_TH_CFG_REG	(SCHGM_FLASH_BASE + 0x67)

#define SCHGM_SOC_BASED_FLASH_DISABLE_TH_CFG_REG \
						(SCHGM_FLASH_BASE + 0x68)

enum torch_mode {
	TORCH_BUCK_MODE = 0,
	TORCH_BOOST_MODE,
};

int schgm_flash_get_vreg_ok(struct smb_charger *chg, int *val);
void schgm_flash_torch_priority(struct smb_charger *chg, enum torch_mode mode);
int schgm_flash_init(struct smb_charger *chg);
bool is_flash_active(struct smb_charger *chg);

irqreturn_t smb5_schgm_flash_default_irq_handler(int irq, void *data);
irqreturn_t smb5_schgm_flash_ilim2_irq_handler(int irq, void *data);
irqreturn_t smb5_schgm_flash_state_change_irq_handler(int irq, void *data);
#endif /* __SCHGM_FLASH_H__ */

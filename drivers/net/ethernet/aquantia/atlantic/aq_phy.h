/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 *
 * Copyright (C) 2018-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

#ifndef AQ_PHY_H
#define AQ_PHY_H

#include <linux/mdio.h>

#include "hw_atl/hw_atl_llh.h"
#include "hw_atl/hw_atl_llh_internal.h"
#include "aq_hw_utils.h"
#include "aq_hw.h"

#define HW_ATL_PHY_ID_MAX 32U

bool aq_mdio_busy_wait(struct aq_hw_s *aq_hw);

u16 aq_mdio_read_word(struct aq_hw_s *aq_hw, u16 mmd, u16 addr);

void aq_mdio_write_word(struct aq_hw_s *aq_hw, u16 mmd, u16 addr, u16 data);

u16 aq_phy_read_reg(struct aq_hw_s *aq_hw, u16 mmd, u16 address);

void aq_phy_write_reg(struct aq_hw_s *aq_hw, u16 mmd, u16 address, u16 data);

bool aq_phy_init_phy_id(struct aq_hw_s *aq_hw);

bool aq_phy_init(struct aq_hw_s *aq_hw);

void aq_phy_disable_ptp(struct aq_hw_s *aq_hw);

#endif /* AQ_PHY_H */

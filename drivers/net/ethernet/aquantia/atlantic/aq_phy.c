// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2018-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

#include "aq_phy.h"

#define HW_ATL_PTP_DISABLE_MSK	BIT(10)

bool aq_mdio_busy_wait(struct aq_hw_s *aq_hw)
{
	int err = 0;
	u32 val;

	err = readx_poll_timeout_atomic(hw_atl_mdio_busy_get, aq_hw,
					val, val == 0U, 10U, 100000U);

	if (err < 0)
		return false;

	return true;
}

u16 aq_mdio_read_word(struct aq_hw_s *aq_hw, u16 mmd, u16 addr)
{
	u16 phy_addr = aq_hw->phy_id << 5 | mmd;

	/* Set Address register. */
	hw_atl_glb_mdio_iface4_set(aq_hw, (addr & HW_ATL_MDIO_ADDRESS_MSK) <<
				   HW_ATL_MDIO_ADDRESS_SHIFT);
	/* Send Address command. */
	hw_atl_glb_mdio_iface2_set(aq_hw, HW_ATL_MDIO_EXECUTE_OPERATION_MSK |
				   (3 << HW_ATL_MDIO_OP_MODE_SHIFT) |
				   ((phy_addr & HW_ATL_MDIO_PHY_ADDRESS_MSK) <<
				    HW_ATL_MDIO_PHY_ADDRESS_SHIFT));

	aq_mdio_busy_wait(aq_hw);

	/* Send Read command. */
	hw_atl_glb_mdio_iface2_set(aq_hw, HW_ATL_MDIO_EXECUTE_OPERATION_MSK |
				   (1 << HW_ATL_MDIO_OP_MODE_SHIFT) |
				   ((phy_addr & HW_ATL_MDIO_PHY_ADDRESS_MSK) <<
				    HW_ATL_MDIO_PHY_ADDRESS_SHIFT));
	/* Read result. */
	aq_mdio_busy_wait(aq_hw);

	return (u16)hw_atl_glb_mdio_iface5_get(aq_hw);
}

void aq_mdio_write_word(struct aq_hw_s *aq_hw, u16 mmd, u16 addr, u16 data)
{
	u16 phy_addr = aq_hw->phy_id << 5 | mmd;

	/* Set Address register. */
	hw_atl_glb_mdio_iface4_set(aq_hw, (addr & HW_ATL_MDIO_ADDRESS_MSK) <<
				   HW_ATL_MDIO_ADDRESS_SHIFT);
	/* Send Address command. */
	hw_atl_glb_mdio_iface2_set(aq_hw, HW_ATL_MDIO_EXECUTE_OPERATION_MSK |
				   (3 << HW_ATL_MDIO_OP_MODE_SHIFT) |
				   ((phy_addr & HW_ATL_MDIO_PHY_ADDRESS_MSK) <<
				    HW_ATL_MDIO_PHY_ADDRESS_SHIFT));

	aq_mdio_busy_wait(aq_hw);

	hw_atl_glb_mdio_iface3_set(aq_hw, (data & HW_ATL_MDIO_WRITE_DATA_MSK) <<
				   HW_ATL_MDIO_WRITE_DATA_SHIFT);
	/* Send Write command. */
	hw_atl_glb_mdio_iface2_set(aq_hw, HW_ATL_MDIO_EXECUTE_OPERATION_MSK |
				   (2 << HW_ATL_MDIO_OP_MODE_SHIFT) |
				   ((phy_addr & HW_ATL_MDIO_PHY_ADDRESS_MSK) <<
				    HW_ATL_MDIO_PHY_ADDRESS_SHIFT));

	aq_mdio_busy_wait(aq_hw);
}

u16 aq_phy_read_reg(struct aq_hw_s *aq_hw, u16 mmd, u16 address)
{
	int err = 0;
	u32 val;

	err = readx_poll_timeout_atomic(hw_atl_sem_mdio_get, aq_hw,
					val, val == 1U, 10U, 100000U);

	if (err < 0) {
		err = 0xffff;
		goto err_exit;
	}

	err = aq_mdio_read_word(aq_hw, mmd, address);

	hw_atl_reg_glb_cpu_sem_set(aq_hw, 1U, HW_ATL_FW_SM_MDIO);

err_exit:
	return err;
}

void aq_phy_write_reg(struct aq_hw_s *aq_hw, u16 mmd, u16 address, u16 data)
{
	int err = 0;
	u32 val;

	err = readx_poll_timeout_atomic(hw_atl_sem_mdio_get, aq_hw,
					val, val == 1U, 10U, 100000U);
	if (err < 0)
		return;

	aq_mdio_write_word(aq_hw, mmd, address, data);
	hw_atl_reg_glb_cpu_sem_set(aq_hw, 1U, HW_ATL_FW_SM_MDIO);
}

bool aq_phy_init_phy_id(struct aq_hw_s *aq_hw)
{
	u16 val;

	for (aq_hw->phy_id = 0; aq_hw->phy_id < HW_ATL_PHY_ID_MAX;
	     ++aq_hw->phy_id) {
		/* PMA Standard Device Identifier 2: Address 1.3 */
		val = aq_phy_read_reg(aq_hw, MDIO_MMD_PMAPMD, 3);

		if (val != 0xffff)
			return true;
	}

	return false;
}

bool aq_phy_init(struct aq_hw_s *aq_hw)
{
	u32 dev_id;

	if (aq_hw->phy_id == HW_ATL_PHY_ID_MAX)
		if (!aq_phy_init_phy_id(aq_hw))
			return false;

	/* PMA Standard Device Identifier:
	 * Address 1.2 = MSW,
	 * Address 1.3 = LSW
	 */
	dev_id = aq_phy_read_reg(aq_hw, MDIO_MMD_PMAPMD, 2);
	dev_id <<= 16;
	dev_id |= aq_phy_read_reg(aq_hw, MDIO_MMD_PMAPMD, 3);

	if (dev_id == 0xffffffff) {
		aq_hw->phy_id = HW_ATL_PHY_ID_MAX;
		return false;
	}

	return true;
}

void aq_phy_disable_ptp(struct aq_hw_s *aq_hw)
{
	static const u16 ptp_registers[] = {
		0x031e,
		0x031d,
		0x031c,
		0x031b,
	};
	u16 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ptp_registers); i++) {
		val = aq_phy_read_reg(aq_hw, MDIO_MMD_VEND1,
				      ptp_registers[i]);

		aq_phy_write_reg(aq_hw, MDIO_MMD_VEND1,
				 ptp_registers[i],
				 val & ~HW_ATL_PTP_DISABLE_MSK);
	}
}

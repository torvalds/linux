// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Microchip 10BASE-T1S PHYs
 *
 * Support: Microchip Phys:
 *  lan8670/1/2 Rev.B1
 *  lan8650/1 Rev.B0 Internal PHYs
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define PHY_ID_LAN867X_REVB1 0x0007C162
#define PHY_ID_LAN865X_REVB0 0x0007C1B3

#define LAN867X_REG_STS2 0x0019

#define LAN867x_RESET_COMPLETE_STS BIT(11)

#define LAN865X_REG_CFGPARAM_ADDR 0x00D8
#define LAN865X_REG_CFGPARAM_DATA 0x00D9
#define LAN865X_REG_CFGPARAM_CTRL 0x00DA
#define LAN865X_REG_STS2 0x0019

#define LAN865X_CFGPARAM_READ_ENABLE BIT(1)

/* The arrays below are pulled from the following table from AN1699
 * Access MMD Address Value Mask
 * RMW 0x1F 0x00D0 0x0002 0x0E03
 * RMW 0x1F 0x00D1 0x0000 0x0300
 * RMW 0x1F 0x0084 0x3380 0xFFC0
 * RMW 0x1F 0x0085 0x0006 0x000F
 * RMW 0x1F 0x008A 0xC000 0xF800
 * RMW 0x1F 0x0087 0x801C 0x801C
 * RMW 0x1F 0x0088 0x033F 0x1FFF
 * W   0x1F 0x008B 0x0404 ------
 * RMW 0x1F 0x0080 0x0600 0x0600
 * RMW 0x1F 0x00F1 0x2400 0x7F00
 * RMW 0x1F 0x0096 0x2000 0x2000
 * W   0x1F 0x0099 0x7F80 ------
 */

static const u32 lan867x_revb1_fixup_registers[12] = {
	0x00D0, 0x00D1, 0x0084, 0x0085,
	0x008A, 0x0087, 0x0088, 0x008B,
	0x0080, 0x00F1, 0x0096, 0x0099,
};

static const u16 lan867x_revb1_fixup_values[12] = {
	0x0002, 0x0000, 0x3380, 0x0006,
	0xC000, 0x801C, 0x033F, 0x0404,
	0x0600, 0x2400, 0x2000, 0x7F80,
};

static const u16 lan867x_revb1_fixup_masks[12] = {
	0x0E03, 0x0300, 0xFFC0, 0x000F,
	0xF800, 0x801C, 0x1FFF, 0xFFFF,
	0x0600, 0x7F00, 0x2000, 0xFFFF,
};

/* LAN865x Rev.B0 configuration parameters from AN1760 */
static const u32 lan865x_revb0_fixup_registers[28] = {
	0x0091, 0x0081, 0x0043, 0x0044,
	0x0045, 0x0053, 0x0054, 0x0055,
	0x0040, 0x0050, 0x00D0, 0x00E9,
	0x00F5, 0x00F4, 0x00F8, 0x00F9,
	0x00B0, 0x00B1, 0x00B2, 0x00B3,
	0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB,
};

static const u16 lan865x_revb0_fixup_values[28] = {
	0x9660, 0x00C0, 0x00FF, 0xFFFF,
	0x0000, 0x00FF, 0xFFFF, 0x0000,
	0x0002, 0x0002, 0x5F21, 0x9E50,
	0x1CF8, 0xC020, 0x9B00, 0x4E53,
	0x0103, 0x0910, 0x1D26, 0x002A,
	0x0103, 0x070D, 0x1720, 0x0027,
	0x0509, 0x0E13, 0x1C25, 0x002B,
};

static const u16 lan865x_revb0_fixup_cfg_regs[5] = {
	0x0084, 0x008A, 0x00AD, 0x00AE, 0x00AF
};

/* Pulled from AN1760 describing 'indirect read'
 *
 * write_register(0x4, 0x00D8, addr)
 * write_register(0x4, 0x00DA, 0x2)
 * return (int8)(read_register(0x4, 0x00D9))
 *
 * 0x4 refers to memory map selector 4, which maps to MDIO_MMD_VEND2
 */
static int lan865x_revb0_indirect_read(struct phy_device *phydev, u16 addr)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_ADDR,
			    addr);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_CTRL,
			    LAN865X_CFGPARAM_READ_ENABLE);
	if (ret)
		return ret;

	return phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_DATA);
}

/* This is pulled straight from AN1760 from 'calculation of offset 1' &
 * 'calculation of offset 2'
 */
static int lan865x_generate_cfg_offsets(struct phy_device *phydev, s8 offsets[2])
{
	const u16 fixup_regs[2] = {0x0004, 0x0008};
	int ret;

	for (int i = 0; i < ARRAY_SIZE(fixup_regs); i++) {
		ret = lan865x_revb0_indirect_read(phydev, fixup_regs[i]);
		if (ret < 0)
			return ret;
		if (ret & BIT(4))
			offsets[i] = ret | 0xE0;
		else
			offsets[i] = ret;
	}

	return 0;
}

static int lan865x_read_cfg_params(struct phy_device *phydev, u16 cfg_params[])
{
	int ret;

	for (int i = 0; i < ARRAY_SIZE(lan865x_revb0_fixup_cfg_regs); i++) {
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   lan865x_revb0_fixup_cfg_regs[i]);
		if (ret < 0)
			return ret;
		cfg_params[i] = (u16)ret;
	}

	return 0;
}

static int lan865x_write_cfg_params(struct phy_device *phydev, u16 cfg_params[])
{
	int ret;

	for (int i = 0; i < ARRAY_SIZE(lan865x_revb0_fixup_cfg_regs); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb0_fixup_cfg_regs[i],
				    cfg_params[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lan865x_setup_cfgparam(struct phy_device *phydev)
{
	u16 cfg_params[ARRAY_SIZE(lan865x_revb0_fixup_cfg_regs)];
	u16 cfg_results[5];
	s8 offsets[2];
	int ret;

	ret = lan865x_generate_cfg_offsets(phydev, offsets);
	if (ret)
		return ret;

	ret = lan865x_read_cfg_params(phydev, cfg_params);
	if (ret)
		return ret;

	cfg_results[0] = (cfg_params[0] & 0x000F) |
			  FIELD_PREP(GENMASK(15, 10), 9 + offsets[0]) |
			  FIELD_PREP(GENMASK(15, 4), 14 + offsets[0]);
	cfg_results[1] = (cfg_params[1] & 0x03FF) |
			  FIELD_PREP(GENMASK(15, 10), 40 + offsets[1]);
	cfg_results[2] = (cfg_params[2] & 0xC0C0) |
			  FIELD_PREP(GENMASK(15, 8), 5 + offsets[0]) |
			  (9 + offsets[0]);
	cfg_results[3] = (cfg_params[3] & 0xC0C0) |
			  FIELD_PREP(GENMASK(15, 8), 9 + offsets[0]) |
			  (14 + offsets[0]);
	cfg_results[4] = (cfg_params[4] & 0xC0C0) |
			  FIELD_PREP(GENMASK(15, 8), 17 + offsets[0]) |
			  (22 + offsets[0]);

	return lan865x_write_cfg_params(phydev, cfg_results);
}

static int lan865x_revb0_config_init(struct phy_device *phydev)
{
	int ret;

	/* Reference to AN1760
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/SupportingCollateral/AN-LAN8650-1-Configuration-60001760.pdf
	 */
	for (int i = 0; i < ARRAY_SIZE(lan865x_revb0_fixup_registers); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb0_fixup_registers[i],
				    lan865x_revb0_fixup_values[i]);
		if (ret)
			return ret;
	}
	/* Function to calculate and write the configuration parameters in the
	 * 0x0084, 0x008A, 0x00AD, 0x00AE and 0x00AF registers (from AN1760)
	 */
	return lan865x_setup_cfgparam(phydev);
}

static int lan867x_revb1_config_init(struct phy_device *phydev)
{
	int err;

	/* The chip completes a reset in 3us, we might get here earlier than
	 * that, as an added margin we'll conditionally sleep 5us.
	 */
	err = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_STS2);
	if (err < 0)
		return err;

	if (!(err & LAN867x_RESET_COMPLETE_STS)) {
		udelay(5);
		err = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_STS2);
		if (err < 0)
			return err;
		if (!(err & LAN867x_RESET_COMPLETE_STS)) {
			phydev_err(phydev, "PHY reset failed\n");
			return -ENODEV;
		}
	}

	/* Reference to AN1699
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/SupportingCollateral/AN-LAN8670-1-2-config-60001699.pdf
	 * AN1699 says Read, Modify, Write, but the Write is not required if the
	 * register already has the required value. So it is safe to use
	 * phy_modify_mmd here.
	 */
	for (int i = 0; i < ARRAY_SIZE(lan867x_revb1_fixup_registers); i++) {
		err = phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				     lan867x_revb1_fixup_registers[i],
				     lan867x_revb1_fixup_masks[i],
				     lan867x_revb1_fixup_values[i]);
		if (err)
			return err;
	}

	return 0;
}

static int lan86xx_read_status(struct phy_device *phydev)
{
	/* The phy has some limitations, namely:
	 *  - always reports link up
	 *  - only supports 10MBit half duplex
	 *  - does not support auto negotiate
	 */
	phydev->link = 1;
	phydev->duplex = DUPLEX_HALF;
	phydev->speed = SPEED_10;
	phydev->autoneg = AUTONEG_DISABLE;

	return 0;
}

static struct phy_driver microchip_t1s_driver[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVB1),
		.name               = "LAN867X Rev.B1",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan867x_revb1_config_init,
		.read_status        = lan86xx_read_status,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = genphy_c45_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN865X_REVB0),
		.name               = "LAN865X Rev.B0 Internal Phy",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan865x_revb0_config_init,
		.read_status        = lan86xx_read_status,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = genphy_c45_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
};

module_phy_driver(microchip_t1s_driver);

static struct mdio_device_id __maybe_unused tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVB1) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN865X_REVB0) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, tbl);

MODULE_DESCRIPTION("Microchip 10BASE-T1S PHYs driver");
MODULE_AUTHOR("Ramón Nordin Rodriguez");
MODULE_LICENSE("GPL");

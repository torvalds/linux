// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Microchip 10BASE-T1S LAN867X PHY
 *
 * Support: Microchip Phys:
 *  lan8670, lan8671, lan8672
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define PHY_ID_LAN867X 0x0007C160

#define LAN867X_REG_IRQ_1_CTL 0x001C
#define LAN867X_REG_IRQ_2_CTL 0x001D

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

static const int lan867x_fixup_registers[12] = {
	0x00D0, 0x00D1, 0x0084, 0x0085,
	0x008A, 0x0087, 0x0088, 0x008B,
	0x0080, 0x00F1, 0x0096, 0x0099,
};

static const int lan867x_fixup_values[12] = {
	0x0002, 0x0000, 0x3380, 0x0006,
	0xC000, 0x801C, 0x033F, 0x0404,
	0x0600, 0x2400, 0x2000, 0x7F80,
};

static const int lan867x_fixup_masks[12] = {
	0x0E03, 0x0300, 0xFFC0, 0x000F,
	0xF800, 0x801C, 0x1FFF, 0xFFFF,
	0x0600, 0x7F00, 0x2000, 0xFFFF,
};

static int lan867x_config_init(struct phy_device *phydev)
{
	/* HW quirk: Microchip states in the application note (AN1699) for the phy
	 * that a set of read-modify-write (rmw) operations has to be performed
	 * on a set of seemingly magic registers.
	 * The result of these operations is just described as 'optimal performance'
	 * Microchip gives no explanation as to what these mmd regs do,
	 * in fact they are marked as reserved in the datasheet.
	 * It is unclear if phy_modify_mmd would be safe to use or if a write
	 * really has to happen to each register.
	 * In order to exactly conform to what is stated in the AN phy_write_mmd is
	 * used, which might then write the same value back as read + modified.
	 */

	int reg_value;
	int err;
	int reg;

	/* Read-Modified Write Pseudocode (from AN1699)
	 * current_val = read_register(mmd, addr) // Read current register value
	 * new_val = current_val AND (NOT mask) // Clear bit fields to be written
	 * new_val = new_val OR value // Set bits
	 * write_register(mmd, addr, new_val) // Write back updated register value
	 */
	for (int i = 0; i < ARRAY_SIZE(lan867x_fixup_registers); i++) {
		reg = lan867x_fixup_registers[i];
		reg_value = phy_read_mmd(phydev, MDIO_MMD_VEND2, reg);
		reg_value &= ~lan867x_fixup_masks[i];
		reg_value |= lan867x_fixup_values[i];
		err = phy_write_mmd(phydev, MDIO_MMD_VEND2, reg, reg_value);
		if (err != 0)
			return err;
	}

	/* None of the interrupts in the lan867x phy seem relevant.
	 * Other phys inspect the link status and call phy_trigger_machine
	 * in the interrupt handler.
	 * This phy does not support link status, and thus has no interrupt
	 * for it either.
	 * So we'll just disable all interrupts on the chip.
	 */
	err = phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_IRQ_1_CTL, 0xFFFF);
	if (err != 0)
		return err;
	return phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_IRQ_2_CTL, 0xFFFF);
}

static int lan867x_read_status(struct phy_device *phydev)
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

static struct phy_driver lan867x_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_LAN867X),
		.name               = "LAN867X",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan867x_config_init,
		.read_status        = lan867x_read_status,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = genphy_c45_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	}
};

module_phy_driver(lan867x_driver);

static struct mdio_device_id __maybe_unused tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN867X) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, tbl);

MODULE_DESCRIPTION("Microchip 10BASE-T1S lan867x Phy driver");
MODULE_AUTHOR("Ramón Nordin Rodriguez");
MODULE_LICENSE("GPL");

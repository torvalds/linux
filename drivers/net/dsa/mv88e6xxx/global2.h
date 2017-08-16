/*
 * Marvell 88E6xxx Switch Global 2 Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_GLOBAL2_H
#define _MV88E6XXX_GLOBAL2_H

#include "chip.h"

#define MV88E6XXX_G2	0x1c

/* Offset 0x00: Interrupt Source Register */
#define MV88E6XXX_G2_INT_SOURCE			0x00
#define MV88E6XXX_G2_INT_SOURCE_WATCHDOG	15

/* Offset 0x01: Interrupt Mask Register */
#define MV88E6XXX_G2_INT_MASK	0x01

/* Offset 0x02: MGMT Enable Register 2x */
#define MV88E6XXX_G2_MGMT_EN_2X		0x02

/* Offset 0x03: MGMT Enable Register 0x */
#define MV88E6XXX_G2_MGMT_EN_0X		0x03

/* Offset 0x04: Flow Control Delay Register */
#define MV88E6XXX_G2_FLOW_CTL	0x04

/* Offset 0x05: Switch Management Register */
#define MV88E6XXX_G2_SWITCH_MGMT			0x05
#define MV88E6XXX_G2_SWITCH_MGMT_USE_DOUBLE_TAG_DATA	0x8000
#define MV88E6XXX_G2_SWITCH_MGMT_PREVENT_LOOPS		0x4000
#define MV88E6XXX_G2_SWITCH_MGMT_FLOW_CTL_MSG		0x2000
#define MV88E6XXX_G2_SWITCH_MGMT_FORCE_FLOW_CTL_PRI	0x0080
#define MV88E6XXX_G2_SWITCH_MGMT_RSVD2CPU		0x0008

/* Offset 0x06: Device Mapping Table Register */
#define MV88E6XXX_G2_DEVICE_MAPPING		0x06
#define MV88E6XXX_G2_DEVICE_MAPPING_UPDATE	0x8000
#define MV88E6XXX_G2_DEVICE_MAPPING_DEV_MASK	0x1f00
#define MV88E6XXX_G2_DEVICE_MAPPING_PORT_MASK	0x000f

/* Offset 0x07: Trunk Mask Table Register */
#define MV88E6XXX_G2_TRUNK_MASK			0x07
#define MV88E6XXX_G2_TRUNK_MASK_UPDATE		0x8000
#define MV88E6XXX_G2_TRUNK_MASK_NUM_MASK	0x7000
#define MV88E6XXX_G2_TRUNK_MASK_HASH		0x0800

/* Offset 0x08: Trunk Mapping Table Register */
#define MV88E6XXX_G2_TRUNK_MAPPING		0x08
#define MV88E6XXX_G2_TRUNK_MAPPING_UPDATE	0x8000
#define MV88E6XXX_G2_TRUNK_MAPPING_ID_MASK	0x7800

/* Offset 0x09: Ingress Rate Command Register */
#define MV88E6XXX_G2_IRL_CMD			0x09
#define MV88E6XXX_G2_IRL_CMD_BUSY		0x8000
#define MV88E6352_G2_IRL_CMD_OP_MASK		0x7000
#define MV88E6352_G2_IRL_CMD_OP_NOOP		0x0000
#define MV88E6352_G2_IRL_CMD_OP_INIT_ALL	0x1000
#define MV88E6352_G2_IRL_CMD_OP_INIT_RES	0x2000
#define MV88E6352_G2_IRL_CMD_OP_WRITE_REG	0x3000
#define MV88E6352_G2_IRL_CMD_OP_READ_REG	0x4000
#define MV88E6390_G2_IRL_CMD_OP_MASK		0x6000
#define MV88E6390_G2_IRL_CMD_OP_READ_REG	0x0000
#define MV88E6390_G2_IRL_CMD_OP_INIT_ALL	0x2000
#define MV88E6390_G2_IRL_CMD_OP_INIT_RES	0x4000
#define MV88E6390_G2_IRL_CMD_OP_WRITE_REG	0x6000
#define MV88E6352_G2_IRL_CMD_PORT_MASK		0x0f00
#define MV88E6390_G2_IRL_CMD_PORT_MASK		0x1f00
#define MV88E6XXX_G2_IRL_CMD_RES_MASK		0x00e0
#define MV88E6XXX_G2_IRL_CMD_REG_MASK		0x000f

/* Offset 0x0A: Ingress Rate Data Register */
#define MV88E6XXX_G2_IRL_DATA		0x0a
#define MV88E6XXX_G2_IRL_DATA_MASK	0xffff

/* Offset 0x0B: Cross-chip Port VLAN Register */
#define MV88E6XXX_G2_PVT_ADDR			0x0b
#define MV88E6XXX_G2_PVT_ADDR_BUSY		0x8000
#define MV88E6XXX_G2_PVT_ADDR_OP_MASK		0x7000
#define MV88E6XXX_G2_PVT_ADDR_OP_INIT_ONES	0x1000
#define MV88E6XXX_G2_PVT_ADDR_OP_WRITE_PVLAN	0x3000
#define MV88E6XXX_G2_PVT_ADDR_OP_READ		0x4000
#define MV88E6XXX_G2_PVT_ADDR_PTR_MASK		0x01ff

/* Offset 0x0C: Cross-chip Port VLAN Data Register */
#define MV88E6XXX_G2_PVT_DATA		0x0c
#define MV88E6XXX_G2_PVT_DATA_MASK	0x7f

/* Offset 0x0D: Switch MAC/WoL/WoF Register */
#define MV88E6XXX_G2_SWITCH_MAC			0x0d
#define MV88E6XXX_G2_SWITCH_MAC_UPDATE		0x8000
#define MV88E6XXX_G2_SWITCH_MAC_PTR_MASK	0x1f00
#define MV88E6XXX_G2_SWITCH_MAC_DATA_MASK	0x00ff

/* Offset 0x0E: ATU Stats Register */
#define MV88E6XXX_G2_ATU_STATS		0x0e

/* Offset 0x0F: Priority Override Table */
#define MV88E6XXX_G2_PRIO_OVERRIDE		0x0f
#define MV88E6XXX_G2_PRIO_OVERRIDE_UPDATE	0x8000
#define MV88E6XXX_G2_PRIO_OVERRIDE_FPRISET	0x1000
#define MV88E6XXX_G2_PRIO_OVERRIDE_PTR_MASK	0x0f00
#define MV88E6352_G2_PRIO_OVERRIDE_QPRIAVBEN	0x0080
#define MV88E6352_G2_PRIO_OVERRIDE_DATAAVB_MASK	0x0030
#define MV88E6XXX_G2_PRIO_OVERRIDE_QFPRIEN	0x0008
#define MV88E6XXX_G2_PRIO_OVERRIDE_DATA_MASK	0x0007

/* Offset 0x14: EEPROM Command */
#define MV88E6XXX_G2_EEPROM_CMD			0x14
#define MV88E6XXX_G2_EEPROM_CMD_BUSY		0x8000
#define MV88E6XXX_G2_EEPROM_CMD_OP_MASK		0x7000
#define MV88E6XXX_G2_EEPROM_CMD_OP_WRITE	0x3000
#define MV88E6XXX_G2_EEPROM_CMD_OP_READ		0x4000
#define MV88E6XXX_G2_EEPROM_CMD_OP_LOAD		0x6000
#define MV88E6XXX_G2_EEPROM_CMD_RUNNING		0x0800
#define MV88E6XXX_G2_EEPROM_CMD_WRITE_EN	0x0400
#define MV88E6352_G2_EEPROM_CMD_ADDR_MASK	0x00ff
#define MV88E6390_G2_EEPROM_CMD_DATA_MASK	0x00ff

/* Offset 0x15: EEPROM Data */
#define MV88E6352_G2_EEPROM_DATA	0x15
#define MV88E6352_G2_EEPROM_DATA_MASK	0xffff

/* Offset 0x15: EEPROM Addr */
#define MV88E6390_G2_EEPROM_ADDR	0x15
#define MV88E6390_G2_EEPROM_ADDR_MASK	0xffff

/* Offset 0x16: AVB Command Register */
#define MV88E6352_G2_AVB_CMD		0x16

/* Offset 0x17: AVB Data Register */
#define MV88E6352_G2_AVB_DATA		0x17

/* Offset 0x18: SMI PHY Command Register */
#define MV88E6XXX_G2_SMI_PHY_CMD			0x18
#define MV88E6XXX_G2_SMI_PHY_CMD_BUSY			0x8000
#define MV88E6390_G2_SMI_PHY_CMD_FUNC_MASK		0x6000
#define MV88E6390_G2_SMI_PHY_CMD_FUNC_INTERNAL		0x0000
#define MV88E6390_G2_SMI_PHY_CMD_FUNC_EXTERNAL		0x2000
#define MV88E6390_G2_SMI_PHY_CMD_FUNC_SETUP		0x4000
#define MV88E6XXX_G2_SMI_PHY_CMD_MODE_MASK		0x1000
#define MV88E6XXX_G2_SMI_PHY_CMD_MODE_45		0x0000
#define MV88E6XXX_G2_SMI_PHY_CMD_MODE_22		0x1000
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_MASK		0x0c00
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_22_WRITE_DATA	0x0400
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_22_READ_DATA	0x0800
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_45_WRITE_ADDR	0x0000
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_45_WRITE_DATA	0x0400
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_45_READ_DATA_INC	0x0800
#define MV88E6XXX_G2_SMI_PHY_CMD_OP_45_READ_DATA	0x0c00
#define MV88E6XXX_G2_SMI_PHY_CMD_DEV_ADDR_MASK		0x03e0
#define MV88E6XXX_G2_SMI_PHY_CMD_REG_ADDR_MASK		0x001f
#define MV88E6XXX_G2_SMI_PHY_CMD_SETUP_PTR_MASK		0x03ff

/* Offset 0x19: SMI PHY Data Register */
#define MV88E6XXX_G2_SMI_PHY_DATA	0x19

/* Offset 0x1A: Scratch and Misc. Register */
#define MV88E6XXX_G2_SCRATCH_MISC_MISC		0x1a
#define MV88E6XXX_G2_SCRATCH_MISC_UPDATE	0x8000
#define MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK	0x7f00
#define MV88E6XXX_G2_SCRATCH_MISC_DATA_MASK	0x00ff

/* Offset 0x1B: Watch Dog Control Register */
#define MV88E6352_G2_WDOG_CTL			0x1b
#define MV88E6352_G2_WDOG_CTL_EGRESS_EVENT	0x0080
#define MV88E6352_G2_WDOG_CTL_RMU_TIMEOUT	0x0040
#define MV88E6352_G2_WDOG_CTL_QC_ENABLE		0x0020
#define MV88E6352_G2_WDOG_CTL_EGRESS_HISTORY	0x0010
#define MV88E6352_G2_WDOG_CTL_EGRESS_ENABLE	0x0008
#define MV88E6352_G2_WDOG_CTL_FORCE_IRQ		0x0004
#define MV88E6352_G2_WDOG_CTL_HISTORY		0x0002
#define MV88E6352_G2_WDOG_CTL_SWRESET		0x0001

/* Offset 0x1B: Watch Dog Control Register */
#define MV88E6390_G2_WDOG_CTL				0x1b
#define MV88E6390_G2_WDOG_CTL_UPDATE			0x8000
#define MV88E6390_G2_WDOG_CTL_PTR_MASK			0x7f00
#define MV88E6390_G2_WDOG_CTL_PTR_INT_SOURCE		0x0000
#define MV88E6390_G2_WDOG_CTL_PTR_INT_STS		0x1000
#define MV88E6390_G2_WDOG_CTL_PTR_INT_ENABLE		0x1100
#define MV88E6390_G2_WDOG_CTL_PTR_EVENT			0x1200
#define MV88E6390_G2_WDOG_CTL_PTR_HISTORY		0x1300
#define MV88E6390_G2_WDOG_CTL_DATA_MASK			0x00ff
#define MV88E6390_G2_WDOG_CTL_CUT_THROUGH		0x0008
#define MV88E6390_G2_WDOG_CTL_QUEUE_CONTROLLER		0x0004
#define MV88E6390_G2_WDOG_CTL_EGRESS			0x0002
#define MV88E6390_G2_WDOG_CTL_FORCE_IRQ			0x0001

/* Offset 0x1C: QoS Weights Register */
#define MV88E6XXX_G2_QOS_WEIGHTS		0x1c
#define MV88E6XXX_G2_QOS_WEIGHTS_UPDATE		0x8000
#define MV88E6352_G2_QOS_WEIGHTS_PTR_MASK	0x3f00
#define MV88E6390_G2_QOS_WEIGHTS_PTR_MASK	0x7f00
#define MV88E6XXX_G2_QOS_WEIGHTS_DATA_MASK	0x00ff

/* Offset 0x1D: Misc Register */
#define MV88E6XXX_G2_MISC		0x1d
#define MV88E6XXX_G2_MISC_5_BIT_PORT	0x4000
#define MV88E6352_G2_NOEGR_POLICY	0x2000
#define MV88E6390_G2_LAG_ID_4		0x2000

#ifdef CONFIG_NET_DSA_MV88E6XXX_GLOBAL2

static inline int mv88e6xxx_g2_require(struct mv88e6xxx_chip *chip)
{
	return 0;
}

int mv88e6352_g2_irl_init_all(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g2_irl_init_all(struct mv88e6xxx_chip *chip, int port);

int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip,
			      struct mii_bus *bus,
			      int addr, int reg, u16 *val);
int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip,
			       struct mii_bus *bus,
			       int addr, int reg, u16 val);
int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr);

int mv88e6xxx_g2_get_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_set_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data);

int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);

int mv88e6xxx_g2_pvt_write(struct mv88e6xxx_chip *chip, int src_dev,
			   int src_port, u16 data);
int mv88e6xxx_g2_misc_4_bit_port(struct mv88e6xxx_chip *chip);

int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip);
int mv88e6095_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip);

extern const struct mv88e6xxx_irq_ops mv88e6097_watchdog_ops;
extern const struct mv88e6xxx_irq_ops mv88e6390_watchdog_ops;

#else /* !CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 */

static inline int mv88e6xxx_g2_require(struct mv88e6xxx_chip *chip)
{
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_GLOBAL2)) {
		dev_err(chip->dev, "this chip requires CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 enabled\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static inline int mv88e6352_g2_irl_init_all(struct mv88e6xxx_chip *chip,
					    int port)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6390_g2_irl_init_all(struct mv88e6xxx_chip *chip,
					    int port)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip,
					    struct mii_bus *bus,
					    int addr, int reg, u16 *val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip,
					     struct mii_bus *bus,
					     int addr, int reg, u16 val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip,
					      u8 *addr)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_get_eeprom8(struct mv88e6xxx_chip *chip,
					   struct ethtool_eeprom *eeprom,
					   u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_eeprom8(struct mv88e6xxx_chip *chip,
					   struct ethtool_eeprom *eeprom,
					   u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
					    struct ethtool_eeprom *eeprom,
					    u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
					    struct ethtool_eeprom *eeprom,
					    u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_pvt_write(struct mv88e6xxx_chip *chip,
					 int src_dev, int src_port, u16 data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_misc_4_bit_port(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip)
{
}

static inline int mv88e6095_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static const struct mv88e6xxx_irq_ops mv88e6097_watchdog_ops = {};
static const struct mv88e6xxx_irq_ops mv88e6390_watchdog_ops = {};

#endif /* CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 */

#endif /* _MV88E6XXX_GLOBAL2_H */

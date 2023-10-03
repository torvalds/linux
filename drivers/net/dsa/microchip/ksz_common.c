// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip switch driver main logic
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#include <linux/delay.h>
#include <linux/dsa/ksz_common.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/if_hsr.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/micrel_phy.h>
#include <net/dsa.h>
#include <net/pkt_cls.h>
#include <net/switchdev.h>

#include "ksz_common.h"
#include "ksz_ptp.h"
#include "ksz8.h"
#include "ksz9477.h"
#include "lan937x.h"

#define MIB_COUNTER_NUM 0x20

struct ksz_stats_raw {
	u64 rx_hi;
	u64 rx_undersize;
	u64 rx_fragments;
	u64 rx_oversize;
	u64 rx_jabbers;
	u64 rx_symbol_err;
	u64 rx_crc_err;
	u64 rx_align_err;
	u64 rx_mac_ctrl;
	u64 rx_pause;
	u64 rx_bcast;
	u64 rx_mcast;
	u64 rx_ucast;
	u64 rx_64_or_less;
	u64 rx_65_127;
	u64 rx_128_255;
	u64 rx_256_511;
	u64 rx_512_1023;
	u64 rx_1024_1522;
	u64 rx_1523_2000;
	u64 rx_2001;
	u64 tx_hi;
	u64 tx_late_col;
	u64 tx_pause;
	u64 tx_bcast;
	u64 tx_mcast;
	u64 tx_ucast;
	u64 tx_deferred;
	u64 tx_total_col;
	u64 tx_exc_col;
	u64 tx_single_col;
	u64 tx_mult_col;
	u64 rx_total;
	u64 tx_total;
	u64 rx_discards;
	u64 tx_discards;
};

struct ksz88xx_stats_raw {
	u64 rx;
	u64 rx_hi;
	u64 rx_undersize;
	u64 rx_fragments;
	u64 rx_oversize;
	u64 rx_jabbers;
	u64 rx_symbol_err;
	u64 rx_crc_err;
	u64 rx_align_err;
	u64 rx_mac_ctrl;
	u64 rx_pause;
	u64 rx_bcast;
	u64 rx_mcast;
	u64 rx_ucast;
	u64 rx_64_or_less;
	u64 rx_65_127;
	u64 rx_128_255;
	u64 rx_256_511;
	u64 rx_512_1023;
	u64 rx_1024_1522;
	u64 tx;
	u64 tx_hi;
	u64 tx_late_col;
	u64 tx_pause;
	u64 tx_bcast;
	u64 tx_mcast;
	u64 tx_ucast;
	u64 tx_deferred;
	u64 tx_total_col;
	u64 tx_exc_col;
	u64 tx_single_col;
	u64 tx_mult_col;
	u64 rx_discards;
	u64 tx_discards;
};

static const struct ksz_mib_names ksz88xx_mib_names[] = {
	{ 0x00, "rx" },
	{ 0x01, "rx_hi" },
	{ 0x02, "rx_undersize" },
	{ 0x03, "rx_fragments" },
	{ 0x04, "rx_oversize" },
	{ 0x05, "rx_jabbers" },
	{ 0x06, "rx_symbol_err" },
	{ 0x07, "rx_crc_err" },
	{ 0x08, "rx_align_err" },
	{ 0x09, "rx_mac_ctrl" },
	{ 0x0a, "rx_pause" },
	{ 0x0b, "rx_bcast" },
	{ 0x0c, "rx_mcast" },
	{ 0x0d, "rx_ucast" },
	{ 0x0e, "rx_64_or_less" },
	{ 0x0f, "rx_65_127" },
	{ 0x10, "rx_128_255" },
	{ 0x11, "rx_256_511" },
	{ 0x12, "rx_512_1023" },
	{ 0x13, "rx_1024_1522" },
	{ 0x14, "tx" },
	{ 0x15, "tx_hi" },
	{ 0x16, "tx_late_col" },
	{ 0x17, "tx_pause" },
	{ 0x18, "tx_bcast" },
	{ 0x19, "tx_mcast" },
	{ 0x1a, "tx_ucast" },
	{ 0x1b, "tx_deferred" },
	{ 0x1c, "tx_total_col" },
	{ 0x1d, "tx_exc_col" },
	{ 0x1e, "tx_single_col" },
	{ 0x1f, "tx_mult_col" },
	{ 0x100, "rx_discards" },
	{ 0x101, "tx_discards" },
};

static const struct ksz_mib_names ksz9477_mib_names[] = {
	{ 0x00, "rx_hi" },
	{ 0x01, "rx_undersize" },
	{ 0x02, "rx_fragments" },
	{ 0x03, "rx_oversize" },
	{ 0x04, "rx_jabbers" },
	{ 0x05, "rx_symbol_err" },
	{ 0x06, "rx_crc_err" },
	{ 0x07, "rx_align_err" },
	{ 0x08, "rx_mac_ctrl" },
	{ 0x09, "rx_pause" },
	{ 0x0A, "rx_bcast" },
	{ 0x0B, "rx_mcast" },
	{ 0x0C, "rx_ucast" },
	{ 0x0D, "rx_64_or_less" },
	{ 0x0E, "rx_65_127" },
	{ 0x0F, "rx_128_255" },
	{ 0x10, "rx_256_511" },
	{ 0x11, "rx_512_1023" },
	{ 0x12, "rx_1024_1522" },
	{ 0x13, "rx_1523_2000" },
	{ 0x14, "rx_2001" },
	{ 0x15, "tx_hi" },
	{ 0x16, "tx_late_col" },
	{ 0x17, "tx_pause" },
	{ 0x18, "tx_bcast" },
	{ 0x19, "tx_mcast" },
	{ 0x1A, "tx_ucast" },
	{ 0x1B, "tx_deferred" },
	{ 0x1C, "tx_total_col" },
	{ 0x1D, "tx_exc_col" },
	{ 0x1E, "tx_single_col" },
	{ 0x1F, "tx_mult_col" },
	{ 0x80, "rx_total" },
	{ 0x81, "tx_total" },
	{ 0x82, "rx_discards" },
	{ 0x83, "tx_discards" },
};

struct ksz_driver_strength_prop {
	const char *name;
	int offset;
	int value;
};

enum ksz_driver_strength_type {
	KSZ_DRIVER_STRENGTH_HI,
	KSZ_DRIVER_STRENGTH_LO,
	KSZ_DRIVER_STRENGTH_IO,
};

/**
 * struct ksz_drive_strength - drive strength mapping
 * @reg_val:	register value
 * @microamp:	microamp value
 */
struct ksz_drive_strength {
	u32 reg_val;
	u32 microamp;
};

/* ksz9477_drive_strengths - Drive strength mapping for KSZ9477 variants
 *
 * This values are not documented in KSZ9477 variants but confirmed by
 * Microchip that KSZ9477, KSZ9567, KSZ8567, KSZ9897, KSZ9896, KSZ9563, KSZ9893
 * and KSZ8563 are using same register (drive strength) settings like KSZ8795.
 *
 * Documentation in KSZ8795CLX provides more information with some
 * recommendations:
 * - for high speed signals
 *   1. 4 mA or 8 mA is often used for MII, RMII, and SPI interface with using
 *      2.5V or 3.3V VDDIO.
 *   2. 12 mA or 16 mA is often used for MII, RMII, and SPI interface with
 *      using 1.8V VDDIO.
 *   3. 20 mA or 24 mA is often used for GMII/RGMII interface with using 2.5V
 *      or 3.3V VDDIO.
 *   4. 28 mA is often used for GMII/RGMII interface with using 1.8V VDDIO.
 *   5. In same interface, the heavy loading should use higher one of the
 *      drive current strength.
 * - for low speed signals
 *   1. 3.3V VDDIO, use either 4 mA or 8 mA.
 *   2. 2.5V VDDIO, use either 8 mA or 12 mA.
 *   3. 1.8V VDDIO, use either 12 mA or 16 mA.
 *   4. If it is heavy loading, can use higher drive current strength.
 */
static const struct ksz_drive_strength ksz9477_drive_strengths[] = {
	{ SW_DRIVE_STRENGTH_2MA,  2000 },
	{ SW_DRIVE_STRENGTH_4MA,  4000 },
	{ SW_DRIVE_STRENGTH_8MA,  8000 },
	{ SW_DRIVE_STRENGTH_12MA, 12000 },
	{ SW_DRIVE_STRENGTH_16MA, 16000 },
	{ SW_DRIVE_STRENGTH_20MA, 20000 },
	{ SW_DRIVE_STRENGTH_24MA, 24000 },
	{ SW_DRIVE_STRENGTH_28MA, 28000 },
};

/* ksz8830_drive_strengths - Drive strength mapping for KSZ8830, KSZ8873, ..
 *			     variants.
 * This values are documented in KSZ8873 and KSZ8863 datasheets.
 */
static const struct ksz_drive_strength ksz8830_drive_strengths[] = {
	{ 0,  8000 },
	{ KSZ8873_DRIVE_STRENGTH_16MA, 16000 },
};

static const struct ksz_dev_ops ksz8_dev_ops = {
	.setup = ksz8_setup,
	.get_port_addr = ksz8_get_port_addr,
	.cfg_port_member = ksz8_cfg_port_member,
	.flush_dyn_mac_table = ksz8_flush_dyn_mac_table,
	.port_setup = ksz8_port_setup,
	.r_phy = ksz8_r_phy,
	.w_phy = ksz8_w_phy,
	.r_mib_cnt = ksz8_r_mib_cnt,
	.r_mib_pkt = ksz8_r_mib_pkt,
	.r_mib_stat64 = ksz88xx_r_mib_stats64,
	.freeze_mib = ksz8_freeze_mib,
	.port_init_cnt = ksz8_port_init_cnt,
	.fdb_dump = ksz8_fdb_dump,
	.fdb_add = ksz8_fdb_add,
	.fdb_del = ksz8_fdb_del,
	.mdb_add = ksz8_mdb_add,
	.mdb_del = ksz8_mdb_del,
	.vlan_filtering = ksz8_port_vlan_filtering,
	.vlan_add = ksz8_port_vlan_add,
	.vlan_del = ksz8_port_vlan_del,
	.mirror_add = ksz8_port_mirror_add,
	.mirror_del = ksz8_port_mirror_del,
	.get_caps = ksz8_get_caps,
	.config_cpu_port = ksz8_config_cpu_port,
	.enable_stp_addr = ksz8_enable_stp_addr,
	.reset = ksz8_reset_switch,
	.init = ksz8_switch_init,
	.exit = ksz8_switch_exit,
	.change_mtu = ksz8_change_mtu,
};

static void ksz9477_phylink_mac_link_up(struct ksz_device *dev, int port,
					unsigned int mode,
					phy_interface_t interface,
					struct phy_device *phydev, int speed,
					int duplex, bool tx_pause,
					bool rx_pause);

static const struct ksz_dev_ops ksz9477_dev_ops = {
	.setup = ksz9477_setup,
	.get_port_addr = ksz9477_get_port_addr,
	.cfg_port_member = ksz9477_cfg_port_member,
	.flush_dyn_mac_table = ksz9477_flush_dyn_mac_table,
	.port_setup = ksz9477_port_setup,
	.set_ageing_time = ksz9477_set_ageing_time,
	.r_phy = ksz9477_r_phy,
	.w_phy = ksz9477_w_phy,
	.r_mib_cnt = ksz9477_r_mib_cnt,
	.r_mib_pkt = ksz9477_r_mib_pkt,
	.r_mib_stat64 = ksz_r_mib_stats64,
	.freeze_mib = ksz9477_freeze_mib,
	.port_init_cnt = ksz9477_port_init_cnt,
	.vlan_filtering = ksz9477_port_vlan_filtering,
	.vlan_add = ksz9477_port_vlan_add,
	.vlan_del = ksz9477_port_vlan_del,
	.mirror_add = ksz9477_port_mirror_add,
	.mirror_del = ksz9477_port_mirror_del,
	.get_caps = ksz9477_get_caps,
	.fdb_dump = ksz9477_fdb_dump,
	.fdb_add = ksz9477_fdb_add,
	.fdb_del = ksz9477_fdb_del,
	.mdb_add = ksz9477_mdb_add,
	.mdb_del = ksz9477_mdb_del,
	.change_mtu = ksz9477_change_mtu,
	.phylink_mac_link_up = ksz9477_phylink_mac_link_up,
	.config_cpu_port = ksz9477_config_cpu_port,
	.tc_cbs_set_cinc = ksz9477_tc_cbs_set_cinc,
	.enable_stp_addr = ksz9477_enable_stp_addr,
	.reset = ksz9477_reset_switch,
	.init = ksz9477_switch_init,
	.exit = ksz9477_switch_exit,
};

static const struct ksz_dev_ops lan937x_dev_ops = {
	.setup = lan937x_setup,
	.teardown = lan937x_teardown,
	.get_port_addr = ksz9477_get_port_addr,
	.cfg_port_member = ksz9477_cfg_port_member,
	.flush_dyn_mac_table = ksz9477_flush_dyn_mac_table,
	.port_setup = lan937x_port_setup,
	.set_ageing_time = lan937x_set_ageing_time,
	.r_phy = lan937x_r_phy,
	.w_phy = lan937x_w_phy,
	.r_mib_cnt = ksz9477_r_mib_cnt,
	.r_mib_pkt = ksz9477_r_mib_pkt,
	.r_mib_stat64 = ksz_r_mib_stats64,
	.freeze_mib = ksz9477_freeze_mib,
	.port_init_cnt = ksz9477_port_init_cnt,
	.vlan_filtering = ksz9477_port_vlan_filtering,
	.vlan_add = ksz9477_port_vlan_add,
	.vlan_del = ksz9477_port_vlan_del,
	.mirror_add = ksz9477_port_mirror_add,
	.mirror_del = ksz9477_port_mirror_del,
	.get_caps = lan937x_phylink_get_caps,
	.setup_rgmii_delay = lan937x_setup_rgmii_delay,
	.fdb_dump = ksz9477_fdb_dump,
	.fdb_add = ksz9477_fdb_add,
	.fdb_del = ksz9477_fdb_del,
	.mdb_add = ksz9477_mdb_add,
	.mdb_del = ksz9477_mdb_del,
	.change_mtu = lan937x_change_mtu,
	.phylink_mac_link_up = ksz9477_phylink_mac_link_up,
	.config_cpu_port = lan937x_config_cpu_port,
	.tc_cbs_set_cinc = lan937x_tc_cbs_set_cinc,
	.enable_stp_addr = ksz9477_enable_stp_addr,
	.reset = lan937x_reset_switch,
	.init = lan937x_switch_init,
	.exit = lan937x_switch_exit,
};

static const u16 ksz8795_regs[] = {
	[REG_SW_MAC_ADDR]		= 0x68,
	[REG_IND_CTRL_0]		= 0x6E,
	[REG_IND_DATA_8]		= 0x70,
	[REG_IND_DATA_CHECK]		= 0x72,
	[REG_IND_DATA_HI]		= 0x71,
	[REG_IND_DATA_LO]		= 0x75,
	[REG_IND_MIB_CHECK]		= 0x74,
	[REG_IND_BYTE]			= 0xA0,
	[P_FORCE_CTRL]			= 0x0C,
	[P_LINK_STATUS]			= 0x0E,
	[P_LOCAL_CTRL]			= 0x07,
	[P_NEG_RESTART_CTRL]		= 0x0D,
	[P_REMOTE_STATUS]		= 0x08,
	[P_SPEED_STATUS]		= 0x09,
	[S_TAIL_TAG_CTRL]		= 0x0C,
	[P_STP_CTRL]			= 0x02,
	[S_START_CTRL]			= 0x01,
	[S_BROADCAST_CTRL]		= 0x06,
	[S_MULTICAST_CTRL]		= 0x04,
	[P_XMII_CTRL_0]			= 0x06,
	[P_XMII_CTRL_1]			= 0x06,
};

static const u32 ksz8795_masks[] = {
	[PORT_802_1P_REMAPPING]		= BIT(7),
	[SW_TAIL_TAG_ENABLE]		= BIT(1),
	[MIB_COUNTER_OVERFLOW]		= BIT(6),
	[MIB_COUNTER_VALID]		= BIT(5),
	[VLAN_TABLE_FID]		= GENMASK(6, 0),
	[VLAN_TABLE_MEMBERSHIP]		= GENMASK(11, 7),
	[VLAN_TABLE_VALID]		= BIT(12),
	[STATIC_MAC_TABLE_VALID]	= BIT(21),
	[STATIC_MAC_TABLE_USE_FID]	= BIT(23),
	[STATIC_MAC_TABLE_FID]		= GENMASK(30, 24),
	[STATIC_MAC_TABLE_OVERRIDE]	= BIT(22),
	[STATIC_MAC_TABLE_FWD_PORTS]	= GENMASK(20, 16),
	[DYNAMIC_MAC_TABLE_ENTRIES_H]	= GENMASK(6, 0),
	[DYNAMIC_MAC_TABLE_MAC_EMPTY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_NOT_READY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_ENTRIES]	= GENMASK(31, 29),
	[DYNAMIC_MAC_TABLE_FID]		= GENMASK(22, 16),
	[DYNAMIC_MAC_TABLE_SRC_PORT]	= GENMASK(26, 24),
	[DYNAMIC_MAC_TABLE_TIMESTAMP]	= GENMASK(28, 27),
	[P_MII_TX_FLOW_CTRL]		= BIT(5),
	[P_MII_RX_FLOW_CTRL]		= BIT(5),
};

static const u8 ksz8795_xmii_ctrl0[] = {
	[P_MII_100MBIT]			= 0,
	[P_MII_10MBIT]			= 1,
	[P_MII_FULL_DUPLEX]		= 0,
	[P_MII_HALF_DUPLEX]		= 1,
};

static const u8 ksz8795_xmii_ctrl1[] = {
	[P_RGMII_SEL]			= 3,
	[P_GMII_SEL]			= 2,
	[P_RMII_SEL]			= 1,
	[P_MII_SEL]			= 0,
	[P_GMII_1GBIT]			= 1,
	[P_GMII_NOT_1GBIT]		= 0,
};

static const u8 ksz8795_shifts[] = {
	[VLAN_TABLE_MEMBERSHIP_S]	= 7,
	[VLAN_TABLE]			= 16,
	[STATIC_MAC_FWD_PORTS]		= 16,
	[STATIC_MAC_FID]		= 24,
	[DYNAMIC_MAC_ENTRIES_H]		= 3,
	[DYNAMIC_MAC_ENTRIES]		= 29,
	[DYNAMIC_MAC_FID]		= 16,
	[DYNAMIC_MAC_TIMESTAMP]		= 27,
	[DYNAMIC_MAC_SRC_PORT]		= 24,
};

static const u16 ksz8863_regs[] = {
	[REG_IND_CTRL_0]		= 0x79,
	[REG_IND_DATA_8]		= 0x7B,
	[REG_IND_DATA_CHECK]		= 0x7B,
	[REG_IND_DATA_HI]		= 0x7C,
	[REG_IND_DATA_LO]		= 0x80,
	[REG_IND_MIB_CHECK]		= 0x80,
	[P_FORCE_CTRL]			= 0x0C,
	[P_LINK_STATUS]			= 0x0E,
	[P_LOCAL_CTRL]			= 0x0C,
	[P_NEG_RESTART_CTRL]		= 0x0D,
	[P_REMOTE_STATUS]		= 0x0E,
	[P_SPEED_STATUS]		= 0x0F,
	[S_TAIL_TAG_CTRL]		= 0x03,
	[P_STP_CTRL]			= 0x02,
	[S_START_CTRL]			= 0x01,
	[S_BROADCAST_CTRL]		= 0x06,
	[S_MULTICAST_CTRL]		= 0x04,
};

static const u32 ksz8863_masks[] = {
	[PORT_802_1P_REMAPPING]		= BIT(3),
	[SW_TAIL_TAG_ENABLE]		= BIT(6),
	[MIB_COUNTER_OVERFLOW]		= BIT(7),
	[MIB_COUNTER_VALID]		= BIT(6),
	[VLAN_TABLE_FID]		= GENMASK(15, 12),
	[VLAN_TABLE_MEMBERSHIP]		= GENMASK(18, 16),
	[VLAN_TABLE_VALID]		= BIT(19),
	[STATIC_MAC_TABLE_VALID]	= BIT(19),
	[STATIC_MAC_TABLE_USE_FID]	= BIT(21),
	[STATIC_MAC_TABLE_FID]		= GENMASK(25, 22),
	[STATIC_MAC_TABLE_OVERRIDE]	= BIT(20),
	[STATIC_MAC_TABLE_FWD_PORTS]	= GENMASK(18, 16),
	[DYNAMIC_MAC_TABLE_ENTRIES_H]	= GENMASK(1, 0),
	[DYNAMIC_MAC_TABLE_MAC_EMPTY]	= BIT(2),
	[DYNAMIC_MAC_TABLE_NOT_READY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_ENTRIES]	= GENMASK(31, 24),
	[DYNAMIC_MAC_TABLE_FID]		= GENMASK(19, 16),
	[DYNAMIC_MAC_TABLE_SRC_PORT]	= GENMASK(21, 20),
	[DYNAMIC_MAC_TABLE_TIMESTAMP]	= GENMASK(23, 22),
};

static u8 ksz8863_shifts[] = {
	[VLAN_TABLE_MEMBERSHIP_S]	= 16,
	[STATIC_MAC_FWD_PORTS]		= 16,
	[STATIC_MAC_FID]		= 22,
	[DYNAMIC_MAC_ENTRIES_H]		= 8,
	[DYNAMIC_MAC_ENTRIES]		= 24,
	[DYNAMIC_MAC_FID]		= 16,
	[DYNAMIC_MAC_TIMESTAMP]		= 22,
	[DYNAMIC_MAC_SRC_PORT]		= 20,
};

static const u16 ksz9477_regs[] = {
	[REG_SW_MAC_ADDR]		= 0x0302,
	[P_STP_CTRL]			= 0x0B04,
	[S_START_CTRL]			= 0x0300,
	[S_BROADCAST_CTRL]		= 0x0332,
	[S_MULTICAST_CTRL]		= 0x0331,
	[P_XMII_CTRL_0]			= 0x0300,
	[P_XMII_CTRL_1]			= 0x0301,
};

static const u32 ksz9477_masks[] = {
	[ALU_STAT_WRITE]		= 0,
	[ALU_STAT_READ]			= 1,
	[P_MII_TX_FLOW_CTRL]		= BIT(5),
	[P_MII_RX_FLOW_CTRL]		= BIT(3),
};

static const u8 ksz9477_shifts[] = {
	[ALU_STAT_INDEX]		= 16,
};

static const u8 ksz9477_xmii_ctrl0[] = {
	[P_MII_100MBIT]			= 1,
	[P_MII_10MBIT]			= 0,
	[P_MII_FULL_DUPLEX]		= 1,
	[P_MII_HALF_DUPLEX]		= 0,
};

static const u8 ksz9477_xmii_ctrl1[] = {
	[P_RGMII_SEL]			= 0,
	[P_RMII_SEL]			= 1,
	[P_GMII_SEL]			= 2,
	[P_MII_SEL]			= 3,
	[P_GMII_1GBIT]			= 0,
	[P_GMII_NOT_1GBIT]		= 1,
};

static const u32 lan937x_masks[] = {
	[ALU_STAT_WRITE]		= 1,
	[ALU_STAT_READ]			= 2,
	[P_MII_TX_FLOW_CTRL]		= BIT(5),
	[P_MII_RX_FLOW_CTRL]		= BIT(3),
};

static const u8 lan937x_shifts[] = {
	[ALU_STAT_INDEX]		= 8,
};

static const struct regmap_range ksz8563_valid_regs[] = {
	regmap_reg_range(0x0000, 0x0003),
	regmap_reg_range(0x0006, 0x0006),
	regmap_reg_range(0x000f, 0x001f),
	regmap_reg_range(0x0100, 0x0100),
	regmap_reg_range(0x0104, 0x0107),
	regmap_reg_range(0x010d, 0x010d),
	regmap_reg_range(0x0110, 0x0113),
	regmap_reg_range(0x0120, 0x012b),
	regmap_reg_range(0x0201, 0x0201),
	regmap_reg_range(0x0210, 0x0213),
	regmap_reg_range(0x0300, 0x0300),
	regmap_reg_range(0x0302, 0x031b),
	regmap_reg_range(0x0320, 0x032b),
	regmap_reg_range(0x0330, 0x0336),
	regmap_reg_range(0x0338, 0x033e),
	regmap_reg_range(0x0340, 0x035f),
	regmap_reg_range(0x0370, 0x0370),
	regmap_reg_range(0x0378, 0x0378),
	regmap_reg_range(0x037c, 0x037d),
	regmap_reg_range(0x0390, 0x0393),
	regmap_reg_range(0x0400, 0x040e),
	regmap_reg_range(0x0410, 0x042f),
	regmap_reg_range(0x0500, 0x0519),
	regmap_reg_range(0x0520, 0x054b),
	regmap_reg_range(0x0550, 0x05b3),

	/* port 1 */
	regmap_reg_range(0x1000, 0x1001),
	regmap_reg_range(0x1004, 0x100b),
	regmap_reg_range(0x1013, 0x1013),
	regmap_reg_range(0x1017, 0x1017),
	regmap_reg_range(0x101b, 0x101b),
	regmap_reg_range(0x101f, 0x1021),
	regmap_reg_range(0x1030, 0x1030),
	regmap_reg_range(0x1100, 0x1111),
	regmap_reg_range(0x111a, 0x111d),
	regmap_reg_range(0x1122, 0x1127),
	regmap_reg_range(0x112a, 0x112b),
	regmap_reg_range(0x1136, 0x1139),
	regmap_reg_range(0x113e, 0x113f),
	regmap_reg_range(0x1400, 0x1401),
	regmap_reg_range(0x1403, 0x1403),
	regmap_reg_range(0x1410, 0x1417),
	regmap_reg_range(0x1420, 0x1423),
	regmap_reg_range(0x1500, 0x1507),
	regmap_reg_range(0x1600, 0x1612),
	regmap_reg_range(0x1800, 0x180f),
	regmap_reg_range(0x1900, 0x1907),
	regmap_reg_range(0x1914, 0x191b),
	regmap_reg_range(0x1a00, 0x1a03),
	regmap_reg_range(0x1a04, 0x1a08),
	regmap_reg_range(0x1b00, 0x1b01),
	regmap_reg_range(0x1b04, 0x1b04),
	regmap_reg_range(0x1c00, 0x1c05),
	regmap_reg_range(0x1c08, 0x1c1b),

	/* port 2 */
	regmap_reg_range(0x2000, 0x2001),
	regmap_reg_range(0x2004, 0x200b),
	regmap_reg_range(0x2013, 0x2013),
	regmap_reg_range(0x2017, 0x2017),
	regmap_reg_range(0x201b, 0x201b),
	regmap_reg_range(0x201f, 0x2021),
	regmap_reg_range(0x2030, 0x2030),
	regmap_reg_range(0x2100, 0x2111),
	regmap_reg_range(0x211a, 0x211d),
	regmap_reg_range(0x2122, 0x2127),
	regmap_reg_range(0x212a, 0x212b),
	regmap_reg_range(0x2136, 0x2139),
	regmap_reg_range(0x213e, 0x213f),
	regmap_reg_range(0x2400, 0x2401),
	regmap_reg_range(0x2403, 0x2403),
	regmap_reg_range(0x2410, 0x2417),
	regmap_reg_range(0x2420, 0x2423),
	regmap_reg_range(0x2500, 0x2507),
	regmap_reg_range(0x2600, 0x2612),
	regmap_reg_range(0x2800, 0x280f),
	regmap_reg_range(0x2900, 0x2907),
	regmap_reg_range(0x2914, 0x291b),
	regmap_reg_range(0x2a00, 0x2a03),
	regmap_reg_range(0x2a04, 0x2a08),
	regmap_reg_range(0x2b00, 0x2b01),
	regmap_reg_range(0x2b04, 0x2b04),
	regmap_reg_range(0x2c00, 0x2c05),
	regmap_reg_range(0x2c08, 0x2c1b),

	/* port 3 */
	regmap_reg_range(0x3000, 0x3001),
	regmap_reg_range(0x3004, 0x300b),
	regmap_reg_range(0x3013, 0x3013),
	regmap_reg_range(0x3017, 0x3017),
	regmap_reg_range(0x301b, 0x301b),
	regmap_reg_range(0x301f, 0x3021),
	regmap_reg_range(0x3030, 0x3030),
	regmap_reg_range(0x3300, 0x3301),
	regmap_reg_range(0x3303, 0x3303),
	regmap_reg_range(0x3400, 0x3401),
	regmap_reg_range(0x3403, 0x3403),
	regmap_reg_range(0x3410, 0x3417),
	regmap_reg_range(0x3420, 0x3423),
	regmap_reg_range(0x3500, 0x3507),
	regmap_reg_range(0x3600, 0x3612),
	regmap_reg_range(0x3800, 0x380f),
	regmap_reg_range(0x3900, 0x3907),
	regmap_reg_range(0x3914, 0x391b),
	regmap_reg_range(0x3a00, 0x3a03),
	regmap_reg_range(0x3a04, 0x3a08),
	regmap_reg_range(0x3b00, 0x3b01),
	regmap_reg_range(0x3b04, 0x3b04),
	regmap_reg_range(0x3c00, 0x3c05),
	regmap_reg_range(0x3c08, 0x3c1b),
};

static const struct regmap_access_table ksz8563_register_set = {
	.yes_ranges = ksz8563_valid_regs,
	.n_yes_ranges = ARRAY_SIZE(ksz8563_valid_regs),
};

static const struct regmap_range ksz9477_valid_regs[] = {
	regmap_reg_range(0x0000, 0x0003),
	regmap_reg_range(0x0006, 0x0006),
	regmap_reg_range(0x0010, 0x001f),
	regmap_reg_range(0x0100, 0x0100),
	regmap_reg_range(0x0103, 0x0107),
	regmap_reg_range(0x010d, 0x010d),
	regmap_reg_range(0x0110, 0x0113),
	regmap_reg_range(0x0120, 0x012b),
	regmap_reg_range(0x0201, 0x0201),
	regmap_reg_range(0x0210, 0x0213),
	regmap_reg_range(0x0300, 0x0300),
	regmap_reg_range(0x0302, 0x031b),
	regmap_reg_range(0x0320, 0x032b),
	regmap_reg_range(0x0330, 0x0336),
	regmap_reg_range(0x0338, 0x033b),
	regmap_reg_range(0x033e, 0x033e),
	regmap_reg_range(0x0340, 0x035f),
	regmap_reg_range(0x0370, 0x0370),
	regmap_reg_range(0x0378, 0x0378),
	regmap_reg_range(0x037c, 0x037d),
	regmap_reg_range(0x0390, 0x0393),
	regmap_reg_range(0x0400, 0x040e),
	regmap_reg_range(0x0410, 0x042f),
	regmap_reg_range(0x0444, 0x044b),
	regmap_reg_range(0x0450, 0x046f),
	regmap_reg_range(0x0500, 0x0519),
	regmap_reg_range(0x0520, 0x054b),
	regmap_reg_range(0x0550, 0x05b3),
	regmap_reg_range(0x0604, 0x060b),
	regmap_reg_range(0x0610, 0x0612),
	regmap_reg_range(0x0614, 0x062c),
	regmap_reg_range(0x0640, 0x0645),
	regmap_reg_range(0x0648, 0x064d),

	/* port 1 */
	regmap_reg_range(0x1000, 0x1001),
	regmap_reg_range(0x1013, 0x1013),
	regmap_reg_range(0x1017, 0x1017),
	regmap_reg_range(0x101b, 0x101b),
	regmap_reg_range(0x101f, 0x1020),
	regmap_reg_range(0x1030, 0x1030),
	regmap_reg_range(0x1100, 0x1115),
	regmap_reg_range(0x111a, 0x111f),
	regmap_reg_range(0x1120, 0x112b),
	regmap_reg_range(0x1134, 0x113b),
	regmap_reg_range(0x113c, 0x113f),
	regmap_reg_range(0x1400, 0x1401),
	regmap_reg_range(0x1403, 0x1403),
	regmap_reg_range(0x1410, 0x1417),
	regmap_reg_range(0x1420, 0x1423),
	regmap_reg_range(0x1500, 0x1507),
	regmap_reg_range(0x1600, 0x1613),
	regmap_reg_range(0x1800, 0x180f),
	regmap_reg_range(0x1820, 0x1827),
	regmap_reg_range(0x1830, 0x1837),
	regmap_reg_range(0x1840, 0x184b),
	regmap_reg_range(0x1900, 0x1907),
	regmap_reg_range(0x1914, 0x191b),
	regmap_reg_range(0x1920, 0x1920),
	regmap_reg_range(0x1923, 0x1927),
	regmap_reg_range(0x1a00, 0x1a03),
	regmap_reg_range(0x1a04, 0x1a07),
	regmap_reg_range(0x1b00, 0x1b01),
	regmap_reg_range(0x1b04, 0x1b04),
	regmap_reg_range(0x1c00, 0x1c05),
	regmap_reg_range(0x1c08, 0x1c1b),

	/* port 2 */
	regmap_reg_range(0x2000, 0x2001),
	regmap_reg_range(0x2013, 0x2013),
	regmap_reg_range(0x2017, 0x2017),
	regmap_reg_range(0x201b, 0x201b),
	regmap_reg_range(0x201f, 0x2020),
	regmap_reg_range(0x2030, 0x2030),
	regmap_reg_range(0x2100, 0x2115),
	regmap_reg_range(0x211a, 0x211f),
	regmap_reg_range(0x2120, 0x212b),
	regmap_reg_range(0x2134, 0x213b),
	regmap_reg_range(0x213c, 0x213f),
	regmap_reg_range(0x2400, 0x2401),
	regmap_reg_range(0x2403, 0x2403),
	regmap_reg_range(0x2410, 0x2417),
	regmap_reg_range(0x2420, 0x2423),
	regmap_reg_range(0x2500, 0x2507),
	regmap_reg_range(0x2600, 0x2613),
	regmap_reg_range(0x2800, 0x280f),
	regmap_reg_range(0x2820, 0x2827),
	regmap_reg_range(0x2830, 0x2837),
	regmap_reg_range(0x2840, 0x284b),
	regmap_reg_range(0x2900, 0x2907),
	regmap_reg_range(0x2914, 0x291b),
	regmap_reg_range(0x2920, 0x2920),
	regmap_reg_range(0x2923, 0x2927),
	regmap_reg_range(0x2a00, 0x2a03),
	regmap_reg_range(0x2a04, 0x2a07),
	regmap_reg_range(0x2b00, 0x2b01),
	regmap_reg_range(0x2b04, 0x2b04),
	regmap_reg_range(0x2c00, 0x2c05),
	regmap_reg_range(0x2c08, 0x2c1b),

	/* port 3 */
	regmap_reg_range(0x3000, 0x3001),
	regmap_reg_range(0x3013, 0x3013),
	regmap_reg_range(0x3017, 0x3017),
	regmap_reg_range(0x301b, 0x301b),
	regmap_reg_range(0x301f, 0x3020),
	regmap_reg_range(0x3030, 0x3030),
	regmap_reg_range(0x3100, 0x3115),
	regmap_reg_range(0x311a, 0x311f),
	regmap_reg_range(0x3120, 0x312b),
	regmap_reg_range(0x3134, 0x313b),
	regmap_reg_range(0x313c, 0x313f),
	regmap_reg_range(0x3400, 0x3401),
	regmap_reg_range(0x3403, 0x3403),
	regmap_reg_range(0x3410, 0x3417),
	regmap_reg_range(0x3420, 0x3423),
	regmap_reg_range(0x3500, 0x3507),
	regmap_reg_range(0x3600, 0x3613),
	regmap_reg_range(0x3800, 0x380f),
	regmap_reg_range(0x3820, 0x3827),
	regmap_reg_range(0x3830, 0x3837),
	regmap_reg_range(0x3840, 0x384b),
	regmap_reg_range(0x3900, 0x3907),
	regmap_reg_range(0x3914, 0x391b),
	regmap_reg_range(0x3920, 0x3920),
	regmap_reg_range(0x3923, 0x3927),
	regmap_reg_range(0x3a00, 0x3a03),
	regmap_reg_range(0x3a04, 0x3a07),
	regmap_reg_range(0x3b00, 0x3b01),
	regmap_reg_range(0x3b04, 0x3b04),
	regmap_reg_range(0x3c00, 0x3c05),
	regmap_reg_range(0x3c08, 0x3c1b),

	/* port 4 */
	regmap_reg_range(0x4000, 0x4001),
	regmap_reg_range(0x4013, 0x4013),
	regmap_reg_range(0x4017, 0x4017),
	regmap_reg_range(0x401b, 0x401b),
	regmap_reg_range(0x401f, 0x4020),
	regmap_reg_range(0x4030, 0x4030),
	regmap_reg_range(0x4100, 0x4115),
	regmap_reg_range(0x411a, 0x411f),
	regmap_reg_range(0x4120, 0x412b),
	regmap_reg_range(0x4134, 0x413b),
	regmap_reg_range(0x413c, 0x413f),
	regmap_reg_range(0x4400, 0x4401),
	regmap_reg_range(0x4403, 0x4403),
	regmap_reg_range(0x4410, 0x4417),
	regmap_reg_range(0x4420, 0x4423),
	regmap_reg_range(0x4500, 0x4507),
	regmap_reg_range(0x4600, 0x4613),
	regmap_reg_range(0x4800, 0x480f),
	regmap_reg_range(0x4820, 0x4827),
	regmap_reg_range(0x4830, 0x4837),
	regmap_reg_range(0x4840, 0x484b),
	regmap_reg_range(0x4900, 0x4907),
	regmap_reg_range(0x4914, 0x491b),
	regmap_reg_range(0x4920, 0x4920),
	regmap_reg_range(0x4923, 0x4927),
	regmap_reg_range(0x4a00, 0x4a03),
	regmap_reg_range(0x4a04, 0x4a07),
	regmap_reg_range(0x4b00, 0x4b01),
	regmap_reg_range(0x4b04, 0x4b04),
	regmap_reg_range(0x4c00, 0x4c05),
	regmap_reg_range(0x4c08, 0x4c1b),

	/* port 5 */
	regmap_reg_range(0x5000, 0x5001),
	regmap_reg_range(0x5013, 0x5013),
	regmap_reg_range(0x5017, 0x5017),
	regmap_reg_range(0x501b, 0x501b),
	regmap_reg_range(0x501f, 0x5020),
	regmap_reg_range(0x5030, 0x5030),
	regmap_reg_range(0x5100, 0x5115),
	regmap_reg_range(0x511a, 0x511f),
	regmap_reg_range(0x5120, 0x512b),
	regmap_reg_range(0x5134, 0x513b),
	regmap_reg_range(0x513c, 0x513f),
	regmap_reg_range(0x5400, 0x5401),
	regmap_reg_range(0x5403, 0x5403),
	regmap_reg_range(0x5410, 0x5417),
	regmap_reg_range(0x5420, 0x5423),
	regmap_reg_range(0x5500, 0x5507),
	regmap_reg_range(0x5600, 0x5613),
	regmap_reg_range(0x5800, 0x580f),
	regmap_reg_range(0x5820, 0x5827),
	regmap_reg_range(0x5830, 0x5837),
	regmap_reg_range(0x5840, 0x584b),
	regmap_reg_range(0x5900, 0x5907),
	regmap_reg_range(0x5914, 0x591b),
	regmap_reg_range(0x5920, 0x5920),
	regmap_reg_range(0x5923, 0x5927),
	regmap_reg_range(0x5a00, 0x5a03),
	regmap_reg_range(0x5a04, 0x5a07),
	regmap_reg_range(0x5b00, 0x5b01),
	regmap_reg_range(0x5b04, 0x5b04),
	regmap_reg_range(0x5c00, 0x5c05),
	regmap_reg_range(0x5c08, 0x5c1b),

	/* port 6 */
	regmap_reg_range(0x6000, 0x6001),
	regmap_reg_range(0x6013, 0x6013),
	regmap_reg_range(0x6017, 0x6017),
	regmap_reg_range(0x601b, 0x601b),
	regmap_reg_range(0x601f, 0x6020),
	regmap_reg_range(0x6030, 0x6030),
	regmap_reg_range(0x6300, 0x6301),
	regmap_reg_range(0x6400, 0x6401),
	regmap_reg_range(0x6403, 0x6403),
	regmap_reg_range(0x6410, 0x6417),
	regmap_reg_range(0x6420, 0x6423),
	regmap_reg_range(0x6500, 0x6507),
	regmap_reg_range(0x6600, 0x6613),
	regmap_reg_range(0x6800, 0x680f),
	regmap_reg_range(0x6820, 0x6827),
	regmap_reg_range(0x6830, 0x6837),
	regmap_reg_range(0x6840, 0x684b),
	regmap_reg_range(0x6900, 0x6907),
	regmap_reg_range(0x6914, 0x691b),
	regmap_reg_range(0x6920, 0x6920),
	regmap_reg_range(0x6923, 0x6927),
	regmap_reg_range(0x6a00, 0x6a03),
	regmap_reg_range(0x6a04, 0x6a07),
	regmap_reg_range(0x6b00, 0x6b01),
	regmap_reg_range(0x6b04, 0x6b04),
	regmap_reg_range(0x6c00, 0x6c05),
	regmap_reg_range(0x6c08, 0x6c1b),

	/* port 7 */
	regmap_reg_range(0x7000, 0x7001),
	regmap_reg_range(0x7013, 0x7013),
	regmap_reg_range(0x7017, 0x7017),
	regmap_reg_range(0x701b, 0x701b),
	regmap_reg_range(0x701f, 0x7020),
	regmap_reg_range(0x7030, 0x7030),
	regmap_reg_range(0x7200, 0x7203),
	regmap_reg_range(0x7206, 0x7207),
	regmap_reg_range(0x7300, 0x7301),
	regmap_reg_range(0x7400, 0x7401),
	regmap_reg_range(0x7403, 0x7403),
	regmap_reg_range(0x7410, 0x7417),
	regmap_reg_range(0x7420, 0x7423),
	regmap_reg_range(0x7500, 0x7507),
	regmap_reg_range(0x7600, 0x7613),
	regmap_reg_range(0x7800, 0x780f),
	regmap_reg_range(0x7820, 0x7827),
	regmap_reg_range(0x7830, 0x7837),
	regmap_reg_range(0x7840, 0x784b),
	regmap_reg_range(0x7900, 0x7907),
	regmap_reg_range(0x7914, 0x791b),
	regmap_reg_range(0x7920, 0x7920),
	regmap_reg_range(0x7923, 0x7927),
	regmap_reg_range(0x7a00, 0x7a03),
	regmap_reg_range(0x7a04, 0x7a07),
	regmap_reg_range(0x7b00, 0x7b01),
	regmap_reg_range(0x7b04, 0x7b04),
	regmap_reg_range(0x7c00, 0x7c05),
	regmap_reg_range(0x7c08, 0x7c1b),
};

static const struct regmap_access_table ksz9477_register_set = {
	.yes_ranges = ksz9477_valid_regs,
	.n_yes_ranges = ARRAY_SIZE(ksz9477_valid_regs),
};

static const struct regmap_range ksz9896_valid_regs[] = {
	regmap_reg_range(0x0000, 0x0003),
	regmap_reg_range(0x0006, 0x0006),
	regmap_reg_range(0x0010, 0x001f),
	regmap_reg_range(0x0100, 0x0100),
	regmap_reg_range(0x0103, 0x0107),
	regmap_reg_range(0x010d, 0x010d),
	regmap_reg_range(0x0110, 0x0113),
	regmap_reg_range(0x0120, 0x0127),
	regmap_reg_range(0x0201, 0x0201),
	regmap_reg_range(0x0210, 0x0213),
	regmap_reg_range(0x0300, 0x0300),
	regmap_reg_range(0x0302, 0x030b),
	regmap_reg_range(0x0310, 0x031b),
	regmap_reg_range(0x0320, 0x032b),
	regmap_reg_range(0x0330, 0x0336),
	regmap_reg_range(0x0338, 0x033b),
	regmap_reg_range(0x033e, 0x033e),
	regmap_reg_range(0x0340, 0x035f),
	regmap_reg_range(0x0370, 0x0370),
	regmap_reg_range(0x0378, 0x0378),
	regmap_reg_range(0x037c, 0x037d),
	regmap_reg_range(0x0390, 0x0393),
	regmap_reg_range(0x0400, 0x040e),
	regmap_reg_range(0x0410, 0x042f),

	/* port 1 */
	regmap_reg_range(0x1000, 0x1001),
	regmap_reg_range(0x1013, 0x1013),
	regmap_reg_range(0x1017, 0x1017),
	regmap_reg_range(0x101b, 0x101b),
	regmap_reg_range(0x101f, 0x1020),
	regmap_reg_range(0x1030, 0x1030),
	regmap_reg_range(0x1100, 0x1115),
	regmap_reg_range(0x111a, 0x111f),
	regmap_reg_range(0x1122, 0x1127),
	regmap_reg_range(0x112a, 0x112b),
	regmap_reg_range(0x1136, 0x1139),
	regmap_reg_range(0x113e, 0x113f),
	regmap_reg_range(0x1400, 0x1401),
	regmap_reg_range(0x1403, 0x1403),
	regmap_reg_range(0x1410, 0x1417),
	regmap_reg_range(0x1420, 0x1423),
	regmap_reg_range(0x1500, 0x1507),
	regmap_reg_range(0x1600, 0x1612),
	regmap_reg_range(0x1800, 0x180f),
	regmap_reg_range(0x1820, 0x1827),
	regmap_reg_range(0x1830, 0x1837),
	regmap_reg_range(0x1840, 0x184b),
	regmap_reg_range(0x1900, 0x1907),
	regmap_reg_range(0x1914, 0x1915),
	regmap_reg_range(0x1a00, 0x1a03),
	regmap_reg_range(0x1a04, 0x1a07),
	regmap_reg_range(0x1b00, 0x1b01),
	regmap_reg_range(0x1b04, 0x1b04),

	/* port 2 */
	regmap_reg_range(0x2000, 0x2001),
	regmap_reg_range(0x2013, 0x2013),
	regmap_reg_range(0x2017, 0x2017),
	regmap_reg_range(0x201b, 0x201b),
	regmap_reg_range(0x201f, 0x2020),
	regmap_reg_range(0x2030, 0x2030),
	regmap_reg_range(0x2100, 0x2115),
	regmap_reg_range(0x211a, 0x211f),
	regmap_reg_range(0x2122, 0x2127),
	regmap_reg_range(0x212a, 0x212b),
	regmap_reg_range(0x2136, 0x2139),
	regmap_reg_range(0x213e, 0x213f),
	regmap_reg_range(0x2400, 0x2401),
	regmap_reg_range(0x2403, 0x2403),
	regmap_reg_range(0x2410, 0x2417),
	regmap_reg_range(0x2420, 0x2423),
	regmap_reg_range(0x2500, 0x2507),
	regmap_reg_range(0x2600, 0x2612),
	regmap_reg_range(0x2800, 0x280f),
	regmap_reg_range(0x2820, 0x2827),
	regmap_reg_range(0x2830, 0x2837),
	regmap_reg_range(0x2840, 0x284b),
	regmap_reg_range(0x2900, 0x2907),
	regmap_reg_range(0x2914, 0x2915),
	regmap_reg_range(0x2a00, 0x2a03),
	regmap_reg_range(0x2a04, 0x2a07),
	regmap_reg_range(0x2b00, 0x2b01),
	regmap_reg_range(0x2b04, 0x2b04),

	/* port 3 */
	regmap_reg_range(0x3000, 0x3001),
	regmap_reg_range(0x3013, 0x3013),
	regmap_reg_range(0x3017, 0x3017),
	regmap_reg_range(0x301b, 0x301b),
	regmap_reg_range(0x301f, 0x3020),
	regmap_reg_range(0x3030, 0x3030),
	regmap_reg_range(0x3100, 0x3115),
	regmap_reg_range(0x311a, 0x311f),
	regmap_reg_range(0x3122, 0x3127),
	regmap_reg_range(0x312a, 0x312b),
	regmap_reg_range(0x3136, 0x3139),
	regmap_reg_range(0x313e, 0x313f),
	regmap_reg_range(0x3400, 0x3401),
	regmap_reg_range(0x3403, 0x3403),
	regmap_reg_range(0x3410, 0x3417),
	regmap_reg_range(0x3420, 0x3423),
	regmap_reg_range(0x3500, 0x3507),
	regmap_reg_range(0x3600, 0x3612),
	regmap_reg_range(0x3800, 0x380f),
	regmap_reg_range(0x3820, 0x3827),
	regmap_reg_range(0x3830, 0x3837),
	regmap_reg_range(0x3840, 0x384b),
	regmap_reg_range(0x3900, 0x3907),
	regmap_reg_range(0x3914, 0x3915),
	regmap_reg_range(0x3a00, 0x3a03),
	regmap_reg_range(0x3a04, 0x3a07),
	regmap_reg_range(0x3b00, 0x3b01),
	regmap_reg_range(0x3b04, 0x3b04),

	/* port 4 */
	regmap_reg_range(0x4000, 0x4001),
	regmap_reg_range(0x4013, 0x4013),
	regmap_reg_range(0x4017, 0x4017),
	regmap_reg_range(0x401b, 0x401b),
	regmap_reg_range(0x401f, 0x4020),
	regmap_reg_range(0x4030, 0x4030),
	regmap_reg_range(0x4100, 0x4115),
	regmap_reg_range(0x411a, 0x411f),
	regmap_reg_range(0x4122, 0x4127),
	regmap_reg_range(0x412a, 0x412b),
	regmap_reg_range(0x4136, 0x4139),
	regmap_reg_range(0x413e, 0x413f),
	regmap_reg_range(0x4400, 0x4401),
	regmap_reg_range(0x4403, 0x4403),
	regmap_reg_range(0x4410, 0x4417),
	regmap_reg_range(0x4420, 0x4423),
	regmap_reg_range(0x4500, 0x4507),
	regmap_reg_range(0x4600, 0x4612),
	regmap_reg_range(0x4800, 0x480f),
	regmap_reg_range(0x4820, 0x4827),
	regmap_reg_range(0x4830, 0x4837),
	regmap_reg_range(0x4840, 0x484b),
	regmap_reg_range(0x4900, 0x4907),
	regmap_reg_range(0x4914, 0x4915),
	regmap_reg_range(0x4a00, 0x4a03),
	regmap_reg_range(0x4a04, 0x4a07),
	regmap_reg_range(0x4b00, 0x4b01),
	regmap_reg_range(0x4b04, 0x4b04),

	/* port 5 */
	regmap_reg_range(0x5000, 0x5001),
	regmap_reg_range(0x5013, 0x5013),
	regmap_reg_range(0x5017, 0x5017),
	regmap_reg_range(0x501b, 0x501b),
	regmap_reg_range(0x501f, 0x5020),
	regmap_reg_range(0x5030, 0x5030),
	regmap_reg_range(0x5100, 0x5115),
	regmap_reg_range(0x511a, 0x511f),
	regmap_reg_range(0x5122, 0x5127),
	regmap_reg_range(0x512a, 0x512b),
	regmap_reg_range(0x5136, 0x5139),
	regmap_reg_range(0x513e, 0x513f),
	regmap_reg_range(0x5400, 0x5401),
	regmap_reg_range(0x5403, 0x5403),
	regmap_reg_range(0x5410, 0x5417),
	regmap_reg_range(0x5420, 0x5423),
	regmap_reg_range(0x5500, 0x5507),
	regmap_reg_range(0x5600, 0x5612),
	regmap_reg_range(0x5800, 0x580f),
	regmap_reg_range(0x5820, 0x5827),
	regmap_reg_range(0x5830, 0x5837),
	regmap_reg_range(0x5840, 0x584b),
	regmap_reg_range(0x5900, 0x5907),
	regmap_reg_range(0x5914, 0x5915),
	regmap_reg_range(0x5a00, 0x5a03),
	regmap_reg_range(0x5a04, 0x5a07),
	regmap_reg_range(0x5b00, 0x5b01),
	regmap_reg_range(0x5b04, 0x5b04),

	/* port 6 */
	regmap_reg_range(0x6000, 0x6001),
	regmap_reg_range(0x6013, 0x6013),
	regmap_reg_range(0x6017, 0x6017),
	regmap_reg_range(0x601b, 0x601b),
	regmap_reg_range(0x601f, 0x6020),
	regmap_reg_range(0x6030, 0x6030),
	regmap_reg_range(0x6100, 0x6115),
	regmap_reg_range(0x611a, 0x611f),
	regmap_reg_range(0x6122, 0x6127),
	regmap_reg_range(0x612a, 0x612b),
	regmap_reg_range(0x6136, 0x6139),
	regmap_reg_range(0x613e, 0x613f),
	regmap_reg_range(0x6300, 0x6301),
	regmap_reg_range(0x6400, 0x6401),
	regmap_reg_range(0x6403, 0x6403),
	regmap_reg_range(0x6410, 0x6417),
	regmap_reg_range(0x6420, 0x6423),
	regmap_reg_range(0x6500, 0x6507),
	regmap_reg_range(0x6600, 0x6612),
	regmap_reg_range(0x6800, 0x680f),
	regmap_reg_range(0x6820, 0x6827),
	regmap_reg_range(0x6830, 0x6837),
	regmap_reg_range(0x6840, 0x684b),
	regmap_reg_range(0x6900, 0x6907),
	regmap_reg_range(0x6914, 0x6915),
	regmap_reg_range(0x6a00, 0x6a03),
	regmap_reg_range(0x6a04, 0x6a07),
	regmap_reg_range(0x6b00, 0x6b01),
	regmap_reg_range(0x6b04, 0x6b04),
};

static const struct regmap_access_table ksz9896_register_set = {
	.yes_ranges = ksz9896_valid_regs,
	.n_yes_ranges = ARRAY_SIZE(ksz9896_valid_regs),
};

static const struct regmap_range ksz8873_valid_regs[] = {
	regmap_reg_range(0x00, 0x01),
	/* global control register */
	regmap_reg_range(0x02, 0x0f),

	/* port registers */
	regmap_reg_range(0x10, 0x1d),
	regmap_reg_range(0x1e, 0x1f),
	regmap_reg_range(0x20, 0x2d),
	regmap_reg_range(0x2e, 0x2f),
	regmap_reg_range(0x30, 0x39),
	regmap_reg_range(0x3f, 0x3f),

	/* advanced control registers */
	regmap_reg_range(0x60, 0x6f),
	regmap_reg_range(0x70, 0x75),
	regmap_reg_range(0x76, 0x78),
	regmap_reg_range(0x79, 0x7a),
	regmap_reg_range(0x7b, 0x83),
	regmap_reg_range(0x8e, 0x99),
	regmap_reg_range(0x9a, 0xa5),
	regmap_reg_range(0xa6, 0xa6),
	regmap_reg_range(0xa7, 0xaa),
	regmap_reg_range(0xab, 0xae),
	regmap_reg_range(0xaf, 0xba),
	regmap_reg_range(0xbb, 0xbc),
	regmap_reg_range(0xbd, 0xbd),
	regmap_reg_range(0xc0, 0xc0),
	regmap_reg_range(0xc2, 0xc2),
	regmap_reg_range(0xc3, 0xc3),
	regmap_reg_range(0xc4, 0xc4),
	regmap_reg_range(0xc6, 0xc6),
};

static const struct regmap_access_table ksz8873_register_set = {
	.yes_ranges = ksz8873_valid_regs,
	.n_yes_ranges = ARRAY_SIZE(ksz8873_valid_regs),
};

const struct ksz_chip_data ksz_switch_chips[] = {
	[KSZ8563] = {
		.chip_id = KSZ8563_CHIP_ID,
		.dev_name = "KSZ8563",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x07,	/* can be configured as cpu port */
		.port_cnt = 3,		/* total port count */
		.port_nirqs = 3,
		.num_tx_queues = 4,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1, /* Same as ksz8795 */
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.supports_rgmii = {false, false, true},
		.internal_phy = {true, true, false},
		.gbit_capable = {false, false, true},
		.wr_table = &ksz8563_register_set,
		.rd_table = &ksz8563_register_set,
	},

	[KSZ8795] = {
		.chip_id = KSZ8795_CHIP_ID,
		.dev_name = "KSZ8795",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.num_tx_queues = 4,
		.ops = &ksz8_dev_ops,
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz8795_regs,
		.masks = ksz8795_masks,
		.shifts = ksz8795_shifts,
		.xmii_ctrl0 = ksz8795_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[KSZ8794] = {
		/* WARNING
		 * =======
		 * KSZ8794 is similar to KSZ8795, except the port map
		 * contains a gap between external and CPU ports, the
		 * port map is NOT continuous. The per-port register
		 * map is shifted accordingly too, i.e. registers at
		 * offset 0x40 are NOT used on KSZ8794 and they ARE
		 * used on KSZ8795 for external port 3.
		 *           external  cpu
		 * KSZ8794   0,1,2      4
		 * KSZ8795   0,1,2,3    4
		 * KSZ8765   0,1,2,3    4
		 * port_cnt is configured as 5, even though it is 4
		 */
		.chip_id = KSZ8794_CHIP_ID,
		.dev_name = "KSZ8794",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.num_tx_queues = 4,
		.ops = &ksz8_dev_ops,
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz8795_regs,
		.masks = ksz8795_masks,
		.shifts = ksz8795_shifts,
		.xmii_ctrl0 = ksz8795_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, false, false},
	},

	[KSZ8765] = {
		.chip_id = KSZ8765_CHIP_ID,
		.dev_name = "KSZ8765",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.num_tx_queues = 4,
		.ops = &ksz8_dev_ops,
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz8795_regs,
		.masks = ksz8795_masks,
		.shifts = ksz8795_shifts,
		.xmii_ctrl0 = ksz8795_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[KSZ8830] = {
		.chip_id = KSZ8830_CHIP_ID,
		.dev_name = "KSZ8863/KSZ8873",
		.num_vlans = 16,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x4,	/* can be configured as cpu port */
		.port_cnt = 3,
		.num_tx_queues = 4,
		.ops = &ksz8_dev_ops,
		.mib_names = ksz88xx_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz88xx_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz8863_regs,
		.masks = ksz8863_masks,
		.shifts = ksz8863_shifts,
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.internal_phy = {true, true, false},
		.wr_table = &ksz8873_register_set,
		.rd_table = &ksz8873_register_set,
	},

	[KSZ9477] = {
		.chip_id = KSZ9477_CHIP_ID,
		.dev_name = "KSZ9477",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.port_nirqs = 4,
		.num_tx_queues = 4,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   false, true, false},
		.supports_rmii	= {false, false, false, false,
				   false, true, false},
		.supports_rgmii = {false, false, false, false,
				   false, true, false},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
		.gbit_capable	= {true, true, true, true, true, true, true},
		.wr_table = &ksz9477_register_set,
		.rd_table = &ksz9477_register_set,
	},

	[KSZ9896] = {
		.chip_id = KSZ9896_CHIP_ID,
		.dev_name = "KSZ9896",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x3F,	/* can be configured as cpu port */
		.port_cnt = 6,		/* total physical port count */
		.port_nirqs = 2,
		.num_tx_queues = 4,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   false, true},
		.supports_rmii	= {false, false, false, false,
				   false, true},
		.supports_rgmii = {false, false, false, false,
				   false, true},
		.internal_phy	= {true, true, true, true,
				   true, false},
		.gbit_capable	= {true, true, true, true, true, true},
		.wr_table = &ksz9896_register_set,
		.rd_table = &ksz9896_register_set,
	},

	[KSZ9897] = {
		.chip_id = KSZ9897_CHIP_ID,
		.dev_name = "KSZ9897",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.port_nirqs = 2,
		.num_tx_queues = 4,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   false, true, true},
		.supports_rmii	= {false, false, false, false,
				   false, true, true},
		.supports_rgmii = {false, false, false, false,
				   false, true, true},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
		.gbit_capable	= {true, true, true, true, true, true, true},
	},

	[KSZ9893] = {
		.chip_id = KSZ9893_CHIP_ID,
		.dev_name = "KSZ9893",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x07,	/* can be configured as cpu port */
		.port_cnt = 3,		/* total port count */
		.port_nirqs = 2,
		.num_tx_queues = 4,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1, /* Same as ksz8795 */
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.supports_rgmii = {false, false, true},
		.internal_phy = {true, true, false},
		.gbit_capable = {true, true, true},
	},

	[KSZ9563] = {
		.chip_id = KSZ9563_CHIP_ID,
		.dev_name = "KSZ9563",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x07,	/* can be configured as cpu port */
		.port_cnt = 3,		/* total port count */
		.port_nirqs = 3,
		.num_tx_queues = 4,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz8795_xmii_ctrl1, /* Same as ksz8795 */
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.supports_rgmii = {false, false, true},
		.internal_phy = {true, true, false},
		.gbit_capable = {true, true, true},
	},

	[KSZ9567] = {
		.chip_id = KSZ9567_CHIP_ID,
		.dev_name = "KSZ9567",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.port_nirqs = 3,
		.num_tx_queues = 4,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &ksz9477_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = ksz9477_masks,
		.shifts = ksz9477_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   false, true, true},
		.supports_rmii	= {false, false, false, false,
				   false, true, true},
		.supports_rgmii = {false, false, false, false,
				   false, true, true},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
		.gbit_capable	= {true, true, true, true, true, true, true},
	},

	[LAN9370] = {
		.chip_id = LAN9370_CHIP_ID,
		.dev_name = "LAN9370",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total physical port count */
		.port_nirqs = 6,
		.num_tx_queues = 8,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &lan937x_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = lan937x_masks,
		.shifts = lan937x_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[LAN9371] = {
		.chip_id = LAN9371_CHIP_ID,
		.dev_name = "LAN9371",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 6,		/* total physical port count */
		.port_nirqs = 6,
		.num_tx_queues = 8,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &lan937x_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = lan937x_masks,
		.shifts = lan937x_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii = {false, false, false, false, true, true},
		.supports_rmii = {false, false, false, false, true, true},
		.supports_rgmii = {false, false, false, false, true, true},
		.internal_phy = {true, true, true, true, false, false},
	},

	[LAN9372] = {
		.chip_id = LAN9372_CHIP_ID,
		.dev_name = "LAN9372",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 8,		/* total physical port count */
		.port_nirqs = 6,
		.num_tx_queues = 8,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &lan937x_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = lan937x_masks,
		.shifts = lan937x_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, true,
				   false, false, true, true},
	},

	[LAN9373] = {
		.chip_id = LAN9373_CHIP_ID,
		.dev_name = "LAN9373",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x38,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total physical port count */
		.port_nirqs = 6,
		.num_tx_queues = 8,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &lan937x_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = lan937x_masks,
		.shifts = lan937x_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, false,
				   false, false, true, true},
	},

	[LAN9374] = {
		.chip_id = LAN9374_CHIP_ID,
		.dev_name = "LAN9374",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 8,		/* total physical port count */
		.port_nirqs = 6,
		.num_tx_queues = 8,
		.tc_cbs_supported = true,
		.tc_ets_supported = true,
		.ops = &lan937x_dev_ops,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.regs = ksz9477_regs,
		.masks = lan937x_masks,
		.shifts = lan937x_shifts,
		.xmii_ctrl0 = ksz9477_xmii_ctrl0,
		.xmii_ctrl1 = ksz9477_xmii_ctrl1,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, true,
				   false, false, true, true},
	},
};
EXPORT_SYMBOL_GPL(ksz_switch_chips);

static const struct ksz_chip_data *ksz_lookup_info(unsigned int prod_num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ksz_switch_chips); i++) {
		const struct ksz_chip_data *chip = &ksz_switch_chips[i];

		if (chip->chip_id == prod_num)
			return chip;
	}

	return NULL;
}

static int ksz_check_device_id(struct ksz_device *dev)
{
	const struct ksz_chip_data *dt_chip_data;

	dt_chip_data = of_device_get_match_data(dev->dev);

	/* Check for Device Tree and Chip ID */
	if (dt_chip_data->chip_id != dev->chip_id) {
		dev_err(dev->dev,
			"Device tree specifies chip %s but found %s, please fix it!\n",
			dt_chip_data->dev_name, dev->info->dev_name);
		return -ENODEV;
	}

	return 0;
}

static void ksz_phylink_get_caps(struct dsa_switch *ds, int port,
				 struct phylink_config *config)
{
	struct ksz_device *dev = ds->priv;

	if (dev->info->supports_mii[port])
		__set_bit(PHY_INTERFACE_MODE_MII, config->supported_interfaces);

	if (dev->info->supports_rmii[port])
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);

	if (dev->info->supports_rgmii[port])
		phy_interface_set_rgmii(config->supported_interfaces);

	if (dev->info->internal_phy[port]) {
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		/* Compatibility for phylib's default interface type when the
		 * phy-mode property is absent
		 */
		__set_bit(PHY_INTERFACE_MODE_GMII,
			  config->supported_interfaces);
	}

	if (dev->dev_ops->get_caps)
		dev->dev_ops->get_caps(dev, port, config);
}

void ksz_r_mib_stats64(struct ksz_device *dev, int port)
{
	struct ethtool_pause_stats *pstats;
	struct rtnl_link_stats64 *stats;
	struct ksz_stats_raw *raw;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	stats = &mib->stats64;
	pstats = &mib->pause_stats;
	raw = (struct ksz_stats_raw *)mib->counters;

	spin_lock(&mib->stats64_lock);

	stats->rx_packets = raw->rx_bcast + raw->rx_mcast + raw->rx_ucast +
		raw->rx_pause;
	stats->tx_packets = raw->tx_bcast + raw->tx_mcast + raw->tx_ucast +
		raw->tx_pause;

	/* HW counters are counting bytes + FCS which is not acceptable
	 * for rtnl_link_stats64 interface
	 */
	stats->rx_bytes = raw->rx_total - stats->rx_packets * ETH_FCS_LEN;
	stats->tx_bytes = raw->tx_total - stats->tx_packets * ETH_FCS_LEN;

	stats->rx_length_errors = raw->rx_undersize + raw->rx_fragments +
		raw->rx_oversize;

	stats->rx_crc_errors = raw->rx_crc_err;
	stats->rx_frame_errors = raw->rx_align_err;
	stats->rx_dropped = raw->rx_discards;
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
		stats->rx_frame_errors  + stats->rx_dropped;

	stats->tx_window_errors = raw->tx_late_col;
	stats->tx_fifo_errors = raw->tx_discards;
	stats->tx_aborted_errors = raw->tx_exc_col;
	stats->tx_errors = stats->tx_window_errors + stats->tx_fifo_errors +
		stats->tx_aborted_errors;

	stats->multicast = raw->rx_mcast;
	stats->collisions = raw->tx_total_col;

	pstats->tx_pause_frames = raw->tx_pause;
	pstats->rx_pause_frames = raw->rx_pause;

	spin_unlock(&mib->stats64_lock);
}

void ksz88xx_r_mib_stats64(struct ksz_device *dev, int port)
{
	struct ethtool_pause_stats *pstats;
	struct rtnl_link_stats64 *stats;
	struct ksz88xx_stats_raw *raw;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	stats = &mib->stats64;
	pstats = &mib->pause_stats;
	raw = (struct ksz88xx_stats_raw *)mib->counters;

	spin_lock(&mib->stats64_lock);

	stats->rx_packets = raw->rx_bcast + raw->rx_mcast + raw->rx_ucast +
		raw->rx_pause;
	stats->tx_packets = raw->tx_bcast + raw->tx_mcast + raw->tx_ucast +
		raw->tx_pause;

	/* HW counters are counting bytes + FCS which is not acceptable
	 * for rtnl_link_stats64 interface
	 */
	stats->rx_bytes = raw->rx + raw->rx_hi - stats->rx_packets * ETH_FCS_LEN;
	stats->tx_bytes = raw->tx + raw->tx_hi - stats->tx_packets * ETH_FCS_LEN;

	stats->rx_length_errors = raw->rx_undersize + raw->rx_fragments +
		raw->rx_oversize;

	stats->rx_crc_errors = raw->rx_crc_err;
	stats->rx_frame_errors = raw->rx_align_err;
	stats->rx_dropped = raw->rx_discards;
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
		stats->rx_frame_errors  + stats->rx_dropped;

	stats->tx_window_errors = raw->tx_late_col;
	stats->tx_fifo_errors = raw->tx_discards;
	stats->tx_aborted_errors = raw->tx_exc_col;
	stats->tx_errors = stats->tx_window_errors + stats->tx_fifo_errors +
		stats->tx_aborted_errors;

	stats->multicast = raw->rx_mcast;
	stats->collisions = raw->tx_total_col;

	pstats->tx_pause_frames = raw->tx_pause;
	pstats->rx_pause_frames = raw->rx_pause;

	spin_unlock(&mib->stats64_lock);
}

static void ksz_get_stats64(struct dsa_switch *ds, int port,
			    struct rtnl_link_stats64 *s)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;

	spin_lock(&mib->stats64_lock);
	memcpy(s, &mib->stats64, sizeof(*s));
	spin_unlock(&mib->stats64_lock);
}

static void ksz_get_pause_stats(struct dsa_switch *ds, int port,
				struct ethtool_pause_stats *pause_stats)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;

	spin_lock(&mib->stats64_lock);
	memcpy(pause_stats, &mib->pause_stats, sizeof(*pause_stats));
	spin_unlock(&mib->stats64_lock);
}

static void ksz_get_strings(struct dsa_switch *ds, int port,
			    u32 stringset, uint8_t *buf)
{
	struct ksz_device *dev = ds->priv;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < dev->info->mib_cnt; i++) {
		memcpy(buf + i * ETH_GSTRING_LEN,
		       dev->info->mib_names[i].string, ETH_GSTRING_LEN);
	}
}

static void ksz_update_port_member(struct ksz_device *dev, int port)
{
	struct ksz_port *p = &dev->ports[port];
	struct dsa_switch *ds = dev->ds;
	u8 port_member = 0, cpu_port;
	const struct dsa_port *dp;
	int i, j;

	if (!dsa_is_user_port(ds, port))
		return;

	dp = dsa_to_port(ds, port);
	cpu_port = BIT(dsa_upstream_port(ds, port));

	for (i = 0; i < ds->num_ports; i++) {
		const struct dsa_port *other_dp = dsa_to_port(ds, i);
		struct ksz_port *other_p = &dev->ports[i];
		u8 val = 0;

		if (!dsa_is_user_port(ds, i))
			continue;
		if (port == i)
			continue;
		if (!dsa_port_bridge_same(dp, other_dp))
			continue;
		if (other_p->stp_state != BR_STATE_FORWARDING)
			continue;

		if (p->stp_state == BR_STATE_FORWARDING) {
			val |= BIT(port);
			port_member |= BIT(i);
		}

		/* Retain port [i]'s relationship to other ports than [port] */
		for (j = 0; j < ds->num_ports; j++) {
			const struct dsa_port *third_dp;
			struct ksz_port *third_p;

			if (j == i)
				continue;
			if (j == port)
				continue;
			if (!dsa_is_user_port(ds, j))
				continue;
			third_p = &dev->ports[j];
			if (third_p->stp_state != BR_STATE_FORWARDING)
				continue;
			third_dp = dsa_to_port(ds, j);
			if (dsa_port_bridge_same(other_dp, third_dp))
				val |= BIT(j);
		}

		dev->dev_ops->cfg_port_member(dev, i, val | cpu_port);
	}

	dev->dev_ops->cfg_port_member(dev, port, port_member | cpu_port);
}

static int ksz_sw_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct ksz_device *dev = bus->priv;
	u16 val;
	int ret;

	ret = dev->dev_ops->r_phy(dev, addr, regnum, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int ksz_sw_mdio_write(struct mii_bus *bus, int addr, int regnum,
			     u16 val)
{
	struct ksz_device *dev = bus->priv;

	return dev->dev_ops->w_phy(dev, addr, regnum, val);
}

static int ksz_irq_phy_setup(struct ksz_device *dev)
{
	struct dsa_switch *ds = dev->ds;
	int phy;
	int irq;
	int ret;

	for (phy = 0; phy < KSZ_MAX_NUM_PORTS; phy++) {
		if (BIT(phy) & ds->phys_mii_mask) {
			irq = irq_find_mapping(dev->ports[phy].pirq.domain,
					       PORT_SRC_PHY_INT);
			if (irq < 0) {
				ret = irq;
				goto out;
			}
			ds->slave_mii_bus->irq[phy] = irq;
		}
	}
	return 0;
out:
	while (phy--)
		if (BIT(phy) & ds->phys_mii_mask)
			irq_dispose_mapping(ds->slave_mii_bus->irq[phy]);

	return ret;
}

static void ksz_irq_phy_free(struct ksz_device *dev)
{
	struct dsa_switch *ds = dev->ds;
	int phy;

	for (phy = 0; phy < KSZ_MAX_NUM_PORTS; phy++)
		if (BIT(phy) & ds->phys_mii_mask)
			irq_dispose_mapping(ds->slave_mii_bus->irq[phy]);
}

static int ksz_mdio_register(struct ksz_device *dev)
{
	struct dsa_switch *ds = dev->ds;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret;

	mdio_np = of_get_child_by_name(dev->dev->of_node, "mdio");
	if (!mdio_np)
		return 0;

	bus = devm_mdiobus_alloc(ds->dev);
	if (!bus) {
		of_node_put(mdio_np);
		return -ENOMEM;
	}

	bus->priv = dev;
	bus->read = ksz_sw_mdio_read;
	bus->write = ksz_sw_mdio_write;
	bus->name = "ksz slave smi";
	snprintf(bus->id, MII_BUS_ID_SIZE, "SMI-%d", ds->index);
	bus->parent = ds->dev;
	bus->phy_mask = ~ds->phys_mii_mask;

	ds->slave_mii_bus = bus;

	if (dev->irq > 0) {
		ret = ksz_irq_phy_setup(dev);
		if (ret) {
			of_node_put(mdio_np);
			return ret;
		}
	}

	ret = devm_of_mdiobus_register(ds->dev, bus, mdio_np);
	if (ret) {
		dev_err(ds->dev, "unable to register MDIO bus %s\n",
			bus->id);
		if (dev->irq > 0)
			ksz_irq_phy_free(dev);
	}

	of_node_put(mdio_np);

	return ret;
}

static void ksz_irq_mask(struct irq_data *d)
{
	struct ksz_irq *kirq = irq_data_get_irq_chip_data(d);

	kirq->masked |= BIT(d->hwirq);
}

static void ksz_irq_unmask(struct irq_data *d)
{
	struct ksz_irq *kirq = irq_data_get_irq_chip_data(d);

	kirq->masked &= ~BIT(d->hwirq);
}

static void ksz_irq_bus_lock(struct irq_data *d)
{
	struct ksz_irq *kirq  = irq_data_get_irq_chip_data(d);

	mutex_lock(&kirq->dev->lock_irq);
}

static void ksz_irq_bus_sync_unlock(struct irq_data *d)
{
	struct ksz_irq *kirq  = irq_data_get_irq_chip_data(d);
	struct ksz_device *dev = kirq->dev;
	int ret;

	ret = ksz_write32(dev, kirq->reg_mask, kirq->masked);
	if (ret)
		dev_err(dev->dev, "failed to change IRQ mask\n");

	mutex_unlock(&dev->lock_irq);
}

static const struct irq_chip ksz_irq_chip = {
	.name			= "ksz-irq",
	.irq_mask		= ksz_irq_mask,
	.irq_unmask		= ksz_irq_unmask,
	.irq_bus_lock		= ksz_irq_bus_lock,
	.irq_bus_sync_unlock	= ksz_irq_bus_sync_unlock,
};

static int ksz_irq_domain_map(struct irq_domain *d,
			      unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &ksz_irq_chip, handle_level_irq);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops ksz_irq_domain_ops = {
	.map	= ksz_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void ksz_irq_free(struct ksz_irq *kirq)
{
	int irq, virq;

	free_irq(kirq->irq_num, kirq);

	for (irq = 0; irq < kirq->nirqs; irq++) {
		virq = irq_find_mapping(kirq->domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(kirq->domain);
}

static irqreturn_t ksz_irq_thread_fn(int irq, void *dev_id)
{
	struct ksz_irq *kirq = dev_id;
	unsigned int nhandled = 0;
	struct ksz_device *dev;
	unsigned int sub_irq;
	u8 data;
	int ret;
	u8 n;

	dev = kirq->dev;

	/* Read interrupt status register */
	ret = ksz_read8(dev, kirq->reg_status, &data);
	if (ret)
		goto out;

	for (n = 0; n < kirq->nirqs; ++n) {
		if (data & BIT(n)) {
			sub_irq = irq_find_mapping(kirq->domain, n);
			handle_nested_irq(sub_irq);
			++nhandled;
		}
	}
out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static int ksz_irq_common_setup(struct ksz_device *dev, struct ksz_irq *kirq)
{
	int ret, n;

	kirq->dev = dev;
	kirq->masked = ~0;

	kirq->domain = irq_domain_add_simple(dev->dev->of_node, kirq->nirqs, 0,
					     &ksz_irq_domain_ops, kirq);
	if (!kirq->domain)
		return -ENOMEM;

	for (n = 0; n < kirq->nirqs; n++)
		irq_create_mapping(kirq->domain, n);

	ret = request_threaded_irq(kirq->irq_num, NULL, ksz_irq_thread_fn,
				   IRQF_ONESHOT, kirq->name, kirq);
	if (ret)
		goto out;

	return 0;

out:
	ksz_irq_free(kirq);

	return ret;
}

static int ksz_girq_setup(struct ksz_device *dev)
{
	struct ksz_irq *girq = &dev->girq;

	girq->nirqs = dev->info->port_cnt;
	girq->reg_mask = REG_SW_PORT_INT_MASK__1;
	girq->reg_status = REG_SW_PORT_INT_STATUS__1;
	snprintf(girq->name, sizeof(girq->name), "global_port_irq");

	girq->irq_num = dev->irq;

	return ksz_irq_common_setup(dev, girq);
}

static int ksz_pirq_setup(struct ksz_device *dev, u8 p)
{
	struct ksz_irq *pirq = &dev->ports[p].pirq;

	pirq->nirqs = dev->info->port_nirqs;
	pirq->reg_mask = dev->dev_ops->get_port_addr(p, REG_PORT_INT_MASK);
	pirq->reg_status = dev->dev_ops->get_port_addr(p, REG_PORT_INT_STATUS);
	snprintf(pirq->name, sizeof(pirq->name), "port_irq-%d", p);

	pirq->irq_num = irq_find_mapping(dev->girq.domain, p);
	if (pirq->irq_num < 0)
		return pirq->irq_num;

	return ksz_irq_common_setup(dev, pirq);
}

static int ksz_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct dsa_port *dp;
	struct ksz_port *p;
	const u16 *regs;
	int ret;

	regs = dev->info->regs;

	dev->vlan_cache = devm_kcalloc(dev->dev, sizeof(struct vlan_table),
				       dev->info->num_vlans, GFP_KERNEL);
	if (!dev->vlan_cache)
		return -ENOMEM;

	ret = dev->dev_ops->reset(dev);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	/* set broadcast storm protection 10% rate */
	regmap_update_bits(ksz_regmap_16(dev), regs[S_BROADCAST_CTRL],
			   BROADCAST_STORM_RATE,
			   (BROADCAST_STORM_VALUE *
			   BROADCAST_STORM_PROT_RATE) / 100);

	dev->dev_ops->config_cpu_port(ds);

	dev->dev_ops->enable_stp_addr(dev);

	ds->num_tx_queues = dev->info->num_tx_queues;

	regmap_update_bits(ksz_regmap_8(dev), regs[S_MULTICAST_CTRL],
			   MULTICAST_STORM_DISABLE, MULTICAST_STORM_DISABLE);

	ksz_init_mib_timer(dev);

	ds->configure_vlan_while_not_filtering = false;

	if (dev->dev_ops->setup) {
		ret = dev->dev_ops->setup(ds);
		if (ret)
			return ret;
	}

	/* Start with learning disabled on standalone user ports, and enabled
	 * on the CPU port. In lack of other finer mechanisms, learning on the
	 * CPU port will avoid flooding bridge local addresses on the network
	 * in some cases.
	 */
	p = &dev->ports[dev->cpu_port];
	p->learning = true;

	if (dev->irq > 0) {
		ret = ksz_girq_setup(dev);
		if (ret)
			return ret;

		dsa_switch_for_each_user_port(dp, dev->ds) {
			ret = ksz_pirq_setup(dev, dp->index);
			if (ret)
				goto out_girq;

			ret = ksz_ptp_irq_setup(ds, dp->index);
			if (ret)
				goto out_pirq;
		}
	}

	ret = ksz_ptp_clock_register(ds);
	if (ret) {
		dev_err(dev->dev, "Failed to register PTP clock: %d\n", ret);
		goto out_ptpirq;
	}

	ret = ksz_mdio_register(dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to register the mdio");
		goto out_ptp_clock_unregister;
	}

	/* start switch */
	regmap_update_bits(ksz_regmap_8(dev), regs[S_START_CTRL],
			   SW_START, SW_START);

	return 0;

out_ptp_clock_unregister:
	ksz_ptp_clock_unregister(ds);
out_ptpirq:
	if (dev->irq > 0)
		dsa_switch_for_each_user_port(dp, dev->ds)
			ksz_ptp_irq_free(ds, dp->index);
out_pirq:
	if (dev->irq > 0)
		dsa_switch_for_each_user_port(dp, dev->ds)
			ksz_irq_free(&dev->ports[dp->index].pirq);
out_girq:
	if (dev->irq > 0)
		ksz_irq_free(&dev->girq);

	return ret;
}

static void ksz_teardown(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct dsa_port *dp;

	ksz_ptp_clock_unregister(ds);

	if (dev->irq > 0) {
		dsa_switch_for_each_user_port(dp, dev->ds) {
			ksz_ptp_irq_free(ds, dp->index);

			ksz_irq_free(&dev->ports[dp->index].pirq);
		}

		ksz_irq_free(&dev->girq);
	}

	if (dev->dev_ops->teardown)
		dev->dev_ops->teardown(ds);
}

static void port_r_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;
	u64 *dropped;

	/* Some ports may not have MIB counters before SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->reg_mib_cnt) {
		dev->dev_ops->r_mib_cnt(dev, port, mib->cnt_ptr,
					&mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}

	/* last one in storage */
	dropped = &mib->counters[dev->info->mib_cnt];

	/* Some ports may not have MIB counters after SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->mib_cnt) {
		dev->dev_ops->r_mib_pkt(dev, port, mib->cnt_ptr,
					dropped, &mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}
	mib->cnt_ptr = 0;
}

static void ksz_mib_read_work(struct work_struct *work)
{
	struct ksz_device *dev = container_of(work, struct ksz_device,
					      mib_read.work);
	struct ksz_port_mib *mib;
	struct ksz_port *p;
	int i;

	for (i = 0; i < dev->info->port_cnt; i++) {
		if (dsa_is_unused_port(dev->ds, i))
			continue;

		p = &dev->ports[i];
		mib = &p->mib;
		mutex_lock(&mib->cnt_mutex);

		/* Only read MIB counters when the port is told to do.
		 * If not, read only dropped counters when link is not up.
		 */
		if (!p->read) {
			const struct dsa_port *dp = dsa_to_port(dev->ds, i);

			if (!netif_carrier_ok(dp->slave))
				mib->cnt_ptr = dev->info->reg_mib_cnt;
		}
		port_r_cnt(dev, i);
		p->read = false;

		if (dev->dev_ops->r_mib_stat64)
			dev->dev_ops->r_mib_stat64(dev, i);

		mutex_unlock(&mib->cnt_mutex);
	}

	schedule_delayed_work(&dev->mib_read, dev->mib_read_interval);
}

void ksz_init_mib_timer(struct ksz_device *dev)
{
	int i;

	INIT_DELAYED_WORK(&dev->mib_read, ksz_mib_read_work);

	for (i = 0; i < dev->info->port_cnt; i++) {
		struct ksz_port_mib *mib = &dev->ports[i].mib;

		dev->dev_ops->port_init_cnt(dev, i);

		mib->cnt_ptr = 0;
		memset(mib->counters, 0, dev->info->mib_cnt * sizeof(u64));
	}
}

static int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct ksz_device *dev = ds->priv;
	u16 val = 0xffff;
	int ret;

	ret = dev->dev_ops->r_phy(dev, addr, reg, &val);
	if (ret)
		return ret;

	return val;
}

static int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	ret = dev->dev_ops->w_phy(dev, addr, reg, val);
	if (ret)
		return ret;

	return 0;
}

static u32 ksz_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	switch (dev->chip_id) {
	case KSZ8830_CHIP_ID:
		/* Silicon Errata Sheet (DS80000830A):
		 * Port 1 does not work with LinkMD Cable-Testing.
		 * Port 1 does not respond to received PAUSE control frames.
		 */
		if (!port)
			return MICREL_KSZ8_P1_ERRATA;
		break;
	case KSZ9477_CHIP_ID:
		/* KSZ9477 Errata DS80000754C
		 *
		 * Module 4: Energy Efficient Ethernet (EEE) feature select must
		 * be manually disabled
		 *   The EEE feature is enabled by default, but it is not fully
		 *   operational. It must be manually disabled through register
		 *   controls. If not disabled, the PHY ports can auto-negotiate
		 *   to enable EEE, and this feature can cause link drops when
		 *   linked to another device supporting EEE.
		 */
		return MICREL_NO_EEE;
	}

	return 0;
}

static void ksz_mac_link_down(struct dsa_switch *ds, int port,
			      unsigned int mode, phy_interface_t interface)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p = &dev->ports[port];

	/* Read all MIB counters when the link is going down. */
	p->read = true;
	/* timer started */
	if (dev->mib_read_interval)
		schedule_delayed_work(&dev->mib_read, 0);
}

static int ksz_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ksz_device *dev = ds->priv;

	if (sset != ETH_SS_STATS)
		return 0;

	return dev->info->mib_cnt;
}

static void ksz_get_ethtool_stats(struct dsa_switch *ds, int port,
				  uint64_t *buf)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	mutex_lock(&mib->cnt_mutex);

	/* Only read dropped counters if no link. */
	if (!netif_carrier_ok(dp->slave))
		mib->cnt_ptr = dev->info->reg_mib_cnt;
	port_r_cnt(dev, port);
	memcpy(buf, mib->counters, dev->info->mib_cnt * sizeof(u64));
	mutex_unlock(&mib->cnt_mutex);
}

static int ksz_port_bridge_join(struct dsa_switch *ds, int port,
				struct dsa_bridge bridge,
				bool *tx_fwd_offload,
				struct netlink_ext_ack *extack)
{
	/* port_stp_state_set() will be called after to put the port in
	 * appropriate state so there is no need to do anything.
	 */

	return 0;
}

static void ksz_port_bridge_leave(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge)
{
	/* port_stp_state_set() will be called after to put the port in
	 * forwarding state so there is no need to do anything.
	 */
}

static void ksz_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	dev->dev_ops->flush_dyn_mac_table(dev, port);
}

static int ksz_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->set_ageing_time)
		return -EOPNOTSUPP;

	return dev->dev_ops->set_ageing_time(dev, msecs);
}

static int ksz_port_fdb_add(struct dsa_switch *ds, int port,
			    const unsigned char *addr, u16 vid,
			    struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->fdb_add)
		return -EOPNOTSUPP;

	return dev->dev_ops->fdb_add(dev, port, addr, vid, db);
}

static int ksz_port_fdb_del(struct dsa_switch *ds, int port,
			    const unsigned char *addr,
			    u16 vid, struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->fdb_del)
		return -EOPNOTSUPP;

	return dev->dev_ops->fdb_del(dev, port, addr, vid, db);
}

static int ksz_port_fdb_dump(struct dsa_switch *ds, int port,
			     dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->fdb_dump)
		return -EOPNOTSUPP;

	return dev->dev_ops->fdb_dump(dev, port, cb, data);
}

static int ksz_port_mdb_add(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_mdb *mdb,
			    struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->mdb_add)
		return -EOPNOTSUPP;

	return dev->dev_ops->mdb_add(dev, port, mdb, db);
}

static int ksz_port_mdb_del(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_mdb *mdb,
			    struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->mdb_del)
		return -EOPNOTSUPP;

	return dev->dev_ops->mdb_del(dev, port, mdb, db);
}

static int ksz_port_setup(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	if (!dsa_is_user_port(ds, port))
		return 0;

	/* setup slave port */
	dev->dev_ops->port_setup(dev, port, false);

	/* port_stp_state_set() will be called after to enable the port so
	 * there is no need to do anything.
	 */

	return 0;
}

void ksz_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p;
	const u16 *regs;
	u8 data;

	regs = dev->info->regs;

	ksz_pread8(dev, port, regs[P_STP_CTRL], &data);
	data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE | PORT_LEARN_DISABLE);

	p = &dev->ports[port];

	switch (state) {
	case BR_STATE_DISABLED:
		data |= PORT_LEARN_DISABLE;
		break;
	case BR_STATE_LISTENING:
		data |= (PORT_RX_ENABLE | PORT_LEARN_DISABLE);
		break;
	case BR_STATE_LEARNING:
		data |= PORT_RX_ENABLE;
		if (!p->learning)
			data |= PORT_LEARN_DISABLE;
		break;
	case BR_STATE_FORWARDING:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);
		if (!p->learning)
			data |= PORT_LEARN_DISABLE;
		break;
	case BR_STATE_BLOCKING:
		data |= PORT_LEARN_DISABLE;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ksz_pwrite8(dev, port, regs[P_STP_CTRL], data);

	p->stp_state = state;

	ksz_update_port_member(dev, port);
}

static void ksz_port_teardown(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	switch (dev->chip_id) {
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
		if (dsa_is_user_port(ds, port))
			ksz9477_port_acl_free(dev, port);
	}
}

static int ksz_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				     struct switchdev_brport_flags flags,
				     struct netlink_ext_ack *extack)
{
	if (flags.mask & ~BR_LEARNING)
		return -EINVAL;

	return 0;
}

static int ksz_port_bridge_flags(struct dsa_switch *ds, int port,
				 struct switchdev_brport_flags flags,
				 struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p = &dev->ports[port];

	if (flags.mask & BR_LEARNING) {
		p->learning = !!(flags.val & BR_LEARNING);

		/* Make the change take effect immediately */
		ksz_port_stp_state_set(ds, port, p->stp_state);
	}

	return 0;
}

static enum dsa_tag_protocol ksz_get_tag_protocol(struct dsa_switch *ds,
						  int port,
						  enum dsa_tag_protocol mp)
{
	struct ksz_device *dev = ds->priv;
	enum dsa_tag_protocol proto = DSA_TAG_PROTO_NONE;

	if (dev->chip_id == KSZ8795_CHIP_ID ||
	    dev->chip_id == KSZ8794_CHIP_ID ||
	    dev->chip_id == KSZ8765_CHIP_ID)
		proto = DSA_TAG_PROTO_KSZ8795;

	if (dev->chip_id == KSZ8830_CHIP_ID ||
	    dev->chip_id == KSZ8563_CHIP_ID ||
	    dev->chip_id == KSZ9893_CHIP_ID ||
	    dev->chip_id == KSZ9563_CHIP_ID)
		proto = DSA_TAG_PROTO_KSZ9893;

	if (dev->chip_id == KSZ9477_CHIP_ID ||
	    dev->chip_id == KSZ9896_CHIP_ID ||
	    dev->chip_id == KSZ9897_CHIP_ID ||
	    dev->chip_id == KSZ9567_CHIP_ID)
		proto = DSA_TAG_PROTO_KSZ9477;

	if (is_lan937x(dev))
		proto = DSA_TAG_PROTO_LAN937X_VALUE;

	return proto;
}

static int ksz_connect_tag_protocol(struct dsa_switch *ds,
				    enum dsa_tag_protocol proto)
{
	struct ksz_tagger_data *tagger_data;

	tagger_data = ksz_tagger_data(ds);
	tagger_data->xmit_work_fn = ksz_port_deferred_xmit;

	return 0;
}

static int ksz_port_vlan_filtering(struct dsa_switch *ds, int port,
				   bool flag, struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->vlan_filtering)
		return -EOPNOTSUPP;

	return dev->dev_ops->vlan_filtering(dev, port, flag, extack);
}

static int ksz_port_vlan_add(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_vlan *vlan,
			     struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->vlan_add)
		return -EOPNOTSUPP;

	return dev->dev_ops->vlan_add(dev, port, vlan, extack);
}

static int ksz_port_vlan_del(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_vlan *vlan)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->vlan_del)
		return -EOPNOTSUPP;

	return dev->dev_ops->vlan_del(dev, port, vlan);
}

static int ksz_port_mirror_add(struct dsa_switch *ds, int port,
			       struct dsa_mall_mirror_tc_entry *mirror,
			       bool ingress, struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->mirror_add)
		return -EOPNOTSUPP;

	return dev->dev_ops->mirror_add(dev, port, mirror, ingress, extack);
}

static void ksz_port_mirror_del(struct dsa_switch *ds, int port,
				struct dsa_mall_mirror_tc_entry *mirror)
{
	struct ksz_device *dev = ds->priv;

	if (dev->dev_ops->mirror_del)
		dev->dev_ops->mirror_del(dev, port, mirror);
}

static int ksz_change_mtu(struct dsa_switch *ds, int port, int mtu)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->dev_ops->change_mtu)
		return -EOPNOTSUPP;

	return dev->dev_ops->change_mtu(dev, port, mtu);
}

static int ksz_max_mtu(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	switch (dev->chip_id) {
	case KSZ8795_CHIP_ID:
	case KSZ8794_CHIP_ID:
	case KSZ8765_CHIP_ID:
		return KSZ8795_HUGE_PACKET_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN;
	case KSZ8830_CHIP_ID:
		return KSZ8863_HUGE_PACKET_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN;
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
	case LAN9370_CHIP_ID:
	case LAN9371_CHIP_ID:
	case LAN9372_CHIP_ID:
	case LAN9373_CHIP_ID:
	case LAN9374_CHIP_ID:
		return KSZ9477_MAX_FRAME_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN;
	}

	return -EOPNOTSUPP;
}

static int ksz_validate_eee(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	if (!dev->info->internal_phy[port])
		return -EOPNOTSUPP;

	switch (dev->chip_id) {
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
		return 0;
	}

	return -EOPNOTSUPP;
}

static int ksz_get_mac_eee(struct dsa_switch *ds, int port,
			   struct ethtool_eee *e)
{
	int ret;

	ret = ksz_validate_eee(ds, port);
	if (ret)
		return ret;

	/* There is no documented control of Tx LPI configuration. */
	e->tx_lpi_enabled = true;

	/* There is no documented control of Tx LPI timer. According to tests
	 * Tx LPI timer seems to be set by default to minimal value.
	 */
	e->tx_lpi_timer = 0;

	return 0;
}

static int ksz_set_mac_eee(struct dsa_switch *ds, int port,
			   struct ethtool_eee *e)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	ret = ksz_validate_eee(ds, port);
	if (ret)
		return ret;

	if (!e->tx_lpi_enabled) {
		dev_err(dev->dev, "Disabling EEE Tx LPI is not supported\n");
		return -EINVAL;
	}

	if (e->tx_lpi_timer) {
		dev_err(dev->dev, "Setting EEE Tx LPI timer is not supported\n");
		return -EINVAL;
	}

	return 0;
}

static void ksz_set_xmii(struct ksz_device *dev, int port,
			 phy_interface_t interface)
{
	const u8 *bitval = dev->info->xmii_ctrl1;
	struct ksz_port *p = &dev->ports[port];
	const u16 *regs = dev->info->regs;
	u8 data8;

	ksz_pread8(dev, port, regs[P_XMII_CTRL_1], &data8);

	data8 &= ~(P_MII_SEL_M | P_RGMII_ID_IG_ENABLE |
		   P_RGMII_ID_EG_ENABLE);

	switch (interface) {
	case PHY_INTERFACE_MODE_MII:
		data8 |= bitval[P_MII_SEL];
		break;
	case PHY_INTERFACE_MODE_RMII:
		data8 |= bitval[P_RMII_SEL];
		break;
	case PHY_INTERFACE_MODE_GMII:
		data8 |= bitval[P_GMII_SEL];
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		data8 |= bitval[P_RGMII_SEL];
		/* On KSZ9893, disable RGMII in-band status support */
		if (dev->chip_id == KSZ9893_CHIP_ID ||
		    dev->chip_id == KSZ8563_CHIP_ID ||
		    dev->chip_id == KSZ9563_CHIP_ID)
			data8 &= ~P_MII_MAC_MODE;
		break;
	default:
		dev_err(dev->dev, "Unsupported interface '%s' for port %d\n",
			phy_modes(interface), port);
		return;
	}

	if (p->rgmii_tx_val)
		data8 |= P_RGMII_ID_EG_ENABLE;

	if (p->rgmii_rx_val)
		data8 |= P_RGMII_ID_IG_ENABLE;

	/* Write the updated value */
	ksz_pwrite8(dev, port, regs[P_XMII_CTRL_1], data8);
}

phy_interface_t ksz_get_xmii(struct ksz_device *dev, int port, bool gbit)
{
	const u8 *bitval = dev->info->xmii_ctrl1;
	const u16 *regs = dev->info->regs;
	phy_interface_t interface;
	u8 data8;
	u8 val;

	ksz_pread8(dev, port, regs[P_XMII_CTRL_1], &data8);

	val = FIELD_GET(P_MII_SEL_M, data8);

	if (val == bitval[P_MII_SEL]) {
		if (gbit)
			interface = PHY_INTERFACE_MODE_GMII;
		else
			interface = PHY_INTERFACE_MODE_MII;
	} else if (val == bitval[P_RMII_SEL]) {
		interface = PHY_INTERFACE_MODE_RGMII;
	} else {
		interface = PHY_INTERFACE_MODE_RGMII;
		if (data8 & P_RGMII_ID_EG_ENABLE)
			interface = PHY_INTERFACE_MODE_RGMII_TXID;
		if (data8 & P_RGMII_ID_IG_ENABLE) {
			interface = PHY_INTERFACE_MODE_RGMII_RXID;
			if (data8 & P_RGMII_ID_EG_ENABLE)
				interface = PHY_INTERFACE_MODE_RGMII_ID;
		}
	}

	return interface;
}

static void ksz_phylink_mac_config(struct dsa_switch *ds, int port,
				   unsigned int mode,
				   const struct phylink_link_state *state)
{
	struct ksz_device *dev = ds->priv;

	if (ksz_is_ksz88x3(dev))
		return;

	/* Internal PHYs */
	if (dev->info->internal_phy[port])
		return;

	if (phylink_autoneg_inband(mode)) {
		dev_err(dev->dev, "In-band AN not supported!\n");
		return;
	}

	ksz_set_xmii(dev, port, state->interface);

	if (dev->dev_ops->phylink_mac_config)
		dev->dev_ops->phylink_mac_config(dev, port, mode, state);

	if (dev->dev_ops->setup_rgmii_delay)
		dev->dev_ops->setup_rgmii_delay(dev, port);
}

bool ksz_get_gbit(struct ksz_device *dev, int port)
{
	const u8 *bitval = dev->info->xmii_ctrl1;
	const u16 *regs = dev->info->regs;
	bool gbit = false;
	u8 data8;
	bool val;

	ksz_pread8(dev, port, regs[P_XMII_CTRL_1], &data8);

	val = FIELD_GET(P_GMII_1GBIT_M, data8);

	if (val == bitval[P_GMII_1GBIT])
		gbit = true;

	return gbit;
}

static void ksz_set_gbit(struct ksz_device *dev, int port, bool gbit)
{
	const u8 *bitval = dev->info->xmii_ctrl1;
	const u16 *regs = dev->info->regs;
	u8 data8;

	ksz_pread8(dev, port, regs[P_XMII_CTRL_1], &data8);

	data8 &= ~P_GMII_1GBIT_M;

	if (gbit)
		data8 |= FIELD_PREP(P_GMII_1GBIT_M, bitval[P_GMII_1GBIT]);
	else
		data8 |= FIELD_PREP(P_GMII_1GBIT_M, bitval[P_GMII_NOT_1GBIT]);

	/* Write the updated value */
	ksz_pwrite8(dev, port, regs[P_XMII_CTRL_1], data8);
}

static void ksz_set_100_10mbit(struct ksz_device *dev, int port, int speed)
{
	const u8 *bitval = dev->info->xmii_ctrl0;
	const u16 *regs = dev->info->regs;
	u8 data8;

	ksz_pread8(dev, port, regs[P_XMII_CTRL_0], &data8);

	data8 &= ~P_MII_100MBIT_M;

	if (speed == SPEED_100)
		data8 |= FIELD_PREP(P_MII_100MBIT_M, bitval[P_MII_100MBIT]);
	else
		data8 |= FIELD_PREP(P_MII_100MBIT_M, bitval[P_MII_10MBIT]);

	/* Write the updated value */
	ksz_pwrite8(dev, port, regs[P_XMII_CTRL_0], data8);
}

static void ksz_port_set_xmii_speed(struct ksz_device *dev, int port, int speed)
{
	if (speed == SPEED_1000)
		ksz_set_gbit(dev, port, true);
	else
		ksz_set_gbit(dev, port, false);

	if (speed == SPEED_100 || speed == SPEED_10)
		ksz_set_100_10mbit(dev, port, speed);
}

static void ksz_duplex_flowctrl(struct ksz_device *dev, int port, int duplex,
				bool tx_pause, bool rx_pause)
{
	const u8 *bitval = dev->info->xmii_ctrl0;
	const u32 *masks = dev->info->masks;
	const u16 *regs = dev->info->regs;
	u8 mask;
	u8 val;

	mask = P_MII_DUPLEX_M | masks[P_MII_TX_FLOW_CTRL] |
	       masks[P_MII_RX_FLOW_CTRL];

	if (duplex == DUPLEX_FULL)
		val = FIELD_PREP(P_MII_DUPLEX_M, bitval[P_MII_FULL_DUPLEX]);
	else
		val = FIELD_PREP(P_MII_DUPLEX_M, bitval[P_MII_HALF_DUPLEX]);

	if (tx_pause)
		val |= masks[P_MII_TX_FLOW_CTRL];

	if (rx_pause)
		val |= masks[P_MII_RX_FLOW_CTRL];

	ksz_prmw8(dev, port, regs[P_XMII_CTRL_0], mask, val);
}

static void ksz9477_phylink_mac_link_up(struct ksz_device *dev, int port,
					unsigned int mode,
					phy_interface_t interface,
					struct phy_device *phydev, int speed,
					int duplex, bool tx_pause,
					bool rx_pause)
{
	struct ksz_port *p;

	p = &dev->ports[port];

	/* Internal PHYs */
	if (dev->info->internal_phy[port])
		return;

	p->phydev.speed = speed;

	ksz_port_set_xmii_speed(dev, port, speed);

	ksz_duplex_flowctrl(dev, port, duplex, tx_pause, rx_pause);
}

static void ksz_phylink_mac_link_up(struct dsa_switch *ds, int port,
				    unsigned int mode,
				    phy_interface_t interface,
				    struct phy_device *phydev, int speed,
				    int duplex, bool tx_pause, bool rx_pause)
{
	struct ksz_device *dev = ds->priv;

	if (dev->dev_ops->phylink_mac_link_up)
		dev->dev_ops->phylink_mac_link_up(dev, port, mode, interface,
						  phydev, speed, duplex,
						  tx_pause, rx_pause);
}

static int ksz_switch_detect(struct ksz_device *dev)
{
	u8 id1, id2, id4;
	u16 id16;
	u32 id32;
	int ret;

	/* read chip id */
	ret = ksz_read16(dev, REG_CHIP_ID0, &id16);
	if (ret)
		return ret;

	id1 = FIELD_GET(SW_FAMILY_ID_M, id16);
	id2 = FIELD_GET(SW_CHIP_ID_M, id16);

	switch (id1) {
	case KSZ87_FAMILY_ID:
		if (id2 == KSZ87_CHIP_ID_95) {
			u8 val;

			dev->chip_id = KSZ8795_CHIP_ID;

			ksz_read8(dev, KSZ8_PORT_STATUS_0, &val);
			if (val & KSZ8_PORT_FIBER_MODE)
				dev->chip_id = KSZ8765_CHIP_ID;
		} else if (id2 == KSZ87_CHIP_ID_94) {
			dev->chip_id = KSZ8794_CHIP_ID;
		} else {
			return -ENODEV;
		}
		break;
	case KSZ88_FAMILY_ID:
		if (id2 == KSZ88_CHIP_ID_63)
			dev->chip_id = KSZ8830_CHIP_ID;
		else
			return -ENODEV;
		break;
	default:
		ret = ksz_read32(dev, REG_CHIP_ID0, &id32);
		if (ret)
			return ret;

		dev->chip_rev = FIELD_GET(SW_REV_ID_M, id32);
		id32 &= ~0xFF;

		switch (id32) {
		case KSZ9477_CHIP_ID:
		case KSZ9896_CHIP_ID:
		case KSZ9897_CHIP_ID:
		case KSZ9567_CHIP_ID:
		case LAN9370_CHIP_ID:
		case LAN9371_CHIP_ID:
		case LAN9372_CHIP_ID:
		case LAN9373_CHIP_ID:
		case LAN9374_CHIP_ID:
			dev->chip_id = id32;
			break;
		case KSZ9893_CHIP_ID:
			ret = ksz_read8(dev, REG_CHIP_ID4,
					&id4);
			if (ret)
				return ret;

			if (id4 == SKU_ID_KSZ8563)
				dev->chip_id = KSZ8563_CHIP_ID;
			else if (id4 == SKU_ID_KSZ9563)
				dev->chip_id = KSZ9563_CHIP_ID;
			else
				dev->chip_id = KSZ9893_CHIP_ID;

			break;
		default:
			dev_err(dev->dev,
				"unsupported switch detected %x)\n", id32);
			return -ENODEV;
		}
	}
	return 0;
}

static int ksz_cls_flower_add(struct dsa_switch *ds, int port,
			      struct flow_cls_offload *cls, bool ingress)
{
	struct ksz_device *dev = ds->priv;

	switch (dev->chip_id) {
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
		return ksz9477_cls_flower_add(ds, port, cls, ingress);
	}

	return -EOPNOTSUPP;
}

static int ksz_cls_flower_del(struct dsa_switch *ds, int port,
			      struct flow_cls_offload *cls, bool ingress)
{
	struct ksz_device *dev = ds->priv;

	switch (dev->chip_id) {
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
		return ksz9477_cls_flower_del(ds, port, cls, ingress);
	}

	return -EOPNOTSUPP;
}

/* Bandwidth is calculated by idle slope/transmission speed. Then the Bandwidth
 * is converted to Hex-decimal using the successive multiplication method. On
 * every step, integer part is taken and decimal part is carry forwarded.
 */
static int cinc_cal(s32 idle_slope, s32 send_slope, u32 *bw)
{
	u32 cinc = 0;
	u32 txrate;
	u32 rate;
	u8 temp;
	u8 i;

	txrate = idle_slope - send_slope;

	if (!txrate)
		return -EINVAL;

	rate = idle_slope;

	/* 24 bit register */
	for (i = 0; i < 6; i++) {
		rate = rate * 16;

		temp = rate / txrate;

		rate %= txrate;

		cinc = ((cinc << 4) | temp);
	}

	*bw = cinc;

	return 0;
}

static int ksz_setup_tc_mode(struct ksz_device *dev, int port, u8 scheduler,
			     u8 shaper)
{
	return ksz_pwrite8(dev, port, REG_PORT_MTI_QUEUE_CTRL_0,
			   FIELD_PREP(MTI_SCHEDULE_MODE_M, scheduler) |
			   FIELD_PREP(MTI_SHAPING_M, shaper));
}

static int ksz_setup_tc_cbs(struct dsa_switch *ds, int port,
			    struct tc_cbs_qopt_offload *qopt)
{
	struct ksz_device *dev = ds->priv;
	int ret;
	u32 bw;

	if (!dev->info->tc_cbs_supported)
		return -EOPNOTSUPP;

	if (qopt->queue > dev->info->num_tx_queues)
		return -EINVAL;

	/* Queue Selection */
	ret = ksz_pwrite32(dev, port, REG_PORT_MTI_QUEUE_INDEX__4, qopt->queue);
	if (ret)
		return ret;

	if (!qopt->enable)
		return ksz_setup_tc_mode(dev, port, MTI_SCHEDULE_WRR,
					 MTI_SHAPING_OFF);

	/* High Credit */
	ret = ksz_pwrite16(dev, port, REG_PORT_MTI_HI_WATER_MARK,
			   qopt->hicredit);
	if (ret)
		return ret;

	/* Low Credit */
	ret = ksz_pwrite16(dev, port, REG_PORT_MTI_LO_WATER_MARK,
			   qopt->locredit);
	if (ret)
		return ret;

	/* Credit Increment Register */
	ret = cinc_cal(qopt->idleslope, qopt->sendslope, &bw);
	if (ret)
		return ret;

	if (dev->dev_ops->tc_cbs_set_cinc) {
		ret = dev->dev_ops->tc_cbs_set_cinc(dev, port, bw);
		if (ret)
			return ret;
	}

	return ksz_setup_tc_mode(dev, port, MTI_SCHEDULE_STRICT_PRIO,
				 MTI_SHAPING_SRP);
}

static int ksz_disable_egress_rate_limit(struct ksz_device *dev, int port)
{
	int queue, ret;

	/* Configuration will not take effect until the last Port Queue X
	 * Egress Limit Control Register is written.
	 */
	for (queue = 0; queue < dev->info->num_tx_queues; queue++) {
		ret = ksz_pwrite8(dev, port, KSZ9477_REG_PORT_OUT_RATE_0 + queue,
				  KSZ9477_OUT_RATE_NO_LIMIT);
		if (ret)
			return ret;
	}

	return 0;
}

static int ksz_ets_band_to_queue(struct tc_ets_qopt_offload_replace_params *p,
				 int band)
{
	/* Compared to queues, bands prioritize packets differently. In strict
	 * priority mode, the lowest priority is assigned to Queue 0 while the
	 * highest priority is given to Band 0.
	 */
	return p->bands - 1 - band;
}

static int ksz_queue_set_strict(struct ksz_device *dev, int port, int queue)
{
	int ret;

	ret = ksz_pwrite32(dev, port, REG_PORT_MTI_QUEUE_INDEX__4, queue);
	if (ret)
		return ret;

	return ksz_setup_tc_mode(dev, port, MTI_SCHEDULE_STRICT_PRIO,
				 MTI_SHAPING_OFF);
}

static int ksz_queue_set_wrr(struct ksz_device *dev, int port, int queue,
			     int weight)
{
	int ret;

	ret = ksz_pwrite32(dev, port, REG_PORT_MTI_QUEUE_INDEX__4, queue);
	if (ret)
		return ret;

	ret = ksz_setup_tc_mode(dev, port, MTI_SCHEDULE_WRR,
				MTI_SHAPING_OFF);
	if (ret)
		return ret;

	return ksz_pwrite8(dev, port, KSZ9477_PORT_MTI_QUEUE_CTRL_1, weight);
}

static int ksz_tc_ets_add(struct ksz_device *dev, int port,
			  struct tc_ets_qopt_offload_replace_params *p)
{
	int ret, band, tc_prio;
	u32 queue_map = 0;

	/* In order to ensure proper prioritization, it is necessary to set the
	 * rate limit for the related queue to zero. Otherwise strict priority
	 * or WRR mode will not work. This is a hardware limitation.
	 */
	ret = ksz_disable_egress_rate_limit(dev, port);
	if (ret)
		return ret;

	/* Configure queue scheduling mode for all bands. Currently only strict
	 * prio mode is supported.
	 */
	for (band = 0; band < p->bands; band++) {
		int queue = ksz_ets_band_to_queue(p, band);

		ret = ksz_queue_set_strict(dev, port, queue);
		if (ret)
			return ret;
	}

	/* Configure the mapping between traffic classes and queues. Note:
	 * priomap variable support 16 traffic classes, but the chip can handle
	 * only 8 classes.
	 */
	for (tc_prio = 0; tc_prio < ARRAY_SIZE(p->priomap); tc_prio++) {
		int queue;

		if (tc_prio > KSZ9477_MAX_TC_PRIO)
			break;

		queue = ksz_ets_band_to_queue(p, p->priomap[tc_prio]);
		queue_map |= queue << (tc_prio * KSZ9477_PORT_TC_MAP_S);
	}

	return ksz_pwrite32(dev, port, KSZ9477_PORT_MRI_TC_MAP__4, queue_map);
}

static int ksz_tc_ets_del(struct ksz_device *dev, int port)
{
	int ret, queue, tc_prio, s;
	u32 queue_map = 0;

	/* To restore the default chip configuration, set all queues to use the
	 * WRR scheduler with a weight of 1.
	 */
	for (queue = 0; queue < dev->info->num_tx_queues; queue++) {
		ret = ksz_queue_set_wrr(dev, port, queue,
					KSZ9477_DEFAULT_WRR_WEIGHT);
		if (ret)
			return ret;
	}

	switch (dev->info->num_tx_queues) {
	case 2:
		s = 2;
		break;
	case 4:
		s = 1;
		break;
	case 8:
		s = 0;
		break;
	default:
		return -EINVAL;
	}

	/* Revert the queue mapping for TC-priority to its default setting on
	 * the chip.
	 */
	for (tc_prio = 0; tc_prio <= KSZ9477_MAX_TC_PRIO; tc_prio++) {
		int queue;

		queue = tc_prio >> s;
		queue_map |= queue << (tc_prio * KSZ9477_PORT_TC_MAP_S);
	}

	return ksz_pwrite32(dev, port, KSZ9477_PORT_MRI_TC_MAP__4, queue_map);
}

static int ksz_tc_ets_validate(struct ksz_device *dev, int port,
			       struct tc_ets_qopt_offload_replace_params *p)
{
	int band;

	/* Since it is not feasible to share one port among multiple qdisc,
	 * the user must configure all available queues appropriately.
	 */
	if (p->bands != dev->info->num_tx_queues) {
		dev_err(dev->dev, "Not supported amount of bands. It should be %d\n",
			dev->info->num_tx_queues);
		return -EOPNOTSUPP;
	}

	for (band = 0; band < p->bands; ++band) {
		/* The KSZ switches utilize a weighted round robin configuration
		 * where a certain number of packets can be transmitted from a
		 * queue before the next queue is serviced. For more information
		 * on this, refer to section 5.2.8.4 of the KSZ8565R
		 * documentation on the Port Transmit Queue Control 1 Register.
		 * However, the current ETS Qdisc implementation (as of February
		 * 2023) assigns a weight to each queue based on the number of
		 * bytes or extrapolated bandwidth in percentages. Since this
		 * differs from the KSZ switches' method and we don't want to
		 * fake support by converting bytes to packets, it is better to
		 * return an error instead.
		 */
		if (p->quanta[band]) {
			dev_err(dev->dev, "Quanta/weights configuration is not supported.\n");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int ksz_tc_setup_qdisc_ets(struct dsa_switch *ds, int port,
				  struct tc_ets_qopt_offload *qopt)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	if (!dev->info->tc_ets_supported)
		return -EOPNOTSUPP;

	if (qopt->parent != TC_H_ROOT) {
		dev_err(dev->dev, "Parent should be \"root\"\n");
		return -EOPNOTSUPP;
	}

	switch (qopt->command) {
	case TC_ETS_REPLACE:
		ret = ksz_tc_ets_validate(dev, port, &qopt->replace_params);
		if (ret)
			return ret;

		return ksz_tc_ets_add(dev, port, &qopt->replace_params);
	case TC_ETS_DESTROY:
		return ksz_tc_ets_del(dev, port);
	case TC_ETS_STATS:
	case TC_ETS_GRAFT:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int ksz_setup_tc(struct dsa_switch *ds, int port,
			enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_CBS:
		return ksz_setup_tc_cbs(ds, port, type_data);
	case TC_SETUP_QDISC_ETS:
		return ksz_tc_setup_qdisc_ets(ds, port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int ksz_port_set_mac_address(struct dsa_switch *ds, int port,
				    const unsigned char *addr)
{
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (dp->hsr_dev) {
		dev_err(ds->dev,
			"Cannot change MAC address on port %d with active HSR offload\n",
			port);
		return -EBUSY;
	}

	return 0;
}

/* Program the switch's MAC address register with the MAC address of the
 * requesting user port. This single address is used by the switch for multiple
 * features, like HSR self-address filtering and WoL. Other user ports are
 * allowed to share ownership of this address as long as their MAC address is
 * the same. The user ports' MAC addresses must not change while they have
 * ownership of the switch MAC address.
 */
static int ksz_switch_macaddr_get(struct dsa_switch *ds, int port,
				  struct netlink_ext_ack *extack)
{
	struct net_device *slave = dsa_to_port(ds, port)->slave;
	const unsigned char *addr = slave->dev_addr;
	struct ksz_switch_macaddr *switch_macaddr;
	struct ksz_device *dev = ds->priv;
	const u16 *regs = dev->info->regs;
	int i;

	/* Make sure concurrent MAC address changes are blocked */
	ASSERT_RTNL();

	switch_macaddr = dev->switch_macaddr;
	if (switch_macaddr) {
		if (!ether_addr_equal(switch_macaddr->addr, addr)) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Switch already configured for MAC address %pM",
					       switch_macaddr->addr);
			return -EBUSY;
		}

		refcount_inc(&switch_macaddr->refcount);
		return 0;
	}

	switch_macaddr = kzalloc(sizeof(*switch_macaddr), GFP_KERNEL);
	if (!switch_macaddr)
		return -ENOMEM;

	ether_addr_copy(switch_macaddr->addr, addr);
	refcount_set(&switch_macaddr->refcount, 1);
	dev->switch_macaddr = switch_macaddr;

	/* Program the switch MAC address to hardware */
	for (i = 0; i < ETH_ALEN; i++)
		ksz_write8(dev, regs[REG_SW_MAC_ADDR] + i, addr[i]);

	return 0;
}

static void ksz_switch_macaddr_put(struct dsa_switch *ds)
{
	struct ksz_switch_macaddr *switch_macaddr;
	struct ksz_device *dev = ds->priv;
	const u16 *regs = dev->info->regs;
	int i;

	/* Make sure concurrent MAC address changes are blocked */
	ASSERT_RTNL();

	switch_macaddr = dev->switch_macaddr;
	if (!refcount_dec_and_test(&switch_macaddr->refcount))
		return;

	for (i = 0; i < ETH_ALEN; i++)
		ksz_write8(dev, regs[REG_SW_MAC_ADDR] + i, 0);

	dev->switch_macaddr = NULL;
	kfree(switch_macaddr);
}

static int ksz_hsr_join(struct dsa_switch *ds, int port, struct net_device *hsr,
			struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;
	enum hsr_version ver;
	int ret;

	ret = hsr_get_version(hsr, &ver);
	if (ret)
		return ret;

	if (dev->chip_id != KSZ9477_CHIP_ID) {
		NL_SET_ERR_MSG_MOD(extack, "Chip does not support HSR offload");
		return -EOPNOTSUPP;
	}

	/* KSZ9477 can support HW offloading of only 1 HSR device */
	if (dev->hsr_dev && hsr != dev->hsr_dev) {
		NL_SET_ERR_MSG_MOD(extack, "Offload supported for a single HSR");
		return -EOPNOTSUPP;
	}

	/* KSZ9477 only supports HSR v0 and v1 */
	if (!(ver == HSR_V0 || ver == HSR_V1)) {
		NL_SET_ERR_MSG_MOD(extack, "Only HSR v0 and v1 supported");
		return -EOPNOTSUPP;
	}

	/* Self MAC address filtering, to avoid frames traversing
	 * the HSR ring more than once.
	 */
	ret = ksz_switch_macaddr_get(ds, port, extack);
	if (ret)
		return ret;

	ksz9477_hsr_join(ds, port, hsr);
	dev->hsr_dev = hsr;
	dev->hsr_ports |= BIT(port);

	return 0;
}

static int ksz_hsr_leave(struct dsa_switch *ds, int port,
			 struct net_device *hsr)
{
	struct ksz_device *dev = ds->priv;

	WARN_ON(dev->chip_id != KSZ9477_CHIP_ID);

	ksz9477_hsr_leave(ds, port, hsr);
	dev->hsr_ports &= ~BIT(port);
	if (!dev->hsr_ports)
		dev->hsr_dev = NULL;

	ksz_switch_macaddr_put(ds);

	return 0;
}

static const struct dsa_switch_ops ksz_switch_ops = {
	.get_tag_protocol	= ksz_get_tag_protocol,
	.connect_tag_protocol   = ksz_connect_tag_protocol,
	.get_phy_flags		= ksz_get_phy_flags,
	.setup			= ksz_setup,
	.teardown		= ksz_teardown,
	.phy_read		= ksz_phy_read16,
	.phy_write		= ksz_phy_write16,
	.phylink_get_caps	= ksz_phylink_get_caps,
	.phylink_mac_config	= ksz_phylink_mac_config,
	.phylink_mac_link_up	= ksz_phylink_mac_link_up,
	.phylink_mac_link_down	= ksz_mac_link_down,
	.port_setup		= ksz_port_setup,
	.set_ageing_time	= ksz_set_ageing_time,
	.get_strings		= ksz_get_strings,
	.get_ethtool_stats	= ksz_get_ethtool_stats,
	.get_sset_count		= ksz_sset_count,
	.port_bridge_join	= ksz_port_bridge_join,
	.port_bridge_leave	= ksz_port_bridge_leave,
	.port_hsr_join		= ksz_hsr_join,
	.port_hsr_leave		= ksz_hsr_leave,
	.port_set_mac_address	= ksz_port_set_mac_address,
	.port_stp_state_set	= ksz_port_stp_state_set,
	.port_teardown		= ksz_port_teardown,
	.port_pre_bridge_flags	= ksz_port_pre_bridge_flags,
	.port_bridge_flags	= ksz_port_bridge_flags,
	.port_fast_age		= ksz_port_fast_age,
	.port_vlan_filtering	= ksz_port_vlan_filtering,
	.port_vlan_add		= ksz_port_vlan_add,
	.port_vlan_del		= ksz_port_vlan_del,
	.port_fdb_dump		= ksz_port_fdb_dump,
	.port_fdb_add		= ksz_port_fdb_add,
	.port_fdb_del		= ksz_port_fdb_del,
	.port_mdb_add           = ksz_port_mdb_add,
	.port_mdb_del           = ksz_port_mdb_del,
	.port_mirror_add	= ksz_port_mirror_add,
	.port_mirror_del	= ksz_port_mirror_del,
	.get_stats64		= ksz_get_stats64,
	.get_pause_stats	= ksz_get_pause_stats,
	.port_change_mtu	= ksz_change_mtu,
	.port_max_mtu		= ksz_max_mtu,
	.get_ts_info		= ksz_get_ts_info,
	.port_hwtstamp_get	= ksz_hwtstamp_get,
	.port_hwtstamp_set	= ksz_hwtstamp_set,
	.port_txtstamp		= ksz_port_txtstamp,
	.port_rxtstamp		= ksz_port_rxtstamp,
	.cls_flower_add		= ksz_cls_flower_add,
	.cls_flower_del		= ksz_cls_flower_del,
	.port_setup_tc		= ksz_setup_tc,
	.get_mac_eee		= ksz_get_mac_eee,
	.set_mac_eee		= ksz_set_mac_eee,
};

struct ksz_device *ksz_switch_alloc(struct device *base, void *priv)
{
	struct dsa_switch *ds;
	struct ksz_device *swdev;

	ds = devm_kzalloc(base, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = base;
	ds->num_ports = DSA_MAX_PORTS;
	ds->ops = &ksz_switch_ops;

	swdev = devm_kzalloc(base, sizeof(*swdev), GFP_KERNEL);
	if (!swdev)
		return NULL;

	ds->priv = swdev;
	swdev->dev = base;

	swdev->ds = ds;
	swdev->priv = priv;

	return swdev;
}
EXPORT_SYMBOL(ksz_switch_alloc);

static void ksz_parse_rgmii_delay(struct ksz_device *dev, int port_num,
				  struct device_node *port_dn)
{
	phy_interface_t phy_mode = dev->ports[port_num].interface;
	int rx_delay = -1, tx_delay = -1;

	if (!phy_interface_mode_is_rgmii(phy_mode))
		return;

	of_property_read_u32(port_dn, "rx-internal-delay-ps", &rx_delay);
	of_property_read_u32(port_dn, "tx-internal-delay-ps", &tx_delay);

	if (rx_delay == -1 && tx_delay == -1) {
		dev_warn(dev->dev,
			 "Port %d interpreting RGMII delay settings based on \"phy-mode\" property, "
			 "please update device tree to specify \"rx-internal-delay-ps\" and "
			 "\"tx-internal-delay-ps\"",
			 port_num);

		if (phy_mode == PHY_INTERFACE_MODE_RGMII_RXID ||
		    phy_mode == PHY_INTERFACE_MODE_RGMII_ID)
			rx_delay = 2000;

		if (phy_mode == PHY_INTERFACE_MODE_RGMII_TXID ||
		    phy_mode == PHY_INTERFACE_MODE_RGMII_ID)
			tx_delay = 2000;
	}

	if (rx_delay < 0)
		rx_delay = 0;
	if (tx_delay < 0)
		tx_delay = 0;

	dev->ports[port_num].rgmii_rx_val = rx_delay;
	dev->ports[port_num].rgmii_tx_val = tx_delay;
}

/**
 * ksz_drive_strength_to_reg() - Convert drive strength value to corresponding
 *				 register value.
 * @array:	The array of drive strength values to search.
 * @array_size:	The size of the array.
 * @microamp:	The drive strength value in microamp to be converted.
 *
 * This function searches the array of drive strength values for the given
 * microamp value and returns the corresponding register value for that drive.
 *
 * Returns: If found, the corresponding register value for that drive strength
 * is returned. Otherwise, -EINVAL is returned indicating an invalid value.
 */
static int ksz_drive_strength_to_reg(const struct ksz_drive_strength *array,
				     size_t array_size, int microamp)
{
	int i;

	for (i = 0; i < array_size; i++) {
		if (array[i].microamp == microamp)
			return array[i].reg_val;
	}

	return -EINVAL;
}

/**
 * ksz_drive_strength_error() - Report invalid drive strength value
 * @dev:	ksz device
 * @array:	The array of drive strength values to search.
 * @array_size:	The size of the array.
 * @microamp:	Invalid drive strength value in microamp
 *
 * This function logs an error message when an unsupported drive strength value
 * is detected. It lists out all the supported drive strength values for
 * reference in the error message.
 */
static void ksz_drive_strength_error(struct ksz_device *dev,
				     const struct ksz_drive_strength *array,
				     size_t array_size, int microamp)
{
	char supported_values[100];
	size_t remaining_size;
	int added_len;
	char *ptr;
	int i;

	remaining_size = sizeof(supported_values);
	ptr = supported_values;

	for (i = 0; i < array_size; i++) {
		added_len = snprintf(ptr, remaining_size,
				     i == 0 ? "%d" : ", %d", array[i].microamp);

		if (added_len >= remaining_size)
			break;

		ptr += added_len;
		remaining_size -= added_len;
	}

	dev_err(dev->dev, "Invalid drive strength %d, supported values are %s\n",
		microamp, supported_values);
}

/**
 * ksz9477_drive_strength_write() - Set the drive strength for specific KSZ9477
 *				    chip variants.
 * @dev:       ksz device
 * @props:     Array of drive strength properties to be applied
 * @num_props: Number of properties in the array
 *
 * This function configures the drive strength for various KSZ9477 chip variants
 * based on the provided properties. It handles chip-specific nuances and
 * ensures only valid drive strengths are written to the respective chip.
 *
 * Return: 0 on successful configuration, a negative error code on failure.
 */
static int ksz9477_drive_strength_write(struct ksz_device *dev,
					struct ksz_driver_strength_prop *props,
					int num_props)
{
	size_t array_size = ARRAY_SIZE(ksz9477_drive_strengths);
	int i, ret, reg;
	u8 mask = 0;
	u8 val = 0;

	if (props[KSZ_DRIVER_STRENGTH_IO].value != -1)
		dev_warn(dev->dev, "%s is not supported by this chip variant\n",
			 props[KSZ_DRIVER_STRENGTH_IO].name);

	if (dev->chip_id == KSZ8795_CHIP_ID ||
	    dev->chip_id == KSZ8794_CHIP_ID ||
	    dev->chip_id == KSZ8765_CHIP_ID)
		reg = KSZ8795_REG_SW_CTRL_20;
	else
		reg = KSZ9477_REG_SW_IO_STRENGTH;

	for (i = 0; i < num_props; i++) {
		if (props[i].value == -1)
			continue;

		ret = ksz_drive_strength_to_reg(ksz9477_drive_strengths,
						array_size, props[i].value);
		if (ret < 0) {
			ksz_drive_strength_error(dev, ksz9477_drive_strengths,
						 array_size, props[i].value);
			return ret;
		}

		mask |= SW_DRIVE_STRENGTH_M << props[i].offset;
		val |= ret << props[i].offset;
	}

	return ksz_rmw8(dev, reg, mask, val);
}

/**
 * ksz8830_drive_strength_write() - Set the drive strength configuration for
 *				    KSZ8830 compatible chip variants.
 * @dev:       ksz device
 * @props:     Array of drive strength properties to be set
 * @num_props: Number of properties in the array
 *
 * This function applies the specified drive strength settings to KSZ8830 chip
 * variants (KSZ8873, KSZ8863).
 * It ensures the configurations align with what the chip variant supports and
 * warns or errors out on unsupported settings.
 *
 * Return: 0 on success, error code otherwise
 */
static int ksz8830_drive_strength_write(struct ksz_device *dev,
					struct ksz_driver_strength_prop *props,
					int num_props)
{
	size_t array_size = ARRAY_SIZE(ksz8830_drive_strengths);
	int microamp;
	int i, ret;

	for (i = 0; i < num_props; i++) {
		if (props[i].value == -1 || i == KSZ_DRIVER_STRENGTH_IO)
			continue;

		dev_warn(dev->dev, "%s is not supported by this chip variant\n",
			 props[i].name);
	}

	microamp = props[KSZ_DRIVER_STRENGTH_IO].value;
	ret = ksz_drive_strength_to_reg(ksz8830_drive_strengths, array_size,
					microamp);
	if (ret < 0) {
		ksz_drive_strength_error(dev, ksz8830_drive_strengths,
					 array_size, microamp);
		return ret;
	}

	return ksz_rmw8(dev, KSZ8873_REG_GLOBAL_CTRL_12,
			KSZ8873_DRIVE_STRENGTH_16MA, ret);
}

/**
 * ksz_parse_drive_strength() - Extract and apply drive strength configurations
 *				from device tree properties.
 * @dev:	ksz device
 *
 * This function reads the specified drive strength properties from the
 * device tree, validates against the supported chip variants, and sets
 * them accordingly. An error should be critical here, as the drive strength
 * settings are crucial for EMI compliance.
 *
 * Return: 0 on success, error code otherwise
 */
static int ksz_parse_drive_strength(struct ksz_device *dev)
{
	struct ksz_driver_strength_prop of_props[] = {
		[KSZ_DRIVER_STRENGTH_HI] = {
			.name = "microchip,hi-drive-strength-microamp",
			.offset = SW_HI_SPEED_DRIVE_STRENGTH_S,
			.value = -1,
		},
		[KSZ_DRIVER_STRENGTH_LO] = {
			.name = "microchip,lo-drive-strength-microamp",
			.offset = SW_LO_SPEED_DRIVE_STRENGTH_S,
			.value = -1,
		},
		[KSZ_DRIVER_STRENGTH_IO] = {
			.name = "microchip,io-drive-strength-microamp",
			.offset = 0, /* don't care */
			.value = -1,
		},
	};
	struct device_node *np = dev->dev->of_node;
	bool have_any_prop = false;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(of_props); i++) {
		ret = of_property_read_u32(np, of_props[i].name,
					   &of_props[i].value);
		if (ret && ret != -EINVAL)
			dev_warn(dev->dev, "Failed to read %s\n",
				 of_props[i].name);
		if (ret)
			continue;

		have_any_prop = true;
	}

	if (!have_any_prop)
		return 0;

	switch (dev->chip_id) {
	case KSZ8830_CHIP_ID:
		return ksz8830_drive_strength_write(dev, of_props,
						    ARRAY_SIZE(of_props));
	case KSZ8795_CHIP_ID:
	case KSZ8794_CHIP_ID:
	case KSZ8765_CHIP_ID:
	case KSZ8563_CHIP_ID:
	case KSZ9477_CHIP_ID:
	case KSZ9563_CHIP_ID:
	case KSZ9567_CHIP_ID:
	case KSZ9893_CHIP_ID:
	case KSZ9896_CHIP_ID:
	case KSZ9897_CHIP_ID:
		return ksz9477_drive_strength_write(dev, of_props,
						    ARRAY_SIZE(of_props));
	default:
		for (i = 0; i < ARRAY_SIZE(of_props); i++) {
			if (of_props[i].value == -1)
				continue;

			dev_warn(dev->dev, "%s is not supported by this chip variant\n",
				 of_props[i].name);
		}
	}

	return 0;
}

int ksz_switch_register(struct ksz_device *dev)
{
	const struct ksz_chip_data *info;
	struct device_node *port, *ports;
	phy_interface_t interface;
	unsigned int port_num;
	int ret;
	int i;

	if (dev->pdata)
		dev->chip_id = dev->pdata->chip_id;

	dev->reset_gpio = devm_gpiod_get_optional(dev->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(dev->reset_gpio))
		return PTR_ERR(dev->reset_gpio);

	if (dev->reset_gpio) {
		gpiod_set_value_cansleep(dev->reset_gpio, 1);
		usleep_range(10000, 12000);
		gpiod_set_value_cansleep(dev->reset_gpio, 0);
		msleep(100);
	}

	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->regmap_mutex);
	mutex_init(&dev->alu_mutex);
	mutex_init(&dev->vlan_mutex);

	ret = ksz_switch_detect(dev);
	if (ret)
		return ret;

	info = ksz_lookup_info(dev->chip_id);
	if (!info)
		return -ENODEV;

	/* Update the compatible info with the probed one */
	dev->info = info;

	dev_info(dev->dev, "found switch: %s, rev %i\n",
		 dev->info->dev_name, dev->chip_rev);

	ret = ksz_check_device_id(dev);
	if (ret)
		return ret;

	dev->dev_ops = dev->info->ops;

	ret = dev->dev_ops->init(dev);
	if (ret)
		return ret;

	dev->ports = devm_kzalloc(dev->dev,
				  dev->info->port_cnt * sizeof(struct ksz_port),
				  GFP_KERNEL);
	if (!dev->ports)
		return -ENOMEM;

	for (i = 0; i < dev->info->port_cnt; i++) {
		spin_lock_init(&dev->ports[i].mib.stats64_lock);
		mutex_init(&dev->ports[i].mib.cnt_mutex);
		dev->ports[i].mib.counters =
			devm_kzalloc(dev->dev,
				     sizeof(u64) * (dev->info->mib_cnt + 1),
				     GFP_KERNEL);
		if (!dev->ports[i].mib.counters)
			return -ENOMEM;

		dev->ports[i].ksz_dev = dev;
		dev->ports[i].num = i;
	}

	/* set the real number of ports */
	dev->ds->num_ports = dev->info->port_cnt;

	/* Host port interface will be self detected, or specifically set in
	 * device tree.
	 */
	for (port_num = 0; port_num < dev->info->port_cnt; ++port_num)
		dev->ports[port_num].interface = PHY_INTERFACE_MODE_NA;
	if (dev->dev->of_node) {
		ret = ksz_parse_drive_strength(dev);
		if (ret)
			return ret;

		ret = of_get_phy_mode(dev->dev->of_node, &interface);
		if (ret == 0)
			dev->compat_interface = interface;
		ports = of_get_child_by_name(dev->dev->of_node, "ethernet-ports");
		if (!ports)
			ports = of_get_child_by_name(dev->dev->of_node, "ports");
		if (ports) {
			for_each_available_child_of_node(ports, port) {
				if (of_property_read_u32(port, "reg",
							 &port_num))
					continue;
				if (!(dev->port_mask & BIT(port_num))) {
					of_node_put(port);
					of_node_put(ports);
					return -EINVAL;
				}
				of_get_phy_mode(port,
						&dev->ports[port_num].interface);

				ksz_parse_rgmii_delay(dev, port_num, port);
			}
			of_node_put(ports);
		}
		dev->synclko_125 = of_property_read_bool(dev->dev->of_node,
							 "microchip,synclko-125");
		dev->synclko_disable = of_property_read_bool(dev->dev->of_node,
							     "microchip,synclko-disable");
		if (dev->synclko_125 && dev->synclko_disable) {
			dev_err(dev->dev, "inconsistent synclko settings\n");
			return -EINVAL;
		}
	}

	ret = dsa_register_switch(dev->ds);
	if (ret) {
		dev->dev_ops->exit(dev);
		return ret;
	}

	/* Read MIB counters every 30 seconds to avoid overflow. */
	dev->mib_read_interval = msecs_to_jiffies(5000);

	/* Start the MIB timer. */
	schedule_delayed_work(&dev->mib_read, 0);

	return ret;
}
EXPORT_SYMBOL(ksz_switch_register);

void ksz_switch_remove(struct ksz_device *dev)
{
	/* timer started */
	if (dev->mib_read_interval) {
		dev->mib_read_interval = 0;
		cancel_delayed_work_sync(&dev->mib_read);
	}

	dev->dev_ops->exit(dev);
	dsa_unregister_switch(dev->ds);

	if (dev->reset_gpio)
		gpiod_set_value_cansleep(dev->reset_gpio, 1);

}
EXPORT_SYMBOL(ksz_switch_remove);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ Series Switch DSA Driver");
MODULE_LICENSE("GPL");

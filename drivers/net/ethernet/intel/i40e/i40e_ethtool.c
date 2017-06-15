/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

/* ethtool support for i40e */

#include "i40e.h"
#include "i40e_diag.h"

struct i40e_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define I40E_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

#define I40E_NETDEV_STAT(_net_stat) \
		I40E_STAT(struct rtnl_link_stats64, #_net_stat, _net_stat)
#define I40E_PF_STAT(_name, _stat) \
		I40E_STAT(struct i40e_pf, _name, _stat)
#define I40E_VSI_STAT(_name, _stat) \
		I40E_STAT(struct i40e_vsi, _name, _stat)
#define I40E_VEB_STAT(_name, _stat) \
		I40E_STAT(struct i40e_veb, _name, _stat)

static const struct i40e_stats i40e_gstrings_net_stats[] = {
	I40E_NETDEV_STAT(rx_packets),
	I40E_NETDEV_STAT(tx_packets),
	I40E_NETDEV_STAT(rx_bytes),
	I40E_NETDEV_STAT(tx_bytes),
	I40E_NETDEV_STAT(rx_errors),
	I40E_NETDEV_STAT(tx_errors),
	I40E_NETDEV_STAT(rx_dropped),
	I40E_NETDEV_STAT(tx_dropped),
	I40E_NETDEV_STAT(collisions),
	I40E_NETDEV_STAT(rx_length_errors),
	I40E_NETDEV_STAT(rx_crc_errors),
};

static const struct i40e_stats i40e_gstrings_veb_stats[] = {
	I40E_VEB_STAT("rx_bytes", stats.rx_bytes),
	I40E_VEB_STAT("tx_bytes", stats.tx_bytes),
	I40E_VEB_STAT("rx_unicast", stats.rx_unicast),
	I40E_VEB_STAT("tx_unicast", stats.tx_unicast),
	I40E_VEB_STAT("rx_multicast", stats.rx_multicast),
	I40E_VEB_STAT("tx_multicast", stats.tx_multicast),
	I40E_VEB_STAT("rx_broadcast", stats.rx_broadcast),
	I40E_VEB_STAT("tx_broadcast", stats.tx_broadcast),
	I40E_VEB_STAT("rx_discards", stats.rx_discards),
	I40E_VEB_STAT("tx_discards", stats.tx_discards),
	I40E_VEB_STAT("tx_errors", stats.tx_errors),
	I40E_VEB_STAT("rx_unknown_protocol", stats.rx_unknown_protocol),
};

static const struct i40e_stats i40e_gstrings_misc_stats[] = {
	I40E_VSI_STAT("rx_unicast", eth_stats.rx_unicast),
	I40E_VSI_STAT("tx_unicast", eth_stats.tx_unicast),
	I40E_VSI_STAT("rx_multicast", eth_stats.rx_multicast),
	I40E_VSI_STAT("tx_multicast", eth_stats.tx_multicast),
	I40E_VSI_STAT("rx_broadcast", eth_stats.rx_broadcast),
	I40E_VSI_STAT("tx_broadcast", eth_stats.tx_broadcast),
	I40E_VSI_STAT("rx_unknown_protocol", eth_stats.rx_unknown_protocol),
	I40E_VSI_STAT("tx_linearize", tx_linearize),
	I40E_VSI_STAT("tx_force_wb", tx_force_wb),
	I40E_VSI_STAT("rx_alloc_fail", rx_buf_failed),
	I40E_VSI_STAT("rx_pg_alloc_fail", rx_page_failed),
};

/* These PF_STATs might look like duplicates of some NETDEV_STATs,
 * but they are separate.  This device supports Virtualization, and
 * as such might have several netdevs supporting VMDq and FCoE going
 * through a single port.  The NETDEV_STATs are for individual netdevs
 * seen at the top of the stack, and the PF_STATs are for the physical
 * function at the bottom of the stack hosting those netdevs.
 *
 * The PF_STATs are appended to the netdev stats only when ethtool -S
 * is queried on the base PF netdev, not on the VMDq or FCoE netdev.
 */
static const struct i40e_stats i40e_gstrings_stats[] = {
	I40E_PF_STAT("rx_bytes", stats.eth.rx_bytes),
	I40E_PF_STAT("tx_bytes", stats.eth.tx_bytes),
	I40E_PF_STAT("rx_unicast", stats.eth.rx_unicast),
	I40E_PF_STAT("tx_unicast", stats.eth.tx_unicast),
	I40E_PF_STAT("rx_multicast", stats.eth.rx_multicast),
	I40E_PF_STAT("tx_multicast", stats.eth.tx_multicast),
	I40E_PF_STAT("rx_broadcast", stats.eth.rx_broadcast),
	I40E_PF_STAT("tx_broadcast", stats.eth.tx_broadcast),
	I40E_PF_STAT("tx_errors", stats.eth.tx_errors),
	I40E_PF_STAT("rx_dropped", stats.eth.rx_discards),
	I40E_PF_STAT("tx_dropped_link_down", stats.tx_dropped_link_down),
	I40E_PF_STAT("rx_crc_errors", stats.crc_errors),
	I40E_PF_STAT("illegal_bytes", stats.illegal_bytes),
	I40E_PF_STAT("mac_local_faults", stats.mac_local_faults),
	I40E_PF_STAT("mac_remote_faults", stats.mac_remote_faults),
	I40E_PF_STAT("tx_timeout", tx_timeout_count),
	I40E_PF_STAT("rx_csum_bad", hw_csum_rx_error),
	I40E_PF_STAT("rx_length_errors", stats.rx_length_errors),
	I40E_PF_STAT("link_xon_rx", stats.link_xon_rx),
	I40E_PF_STAT("link_xoff_rx", stats.link_xoff_rx),
	I40E_PF_STAT("link_xon_tx", stats.link_xon_tx),
	I40E_PF_STAT("link_xoff_tx", stats.link_xoff_tx),
	I40E_PF_STAT("rx_size_64", stats.rx_size_64),
	I40E_PF_STAT("rx_size_127", stats.rx_size_127),
	I40E_PF_STAT("rx_size_255", stats.rx_size_255),
	I40E_PF_STAT("rx_size_511", stats.rx_size_511),
	I40E_PF_STAT("rx_size_1023", stats.rx_size_1023),
	I40E_PF_STAT("rx_size_1522", stats.rx_size_1522),
	I40E_PF_STAT("rx_size_big", stats.rx_size_big),
	I40E_PF_STAT("tx_size_64", stats.tx_size_64),
	I40E_PF_STAT("tx_size_127", stats.tx_size_127),
	I40E_PF_STAT("tx_size_255", stats.tx_size_255),
	I40E_PF_STAT("tx_size_511", stats.tx_size_511),
	I40E_PF_STAT("tx_size_1023", stats.tx_size_1023),
	I40E_PF_STAT("tx_size_1522", stats.tx_size_1522),
	I40E_PF_STAT("tx_size_big", stats.tx_size_big),
	I40E_PF_STAT("rx_undersize", stats.rx_undersize),
	I40E_PF_STAT("rx_fragments", stats.rx_fragments),
	I40E_PF_STAT("rx_oversize", stats.rx_oversize),
	I40E_PF_STAT("rx_jabber", stats.rx_jabber),
	I40E_PF_STAT("VF_admin_queue_requests", vf_aq_requests),
	I40E_PF_STAT("arq_overflows", arq_overflows),
	I40E_PF_STAT("rx_hwtstamp_cleared", rx_hwtstamp_cleared),
	I40E_PF_STAT("tx_hwtstamp_skipped", tx_hwtstamp_skipped),
	I40E_PF_STAT("fdir_flush_cnt", fd_flush_cnt),
	I40E_PF_STAT("fdir_atr_match", stats.fd_atr_match),
	I40E_PF_STAT("fdir_atr_tunnel_match", stats.fd_atr_tunnel_match),
	I40E_PF_STAT("fdir_atr_status", stats.fd_atr_status),
	I40E_PF_STAT("fdir_sb_match", stats.fd_sb_match),
	I40E_PF_STAT("fdir_sb_status", stats.fd_sb_status),

	/* LPI stats */
	I40E_PF_STAT("tx_lpi_status", stats.tx_lpi_status),
	I40E_PF_STAT("rx_lpi_status", stats.rx_lpi_status),
	I40E_PF_STAT("tx_lpi_count", stats.tx_lpi_count),
	I40E_PF_STAT("rx_lpi_count", stats.rx_lpi_count),
};

#define I40E_QUEUE_STATS_LEN(n) \
	(((struct i40e_netdev_priv *)netdev_priv((n)))->vsi->num_queue_pairs \
	    * 2 /* Tx and Rx together */                                     \
	    * (sizeof(struct i40e_queue_stats) / sizeof(u64)))
#define I40E_GLOBAL_STATS_LEN	ARRAY_SIZE(i40e_gstrings_stats)
#define I40E_NETDEV_STATS_LEN   ARRAY_SIZE(i40e_gstrings_net_stats)
#define I40E_MISC_STATS_LEN	ARRAY_SIZE(i40e_gstrings_misc_stats)
#define I40E_VSI_STATS_LEN(n)   (I40E_NETDEV_STATS_LEN + \
				 I40E_MISC_STATS_LEN + \
				 I40E_QUEUE_STATS_LEN((n)))
#define I40E_PFC_STATS_LEN ( \
		(FIELD_SIZEOF(struct i40e_pf, stats.priority_xoff_rx) + \
		 FIELD_SIZEOF(struct i40e_pf, stats.priority_xon_rx) + \
		 FIELD_SIZEOF(struct i40e_pf, stats.priority_xoff_tx) + \
		 FIELD_SIZEOF(struct i40e_pf, stats.priority_xon_tx) + \
		 FIELD_SIZEOF(struct i40e_pf, stats.priority_xon_2_xoff)) \
		 / sizeof(u64))
#define I40E_VEB_TC_STATS_LEN ( \
		(FIELD_SIZEOF(struct i40e_veb, tc_stats.tc_rx_packets) + \
		 FIELD_SIZEOF(struct i40e_veb, tc_stats.tc_rx_bytes) + \
		 FIELD_SIZEOF(struct i40e_veb, tc_stats.tc_tx_packets) + \
		 FIELD_SIZEOF(struct i40e_veb, tc_stats.tc_tx_bytes)) \
		 / sizeof(u64))
#define I40E_VEB_STATS_LEN	ARRAY_SIZE(i40e_gstrings_veb_stats)
#define I40E_VEB_STATS_TOTAL	(I40E_VEB_STATS_LEN + I40E_VEB_TC_STATS_LEN)
#define I40E_PF_STATS_LEN(n)	(I40E_GLOBAL_STATS_LEN + \
				 I40E_PFC_STATS_LEN + \
				 I40E_VSI_STATS_LEN((n)))

enum i40e_ethtool_test_id {
	I40E_ETH_TEST_REG = 0,
	I40E_ETH_TEST_EEPROM,
	I40E_ETH_TEST_INTR,
	I40E_ETH_TEST_LINK,
};

static const char i40e_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"Eeprom test    (offline)",
	"Interrupt test (offline)",
	"Link test   (on/offline)"
};

#define I40E_TEST_LEN (sizeof(i40e_gstrings_test) / ETH_GSTRING_LEN)

struct i40e_priv_flags {
	char flag_string[ETH_GSTRING_LEN];
	u64 flag;
	bool read_only;
};

#define I40E_PRIV_FLAG(_name, _flag, _read_only) { \
	.flag_string = _name, \
	.flag = _flag, \
	.read_only = _read_only, \
}

static const struct i40e_priv_flags i40e_gstrings_priv_flags[] = {
	/* NOTE: MFP setting cannot be changed */
	I40E_PRIV_FLAG("MFP", I40E_FLAG_MFP_ENABLED, 1),
	I40E_PRIV_FLAG("LinkPolling", I40E_FLAG_LINK_POLLING_ENABLED, 0),
	I40E_PRIV_FLAG("flow-director-atr", I40E_FLAG_FD_ATR_ENABLED, 0),
	I40E_PRIV_FLAG("veb-stats", I40E_FLAG_VEB_STATS_ENABLED, 0),
	I40E_PRIV_FLAG("hw-atr-eviction", I40E_FLAG_HW_ATR_EVICT_ENABLED, 0),
	I40E_PRIV_FLAG("legacy-rx", I40E_FLAG_LEGACY_RX, 0),
};

#define I40E_PRIV_FLAGS_STR_LEN ARRAY_SIZE(i40e_gstrings_priv_flags)

/* Private flags with a global effect, restricted to PF 0 */
static const struct i40e_priv_flags i40e_gl_gstrings_priv_flags[] = {
	I40E_PRIV_FLAG("vf-true-promisc-support",
		       I40E_FLAG_TRUE_PROMISC_SUPPORT, 0),
};

#define I40E_GL_PRIV_FLAGS_STR_LEN ARRAY_SIZE(i40e_gl_gstrings_priv_flags)

/**
 * i40e_partition_setting_complaint - generic complaint for MFP restriction
 * @pf: the PF struct
 **/
static void i40e_partition_setting_complaint(struct i40e_pf *pf)
{
	dev_info(&pf->pdev->dev,
		 "The link settings are allowed to be changed only from the first partition of a given port. Please switch to the first partition in order to change the setting.\n");
}

/**
 * i40e_phy_type_to_ethtool - convert the phy_types to ethtool link modes
 * @phy_types: PHY types to convert
 * @supported: pointer to the ethtool supported variable to fill in
 * @advertising: pointer to the ethtool advertising variable to fill in
 *
 **/
static void i40e_phy_type_to_ethtool(struct i40e_pf *pf, u32 *supported,
				     u32 *advertising)
{
	struct i40e_link_status *hw_link_info = &pf->hw.phy.link_info;
	u64 phy_types = pf->hw.phy.phy_types;

	*supported = 0x0;
	*advertising = 0x0;

	if (phy_types & I40E_CAP_PHY_TYPE_SGMII) {
		*supported |= SUPPORTED_Autoneg |
			      SUPPORTED_1000baseT_Full;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			*advertising |= ADVERTISED_1000baseT_Full;
		if (pf->flags & I40E_FLAG_100M_SGMII_CAPABLE) {
			*supported |= SUPPORTED_100baseT_Full;
			*advertising |= ADVERTISED_100baseT_Full;
		}
	}
	if (phy_types & I40E_CAP_PHY_TYPE_XAUI ||
	    phy_types & I40E_CAP_PHY_TYPE_XFI ||
	    phy_types & I40E_CAP_PHY_TYPE_SFI ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_SFPP_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_AOC)
		*supported |= SUPPORTED_10000baseT_Full;
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1 ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_LR) {
		*supported |= SUPPORTED_Autoneg |
			      SUPPORTED_10000baseT_Full;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			*advertising |= ADVERTISED_10000baseT_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_XLAUI ||
	    phy_types & I40E_CAP_PHY_TYPE_XLPPI ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_AOC)
		*supported |= SUPPORTED_40000baseCR4_Full;
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4) {
		*supported |= SUPPORTED_Autoneg |
			      SUPPORTED_40000baseCR4_Full;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_40GB)
			*advertising |= ADVERTISED_40000baseCR4_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_100BASE_TX) {
		*supported |= SUPPORTED_Autoneg |
			      SUPPORTED_100baseT_Full;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_100MB)
			*advertising |= ADVERTISED_100baseT_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_1000BASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_SX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_LX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_T_OPTICAL) {
		*supported |= SUPPORTED_Autoneg |
			      SUPPORTED_1000baseT_Full;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			*advertising |= ADVERTISED_1000baseT_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_SR4)
		*supported |= SUPPORTED_40000baseSR4_Full;
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_LR4)
		*supported |= SUPPORTED_40000baseLR4_Full;
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_KR4) {
		*supported |= SUPPORTED_40000baseKR4_Full |
			      SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_40000baseKR4_Full |
				ADVERTISED_Autoneg;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_20GBASE_KR2) {
		*supported |= SUPPORTED_20000baseKR2_Full |
			      SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_20GB)
			*advertising |= ADVERTISED_20000baseKR2_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_KR) {
		if (!(pf->flags & I40E_FLAG_HAVE_CRT_RETIMER))
			*supported |= SUPPORTED_10000baseKR_Full |
				      SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			if (!(pf->flags & I40E_FLAG_HAVE_CRT_RETIMER))
				*advertising |= ADVERTISED_10000baseKR_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_KX4) {
		*supported |= SUPPORTED_10000baseKX4_Full |
			      SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			*advertising |= ADVERTISED_10000baseKX4_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_1000BASE_KX) {
		if (!(pf->flags & I40E_FLAG_HAVE_CRT_RETIMER))
			*supported |= SUPPORTED_1000baseKX_Full |
				      SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			if (!(pf->flags & I40E_FLAG_HAVE_CRT_RETIMER))
				*advertising |= ADVERTISED_1000baseKX_Full;
	}
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_KR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_CR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_LR) {
		*supported |= SUPPORTED_Autoneg;
		*advertising |= ADVERTISED_Autoneg;
	}
}

/**
 * i40e_get_settings_link_up - Get the Link settings for when link is up
 * @hw: hw structure
 * @ecmd: ethtool command to fill in
 * @netdev: network interface device structure
 *
 **/
static void i40e_get_settings_link_up(struct i40e_hw *hw,
				      struct ethtool_link_ksettings *cmd,
				      struct net_device *netdev,
				      struct i40e_pf *pf)
{
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	u32 link_speed = hw_link_info->link_speed;
	u32 e_advertising = 0x0;
	u32 e_supported = 0x0;
	u32 supported, advertising;

	ethtool_convert_link_mode_to_legacy_u32(&supported,
						cmd->link_modes.supported);
	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	/* Initialize supported and advertised settings based on phy settings */
	switch (hw_link_info->phy_type) {
	case I40E_PHY_TYPE_40GBASE_CR4:
	case I40E_PHY_TYPE_40GBASE_CR4_CU:
		supported = SUPPORTED_Autoneg |
			    SUPPORTED_40000baseCR4_Full;
		advertising = ADVERTISED_Autoneg |
			      ADVERTISED_40000baseCR4_Full;
		break;
	case I40E_PHY_TYPE_XLAUI:
	case I40E_PHY_TYPE_XLPPI:
	case I40E_PHY_TYPE_40GBASE_AOC:
		supported = SUPPORTED_40000baseCR4_Full;
		break;
	case I40E_PHY_TYPE_40GBASE_SR4:
		supported = SUPPORTED_40000baseSR4_Full;
		break;
	case I40E_PHY_TYPE_40GBASE_LR4:
		supported = SUPPORTED_40000baseLR4_Full;
		break;
	case I40E_PHY_TYPE_10GBASE_SR:
	case I40E_PHY_TYPE_10GBASE_LR:
	case I40E_PHY_TYPE_1000BASE_SX:
	case I40E_PHY_TYPE_1000BASE_LX:
		supported = SUPPORTED_10000baseT_Full;
		if (hw_link_info->module_type[2] &
		    I40E_MODULE_TYPE_1000BASE_SX ||
		    hw_link_info->module_type[2] &
		    I40E_MODULE_TYPE_1000BASE_LX) {
			supported |= SUPPORTED_1000baseT_Full;
			if (hw_link_info->requested_speeds &
			    I40E_LINK_SPEED_1GB)
				advertising |= ADVERTISED_1000baseT_Full;
		}
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			advertising |= ADVERTISED_10000baseT_Full;
		break;
	case I40E_PHY_TYPE_10GBASE_T:
	case I40E_PHY_TYPE_1000BASE_T:
	case I40E_PHY_TYPE_100BASE_TX:
		supported = SUPPORTED_Autoneg |
			    SUPPORTED_10000baseT_Full |
			    SUPPORTED_1000baseT_Full |
			    SUPPORTED_100baseT_Full;
		advertising = ADVERTISED_Autoneg;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			advertising |= ADVERTISED_10000baseT_Full;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			advertising |= ADVERTISED_1000baseT_Full;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_100MB)
			advertising |= ADVERTISED_100baseT_Full;
		break;
	case I40E_PHY_TYPE_1000BASE_T_OPTICAL:
		supported = SUPPORTED_Autoneg |
			    SUPPORTED_1000baseT_Full;
		advertising = ADVERTISED_Autoneg |
			      ADVERTISED_1000baseT_Full;
		break;
	case I40E_PHY_TYPE_10GBASE_CR1_CU:
	case I40E_PHY_TYPE_10GBASE_CR1:
		supported = SUPPORTED_Autoneg |
			    SUPPORTED_10000baseT_Full;
		advertising = ADVERTISED_Autoneg |
			      ADVERTISED_10000baseT_Full;
		break;
	case I40E_PHY_TYPE_XAUI:
	case I40E_PHY_TYPE_XFI:
	case I40E_PHY_TYPE_SFI:
	case I40E_PHY_TYPE_10GBASE_SFPP_CU:
	case I40E_PHY_TYPE_10GBASE_AOC:
		supported = SUPPORTED_10000baseT_Full;
		advertising = SUPPORTED_10000baseT_Full;
		break;
	case I40E_PHY_TYPE_SGMII:
		supported = SUPPORTED_Autoneg |
			    SUPPORTED_1000baseT_Full;
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			advertising |= ADVERTISED_1000baseT_Full;
		if (pf->flags & I40E_FLAG_100M_SGMII_CAPABLE) {
			supported |= SUPPORTED_100baseT_Full;
			if (hw_link_info->requested_speeds &
			    I40E_LINK_SPEED_100MB)
				advertising |= ADVERTISED_100baseT_Full;
		}
		break;
	case I40E_PHY_TYPE_40GBASE_KR4:
	case I40E_PHY_TYPE_20GBASE_KR2:
	case I40E_PHY_TYPE_10GBASE_KR:
	case I40E_PHY_TYPE_10GBASE_KX4:
	case I40E_PHY_TYPE_1000BASE_KX:
		supported |= SUPPORTED_40000baseKR4_Full |
			     SUPPORTED_20000baseKR2_Full |
			     SUPPORTED_10000baseKR_Full |
			     SUPPORTED_10000baseKX4_Full |
			     SUPPORTED_1000baseKX_Full |
			     SUPPORTED_Autoneg;
		advertising |= ADVERTISED_40000baseKR4_Full |
			       ADVERTISED_20000baseKR2_Full |
			       ADVERTISED_10000baseKR_Full |
			       ADVERTISED_10000baseKX4_Full |
			       ADVERTISED_1000baseKX_Full |
			       ADVERTISED_Autoneg;
		break;
	case I40E_PHY_TYPE_25GBASE_KR:
	case I40E_PHY_TYPE_25GBASE_CR:
	case I40E_PHY_TYPE_25GBASE_SR:
	case I40E_PHY_TYPE_25GBASE_LR:
		supported = SUPPORTED_Autoneg;
		advertising = ADVERTISED_Autoneg;
		/* TODO: add speeds when ethtool is ready to support*/
		break;
	default:
		/* if we got here and link is up something bad is afoot */
		netdev_info(netdev, "WARNING: Link is up but PHY type 0x%x is not recognized.\n",
			    hw_link_info->phy_type);
	}

	/* Now that we've worked out everything that could be supported by the
	 * current PHY type, get what is supported by the NVM and them to
	 * get what is truly supported
	 */
	i40e_phy_type_to_ethtool(pf, &e_supported,
				 &e_advertising);

	supported = supported & e_supported;
	advertising = advertising & e_advertising;

	/* Set speed and duplex */
	switch (link_speed) {
	case I40E_LINK_SPEED_40GB:
		cmd->base.speed = SPEED_40000;
		break;
	case I40E_LINK_SPEED_25GB:
#ifdef SPEED_25000
		cmd->base.speed = SPEED_25000;
#else
		netdev_info(netdev,
			    "Speed is 25G, display not supported by this version of ethtool.\n");
#endif
		break;
	case I40E_LINK_SPEED_20GB:
		cmd->base.speed = SPEED_20000;
		break;
	case I40E_LINK_SPEED_10GB:
		cmd->base.speed = SPEED_10000;
		break;
	case I40E_LINK_SPEED_1GB:
		cmd->base.speed = SPEED_1000;
		break;
	case I40E_LINK_SPEED_100MB:
		cmd->base.speed = SPEED_100;
		break;
	default:
		break;
	}
	cmd->base.duplex = DUPLEX_FULL;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);
}

/**
 * i40e_get_settings_link_down - Get the Link settings for when link is down
 * @hw: hw structure
 * @ecmd: ethtool command to fill in
 *
 * Reports link settings that can be determined when link is down
 **/
static void i40e_get_settings_link_down(struct i40e_hw *hw,
					struct ethtool_link_ksettings *cmd,
					struct i40e_pf *pf)
{
	u32 supported, advertising;

	/* link is down and the driver needs to fall back on
	 * supported phy types to figure out what info to display
	 */
	i40e_phy_type_to_ethtool(pf, &supported, &advertising);

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	/* With no link speed and duplex are unknown */
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;
}

/**
 * i40e_get_settings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @ecmd: ethtool command
 *
 * Reports speed/duplex settings based on media_type
 **/
static int i40e_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	bool link_up = hw_link_info->link_info & I40E_AQ_LINK_UP;
	u32 advertising;

	if (link_up)
		i40e_get_settings_link_up(hw, cmd, netdev, pf);
	else
		i40e_get_settings_link_down(hw, cmd, pf);

	/* Now set the settings that don't rely on link being up/down */
	/* Set autoneg settings */
	cmd->base.autoneg = ((hw_link_info->an_info & I40E_AQ_AN_COMPLETED) ?
			  AUTONEG_ENABLE : AUTONEG_DISABLE);

	switch (hw->phy.media_type) {
	case I40E_MEDIA_TYPE_BACKPLANE:
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     Backplane);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Backplane);
		cmd->base.port = PORT_NONE;
		break;
	case I40E_MEDIA_TYPE_BASET:
		ethtool_link_ksettings_add_link_mode(cmd, supported, TP);
		ethtool_link_ksettings_add_link_mode(cmd, advertising, TP);
		cmd->base.port = PORT_TP;
		break;
	case I40E_MEDIA_TYPE_DA:
	case I40E_MEDIA_TYPE_CX4:
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);
		cmd->base.port = PORT_DA;
		break;
	case I40E_MEDIA_TYPE_FIBER:
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
		cmd->base.port = PORT_FIBRE;
		break;
	case I40E_MEDIA_TYPE_UNKNOWN:
	default:
		cmd->base.port = PORT_OTHER;
		break;
	}

	/* Set flow control settings */
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);

	switch (hw->fc.requested_mode) {
	case I40E_FC_FULL:
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Pause);
		break;
	case I40E_FC_TX_PAUSE:
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	case I40E_FC_RX_PAUSE:
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Pause);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	default:
		ethtool_convert_link_mode_to_legacy_u32(
			&advertising, cmd->link_modes.advertising);

		advertising &= ~(ADVERTISED_Pause | ADVERTISED_Asym_Pause);

		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.advertising, advertising);
		break;
	}

	return 0;
}

/**
 * i40e_set_settings - Set Speed and Duplex
 * @netdev: network interface device structure
 * @ecmd: ethtool command
 *
 * Set speed/duplex per media_types advertised/forced
 **/
static int i40e_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_aq_set_phy_config config;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	struct ethtool_link_ksettings safe_cmd;
	struct ethtool_link_ksettings copy_cmd;
	i40e_status status = 0;
	bool change = false;
	int timeout = 50;
	int err = 0;
	u32 autoneg;
	u32 advertise;
	u32 tmp;

	/* Changing port settings is not supported if this isn't the
	 * port's controlling PF
	 */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}

	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;

	if (hw->phy.media_type != I40E_MEDIA_TYPE_BASET &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_FIBER &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_BACKPLANE &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_DA &&
	    hw->phy.link_info.link_info & I40E_AQ_LINK_UP)
		return -EOPNOTSUPP;

	if (hw->device_id == I40E_DEV_ID_KX_B ||
	    hw->device_id == I40E_DEV_ID_KX_C ||
	    hw->device_id == I40E_DEV_ID_20G_KR2 ||
	    hw->device_id == I40E_DEV_ID_20G_KR2_A) {
		netdev_info(netdev, "Changing settings is not supported on backplane.\n");
		return -EOPNOTSUPP;
	}

	/* copy the cmd to copy_cmd to avoid modifying the origin */
	memcpy(&copy_cmd, cmd, sizeof(struct ethtool_link_ksettings));

	/* get our own copy of the bits to check against */
	memset(&safe_cmd, 0, sizeof(struct ethtool_link_ksettings));
	i40e_get_link_ksettings(netdev, &safe_cmd);

	/* save autoneg and speed out of cmd */
	autoneg = cmd->base.autoneg;
	ethtool_convert_link_mode_to_legacy_u32(&advertise,
						cmd->link_modes.advertising);

	/* set autoneg and speed back to what they currently are */
	copy_cmd.base.autoneg = safe_cmd.base.autoneg;
	ethtool_convert_link_mode_to_legacy_u32(
		&tmp, safe_cmd.link_modes.advertising);
	ethtool_convert_legacy_u32_to_link_mode(
		copy_cmd.link_modes.advertising, tmp);

	copy_cmd.base.cmd = safe_cmd.base.cmd;

	/* If copy_cmd and safe_cmd are not the same now, then they are
	 * trying to set something that we do not support
	 */
	if (memcmp(&copy_cmd, &safe_cmd, sizeof(struct ethtool_link_ksettings)))
		return -EOPNOTSUPP;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	/* Get the current phy config */
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	/* Copy abilities to config in case autoneg is not
	 * set below
	 */
	memset(&config, 0, sizeof(struct i40e_aq_set_phy_config));
	config.abilities = abilities.abilities;

	/* Check autoneg */
	if (autoneg == AUTONEG_ENABLE) {
		/* If autoneg was not already enabled */
		if (!(hw->phy.link_info.an_info & I40E_AQ_AN_COMPLETED)) {
			/* If autoneg is not supported, return error */
			if (!ethtool_link_ksettings_test_link_mode(
				    &safe_cmd, supported, Autoneg)) {
				netdev_info(netdev, "Autoneg not supported on this phy\n");
				err = -EINVAL;
				goto done;
			}
			/* Autoneg is allowed to change */
			config.abilities = abilities.abilities |
					   I40E_AQ_PHY_ENABLE_AN;
			change = true;
		}
	} else {
		/* If autoneg is currently enabled */
		if (hw->phy.link_info.an_info & I40E_AQ_AN_COMPLETED) {
			/* If autoneg is supported 10GBASE_T is the only PHY
			 * that can disable it, so otherwise return error
			 */
			if (ethtool_link_ksettings_test_link_mode(
				    &safe_cmd, supported, Autoneg) &&
			    hw->phy.link_info.phy_type !=
			    I40E_PHY_TYPE_10GBASE_T) {
				netdev_info(netdev, "Autoneg cannot be disabled on this phy\n");
				err = -EINVAL;
				goto done;
			}
			/* Autoneg is allowed to change */
			config.abilities = abilities.abilities &
					   ~I40E_AQ_PHY_ENABLE_AN;
			change = true;
		}
	}

	ethtool_convert_link_mode_to_legacy_u32(&tmp,
						safe_cmd.link_modes.supported);
	if (advertise & ~tmp) {
		err = -EINVAL;
		goto done;
	}

	if (advertise & ADVERTISED_100baseT_Full)
		config.link_speed |= I40E_LINK_SPEED_100MB;
	if (advertise & ADVERTISED_1000baseT_Full ||
	    advertise & ADVERTISED_1000baseKX_Full)
		config.link_speed |= I40E_LINK_SPEED_1GB;
	if (advertise & ADVERTISED_10000baseT_Full ||
	    advertise & ADVERTISED_10000baseKX4_Full ||
	    advertise & ADVERTISED_10000baseKR_Full)
		config.link_speed |= I40E_LINK_SPEED_10GB;
	if (advertise & ADVERTISED_20000baseKR2_Full)
		config.link_speed |= I40E_LINK_SPEED_20GB;
	if (advertise & ADVERTISED_40000baseKR4_Full ||
	    advertise & ADVERTISED_40000baseCR4_Full ||
	    advertise & ADVERTISED_40000baseSR4_Full ||
	    advertise & ADVERTISED_40000baseLR4_Full)
		config.link_speed |= I40E_LINK_SPEED_40GB;

	/* If speed didn't get set, set it to what it currently is.
	 * This is needed because if advertise is 0 (as it is when autoneg
	 * is disabled) then speed won't get set.
	 */
	if (!config.link_speed)
		config.link_speed = abilities.link_speed;

	if (change || (abilities.link_speed != config.link_speed)) {
		/* copy over the rest of the abilities */
		config.phy_type = abilities.phy_type;
		config.phy_type_ext = abilities.phy_type_ext;
		config.eee_capability = abilities.eee_capability;
		config.eeer = abilities.eeer_val;
		config.low_power_ctrl = abilities.d3_lpan;
		config.fec_config = abilities.fec_cfg_curr_mod_ext_info &
				    I40E_AQ_PHY_FEC_CONFIG_MASK;

		/* save the requested speeds */
		hw->phy.link_info.requested_speeds = config.link_speed;
		/* set link and auto negotiation so changes take effect */
		config.abilities |= I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
		/* If link is up put link down */
		if (hw->phy.link_info.link_info & I40E_AQ_LINK_UP) {
			/* Tell the OS link is going down, the link will go
			 * back up when fw says it is ready asynchronously
			 */
			i40e_print_link_message(vsi, false);
			netif_carrier_off(netdev);
			netif_tx_stop_all_queues(netdev);
		}

		/* make the aq call */
		status = i40e_aq_set_phy_config(hw, &config, NULL);
		if (status) {
			netdev_info(netdev, "Set phy config failed, err %s aq_err %s\n",
				    i40e_stat_str(hw, status),
				    i40e_aq_str(hw, hw->aq.asq_last_status));
			err = -EAGAIN;
			goto done;
		}

		status = i40e_update_link_info(hw);
		if (status)
			netdev_dbg(netdev, "Updating link info failed with err %s aq_err %s\n",
				   i40e_stat_str(hw, status),
				   i40e_aq_str(hw, hw->aq.asq_last_status));

	} else {
		netdev_info(netdev, "Nothing changed, exiting without setting anything.\n");
	}

done:
	clear_bit(__I40E_CONFIG_BUSY, pf->state);

	return err;
}

static int i40e_nway_reset(struct net_device *netdev)
{
	/* restart autonegotiation */
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	bool link_up = hw->phy.link_info.link_info & I40E_AQ_LINK_UP;
	i40e_status ret = 0;

	ret = i40e_aq_set_link_restart_an(hw, link_up, NULL);
	if (ret) {
		netdev_info(netdev, "link restart failed, err %s aq_err %s\n",
			    i40e_stat_str(hw, ret),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		return -EIO;
	}

	return 0;
}

/**
 * i40e_get_pauseparam -  Get Flow Control status
 * Return tx/rx-pause status
 **/
static void i40e_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	struct i40e_dcbx_config *dcbx_cfg = &hw->local_dcbx_config;

	pause->autoneg =
		((hw_link_info->an_info & I40E_AQ_AN_COMPLETED) ?
		  AUTONEG_ENABLE : AUTONEG_DISABLE);

	/* PFC enabled so report LFC as off */
	if (dcbx_cfg->pfc.pfcenable) {
		pause->rx_pause = 0;
		pause->tx_pause = 0;
		return;
	}

	if (hw->fc.current_mode == I40E_FC_RX_PAUSE) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == I40E_FC_TX_PAUSE) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == I40E_FC_FULL) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

/**
 * i40e_set_pauseparam - Set Flow Control parameter
 * @netdev: network interface device structure
 * @pause: return tx/rx flow control status
 **/
static int i40e_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	struct i40e_dcbx_config *dcbx_cfg = &hw->local_dcbx_config;
	bool link_up = hw_link_info->link_info & I40E_AQ_LINK_UP;
	i40e_status status;
	u8 aq_failures;
	int err = 0;

	/* Changing the port's flow control is not supported if this isn't the
	 * port's controlling PF
	 */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}

	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;

	if (pause->autoneg != ((hw_link_info->an_info & I40E_AQ_AN_COMPLETED) ?
	    AUTONEG_ENABLE : AUTONEG_DISABLE)) {
		netdev_info(netdev, "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	/* If we have link and don't have autoneg */
	if (!test_bit(__I40E_DOWN, pf->state) &&
	    !(hw_link_info->an_info & I40E_AQ_AN_COMPLETED)) {
		/* Send message that it might not necessarily work*/
		netdev_info(netdev, "Autoneg did not complete so changing settings may not result in an actual change.\n");
	}

	if (dcbx_cfg->pfc.pfcenable) {
		netdev_info(netdev,
			    "Priority flow control enabled. Cannot set link flow control.\n");
		return -EOPNOTSUPP;
	}

	if (pause->rx_pause && pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_FULL;
	else if (pause->rx_pause && !pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_RX_PAUSE;
	else if (!pause->rx_pause && pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_TX_PAUSE;
	else if (!pause->rx_pause && !pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_NONE;
	else
		 return -EINVAL;

	/* Tell the OS link is going down, the link will go back up when fw
	 * says it is ready asynchronously
	 */
	i40e_print_link_message(vsi, false);
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	/* Set the fc mode and only restart an if link is up*/
	status = i40e_set_fc(hw, &aq_failures, link_up);

	if (aq_failures & I40E_SET_FC_AQ_FAIL_GET) {
		netdev_info(netdev, "Set fc failed on the get_phy_capabilities call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}
	if (aq_failures & I40E_SET_FC_AQ_FAIL_SET) {
		netdev_info(netdev, "Set fc failed on the set_phy_config call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}
	if (aq_failures & I40E_SET_FC_AQ_FAIL_UPDATE) {
		netdev_info(netdev, "Set fc failed on the get_link_info call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}

	if (!test_bit(__I40E_DOWN, pf->state)) {
		/* Give it a little more time to try to come back */
		msleep(75);
		if (!test_bit(__I40E_DOWN, pf->state))
			return i40e_nway_reset(netdev);
	}

	return err;
}

static u32 i40e_get_msglevel(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	u32 debug_mask = pf->hw.debug_mask;

	if (debug_mask)
		netdev_info(netdev, "i40e debug_mask: 0x%08X\n", debug_mask);

	return pf->msg_enable;
}

static void i40e_set_msglevel(struct net_device *netdev, u32 data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	if (I40E_DEBUG_USER & data)
		pf->hw.debug_mask = data;
	else
		pf->msg_enable = data;
}

static int i40e_get_regs_len(struct net_device *netdev)
{
	int reg_count = 0;
	int i;

	for (i = 0; i40e_reg_list[i].offset != 0; i++)
		reg_count += i40e_reg_list[i].elements;

	return reg_count * sizeof(u32);
}

static void i40e_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			  void *p)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 *reg_buf = p;
	int i, j, ri;
	u32 reg;

	/* Tell ethtool which driver-version-specific regs output we have.
	 *
	 * At some point, if we have ethtool doing special formatting of
	 * this data, it will rely on this version number to know how to
	 * interpret things.  Hence, this needs to be updated if/when the
	 * diags register table is changed.
	 */
	regs->version = 1;

	/* loop through the diags reg table for what to print */
	ri = 0;
	for (i = 0; i40e_reg_list[i].offset != 0; i++) {
		for (j = 0; j < i40e_reg_list[i].elements; j++) {
			reg = i40e_reg_list[i].offset
				+ (j * i40e_reg_list[i].stride);
			reg_buf[ri++] = rd32(hw, reg);
		}
	}

}

static int i40e_get_eeprom(struct net_device *netdev,
			   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_pf *pf = np->vsi->back;
	int ret_val = 0, len, offset;
	u8 *eeprom_buff;
	u16 i, sectors;
	bool last;
	u32 magic;

#define I40E_NVM_SECTOR_SIZE  4096
	if (eeprom->len == 0)
		return -EINVAL;

	/* check for NVMUpdate access method */
	magic = hw->vendor_id | (hw->device_id << 16);
	if (eeprom->magic && eeprom->magic != magic) {
		struct i40e_nvm_access *cmd = (struct i40e_nvm_access *)eeprom;
		int errno = 0;

		/* make sure it is the right magic for NVMUpdate */
		if ((eeprom->magic >> 16) != hw->device_id)
			errno = -EINVAL;
		else if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
			 test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
			errno = -EBUSY;
		else
			ret_val = i40e_nvmupd_command(hw, cmd, bytes, &errno);

		if ((errno || ret_val) && (hw->debug_mask & I40E_DEBUG_NVM))
			dev_info(&pf->pdev->dev,
				 "NVMUpdate read failed err=%d status=0x%x errno=%d module=%d offset=0x%x size=%d\n",
				 ret_val, hw->aq.asq_last_status, errno,
				 (u8)(cmd->config & I40E_NVM_MOD_PNT_MASK),
				 cmd->offset, cmd->data_size);

		return errno;
	}

	/* normal ethtool get_eeprom support */
	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	eeprom_buff = kzalloc(eeprom->len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ret_val = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret_val) {
		dev_info(&pf->pdev->dev,
			 "Failed Acquiring NVM resource for read err=%d status=0x%x\n",
			 ret_val, hw->aq.asq_last_status);
		goto free_buff;
	}

	sectors = eeprom->len / I40E_NVM_SECTOR_SIZE;
	sectors += (eeprom->len % I40E_NVM_SECTOR_SIZE) ? 1 : 0;
	len = I40E_NVM_SECTOR_SIZE;
	last = false;
	for (i = 0; i < sectors; i++) {
		if (i == (sectors - 1)) {
			len = eeprom->len - (I40E_NVM_SECTOR_SIZE * i);
			last = true;
		}
		offset = eeprom->offset + (I40E_NVM_SECTOR_SIZE * i),
		ret_val = i40e_aq_read_nvm(hw, 0x0, offset, len,
				(u8 *)eeprom_buff + (I40E_NVM_SECTOR_SIZE * i),
				last, NULL);
		if (ret_val && hw->aq.asq_last_status == I40E_AQ_RC_EPERM) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed, invalid offset 0x%x\n",
				 offset);
			break;
		} else if (ret_val &&
			   hw->aq.asq_last_status == I40E_AQ_RC_EACCES) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed, access, offset 0x%x\n",
				 offset);
			break;
		} else if (ret_val) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed offset %d err=%d status=0x%x\n",
				 offset, ret_val, hw->aq.asq_last_status);
			break;
		}
	}

	i40e_release_nvm(hw);
	memcpy(bytes, (u8 *)eeprom_buff, eeprom->len);
free_buff:
	kfree(eeprom_buff);
	return ret_val;
}

static int i40e_get_eeprom_len(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	u32 val;

#define X722_EEPROM_SCOPE_LIMIT 0x5B9FFF
	if (hw->mac.type == I40E_MAC_X722) {
		val = X722_EEPROM_SCOPE_LIMIT + 1;
		return val;
	}
	val = (rd32(hw, I40E_GLPCI_LBARCTRL)
		& I40E_GLPCI_LBARCTRL_FL_SIZE_MASK)
		>> I40E_GLPCI_LBARCTRL_FL_SIZE_SHIFT;
	/* register returns value in power of 2, 64Kbyte chunks. */
	val = (64 * 1024) * BIT(val);
	return val;
}

static int i40e_set_eeprom(struct net_device *netdev,
			   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_nvm_access *cmd = (struct i40e_nvm_access *)eeprom;
	int ret_val = 0;
	int errno = 0;
	u32 magic;

	/* normal ethtool set_eeprom is not supported */
	magic = hw->vendor_id | (hw->device_id << 16);
	if (eeprom->magic == magic)
		errno = -EOPNOTSUPP;
	/* check for NVMUpdate access method */
	else if (!eeprom->magic || (eeprom->magic >> 16) != hw->device_id)
		errno = -EINVAL;
	else if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
		 test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		errno = -EBUSY;
	else
		ret_val = i40e_nvmupd_command(hw, cmd, bytes, &errno);

	if ((errno || ret_val) && (hw->debug_mask & I40E_DEBUG_NVM))
		dev_info(&pf->pdev->dev,
			 "NVMUpdate write failed err=%d status=0x%x errno=%d module=%d offset=0x%x size=%d\n",
			 ret_val, hw->aq.asq_last_status, errno,
			 (u8)(cmd->config & I40E_NVM_MOD_PNT_MASK),
			 cmd->offset, cmd->data_size);

	return errno;
}

static void i40e_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	strlcpy(drvinfo->driver, i40e_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, i40e_driver_version_str,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, i40e_nvm_version_str(&pf->hw),
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(pf->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_priv_flags = I40E_PRIV_FLAGS_STR_LEN;
	if (pf->hw.pf_id == 0)
		drvinfo->n_priv_flags += I40E_GL_PRIV_FLAGS_STR_LEN;
}

static void i40e_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];

	ring->rx_max_pending = I40E_MAX_NUM_DESCRIPTORS;
	ring->tx_max_pending = I40E_MAX_NUM_DESCRIPTORS;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = vsi->rx_rings[0]->count;
	ring->tx_pending = vsi->tx_rings[0]->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int i40e_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct i40e_ring *tx_rings = NULL, *rx_rings = NULL;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 new_rx_count, new_tx_count;
	int timeout = 50;
	int i, err = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	if (ring->tx_pending > I40E_MAX_NUM_DESCRIPTORS ||
	    ring->tx_pending < I40E_MIN_NUM_DESCRIPTORS ||
	    ring->rx_pending > I40E_MAX_NUM_DESCRIPTORS ||
	    ring->rx_pending < I40E_MIN_NUM_DESCRIPTORS) {
		netdev_info(netdev,
			    "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d]\n",
			    ring->tx_pending, ring->rx_pending,
			    I40E_MIN_NUM_DESCRIPTORS, I40E_MAX_NUM_DESCRIPTORS);
		return -EINVAL;
	}

	new_tx_count = ALIGN(ring->tx_pending, I40E_REQ_DESCRIPTOR_MULTIPLE);
	new_rx_count = ALIGN(ring->rx_pending, I40E_REQ_DESCRIPTOR_MULTIPLE);

	/* if nothing to do return success */
	if ((new_tx_count == vsi->tx_rings[0]->count) &&
	    (new_rx_count == vsi->rx_rings[0]->count))
		return 0;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	if (!netif_running(vsi->netdev)) {
		/* simple case - set for the next time the netdev is started */
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			vsi->tx_rings[i]->count = new_tx_count;
			vsi->rx_rings[i]->count = new_rx_count;
		}
		goto done;
	}

	/* We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the Tx and Rx ring structs.
	 */

	/* alloc updated Tx resources */
	if (new_tx_count != vsi->tx_rings[0]->count) {
		netdev_info(netdev,
			    "Changing Tx descriptor count from %d to %d.\n",
			    vsi->tx_rings[0]->count, new_tx_count);
		tx_rings = kcalloc(vsi->alloc_queue_pairs,
				   sizeof(struct i40e_ring), GFP_KERNEL);
		if (!tx_rings) {
			err = -ENOMEM;
			goto done;
		}

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			/* clone ring and setup updated count */
			tx_rings[i] = *vsi->tx_rings[i];
			tx_rings[i].count = new_tx_count;
			/* the desc and bi pointers will be reallocated in the
			 * setup call
			 */
			tx_rings[i].desc = NULL;
			tx_rings[i].rx_bi = NULL;
			err = i40e_setup_tx_descriptors(&tx_rings[i]);
			if (err) {
				while (i) {
					i--;
					i40e_free_tx_resources(&tx_rings[i]);
				}
				kfree(tx_rings);
				tx_rings = NULL;

				goto done;
			}
		}
	}

	/* alloc updated Rx resources */
	if (new_rx_count != vsi->rx_rings[0]->count) {
		netdev_info(netdev,
			    "Changing Rx descriptor count from %d to %d\n",
			    vsi->rx_rings[0]->count, new_rx_count);
		rx_rings = kcalloc(vsi->alloc_queue_pairs,
				   sizeof(struct i40e_ring), GFP_KERNEL);
		if (!rx_rings) {
			err = -ENOMEM;
			goto free_tx;
		}

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			struct i40e_ring *ring;
			u16 unused;

			/* clone ring and setup updated count */
			rx_rings[i] = *vsi->rx_rings[i];
			rx_rings[i].count = new_rx_count;
			/* the desc and bi pointers will be reallocated in the
			 * setup call
			 */
			rx_rings[i].desc = NULL;
			rx_rings[i].rx_bi = NULL;
			/* this is to allow wr32 to have something to write to
			 * during early allocation of Rx buffers
			 */
			rx_rings[i].tail = hw->hw_addr + I40E_PRTGEN_STATUS;
			err = i40e_setup_rx_descriptors(&rx_rings[i]);
			if (err)
				goto rx_unwind;

			/* now allocate the Rx buffers to make sure the OS
			 * has enough memory, any failure here means abort
			 */
			ring = &rx_rings[i];
			unused = I40E_DESC_UNUSED(ring);
			err = i40e_alloc_rx_buffers(ring, unused);
rx_unwind:
			if (err) {
				do {
					i40e_free_rx_resources(&rx_rings[i]);
				} while (i--);
				kfree(rx_rings);
				rx_rings = NULL;

				goto free_tx;
			}
		}
	}

	/* Bring interface down, copy in the new ring info,
	 * then restore the interface
	 */
	i40e_down(vsi);

	if (tx_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			i40e_free_tx_resources(vsi->tx_rings[i]);
			*vsi->tx_rings[i] = tx_rings[i];
		}
		kfree(tx_rings);
		tx_rings = NULL;
	}

	if (rx_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			i40e_free_rx_resources(vsi->rx_rings[i]);
			/* get the real tail offset */
			rx_rings[i].tail = vsi->rx_rings[i]->tail;
			/* this is to fake out the allocation routine
			 * into thinking it has to realloc everything
			 * but the recycling logic will let us re-use
			 * the buffers allocated above
			 */
			rx_rings[i].next_to_use = 0;
			rx_rings[i].next_to_clean = 0;
			rx_rings[i].next_to_alloc = 0;
			/* do a struct copy */
			*vsi->rx_rings[i] = rx_rings[i];
		}
		kfree(rx_rings);
		rx_rings = NULL;
	}

	i40e_up(vsi);

free_tx:
	/* error cleanup if the Rx allocations failed after getting Tx */
	if (tx_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++)
			i40e_free_tx_resources(&tx_rings[i]);
		kfree(tx_rings);
		tx_rings = NULL;
	}

done:
	clear_bit(__I40E_CONFIG_BUSY, pf->state);

	return err;
}

static int i40e_get_sset_count(struct net_device *netdev, int sset)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	switch (sset) {
	case ETH_SS_TEST:
		return I40E_TEST_LEN;
	case ETH_SS_STATS:
		if (vsi == pf->vsi[pf->lan_vsi] && pf->hw.partition_id == 1) {
			int len = I40E_PF_STATS_LEN(netdev);

			if ((pf->lan_veb != I40E_NO_VEB) &&
			    (pf->flags & I40E_FLAG_VEB_STATS_ENABLED))
				len += I40E_VEB_STATS_TOTAL;
			return len;
		} else {
			return I40E_VSI_STATS_LEN(netdev);
		}
	case ETH_SS_PRIV_FLAGS:
		return I40E_PRIV_FLAGS_STR_LEN +
			(pf->hw.pf_id == 0 ? I40E_GL_PRIV_FLAGS_STR_LEN : 0);
	default:
		return -EOPNOTSUPP;
	}
}

static void i40e_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_ring *tx_ring, *rx_ring;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int i = 0;
	char *p;
	int j;
	struct rtnl_link_stats64 *net_stats = i40e_get_vsi_stats_struct(vsi);
	unsigned int start;

	i40e_update_stats(vsi);

	for (j = 0; j < I40E_NETDEV_STATS_LEN; j++) {
		p = (char *)net_stats + i40e_gstrings_net_stats[j].stat_offset;
		data[i++] = (i40e_gstrings_net_stats[j].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < I40E_MISC_STATS_LEN; j++) {
		p = (char *)vsi + i40e_gstrings_misc_stats[j].stat_offset;
		data[i++] = (i40e_gstrings_misc_stats[j].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	rcu_read_lock();
	for (j = 0; j < vsi->num_queue_pairs; j++) {
		tx_ring = ACCESS_ONCE(vsi->tx_rings[j]);

		if (!tx_ring)
			continue;

		/* process Tx ring statistics */
		do {
			start = u64_stats_fetch_begin_irq(&tx_ring->syncp);
			data[i] = tx_ring->stats.packets;
			data[i + 1] = tx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&tx_ring->syncp, start));
		i += 2;

		/* Rx ring is the 2nd half of the queue pair */
		rx_ring = &tx_ring[1];
		do {
			start = u64_stats_fetch_begin_irq(&rx_ring->syncp);
			data[i] = rx_ring->stats.packets;
			data[i + 1] = rx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&rx_ring->syncp, start));
		i += 2;
	}
	rcu_read_unlock();
	if (vsi != pf->vsi[pf->lan_vsi] || pf->hw.partition_id != 1)
		return;

	if ((pf->lan_veb != I40E_NO_VEB) &&
	    (pf->flags & I40E_FLAG_VEB_STATS_ENABLED)) {
		struct i40e_veb *veb = pf->veb[pf->lan_veb];

		for (j = 0; j < I40E_VEB_STATS_LEN; j++) {
			p = (char *)veb;
			p += i40e_gstrings_veb_stats[j].stat_offset;
			data[i++] = (i40e_gstrings_veb_stats[j].sizeof_stat ==
				     sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
		for (j = 0; j < I40E_MAX_TRAFFIC_CLASS; j++) {
			data[i++] = veb->tc_stats.tc_tx_packets[j];
			data[i++] = veb->tc_stats.tc_tx_bytes[j];
			data[i++] = veb->tc_stats.tc_rx_packets[j];
			data[i++] = veb->tc_stats.tc_rx_bytes[j];
		}
	}
	for (j = 0; j < I40E_GLOBAL_STATS_LEN; j++) {
		p = (char *)pf + i40e_gstrings_stats[j].stat_offset;
		data[i++] = (i40e_gstrings_stats[j].sizeof_stat ==
			     sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < I40E_MAX_USER_PRIORITY; j++) {
		data[i++] = pf->stats.priority_xon_tx[j];
		data[i++] = pf->stats.priority_xoff_tx[j];
	}
	for (j = 0; j < I40E_MAX_USER_PRIORITY; j++) {
		data[i++] = pf->stats.priority_xon_rx[j];
		data[i++] = pf->stats.priority_xoff_rx[j];
	}
	for (j = 0; j < I40E_MAX_USER_PRIORITY; j++)
		data[i++] = pf->stats.priority_xon_2_xoff[j];
}

static void i40e_get_strings(struct net_device *netdev, u32 stringset,
			     u8 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	char *p = (char *)data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, i40e_gstrings_test,
		       I40E_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < I40E_NETDEV_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 i40e_gstrings_net_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < I40E_MISC_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 i40e_gstrings_misc_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			snprintf(p, ETH_GSTRING_LEN, "tx-%d.tx_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "tx-%d.tx_bytes", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "rx-%d.rx_packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "rx-%d.rx_bytes", i);
			p += ETH_GSTRING_LEN;
		}
		if (vsi != pf->vsi[pf->lan_vsi] || pf->hw.partition_id != 1)
			return;

		if ((pf->lan_veb != I40E_NO_VEB) &&
		    (pf->flags & I40E_FLAG_VEB_STATS_ENABLED)) {
			for (i = 0; i < I40E_VEB_STATS_LEN; i++) {
				snprintf(p, ETH_GSTRING_LEN, "veb.%s",
					i40e_gstrings_veb_stats[i].stat_string);
				p += ETH_GSTRING_LEN;
			}
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				snprintf(p, ETH_GSTRING_LEN,
					 "veb.tc_%d_tx_packets", i);
				p += ETH_GSTRING_LEN;
				snprintf(p, ETH_GSTRING_LEN,
					 "veb.tc_%d_tx_bytes", i);
				p += ETH_GSTRING_LEN;
				snprintf(p, ETH_GSTRING_LEN,
					 "veb.tc_%d_rx_packets", i);
				p += ETH_GSTRING_LEN;
				snprintf(p, ETH_GSTRING_LEN,
					 "veb.tc_%d_rx_bytes", i);
				p += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < I40E_GLOBAL_STATS_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "port.%s",
				 i40e_gstrings_stats[i].stat_string);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
			snprintf(p, ETH_GSTRING_LEN,
				 "port.tx_priority_%d_xon", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN,
				 "port.tx_priority_%d_xoff", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
			snprintf(p, ETH_GSTRING_LEN,
				 "port.rx_priority_%d_xon", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN,
				 "port.rx_priority_%d_xoff", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
			snprintf(p, ETH_GSTRING_LEN,
				 "port.rx_priority_%d_xon_2_xoff", i);
			p += ETH_GSTRING_LEN;
		}
		/* BUG_ON(p - data != I40E_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 i40e_gstrings_priv_flags[i].flag_string);
			p += ETH_GSTRING_LEN;
		}
		if (pf->hw.pf_id != 0)
			break;
		for (i = 0; i < I40E_GL_PRIV_FLAGS_STR_LEN; i++) {
			snprintf(p, ETH_GSTRING_LEN, "%s",
				 i40e_gl_gstrings_priv_flags[i].flag_string);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		break;
	}
}

static int i40e_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);

	/* only report HW timestamping if PTP is enabled */
	if (!(pf->flags & I40E_FLAG_PTP))
		return ethtool_op_get_ts_info(dev, info);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	if (pf->ptp_clock)
		info->phc_index = ptp_clock_index(pf->ptp_clock);
	else
		info->phc_index = -1;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ);

	if (pf->flags & I40E_FLAG_PTP_L4_CAPABLE)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	return 0;
}

static int i40e_link_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	i40e_status status;
	bool link_up = false;

	netif_info(pf, hw, netdev, "link test\n");
	status = i40e_get_link_status(&pf->hw, &link_up);
	if (status) {
		netif_err(pf, drv, netdev, "link query timed out, please retry test\n");
		*data = 1;
		return *data;
	}

	if (link_up)
		*data = 0;
	else
		*data = 1;

	return *data;
}

static int i40e_reg_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	netif_info(pf, hw, netdev, "register test\n");
	*data = i40e_diag_reg_test(&pf->hw);

	return *data;
}

static int i40e_eeprom_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	netif_info(pf, hw, netdev, "eeprom test\n");
	*data = i40e_diag_eeprom_test(&pf->hw);

	/* forcebly clear the NVM Update state machine */
	pf->hw.nvmupd_state = I40E_NVMUPD_STATE_INIT;

	return *data;
}

static int i40e_intr_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	u16 swc_old = pf->sw_int_count;

	netif_info(pf, hw, netdev, "interrupt test\n");
	wr32(&pf->hw, I40E_PFINT_DYN_CTL0,
	     (I40E_PFINT_DYN_CTL0_INTENA_MASK |
	      I40E_PFINT_DYN_CTL0_SWINT_TRIG_MASK |
	      I40E_PFINT_DYN_CTL0_ITR_INDX_MASK |
	      I40E_PFINT_DYN_CTL0_SW_ITR_INDX_ENA_MASK |
	      I40E_PFINT_DYN_CTL0_SW_ITR_INDX_MASK));
	usleep_range(1000, 2000);
	*data = (swc_old == pf->sw_int_count);

	return *data;
}

static inline bool i40e_active_vfs(struct i40e_pf *pf)
{
	struct i40e_vf *vfs = pf->vf;
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++)
		if (test_bit(I40E_VF_STATE_ACTIVE, &vfs[i].vf_states))
			return true;
	return false;
}

static inline bool i40e_active_vmdqs(struct i40e_pf *pf)
{
	return !!i40e_find_vsi_by_type(pf, I40E_VSI_VMDQ2);
}

static void i40e_diag_test(struct net_device *netdev,
			   struct ethtool_test *eth_test, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	bool if_running = netif_running(netdev);
	struct i40e_pf *pf = np->vsi->back;

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */
		netif_info(pf, drv, netdev, "offline testing starting\n");

		set_bit(__I40E_TESTING, pf->state);

		if (i40e_active_vfs(pf) || i40e_active_vmdqs(pf)) {
			dev_warn(&pf->pdev->dev,
				 "Please take active VFs and Netqueues offline and restart the adapter before running NIC diagnostics\n");
			data[I40E_ETH_TEST_REG]		= 1;
			data[I40E_ETH_TEST_EEPROM]	= 1;
			data[I40E_ETH_TEST_INTR]	= 1;
			data[I40E_ETH_TEST_LINK]	= 1;
			eth_test->flags |= ETH_TEST_FL_FAILED;
			clear_bit(__I40E_TESTING, pf->state);
			goto skip_ol_tests;
		}

		/* If the device is online then take it offline */
		if (if_running)
			/* indicate we're in test mode */
			i40e_close(netdev);
		else
			/* This reset does not affect link - if it is
			 * changed to a type of reset that does affect
			 * link then the following link test would have
			 * to be moved to before the reset
			 */
			i40e_do_reset(pf, BIT(__I40E_PF_RESET_REQUESTED), true);

		if (i40e_link_test(netdev, &data[I40E_ETH_TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (i40e_eeprom_test(netdev, &data[I40E_ETH_TEST_EEPROM]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (i40e_intr_test(netdev, &data[I40E_ETH_TEST_INTR]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* run reg test last, a reset is required after it */
		if (i40e_reg_test(netdev, &data[I40E_ETH_TEST_REG]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		clear_bit(__I40E_TESTING, pf->state);
		i40e_do_reset(pf, BIT(__I40E_PF_RESET_REQUESTED), true);

		if (if_running)
			i40e_open(netdev);
	} else {
		/* Online tests */
		netif_info(pf, drv, netdev, "online testing starting\n");

		if (i40e_link_test(netdev, &data[I40E_ETH_TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Offline only tests, not run in online; pass by default */
		data[I40E_ETH_TEST_REG] = 0;
		data[I40E_ETH_TEST_EEPROM] = 0;
		data[I40E_ETH_TEST_INTR] = 0;
	}

skip_ol_tests:

	netif_info(pf, drv, netdev, "testing finished\n");
}

static void i40e_get_wol(struct net_device *netdev,
			 struct ethtool_wolinfo *wol)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 wol_nvm_bits;

	/* NVM bit on means WoL disabled for the port */
	i40e_read_nvm_word(hw, I40E_SR_NVM_WAKE_ON_LAN, &wol_nvm_bits);
	if ((BIT(hw->port) & wol_nvm_bits) || (hw->partition_id != 1)) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = (pf->wol_en ? WAKE_MAGIC : 0);
	}
}

/**
 * i40e_set_wol - set the WakeOnLAN configuration
 * @netdev: the netdev in question
 * @wol: the ethtool WoL setting data
 **/
static int i40e_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	u16 wol_nvm_bits;

	/* WoL not supported if this isn't the controlling PF on the port */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}

	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;

	/* NVM bit on means WoL disabled for the port */
	i40e_read_nvm_word(hw, I40E_SR_NVM_WAKE_ON_LAN, &wol_nvm_bits);
	if (BIT(hw->port) & wol_nvm_bits)
		return -EOPNOTSUPP;

	/* only magic packet is supported */
	if (wol->wolopts && (wol->wolopts != WAKE_MAGIC))
		return -EOPNOTSUPP;

	/* is this a new value? */
	if (pf->wol_en != !!wol->wolopts) {
		pf->wol_en = !!wol->wolopts;
		device_set_wakeup_enable(&pf->pdev->dev, pf->wol_en);
	}

	return 0;
}

static int i40e_set_phys_id(struct net_device *netdev,
			    enum ethtool_phys_id_state state)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	i40e_status ret = 0;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int blink_freq = 2;
	u16 temp_status;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		if (!(pf->flags & I40E_FLAG_PHY_CONTROLS_LEDS)) {
			pf->led_status = i40e_led_get(hw);
		} else {
			i40e_aq_set_phy_debug(hw, I40E_PHY_DEBUG_ALL, NULL);
			ret = i40e_led_get_phy(hw, &temp_status,
					       &pf->phy_led_val);
			pf->led_status = temp_status;
		}
		return blink_freq;
	case ETHTOOL_ID_ON:
		if (!(pf->flags & I40E_FLAG_PHY_CONTROLS_LEDS))
			i40e_led_set(hw, 0xf, false);
		else
			ret = i40e_led_set_phy(hw, true, pf->led_status, 0);
		break;
	case ETHTOOL_ID_OFF:
		if (!(pf->flags & I40E_FLAG_PHY_CONTROLS_LEDS))
			i40e_led_set(hw, 0x0, false);
		else
			ret = i40e_led_set_phy(hw, false, pf->led_status, 0);
		break;
	case ETHTOOL_ID_INACTIVE:
		if (!(pf->flags & I40E_FLAG_PHY_CONTROLS_LEDS)) {
			i40e_led_set(hw, pf->led_status, false);
		} else {
			ret = i40e_led_set_phy(hw, false, pf->led_status,
					       (pf->phy_led_val |
					       I40E_PHY_LED_MODE_ORIG));
			i40e_aq_set_phy_debug(hw, 0, NULL);
		}
		break;
	default:
		break;
	}
		if (ret)
			return -ENOENT;
		else
			return 0;
}

/* NOTE: i40e hardware uses a conversion factor of 2 for Interrupt
 * Throttle Rate (ITR) ie. ITR(1) = 2us ITR(10) = 20 us, and also
 * 125us (8000 interrupts per second) == ITR(62)
 */

/**
 * __i40e_get_coalesce - get per-queue coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 * @queue: which queue to pick
 *
 * Gets the per-queue settings for coalescence. Specifically Rx and Tx usecs
 * are per queue. If queue is <0 then we default to queue 0 as the
 * representative value.
 **/
static int __i40e_get_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec,
			       int queue)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_ring *rx_ring, *tx_ring;
	struct i40e_vsi *vsi = np->vsi;

	ec->tx_max_coalesced_frames_irq = vsi->work_limit;
	ec->rx_max_coalesced_frames_irq = vsi->work_limit;

	/* rx and tx usecs has per queue value. If user doesn't specify the queue,
	 * return queue 0's value to represent.
	 */
	if (queue < 0) {
		queue = 0;
	} else if (queue >= vsi->num_queue_pairs) {
		return -EINVAL;
	}

	rx_ring = vsi->rx_rings[queue];
	tx_ring = vsi->tx_rings[queue];

	if (ITR_IS_DYNAMIC(rx_ring->rx_itr_setting))
		ec->use_adaptive_rx_coalesce = 1;

	if (ITR_IS_DYNAMIC(tx_ring->tx_itr_setting))
		ec->use_adaptive_tx_coalesce = 1;

	ec->rx_coalesce_usecs = rx_ring->rx_itr_setting & ~I40E_ITR_DYNAMIC;
	ec->tx_coalesce_usecs = tx_ring->tx_itr_setting & ~I40E_ITR_DYNAMIC;


	/* we use the _usecs_high to store/set the interrupt rate limit
	 * that the hardware supports, that almost but not quite
	 * fits the original intent of the ethtool variable,
	 * the rx_coalesce_usecs_high limits total interrupts
	 * per second from both tx/rx sources.
	 */
	ec->rx_coalesce_usecs_high = vsi->int_rate_limit;
	ec->tx_coalesce_usecs_high = vsi->int_rate_limit;

	return 0;
}

/**
 * i40e_get_coalesce - get a netdev's coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 *
 * Gets the coalesce settings for a particular netdev. Note that if user has
 * modified per-queue settings, this only guarantees to represent queue 0. See
 * __i40e_get_coalesce for more details.
 **/
static int i40e_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __i40e_get_coalesce(netdev, ec, -1);
}

/**
 * i40e_get_per_queue_coalesce - gets coalesce settings for particular queue
 * @netdev: netdev structure
 * @ec: ethtool's coalesce settings
 * @queue: the particular queue to read
 *
 * Will read a specific queue's coalesce settings
 **/
static int i40e_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __i40e_get_coalesce(netdev, ec, queue);
}

/**
 * i40e_set_itr_per_queue - set ITR values for specific queue
 * @vsi: the VSI to set values for
 * @ec: coalesce settings from ethtool
 * @queue: the queue to modify
 *
 * Change the ITR settings for a specific queue.
 **/

static void i40e_set_itr_per_queue(struct i40e_vsi *vsi,
				   struct ethtool_coalesce *ec,
				   int queue)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_q_vector *q_vector;
	u16 vector, intrl;

	intrl = i40e_intrl_usec_to_reg(vsi->int_rate_limit);

	vsi->rx_rings[queue]->rx_itr_setting = ec->rx_coalesce_usecs;
	vsi->tx_rings[queue]->tx_itr_setting = ec->tx_coalesce_usecs;

	if (ec->use_adaptive_rx_coalesce)
		vsi->rx_rings[queue]->rx_itr_setting |= I40E_ITR_DYNAMIC;
	else
		vsi->rx_rings[queue]->rx_itr_setting &= ~I40E_ITR_DYNAMIC;

	if (ec->use_adaptive_tx_coalesce)
		vsi->tx_rings[queue]->tx_itr_setting |= I40E_ITR_DYNAMIC;
	else
		vsi->tx_rings[queue]->tx_itr_setting &= ~I40E_ITR_DYNAMIC;

	q_vector = vsi->rx_rings[queue]->q_vector;
	q_vector->rx.itr = ITR_TO_REG(vsi->rx_rings[queue]->rx_itr_setting);
	vector = vsi->base_vector + q_vector->v_idx;
	wr32(hw, I40E_PFINT_ITRN(I40E_RX_ITR, vector - 1), q_vector->rx.itr);

	q_vector = vsi->tx_rings[queue]->q_vector;
	q_vector->tx.itr = ITR_TO_REG(vsi->tx_rings[queue]->tx_itr_setting);
	vector = vsi->base_vector + q_vector->v_idx;
	wr32(hw, I40E_PFINT_ITRN(I40E_TX_ITR, vector - 1), q_vector->tx.itr);

	wr32(hw, I40E_PFINT_RATEN(vector - 1), intrl);
	i40e_flush(hw);
}

/**
 * __i40e_set_coalesce - set coalesce settings for particular queue
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 * @queue: the queue to change
 *
 * Sets the coalesce settings for a particular queue.
 **/
static int __i40e_set_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec,
			       int queue)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u16 intrl_reg;
	int i;

	if (ec->tx_max_coalesced_frames_irq || ec->rx_max_coalesced_frames_irq)
		vsi->work_limit = ec->tx_max_coalesced_frames_irq;

	/* tx_coalesce_usecs_high is ignored, use rx-usecs-high instead */
	if (ec->tx_coalesce_usecs_high != vsi->int_rate_limit) {
		netif_info(pf, drv, netdev, "tx-usecs-high is not used, please program rx-usecs-high\n");
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs_high > INTRL_REG_TO_USEC(I40E_MAX_INTRL)) {
		netif_info(pf, drv, netdev, "Invalid value, rx-usecs-high range is 0-%lu\n",
			   INTRL_REG_TO_USEC(I40E_MAX_INTRL));
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs == 0) {
		if (ec->use_adaptive_rx_coalesce)
			netif_info(pf, drv, netdev, "rx-usecs=0, need to disable adaptive-rx for a complete disable\n");
	} else if ((ec->rx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		   (ec->rx_coalesce_usecs > (I40E_MAX_ITR << 1))) {
			netif_info(pf, drv, netdev, "Invalid value, rx-usecs range is 0-8160\n");
			return -EINVAL;
	}

	intrl_reg = i40e_intrl_usec_to_reg(ec->rx_coalesce_usecs_high);
	vsi->int_rate_limit = INTRL_REG_TO_USEC(intrl_reg);
	if (vsi->int_rate_limit != ec->rx_coalesce_usecs_high) {
		netif_info(pf, drv, netdev, "Interrupt rate limit rounded down to %d\n",
			   vsi->int_rate_limit);
	}

	if (ec->tx_coalesce_usecs == 0) {
		if (ec->use_adaptive_tx_coalesce)
			netif_info(pf, drv, netdev, "tx-usecs=0, need to disable adaptive-tx for a complete disable\n");
	} else if ((ec->tx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		   (ec->tx_coalesce_usecs > (I40E_MAX_ITR << 1))) {
			netif_info(pf, drv, netdev, "Invalid value, tx-usecs range is 0-8160\n");
			return -EINVAL;
	}

	/* rx and tx usecs has per queue value. If user doesn't specify the queue,
	 * apply to all queues.
	 */
	if (queue < 0) {
		for (i = 0; i < vsi->num_queue_pairs; i++)
			i40e_set_itr_per_queue(vsi, ec, i);
	} else if (queue < vsi->num_queue_pairs) {
		i40e_set_itr_per_queue(vsi, ec, queue);
	} else {
		netif_info(pf, drv, netdev, "Invalid queue value, queue range is 0 - %d\n",
			   vsi->num_queue_pairs - 1);
		return -EINVAL;
	}

	return 0;
}

/**
 * i40e_set_coalesce - set coalesce settings for every queue on the netdev
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 *
 * This will set each queue to the same coalesce settings.
 **/
static int i40e_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __i40e_set_coalesce(netdev, ec, -1);
}

/**
 * i40e_set_per_queue_coalesce - set specific queue's coalesce settings
 * @netdev: the netdev to change
 * @ec: ethtool's coalesce settings
 * @queue: the queue to change
 *
 * Sets the specified queue's coalesce settings.
 **/
static int i40e_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __i40e_set_coalesce(netdev, ec, queue);
}

/**
 * i40e_get_rss_hash_opts - Get RSS hash Input Set for each flow type
 * @pf: pointer to the physical function struct
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow is supported, else Invalid Input.
 **/
static int i40e_get_rss_hash_opts(struct i40e_pf *pf, struct ethtool_rxnfc *cmd)
{
	struct i40e_hw *hw = &pf->hw;
	u8 flow_pctype = 0;
	u64 i_set = 0;

	cmd->data = 0;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		break;
	case UDP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		break;
	case TCP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		break;
	case UDP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		break;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		/* Default is src/dest for IP, no matter the L4 hashing */
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	/* Read flow based hash input set register */
	if (flow_pctype) {
		i_set = (u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(0,
					      flow_pctype)) |
			((u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(1,
					       flow_pctype)) << 32);
	}

	/* Process bits of hash input set */
	if (i_set) {
		if (i_set & I40E_L4_SRC_MASK)
			cmd->data |= RXH_L4_B_0_1;
		if (i_set & I40E_L4_DST_MASK)
			cmd->data |= RXH_L4_B_2_3;

		if (cmd->flow_type == TCP_V4_FLOW ||
		    cmd->flow_type == UDP_V4_FLOW) {
			if (i_set & I40E_L3_SRC_MASK)
				cmd->data |= RXH_IP_SRC;
			if (i_set & I40E_L3_DST_MASK)
				cmd->data |= RXH_IP_DST;
		} else if (cmd->flow_type == TCP_V6_FLOW ||
			  cmd->flow_type == UDP_V6_FLOW) {
			if (i_set & I40E_L3_V6_SRC_MASK)
				cmd->data |= RXH_IP_SRC;
			if (i_set & I40E_L3_V6_DST_MASK)
				cmd->data |= RXH_IP_DST;
		}
	}

	return 0;
}

/**
 * i40e_check_mask - Check whether a mask field is set
 * @mask: the full mask value
 * @field; mask of the field to check
 *
 * If the given mask is fully set, return positive value. If the mask for the
 * field is fully unset, return zero. Otherwise return a negative error code.
 **/
static int i40e_check_mask(u64 mask, u64 field)
{
	u64 value = mask & field;

	if (value == field)
		return 1;
	else if (!value)
		return 0;
	else
		return -1;
}

/**
 * i40e_parse_rx_flow_user_data - Deconstruct user-defined data
 * @fsp: pointer to rx flow specification
 * @data: pointer to userdef data structure for storage
 *
 * Read the user-defined data and deconstruct the value into a structure. No
 * other code should read the user-defined data, so as to ensure that every
 * place consistently reads the value correctly.
 *
 * The user-defined field is a 64bit Big Endian format value, which we
 * deconstruct by reading bits or bit fields from it. Single bit flags shall
 * be defined starting from the highest bits, while small bit field values
 * shall be defined starting from the lowest bits.
 *
 * Returns 0 if the data is valid, and non-zero if the userdef data is invalid
 * and the filter should be rejected. The data structure will always be
 * modified even if FLOW_EXT is not set.
 *
 **/
static int i40e_parse_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
					struct i40e_rx_flow_userdef *data)
{
	u64 value, mask;
	int valid;

	/* Zero memory first so it's always consistent. */
	memset(data, 0, sizeof(*data));

	if (!(fsp->flow_type & FLOW_EXT))
		return 0;

	value = be64_to_cpu(*((__be64 *)fsp->h_ext.data));
	mask = be64_to_cpu(*((__be64 *)fsp->m_ext.data));

#define I40E_USERDEF_FLEX_WORD		GENMASK_ULL(15, 0)
#define I40E_USERDEF_FLEX_OFFSET	GENMASK_ULL(31, 16)
#define I40E_USERDEF_FLEX_FILTER	GENMASK_ULL(31, 0)

	valid = i40e_check_mask(mask, I40E_USERDEF_FLEX_FILTER);
	if (valid < 0) {
		return -EINVAL;
	} else if (valid) {
		data->flex_word = value & I40E_USERDEF_FLEX_WORD;
		data->flex_offset =
			(value & I40E_USERDEF_FLEX_OFFSET) >> 16;
		data->flex_filter = true;
	}

	return 0;
}

/**
 * i40e_fill_rx_flow_user_data - Fill in user-defined data field
 * @fsp: pointer to rx_flow specification
 *
 * Reads the userdef data structure and properly fills in the user defined
 * fields of the rx_flow_spec.
 **/
static void i40e_fill_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
					struct i40e_rx_flow_userdef *data)
{
	u64 value = 0, mask = 0;

	if (data->flex_filter) {
		value |= data->flex_word;
		value |= (u64)data->flex_offset << 16;
		mask |= I40E_USERDEF_FLEX_FILTER;
	}

	if (value || mask)
		fsp->flow_type |= FLOW_EXT;

	*((__be64 *)fsp->h_ext.data) = cpu_to_be64(value);
	*((__be64 *)fsp->m_ext.data) = cpu_to_be64(mask);
}

/**
 * i40e_get_ethtool_fdir_all - Populates the rule count of a command
 * @pf: Pointer to the physical function struct
 * @cmd: The command to get or set Rx flow classification rules
 * @rule_locs: Array of used rule locations
 *
 * This function populates both the total and actual rule count of
 * the ethtool flow classification command
 *
 * Returns 0 on success or -EMSGSIZE if entry not found
 **/
static int i40e_get_ethtool_fdir_all(struct i40e_pf *pf,
				     struct ethtool_rxnfc *cmd,
				     u32 *rule_locs)
{
	struct i40e_fdir_filter *rule;
	struct hlist_node *node2;
	int cnt = 0;

	/* report total rule count */
	cmd->data = i40e_get_fd_cnt_all(pf);

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (cnt == cmd->rule_cnt)
			return -EMSGSIZE;

		rule_locs[cnt] = rule->fd_id;
		cnt++;
	}

	cmd->rule_cnt = cnt;

	return 0;
}

/**
 * i40e_get_ethtool_fdir_entry - Look up a filter based on Rx flow
 * @pf: Pointer to the physical function struct
 * @cmd: The command to get or set Rx flow classification rules
 *
 * This function looks up a filter based on the Rx flow classification
 * command and fills the flow spec info for it if found
 *
 * Returns 0 on success or -EINVAL if filter not found
 **/
static int i40e_get_ethtool_fdir_entry(struct i40e_pf *pf,
				       struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
			(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct i40e_rx_flow_userdef userdef = {0};
	struct i40e_fdir_filter *rule = NULL;
	struct hlist_node *node2;
	u64 input_set;
	u16 index;

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (fsp->location <= rule->fd_id)
			break;
	}

	if (!rule || fsp->location != rule->fd_id)
		return -EINVAL;

	fsp->flow_type = rule->flow_type;
	if (fsp->flow_type == IP_USER_FLOW) {
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.proto = 0;
	}

	/* Reverse the src and dest notion, since the HW views them from
	 * Tx perspective where as the user expects it from Rx filter view.
	 */
	fsp->h_u.tcp_ip4_spec.psrc = rule->dst_port;
	fsp->h_u.tcp_ip4_spec.pdst = rule->src_port;
	fsp->h_u.tcp_ip4_spec.ip4src = rule->dst_ip;
	fsp->h_u.tcp_ip4_spec.ip4dst = rule->src_ip;

	switch (rule->flow_type) {
	case SCTP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_SCTP;
		break;
	case TCP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		break;
	case UDP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		break;
	case IP_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_OTHER;
		break;
	default:
		/* If we have stored a filter with a flow type not listed here
		 * it is almost certainly a driver bug. WARN(), and then
		 * assign the input_set as if all fields are enabled to avoid
		 * reading unassigned memory.
		 */
		WARN(1, "Missing input set index for flow_type %d\n",
		     rule->flow_type);
		input_set = 0xFFFFFFFFFFFFFFFFULL;
		goto no_input_set;
	}

	input_set = i40e_read_fd_input_set(pf, index);

no_input_set:
	if (input_set & I40E_L3_SRC_MASK)
		fsp->m_u.tcp_ip4_spec.ip4src = htonl(0xFFFF);

	if (input_set & I40E_L3_DST_MASK)
		fsp->m_u.tcp_ip4_spec.ip4dst = htonl(0xFFFF);

	if (input_set & I40E_L4_SRC_MASK)
		fsp->m_u.tcp_ip4_spec.psrc = htons(0xFFFFFFFF);

	if (input_set & I40E_L4_DST_MASK)
		fsp->m_u.tcp_ip4_spec.pdst = htons(0xFFFFFFFF);

	if (rule->dest_ctl == I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->q_index;

	if (rule->dest_vsi != pf->vsi[pf->lan_vsi]->id) {
		struct i40e_vsi *vsi;

		vsi = i40e_find_vsi_from_id(pf, rule->dest_vsi);
		if (vsi && vsi->type == I40E_VSI_SRIOV) {
			/* VFs are zero-indexed by the driver, but ethtool
			 * expects them to be one-indexed, so add one here
			 */
			u64 ring_vf = vsi->vf_id + 1;

			ring_vf <<= ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
			fsp->ring_cookie |= ring_vf;
		}
	}

	if (rule->flex_filter) {
		userdef.flex_filter = true;
		userdef.flex_word = be16_to_cpu(rule->flex_word);
		userdef.flex_offset = rule->flex_offset;
	}

	i40e_fill_rx_flow_user_data(fsp, &userdef);

	return 0;
}

/**
 * i40e_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the command is supported.
 **/
static int i40e_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = vsi->num_queue_pairs;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		ret = i40e_get_rss_hash_opts(pf, cmd);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = pf->fdir_pf_active_filters;
		/* report total rule count */
		cmd->data = i40e_get_fd_cnt_all(pf);
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = i40e_get_ethtool_fdir_entry(pf, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = i40e_get_ethtool_fdir_all(pf, cmd, rule_locs);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * i40e_get_rss_hash_bits - Read RSS Hash bits from register
 * @nfc: pointer to user request
 * @i_setc bits currently set
 *
 * Returns value of bits to be set per user request
 **/
static u64 i40e_get_rss_hash_bits(struct ethtool_rxnfc *nfc, u64 i_setc)
{
	u64 i_set = i_setc;
	u64 src_l3 = 0, dst_l3 = 0;

	if (nfc->data & RXH_L4_B_0_1)
		i_set |= I40E_L4_SRC_MASK;
	else
		i_set &= ~I40E_L4_SRC_MASK;
	if (nfc->data & RXH_L4_B_2_3)
		i_set |= I40E_L4_DST_MASK;
	else
		i_set &= ~I40E_L4_DST_MASK;

	if (nfc->flow_type == TCP_V6_FLOW || nfc->flow_type == UDP_V6_FLOW) {
		src_l3 = I40E_L3_V6_SRC_MASK;
		dst_l3 = I40E_L3_V6_DST_MASK;
	} else if (nfc->flow_type == TCP_V4_FLOW ||
		  nfc->flow_type == UDP_V4_FLOW) {
		src_l3 = I40E_L3_SRC_MASK;
		dst_l3 = I40E_L3_DST_MASK;
	} else {
		/* Any other flow type are not supported here */
		return i_set;
	}

	if (nfc->data & RXH_IP_SRC)
		i_set |= src_l3;
	else
		i_set &= ~src_l3;
	if (nfc->data & RXH_IP_DST)
		i_set |= dst_l3;
	else
		i_set &= ~dst_l3;

	return i_set;
}

/**
 * i40e_set_rss_hash_opt - Enable/Disable flow types for RSS hash
 * @pf: pointer to the physical function struct
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 **/
static int i40e_set_rss_hash_opt(struct i40e_pf *pf, struct ethtool_rxnfc *nfc)
{
	struct i40e_hw *hw = &pf->hw;
	u64 hena = (u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(0)) |
		   ((u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(1)) << 32);
	u8 flow_pctype = 0;
	u64 i_set, i_setc;

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		if (pf->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK);
		break;
	case TCP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		if (pf->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK);
		if (pf->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK);
		break;
	case UDP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		if (pf->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) |
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP);

		hena |= BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4);
		break;
	case UDP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		if (pf->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) |
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP);

		hena |= BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6);
		break;
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V4_FLOW:
		if ((nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER);
		break;
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
		if ((nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER);
		break;
	case IPV4_FLOW:
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER) |
			BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4);
		break;
	case IPV6_FLOW:
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER) |
			BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6);
		break;
	default:
		return -EINVAL;
	}

	if (flow_pctype) {
		i_setc = (u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(0,
					       flow_pctype)) |
			((u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(1,
					       flow_pctype)) << 32);
		i_set = i40e_get_rss_hash_bits(nfc, i_setc);
		i40e_write_rx_ctl(hw, I40E_GLQF_HASH_INSET(0, flow_pctype),
				  (u32)i_set);
		i40e_write_rx_ctl(hw, I40E_GLQF_HASH_INSET(1, flow_pctype),
				  (u32)(i_set >> 32));
		hena |= BIT_ULL(flow_pctype);
	}

	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(0), (u32)hena);
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(1), (u32)(hena >> 32));
	i40e_flush(hw);

	return 0;
}

/**
 * i40e_update_ethtool_fdir_entry - Updates the fdir filter entry
 * @vsi: Pointer to the targeted VSI
 * @input: The filter to update or NULL to indicate deletion
 * @sw_idx: Software index to the filter
 * @cmd: The command to get or set Rx flow classification rules
 *
 * This function updates (or deletes) a Flow Director entry from
 * the hlist of the corresponding PF
 *
 * Returns 0 on success
 **/
static int i40e_update_ethtool_fdir_entry(struct i40e_vsi *vsi,
					  struct i40e_fdir_filter *input,
					  u16 sw_idx,
					  struct ethtool_rxnfc *cmd)
{
	struct i40e_fdir_filter *rule, *parent;
	struct i40e_pf *pf = vsi->back;
	struct hlist_node *node2;
	int err = -EINVAL;

	parent = NULL;
	rule = NULL;

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		/* hash found, or no matching entry */
		if (rule->fd_id >= sw_idx)
			break;
		parent = rule;
	}

	/* if there is an old rule occupying our place remove it */
	if (rule && (rule->fd_id == sw_idx)) {
		/* Remove this rule, since we're either deleting it, or
		 * replacing it.
		 */
		err = i40e_add_del_fdir(vsi, rule, false);
		hlist_del(&rule->fdir_node);
		kfree(rule);
		pf->fdir_pf_active_filters--;
	}

	/* If we weren't given an input, this is a delete, so just return the
	 * error code indicating if there was an entry at the requested slot
	 */
	if (!input)
		return err;

	/* Otherwise, install the new rule as requested */
	INIT_HLIST_NODE(&input->fdir_node);

	/* add filter to the list */
	if (parent)
		hlist_add_behind(&input->fdir_node, &parent->fdir_node);
	else
		hlist_add_head(&input->fdir_node,
			       &pf->fdir_filter_list);

	/* update counts */
	pf->fdir_pf_active_filters++;

	return 0;
}

/**
 * i40e_prune_flex_pit_list - Cleanup unused entries in FLX_PIT table
 * @pf: pointer to PF structure
 *
 * This function searches the list of filters and determines which FLX_PIT
 * entries are still required. It will prune any entries which are no longer
 * in use after the deletion.
 **/
static void i40e_prune_flex_pit_list(struct i40e_pf *pf)
{
	struct i40e_flex_pit *entry, *tmp;
	struct i40e_fdir_filter *rule;

	/* First, we'll check the l3 table */
	list_for_each_entry_safe(entry, tmp, &pf->l3_flex_pit_list, list) {
		bool found = false;

		hlist_for_each_entry(rule, &pf->fdir_filter_list, fdir_node) {
			if (rule->flow_type != IP_USER_FLOW)
				continue;
			if (rule->flex_filter &&
			    rule->flex_offset == entry->src_offset) {
				found = true;
				break;
			}
		}

		/* If we didn't find the filter, then we can prune this entry
		 * from the list.
		 */
		if (!found) {
			list_del(&entry->list);
			kfree(entry);
		}
	}

	/* Followed by the L4 table */
	list_for_each_entry_safe(entry, tmp, &pf->l4_flex_pit_list, list) {
		bool found = false;

		hlist_for_each_entry(rule, &pf->fdir_filter_list, fdir_node) {
			/* Skip this filter if it's L3, since we already
			 * checked those in the above loop
			 */
			if (rule->flow_type == IP_USER_FLOW)
				continue;
			if (rule->flex_filter &&
			    rule->flex_offset == entry->src_offset) {
				found = true;
				break;
			}
		}

		/* If we didn't find the filter, then we can prune this entry
		 * from the list.
		 */
		if (!found) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
}

/**
 * i40e_del_fdir_entry - Deletes a Flow Director filter entry
 * @vsi: Pointer to the targeted VSI
 * @cmd: The command to get or set Rx flow classification rules
 *
 * The function removes a Flow Director filter entry from the
 * hlist of the corresponding PF
 *
 * Returns 0 on success
 */
static int i40e_del_fdir_entry(struct i40e_vsi *vsi,
			       struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct i40e_pf *pf = vsi->back;
	int ret = 0;

	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		return -EBUSY;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		return -EBUSY;

	ret = i40e_update_ethtool_fdir_entry(vsi, NULL, fsp->location, cmd);

	i40e_prune_flex_pit_list(pf);

	i40e_fdir_check_and_reenable(pf);
	return ret;
}

/**
 * i40e_unused_pit_index - Find an unused PIT index for given list
 * @pf: the PF data structure
 *
 * Find the first unused flexible PIT index entry. We search both the L3 and
 * L4 flexible PIT lists so that the returned index is unique and unused by
 * either currently programmed L3 or L4 filters. We use a bit field as storage
 * to track which indexes are already used.
 **/
static u8 i40e_unused_pit_index(struct i40e_pf *pf)
{
	unsigned long available_index = 0xFF;
	struct i40e_flex_pit *entry;

	/* We need to make sure that the new index isn't in use by either L3
	 * or L4 filters so that IP_USER_FLOW filters can program both L3 and
	 * L4 to use the same index.
	 */

	list_for_each_entry(entry, &pf->l4_flex_pit_list, list)
		clear_bit(entry->pit_index, &available_index);

	list_for_each_entry(entry, &pf->l3_flex_pit_list, list)
		clear_bit(entry->pit_index, &available_index);

	return find_first_bit(&available_index, 8);
}

/**
 * i40e_find_flex_offset - Find an existing flex src_offset
 * @flex_pit_list: L3 or L4 flex PIT list
 * @src_offset: new src_offset to find
 *
 * Searches the flex_pit_list for an existing offset. If no offset is
 * currently programmed, then this will return an ERR_PTR if there is no space
 * to add a new offset, otherwise it returns NULL.
 **/
static
struct i40e_flex_pit *i40e_find_flex_offset(struct list_head *flex_pit_list,
					    u16 src_offset)
{
	struct i40e_flex_pit *entry;
	int size = 0;

	/* Search for the src_offset first. If we find a matching entry
	 * already programmed, we can simply re-use it.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		size++;
		if (entry->src_offset == src_offset)
			return entry;
	}

	/* If we haven't found an entry yet, then the provided src offset has
	 * not yet been programmed. We will program the src offset later on,
	 * but we need to indicate whether there is enough space to do so
	 * here. We'll make use of ERR_PTR for this purpose.
	 */
	if (size >= I40E_FLEX_PIT_TABLE_SIZE)
		return ERR_PTR(-ENOSPC);

	return NULL;
}

/**
 * i40e_add_flex_offset - Add src_offset to flex PIT table list
 * @flex_pit_list: L3 or L4 flex PIT list
 * @src_offset: new src_offset to add
 * @pit_index: the PIT index to program
 *
 * This function programs the new src_offset to the list. It is expected that
 * i40e_find_flex_offset has already been tried and returned NULL, indicating
 * that this offset is not programmed, and that the list has enough space to
 * store another offset.
 *
 * Returns 0 on success, and negative value on error.
 **/
static int i40e_add_flex_offset(struct list_head *flex_pit_list,
				u16 src_offset,
				u8 pit_index)
{
	struct i40e_flex_pit *new_pit, *entry;

	new_pit = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!new_pit)
		return -ENOMEM;

	new_pit->src_offset = src_offset;
	new_pit->pit_index = pit_index;

	/* We need to insert this item such that the list is sorted by
	 * src_offset in ascending order.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		if (new_pit->src_offset < entry->src_offset) {
			list_add_tail(&new_pit->list, &entry->list);
			return 0;
		}

		/* If we found an entry with our offset already programmed we
		 * can simply return here, after freeing the memory. However,
		 * if the pit_index does not match we need to report an error.
		 */
		if (new_pit->src_offset == entry->src_offset) {
			int err = 0;

			/* If the PIT index is not the same we can't re-use
			 * the entry, so we must report an error.
			 */
			if (new_pit->pit_index != entry->pit_index)
				err = -EINVAL;

			kfree(new_pit);
			return err;
		}
	}

	/* If we reached here, then we haven't yet added the item. This means
	 * that we should add the item at the end of the list.
	 */
	list_add_tail(&new_pit->list, flex_pit_list);
	return 0;
}

/**
 * __i40e_reprogram_flex_pit - Re-program specific FLX_PIT table
 * @pf: Pointer to the PF structure
 * @flex_pit_list: list of flexible src offsets in use
 * #flex_pit_start: index to first entry for this section of the table
 *
 * In order to handle flexible data, the hardware uses a table of values
 * called the FLX_PIT table. This table is used to indicate which sections of
 * the input correspond to what PIT index values. Unfortunately, hardware is
 * very restrictive about programming this table. Entries must be ordered by
 * src_offset in ascending order, without duplicates. Additionally, unused
 * entries must be set to the unused index value, and must have valid size and
 * length according to the src_offset ordering.
 *
 * This function will reprogram the FLX_PIT register from a book-keeping
 * structure that we guarantee is already ordered correctly, and has no more
 * than 3 entries.
 *
 * To make things easier, we only support flexible values of one word length,
 * rather than allowing variable length flexible values.
 **/
static void __i40e_reprogram_flex_pit(struct i40e_pf *pf,
				      struct list_head *flex_pit_list,
				      int flex_pit_start)
{
	struct i40e_flex_pit *entry = NULL;
	u16 last_offset = 0;
	int i = 0, j = 0;

	/* First, loop over the list of flex PIT entries, and reprogram the
	 * registers.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		/* We have to be careful when programming values for the
		 * largest SRC_OFFSET value. It is possible that adding
		 * additional empty values at the end would overflow the space
		 * for the SRC_OFFSET in the FLX_PIT register. To avoid this,
		 * we check here and add the empty values prior to adding the
		 * largest value.
		 *
		 * To determine this, we will use a loop from i+1 to 3, which
		 * will determine whether the unused entries would have valid
		 * SRC_OFFSET. Note that there cannot be extra entries past
		 * this value, because the only valid values would have been
		 * larger than I40E_MAX_FLEX_SRC_OFFSET, and thus would not
		 * have been added to the list in the first place.
		 */
		for (j = i + 1; j < 3; j++) {
			u16 offset = entry->src_offset + j;
			int index = flex_pit_start + i;
			u32 value = I40E_FLEX_PREP_VAL(I40E_FLEX_DEST_UNUSED,
						       1,
						       offset - 3);

			if (offset > I40E_MAX_FLEX_SRC_OFFSET) {
				i40e_write_rx_ctl(&pf->hw,
						  I40E_PRTQF_FLX_PIT(index),
						  value);
				i++;
			}
		}

		/* Now, we can program the actual value into the table */
		i40e_write_rx_ctl(&pf->hw,
				  I40E_PRTQF_FLX_PIT(flex_pit_start + i),
				  I40E_FLEX_PREP_VAL(entry->pit_index + 50,
						     1,
						     entry->src_offset));
		i++;
	}

	/* In order to program the last entries in the table, we need to
	 * determine the valid offset. If the list is empty, we'll just start
	 * with 0. Otherwise, we'll start with the last item offset and add 1.
	 * This ensures that all entries have valid sizes. If we don't do this
	 * correctly, the hardware will disable flexible field parsing.
	 */
	if (!list_empty(flex_pit_list))
		last_offset = list_prev_entry(entry, list)->src_offset + 1;

	for (; i < 3; i++, last_offset++) {
		i40e_write_rx_ctl(&pf->hw,
				  I40E_PRTQF_FLX_PIT(flex_pit_start + i),
				  I40E_FLEX_PREP_VAL(I40E_FLEX_DEST_UNUSED,
						     1,
						     last_offset));
	}
}

/**
 * i40e_reprogram_flex_pit - Reprogram all FLX_PIT tables after input set change
 * @pf: pointer to the PF structure
 *
 * This function reprograms both the L3 and L4 FLX_PIT tables. See the
 * internal helper function for implementation details.
 **/
static void i40e_reprogram_flex_pit(struct i40e_pf *pf)
{
	__i40e_reprogram_flex_pit(pf, &pf->l3_flex_pit_list,
				  I40E_FLEX_PIT_IDX_START_L3);

	__i40e_reprogram_flex_pit(pf, &pf->l4_flex_pit_list,
				  I40E_FLEX_PIT_IDX_START_L4);

	/* We also need to program the L3 and L4 GLQF ORT register */
	i40e_write_rx_ctl(&pf->hw,
			  I40E_GLQF_ORT(I40E_L3_GLQF_ORT_IDX),
			  I40E_ORT_PREP_VAL(I40E_FLEX_PIT_IDX_START_L3,
					    3, 1));

	i40e_write_rx_ctl(&pf->hw,
			  I40E_GLQF_ORT(I40E_L4_GLQF_ORT_IDX),
			  I40E_ORT_PREP_VAL(I40E_FLEX_PIT_IDX_START_L4,
					    3, 1));
}

/**
 * i40e_flow_str - Converts a flow_type into a human readable string
 * @flow_type: the flow type from a flow specification
 *
 * Currently only flow types we support are included here, and the string
 * value attempts to match what ethtool would use to configure this flow type.
 **/
static const char *i40e_flow_str(struct ethtool_rx_flow_spec *fsp)
{
	switch (fsp->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		return "tcp4";
	case UDP_V4_FLOW:
		return "udp4";
	case SCTP_V4_FLOW:
		return "sctp4";
	case IP_USER_FLOW:
		return "ip4";
	default:
		return "unknown";
	}
}

/**
 * i40e_pit_index_to_mask - Return the FLEX mask for a given PIT index
 * @pit_index: PIT index to convert
 *
 * Returns the mask for a given PIT index. Will return 0 if the pit_index is
 * of range.
 **/
static u64 i40e_pit_index_to_mask(int pit_index)
{
	switch (pit_index) {
	case 0:
		return I40E_FLEX_50_MASK;
	case 1:
		return I40E_FLEX_51_MASK;
	case 2:
		return I40E_FLEX_52_MASK;
	case 3:
		return I40E_FLEX_53_MASK;
	case 4:
		return I40E_FLEX_54_MASK;
	case 5:
		return I40E_FLEX_55_MASK;
	case 6:
		return I40E_FLEX_56_MASK;
	case 7:
		return I40E_FLEX_57_MASK;
	default:
		return 0;
	}
}

/**
 * i40e_print_input_set - Show changes between two input sets
 * @vsi: the vsi being configured
 * @old: the old input set
 * @new: the new input set
 *
 * Print the difference between old and new input sets by showing which series
 * of words are toggled on or off. Only displays the bits we actually support
 * changing.
 **/
static void i40e_print_input_set(struct i40e_vsi *vsi, u64 old, u64 new)
{
	struct i40e_pf *pf = vsi->back;
	bool old_value, new_value;
	int i;

	old_value = !!(old & I40E_L3_SRC_MASK);
	new_value = !!(new & I40E_L3_SRC_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L3 source address: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L3_DST_MASK);
	new_value = !!(new & I40E_L3_DST_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L3 destination address: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L4_SRC_MASK);
	new_value = !!(new & I40E_L4_SRC_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L4 source port: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L4_DST_MASK);
	new_value = !!(new & I40E_L4_DST_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L4 destination port: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_VERIFY_TAG_MASK);
	new_value = !!(new & I40E_VERIFY_TAG_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "SCTP verification tag: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	/* Show change of flexible filter entries */
	for (i = 0; i < I40E_FLEX_INDEX_ENTRIES; i++) {
		u64 flex_mask = i40e_pit_index_to_mask(i);

		old_value = !!(old & flex_mask);
		new_value = !!(new & flex_mask);
		if (old_value != new_value)
			netif_info(pf, drv, vsi->netdev, "FLEX index %d: %s -> %s\n",
				   i,
				   old_value ? "ON" : "OFF",
				   new_value ? "ON" : "OFF");
	}

	netif_info(pf, drv, vsi->netdev, "  Current input set: %0llx\n",
		   old);
	netif_info(pf, drv, vsi->netdev, "Requested input set: %0llx\n",
		   new);
}

/**
 * i40e_check_fdir_input_set - Check that a given rx_flow_spec mask is valid
 * @vsi: pointer to the targeted VSI
 * @fsp: pointer to Rx flow specification
 * @userdef: userdefined data from flow specification
 *
 * Ensures that a given ethtool_rx_flow_spec has a valid mask. Some support
 * for partial matches exists with a few limitations. First, hardware only
 * supports masking by word boundary (2 bytes) and not per individual bit.
 * Second, hardware is limited to using one mask for a flow type and cannot
 * use a separate mask for each filter.
 *
 * To support these limitations, if we already have a configured filter for
 * the specified type, this function enforces that new filters of the type
 * match the configured input set. Otherwise, if we do not have a filter of
 * the specified type, we allow the input set to be updated to match the
 * desired filter.
 *
 * To help ensure that administrators understand why filters weren't displayed
 * as supported, we print a diagnostic message displaying how the input set
 * would change and warning to delete the preexisting filters if required.
 *
 * Returns 0 on successful input set match, and a negative return code on
 * failure.
 **/
static int i40e_check_fdir_input_set(struct i40e_vsi *vsi,
				     struct ethtool_rx_flow_spec *fsp,
				     struct i40e_rx_flow_userdef *userdef)
{
	struct i40e_pf *pf = vsi->back;
	struct ethtool_tcpip4_spec *tcp_ip4_spec;
	struct ethtool_usrip4_spec *usr_ip4_spec;
	u64 current_mask, new_mask;
	bool new_flex_offset = false;
	bool flex_l3 = false;
	u16 *fdir_filter_count;
	u16 index, src_offset = 0;
	u8 pit_index = 0;
	int err;

	switch (fsp->flow_type & ~FLOW_EXT) {
	case SCTP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_SCTP;
		fdir_filter_count = &pf->fd_sctp4_filter_cnt;
		break;
	case TCP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		fdir_filter_count = &pf->fd_tcp4_filter_cnt;
		break;
	case UDP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		fdir_filter_count = &pf->fd_udp4_filter_cnt;
		break;
	case IP_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_OTHER;
		fdir_filter_count = &pf->fd_ip4_filter_cnt;
		flex_l3 = true;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Read the current input set from register memory. */
	current_mask = i40e_read_fd_input_set(pf, index);
	new_mask = current_mask;

	/* Determine, if any, the required changes to the input set in order
	 * to support the provided mask.
	 *
	 * Hardware only supports masking at word (2 byte) granularity and does
	 * not support full bitwise masking. This implementation simplifies
	 * even further and only supports fully enabled or fully disabled
	 * masks for each field, even though we could split the ip4src and
	 * ip4dst fields.
	 */
	switch (fsp->flow_type & ~FLOW_EXT) {
	case SCTP_V4_FLOW:
		new_mask &= ~I40E_VERIFY_TAG_MASK;
		/* Fall through */
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		tcp_ip4_spec = &fsp->m_u.tcp_ip4_spec;

		/* IPv4 source address */
		if (tcp_ip4_spec->ip4src == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_SRC_MASK;
		else if (!tcp_ip4_spec->ip4src)
			new_mask &= ~I40E_L3_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* IPv4 destination address */
		if (tcp_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_DST_MASK;
		else if (!tcp_ip4_spec->ip4dst)
			new_mask &= ~I40E_L3_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 source port */
		if (tcp_ip4_spec->psrc == htons(0xFFFF))
			new_mask |= I40E_L4_SRC_MASK;
		else if (!tcp_ip4_spec->psrc)
			new_mask &= ~I40E_L4_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 destination port */
		if (tcp_ip4_spec->pdst == htons(0xFFFF))
			new_mask |= I40E_L4_DST_MASK;
		else if (!tcp_ip4_spec->pdst)
			new_mask &= ~I40E_L4_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* Filtering on Type of Service is not supported. */
		if (tcp_ip4_spec->tos)
			return -EOPNOTSUPP;

		break;
	case IP_USER_FLOW:
		usr_ip4_spec = &fsp->m_u.usr_ip4_spec;

		/* IPv4 source address */
		if (usr_ip4_spec->ip4src == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_SRC_MASK;
		else if (!usr_ip4_spec->ip4src)
			new_mask &= ~I40E_L3_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* IPv4 destination address */
		if (usr_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_DST_MASK;
		else if (!usr_ip4_spec->ip4dst)
			new_mask &= ~I40E_L3_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* First 4 bytes of L4 header */
		if (usr_ip4_spec->l4_4_bytes == htonl(0xFFFFFFFF))
			new_mask |= I40E_L4_SRC_MASK | I40E_L4_DST_MASK;
		else if (!usr_ip4_spec->l4_4_bytes)
			new_mask &= ~(I40E_L4_SRC_MASK | I40E_L4_DST_MASK);
		else
			return -EOPNOTSUPP;

		/* Filtering on Type of Service is not supported. */
		if (usr_ip4_spec->tos)
			return -EOPNOTSUPP;

		/* Filtering on IP version is not supported */
		if (usr_ip4_spec->ip_ver)
			return -EINVAL;

		/* Filtering on L4 protocol is not supported */
		if (usr_ip4_spec->proto)
			return -EINVAL;

		break;
	default:
		return -EOPNOTSUPP;
	}

	/* First, clear all flexible filter entries */
	new_mask &= ~I40E_FLEX_INPUT_MASK;

	/* If we have a flexible filter, try to add this offset to the correct
	 * flexible filter PIT list. Once finished, we can update the mask.
	 * If the src_offset changed, we will get a new mask value which will
	 * trigger an input set change.
	 */
	if (userdef->flex_filter) {
		struct i40e_flex_pit *l3_flex_pit = NULL, *flex_pit = NULL;

		/* Flexible offset must be even, since the flexible payload
		 * must be aligned on 2-byte boundary.
		 */
		if (userdef->flex_offset & 0x1) {
			dev_warn(&pf->pdev->dev,
				 "Flexible data offset must be 2-byte aligned\n");
			return -EINVAL;
		}

		src_offset = userdef->flex_offset >> 1;

		/* FLX_PIT source offset value is only so large */
		if (src_offset > I40E_MAX_FLEX_SRC_OFFSET) {
			dev_warn(&pf->pdev->dev,
				 "Flexible data must reside within first 64 bytes of the packet payload\n");
			return -EINVAL;
		}

		/* See if this offset has already been programmed. If we get
		 * an ERR_PTR, then the filter is not safe to add. Otherwise,
		 * if we get a NULL pointer, this means we will need to add
		 * the offset.
		 */
		flex_pit = i40e_find_flex_offset(&pf->l4_flex_pit_list,
						 src_offset);
		if (IS_ERR(flex_pit))
			return PTR_ERR(flex_pit);

		/* IP_USER_FLOW filters match both L4 (ICMP) and L3 (unknown)
		 * packet types, and thus we need to program both L3 and L4
		 * flexible values. These must have identical flexible index,
		 * as otherwise we can't correctly program the input set. So
		 * we'll find both an L3 and L4 index and make sure they are
		 * the same.
		 */
		if (flex_l3) {
			l3_flex_pit =
				i40e_find_flex_offset(&pf->l3_flex_pit_list,
						      src_offset);
			if (IS_ERR(l3_flex_pit))
				return PTR_ERR(l3_flex_pit);

			if (flex_pit) {
				/* If we already had a matching L4 entry, we
				 * need to make sure that the L3 entry we
				 * obtained uses the same index.
				 */
				if (l3_flex_pit) {
					if (l3_flex_pit->pit_index !=
					    flex_pit->pit_index) {
						return -EINVAL;
					}
				} else {
					new_flex_offset = true;
				}
			} else {
				flex_pit = l3_flex_pit;
			}
		}

		/* If we didn't find an existing flex offset, we need to
		 * program a new one. However, we don't immediately program it
		 * here because we will wait to program until after we check
		 * that it is safe to change the input set.
		 */
		if (!flex_pit) {
			new_flex_offset = true;
			pit_index = i40e_unused_pit_index(pf);
		} else {
			pit_index = flex_pit->pit_index;
		}

		/* Update the mask with the new offset */
		new_mask |= i40e_pit_index_to_mask(pit_index);
	}

	/* If the mask and flexible filter offsets for this filter match the
	 * currently programmed values we don't need any input set change, so
	 * this filter is safe to install.
	 */
	if (new_mask == current_mask && !new_flex_offset)
		return 0;

	netif_info(pf, drv, vsi->netdev, "Input set change requested for %s flows:\n",
		   i40e_flow_str(fsp));
	i40e_print_input_set(vsi, current_mask, new_mask);
	if (new_flex_offset) {
		netif_info(pf, drv, vsi->netdev, "FLEX index %d: Offset -> %d",
			   pit_index, src_offset);
	}

	/* Hardware input sets are global across multiple ports, so even the
	 * main port cannot change them when in MFP mode as this would impact
	 * any filters on the other ports.
	 */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		netif_err(pf, drv, vsi->netdev, "Cannot change Flow Director input sets while MFP is enabled\n");
		return -EOPNOTSUPP;
	}

	/* This filter requires us to update the input set. However, hardware
	 * only supports one input set per flow type, and does not support
	 * separate masks for each filter. This means that we can only support
	 * a single mask for all filters of a specific type.
	 *
	 * If we have preexisting filters, they obviously depend on the
	 * current programmed input set. Display a diagnostic message in this
	 * case explaining why the filter could not be accepted.
	 */
	if (*fdir_filter_count) {
		netif_err(pf, drv, vsi->netdev, "Cannot change input set for %s flows until %d preexisting filters are removed\n",
			  i40e_flow_str(fsp),
			  *fdir_filter_count);
		return -EOPNOTSUPP;
	}

	i40e_write_fd_input_set(pf, index, new_mask);

	/* Add the new offset and update table, if necessary */
	if (new_flex_offset) {
		err = i40e_add_flex_offset(&pf->l4_flex_pit_list, src_offset,
					   pit_index);
		if (err)
			return err;

		if (flex_l3) {
			err = i40e_add_flex_offset(&pf->l3_flex_pit_list,
						   src_offset,
						   pit_index);
			if (err)
				return err;
		}

		i40e_reprogram_flex_pit(pf);
	}

	return 0;
}

/**
 * i40e_add_fdir_ethtool - Add/Remove Flow Director filters
 * @vsi: pointer to the targeted VSI
 * @cmd: command to get or set RX flow classification rules
 *
 * Add Flow Director filters for a specific flow spec based on their
 * protocol.  Returns 0 if the filters were successfully added.
 **/
static int i40e_add_fdir_ethtool(struct i40e_vsi *vsi,
				 struct ethtool_rxnfc *cmd)
{
	struct i40e_rx_flow_userdef userdef;
	struct ethtool_rx_flow_spec *fsp;
	struct i40e_fdir_filter *input;
	u16 dest_vsi = 0, q_index = 0;
	struct i40e_pf *pf;
	int ret = -EINVAL;
	u8 dest_ctl;

	if (!vsi)
		return -EINVAL;
	pf = vsi->back;

	if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
		return -EOPNOTSUPP;

	if (pf->flags & I40E_FLAG_FD_SB_AUTO_DISABLED)
		return -ENOSPC;

	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		return -EBUSY;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		return -EBUSY;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	/* Parse the user-defined field */
	if (i40e_parse_rx_flow_user_data(fsp, &userdef))
		return -EINVAL;

	/* Extended MAC field is not supported */
	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	ret = i40e_check_fdir_input_set(vsi, fsp, &userdef);
	if (ret)
		return ret;

	if (fsp->location >= (pf->hw.func_caps.fd_filters_best_effort +
			      pf->hw.func_caps.fd_filters_guaranteed)) {
		return -EINVAL;
	}

	/* ring_cookie is either the drop index, or is a mask of the queue
	 * index and VF id we wish to target.
	 */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		dest_ctl = I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET;
	} else {
		u32 ring = ethtool_get_flow_spec_ring(fsp->ring_cookie);
		u8 vf = ethtool_get_flow_spec_ring_vf(fsp->ring_cookie);

		if (!vf) {
			if (ring >= vsi->num_queue_pairs)
				return -EINVAL;
			dest_vsi = vsi->id;
		} else {
			/* VFs are zero-indexed, so we subtract one here */
			vf--;

			if (vf >= pf->num_alloc_vfs)
				return -EINVAL;
			if (ring >= pf->vf[vf].num_queue_pairs)
				return -EINVAL;
			dest_vsi = pf->vf[vf].lan_vsi_id;
		}
		dest_ctl = I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_QINDEX;
		q_index = ring;
	}

	input = kzalloc(sizeof(*input), GFP_KERNEL);

	if (!input)
		return -ENOMEM;

	input->fd_id = fsp->location;
	input->q_index = q_index;
	input->dest_vsi = dest_vsi;
	input->dest_ctl = dest_ctl;
	input->fd_status = I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID;
	input->cnt_index  = I40E_FD_SB_STAT_IDX(pf->hw.pf_id);
	input->dst_ip = fsp->h_u.tcp_ip4_spec.ip4src;
	input->src_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
	input->flow_type = fsp->flow_type & ~FLOW_EXT;
	input->ip4_proto = fsp->h_u.usr_ip4_spec.proto;

	/* Reverse the src and dest notion, since the HW expects them to be from
	 * Tx perspective where as the input from user is from Rx filter view.
	 */
	input->dst_port = fsp->h_u.tcp_ip4_spec.psrc;
	input->src_port = fsp->h_u.tcp_ip4_spec.pdst;
	input->dst_ip = fsp->h_u.tcp_ip4_spec.ip4src;
	input->src_ip = fsp->h_u.tcp_ip4_spec.ip4dst;

	if (userdef.flex_filter) {
		input->flex_filter = true;
		input->flex_word = cpu_to_be16(userdef.flex_word);
		input->flex_offset = userdef.flex_offset;
	}

	ret = i40e_add_del_fdir(vsi, input, true);
	if (ret)
		goto free_input;

	/* Add the input filter to the fdir_input_list, possibly replacing
	 * a previous filter. Do not free the input structure after adding it
	 * to the list as this would cause a use-after-free bug.
	 */
	i40e_update_ethtool_fdir_entry(vsi, input, fsp->location, NULL);

	return 0;

free_input:
	kfree(input);
	return ret;
}

/**
 * i40e_set_rxnfc - command to set RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the command is supported.
 **/
static int i40e_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = i40e_set_rss_hash_opt(pf, cmd);
		break;
	case ETHTOOL_SRXCLSRLINS:
		ret = i40e_add_fdir_ethtool(vsi, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = i40e_del_fdir_entry(vsi, cmd);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * i40e_max_channels - get Max number of combined channels supported
 * @vsi: vsi pointer
 **/
static unsigned int i40e_max_channels(struct i40e_vsi *vsi)
{
	/* TODO: This code assumes DCB and FD is disabled for now. */
	return vsi->alloc_queue_pairs;
}

/**
 * i40e_get_channels - Get the current channels enabled and max supported etc.
 * @netdev: network interface device structure
 * @ch: ethtool channels structure
 *
 * We don't support separate tx and rx queues as channels. The other count
 * represents how many queues are being used for control. max_combined counts
 * how many queue pairs we can support. They may not be mapped 1 to 1 with
 * q_vectors since we support a lot more queue pairs than q_vectors.
 **/
static void i40e_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	/* report maximum channels */
	ch->max_combined = i40e_max_channels(vsi);

	/* report info for other vector */
	ch->other_count = (pf->flags & I40E_FLAG_FD_SB_ENABLED) ? 1 : 0;
	ch->max_other = ch->other_count;

	/* Note: This code assumes DCB is disabled for now. */
	ch->combined_count = vsi->num_queue_pairs;
}

/**
 * i40e_set_channels - Set the new channels count.
 * @netdev: network interface device structure
 * @ch: ethtool channels structure
 *
 * The new channels count may not be the same as requested by the user
 * since it gets rounded down to a power of 2 value.
 **/
static int i40e_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	const u8 drop = I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET;
	struct i40e_netdev_priv *np = netdev_priv(dev);
	unsigned int count = ch->combined_count;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_fdir_filter *rule;
	struct hlist_node *node2;
	int new_count;
	int err = 0;

	/* We do not support setting channels for any other VSI at present */
	if (vsi->type != I40E_VSI_MAIN)
		return -EINVAL;

	/* verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* verify other_count has not changed */
	if (ch->other_count != ((pf->flags & I40E_FLAG_FD_SB_ENABLED) ? 1 : 0))
		return -EINVAL;

	/* verify the number of channels does not exceed hardware limits */
	if (count > i40e_max_channels(vsi))
		return -EINVAL;

	/* verify that the number of channels does not invalidate any current
	 * flow director rules
	 */
	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (rule->dest_ctl != drop && count <= rule->q_index) {
			dev_warn(&pf->pdev->dev,
				 "Existing user defined filter %d assigns flow to queue %d\n",
				 rule->fd_id, rule->q_index);
			err = -EINVAL;
		}
	}

	if (err) {
		dev_err(&pf->pdev->dev,
			"Existing filter rules must be deleted to reduce combined channel count to %d\n",
			count);
		return err;
	}

	/* update feature limits from largest to smallest supported values */
	/* TODO: Flow director limit, DCB etc */

	/* use rss_reconfig to rebuild with new queue count and update traffic
	 * class queue mapping
	 */
	new_count = i40e_reconfig_rss_queues(pf, count);
	if (new_count > 0)
		return 0;
	else
		return -EINVAL;
}

/**
 * i40e_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40e_get_rxfh_key_size(struct net_device *netdev)
{
	return I40E_HKEY_ARRAY_SIZE;
}

/**
 * i40e_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40e_get_rxfh_indir_size(struct net_device *netdev)
{
	return I40E_HLUT_ARRAY_SIZE;
}

static int i40e_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	u8 *lut, *seed = NULL;
	int ret;
	u16 i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	seed = key;
	lut = kzalloc(I40E_HLUT_ARRAY_SIZE, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;
	ret = i40e_get_rss(vsi, seed, lut, I40E_HLUT_ARRAY_SIZE);
	if (ret)
		goto out;
	for (i = 0; i < I40E_HLUT_ARRAY_SIZE; i++)
		indir[i] = (u32)(lut[i]);

out:
	kfree(lut);

	return ret;
}

/**
 * i40e_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int i40e_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u8 *seed = NULL;
	u16 i;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (key) {
		if (!vsi->rss_hkey_user) {
			vsi->rss_hkey_user = kzalloc(I40E_HKEY_ARRAY_SIZE,
						     GFP_KERNEL);
			if (!vsi->rss_hkey_user)
				return -ENOMEM;
		}
		memcpy(vsi->rss_hkey_user, key, I40E_HKEY_ARRAY_SIZE);
		seed = vsi->rss_hkey_user;
	}
	if (!vsi->rss_lut_user) {
		vsi->rss_lut_user = kzalloc(I40E_HLUT_ARRAY_SIZE, GFP_KERNEL);
		if (!vsi->rss_lut_user)
			return -ENOMEM;
	}

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	if (indir)
		for (i = 0; i < I40E_HLUT_ARRAY_SIZE; i++)
			vsi->rss_lut_user[i] = (u8)(indir[i]);
	else
		i40e_fill_rss_lut(pf, vsi->rss_lut_user, I40E_HLUT_ARRAY_SIZE,
				  vsi->rss_size);

	return i40e_config_rss(vsi, seed, vsi->rss_lut_user,
			       I40E_HLUT_ARRAY_SIZE);
}

/**
 * i40e_get_priv_flags - report device private flags
 * @dev: network interface device structure
 *
 * The get string set count and the string set should be matched for each
 * flag returned.  Add new strings for each flag to the i40e_gstrings_priv_flags
 * array.
 *
 * Returns a u32 bitmap of flags.
 **/
static u32 i40e_get_priv_flags(struct net_device *dev)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 i, j, ret_flags = 0;

	for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gstrings_priv_flags[i];

		if (priv_flags->flag & pf->flags)
			ret_flags |= BIT(i);
	}

	if (pf->hw.pf_id != 0)
		return ret_flags;

	for (j = 0; j < I40E_GL_PRIV_FLAGS_STR_LEN; j++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gl_gstrings_priv_flags[j];

		if (priv_flags->flag & pf->flags)
			ret_flags |= BIT(i + j);
	}

	return ret_flags;
}

/**
 * i40e_set_priv_flags - set private flags
 * @dev: network interface device structure
 * @flags: bit flags to be set
 **/
static int i40e_set_priv_flags(struct net_device *dev, u32 flags)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u64 changed_flags;
	u32 i, j;

	changed_flags = pf->flags;

	for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gstrings_priv_flags[i];

		if (priv_flags->read_only)
			continue;

		if (flags & BIT(i))
			pf->flags |= priv_flags->flag;
		else
			pf->flags &= ~(priv_flags->flag);
	}

	if (pf->hw.pf_id != 0)
		goto flags_complete;

	for (j = 0; j < I40E_GL_PRIV_FLAGS_STR_LEN; j++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gl_gstrings_priv_flags[j];

		if (priv_flags->read_only)
			continue;

		if (flags & BIT(i + j))
			pf->flags |= priv_flags->flag;
		else
			pf->flags &= ~(priv_flags->flag);
	}

flags_complete:
	/* check for flags that changed */
	changed_flags ^= pf->flags;

	/* Process any additional changes needed as a result of flag changes.
	 * The changed_flags value reflects the list of bits that were
	 * changed in the code above.
	 */

	/* Flush current ATR settings if ATR was disabled */
	if ((changed_flags & I40E_FLAG_FD_ATR_ENABLED) &&
	    !(pf->flags & I40E_FLAG_FD_ATR_ENABLED)) {
		pf->flags |= I40E_FLAG_FD_ATR_AUTO_DISABLED;
		set_bit(__I40E_FD_FLUSH_REQUESTED, pf->state);
	}

	/* Only allow ATR evict on hardware that is capable of handling it */
	if (pf->flags & I40E_FLAG_HW_ATR_EVICT_CAPABLE)
		pf->flags &= ~I40E_FLAG_HW_ATR_EVICT_ENABLED;

	if (changed_flags & I40E_FLAG_TRUE_PROMISC_SUPPORT) {
		u16 sw_flags = 0, valid_flags = 0;
		int ret;

		if (!(pf->flags & I40E_FLAG_TRUE_PROMISC_SUPPORT))
			sw_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		valid_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		ret = i40e_aq_set_switch_config(&pf->hw, sw_flags, valid_flags,
						NULL);
		if (ret && pf->hw.aq.asq_last_status != I40E_AQ_RC_ESRCH) {
			dev_info(&pf->pdev->dev,
				 "couldn't set switch config bits, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			/* not a fatal problem, just keep going */
		}
	}

	/* Issue reset to cause things to take effect, as additional bits
	 * are added we will need to create a mask of bits requiring reset
	 */
	if ((changed_flags & I40E_FLAG_VEB_STATS_ENABLED) ||
	    ((changed_flags & I40E_FLAG_LEGACY_RX) && netif_running(dev)))
		i40e_do_reset(pf, BIT(__I40E_PF_RESET_REQUESTED), true);

	return 0;
}

static const struct ethtool_ops i40e_ethtool_ops = {
	.get_drvinfo		= i40e_get_drvinfo,
	.get_regs_len		= i40e_get_regs_len,
	.get_regs		= i40e_get_regs,
	.nway_reset		= i40e_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_wol		= i40e_get_wol,
	.set_wol		= i40e_set_wol,
	.set_eeprom		= i40e_set_eeprom,
	.get_eeprom_len		= i40e_get_eeprom_len,
	.get_eeprom		= i40e_get_eeprom,
	.get_ringparam		= i40e_get_ringparam,
	.set_ringparam		= i40e_set_ringparam,
	.get_pauseparam		= i40e_get_pauseparam,
	.set_pauseparam		= i40e_set_pauseparam,
	.get_msglevel		= i40e_get_msglevel,
	.set_msglevel		= i40e_set_msglevel,
	.get_rxnfc		= i40e_get_rxnfc,
	.set_rxnfc		= i40e_set_rxnfc,
	.self_test		= i40e_diag_test,
	.get_strings		= i40e_get_strings,
	.set_phys_id		= i40e_set_phys_id,
	.get_sset_count		= i40e_get_sset_count,
	.get_ethtool_stats	= i40e_get_ethtool_stats,
	.get_coalesce		= i40e_get_coalesce,
	.set_coalesce		= i40e_set_coalesce,
	.get_rxfh_key_size	= i40e_get_rxfh_key_size,
	.get_rxfh_indir_size	= i40e_get_rxfh_indir_size,
	.get_rxfh		= i40e_get_rxfh,
	.set_rxfh		= i40e_set_rxfh,
	.get_channels		= i40e_get_channels,
	.set_channels		= i40e_set_channels,
	.get_ts_info		= i40e_get_ts_info,
	.get_priv_flags		= i40e_get_priv_flags,
	.set_priv_flags		= i40e_set_priv_flags,
	.get_per_queue_coalesce	= i40e_get_per_queue_coalesce,
	.set_per_queue_coalesce	= i40e_set_per_queue_coalesce,
	.get_link_ksettings	= i40e_get_link_ksettings,
	.set_link_ksettings	= i40e_set_link_ksettings,
};

void i40e_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &i40e_ethtool_ops;
}

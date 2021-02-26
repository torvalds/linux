/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/netdevice.h>
#include "lan743x_main.h"
#include "lan743x_ethtool.h"
#include <linux/net_tstamp.h>
#include <linux/pci.h>
#include <linux/phy.h>

/* eeprom */
#define LAN743X_EEPROM_MAGIC		    (0x74A5)
#define LAN743X_OTP_MAGIC		    (0x74F3)
#define EEPROM_INDICATOR_1		    (0xA5)
#define EEPROM_INDICATOR_2		    (0xAA)
#define EEPROM_MAC_OFFSET		    (0x01)
#define MAX_EEPROM_SIZE			    512
#define OTP_INDICATOR_1			    (0xF3)
#define OTP_INDICATOR_2			    (0xF7)

static int lan743x_otp_write(struct lan743x_adapter *adapter, u32 offset,
			     u32 length, u8 *data)
{
	unsigned long timeout;
	u32 buf;
	int i;

	buf = lan743x_csr_read(adapter, OTP_PWR_DN);

	if (buf & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		lan743x_csr_write(adapter, OTP_PWR_DN, 0);

		timeout = jiffies + HZ;
		do {
			udelay(1);
			buf = lan743x_csr_read(adapter, OTP_PWR_DN);
			if (time_after(jiffies, timeout)) {
				netif_warn(adapter, drv, adapter->netdev,
					   "timeout on OTP_PWR_DN completion\n");
				return -EIO;
			}
		} while (buf & OTP_PWR_DN_PWRDN_N_);
	}

	/* set to BYTE program mode */
	lan743x_csr_write(adapter, OTP_PRGM_MODE, OTP_PRGM_MODE_BYTE_);

	for (i = 0; i < length; i++) {
		lan743x_csr_write(adapter, OTP_ADDR1,
				  ((offset + i) >> 8) &
				  OTP_ADDR1_15_11_MASK_);
		lan743x_csr_write(adapter, OTP_ADDR2,
				  ((offset + i) &
				  OTP_ADDR2_10_3_MASK_));
		lan743x_csr_write(adapter, OTP_PRGM_DATA, data[i]);
		lan743x_csr_write(adapter, OTP_TST_CMD, OTP_TST_CMD_PRGVRFY_);
		lan743x_csr_write(adapter, OTP_CMD_GO, OTP_CMD_GO_GO_);

		timeout = jiffies + HZ;
		do {
			udelay(1);
			buf = lan743x_csr_read(adapter, OTP_STATUS);
			if (time_after(jiffies, timeout)) {
				netif_warn(adapter, drv, adapter->netdev,
					   "Timeout on OTP_STATUS completion\n");
				return -EIO;
			}
		} while (buf & OTP_STATUS_BUSY_);
	}

	return 0;
}

static int lan743x_eeprom_wait(struct lan743x_adapter *adapter)
{
	unsigned long start_time = jiffies;
	u32 val;

	do {
		val = lan743x_csr_read(adapter, E2P_CMD);

		if (!(val & E2P_CMD_EPC_BUSY_) ||
		    (val & E2P_CMD_EPC_TIMEOUT_))
			break;
		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_EPC_TIMEOUT_ | E2P_CMD_EPC_BUSY_)) {
		netif_warn(adapter, drv, adapter->netdev,
			   "EEPROM read operation timeout\n");
		return -EIO;
	}

	return 0;
}

static int lan743x_eeprom_confirm_not_busy(struct lan743x_adapter *adapter)
{
	unsigned long start_time = jiffies;
	u32 val;

	do {
		val = lan743x_csr_read(adapter, E2P_CMD);

		if (!(val & E2P_CMD_EPC_BUSY_))
			return 0;

		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	netif_warn(adapter, drv, adapter->netdev, "EEPROM is busy\n");
	return -EIO;
}

static int lan743x_eeprom_read(struct lan743x_adapter *adapter,
			       u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	retval = lan743x_eeprom_confirm_not_busy(adapter);
	if (retval)
		return retval;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_READ_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, E2P_CMD, val);

		retval = lan743x_eeprom_wait(adapter);
		if (retval < 0)
			return retval;

		val = lan743x_csr_read(adapter, E2P_DATA);
		data[i] = val & 0xFF;
		offset++;
	}

	return 0;
}

static int lan743x_eeprom_write(struct lan743x_adapter *adapter,
				u32 offset, u32 length, u8 *data)
{
	int retval;
	u32 val;
	int i;

	retval = lan743x_eeprom_confirm_not_busy(adapter);
	if (retval)
		return retval;

	/* Issue write/erase enable command */
	val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_EWEN_;
	lan743x_csr_write(adapter, E2P_CMD, val);

	retval = lan743x_eeprom_wait(adapter);
	if (retval < 0)
		return retval;

	for (i = 0; i < length; i++) {
		/* Fill data register */
		val = data[i];
		lan743x_csr_write(adapter, E2P_DATA, val);

		/* Send "write" command */
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_WRITE_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		lan743x_csr_write(adapter, E2P_CMD, val);

		retval = lan743x_eeprom_wait(adapter);
		if (retval < 0)
			return retval;

		offset++;
	}

	return 0;
}

static void lan743x_ethtool_get_drvinfo(struct net_device *netdev,
					struct ethtool_drvinfo *info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	strlcpy(info->bus_info,
		pci_name(adapter->pdev), sizeof(info->bus_info));
}

static u32 lan743x_ethtool_get_msglevel(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void lan743x_ethtool_set_msglevel(struct net_device *netdev,
					 u32 msglevel)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = msglevel;
}

static int lan743x_ethtool_get_eeprom_len(struct net_device *netdev)
{
	return MAX_EEPROM_SIZE;
}

static int lan743x_ethtool_get_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return lan743x_eeprom_read(adapter, ee->offset, ee->len, data);
}

static int lan743x_ethtool_set_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int ret = -EINVAL;

	if (ee->magic == LAN743X_EEPROM_MAGIC)
		ret = lan743x_eeprom_write(adapter, ee->offset, ee->len,
					   data);
	/* Beware!  OTP is One Time Programming ONLY!
	 * So do some strict condition check before messing up
	 */
	else if ((ee->magic == LAN743X_OTP_MAGIC) &&
		 (ee->offset == 0) &&
		 (ee->len == MAX_EEPROM_SIZE) &&
		 (data[0] == OTP_INDICATOR_1))
		ret = lan743x_otp_write(adapter, ee->offset, ee->len, data);

	return ret;
}

static const char lan743x_set0_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX FCS Errors",
	"RX Alignment Errors",
	"Rx Fragment Errors",
	"RX Jabber Errors",
	"RX Undersize Frame Errors",
	"RX Oversize Frame Errors",
	"RX Dropped Frames",
	"RX Unicast Byte Count",
	"RX Broadcast Byte Count",
	"RX Multicast Byte Count",
	"RX Unicast Frames",
	"RX Broadcast Frames",
	"RX Multicast Frames",
	"RX Pause Frames",
	"RX 64 Byte Frames",
	"RX 65 - 127 Byte Frames",
	"RX 128 - 255 Byte Frames",
	"RX 256 - 511 Bytes Frames",
	"RX 512 - 1023 Byte Frames",
	"RX 1024 - 1518 Byte Frames",
	"RX Greater 1518 Byte Frames",
};

static const char lan743x_set1_sw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Queue 0 Frames",
	"RX Queue 1 Frames",
	"RX Queue 2 Frames",
	"RX Queue 3 Frames",
};

static const char lan743x_set2_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Total Frames",
	"EEE RX LPI Transitions",
	"EEE RX LPI Time",
	"RX Counter Rollover Status",
	"TX FCS Errors",
	"TX Excess Deferral Errors",
	"TX Carrier Errors",
	"TX Bad Byte Count",
	"TX Single Collisions",
	"TX Multiple Collisions",
	"TX Excessive Collision",
	"TX Late Collisions",
	"TX Unicast Byte Count",
	"TX Broadcast Byte Count",
	"TX Multicast Byte Count",
	"TX Unicast Frames",
	"TX Broadcast Frames",
	"TX Multicast Frames",
	"TX Pause Frames",
	"TX 64 Byte Frames",
	"TX 65 - 127 Byte Frames",
	"TX 128 - 255 Byte Frames",
	"TX 256 - 511 Bytes Frames",
	"TX 512 - 1023 Byte Frames",
	"TX 1024 - 1518 Byte Frames",
	"TX Greater 1518 Byte Frames",
	"TX Total Frames",
	"EEE TX LPI Transitions",
	"EEE TX LPI Time",
	"TX Counter Rollover Status",
};

static const u32 lan743x_set0_hw_cnt_addr[] = {
	STAT_RX_FCS_ERRORS,
	STAT_RX_ALIGNMENT_ERRORS,
	STAT_RX_FRAGMENT_ERRORS,
	STAT_RX_JABBER_ERRORS,
	STAT_RX_UNDERSIZE_FRAME_ERRORS,
	STAT_RX_OVERSIZE_FRAME_ERRORS,
	STAT_RX_DROPPED_FRAMES,
	STAT_RX_UNICAST_BYTE_COUNT,
	STAT_RX_BROADCAST_BYTE_COUNT,
	STAT_RX_MULTICAST_BYTE_COUNT,
	STAT_RX_UNICAST_FRAMES,
	STAT_RX_BROADCAST_FRAMES,
	STAT_RX_MULTICAST_FRAMES,
	STAT_RX_PAUSE_FRAMES,
	STAT_RX_64_BYTE_FRAMES,
	STAT_RX_65_127_BYTE_FRAMES,
	STAT_RX_128_255_BYTE_FRAMES,
	STAT_RX_256_511_BYTES_FRAMES,
	STAT_RX_512_1023_BYTE_FRAMES,
	STAT_RX_1024_1518_BYTE_FRAMES,
	STAT_RX_GREATER_1518_BYTE_FRAMES,
};

static const u32 lan743x_set2_hw_cnt_addr[] = {
	STAT_RX_TOTAL_FRAMES,
	STAT_EEE_RX_LPI_TRANSITIONS,
	STAT_EEE_RX_LPI_TIME,
	STAT_RX_COUNTER_ROLLOVER_STATUS,
	STAT_TX_FCS_ERRORS,
	STAT_TX_EXCESS_DEFERRAL_ERRORS,
	STAT_TX_CARRIER_ERRORS,
	STAT_TX_BAD_BYTE_COUNT,
	STAT_TX_SINGLE_COLLISIONS,
	STAT_TX_MULTIPLE_COLLISIONS,
	STAT_TX_EXCESSIVE_COLLISION,
	STAT_TX_LATE_COLLISIONS,
	STAT_TX_UNICAST_BYTE_COUNT,
	STAT_TX_BROADCAST_BYTE_COUNT,
	STAT_TX_MULTICAST_BYTE_COUNT,
	STAT_TX_UNICAST_FRAMES,
	STAT_TX_BROADCAST_FRAMES,
	STAT_TX_MULTICAST_FRAMES,
	STAT_TX_PAUSE_FRAMES,
	STAT_TX_64_BYTE_FRAMES,
	STAT_TX_65_127_BYTE_FRAMES,
	STAT_TX_128_255_BYTE_FRAMES,
	STAT_TX_256_511_BYTES_FRAMES,
	STAT_TX_512_1023_BYTE_FRAMES,
	STAT_TX_1024_1518_BYTE_FRAMES,
	STAT_TX_GREATER_1518_BYTE_FRAMES,
	STAT_TX_TOTAL_FRAMES,
	STAT_EEE_TX_LPI_TRANSITIONS,
	STAT_EEE_TX_LPI_TIME,
	STAT_TX_COUNTER_ROLLOVER_STATUS
};

static void lan743x_ethtool_get_strings(struct net_device *netdev,
					u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, lan743x_set0_hw_cnt_strings,
		       sizeof(lan743x_set0_hw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings)],
		       lan743x_set1_sw_cnt_strings,
		       sizeof(lan743x_set1_sw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings) +
		       sizeof(lan743x_set1_sw_cnt_strings)],
		       lan743x_set2_hw_cnt_strings,
		       sizeof(lan743x_set2_hw_cnt_strings));
		break;
	}
}

static void lan743x_ethtool_get_ethtool_stats(struct net_device *netdev,
					      struct ethtool_stats *stats,
					      u64 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int data_index = 0;
	u32 buf;
	int i;

	for (i = 0; i < ARRAY_SIZE(lan743x_set0_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set0_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
	for (i = 0; i < ARRAY_SIZE(adapter->rx); i++)
		data[data_index++] = (u64)(adapter->rx[i].frame_count);
	for (i = 0; i < ARRAY_SIZE(lan743x_set2_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set2_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
}

static int lan743x_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
	{
		int ret;

		ret = ARRAY_SIZE(lan743x_set0_hw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set1_sw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set2_hw_cnt_strings);
		return ret;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static int lan743x_ethtool_get_rxnfc(struct net_device *netdev,
				     struct ethtool_rxnfc *rxnfc,
				     u32 *rule_locs)
{
	switch (rxnfc->cmd) {
	case ETHTOOL_GRXFH:
		rxnfc->data = 0;
		switch (rxnfc->flow_type) {
		case TCP_V4_FLOW:case UDP_V4_FLOW:
		case TCP_V6_FLOW:case UDP_V6_FLOW:
			rxnfc->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			/* fall through */
		case IPV4_FLOW: case IPV6_FLOW:
			rxnfc->data |= RXH_IP_SRC | RXH_IP_DST;
			return 0;
		}
		break;
	case ETHTOOL_GRXRINGS:
		rxnfc->data = LAN743X_USED_RX_CHANNELS;
		return 0;
	}
	return -EOPNOTSUPP;
}

static u32 lan743x_ethtool_get_rxfh_key_size(struct net_device *netdev)
{
	return 40;
}

static u32 lan743x_ethtool_get_rxfh_indir_size(struct net_device *netdev)
{
	return 128;
}

static int lan743x_ethtool_get_rxfh(struct net_device *netdev,
				    u32 *indir, u8 *key, u8 *hfunc)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	if (indir) {
		int dw_index;
		int byte_index = 0;

		for (dw_index = 0; dw_index < 32; dw_index++) {
			u32 four_entries =
				lan743x_csr_read(adapter, RFE_INDX(dw_index));

			byte_index = dw_index << 2;
			indir[byte_index + 0] =
				((four_entries >> 0) & 0x000000FF);
			indir[byte_index + 1] =
				((four_entries >> 8) & 0x000000FF);
			indir[byte_index + 2] =
				((four_entries >> 16) & 0x000000FF);
			indir[byte_index + 3] =
				((four_entries >> 24) & 0x000000FF);
		}
	}
	if (key) {
		int dword_index;
		int byte_index = 0;

		for (dword_index = 0; dword_index < 10; dword_index++) {
			u32 four_entries =
				lan743x_csr_read(adapter,
						 RFE_HASH_KEY(dword_index));

			byte_index = dword_index << 2;
			key[byte_index + 0] =
				((four_entries >> 0) & 0x000000FF);
			key[byte_index + 1] =
				((four_entries >> 8) & 0x000000FF);
			key[byte_index + 2] =
				((four_entries >> 16) & 0x000000FF);
			key[byte_index + 3] =
				((four_entries >> 24) & 0x000000FF);
		}
	}
	if (hfunc)
		(*hfunc) = ETH_RSS_HASH_TOP;
	return 0;
}

static int lan743x_ethtool_set_rxfh(struct net_device *netdev,
				    const u32 *indir, const u8 *key,
				    const u8 hfunc)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (indir) {
		u32 indir_value = 0;
		int dword_index = 0;
		int byte_index = 0;

		for (dword_index = 0; dword_index < 32; dword_index++) {
			byte_index = dword_index << 2;
			indir_value =
				(((indir[byte_index + 0] & 0x000000FF) << 0) |
				((indir[byte_index + 1] & 0x000000FF) << 8) |
				((indir[byte_index + 2] & 0x000000FF) << 16) |
				((indir[byte_index + 3] & 0x000000FF) << 24));
			lan743x_csr_write(adapter, RFE_INDX(dword_index),
					  indir_value);
		}
	}
	if (key) {
		int dword_index = 0;
		int byte_index = 0;
		u32 key_value = 0;

		for (dword_index = 0; dword_index < 10; dword_index++) {
			byte_index = dword_index << 2;
			key_value =
				((((u32)(key[byte_index + 0])) << 0) |
				(((u32)(key[byte_index + 1])) << 8) |
				(((u32)(key[byte_index + 2])) << 16) |
				(((u32)(key[byte_index + 3])) << 24));
			lan743x_csr_write(adapter, RFE_HASH_KEY(dword_index),
					  key_value);
		}
	}
	return 0;
}

static int lan743x_ethtool_get_ts_info(struct net_device *netdev,
				       struct ethtool_ts_info *ts_info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_RX_SOFTWARE |
				   SOF_TIMESTAMPING_SOFTWARE |
				   SOF_TIMESTAMPING_TX_HARDWARE |
				   SOF_TIMESTAMPING_RX_HARDWARE |
				   SOF_TIMESTAMPING_RAW_HARDWARE;

	if (adapter->ptp.ptp_clock)
		ts_info->phc_index = ptp_clock_index(adapter->ptp.ptp_clock);
	else
		ts_info->phc_index = -1;

	ts_info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			    BIT(HWTSTAMP_TX_ON) |
			    BIT(HWTSTAMP_TX_ONESTEP_SYNC);
	ts_info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			      BIT(HWTSTAMP_FILTER_ALL);
	return 0;
}

static int lan743x_ethtool_get_eee(struct net_device *netdev,
				   struct ethtool_eee *eee)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	u32 buf;
	int ret;

	if (!phydev)
		return -EIO;
	if (!phydev->drv) {
		netif_err(adapter, drv, adapter->netdev,
			  "Missing PHY Driver\n");
		return -EIO;
	}

	ret = phy_ethtool_get_eee(phydev, eee);
	if (ret < 0)
		return ret;

	buf = lan743x_csr_read(adapter, MAC_CR);
	if (buf & MAC_CR_EEE_EN_) {
		eee->eee_enabled = true;
		eee->eee_active = !!(eee->advertised & eee->lp_advertised);
		eee->tx_lpi_enabled = true;
		/* EEE_TX_LPI_REQ_DLY & tx_lpi_timer are same uSec unit */
		buf = lan743x_csr_read(adapter, MAC_EEE_TX_LPI_REQ_DLY_CNT);
		eee->tx_lpi_timer = buf;
	} else {
		eee->eee_enabled = false;
		eee->eee_active = false;
		eee->tx_lpi_enabled = false;
		eee->tx_lpi_timer = 0;
	}

	return 0;
}

static int lan743x_ethtool_set_eee(struct net_device *netdev,
				   struct ethtool_eee *eee)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	struct phy_device *phydev = NULL;
	u32 buf = 0;
	int ret = 0;

	if (!netdev)
		return -EINVAL;
	adapter = netdev_priv(netdev);
	if (!adapter)
		return -EINVAL;
	phydev = netdev->phydev;
	if (!phydev)
		return -EIO;
	if (!phydev->drv) {
		netif_err(adapter, drv, adapter->netdev,
			  "Missing PHY Driver\n");
		return -EIO;
	}

	if (eee->eee_enabled) {
		ret = phy_init_eee(phydev, 0);
		if (ret) {
			netif_err(adapter, drv, adapter->netdev,
				  "EEE initialization failed\n");
			return ret;
		}

		buf = (u32)eee->tx_lpi_timer;
		lan743x_csr_write(adapter, MAC_EEE_TX_LPI_REQ_DLY_CNT, buf);

		buf = lan743x_csr_read(adapter, MAC_CR);
		buf |= MAC_CR_EEE_EN_;
		lan743x_csr_write(adapter, MAC_CR, buf);
	} else {
		buf = lan743x_csr_read(adapter, MAC_CR);
		buf &= ~MAC_CR_EEE_EN_;
		lan743x_csr_write(adapter, MAC_CR, buf);
	}

	return phy_ethtool_set_eee(phydev, eee);
}

#ifdef CONFIG_PM
static void lan743x_ethtool_get_wol(struct net_device *netdev,
				    struct ethtool_wolinfo *wol)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	wol->supported = 0;
	wol->wolopts = 0;

	if (netdev->phydev)
		phy_ethtool_get_wol(netdev->phydev, wol);

	wol->supported |= WAKE_BCAST | WAKE_UCAST | WAKE_MCAST |
		WAKE_MAGIC | WAKE_PHY | WAKE_ARP;

	wol->wolopts |= adapter->wolopts;
}

static int lan743x_ethtool_set_wol(struct net_device *netdev,
				   struct ethtool_wolinfo *wol)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	adapter->wolopts = 0;
	if (wol->wolopts & WAKE_UCAST)
		adapter->wolopts |= WAKE_UCAST;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wolopts |= WAKE_MCAST;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wolopts |= WAKE_BCAST;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wolopts |= WAKE_MAGIC;
	if (wol->wolopts & WAKE_PHY)
		adapter->wolopts |= WAKE_PHY;
	if (wol->wolopts & WAKE_ARP)
		adapter->wolopts |= WAKE_ARP;

	device_set_wakeup_enable(&adapter->pdev->dev, (bool)wol->wolopts);

	return netdev->phydev ? phy_ethtool_set_wol(netdev->phydev, wol)
			: -ENETDOWN;
}
#endif /* CONFIG_PM */

const struct ethtool_ops lan743x_ethtool_ops = {
	.get_drvinfo = lan743x_ethtool_get_drvinfo,
	.get_msglevel = lan743x_ethtool_get_msglevel,
	.set_msglevel = lan743x_ethtool_set_msglevel,
	.get_link = ethtool_op_get_link,

	.get_eeprom_len = lan743x_ethtool_get_eeprom_len,
	.get_eeprom = lan743x_ethtool_get_eeprom,
	.set_eeprom = lan743x_ethtool_set_eeprom,
	.get_strings = lan743x_ethtool_get_strings,
	.get_ethtool_stats = lan743x_ethtool_get_ethtool_stats,
	.get_sset_count = lan743x_ethtool_get_sset_count,
	.get_rxnfc = lan743x_ethtool_get_rxnfc,
	.get_rxfh_key_size = lan743x_ethtool_get_rxfh_key_size,
	.get_rxfh_indir_size = lan743x_ethtool_get_rxfh_indir_size,
	.get_rxfh = lan743x_ethtool_get_rxfh,
	.set_rxfh = lan743x_ethtool_set_rxfh,
	.get_ts_info = lan743x_ethtool_get_ts_info,
	.get_eee = lan743x_ethtool_get_eee,
	.set_eee = lan743x_ethtool_set_eee,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
#ifdef CONFIG_PM
	.get_wol = lan743x_ethtool_get_wol,
	.set_wol = lan743x_ethtool_set_wol,
#endif
};

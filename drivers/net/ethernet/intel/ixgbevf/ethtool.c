/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2015 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* ethtool support for ixgbevf */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/if_vlan.h>
#include <linux/uaccess.h>

#include "ixgbevf.h"

#define IXGBE_ALL_RAR_ENTRIES 16

struct ixgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	struct {
		int sizeof_stat;
		int stat_offset;
		int base_stat_offset;
		int saved_reset_offset;
	};
};

#define IXGBEVF_STAT(m, b, r) { \
	.sizeof_stat = FIELD_SIZEOF(struct ixgbevf_adapter, m), \
	.stat_offset = offsetof(struct ixgbevf_adapter, m), \
	.base_stat_offset = offsetof(struct ixgbevf_adapter, b), \
	.saved_reset_offset = offsetof(struct ixgbevf_adapter, r) \
}

#define IXGBEVF_ZSTAT(m) { \
	.sizeof_stat = FIELD_SIZEOF(struct ixgbevf_adapter, m), \
	.stat_offset = offsetof(struct ixgbevf_adapter, m), \
	.base_stat_offset = -1, \
	.saved_reset_offset = -1 \
}

static const struct ixgbe_stats ixgbe_gstrings_stats[] = {
	{"rx_packets", IXGBEVF_STAT(stats.vfgprc, stats.base_vfgprc,
				    stats.saved_reset_vfgprc)},
	{"tx_packets", IXGBEVF_STAT(stats.vfgptc, stats.base_vfgptc,
				    stats.saved_reset_vfgptc)},
	{"rx_bytes", IXGBEVF_STAT(stats.vfgorc, stats.base_vfgorc,
				  stats.saved_reset_vfgorc)},
	{"tx_bytes", IXGBEVF_STAT(stats.vfgotc, stats.base_vfgotc,
				  stats.saved_reset_vfgotc)},
	{"tx_busy", IXGBEVF_ZSTAT(tx_busy)},
	{"tx_restart_queue", IXGBEVF_ZSTAT(restart_queue)},
	{"tx_timeout_count", IXGBEVF_ZSTAT(tx_timeout_count)},
	{"multicast", IXGBEVF_STAT(stats.vfmprc, stats.base_vfmprc,
				   stats.saved_reset_vfmprc)},
	{"rx_csum_offload_errors", IXGBEVF_ZSTAT(hw_csum_rx_error)},
#ifdef BP_EXTENDED_STATS
	{"rx_bp_poll_yield", IXGBEVF_ZSTAT(bp_rx_yields)},
	{"rx_bp_cleaned", IXGBEVF_ZSTAT(bp_rx_cleaned)},
	{"rx_bp_misses", IXGBEVF_ZSTAT(bp_rx_missed)},
	{"tx_bp_napi_yield", IXGBEVF_ZSTAT(bp_tx_yields)},
	{"tx_bp_cleaned", IXGBEVF_ZSTAT(bp_tx_cleaned)},
	{"tx_bp_misses", IXGBEVF_ZSTAT(bp_tx_missed)},
#endif
};

#define IXGBE_QUEUE_STATS_LEN 0
#define IXGBE_GLOBAL_STATS_LEN	ARRAY_SIZE(ixgbe_gstrings_stats)

#define IXGBEVF_STATS_LEN (IXGBE_GLOBAL_STATS_LEN + IXGBE_QUEUE_STATS_LEN)
static const char ixgbe_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"Link test   (on/offline)"
};

#define IXGBE_TEST_LEN (sizeof(ixgbe_gstrings_test) / ETH_GSTRING_LEN)

static int ixgbevf_get_settings(struct net_device *netdev,
				struct ethtool_cmd *ecmd)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 link_speed = 0;
	bool link_up;

	ecmd->supported = SUPPORTED_10000baseT_Full;
	ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->transceiver = XCVR_DUMMY1;
	ecmd->port = -1;

	hw->mac.get_link_status = 1;
	hw->mac.ops.check_link(hw, &link_speed, &link_up, false);

	if (link_up) {
		__u32 speed = SPEED_10000;

		switch (link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			speed = SPEED_10000;
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			speed = SPEED_1000;
			break;
		case IXGBE_LINK_SPEED_100_FULL:
			speed = SPEED_100;
			break;
		}

		ethtool_cmd_speed_set(ecmd, speed);
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_UNKNOWN);
		ecmd->duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static u32 ixgbevf_get_msglevel(struct net_device *netdev)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void ixgbevf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

#define IXGBE_GET_STAT(_A_, _R_) (_A_->stats._R_)

static int ixgbevf_get_regs_len(struct net_device *netdev)
{
#define IXGBE_REGS_LEN 45
	return IXGBE_REGS_LEN * sizeof(u32);
}

static void ixgbevf_get_regs(struct net_device *netdev,
			     struct ethtool_regs *regs,
			     void *p)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u32 regs_len = ixgbevf_get_regs_len(netdev);
	u8 i;

	memset(p, 0, regs_len);

	regs->version = (1 << 24) | hw->revision_id << 16 | hw->device_id;

	/* General Registers */
	regs_buff[0] = IXGBE_READ_REG(hw, IXGBE_VFCTRL);
	regs_buff[1] = IXGBE_READ_REG(hw, IXGBE_VFSTATUS);
	regs_buff[2] = IXGBE_READ_REG(hw, IXGBE_VFLINKS);
	regs_buff[3] = IXGBE_READ_REG(hw, IXGBE_VFRXMEMWRAP);
	regs_buff[4] = IXGBE_READ_REG(hw, IXGBE_VFFRTIMER);

	/* Interrupt */
	/* don't read EICR because it can clear interrupt causes, instead
	 * read EICS which is a shadow but doesn't clear EICR
	 */
	regs_buff[5] = IXGBE_READ_REG(hw, IXGBE_VTEICS);
	regs_buff[6] = IXGBE_READ_REG(hw, IXGBE_VTEICS);
	regs_buff[7] = IXGBE_READ_REG(hw, IXGBE_VTEIMS);
	regs_buff[8] = IXGBE_READ_REG(hw, IXGBE_VTEIMC);
	regs_buff[9] = IXGBE_READ_REG(hw, IXGBE_VTEIAC);
	regs_buff[10] = IXGBE_READ_REG(hw, IXGBE_VTEIAM);
	regs_buff[11] = IXGBE_READ_REG(hw, IXGBE_VTEITR(0));
	regs_buff[12] = IXGBE_READ_REG(hw, IXGBE_VTIVAR(0));
	regs_buff[13] = IXGBE_READ_REG(hw, IXGBE_VTIVAR_MISC);

	/* Receive DMA */
	for (i = 0; i < 2; i++)
		regs_buff[14 + i] = IXGBE_READ_REG(hw, IXGBE_VFRDBAL(i));
	for (i = 0; i < 2; i++)
		regs_buff[16 + i] = IXGBE_READ_REG(hw, IXGBE_VFRDBAH(i));
	for (i = 0; i < 2; i++)
		regs_buff[18 + i] = IXGBE_READ_REG(hw, IXGBE_VFRDLEN(i));
	for (i = 0; i < 2; i++)
		regs_buff[20 + i] = IXGBE_READ_REG(hw, IXGBE_VFRDH(i));
	for (i = 0; i < 2; i++)
		regs_buff[22 + i] = IXGBE_READ_REG(hw, IXGBE_VFRDT(i));
	for (i = 0; i < 2; i++)
		regs_buff[24 + i] = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i));
	for (i = 0; i < 2; i++)
		regs_buff[26 + i] = IXGBE_READ_REG(hw, IXGBE_VFSRRCTL(i));

	/* Receive */
	regs_buff[28] = IXGBE_READ_REG(hw, IXGBE_VFPSRTYPE);

	/* Transmit */
	for (i = 0; i < 2; i++)
		regs_buff[29 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDBAL(i));
	for (i = 0; i < 2; i++)
		regs_buff[31 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDBAH(i));
	for (i = 0; i < 2; i++)
		regs_buff[33 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDLEN(i));
	for (i = 0; i < 2; i++)
		regs_buff[35 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDH(i));
	for (i = 0; i < 2; i++)
		regs_buff[37 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDT(i));
	for (i = 0; i < 2; i++)
		regs_buff[39 + i] = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(i));
	for (i = 0; i < 2; i++)
		regs_buff[41 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDWBAL(i));
	for (i = 0; i < 2; i++)
		regs_buff[43 + i] = IXGBE_READ_REG(hw, IXGBE_VFTDWBAH(i));
}

static void ixgbevf_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, ixgbevf_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, ixgbevf_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}

static void ixgbevf_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IXGBEVF_MAX_RXD;
	ring->tx_max_pending = IXGBEVF_MAX_TXD;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
}

static int ixgbevf_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	struct ixgbevf_ring *tx_ring = NULL, *rx_ring = NULL;
	u32 new_rx_count, new_tx_count;
	int i, err = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_tx_count = max_t(u32, ring->tx_pending, IXGBEVF_MIN_TXD);
	new_tx_count = min_t(u32, new_tx_count, IXGBEVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, IXGBE_REQ_TX_DESCRIPTOR_MULTIPLE);

	new_rx_count = max_t(u32, ring->rx_pending, IXGBEVF_MIN_RXD);
	new_rx_count = min_t(u32, new_rx_count, IXGBEVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);

	/* if nothing to do return success */
	if ((new_tx_count == adapter->tx_ring_count) &&
	    (new_rx_count == adapter->rx_ring_count))
		return 0;

	while (test_and_set_bit(__IXGBEVF_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	if (new_tx_count != adapter->tx_ring_count) {
		tx_ring = vmalloc(adapter->num_tx_queues * sizeof(*tx_ring));
		if (!tx_ring) {
			err = -ENOMEM;
			goto clear_reset;
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			/* clone ring and setup updated count */
			tx_ring[i] = *adapter->tx_ring[i];
			tx_ring[i].count = new_tx_count;
			err = ixgbevf_setup_tx_resources(&tx_ring[i]);
			if (err) {
				while (i) {
					i--;
					ixgbevf_free_tx_resources(&tx_ring[i]);
				}

				vfree(tx_ring);
				tx_ring = NULL;

				goto clear_reset;
			}
		}
	}

	if (new_rx_count != adapter->rx_ring_count) {
		rx_ring = vmalloc(adapter->num_rx_queues * sizeof(*rx_ring));
		if (!rx_ring) {
			err = -ENOMEM;
			goto clear_reset;
		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			/* clone ring and setup updated count */
			rx_ring[i] = *adapter->rx_ring[i];
			rx_ring[i].count = new_rx_count;
			err = ixgbevf_setup_rx_resources(&rx_ring[i]);
			if (err) {
				while (i) {
					i--;
					ixgbevf_free_rx_resources(&rx_ring[i]);
				}

				vfree(rx_ring);
				rx_ring = NULL;

				goto clear_reset;
			}
		}
	}

	/* bring interface down to prepare for update */
	ixgbevf_down(adapter);

	/* Tx */
	if (tx_ring) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			ixgbevf_free_tx_resources(adapter->tx_ring[i]);
			*adapter->tx_ring[i] = tx_ring[i];
		}
		adapter->tx_ring_count = new_tx_count;

		vfree(tx_ring);
		tx_ring = NULL;
	}

	/* Rx */
	if (rx_ring) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			ixgbevf_free_rx_resources(adapter->rx_ring[i]);
			*adapter->rx_ring[i] = rx_ring[i];
		}
		adapter->rx_ring_count = new_rx_count;

		vfree(rx_ring);
		rx_ring = NULL;
	}

	/* restore interface using new values */
	ixgbevf_up(adapter);

clear_reset:
	/* free Tx resources if Rx error is encountered */
	if (tx_ring) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			ixgbevf_free_tx_resources(&tx_ring[i]);
		vfree(tx_ring);
	}

	clear_bit(__IXGBEVF_RESETTING, &adapter->state);
	return err;
}

static int ixgbevf_get_sset_count(struct net_device *dev, int stringset)
{
	switch (stringset) {
	case ETH_SS_TEST:
		return IXGBE_TEST_LEN;
	case ETH_SS_STATS:
		return IXGBE_GLOBAL_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void ixgbevf_get_ethtool_stats(struct net_device *netdev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	char *base = (char *)adapter;
	int i;
#ifdef BP_EXTENDED_STATS
	u64 rx_yields = 0, rx_cleaned = 0, rx_missed = 0,
	    tx_yields = 0, tx_cleaned = 0, tx_missed = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		rx_yields += adapter->rx_ring[i]->stats.yields;
		rx_cleaned += adapter->rx_ring[i]->stats.cleaned;
		rx_yields += adapter->rx_ring[i]->stats.yields;
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		tx_yields += adapter->tx_ring[i]->stats.yields;
		tx_cleaned += adapter->tx_ring[i]->stats.cleaned;
		tx_yields += adapter->tx_ring[i]->stats.yields;
	}

	adapter->bp_rx_yields = rx_yields;
	adapter->bp_rx_cleaned = rx_cleaned;
	adapter->bp_rx_missed = rx_missed;

	adapter->bp_tx_yields = tx_yields;
	adapter->bp_tx_cleaned = tx_cleaned;
	adapter->bp_tx_missed = tx_missed;
#endif

	ixgbevf_update_stats(adapter);
	for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
		char *p = base + ixgbe_gstrings_stats[i].stat_offset;
		char *b = base + ixgbe_gstrings_stats[i].base_stat_offset;
		char *r = base + ixgbe_gstrings_stats[i].saved_reset_offset;

		if (ixgbe_gstrings_stats[i].sizeof_stat == sizeof(u64)) {
			if (ixgbe_gstrings_stats[i].base_stat_offset >= 0)
				data[i] = *(u64 *)p - *(u64 *)b + *(u64 *)r;
			else
				data[i] = *(u64 *)p;
		} else {
			if (ixgbe_gstrings_stats[i].base_stat_offset >= 0)
				data[i] = *(u32 *)p - *(u32 *)b + *(u32 *)r;
			else
				data[i] = *(u32 *)p;
		}
	}
}

static void ixgbevf_get_strings(struct net_device *netdev, u32 stringset,
				u8 *data)
{
	char *p = (char *)data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *ixgbe_gstrings_test,
		       IXGBE_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
			memcpy(p, ixgbe_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int ixgbevf_link_test(struct ixgbevf_adapter *adapter, u64 *data)
{
	struct ixgbe_hw *hw = &adapter->hw;
	bool link_up;
	u32 link_speed = 0;
	*data = 0;

	hw->mac.ops.check_link(hw, &link_speed, &link_up, true);
	if (!link_up)
		*data = 1;

	return *data;
}

/* ethtool register test data */
struct ixgbevf_reg_test {
	u16 reg;
	u8  array_len;
	u8  test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x40 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST	1
#define SET_READ_TEST	2
#define WRITE_NO_TEST	3
#define TABLE32_TEST	4
#define TABLE64_TEST_LO	5
#define TABLE64_TEST_HI	6

/* default VF register test */
static const struct ixgbevf_reg_test reg_test_vf[] = {
	{ IXGBE_VFRDBAL(0), 2, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFF80 },
	{ IXGBE_VFRDBAH(0), 2, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_VFRDLEN(0), 2, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ IXGBE_VFRXDCTL(0), 2, WRITE_NO_TEST, 0, IXGBE_RXDCTL_ENABLE },
	{ IXGBE_VFRDT(0), 2, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ IXGBE_VFRXDCTL(0), 2, WRITE_NO_TEST, 0, 0 },
	{ IXGBE_VFTDBAL(0), 2, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_VFTDBAH(0), 2, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_VFTDLEN(0), 2, PATTERN_TEST, 0x000FFF80, 0x000FFF80 },
	{ .reg = 0 }
};

static const u32 register_test_patterns[] = {
	0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF
};

static bool reg_pattern_test(struct ixgbevf_adapter *adapter, u64 *data,
			     int reg, u32 mask, u32 write)
{
	u32 pat, val, before;

	if (IXGBE_REMOVED(adapter->hw.hw_addr)) {
		*data = 1;
		return true;
	}
	for (pat = 0; pat < ARRAY_SIZE(register_test_patterns); pat++) {
		before = ixgbevf_read_reg(&adapter->hw, reg);
		ixgbe_write_reg(&adapter->hw, reg,
				register_test_patterns[pat] & write);
		val = ixgbevf_read_reg(&adapter->hw, reg);
		if (val != (register_test_patterns[pat] & write & mask)) {
			hw_dbg(&adapter->hw,
			       "pattern test reg %04X failed: got 0x%08X expected 0x%08X\n",
			       reg, val,
			       register_test_patterns[pat] & write & mask);
			*data = reg;
			ixgbe_write_reg(&adapter->hw, reg, before);
			return true;
		}
		ixgbe_write_reg(&adapter->hw, reg, before);
	}
	return false;
}

static bool reg_set_and_check(struct ixgbevf_adapter *adapter, u64 *data,
			      int reg, u32 mask, u32 write)
{
	u32 val, before;

	if (IXGBE_REMOVED(adapter->hw.hw_addr)) {
		*data = 1;
		return true;
	}
	before = ixgbevf_read_reg(&adapter->hw, reg);
	ixgbe_write_reg(&adapter->hw, reg, write & mask);
	val = ixgbevf_read_reg(&adapter->hw, reg);
	if ((write & mask) != (val & mask)) {
		pr_err("set/check reg %04X test failed: got 0x%08X expected 0x%08X\n",
		       reg, (val & mask), write & mask);
		*data = reg;
		ixgbe_write_reg(&adapter->hw, reg, before);
		return true;
	}
	ixgbe_write_reg(&adapter->hw, reg, before);
	return false;
}

static int ixgbevf_reg_test(struct ixgbevf_adapter *adapter, u64 *data)
{
	const struct ixgbevf_reg_test *test;
	u32 i;

	if (IXGBE_REMOVED(adapter->hw.hw_addr)) {
		dev_err(&adapter->pdev->dev,
			"Adapter removed - register test blocked\n");
		*data = 1;
		return 1;
	}
	test = reg_test_vf;

	/* Perform the register test, looping through the test table
	 * until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			bool b = false;

			switch (test->test_type) {
			case PATTERN_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 0x40),
						     test->mask,
						     test->write);
				break;
			case SET_READ_TEST:
				b = reg_set_and_check(adapter, data,
						      test->reg + (i * 0x40),
						      test->mask,
						      test->write);
				break;
			case WRITE_NO_TEST:
				ixgbe_write_reg(&adapter->hw,
						test->reg + (i * 0x40),
						test->write);
				break;
			case TABLE32_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 4),
						     test->mask,
						     test->write);
				break;
			case TABLE64_TEST_LO:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 8),
						     test->mask,
						     test->write);
				break;
			case TABLE64_TEST_HI:
				b = reg_pattern_test(adapter, data,
						     test->reg + 4 + (i * 8),
						     test->mask,
						     test->write);
				break;
			}
			if (b)
				return 1;
		}
		test++;
	}

	*data = 0;
	return *data;
}

static void ixgbevf_diag_test(struct net_device *netdev,
			      struct ethtool_test *eth_test, u64 *data)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	bool if_running = netif_running(netdev);

	if (IXGBE_REMOVED(adapter->hw.hw_addr)) {
		dev_err(&adapter->pdev->dev,
			"Adapter removed - test blocked\n");
		data[0] = 1;
		data[1] = 1;
		eth_test->flags |= ETH_TEST_FL_FAILED;
		return;
	}
	set_bit(__IXGBEVF_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		hw_dbg(&adapter->hw, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (ixgbevf_link_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			ixgbevf_close(netdev);
		else
			ixgbevf_reset(adapter);

		hw_dbg(&adapter->hw, "register testing starting\n");
		if (ixgbevf_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbevf_reset(adapter);

		clear_bit(__IXGBEVF_TESTING, &adapter->state);
		if (if_running)
			ixgbevf_open(netdev);
	} else {
		hw_dbg(&adapter->hw, "online testing starting\n");
		/* Online tests */
		if (ixgbevf_link_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Online tests aren't run; pass by default */
		data[0] = 0;

		clear_bit(__IXGBEVF_TESTING, &adapter->state);
	}
	msleep_interruptible(4 * 1000);
}

static int ixgbevf_nway_reset(struct net_device *netdev)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		ixgbevf_reinit_locked(adapter);

	return 0;
}

static int ixgbevf_get_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *ec)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	/* only valid if in constant ITR mode */
	if (adapter->rx_itr_setting <= 1)
		ec->rx_coalesce_usecs = adapter->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->rx_itr_setting >> 2;

	/* if in mixed Tx/Rx queues per vector mode, report only Rx settings */
	if (adapter->q_vector[0]->tx.count && adapter->q_vector[0]->rx.count)
		return 0;

	/* only valid if in constant ITR mode */
	if (adapter->tx_itr_setting <= 1)
		ec->tx_coalesce_usecs = adapter->tx_itr_setting;
	else
		ec->tx_coalesce_usecs = adapter->tx_itr_setting >> 2;

	return 0;
}

static int ixgbevf_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *ec)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	struct ixgbevf_q_vector *q_vector;
	int num_vectors, i;
	u16 tx_itr_param, rx_itr_param;

	/* don't accept Tx specific changes if we've got mixed RxTx vectors */
	if (adapter->q_vector[0]->tx.count &&
	    adapter->q_vector[0]->rx.count && ec->tx_coalesce_usecs)
		return -EINVAL;

	if ((ec->rx_coalesce_usecs > (IXGBE_MAX_EITR >> 2)) ||
	    (ec->tx_coalesce_usecs > (IXGBE_MAX_EITR >> 2)))
		return -EINVAL;

	if (ec->rx_coalesce_usecs > 1)
		adapter->rx_itr_setting = ec->rx_coalesce_usecs << 2;
	else
		adapter->rx_itr_setting = ec->rx_coalesce_usecs;

	if (adapter->rx_itr_setting == 1)
		rx_itr_param = IXGBE_20K_ITR;
	else
		rx_itr_param = adapter->rx_itr_setting;

	if (ec->tx_coalesce_usecs > 1)
		adapter->tx_itr_setting = ec->tx_coalesce_usecs << 2;
	else
		adapter->tx_itr_setting = ec->tx_coalesce_usecs;

	if (adapter->tx_itr_setting == 1)
		tx_itr_param = IXGBE_12K_ITR;
	else
		tx_itr_param = adapter->tx_itr_setting;

	num_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	for (i = 0; i < num_vectors; i++) {
		q_vector = adapter->q_vector[i];
		if (q_vector->tx.count && !q_vector->rx.count)
			/* Tx only */
			q_vector->itr = tx_itr_param;
		else
			/* Rx only or mixed */
			q_vector->itr = rx_itr_param;
		ixgbevf_write_eitr(q_vector);
	}

	return 0;
}

static int ixgbevf_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			     u32 *rules __always_unused)
{
	struct ixgbevf_adapter *adapter = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = adapter->num_rx_queues;
		return 0;
	default:
		hw_dbg(&adapter->hw, "Command parameters not supported\n");
		return -EOPNOTSUPP;
	}
}

static u32 ixgbevf_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);

	if (adapter->hw.mac.type >= ixgbe_mac_X550_vf)
		return IXGBEVF_X550_VFRETA_SIZE;

	return IXGBEVF_82599_RETA_SIZE;
}

static u32 ixgbevf_get_rxfh_key_size(struct net_device *netdev)
{
	return IXGBEVF_RSS_HASH_KEY_SIZE;
}

static int ixgbevf_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			    u8 *hfunc)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	int err = 0;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (adapter->hw.mac.type >= ixgbe_mac_X550_vf) {
		if (key)
			memcpy(key, adapter->rss_key, sizeof(adapter->rss_key));

		if (indir) {
			int i;

			for (i = 0; i < IXGBEVF_X550_VFRETA_SIZE; i++)
				indir[i] = adapter->rss_indir_tbl[i];
		}
	} else {
		/* If neither indirection table nor hash key was requested
		 *  - just return a success avoiding taking any locks.
		 */
		if (!indir && !key)
			return 0;

		spin_lock_bh(&adapter->mbx_lock);
		if (indir)
			err = ixgbevf_get_reta_locked(&adapter->hw, indir,
						      adapter->num_rx_queues);

		if (!err && key)
			err = ixgbevf_get_rss_key_locked(&adapter->hw, key);

		spin_unlock_bh(&adapter->mbx_lock);
	}

	return err;
}

static const struct ethtool_ops ixgbevf_ethtool_ops = {
	.get_settings		= ixgbevf_get_settings,
	.get_drvinfo		= ixgbevf_get_drvinfo,
	.get_regs_len		= ixgbevf_get_regs_len,
	.get_regs		= ixgbevf_get_regs,
	.nway_reset		= ixgbevf_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= ixgbevf_get_ringparam,
	.set_ringparam		= ixgbevf_set_ringparam,
	.get_msglevel		= ixgbevf_get_msglevel,
	.set_msglevel		= ixgbevf_set_msglevel,
	.self_test		= ixgbevf_diag_test,
	.get_sset_count		= ixgbevf_get_sset_count,
	.get_strings		= ixgbevf_get_strings,
	.get_ethtool_stats	= ixgbevf_get_ethtool_stats,
	.get_coalesce		= ixgbevf_get_coalesce,
	.set_coalesce		= ixgbevf_set_coalesce,
	.get_rxnfc		= ixgbevf_get_rxnfc,
	.get_rxfh_indir_size	= ixgbevf_get_rxfh_indir_size,
	.get_rxfh_key_size	= ixgbevf_get_rxfh_key_size,
	.get_rxfh		= ixgbevf_get_rxfh,
};

void ixgbevf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ixgbevf_ethtool_ops;
}

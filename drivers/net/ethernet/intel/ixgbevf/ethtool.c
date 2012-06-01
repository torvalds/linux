/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

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

#ifdef ETHTOOL_GSTATS
struct ixgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
	int base_stat_offset;
	int saved_reset_offset;
};

#define IXGBEVF_STAT(m, b, r)  sizeof(((struct ixgbevf_adapter *)0)->m), \
			    offsetof(struct ixgbevf_adapter, m),         \
			    offsetof(struct ixgbevf_adapter, b),         \
			    offsetof(struct ixgbevf_adapter, r)

static const struct ixgbe_stats ixgbe_gstrings_stats[] = {
	{"rx_packets", IXGBEVF_STAT(stats.vfgprc, stats.base_vfgprc,
				    stats.saved_reset_vfgprc)},
	{"tx_packets", IXGBEVF_STAT(stats.vfgptc, stats.base_vfgptc,
				    stats.saved_reset_vfgptc)},
	{"rx_bytes", IXGBEVF_STAT(stats.vfgorc, stats.base_vfgorc,
				  stats.saved_reset_vfgorc)},
	{"tx_bytes", IXGBEVF_STAT(stats.vfgotc, stats.base_vfgotc,
				  stats.saved_reset_vfgotc)},
	{"tx_busy", IXGBEVF_STAT(tx_busy, zero_base, zero_base)},
	{"multicast", IXGBEVF_STAT(stats.vfmprc, stats.base_vfmprc,
				   stats.saved_reset_vfmprc)},
	{"rx_csum_offload_good", IXGBEVF_STAT(hw_csum_rx_good, zero_base,
					      zero_base)},
	{"rx_csum_offload_errors", IXGBEVF_STAT(hw_csum_rx_error, zero_base,
						zero_base)},
	{"tx_csum_offload_ctxt", IXGBEVF_STAT(hw_csum_tx_good, zero_base,
					      zero_base)},
	{"rx_header_split", IXGBEVF_STAT(rx_hdr_split, zero_base, zero_base)},
};

#define IXGBE_QUEUE_STATS_LEN 0
#define IXGBE_GLOBAL_STATS_LEN	ARRAY_SIZE(ixgbe_gstrings_stats)

#define IXGBEVF_STATS_LEN (IXGBE_GLOBAL_STATS_LEN + IXGBE_QUEUE_STATS_LEN)
#endif /* ETHTOOL_GSTATS */
#ifdef ETHTOOL_TEST
static const char ixgbe_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"Link test   (on/offline)"
};
#define IXGBE_TEST_LEN (sizeof(ixgbe_gstrings_test) / ETH_GSTRING_LEN)
#endif /* ETHTOOL_TEST */

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
		ethtool_cmd_speed_set(ecmd, -1);
		ecmd->duplex = -1;
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

static char *ixgbevf_reg_names[] = {
	"IXGBE_VFCTRL",
	"IXGBE_VFSTATUS",
	"IXGBE_VFLINKS",
	"IXGBE_VFRXMEMWRAP",
	"IXGBE_VFFRTIMER",
	"IXGBE_VTEICR",
	"IXGBE_VTEICS",
	"IXGBE_VTEIMS",
	"IXGBE_VTEIMC",
	"IXGBE_VTEIAC",
	"IXGBE_VTEIAM",
	"IXGBE_VTEITR",
	"IXGBE_VTIVAR",
	"IXGBE_VTIVAR_MISC",
	"IXGBE_VFRDBAL0",
	"IXGBE_VFRDBAL1",
	"IXGBE_VFRDBAH0",
	"IXGBE_VFRDBAH1",
	"IXGBE_VFRDLEN0",
	"IXGBE_VFRDLEN1",
	"IXGBE_VFRDH0",
	"IXGBE_VFRDH1",
	"IXGBE_VFRDT0",
	"IXGBE_VFRDT1",
	"IXGBE_VFRXDCTL0",
	"IXGBE_VFRXDCTL1",
	"IXGBE_VFSRRCTL0",
	"IXGBE_VFSRRCTL1",
	"IXGBE_VFPSRTYPE",
	"IXGBE_VFTDBAL0",
	"IXGBE_VFTDBAL1",
	"IXGBE_VFTDBAH0",
	"IXGBE_VFTDBAH1",
	"IXGBE_VFTDLEN0",
	"IXGBE_VFTDLEN1",
	"IXGBE_VFTDH0",
	"IXGBE_VFTDH1",
	"IXGBE_VFTDT0",
	"IXGBE_VFTDT1",
	"IXGBE_VFTXDCTL0",
	"IXGBE_VFTXDCTL1",
	"IXGBE_VFTDWBAL0",
	"IXGBE_VFTDWBAL1",
	"IXGBE_VFTDWBAH0",
	"IXGBE_VFTDWBAH1"
};


static int ixgbevf_get_regs_len(struct net_device *netdev)
{
	return (ARRAY_SIZE(ixgbevf_reg_names)) * sizeof(u32);
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
	 * read EICS which is a shadow but doesn't clear EICR */
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

	for (i = 0; i < ARRAY_SIZE(ixgbevf_reg_names); i++)
		hw_dbg(hw, "%s\t%8.8x\n", ixgbevf_reg_names[i], regs_buff[i]);
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
	struct ixgbevf_ring *tx_ring = adapter->tx_ring;
	struct ixgbevf_ring *rx_ring = adapter->rx_ring;

	ring->rx_max_pending = IXGBEVF_MAX_RXD;
	ring->tx_max_pending = IXGBEVF_MAX_TXD;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
}

static int ixgbevf_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	struct ixgbevf_adapter *adapter = netdev_priv(netdev);
	struct ixgbevf_ring *tx_ring = NULL, *rx_ring = NULL;
	int i, err = 0;
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = max(ring->rx_pending, (u32)IXGBEVF_MIN_RXD);
	new_rx_count = min(new_rx_count, (u32)IXGBEVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = max(ring->tx_pending, (u32)IXGBEVF_MIN_TXD);
	new_tx_count = min(new_tx_count, (u32)IXGBEVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, IXGBE_REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring->count) &&
	    (new_rx_count == adapter->rx_ring->count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IXGBEVF_RESETTING, &adapter->state))
		msleep(1);

	/*
	 * If the adapter isn't up and running then just set the
	 * new parameters and scurry for the exits.
	 */
	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i].count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i].count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	tx_ring = kcalloc(adapter->num_tx_queues,
			  sizeof(struct ixgbevf_ring), GFP_KERNEL);
	if (!tx_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	rx_ring = kcalloc(adapter->num_rx_queues,
			  sizeof(struct ixgbevf_ring), GFP_KERNEL);
	if (!rx_ring) {
		err = -ENOMEM;
		goto err_rx_setup;
	}

	ixgbevf_down(adapter);

	memcpy(tx_ring, adapter->tx_ring,
	       adapter->num_tx_queues * sizeof(struct ixgbevf_ring));
	for (i = 0; i < adapter->num_tx_queues; i++) {
		tx_ring[i].count = new_tx_count;
		err = ixgbevf_setup_tx_resources(adapter, &tx_ring[i]);
		if (err) {
			while (i) {
				i--;
				ixgbevf_free_tx_resources(adapter,
							  &tx_ring[i]);
			}
			goto err_tx_ring_setup;
		}
		tx_ring[i].v_idx = adapter->tx_ring[i].v_idx;
	}

	memcpy(rx_ring, adapter->rx_ring,
	       adapter->num_rx_queues * sizeof(struct ixgbevf_ring));
	for (i = 0; i < adapter->num_rx_queues; i++) {
		rx_ring[i].count = new_rx_count;
		err = ixgbevf_setup_rx_resources(adapter, &rx_ring[i]);
		if (err) {
			while (i) {
				i--;
				ixgbevf_free_rx_resources(adapter,
							  &rx_ring[i]);
			}
				goto err_rx_ring_setup;
		}
		rx_ring[i].v_idx = adapter->rx_ring[i].v_idx;
	}

	/*
	 * Only switch to new rings if all the prior allocations
	 * and ring setups have succeeded.
	 */
	kfree(adapter->tx_ring);
	adapter->tx_ring = tx_ring;
	adapter->tx_ring_count = new_tx_count;

	kfree(adapter->rx_ring);
	adapter->rx_ring = rx_ring;
	adapter->rx_ring_count = new_rx_count;

	/* success! */
	ixgbevf_up(adapter);

	goto clear_reset;

err_rx_ring_setup:
	for(i = 0; i < adapter->num_tx_queues; i++)
		ixgbevf_free_tx_resources(adapter, &tx_ring[i]);

err_tx_ring_setup:
	kfree(rx_ring);

err_rx_setup:
	kfree(tx_ring);

clear_reset:
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
	int i;

	ixgbevf_update_stats(adapter);
	for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
		char *p = (char *)adapter +
			ixgbe_gstrings_stats[i].stat_offset;
		char *b = (char *)adapter +
			ixgbe_gstrings_stats[i].base_stat_offset;
		char *r = (char *)adapter +
			ixgbe_gstrings_stats[i].saved_reset_offset;
		data[i] = ((ixgbe_gstrings_stats[i].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)p : *(u32 *)p) -
			  ((ixgbe_gstrings_stats[i].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)b : *(u32 *)b) +
			  ((ixgbe_gstrings_stats[i].sizeof_stat ==
			    sizeof(u64)) ? *(u64 *)r : *(u32 *)r);
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
	{ 0, 0, 0, 0 }
};

static const u32 register_test_patterns[] = {
	0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF
};

#define REG_PATTERN_TEST(R, M, W)                                             \
{                                                                             \
	u32 pat, val, before;                                                 \
	for (pat = 0; pat < ARRAY_SIZE(register_test_patterns); pat++) {      \
		before = readl(adapter->hw.hw_addr + R);                      \
		writel((register_test_patterns[pat] & W),                     \
		       (adapter->hw.hw_addr + R));                            \
		val = readl(adapter->hw.hw_addr + R);                         \
		if (val != (register_test_patterns[pat] & W & M)) {           \
			hw_dbg(&adapter->hw,                                  \
			"pattern test reg %04X failed: got "                  \
			"0x%08X expected 0x%08X\n",                           \
			R, val, (register_test_patterns[pat] & W & M));       \
			*data = R;                                            \
			writel(before, adapter->hw.hw_addr + R);              \
			return 1;                                             \
		}                                                             \
		writel(before, adapter->hw.hw_addr + R);                      \
	}                                                                     \
}

#define REG_SET_AND_CHECK(R, M, W)                                            \
{                                                                             \
	u32 val, before;                                                      \
	before = readl(adapter->hw.hw_addr + R);                              \
	writel((W & M), (adapter->hw.hw_addr + R));                           \
	val = readl(adapter->hw.hw_addr + R);                                 \
	if ((W & M) != (val & M)) {                                           \
		pr_err("set/check reg %04X test failed: got 0x%08X expected " \
		       "0x%08X\n", R, (val & M), (W & M));                    \
		*data = R;                                                    \
		writel(before, (adapter->hw.hw_addr + R));                    \
		return 1;                                                     \
	}                                                                     \
	writel(before, (adapter->hw.hw_addr + R));                            \
}

static int ixgbevf_reg_test(struct ixgbevf_adapter *adapter, u64 *data)
{
	const struct ixgbevf_reg_test *test;
	u32 i;

	test = reg_test_vf;

	/*
	 * Perform the register test, looping through the test table
	 * until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			switch (test->test_type) {
			case PATTERN_TEST:
				REG_PATTERN_TEST(test->reg + (i * 0x40),
						test->mask,
						test->write);
				break;
			case SET_READ_TEST:
				REG_SET_AND_CHECK(test->reg + (i * 0x40),
						test->mask,
						test->write);
				break;
			case WRITE_NO_TEST:
				writel(test->write,
				       (adapter->hw.hw_addr + test->reg)
				       + (i * 0x40));
				break;
			case TABLE32_TEST:
				REG_PATTERN_TEST(test->reg + (i * 4),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_LO:
				REG_PATTERN_TEST(test->reg + (i * 8),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_HI:
				REG_PATTERN_TEST((test->reg + 4) + (i * 8),
						test->mask,
						test->write);
				break;
			}
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

	set_bit(__IXGBEVF_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		hw_dbg(&adapter->hw, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result */
		if (ixgbevf_link_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			ixgbevf_reset(adapter);

		hw_dbg(&adapter->hw, "register testing starting\n");
		if (ixgbevf_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbevf_reset(adapter);

		clear_bit(__IXGBEVF_TESTING, &adapter->state);
		if (if_running)
			dev_open(netdev);
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

	if (netif_running(netdev)) {
		if (!adapter->dev_closed)
			ixgbevf_reinit_locked(adapter);
	}

	return 0;
}

static const struct ethtool_ops ixgbevf_ethtool_ops = {
	.get_settings           = ixgbevf_get_settings,
	.get_drvinfo            = ixgbevf_get_drvinfo,
	.get_regs_len           = ixgbevf_get_regs_len,
	.get_regs               = ixgbevf_get_regs,
	.nway_reset             = ixgbevf_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_ringparam          = ixgbevf_get_ringparam,
	.set_ringparam          = ixgbevf_set_ringparam,
	.get_msglevel           = ixgbevf_get_msglevel,
	.set_msglevel           = ixgbevf_set_msglevel,
	.self_test              = ixgbevf_diag_test,
	.get_sset_count         = ixgbevf_get_sset_count,
	.get_strings            = ixgbevf_get_strings,
	.get_ethtool_stats      = ixgbevf_get_ethtool_stats,
};

void ixgbevf_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &ixgbevf_ethtool_ops);
}

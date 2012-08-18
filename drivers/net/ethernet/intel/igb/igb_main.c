/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2012 Intel Corporation.

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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/net_tstamp.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/if_ether.h>
#include <linux/aer.h>
#include <linux/prefetch.h>
#include <linux/pm_runtime.h>
#ifdef CONFIG_IGB_DCA
#include <linux/dca.h>
#endif
#include "igb.h"

#define MAJ 4
#define MIN 0
#define BUILD 1
#define DRV_VERSION __stringify(MAJ) "." __stringify(MIN) "." \
__stringify(BUILD) "-k"
char igb_driver_name[] = "igb";
char igb_driver_version[] = DRV_VERSION;
static const char igb_driver_string[] =
				"Intel(R) Gigabit Ethernet Network Driver";
static const char igb_copyright[] = "Copyright (c) 2007-2012 Intel Corporation.";

static const struct e1000_info *igb_info_tbl[] = {
	[board_82575] = &e1000_82575_info,
};

static DEFINE_PCI_DEVICE_TABLE(igb_pci_tbl) = {
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I211_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I210_SGMII), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I350_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I350_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I350_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_I350_SGMII), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_QUAD_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_SGMII), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82580_COPPER_DUAL), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_DH89XXCC_SGMII), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_DH89XXCC_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_DH89XXCC_BACKPLANE), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_DH89XXCC_SFP), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_NS), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_NS_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_FIBER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_SERDES_QUAD), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_QUAD_COPPER_ET2), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82576_QUAD_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575EB_COPPER), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575EB_FIBER_SERDES), board_82575 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82575GB_QUAD_COPPER), board_82575 },
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, igb_pci_tbl);

void igb_reset(struct igb_adapter *);
static int igb_setup_all_tx_resources(struct igb_adapter *);
static int igb_setup_all_rx_resources(struct igb_adapter *);
static void igb_free_all_tx_resources(struct igb_adapter *);
static void igb_free_all_rx_resources(struct igb_adapter *);
static void igb_setup_mrqc(struct igb_adapter *);
static int igb_probe(struct pci_dev *, const struct pci_device_id *);
static void __devexit igb_remove(struct pci_dev *pdev);
static int igb_sw_init(struct igb_adapter *);
static int igb_open(struct net_device *);
static int igb_close(struct net_device *);
static void igb_configure_tx(struct igb_adapter *);
static void igb_configure_rx(struct igb_adapter *);
static void igb_clean_all_tx_rings(struct igb_adapter *);
static void igb_clean_all_rx_rings(struct igb_adapter *);
static void igb_clean_tx_ring(struct igb_ring *);
static void igb_clean_rx_ring(struct igb_ring *);
static void igb_set_rx_mode(struct net_device *);
static void igb_update_phy_info(unsigned long);
static void igb_watchdog(unsigned long);
static void igb_watchdog_task(struct work_struct *);
static netdev_tx_t igb_xmit_frame(struct sk_buff *skb, struct net_device *);
static struct rtnl_link_stats64 *igb_get_stats64(struct net_device *dev,
						 struct rtnl_link_stats64 *stats);
static int igb_change_mtu(struct net_device *, int);
static int igb_set_mac(struct net_device *, void *);
static void igb_set_uta(struct igb_adapter *adapter);
static irqreturn_t igb_intr(int irq, void *);
static irqreturn_t igb_intr_msi(int irq, void *);
static irqreturn_t igb_msix_other(int irq, void *);
static irqreturn_t igb_msix_ring(int irq, void *);
#ifdef CONFIG_IGB_DCA
static void igb_update_dca(struct igb_q_vector *);
static void igb_setup_dca(struct igb_adapter *);
#endif /* CONFIG_IGB_DCA */
static int igb_poll(struct napi_struct *, int);
static bool igb_clean_tx_irq(struct igb_q_vector *);
static bool igb_clean_rx_irq(struct igb_q_vector *, int);
static int igb_ioctl(struct net_device *, struct ifreq *, int cmd);
static void igb_tx_timeout(struct net_device *);
static void igb_reset_task(struct work_struct *);
static void igb_vlan_mode(struct net_device *netdev, netdev_features_t features);
static int igb_vlan_rx_add_vid(struct net_device *, u16);
static int igb_vlan_rx_kill_vid(struct net_device *, u16);
static void igb_restore_vlan(struct igb_adapter *);
static void igb_rar_set_qsel(struct igb_adapter *, u8 *, u32 , u8);
static void igb_ping_all_vfs(struct igb_adapter *);
static void igb_msg_task(struct igb_adapter *);
static void igb_vmm_control(struct igb_adapter *);
static int igb_set_vf_mac(struct igb_adapter *, int, unsigned char *);
static void igb_restore_vf_multicasts(struct igb_adapter *adapter);
static int igb_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac);
static int igb_ndo_set_vf_vlan(struct net_device *netdev,
			       int vf, u16 vlan, u8 qos);
static int igb_ndo_set_vf_bw(struct net_device *netdev, int vf, int tx_rate);
static int igb_ndo_get_vf_config(struct net_device *netdev, int vf,
				 struct ifla_vf_info *ivi);
static void igb_check_vf_rate_limit(struct igb_adapter *);

#ifdef CONFIG_PCI_IOV
static int igb_vf_configure(struct igb_adapter *adapter, int vf);
static int igb_find_enabled_vfs(struct igb_adapter *adapter);
static int igb_check_vf_assignment(struct igb_adapter *adapter);
#endif

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
static int igb_suspend(struct device *);
#endif
static int igb_resume(struct device *);
#ifdef CONFIG_PM_RUNTIME
static int igb_runtime_suspend(struct device *dev);
static int igb_runtime_resume(struct device *dev);
static int igb_runtime_idle(struct device *dev);
#endif
static const struct dev_pm_ops igb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(igb_suspend, igb_resume)
	SET_RUNTIME_PM_OPS(igb_runtime_suspend, igb_runtime_resume,
			igb_runtime_idle)
};
#endif
static void igb_shutdown(struct pci_dev *);
#ifdef CONFIG_IGB_DCA
static int igb_notify_dca(struct notifier_block *, unsigned long, void *);
static struct notifier_block dca_notifier = {
	.notifier_call	= igb_notify_dca,
	.next		= NULL,
	.priority	= 0
};
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
/* for netdump / net console */
static void igb_netpoll(struct net_device *);
#endif
#ifdef CONFIG_PCI_IOV
static unsigned int max_vfs = 0;
module_param(max_vfs, uint, 0);
MODULE_PARM_DESC(max_vfs, "Maximum number of virtual functions to allocate "
                 "per physical function");
#endif /* CONFIG_PCI_IOV */

static pci_ers_result_t igb_io_error_detected(struct pci_dev *,
		     pci_channel_state_t);
static pci_ers_result_t igb_io_slot_reset(struct pci_dev *);
static void igb_io_resume(struct pci_dev *);

static struct pci_error_handlers igb_err_handler = {
	.error_detected = igb_io_error_detected,
	.slot_reset = igb_io_slot_reset,
	.resume = igb_io_resume,
};

static void igb_init_dmac(struct igb_adapter *adapter, u32 pba);

static struct pci_driver igb_driver = {
	.name     = igb_driver_name,
	.id_table = igb_pci_tbl,
	.probe    = igb_probe,
	.remove   = __devexit_p(igb_remove),
#ifdef CONFIG_PM
	.driver.pm = &igb_pm_ops,
#endif
	.shutdown = igb_shutdown,
	.err_handler = &igb_err_handler
};

MODULE_AUTHOR("Intel Corporation, <e1000-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Gigabit Ethernet Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK)
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

struct igb_reg_info {
	u32 ofs;
	char *name;
};

static const struct igb_reg_info igb_reg_info_tbl[] = {

	/* General Registers */
	{E1000_CTRL, "CTRL"},
	{E1000_STATUS, "STATUS"},
	{E1000_CTRL_EXT, "CTRL_EXT"},

	/* Interrupt Registers */
	{E1000_ICR, "ICR"},

	/* RX Registers */
	{E1000_RCTL, "RCTL"},
	{E1000_RDLEN(0), "RDLEN"},
	{E1000_RDH(0), "RDH"},
	{E1000_RDT(0), "RDT"},
	{E1000_RXDCTL(0), "RXDCTL"},
	{E1000_RDBAL(0), "RDBAL"},
	{E1000_RDBAH(0), "RDBAH"},

	/* TX Registers */
	{E1000_TCTL, "TCTL"},
	{E1000_TDBAL(0), "TDBAL"},
	{E1000_TDBAH(0), "TDBAH"},
	{E1000_TDLEN(0), "TDLEN"},
	{E1000_TDH(0), "TDH"},
	{E1000_TDT(0), "TDT"},
	{E1000_TXDCTL(0), "TXDCTL"},
	{E1000_TDFH, "TDFH"},
	{E1000_TDFT, "TDFT"},
	{E1000_TDFHS, "TDFHS"},
	{E1000_TDFPC, "TDFPC"},

	/* List Terminator */
	{}
};

/*
 * igb_regdump - register printout routine
 */
static void igb_regdump(struct e1000_hw *hw, struct igb_reg_info *reginfo)
{
	int n = 0;
	char rname[16];
	u32 regs[8];

	switch (reginfo->ofs) {
	case E1000_RDLEN(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDLEN(n));
		break;
	case E1000_RDH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDH(n));
		break;
	case E1000_RDT(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDT(n));
		break;
	case E1000_RXDCTL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RXDCTL(n));
		break;
	case E1000_RDBAL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDBAL(n));
		break;
	case E1000_RDBAH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDBAH(n));
		break;
	case E1000_TDBAL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_RDBAL(n));
		break;
	case E1000_TDBAH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_TDBAH(n));
		break;
	case E1000_TDLEN(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_TDLEN(n));
		break;
	case E1000_TDH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_TDH(n));
		break;
	case E1000_TDT(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_TDT(n));
		break;
	case E1000_TXDCTL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(E1000_TXDCTL(n));
		break;
	default:
		pr_info("%-15s %08x\n", reginfo->name, rd32(reginfo->ofs));
		return;
	}

	snprintf(rname, 16, "%s%s", reginfo->name, "[0-3]");
	pr_info("%-15s %08x %08x %08x %08x\n", rname, regs[0], regs[1],
		regs[2], regs[3]);
}

/*
 * igb_dump - Print registers, tx-rings and rx-rings
 */
static void igb_dump(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	struct igb_reg_info *reginfo;
	struct igb_ring *tx_ring;
	union e1000_adv_tx_desc *tx_desc;
	struct my_u0 { u64 a; u64 b; } *u0;
	struct igb_ring *rx_ring;
	union e1000_adv_rx_desc *rx_desc;
	u32 staterr;
	u16 i, n;

	if (!netif_msg_hw(adapter))
		return;

	/* Print netdevice Info */
	if (netdev) {
		dev_info(&adapter->pdev->dev, "Net device Info\n");
		pr_info("Device Name     state            trans_start      "
			"last_rx\n");
		pr_info("%-15s %016lX %016lX %016lX\n", netdev->name,
			netdev->state, netdev->trans_start, netdev->last_rx);
	}

	/* Print Registers */
	dev_info(&adapter->pdev->dev, "Register Dump\n");
	pr_info(" Register Name   Value\n");
	for (reginfo = (struct igb_reg_info *)igb_reg_info_tbl;
	     reginfo->name; reginfo++) {
		igb_regdump(hw, reginfo);
	}

	/* Print TX Ring Summary */
	if (!netdev || !netif_running(netdev))
		goto exit;

	dev_info(&adapter->pdev->dev, "TX Rings Summary\n");
	pr_info("Queue [NTU] [NTC] [bi(ntc)->dma  ] leng ntw timestamp\n");
	for (n = 0; n < adapter->num_tx_queues; n++) {
		struct igb_tx_buffer *buffer_info;
		tx_ring = adapter->tx_ring[n];
		buffer_info = &tx_ring->tx_buffer_info[tx_ring->next_to_clean];
		pr_info(" %5d %5X %5X %016llX %04X %p %016llX\n",
			n, tx_ring->next_to_use, tx_ring->next_to_clean,
			(u64)buffer_info->dma,
			buffer_info->length,
			buffer_info->next_to_watch,
			(u64)buffer_info->time_stamp);
	}

	/* Print TX Rings */
	if (!netif_msg_tx_done(adapter))
		goto rx_ring_summary;

	dev_info(&adapter->pdev->dev, "TX Rings Dump\n");

	/* Transmit Descriptor Formats
	 *
	 * Advanced Transmit Descriptor
	 *   +--------------------------------------------------------------+
	 * 0 |         Buffer Address [63:0]                                |
	 *   +--------------------------------------------------------------+
	 * 8 | PAYLEN  | PORTS  |CC|IDX | STA | DCMD  |DTYP|MAC|RSV| DTALEN |
	 *   +--------------------------------------------------------------+
	 *   63      46 45    40 39 38 36 35 32 31   24             15       0
	 */

	for (n = 0; n < adapter->num_tx_queues; n++) {
		tx_ring = adapter->tx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("TX QUEUE INDEX = %d\n", tx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("T [desc]     [address 63:0  ] [PlPOCIStDDM Ln] "
			"[bi->dma       ] leng  ntw timestamp        "
			"bi->skb\n");

		for (i = 0; tx_ring->desc && (i < tx_ring->count); i++) {
			const char *next_desc;
			struct igb_tx_buffer *buffer_info;
			tx_desc = IGB_TX_DESC(tx_ring, i);
			buffer_info = &tx_ring->tx_buffer_info[i];
			u0 = (struct my_u0 *)tx_desc;
			if (i == tx_ring->next_to_use &&
			    i == tx_ring->next_to_clean)
				next_desc = " NTC/U";
			else if (i == tx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == tx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			pr_info("T [0x%03X]    %016llX %016llX %016llX"
				" %04X  %p %016llX %p%s\n", i,
				le64_to_cpu(u0->a),
				le64_to_cpu(u0->b),
				(u64)buffer_info->dma,
				buffer_info->length,
				buffer_info->next_to_watch,
				(u64)buffer_info->time_stamp,
				buffer_info->skb, next_desc);

			if (netif_msg_pktdata(adapter) && buffer_info->skb)
				print_hex_dump(KERN_INFO, "",
					DUMP_PREFIX_ADDRESS,
					16, 1, buffer_info->skb->data,
					buffer_info->length, true);
		}
	}

	/* Print RX Rings Summary */
rx_ring_summary:
	dev_info(&adapter->pdev->dev, "RX Rings Summary\n");
	pr_info("Queue [NTU] [NTC]\n");
	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info(" %5d %5X %5X\n",
			n, rx_ring->next_to_use, rx_ring->next_to_clean);
	}

	/* Print RX Rings */
	if (!netif_msg_rx_status(adapter))
		goto exit;

	dev_info(&adapter->pdev->dev, "RX Rings Dump\n");

	/* Advanced Receive Descriptor (Read) Format
	 *    63                                           1        0
	 *    +-----------------------------------------------------+
	 *  0 |       Packet Buffer Address [63:1]           |A0/NSE|
	 *    +----------------------------------------------+------+
	 *  8 |       Header Buffer Address [63:1]           |  DD  |
	 *    +-----------------------------------------------------+
	 *
	 *
	 * Advanced Receive Descriptor (Write-Back) Format
	 *
	 *   63       48 47    32 31  30      21 20 17 16   4 3     0
	 *   +------------------------------------------------------+
	 * 0 | Packet     IP     |SPH| HDR_LEN   | RSV|Packet|  RSS |
	 *   | Checksum   Ident  |   |           |    | Type | Type |
	 *   +------------------------------------------------------+
	 * 8 | VLAN Tag | Length | Extended Error | Extended Status |
	 *   +------------------------------------------------------+
	 *   63       48 47    32 31            20 19               0
	 */

	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("RX QUEUE INDEX = %d\n", rx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("R  [desc]      [ PktBuf     A0] [  HeadBuf   DD] "
			"[bi->dma       ] [bi->skb] <-- Adv Rx Read format\n");
		pr_info("RWB[desc]      [PcsmIpSHl PtRs] [vl er S cks ln] -----"
			"----------- [bi->skb] <-- Adv Rx Write-Back format\n");

		for (i = 0; i < rx_ring->count; i++) {
			const char *next_desc;
			struct igb_rx_buffer *buffer_info;
			buffer_info = &rx_ring->rx_buffer_info[i];
			rx_desc = IGB_RX_DESC(rx_ring, i);
			u0 = (struct my_u0 *)rx_desc;
			staterr = le32_to_cpu(rx_desc->wb.upper.status_error);

			if (i == rx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == rx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			if (staterr & E1000_RXD_STAT_DD) {
				/* Descriptor Done */
				pr_info("%s[0x%03X]     %016llX %016llX -------"
					"--------- %p%s\n", "RWB", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					buffer_info->skb, next_desc);
			} else {
				pr_info("%s[0x%03X]     %016llX %016llX %016llX"
					" %p%s\n", "R  ", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					(u64)buffer_info->dma,
					buffer_info->skb, next_desc);

				if (netif_msg_pktdata(adapter) &&
				    buffer_info->dma && buffer_info->skb) {
					print_hex_dump(KERN_INFO, "",
						  DUMP_PREFIX_ADDRESS,
						  16, 1, buffer_info->skb->data,
						  IGB_RX_HDR_LEN, true);
					print_hex_dump(KERN_INFO, "",
					  DUMP_PREFIX_ADDRESS,
					  16, 1,
					  page_address(buffer_info->page) +
						      buffer_info->page_offset,
					  PAGE_SIZE/2, true);
				}
			}
		}
	}

exit:
	return;
}

/**
 * igb_get_hw_dev - return device
 * used by hardware layer to print debugging information
 **/
struct net_device *igb_get_hw_dev(struct e1000_hw *hw)
{
	struct igb_adapter *adapter = hw->back;
	return adapter->netdev;
}

/**
 * igb_init_module - Driver Registration Routine
 *
 * igb_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init igb_init_module(void)
{
	int ret;
	pr_info("%s - version %s\n",
	       igb_driver_string, igb_driver_version);

	pr_info("%s\n", igb_copyright);

#ifdef CONFIG_IGB_DCA
	dca_register_notify(&dca_notifier);
#endif
	ret = pci_register_driver(&igb_driver);
	return ret;
}

module_init(igb_init_module);

/**
 * igb_exit_module - Driver Exit Cleanup Routine
 *
 * igb_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit igb_exit_module(void)
{
#ifdef CONFIG_IGB_DCA
	dca_unregister_notify(&dca_notifier);
#endif
	pci_unregister_driver(&igb_driver);
}

module_exit(igb_exit_module);

#define Q_IDX_82576(i) (((i & 0x1) << 3) + (i >> 1))
/**
 * igb_cache_ring_register - Descriptor ring to register mapping
 * @adapter: board private structure to initialize
 *
 * Once we know the feature-set enabled for the device, we'll cache
 * the register offset the descriptor ring is assigned to.
 **/
static void igb_cache_ring_register(struct igb_adapter *adapter)
{
	int i = 0, j = 0;
	u32 rbase_offset = adapter->vfs_allocated_count;

	switch (adapter->hw.mac.type) {
	case e1000_82576:
		/* The queues are allocated for virtualization such that VF 0
		 * is allocated queues 0 and 8, VF 1 queues 1 and 9, etc.
		 * In order to avoid collision we start at the first free queue
		 * and continue consuming queues in the same sequence
		 */
		if (adapter->vfs_allocated_count) {
			for (; i < adapter->rss_queues; i++)
				adapter->rx_ring[i]->reg_idx = rbase_offset +
				                               Q_IDX_82576(i);
		}
	case e1000_82575:
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
	default:
		for (; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->reg_idx = rbase_offset + i;
		for (; j < adapter->num_tx_queues; j++)
			adapter->tx_ring[j]->reg_idx = rbase_offset + j;
		break;
	}
}

static void igb_free_queues(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		kfree(adapter->tx_ring[i]);
		adapter->tx_ring[i] = NULL;
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		kfree(adapter->rx_ring[i]);
		adapter->rx_ring[i] = NULL;
	}
	adapter->num_rx_queues = 0;
	adapter->num_tx_queues = 0;
}

/**
 * igb_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.
 **/
static int igb_alloc_queues(struct igb_adapter *adapter)
{
	struct igb_ring *ring;
	int i;
	int orig_node = adapter->node;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		if (orig_node == -1) {
			int cur_node = next_online_node(adapter->node);
			if (cur_node == MAX_NUMNODES)
				cur_node = first_online_node;
			adapter->node = cur_node;
		}
		ring = kzalloc_node(sizeof(struct igb_ring), GFP_KERNEL,
				    adapter->node);
		if (!ring)
			ring = kzalloc(sizeof(struct igb_ring), GFP_KERNEL);
		if (!ring)
			goto err;
		ring->count = adapter->tx_ring_count;
		ring->queue_index = i;
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;
		ring->numa_node = adapter->node;
		/* For 82575, context index must be unique per ring. */
		if (adapter->hw.mac.type == e1000_82575)
			set_bit(IGB_RING_FLAG_TX_CTX_IDX, &ring->flags);
		adapter->tx_ring[i] = ring;
	}
	/* Restore the adapter's original node */
	adapter->node = orig_node;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		if (orig_node == -1) {
			int cur_node = next_online_node(adapter->node);
			if (cur_node == MAX_NUMNODES)
				cur_node = first_online_node;
			adapter->node = cur_node;
		}
		ring = kzalloc_node(sizeof(struct igb_ring), GFP_KERNEL,
				    adapter->node);
		if (!ring)
			ring = kzalloc(sizeof(struct igb_ring), GFP_KERNEL);
		if (!ring)
			goto err;
		ring->count = adapter->rx_ring_count;
		ring->queue_index = i;
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;
		ring->numa_node = adapter->node;
		/* set flag indicating ring supports SCTP checksum offload */
		if (adapter->hw.mac.type >= e1000_82576)
			set_bit(IGB_RING_FLAG_RX_SCTP_CSUM, &ring->flags);

		/*
		 * On i350, i210, and i211, loopback VLAN packets
		 * have the tag byte-swapped.
		 * */
		if (adapter->hw.mac.type >= e1000_i350)
			set_bit(IGB_RING_FLAG_RX_LB_VLAN_BSWAP, &ring->flags);

		adapter->rx_ring[i] = ring;
	}
	/* Restore the adapter's original node */
	adapter->node = orig_node;

	igb_cache_ring_register(adapter);

	return 0;

err:
	/* Restore the adapter's original node */
	adapter->node = orig_node;
	igb_free_queues(adapter);

	return -ENOMEM;
}

/**
 *  igb_write_ivar - configure ivar for given MSI-X vector
 *  @hw: pointer to the HW structure
 *  @msix_vector: vector number we are allocating to a given ring
 *  @index: row index of IVAR register to write within IVAR table
 *  @offset: column offset of in IVAR, should be multiple of 8
 *
 *  This function is intended to handle the writing of the IVAR register
 *  for adapters 82576 and newer.  The IVAR table consists of 2 columns,
 *  each containing an cause allocation for an Rx and Tx ring, and a
 *  variable number of rows depending on the number of queues supported.
 **/
static void igb_write_ivar(struct e1000_hw *hw, int msix_vector,
			   int index, int offset)
{
	u32 ivar = array_rd32(E1000_IVAR0, index);

	/* clear any bits that are currently set */
	ivar &= ~((u32)0xFF << offset);

	/* write vector and valid bit */
	ivar |= (msix_vector | E1000_IVAR_VALID) << offset;

	array_wr32(E1000_IVAR0, index, ivar);
}

#define IGB_N0_QUEUE -1
static void igb_assign_vector(struct igb_q_vector *q_vector, int msix_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	struct e1000_hw *hw = &adapter->hw;
	int rx_queue = IGB_N0_QUEUE;
	int tx_queue = IGB_N0_QUEUE;
	u32 msixbm = 0;

	if (q_vector->rx.ring)
		rx_queue = q_vector->rx.ring->reg_idx;
	if (q_vector->tx.ring)
		tx_queue = q_vector->tx.ring->reg_idx;

	switch (hw->mac.type) {
	case e1000_82575:
		/* The 82575 assigns vectors using a bitmask, which matches the
		   bitmask for the EICR/EIMS/EIMC registers.  To assign one
		   or more queues to a vector, we write the appropriate bits
		   into the MSIXBM register for that vector. */
		if (rx_queue > IGB_N0_QUEUE)
			msixbm = E1000_EICR_RX_QUEUE0 << rx_queue;
		if (tx_queue > IGB_N0_QUEUE)
			msixbm |= E1000_EICR_TX_QUEUE0 << tx_queue;
		if (!adapter->msix_entries && msix_vector == 0)
			msixbm |= E1000_EIMS_OTHER;
		array_wr32(E1000_MSIXBM(0), msix_vector, msixbm);
		q_vector->eims_value = msixbm;
		break;
	case e1000_82576:
		/*
		 * 82576 uses a table that essentially consists of 2 columns
		 * with 8 rows.  The ordering is column-major so we use the
		 * lower 3 bits as the row index, and the 4th bit as the
		 * column offset.
		 */
		if (rx_queue > IGB_N0_QUEUE)
			igb_write_ivar(hw, msix_vector,
				       rx_queue & 0x7,
				       (rx_queue & 0x8) << 1);
		if (tx_queue > IGB_N0_QUEUE)
			igb_write_ivar(hw, msix_vector,
				       tx_queue & 0x7,
				       ((tx_queue & 0x8) << 1) + 8);
		q_vector->eims_value = 1 << msix_vector;
		break;
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		/*
		 * On 82580 and newer adapters the scheme is similar to 82576
		 * however instead of ordering column-major we have things
		 * ordered row-major.  So we traverse the table by using
		 * bit 0 as the column offset, and the remaining bits as the
		 * row index.
		 */
		if (rx_queue > IGB_N0_QUEUE)
			igb_write_ivar(hw, msix_vector,
				       rx_queue >> 1,
				       (rx_queue & 0x1) << 4);
		if (tx_queue > IGB_N0_QUEUE)
			igb_write_ivar(hw, msix_vector,
				       tx_queue >> 1,
				       ((tx_queue & 0x1) << 4) + 8);
		q_vector->eims_value = 1 << msix_vector;
		break;
	default:
		BUG();
		break;
	}

	/* add q_vector eims value to global eims_enable_mask */
	adapter->eims_enable_mask |= q_vector->eims_value;

	/* configure q_vector to set itr on first interrupt */
	q_vector->set_itr = 1;
}

/**
 * igb_configure_msix - Configure MSI-X hardware
 *
 * igb_configure_msix sets up the hardware to properly
 * generate MSI-X interrupts.
 **/
static void igb_configure_msix(struct igb_adapter *adapter)
{
	u32 tmp;
	int i, vector = 0;
	struct e1000_hw *hw = &adapter->hw;

	adapter->eims_enable_mask = 0;

	/* set vector for other causes, i.e. link changes */
	switch (hw->mac.type) {
	case e1000_82575:
		tmp = rd32(E1000_CTRL_EXT);
		/* enable MSI-X PBA support*/
		tmp |= E1000_CTRL_EXT_PBA_CLR;

		/* Auto-Mask interrupts upon ICR read. */
		tmp |= E1000_CTRL_EXT_EIAME;
		tmp |= E1000_CTRL_EXT_IRCA;

		wr32(E1000_CTRL_EXT, tmp);

		/* enable msix_other interrupt */
		array_wr32(E1000_MSIXBM(0), vector++,
		                      E1000_EIMS_OTHER);
		adapter->eims_other = E1000_EIMS_OTHER;

		break;

	case e1000_82576:
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		/* Turn on MSI-X capability first, or our settings
		 * won't stick.  And it will take days to debug. */
		wr32(E1000_GPIE, E1000_GPIE_MSIX_MODE |
		                E1000_GPIE_PBA | E1000_GPIE_EIAME |
		                E1000_GPIE_NSICR);

		/* enable msix_other interrupt */
		adapter->eims_other = 1 << vector;
		tmp = (vector++ | E1000_IVAR_VALID) << 8;

		wr32(E1000_IVAR_MISC, tmp);
		break;
	default:
		/* do nothing, since nothing else supports MSI-X */
		break;
	} /* switch (hw->mac.type) */

	adapter->eims_enable_mask |= adapter->eims_other;

	for (i = 0; i < adapter->num_q_vectors; i++)
		igb_assign_vector(adapter->q_vector[i], vector++);

	wrfl();
}

/**
 * igb_request_msix - Initialize MSI-X interrupts
 *
 * igb_request_msix allocates MSI-X vectors and requests interrupts from the
 * kernel.
 **/
static int igb_request_msix(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	int i, err = 0, vector = 0;

	err = request_irq(adapter->msix_entries[vector].vector,
	                  igb_msix_other, 0, netdev->name, adapter);
	if (err)
		goto out;
	vector++;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		struct igb_q_vector *q_vector = adapter->q_vector[i];

		q_vector->itr_register = hw->hw_addr + E1000_EITR(vector);

		if (q_vector->rx.ring && q_vector->tx.ring)
			sprintf(q_vector->name, "%s-TxRx-%u", netdev->name,
				q_vector->rx.ring->queue_index);
		else if (q_vector->tx.ring)
			sprintf(q_vector->name, "%s-tx-%u", netdev->name,
				q_vector->tx.ring->queue_index);
		else if (q_vector->rx.ring)
			sprintf(q_vector->name, "%s-rx-%u", netdev->name,
				q_vector->rx.ring->queue_index);
		else
			sprintf(q_vector->name, "%s-unused", netdev->name);

		err = request_irq(adapter->msix_entries[vector].vector,
		                  igb_msix_ring, 0, q_vector->name,
		                  q_vector);
		if (err)
			goto out;
		vector++;
	}

	igb_configure_msix(adapter);
	return 0;
out:
	return err;
}

static void igb_reset_interrupt_capability(struct igb_adapter *adapter)
{
	if (adapter->msix_entries) {
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IGB_FLAG_HAS_MSI) {
		pci_disable_msi(adapter->pdev);
	}
}

/**
 * igb_free_q_vectors - Free memory allocated for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void igb_free_q_vectors(struct igb_adapter *adapter)
{
	int v_idx;

	for (v_idx = 0; v_idx < adapter->num_q_vectors; v_idx++) {
		struct igb_q_vector *q_vector = adapter->q_vector[v_idx];
		adapter->q_vector[v_idx] = NULL;
		if (!q_vector)
			continue;
		netif_napi_del(&q_vector->napi);
		kfree(q_vector);
	}
	adapter->num_q_vectors = 0;
}

/**
 * igb_clear_interrupt_scheme - reset the device to a state of no interrupts
 *
 * This function resets the device so that it has 0 rx queues, tx queues, and
 * MSI-X interrupts allocated.
 */
static void igb_clear_interrupt_scheme(struct igb_adapter *adapter)
{
	igb_free_queues(adapter);
	igb_free_q_vectors(adapter);
	igb_reset_interrupt_capability(adapter);
}

/**
 * igb_set_interrupt_capability - set MSI or MSI-X if supported
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int igb_set_interrupt_capability(struct igb_adapter *adapter)
{
	int err;
	int numvecs, i;

	/* Number of supported queues. */
	adapter->num_rx_queues = adapter->rss_queues;
	if (adapter->vfs_allocated_count)
		adapter->num_tx_queues = 1;
	else
		adapter->num_tx_queues = adapter->rss_queues;

	/* start with one vector for every rx queue */
	numvecs = adapter->num_rx_queues;

	/* if tx handler is separate add 1 for every tx queue */
	if (!(adapter->flags & IGB_FLAG_QUEUE_PAIRS))
		numvecs += adapter->num_tx_queues;

	/* store the number of vectors reserved for queues */
	adapter->num_q_vectors = numvecs;

	/* add 1 vector for link status interrupts */
	numvecs++;
	adapter->msix_entries = kcalloc(numvecs, sizeof(struct msix_entry),
					GFP_KERNEL);

	if (!adapter->msix_entries)
		goto msi_only;

	for (i = 0; i < numvecs; i++)
		adapter->msix_entries[i].entry = i;

	err = pci_enable_msix(adapter->pdev,
			      adapter->msix_entries,
			      numvecs);
	if (err == 0)
		goto out;

	igb_reset_interrupt_capability(adapter);

	/* If we can't do MSI-X, try MSI */
msi_only:
#ifdef CONFIG_PCI_IOV
	/* disable SR-IOV for non MSI-X configurations */
	if (adapter->vf_data) {
		struct e1000_hw *hw = &adapter->hw;
		/* disable iov and allow time for transactions to clear */
		pci_disable_sriov(adapter->pdev);
		msleep(500);

		kfree(adapter->vf_data);
		adapter->vf_data = NULL;
		wr32(E1000_IOVCTL, E1000_IOVCTL_REUSE_VFQ);
		wrfl();
		msleep(100);
		dev_info(&adapter->pdev->dev, "IOV Disabled\n");
	}
#endif
	adapter->vfs_allocated_count = 0;
	adapter->rss_queues = 1;
	adapter->flags |= IGB_FLAG_QUEUE_PAIRS;
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;
	adapter->num_q_vectors = 1;
	if (!pci_enable_msi(adapter->pdev))
		adapter->flags |= IGB_FLAG_HAS_MSI;
out:
	/* Notify the stack of the (possibly) reduced queue counts. */
	rtnl_lock();
	netif_set_real_num_tx_queues(adapter->netdev, adapter->num_tx_queues);
	err = netif_set_real_num_rx_queues(adapter->netdev,
		adapter->num_rx_queues);
	rtnl_unlock();
	return err;
}

/**
 * igb_alloc_q_vectors - Allocate memory for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int igb_alloc_q_vectors(struct igb_adapter *adapter)
{
	struct igb_q_vector *q_vector;
	struct e1000_hw *hw = &adapter->hw;
	int v_idx;
	int orig_node = adapter->node;

	for (v_idx = 0; v_idx < adapter->num_q_vectors; v_idx++) {
		if ((adapter->num_q_vectors == (adapter->num_rx_queues +
						adapter->num_tx_queues)) &&
		    (adapter->num_rx_queues == v_idx))
			adapter->node = orig_node;
		if (orig_node == -1) {
			int cur_node = next_online_node(adapter->node);
			if (cur_node == MAX_NUMNODES)
				cur_node = first_online_node;
			adapter->node = cur_node;
		}
		q_vector = kzalloc_node(sizeof(struct igb_q_vector), GFP_KERNEL,
					adapter->node);
		if (!q_vector)
			q_vector = kzalloc(sizeof(struct igb_q_vector),
					   GFP_KERNEL);
		if (!q_vector)
			goto err_out;
		q_vector->adapter = adapter;
		q_vector->itr_register = hw->hw_addr + E1000_EITR(0);
		q_vector->itr_val = IGB_START_ITR;
		netif_napi_add(adapter->netdev, &q_vector->napi, igb_poll, 64);
		adapter->q_vector[v_idx] = q_vector;
	}
	/* Restore the adapter's original node */
	adapter->node = orig_node;

	return 0;

err_out:
	/* Restore the adapter's original node */
	adapter->node = orig_node;
	igb_free_q_vectors(adapter);
	return -ENOMEM;
}

static void igb_map_rx_ring_to_vector(struct igb_adapter *adapter,
                                      int ring_idx, int v_idx)
{
	struct igb_q_vector *q_vector = adapter->q_vector[v_idx];

	q_vector->rx.ring = adapter->rx_ring[ring_idx];
	q_vector->rx.ring->q_vector = q_vector;
	q_vector->rx.count++;
	q_vector->itr_val = adapter->rx_itr_setting;
	if (q_vector->itr_val && q_vector->itr_val <= 3)
		q_vector->itr_val = IGB_START_ITR;
}

static void igb_map_tx_ring_to_vector(struct igb_adapter *adapter,
                                      int ring_idx, int v_idx)
{
	struct igb_q_vector *q_vector = adapter->q_vector[v_idx];

	q_vector->tx.ring = adapter->tx_ring[ring_idx];
	q_vector->tx.ring->q_vector = q_vector;
	q_vector->tx.count++;
	q_vector->itr_val = adapter->tx_itr_setting;
	q_vector->tx.work_limit = adapter->tx_work_limit;
	if (q_vector->itr_val && q_vector->itr_val <= 3)
		q_vector->itr_val = IGB_START_ITR;
}

/**
 * igb_map_ring_to_vector - maps allocated queues to vectors
 *
 * This function maps the recently allocated queues to vectors.
 **/
static int igb_map_ring_to_vector(struct igb_adapter *adapter)
{
	int i;
	int v_idx = 0;

	if ((adapter->num_q_vectors < adapter->num_rx_queues) ||
	    (adapter->num_q_vectors < adapter->num_tx_queues))
		return -ENOMEM;

	if (adapter->num_q_vectors >=
	    (adapter->num_rx_queues + adapter->num_tx_queues)) {
		for (i = 0; i < adapter->num_rx_queues; i++)
			igb_map_rx_ring_to_vector(adapter, i, v_idx++);
		for (i = 0; i < adapter->num_tx_queues; i++)
			igb_map_tx_ring_to_vector(adapter, i, v_idx++);
	} else {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			if (i < adapter->num_tx_queues)
				igb_map_tx_ring_to_vector(adapter, i, v_idx);
			igb_map_rx_ring_to_vector(adapter, i, v_idx++);
		}
		for (; i < adapter->num_tx_queues; i++)
			igb_map_tx_ring_to_vector(adapter, i, v_idx++);
	}
	return 0;
}

/**
 * igb_init_interrupt_scheme - initialize interrupts, allocate queues/vectors
 *
 * This function initializes the interrupts and allocates all of the queues.
 **/
static int igb_init_interrupt_scheme(struct igb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int err;

	err = igb_set_interrupt_capability(adapter);
	if (err)
		return err;

	err = igb_alloc_q_vectors(adapter);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate memory for vectors\n");
		goto err_alloc_q_vectors;
	}

	err = igb_alloc_queues(adapter);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		goto err_alloc_queues;
	}

	err = igb_map_ring_to_vector(adapter);
	if (err) {
		dev_err(&pdev->dev, "Invalid q_vector to ring mapping\n");
		goto err_map_queues;
	}


	return 0;
err_map_queues:
	igb_free_queues(adapter);
err_alloc_queues:
	igb_free_q_vectors(adapter);
err_alloc_q_vectors:
	igb_reset_interrupt_capability(adapter);
	return err;
}

/**
 * igb_request_irq - initialize interrupts
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int igb_request_irq(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int err = 0;

	if (adapter->msix_entries) {
		err = igb_request_msix(adapter);
		if (!err)
			goto request_done;
		/* fall back to MSI */
		igb_clear_interrupt_scheme(adapter);
		if (!pci_enable_msi(pdev))
			adapter->flags |= IGB_FLAG_HAS_MSI;
		igb_free_all_tx_resources(adapter);
		igb_free_all_rx_resources(adapter);
		adapter->num_tx_queues = 1;
		adapter->num_rx_queues = 1;
		adapter->num_q_vectors = 1;
		err = igb_alloc_q_vectors(adapter);
		if (err) {
			dev_err(&pdev->dev,
			        "Unable to allocate memory for vectors\n");
			goto request_done;
		}
		err = igb_alloc_queues(adapter);
		if (err) {
			dev_err(&pdev->dev,
			        "Unable to allocate memory for queues\n");
			igb_free_q_vectors(adapter);
			goto request_done;
		}
		igb_setup_all_tx_resources(adapter);
		igb_setup_all_rx_resources(adapter);
	}

	igb_assign_vector(adapter->q_vector[0], 0);

	if (adapter->flags & IGB_FLAG_HAS_MSI) {
		err = request_irq(pdev->irq, igb_intr_msi, 0,
				  netdev->name, adapter);
		if (!err)
			goto request_done;

		/* fall back to legacy interrupts */
		igb_reset_interrupt_capability(adapter);
		adapter->flags &= ~IGB_FLAG_HAS_MSI;
	}

	err = request_irq(pdev->irq, igb_intr, IRQF_SHARED,
			  netdev->name, adapter);

	if (err)
		dev_err(&pdev->dev, "Error %d getting interrupt\n",
			err);

request_done:
	return err;
}

static void igb_free_irq(struct igb_adapter *adapter)
{
	if (adapter->msix_entries) {
		int vector = 0, i;

		free_irq(adapter->msix_entries[vector++].vector, adapter);

		for (i = 0; i < adapter->num_q_vectors; i++)
			free_irq(adapter->msix_entries[vector++].vector,
				 adapter->q_vector[i]);
	} else {
		free_irq(adapter->pdev->irq, adapter);
	}
}

/**
 * igb_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static void igb_irq_disable(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/*
	 * we need to be careful when disabling interrupts.  The VFs are also
	 * mapped into these registers and so clearing the bits can cause
	 * issues on the VF drivers so we only need to clear what we set
	 */
	if (adapter->msix_entries) {
		u32 regval = rd32(E1000_EIAM);
		wr32(E1000_EIAM, regval & ~adapter->eims_enable_mask);
		wr32(E1000_EIMC, adapter->eims_enable_mask);
		regval = rd32(E1000_EIAC);
		wr32(E1000_EIAC, regval & ~adapter->eims_enable_mask);
	}

	wr32(E1000_IAM, 0);
	wr32(E1000_IMC, ~0);
	wrfl();
	if (adapter->msix_entries) {
		int i;
		for (i = 0; i < adapter->num_q_vectors; i++)
			synchronize_irq(adapter->msix_entries[i].vector);
	} else {
		synchronize_irq(adapter->pdev->irq);
	}
}

/**
 * igb_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static void igb_irq_enable(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		u32 ims = E1000_IMS_LSC | E1000_IMS_DOUTSYNC | E1000_IMS_DRSTA;
		u32 regval = rd32(E1000_EIAC);
		wr32(E1000_EIAC, regval | adapter->eims_enable_mask);
		regval = rd32(E1000_EIAM);
		wr32(E1000_EIAM, regval | adapter->eims_enable_mask);
		wr32(E1000_EIMS, adapter->eims_enable_mask);
		if (adapter->vfs_allocated_count) {
			wr32(E1000_MBVFIMR, 0xFF);
			ims |= E1000_IMS_VMMB;
		}
		wr32(E1000_IMS, ims);
	} else {
		wr32(E1000_IMS, IMS_ENABLE_MASK |
				E1000_IMS_DRSTA);
		wr32(E1000_IAM, IMS_ENABLE_MASK |
				E1000_IMS_DRSTA);
	}
}

static void igb_update_mng_vlan(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 vid = adapter->hw.mng_cookie.vlan_id;
	u16 old_vid = adapter->mng_vlan_id;

	if (hw->mng_cookie.status & E1000_MNG_DHCP_COOKIE_STATUS_VLAN) {
		/* add VID to filter table */
		igb_vfta_set(hw, vid, true);
		adapter->mng_vlan_id = vid;
	} else {
		adapter->mng_vlan_id = IGB_MNG_VLAN_NONE;
	}

	if ((old_vid != (u16)IGB_MNG_VLAN_NONE) &&
	    (vid != old_vid) &&
	    !test_bit(old_vid, adapter->active_vlans)) {
		/* remove VID from filter table */
		igb_vfta_set(hw, old_vid, false);
	}
}

/**
 * igb_release_hw_control - release control of the h/w to f/w
 * @adapter: address of board private structure
 *
 * igb_release_hw_control resets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.
 *
 **/
static void igb_release_hw_control(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware take over control of h/w */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	wr32(E1000_CTRL_EXT,
			ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
}

/**
 * igb_get_hw_control - get control of the h/w from f/w
 * @adapter: address of board private structure
 *
 * igb_get_hw_control sets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded.
 *
 **/
static void igb_get_hw_control(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware know the driver has taken over */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	wr32(E1000_CTRL_EXT,
			ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
}

/**
 * igb_configure - configure the hardware for RX and TX
 * @adapter: private board structure
 **/
static void igb_configure(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	igb_get_hw_control(adapter);
	igb_set_rx_mode(netdev);

	igb_restore_vlan(adapter);

	igb_setup_tctl(adapter);
	igb_setup_mrqc(adapter);
	igb_setup_rctl(adapter);

	igb_configure_tx(adapter);
	igb_configure_rx(adapter);

	igb_rx_fifo_flush_82575(&adapter->hw);

	/* call igb_desc_unused which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igb_ring *ring = adapter->rx_ring[i];
		igb_alloc_rx_buffers(ring, igb_desc_unused(ring));
	}
}

/**
 * igb_power_up_link - Power up the phy/serdes link
 * @adapter: address of board private structure
 **/
void igb_power_up_link(struct igb_adapter *adapter)
{
	igb_reset_phy(&adapter->hw);

	if (adapter->hw.phy.media_type == e1000_media_type_copper)
		igb_power_up_phy_copper(&adapter->hw);
	else
		igb_power_up_serdes_link_82575(&adapter->hw);
}

/**
 * igb_power_down_link - Power down the phy/serdes link
 * @adapter: address of board private structure
 */
static void igb_power_down_link(struct igb_adapter *adapter)
{
	if (adapter->hw.phy.media_type == e1000_media_type_copper)
		igb_power_down_phy_copper_82575(&adapter->hw);
	else
		igb_shutdown_serdes_link_82575(&adapter->hw);
}

/**
 * igb_up - Open the interface and prepare it to handle traffic
 * @adapter: board private structure
 **/
int igb_up(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	int i;

	/* hardware has been reset, we need to reload some things */
	igb_configure(adapter);

	clear_bit(__IGB_DOWN, &adapter->state);

	for (i = 0; i < adapter->num_q_vectors; i++)
		napi_enable(&(adapter->q_vector[i]->napi));

	if (adapter->msix_entries)
		igb_configure_msix(adapter);
	else
		igb_assign_vector(adapter->q_vector[0], 0);

	/* Clear any pending interrupts. */
	rd32(E1000_ICR);
	igb_irq_enable(adapter);

	/* notify VFs that reset has been completed */
	if (adapter->vfs_allocated_count) {
		u32 reg_data = rd32(E1000_CTRL_EXT);
		reg_data |= E1000_CTRL_EXT_PFRSTD;
		wr32(E1000_CTRL_EXT, reg_data);
	}

	netif_tx_start_all_queues(adapter->netdev);

	/* start the watchdog. */
	hw->mac.get_link_status = 1;
	schedule_work(&adapter->watchdog_task);

	return 0;
}

void igb_down(struct igb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	u32 tctl, rctl;
	int i;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__IGB_DOWN, &adapter->state);

	/* disable receives in the hardware */
	rctl = rd32(E1000_RCTL);
	wr32(E1000_RCTL, rctl & ~E1000_RCTL_EN);
	/* flush and sleep below */

	netif_tx_stop_all_queues(netdev);

	/* disable transmits in the hardware */
	tctl = rd32(E1000_TCTL);
	tctl &= ~E1000_TCTL_EN;
	wr32(E1000_TCTL, tctl);
	/* flush both disables and wait for them to finish */
	wrfl();
	msleep(10);

	for (i = 0; i < adapter->num_q_vectors; i++)
		napi_disable(&(adapter->q_vector[i]->napi));

	igb_irq_disable(adapter);

	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	netif_carrier_off(netdev);

	/* record the stats before reset*/
	spin_lock(&adapter->stats64_lock);
	igb_update_stats(adapter, &adapter->stats64);
	spin_unlock(&adapter->stats64_lock);

	adapter->link_speed = 0;
	adapter->link_duplex = 0;

	if (!pci_channel_offline(adapter->pdev))
		igb_reset(adapter);
	igb_clean_all_tx_rings(adapter);
	igb_clean_all_rx_rings(adapter);
#ifdef CONFIG_IGB_DCA

	/* since we reset the hardware DCA settings were cleared */
	igb_setup_dca(adapter);
#endif
}

void igb_reinit_locked(struct igb_adapter *adapter)
{
	WARN_ON(in_interrupt());
	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);
	igb_down(adapter);
	igb_up(adapter);
	clear_bit(__IGB_RESETTING, &adapter->state);
}

void igb_reset(struct igb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_fc_info *fc = &hw->fc;
	u32 pba = 0, tx_space, min_tx_space, min_rx_space;
	u16 hwm;

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */
	switch (mac->type) {
	case e1000_i350:
	case e1000_82580:
		pba = rd32(E1000_RXPBS);
		pba = igb_rxpbs_adjust_82580(pba);
		break;
	case e1000_82576:
		pba = rd32(E1000_RXPBS);
		pba &= E1000_RXPBS_SIZE_MASK_82576;
		break;
	case e1000_82575:
	case e1000_i210:
	case e1000_i211:
	default:
		pba = E1000_PBA_34K;
		break;
	}

	if ((adapter->max_frame_size > ETH_FRAME_LEN + ETH_FCS_LEN) &&
	    (mac->type < e1000_82576)) {
		/* adjust PBA for jumbo frames */
		wr32(E1000_PBA, pba);

		/* To maintain wire speed transmits, the Tx FIFO should be
		 * large enough to accommodate two full transmit packets,
		 * rounded up to the next 1KB and expressed in KB.  Likewise,
		 * the Rx FIFO should be large enough to accommodate at least
		 * one full receive packet and is similarly rounded up and
		 * expressed in KB. */
		pba = rd32(E1000_PBA);
		/* upper 16 bits has Tx packet buffer allocation size in KB */
		tx_space = pba >> 16;
		/* lower 16 bits has Rx packet buffer allocation size in KB */
		pba &= 0xffff;
		/* the tx fifo also stores 16 bytes of information about the tx
		 * but don't include ethernet FCS because hardware appends it */
		min_tx_space = (adapter->max_frame_size +
				sizeof(union e1000_adv_tx_desc) -
				ETH_FCS_LEN) * 2;
		min_tx_space = ALIGN(min_tx_space, 1024);
		min_tx_space >>= 10;
		/* software strips receive CRC, so leave room for it */
		min_rx_space = adapter->max_frame_size;
		min_rx_space = ALIGN(min_rx_space, 1024);
		min_rx_space >>= 10;

		/* If current Tx allocation is less than the min Tx FIFO size,
		 * and the min Tx FIFO size is less than the current Rx FIFO
		 * allocation, take space away from current Rx allocation */
		if (tx_space < min_tx_space &&
		    ((min_tx_space - tx_space) < pba)) {
			pba = pba - (min_tx_space - tx_space);

			/* if short on rx space, rx wins and must trump tx
			 * adjustment */
			if (pba < min_rx_space)
				pba = min_rx_space;
		}
		wr32(E1000_PBA, pba);
	}

	/* flow control settings */
	/* The high water mark must be low enough to fit one full frame
	 * (or the size used for early receive) above it in the Rx FIFO.
	 * Set it to the lower of:
	 * - 90% of the Rx FIFO size, or
	 * - the full Rx FIFO size minus one full frame */
	hwm = min(((pba << 10) * 9 / 10),
			((pba << 10) - 2 * adapter->max_frame_size));

	fc->high_water = hwm & 0xFFF0;	/* 16-byte granularity */
	fc->low_water = fc->high_water - 16;
	fc->pause_time = 0xFFFF;
	fc->send_xon = 1;
	fc->current_mode = fc->requested_mode;

	/* disable receive for all VFs and wait one second */
	if (adapter->vfs_allocated_count) {
		int i;
		for (i = 0 ; i < adapter->vfs_allocated_count; i++)
			adapter->vf_data[i].flags &= IGB_VF_FLAG_PF_SET_MAC;

		/* ping all the active vfs to let them know we are going down */
		igb_ping_all_vfs(adapter);

		/* disable transmits and receives */
		wr32(E1000_VFRE, 0);
		wr32(E1000_VFTE, 0);
	}

	/* Allow time for pending master requests to run */
	hw->mac.ops.reset_hw(hw);
	wr32(E1000_WUC, 0);

	if (hw->mac.ops.init_hw(hw))
		dev_err(&pdev->dev, "Hardware Error\n");

	/*
	 * Flow control settings reset on hardware reset, so guarantee flow
	 * control is off when forcing speed.
	 */
	if (!hw->mac.autoneg)
		igb_force_mac_fc(hw);

	igb_init_dmac(adapter, pba);
	if (!netif_running(adapter->netdev))
		igb_power_down_link(adapter);

	igb_update_mng_vlan(adapter);

	/* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
	wr32(E1000_VET, ETHERNET_IEEE_VLAN_TYPE);

#ifdef CONFIG_IGB_PTP
	/* Re-enable PTP, where applicable. */
	igb_ptp_reset(adapter);
#endif /* CONFIG_IGB_PTP */

	igb_get_phy_info(hw);
}

static netdev_features_t igb_fix_features(struct net_device *netdev,
	netdev_features_t features)
{
	/*
	 * Since there is no support for separate rx/tx vlan accel
	 * enable/disable make sure tx flag is always in same state as rx.
	 */
	if (features & NETIF_F_HW_VLAN_RX)
		features |= NETIF_F_HW_VLAN_TX;
	else
		features &= ~NETIF_F_HW_VLAN_TX;

	return features;
}

static int igb_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (changed & NETIF_F_HW_VLAN_RX)
		igb_vlan_mode(netdev, features);

	if (!(changed & NETIF_F_RXALL))
		return 0;

	netdev->features = features;

	if (netif_running(netdev))
		igb_reinit_locked(adapter);
	else
		igb_reset(adapter);

	return 0;
}

static const struct net_device_ops igb_netdev_ops = {
	.ndo_open		= igb_open,
	.ndo_stop		= igb_close,
	.ndo_start_xmit		= igb_xmit_frame,
	.ndo_get_stats64	= igb_get_stats64,
	.ndo_set_rx_mode	= igb_set_rx_mode,
	.ndo_set_mac_address	= igb_set_mac,
	.ndo_change_mtu		= igb_change_mtu,
	.ndo_do_ioctl		= igb_ioctl,
	.ndo_tx_timeout		= igb_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= igb_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= igb_vlan_rx_kill_vid,
	.ndo_set_vf_mac		= igb_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= igb_ndo_set_vf_vlan,
	.ndo_set_vf_tx_rate	= igb_ndo_set_vf_bw,
	.ndo_get_vf_config	= igb_ndo_get_vf_config,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= igb_netpoll,
#endif
	.ndo_fix_features	= igb_fix_features,
	.ndo_set_features	= igb_set_features,
};

/**
 * igb_set_fw_version - Configure version string for ethtool
 * @adapter: adapter struct
 *
 **/
void igb_set_fw_version(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 eeprom_verh, eeprom_verl, comb_verh, comb_verl, comb_offset;
	u16 major, build, patch, fw_version;
	u32 etrack_id;

	hw->nvm.ops.read(hw, 5, 1, &fw_version);
	if (adapter->hw.mac.type != e1000_i211) {
		hw->nvm.ops.read(hw, NVM_ETRACK_WORD, 1, &eeprom_verh);
		hw->nvm.ops.read(hw, (NVM_ETRACK_WORD + 1), 1, &eeprom_verl);
		etrack_id = (eeprom_verh << IGB_ETRACK_SHIFT) | eeprom_verl;

		/* combo image version needs to be found */
		hw->nvm.ops.read(hw, NVM_COMB_VER_PTR, 1, &comb_offset);
		if ((comb_offset != 0x0) &&
		    (comb_offset != IGB_NVM_VER_INVALID)) {
			hw->nvm.ops.read(hw, (NVM_COMB_VER_OFF + comb_offset
					 + 1), 1, &comb_verh);
			hw->nvm.ops.read(hw, (NVM_COMB_VER_OFF + comb_offset),
					 1, &comb_verl);

			/* Only display Option Rom if it exists and is valid */
			if ((comb_verh && comb_verl) &&
			    ((comb_verh != IGB_NVM_VER_INVALID) &&
			     (comb_verl != IGB_NVM_VER_INVALID))) {
				major = comb_verl >> IGB_COMB_VER_SHFT;
				build = (comb_verl << IGB_COMB_VER_SHFT) |
					(comb_verh >> IGB_COMB_VER_SHFT);
				patch = comb_verh & IGB_COMB_VER_MASK;
				snprintf(adapter->fw_version,
					 sizeof(adapter->fw_version),
					 "%d.%d%d, 0x%08x, %d.%d.%d",
					 (fw_version & IGB_MAJOR_MASK) >>
					 IGB_MAJOR_SHIFT,
					 (fw_version & IGB_MINOR_MASK) >>
					 IGB_MINOR_SHIFT,
					 (fw_version & IGB_BUILD_MASK),
					 etrack_id, major, build, patch);
				goto out;
			}
		}
		snprintf(adapter->fw_version, sizeof(adapter->fw_version),
			 "%d.%d%d, 0x%08x",
			 (fw_version & IGB_MAJOR_MASK) >> IGB_MAJOR_SHIFT,
			 (fw_version & IGB_MINOR_MASK) >> IGB_MINOR_SHIFT,
			 (fw_version & IGB_BUILD_MASK), etrack_id);
	} else {
		snprintf(adapter->fw_version, sizeof(adapter->fw_version),
			 "%d.%d%d",
			 (fw_version & IGB_MAJOR_MASK) >> IGB_MAJOR_SHIFT,
			 (fw_version & IGB_MINOR_MASK) >> IGB_MINOR_SHIFT,
			 (fw_version & IGB_BUILD_MASK));
	}
out:
	return;
}

/**
 * igb_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in igb_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * igb_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit igb_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct igb_adapter *adapter;
	struct e1000_hw *hw;
	u16 eeprom_data = 0;
	s32 ret_val;
	static int global_quad_port_a; /* global quad port a indication */
	const struct e1000_info *ei = igb_info_tbl[ent->driver_data];
	unsigned long mmio_start, mmio_len;
	int err, pci_using_dac;
	u16 eeprom_apme_mask = IGB_EEPROM_APME;
	u8 part_str[E1000_PBANUM_LENGTH];

	/* Catch broken hardware that put the wrong VF device ID in
	 * the PCIe SR-IOV capability.
	 */
	if (pdev->is_virtfn) {
		WARN(1, KERN_ERR "%s (%hx:%hx) should not be a VF!\n",
			pci_name(pdev), pdev->vendor, pdev->device);
		return -EINVAL;
	}

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	pci_using_dac = 0;
	err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (!err) {
		err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
		if (!err)
			pci_using_dac = 1;
	} else {
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
			if (err) {
				dev_err(&pdev->dev, "No usable DMA "
					"configuration, aborting\n");
				goto err_dma;
			}
		}
	}

	err = pci_request_selected_regions(pdev, pci_select_bars(pdev,
	                                   IORESOURCE_MEM),
	                                   igb_driver_name);
	if (err)
		goto err_pci_reg;

	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);
	pci_save_state(pdev);

	err = -ENOMEM;
	netdev = alloc_etherdev_mq(sizeof(struct igb_adapter),
				   IGB_MAX_TX_QUEUES);
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	err = -EIO;
	hw->hw_addr = ioremap(mmio_start, mmio_len);
	if (!hw->hw_addr)
		goto err_ioremap;

	netdev->netdev_ops = &igb_netdev_ops;
	igb_set_ethtool_ops(netdev);
	netdev->watchdog_timeo = 5 * HZ;

	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* Copy the default MAC, PHY and NVM function pointers */
	memcpy(&hw->mac.ops, ei->mac_ops, sizeof(hw->mac.ops));
	memcpy(&hw->phy.ops, ei->phy_ops, sizeof(hw->phy.ops));
	memcpy(&hw->nvm.ops, ei->nvm_ops, sizeof(hw->nvm.ops));
	/* Initialize skew-specific constants */
	err = ei->get_invariants(hw);
	if (err)
		goto err_sw_init;

	/* setup the private structure */
	err = igb_sw_init(adapter);
	if (err)
		goto err_sw_init;

	igb_get_bus_info_pcie(hw);

	hw->phy.autoneg_wait_to_complete = false;

	/* Copper options */
	if (hw->phy.media_type == e1000_media_type_copper) {
		hw->phy.mdix = AUTO_ALL_MODES;
		hw->phy.disable_polarity_correction = false;
		hw->phy.ms_type = e1000_ms_hw_default;
	}

	if (igb_check_reset_block(hw))
		dev_info(&pdev->dev,
			"PHY reset is blocked due to SOL/IDER session.\n");

	/*
	 * features is initialized to 0 in allocation, it might have bits
	 * set by igb_sw_init so we should use an or instead of an
	 * assignment.
	 */
	netdev->features |= NETIF_F_SG |
			    NETIF_F_IP_CSUM |
			    NETIF_F_IPV6_CSUM |
			    NETIF_F_TSO |
			    NETIF_F_TSO6 |
			    NETIF_F_RXHASH |
			    NETIF_F_RXCSUM |
			    NETIF_F_HW_VLAN_RX |
			    NETIF_F_HW_VLAN_TX;

	/* copy netdev features into list of user selectable features */
	netdev->hw_features |= netdev->features;
	netdev->hw_features |= NETIF_F_RXALL;

	/* set this bit last since it cannot be part of hw_features */
	netdev->features |= NETIF_F_HW_VLAN_FILTER;

	netdev->vlan_features |= NETIF_F_TSO |
				 NETIF_F_TSO6 |
				 NETIF_F_IP_CSUM |
				 NETIF_F_IPV6_CSUM |
				 NETIF_F_SG;

	netdev->priv_flags |= IFF_SUPP_NOFCS;

	if (pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (hw->mac.type >= e1000_82576) {
		netdev->hw_features |= NETIF_F_SCTP_CSUM;
		netdev->features |= NETIF_F_SCTP_CSUM;
	}

	netdev->priv_flags |= IFF_UNICAST_FLT;

	adapter->en_mng_pt = igb_enable_mng_pass_thru(hw);

	/* before reading the NVM, reset the controller to put the device in a
	 * known good starting state */
	hw->mac.ops.reset_hw(hw);

	/*
	 * make sure the NVM is good , i211 parts have special NVM that
	 * doesn't contain a checksum
	 */
	if (hw->mac.type != e1000_i211) {
		if (hw->nvm.ops.validate(hw) < 0) {
			dev_err(&pdev->dev, "The NVM Checksum Is Not Valid\n");
			err = -EIO;
			goto err_eeprom;
		}
	}

	/* copy the MAC address out of the NVM */
	if (hw->mac.ops.read_mac_addr(hw))
		dev_err(&pdev->dev, "NVM Read Error\n");

	memcpy(netdev->dev_addr, hw->mac.addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac.addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->perm_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		err = -EIO;
		goto err_eeprom;
	}

	/* get firmware version for ethtool -i */
	igb_set_fw_version(adapter);

	setup_timer(&adapter->watchdog_timer, igb_watchdog,
	            (unsigned long) adapter);
	setup_timer(&adapter->phy_info_timer, igb_update_phy_info,
	            (unsigned long) adapter);

	INIT_WORK(&adapter->reset_task, igb_reset_task);
	INIT_WORK(&adapter->watchdog_task, igb_watchdog_task);

	/* Initialize link properties that are user-changeable */
	adapter->fc_autoneg = true;
	hw->mac.autoneg = true;
	hw->phy.autoneg_advertised = 0x2f;

	hw->fc.requested_mode = e1000_fc_default;
	hw->fc.current_mode = e1000_fc_default;

	igb_validate_mdi_setting(hw);

	/* Initial Wake on LAN setting If APM wake is enabled in the EEPROM,
	 * enable the ACPI Magic Packet filter
	 */

	if (hw->bus.func == 0)
		hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
	else if (hw->mac.type >= e1000_82580)
		hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_A +
		                 NVM_82580_LAN_FUNC_OFFSET(hw->bus.func), 1,
		                 &eeprom_data);
	else if (hw->bus.func == 1)
		hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);

	if (eeprom_data & eeprom_apme_mask)
		adapter->eeprom_wol |= E1000_WUFC_MAG;

	/* now that we have the eeprom settings, apply the special cases where
	 * the eeprom may be wrong or the board simply won't support wake on
	 * lan on a particular port */
	switch (pdev->device) {
	case E1000_DEV_ID_82575GB_QUAD_COPPER:
		adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82575EB_FIBER_SERDES:
	case E1000_DEV_ID_82576_FIBER:
	case E1000_DEV_ID_82576_SERDES:
		/* Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting */
		if (rd32(E1000_STATUS) & E1000_STATUS_FUNC_1)
			adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82576_QUAD_COPPER:
	case E1000_DEV_ID_82576_QUAD_COPPER_ET2:
		/* if quad port adapter, disable WoL on all but port A */
		if (global_quad_port_a != 0)
			adapter->eeprom_wol = 0;
		else
			adapter->flags |= IGB_FLAG_QUAD_PORT_A;
		/* Reset for multiple quad port adapters */
		if (++global_quad_port_a == 4)
			global_quad_port_a = 0;
		break;
	}

	/* initialize the wol settings based on the eeprom settings */
	adapter->wol = adapter->eeprom_wol;
	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	/* reset the hardware with the new settings */
	igb_reset(adapter);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

#ifdef CONFIG_IGB_DCA
	if (dca_add_requester(&pdev->dev) == 0) {
		adapter->flags |= IGB_FLAG_DCA_ENABLED;
		dev_info(&pdev->dev, "DCA enabled\n");
		igb_setup_dca(adapter);
	}

#endif

#ifdef CONFIG_IGB_PTP
	/* do hw tstamp init after resetting */
	igb_ptp_init(adapter);
#endif /* CONFIG_IGB_PTP */

	dev_info(&pdev->dev, "Intel(R) Gigabit Ethernet Network Connection\n");
	/* print bus type/speed/width info */
	dev_info(&pdev->dev, "%s: (PCIe:%s:%s) %pM\n",
		 netdev->name,
		 ((hw->bus.speed == e1000_bus_speed_2500) ? "2.5Gb/s" :
		  (hw->bus.speed == e1000_bus_speed_5000) ? "5.0Gb/s" :
		                                            "unknown"),
		 ((hw->bus.width == e1000_bus_width_pcie_x4) ? "Width x4" :
		  (hw->bus.width == e1000_bus_width_pcie_x2) ? "Width x2" :
		  (hw->bus.width == e1000_bus_width_pcie_x1) ? "Width x1" :
		   "unknown"),
		 netdev->dev_addr);

	ret_val = igb_read_part_string(hw, part_str, E1000_PBANUM_LENGTH);
	if (ret_val)
		strcpy(part_str, "Unknown");
	dev_info(&pdev->dev, "%s: PBA No: %s\n", netdev->name, part_str);
	dev_info(&pdev->dev,
		"Using %s interrupts. %d rx queue(s), %d tx queue(s)\n",
		adapter->msix_entries ? "MSI-X" :
		(adapter->flags & IGB_FLAG_HAS_MSI) ? "MSI" : "legacy",
		adapter->num_rx_queues, adapter->num_tx_queues);
	switch (hw->mac.type) {
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		igb_set_eee_i350(hw);
		break;
	default:
		break;
	}

	pm_runtime_put_noidle(&pdev->dev);
	return 0;

err_register:
	igb_release_hw_control(adapter);
err_eeprom:
	if (!igb_check_reset_block(hw))
		igb_reset_phy(hw);

	if (hw->flash_address)
		iounmap(hw->flash_address);
err_sw_init:
	igb_clear_interrupt_scheme(adapter);
	iounmap(hw->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_selected_regions(pdev,
	                             pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * igb_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * igb_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void __devexit igb_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	pm_runtime_get_noresume(&pdev->dev);
#ifdef CONFIG_IGB_PTP
	igb_ptp_stop(adapter);
#endif /* CONFIG_IGB_PTP */

	/*
	 * The watchdog timer may be rescheduled, so explicitly
	 * disable watchdog from being rescheduled.
	 */
	set_bit(__IGB_DOWN, &adapter->state);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	cancel_work_sync(&adapter->reset_task);
	cancel_work_sync(&adapter->watchdog_task);

#ifdef CONFIG_IGB_DCA
	if (adapter->flags & IGB_FLAG_DCA_ENABLED) {
		dev_info(&pdev->dev, "DCA disabled\n");
		dca_remove_requester(&pdev->dev);
		adapter->flags &= ~IGB_FLAG_DCA_ENABLED;
		wr32(E1000_DCA_CTRL, E1000_DCA_CTRL_DCA_MODE_DISABLE);
	}
#endif

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	igb_release_hw_control(adapter);

	unregister_netdev(netdev);

	igb_clear_interrupt_scheme(adapter);

#ifdef CONFIG_PCI_IOV
	/* reclaim resources allocated to VFs */
	if (adapter->vf_data) {
		/* disable iov and allow time for transactions to clear */
		if (!igb_check_vf_assignment(adapter)) {
			pci_disable_sriov(pdev);
			msleep(500);
		} else {
			dev_info(&pdev->dev, "VF(s) assigned to guests!\n");
		}

		kfree(adapter->vf_data);
		adapter->vf_data = NULL;
		wr32(E1000_IOVCTL, E1000_IOVCTL_REUSE_VFQ);
		wrfl();
		msleep(100);
		dev_info(&pdev->dev, "IOV Disabled\n");
	}
#endif

	iounmap(hw->hw_addr);
	if (hw->flash_address)
		iounmap(hw->flash_address);
	pci_release_selected_regions(pdev,
	                             pci_select_bars(pdev, IORESOURCE_MEM));

	kfree(adapter->shadow_vfta);
	free_netdev(netdev);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

/**
 * igb_probe_vfs - Initialize vf data storage and add VFs to pci config space
 * @adapter: board private structure to initialize
 *
 * This function initializes the vf specific data storage and then attempts to
 * allocate the VFs.  The reason for ordering it this way is because it is much
 * mor expensive time wise to disable SR-IOV than it is to allocate and free
 * the memory for the VFs.
 **/
static void __devinit igb_probe_vfs(struct igb_adapter * adapter)
{
#ifdef CONFIG_PCI_IOV
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	int old_vfs = igb_find_enabled_vfs(adapter);
	int i;

	/* Virtualization features not supported on i210 family. */
	if ((hw->mac.type == e1000_i210) || (hw->mac.type == e1000_i211))
		return;

	if (old_vfs) {
		dev_info(&pdev->dev, "%d pre-allocated VFs found - override "
			 "max_vfs setting of %d\n", old_vfs, max_vfs);
		adapter->vfs_allocated_count = old_vfs;
	}

	if (!adapter->vfs_allocated_count)
		return;

	adapter->vf_data = kcalloc(adapter->vfs_allocated_count,
				sizeof(struct vf_data_storage), GFP_KERNEL);

	/* if allocation failed then we do not support SR-IOV */
	if (!adapter->vf_data) {
		adapter->vfs_allocated_count = 0;
		dev_err(&pdev->dev, "Unable to allocate memory for VF "
			"Data Storage\n");
		goto out;
	}

	if (!old_vfs) {
		if (pci_enable_sriov(pdev, adapter->vfs_allocated_count))
			goto err_out;
	}
	dev_info(&pdev->dev, "%d VFs allocated\n",
		 adapter->vfs_allocated_count);
	for (i = 0; i < adapter->vfs_allocated_count; i++)
		igb_vf_configure(adapter, i);

	/* DMA Coalescing is not supported in IOV mode. */
	adapter->flags &= ~IGB_FLAG_DMAC;
	goto out;
err_out:
	kfree(adapter->vf_data);
	adapter->vf_data = NULL;
	adapter->vfs_allocated_count = 0;
out:
	return;
#endif /* CONFIG_PCI_IOV */
}

/**
 * igb_sw_init - Initialize general software structures (struct igb_adapter)
 * @adapter: board private structure to initialize
 *
 * igb_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int __devinit igb_sw_init(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	u32 max_rss_queues;

	pci_read_config_word(pdev, PCI_COMMAND, &hw->bus.pci_cmd_word);

	/* set default ring sizes */
	adapter->tx_ring_count = IGB_DEFAULT_TXD;
	adapter->rx_ring_count = IGB_DEFAULT_RXD;

	/* set default ITR values */
	adapter->rx_itr_setting = IGB_DEFAULT_ITR;
	adapter->tx_itr_setting = IGB_DEFAULT_ITR;

	/* set default work limits */
	adapter->tx_work_limit = IGB_DEFAULT_TX_WORK;

	adapter->max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN +
				  VLAN_HLEN;
	adapter->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;

	adapter->node = -1;

	spin_lock_init(&adapter->stats64_lock);
#ifdef CONFIG_PCI_IOV
	switch (hw->mac.type) {
	case e1000_82576:
	case e1000_i350:
		if (max_vfs > 7) {
			dev_warn(&pdev->dev,
				 "Maximum of 7 VFs per PF, using max\n");
			adapter->vfs_allocated_count = 7;
		} else
			adapter->vfs_allocated_count = max_vfs;
		break;
	default:
		break;
	}
#endif /* CONFIG_PCI_IOV */

	/* Determine the maximum number of RSS queues supported. */
	switch (hw->mac.type) {
	case e1000_i211:
		max_rss_queues = IGB_MAX_RX_QUEUES_I211;
		break;
	case e1000_82575:
	case e1000_i210:
		max_rss_queues = IGB_MAX_RX_QUEUES_82575;
		break;
	case e1000_i350:
		/* I350 cannot do RSS and SR-IOV at the same time */
		if (!!adapter->vfs_allocated_count) {
			max_rss_queues = 1;
			break;
		}
		/* fall through */
	case e1000_82576:
		if (!!adapter->vfs_allocated_count) {
			max_rss_queues = 2;
			break;
		}
		/* fall through */
	case e1000_82580:
	default:
		max_rss_queues = IGB_MAX_RX_QUEUES;
		break;
	}

	adapter->rss_queues = min_t(u32, max_rss_queues, num_online_cpus());

	/* Determine if we need to pair queues. */
	switch (hw->mac.type) {
	case e1000_82575:
	case e1000_i211:
		/* Device supports enough interrupts without queue pairing. */
		break;
	case e1000_82576:
		/*
		 * If VFs are going to be allocated with RSS queues then we
		 * should pair the queues in order to conserve interrupts due
		 * to limited supply.
		 */
		if ((adapter->rss_queues > 1) &&
		    (adapter->vfs_allocated_count > 6))
			adapter->flags |= IGB_FLAG_QUEUE_PAIRS;
		/* fall through */
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	default:
		/*
		 * If rss_queues > half of max_rss_queues, pair the queues in
		 * order to conserve interrupts due to limited supply.
		 */
		if (adapter->rss_queues > (max_rss_queues / 2))
			adapter->flags |= IGB_FLAG_QUEUE_PAIRS;
		break;
	}

	/* Setup and initialize a copy of the hw vlan table array */
	adapter->shadow_vfta = kzalloc(sizeof(u32) *
				E1000_VLAN_FILTER_TBL_SIZE,
				GFP_ATOMIC);

	/* This call may decrease the number of queues */
	if (igb_init_interrupt_scheme(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	igb_probe_vfs(adapter);

	/* Explicitly disable IRQ since the NIC can be in any state. */
	igb_irq_disable(adapter);

	if (hw->mac.type >= e1000_i350)
		adapter->flags &= ~IGB_FLAG_DMAC;

	set_bit(__IGB_DOWN, &adapter->state);
	return 0;
}

/**
 * igb_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int __igb_open(struct net_device *netdev, bool resuming)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	int err;
	int i;

	/* disallow open during test */
	if (test_bit(__IGB_TESTING, &adapter->state)) {
		WARN_ON(resuming);
		return -EBUSY;
	}

	if (!resuming)
		pm_runtime_get_sync(&pdev->dev);

	netif_carrier_off(netdev);

	/* allocate transmit descriptors */
	err = igb_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = igb_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	igb_power_up_link(adapter);

	/* before we allocate an interrupt, we must be ready to handle it.
	 * Setting DEBUG_SHIRQ in the kernel makes it fire an interrupt
	 * as soon as we call pci_request_irq, so we have to setup our
	 * clean_rx handler before we do so.  */
	igb_configure(adapter);

	err = igb_request_irq(adapter);
	if (err)
		goto err_req_irq;

	/* From here on the code is the same as igb_up() */
	clear_bit(__IGB_DOWN, &adapter->state);

	for (i = 0; i < adapter->num_q_vectors; i++)
		napi_enable(&(adapter->q_vector[i]->napi));

	/* Clear any pending interrupts. */
	rd32(E1000_ICR);

	igb_irq_enable(adapter);

	/* notify VFs that reset has been completed */
	if (adapter->vfs_allocated_count) {
		u32 reg_data = rd32(E1000_CTRL_EXT);
		reg_data |= E1000_CTRL_EXT_PFRSTD;
		wr32(E1000_CTRL_EXT, reg_data);
	}

	netif_tx_start_all_queues(netdev);

	if (!resuming)
		pm_runtime_put(&pdev->dev);

	/* start the watchdog. */
	hw->mac.get_link_status = 1;
	schedule_work(&adapter->watchdog_task);

	return 0;

err_req_irq:
	igb_release_hw_control(adapter);
	igb_power_down_link(adapter);
	igb_free_all_rx_resources(adapter);
err_setup_rx:
	igb_free_all_tx_resources(adapter);
err_setup_tx:
	igb_reset(adapter);
	if (!resuming)
		pm_runtime_put(&pdev->dev);

	return err;
}

static int igb_open(struct net_device *netdev)
{
	return __igb_open(netdev, false);
}

/**
 * igb_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the driver's control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int __igb_close(struct net_device *netdev, bool suspending)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;

	WARN_ON(test_bit(__IGB_RESETTING, &adapter->state));

	if (!suspending)
		pm_runtime_get_sync(&pdev->dev);

	igb_down(adapter);
	igb_free_irq(adapter);

	igb_free_all_tx_resources(adapter);
	igb_free_all_rx_resources(adapter);

	if (!suspending)
		pm_runtime_put_sync(&pdev->dev);
	return 0;
}

static int igb_close(struct net_device *netdev)
{
	return __igb_close(netdev, false);
}

/**
 * igb_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
int igb_setup_tx_resources(struct igb_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int orig_node = dev_to_node(dev);
	int size;

	size = sizeof(struct igb_tx_buffer) * tx_ring->count;
	tx_ring->tx_buffer_info = vzalloc_node(size, tx_ring->numa_node);
	if (!tx_ring->tx_buffer_info)
		tx_ring->tx_buffer_info = vzalloc(size);
	if (!tx_ring->tx_buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(union e1000_adv_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	set_dev_node(dev, tx_ring->numa_node);
	tx_ring->desc = dma_alloc_coherent(dev,
					   tx_ring->size,
					   &tx_ring->dma,
					   GFP_KERNEL);
	set_dev_node(dev, orig_node);
	if (!tx_ring->desc)
		tx_ring->desc = dma_alloc_coherent(dev,
						   tx_ring->size,
						   &tx_ring->dma,
						   GFP_KERNEL);

	if (!tx_ring->desc)
		goto err;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return 0;

err:
	vfree(tx_ring->tx_buffer_info);
	dev_err(dev,
		"Unable to allocate memory for the transmit descriptor ring\n");
	return -ENOMEM;
}

/**
 * igb_setup_all_tx_resources - wrapper to allocate Tx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int igb_setup_all_tx_resources(struct igb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = igb_setup_tx_resources(adapter->tx_ring[i]);
		if (err) {
			dev_err(&pdev->dev,
				"Allocation for Tx Queue %u failed\n", i);
			for (i--; i >= 0; i--)
				igb_free_tx_resources(adapter->tx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * igb_setup_tctl - configure the transmit control registers
 * @adapter: Board private structure
 **/
void igb_setup_tctl(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 tctl;

	/* disable queue 0 which is enabled by default on 82575 and 82576 */
	wr32(E1000_TXDCTL(0), 0);

	/* Program the Transmit Control Register */
	tctl = rd32(E1000_TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	igb_config_collision_dist(hw);

	/* Enable transmits */
	tctl |= E1000_TCTL_EN;

	wr32(E1000_TCTL, tctl);
}

/**
 * igb_configure_tx_ring - Configure transmit ring after Reset
 * @adapter: board private structure
 * @ring: tx ring to configure
 *
 * Configure a transmit ring after a reset.
 **/
void igb_configure_tx_ring(struct igb_adapter *adapter,
                           struct igb_ring *ring)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 txdctl = 0;
	u64 tdba = ring->dma;
	int reg_idx = ring->reg_idx;

	/* disable the queue */
	wr32(E1000_TXDCTL(reg_idx), 0);
	wrfl();
	mdelay(10);

	wr32(E1000_TDLEN(reg_idx),
	                ring->count * sizeof(union e1000_adv_tx_desc));
	wr32(E1000_TDBAL(reg_idx),
	                tdba & 0x00000000ffffffffULL);
	wr32(E1000_TDBAH(reg_idx), tdba >> 32);

	ring->tail = hw->hw_addr + E1000_TDT(reg_idx);
	wr32(E1000_TDH(reg_idx), 0);
	writel(0, ring->tail);

	txdctl |= IGB_TX_PTHRESH;
	txdctl |= IGB_TX_HTHRESH << 8;
	txdctl |= IGB_TX_WTHRESH << 16;

	txdctl |= E1000_TXDCTL_QUEUE_ENABLE;
	wr32(E1000_TXDCTL(reg_idx), txdctl);
}

/**
 * igb_configure_tx - Configure transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void igb_configure_tx(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_configure_tx_ring(adapter, adapter->tx_ring[i]);
}

/**
 * igb_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int igb_setup_rx_resources(struct igb_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int orig_node = dev_to_node(dev);
	int size, desc_len;

	size = sizeof(struct igb_rx_buffer) * rx_ring->count;
	rx_ring->rx_buffer_info = vzalloc_node(size, rx_ring->numa_node);
	if (!rx_ring->rx_buffer_info)
		rx_ring->rx_buffer_info = vzalloc(size);
	if (!rx_ring->rx_buffer_info)
		goto err;

	desc_len = sizeof(union e1000_adv_rx_desc);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * desc_len;
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	set_dev_node(dev, rx_ring->numa_node);
	rx_ring->desc = dma_alloc_coherent(dev,
					   rx_ring->size,
					   &rx_ring->dma,
					   GFP_KERNEL);
	set_dev_node(dev, orig_node);
	if (!rx_ring->desc)
		rx_ring->desc = dma_alloc_coherent(dev,
						   rx_ring->size,
						   &rx_ring->dma,
						   GFP_KERNEL);

	if (!rx_ring->desc)
		goto err;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	return 0;

err:
	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the receive descriptor"
		" ring\n");
	return -ENOMEM;
}

/**
 * igb_setup_all_rx_resources - wrapper to allocate Rx resources
 *				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int igb_setup_all_rx_resources(struct igb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = igb_setup_rx_resources(adapter->rx_ring[i]);
		if (err) {
			dev_err(&pdev->dev,
				"Allocation for Rx Queue %u failed\n", i);
			for (i--; i >= 0; i--)
				igb_free_rx_resources(adapter->rx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * igb_setup_mrqc - configure the multiple receive queue control registers
 * @adapter: Board private structure
 **/
static void igb_setup_mrqc(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 mrqc, rxcsum;
	u32 j, num_rx_queues, shift = 0, shift2 = 0;
	union e1000_reta {
		u32 dword;
		u8  bytes[4];
	} reta;
	static const u8 rsshash[40] = {
		0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2, 0x41, 0x67,
		0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0, 0xd0, 0xca, 0x2b, 0xcb,
		0xae, 0x7b, 0x30, 0xb4,	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30,
		0xf2, 0x0c, 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa };

	/* Fill out hash function seeds */
	for (j = 0; j < 10; j++) {
		u32 rsskey = rsshash[(j * 4)];
		rsskey |= rsshash[(j * 4) + 1] << 8;
		rsskey |= rsshash[(j * 4) + 2] << 16;
		rsskey |= rsshash[(j * 4) + 3] << 24;
		array_wr32(E1000_RSSRK(0), j, rsskey);
	}

	num_rx_queues = adapter->rss_queues;

	if (adapter->vfs_allocated_count) {
		/* 82575 and 82576 supports 2 RSS queues for VMDq */
		switch (hw->mac.type) {
		case e1000_i350:
		case e1000_82580:
			num_rx_queues = 1;
			shift = 0;
			break;
		case e1000_82576:
			shift = 3;
			num_rx_queues = 2;
			break;
		case e1000_82575:
			shift = 2;
			shift2 = 6;
		default:
			break;
		}
	} else {
		if (hw->mac.type == e1000_82575)
			shift = 6;
	}

	for (j = 0; j < (32 * 4); j++) {
		reta.bytes[j & 3] = (j % num_rx_queues) << shift;
		if (shift2)
			reta.bytes[j & 3] |= num_rx_queues << shift2;
		if ((j & 3) == 3)
			wr32(E1000_RETA(j >> 2), reta.dword);
	}

	/*
	 * Disable raw packet checksumming so that RSS hash is placed in
	 * descriptor on writeback.  No need to enable TCP/UDP/IP checksum
	 * offloads as they are enabled by default
	 */
	rxcsum = rd32(E1000_RXCSUM);
	rxcsum |= E1000_RXCSUM_PCSD;

	if (adapter->hw.mac.type >= e1000_82576)
		/* Enable Receive Checksum Offload for SCTP */
		rxcsum |= E1000_RXCSUM_CRCOFL;

	/* Don't need to set TUOFL or IPOFL, they default to 1 */
	wr32(E1000_RXCSUM, rxcsum);
	/*
	 * Generate RSS hash based on TCP port numbers and/or
	 * IPv4/v6 src and dst addresses since UDP cannot be
	 * hashed reliably due to IP fragmentation
	 */

	mrqc = E1000_MRQC_RSS_FIELD_IPV4 |
	       E1000_MRQC_RSS_FIELD_IPV4_TCP |
	       E1000_MRQC_RSS_FIELD_IPV6 |
	       E1000_MRQC_RSS_FIELD_IPV6_TCP |
	       E1000_MRQC_RSS_FIELD_IPV6_TCP_EX;

	/* If VMDq is enabled then we set the appropriate mode for that, else
	 * we default to RSS so that an RSS hash is calculated per packet even
	 * if we are only using one queue */
	if (adapter->vfs_allocated_count) {
		if (hw->mac.type > e1000_82575) {
			/* Set the default pool for the PF's first queue */
			u32 vtctl = rd32(E1000_VT_CTL);
			vtctl &= ~(E1000_VT_CTL_DEFAULT_POOL_MASK |
				   E1000_VT_CTL_DISABLE_DEF_POOL);
			vtctl |= adapter->vfs_allocated_count <<
				E1000_VT_CTL_DEFAULT_POOL_SHIFT;
			wr32(E1000_VT_CTL, vtctl);
		}
		if (adapter->rss_queues > 1)
			mrqc |= E1000_MRQC_ENABLE_VMDQ_RSS_2Q;
		else
			mrqc |= E1000_MRQC_ENABLE_VMDQ;
	} else {
		if (hw->mac.type != e1000_i211)
			mrqc |= E1000_MRQC_ENABLE_RSS_4Q;
	}
	igb_vmm_control(adapter);

	wr32(E1000_MRQC, mrqc);
}

/**
 * igb_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 **/
void igb_setup_rctl(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	rctl = rd32(E1000_RCTL);

	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl &= ~(E1000_RCTL_LBM_TCVR | E1000_RCTL_LBM_MAC);

	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_RDMTS_HALF |
		(hw->mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/*
	 * enable stripping of CRC. It's unlikely this will break BMC
	 * redirection as it did with e1000. Newer features require
	 * that the HW strips the CRC.
	 */
	rctl |= E1000_RCTL_SECRC;

	/* disable store bad packets and clear size bits. */
	rctl &= ~(E1000_RCTL_SBP | E1000_RCTL_SZ_256);

	/* enable LPE to prevent packets larger than max_frame_size */
	rctl |= E1000_RCTL_LPE;

	/* disable queue 0 to prevent tail write w/o re-config */
	wr32(E1000_RXDCTL(0), 0);

	/* Attention!!!  For SR-IOV PF driver operations you must enable
	 * queue drop for all VF and PF queues to prevent head of line blocking
	 * if an un-trusted VF does not provide descriptors to hardware.
	 */
	if (adapter->vfs_allocated_count) {
		/* set all queue drop enable bits */
		wr32(E1000_QDE, ALL_QUEUES);
	}

	/* This is useful for sniffing bad packets. */
	if (adapter->netdev->features & NETIF_F_RXALL) {
		/* UPE and MPE will be handled by normal PROMISC logic
		 * in e1000e_set_rx_mode */
		rctl |= (E1000_RCTL_SBP | /* Receive bad packets */
			 E1000_RCTL_BAM | /* RX All Bcast Pkts */
			 E1000_RCTL_PMCF); /* RX All MAC Ctrl Pkts */

		rctl &= ~(E1000_RCTL_VFE | /* Disable VLAN filter */
			  E1000_RCTL_DPF | /* Allow filtered pause */
			  E1000_RCTL_CFIEN); /* Dis VLAN CFIEN Filter */
		/* Do not mess with E1000_CTRL_VME, it affects transmit as well,
		 * and that breaks VLANs.
		 */
	}

	wr32(E1000_RCTL, rctl);
}

static inline int igb_set_vf_rlpml(struct igb_adapter *adapter, int size,
                                   int vfn)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 vmolr;

	/* if it isn't the PF check to see if VFs are enabled and
	 * increase the size to support vlan tags */
	if (vfn < adapter->vfs_allocated_count &&
	    adapter->vf_data[vfn].vlans_enabled)
		size += VLAN_TAG_SIZE;

	vmolr = rd32(E1000_VMOLR(vfn));
	vmolr &= ~E1000_VMOLR_RLPML_MASK;
	vmolr |= size | E1000_VMOLR_LPE;
	wr32(E1000_VMOLR(vfn), vmolr);

	return 0;
}

/**
 * igb_rlpml_set - set maximum receive packet size
 * @adapter: board private structure
 *
 * Configure maximum receivable packet size.
 **/
static void igb_rlpml_set(struct igb_adapter *adapter)
{
	u32 max_frame_size = adapter->max_frame_size;
	struct e1000_hw *hw = &adapter->hw;
	u16 pf_id = adapter->vfs_allocated_count;

	if (pf_id) {
		igb_set_vf_rlpml(adapter, max_frame_size, pf_id);
		/*
		 * If we're in VMDQ or SR-IOV mode, then set global RLPML
		 * to our max jumbo frame size, in case we need to enable
		 * jumbo frames on one of the rings later.
		 * This will not pass over-length frames into the default
		 * queue because it's gated by the VMOLR.RLPML.
		 */
		max_frame_size = MAX_JUMBO_FRAME_SIZE;
	}

	wr32(E1000_RLPML, max_frame_size);
}

static inline void igb_set_vmolr(struct igb_adapter *adapter,
				 int vfn, bool aupe)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 vmolr;

	/*
	 * This register exists only on 82576 and newer so if we are older then
	 * we should exit and do nothing
	 */
	if (hw->mac.type < e1000_82576)
		return;

	vmolr = rd32(E1000_VMOLR(vfn));
	vmolr |= E1000_VMOLR_STRVLAN;      /* Strip vlan tags */
	if (aupe)
		vmolr |= E1000_VMOLR_AUPE;        /* Accept untagged packets */
	else
		vmolr &= ~(E1000_VMOLR_AUPE); /* Tagged packets ONLY */

	/* clear all bits that might not be set */
	vmolr &= ~(E1000_VMOLR_BAM | E1000_VMOLR_RSSE);

	if (adapter->rss_queues > 1 && vfn == adapter->vfs_allocated_count)
		vmolr |= E1000_VMOLR_RSSE; /* enable RSS */
	/*
	 * for VMDq only allow the VFs and pool 0 to accept broadcast and
	 * multicast packets
	 */
	if (vfn <= adapter->vfs_allocated_count)
		vmolr |= E1000_VMOLR_BAM;	   /* Accept broadcast */

	wr32(E1000_VMOLR(vfn), vmolr);
}

/**
 * igb_configure_rx_ring - Configure a receive ring after Reset
 * @adapter: board private structure
 * @ring: receive ring to be configured
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
void igb_configure_rx_ring(struct igb_adapter *adapter,
                           struct igb_ring *ring)
{
	struct e1000_hw *hw = &adapter->hw;
	u64 rdba = ring->dma;
	int reg_idx = ring->reg_idx;
	u32 srrctl = 0, rxdctl = 0;

	/* disable the queue */
	wr32(E1000_RXDCTL(reg_idx), 0);

	/* Set DMA base address registers */
	wr32(E1000_RDBAL(reg_idx),
	     rdba & 0x00000000ffffffffULL);
	wr32(E1000_RDBAH(reg_idx), rdba >> 32);
	wr32(E1000_RDLEN(reg_idx),
	               ring->count * sizeof(union e1000_adv_rx_desc));

	/* initialize head and tail */
	ring->tail = hw->hw_addr + E1000_RDT(reg_idx);
	wr32(E1000_RDH(reg_idx), 0);
	writel(0, ring->tail);

	/* set descriptor configuration */
	srrctl = IGB_RX_HDR_LEN << E1000_SRRCTL_BSIZEHDRSIZE_SHIFT;
#if (PAGE_SIZE / 2) > IGB_RXBUFFER_16384
	srrctl |= IGB_RXBUFFER_16384 >> E1000_SRRCTL_BSIZEPKT_SHIFT;
#else
	srrctl |= (PAGE_SIZE / 2) >> E1000_SRRCTL_BSIZEPKT_SHIFT;
#endif
	srrctl |= E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
#ifdef CONFIG_IGB_PTP
	if (hw->mac.type >= e1000_82580)
		srrctl |= E1000_SRRCTL_TIMESTAMP;
#endif /* CONFIG_IGB_PTP */
	/* Only set Drop Enable if we are supporting multiple queues */
	if (adapter->vfs_allocated_count || adapter->num_rx_queues > 1)
		srrctl |= E1000_SRRCTL_DROP_EN;

	wr32(E1000_SRRCTL(reg_idx), srrctl);

	/* set filtering for VMDQ pools */
	igb_set_vmolr(adapter, reg_idx & 0x7, true);

	rxdctl |= IGB_RX_PTHRESH;
	rxdctl |= IGB_RX_HTHRESH << 8;
	rxdctl |= IGB_RX_WTHRESH << 16;

	/* enable receive descriptor fetching */
	rxdctl |= E1000_RXDCTL_QUEUE_ENABLE;
	wr32(E1000_RXDCTL(reg_idx), rxdctl);
}

/**
 * igb_configure_rx - Configure receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void igb_configure_rx(struct igb_adapter *adapter)
{
	int i;

	/* set UTA to appropriate mode */
	igb_set_uta(adapter);

	/* set the correct pool for the PF default MAC address in entry 0 */
	igb_rar_set_qsel(adapter, adapter->hw.mac.addr, 0,
	                 adapter->vfs_allocated_count);

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_configure_rx_ring(adapter, adapter->rx_ring[i]);
}

/**
 * igb_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
void igb_free_tx_resources(struct igb_ring *tx_ring)
{
	igb_clean_tx_ring(tx_ring);

	vfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * igb_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
static void igb_free_all_tx_resources(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_free_tx_resources(adapter->tx_ring[i]);
}

void igb_unmap_and_free_tx_resource(struct igb_ring *ring,
				    struct igb_tx_buffer *tx_buffer)
{
	if (tx_buffer->skb) {
		dev_kfree_skb_any(tx_buffer->skb);
		if (tx_buffer->dma)
			dma_unmap_single(ring->dev,
					 tx_buffer->dma,
					 tx_buffer->length,
					 DMA_TO_DEVICE);
	} else if (tx_buffer->dma) {
		dma_unmap_page(ring->dev,
			       tx_buffer->dma,
			       tx_buffer->length,
			       DMA_TO_DEVICE);
	}
	tx_buffer->next_to_watch = NULL;
	tx_buffer->skb = NULL;
	tx_buffer->dma = 0;
	/* buffer_info must be completely set up in the transmit path */
}

/**
 * igb_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 **/
static void igb_clean_tx_ring(struct igb_ring *tx_ring)
{
	struct igb_tx_buffer *buffer_info;
	unsigned long size;
	u16 i;

	if (!tx_ring->tx_buffer_info)
		return;
	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->tx_buffer_info[i];
		igb_unmap_and_free_tx_resource(tx_ring, buffer_info);
	}

	netdev_tx_reset_queue(txring_txq(tx_ring));

	size = sizeof(struct igb_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * igb_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/
static void igb_clean_all_tx_rings(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igb_clean_tx_ring(adapter->tx_ring[i]);
}

/**
 * igb_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
void igb_free_rx_resources(struct igb_ring *rx_ring)
{
	igb_clean_rx_ring(rx_ring);

	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * igb_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/
static void igb_free_all_rx_resources(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_free_rx_resources(adapter->rx_ring[i]);
}

/**
 * igb_clean_rx_ring - Free Rx Buffers per Queue
 * @rx_ring: ring to free buffers from
 **/
static void igb_clean_rx_ring(struct igb_ring *rx_ring)
{
	unsigned long size;
	u16 i;

	if (!rx_ring->rx_buffer_info)
		return;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct igb_rx_buffer *buffer_info = &rx_ring->rx_buffer_info[i];
		if (buffer_info->dma) {
			dma_unmap_single(rx_ring->dev,
			                 buffer_info->dma,
					 IGB_RX_HDR_LEN,
					 DMA_FROM_DEVICE);
			buffer_info->dma = 0;
		}

		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
		if (buffer_info->page_dma) {
			dma_unmap_page(rx_ring->dev,
			               buffer_info->page_dma,
				       PAGE_SIZE / 2,
				       DMA_FROM_DEVICE);
			buffer_info->page_dma = 0;
		}
		if (buffer_info->page) {
			put_page(buffer_info->page);
			buffer_info->page = NULL;
			buffer_info->page_offset = 0;
		}
	}

	size = sizeof(struct igb_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * igb_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/
static void igb_clean_all_rx_rings(struct igb_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		igb_clean_rx_ring(adapter->rx_ring[i]);
}

/**
 * igb_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int igb_set_mac(struct net_device *netdev, void *p)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(hw->mac.addr, addr->sa_data, netdev->addr_len);

	/* set the correct pool for the new PF MAC address in entry 0 */
	igb_rar_set_qsel(adapter, hw->mac.addr, 0,
	                 adapter->vfs_allocated_count);

	return 0;
}

/**
 * igb_write_mc_addr_list - write multicast addresses to MTA
 * @netdev: network interface device structure
 *
 * Writes multicast address list to the MTA hash table.
 * Returns: -ENOMEM on failure
 *                0 on no addresses written
 *                X on writing X addresses to MTA
 **/
static int igb_write_mc_addr_list(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u8  *mta_list;
	int i;

	if (netdev_mc_empty(netdev)) {
		/* nothing to program, so clear mc list */
		igb_update_mc_addr_list(hw, NULL, 0);
		igb_restore_vf_multicasts(adapter);
		return 0;
	}

	mta_list = kzalloc(netdev_mc_count(netdev) * 6, GFP_ATOMIC);
	if (!mta_list)
		return -ENOMEM;

	/* The shared function expects a packed array of only addresses. */
	i = 0;
	netdev_for_each_mc_addr(ha, netdev)
		memcpy(mta_list + (i++ * ETH_ALEN), ha->addr, ETH_ALEN);

	igb_update_mc_addr_list(hw, mta_list, i);
	kfree(mta_list);

	return netdev_mc_count(netdev);
}

/**
 * igb_write_uc_addr_list - write unicast addresses to RAR table
 * @netdev: network interface device structure
 *
 * Writes unicast address list to the RAR table.
 * Returns: -ENOMEM on failure/insufficient address space
 *                0 on no addresses written
 *                X on writing X addresses to the RAR table
 **/
static int igb_write_uc_addr_list(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	unsigned int vfn = adapter->vfs_allocated_count;
	unsigned int rar_entries = hw->mac.rar_entry_count - (vfn + 1);
	int count = 0;

	/* return ENOMEM indicating insufficient memory for addresses */
	if (netdev_uc_count(netdev) > rar_entries)
		return -ENOMEM;

	if (!netdev_uc_empty(netdev) && rar_entries) {
		struct netdev_hw_addr *ha;

		netdev_for_each_uc_addr(ha, netdev) {
			if (!rar_entries)
				break;
			igb_rar_set_qsel(adapter, ha->addr,
			                 rar_entries--,
			                 vfn);
			count++;
		}
	}
	/* write the addresses in reverse order to avoid write combining */
	for (; rar_entries > 0 ; rar_entries--) {
		wr32(E1000_RAH(rar_entries), 0);
		wr32(E1000_RAL(rar_entries), 0);
	}
	wrfl();

	return count;
}

/**
 * igb_set_rx_mode - Secondary Unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_rx_mode entry point is called whenever the unicast or multicast
 * address lists or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast,
 * promiscuous mode, and all-multi behavior.
 **/
static void igb_set_rx_mode(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	unsigned int vfn = adapter->vfs_allocated_count;
	u32 rctl, vmolr = 0;
	int count;

	/* Check for Promiscuous and All Multicast modes */
	rctl = rd32(E1000_RCTL);

	/* clear the effected bits */
	rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_VFE);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		vmolr |= (E1000_VMOLR_ROPE | E1000_VMOLR_MPME);
	} else {
		if (netdev->flags & IFF_ALLMULTI) {
			rctl |= E1000_RCTL_MPE;
			vmolr |= E1000_VMOLR_MPME;
		} else {
			/*
			 * Write addresses to the MTA, if the attempt fails
			 * then we should just turn on promiscuous mode so
			 * that we can at least receive multicast traffic
			 */
			count = igb_write_mc_addr_list(netdev);
			if (count < 0) {
				rctl |= E1000_RCTL_MPE;
				vmolr |= E1000_VMOLR_MPME;
			} else if (count) {
				vmolr |= E1000_VMOLR_ROMPE;
			}
		}
		/*
		 * Write addresses to available RAR registers, if there is not
		 * sufficient space to store all the addresses then enable
		 * unicast promiscuous mode
		 */
		count = igb_write_uc_addr_list(netdev);
		if (count < 0) {
			rctl |= E1000_RCTL_UPE;
			vmolr |= E1000_VMOLR_ROPE;
		}
		rctl |= E1000_RCTL_VFE;
	}
	wr32(E1000_RCTL, rctl);

	/*
	 * In order to support SR-IOV and eventually VMDq it is necessary to set
	 * the VMOLR to enable the appropriate modes.  Without this workaround
	 * we will have issues with VLAN tag stripping not being done for frames
	 * that are only arriving because we are the default pool
	 */
	if ((hw->mac.type < e1000_82576) || (hw->mac.type > e1000_i350))
		return;

	vmolr |= rd32(E1000_VMOLR(vfn)) &
	         ~(E1000_VMOLR_ROPE | E1000_VMOLR_MPME | E1000_VMOLR_ROMPE);
	wr32(E1000_VMOLR(vfn), vmolr);
	igb_restore_vf_multicasts(adapter);
}

static void igb_check_wvbr(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 wvbr = 0;

	switch (hw->mac.type) {
	case e1000_82576:
	case e1000_i350:
		if (!(wvbr = rd32(E1000_WVBR)))
			return;
		break;
	default:
		break;
	}

	adapter->wvbr |= wvbr;
}

#define IGB_STAGGERED_QUEUE_OFFSET 8

static void igb_spoof_check(struct igb_adapter *adapter)
{
	int j;

	if (!adapter->wvbr)
		return;

	for(j = 0; j < adapter->vfs_allocated_count; j++) {
		if (adapter->wvbr & (1 << j) ||
		    adapter->wvbr & (1 << (j + IGB_STAGGERED_QUEUE_OFFSET))) {
			dev_warn(&adapter->pdev->dev,
				"Spoof event(s) detected on VF %d\n", j);
			adapter->wvbr &=
				~((1 << j) |
				  (1 << (j + IGB_STAGGERED_QUEUE_OFFSET)));
		}
	}
}

/* Need to wait a few seconds after link up to get diagnostic information from
 * the phy */
static void igb_update_phy_info(unsigned long data)
{
	struct igb_adapter *adapter = (struct igb_adapter *) data;
	igb_get_phy_info(&adapter->hw);
}

/**
 * igb_has_link - check shared code for link and determine up/down
 * @adapter: pointer to driver private info
 **/
bool igb_has_link(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	bool link_active = false;
	s32 ret_val = 0;

	/* get_link_status is set on LSC (link status) interrupt or
	 * rx sequence error interrupt.  get_link_status will stay
	 * false until the e1000_check_for_link establishes link
	 * for copper adapters ONLY
	 */
	switch (hw->phy.media_type) {
	case e1000_media_type_copper:
		if (hw->mac.get_link_status) {
			ret_val = hw->mac.ops.check_for_link(hw);
			link_active = !hw->mac.get_link_status;
		} else {
			link_active = true;
		}
		break;
	case e1000_media_type_internal_serdes:
		ret_val = hw->mac.ops.check_for_link(hw);
		link_active = hw->mac.serdes_has_link;
		break;
	default:
	case e1000_media_type_unknown:
		break;
	}

	return link_active;
}

static bool igb_thermal_sensor_event(struct e1000_hw *hw, u32 event)
{
	bool ret = false;
	u32 ctrl_ext, thstat;

	/* check for thermal sensor event on i350 copper only */
	if (hw->mac.type == e1000_i350) {
		thstat = rd32(E1000_THSTAT);
		ctrl_ext = rd32(E1000_CTRL_EXT);

		if ((hw->phy.media_type == e1000_media_type_copper) &&
		    !(ctrl_ext & E1000_CTRL_EXT_LINK_MODE_SGMII)) {
			ret = !!(thstat & event);
		}
	}

	return ret;
}

/**
 * igb_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void igb_watchdog(unsigned long data)
{
	struct igb_adapter *adapter = (struct igb_adapter *)data;
	/* Do the rest outside of interrupt context */
	schedule_work(&adapter->watchdog_task);
}

static void igb_watchdog_task(struct work_struct *work)
{
	struct igb_adapter *adapter = container_of(work,
	                                           struct igb_adapter,
                                                   watchdog_task);
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 link;
	int i;

	link = igb_has_link(adapter);
	if (link) {
		/* Cancel scheduled suspend requests. */
		pm_runtime_resume(netdev->dev.parent);

		if (!netif_carrier_ok(netdev)) {
			u32 ctrl;
			hw->mac.ops.get_speed_and_duplex(hw,
			                                 &adapter->link_speed,
			                                 &adapter->link_duplex);

			ctrl = rd32(E1000_CTRL);
			/* Links status message must follow this format */
			printk(KERN_INFO "igb: %s NIC Link is Up %d Mbps %s "
			       "Duplex, Flow Control: %s\n",
			       netdev->name,
			       adapter->link_speed,
			       adapter->link_duplex == FULL_DUPLEX ?
			       "Full" : "Half",
			       (ctrl & E1000_CTRL_TFCE) &&
			       (ctrl & E1000_CTRL_RFCE) ? "RX/TX" :
			       (ctrl & E1000_CTRL_RFCE) ?  "RX" :
			       (ctrl & E1000_CTRL_TFCE) ?  "TX" : "None");

			/* check for thermal sensor event */
			if (igb_thermal_sensor_event(hw,
			    E1000_THSTAT_LINK_THROTTLE)) {
				netdev_info(netdev, "The network adapter link "
					    "speed was downshifted because it "
					    "overheated\n");
			}

			/* adjust timeout factor according to speed/duplex */
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				adapter->tx_timeout_factor = 14;
				break;
			case SPEED_100:
				/* maybe add some timeout factor ? */
				break;
			}

			netif_carrier_on(netdev);

			igb_ping_all_vfs(adapter);
			igb_check_vf_rate_limit(adapter);

			/* link state has changed, schedule phy info update */
			if (!test_bit(__IGB_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;

			/* check for thermal sensor event */
			if (igb_thermal_sensor_event(hw,
			    E1000_THSTAT_PWR_DOWN)) {
				netdev_err(netdev, "The network adapter was "
					   "stopped because it overheated\n");
			}

			/* Links status message must follow this format */
			printk(KERN_INFO "igb: %s NIC Link is Down\n",
			       netdev->name);
			netif_carrier_off(netdev);

			igb_ping_all_vfs(adapter);

			/* link state has changed, schedule phy info update */
			if (!test_bit(__IGB_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));

			pm_schedule_suspend(netdev->dev.parent,
					    MSEC_PER_SEC * 5);
		}
	}

	spin_lock(&adapter->stats64_lock);
	igb_update_stats(adapter, &adapter->stats64);
	spin_unlock(&adapter->stats64_lock);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *tx_ring = adapter->tx_ring[i];
		if (!netif_carrier_ok(netdev)) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context). */
			if (igb_desc_unused(tx_ring) + 1 < tx_ring->count) {
				adapter->tx_timeout_count++;
				schedule_work(&adapter->reset_task);
				/* return immediately since reset is imminent */
				return;
			}
		}

		/* Force detection of hung controller every watchdog period */
		set_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
	}

	/* Cause software interrupt to ensure rx ring is cleaned */
	if (adapter->msix_entries) {
		u32 eics = 0;
		for (i = 0; i < adapter->num_q_vectors; i++)
			eics |= adapter->q_vector[i]->eims_value;
		wr32(E1000_EICS, eics);
	} else {
		wr32(E1000_ICS, E1000_ICS_RXDMT0);
	}

	igb_spoof_check(adapter);

	/* Reset the timer */
	if (!test_bit(__IGB_DOWN, &adapter->state))
		mod_timer(&adapter->watchdog_timer,
			  round_jiffies(jiffies + 2 * HZ));
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

/**
 * igb_update_ring_itr - update the dynamic ITR value based on packet size
 *
 *      Stores a new ITR value based on strictly on packet size.  This
 *      algorithm is less sophisticated than that used in igb_update_itr,
 *      due to the difficulty of synchronizing statistics across multiple
 *      receive rings.  The divisors and thresholds used by this function
 *      were determined based on theoretical maximum wire speed and testing
 *      data, in order to minimize response time while increasing bulk
 *      throughput.
 *      This functionality is controlled by the InterruptThrottleRate module
 *      parameter (see igb_param.c)
 *      NOTE:  This function is called only when operating in a multiqueue
 *             receive environment.
 * @q_vector: pointer to q_vector
 **/
static void igb_update_ring_itr(struct igb_q_vector *q_vector)
{
	int new_val = q_vector->itr_val;
	int avg_wire_size = 0;
	struct igb_adapter *adapter = q_vector->adapter;
	unsigned int packets;

	/* For non-gigabit speeds, just fix the interrupt rate at 4000
	 * ints/sec - ITR timer value of 120 ticks.
	 */
	if (adapter->link_speed != SPEED_1000) {
		new_val = IGB_4K_ITR;
		goto set_itr_val;
	}

	packets = q_vector->rx.total_packets;
	if (packets)
		avg_wire_size = q_vector->rx.total_bytes / packets;

	packets = q_vector->tx.total_packets;
	if (packets)
		avg_wire_size = max_t(u32, avg_wire_size,
				      q_vector->tx.total_bytes / packets);

	/* if avg_wire_size isn't set no work was done */
	if (!avg_wire_size)
		goto clear_counts;

	/* Add 24 bytes to size to account for CRC, preamble, and gap */
	avg_wire_size += 24;

	/* Don't starve jumbo frames */
	avg_wire_size = min(avg_wire_size, 3000);

	/* Give a little boost to mid-size frames */
	if ((avg_wire_size > 300) && (avg_wire_size < 1200))
		new_val = avg_wire_size / 3;
	else
		new_val = avg_wire_size / 2;

	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (new_val < IGB_20K_ITR &&
	    ((q_vector->rx.ring && adapter->rx_itr_setting == 3) ||
	     (!q_vector->rx.ring && adapter->tx_itr_setting == 3)))
		new_val = IGB_20K_ITR;

set_itr_val:
	if (new_val != q_vector->itr_val) {
		q_vector->itr_val = new_val;
		q_vector->set_itr = 1;
	}
clear_counts:
	q_vector->rx.total_bytes = 0;
	q_vector->rx.total_packets = 0;
	q_vector->tx.total_bytes = 0;
	q_vector->tx.total_packets = 0;
}

/**
 * igb_update_itr - update the dynamic ITR value based on statistics
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.
 *      this functionality is controlled by the InterruptThrottleRate module
 *      parameter (see igb_param.c)
 *      NOTE:  These calculations are only valid when operating in a single-
 *             queue environment.
 * @q_vector: pointer to q_vector
 * @ring_container: ring info to update the itr for
 **/
static void igb_update_itr(struct igb_q_vector *q_vector,
			   struct igb_ring_container *ring_container)
{
	unsigned int packets = ring_container->total_packets;
	unsigned int bytes = ring_container->total_bytes;
	u8 itrval = ring_container->itr;

	/* no packets, exit with status unchanged */
	if (packets == 0)
		return;

	switch (itrval) {
	case lowest_latency:
		/* handle TSO and jumbo frames */
		if (bytes/packets > 8000)
			itrval = bulk_latency;
		else if ((packets < 5) && (bytes > 512))
			itrval = low_latency;
		break;
	case low_latency:  /* 50 usec aka 20000 ints/s */
		if (bytes > 10000) {
			/* this if handles the TSO accounting */
			if (bytes/packets > 8000) {
				itrval = bulk_latency;
			} else if ((packets < 10) || ((bytes/packets) > 1200)) {
				itrval = bulk_latency;
			} else if ((packets > 35)) {
				itrval = lowest_latency;
			}
		} else if (bytes/packets > 2000) {
			itrval = bulk_latency;
		} else if (packets <= 2 && bytes < 512) {
			itrval = lowest_latency;
		}
		break;
	case bulk_latency: /* 250 usec aka 4000 ints/s */
		if (bytes > 25000) {
			if (packets > 35)
				itrval = low_latency;
		} else if (bytes < 1500) {
			itrval = low_latency;
		}
		break;
	}

	/* clear work counters since we have the values we need */
	ring_container->total_bytes = 0;
	ring_container->total_packets = 0;

	/* write updated itr to ring container */
	ring_container->itr = itrval;
}

static void igb_set_itr(struct igb_q_vector *q_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	u32 new_itr = q_vector->itr_val;
	u8 current_itr = 0;

	/* for non-gigabit speeds, just fix the interrupt rate at 4000 */
	if (adapter->link_speed != SPEED_1000) {
		current_itr = 0;
		new_itr = IGB_4K_ITR;
		goto set_itr_now;
	}

	igb_update_itr(q_vector, &q_vector->tx);
	igb_update_itr(q_vector, &q_vector->rx);

	current_itr = max(q_vector->rx.itr, q_vector->tx.itr);

	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (current_itr == lowest_latency &&
	    ((q_vector->rx.ring && adapter->rx_itr_setting == 3) ||
	     (!q_vector->rx.ring && adapter->tx_itr_setting == 3)))
		current_itr = low_latency;

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = IGB_70K_ITR; /* 70,000 ints/sec */
		break;
	case low_latency:
		new_itr = IGB_20K_ITR; /* 20,000 ints/sec */
		break;
	case bulk_latency:
		new_itr = IGB_4K_ITR;  /* 4,000 ints/sec */
		break;
	default:
		break;
	}

set_itr_now:
	if (new_itr != q_vector->itr_val) {
		/* this attempts to bias the interrupt rate towards Bulk
		 * by adding intermediate steps when interrupt rate is
		 * increasing */
		new_itr = new_itr > q_vector->itr_val ?
		             max((new_itr * q_vector->itr_val) /
		                 (new_itr + (q_vector->itr_val >> 2)),
				 new_itr) :
			     new_itr;
		/* Don't write the value here; it resets the adapter's
		 * internal timer, and causes us to delay far longer than
		 * we should between interrupts.  Instead, we write the ITR
		 * value at the beginning of the next interrupt so the timing
		 * ends up being correct.
		 */
		q_vector->itr_val = new_itr;
		q_vector->set_itr = 1;
	}
}

static void igb_tx_ctxtdesc(struct igb_ring *tx_ring, u32 vlan_macip_lens,
			    u32 type_tucmd, u32 mss_l4len_idx)
{
	struct e1000_adv_tx_context_desc *context_desc;
	u16 i = tx_ring->next_to_use;

	context_desc = IGB_TX_CTXTDESC(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	/* set bits to identify this as an advanced context descriptor */
	type_tucmd |= E1000_TXD_CMD_DEXT | E1000_ADVTXD_DTYP_CTXT;

	/* For 82575, context index must be unique per ring. */
	if (test_bit(IGB_RING_FLAG_TX_CTX_IDX, &tx_ring->flags))
		mss_l4len_idx |= tx_ring->reg_idx << 4;

	context_desc->vlan_macip_lens	= cpu_to_le32(vlan_macip_lens);
	context_desc->seqnum_seed	= 0;
	context_desc->type_tucmd_mlhl	= cpu_to_le32(type_tucmd);
	context_desc->mss_l4len_idx	= cpu_to_le32(mss_l4len_idx);
}

static int igb_tso(struct igb_ring *tx_ring,
		   struct igb_tx_buffer *first,
		   u8 *hdr_len)
{
	struct sk_buff *skb = first->skb;
	u32 vlan_macip_lens, type_tucmd;
	u32 mss_l4len_idx, l4len;

	if (!skb_is_gso(skb))
		return 0;

	if (skb_header_cloned(skb)) {
		int err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
		if (err)
			return err;
	}

	/* ADV DTYP TUCMD MKRLOC/ISCSIHEDLEN */
	type_tucmd = E1000_ADVTXD_TUCMD_L4T_TCP;

	if (first->protocol == __constant_htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
							 iph->daddr, 0,
							 IPPROTO_TCP,
							 0);
		type_tucmd |= E1000_ADVTXD_TUCMD_IPV4;
		first->tx_flags |= IGB_TX_FLAGS_TSO |
				   IGB_TX_FLAGS_CSUM |
				   IGB_TX_FLAGS_IPV4;
	} else if (skb_is_gso_v6(skb)) {
		ipv6_hdr(skb)->payload_len = 0;
		tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						       &ipv6_hdr(skb)->daddr,
						       0, IPPROTO_TCP, 0);
		first->tx_flags |= IGB_TX_FLAGS_TSO |
				   IGB_TX_FLAGS_CSUM;
	}

	/* compute header lengths */
	l4len = tcp_hdrlen(skb);
	*hdr_len = skb_transport_offset(skb) + l4len;

	/* update gso size and bytecount with header size */
	first->gso_segs = skb_shinfo(skb)->gso_segs;
	first->bytecount += (first->gso_segs - 1) * *hdr_len;

	/* MSS L4LEN IDX */
	mss_l4len_idx = l4len << E1000_ADVTXD_L4LEN_SHIFT;
	mss_l4len_idx |= skb_shinfo(skb)->gso_size << E1000_ADVTXD_MSS_SHIFT;

	/* VLAN MACLEN IPLEN */
	vlan_macip_lens = skb_network_header_len(skb);
	vlan_macip_lens |= skb_network_offset(skb) << E1000_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= first->tx_flags & IGB_TX_FLAGS_VLAN_MASK;

	igb_tx_ctxtdesc(tx_ring, vlan_macip_lens, type_tucmd, mss_l4len_idx);

	return 1;
}

static void igb_tx_csum(struct igb_ring *tx_ring, struct igb_tx_buffer *first)
{
	struct sk_buff *skb = first->skb;
	u32 vlan_macip_lens = 0;
	u32 mss_l4len_idx = 0;
	u32 type_tucmd = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		if (!(first->tx_flags & IGB_TX_FLAGS_VLAN))
			return;
	} else {
		u8 l4_hdr = 0;
		switch (first->protocol) {
		case __constant_htons(ETH_P_IP):
			vlan_macip_lens |= skb_network_header_len(skb);
			type_tucmd |= E1000_ADVTXD_TUCMD_IPV4;
			l4_hdr = ip_hdr(skb)->protocol;
			break;
		case __constant_htons(ETH_P_IPV6):
			vlan_macip_lens |= skb_network_header_len(skb);
			l4_hdr = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			if (unlikely(net_ratelimit())) {
				dev_warn(tx_ring->dev,
				 "partial checksum but proto=%x!\n",
				 first->protocol);
			}
			break;
		}

		switch (l4_hdr) {
		case IPPROTO_TCP:
			type_tucmd |= E1000_ADVTXD_TUCMD_L4T_TCP;
			mss_l4len_idx = tcp_hdrlen(skb) <<
					E1000_ADVTXD_L4LEN_SHIFT;
			break;
		case IPPROTO_SCTP:
			type_tucmd |= E1000_ADVTXD_TUCMD_L4T_SCTP;
			mss_l4len_idx = sizeof(struct sctphdr) <<
					E1000_ADVTXD_L4LEN_SHIFT;
			break;
		case IPPROTO_UDP:
			mss_l4len_idx = sizeof(struct udphdr) <<
					E1000_ADVTXD_L4LEN_SHIFT;
			break;
		default:
			if (unlikely(net_ratelimit())) {
				dev_warn(tx_ring->dev,
				 "partial checksum but l4 proto=%x!\n",
				 l4_hdr);
			}
			break;
		}

		/* update TX checksum flag */
		first->tx_flags |= IGB_TX_FLAGS_CSUM;
	}

	vlan_macip_lens |= skb_network_offset(skb) << E1000_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= first->tx_flags & IGB_TX_FLAGS_VLAN_MASK;

	igb_tx_ctxtdesc(tx_ring, vlan_macip_lens, type_tucmd, mss_l4len_idx);
}

static __le32 igb_tx_cmd_type(u32 tx_flags)
{
	/* set type for advanced descriptor with frame checksum insertion */
	__le32 cmd_type = cpu_to_le32(E1000_ADVTXD_DTYP_DATA |
				      E1000_ADVTXD_DCMD_IFCS |
				      E1000_ADVTXD_DCMD_DEXT);

	/* set HW vlan bit if vlan is present */
	if (tx_flags & IGB_TX_FLAGS_VLAN)
		cmd_type |= cpu_to_le32(E1000_ADVTXD_DCMD_VLE);

#ifdef CONFIG_IGB_PTP
	/* set timestamp bit if present */
	if (unlikely(tx_flags & IGB_TX_FLAGS_TSTAMP))
		cmd_type |= cpu_to_le32(E1000_ADVTXD_MAC_TSTAMP);
#endif /* CONFIG_IGB_PTP */

	/* set segmentation bits for TSO */
	if (tx_flags & IGB_TX_FLAGS_TSO)
		cmd_type |= cpu_to_le32(E1000_ADVTXD_DCMD_TSE);

	return cmd_type;
}

static void igb_tx_olinfo_status(struct igb_ring *tx_ring,
				 union e1000_adv_tx_desc *tx_desc,
				 u32 tx_flags, unsigned int paylen)
{
	u32 olinfo_status = paylen << E1000_ADVTXD_PAYLEN_SHIFT;

	/* 82575 requires a unique index per ring if any offload is enabled */
	if ((tx_flags & (IGB_TX_FLAGS_CSUM | IGB_TX_FLAGS_VLAN)) &&
	    test_bit(IGB_RING_FLAG_TX_CTX_IDX, &tx_ring->flags))
		olinfo_status |= tx_ring->reg_idx << 4;

	/* insert L4 checksum */
	if (tx_flags & IGB_TX_FLAGS_CSUM) {
		olinfo_status |= E1000_TXD_POPTS_TXSM << 8;

		/* insert IPv4 checksum */
		if (tx_flags & IGB_TX_FLAGS_IPV4)
			olinfo_status |= E1000_TXD_POPTS_IXSM << 8;
	}

	tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);
}

/*
 * The largest size we can write to the descriptor is 65535.  In order to
 * maintain a power of two alignment we have to limit ourselves to 32K.
 */
#define IGB_MAX_TXD_PWR	15
#define IGB_MAX_DATA_PER_TXD	(1<<IGB_MAX_TXD_PWR)

static void igb_tx_map(struct igb_ring *tx_ring,
		       struct igb_tx_buffer *first,
		       const u8 hdr_len)
{
	struct sk_buff *skb = first->skb;
	struct igb_tx_buffer *tx_buffer_info;
	union e1000_adv_tx_desc *tx_desc;
	dma_addr_t dma;
	struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[0];
	unsigned int data_len = skb->data_len;
	unsigned int size = skb_headlen(skb);
	unsigned int paylen = skb->len - hdr_len;
	__le32 cmd_type;
	u32 tx_flags = first->tx_flags;
	u16 i = tx_ring->next_to_use;

	tx_desc = IGB_TX_DESC(tx_ring, i);

	igb_tx_olinfo_status(tx_ring, tx_desc, tx_flags, paylen);
	cmd_type = igb_tx_cmd_type(tx_flags);

	dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(tx_ring->dev, dma))
		goto dma_error;

	/* record length, and DMA address */
	first->length = size;
	first->dma = dma;
	tx_desc->read.buffer_addr = cpu_to_le64(dma);

	for (;;) {
		while (unlikely(size > IGB_MAX_DATA_PER_TXD)) {
			tx_desc->read.cmd_type_len =
				cmd_type | cpu_to_le32(IGB_MAX_DATA_PER_TXD);

			i++;
			tx_desc++;
			if (i == tx_ring->count) {
				tx_desc = IGB_TX_DESC(tx_ring, 0);
				i = 0;
			}

			dma += IGB_MAX_DATA_PER_TXD;
			size -= IGB_MAX_DATA_PER_TXD;

			tx_desc->read.olinfo_status = 0;
			tx_desc->read.buffer_addr = cpu_to_le64(dma);
		}

		if (likely(!data_len))
			break;

		tx_desc->read.cmd_type_len = cmd_type | cpu_to_le32(size);

		i++;
		tx_desc++;
		if (i == tx_ring->count) {
			tx_desc = IGB_TX_DESC(tx_ring, 0);
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0,
				   size, DMA_TO_DEVICE);
		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_error;

		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		tx_buffer_info->length = size;
		tx_buffer_info->dma = dma;

		tx_desc->read.olinfo_status = 0;
		tx_desc->read.buffer_addr = cpu_to_le64(dma);

		frag++;
	}

	netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

	/* write last descriptor with RS and EOP bits */
	cmd_type |= cpu_to_le32(size) | cpu_to_le32(IGB_TXD_DCMD);
	if (unlikely(skb->no_fcs))
		cmd_type &= ~(cpu_to_le32(E1000_ADVTXD_DCMD_IFCS));
	tx_desc->read.cmd_type_len = cmd_type;

	/* set the timestamp */
	first->time_stamp = jiffies;

	/*
	 * Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.  (Only applicable for weak-ordered
	 * memory model archs, such as IA-64).
	 *
	 * We also need this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	wmb();

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	writel(i, tx_ring->tail);

	/* we need this if more than one processor can write to our tail
	 * at a time, it syncronizes IO on IA64/Altix systems */
	mmiowb();

	return;

dma_error:
	dev_err(tx_ring->dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_buffer_info map */
	for (;;) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		igb_unmap_and_free_tx_resource(tx_ring, tx_buffer_info);
		if (tx_buffer_info == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	tx_ring->next_to_use = i;
}

static int __igb_maybe_stop_tx(struct igb_ring *tx_ring, const u16 size)
{
	struct net_device *netdev = tx_ring->netdev;

	netif_stop_subqueue(netdev, tx_ring->queue_index);

	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (igb_desc_unused(tx_ring) < size)
		return -EBUSY;

	/* A reprieve! */
	netif_wake_subqueue(netdev, tx_ring->queue_index);

	u64_stats_update_begin(&tx_ring->tx_syncp2);
	tx_ring->tx_stats.restart_queue2++;
	u64_stats_update_end(&tx_ring->tx_syncp2);

	return 0;
}

static inline int igb_maybe_stop_tx(struct igb_ring *tx_ring, const u16 size)
{
	if (igb_desc_unused(tx_ring) >= size)
		return 0;
	return __igb_maybe_stop_tx(tx_ring, size);
}

netdev_tx_t igb_xmit_frame_ring(struct sk_buff *skb,
				struct igb_ring *tx_ring)
{
#ifdef CONFIG_IGB_PTP
	struct igb_adapter *adapter = netdev_priv(tx_ring->netdev);
#endif /* CONFIG_IGB_PTP */
	struct igb_tx_buffer *first;
	int tso;
	u32 tx_flags = 0;
	__be16 protocol = vlan_get_protocol(skb);
	u8 hdr_len = 0;

	/* need: 1 descriptor per page,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for skb->data,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time */
	if (igb_maybe_stop_tx(tx_ring, skb_shinfo(skb)->nr_frags + 4)) {
		/* this is a hard error */
		return NETDEV_TX_BUSY;
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = skb->len;
	first->gso_segs = 1;

#ifdef CONFIG_IGB_PTP
	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     !(adapter->ptp_tx_skb))) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		tx_flags |= IGB_TX_FLAGS_TSTAMP;

		adapter->ptp_tx_skb = skb_get(skb);
		if (adapter->hw.mac.type == e1000_82576)
			schedule_work(&adapter->ptp_tx_work);
	}
#endif /* CONFIG_IGB_PTP */

	if (vlan_tx_tag_present(skb)) {
		tx_flags |= IGB_TX_FLAGS_VLAN;
		tx_flags |= (vlan_tx_tag_get(skb) << IGB_TX_FLAGS_VLAN_SHIFT);
	}

	/* record initial flags and protocol */
	first->tx_flags = tx_flags;
	first->protocol = protocol;

	tso = igb_tso(tx_ring, first, &hdr_len);
	if (tso < 0)
		goto out_drop;
	else if (!tso)
		igb_tx_csum(tx_ring, first);

	igb_tx_map(tx_ring, first, hdr_len);

	/* Make sure there is space in the ring for the next send. */
	igb_maybe_stop_tx(tx_ring, MAX_SKB_FRAGS + 4);

	return NETDEV_TX_OK;

out_drop:
	igb_unmap_and_free_tx_resource(tx_ring, first);

	return NETDEV_TX_OK;
}

static inline struct igb_ring *igb_tx_queue_mapping(struct igb_adapter *adapter,
						    struct sk_buff *skb)
{
	unsigned int r_idx = skb->queue_mapping;

	if (r_idx >= adapter->num_tx_queues)
		r_idx = r_idx % adapter->num_tx_queues;

	return adapter->tx_ring[r_idx];
}

static netdev_tx_t igb_xmit_frame(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (test_bit(__IGB_DOWN, &adapter->state)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/*
	 * The minimum packet size with TCTL.PSP set is 17 so pad the skb
	 * in order to meet this minimum size requirement.
	 */
	if (skb->len < 17) {
		if (skb_padto(skb, 17))
			return NETDEV_TX_OK;
		skb->len = 17;
	}

	return igb_xmit_frame_ring(skb, igb_tx_queue_mapping(adapter, skb));
}

/**
 * igb_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void igb_tx_timeout(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/* Do the reset outside of interrupt context */
	adapter->tx_timeout_count++;

	if (hw->mac.type >= e1000_82580)
		hw->dev_spec._82575.global_device_reset = true;

	schedule_work(&adapter->reset_task);
	wr32(E1000_EICS,
	     (adapter->eims_enable_mask & ~adapter->eims_other));
}

static void igb_reset_task(struct work_struct *work)
{
	struct igb_adapter *adapter;
	adapter = container_of(work, struct igb_adapter, reset_task);

	igb_dump(adapter);
	netdev_err(adapter->netdev, "Reset adapter\n");
	igb_reinit_locked(adapter);
}

/**
 * igb_get_stats64 - Get System Network Statistics
 * @netdev: network interface device structure
 * @stats: rtnl_link_stats64 pointer
 *
 **/
static struct rtnl_link_stats64 *igb_get_stats64(struct net_device *netdev,
						 struct rtnl_link_stats64 *stats)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	spin_lock(&adapter->stats64_lock);
	igb_update_stats(adapter, &adapter->stats64);
	memcpy(stats, &adapter->stats64, sizeof(*stats));
	spin_unlock(&adapter->stats64_lock);

	return stats;
}

/**
 * igb_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int igb_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if ((new_mtu < 68) || (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		dev_err(&pdev->dev, "Invalid MTU setting\n");
		return -EINVAL;
	}

#define MAX_STD_JUMBO_FRAME_SIZE 9238
	if (max_frame > MAX_STD_JUMBO_FRAME_SIZE) {
		dev_err(&pdev->dev, "MTU > 9216 not supported.\n");
		return -EINVAL;
	}

	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);

	/* igb_down has a dependency on max_frame_size */
	adapter->max_frame_size = max_frame;

	if (netif_running(netdev))
		igb_down(adapter);

	dev_info(&pdev->dev, "changing MTU from %d to %d\n",
		 netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		igb_up(adapter);
	else
		igb_reset(adapter);

	clear_bit(__IGB_RESETTING, &adapter->state);

	return 0;
}

/**
 * igb_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/

void igb_update_stats(struct igb_adapter *adapter,
		      struct rtnl_link_stats64 *net_stats)
{
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	u32 reg, mpc;
	u16 phy_tmp;
	int i;
	u64 bytes, packets;
	unsigned int start;
	u64 _bytes, _packets;

#define PHY_IDLE_ERROR_COUNT_MASK 0x00FF

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pci_channel_offline(pdev))
		return;

	bytes = 0;
	packets = 0;
	for (i = 0; i < adapter->num_rx_queues; i++) {
		u32 rqdpc_tmp = rd32(E1000_RQDPC(i)) & 0x0FFF;
		struct igb_ring *ring = adapter->rx_ring[i];

		ring->rx_stats.drops += rqdpc_tmp;
		net_stats->rx_fifo_errors += rqdpc_tmp;

		do {
			start = u64_stats_fetch_begin_bh(&ring->rx_syncp);
			_bytes = ring->rx_stats.bytes;
			_packets = ring->rx_stats.packets;
		} while (u64_stats_fetch_retry_bh(&ring->rx_syncp, start));
		bytes += _bytes;
		packets += _packets;
	}

	net_stats->rx_bytes = bytes;
	net_stats->rx_packets = packets;

	bytes = 0;
	packets = 0;
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igb_ring *ring = adapter->tx_ring[i];
		do {
			start = u64_stats_fetch_begin_bh(&ring->tx_syncp);
			_bytes = ring->tx_stats.bytes;
			_packets = ring->tx_stats.packets;
		} while (u64_stats_fetch_retry_bh(&ring->tx_syncp, start));
		bytes += _bytes;
		packets += _packets;
	}
	net_stats->tx_bytes = bytes;
	net_stats->tx_packets = packets;

	/* read stats registers */
	adapter->stats.crcerrs += rd32(E1000_CRCERRS);
	adapter->stats.gprc += rd32(E1000_GPRC);
	adapter->stats.gorc += rd32(E1000_GORCL);
	rd32(E1000_GORCH); /* clear GORCL */
	adapter->stats.bprc += rd32(E1000_BPRC);
	adapter->stats.mprc += rd32(E1000_MPRC);
	adapter->stats.roc += rd32(E1000_ROC);

	adapter->stats.prc64 += rd32(E1000_PRC64);
	adapter->stats.prc127 += rd32(E1000_PRC127);
	adapter->stats.prc255 += rd32(E1000_PRC255);
	adapter->stats.prc511 += rd32(E1000_PRC511);
	adapter->stats.prc1023 += rd32(E1000_PRC1023);
	adapter->stats.prc1522 += rd32(E1000_PRC1522);
	adapter->stats.symerrs += rd32(E1000_SYMERRS);
	adapter->stats.sec += rd32(E1000_SEC);

	mpc = rd32(E1000_MPC);
	adapter->stats.mpc += mpc;
	net_stats->rx_fifo_errors += mpc;
	adapter->stats.scc += rd32(E1000_SCC);
	adapter->stats.ecol += rd32(E1000_ECOL);
	adapter->stats.mcc += rd32(E1000_MCC);
	adapter->stats.latecol += rd32(E1000_LATECOL);
	adapter->stats.dc += rd32(E1000_DC);
	adapter->stats.rlec += rd32(E1000_RLEC);
	adapter->stats.xonrxc += rd32(E1000_XONRXC);
	adapter->stats.xontxc += rd32(E1000_XONTXC);
	adapter->stats.xoffrxc += rd32(E1000_XOFFRXC);
	adapter->stats.xofftxc += rd32(E1000_XOFFTXC);
	adapter->stats.fcruc += rd32(E1000_FCRUC);
	adapter->stats.gptc += rd32(E1000_GPTC);
	adapter->stats.gotc += rd32(E1000_GOTCL);
	rd32(E1000_GOTCH); /* clear GOTCL */
	adapter->stats.rnbc += rd32(E1000_RNBC);
	adapter->stats.ruc += rd32(E1000_RUC);
	adapter->stats.rfc += rd32(E1000_RFC);
	adapter->stats.rjc += rd32(E1000_RJC);
	adapter->stats.tor += rd32(E1000_TORH);
	adapter->stats.tot += rd32(E1000_TOTH);
	adapter->stats.tpr += rd32(E1000_TPR);

	adapter->stats.ptc64 += rd32(E1000_PTC64);
	adapter->stats.ptc127 += rd32(E1000_PTC127);
	adapter->stats.ptc255 += rd32(E1000_PTC255);
	adapter->stats.ptc511 += rd32(E1000_PTC511);
	adapter->stats.ptc1023 += rd32(E1000_PTC1023);
	adapter->stats.ptc1522 += rd32(E1000_PTC1522);

	adapter->stats.mptc += rd32(E1000_MPTC);
	adapter->stats.bptc += rd32(E1000_BPTC);

	adapter->stats.tpt += rd32(E1000_TPT);
	adapter->stats.colc += rd32(E1000_COLC);

	adapter->stats.algnerrc += rd32(E1000_ALGNERRC);
	/* read internal phy specific stats */
	reg = rd32(E1000_CTRL_EXT);
	if (!(reg & E1000_CTRL_EXT_LINK_MODE_MASK)) {
		adapter->stats.rxerrc += rd32(E1000_RXERRC);
		adapter->stats.tncrs += rd32(E1000_TNCRS);
	}

	adapter->stats.tsctc += rd32(E1000_TSCTC);
	adapter->stats.tsctfc += rd32(E1000_TSCTFC);

	adapter->stats.iac += rd32(E1000_IAC);
	adapter->stats.icrxoc += rd32(E1000_ICRXOC);
	adapter->stats.icrxptc += rd32(E1000_ICRXPTC);
	adapter->stats.icrxatc += rd32(E1000_ICRXATC);
	adapter->stats.ictxptc += rd32(E1000_ICTXPTC);
	adapter->stats.ictxatc += rd32(E1000_ICTXATC);
	adapter->stats.ictxqec += rd32(E1000_ICTXQEC);
	adapter->stats.ictxqmtc += rd32(E1000_ICTXQMTC);
	adapter->stats.icrxdmtc += rd32(E1000_ICRXDMTC);

	/* Fill out the OS statistics structure */
	net_stats->multicast = adapter->stats.mprc;
	net_stats->collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	 * our own version based on RUC and ROC */
	net_stats->rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.ruc + adapter->stats.roc +
		adapter->stats.cexterr;
	net_stats->rx_length_errors = adapter->stats.ruc +
				      adapter->stats.roc;
	net_stats->rx_crc_errors = adapter->stats.crcerrs;
	net_stats->rx_frame_errors = adapter->stats.algnerrc;
	net_stats->rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	net_stats->tx_errors = adapter->stats.ecol +
			       adapter->stats.latecol;
	net_stats->tx_aborted_errors = adapter->stats.ecol;
	net_stats->tx_window_errors = adapter->stats.latecol;
	net_stats->tx_carrier_errors = adapter->stats.tncrs;

	/* Tx Dropped needs to be maintained elsewhere */

	/* Phy Stats */
	if (hw->phy.media_type == e1000_media_type_copper) {
		if ((adapter->link_speed == SPEED_1000) &&
		   (!igb_read_phy_reg(hw, PHY_1000T_STATUS, &phy_tmp))) {
			phy_tmp &= PHY_IDLE_ERROR_COUNT_MASK;
			adapter->phy_stats.idle_errors += phy_tmp;
		}
	}

	/* Management Stats */
	adapter->stats.mgptc += rd32(E1000_MGTPTC);
	adapter->stats.mgprc += rd32(E1000_MGTPRC);
	adapter->stats.mgpdc += rd32(E1000_MGTPDC);

	/* OS2BMC Stats */
	reg = rd32(E1000_MANC);
	if (reg & E1000_MANC_EN_BMC2OS) {
		adapter->stats.o2bgptc += rd32(E1000_O2BGPTC);
		adapter->stats.o2bspc += rd32(E1000_O2BSPC);
		adapter->stats.b2ospc += rd32(E1000_B2OSPC);
		adapter->stats.b2ogprc += rd32(E1000_B2OGPRC);
	}
}

static irqreturn_t igb_msix_other(int irq, void *data)
{
	struct igb_adapter *adapter = data;
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = rd32(E1000_ICR);
	/* reading ICR causes bit 31 of EICR to be cleared */

	if (icr & E1000_ICR_DRSTA)
		schedule_work(&adapter->reset_task);

	if (icr & E1000_ICR_DOUTSYNC) {
		/* HW is reporting DMA is out of sync */
		adapter->stats.doosync++;
		/* The DMA Out of Sync is also indication of a spoof event
		 * in IOV mode. Check the Wrong VM Behavior register to
		 * see if it is really a spoof event. */
		igb_check_wvbr(adapter);
	}

	/* Check for a mailbox event */
	if (icr & E1000_ICR_VMMB)
		igb_msg_task(adapter);

	if (icr & E1000_ICR_LSC) {
		hw->mac.get_link_status = 1;
		/* guard against interrupt when we're going down */
		if (!test_bit(__IGB_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

#ifdef CONFIG_IGB_PTP
	if (icr & E1000_ICR_TS) {
		u32 tsicr = rd32(E1000_TSICR);

		if (tsicr & E1000_TSICR_TXTS) {
			/* acknowledge the interrupt */
			wr32(E1000_TSICR, E1000_TSICR_TXTS);
			/* retrieve hardware timestamp */
			schedule_work(&adapter->ptp_tx_work);
		}
	}
#endif /* CONFIG_IGB_PTP */

	wr32(E1000_EIMS, adapter->eims_other);

	return IRQ_HANDLED;
}

static void igb_write_itr(struct igb_q_vector *q_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	u32 itr_val = q_vector->itr_val & 0x7FFC;

	if (!q_vector->set_itr)
		return;

	if (!itr_val)
		itr_val = 0x4;

	if (adapter->hw.mac.type == e1000_82575)
		itr_val |= itr_val << 16;
	else
		itr_val |= E1000_EITR_CNT_IGNR;

	writel(itr_val, q_vector->itr_register);
	q_vector->set_itr = 0;
}

static irqreturn_t igb_msix_ring(int irq, void *data)
{
	struct igb_q_vector *q_vector = data;

	/* Write the ITR value calculated from the previous interrupt. */
	igb_write_itr(q_vector);

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

#ifdef CONFIG_IGB_DCA
static void igb_update_dca(struct igb_q_vector *q_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	struct e1000_hw *hw = &adapter->hw;
	int cpu = get_cpu();

	if (q_vector->cpu == cpu)
		goto out_no_update;

	if (q_vector->tx.ring) {
		int q = q_vector->tx.ring->reg_idx;
		u32 dca_txctrl = rd32(E1000_DCA_TXCTRL(q));
		if (hw->mac.type == e1000_82575) {
			dca_txctrl &= ~E1000_DCA_TXCTRL_CPUID_MASK;
			dca_txctrl |= dca3_get_tag(&adapter->pdev->dev, cpu);
		} else {
			dca_txctrl &= ~E1000_DCA_TXCTRL_CPUID_MASK_82576;
			dca_txctrl |= dca3_get_tag(&adapter->pdev->dev, cpu) <<
			              E1000_DCA_TXCTRL_CPUID_SHIFT;
		}
		dca_txctrl |= E1000_DCA_TXCTRL_DESC_DCA_EN;
		wr32(E1000_DCA_TXCTRL(q), dca_txctrl);
	}
	if (q_vector->rx.ring) {
		int q = q_vector->rx.ring->reg_idx;
		u32 dca_rxctrl = rd32(E1000_DCA_RXCTRL(q));
		if (hw->mac.type == e1000_82575) {
			dca_rxctrl &= ~E1000_DCA_RXCTRL_CPUID_MASK;
			dca_rxctrl |= dca3_get_tag(&adapter->pdev->dev, cpu);
		} else {
			dca_rxctrl &= ~E1000_DCA_RXCTRL_CPUID_MASK_82576;
			dca_rxctrl |= dca3_get_tag(&adapter->pdev->dev, cpu) <<
			              E1000_DCA_RXCTRL_CPUID_SHIFT;
		}
		dca_rxctrl |= E1000_DCA_RXCTRL_DESC_DCA_EN;
		dca_rxctrl |= E1000_DCA_RXCTRL_HEAD_DCA_EN;
		dca_rxctrl |= E1000_DCA_RXCTRL_DATA_DCA_EN;
		wr32(E1000_DCA_RXCTRL(q), dca_rxctrl);
	}
	q_vector->cpu = cpu;
out_no_update:
	put_cpu();
}

static void igb_setup_dca(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	int i;

	if (!(adapter->flags & IGB_FLAG_DCA_ENABLED))
		return;

	/* Always use CB2 mode, difference is masked in the CB driver. */
	wr32(E1000_DCA_CTRL, E1000_DCA_CTRL_DCA_MODE_CB2);

	for (i = 0; i < adapter->num_q_vectors; i++) {
		adapter->q_vector[i]->cpu = -1;
		igb_update_dca(adapter->q_vector[i]);
	}
}

static int __igb_notify_dca(struct device *dev, void *data)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	unsigned long event = *(unsigned long *)data;

	switch (event) {
	case DCA_PROVIDER_ADD:
		/* if already enabled, don't do it again */
		if (adapter->flags & IGB_FLAG_DCA_ENABLED)
			break;
		if (dca_add_requester(dev) == 0) {
			adapter->flags |= IGB_FLAG_DCA_ENABLED;
			dev_info(&pdev->dev, "DCA enabled\n");
			igb_setup_dca(adapter);
			break;
		}
		/* Fall Through since DCA is disabled. */
	case DCA_PROVIDER_REMOVE:
		if (adapter->flags & IGB_FLAG_DCA_ENABLED) {
			/* without this a class_device is left
			 * hanging around in the sysfs model */
			dca_remove_requester(dev);
			dev_info(&pdev->dev, "DCA disabled\n");
			adapter->flags &= ~IGB_FLAG_DCA_ENABLED;
			wr32(E1000_DCA_CTRL, E1000_DCA_CTRL_DCA_MODE_DISABLE);
		}
		break;
	}

	return 0;
}

static int igb_notify_dca(struct notifier_block *nb, unsigned long event,
                          void *p)
{
	int ret_val;

	ret_val = driver_for_each_device(&igb_driver.driver, NULL, &event,
	                                 __igb_notify_dca);

	return ret_val ? NOTIFY_BAD : NOTIFY_DONE;
}
#endif /* CONFIG_IGB_DCA */

#ifdef CONFIG_PCI_IOV
static int igb_vf_configure(struct igb_adapter *adapter, int vf)
{
	unsigned char mac_addr[ETH_ALEN];
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pvfdev;
	unsigned int device_id;
	u16 thisvf_devfn;

	eth_random_addr(mac_addr);
	igb_set_vf_mac(adapter, vf, mac_addr);

	switch (adapter->hw.mac.type) {
	case e1000_82576:
		device_id = IGB_82576_VF_DEV_ID;
		/* VF Stride for 82576 is 2 */
		thisvf_devfn = (pdev->devfn + 0x80 + (vf << 1)) |
			(pdev->devfn & 1);
		break;
	case e1000_i350:
		device_id = IGB_I350_VF_DEV_ID;
		/* VF Stride for I350 is 4 */
		thisvf_devfn = (pdev->devfn + 0x80 + (vf << 2)) |
				(pdev->devfn & 3);
		break;
	default:
		device_id = 0;
		thisvf_devfn = 0;
		break;
	}

	pvfdev = pci_get_device(hw->vendor_id, device_id, NULL);
	while (pvfdev) {
		if (pvfdev->devfn == thisvf_devfn)
			break;
		pvfdev = pci_get_device(hw->vendor_id,
					device_id, pvfdev);
	}

	if (pvfdev)
		adapter->vf_data[vf].vfdev = pvfdev;
	else
		dev_err(&pdev->dev,
			"Couldn't find pci dev ptr for VF %4.4x\n",
			thisvf_devfn);
	return pvfdev != NULL;
}

static int igb_find_enabled_vfs(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	struct pci_dev *pvfdev;
	u16 vf_devfn = 0;
	u16 vf_stride;
	unsigned int device_id;
	int vfs_found = 0;

	switch (adapter->hw.mac.type) {
	case e1000_82576:
		device_id = IGB_82576_VF_DEV_ID;
		/* VF Stride for 82576 is 2 */
		vf_stride = 2;
		break;
	case e1000_i350:
		device_id = IGB_I350_VF_DEV_ID;
		/* VF Stride for I350 is 4 */
		vf_stride = 4;
		break;
	default:
		device_id = 0;
		vf_stride = 0;
		break;
	}

	vf_devfn = pdev->devfn + 0x80;
	pvfdev = pci_get_device(hw->vendor_id, device_id, NULL);
	while (pvfdev) {
		if (pvfdev->devfn == vf_devfn &&
		    (pvfdev->bus->number >= pdev->bus->number))
			vfs_found++;
		vf_devfn += vf_stride;
		pvfdev = pci_get_device(hw->vendor_id,
					device_id, pvfdev);
	}

	return vfs_found;
}

static int igb_check_vf_assignment(struct igb_adapter *adapter)
{
	int i;
	for (i = 0; i < adapter->vfs_allocated_count; i++) {
		if (adapter->vf_data[i].vfdev) {
			if (adapter->vf_data[i].vfdev->dev_flags &
			    PCI_DEV_FLAGS_ASSIGNED)
				return true;
		}
	}
	return false;
}

#endif
static void igb_ping_all_vfs(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ping;
	int i;

	for (i = 0 ; i < adapter->vfs_allocated_count; i++) {
		ping = E1000_PF_CONTROL_MSG;
		if (adapter->vf_data[i].flags & IGB_VF_FLAG_CTS)
			ping |= E1000_VT_MSGTYPE_CTS;
		igb_write_mbx(hw, &ping, 1, i);
	}
}

static int igb_set_vf_promisc(struct igb_adapter *adapter, u32 *msgbuf, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 vmolr = rd32(E1000_VMOLR(vf));
	struct vf_data_storage *vf_data = &adapter->vf_data[vf];

	vf_data->flags &= ~(IGB_VF_FLAG_UNI_PROMISC |
	                    IGB_VF_FLAG_MULTI_PROMISC);
	vmolr &= ~(E1000_VMOLR_ROPE | E1000_VMOLR_ROMPE | E1000_VMOLR_MPME);

	if (*msgbuf & E1000_VF_SET_PROMISC_MULTICAST) {
		vmolr |= E1000_VMOLR_MPME;
		vf_data->flags |= IGB_VF_FLAG_MULTI_PROMISC;
		*msgbuf &= ~E1000_VF_SET_PROMISC_MULTICAST;
	} else {
		/*
		 * if we have hashes and we are clearing a multicast promisc
		 * flag we need to write the hashes to the MTA as this step
		 * was previously skipped
		 */
		if (vf_data->num_vf_mc_hashes > 30) {
			vmolr |= E1000_VMOLR_MPME;
		} else if (vf_data->num_vf_mc_hashes) {
			int j;
			vmolr |= E1000_VMOLR_ROMPE;
			for (j = 0; j < vf_data->num_vf_mc_hashes; j++)
				igb_mta_set(hw, vf_data->vf_mc_hashes[j]);
		}
	}

	wr32(E1000_VMOLR(vf), vmolr);

	/* there are flags left unprocessed, likely not supported */
	if (*msgbuf & E1000_VT_MSGINFO_MASK)
		return -EINVAL;

	return 0;

}

static int igb_set_vf_multicasts(struct igb_adapter *adapter,
				  u32 *msgbuf, u32 vf)
{
	int n = (msgbuf[0] & E1000_VT_MSGINFO_MASK) >> E1000_VT_MSGINFO_SHIFT;
	u16 *hash_list = (u16 *)&msgbuf[1];
	struct vf_data_storage *vf_data = &adapter->vf_data[vf];
	int i;

	/* salt away the number of multicast addresses assigned
	 * to this VF for later use to restore when the PF multi cast
	 * list changes
	 */
	vf_data->num_vf_mc_hashes = n;

	/* only up to 30 hash values supported */
	if (n > 30)
		n = 30;

	/* store the hashes for later use */
	for (i = 0; i < n; i++)
		vf_data->vf_mc_hashes[i] = hash_list[i];

	/* Flush and reset the mta with the new values */
	igb_set_rx_mode(adapter->netdev);

	return 0;
}

static void igb_restore_vf_multicasts(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct vf_data_storage *vf_data;
	int i, j;

	for (i = 0; i < adapter->vfs_allocated_count; i++) {
		u32 vmolr = rd32(E1000_VMOLR(i));
		vmolr &= ~(E1000_VMOLR_ROMPE | E1000_VMOLR_MPME);

		vf_data = &adapter->vf_data[i];

		if ((vf_data->num_vf_mc_hashes > 30) ||
		    (vf_data->flags & IGB_VF_FLAG_MULTI_PROMISC)) {
			vmolr |= E1000_VMOLR_MPME;
		} else if (vf_data->num_vf_mc_hashes) {
			vmolr |= E1000_VMOLR_ROMPE;
			for (j = 0; j < vf_data->num_vf_mc_hashes; j++)
				igb_mta_set(hw, vf_data->vf_mc_hashes[j]);
		}
		wr32(E1000_VMOLR(i), vmolr);
	}
}

static void igb_clear_vf_vfta(struct igb_adapter *adapter, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 pool_mask, reg, vid;
	int i;

	pool_mask = 1 << (E1000_VLVF_POOLSEL_SHIFT + vf);

	/* Find the vlan filter for this id */
	for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
		reg = rd32(E1000_VLVF(i));

		/* remove the vf from the pool */
		reg &= ~pool_mask;

		/* if pool is empty then remove entry from vfta */
		if (!(reg & E1000_VLVF_POOLSEL_MASK) &&
		    (reg & E1000_VLVF_VLANID_ENABLE)) {
			reg = 0;
			vid = reg & E1000_VLVF_VLANID_MASK;
			igb_vfta_set(hw, vid, false);
		}

		wr32(E1000_VLVF(i), reg);
	}

	adapter->vf_data[vf].vlans_enabled = 0;
}

static s32 igb_vlvf_set(struct igb_adapter *adapter, u32 vid, bool add, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 reg, i;

	/* The vlvf table only exists on 82576 hardware and newer */
	if (hw->mac.type < e1000_82576)
		return -1;

	/* we only need to do this if VMDq is enabled */
	if (!adapter->vfs_allocated_count)
		return -1;

	/* Find the vlan filter for this id */
	for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
		reg = rd32(E1000_VLVF(i));
		if ((reg & E1000_VLVF_VLANID_ENABLE) &&
		    vid == (reg & E1000_VLVF_VLANID_MASK))
			break;
	}

	if (add) {
		if (i == E1000_VLVF_ARRAY_SIZE) {
			/* Did not find a matching VLAN ID entry that was
			 * enabled.  Search for a free filter entry, i.e.
			 * one without the enable bit set
			 */
			for (i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
				reg = rd32(E1000_VLVF(i));
				if (!(reg & E1000_VLVF_VLANID_ENABLE))
					break;
			}
		}
		if (i < E1000_VLVF_ARRAY_SIZE) {
			/* Found an enabled/available entry */
			reg |= 1 << (E1000_VLVF_POOLSEL_SHIFT + vf);

			/* if !enabled we need to set this up in vfta */
			if (!(reg & E1000_VLVF_VLANID_ENABLE)) {
				/* add VID to filter table */
				igb_vfta_set(hw, vid, true);
				reg |= E1000_VLVF_VLANID_ENABLE;
			}
			reg &= ~E1000_VLVF_VLANID_MASK;
			reg |= vid;
			wr32(E1000_VLVF(i), reg);

			/* do not modify RLPML for PF devices */
			if (vf >= adapter->vfs_allocated_count)
				return 0;

			if (!adapter->vf_data[vf].vlans_enabled) {
				u32 size;
				reg = rd32(E1000_VMOLR(vf));
				size = reg & E1000_VMOLR_RLPML_MASK;
				size += 4;
				reg &= ~E1000_VMOLR_RLPML_MASK;
				reg |= size;
				wr32(E1000_VMOLR(vf), reg);
			}

			adapter->vf_data[vf].vlans_enabled++;
		}
	} else {
		if (i < E1000_VLVF_ARRAY_SIZE) {
			/* remove vf from the pool */
			reg &= ~(1 << (E1000_VLVF_POOLSEL_SHIFT + vf));
			/* if pool is empty then remove entry from vfta */
			if (!(reg & E1000_VLVF_POOLSEL_MASK)) {
				reg = 0;
				igb_vfta_set(hw, vid, false);
			}
			wr32(E1000_VLVF(i), reg);

			/* do not modify RLPML for PF devices */
			if (vf >= adapter->vfs_allocated_count)
				return 0;

			adapter->vf_data[vf].vlans_enabled--;
			if (!adapter->vf_data[vf].vlans_enabled) {
				u32 size;
				reg = rd32(E1000_VMOLR(vf));
				size = reg & E1000_VMOLR_RLPML_MASK;
				size -= 4;
				reg &= ~E1000_VMOLR_RLPML_MASK;
				reg |= size;
				wr32(E1000_VMOLR(vf), reg);
			}
		}
	}
	return 0;
}

static void igb_set_vmvir(struct igb_adapter *adapter, u32 vid, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;

	if (vid)
		wr32(E1000_VMVIR(vf), (vid | E1000_VMVIR_VLANA_DEFAULT));
	else
		wr32(E1000_VMVIR(vf), 0);
}

static int igb_ndo_set_vf_vlan(struct net_device *netdev,
			       int vf, u16 vlan, u8 qos)
{
	int err = 0;
	struct igb_adapter *adapter = netdev_priv(netdev);

	if ((vf >= adapter->vfs_allocated_count) || (vlan > 4095) || (qos > 7))
		return -EINVAL;
	if (vlan || qos) {
		err = igb_vlvf_set(adapter, vlan, !!vlan, vf);
		if (err)
			goto out;
		igb_set_vmvir(adapter, vlan | (qos << VLAN_PRIO_SHIFT), vf);
		igb_set_vmolr(adapter, vf, !vlan);
		adapter->vf_data[vf].pf_vlan = vlan;
		adapter->vf_data[vf].pf_qos = qos;
		dev_info(&adapter->pdev->dev,
			 "Setting VLAN %d, QOS 0x%x on VF %d\n", vlan, qos, vf);
		if (test_bit(__IGB_DOWN, &adapter->state)) {
			dev_warn(&adapter->pdev->dev,
				 "The VF VLAN has been set,"
				 " but the PF device is not up.\n");
			dev_warn(&adapter->pdev->dev,
				 "Bring the PF device up before"
				 " attempting to use the VF device.\n");
		}
	} else {
		igb_vlvf_set(adapter, adapter->vf_data[vf].pf_vlan,
				   false, vf);
		igb_set_vmvir(adapter, vlan, vf);
		igb_set_vmolr(adapter, vf, true);
		adapter->vf_data[vf].pf_vlan = 0;
		adapter->vf_data[vf].pf_qos = 0;
       }
out:
       return err;
}

static int igb_set_vf_vlan(struct igb_adapter *adapter, u32 *msgbuf, u32 vf)
{
	int add = (msgbuf[0] & E1000_VT_MSGINFO_MASK) >> E1000_VT_MSGINFO_SHIFT;
	int vid = (msgbuf[1] & E1000_VLVF_VLANID_MASK);

	return igb_vlvf_set(adapter, vid, add, vf);
}

static inline void igb_vf_reset(struct igb_adapter *adapter, u32 vf)
{
	/* clear flags - except flag that indicates PF has set the MAC */
	adapter->vf_data[vf].flags &= IGB_VF_FLAG_PF_SET_MAC;
	adapter->vf_data[vf].last_nack = jiffies;

	/* reset offloads to defaults */
	igb_set_vmolr(adapter, vf, true);

	/* reset vlans for device */
	igb_clear_vf_vfta(adapter, vf);
	if (adapter->vf_data[vf].pf_vlan)
		igb_ndo_set_vf_vlan(adapter->netdev, vf,
				    adapter->vf_data[vf].pf_vlan,
				    adapter->vf_data[vf].pf_qos);
	else
		igb_clear_vf_vfta(adapter, vf);

	/* reset multicast table array for vf */
	adapter->vf_data[vf].num_vf_mc_hashes = 0;

	/* Flush and reset the mta with the new values */
	igb_set_rx_mode(adapter->netdev);
}

static void igb_vf_reset_event(struct igb_adapter *adapter, u32 vf)
{
	unsigned char *vf_mac = adapter->vf_data[vf].vf_mac_addresses;

	/* generate a new mac address as we were hotplug removed/added */
	if (!(adapter->vf_data[vf].flags & IGB_VF_FLAG_PF_SET_MAC))
		eth_random_addr(vf_mac);

	/* process remaining reset events */
	igb_vf_reset(adapter, vf);
}

static void igb_vf_reset_msg(struct igb_adapter *adapter, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;
	unsigned char *vf_mac = adapter->vf_data[vf].vf_mac_addresses;
	int rar_entry = hw->mac.rar_entry_count - (vf + 1);
	u32 reg, msgbuf[3];
	u8 *addr = (u8 *)(&msgbuf[1]);

	/* process all the same items cleared in a function level reset */
	igb_vf_reset(adapter, vf);

	/* set vf mac address */
	igb_rar_set_qsel(adapter, vf_mac, rar_entry, vf);

	/* enable transmit and receive for vf */
	reg = rd32(E1000_VFTE);
	wr32(E1000_VFTE, reg | (1 << vf));
	reg = rd32(E1000_VFRE);
	wr32(E1000_VFRE, reg | (1 << vf));

	adapter->vf_data[vf].flags |= IGB_VF_FLAG_CTS;

	/* reply to reset with ack and vf mac address */
	msgbuf[0] = E1000_VF_RESET | E1000_VT_MSGTYPE_ACK;
	memcpy(addr, vf_mac, 6);
	igb_write_mbx(hw, msgbuf, 3, vf);
}

static int igb_set_vf_mac_addr(struct igb_adapter *adapter, u32 *msg, int vf)
{
	/*
	 * The VF MAC Address is stored in a packed array of bytes
	 * starting at the second 32 bit word of the msg array
	 */
	unsigned char *addr = (char *)&msg[1];
	int err = -1;

	if (is_valid_ether_addr(addr))
		err = igb_set_vf_mac(adapter, vf, addr);

	return err;
}

static void igb_rcv_ack_from_vf(struct igb_adapter *adapter, u32 vf)
{
	struct e1000_hw *hw = &adapter->hw;
	struct vf_data_storage *vf_data = &adapter->vf_data[vf];
	u32 msg = E1000_VT_MSGTYPE_NACK;

	/* if device isn't clear to send it shouldn't be reading either */
	if (!(vf_data->flags & IGB_VF_FLAG_CTS) &&
	    time_after(jiffies, vf_data->last_nack + (2 * HZ))) {
		igb_write_mbx(hw, &msg, 1, vf);
		vf_data->last_nack = jiffies;
	}
}

static void igb_rcv_msg_from_vf(struct igb_adapter *adapter, u32 vf)
{
	struct pci_dev *pdev = adapter->pdev;
	u32 msgbuf[E1000_VFMAILBOX_SIZE];
	struct e1000_hw *hw = &adapter->hw;
	struct vf_data_storage *vf_data = &adapter->vf_data[vf];
	s32 retval;

	retval = igb_read_mbx(hw, msgbuf, E1000_VFMAILBOX_SIZE, vf);

	if (retval) {
		/* if receive failed revoke VF CTS stats and restart init */
		dev_err(&pdev->dev, "Error receiving message from VF\n");
		vf_data->flags &= ~IGB_VF_FLAG_CTS;
		if (!time_after(jiffies, vf_data->last_nack + (2 * HZ)))
			return;
		goto out;
	}

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (E1000_VT_MSGTYPE_ACK | E1000_VT_MSGTYPE_NACK))
		return;

	/*
	 * until the vf completes a reset it should not be
	 * allowed to start any configuration.
	 */

	if (msgbuf[0] == E1000_VF_RESET) {
		igb_vf_reset_msg(adapter, vf);
		return;
	}

	if (!(vf_data->flags & IGB_VF_FLAG_CTS)) {
		if (!time_after(jiffies, vf_data->last_nack + (2 * HZ)))
			return;
		retval = -1;
		goto out;
	}

	switch ((msgbuf[0] & 0xFFFF)) {
	case E1000_VF_SET_MAC_ADDR:
		retval = -EINVAL;
		if (!(vf_data->flags & IGB_VF_FLAG_PF_SET_MAC))
			retval = igb_set_vf_mac_addr(adapter, msgbuf, vf);
		else
			dev_warn(&pdev->dev,
				 "VF %d attempted to override administratively "
				 "set MAC address\nReload the VF driver to "
				 "resume operations\n", vf);
		break;
	case E1000_VF_SET_PROMISC:
		retval = igb_set_vf_promisc(adapter, msgbuf, vf);
		break;
	case E1000_VF_SET_MULTICAST:
		retval = igb_set_vf_multicasts(adapter, msgbuf, vf);
		break;
	case E1000_VF_SET_LPE:
		retval = igb_set_vf_rlpml(adapter, msgbuf[1], vf);
		break;
	case E1000_VF_SET_VLAN:
		retval = -1;
		if (vf_data->pf_vlan)
			dev_warn(&pdev->dev,
				 "VF %d attempted to override administratively "
				 "set VLAN tag\nReload the VF driver to "
				 "resume operations\n", vf);
		else
			retval = igb_set_vf_vlan(adapter, msgbuf, vf);
		break;
	default:
		dev_err(&pdev->dev, "Unhandled Msg %08x\n", msgbuf[0]);
		retval = -1;
		break;
	}

	msgbuf[0] |= E1000_VT_MSGTYPE_CTS;
out:
	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= E1000_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= E1000_VT_MSGTYPE_ACK;

	igb_write_mbx(hw, msgbuf, 1, vf);
}

static void igb_msg_task(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 vf;

	for (vf = 0; vf < adapter->vfs_allocated_count; vf++) {
		/* process any reset requests */
		if (!igb_check_for_rst(hw, vf))
			igb_vf_reset_event(adapter, vf);

		/* process any messages pending */
		if (!igb_check_for_msg(hw, vf))
			igb_rcv_msg_from_vf(adapter, vf);

		/* process any acks */
		if (!igb_check_for_ack(hw, vf))
			igb_rcv_ack_from_vf(adapter, vf);
	}
}

/**
 *  igb_set_uta - Set unicast filter table address
 *  @adapter: board private structure
 *
 *  The unicast table address is a register array of 32-bit registers.
 *  The table is meant to be used in a way similar to how the MTA is used
 *  however due to certain limitations in the hardware it is necessary to
 *  set all the hash bits to 1 and use the VMOLR ROPE bit as a promiscuous
 *  enable bit to allow vlan tag stripping when promiscuous mode is enabled
 **/
static void igb_set_uta(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	int i;

	/* The UTA table only exists on 82576 hardware and newer */
	if (hw->mac.type < e1000_82576)
		return;

	/* we only need to do this if VMDq is enabled */
	if (!adapter->vfs_allocated_count)
		return;

	for (i = 0; i < hw->mac.uta_reg_count; i++)
		array_wr32(E1000_UTA, i, ~0);
}

/**
 * igb_intr_msi - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t igb_intr_msi(int irq, void *data)
{
	struct igb_adapter *adapter = data;
	struct igb_q_vector *q_vector = adapter->q_vector[0];
	struct e1000_hw *hw = &adapter->hw;
	/* read ICR disables interrupts using IAM */
	u32 icr = rd32(E1000_ICR);

	igb_write_itr(q_vector);

	if (icr & E1000_ICR_DRSTA)
		schedule_work(&adapter->reset_task);

	if (icr & E1000_ICR_DOUTSYNC) {
		/* HW is reporting DMA is out of sync */
		adapter->stats.doosync++;
	}

	if (icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		hw->mac.get_link_status = 1;
		if (!test_bit(__IGB_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

#ifdef CONFIG_IGB_PTP
	if (icr & E1000_ICR_TS) {
		u32 tsicr = rd32(E1000_TSICR);

		if (tsicr & E1000_TSICR_TXTS) {
			/* acknowledge the interrupt */
			wr32(E1000_TSICR, E1000_TSICR_TXTS);
			/* retrieve hardware timestamp */
			schedule_work(&adapter->ptp_tx_work);
		}
	}
#endif /* CONFIG_IGB_PTP */

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * igb_intr - Legacy Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t igb_intr(int irq, void *data)
{
	struct igb_adapter *adapter = data;
	struct igb_q_vector *q_vector = adapter->q_vector[0];
	struct e1000_hw *hw = &adapter->hw;
	/* Interrupt Auto-Mask...upon reading ICR, interrupts are masked.  No
	 * need for the IMC write */
	u32 icr = rd32(E1000_ICR);

	/* IMS will not auto-mask if INT_ASSERTED is not set, and if it is
	 * not set, then the adapter didn't send an interrupt */
	if (!(icr & E1000_ICR_INT_ASSERTED))
		return IRQ_NONE;

	igb_write_itr(q_vector);

	if (icr & E1000_ICR_DRSTA)
		schedule_work(&adapter->reset_task);

	if (icr & E1000_ICR_DOUTSYNC) {
		/* HW is reporting DMA is out of sync */
		adapter->stats.doosync++;
	}

	if (icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC)) {
		hw->mac.get_link_status = 1;
		/* guard against interrupt when we're going down */
		if (!test_bit(__IGB_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

#ifdef CONFIG_IGB_PTP
	if (icr & E1000_ICR_TS) {
		u32 tsicr = rd32(E1000_TSICR);

		if (tsicr & E1000_TSICR_TXTS) {
			/* acknowledge the interrupt */
			wr32(E1000_TSICR, E1000_TSICR_TXTS);
			/* retrieve hardware timestamp */
			schedule_work(&adapter->ptp_tx_work);
		}
	}
#endif /* CONFIG_IGB_PTP */

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

static void igb_ring_irq_enable(struct igb_q_vector *q_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	struct e1000_hw *hw = &adapter->hw;

	if ((q_vector->rx.ring && (adapter->rx_itr_setting & 3)) ||
	    (!q_vector->rx.ring && (adapter->tx_itr_setting & 3))) {
		if ((adapter->num_q_vectors == 1) && !adapter->vf_data)
			igb_set_itr(q_vector);
		else
			igb_update_ring_itr(q_vector);
	}

	if (!test_bit(__IGB_DOWN, &adapter->state)) {
		if (adapter->msix_entries)
			wr32(E1000_EIMS, q_vector->eims_value);
		else
			igb_irq_enable(adapter);
	}
}

/**
 * igb_poll - NAPI Rx polling callback
 * @napi: napi polling structure
 * @budget: count of how many packets we should handle
 **/
static int igb_poll(struct napi_struct *napi, int budget)
{
	struct igb_q_vector *q_vector = container_of(napi,
	                                             struct igb_q_vector,
	                                             napi);
	bool clean_complete = true;

#ifdef CONFIG_IGB_DCA
	if (q_vector->adapter->flags & IGB_FLAG_DCA_ENABLED)
		igb_update_dca(q_vector);
#endif
	if (q_vector->tx.ring)
		clean_complete = igb_clean_tx_irq(q_vector);

	if (q_vector->rx.ring)
		clean_complete &= igb_clean_rx_irq(q_vector, budget);

	/* If all work not completed, return budget and keep polling */
	if (!clean_complete)
		return budget;

	/* If not enough Rx work done, exit the polling mode */
	napi_complete(napi);
	igb_ring_irq_enable(q_vector);

	return 0;
}

/**
 * igb_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: pointer to q_vector containing needed info
 *
 * returns true if ring is completely cleaned
 **/
static bool igb_clean_tx_irq(struct igb_q_vector *q_vector)
{
	struct igb_adapter *adapter = q_vector->adapter;
	struct igb_ring *tx_ring = q_vector->tx.ring;
	struct igb_tx_buffer *tx_buffer;
	union e1000_adv_tx_desc *tx_desc, *eop_desc;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int budget = q_vector->tx.work_limit;
	unsigned int i = tx_ring->next_to_clean;

	if (test_bit(__IGB_DOWN, &adapter->state))
		return true;

	tx_buffer = &tx_ring->tx_buffer_info[i];
	tx_desc = IGB_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	for (; budget; budget--) {
		eop_desc = tx_buffer->next_to_watch;

		/* prevent any other reads prior to eop_desc */
		rmb();

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* if DD is not set pending work has not been completed */
		if (!(eop_desc->wb.status & cpu_to_le32(E1000_TXD_STAT_DD)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buffer->bytecount;
		total_packets += tx_buffer->gso_segs;

		/* free the skb */
		dev_kfree_skb_any(tx_buffer->skb);
		tx_buffer->skb = NULL;

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 tx_buffer->dma,
				 tx_buffer->length,
				 DMA_TO_DEVICE);

		/* clear last DMA location and unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer->dma = 0;

			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = IGB_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (tx_buffer->dma) {
				dma_unmap_page(tx_ring->dev,
					       tx_buffer->dma,
					       tx_buffer->length,
					       DMA_TO_DEVICE);
			}
		}

		/* clear last DMA location */
		tx_buffer->dma = 0;

		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buffer = tx_ring->tx_buffer_info;
			tx_desc = IGB_TX_DESC(tx_ring, 0);
		}
	}

	netdev_tx_completed_queue(txring_txq(tx_ring),
				  total_packets, total_bytes);
	i += tx_ring->count;
	tx_ring->next_to_clean = i;
	u64_stats_update_begin(&tx_ring->tx_syncp);
	tx_ring->tx_stats.bytes += total_bytes;
	tx_ring->tx_stats.packets += total_packets;
	u64_stats_update_end(&tx_ring->tx_syncp);
	q_vector->tx.total_bytes += total_bytes;
	q_vector->tx.total_packets += total_packets;

	if (test_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags)) {
		struct e1000_hw *hw = &adapter->hw;

		eop_desc = tx_buffer->next_to_watch;

		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i */
		clear_bit(IGB_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
		if (eop_desc &&
		    time_after(jiffies, tx_buffer->time_stamp +
			       (adapter->tx_timeout_factor * HZ)) &&
		    !(rd32(E1000_STATUS) & E1000_STATUS_TXOFF)) {

			/* detected Tx unit hang */
			dev_err(tx_ring->dev,
				"Detected Tx Unit Hang\n"
				"  Tx Queue             <%d>\n"
				"  TDH                  <%x>\n"
				"  TDT                  <%x>\n"
				"  next_to_use          <%x>\n"
				"  next_to_clean        <%x>\n"
				"buffer_info[next_to_clean]\n"
				"  time_stamp           <%lx>\n"
				"  next_to_watch        <%p>\n"
				"  jiffies              <%lx>\n"
				"  desc.status          <%x>\n",
				tx_ring->queue_index,
				rd32(E1000_TDH(tx_ring->reg_idx)),
				readl(tx_ring->tail),
				tx_ring->next_to_use,
				tx_ring->next_to_clean,
				tx_buffer->time_stamp,
				eop_desc,
				jiffies,
				eop_desc->wb.status);
			netif_stop_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);

			/* we are about to reset, no point in enabling stuff */
			return true;
		}
	}

	if (unlikely(total_packets &&
		     netif_carrier_ok(tx_ring->netdev) &&
		     igb_desc_unused(tx_ring) >= IGB_TX_QUEUE_WAKE)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->queue_index) &&
		    !(test_bit(__IGB_DOWN, &adapter->state))) {
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);

			u64_stats_update_begin(&tx_ring->tx_syncp);
			tx_ring->tx_stats.restart_queue++;
			u64_stats_update_end(&tx_ring->tx_syncp);
		}
	}

	return !!budget;
}

static inline void igb_rx_checksum(struct igb_ring *ring,
				   union e1000_adv_rx_desc *rx_desc,
				   struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	/* Ignore Checksum bit is set */
	if (igb_test_staterr(rx_desc, E1000_RXD_STAT_IXSM))
		return;

	/* Rx checksum disabled via ethtool */
	if (!(ring->netdev->features & NETIF_F_RXCSUM))
		return;

	/* TCP/UDP checksum error bit is set */
	if (igb_test_staterr(rx_desc,
			     E1000_RXDEXT_STATERR_TCPE |
			     E1000_RXDEXT_STATERR_IPE)) {
		/*
		 * work around errata with sctp packets where the TCPE aka
		 * L4E bit is set incorrectly on 64 byte (60 byte w/o crc)
		 * packets, (aka let the stack check the crc32c)
		 */
		if (!((skb->len == 60) &&
		      test_bit(IGB_RING_FLAG_RX_SCTP_CSUM, &ring->flags))) {
			u64_stats_update_begin(&ring->rx_syncp);
			ring->rx_stats.csum_err++;
			u64_stats_update_end(&ring->rx_syncp);
		}
		/* let the stack verify checksum errors */
		return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (igb_test_staterr(rx_desc, E1000_RXD_STAT_TCPCS |
				      E1000_RXD_STAT_UDPCS))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	dev_dbg(ring->dev, "cksum success: bits %08X\n",
		le32_to_cpu(rx_desc->wb.upper.status_error));
}

static inline void igb_rx_hash(struct igb_ring *ring,
			       union e1000_adv_rx_desc *rx_desc,
			       struct sk_buff *skb)
{
	if (ring->netdev->features & NETIF_F_RXHASH)
		skb->rxhash = le32_to_cpu(rx_desc->wb.lower.hi_dword.rss);
}

static void igb_rx_vlan(struct igb_ring *ring,
			union e1000_adv_rx_desc *rx_desc,
			struct sk_buff *skb)
{
	if (igb_test_staterr(rx_desc, E1000_RXD_STAT_VP)) {
		u16 vid;
		if (igb_test_staterr(rx_desc, E1000_RXDEXT_STATERR_LB) &&
		    test_bit(IGB_RING_FLAG_RX_LB_VLAN_BSWAP, &ring->flags))
			vid = be16_to_cpu(rx_desc->wb.upper.vlan);
		else
			vid = le16_to_cpu(rx_desc->wb.upper.vlan);

		__vlan_hwaccel_put_tag(skb, vid);
	}
}

static inline u16 igb_get_hlen(union e1000_adv_rx_desc *rx_desc)
{
	/* HW will not DMA in data larger than the given buffer, even if it
	 * parses the (NFS, of course) header to be larger.  In that case, it
	 * fills the header buffer and spills the rest into the page.
	 */
	u16 hlen = (le16_to_cpu(rx_desc->wb.lower.lo_dword.hdr_info) &
	           E1000_RXDADV_HDRBUFLEN_MASK) >> E1000_RXDADV_HDRBUFLEN_SHIFT;
	if (hlen > IGB_RX_HDR_LEN)
		hlen = IGB_RX_HDR_LEN;
	return hlen;
}

static bool igb_clean_rx_irq(struct igb_q_vector *q_vector, int budget)
{
	struct igb_ring *rx_ring = q_vector->rx.ring;
	union e1000_adv_rx_desc *rx_desc;
	const int current_node = numa_node_id();
	unsigned int total_bytes = 0, total_packets = 0;
	u16 cleaned_count = igb_desc_unused(rx_ring);
	u16 i = rx_ring->next_to_clean;

	rx_desc = IGB_RX_DESC(rx_ring, i);

	while (igb_test_staterr(rx_desc, E1000_RXD_STAT_DD)) {
		struct igb_rx_buffer *buffer_info = &rx_ring->rx_buffer_info[i];
		struct sk_buff *skb = buffer_info->skb;
		union e1000_adv_rx_desc *next_rxd;

		buffer_info->skb = NULL;
		prefetch(skb->data);

		i++;
		if (i == rx_ring->count)
			i = 0;

		next_rxd = IGB_RX_DESC(rx_ring, i);
		prefetch(next_rxd);

		/*
		 * This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * RXD_STAT_DD bit is set
		 */
		rmb();

		if (!skb_is_nonlinear(skb)) {
			__skb_put(skb, igb_get_hlen(rx_desc));
			dma_unmap_single(rx_ring->dev, buffer_info->dma,
					 IGB_RX_HDR_LEN,
					 DMA_FROM_DEVICE);
			buffer_info->dma = 0;
		}

		if (rx_desc->wb.upper.length) {
			u16 length = le16_to_cpu(rx_desc->wb.upper.length);

			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
						buffer_info->page,
						buffer_info->page_offset,
						length);

			skb->len += length;
			skb->data_len += length;
			skb->truesize += PAGE_SIZE / 2;

			if ((page_count(buffer_info->page) != 1) ||
			    (page_to_nid(buffer_info->page) != current_node))
				buffer_info->page = NULL;
			else
				get_page(buffer_info->page);

			dma_unmap_page(rx_ring->dev, buffer_info->page_dma,
				       PAGE_SIZE / 2, DMA_FROM_DEVICE);
			buffer_info->page_dma = 0;
		}

		if (!igb_test_staterr(rx_desc, E1000_RXD_STAT_EOP)) {
			struct igb_rx_buffer *next_buffer;
			next_buffer = &rx_ring->rx_buffer_info[i];
			buffer_info->skb = next_buffer->skb;
			buffer_info->dma = next_buffer->dma;
			next_buffer->skb = skb;
			next_buffer->dma = 0;
			goto next_desc;
		}

		if (unlikely((igb_test_staterr(rx_desc,
					       E1000_RXDEXT_ERR_FRAME_ERR_MASK))
			     && !(rx_ring->netdev->features & NETIF_F_RXALL))) {
			dev_kfree_skb_any(skb);
			goto next_desc;
		}

#ifdef CONFIG_IGB_PTP
		igb_ptp_rx_hwtstamp(q_vector, rx_desc, skb);
#endif /* CONFIG_IGB_PTP */
		igb_rx_hash(rx_ring, rx_desc, skb);
		igb_rx_checksum(rx_ring, rx_desc, skb);
		igb_rx_vlan(rx_ring, rx_desc, skb);

		total_bytes += skb->len;
		total_packets++;

		skb->protocol = eth_type_trans(skb, rx_ring->netdev);

		napi_gro_receive(&q_vector->napi, skb);

		budget--;
next_desc:
		if (!budget)
			break;

		cleaned_count++;
		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IGB_RX_BUFFER_WRITE) {
			igb_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
	}

	rx_ring->next_to_clean = i;
	u64_stats_update_begin(&rx_ring->rx_syncp);
	rx_ring->rx_stats.packets += total_packets;
	rx_ring->rx_stats.bytes += total_bytes;
	u64_stats_update_end(&rx_ring->rx_syncp);
	q_vector->rx.total_packets += total_packets;
	q_vector->rx.total_bytes += total_bytes;

	if (cleaned_count)
		igb_alloc_rx_buffers(rx_ring, cleaned_count);

	return !!budget;
}

static bool igb_alloc_mapped_skb(struct igb_ring *rx_ring,
				 struct igb_rx_buffer *bi)
{
	struct sk_buff *skb = bi->skb;
	dma_addr_t dma = bi->dma;

	if (dma)
		return true;

	if (likely(!skb)) {
		skb = netdev_alloc_skb_ip_align(rx_ring->netdev,
						IGB_RX_HDR_LEN);
		bi->skb = skb;
		if (!skb) {
			rx_ring->rx_stats.alloc_failed++;
			return false;
		}

		/* initialize skb for ring */
		skb_record_rx_queue(skb, rx_ring->queue_index);
	}

	dma = dma_map_single(rx_ring->dev, skb->data,
			     IGB_RX_HDR_LEN, DMA_FROM_DEVICE);

	if (dma_mapping_error(rx_ring->dev, dma)) {
		rx_ring->rx_stats.alloc_failed++;
		return false;
	}

	bi->dma = dma;
	return true;
}

static bool igb_alloc_mapped_page(struct igb_ring *rx_ring,
				  struct igb_rx_buffer *bi)
{
	struct page *page = bi->page;
	dma_addr_t page_dma = bi->page_dma;
	unsigned int page_offset = bi->page_offset ^ (PAGE_SIZE / 2);

	if (page_dma)
		return true;

	if (!page) {
		page = __skb_alloc_page(GFP_ATOMIC, bi->skb);
		bi->page = page;
		if (unlikely(!page)) {
			rx_ring->rx_stats.alloc_failed++;
			return false;
		}
	}

	page_dma = dma_map_page(rx_ring->dev, page,
				page_offset, PAGE_SIZE / 2,
				DMA_FROM_DEVICE);

	if (dma_mapping_error(rx_ring->dev, page_dma)) {
		rx_ring->rx_stats.alloc_failed++;
		return false;
	}

	bi->page_dma = page_dma;
	bi->page_offset = page_offset;
	return true;
}

/**
 * igb_alloc_rx_buffers - Replace used receive buffers; packet split
 * @adapter: address of board private structure
 **/
void igb_alloc_rx_buffers(struct igb_ring *rx_ring, u16 cleaned_count)
{
	union e1000_adv_rx_desc *rx_desc;
	struct igb_rx_buffer *bi;
	u16 i = rx_ring->next_to_use;

	rx_desc = IGB_RX_DESC(rx_ring, i);
	bi = &rx_ring->rx_buffer_info[i];
	i -= rx_ring->count;

	while (cleaned_count--) {
		if (!igb_alloc_mapped_skb(rx_ring, bi))
			break;

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info. */
		rx_desc->read.hdr_addr = cpu_to_le64(bi->dma);

		if (!igb_alloc_mapped_page(rx_ring, bi))
			break;

		rx_desc->read.pkt_addr = cpu_to_le64(bi->page_dma);

		rx_desc++;
		bi++;
		i++;
		if (unlikely(!i)) {
			rx_desc = IGB_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buffer_info;
			i -= rx_ring->count;
		}

		/* clear the hdr_addr for the next_to_use descriptor */
		rx_desc->read.hdr_addr = 0;
	}

	i += rx_ring->count;

	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64). */
		wmb();
		writel(i, rx_ring->tail);
	}
}

/**
 * igb_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/
static int igb_mii_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(ifr);

	if (adapter->hw.phy.media_type != e1000_media_type_copper)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = adapter->hw.phy.addr;
		break;
	case SIOCGMIIREG:
		if (igb_read_phy_reg(&adapter->hw, data->reg_num & 0x1F,
		                     &data->val_out))
			return -EIO;
		break;
	case SIOCSMIIREG:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/**
 * igb_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/
static int igb_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return igb_mii_ioctl(netdev, ifr, cmd);
#ifdef CONFIG_IGB_PTP
	case SIOCSHWTSTAMP:
		return igb_ptp_hwtstamp_ioctl(netdev, ifr, cmd);
#endif /* CONFIG_IGB_PTP */
	default:
		return -EOPNOTSUPP;
	}
}

s32 igb_read_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value)
{
	struct igb_adapter *adapter = hw->back;
	u16 cap_offset;

	cap_offset = adapter->pdev->pcie_cap;
	if (!cap_offset)
		return -E1000_ERR_CONFIG;

	pci_read_config_word(adapter->pdev, cap_offset + reg, value);

	return 0;
}

s32 igb_write_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value)
{
	struct igb_adapter *adapter = hw->back;
	u16 cap_offset;

	cap_offset = adapter->pdev->pcie_cap;
	if (!cap_offset)
		return -E1000_ERR_CONFIG;

	pci_write_config_word(adapter->pdev, cap_offset + reg, *value);

	return 0;
}

static void igb_vlan_mode(struct net_device *netdev, netdev_features_t features)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl, rctl;
	bool enable = !!(features & NETIF_F_HW_VLAN_RX);

	if (enable) {
		/* enable VLAN tag insert/strip */
		ctrl = rd32(E1000_CTRL);
		ctrl |= E1000_CTRL_VME;
		wr32(E1000_CTRL, ctrl);

		/* Disable CFI check */
		rctl = rd32(E1000_RCTL);
		rctl &= ~E1000_RCTL_CFIEN;
		wr32(E1000_RCTL, rctl);
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = rd32(E1000_CTRL);
		ctrl &= ~E1000_CTRL_VME;
		wr32(E1000_CTRL, ctrl);
	}

	igb_rlpml_set(adapter);
}

static int igb_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int pf_id = adapter->vfs_allocated_count;

	/* attempt to add filter to vlvf array */
	igb_vlvf_set(adapter, vid, true, pf_id);

	/* add the filter since PF can receive vlans w/o entry in vlvf */
	igb_vfta_set(hw, vid, true);

	set_bit(vid, adapter->active_vlans);

	return 0;
}

static int igb_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int pf_id = adapter->vfs_allocated_count;
	s32 err;

	/* remove vlan from VLVF table array */
	err = igb_vlvf_set(adapter, vid, false, pf_id);

	/* if vid was not present in VLVF just remove it from table */
	if (err)
		igb_vfta_set(hw, vid, false);

	clear_bit(vid, adapter->active_vlans);

	return 0;
}

static void igb_restore_vlan(struct igb_adapter *adapter)
{
	u16 vid;

	igb_vlan_mode(adapter->netdev, adapter->netdev->features);

	for_each_set_bit(vid, adapter->active_vlans, VLAN_N_VID)
		igb_vlan_rx_add_vid(adapter->netdev, vid);
}

int igb_set_spd_dplx(struct igb_adapter *adapter, u32 spd, u8 dplx)
{
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_mac_info *mac = &adapter->hw.mac;

	mac->autoneg = 0;

	/* Make sure dplx is at most 1 bit and lsb of speed is not set
	 * for the switch() below to work */
	if ((spd & 1) || (dplx & ~1))
		goto err_inval;

	/* Fiber NIC's only allow 1000 Gbps Full duplex */
	if ((adapter->hw.phy.media_type == e1000_media_type_internal_serdes) &&
	    spd != SPEED_1000 &&
	    dplx != DUPLEX_FULL)
		goto err_inval;

	switch (spd + dplx) {
	case SPEED_10 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_10_HALF;
		break;
	case SPEED_10 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_10_FULL;
		break;
	case SPEED_100 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_100_HALF;
		break;
	case SPEED_100 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_100_FULL;
		break;
	case SPEED_1000 + DUPLEX_FULL:
		mac->autoneg = 1;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + DUPLEX_HALF: /* not supported */
	default:
		goto err_inval;
	}

	/* clear MDI, MDI(-X) override is only allowed when autoneg enabled */
	adapter->hw.phy.mdix = AUTO_ALL_MODES;

	return 0;

err_inval:
	dev_err(&pdev->dev, "Unsupported Speed/Duplex configuration\n");
	return -EINVAL;
}

static int __igb_shutdown(struct pci_dev *pdev, bool *enable_wake,
			  bool runtime)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl, rctl, status;
	u32 wufc = runtime ? E1000_WUFC_LNKC : adapter->wol;
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev))
		__igb_close(netdev, true);

	igb_clear_interrupt_scheme(adapter);

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;
#endif

	status = rd32(E1000_STATUS);
	if (status & E1000_STATUS_LU)
		wufc &= ~E1000_WUFC_LNKC;

	if (wufc) {
		igb_setup_rctl(adapter);
		igb_set_rx_mode(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & E1000_WUFC_MC) {
			rctl = rd32(E1000_RCTL);
			rctl |= E1000_RCTL_MPE;
			wr32(E1000_RCTL, rctl);
		}

		ctrl = rd32(E1000_CTRL);
		/* advertise wake from D3Cold */
		#define E1000_CTRL_ADVD3WUC 0x00100000
		/* phy power management enable */
		#define E1000_CTRL_EN_PHY_PWR_MGMT 0x00200000
		ctrl |= E1000_CTRL_ADVD3WUC;
		wr32(E1000_CTRL, ctrl);

		/* Allow time for pending master requests to run */
		igb_disable_pcie_master(hw);

		wr32(E1000_WUC, E1000_WUC_PME_EN);
		wr32(E1000_WUFC, wufc);
	} else {
		wr32(E1000_WUC, 0);
		wr32(E1000_WUFC, 0);
	}

	*enable_wake = wufc || adapter->en_mng_pt;
	if (!*enable_wake)
		igb_power_down_link(adapter);
	else
		igb_power_up_link(adapter);

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant. */
	igb_release_hw_control(adapter);

	pci_disable_device(pdev);

	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_SLEEP
static int igb_suspend(struct device *dev)
{
	int retval;
	bool wake;
	struct pci_dev *pdev = to_pci_dev(dev);

	retval = __igb_shutdown(pdev, &wake, 0);
	if (retval)
		return retval;

	if (wake) {
		pci_prepare_to_sleep(pdev);
	} else {
		pci_wake_from_d3(pdev, false);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int igb_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"igb: Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	if (igb_init_interrupt_scheme(adapter)) {
		dev_err(&pdev->dev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	igb_reset(adapter);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);

	wr32(E1000_WUS, ~0);

	if (netdev->flags & IFF_UP) {
		err = __igb_open(netdev, true);
		if (err)
			return err;
	}

	netif_device_attach(netdev);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int igb_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (!igb_has_link(adapter))
		pm_schedule_suspend(dev, MSEC_PER_SEC * 5);

	return -EBUSY;
}

static int igb_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int retval;
	bool wake;

	retval = __igb_shutdown(pdev, &wake, 1);
	if (retval)
		return retval;

	if (wake) {
		pci_prepare_to_sleep(pdev);
	} else {
		pci_wake_from_d3(pdev, false);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;
}

static int igb_runtime_resume(struct device *dev)
{
	return igb_resume(dev);
}
#endif /* CONFIG_PM_RUNTIME */
#endif

static void igb_shutdown(struct pci_dev *pdev)
{
	bool wake;

	__igb_shutdown(pdev, &wake, 0);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void igb_netpoll(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct igb_q_vector *q_vector;
	int i;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		q_vector = adapter->q_vector[i];
		if (adapter->msix_entries)
			wr32(E1000_EIMC, q_vector->eims_value);
		else
			igb_irq_disable(adapter);
		napi_schedule(&q_vector->napi);
	}
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/**
 * igb_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t igb_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev))
		igb_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * igb_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the igb_resume routine.
 */
static pci_ers_result_t igb_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	pci_ers_result_t result;
	int err;

	if (pci_enable_device_mem(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);

		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);

		igb_reset(adapter);
		wr32(E1000_WUS, ~0);
		result = PCI_ERS_RESULT_RECOVERED;
	}

	err = pci_cleanup_aer_uncorrect_error_status(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_cleanup_aer_uncorrect_error_status "
		        "failed 0x%0x\n", err);
		/* non-fatal, continue */
	}

	return result;
}

/**
 * igb_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the igb_resume routine.
 */
static void igb_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (igb_up(adapter)) {
			dev_err(&pdev->dev, "igb_up failed after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);

	/* let the f/w know that the h/w is now under the control of the
	 * driver. */
	igb_get_hw_control(adapter);
}

static void igb_rar_set_qsel(struct igb_adapter *adapter, u8 *addr, u32 index,
                             u8 qsel)
{
	u32 rar_low, rar_high;
	struct e1000_hw *hw = &adapter->hw;

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32) addr[0] | ((u32) addr[1] << 8) |
	          ((u32) addr[2] << 16) | ((u32) addr[3] << 24));
	rar_high = ((u32) addr[4] | ((u32) addr[5] << 8));

	/* Indicate to hardware the Address is Valid. */
	rar_high |= E1000_RAH_AV;

	if (hw->mac.type == e1000_82575)
		rar_high |= E1000_RAH_POOL_1 * qsel;
	else
		rar_high |= E1000_RAH_POOL_1 << qsel;

	wr32(E1000_RAL(index), rar_low);
	wrfl();
	wr32(E1000_RAH(index), rar_high);
	wrfl();
}

static int igb_set_vf_mac(struct igb_adapter *adapter,
                          int vf, unsigned char *mac_addr)
{
	struct e1000_hw *hw = &adapter->hw;
	/* VF MAC addresses start at end of receive addresses and moves
	 * torwards the first, as a result a collision should not be possible */
	int rar_entry = hw->mac.rar_entry_count - (vf + 1);

	memcpy(adapter->vf_data[vf].vf_mac_addresses, mac_addr, ETH_ALEN);

	igb_rar_set_qsel(adapter, mac_addr, rar_entry, vf);

	return 0;
}

static int igb_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	if (!is_valid_ether_addr(mac) || (vf >= adapter->vfs_allocated_count))
		return -EINVAL;
	adapter->vf_data[vf].flags |= IGB_VF_FLAG_PF_SET_MAC;
	dev_info(&adapter->pdev->dev, "setting MAC %pM on VF %d\n", mac, vf);
	dev_info(&adapter->pdev->dev, "Reload the VF driver to make this"
				      " change effective.");
	if (test_bit(__IGB_DOWN, &adapter->state)) {
		dev_warn(&adapter->pdev->dev, "The VF MAC address has been set,"
			 " but the PF device is not up.\n");
		dev_warn(&adapter->pdev->dev, "Bring the PF device up before"
			 " attempting to use the VF device.\n");
	}
	return igb_set_vf_mac(adapter, vf, mac);
}

static int igb_link_mbps(int internal_link_speed)
{
	switch (internal_link_speed) {
	case SPEED_100:
		return 100;
	case SPEED_1000:
		return 1000;
	default:
		return 0;
	}
}

static void igb_set_vf_rate_limit(struct e1000_hw *hw, int vf, int tx_rate,
				  int link_speed)
{
	int rf_dec, rf_int;
	u32 bcnrc_val;

	if (tx_rate != 0) {
		/* Calculate the rate factor values to set */
		rf_int = link_speed / tx_rate;
		rf_dec = (link_speed - (rf_int * tx_rate));
		rf_dec = (rf_dec * (1<<E1000_RTTBCNRC_RF_INT_SHIFT)) / tx_rate;

		bcnrc_val = E1000_RTTBCNRC_RS_ENA;
		bcnrc_val |= ((rf_int<<E1000_RTTBCNRC_RF_INT_SHIFT) &
		               E1000_RTTBCNRC_RF_INT_MASK);
		bcnrc_val |= (rf_dec & E1000_RTTBCNRC_RF_DEC_MASK);
	} else {
		bcnrc_val = 0;
	}

	wr32(E1000_RTTDQSEL, vf); /* vf X uses queue X */
	/*
	 * Set global transmit compensation time to the MMW_SIZE in RTTBCNRM
	 * register. MMW_SIZE=0x014 if 9728-byte jumbo is supported.
	 */
	wr32(E1000_RTTBCNRM, 0x14);
	wr32(E1000_RTTBCNRC, bcnrc_val);
}

static void igb_check_vf_rate_limit(struct igb_adapter *adapter)
{
	int actual_link_speed, i;
	bool reset_rate = false;

	/* VF TX rate limit was not set or not supported */
	if ((adapter->vf_rate_link_speed == 0) ||
	    (adapter->hw.mac.type != e1000_82576))
		return;

	actual_link_speed = igb_link_mbps(adapter->link_speed);
	if (actual_link_speed != adapter->vf_rate_link_speed) {
		reset_rate = true;
		adapter->vf_rate_link_speed = 0;
		dev_info(&adapter->pdev->dev,
		         "Link speed has been changed. VF Transmit "
		         "rate is disabled\n");
	}

	for (i = 0; i < adapter->vfs_allocated_count; i++) {
		if (reset_rate)
			adapter->vf_data[i].tx_rate = 0;

		igb_set_vf_rate_limit(&adapter->hw, i,
		                      adapter->vf_data[i].tx_rate,
		                      actual_link_speed);
	}
}

static int igb_ndo_set_vf_bw(struct net_device *netdev, int vf, int tx_rate)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int actual_link_speed;

	if (hw->mac.type != e1000_82576)
		return -EOPNOTSUPP;

	actual_link_speed = igb_link_mbps(adapter->link_speed);
	if ((vf >= adapter->vfs_allocated_count) ||
	    (!(rd32(E1000_STATUS) & E1000_STATUS_LU)) ||
	    (tx_rate < 0) || (tx_rate > actual_link_speed))
		return -EINVAL;

	adapter->vf_rate_link_speed = actual_link_speed;
	adapter->vf_data[vf].tx_rate = (u16)tx_rate;
	igb_set_vf_rate_limit(hw, vf, tx_rate, actual_link_speed);

	return 0;
}

static int igb_ndo_get_vf_config(struct net_device *netdev,
				 int vf, struct ifla_vf_info *ivi)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	if (vf >= adapter->vfs_allocated_count)
		return -EINVAL;
	ivi->vf = vf;
	memcpy(&ivi->mac, adapter->vf_data[vf].vf_mac_addresses, ETH_ALEN);
	ivi->tx_rate = adapter->vf_data[vf].tx_rate;
	ivi->vlan = adapter->vf_data[vf].pf_vlan;
	ivi->qos = adapter->vf_data[vf].pf_qos;
	return 0;
}

static void igb_vmm_control(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 reg;

	switch (hw->mac.type) {
	case e1000_82575:
	case e1000_i210:
	case e1000_i211:
	default:
		/* replication is not supported for 82575 */
		return;
	case e1000_82576:
		/* notify HW that the MAC is adding vlan tags */
		reg = rd32(E1000_DTXCTL);
		reg |= E1000_DTXCTL_VLAN_ADDED;
		wr32(E1000_DTXCTL, reg);
	case e1000_82580:
		/* enable replication vlan tag stripping */
		reg = rd32(E1000_RPLOLR);
		reg |= E1000_RPLOLR_STRVLAN;
		wr32(E1000_RPLOLR, reg);
	case e1000_i350:
		/* none of the above registers are supported by i350 */
		break;
	}

	if (adapter->vfs_allocated_count) {
		igb_vmdq_set_loopback_pf(hw, true);
		igb_vmdq_set_replication_pf(hw, true);
		igb_vmdq_set_anti_spoofing_pf(hw, true,
						adapter->vfs_allocated_count);
	} else {
		igb_vmdq_set_loopback_pf(hw, false);
		igb_vmdq_set_replication_pf(hw, false);
	}
}

static void igb_init_dmac(struct igb_adapter *adapter, u32 pba)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 dmac_thr;
	u16 hwm;

	if (hw->mac.type > e1000_82580) {
		if (adapter->flags & IGB_FLAG_DMAC) {
			u32 reg;

			/* force threshold to 0. */
			wr32(E1000_DMCTXTH, 0);

			/*
			 * DMA Coalescing high water mark needs to be greater
			 * than the Rx threshold. Set hwm to PBA - max frame
			 * size in 16B units, capping it at PBA - 6KB.
			 */
			hwm = 64 * pba - adapter->max_frame_size / 16;
			if (hwm < 64 * (pba - 6))
				hwm = 64 * (pba - 6);
			reg = rd32(E1000_FCRTC);
			reg &= ~E1000_FCRTC_RTH_COAL_MASK;
			reg |= ((hwm << E1000_FCRTC_RTH_COAL_SHIFT)
				& E1000_FCRTC_RTH_COAL_MASK);
			wr32(E1000_FCRTC, reg);

			/*
			 * Set the DMA Coalescing Rx threshold to PBA - 2 * max
			 * frame size, capping it at PBA - 10KB.
			 */
			dmac_thr = pba - adapter->max_frame_size / 512;
			if (dmac_thr < pba - 10)
				dmac_thr = pba - 10;
			reg = rd32(E1000_DMACR);
			reg &= ~E1000_DMACR_DMACTHR_MASK;
			reg |= ((dmac_thr << E1000_DMACR_DMACTHR_SHIFT)
				& E1000_DMACR_DMACTHR_MASK);

			/* transition to L0x or L1 if available..*/
			reg |= (E1000_DMACR_DMAC_EN | E1000_DMACR_DMAC_LX_MASK);

			/* watchdog timer= +-1000 usec in 32usec intervals */
			reg |= (1000 >> 5);

			/* Disable BMC-to-OS Watchdog Enable */
			reg &= ~E1000_DMACR_DC_BMC2OSW_EN;
			wr32(E1000_DMACR, reg);

			/*
			 * no lower threshold to disable
			 * coalescing(smart fifb)-UTRESH=0
			 */
			wr32(E1000_DMCRTRH, 0);

			reg = (IGB_DMCTLX_DCFLUSH_DIS | 0x4);

			wr32(E1000_DMCTLX, reg);

			/*
			 * free space in tx packet buffer to wake from
			 * DMA coal
			 */
			wr32(E1000_DMCTXTH, (IGB_MIN_TXPBSIZE -
			     (IGB_TX_BUF_4096 + adapter->max_frame_size)) >> 6);

			/*
			 * make low power state decision controlled
			 * by DMA coal
			 */
			reg = rd32(E1000_PCIEMISC);
			reg &= ~E1000_PCIEMISC_LX_DECISION;
			wr32(E1000_PCIEMISC, reg);
		} /* endif adapter->dmac is not disabled */
	} else if (hw->mac.type == e1000_82580) {
		u32 reg = rd32(E1000_PCIEMISC);
		wr32(E1000_PCIEMISC, reg & ~E1000_PCIEMISC_LX_DECISION);
		wr32(E1000_DMACR, 0);
	}
}

/* igb_main.c */

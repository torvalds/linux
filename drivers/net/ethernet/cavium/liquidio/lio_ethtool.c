/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/pci.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"
#include "cn66xx_regs.h"
#include "cn66xx_device.h"
#include "cn23xx_pf_device.h"
#include "cn23xx_vf_device.h"

static int lio_reset_queues(struct net_device *netdev, uint32_t num_qs);

struct oct_intrmod_resp {
	u64     rh;
	struct oct_intrmod_cfg intrmod;
	u64     status;
};

struct oct_mdio_cmd_resp {
	u64 rh;
	struct oct_mdio_cmd resp;
	u64 status;
};

#define OCT_MDIO45_RESP_SIZE   (sizeof(struct oct_mdio_cmd_resp))

/* Octeon's interface mode of operation */
enum {
	INTERFACE_MODE_DISABLED,
	INTERFACE_MODE_RGMII,
	INTERFACE_MODE_GMII,
	INTERFACE_MODE_SPI,
	INTERFACE_MODE_PCIE,
	INTERFACE_MODE_XAUI,
	INTERFACE_MODE_SGMII,
	INTERFACE_MODE_PICMG,
	INTERFACE_MODE_NPI,
	INTERFACE_MODE_LOOP,
	INTERFACE_MODE_SRIO,
	INTERFACE_MODE_ILK,
	INTERFACE_MODE_RXAUI,
	INTERFACE_MODE_QSGMII,
	INTERFACE_MODE_AGL,
	INTERFACE_MODE_XLAUI,
	INTERFACE_MODE_XFI,
	INTERFACE_MODE_10G_KR,
	INTERFACE_MODE_40G_KR4,
	INTERFACE_MODE_MIXED,
};

#define OCT_ETHTOOL_REGDUMP_LEN  4096
#define OCT_ETHTOOL_REGDUMP_LEN_23XX  (4096 * 11)
#define OCT_ETHTOOL_REGDUMP_LEN_23XX_VF  (4096 * 2)
#define OCT_ETHTOOL_REGSVER  1

/* statistics of PF */
static const char oct_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",
	"tx_errors",
	"rx_dropped",
	"tx_dropped",

	"tx_total_sent",
	"tx_total_fwd",
	"tx_err_pko",
	"tx_err_pki",
	"tx_err_link",
	"tx_err_drop",

	"tx_tso",
	"tx_tso_packets",
	"tx_tso_err",
	"tx_vxlan",

	"tx_mcast",
	"tx_bcast",

	"mac_tx_total_pkts",
	"mac_tx_total_bytes",
	"mac_tx_mcast_pkts",
	"mac_tx_bcast_pkts",
	"mac_tx_ctl_packets",
	"mac_tx_total_collisions",
	"mac_tx_one_collision",
	"mac_tx_multi_collision",
	"mac_tx_max_collision_fail",
	"mac_tx_max_deferral_fail",
	"mac_tx_fifo_err",
	"mac_tx_runts",

	"rx_total_rcvd",
	"rx_total_fwd",
	"rx_mcast",
	"rx_bcast",
	"rx_jabber_err",
	"rx_l2_err",
	"rx_frame_err",
	"rx_err_pko",
	"rx_err_link",
	"rx_err_drop",

	"rx_vxlan",
	"rx_vxlan_err",

	"rx_lro_pkts",
	"rx_lro_bytes",
	"rx_total_lro",

	"rx_lro_aborts",
	"rx_lro_aborts_port",
	"rx_lro_aborts_seq",
	"rx_lro_aborts_tsval",
	"rx_lro_aborts_timer",
	"rx_fwd_rate",

	"mac_rx_total_rcvd",
	"mac_rx_bytes",
	"mac_rx_total_bcst",
	"mac_rx_total_mcst",
	"mac_rx_runts",
	"mac_rx_ctl_packets",
	"mac_rx_fifo_err",
	"mac_rx_dma_drop",
	"mac_rx_fcs_err",

	"link_state_changes",
};

/* statistics of VF */
static const char oct_vf_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",
	"tx_errors",
	"rx_dropped",
	"tx_dropped",
	"rx_mcast",
	"tx_mcast",
	"rx_bcast",
	"tx_bcast",
	"link_state_changes",
};

/* statistics of host tx queue */
static const char oct_iq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",
	"bytes",
	"dropped",
	"iq_busy",
	"sgentry_sent",

	"fw_instr_posted",
	"fw_instr_processed",
	"fw_instr_dropped",
	"fw_bytes_sent",

	"tso",
	"vxlan",
	"txq_restart",
};

/* statistics of host rx queue */
static const char oct_droq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",
	"bytes",
	"dropped",
	"dropped_nomem",
	"dropped_toomany",
	"fw_dropped",
	"fw_pkts_received",
	"fw_bytes_received",
	"fw_dropped_nodispatch",

	"vxlan",
	"buffer_alloc_failure",
};

/* LiquidIO driver private flags */
static const char oct_priv_flags_strings[][ETH_GSTRING_LEN] = {
};

#define OCTNIC_NCMD_AUTONEG_ON  0x1
#define OCTNIC_NCMD_PHY_ON      0x2

static int lio_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *ecmd)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_link_info *linfo;

	linfo = &lio->linfo;

	ethtool_link_ksettings_zero_link_mode(ecmd, supported);
	ethtool_link_ksettings_zero_link_mode(ecmd, advertising);

	switch (linfo->link.s.phy_type) {
	case LIO_PHY_PORT_TP:
		ecmd->base.port = PORT_TP;
		ecmd->base.autoneg = AUTONEG_DISABLE;
		ethtool_link_ksettings_add_link_mode(ecmd, supported, TP);
		ethtool_link_ksettings_add_link_mode(ecmd, supported, Pause);
		ethtool_link_ksettings_add_link_mode(ecmd, supported,
						     10000baseT_Full);

		ethtool_link_ksettings_add_link_mode(ecmd, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising,
						     10000baseT_Full);

		break;

	case LIO_PHY_PORT_FIBRE:
		if (linfo->link.s.if_mode == INTERFACE_MODE_XAUI ||
		    linfo->link.s.if_mode == INTERFACE_MODE_RXAUI ||
		    linfo->link.s.if_mode == INTERFACE_MODE_XLAUI ||
		    linfo->link.s.if_mode == INTERFACE_MODE_XFI) {
			dev_dbg(&oct->pci_dev->dev, "ecmd->base.transceiver is XCVR_EXTERNAL\n");
			ecmd->base.transceiver = XCVR_EXTERNAL;
		} else {
			dev_err(&oct->pci_dev->dev, "Unknown link interface mode: %d\n",
				linfo->link.s.if_mode);
		}

		ecmd->base.port = PORT_FIBRE;
		ecmd->base.autoneg = AUTONEG_DISABLE;
		ethtool_link_ksettings_add_link_mode(ecmd, supported, FIBRE);

		ethtool_link_ksettings_add_link_mode(ecmd, supported, Pause);
		ethtool_link_ksettings_add_link_mode(ecmd, advertising, Pause);
		if (oct->subsystem_id == OCTEON_CN2350_25GB_SUBSYS_ID ||
		    oct->subsystem_id == OCTEON_CN2360_25GB_SUBSYS_ID) {
			if (OCTEON_CN23XX_PF(oct)) {
				ethtool_link_ksettings_add_link_mode
					(ecmd, supported, 25000baseSR_Full);
				ethtool_link_ksettings_add_link_mode
					(ecmd, supported, 25000baseKR_Full);
				ethtool_link_ksettings_add_link_mode
					(ecmd, supported, 25000baseCR_Full);

				if (oct->no_speed_setting == 0)  {
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseCR_Full);
				}

				if (oct->no_speed_setting == 0) {
					liquidio_get_speed(lio);
					liquidio_get_fec(lio);
				} else {
					oct->speed_setting = 25;
				}

				if (oct->speed_setting == 10) {
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseCR_Full);
				}
				if (oct->speed_setting == 25) {
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseCR_Full);
				}

				if (oct->no_speed_setting)
					break;

				ethtool_link_ksettings_add_link_mode
					(ecmd, supported, FEC_RS);
				ethtool_link_ksettings_add_link_mode
					(ecmd, supported, FEC_NONE);
					/*FEC_OFF*/
				if (oct->props[lio->ifidx].fec == 1) {
					/* ETHTOOL_FEC_RS */
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising, FEC_RS);
				} else {
					/* ETHTOOL_FEC_OFF */
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising, FEC_NONE);
				}
			} else { /* VF */
				if (linfo->link.s.speed == 10000) {
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 10000baseCR_Full);

					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 10000baseCR_Full);
				}

				if (linfo->link.s.speed == 25000) {
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 25000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 25000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, supported,
						 25000baseCR_Full);

					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseSR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseKR_Full);
					ethtool_link_ksettings_add_link_mode
						(ecmd, advertising,
						 25000baseCR_Full);
				}
			}
		} else {
			ethtool_link_ksettings_add_link_mode(ecmd, supported,
							     10000baseT_Full);
			ethtool_link_ksettings_add_link_mode(ecmd, advertising,
							     10000baseT_Full);
		}
		break;
	}

	if (linfo->link.s.link_up) {
		ecmd->base.speed = linfo->link.s.speed;
		ecmd->base.duplex = linfo->link.s.duplex;
	} else {
		ecmd->base.speed = SPEED_UNKNOWN;
		ecmd->base.duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static int lio_set_link_ksettings(struct net_device *netdev,
				  const struct ethtool_link_ksettings *ecmd)
{
	const int speed = ecmd->base.speed;
	struct lio *lio = GET_LIO(netdev);
	struct oct_link_info *linfo;
	struct octeon_device *oct;

	oct = lio->oct_dev;

	linfo = &lio->linfo;

	if (!(oct->subsystem_id == OCTEON_CN2350_25GB_SUBSYS_ID ||
	      oct->subsystem_id == OCTEON_CN2360_25GB_SUBSYS_ID))
		return -EOPNOTSUPP;

	if (oct->no_speed_setting) {
		dev_err(&oct->pci_dev->dev, "%s: Changing speed is not supported\n",
			__func__);
		return -EOPNOTSUPP;
	}

	if ((ecmd->base.duplex != DUPLEX_UNKNOWN &&
	     ecmd->base.duplex != linfo->link.s.duplex) ||
	     ecmd->base.autoneg != AUTONEG_DISABLE ||
	    (ecmd->base.speed != 10000 && ecmd->base.speed != 25000 &&
	     ecmd->base.speed != SPEED_UNKNOWN))
		return -EOPNOTSUPP;

	if ((oct->speed_boot == speed / 1000) &&
	    oct->speed_boot == oct->speed_setting)
		return 0;

	liquidio_set_speed(lio, speed / 1000);

	dev_dbg(&oct->pci_dev->dev, "Port speed is set to %dG\n",
		oct->speed_setting);

	return 0;
}

static void
lio_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct lio *lio;
	struct octeon_device *oct;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strcpy(drvinfo->driver, "liquidio");
	strncpy(drvinfo->fw_version, oct->fw_info.liquidio_firmware_version,
		ETHTOOL_FWVERS_LEN);
	strncpy(drvinfo->bus_info, pci_name(oct->pci_dev), 32);
}

static void
lio_get_vf_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct octeon_device *oct;
	struct lio *lio;

	lio = GET_LIO(netdev);
	oct = lio->oct_dev;

	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strcpy(drvinfo->driver, "liquidio_vf");
	strncpy(drvinfo->fw_version, oct->fw_info.liquidio_firmware_version,
		ETHTOOL_FWVERS_LEN);
	strncpy(drvinfo->bus_info, pci_name(oct->pci_dev), 32);
}

static int
lio_send_queue_count_update(struct net_device *netdev, uint32_t num_queues)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_QUEUE_COUNT_CTL;
	nctrl.ncmd.s.param1 = num_queues;
	nctrl.ncmd.s.param2 = num_queues;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Failed to send Queue reset command (ret: 0x%x)\n",
			ret);
		return -1;
	}

	return 0;
}

static void
lio_ethtool_get_channels(struct net_device *dev,
			 struct ethtool_channels *channel)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;
	u32 max_rx = 0, max_tx = 0, tx_count = 0, rx_count = 0;
	u32 combined_count = 0, max_combined = 0;

	if (OCTEON_CN6XXX(oct)) {
		struct octeon_config *conf6x = CHIP_CONF(oct, cn6xxx);

		max_rx = CFG_GET_OQ_MAX_Q(conf6x);
		max_tx = CFG_GET_IQ_MAX_Q(conf6x);
		rx_count = CFG_GET_NUM_RXQS_NIC_IF(conf6x, lio->ifidx);
		tx_count = CFG_GET_NUM_TXQS_NIC_IF(conf6x, lio->ifidx);
	} else if (OCTEON_CN23XX_PF(oct)) {
		if (oct->sriov_info.sriov_enabled) {
			max_combined = lio->linfo.num_txpciq;
		} else {
			struct octeon_config *conf23_pf =
				CHIP_CONF(oct, cn23xx_pf);

			max_combined = CFG_GET_IQ_MAX_Q(conf23_pf);
		}
		combined_count = oct->num_iqs;
	} else if (OCTEON_CN23XX_VF(oct)) {
		u64 reg_val = 0ULL;
		u64 ctrl = CN23XX_VF_SLI_IQ_PKT_CONTROL64(0);

		reg_val = octeon_read_csr64(oct, ctrl);
		reg_val = reg_val >> CN23XX_PKT_INPUT_CTL_RPVF_POS;
		max_combined = reg_val & CN23XX_PKT_INPUT_CTL_RPVF_MASK;
		combined_count = oct->num_iqs;
	}

	channel->max_rx = max_rx;
	channel->max_tx = max_tx;
	channel->max_combined = max_combined;
	channel->rx_count = rx_count;
	channel->tx_count = tx_count;
	channel->combined_count = combined_count;
}

static int
lio_irq_reallocate_irqs(struct octeon_device *oct, uint32_t num_ioqs)
{
	struct msix_entry *msix_entries;
	int num_msix_irqs = 0;
	int i;

	if (!oct->msix_on)
		return 0;

	/* Disable the input and output queues now. No more packets will
	 * arrive from Octeon.
	 */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	if (oct->msix_on) {
		if (OCTEON_CN23XX_PF(oct))
			num_msix_irqs = oct->num_msix_irqs - 1;
		else if (OCTEON_CN23XX_VF(oct))
			num_msix_irqs = oct->num_msix_irqs;

		msix_entries = (struct msix_entry *)oct->msix_entries;
		for (i = 0; i < num_msix_irqs; i++) {
			if (oct->ioq_vector[i].vector) {
				/* clear the affinity_cpumask */
				irq_set_affinity_hint(msix_entries[i].vector,
						      NULL);
				free_irq(msix_entries[i].vector,
					 &oct->ioq_vector[i]);
				oct->ioq_vector[i].vector = 0;
			}
		}

		/* non-iov vector's argument is oct struct */
		if (OCTEON_CN23XX_PF(oct))
			free_irq(msix_entries[i].vector, oct);

		pci_disable_msix(oct->pci_dev);
		kfree(oct->msix_entries);
		oct->msix_entries = NULL;
	}

	kfree(oct->irq_name_storage);
	oct->irq_name_storage = NULL;

	if (octeon_allocate_ioq_vector(oct, num_ioqs)) {
		dev_err(&oct->pci_dev->dev, "OCTEON: ioq vector allocation failed\n");
		return -1;
	}

	if (octeon_setup_interrupt(oct, num_ioqs)) {
		dev_info(&oct->pci_dev->dev, "Setup interrupt failed\n");
		return -1;
	}

	/* Enable Octeon device interrupts */
	oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);

	return 0;
}

static int
lio_ethtool_set_channels(struct net_device *dev,
			 struct ethtool_channels *channel)
{
	u32 combined_count, max_combined;
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;
	int stopped = 0;

	if (strcmp(oct->fw_info.liquidio_firmware_version, "1.6.1") < 0) {
		dev_err(&oct->pci_dev->dev, "Minimum firmware version required is 1.6.1\n");
		return -EINVAL;
	}

	if (!channel->combined_count || channel->other_count ||
	    channel->rx_count || channel->tx_count)
		return -EINVAL;

	combined_count = channel->combined_count;

	if (OCTEON_CN23XX_PF(oct)) {
		if (oct->sriov_info.sriov_enabled) {
			max_combined = lio->linfo.num_txpciq;
		} else {
			struct octeon_config *conf23_pf =
				CHIP_CONF(oct,
					  cn23xx_pf);

			max_combined =
				CFG_GET_IQ_MAX_Q(conf23_pf);
		}
	} else if (OCTEON_CN23XX_VF(oct)) {
		u64 reg_val = 0ULL;
		u64 ctrl = CN23XX_VF_SLI_IQ_PKT_CONTROL64(0);

		reg_val = octeon_read_csr64(oct, ctrl);
		reg_val = reg_val >> CN23XX_PKT_INPUT_CTL_RPVF_POS;
		max_combined = reg_val & CN23XX_PKT_INPUT_CTL_RPVF_MASK;
	} else {
		return -EINVAL;
	}

	if (combined_count > max_combined || combined_count < 1)
		return -EINVAL;

	if (combined_count == oct->num_iqs)
		return 0;

	ifstate_set(lio, LIO_IFSTATE_RESETTING);

	if (netif_running(dev)) {
		dev->netdev_ops->ndo_stop(dev);
		stopped = 1;
	}

	if (lio_reset_queues(dev, combined_count))
		return -EINVAL;

	if (stopped)
		dev->netdev_ops->ndo_open(dev);

	ifstate_reset(lio, LIO_IFSTATE_RESETTING);

	return 0;
}

static int lio_get_eeprom_len(struct net_device *netdev)
{
	u8 buf[192];
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_board_info *board_info;
	int len;

	board_info = (struct octeon_board_info *)(&oct_dev->boardinfo);
	len = sprintf(buf, "boardname:%s serialnum:%s maj:%lld min:%lld\n",
		      board_info->name, board_info->serial_number,
		      board_info->major, board_info->minor);

	return len;
}

static int
lio_get_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
	       u8 *bytes)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_board_info *board_info;

	if (eeprom->offset)
		return -EINVAL;

	eeprom->magic = oct_dev->pci_dev->vendor;
	board_info = (struct octeon_board_info *)(&oct_dev->boardinfo);
	sprintf((char *)bytes,
		"boardname:%s serialnum:%s maj:%lld min:%lld\n",
		board_info->name, board_info->serial_number,
		board_info->major, board_info->minor);

	return 0;
}

static int octnet_gpio_access(struct net_device *netdev, int addr, int val)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_GPIO_ACCESS;
	nctrl.ncmd.s.param1 = addr;
	nctrl.ncmd.s.param2 = val;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"Failed to configure gpio value, ret=%d\n", ret);
		return -EINVAL;
	}

	return 0;
}

static int octnet_id_active(struct net_device *netdev, int val)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_ID_ACTIVE;
	nctrl.ncmd.s.param1 = val;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"Failed to configure gpio value, ret=%d\n", ret);
		return -EINVAL;
	}

	return 0;
}

/* This routine provides PHY access routines for
 * mdio  clause45 .
 */
static int
octnet_mdio45_access(struct lio *lio, int op, int loc, int *value)
{
	struct octeon_device *oct_dev = lio->oct_dev;
	struct octeon_soft_command *sc;
	struct oct_mdio_cmd_resp *mdio_cmd_rsp;
	struct oct_mdio_cmd *mdio_cmd;
	int retval = 0;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  sizeof(struct oct_mdio_cmd),
					  sizeof(struct oct_mdio_cmd_resp), 0);

	if (!sc)
		return -ENOMEM;

	mdio_cmd_rsp = (struct oct_mdio_cmd_resp *)sc->virtrptr;
	mdio_cmd = (struct oct_mdio_cmd *)sc->virtdptr;

	mdio_cmd->op = op;
	mdio_cmd->mdio_addr = loc;
	if (op)
		mdio_cmd->value1 = *value;
	octeon_swap_8B_data((u64 *)mdio_cmd, sizeof(struct oct_mdio_cmd) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC, OPCODE_NIC_MDIO45,
				    0, 0, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		dev_err(&oct_dev->pci_dev->dev,
			"octnet_mdio45_access instruction failed status: %x\n",
			retval);
		octeon_free_soft_command(oct_dev, sc);
		return -EBUSY;
	} else {
		/* Sleep on a wait queue till the cond flag indicates that the
		 * response arrived
		 */
		retval = wait_for_sc_completion_timeout(oct_dev, sc, 0);
		if (retval)
			return retval;

		retval = mdio_cmd_rsp->status;
		if (retval) {
			dev_err(&oct_dev->pci_dev->dev,
				"octnet mdio45 access failed: %x\n", retval);
			WRITE_ONCE(sc->caller_is_done, true);
			return -EBUSY;
		}

		octeon_swap_8B_data((u64 *)(&mdio_cmd_rsp->resp),
				    sizeof(struct oct_mdio_cmd) / 8);

		if (!op)
			*value = mdio_cmd_rsp->resp.value1;

		WRITE_ONCE(sc->caller_is_done, true);
	}

	return retval;
}

static int lio_set_phys_id(struct net_device *netdev,
			   enum ethtool_phys_id_state state)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct oct_link_info *linfo;
	int value, ret;
	u32 cur_ver;

	linfo = &lio->linfo;
	cur_ver = OCT_FW_VER(oct->fw_info.ver.maj,
			     oct->fw_info.ver.min,
			     oct->fw_info.ver.rev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		if (oct->chip_id == OCTEON_CN66XX) {
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_DRIVEON);
			return 2;

		} else if (oct->chip_id == OCTEON_CN68XX) {
			/* Save the current LED settings */
			ret = octnet_mdio45_access(lio, 0,
						   LIO68XX_LED_BEACON_ADDR,
						   &lio->phy_beacon_val);
			if (ret)
				return ret;

			ret = octnet_mdio45_access(lio, 0,
						   LIO68XX_LED_CTRL_ADDR,
						   &lio->led_ctrl_val);
			if (ret)
				return ret;

			/* Configure Beacon values */
			value = LIO68XX_LED_BEACON_CFGON;
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_BEACON_ADDR,
						   &value);
			if (ret)
				return ret;

			value = LIO68XX_LED_CTRL_CFGON;
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_CTRL_ADDR,
						   &value);
			if (ret)
				return ret;
		} else if (oct->chip_id == OCTEON_CN23XX_PF_VID) {
			octnet_id_active(netdev, LED_IDENTIFICATION_ON);
			if (linfo->link.s.phy_type == LIO_PHY_PORT_TP &&
			    cur_ver > OCT_FW_VER(1, 7, 2))
				return 2;
			else
				return 0;
		} else {
			return -EINVAL;
		}
		break;

	case ETHTOOL_ID_ON:
		if (oct->chip_id == OCTEON_CN23XX_PF_VID &&
		    linfo->link.s.phy_type == LIO_PHY_PORT_TP &&
		    cur_ver > OCT_FW_VER(1, 7, 2))
			octnet_id_active(netdev, LED_IDENTIFICATION_ON);
		else if (oct->chip_id == OCTEON_CN66XX)
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_HIGH);
		else
			return -EINVAL;

		break;

	case ETHTOOL_ID_OFF:
		if (oct->chip_id == OCTEON_CN23XX_PF_VID &&
		    linfo->link.s.phy_type == LIO_PHY_PORT_TP &&
		    cur_ver > OCT_FW_VER(1, 7, 2))
			octnet_id_active(netdev, LED_IDENTIFICATION_OFF);
		else if (oct->chip_id == OCTEON_CN66XX)
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_LOW);
		else
			return -EINVAL;

		break;

	case ETHTOOL_ID_INACTIVE:
		if (oct->chip_id == OCTEON_CN66XX) {
			octnet_gpio_access(netdev, VITESSE_PHY_GPIO_CFG,
					   VITESSE_PHY_GPIO_DRIVEOFF);
		} else if (oct->chip_id == OCTEON_CN68XX) {
			/* Restore LED settings */
			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_CTRL_ADDR,
						   &lio->led_ctrl_val);
			if (ret)
				return ret;

			ret = octnet_mdio45_access(lio, 1,
						   LIO68XX_LED_BEACON_ADDR,
						   &lio->phy_beacon_val);
			if (ret)
				return ret;
		} else if (oct->chip_id == OCTEON_CN23XX_PF_VID) {
			octnet_id_active(netdev, LED_IDENTIFICATION_OFF);

			return 0;
		} else {
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void
lio_ethtool_get_ringparam(struct net_device *netdev,
			  struct ethtool_ringparam *ering)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	u32 tx_max_pending = 0, rx_max_pending = 0, tx_pending = 0,
	    rx_pending = 0;

	if (ifstate_check(lio, LIO_IFSTATE_RESETTING))
		return;

	if (OCTEON_CN6XXX(oct)) {
		struct octeon_config *conf6x = CHIP_CONF(oct, cn6xxx);

		tx_max_pending = CN6XXX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = CN6XXX_MAX_OQ_DESCRIPTORS;
		rx_pending = CFG_GET_NUM_RX_DESCS_NIC_IF(conf6x, lio->ifidx);
		tx_pending = CFG_GET_NUM_TX_DESCS_NIC_IF(conf6x, lio->ifidx);
	} else if (OCTEON_CN23XX_PF(oct) || OCTEON_CN23XX_VF(oct)) {
		tx_max_pending = CN23XX_MAX_IQ_DESCRIPTORS;
		rx_max_pending = CN23XX_MAX_OQ_DESCRIPTORS;
		rx_pending = oct->droq[0]->max_count;
		tx_pending = oct->instr_queue[0]->max_count;
	}

	ering->tx_pending = tx_pending;
	ering->tx_max_pending = tx_max_pending;
	ering->rx_pending = rx_pending;
	ering->rx_max_pending = rx_max_pending;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
}

static int lio_23xx_reconfigure_queue_count(struct lio *lio)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 resp_size, data_size;
	struct liquidio_if_cfg_resp *resp;
	struct octeon_soft_command *sc;
	union oct_nic_if_cfg if_cfg;
	struct lio_version *vdata;
	u32 ifidx_or_pfnum;
	int retval;
	int j;

	resp_size = sizeof(struct liquidio_if_cfg_resp);
	data_size = sizeof(struct lio_version);
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, data_size,
					  resp_size, 0);
	if (!sc) {
		dev_err(&oct->pci_dev->dev, "%s: Failed to allocate soft command\n",
			__func__);
		return -1;
	}

	resp = (struct liquidio_if_cfg_resp *)sc->virtrptr;
	vdata = (struct lio_version *)sc->virtdptr;

	vdata->major = (__force u16)cpu_to_be16(LIQUIDIO_BASE_MAJOR_VERSION);
	vdata->minor = (__force u16)cpu_to_be16(LIQUIDIO_BASE_MINOR_VERSION);
	vdata->micro = (__force u16)cpu_to_be16(LIQUIDIO_BASE_MICRO_VERSION);

	ifidx_or_pfnum = oct->pf_num;

	if_cfg.u64 = 0;
	if_cfg.s.num_iqueues = oct->sriov_info.num_pf_rings;
	if_cfg.s.num_oqueues = oct->sriov_info.num_pf_rings;
	if_cfg.s.base_queue = oct->sriov_info.pf_srn;
	if_cfg.s.gmx_port_id = oct->pf_num;

	sc->iq_no = 0;
	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_QCOUNT_UPDATE, 0,
				    if_cfg.u64, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct, sc);
	if (retval == IQ_SEND_FAILED) {
		dev_err(&oct->pci_dev->dev,
			"Sending iq/oq config failed status: %x\n",
			retval);
		octeon_free_soft_command(oct, sc);
		return -EIO;
	}

	retval = wait_for_sc_completion_timeout(oct, sc, 0);
	if (retval)
		return retval;

	retval = resp->status;
	if (retval) {
		dev_err(&oct->pci_dev->dev,
			"iq/oq config failed: %x\n", retval);
		WRITE_ONCE(sc->caller_is_done, true);
		return -1;
	}

	octeon_swap_8B_data((u64 *)(&resp->cfg_info),
			    (sizeof(struct liquidio_if_cfg_info)) >> 3);

	lio->ifidx = ifidx_or_pfnum;
	lio->linfo.num_rxpciq = hweight64(resp->cfg_info.iqmask);
	lio->linfo.num_txpciq = hweight64(resp->cfg_info.iqmask);
	for (j = 0; j < lio->linfo.num_rxpciq; j++) {
		lio->linfo.rxpciq[j].u64 =
			resp->cfg_info.linfo.rxpciq[j].u64;
	}

	for (j = 0; j < lio->linfo.num_txpciq; j++) {
		lio->linfo.txpciq[j].u64 =
			resp->cfg_info.linfo.txpciq[j].u64;
	}

	lio->linfo.hw_addr = resp->cfg_info.linfo.hw_addr;
	lio->linfo.gmxport = resp->cfg_info.linfo.gmxport;
	lio->linfo.link.u64 = resp->cfg_info.linfo.link.u64;
	lio->txq = lio->linfo.txpciq[0].s.q_no;
	lio->rxq = lio->linfo.rxpciq[0].s.q_no;

	dev_info(&oct->pci_dev->dev, "Queue count updated to %d\n",
		 lio->linfo.num_rxpciq);

	WRITE_ONCE(sc->caller_is_done, true);

	return 0;
}

static int lio_reset_queues(struct net_device *netdev, uint32_t num_qs)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	int i, queue_count_update = 0;
	struct napi_struct *napi, *n;
	int ret;

	schedule_timeout_uninterruptible(msecs_to_jiffies(100));

	if (wait_for_pending_requests(oct))
		dev_err(&oct->pci_dev->dev, "There were pending requests\n");

	if (lio_wait_for_instr_fetch(oct))
		dev_err(&oct->pci_dev->dev, "IQ had pending instructions\n");

	if (octeon_set_io_queues_off(oct)) {
		dev_err(&oct->pci_dev->dev, "Setting io queues off failed\n");
		return -1;
	}

	/* Disable the input and output queues now. No more packets will
	 * arrive from Octeon.
	 */
	oct->fn_list.disable_io_queues(oct);
	/* Delete NAPI */
	list_for_each_entry_safe(napi, n, &netdev->napi_list, dev_list)
		netif_napi_del(napi);

	if (num_qs != oct->num_iqs) {
		ret = netif_set_real_num_rx_queues(netdev, num_qs);
		if (ret) {
			dev_err(&oct->pci_dev->dev,
				"Setting real number rx failed\n");
			return ret;
		}

		ret = netif_set_real_num_tx_queues(netdev, num_qs);
		if (ret) {
			dev_err(&oct->pci_dev->dev,
				"Setting real number tx failed\n");
			return ret;
		}

		/* The value of queue_count_update decides whether it is the
		 * queue count or the descriptor count that is being
		 * re-configured.
		 */
		queue_count_update = 1;
	}

	/* Re-configuration of queues can happen in two scenarios, SRIOV enabled
	 * and SRIOV disabled. Few things like recreating queue zero, resetting
	 * glists and IRQs are required for both. For the latter, some more
	 * steps like updating sriov_info for the octeon device need to be done.
	 */
	if (queue_count_update) {
		cleanup_rx_oom_poll_fn(netdev);

		lio_delete_glists(lio);

		/* Delete mbox for PF which is SRIOV disabled because sriov_info
		 * will be now changed.
		 */
		if ((OCTEON_CN23XX_PF(oct)) && !oct->sriov_info.sriov_enabled)
			oct->fn_list.free_mbox(oct);
	}

	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct); i++) {
		if (!(oct->io_qmask.oq & BIT_ULL(i)))
			continue;
		octeon_delete_droq(oct, i);
	}

	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct); i++) {
		if (!(oct->io_qmask.iq & BIT_ULL(i)))
			continue;
		octeon_delete_instr_queue(oct, i);
	}

	if (queue_count_update) {
		/* For PF re-configure sriov related information */
		if ((OCTEON_CN23XX_PF(oct)) &&
		    !oct->sriov_info.sriov_enabled) {
			oct->sriov_info.num_pf_rings = num_qs;
			if (cn23xx_sriov_config(oct)) {
				dev_err(&oct->pci_dev->dev,
					"Queue reset aborted: SRIOV config failed\n");
				return -1;
			}

			num_qs = oct->sriov_info.num_pf_rings;
		}
	}

	if (oct->fn_list.setup_device_regs(oct)) {
		dev_err(&oct->pci_dev->dev, "Failed to configure device registers\n");
		return -1;
	}

	/* The following are needed in case of queue count re-configuration and
	 * not for descriptor count re-configuration.
	 */
	if (queue_count_update) {
		if (octeon_setup_instr_queues(oct))
			return -1;

		if (octeon_setup_output_queues(oct))
			return -1;

		/* Recreating mbox for PF that is SRIOV disabled */
		if (OCTEON_CN23XX_PF(oct) && !oct->sriov_info.sriov_enabled) {
			if (oct->fn_list.setup_mbox(oct)) {
				dev_err(&oct->pci_dev->dev, "Mailbox setup failed\n");
				return -1;
			}
		}

		/* Deleting and recreating IRQs whether the interface is SRIOV
		 * enabled or disabled.
		 */
		if (lio_irq_reallocate_irqs(oct, num_qs)) {
			dev_err(&oct->pci_dev->dev, "IRQs could not be allocated\n");
			return -1;
		}

		/* Enable the input and output queues for this Octeon device */
		if (oct->fn_list.enable_io_queues(oct)) {
			dev_err(&oct->pci_dev->dev, "Failed to enable input/output queues\n");
			return -1;
		}

		for (i = 0; i < oct->num_oqs; i++)
			writel(oct->droq[i]->max_count,
			       oct->droq[i]->pkts_credit_reg);

		/* Informing firmware about the new queue count. It is required
		 * for firmware to allocate more number of queues than those at
		 * load time.
		 */
		if (OCTEON_CN23XX_PF(oct) && !oct->sriov_info.sriov_enabled) {
			if (lio_23xx_reconfigure_queue_count(lio))
				return -1;
		}
	}

	/* Once firmware is aware of the new value, queues can be recreated */
	if (liquidio_setup_io_queues(oct, 0, num_qs, num_qs)) {
		dev_err(&oct->pci_dev->dev, "I/O queues creation failed\n");
		return -1;
	}

	if (queue_count_update) {
		if (lio_setup_glists(oct, lio, num_qs)) {
			dev_err(&oct->pci_dev->dev, "Gather list allocation failed\n");
			return -1;
		}

		if (setup_rx_oom_poll_fn(netdev)) {
			dev_err(&oct->pci_dev->dev, "lio_setup_rx_oom_poll_fn failed\n");
			return 1;
		}

		/* Send firmware the information about new number of queues
		 * if the interface is a VF or a PF that is SRIOV enabled.
		 */
		if (oct->sriov_info.sriov_enabled || OCTEON_CN23XX_VF(oct))
			if (lio_send_queue_count_update(netdev, num_qs))
				return -1;
	}

	return 0;
}

static int lio_ethtool_set_ringparam(struct net_device *netdev,
				     struct ethtool_ringparam *ering)
{
	u32 rx_count, tx_count, rx_count_old, tx_count_old;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	int stopped = 0;

	if (!OCTEON_CN23XX_PF(oct) && !OCTEON_CN23XX_VF(oct))
		return -EINVAL;

	if (ering->rx_mini_pending || ering->rx_jumbo_pending)
		return -EINVAL;

	rx_count = clamp_t(u32, ering->rx_pending, CN23XX_MIN_OQ_DESCRIPTORS,
			   CN23XX_MAX_OQ_DESCRIPTORS);
	tx_count = clamp_t(u32, ering->tx_pending, CN23XX_MIN_IQ_DESCRIPTORS,
			   CN23XX_MAX_IQ_DESCRIPTORS);

	rx_count_old = oct->droq[0]->max_count;
	tx_count_old = oct->instr_queue[0]->max_count;

	if (rx_count == rx_count_old && tx_count == tx_count_old)
		return 0;

	ifstate_set(lio, LIO_IFSTATE_RESETTING);

	if (netif_running(netdev)) {
		netdev->netdev_ops->ndo_stop(netdev);
		stopped = 1;
	}

	/* Change RX/TX DESCS  count */
	if (tx_count != tx_count_old)
		CFG_SET_NUM_TX_DESCS_NIC_IF(octeon_get_conf(oct), lio->ifidx,
					    tx_count);
	if (rx_count != rx_count_old)
		CFG_SET_NUM_RX_DESCS_NIC_IF(octeon_get_conf(oct), lio->ifidx,
					    rx_count);

	if (lio_reset_queues(netdev, oct->num_iqs))
		goto err_lio_reset_queues;

	if (stopped)
		netdev->netdev_ops->ndo_open(netdev);

	ifstate_reset(lio, LIO_IFSTATE_RESETTING);

	return 0;

err_lio_reset_queues:
	if (tx_count != tx_count_old)
		CFG_SET_NUM_TX_DESCS_NIC_IF(octeon_get_conf(oct), lio->ifidx,
					    tx_count_old);
	if (rx_count != rx_count_old)
		CFG_SET_NUM_RX_DESCS_NIC_IF(octeon_get_conf(oct), lio->ifidx,
					    rx_count_old);
	return -EINVAL;
}

static u32 lio_get_msglevel(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	return lio->msg_enable;
}

static void lio_set_msglevel(struct net_device *netdev, u32 msglvl)
{
	struct lio *lio = GET_LIO(netdev);

	if ((msglvl ^ lio->msg_enable) & NETIF_MSG_HW) {
		if (msglvl & NETIF_MSG_HW)
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_ENABLE, 0);
		else
			liquidio_set_feature(netdev,
					     OCTNET_CMD_VERBOSE_DISABLE, 0);
	}

	lio->msg_enable = msglvl;
}

static void lio_vf_set_msglevel(struct net_device *netdev, u32 msglvl)
{
	struct lio *lio = GET_LIO(netdev);

	lio->msg_enable = msglvl;
}

static void
lio_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	/* Notes: Not supporting any auto negotiation in these
	 * drivers. Just report pause frame support.
	 */
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	pause->autoneg = 0;

	pause->tx_pause = oct->tx_pause;
	pause->rx_pause = oct->rx_pause;
}

static int
lio_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause)
{
	/* Notes: Not supporting any auto negotiation in these
	 * drivers.
	 */
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	struct oct_link_info *linfo = &lio->linfo;

	int ret = 0;

	if (oct->chip_id != OCTEON_CN23XX_PF_VID)
		return -EINVAL;

	if (linfo->link.s.duplex == 0) {
		/*no flow control for half duplex*/
		if (pause->rx_pause || pause->tx_pause)
			return -EINVAL;
	}

	/*do not support autoneg of link flow control*/
	if (pause->autoneg == AUTONEG_ENABLE)
		return -EINVAL;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = OCTNET_CMD_SET_FLOW_CTL;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	if (pause->rx_pause) {
		/*enable rx pause*/
		nctrl.ncmd.s.param1 = 1;
	} else {
		/*disable rx pause*/
		nctrl.ncmd.s.param1 = 0;
	}

	if (pause->tx_pause) {
		/*enable tx pause*/
		nctrl.ncmd.s.param2 = 1;
	} else {
		/*disable tx pause*/
		nctrl.ncmd.s.param2 = 0;
	}

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"Failed to set pause parameter, ret=%d\n", ret);
		return -EINVAL;
	}

	oct->rx_pause = pause->rx_pause;
	oct->tx_pause = pause->tx_pause;

	return 0;
}

static void
lio_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats  __attribute__((unused)),
		      u64 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	struct rtnl_link_stats64 lstats;
	int i = 0, j;

	if (ifstate_check(lio, LIO_IFSTATE_RESETTING))
		return;

	netdev->netdev_ops->ndo_get_stats64(netdev, &lstats);
	/*sum of oct->droq[oq_no]->stats->rx_pkts_received */
	data[i++] = lstats.rx_packets;
	/*sum of oct->instr_queue[iq_no]->stats.tx_done */
	data[i++] = lstats.tx_packets;
	/*sum of oct->droq[oq_no]->stats->rx_bytes_received */
	data[i++] = lstats.rx_bytes;
	/*sum of oct->instr_queue[iq_no]->stats.tx_tot_bytes */
	data[i++] = lstats.tx_bytes;
	data[i++] = lstats.rx_errors +
			oct_dev->link_stats.fromwire.fcs_err +
			oct_dev->link_stats.fromwire.jabber_err +
			oct_dev->link_stats.fromwire.l2_err +
			oct_dev->link_stats.fromwire.frame_err;
	data[i++] = lstats.tx_errors;
	/*sum of oct->droq[oq_no]->stats->rx_dropped +
	 *oct->droq[oq_no]->stats->dropped_nodispatch +
	 *oct->droq[oq_no]->stats->dropped_toomany +
	 *oct->droq[oq_no]->stats->dropped_nomem
	 */
	data[i++] = lstats.rx_dropped +
			oct_dev->link_stats.fromwire.fifo_err +
			oct_dev->link_stats.fromwire.dmac_drop +
			oct_dev->link_stats.fromwire.red_drops +
			oct_dev->link_stats.fromwire.fw_err_pko +
			oct_dev->link_stats.fromwire.fw_err_link +
			oct_dev->link_stats.fromwire.fw_err_drop;
	/*sum of oct->instr_queue[iq_no]->stats.tx_dropped */
	data[i++] = lstats.tx_dropped +
			oct_dev->link_stats.fromhost.max_collision_fail +
			oct_dev->link_stats.fromhost.max_deferral_fail +
			oct_dev->link_stats.fromhost.total_collisions +
			oct_dev->link_stats.fromhost.fw_err_pko +
			oct_dev->link_stats.fromhost.fw_err_link +
			oct_dev->link_stats.fromhost.fw_err_drop +
			oct_dev->link_stats.fromhost.fw_err_pki;

	/* firmware tx stats */
	/*per_core_stats[cvmx_get_core_num()].link_stats[mdata->from_ifidx].
	 *fromhost.fw_total_sent
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_total_sent);
	/*per_core_stats[i].link_stats[port].fromwire.fw_total_fwd */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_total_fwd);
	/*per_core_stats[j].link_stats[i].fromhost.fw_err_pko */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_pko);
	/*per_core_stats[j].link_stats[i].fromhost.fw_err_pki */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_pki);
	/*per_core_stats[j].link_stats[i].fromhost.fw_err_link */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_link);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_err_drop
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_drop);

	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.fw_tso */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tso);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_tso_fwd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tso_fwd);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_err_tso
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_err_tso);
	/*per_core_stats[cvmx_get_core_num()].link_stats[idx].fromhost.
	 *fw_tx_vxlan
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fw_tx_vxlan);

	/* Multicast packets sent by this port */
	data[i++] = oct_dev->link_stats.fromhost.fw_total_mcast_sent;
	data[i++] = oct_dev->link_stats.fromhost.fw_total_bcast_sent;

	/* mac tx statistics */
	/*CVMX_BGXX_CMRX_TX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT4 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_bytes_sent);
	/*CVMX_BGXX_CMRX_TX_STAT15 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.mcast_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT14 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.bcast_pkts_sent);
	/*CVMX_BGXX_CMRX_TX_STAT17 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.ctl_sent);
	/*CVMX_BGXX_CMRX_TX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.total_collisions);
	/*CVMX_BGXX_CMRX_TX_STAT3 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.one_collision_sent);
	/*CVMX_BGXX_CMRX_TX_STAT2 */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromhost.multi_collision_sent);
	/*CVMX_BGXX_CMRX_TX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.max_collision_fail);
	/*CVMX_BGXX_CMRX_TX_STAT1 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.max_deferral_fail);
	/*CVMX_BGXX_CMRX_TX_STAT16 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.fifo_err);
	/*CVMX_BGXX_CMRX_TX_STAT6 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromhost.runts);

	/* RX firmware stats */
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_total_rcvd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_rcvd);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_total_fwd
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_fwd);
	/* Multicast packets received on this port */
	data[i++] = oct_dev->link_stats.fromwire.fw_total_mcast;
	data[i++] = oct_dev->link_stats.fromwire.fw_total_bcast;
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.jabber_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.jabber_err);
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.l2_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.l2_err);
	/*per_core_stats[core_id].link_stats[ifidx].fromwire.frame_err */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.frame_err);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_err_pko
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_pko);
	/*per_core_stats[j].link_stats[i].fromwire.fw_err_link */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_link);
	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_err_drop
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_err_drop);

	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_rx_vxlan
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_rx_vxlan);
	/*per_core_stats[cvmx_get_core_num()].link_stats[lro_ctx->ifidx].
	 *fromwire.fw_rx_vxlan_err
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_rx_vxlan_err);

	/* LRO */
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_pkts
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_pkts);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_octs
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_octs);
	/*per_core_stats[j].link_stats[i].fromwire.fw_total_lro */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_total_lro);
	/*per_core_stats[j].link_stats[i].fromwire.fw_lro_aborts */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_port
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_port);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_seq
	 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_seq);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_tsval
	 */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_tsval);
	/*per_core_stats[cvmx_get_core_num()].link_stats[ifidx].fromwire.
	 *fw_lro_aborts_timer
	 */
	/* intrmod: packet forward rate */
	data[i++] =
		CVM_CAST64(oct_dev->link_stats.fromwire.fw_lro_aborts_timer);
	/*per_core_stats[j].link_stats[i].fromwire.fw_lro_aborts */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fwd_rate);

	/* mac: link-level stats */
	/*CVMX_BGXX_CMRX_RX_STAT0 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_rcvd);
	/*CVMX_BGXX_CMRX_RX_STAT1 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.bytes_rcvd);
	/*CVMX_PKI_STATX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_bcst);
	/*CVMX_PKI_STATX_STAT5 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.total_mcst);
	/*wqe->word2.err_code or wqe->word2.err_level */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.runts);
	/*CVMX_BGXX_CMRX_RX_STAT2 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.ctl_rcvd);
	/*CVMX_BGXX_CMRX_RX_STAT6 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fifo_err);
	/*CVMX_BGXX_CMRX_RX_STAT4 */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.dmac_drop);
	/*wqe->word2.err_code or wqe->word2.err_level */
	data[i++] = CVM_CAST64(oct_dev->link_stats.fromwire.fcs_err);
	/*lio->link_changes*/
	data[i++] = CVM_CAST64(lio->link_changes);

	for (j = 0; j < MAX_OCTEON_INSTR_QUEUES(oct_dev); j++) {
		if (!(oct_dev->io_qmask.iq & BIT_ULL(j)))
			continue;
		/*packets to network port*/
		/*# of packets tx to network */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_done);
		/*# of bytes tx to network */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_tot_bytes);
		/*# of packets dropped */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_dropped);
		/*# of tx fails due to queue full */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_iq_busy);
		/*XXX gather entries sent */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.sgentry_sent);

		/*instruction to firmware: data and control */
		/*# of instructions to the queue */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_posted);
		/*# of instructions processed */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_processed);
		/*# of instructions could not be processed */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_dropped);
		/*bytes sent through the queue */
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.bytes_sent);

		/*tso request*/
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_gso);
		/*vxlan request*/
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_vxlan);
		/*txq restart*/
		data[i++] =
			CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_restart);
	}

	/* RX */
	for (j = 0; j < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); j++) {
		if (!(oct_dev->io_qmask.oq & BIT_ULL(j)))
			continue;

		/*packets send to TCP/IP network stack */
		/*# of packets to network stack */
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_pkts_received);
		/*# of bytes to network stack */
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_bytes_received);
		/*# of packets dropped */
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem +
				       oct_dev->droq[j]->stats.dropped_toomany +
				       oct_dev->droq[j]->stats.rx_dropped);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_toomany);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_dropped);

		/*control and data path*/
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.pkts_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nodispatch);

		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_vxlan);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.rx_alloc_failure);
	}
}

static void lio_vf_get_ethtool_stats(struct net_device *netdev,
				     struct ethtool_stats *stats
				     __attribute__((unused)),
				     u64 *data)
{
	struct rtnl_link_stats64 lstats;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int i = 0, j, vj;

	if (ifstate_check(lio, LIO_IFSTATE_RESETTING))
		return;

	netdev->netdev_ops->ndo_get_stats64(netdev, &lstats);
	/* sum of oct->droq[oq_no]->stats->rx_pkts_received */
	data[i++] = lstats.rx_packets;
	/* sum of oct->instr_queue[iq_no]->stats.tx_done */
	data[i++] = lstats.tx_packets;
	/* sum of oct->droq[oq_no]->stats->rx_bytes_received */
	data[i++] = lstats.rx_bytes;
	/* sum of oct->instr_queue[iq_no]->stats.tx_tot_bytes */
	data[i++] = lstats.tx_bytes;
	data[i++] = lstats.rx_errors;
	data[i++] = lstats.tx_errors;
	 /* sum of oct->droq[oq_no]->stats->rx_dropped +
	  * oct->droq[oq_no]->stats->dropped_nodispatch +
	  * oct->droq[oq_no]->stats->dropped_toomany +
	  * oct->droq[oq_no]->stats->dropped_nomem
	  */
	data[i++] = lstats.rx_dropped;
	/* sum of oct->instr_queue[iq_no]->stats.tx_dropped */
	data[i++] = lstats.tx_dropped +
		oct_dev->link_stats.fromhost.fw_err_drop;

	data[i++] = oct_dev->link_stats.fromwire.fw_total_mcast;
	data[i++] = oct_dev->link_stats.fromhost.fw_total_mcast_sent;
	data[i++] = oct_dev->link_stats.fromwire.fw_total_bcast;
	data[i++] = oct_dev->link_stats.fromhost.fw_total_bcast_sent;

	/* lio->link_changes */
	data[i++] = CVM_CAST64(lio->link_changes);

	for (vj = 0; vj < oct_dev->num_iqs; vj++) {
		j = lio->linfo.txpciq[vj].s.q_no;

		/* packets to network port */
		/* # of packets tx to network */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_done);
		 /* # of bytes tx to network */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_tot_bytes);
		/* # of packets dropped */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_dropped);
		/* # of tx fails due to queue full */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_iq_busy);
		/* XXX gather entries sent */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.sgentry_sent);

		/* instruction to firmware: data and control */
		/* # of instructions to the queue */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.instr_posted);
		/* # of instructions processed */
		data[i++] =
		    CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_processed);
		/* # of instructions could not be processed */
		data[i++] =
		    CVM_CAST64(oct_dev->instr_queue[j]->stats.instr_dropped);
		/* bytes sent through the queue */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.bytes_sent);
		/* tso request */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_gso);
		/* vxlan request */
		data[i++] = CVM_CAST64(oct_dev->instr_queue[j]->stats.tx_vxlan);
		/* txq restart */
		data[i++] = CVM_CAST64(
				oct_dev->instr_queue[j]->stats.tx_restart);
	}

	/* RX */
	for (vj = 0; vj < oct_dev->num_oqs; vj++) {
		j = lio->linfo.rxpciq[vj].s.q_no;

		/* packets send to TCP/IP network stack */
		/* # of packets to network stack */
		data[i++] = CVM_CAST64(
				oct_dev->droq[j]->stats.rx_pkts_received);
		/* # of bytes to network stack */
		data[i++] = CVM_CAST64(
				oct_dev->droq[j]->stats.rx_bytes_received);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem +
				       oct_dev->droq[j]->stats.dropped_toomany +
				       oct_dev->droq[j]->stats.rx_dropped);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_nomem);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.dropped_toomany);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.rx_dropped);

		/* control and data path */
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.pkts_received);
		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.bytes_received);
		data[i++] =
			CVM_CAST64(oct_dev->droq[j]->stats.dropped_nodispatch);

		data[i++] = CVM_CAST64(oct_dev->droq[j]->stats.rx_vxlan);
		data[i++] =
		    CVM_CAST64(oct_dev->droq[j]->stats.rx_alloc_failure);
	}
}

static void lio_get_priv_flags_strings(struct lio *lio, u8 *data)
{
	struct octeon_device *oct_dev = lio->oct_dev;
	int i;

	switch (oct_dev->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		for (i = 0; i < ARRAY_SIZE(oct_priv_flags_strings); i++) {
			sprintf(data, "%s", oct_priv_flags_strings[i]);
			data += ETH_GSTRING_LEN;
		}
		break;
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		break;
	}
}

static void lio_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int num_iq_stats, num_oq_stats, i, j;
	int num_stats;

	switch (stringset) {
	case ETH_SS_STATS:
		num_stats = ARRAY_SIZE(oct_stats_strings);
		for (j = 0; j < num_stats; j++) {
			sprintf(data, "%s", oct_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}

		num_iq_stats = ARRAY_SIZE(oct_iq_stats_strings);
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.iq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_iq_stats; j++) {
				sprintf(data, "tx-%d-%s", i,
					oct_iq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}

		num_oq_stats = ARRAY_SIZE(oct_droq_stats_strings);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.oq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_oq_stats; j++) {
				sprintf(data, "rx-%d-%s", i,
					oct_droq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}
		break;

	case ETH_SS_PRIV_FLAGS:
		lio_get_priv_flags_strings(lio, data);
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Stringset !!\n");
		break;
	}
}

static void lio_vf_get_strings(struct net_device *netdev, u32 stringset,
			       u8 *data)
{
	int num_iq_stats, num_oq_stats, i, j;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;
	int num_stats;

	switch (stringset) {
	case ETH_SS_STATS:
		num_stats = ARRAY_SIZE(oct_vf_stats_strings);
		for (j = 0; j < num_stats; j++) {
			sprintf(data, "%s", oct_vf_stats_strings[j]);
			data += ETH_GSTRING_LEN;
		}

		num_iq_stats = ARRAY_SIZE(oct_iq_stats_strings);
		for (i = 0; i < MAX_OCTEON_INSTR_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.iq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_iq_stats; j++) {
				sprintf(data, "tx-%d-%s", i,
					oct_iq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}

		num_oq_stats = ARRAY_SIZE(oct_droq_stats_strings);
		for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES(oct_dev); i++) {
			if (!(oct_dev->io_qmask.oq & BIT_ULL(i)))
				continue;
			for (j = 0; j < num_oq_stats; j++) {
				sprintf(data, "rx-%d-%s", i,
					oct_droq_stats_strings[j]);
				data += ETH_GSTRING_LEN;
			}
		}
		break;

	case ETH_SS_PRIV_FLAGS:
		lio_get_priv_flags_strings(lio, data);
		break;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Stringset !!\n");
		break;
	}
}

static int lio_get_priv_flags_ss_count(struct lio *lio)
{
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (oct_dev->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		return ARRAY_SIZE(oct_priv_flags_strings);
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		return -EOPNOTSUPP;
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		return -EOPNOTSUPP;
	}
}

static int lio_get_sset_count(struct net_device *netdev, int sset)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (sset) {
	case ETH_SS_STATS:
		return (ARRAY_SIZE(oct_stats_strings) +
			ARRAY_SIZE(oct_iq_stats_strings) * oct_dev->num_iqs +
			ARRAY_SIZE(oct_droq_stats_strings) * oct_dev->num_oqs);
	case ETH_SS_PRIV_FLAGS:
		return lio_get_priv_flags_ss_count(lio);
	default:
		return -EOPNOTSUPP;
	}
}

static int lio_vf_get_sset_count(struct net_device *netdev, int sset)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct_dev = lio->oct_dev;

	switch (sset) {
	case ETH_SS_STATS:
		return (ARRAY_SIZE(oct_vf_stats_strings) +
			ARRAY_SIZE(oct_iq_stats_strings) * oct_dev->num_iqs +
			ARRAY_SIZE(oct_droq_stats_strings) * oct_dev->num_oqs);
	case ETH_SS_PRIV_FLAGS:
		return lio_get_priv_flags_ss_count(lio);
	default:
		return -EOPNOTSUPP;
	}
}

/*  get interrupt moderation parameters */
static int octnet_get_intrmod_cfg(struct lio *lio,
				  struct oct_intrmod_cfg *intr_cfg)
{
	struct octeon_soft_command *sc;
	struct oct_intrmod_resp *resp;
	int retval;
	struct octeon_device *oct_dev = lio->oct_dev;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  0,
					  sizeof(struct oct_intrmod_resp), 0);

	if (!sc)
		return -ENOMEM;

	resp = (struct oct_intrmod_resp *)sc->virtrptr;
	memset(resp, 0, sizeof(struct oct_intrmod_resp));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_INTRMOD_PARAMS, 0, 0, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	/* Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	retval = wait_for_sc_completion_timeout(oct_dev, sc, 0);
	if (retval)
		return -ENODEV;

	if (resp->status) {
		dev_err(&oct_dev->pci_dev->dev,
			"Get interrupt moderation parameters failed\n");
		WRITE_ONCE(sc->caller_is_done, true);
		return -ENODEV;
	}

	octeon_swap_8B_data((u64 *)&resp->intrmod,
			    (sizeof(struct oct_intrmod_cfg)) / 8);
	memcpy(intr_cfg, &resp->intrmod, sizeof(struct oct_intrmod_cfg));
	WRITE_ONCE(sc->caller_is_done, true);

	return 0;
}

/*  Configure interrupt moderation parameters */
static int octnet_set_intrmod_cfg(struct lio *lio,
				  struct oct_intrmod_cfg *intr_cfg)
{
	struct octeon_soft_command *sc;
	struct oct_intrmod_cfg *cfg;
	int retval;
	struct octeon_device *oct_dev = lio->oct_dev;

	/* Alloc soft command */
	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct_dev,
					  sizeof(struct oct_intrmod_cfg),
					  16, 0);

	if (!sc)
		return -ENOMEM;

	cfg = (struct oct_intrmod_cfg *)sc->virtdptr;

	memcpy(cfg, intr_cfg, sizeof(struct oct_intrmod_cfg));
	octeon_swap_8B_data((u64 *)cfg, (sizeof(struct oct_intrmod_cfg)) / 8);

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	octeon_prepare_soft_command(oct_dev, sc, OPCODE_NIC,
				    OPCODE_NIC_INTRMOD_CFG, 0, 0, 0);

	init_completion(&sc->complete);
	sc->sc_status = OCTEON_REQUEST_PENDING;

	retval = octeon_send_soft_command(oct_dev, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct_dev, sc);
		return -EINVAL;
	}

	/* Sleep on a wait queue till the cond flag indicates that the
	 * response arrived or timed-out.
	 */
	retval = wait_for_sc_completion_timeout(oct_dev, sc, 0);
	if (retval)
		return retval;

	retval = sc->sc_status;
	if (retval == 0) {
		dev_info(&oct_dev->pci_dev->dev,
			 "Rx-Adaptive Interrupt moderation %s\n",
			 (intr_cfg->rx_enable) ?
			 "enabled" : "disabled");
		WRITE_ONCE(sc->caller_is_done, true);
		return 0;
	}

	dev_err(&oct_dev->pci_dev->dev,
		"intrmod config failed. Status: %x\n", retval);
	WRITE_ONCE(sc->caller_is_done, true);
	return -ENODEV;
}

static int lio_get_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_instr_queue *iq;
	struct oct_intrmod_cfg intrmod_cfg;

	if (octnet_get_intrmod_cfg(lio, &intrmod_cfg))
		return -ENODEV;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID: {
		if (!intrmod_cfg.rx_enable) {
			intr_coal->rx_coalesce_usecs = oct->rx_coalesce_usecs;
			intr_coal->rx_max_coalesced_frames =
				oct->rx_max_coalesced_frames;
		}
		if (!intrmod_cfg.tx_enable)
			intr_coal->tx_max_coalesced_frames =
				oct->tx_max_coalesced_frames;
		break;
	}
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;

		if (!intrmod_cfg.rx_enable) {
			intr_coal->rx_coalesce_usecs =
				CFG_GET_OQ_INTR_TIME(cn6xxx->conf);
			intr_coal->rx_max_coalesced_frames =
				CFG_GET_OQ_INTR_PKT(cn6xxx->conf);
		}
		iq = oct->instr_queue[lio->linfo.txpciq[0].s.q_no];
		intr_coal->tx_max_coalesced_frames = iq->fill_threshold;
		break;
	}
	default:
		netif_info(lio, drv, lio->netdev, "Unknown Chip !!\n");
		return -EINVAL;
	}
	if (intrmod_cfg.rx_enable) {
		intr_coal->use_adaptive_rx_coalesce =
			intrmod_cfg.rx_enable;
		intr_coal->rate_sample_interval =
			intrmod_cfg.check_intrvl;
		intr_coal->pkt_rate_high =
			intrmod_cfg.maxpkt_ratethr;
		intr_coal->pkt_rate_low =
			intrmod_cfg.minpkt_ratethr;
		intr_coal->rx_max_coalesced_frames_high =
			intrmod_cfg.rx_maxcnt_trigger;
		intr_coal->rx_coalesce_usecs_high =
			intrmod_cfg.rx_maxtmr_trigger;
		intr_coal->rx_coalesce_usecs_low =
			intrmod_cfg.rx_mintmr_trigger;
		intr_coal->rx_max_coalesced_frames_low =
			intrmod_cfg.rx_mincnt_trigger;
	}
	if ((OCTEON_CN23XX_PF(oct) || OCTEON_CN23XX_VF(oct)) &&
	    (intrmod_cfg.tx_enable)) {
		intr_coal->use_adaptive_tx_coalesce =
			intrmod_cfg.tx_enable;
		intr_coal->tx_max_coalesced_frames_high =
			intrmod_cfg.tx_maxcnt_trigger;
		intr_coal->tx_max_coalesced_frames_low =
			intrmod_cfg.tx_mincnt_trigger;
	}
	return 0;
}

/* Enable/Disable auto interrupt Moderation */
static int oct_cfg_adaptive_intr(struct lio *lio,
				 struct oct_intrmod_cfg *intrmod_cfg,
				 struct ethtool_coalesce *intr_coal)
{
	int ret = 0;

	if (intrmod_cfg->rx_enable || intrmod_cfg->tx_enable) {
		intrmod_cfg->check_intrvl = intr_coal->rate_sample_interval;
		intrmod_cfg->maxpkt_ratethr = intr_coal->pkt_rate_high;
		intrmod_cfg->minpkt_ratethr = intr_coal->pkt_rate_low;
	}
	if (intrmod_cfg->rx_enable) {
		intrmod_cfg->rx_maxcnt_trigger =
			intr_coal->rx_max_coalesced_frames_high;
		intrmod_cfg->rx_maxtmr_trigger =
			intr_coal->rx_coalesce_usecs_high;
		intrmod_cfg->rx_mintmr_trigger =
			intr_coal->rx_coalesce_usecs_low;
		intrmod_cfg->rx_mincnt_trigger =
			intr_coal->rx_max_coalesced_frames_low;
	}
	if (intrmod_cfg->tx_enable) {
		intrmod_cfg->tx_maxcnt_trigger =
			intr_coal->tx_max_coalesced_frames_high;
		intrmod_cfg->tx_mincnt_trigger =
			intr_coal->tx_max_coalesced_frames_low;
	}

	ret = octnet_set_intrmod_cfg(lio, intrmod_cfg);

	return ret;
}

static int
oct_cfg_rx_intrcnt(struct lio *lio,
		   struct oct_intrmod_cfg *intrmod,
		   struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 rx_max_coalesced_frames;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = CN6XXX_OQ_INTR_PKT;
		else
			rx_max_coalesced_frames =
				intr_coal->rx_max_coalesced_frames;
		octeon_write_csr(oct, CN6XXX_SLI_OQ_INT_LEVEL_PKTS,
				 rx_max_coalesced_frames);
		CFG_SET_OQ_INTR_PKT(cn6xxx->conf, rx_max_coalesced_frames);
		break;
	}
	case OCTEON_CN23XX_PF_VID: {
		int q_no;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = intrmod->rx_frames;
		else
			rx_max_coalesced_frames =
			    intr_coal->rx_max_coalesced_frames;
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			q_no += oct->sriov_info.pf_srn;
			octeon_write_csr64(
			    oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
			    (octeon_read_csr64(
				 oct, CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no)) &
			     (0x3fffff00000000UL)) |
				(rx_max_coalesced_frames - 1));
			/*consider setting resend bit*/
		}
		intrmod->rx_frames = rx_max_coalesced_frames;
		oct->rx_max_coalesced_frames = rx_max_coalesced_frames;
		break;
	}
	case OCTEON_CN23XX_VF_VID: {
		int q_no;

		if (!intr_coal->rx_max_coalesced_frames)
			rx_max_coalesced_frames = intrmod->rx_frames;
		else
			rx_max_coalesced_frames =
			    intr_coal->rx_max_coalesced_frames;
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
			    (octeon_read_csr64(
				 oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no)) &
			     (0x3fffff00000000UL)) |
				(rx_max_coalesced_frames - 1));
			/*consider writing to resend bit here*/
		}
		intrmod->rx_frames = rx_max_coalesced_frames;
		oct->rx_max_coalesced_frames = rx_max_coalesced_frames;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int oct_cfg_rx_intrtime(struct lio *lio,
			       struct oct_intrmod_cfg *intrmod,
			       struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 time_threshold, rx_coalesce_usecs;

	/* Config Time based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX: {
		struct octeon_cn6xxx *cn6xxx =
			(struct octeon_cn6xxx *)oct->chip;
		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = CN6XXX_OQ_INTR_TIME;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;

		time_threshold = lio_cn6xxx_get_oq_ticks(oct,
							 rx_coalesce_usecs);
		octeon_write_csr(oct,
				 CN6XXX_SLI_OQ_INT_LEVEL_TIME,
				 time_threshold);

		CFG_SET_OQ_INTR_TIME(cn6xxx->conf, rx_coalesce_usecs);
		break;
	}
	case OCTEON_CN23XX_PF_VID: {
		u64 time_threshold;
		int q_no;

		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = intrmod->rx_usecs;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;
		time_threshold =
		    cn23xx_pf_get_oq_ticks(oct, (u32)rx_coalesce_usecs);
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			q_no += oct->sriov_info.pf_srn;
			octeon_write_csr64(oct,
					   CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
					   (intrmod->rx_frames |
					    ((u64)time_threshold << 32)));
			/*consider writing to resend bit here*/
		}
		intrmod->rx_usecs = rx_coalesce_usecs;
		oct->rx_coalesce_usecs = rx_coalesce_usecs;
		break;
	}
	case OCTEON_CN23XX_VF_VID: {
		u64 time_threshold;
		int q_no;

		if (!intr_coal->rx_coalesce_usecs)
			rx_coalesce_usecs = intrmod->rx_usecs;
		else
			rx_coalesce_usecs = intr_coal->rx_coalesce_usecs;

		time_threshold =
		    cn23xx_vf_get_oq_ticks(oct, (u32)rx_coalesce_usecs);
		for (q_no = 0; q_no < oct->num_oqs; q_no++) {
			octeon_write_csr64(
				oct, CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(q_no),
				(intrmod->rx_frames |
				 ((u64)time_threshold << 32)));
			/*consider setting resend bit*/
		}
		intrmod->rx_usecs = rx_coalesce_usecs;
		oct->rx_coalesce_usecs = rx_coalesce_usecs;
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int
oct_cfg_tx_intrcnt(struct lio *lio,
		   struct oct_intrmod_cfg *intrmod,
		   struct ethtool_coalesce *intr_coal)
{
	struct octeon_device *oct = lio->oct_dev;
	u32 iq_intr_pkt;
	void __iomem *inst_cnt_reg;
	u64 val;

	/* Config Cnt based interrupt values */
	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		break;
	case OCTEON_CN23XX_VF_VID:
	case OCTEON_CN23XX_PF_VID: {
		int q_no;

		if (!intr_coal->tx_max_coalesced_frames)
			iq_intr_pkt = CN23XX_DEF_IQ_INTR_THRESHOLD &
				      CN23XX_PKT_IN_DONE_WMARK_MASK;
		else
			iq_intr_pkt = intr_coal->tx_max_coalesced_frames &
				      CN23XX_PKT_IN_DONE_WMARK_MASK;
		for (q_no = 0; q_no < oct->num_iqs; q_no++) {
			inst_cnt_reg = (oct->instr_queue[q_no])->inst_cnt_reg;
			val = readq(inst_cnt_reg);
			/*clear wmark and count.dont want to write count back*/
			val = (val & 0xFFFF000000000000ULL) |
			      ((u64)(iq_intr_pkt - 1)
			       << CN23XX_PKT_IN_DONE_WMARK_BIT_POS);
			writeq(val, inst_cnt_reg);
			/*consider setting resend bit*/
		}
		intrmod->tx_frames = iq_intr_pkt;
		oct->tx_max_coalesced_frames = iq_intr_pkt;
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int lio_set_intr_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *intr_coal,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct lio *lio = GET_LIO(netdev);
	int ret;
	struct octeon_device *oct = lio->oct_dev;
	struct oct_intrmod_cfg intrmod = {0};
	u32 j, q_no;
	int db_max, db_min;

	switch (oct->chip_id) {
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		db_min = CN6XXX_DB_MIN;
		db_max = CN6XXX_DB_MAX;
		if ((intr_coal->tx_max_coalesced_frames >= db_min) &&
		    (intr_coal->tx_max_coalesced_frames <= db_max)) {
			for (j = 0; j < lio->linfo.num_txpciq; j++) {
				q_no = lio->linfo.txpciq[j].s.q_no;
				oct->instr_queue[q_no]->fill_threshold =
					intr_coal->tx_max_coalesced_frames;
			}
		} else {
			dev_err(&oct->pci_dev->dev,
				"LIQUIDIO: Invalid tx-frames:%d. Range is min:%d max:%d\n",
				intr_coal->tx_max_coalesced_frames,
				db_min, db_max);
			return -EINVAL;
		}
		break;
	case OCTEON_CN23XX_PF_VID:
	case OCTEON_CN23XX_VF_VID:
		break;
	default:
		return -EINVAL;
	}

	intrmod.rx_enable = intr_coal->use_adaptive_rx_coalesce ? 1 : 0;
	intrmod.tx_enable = intr_coal->use_adaptive_tx_coalesce ? 1 : 0;
	intrmod.rx_frames = CFG_GET_OQ_INTR_PKT(octeon_get_conf(oct));
	intrmod.rx_usecs = CFG_GET_OQ_INTR_TIME(octeon_get_conf(oct));
	intrmod.tx_frames = CFG_GET_IQ_INTR_PKT(octeon_get_conf(oct));

	ret = oct_cfg_adaptive_intr(lio, &intrmod, intr_coal);

	if (!intr_coal->use_adaptive_rx_coalesce) {
		ret = oct_cfg_rx_intrtime(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;

		ret = oct_cfg_rx_intrcnt(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;
	} else {
		oct->rx_coalesce_usecs =
			CFG_GET_OQ_INTR_TIME(octeon_get_conf(oct));
		oct->rx_max_coalesced_frames =
			CFG_GET_OQ_INTR_PKT(octeon_get_conf(oct));
	}

	if (!intr_coal->use_adaptive_tx_coalesce) {
		ret = oct_cfg_tx_intrcnt(lio, &intrmod, intr_coal);
		if (ret)
			goto ret_intrmod;
	} else {
		oct->tx_max_coalesced_frames =
			CFG_GET_IQ_INTR_PKT(octeon_get_conf(oct));
	}

	return 0;
ret_intrmod:
	return ret;
}

static int lio_get_ts_info(struct net_device *netdev,
			   struct ethtool_ts_info *info)
{
	struct lio *lio = GET_LIO(netdev);

	info->so_timestamping =
#ifdef PTP_HARDWARE_TIMESTAMPING
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
#endif
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;

	if (lio->ptp_clock)
		info->phc_index = ptp_clock_index(lio->ptp_clock);
	else
		info->phc_index = -1;

#ifdef PTP_HARDWARE_TIMESTAMPING
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);
#endif

	return 0;
}

/* Return register dump len. */
static int lio_get_regs_len(struct net_device *dev)
{
	struct lio *lio = GET_LIO(dev);
	struct octeon_device *oct = lio->oct_dev;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
		return OCT_ETHTOOL_REGDUMP_LEN_23XX;
	case OCTEON_CN23XX_VF_VID:
		return OCT_ETHTOOL_REGDUMP_LEN_23XX_VF;
	default:
		return OCT_ETHTOOL_REGDUMP_LEN;
	}
}

static int cn23xx_read_csr_reg(char *s, struct octeon_device *oct)
{
	u32 reg;
	u8 pf_num = oct->pf_num;
	int len = 0;
	int i;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");

	/*0x29030 or 0x29040*/
	reg = CN23XX_SLI_PKT_MAC_RINFO64(oct->pcie_port, oct->pf_num);
	len += sprintf(s + len,
		       "\n[%08x] (SLI_PKT_MAC%d_PF%d_RINFO): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x27080 or 0x27090*/
	reg = CN23XX_SLI_MAC_PF_INT_ENB64(oct->pcie_port, oct->pf_num);
	len +=
	    sprintf(s + len, "\n[%08x] (SLI_MAC%d_PF%d_INT_ENB): %016llx\n",
		    reg, oct->pcie_port, oct->pf_num,
		    (u64)octeon_read_csr64(oct, reg));

	/*0x27000 or 0x27010*/
	reg = CN23XX_SLI_MAC_PF_INT_SUM64(oct->pcie_port, oct->pf_num);
	len +=
	    sprintf(s + len, "\n[%08x] (SLI_MAC%d_PF%d_INT_SUM): %016llx\n",
		    reg, oct->pcie_port, oct->pf_num,
		    (u64)octeon_read_csr64(oct, reg));

	/*0x29120*/
	reg = 0x29120;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_MEM_CTL): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x27300*/
	reg = 0x27300 + oct->pcie_port * CN23XX_MAC_INT_OFFSET +
	      (oct->pf_num) * CN23XX_PF_INT_OFFSET;
	len += sprintf(
	    s + len, "\n[%08x] (SLI_MAC%d_PF%d_PKT_VF_INT): %016llx\n", reg,
	    oct->pcie_port, oct->pf_num, (u64)octeon_read_csr64(oct, reg));

	/*0x27200*/
	reg = 0x27200 + oct->pcie_port * CN23XX_MAC_INT_OFFSET +
	      (oct->pf_num) * CN23XX_PF_INT_OFFSET;
	len += sprintf(s + len,
		       "\n[%08x] (SLI_MAC%d_PF%d_PP_VF_INT): %016llx\n",
		       reg, oct->pcie_port, oct->pf_num,
		       (u64)octeon_read_csr64(oct, reg));

	/*29130*/
	reg = CN23XX_SLI_PKT_CNT_INT;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_CNT_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29140*/
	reg = CN23XX_SLI_PKT_TIME_INT;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_TIME_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29160*/
	reg = 0x29160;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_INT): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29180*/
	reg = CN23XX_SLI_OQ_WMARK;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_OUTPUT_WMARK): %016llx\n",
		       reg, (u64)octeon_read_csr64(oct, reg));

	/*0x291E0*/
	reg = CN23XX_SLI_PKT_IOQ_RING_RST;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_RING_RST): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29210*/
	reg = CN23XX_SLI_GBL_CONTROL;
	len += sprintf(s + len,
		       "\n[%08x] (SLI_PKT_GBL_CONTROL): %016llx\n", reg,
		       (u64)octeon_read_csr64(oct, reg));

	/*0x29220*/
	reg = 0x29220;
	len += sprintf(s + len, "\n[%08x] (SLI_PKT_BIST_STATUS): %016llx\n",
		       reg, (u64)octeon_read_csr64(oct, reg));

	/*PF only*/
	if (pf_num == 0) {
		/*0x29260*/
		reg = CN23XX_SLI_OUT_BP_EN_W1S;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_OUT_BP_EN_W1S):  %016llx\n",
			       reg, (u64)octeon_read_csr64(oct, reg));
	} else if (pf_num == 1) {
		/*0x29270*/
		reg = CN23XX_SLI_OUT_BP_EN2_W1S;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_OUT_BP_EN2_W1S): %016llx\n",
			       reg, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_BUFF_INFO_SIZE(i);
		len +=
		    sprintf(s + len, "\n[%08x] (SLI_PKT%d_OUT_SIZE): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10040*/
	for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
		reg = CN23XX_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10080*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10090*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_SIZE(i);
		len += sprintf(
		    s + len, "\n[%08x] (SLI_PKT%d_SLIST_FIFO_RSIZE): %016llx\n",
		    reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10050*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKT_CONTROL(i);
		len += sprintf(
			s + len,
			"\n[%08x] (SLI_PKT%d__OUTPUT_CONTROL): %016llx\n",
			reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x10070*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100a0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKT_INT_LEVELS(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INT_LEVELS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100b0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = CN23XX_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%08x] (SLI_PKT%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	/*0x100c0*/
	for (i = 0; i < CN23XX_MAX_OUTPUT_QUEUES; i++) {
		reg = 0x100c0 + i * CN23XX_OQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_ERROR_INFO): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));

		/*0x10000*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_PKT_CONTROL64(i);
			len += sprintf(
				s + len,
				"\n[%08x] (SLI_PKT%d_INPUT_CONTROL): %016llx\n",
				reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10010*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_BASE_ADDR64(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_BADDR): %016llx\n", reg,
			    i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10020*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_DOORBELL(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_BAOFF_DBELL): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10030*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++) {
			reg = CN23XX_SLI_IQ_SIZE(i);
			len += sprintf(
			    s + len,
			    "\n[%08x] (SLI_PKT%d_INSTR_FIFO_RSIZE): %016llx\n",
			    reg, i, (u64)octeon_read_csr64(oct, reg));
		}

		/*0x10040*/
		for (i = 0; i < CN23XX_MAX_INPUT_QUEUES; i++)
			reg = CN23XX_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	return len;
}

static int cn23xx_vf_read_csr_reg(char *s, struct octeon_device *oct)
{
	int len = 0;
	u32 reg;
	int i;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_BUFF_INFO_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_OUT_SIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_FIFO_RSIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKT_CONTROL(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d__OUTPUT_CONTROL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_SLIST_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKT_INT_LEVELS(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INT_LEVELS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%08x] (SLI_PKT%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = 0x100c0 + i * CN23XX_VF_OQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_ERROR_INFO): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = 0x100d0 + i * CN23XX_VF_IQ_OFFSET;
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_VF_INT_SUM): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_PKT_CONTROL64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INPUT_CONTROL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_BASE_ADDR64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_BADDR): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_DOORBELL(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_BAOFF_DBELL): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_SIZE(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT%d_INSTR_FIFO_RSIZE): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	for (i = 0; i < (oct->sriov_info.rings_per_vf); i++) {
		reg = CN23XX_VF_SLI_IQ_INSTR_COUNT64(i);
		len += sprintf(s + len,
			       "\n[%08x] (SLI_PKT_IN_DONE%d_CNTS): %016llx\n",
			       reg, i, (u64)octeon_read_csr64(oct, reg));
	}

	return len;
}

static int cn6xxx_read_csr_reg(char *s, struct octeon_device *oct)
{
	u32 reg;
	int i, len = 0;

	/* PCI  Window Registers */

	len += sprintf(s + len, "\n\t Octeon CSR Registers\n\n");
	reg = CN6XXX_WIN_WR_ADDR_LO;
	len += sprintf(s + len, "\n[%02x] (WIN_WR_ADDR_LO): %08x\n",
		       CN6XXX_WIN_WR_ADDR_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_ADDR_HI;
	len += sprintf(s + len, "[%02x] (WIN_WR_ADDR_HI): %08x\n",
		       CN6XXX_WIN_WR_ADDR_HI, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_RD_ADDR_LO;
	len += sprintf(s + len, "[%02x] (WIN_RD_ADDR_LO): %08x\n",
		       CN6XXX_WIN_RD_ADDR_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_RD_ADDR_HI;
	len += sprintf(s + len, "[%02x] (WIN_RD_ADDR_HI): %08x\n",
		       CN6XXX_WIN_RD_ADDR_HI, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_DATA_LO;
	len += sprintf(s + len, "[%02x] (WIN_WR_DATA_LO): %08x\n",
		       CN6XXX_WIN_WR_DATA_LO, octeon_read_csr(oct, reg));
	reg = CN6XXX_WIN_WR_DATA_HI;
	len += sprintf(s + len, "[%02x] (WIN_WR_DATA_HI): %08x\n",
		       CN6XXX_WIN_WR_DATA_HI, octeon_read_csr(oct, reg));
	len += sprintf(s + len, "[%02x] (WIN_WR_MASK_REG): %08x\n",
		       CN6XXX_WIN_WR_MASK_REG,
		       octeon_read_csr(oct, CN6XXX_WIN_WR_MASK_REG));

	/* PCI  Interrupt Register */
	len += sprintf(s + len, "\n[%x] (INT_ENABLE PORT 0): %08x\n",
		       CN6XXX_SLI_INT_ENB64_PORT0, octeon_read_csr(oct,
						CN6XXX_SLI_INT_ENB64_PORT0));
	len += sprintf(s + len, "\n[%x] (INT_ENABLE PORT 1): %08x\n",
		       CN6XXX_SLI_INT_ENB64_PORT1,
		       octeon_read_csr(oct, CN6XXX_SLI_INT_ENB64_PORT1));
	len += sprintf(s + len, "[%x] (INT_SUM): %08x\n", CN6XXX_SLI_INT_SUM64,
		       octeon_read_csr(oct, CN6XXX_SLI_INT_SUM64));

	/* PCI  Output queue registers */
	for (i = 0; i < oct->num_oqs; i++) {
		reg = CN6XXX_SLI_OQ_PKTS_SENT(i);
		len += sprintf(s + len, "\n[%x] (PKTS_SENT_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
		reg = CN6XXX_SLI_OQ_PKTS_CREDIT(i);
		len += sprintf(s + len, "[%x] (PKT_CREDITS_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
	}
	reg = CN6XXX_SLI_OQ_INT_LEVEL_PKTS;
	len += sprintf(s + len, "\n[%x] (PKTS_SENT_INT_LEVEL): %08x\n",
		       reg, octeon_read_csr(oct, reg));
	reg = CN6XXX_SLI_OQ_INT_LEVEL_TIME;
	len += sprintf(s + len, "[%x] (PKTS_SENT_TIME): %08x\n",
		       reg, octeon_read_csr(oct, reg));

	/* PCI  Input queue registers */
	for (i = 0; i <= 3; i++) {
		u32 reg;

		reg = CN6XXX_SLI_IQ_DOORBELL(i);
		len += sprintf(s + len, "\n[%x] (INSTR_DOORBELL_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
		reg = CN6XXX_SLI_IQ_INSTR_COUNT(i);
		len += sprintf(s + len, "[%x] (INSTR_COUNT_%d): %08x\n",
			       reg, i, octeon_read_csr(oct, reg));
	}

	/* PCI  DMA registers */

	len += sprintf(s + len, "\n[%x] (DMA_CNT_0): %08x\n",
		       CN6XXX_DMA_CNT(0),
		       octeon_read_csr(oct, CN6XXX_DMA_CNT(0)));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(0);
	len += sprintf(s + len, "[%x] (DMA_INT_LEV_0): %08x\n",
		       CN6XXX_DMA_PKT_INT_LEVEL(0), octeon_read_csr(oct, reg));
	reg = CN6XXX_DMA_TIME_INT_LEVEL(0);
	len += sprintf(s + len, "[%x] (DMA_TIME_0): %08x\n",
		       CN6XXX_DMA_TIME_INT_LEVEL(0),
		       octeon_read_csr(oct, reg));

	len += sprintf(s + len, "\n[%x] (DMA_CNT_1): %08x\n",
		       CN6XXX_DMA_CNT(1),
		       octeon_read_csr(oct, CN6XXX_DMA_CNT(1)));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(1);
	len += sprintf(s + len, "[%x] (DMA_INT_LEV_1): %08x\n",
		       CN6XXX_DMA_PKT_INT_LEVEL(1),
		       octeon_read_csr(oct, reg));
	reg = CN6XXX_DMA_PKT_INT_LEVEL(1);
	len += sprintf(s + len, "[%x] (DMA_TIME_1): %08x\n",
		       CN6XXX_DMA_TIME_INT_LEVEL(1),
		       octeon_read_csr(oct, reg));

	/* PCI  Index registers */

	len += sprintf(s + len, "\n");

	for (i = 0; i < 16; i++) {
		reg = lio_pci_readq(oct, CN6XXX_BAR1_REG(i, oct->pcie_port));
		len += sprintf(s + len, "[%llx] (BAR1_INDEX_%02d): %08x\n",
			       CN6XXX_BAR1_REG(i, oct->pcie_port), i, reg);
	}

	return len;
}

static int cn6xxx_read_config_reg(char *s, struct octeon_device *oct)
{
	u32 val;
	int i, len = 0;

	/* PCI CONFIG Registers */

	len += sprintf(s + len,
		       "\n\t Octeon Config space Registers\n\n");

	for (i = 0; i <= 13; i++) {
		pci_read_config_dword(oct->pci_dev, (i * 4), &val);
		len += sprintf(s + len, "[0x%x] (Config[%d]): 0x%08x\n",
			       (i * 4), i, val);
	}

	for (i = 30; i <= 34; i++) {
		pci_read_config_dword(oct->pci_dev, (i * 4), &val);
		len += sprintf(s + len, "[0x%x] (Config[%d]): 0x%08x\n",
			       (i * 4), i, val);
	}

	return len;
}

/*  Return register dump user app.  */
static void lio_get_regs(struct net_device *dev,
			 struct ethtool_regs *regs, void *regbuf)
{
	struct lio *lio = GET_LIO(dev);
	int len = 0;
	struct octeon_device *oct = lio->oct_dev;

	regs->version = OCT_ETHTOOL_REGSVER;

	switch (oct->chip_id) {
	case OCTEON_CN23XX_PF_VID:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN_23XX);
		len += cn23xx_read_csr_reg(regbuf + len, oct);
		break;
	case OCTEON_CN23XX_VF_VID:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN_23XX_VF);
		len += cn23xx_vf_read_csr_reg(regbuf + len, oct);
		break;
	case OCTEON_CN68XX:
	case OCTEON_CN66XX:
		memset(regbuf, 0, OCT_ETHTOOL_REGDUMP_LEN);
		len += cn6xxx_read_csr_reg(regbuf + len, oct);
		len += cn6xxx_read_config_reg(regbuf + len, oct);
		break;
	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown chipid: %d\n",
			__func__, oct->chip_id);
	}
}

static u32 lio_get_priv_flags(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	return lio->oct_dev->priv_flags;
}

static int lio_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct lio *lio = GET_LIO(netdev);
	bool intr_by_tx_bytes = !!(flags & (0x1 << OCT_PRIV_FLAG_TX_BYTES));

	lio_set_priv_flag(lio->oct_dev, OCT_PRIV_FLAG_TX_BYTES,
			  intr_by_tx_bytes);
	return 0;
}

static int lio_get_fecparam(struct net_device *netdev,
			    struct ethtool_fecparam *fec)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	fec->active_fec = ETHTOOL_FEC_NONE;
	fec->fec = ETHTOOL_FEC_NONE;

	if (oct->subsystem_id == OCTEON_CN2350_25GB_SUBSYS_ID ||
	    oct->subsystem_id == OCTEON_CN2360_25GB_SUBSYS_ID) {
		if (oct->no_speed_setting == 1)
			return 0;

		liquidio_get_fec(lio);
		fec->fec = (ETHTOOL_FEC_RS | ETHTOOL_FEC_OFF);
		if (oct->props[lio->ifidx].fec == 1)
			fec->active_fec = ETHTOOL_FEC_RS;
		else
			fec->active_fec = ETHTOOL_FEC_OFF;
	}

	return 0;
}

static int lio_set_fecparam(struct net_device *netdev,
			    struct ethtool_fecparam *fec)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	if (oct->subsystem_id == OCTEON_CN2350_25GB_SUBSYS_ID ||
	    oct->subsystem_id == OCTEON_CN2360_25GB_SUBSYS_ID) {
		if (oct->no_speed_setting == 1)
			return -EOPNOTSUPP;

		if (fec->fec & ETHTOOL_FEC_OFF)
			liquidio_set_fec(lio, 0);
		else if (fec->fec & ETHTOOL_FEC_RS)
			liquidio_set_fec(lio, 1);
		else
			return -EOPNOTSUPP;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

#define LIO_ETHTOOL_COALESCE	(ETHTOOL_COALESCE_RX_USECS |		\
				 ETHTOOL_COALESCE_MAX_FRAMES |		\
				 ETHTOOL_COALESCE_USE_ADAPTIVE |	\
				 ETHTOOL_COALESCE_RX_MAX_FRAMES_LOW |	\
				 ETHTOOL_COALESCE_TX_MAX_FRAMES_LOW |	\
				 ETHTOOL_COALESCE_RX_MAX_FRAMES_HIGH |	\
				 ETHTOOL_COALESCE_TX_MAX_FRAMES_HIGH |	\
				 ETHTOOL_COALESCE_PKT_RATE_RX_USECS)

static const struct ethtool_ops lio_ethtool_ops = {
	.supported_coalesce_params = LIO_ETHTOOL_COALESCE,
	.get_link_ksettings	= lio_get_link_ksettings,
	.set_link_ksettings	= lio_set_link_ksettings,
	.get_fecparam		= lio_get_fecparam,
	.set_fecparam		= lio_set_fecparam,
	.get_link		= ethtool_op_get_link,
	.get_drvinfo		= lio_get_drvinfo,
	.get_ringparam		= lio_ethtool_get_ringparam,
	.set_ringparam		= lio_ethtool_set_ringparam,
	.get_channels		= lio_ethtool_get_channels,
	.set_channels		= lio_ethtool_set_channels,
	.set_phys_id		= lio_set_phys_id,
	.get_eeprom_len		= lio_get_eeprom_len,
	.get_eeprom		= lio_get_eeprom,
	.get_strings		= lio_get_strings,
	.get_ethtool_stats	= lio_get_ethtool_stats,
	.get_pauseparam		= lio_get_pauseparam,
	.set_pauseparam		= lio_set_pauseparam,
	.get_regs_len		= lio_get_regs_len,
	.get_regs		= lio_get_regs,
	.get_msglevel		= lio_get_msglevel,
	.set_msglevel		= lio_set_msglevel,
	.get_sset_count		= lio_get_sset_count,
	.get_coalesce		= lio_get_intr_coalesce,
	.set_coalesce		= lio_set_intr_coalesce,
	.get_priv_flags		= lio_get_priv_flags,
	.set_priv_flags		= lio_set_priv_flags,
	.get_ts_info		= lio_get_ts_info,
};

static const struct ethtool_ops lio_vf_ethtool_ops = {
	.supported_coalesce_params = LIO_ETHTOOL_COALESCE,
	.get_link_ksettings	= lio_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_drvinfo		= lio_get_vf_drvinfo,
	.get_ringparam		= lio_ethtool_get_ringparam,
	.set_ringparam          = lio_ethtool_set_ringparam,
	.get_channels		= lio_ethtool_get_channels,
	.set_channels		= lio_ethtool_set_channels,
	.get_strings		= lio_vf_get_strings,
	.get_ethtool_stats	= lio_vf_get_ethtool_stats,
	.get_regs_len		= lio_get_regs_len,
	.get_regs		= lio_get_regs,
	.get_msglevel		= lio_get_msglevel,
	.set_msglevel		= lio_vf_set_msglevel,
	.get_sset_count		= lio_vf_get_sset_count,
	.get_coalesce		= lio_get_intr_coalesce,
	.set_coalesce		= lio_set_intr_coalesce,
	.get_priv_flags		= lio_get_priv_flags,
	.set_priv_flags		= lio_set_priv_flags,
	.get_ts_info		= lio_get_ts_info,
};

void liquidio_set_ethtool_ops(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	if (OCTEON_CN23XX_VF(oct))
		netdev->ethtool_ops = &lio_vf_ethtool_ops;
	else
		netdev->ethtool_ops = &lio_ethtool_ops;
}

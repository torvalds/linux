#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/dmapool.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>


#include "qlge.h"

struct ql_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define QL_SIZEOF(m) FIELD_SIZEOF(struct ql_adapter, m)
#define QL_OFF(m) offsetof(struct ql_adapter, m)

static const struct ql_stats ql_gstrings_stats[] = {
	{"tx_pkts", QL_SIZEOF(nic_stats.tx_pkts), QL_OFF(nic_stats.tx_pkts)},
	{"tx_bytes", QL_SIZEOF(nic_stats.tx_bytes), QL_OFF(nic_stats.tx_bytes)},
	{"tx_mcast_pkts", QL_SIZEOF(nic_stats.tx_mcast_pkts),
					QL_OFF(nic_stats.tx_mcast_pkts)},
	{"tx_bcast_pkts", QL_SIZEOF(nic_stats.tx_bcast_pkts),
					QL_OFF(nic_stats.tx_bcast_pkts)},
	{"tx_ucast_pkts", QL_SIZEOF(nic_stats.tx_ucast_pkts),
					QL_OFF(nic_stats.tx_ucast_pkts)},
	{"tx_ctl_pkts", QL_SIZEOF(nic_stats.tx_ctl_pkts),
					QL_OFF(nic_stats.tx_ctl_pkts)},
	{"tx_pause_pkts", QL_SIZEOF(nic_stats.tx_pause_pkts),
					QL_OFF(nic_stats.tx_pause_pkts)},
	{"tx_64_pkts", QL_SIZEOF(nic_stats.tx_64_pkt),
					QL_OFF(nic_stats.tx_64_pkt)},
	{"tx_65_to_127_pkts", QL_SIZEOF(nic_stats.tx_65_to_127_pkt),
					QL_OFF(nic_stats.tx_65_to_127_pkt)},
	{"tx_128_to_255_pkts", QL_SIZEOF(nic_stats.tx_128_to_255_pkt),
					QL_OFF(nic_stats.tx_128_to_255_pkt)},
	{"tx_256_511_pkts", QL_SIZEOF(nic_stats.tx_256_511_pkt),
					QL_OFF(nic_stats.tx_256_511_pkt)},
	{"tx_512_to_1023_pkts",	QL_SIZEOF(nic_stats.tx_512_to_1023_pkt),
					QL_OFF(nic_stats.tx_512_to_1023_pkt)},
	{"tx_1024_to_1518_pkts", QL_SIZEOF(nic_stats.tx_1024_to_1518_pkt),
					QL_OFF(nic_stats.tx_1024_to_1518_pkt)},
	{"tx_1519_to_max_pkts",	QL_SIZEOF(nic_stats.tx_1519_to_max_pkt),
					QL_OFF(nic_stats.tx_1519_to_max_pkt)},
	{"tx_undersize_pkts", QL_SIZEOF(nic_stats.tx_undersize_pkt),
					QL_OFF(nic_stats.tx_undersize_pkt)},
	{"tx_oversize_pkts", QL_SIZEOF(nic_stats.tx_oversize_pkt),
					QL_OFF(nic_stats.tx_oversize_pkt)},
	{"rx_bytes", QL_SIZEOF(nic_stats.rx_bytes), QL_OFF(nic_stats.rx_bytes)},
	{"rx_bytes_ok",	QL_SIZEOF(nic_stats.rx_bytes_ok),
					QL_OFF(nic_stats.rx_bytes_ok)},
	{"rx_pkts", QL_SIZEOF(nic_stats.rx_pkts), QL_OFF(nic_stats.rx_pkts)},
	{"rx_pkts_ok", QL_SIZEOF(nic_stats.rx_pkts_ok),
					QL_OFF(nic_stats.rx_pkts_ok)},
	{"rx_bcast_pkts", QL_SIZEOF(nic_stats.rx_bcast_pkts),
					QL_OFF(nic_stats.rx_bcast_pkts)},
	{"rx_mcast_pkts", QL_SIZEOF(nic_stats.rx_mcast_pkts),
					QL_OFF(nic_stats.rx_mcast_pkts)},
	{"rx_ucast_pkts", QL_SIZEOF(nic_stats.rx_ucast_pkts),
					QL_OFF(nic_stats.rx_ucast_pkts)},
	{"rx_undersize_pkts", QL_SIZEOF(nic_stats.rx_undersize_pkts),
					QL_OFF(nic_stats.rx_undersize_pkts)},
	{"rx_oversize_pkts", QL_SIZEOF(nic_stats.rx_oversize_pkts),
					QL_OFF(nic_stats.rx_oversize_pkts)},
	{"rx_jabber_pkts", QL_SIZEOF(nic_stats.rx_jabber_pkts),
					QL_OFF(nic_stats.rx_jabber_pkts)},
	{"rx_undersize_fcerr_pkts",
		QL_SIZEOF(nic_stats.rx_undersize_fcerr_pkts),
				QL_OFF(nic_stats.rx_undersize_fcerr_pkts)},
	{"rx_drop_events", QL_SIZEOF(nic_stats.rx_drop_events),
					QL_OFF(nic_stats.rx_drop_events)},
	{"rx_fcerr_pkts", QL_SIZEOF(nic_stats.rx_fcerr_pkts),
					QL_OFF(nic_stats.rx_fcerr_pkts)},
	{"rx_align_err", QL_SIZEOF(nic_stats.rx_align_err),
					QL_OFF(nic_stats.rx_align_err)},
	{"rx_symbol_err", QL_SIZEOF(nic_stats.rx_symbol_err),
					QL_OFF(nic_stats.rx_symbol_err)},
	{"rx_mac_err", QL_SIZEOF(nic_stats.rx_mac_err),
					QL_OFF(nic_stats.rx_mac_err)},
	{"rx_ctl_pkts",	QL_SIZEOF(nic_stats.rx_ctl_pkts),
					QL_OFF(nic_stats.rx_ctl_pkts)},
	{"rx_pause_pkts", QL_SIZEOF(nic_stats.rx_pause_pkts),
					QL_OFF(nic_stats.rx_pause_pkts)},
	{"rx_64_pkts", QL_SIZEOF(nic_stats.rx_64_pkts),
					QL_OFF(nic_stats.rx_64_pkts)},
	{"rx_65_to_127_pkts", QL_SIZEOF(nic_stats.rx_65_to_127_pkts),
					QL_OFF(nic_stats.rx_65_to_127_pkts)},
	{"rx_128_255_pkts", QL_SIZEOF(nic_stats.rx_128_255_pkts),
					QL_OFF(nic_stats.rx_128_255_pkts)},
	{"rx_256_511_pkts", QL_SIZEOF(nic_stats.rx_256_511_pkts),
					QL_OFF(nic_stats.rx_256_511_pkts)},
	{"rx_512_to_1023_pkts",	QL_SIZEOF(nic_stats.rx_512_to_1023_pkts),
					QL_OFF(nic_stats.rx_512_to_1023_pkts)},
	{"rx_1024_to_1518_pkts", QL_SIZEOF(nic_stats.rx_1024_to_1518_pkts),
					QL_OFF(nic_stats.rx_1024_to_1518_pkts)},
	{"rx_1519_to_max_pkts",	QL_SIZEOF(nic_stats.rx_1519_to_max_pkts),
					QL_OFF(nic_stats.rx_1519_to_max_pkts)},
	{"rx_len_err_pkts", QL_SIZEOF(nic_stats.rx_len_err_pkts),
					QL_OFF(nic_stats.rx_len_err_pkts)},
	{"rx_code_err",	QL_SIZEOF(nic_stats.rx_code_err),
					QL_OFF(nic_stats.rx_code_err)},
	{"rx_oversize_err", QL_SIZEOF(nic_stats.rx_oversize_err),
					QL_OFF(nic_stats.rx_oversize_err)},
	{"rx_undersize_err", QL_SIZEOF(nic_stats.rx_undersize_err),
					QL_OFF(nic_stats.rx_undersize_err)},
	{"rx_preamble_err", QL_SIZEOF(nic_stats.rx_preamble_err),
					QL_OFF(nic_stats.rx_preamble_err)},
	{"rx_frame_len_err", QL_SIZEOF(nic_stats.rx_frame_len_err),
					QL_OFF(nic_stats.rx_frame_len_err)},
	{"rx_crc_err", QL_SIZEOF(nic_stats.rx_crc_err),
					QL_OFF(nic_stats.rx_crc_err)},
	{"rx_err_count", QL_SIZEOF(nic_stats.rx_err_count),
					QL_OFF(nic_stats.rx_err_count)},
	{"tx_cbfc_pause_frames0", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames0),
				QL_OFF(nic_stats.tx_cbfc_pause_frames0)},
	{"tx_cbfc_pause_frames1", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames1),
				QL_OFF(nic_stats.tx_cbfc_pause_frames1)},
	{"tx_cbfc_pause_frames2", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames2),
				QL_OFF(nic_stats.tx_cbfc_pause_frames2)},
	{"tx_cbfc_pause_frames3", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames3),
				QL_OFF(nic_stats.tx_cbfc_pause_frames3)},
	{"tx_cbfc_pause_frames4", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames4),
				QL_OFF(nic_stats.tx_cbfc_pause_frames4)},
	{"tx_cbfc_pause_frames5", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames5),
				QL_OFF(nic_stats.tx_cbfc_pause_frames5)},
	{"tx_cbfc_pause_frames6", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames6),
				QL_OFF(nic_stats.tx_cbfc_pause_frames6)},
	{"tx_cbfc_pause_frames7", QL_SIZEOF(nic_stats.tx_cbfc_pause_frames7),
				QL_OFF(nic_stats.tx_cbfc_pause_frames7)},
	{"rx_cbfc_pause_frames0", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames0),
				QL_OFF(nic_stats.rx_cbfc_pause_frames0)},
	{"rx_cbfc_pause_frames1", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames1),
				QL_OFF(nic_stats.rx_cbfc_pause_frames1)},
	{"rx_cbfc_pause_frames2", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames2),
				QL_OFF(nic_stats.rx_cbfc_pause_frames2)},
	{"rx_cbfc_pause_frames3", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames3),
				QL_OFF(nic_stats.rx_cbfc_pause_frames3)},
	{"rx_cbfc_pause_frames4", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames4),
				QL_OFF(nic_stats.rx_cbfc_pause_frames4)},
	{"rx_cbfc_pause_frames5", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames5),
				QL_OFF(nic_stats.rx_cbfc_pause_frames5)},
	{"rx_cbfc_pause_frames6", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames6),
				QL_OFF(nic_stats.rx_cbfc_pause_frames6)},
	{"rx_cbfc_pause_frames7", QL_SIZEOF(nic_stats.rx_cbfc_pause_frames7),
				QL_OFF(nic_stats.rx_cbfc_pause_frames7)},
	{"rx_nic_fifo_drop", QL_SIZEOF(nic_stats.rx_nic_fifo_drop),
					QL_OFF(nic_stats.rx_nic_fifo_drop)},
};

static const char ql_gstrings_test[][ETH_GSTRING_LEN] = {
	"Loopback test  (offline)"
};
#define QLGE_TEST_LEN (sizeof(ql_gstrings_test) / ETH_GSTRING_LEN)
#define QLGE_STATS_LEN ARRAY_SIZE(ql_gstrings_stats)
#define QLGE_RCV_MAC_ERR_STATS	7

static int ql_update_ring_coalescing(struct ql_adapter *qdev)
{
	int i, status = 0;
	struct rx_ring *rx_ring;
	struct cqicb *cqicb;

	if (!netif_running(qdev->ndev))
		return status;

	/* Skip the default queue, and update the outbound handler
	 * queues if they changed.
	 */
	cqicb = (struct cqicb *)&qdev->rx_ring[qdev->rss_ring_count];
	if (le16_to_cpu(cqicb->irq_delay) != qdev->tx_coalesce_usecs ||
		le16_to_cpu(cqicb->pkt_delay) !=
				qdev->tx_max_coalesced_frames) {
		for (i = qdev->rss_ring_count; i < qdev->rx_ring_count; i++) {
			rx_ring = &qdev->rx_ring[i];
			cqicb = (struct cqicb *)rx_ring;
			cqicb->irq_delay = cpu_to_le16(qdev->tx_coalesce_usecs);
			cqicb->pkt_delay =
			    cpu_to_le16(qdev->tx_max_coalesced_frames);
			cqicb->flags = FLAGS_LI;
			status = ql_write_cfg(qdev, cqicb, sizeof(*cqicb),
						CFG_LCQ, rx_ring->cq_id);
			if (status) {
				netif_err(qdev, ifup, qdev->ndev,
					  "Failed to load CQICB.\n");
				goto exit;
			}
		}
	}

	/* Update the inbound (RSS) handler queues if they changed. */
	cqicb = (struct cqicb *)&qdev->rx_ring[0];
	if (le16_to_cpu(cqicb->irq_delay) != qdev->rx_coalesce_usecs ||
		le16_to_cpu(cqicb->pkt_delay) !=
					qdev->rx_max_coalesced_frames) {
		for (i = 0; i < qdev->rss_ring_count; i++, rx_ring++) {
			rx_ring = &qdev->rx_ring[i];
			cqicb = (struct cqicb *)rx_ring;
			cqicb->irq_delay = cpu_to_le16(qdev->rx_coalesce_usecs);
			cqicb->pkt_delay =
			    cpu_to_le16(qdev->rx_max_coalesced_frames);
			cqicb->flags = FLAGS_LI;
			status = ql_write_cfg(qdev, cqicb, sizeof(*cqicb),
						CFG_LCQ, rx_ring->cq_id);
			if (status) {
				netif_err(qdev, ifup, qdev->ndev,
					  "Failed to load CQICB.\n");
				goto exit;
			}
		}
	}
exit:
	return status;
}

static void ql_update_stats(struct ql_adapter *qdev)
{
	u32 i;
	u64 data;
	u64 *iter = &qdev->nic_stats.tx_pkts;

	spin_lock(&qdev->stats_lock);
	if (ql_sem_spinlock(qdev, qdev->xg_sem_mask)) {
			netif_err(qdev, drv, qdev->ndev,
				  "Couldn't get xgmac sem.\n");
		goto quit;
	}
	/*
	 * Get TX statistics.
	 */
	for (i = 0x200; i < 0x280; i += 8) {
		if (ql_read_xgmac_reg64(qdev, i, &data)) {
			netif_err(qdev, drv, qdev->ndev,
				  "Error reading status register 0x%.04x.\n",
				  i);
			goto end;
		} else
			*iter = data;
		iter++;
	}

	/*
	 * Get RX statistics.
	 */
	for (i = 0x300; i < 0x3d0; i += 8) {
		if (ql_read_xgmac_reg64(qdev, i, &data)) {
			netif_err(qdev, drv, qdev->ndev,
				  "Error reading status register 0x%.04x.\n",
				  i);
			goto end;
		} else
			*iter = data;
		iter++;
	}

	/* Update receive mac error statistics */
	iter += QLGE_RCV_MAC_ERR_STATS;

	/*
	 * Get Per-priority TX pause frame counter statistics.
	 */
	for (i = 0x500; i < 0x540; i += 8) {
		if (ql_read_xgmac_reg64(qdev, i, &data)) {
			netif_err(qdev, drv, qdev->ndev,
				  "Error reading status register 0x%.04x.\n",
				  i);
			goto end;
		} else
			*iter = data;
		iter++;
	}

	/*
	 * Get Per-priority RX pause frame counter statistics.
	 */
	for (i = 0x568; i < 0x5a8; i += 8) {
		if (ql_read_xgmac_reg64(qdev, i, &data)) {
			netif_err(qdev, drv, qdev->ndev,
				  "Error reading status register 0x%.04x.\n",
				  i);
			goto end;
		} else
			*iter = data;
		iter++;
	}

	/*
	 * Get RX NIC FIFO DROP statistics.
	 */
	if (ql_read_xgmac_reg64(qdev, 0x5b8, &data)) {
		netif_err(qdev, drv, qdev->ndev,
			  "Error reading status register 0x%.04x.\n", i);
		goto end;
	} else
		*iter = data;
end:
	ql_sem_unlock(qdev, qdev->xg_sem_mask);
quit:
	spin_unlock(&qdev->stats_lock);

	QL_DUMP_STAT(qdev);
}

static void ql_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	int index;
	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(buf, *ql_gstrings_test, QLGE_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (index = 0; index < QLGE_STATS_LEN; index++) {
			memcpy(buf + index * ETH_GSTRING_LEN,
				ql_gstrings_stats[index].stat_string,
				ETH_GSTRING_LEN);
		}
		break;
	}
}

static int ql_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return QLGE_TEST_LEN;
	case ETH_SS_STATS:
		return QLGE_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void
ql_get_ethtool_stats(struct net_device *ndev,
		     struct ethtool_stats *stats, u64 *data)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	int index, length;

	length = QLGE_STATS_LEN;
	ql_update_stats(qdev);

	for (index = 0; index < length; index++) {
		char *p = (char *)qdev +
			ql_gstrings_stats[index].stat_offset;
		*data++ = (ql_gstrings_stats[index].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : (*(u32 *)p);
	}
}

static int ql_get_settings(struct net_device *ndev,
			      struct ethtool_cmd *ecmd)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	ecmd->supported = SUPPORTED_10000baseT_Full;
	ecmd->advertising = ADVERTISED_10000baseT_Full;
	ecmd->transceiver = XCVR_EXTERNAL;
	if ((qdev->link_status & STS_LINK_TYPE_MASK) ==
				STS_LINK_TYPE_10GBASET) {
		ecmd->supported |= (SUPPORTED_TP | SUPPORTED_Autoneg);
		ecmd->advertising |= (ADVERTISED_TP | ADVERTISED_Autoneg);
		ecmd->port = PORT_TP;
		ecmd->autoneg = AUTONEG_ENABLE;
	} else {
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
	}

	ethtool_cmd_speed_set(ecmd, SPEED_10000);
	ecmd->duplex = DUPLEX_FULL;

	return 0;
}

static void ql_get_drvinfo(struct net_device *ndev,
			   struct ethtool_drvinfo *drvinfo)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	strlcpy(drvinfo->driver, qlge_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, qlge_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "v%d.%d.%d",
		 (qdev->fw_rev_id & 0x00ff0000) >> 16,
		 (qdev->fw_rev_id & 0x0000ff00) >> 8,
		 (qdev->fw_rev_id & 0x000000ff));
	strlcpy(drvinfo->bus_info, pci_name(qdev->pdev),
		sizeof(drvinfo->bus_info));
}

static void ql_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	unsigned short ssys_dev = qdev->pdev->subsystem_device;

	/* WOL is only supported for mezz card. */
	if (ssys_dev == QLGE_MEZZ_SSYS_ID_068 ||
			ssys_dev == QLGE_MEZZ_SSYS_ID_180) {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = qdev->wol;
	}
}

static int ql_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	unsigned short ssys_dev = qdev->pdev->subsystem_device;

	/* WOL is only supported for mezz card. */
	if (ssys_dev != QLGE_MEZZ_SSYS_ID_068 &&
			ssys_dev != QLGE_MEZZ_SSYS_ID_180) {
		netif_info(qdev, drv, qdev->ndev,
				"WOL is only supported for mezz card\n");
		return -EOPNOTSUPP;
	}
	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;
	qdev->wol = wol->wolopts;

	netif_info(qdev, drv, qdev->ndev, "Set wol option 0x%x\n", qdev->wol);
	return 0;
}

static int ql_set_phys_id(struct net_device *ndev,
			  enum ethtool_phys_id_state state)

{
	struct ql_adapter *qdev = netdev_priv(ndev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		/* Save the current LED settings */
		if (ql_mb_get_led_cfg(qdev))
			return -EIO;

		/* Start blinking */
		ql_mb_set_led_cfg(qdev, QL_LED_BLINK);
		return 0;

	case ETHTOOL_ID_INACTIVE:
		/* Restore LED settings */
		if (ql_mb_set_led_cfg(qdev, qdev->led_config))
			return -EIO;
		return 0;

	default:
		return -EINVAL;
	}
}

static int ql_start_loopback(struct ql_adapter *qdev)
{
	if (netif_carrier_ok(qdev->ndev)) {
		set_bit(QL_LB_LINK_UP, &qdev->flags);
		netif_carrier_off(qdev->ndev);
	} else
		clear_bit(QL_LB_LINK_UP, &qdev->flags);
	qdev->link_config |= CFG_LOOPBACK_PCS;
	return ql_mb_set_port_cfg(qdev);
}

static void ql_stop_loopback(struct ql_adapter *qdev)
{
	qdev->link_config &= ~CFG_LOOPBACK_PCS;
	ql_mb_set_port_cfg(qdev);
	if (test_bit(QL_LB_LINK_UP, &qdev->flags)) {
		netif_carrier_on(qdev->ndev);
		clear_bit(QL_LB_LINK_UP, &qdev->flags);
	}
}

static void ql_create_lb_frame(struct sk_buff *skb,
					unsigned int frame_size)
{
	memset(skb->data, 0xFF, frame_size);
	frame_size &= ~1;
	memset(&skb->data[frame_size / 2], 0xAA, frame_size / 2 - 1);
	memset(&skb->data[frame_size / 2 + 10], 0xBE, 1);
	memset(&skb->data[frame_size / 2 + 12], 0xAF, 1);
}

void ql_check_lb_frame(struct ql_adapter *qdev,
					struct sk_buff *skb)
{
	unsigned int frame_size = skb->len;

	if ((*(skb->data + 3) == 0xFF) &&
		(*(skb->data + frame_size / 2 + 10) == 0xBE) &&
		(*(skb->data + frame_size / 2 + 12) == 0xAF)) {
			atomic_dec(&qdev->lb_count);
			return;
	}
}

static int ql_run_loopback_test(struct ql_adapter *qdev)
{
	int i;
	netdev_tx_t rc;
	struct sk_buff *skb;
	unsigned int size = SMALL_BUF_MAP_SIZE;

	for (i = 0; i < 64; i++) {
		skb = netdev_alloc_skb(qdev->ndev, size);
		if (!skb)
			return -ENOMEM;

		skb->queue_mapping = 0;
		skb_put(skb, size);
		ql_create_lb_frame(skb, size);
		rc = ql_lb_send(skb, qdev->ndev);
		if (rc != NETDEV_TX_OK)
			return -EPIPE;
		atomic_inc(&qdev->lb_count);
	}
	/* Give queue time to settle before testing results. */
	msleep(2);
	ql_clean_lb_rx_ring(&qdev->rx_ring[0], 128);
	return atomic_read(&qdev->lb_count) ? -EIO : 0;
}

static int ql_loopback_test(struct ql_adapter *qdev, u64 *data)
{
	*data = ql_start_loopback(qdev);
	if (*data)
		goto out;
	*data = ql_run_loopback_test(qdev);
out:
	ql_stop_loopback(qdev);
	return *data;
}

static void ql_self_test(struct net_device *ndev,
				struct ethtool_test *eth_test, u64 *data)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	memset(data, 0, sizeof(u64) * QLGE_TEST_LEN);

	if (netif_running(ndev)) {
		set_bit(QL_SELFTEST, &qdev->flags);
		if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
			/* Offline tests */
			if (ql_loopback_test(qdev, &data[0]))
				eth_test->flags |= ETH_TEST_FL_FAILED;

		} else {
			/* Online tests */
			data[0] = 0;
		}
		clear_bit(QL_SELFTEST, &qdev->flags);
		/* Give link time to come up after
		 * port configuration changes.
		 */
		msleep_interruptible(4 * 1000);
	} else {
		netif_err(qdev, drv, qdev->ndev,
			  "is down, Loopback test will fail.\n");
		eth_test->flags |= ETH_TEST_FL_FAILED;
	}
}

static int ql_get_regs_len(struct net_device *ndev)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	if (!test_bit(QL_FRC_COREDUMP, &qdev->flags))
		return sizeof(struct ql_mpi_coredump);
	else
		return sizeof(struct ql_reg_dump);
}

static void ql_get_regs(struct net_device *ndev,
			struct ethtool_regs *regs, void *p)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	ql_get_dump(qdev, p);
	qdev->core_is_dumped = 0;
	if (!test_bit(QL_FRC_COREDUMP, &qdev->flags))
		regs->len = sizeof(struct ql_mpi_coredump);
	else
		regs->len = sizeof(struct ql_reg_dump);
}

static int ql_get_coalesce(struct net_device *dev, struct ethtool_coalesce *c)
{
	struct ql_adapter *qdev = netdev_priv(dev);

	c->rx_coalesce_usecs = qdev->rx_coalesce_usecs;
	c->tx_coalesce_usecs = qdev->tx_coalesce_usecs;

	/* This chip coalesces as follows:
	 * If a packet arrives, hold off interrupts until
	 * cqicb->int_delay expires, but if no other packets arrive don't
	 * wait longer than cqicb->pkt_int_delay. But ethtool doesn't use a
	 * timer to coalesce on a frame basis.  So, we have to take ethtool's
	 * max_coalesced_frames value and convert it to a delay in microseconds.
	 * We do this by using a basic thoughput of 1,000,000 frames per
	 * second @ (1024 bytes).  This means one frame per usec. So it's a
	 * simple one to one ratio.
	 */
	c->rx_max_coalesced_frames = qdev->rx_max_coalesced_frames;
	c->tx_max_coalesced_frames = qdev->tx_max_coalesced_frames;

	return 0;
}

static int ql_set_coalesce(struct net_device *ndev, struct ethtool_coalesce *c)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	/* Validate user parameters. */
	if (c->rx_coalesce_usecs > qdev->rx_ring_size / 2)
		return -EINVAL;
       /* Don't wait more than 10 usec. */
	if (c->rx_max_coalesced_frames > MAX_INTER_FRAME_WAIT)
		return -EINVAL;
	if (c->tx_coalesce_usecs > qdev->tx_ring_size / 2)
		return -EINVAL;
	if (c->tx_max_coalesced_frames > MAX_INTER_FRAME_WAIT)
		return -EINVAL;

	/* Verify a change took place before updating the hardware. */
	if (qdev->rx_coalesce_usecs == c->rx_coalesce_usecs &&
	    qdev->tx_coalesce_usecs == c->tx_coalesce_usecs &&
	    qdev->rx_max_coalesced_frames == c->rx_max_coalesced_frames &&
	    qdev->tx_max_coalesced_frames == c->tx_max_coalesced_frames)
		return 0;

	qdev->rx_coalesce_usecs = c->rx_coalesce_usecs;
	qdev->tx_coalesce_usecs = c->tx_coalesce_usecs;
	qdev->rx_max_coalesced_frames = c->rx_max_coalesced_frames;
	qdev->tx_max_coalesced_frames = c->tx_max_coalesced_frames;

	return ql_update_ring_coalescing(qdev);
}

static void ql_get_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *pause)
{
	struct ql_adapter *qdev = netdev_priv(netdev);

	ql_mb_get_port_cfg(qdev);
	if (qdev->link_config & CFG_PAUSE_STD) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int ql_set_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *pause)
{
	struct ql_adapter *qdev = netdev_priv(netdev);
	int status = 0;

	if ((pause->rx_pause) && (pause->tx_pause))
		qdev->link_config |= CFG_PAUSE_STD;
	else if (!pause->rx_pause && !pause->tx_pause)
		qdev->link_config &= ~CFG_PAUSE_STD;
	else
		return -EINVAL;

	status = ql_mb_set_port_cfg(qdev);
	return status;
}

static u32 ql_get_msglevel(struct net_device *ndev)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	return qdev->msg_enable;
}

static void ql_set_msglevel(struct net_device *ndev, u32 value)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	qdev->msg_enable = value;
}

const struct ethtool_ops qlge_ethtool_ops = {
	.get_settings = ql_get_settings,
	.get_drvinfo = ql_get_drvinfo,
	.get_wol = ql_get_wol,
	.set_wol = ql_set_wol,
	.get_regs_len	= ql_get_regs_len,
	.get_regs = ql_get_regs,
	.get_msglevel = ql_get_msglevel,
	.set_msglevel = ql_set_msglevel,
	.get_link = ethtool_op_get_link,
	.set_phys_id		 = ql_set_phys_id,
	.self_test		 = ql_self_test,
	.get_pauseparam		 = ql_get_pauseparam,
	.set_pauseparam		 = ql_set_pauseparam,
	.get_coalesce = ql_get_coalesce,
	.set_coalesce = ql_set_coalesce,
	.get_sset_count = ql_get_sset_count,
	.get_strings = ql_get_strings,
	.get_ethtool_stats = ql_get_ethtool_stats,
};


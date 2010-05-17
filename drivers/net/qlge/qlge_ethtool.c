#include <linux/kernel.h>
#include <linux/init.h>
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

static const char ql_gstrings_test[][ETH_GSTRING_LEN] = {
	"Loopback test  (offline)"
};
#define QLGE_TEST_LEN (sizeof(ql_gstrings_test) / ETH_GSTRING_LEN)

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

	return;
}

static char ql_stats_str_arr[][ETH_GSTRING_LEN] = {
	{"tx_pkts"},
	{"tx_bytes"},
	{"tx_mcast_pkts"},
	{"tx_bcast_pkts"},
	{"tx_ucast_pkts"},
	{"tx_ctl_pkts"},
	{"tx_pause_pkts"},
	{"tx_64_pkts"},
	{"tx_65_to_127_pkts"},
	{"tx_128_to_255_pkts"},
	{"tx_256_511_pkts"},
	{"tx_512_to_1023_pkts"},
	{"tx_1024_to_1518_pkts"},
	{"tx_1519_to_max_pkts"},
	{"tx_undersize_pkts"},
	{"tx_oversize_pkts"},
	{"rx_bytes"},
	{"rx_bytes_ok"},
	{"rx_pkts"},
	{"rx_pkts_ok"},
	{"rx_bcast_pkts"},
	{"rx_mcast_pkts"},
	{"rx_ucast_pkts"},
	{"rx_undersize_pkts"},
	{"rx_oversize_pkts"},
	{"rx_jabber_pkts"},
	{"rx_undersize_fcerr_pkts"},
	{"rx_drop_events"},
	{"rx_fcerr_pkts"},
	{"rx_align_err"},
	{"rx_symbol_err"},
	{"rx_mac_err"},
	{"rx_ctl_pkts"},
	{"rx_pause_pkts"},
	{"rx_64_pkts"},
	{"rx_65_to_127_pkts"},
	{"rx_128_255_pkts"},
	{"rx_256_511_pkts"},
	{"rx_512_to_1023_pkts"},
	{"rx_1024_to_1518_pkts"},
	{"rx_1519_to_max_pkts"},
	{"rx_len_err_pkts"},
	{"tx_cbfc_pause_frames0"},
	{"tx_cbfc_pause_frames1"},
	{"tx_cbfc_pause_frames2"},
	{"tx_cbfc_pause_frames3"},
	{"tx_cbfc_pause_frames4"},
	{"tx_cbfc_pause_frames5"},
	{"tx_cbfc_pause_frames6"},
	{"tx_cbfc_pause_frames7"},
	{"rx_cbfc_pause_frames0"},
	{"rx_cbfc_pause_frames1"},
	{"rx_cbfc_pause_frames2"},
	{"rx_cbfc_pause_frames3"},
	{"rx_cbfc_pause_frames4"},
	{"rx_cbfc_pause_frames5"},
	{"rx_cbfc_pause_frames6"},
	{"rx_cbfc_pause_frames7"},
	{"rx_nic_fifo_drop"},
};

static void ql_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, ql_stats_str_arr, sizeof(ql_stats_str_arr));
		break;
	}
}

static int ql_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return QLGE_TEST_LEN;
	case ETH_SS_STATS:
		return ARRAY_SIZE(ql_stats_str_arr);
	default:
		return -EOPNOTSUPP;
	}
}

static void
ql_get_ethtool_stats(struct net_device *ndev,
		     struct ethtool_stats *stats, u64 *data)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	struct nic_stats *s = &qdev->nic_stats;

	ql_update_stats(qdev);

	*data++ = s->tx_pkts;
	*data++ = s->tx_bytes;
	*data++ = s->tx_mcast_pkts;
	*data++ = s->tx_bcast_pkts;
	*data++ = s->tx_ucast_pkts;
	*data++ = s->tx_ctl_pkts;
	*data++ = s->tx_pause_pkts;
	*data++ = s->tx_64_pkt;
	*data++ = s->tx_65_to_127_pkt;
	*data++ = s->tx_128_to_255_pkt;
	*data++ = s->tx_256_511_pkt;
	*data++ = s->tx_512_to_1023_pkt;
	*data++ = s->tx_1024_to_1518_pkt;
	*data++ = s->tx_1519_to_max_pkt;
	*data++ = s->tx_undersize_pkt;
	*data++ = s->tx_oversize_pkt;
	*data++ = s->rx_bytes;
	*data++ = s->rx_bytes_ok;
	*data++ = s->rx_pkts;
	*data++ = s->rx_pkts_ok;
	*data++ = s->rx_bcast_pkts;
	*data++ = s->rx_mcast_pkts;
	*data++ = s->rx_ucast_pkts;
	*data++ = s->rx_undersize_pkts;
	*data++ = s->rx_oversize_pkts;
	*data++ = s->rx_jabber_pkts;
	*data++ = s->rx_undersize_fcerr_pkts;
	*data++ = s->rx_drop_events;
	*data++ = s->rx_fcerr_pkts;
	*data++ = s->rx_align_err;
	*data++ = s->rx_symbol_err;
	*data++ = s->rx_mac_err;
	*data++ = s->rx_ctl_pkts;
	*data++ = s->rx_pause_pkts;
	*data++ = s->rx_64_pkts;
	*data++ = s->rx_65_to_127_pkts;
	*data++ = s->rx_128_255_pkts;
	*data++ = s->rx_256_511_pkts;
	*data++ = s->rx_512_to_1023_pkts;
	*data++ = s->rx_1024_to_1518_pkts;
	*data++ = s->rx_1519_to_max_pkts;
	*data++ = s->rx_len_err_pkts;
	*data++ = s->tx_cbfc_pause_frames0;
	*data++ = s->tx_cbfc_pause_frames1;
	*data++ = s->tx_cbfc_pause_frames2;
	*data++ = s->tx_cbfc_pause_frames3;
	*data++ = s->tx_cbfc_pause_frames4;
	*data++ = s->tx_cbfc_pause_frames5;
	*data++ = s->tx_cbfc_pause_frames6;
	*data++ = s->tx_cbfc_pause_frames7;
	*data++ = s->rx_cbfc_pause_frames0;
	*data++ = s->rx_cbfc_pause_frames1;
	*data++ = s->rx_cbfc_pause_frames2;
	*data++ = s->rx_cbfc_pause_frames3;
	*data++ = s->rx_cbfc_pause_frames4;
	*data++ = s->rx_cbfc_pause_frames5;
	*data++ = s->rx_cbfc_pause_frames6;
	*data++ = s->rx_cbfc_pause_frames7;
	*data++ = s->rx_nic_fifo_drop;
}

static int ql_get_settings(struct net_device *ndev,
			      struct ethtool_cmd *ecmd)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	ecmd->supported = SUPPORTED_10000baseT_Full;
	ecmd->advertising = ADVERTISED_10000baseT_Full;
	ecmd->autoneg = AUTONEG_ENABLE;
	ecmd->transceiver = XCVR_EXTERNAL;
	if ((qdev->link_status & STS_LINK_TYPE_MASK) ==
				STS_LINK_TYPE_10GBASET) {
		ecmd->supported |= (SUPPORTED_TP | SUPPORTED_Autoneg);
		ecmd->advertising |= (ADVERTISED_TP | ADVERTISED_Autoneg);
		ecmd->port = PORT_TP;
	} else {
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
	}

	ecmd->speed = SPEED_10000;
	ecmd->duplex = DUPLEX_FULL;

	return 0;
}

static void ql_get_drvinfo(struct net_device *ndev,
			   struct ethtool_drvinfo *drvinfo)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	strncpy(drvinfo->driver, qlge_driver_name, 32);
	strncpy(drvinfo->version, qlge_driver_version, 32);
	snprintf(drvinfo->fw_version, 32, "v%d.%d.%d",
		 (qdev->fw_rev_id & 0x00ff0000) >> 16,
		 (qdev->fw_rev_id & 0x0000ff00) >> 8,
		 (qdev->fw_rev_id & 0x000000ff));
	strncpy(drvinfo->bus_info, pci_name(qdev->pdev), 32);
	drvinfo->n_stats = 0;
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static void ql_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	/* What we support. */
	wol->supported = WAKE_MAGIC;
	/* What we've currently got set. */
	wol->wolopts = qdev->wol;
}

static int ql_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	int status;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;
	qdev->wol = wol->wolopts;

	netif_info(qdev, drv, qdev->ndev, "Set wol option 0x%x\n", qdev->wol);
	if (!qdev->wol) {
		u32 wol = 0;
		status = ql_mb_wol_mode(qdev, wol);
		netif_err(qdev, drv, qdev->ndev, "WOL %s (wol code 0x%x)\n",
			  status == 0 ? "cleared successfully" : "clear failed",
			  wol);
	}

	return 0;
}

static int ql_phys_id(struct net_device *ndev, u32 data)
{
	struct ql_adapter *qdev = netdev_priv(ndev);
	u32 led_reg, i;
	int status;

	/* Save the current LED settings */
	status = ql_mb_get_led_cfg(qdev);
	if (status)
		return status;
	led_reg = qdev->led_config;

	/* Start blinking the led */
	if (!data || data > 300)
		data = 300;

	for (i = 0; i < (data * 10); i++)
		ql_mb_set_led_cfg(qdev, QL_LED_BLINK);

	/* Restore LED settings */
	status = ql_mb_set_led_cfg(qdev, led_reg);
	if (status)
		return status;

	return 0;
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
	return sizeof(struct ql_reg_dump);
}

static void ql_get_regs(struct net_device *ndev,
			struct ethtool_regs *regs, void *p)
{
	struct ql_adapter *qdev = netdev_priv(ndev);

	ql_gen_reg_dump(qdev, p);
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
	if (status)
		return status;
	return status;
}

static u32 ql_get_rx_csum(struct net_device *netdev)
{
	struct ql_adapter *qdev = netdev_priv(netdev);
	return qdev->rx_csum;
}

static int ql_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct ql_adapter *qdev = netdev_priv(netdev);
	qdev->rx_csum = data;
	return 0;
}

static int ql_set_tso(struct net_device *ndev, uint32_t data)
{

	if (data) {
		ndev->features |= NETIF_F_TSO;
		ndev->features |= NETIF_F_TSO6;
	} else {
		ndev->features &= ~NETIF_F_TSO;
		ndev->features &= ~NETIF_F_TSO6;
	}
	return 0;
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
	.phys_id		 = ql_phys_id,
	.self_test		 = ql_self_test,
	.get_pauseparam		 = ql_get_pauseparam,
	.set_pauseparam		 = ql_set_pauseparam,
	.get_rx_csum = ql_get_rx_csum,
	.set_rx_csum = ql_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ql_set_tso,
	.get_coalesce = ql_get_coalesce,
	.set_coalesce = ql_set_coalesce,
	.get_sset_count = ql_get_sset_count,
	.get_strings = ql_get_strings,
	.get_ethtool_stats = ql_get_ethtool_stats,
};


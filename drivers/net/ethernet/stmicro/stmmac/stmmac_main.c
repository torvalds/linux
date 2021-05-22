// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  This is the driver for the ST MAC 10/100/1000 on-chip Ethernet controllers.
  ST Ethernet IPs are built around a Synopsys IP Core.

	Copyright(C) 2007-2011 STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>

  Documentation available at:
	http://www.stlinux.com
  Support available at:
	https://bugzilla.stlinux.com/
*******************************************************************************/

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/pinctrl/consumer.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif /* CONFIG_DEBUG_FS */
#include <linux/net_tstamp.h>
#include <linux/phylink.h>
#include <linux/udp.h>
#include <net/pkt_cls.h>
#include "stmmac_ptp.h"
#include "stmmac.h"
#include <linux/reset.h>
#include <linux/of_mdio.h>
#include "dwmac1000.h"
#include "dwxgmac2.h"
#include "hwif.h"

#define	STMMAC_ALIGN(x)		ALIGN(ALIGN(x, SMP_CACHE_BYTES), 16)
#define	TSO_MAX_BUFF_SIZE	(SZ_16K - 1)

/* Module parameters */
#define TX_TIMEO	5000
static int watchdog = TX_TIMEO;
module_param(watchdog, int, 0644);
MODULE_PARM_DESC(watchdog, "Transmit timeout in milliseconds (default 5s)");

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Message Level (-1: default, 0: no output, 16: all)");

static int phyaddr = -1;
module_param(phyaddr, int, 0444);
MODULE_PARM_DESC(phyaddr, "Physical device address");

#define STMMAC_TX_THRESH(x)	((x)->dma_tx_size / 4)
#define STMMAC_RX_THRESH(x)	((x)->dma_rx_size / 4)

static int flow_ctrl = FLOW_AUTO;
module_param(flow_ctrl, int, 0644);
MODULE_PARM_DESC(flow_ctrl, "Flow control ability [on/off]");

static int pause = PAUSE_TIME;
module_param(pause, int, 0644);
MODULE_PARM_DESC(pause, "Flow Control Pause Time");

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;
module_param(tc, int, 0644);
MODULE_PARM_DESC(tc, "DMA threshold control value");

#define	DEFAULT_BUFSIZE	1536
static int buf_sz = DEFAULT_BUFSIZE;
module_param(buf_sz, int, 0644);
MODULE_PARM_DESC(buf_sz, "DMA buffer size");

#define	STMMAC_RX_COPYBREAK	256

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
				      NETIF_MSG_LINK | NETIF_MSG_IFUP |
				      NETIF_MSG_IFDOWN | NETIF_MSG_TIMER);

#define STMMAC_DEFAULT_LPI_TIMER	1000
static int eee_timer = STMMAC_DEFAULT_LPI_TIMER;
module_param(eee_timer, int, 0644);
MODULE_PARM_DESC(eee_timer, "LPI tx expiration time in msec");
#define STMMAC_LPI_T(x) (jiffies + usecs_to_jiffies(x))

/* By default the driver will use the ring mode to manage tx and rx descriptors,
 * but allow user to force to use the chain instead of the ring
 */
static unsigned int chain_mode;
module_param(chain_mode, int, 0444);
MODULE_PARM_DESC(chain_mode, "To use chain instead of ring mode");

static irqreturn_t stmmac_interrupt(int irq, void *dev_id);

#ifdef CONFIG_DEBUG_FS
static const struct net_device_ops stmmac_netdev_ops;
static void stmmac_init_fs(struct net_device *dev);
static void stmmac_exit_fs(struct net_device *dev);
#endif

#define STMMAC_COAL_TIMER(x) (jiffies + usecs_to_jiffies(x))

/**
 * stmmac_verify_args - verify the driver parameters.
 * Description: it checks the driver parameters and set a default in case of
 * errors.
 */
static void stmmac_verify_args(void)
{
	if (unlikely(watchdog < 0))
		watchdog = TX_TIMEO;
	if (unlikely((buf_sz < DEFAULT_BUFSIZE) || (buf_sz > BUF_SIZE_16KiB)))
		buf_sz = DEFAULT_BUFSIZE;
	if (unlikely(flow_ctrl > 1))
		flow_ctrl = FLOW_AUTO;
	else if (likely(flow_ctrl < 0))
		flow_ctrl = FLOW_OFF;
	if (unlikely((pause < 0) || (pause > 0xffff)))
		pause = PAUSE_TIME;
	if (eee_timer < 0)
		eee_timer = STMMAC_DEFAULT_LPI_TIMER;
}

/**
 * stmmac_disable_all_queues - Disable all queues
 * @priv: driver private structure
 */
static void stmmac_disable_all_queues(struct stmmac_priv *priv)
{
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct stmmac_channel *ch = &priv->channel[queue];

		if (queue < rx_queues_cnt)
			napi_disable(&ch->rx_napi);
		if (queue < tx_queues_cnt)
			napi_disable(&ch->tx_napi);
	}
}

/**
 * stmmac_enable_all_queues - Enable all queues
 * @priv: driver private structure
 */
static void stmmac_enable_all_queues(struct stmmac_priv *priv)
{
	u32 rx_queues_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 maxq = max(rx_queues_cnt, tx_queues_cnt);
	u32 queue;

	for (queue = 0; queue < maxq; queue++) {
		struct stmmac_channel *ch = &priv->channel[queue];

		if (queue < rx_queues_cnt)
			napi_enable(&ch->rx_napi);
		if (queue < tx_queues_cnt)
			napi_enable(&ch->tx_napi);
	}
}

static void stmmac_service_event_schedule(struct stmmac_priv *priv)
{
	if (!test_bit(STMMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(STMMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->wq, &priv->service_task);
}

static void stmmac_global_err(struct stmmac_priv *priv)
{
	netif_carrier_off(priv->dev);
	set_bit(STMMAC_RESET_REQUESTED, &priv->state);
	stmmac_service_event_schedule(priv);
}

/**
 * stmmac_clk_csr_set - dynamically set the MDC clock
 * @priv: driver private structure
 * Description: this is to dynamically set the MDC clock according to the csr
 * clock input.
 * Note:
 *	If a specific clk_csr value is passed from the platform
 *	this means that the CSR Clock Range selection cannot be
 *	changed at run-time and it is fixed (as reported in the driver
 *	documentation). Viceversa the driver will try to set the MDC
 *	clock dynamically according to the actual clock input.
 */
static void stmmac_clk_csr_set(struct stmmac_priv *priv)
{
	u32 clk_rate;

	clk_rate = clk_get_rate(priv->plat->stmmac_clk);

	/* Platform provided default clk_csr would be assumed valid
	 * for all other cases except for the below mentioned ones.
	 * For values higher than the IEEE 802.3 specified frequency
	 * we can not estimate the proper divider as it is not known
	 * the frequency of clk_csr_i. So we do not change the default
	 * divider.
	 */
	if (!(priv->clk_csr & MAC_CSR_H_FRQ_MASK)) {
		if (clk_rate < CSR_F_35M)
			priv->clk_csr = STMMAC_CSR_20_35M;
		else if ((clk_rate >= CSR_F_35M) && (clk_rate < CSR_F_60M))
			priv->clk_csr = STMMAC_CSR_35_60M;
		else if ((clk_rate >= CSR_F_60M) && (clk_rate < CSR_F_100M))
			priv->clk_csr = STMMAC_CSR_60_100M;
		else if ((clk_rate >= CSR_F_100M) && (clk_rate < CSR_F_150M))
			priv->clk_csr = STMMAC_CSR_100_150M;
		else if ((clk_rate >= CSR_F_150M) && (clk_rate < CSR_F_250M))
			priv->clk_csr = STMMAC_CSR_150_250M;
		else if ((clk_rate >= CSR_F_250M) && (clk_rate < CSR_F_300M))
			priv->clk_csr = STMMAC_CSR_250_300M;
	}

	if (priv->plat->has_sun8i) {
		if (clk_rate > 160000000)
			priv->clk_csr = 0x03;
		else if (clk_rate > 80000000)
			priv->clk_csr = 0x02;
		else if (clk_rate > 40000000)
			priv->clk_csr = 0x01;
		else
			priv->clk_csr = 0;
	}

	if (priv->plat->has_xgmac) {
		if (clk_rate > 400000000)
			priv->clk_csr = 0x5;
		else if (clk_rate > 350000000)
			priv->clk_csr = 0x4;
		else if (clk_rate > 300000000)
			priv->clk_csr = 0x3;
		else if (clk_rate > 250000000)
			priv->clk_csr = 0x2;
		else if (clk_rate > 150000000)
			priv->clk_csr = 0x1;
		else
			priv->clk_csr = 0x0;
	}
}

static void print_pkt(unsigned char *buf, int len)
{
	pr_debug("len = %d byte, buf addr: 0x%p\n", len, buf);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len);
}

static inline u32 stmmac_tx_avail(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	u32 avail;

	if (tx_q->dirty_tx > tx_q->cur_tx)
		avail = tx_q->dirty_tx - tx_q->cur_tx - 1;
	else
		avail = priv->dma_tx_size - tx_q->cur_tx + tx_q->dirty_tx - 1;

	return avail;
}

/**
 * stmmac_rx_dirty - Get RX queue dirty
 * @priv: driver private structure
 * @queue: RX queue index
 */
static inline u32 stmmac_rx_dirty(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	u32 dirty;

	if (rx_q->dirty_rx <= rx_q->cur_rx)
		dirty = rx_q->cur_rx - rx_q->dirty_rx;
	else
		dirty = priv->dma_rx_size - rx_q->dirty_rx + rx_q->cur_rx;

	return dirty;
}

/**
 * stmmac_enable_eee_mode - check and enter in LPI mode
 * @priv: driver private structure
 * Description: this function is to verify and enter in LPI mode in case of
 * EEE.
 */
static void stmmac_enable_eee_mode(struct stmmac_priv *priv)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	/* check if all TX queues have the work finished */
	for (queue = 0; queue < tx_cnt; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

		if (tx_q->dirty_tx != tx_q->cur_tx)
			return; /* still unfinished work */
	}

	/* Check and enter in LPI mode */
	if (!priv->tx_path_in_lpi_mode)
		stmmac_set_eee_mode(priv, priv->hw,
				priv->plat->en_tx_lpi_clockgating);
}

/**
 * stmmac_disable_eee_mode - disable and exit from LPI mode
 * @priv: driver private structure
 * Description: this function is to exit and disable EEE in case of
 * LPI state is true. This is called by the xmit.
 */
void stmmac_disable_eee_mode(struct stmmac_priv *priv)
{
	stmmac_reset_eee_mode(priv, priv->hw);
	del_timer_sync(&priv->eee_ctrl_timer);
	priv->tx_path_in_lpi_mode = false;
}

/**
 * stmmac_eee_ctrl_timer - EEE TX SW timer.
 * @t:  timer_list struct containing private info
 * Description:
 *  if there is no data transfer and if we are not in LPI state,
 *  then MAC Transmitter can be moved to LPI state.
 */
static void stmmac_eee_ctrl_timer(struct timer_list *t)
{
	struct stmmac_priv *priv = from_timer(priv, t, eee_ctrl_timer);

	stmmac_enable_eee_mode(priv);
	mod_timer(&priv->eee_ctrl_timer, STMMAC_LPI_T(priv->tx_lpi_timer));
}

/**
 * stmmac_eee_init - init EEE
 * @priv: driver private structure
 * Description:
 *  if the GMAC supports the EEE (from the HW cap reg) and the phy device
 *  can also manage EEE, this function enable the LPI state and start related
 *  timer.
 */
bool stmmac_eee_init(struct stmmac_priv *priv)
{
	int eee_tw_timer = priv->eee_tw_timer;

	/* Using PCS we cannot dial with the phy registers at this stage
	 * so we do not support extra feature like EEE.
	 */
	if (priv->hw->pcs == STMMAC_PCS_TBI ||
	    priv->hw->pcs == STMMAC_PCS_RTBI)
		return false;

	/* Check if MAC core supports the EEE feature. */
	if (!priv->dma_cap.eee)
		return false;

	mutex_lock(&priv->lock);

	/* Check if it needs to be deactivated */
	if (!priv->eee_active) {
		if (priv->eee_enabled) {
			netdev_dbg(priv->dev, "disable EEE\n");
			del_timer_sync(&priv->eee_ctrl_timer);
			stmmac_set_eee_timer(priv, priv->hw, 0, eee_tw_timer);
		}
		mutex_unlock(&priv->lock);
		return false;
	}

	if (priv->eee_active && !priv->eee_enabled) {
		timer_setup(&priv->eee_ctrl_timer, stmmac_eee_ctrl_timer, 0);
		stmmac_set_eee_timer(priv, priv->hw, STMMAC_DEFAULT_LIT_LS,
				     eee_tw_timer);
	}

	mod_timer(&priv->eee_ctrl_timer, STMMAC_LPI_T(priv->tx_lpi_timer));

	mutex_unlock(&priv->lock);
	netdev_dbg(priv->dev, "Energy-Efficient Ethernet initialized\n");
	return true;
}

/* stmmac_get_tx_hwtstamp - get HW TX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read timestamp from the descriptor & pass it to stack.
 * and also perform some sanity checks.
 */
static void stmmac_get_tx_hwtstamp(struct stmmac_priv *priv,
				   struct dma_desc *p, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps shhwtstamp;
	bool found = false;
	u64 ns = 0;

	if (!priv->hwts_tx_en)
		return;

	/* exit if skb doesn't support hw tstamp */
	if (likely(!skb || !(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)))
		return;

	/* check tx tstamp status */
	if (stmmac_get_tx_timestamp_status(priv, p)) {
		stmmac_get_timestamp(priv, p, priv->adv_ts, &ns);
		found = true;
	} else if (!stmmac_get_mac_tx_timestamp(priv, priv->hw, &ns)) {
		found = true;
	}

	if (found) {
		memset(&shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp.hwtstamp = ns_to_ktime(ns);

		netdev_dbg(priv->dev, "get valid TX hw timestamp %llu\n", ns);
		/* pass tstamp to stack */
		skb_tstamp_tx(skb, &shhwtstamp);
	}
}

/* stmmac_get_rx_hwtstamp - get HW RX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @np : next descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read received packet's timestamp from the descriptor
 * and pass it to stack. It also perform some sanity checks.
 */
static void stmmac_get_rx_hwtstamp(struct stmmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shhwtstamp = NULL;
	struct dma_desc *desc = p;
	u64 ns = 0;

	if (!priv->hwts_rx_en)
		return;
	/* For GMAC4, the valid timestamp is from CTX next desc. */
	if (priv->plat->has_gmac4 || priv->plat->has_xgmac)
		desc = np;

	/* Check if timestamp is available */
	if (stmmac_get_rx_timestamp_status(priv, p, np, priv->adv_ts)) {
		stmmac_get_timestamp(priv, desc, priv->adv_ts, &ns);
		netdev_dbg(priv->dev, "get valid RX hw timestamp %llu\n", ns);
		shhwtstamp = skb_hwtstamps(skb);
		memset(shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp->hwtstamp = ns_to_ktime(ns);
	} else  {
		netdev_dbg(priv->dev, "cannot get RX hw timestamp\n");
	}
}

/**
 *  stmmac_hwtstamp_set - control hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function configures the MAC to enable/disable both outgoing(TX)
 *  and incoming(RX) packets time stamping based on user input.
 *  Return Value:
 *  0 on success and an appropriate -ve integer on failure.
 */
static int stmmac_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;
	struct timespec64 now;
	u64 temp = 0;
	u32 ptp_v2 = 0;
	u32 tstamp_all = 0;
	u32 ptp_over_ipv4_udp = 0;
	u32 ptp_over_ipv6_udp = 0;
	u32 ptp_over_ethernet = 0;
	u32 snap_type_sel = 0;
	u32 ts_master_en = 0;
	u32 ts_event_en = 0;
	u32 sec_inc = 0;
	u32 value = 0;
	bool xmac;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->adv_ts)) {
		netdev_alert(priv->dev, "No support for HW time stamping\n");
		priv->hwts_tx_en = 0;
		priv->hwts_rx_en = 0;

		return -EOPNOTSUPP;
	}

	if (copy_from_user(&config, ifr->ifr_data,
			   sizeof(config)))
		return -EFAULT;

	netdev_dbg(priv->dev, "%s config flags:0x%x, tx_type:0x%x, rx_filter:0x%x\n",
		   __func__, config.flags, config.tx_type, config.rx_filter);

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if (config.tx_type != HWTSTAMP_TX_OFF &&
	    config.tx_type != HWTSTAMP_TX_ON)
		return -ERANGE;

	if (priv->adv_ts) {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			/* time stamp no incoming packet at all */
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			/* 'xmac' hardware can support Sync, Pdelay_Req and
			 * Pdelay_resp by setting bit14 and bits17/16 to 01
			 * This leaves Delay_Req timestamps out.
			 * Enable all events *and* general purpose message
			 * timestamping
			 */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
			/* PTP v1, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_SYNC;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
			/* PTP v1, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
			/* PTP v2, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for all event messages */
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
			/* PTP v2, UDP, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
			/* PTP v2, UDP, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_EVENT:
			/* PTP v2/802.AS1 any layer, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			snap_type_sel = PTP_TCR_SNAPTYPSEL_1;
			if (priv->synopsys_id != DWMAC_CORE_5_10)
				ts_event_en = PTP_TCR_TSEVNTENA;
			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_SYNC:
			/* PTP v2/802.AS1, any layer, Sync packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_SYNC;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for SYNC messages only */
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
			/* PTP v2/802.AS1, any layer, Delay_req packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V2_DELAY_REQ;
			ptp_v2 = PTP_TCR_TSVER2ENA;
			/* take time stamp for Delay_Req messages only */
			ts_master_en = PTP_TCR_TSMSTRENA;
			ts_event_en = PTP_TCR_TSEVNTENA;

			ptp_over_ipv4_udp = PTP_TCR_TSIPV4ENA;
			ptp_over_ipv6_udp = PTP_TCR_TSIPV6ENA;
			ptp_over_ethernet = PTP_TCR_TSIPENA;
			break;

		case HWTSTAMP_FILTER_NTP_ALL:
		case HWTSTAMP_FILTER_ALL:
			/* time stamp any incoming packet */
			config.rx_filter = HWTSTAMP_FILTER_ALL;
			tstamp_all = PTP_TCR_TSENALL;
			break;

		default:
			return -ERANGE;
		}
	} else {
		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			config.rx_filter = HWTSTAMP_FILTER_NONE;
			break;
		default:
			/* PTP v1, UDP, any kind of event packet */
			config.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
			break;
		}
	}
	priv->hwts_rx_en = ((config.rx_filter == HWTSTAMP_FILTER_NONE) ? 0 : 1);
	priv->hwts_tx_en = config.tx_type == HWTSTAMP_TX_ON;

	if (!priv->hwts_tx_en && !priv->hwts_rx_en)
		stmmac_config_hw_tstamping(priv, priv->ptpaddr, 0);
	else {
		value = (PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSCTRLSSR |
			 tstamp_all | ptp_v2 | ptp_over_ethernet |
			 ptp_over_ipv6_udp | ptp_over_ipv4_udp | ts_event_en |
			 ts_master_en | snap_type_sel);
		stmmac_config_hw_tstamping(priv, priv->ptpaddr, value);

		/* program Sub Second Increment reg */
		stmmac_config_sub_second_increment(priv,
				priv->ptpaddr, priv->plat->clk_ptp_rate,
				xmac, &sec_inc);
		temp = div_u64(1000000000ULL, sec_inc);

		/* Store sub second increment and flags for later use */
		priv->sub_second_inc = sec_inc;
		priv->systime_flags = value;

		/* calculate default added value:
		 * formula is :
		 * addend = (2^32)/freq_div_ratio;
		 * where, freq_div_ratio = 1e9ns/sec_inc
		 */
		temp = (u64)(temp << 32);
		priv->default_addend = div_u64(temp, priv->plat->clk_ptp_rate);
		stmmac_config_addend(priv, priv->ptpaddr, priv->default_addend);

		/* initialize system time */
		ktime_get_real_ts64(&now);

		/* lower 32 bits of tv_sec are safe until y2106 */
		stmmac_init_systime(priv, priv->ptpaddr,
				(u32)now.tv_sec, now.tv_nsec);
	}

	memcpy(&priv->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

/**
 *  stmmac_hwtstamp_get - read hardware timestamping.
 *  @dev: device pointer.
 *  @ifr: An IOCTL specific structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  Description:
 *  This function obtain the current hardware timestamping settings
 *  as requested.
 */
static int stmmac_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct hwtstamp_config *config = &priv->tstamp_config;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, config,
			    sizeof(*config)) ? -EFAULT : 0;
}

/**
 * stmmac_init_ptp - init PTP
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PTPv1 or PTPv2.
 * This is done by looking at the HW cap. register.
 * This function also registers the ptp driver.
 */
static int stmmac_init_ptp(struct stmmac_priv *priv)
{
	bool xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return -EOPNOTSUPP;

	priv->adv_ts = 0;
	/* Check if adv_ts can be enabled for dwmac 4.x / xgmac core */
	if (xmac && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;
	/* Dwmac 3.x core with extend_desc can support adv_ts */
	else if (priv->extend_desc && priv->dma_cap.atime_stamp)
		priv->adv_ts = 1;

	if (priv->dma_cap.time_stamp)
		netdev_info(priv->dev, "IEEE 1588-2002 Timestamp supported\n");

	if (priv->adv_ts)
		netdev_info(priv->dev,
			    "IEEE 1588-2008 Advanced Timestamp supported\n");

	priv->hwts_tx_en = 0;
	priv->hwts_rx_en = 0;

	stmmac_ptp_register(priv);

	return 0;
}

static void stmmac_release_ptp(struct stmmac_priv *priv)
{
	clk_disable_unprepare(priv->plat->clk_ptp_ref);
	stmmac_ptp_unregister(priv);
}

/**
 *  stmmac_mac_flow_ctrl - Configure flow control in all queues
 *  @priv: driver private structure
 *  @duplex: duplex passed to the next function
 *  Description: It is used for configuring the flow control in all queues
 */
static void stmmac_mac_flow_ctrl(struct stmmac_priv *priv, u32 duplex)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;

	stmmac_flow_ctrl(priv, priv->hw, duplex, priv->flow_ctrl,
			priv->pause, tx_cnt);
}

static void stmmac_validate(struct phylink_config *config,
			    unsigned long *supported,
			    struct phylink_link_state *state)
{
	struct stmmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mac_supported) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	int tx_cnt = priv->plat->tx_queues_to_use;
	int max_speed = priv->plat->max_speed;

	phylink_set(mac_supported, 10baseT_Half);
	phylink_set(mac_supported, 10baseT_Full);
	phylink_set(mac_supported, 100baseT_Half);
	phylink_set(mac_supported, 100baseT_Full);
	phylink_set(mac_supported, 1000baseT_Half);
	phylink_set(mac_supported, 1000baseT_Full);
	phylink_set(mac_supported, 1000baseKX_Full);

	phylink_set(mac_supported, Autoneg);
	phylink_set(mac_supported, Pause);
	phylink_set(mac_supported, Asym_Pause);
	phylink_set_port_modes(mac_supported);

	/* Cut down 1G if asked to */
	if ((max_speed > 0) && (max_speed < 1000)) {
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
	} else if (priv->plat->has_xgmac) {
		if (!max_speed || (max_speed >= 2500)) {
			phylink_set(mac_supported, 2500baseT_Full);
			phylink_set(mac_supported, 2500baseX_Full);
		}
		if (!max_speed || (max_speed >= 5000)) {
			phylink_set(mac_supported, 5000baseT_Full);
		}
		if (!max_speed || (max_speed >= 10000)) {
			phylink_set(mac_supported, 10000baseSR_Full);
			phylink_set(mac_supported, 10000baseLR_Full);
			phylink_set(mac_supported, 10000baseER_Full);
			phylink_set(mac_supported, 10000baseLRM_Full);
			phylink_set(mac_supported, 10000baseT_Full);
			phylink_set(mac_supported, 10000baseKX4_Full);
			phylink_set(mac_supported, 10000baseKR_Full);
		}
		if (!max_speed || (max_speed >= 25000)) {
			phylink_set(mac_supported, 25000baseCR_Full);
			phylink_set(mac_supported, 25000baseKR_Full);
			phylink_set(mac_supported, 25000baseSR_Full);
		}
		if (!max_speed || (max_speed >= 40000)) {
			phylink_set(mac_supported, 40000baseKR4_Full);
			phylink_set(mac_supported, 40000baseCR4_Full);
			phylink_set(mac_supported, 40000baseSR4_Full);
			phylink_set(mac_supported, 40000baseLR4_Full);
		}
		if (!max_speed || (max_speed >= 50000)) {
			phylink_set(mac_supported, 50000baseCR2_Full);
			phylink_set(mac_supported, 50000baseKR2_Full);
			phylink_set(mac_supported, 50000baseSR2_Full);
			phylink_set(mac_supported, 50000baseKR_Full);
			phylink_set(mac_supported, 50000baseSR_Full);
			phylink_set(mac_supported, 50000baseCR_Full);
			phylink_set(mac_supported, 50000baseLR_ER_FR_Full);
			phylink_set(mac_supported, 50000baseDR_Full);
		}
		if (!max_speed || (max_speed >= 100000)) {
			phylink_set(mac_supported, 100000baseKR4_Full);
			phylink_set(mac_supported, 100000baseSR4_Full);
			phylink_set(mac_supported, 100000baseCR4_Full);
			phylink_set(mac_supported, 100000baseLR4_ER4_Full);
			phylink_set(mac_supported, 100000baseKR2_Full);
			phylink_set(mac_supported, 100000baseSR2_Full);
			phylink_set(mac_supported, 100000baseCR2_Full);
			phylink_set(mac_supported, 100000baseLR2_ER2_FR2_Full);
			phylink_set(mac_supported, 100000baseDR2_Full);
		}
	}

	/* Half-Duplex can only work with single queue */
	if (tx_cnt > 1) {
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 100baseT_Half);
		phylink_set(mask, 1000baseT_Half);
	}

	linkmode_and(supported, supported, mac_supported);
	linkmode_andnot(supported, supported, mask);

	linkmode_and(state->advertising, state->advertising, mac_supported);
	linkmode_andnot(state->advertising, state->advertising, mask);

	/* If PCS is supported, check which modes it supports. */
	stmmac_xpcs_validate(priv, &priv->hw->xpcs_args, supported, state);
}

static void stmmac_mac_pcs_get_state(struct phylink_config *config,
				     struct phylink_link_state *state)
{
	struct stmmac_priv *priv = netdev_priv(to_net_dev(config->dev));

	state->link = 0;
	stmmac_xpcs_get_state(priv, &priv->hw->xpcs_args, state);
}

static void stmmac_mac_config(struct phylink_config *config, unsigned int mode,
			      const struct phylink_link_state *state)
{
	struct stmmac_priv *priv = netdev_priv(to_net_dev(config->dev));

	stmmac_xpcs_config(priv, &priv->hw->xpcs_args, state);
}

static void stmmac_mac_an_restart(struct phylink_config *config)
{
	/* Not Supported */
}

static void stmmac_mac_link_down(struct phylink_config *config,
				 unsigned int mode, phy_interface_t interface)
{
	struct stmmac_priv *priv = netdev_priv(to_net_dev(config->dev));

	stmmac_mac_set(priv, priv->ioaddr, false);
	priv->eee_active = false;
	priv->tx_lpi_enabled = false;
	stmmac_eee_init(priv);
	stmmac_set_eee_pls(priv, priv->hw, false);
}

static void stmmac_mac_link_up(struct phylink_config *config,
			       struct phy_device *phy,
			       unsigned int mode, phy_interface_t interface,
			       int speed, int duplex,
			       bool tx_pause, bool rx_pause)
{
	struct stmmac_priv *priv = netdev_priv(to_net_dev(config->dev));
	u32 ctrl;

	stmmac_xpcs_link_up(priv, &priv->hw->xpcs_args, speed, interface);

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		switch (speed) {
		case SPEED_10000:
			ctrl |= priv->hw->link.xgmii.speed10000;
			break;
		case SPEED_5000:
			ctrl |= priv->hw->link.xgmii.speed5000;
			break;
		case SPEED_2500:
			ctrl |= priv->hw->link.xgmii.speed2500;
			break;
		default:
			return;
		}
	} else if (interface == PHY_INTERFACE_MODE_XLGMII) {
		switch (speed) {
		case SPEED_100000:
			ctrl |= priv->hw->link.xlgmii.speed100000;
			break;
		case SPEED_50000:
			ctrl |= priv->hw->link.xlgmii.speed50000;
			break;
		case SPEED_40000:
			ctrl |= priv->hw->link.xlgmii.speed40000;
			break;
		case SPEED_25000:
			ctrl |= priv->hw->link.xlgmii.speed25000;
			break;
		case SPEED_10000:
			ctrl |= priv->hw->link.xgmii.speed10000;
			break;
		case SPEED_2500:
			ctrl |= priv->hw->link.speed2500;
			break;
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			break;
		default:
			return;
		}
	} else {
		switch (speed) {
		case SPEED_2500:
			ctrl |= priv->hw->link.speed2500;
			break;
		case SPEED_1000:
			ctrl |= priv->hw->link.speed1000;
			break;
		case SPEED_100:
			ctrl |= priv->hw->link.speed100;
			break;
		case SPEED_10:
			ctrl |= priv->hw->link.speed10;
			break;
		default:
			return;
		}
	}

	priv->speed = speed;

	if (priv->plat->fix_mac_speed)
		priv->plat->fix_mac_speed(priv->plat->bsp_priv, speed);

	if (!duplex)
		ctrl &= ~priv->hw->link.duplex;
	else
		ctrl |= priv->hw->link.duplex;

	/* Flow Control operation */
	if (tx_pause && rx_pause)
		stmmac_mac_flow_ctrl(priv, duplex);

	writel(ctrl, priv->ioaddr + MAC_CTRL_REG);

	stmmac_mac_set(priv, priv->ioaddr, true);
	if (phy && priv->dma_cap.eee) {
		priv->eee_active = phy_init_eee(phy, 1) >= 0;
		priv->eee_enabled = stmmac_eee_init(priv);
		priv->tx_lpi_enabled = priv->eee_enabled;
		stmmac_set_eee_pls(priv, priv->hw, true);
	}
}

static const struct phylink_mac_ops stmmac_phylink_mac_ops = {
	.validate = stmmac_validate,
	.mac_pcs_get_state = stmmac_mac_pcs_get_state,
	.mac_config = stmmac_mac_config,
	.mac_an_restart = stmmac_mac_an_restart,
	.mac_link_down = stmmac_mac_link_down,
	.mac_link_up = stmmac_mac_link_up,
};

/**
 * stmmac_check_pcs_mode - verify if RGMII/SGMII is supported
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PCS.
 * Physical Coding Sublayer (PCS) interface that can be used when the MAC is
 * configured for the TBI, RTBI, or SGMII PHY interface.
 */
static void stmmac_check_pcs_mode(struct stmmac_priv *priv)
{
	int interface = priv->plat->interface;

	if (priv->dma_cap.pcs) {
		if ((interface == PHY_INTERFACE_MODE_RGMII) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_ID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_RXID) ||
		    (interface == PHY_INTERFACE_MODE_RGMII_TXID)) {
			netdev_dbg(priv->dev, "PCS RGMII support enabled\n");
			priv->hw->pcs = STMMAC_PCS_RGMII;
		} else if (interface == PHY_INTERFACE_MODE_SGMII) {
			netdev_dbg(priv->dev, "PCS SGMII support enabled\n");
			priv->hw->pcs = STMMAC_PCS_SGMII;
		}
	}
}

/**
 * stmmac_init_phy - PHY initialization
 * @dev: net device structure
 * Description: it initializes the driver's PHY state, and attaches the PHY
 * to the mac driver.
 *  Return value:
 *  0 on success
 */
static int stmmac_init_phy(struct net_device *dev)
{
	struct ethtool_wolinfo wol = { .cmd = ETHTOOL_GWOL };
	struct stmmac_priv *priv = netdev_priv(dev);
	struct device_node *node;
	int ret;

	node = priv->plat->phylink_node;

	if (node)
		ret = phylink_of_phy_connect(priv->phylink, node, 0);

	/* Some DT bindings do not set-up the PHY handle. Let's try to
	 * manually parse it
	 */
	if (!node || ret) {
		int addr = priv->plat->phy_addr;
		struct phy_device *phydev;

		phydev = mdiobus_get_phy(priv->mii, addr);
		if (!phydev) {
			netdev_err(priv->dev, "no phy at addr %d\n", addr);
			return -ENODEV;
		}

		ret = phylink_connect_phy(priv->phylink, phydev);
	}

	phylink_ethtool_get_wol(priv->phylink, &wol);
	device_set_wakeup_capable(priv->device, !!wol.supported);

	return ret;
}

static int stmmac_phy_setup(struct stmmac_priv *priv)
{
	struct fwnode_handle *fwnode = of_fwnode_handle(priv->plat->phylink_node);
	int mode = priv->plat->phy_interface;
	struct phylink *phylink;

	priv->phylink_config.dev = &priv->dev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;
	priv->phylink_config.pcs_poll = true;

	if (!fwnode)
		fwnode = dev_fwnode(priv->device);

	phylink = phylink_create(&priv->phylink_config, fwnode,
				 mode, &stmmac_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	priv->phylink = phylink;
	return 0;
}

static void stmmac_display_rx_rings(struct stmmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	unsigned int desc_size;
	void *head_rx;
	u32 queue;

	/* Display RX rings */
	for (queue = 0; queue < rx_cnt; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		pr_info("\tRX Queue %u rings\n", queue);

		if (priv->extend_desc) {
			head_rx = (void *)rx_q->dma_erx;
			desc_size = sizeof(struct dma_extended_desc);
		} else {
			head_rx = (void *)rx_q->dma_rx;
			desc_size = sizeof(struct dma_desc);
		}

		/* Display RX ring */
		stmmac_display_ring(priv, head_rx, priv->dma_rx_size, true,
				    rx_q->dma_rx_phy, desc_size);
	}
}

static void stmmac_display_tx_rings(struct stmmac_priv *priv)
{
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	unsigned int desc_size;
	void *head_tx;
	u32 queue;

	/* Display TX rings */
	for (queue = 0; queue < tx_cnt; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

		pr_info("\tTX Queue %d rings\n", queue);

		if (priv->extend_desc) {
			head_tx = (void *)tx_q->dma_etx;
			desc_size = sizeof(struct dma_extended_desc);
		} else if (tx_q->tbs & STMMAC_TBS_AVAIL) {
			head_tx = (void *)tx_q->dma_entx;
			desc_size = sizeof(struct dma_edesc);
		} else {
			head_tx = (void *)tx_q->dma_tx;
			desc_size = sizeof(struct dma_desc);
		}

		stmmac_display_ring(priv, head_tx, priv->dma_tx_size, false,
				    tx_q->dma_tx_phy, desc_size);
	}
}

static void stmmac_display_rings(struct stmmac_priv *priv)
{
	/* Display RX ring */
	stmmac_display_rx_rings(priv);

	/* Display TX ring */
	stmmac_display_tx_rings(priv);
}

static int stmmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_8KiB)
		ret = BUF_SIZE_16KiB;
	else if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu > DEFAULT_BUFSIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DEFAULT_BUFSIZE;

	return ret;
}

/**
 * stmmac_clear_rx_descriptors - clear RX descriptors
 * @priv: driver private structure
 * @queue: RX queue index
 * Description: this function is called to clear the RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_rx_descriptors(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int i;

	/* Clear the RX descriptors */
	for (i = 0; i < priv->dma_rx_size; i++)
		if (priv->extend_desc)
			stmmac_init_rx_desc(priv, &rx_q->dma_erx[i].basic,
					priv->use_riwt, priv->mode,
					(i == priv->dma_rx_size - 1),
					priv->dma_buf_sz);
		else
			stmmac_init_rx_desc(priv, &rx_q->dma_rx[i],
					priv->use_riwt, priv->mode,
					(i == priv->dma_rx_size - 1),
					priv->dma_buf_sz);
}

/**
 * stmmac_clear_tx_descriptors - clear tx descriptors
 * @priv: driver private structure
 * @queue: TX queue index.
 * Description: this function is called to clear the TX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_tx_descriptors(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	int i;

	/* Clear the TX descriptors */
	for (i = 0; i < priv->dma_tx_size; i++) {
		int last = (i == (priv->dma_tx_size - 1));
		struct dma_desc *p;

		if (priv->extend_desc)
			p = &tx_q->dma_etx[i].basic;
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[i].basic;
		else
			p = &tx_q->dma_tx[i];

		stmmac_init_tx_desc(priv, p, priv->mode, last);
	}
}

/**
 * stmmac_clear_descriptors - clear descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the TX and RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_descriptors(struct stmmac_priv *priv)
{
	u32 rx_queue_cnt = priv->plat->rx_queues_to_use;
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Clear the RX descriptors */
	for (queue = 0; queue < rx_queue_cnt; queue++)
		stmmac_clear_rx_descriptors(priv, queue);

	/* Clear the TX descriptors */
	for (queue = 0; queue < tx_queue_cnt; queue++)
		stmmac_clear_tx_descriptors(priv, queue);
}

/**
 * stmmac_init_rx_buffers - init the RX descriptor buffer.
 * @priv: driver private structure
 * @p: descriptor pointer
 * @i: descriptor index
 * @flags: gfp flag
 * @queue: RX queue index
 * Description: this function is called to allocate a receive buffer, perform
 * the DMA mapping and init the descriptor.
 */
static int stmmac_init_rx_buffers(struct stmmac_priv *priv, struct dma_desc *p,
				  int i, gfp_t flags, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct stmmac_rx_buffer *buf = &rx_q->buf_pool[i];

	buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
	if (!buf->page)
		return -ENOMEM;

	if (priv->sph) {
		buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
		if (!buf->sec_page)
			return -ENOMEM;

		buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);
		stmmac_set_desc_sec_addr(priv, p, buf->sec_addr, true);
	} else {
		buf->sec_page = NULL;
		stmmac_set_desc_sec_addr(priv, p, buf->sec_addr, false);
	}

	buf->addr = page_pool_get_dma_addr(buf->page);
	stmmac_set_desc_addr(priv, p, buf->addr);
	if (priv->dma_buf_sz == BUF_SIZE_16KiB)
		stmmac_init_desc3(priv, p);

	return 0;
}

/**
 * stmmac_free_rx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void stmmac_free_rx_buffer(struct stmmac_priv *priv, u32 queue, int i)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct stmmac_rx_buffer *buf = &rx_q->buf_pool[i];

	if (buf->page)
		page_pool_put_full_page(rx_q->page_pool, buf->page, false);
	buf->page = NULL;

	if (buf->sec_page)
		page_pool_put_full_page(rx_q->page_pool, buf->sec_page, false);
	buf->sec_page = NULL;
}

/**
 * stmmac_free_tx_buffer - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 * @i: buffer index.
 */
static void stmmac_free_tx_buffer(struct stmmac_priv *priv, u32 queue, int i)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

	if (tx_q->tx_skbuff_dma[i].buf) {
		if (tx_q->tx_skbuff_dma[i].map_as_page)
			dma_unmap_page(priv->device,
				       tx_q->tx_skbuff_dma[i].buf,
				       tx_q->tx_skbuff_dma[i].len,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(priv->device,
					 tx_q->tx_skbuff_dma[i].buf,
					 tx_q->tx_skbuff_dma[i].len,
					 DMA_TO_DEVICE);
	}

	if (tx_q->tx_skbuff[i]) {
		dev_kfree_skb_any(tx_q->tx_skbuff[i]);
		tx_q->tx_skbuff[i] = NULL;
		tx_q->tx_skbuff_dma[i].buf = 0;
		tx_q->tx_skbuff_dma[i].map_as_page = false;
	}
}

/**
 * init_dma_rx_desc_rings - init the RX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_rx_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_count = priv->plat->rx_queues_to_use;
	int ret = -ENOMEM;
	int queue;
	int i;

	/* RX INITIALIZATION */
	netif_dbg(priv, probe, priv->dev,
		  "SKB addresses:\nskb\t\tskb data\tdma data\n");

	for (queue = 0; queue < rx_count; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_rx_phy=0x%08x\n", __func__,
			  (u32)rx_q->dma_rx_phy);

		stmmac_clear_rx_descriptors(priv, queue);

		for (i = 0; i < priv->dma_rx_size; i++) {
			struct dma_desc *p;

			if (priv->extend_desc)
				p = &((rx_q->dma_erx + i)->basic);
			else
				p = rx_q->dma_rx + i;

			ret = stmmac_init_rx_buffers(priv, p, i, flags,
						     queue);
			if (ret)
				goto err_init_rx_buffers;
		}

		rx_q->cur_rx = 0;
		rx_q->dirty_rx = (unsigned int)(i - priv->dma_rx_size);

		/* Setup the chained descriptor addresses */
		if (priv->mode == STMMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				stmmac_mode_init(priv, rx_q->dma_erx,
						 rx_q->dma_rx_phy,
						 priv->dma_rx_size, 1);
			else
				stmmac_mode_init(priv, rx_q->dma_rx,
						 rx_q->dma_rx_phy,
						 priv->dma_rx_size, 0);
		}
	}

	return 0;

err_init_rx_buffers:
	while (queue >= 0) {
		while (--i >= 0)
			stmmac_free_rx_buffer(priv, queue, i);

		if (queue == 0)
			break;

		i = priv->dma_rx_size;
		queue--;
	}

	return ret;
}

/**
 * init_dma_tx_desc_rings - init the TX descriptor rings
 * @dev: net device structure.
 * Description: this function initializes the DMA TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_tx_desc_rings(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;
	int i;

	for (queue = 0; queue < tx_queue_cnt; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

		netif_dbg(priv, probe, priv->dev,
			  "(%s) dma_tx_phy=0x%08x\n", __func__,
			 (u32)tx_q->dma_tx_phy);

		/* Setup the chained descriptor addresses */
		if (priv->mode == STMMAC_CHAIN_MODE) {
			if (priv->extend_desc)
				stmmac_mode_init(priv, tx_q->dma_etx,
						 tx_q->dma_tx_phy,
						 priv->dma_tx_size, 1);
			else if (!(tx_q->tbs & STMMAC_TBS_AVAIL))
				stmmac_mode_init(priv, tx_q->dma_tx,
						 tx_q->dma_tx_phy,
						 priv->dma_tx_size, 0);
		}

		for (i = 0; i < priv->dma_tx_size; i++) {
			struct dma_desc *p;
			if (priv->extend_desc)
				p = &((tx_q->dma_etx + i)->basic);
			else if (tx_q->tbs & STMMAC_TBS_AVAIL)
				p = &((tx_q->dma_entx + i)->basic);
			else
				p = tx_q->dma_tx + i;

			stmmac_clear_desc(priv, p);

			tx_q->tx_skbuff_dma[i].buf = 0;
			tx_q->tx_skbuff_dma[i].map_as_page = false;
			tx_q->tx_skbuff_dma[i].len = 0;
			tx_q->tx_skbuff_dma[i].last_segment = false;
			tx_q->tx_skbuff[i] = NULL;
		}

		tx_q->dirty_tx = 0;
		tx_q->cur_tx = 0;
		tx_q->mss = 0;

		netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	return 0;
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret;

	ret = init_dma_rx_desc_rings(dev, flags);
	if (ret)
		return ret;

	ret = init_dma_tx_desc_rings(dev);

	stmmac_clear_descriptors(priv);

	if (netif_msg_hw(priv))
		stmmac_display_rings(priv);

	return ret;
}

/**
 * dma_free_rx_skbufs - free RX dma buffers
 * @priv: private structure
 * @queue: RX queue index
 */
static void dma_free_rx_skbufs(struct stmmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < priv->dma_rx_size; i++)
		stmmac_free_rx_buffer(priv, queue, i);
}

/**
 * dma_free_tx_skbufs - free TX dma buffers
 * @priv: private structure
 * @queue: TX queue index
 */
static void dma_free_tx_skbufs(struct stmmac_priv *priv, u32 queue)
{
	int i;

	for (i = 0; i < priv->dma_tx_size; i++)
		stmmac_free_tx_buffer(priv, queue, i);
}

/**
 * stmmac_free_tx_skbufs - free TX skb buffers
 * @priv: private structure
 */
static void stmmac_free_tx_skbufs(struct stmmac_priv *priv)
{
	u32 tx_queue_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < tx_queue_cnt; queue++)
		dma_free_tx_skbufs(priv, queue);
}

/**
 * free_dma_rx_desc_resources - free RX dma desc resources
 * @priv: private structure
 */
static void free_dma_rx_desc_resources(struct stmmac_priv *priv)
{
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 queue;

	/* Free RX queue resources */
	for (queue = 0; queue < rx_count; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		/* Release the DMA RX socket buffers */
		dma_free_rx_skbufs(priv, queue);

		/* Free DMA regions of consistent memory previously allocated */
		if (!priv->extend_desc)
			dma_free_coherent(priv->device, priv->dma_rx_size *
					  sizeof(struct dma_desc),
					  rx_q->dma_rx, rx_q->dma_rx_phy);
		else
			dma_free_coherent(priv->device, priv->dma_rx_size *
					  sizeof(struct dma_extended_desc),
					  rx_q->dma_erx, rx_q->dma_rx_phy);

		kfree(rx_q->buf_pool);
		if (rx_q->page_pool)
			page_pool_destroy(rx_q->page_pool);
	}
}

/**
 * free_dma_tx_desc_resources - free TX dma desc resources
 * @priv: private structure
 */
static void free_dma_tx_desc_resources(struct stmmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	/* Free TX queue resources */
	for (queue = 0; queue < tx_count; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

		/* Release the DMA TX socket buffers */
		dma_free_tx_skbufs(priv, queue);

		if (priv->extend_desc) {
			size = sizeof(struct dma_extended_desc);
			addr = tx_q->dma_etx;
		} else if (tx_q->tbs & STMMAC_TBS_AVAIL) {
			size = sizeof(struct dma_edesc);
			addr = tx_q->dma_entx;
		} else {
			size = sizeof(struct dma_desc);
			addr = tx_q->dma_tx;
		}

		size *= priv->dma_tx_size;

		dma_free_coherent(priv->device, size, addr, tx_q->dma_tx_phy);

		kfree(tx_q->tx_skbuff_dma);
		kfree(tx_q->tx_skbuff);
	}
}

/**
 * alloc_dma_rx_desc_resources - alloc RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_rx_desc_resources(struct stmmac_priv *priv)
{
	u32 rx_count = priv->plat->rx_queues_to_use;
	int ret = -ENOMEM;
	u32 queue;

	/* RX queues buffers and DMA */
	for (queue = 0; queue < rx_count; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
		struct page_pool_params pp_params = { 0 };
		unsigned int num_pages;

		rx_q->queue_index = queue;
		rx_q->priv_data = priv;

		pp_params.flags = PP_FLAG_DMA_MAP;
		pp_params.pool_size = priv->dma_rx_size;
		num_pages = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE);
		pp_params.order = ilog2(num_pages);
		pp_params.nid = dev_to_node(priv->device);
		pp_params.dev = priv->device;
		pp_params.dma_dir = DMA_FROM_DEVICE;

		rx_q->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rx_q->page_pool)) {
			ret = PTR_ERR(rx_q->page_pool);
			rx_q->page_pool = NULL;
			goto err_dma;
		}

		rx_q->buf_pool = kcalloc(priv->dma_rx_size,
					 sizeof(*rx_q->buf_pool),
					 GFP_KERNEL);
		if (!rx_q->buf_pool)
			goto err_dma;

		if (priv->extend_desc) {
			rx_q->dma_erx = dma_alloc_coherent(priv->device,
							   priv->dma_rx_size *
							   sizeof(struct dma_extended_desc),
							   &rx_q->dma_rx_phy,
							   GFP_KERNEL);
			if (!rx_q->dma_erx)
				goto err_dma;

		} else {
			rx_q->dma_rx = dma_alloc_coherent(priv->device,
							  priv->dma_rx_size *
							  sizeof(struct dma_desc),
							  &rx_q->dma_rx_phy,
							  GFP_KERNEL);
			if (!rx_q->dma_rx)
				goto err_dma;
		}
	}

	return 0;

err_dma:
	free_dma_rx_desc_resources(priv);

	return ret;
}

/**
 * alloc_dma_tx_desc_resources - alloc TX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_tx_desc_resources(struct stmmac_priv *priv)
{
	u32 tx_count = priv->plat->tx_queues_to_use;
	int ret = -ENOMEM;
	u32 queue;

	/* TX queues buffers and DMA */
	for (queue = 0; queue < tx_count; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
		size_t size;
		void *addr;

		tx_q->queue_index = queue;
		tx_q->priv_data = priv;

		tx_q->tx_skbuff_dma = kcalloc(priv->dma_tx_size,
					      sizeof(*tx_q->tx_skbuff_dma),
					      GFP_KERNEL);
		if (!tx_q->tx_skbuff_dma)
			goto err_dma;

		tx_q->tx_skbuff = kcalloc(priv->dma_tx_size,
					  sizeof(struct sk_buff *),
					  GFP_KERNEL);
		if (!tx_q->tx_skbuff)
			goto err_dma;

		if (priv->extend_desc)
			size = sizeof(struct dma_extended_desc);
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			size = sizeof(struct dma_edesc);
		else
			size = sizeof(struct dma_desc);

		size *= priv->dma_tx_size;

		addr = dma_alloc_coherent(priv->device, size,
					  &tx_q->dma_tx_phy, GFP_KERNEL);
		if (!addr)
			goto err_dma;

		if (priv->extend_desc)
			tx_q->dma_etx = addr;
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			tx_q->dma_entx = addr;
		else
			tx_q->dma_tx = addr;
	}

	return 0;

err_dma:
	free_dma_tx_desc_resources(priv);
	return ret;
}

/**
 * alloc_dma_desc_resources - alloc TX/RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_desc_resources(struct stmmac_priv *priv)
{
	/* RX Allocation */
	int ret = alloc_dma_rx_desc_resources(priv);

	if (ret)
		return ret;

	ret = alloc_dma_tx_desc_resources(priv);

	return ret;
}

/**
 * free_dma_desc_resources - free dma desc resources
 * @priv: private structure
 */
static void free_dma_desc_resources(struct stmmac_priv *priv)
{
	/* Release the DMA RX socket buffers */
	free_dma_rx_desc_resources(priv);

	/* Release the DMA TX socket buffers */
	free_dma_tx_desc_resources(priv);
}

/**
 *  stmmac_mac_enable_rx_queues - Enable MAC rx queues
 *  @priv: driver private structure
 *  Description: It is used for enabling the rx queues in the MAC
 */
static void stmmac_mac_enable_rx_queues(struct stmmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	int queue;
	u8 mode;

	for (queue = 0; queue < rx_queues_count; queue++) {
		mode = priv->plat->rx_queues_cfg[queue].mode_to_use;
		stmmac_rx_queue_enable(priv, priv->hw, mode, queue);
	}
}

/**
 * stmmac_start_rx_dma - start RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This starts a RX DMA channel
 */
static void stmmac_start_rx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes started in channel %d\n", chan);
	stmmac_start_rx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_start_tx_dma - start TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This starts a TX DMA channel
 */
static void stmmac_start_tx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes started in channel %d\n", chan);
	stmmac_start_tx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_stop_rx_dma - stop RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This stops a RX DMA channel
 */
static void stmmac_stop_rx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes stopped in channel %d\n", chan);
	stmmac_stop_rx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_stop_tx_dma - stop TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This stops a TX DMA channel
 */
static void stmmac_stop_tx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes stopped in channel %d\n", chan);
	stmmac_stop_tx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_start_all_dma - start all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This starts all the RX and TX DMA channels
 */
static void stmmac_start_all_dma(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_start_rx_dma(priv, chan);

	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_start_tx_dma(priv, chan);
}

/**
 * stmmac_stop_all_dma - stop all RX and TX DMA channels
 * @priv: driver private structure
 * Description:
 * This stops the RX and TX DMA channels
 */
static void stmmac_stop_all_dma(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;

	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_stop_rx_dma(priv, chan);

	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_stop_tx_dma(priv, chan);
}

/**
 *  stmmac_dma_operation_mode - HW DMA operation mode
 *  @priv: driver private structure
 *  Description: it is used for configuring the DMA operation mode register in
 *  order to program the tx/rx DMA thresholds or Store-And-Forward mode.
 */
static void stmmac_dma_operation_mode(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = 0;
	u32 rxmode = 0;
	u32 chan = 0;
	u8 qmode = 0;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	if (priv->plat->force_thresh_dma_mode) {
		txmode = tc;
		rxmode = tc;
	} else if (priv->plat->force_sf_dma_mode || priv->plat->tx_coe) {
		/*
		 * In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *    that needs to not insert csum in the TDES.
		 */
		txmode = SF_DMA_MODE;
		rxmode = SF_DMA_MODE;
		priv->xstats.threshold = SF_DMA_MODE;
	} else {
		txmode = tc;
		rxmode = SF_DMA_MODE;
	}

	/* configure all channels */
	for (chan = 0; chan < rx_channels_count; chan++) {
		qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;

		stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan,
				rxfifosz, qmode);
		stmmac_set_dma_bfsize(priv, priv->ioaddr, priv->dma_buf_sz,
				chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
		qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;

		stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				txfifosz, qmode);
	}
}

/**
 * stmmac_tx_clean - to manage the transmission completion
 * @priv: driver private structure
 * @budget: napi budget limiting this functions packet handling
 * @queue: TX queue index
 * Description: it reclaims the transmit resources after transmission completes.
 */
static int stmmac_tx_clean(struct stmmac_priv *priv, int budget, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	unsigned int bytes_compl = 0, pkts_compl = 0;
	unsigned int entry, count = 0;

	__netif_tx_lock_bh(netdev_get_tx_queue(priv->dev, queue));

	priv->xstats.tx_clean++;

	entry = tx_q->dirty_tx;
	while ((entry != tx_q->cur_tx) && (count < budget)) {
		struct sk_buff *skb = tx_q->tx_skbuff[entry];
		struct dma_desc *p;
		int status;

		if (priv->extend_desc)
			p = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			p = &tx_q->dma_entx[entry].basic;
		else
			p = tx_q->dma_tx + entry;

		status = stmmac_tx_status(priv, &priv->dev->stats,
				&priv->xstats, p, priv->ioaddr);
		/* Check if the descriptor is owned by the DMA */
		if (unlikely(status & tx_dma_own))
			break;

		count++;

		/* Make sure descriptor fields are read after reading
		 * the own bit.
		 */
		dma_rmb();

		/* Just consider the last segment and ...*/
		if (likely(!(status & tx_not_ls))) {
			/* ... verify the status error condition */
			if (unlikely(status & tx_err)) {
				priv->dev->stats.tx_errors++;
			} else {
				priv->dev->stats.tx_packets++;
				priv->xstats.tx_pkt_n++;
			}
			stmmac_get_tx_hwtstamp(priv, p, skb);
		}

		if (likely(tx_q->tx_skbuff_dma[entry].buf)) {
			if (tx_q->tx_skbuff_dma[entry].map_as_page)
				dma_unmap_page(priv->device,
					       tx_q->tx_skbuff_dma[entry].buf,
					       tx_q->tx_skbuff_dma[entry].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(priv->device,
						 tx_q->tx_skbuff_dma[entry].buf,
						 tx_q->tx_skbuff_dma[entry].len,
						 DMA_TO_DEVICE);
			tx_q->tx_skbuff_dma[entry].buf = 0;
			tx_q->tx_skbuff_dma[entry].len = 0;
			tx_q->tx_skbuff_dma[entry].map_as_page = false;
		}

		stmmac_clean_desc3(priv, tx_q, p);

		tx_q->tx_skbuff_dma[entry].last_segment = false;
		tx_q->tx_skbuff_dma[entry].is_jumbo = false;

		if (likely(skb != NULL)) {
			pkts_compl++;
			bytes_compl += skb->len;
			dev_consume_skb_any(skb);
			tx_q->tx_skbuff[entry] = NULL;
		}

		stmmac_release_tx_desc(priv, p, priv->mode);

		entry = STMMAC_GET_ENTRY(entry, priv->dma_tx_size);
	}
	tx_q->dirty_tx = entry;

	netdev_tx_completed_queue(netdev_get_tx_queue(priv->dev, queue),
				  pkts_compl, bytes_compl);

	if (unlikely(netif_tx_queue_stopped(netdev_get_tx_queue(priv->dev,
								queue))) &&
	    stmmac_tx_avail(priv, queue) > STMMAC_TX_THRESH(priv)) {

		netif_dbg(priv, tx_done, priv->dev,
			  "%s: restart transmit\n", __func__);
		netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	if ((priv->eee_enabled) && (!priv->tx_path_in_lpi_mode)) {
		stmmac_enable_eee_mode(priv);
		mod_timer(&priv->eee_ctrl_timer, STMMAC_LPI_T(priv->tx_lpi_timer));
	}

	/* We still have pending packets, let's call for a new scheduling */
	if (tx_q->dirty_tx != tx_q->cur_tx)
		mod_timer(&tx_q->txtimer, STMMAC_COAL_TIMER(priv->tx_coal_timer));

	__netif_tx_unlock_bh(netdev_get_tx_queue(priv->dev, queue));

	return count;
}

/**
 * stmmac_tx_err - to manage the tx error
 * @priv: driver private structure
 * @chan: channel index
 * Description: it cleans the descriptors and restarts the transmission
 * in case of transmission errors.
 */
static void stmmac_tx_err(struct stmmac_priv *priv, u32 chan)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[chan];

	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, chan));

	stmmac_stop_tx_dma(priv, chan);
	dma_free_tx_skbufs(priv, chan);
	stmmac_clear_tx_descriptors(priv, chan);
	tx_q->dirty_tx = 0;
	tx_q->cur_tx = 0;
	tx_q->mss = 0;
	netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, chan));
	stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    tx_q->dma_tx_phy, chan);
	stmmac_start_tx_dma(priv, chan);

	priv->dev->stats.tx_errors++;
	netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, chan));
}

/**
 *  stmmac_set_dma_operation_mode - Set DMA operation mode by channel
 *  @priv: driver private structure
 *  @txmode: TX operating mode
 *  @rxmode: RX operating mode
 *  @chan: channel index
 *  Description: it is used for configuring of the DMA operation mode in
 *  runtime in order to program the tx/rx DMA thresholds or Store-And-Forward
 *  mode.
 */
static void stmmac_set_dma_operation_mode(struct stmmac_priv *priv, u32 txmode,
					  u32 rxmode, u32 chan)
{
	u8 rxqmode = priv->plat->rx_queues_cfg[chan].mode_to_use;
	u8 txqmode = priv->plat->tx_queues_cfg[chan].mode_to_use;
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan, rxfifosz, rxqmode);
	stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan, txfifosz, txqmode);
}

static bool stmmac_safety_feat_interrupt(struct stmmac_priv *priv)
{
	int ret;

	ret = stmmac_safety_feat_irq_status(priv, priv->dev,
			priv->ioaddr, priv->dma_cap.asp, &priv->sstats);
	if (ret && (ret != -EINVAL)) {
		stmmac_global_err(priv);
		return true;
	}

	return false;
}

static int stmmac_napi_check(struct stmmac_priv *priv, u32 chan)
{
	int status = stmmac_dma_interrupt_status(priv, priv->ioaddr,
						 &priv->xstats, chan);
	struct stmmac_channel *ch = &priv->channel[chan];
	unsigned long flags;

	if ((status & handle_rx) && (chan < priv->plat->rx_queues_to_use)) {
		if (napi_schedule_prep(&ch->rx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			stmmac_disable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule(&ch->rx_napi);
		}
	}

	if ((status & handle_tx) && (chan < priv->plat->tx_queues_to_use)) {
		if (napi_schedule_prep(&ch->tx_napi)) {
			spin_lock_irqsave(&ch->lock, flags);
			stmmac_disable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
			spin_unlock_irqrestore(&ch->lock, flags);
			__napi_schedule(&ch->tx_napi);
		}
	}

	return status;
}

/**
 * stmmac_dma_interrupt - DMA ISR
 * @priv: driver private structure
 * Description: this is the DMA ISR. It is called by the main ISR.
 * It calls the dwmac dma routine and schedule poll method in case of some
 * work can be done.
 */
static void stmmac_dma_interrupt(struct stmmac_priv *priv)
{
	u32 tx_channel_count = priv->plat->tx_queues_to_use;
	u32 rx_channel_count = priv->plat->rx_queues_to_use;
	u32 channels_to_check = tx_channel_count > rx_channel_count ?
				tx_channel_count : rx_channel_count;
	u32 chan;
	int status[max_t(u32, MTL_MAX_TX_QUEUES, MTL_MAX_RX_QUEUES)];

	/* Make sure we never check beyond our status buffer. */
	if (WARN_ON_ONCE(channels_to_check > ARRAY_SIZE(status)))
		channels_to_check = ARRAY_SIZE(status);

	for (chan = 0; chan < channels_to_check; chan++)
		status[chan] = stmmac_napi_check(priv, chan);

	for (chan = 0; chan < tx_channel_count; chan++) {
		if (unlikely(status[chan] & tx_hard_error_bump_tc)) {
			/* Try to bump up the dma threshold on this failure */
			if (unlikely(priv->xstats.threshold != SF_DMA_MODE) &&
			    (tc <= 256)) {
				tc += 64;
				if (priv->plat->force_thresh_dma_mode)
					stmmac_set_dma_operation_mode(priv,
								      tc,
								      tc,
								      chan);
				else
					stmmac_set_dma_operation_mode(priv,
								    tc,
								    SF_DMA_MODE,
								    chan);
				priv->xstats.threshold = tc;
			}
		} else if (unlikely(status[chan] == tx_hard_error)) {
			stmmac_tx_err(priv, chan);
		}
	}
}

/**
 * stmmac_mmc_setup: setup the Mac Management Counters (MMC)
 * @priv: driver private structure
 * Description: this masks the MMC irq, in fact, the counters are managed in SW.
 */
static void stmmac_mmc_setup(struct stmmac_priv *priv)
{
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
			    MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	stmmac_mmc_intr_all_mask(priv, priv->mmcaddr);

	if (priv->dma_cap.rmon) {
		stmmac_mmc_ctrl(priv, priv->mmcaddr, mode);
		memset(&priv->mmc, 0, sizeof(struct stmmac_counters));
	} else
		netdev_info(priv->dev, "No MAC Management Counters available\n");
}

/**
 * stmmac_get_hw_features - get MAC capabilities from the HW cap. register.
 * @priv: driver private structure
 * Description:
 *  new GMAC chip generations have a new register to indicate the
 *  presence of the optional feature/functions.
 *  This can be also used to override the value passed through the
 *  platform and necessary for old MAC10/100 and GMAC chips.
 */
static int stmmac_get_hw_features(struct stmmac_priv *priv)
{
	return stmmac_get_hw_feature(priv, priv->ioaddr, &priv->dma_cap) == 0;
}

/**
 * stmmac_check_ether_addr - check if the MAC addr is valid
 * @priv: driver private structure
 * Description:
 * it is to verify if the MAC address is valid, in case of failures it
 * generates a random MAC address
 */
static void stmmac_check_ether_addr(struct stmmac_priv *priv)
{
	if (!is_valid_ether_addr(priv->dev->dev_addr)) {
		stmmac_get_umac_addr(priv, priv->hw, priv->dev->dev_addr, 0);
		if (!is_valid_ether_addr(priv->dev->dev_addr))
			eth_hw_addr_random(priv->dev);
		dev_info(priv->device, "device MAC address %pM\n",
			 priv->dev->dev_addr);
	}
}

/**
 * stmmac_init_dma_engine - DMA init.
 * @priv: driver private structure
 * Description:
 * It inits the DMA invoking the specific MAC/GMAC callback.
 * Some DMA parameters can be passed from the platform;
 * in case of these are not passed a default is kept for the MAC or GMAC.
 */
static int stmmac_init_dma_engine(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	struct stmmac_rx_queue *rx_q;
	struct stmmac_tx_queue *tx_q;
	u32 chan = 0;
	int atds = 0;
	int ret = 0;

	if (!priv->plat->dma_cfg || !priv->plat->dma_cfg->pbl) {
		dev_err(priv->device, "Invalid DMA configuration\n");
		return -EINVAL;
	}

	if (priv->extend_desc && (priv->mode == STMMAC_RING_MODE))
		atds = 1;

	ret = stmmac_reset(priv, priv->ioaddr);
	if (ret) {
		dev_err(priv->device, "Failed to reset the dma\n");
		return ret;
	}

	/* DMA Configuration */
	stmmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg, atds);

	if (priv->plat->axi)
		stmmac_axi(priv, priv->ioaddr, priv->plat->axi);

	/* DMA CSR Channel configuration */
	for (chan = 0; chan < dma_csr_ch; chan++)
		stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
		rx_q = &priv->rx_queue[chan];

		stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    rx_q->dma_rx_phy, chan);

		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
				     (priv->dma_rx_size *
				      sizeof(struct dma_desc));
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       rx_q->rx_tail_addr, chan);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
		tx_q = &priv->tx_queue[chan];

		stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    tx_q->dma_tx_phy, chan);

		tx_q->tx_tail_addr = tx_q->dma_tx_phy;
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);
	}

	return ret;
}

static void stmmac_tx_timer_arm(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

	mod_timer(&tx_q->txtimer, STMMAC_COAL_TIMER(priv->tx_coal_timer));
}

/**
 * stmmac_tx_timer - mitigation sw timer for tx.
 * @t: data pointer
 * Description:
 * This is the timer handler to directly invoke the stmmac_tx_clean.
 */
static void stmmac_tx_timer(struct timer_list *t)
{
	struct stmmac_tx_queue *tx_q = from_timer(tx_q, t, txtimer);
	struct stmmac_priv *priv = tx_q->priv_data;
	struct stmmac_channel *ch;

	ch = &priv->channel[tx_q->queue_index];

	if (likely(napi_schedule_prep(&ch->tx_napi))) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		stmmac_disable_dma_irq(priv, priv->ioaddr, ch->index, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
		__napi_schedule(&ch->tx_napi);
	}
}

/**
 * stmmac_init_coalesce - init mitigation options.
 * @priv: driver private structure
 * Description:
 * This inits the coalesce parameters: i.e. timer rate,
 * timer handler and default threshold used for enabling the
 * interrupt on completion bit.
 */
static void stmmac_init_coalesce(struct stmmac_priv *priv)
{
	u32 tx_channel_count = priv->plat->tx_queues_to_use;
	u32 chan;

	priv->tx_coal_frames = STMMAC_TX_FRAMES;
	priv->tx_coal_timer = STMMAC_COAL_TX_TIMER;
	priv->rx_coal_frames = STMMAC_RX_FRAMES;

	for (chan = 0; chan < tx_channel_count; chan++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[chan];

		timer_setup(&tx_q->txtimer, stmmac_tx_timer, 0);
	}
}

static void stmmac_set_rings_length(struct stmmac_priv *priv)
{
	u32 rx_channels_count = priv->plat->rx_queues_to_use;
	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan;

	/* set TX ring length */
	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_set_tx_ring_len(priv, priv->ioaddr,
				       (priv->dma_tx_size - 1), chan);

	/* set RX ring length */
	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_set_rx_ring_len(priv, priv->ioaddr,
				       (priv->dma_rx_size - 1), chan);
}

/**
 *  stmmac_set_tx_queue_weight - Set TX queue weight
 *  @priv: driver private structure
 *  Description: It is used for setting TX queues weight
 */
static void stmmac_set_tx_queue_weight(struct stmmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 weight;
	u32 queue;

	for (queue = 0; queue < tx_queues_count; queue++) {
		weight = priv->plat->tx_queues_cfg[queue].weight;
		stmmac_set_mtl_tx_queue_weight(priv, priv->hw, weight, queue);
	}
}

/**
 *  stmmac_configure_cbs - Configure CBS in TX queue
 *  @priv: driver private structure
 *  Description: It is used for configuring CBS in AVB TX queues
 */
static void stmmac_configure_cbs(struct stmmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 mode_to_use;
	u32 queue;

	/* queue 0 is reserved for legacy traffic */
	for (queue = 1; queue < tx_queues_count; queue++) {
		mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
		if (mode_to_use == MTL_QUEUE_DCB)
			continue;

		stmmac_config_cbs(priv, priv->hw,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
	}
}

/**
 *  stmmac_rx_queue_dma_chan_map - Map RX queue to RX dma channel
 *  @priv: driver private structure
 *  Description: It is used for mapping RX queues to RX dma channels
 */
static void stmmac_rx_queue_dma_chan_map(struct stmmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 chan;

	for (queue = 0; queue < rx_queues_count; queue++) {
		chan = priv->plat->rx_queues_cfg[queue].chan;
		stmmac_map_mtl_to_dma(priv, priv->hw, queue, chan);
	}
}

/**
 *  stmmac_mac_config_rx_queues_prio - Configure RX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX Queue Priority
 */
static void stmmac_mac_config_rx_queues_prio(struct stmmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u32 prio;

	for (queue = 0; queue < rx_queues_count; queue++) {
		if (!priv->plat->rx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->rx_queues_cfg[queue].prio;
		stmmac_rx_queue_prio(priv, priv->hw, prio, queue);
	}
}

/**
 *  stmmac_mac_config_tx_queues_prio - Configure TX Queue priority
 *  @priv: driver private structure
 *  Description: It is used for configuring the TX Queue Priority
 */
static void stmmac_mac_config_tx_queues_prio(struct stmmac_priv *priv)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 queue;
	u32 prio;

	for (queue = 0; queue < tx_queues_count; queue++) {
		if (!priv->plat->tx_queues_cfg[queue].use_prio)
			continue;

		prio = priv->plat->tx_queues_cfg[queue].prio;
		stmmac_tx_queue_prio(priv, priv->hw, prio, queue);
	}
}

/**
 *  stmmac_mac_config_rx_queues_routing - Configure RX Queue Routing
 *  @priv: driver private structure
 *  Description: It is used for configuring the RX queue routing
 */
static void stmmac_mac_config_rx_queues_routing(struct stmmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 queue;
	u8 packet;

	for (queue = 0; queue < rx_queues_count; queue++) {
		/* no specific packet type routing specified for the queue */
		if (priv->plat->rx_queues_cfg[queue].pkt_route == 0x0)
			continue;

		packet = priv->plat->rx_queues_cfg[queue].pkt_route;
		stmmac_rx_queue_routing(priv, priv->hw, packet, queue);
	}
}

static void stmmac_mac_config_rss(struct stmmac_priv *priv)
{
	if (!priv->dma_cap.rssen || !priv->plat->rss_en) {
		priv->rss.enable = false;
		return;
	}

	if (priv->dev->features & NETIF_F_RXHASH)
		priv->rss.enable = true;
	else
		priv->rss.enable = false;

	stmmac_rss_configure(priv, priv->hw, &priv->rss,
			     priv->plat->rx_queues_to_use);
}

/**
 *  stmmac_mtl_configuration - Configure MTL
 *  @priv: driver private structure
 *  Description: It is used for configurring MTL
 */
static void stmmac_mtl_configuration(struct stmmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 tx_queues_count = priv->plat->tx_queues_to_use;

	if (tx_queues_count > 1)
		stmmac_set_tx_queue_weight(priv);

	/* Configure MTL RX algorithms */
	if (rx_queues_count > 1)
		stmmac_prog_mtl_rx_algorithms(priv, priv->hw,
				priv->plat->rx_sched_algorithm);

	/* Configure MTL TX algorithms */
	if (tx_queues_count > 1)
		stmmac_prog_mtl_tx_algorithms(priv, priv->hw,
				priv->plat->tx_sched_algorithm);

	/* Configure CBS in AVB TX queues */
	if (tx_queues_count > 1)
		stmmac_configure_cbs(priv);

	/* Map RX MTL to DMA channels */
	stmmac_rx_queue_dma_chan_map(priv);

	/* Enable MAC RX Queues */
	stmmac_mac_enable_rx_queues(priv);

	/* Set RX priorities */
	if (rx_queues_count > 1)
		stmmac_mac_config_rx_queues_prio(priv);

	/* Set TX priorities */
	if (tx_queues_count > 1)
		stmmac_mac_config_tx_queues_prio(priv);

	/* Set RX routing */
	if (rx_queues_count > 1)
		stmmac_mac_config_rx_queues_routing(priv);

	/* Receive Side Scaling */
	if (rx_queues_count > 1)
		stmmac_mac_config_rss(priv);
}

static void stmmac_safety_feat_configuration(struct stmmac_priv *priv)
{
	if (priv->dma_cap.asp) {
		netdev_info(priv->dev, "Enabling Safety Features\n");
		stmmac_safety_feat_config(priv, priv->ioaddr, priv->dma_cap.asp);
	} else {
		netdev_info(priv->dev, "No Safety Features support found\n");
	}
}

/**
 * stmmac_hw_setup - setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  @init_ptp: initialize PTP if set
 *  Description:
 *  this is the main function to setup the HW in a usable state because the
 *  dma engine is reset, the core registers are configured (e.g. AXI,
 *  Checksum features, timers). The DMA is ready to start receiving and
 *  transmitting.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_hw_setup(struct net_device *dev, bool init_ptp)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 chan;
	int ret;

	/* DMA initialization and SW reset */
	ret = stmmac_init_dma_engine(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA engine initialization failed\n",
			   __func__);
		return ret;
	}

	/* Copy the MAC addr into the HW  */
	stmmac_set_umac_addr(priv, priv->hw, dev->dev_addr, 0);

	/* PS and related bits will be programmed according to the speed */
	if (priv->hw->pcs) {
		int speed = priv->plat->mac_port_sel_speed;

		if ((speed == SPEED_10) || (speed == SPEED_100) ||
		    (speed == SPEED_1000)) {
			priv->hw->ps = speed;
		} else {
			dev_warn(priv->device, "invalid port speed\n");
			priv->hw->ps = 0;
		}
	}

	/* Initialize the MAC Core */
	stmmac_core_init(priv, priv->hw, dev);

	/* Initialize MTL*/
	stmmac_mtl_configuration(priv);

	/* Initialize Safety Features */
	stmmac_safety_feat_configuration(priv);

	ret = stmmac_rx_ipc(priv, priv->hw);
	if (!ret) {
		netdev_warn(priv->dev, "RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = STMMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
	}

	/* Enable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, true);

	/* Set the HW DMA mode and the COE */
	stmmac_dma_operation_mode(priv);

	stmmac_mmc_setup(priv);

	if (init_ptp) {
		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0)
			netdev_warn(priv->dev, "failed to enable PTP reference clock: %d\n", ret);

		ret = stmmac_init_ptp(priv);
		if (ret == -EOPNOTSUPP)
			netdev_warn(priv->dev, "PTP not supported by HW\n");
		else if (ret)
			netdev_warn(priv->dev, "PTP init failed\n");
	}

	priv->eee_tw_timer = STMMAC_DEFAULT_TWT_LS;

	/* Convert the timer from msec to usec */
	if (!priv->tx_lpi_timer)
		priv->tx_lpi_timer = eee_timer * 1000;

	if (priv->use_riwt) {
		if (!priv->rx_riwt)
			priv->rx_riwt = DEF_DMA_RIWT;

		ret = stmmac_rx_watchdog(priv, priv->ioaddr, priv->rx_riwt, rx_cnt);
	}

	if (priv->hw->pcs)
		stmmac_pcs_ctrl_ane(priv, priv->ioaddr, 1, priv->hw->ps, 0);

	/* set TX and RX rings length */
	stmmac_set_rings_length(priv);

	/* Enable TSO */
	if (priv->tso) {
		for (chan = 0; chan < tx_cnt; chan++) {
			struct stmmac_tx_queue *tx_q = &priv->tx_queue[chan];

			/* TSO and TBS cannot co-exist */
			if (tx_q->tbs & STMMAC_TBS_AVAIL)
				continue;

			stmmac_enable_tso(priv, priv->ioaddr, 1, chan);
		}
	}

	/* Enable Split Header */
	if (priv->sph && priv->hw->rx_csum) {
		for (chan = 0; chan < rx_cnt; chan++)
			stmmac_enable_sph(priv, priv->ioaddr, 1, chan);
	}

	/* VLAN Tag Insertion */
	if (priv->dma_cap.vlins)
		stmmac_enable_vlan(priv, priv->hw, STMMAC_VLAN_INSERT);

	/* TBS */
	for (chan = 0; chan < tx_cnt; chan++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[chan];
		int enable = tx_q->tbs & STMMAC_TBS_AVAIL;

		stmmac_enable_tbs(priv, priv->ioaddr, enable, chan);
	}

	/* Configure real RX and TX queues */
	netif_set_real_num_rx_queues(dev, priv->plat->rx_queues_to_use);
	netif_set_real_num_tx_queues(dev, priv->plat->tx_queues_to_use);

	/* Start the ball rolling... */
	stmmac_start_all_dma(priv);

	return 0;
}

static void stmmac_hw_teardown(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	clk_disable_unprepare(priv->plat->clk_ptp_ref);
}

/**
 *  stmmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_open(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int bfsize = 0;
	u32 chan;
	int ret;

	if (priv->hw->pcs != STMMAC_PCS_TBI &&
	    priv->hw->pcs != STMMAC_PCS_RTBI &&
	    priv->hw->xpcs == NULL) {
		ret = stmmac_init_phy(dev);
		if (ret) {
			netdev_err(priv->dev,
				   "%s: Cannot attach to PHY (error: %d)\n",
				   __func__, ret);
			return ret;
		}
	}

	/* Extra statistics */
	memset(&priv->xstats, 0, sizeof(struct stmmac_extra_stats));
	priv->xstats.threshold = tc;

	bfsize = stmmac_set_16kib_bfsize(priv, dev->mtu);
	if (bfsize < 0)
		bfsize = 0;

	if (bfsize < BUF_SIZE_16KiB)
		bfsize = stmmac_set_bfsize(dev->mtu, priv->dma_buf_sz);

	priv->dma_buf_sz = bfsize;
	buf_sz = bfsize;

	priv->rx_copybreak = STMMAC_RX_COPYBREAK;

	if (!priv->dma_tx_size)
		priv->dma_tx_size = DMA_DEFAULT_TX_SIZE;
	if (!priv->dma_rx_size)
		priv->dma_rx_size = DMA_DEFAULT_RX_SIZE;

	/* Earlier check for TBS */
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[chan];
		int tbs_en = priv->plat->tx_queues_cfg[chan].tbs_en;

		/* Setup per-TXQ tbs flag before TX descriptor alloc */
		tx_q->tbs |= tbs_en ? STMMAC_TBS_AVAIL : 0;
	}

	ret = alloc_dma_desc_resources(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors allocation failed\n",
			   __func__);
		goto dma_desc_error;
	}

	ret = init_dma_desc_rings(dev, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors initialization failed\n",
			   __func__);
		goto init_error;
	}

	ret = stmmac_hw_setup(dev, true);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		goto init_error;
	}

	stmmac_init_coalesce(priv);

	phylink_start(priv->phylink);
	/* We may have called phylink_speed_down before */
	phylink_speed_up(priv->phylink);

	/* Request the IRQ lines */
	ret = request_irq(dev->irq, stmmac_interrupt,
			  IRQF_SHARED, dev->name, dev);
	if (unlikely(ret < 0)) {
		netdev_err(priv->dev,
			   "%s: ERROR: allocating the IRQ %d (error: %d)\n",
			   __func__, dev->irq, ret);
		goto irq_error;
	}

	/* Request the Wake IRQ in case of another line is used for WoL */
	if (priv->wol_irq != dev->irq) {
		ret = request_irq(priv->wol_irq, stmmac_interrupt,
				  IRQF_SHARED, dev->name, dev);
		if (unlikely(ret < 0)) {
			netdev_err(priv->dev,
				   "%s: ERROR: allocating the WoL IRQ %d (%d)\n",
				   __func__, priv->wol_irq, ret);
			goto wolirq_error;
		}
	}

	/* Request the IRQ lines */
	if (priv->lpi_irq > 0) {
		ret = request_irq(priv->lpi_irq, stmmac_interrupt, IRQF_SHARED,
				  dev->name, dev);
		if (unlikely(ret < 0)) {
			netdev_err(priv->dev,
				   "%s: ERROR: allocating the LPI IRQ %d (%d)\n",
				   __func__, priv->lpi_irq, ret);
			goto lpiirq_error;
		}
	}

	stmmac_enable_all_queues(priv);
	netif_tx_start_all_queues(priv->dev);

	return 0;

lpiirq_error:
	if (priv->wol_irq != dev->irq)
		free_irq(priv->wol_irq, dev);
wolirq_error:
	free_irq(dev->irq, dev);
irq_error:
	phylink_stop(priv->phylink);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		del_timer_sync(&priv->tx_queue[chan].txtimer);

	stmmac_hw_teardown(dev);
init_error:
	free_dma_desc_resources(priv);
dma_desc_error:
	phylink_disconnect_phy(priv->phylink);
	return ret;
}

/**
 *  stmmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int stmmac_release(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 chan;

	if (device_may_wakeup(priv->device))
		phylink_speed_down(priv->phylink, false);
	/* Stop and disconnect the PHY */
	phylink_stop(priv->phylink);
	phylink_disconnect_phy(priv->phylink);

	stmmac_disable_all_queues(priv);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		del_timer_sync(&priv->tx_queue[chan].txtimer);

	/* Free the IRQ lines */
	free_irq(dev->irq, dev);
	if (priv->wol_irq != dev->irq)
		free_irq(priv->wol_irq, dev);
	if (priv->lpi_irq > 0)
		free_irq(priv->lpi_irq, dev);

	if (priv->eee_enabled) {
		priv->tx_path_in_lpi_mode = false;
		del_timer_sync(&priv->eee_ctrl_timer);
	}

	/* Stop TX/RX DMA and clear the descriptors */
	stmmac_stop_all_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv);

	/* Disable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, false);

	netif_carrier_off(dev);

	stmmac_release_ptp(priv);

	return 0;
}

static bool stmmac_vlan_insert(struct stmmac_priv *priv, struct sk_buff *skb,
			       struct stmmac_tx_queue *tx_q)
{
	u16 tag = 0x0, inner_tag = 0x0;
	u32 inner_type = 0x0;
	struct dma_desc *p;

	if (!priv->dma_cap.vlins)
		return false;
	if (!skb_vlan_tag_present(skb))
		return false;
	if (skb->vlan_proto == htons(ETH_P_8021AD)) {
		inner_tag = skb_vlan_tag_get(skb);
		inner_type = STMMAC_VLAN_INSERT;
	}

	tag = skb_vlan_tag_get(skb);

	if (tx_q->tbs & STMMAC_TBS_AVAIL)
		p = &tx_q->dma_entx[tx_q->cur_tx].basic;
	else
		p = &tx_q->dma_tx[tx_q->cur_tx];

	if (stmmac_set_desc_vlan_tag(priv, p, tag, inner_tag, inner_type))
		return false;

	stmmac_set_tx_owner(priv, p);
	tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, priv->dma_tx_size);
	return true;
}

/**
 *  stmmac_tso_allocator - close entry point of the driver
 *  @priv: driver private structure
 *  @des: buffer start address
 *  @total_len: total length to fill in descriptors
 *  @last_segment: condition for the last descriptor
 *  @queue: TX queue index
 *  Description:
 *  This function fills descriptor and request new descriptors according to
 *  buffer length to fill
 */
static void stmmac_tso_allocator(struct stmmac_priv *priv, dma_addr_t des,
				 int total_len, bool last_segment, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];
	struct dma_desc *desc;
	u32 buff_size;
	int tmp_len;

	tmp_len = total_len;

	while (tmp_len > 0) {
		dma_addr_t curr_addr;

		tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx,
						priv->dma_tx_size);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);

		if (tx_q->tbs & STMMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		curr_addr = des + (total_len - tmp_len);
		if (priv->dma_cap.addr64 <= 32)
			desc->des0 = cpu_to_le32(curr_addr);
		else
			stmmac_set_desc_addr(priv, desc, curr_addr);

		buff_size = tmp_len >= TSO_MAX_BUFF_SIZE ?
			    TSO_MAX_BUFF_SIZE : tmp_len;

		stmmac_prepare_tso_tx_desc(priv, desc, 0, buff_size,
				0, 1,
				(last_segment) && (tmp_len <= TSO_MAX_BUFF_SIZE),
				0, 0);

		tmp_len -= TSO_MAX_BUFF_SIZE;
	}
}

/**
 *  stmmac_tso_xmit - Tx entry point of the driver for oversized frames (TSO)
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description: this is the transmit function that is called on TSO frames
 *  (support available on GMAC4 and newer chips).
 *  Diagram below show the ring programming in case of TSO frames:
 *
 *  First Descriptor
 *   --------
 *   | DES0 |---> buffer1 = L2/L3/L4 header
 *   | DES1 |---> TCP Payload (can continue on next descr...)
 *   | DES2 |---> buffer 1 and 2 len
 *   | DES3 |---> must set TSE, TCP hdr len-> [22:19]. TCP payload len [17:0]
 *   --------
 *	|
 *     ...
 *	|
 *   --------
 *   | DES0 | --| Split TCP Payload on Buffers 1 and 2
 *   | DES1 | --|
 *   | DES2 | --> buffer 1 and 2 len
 *   | DES3 |
 *   --------
 *
 * mss is fixed when enable tso, so w/o programming the TDES3 ctx field.
 */
static netdev_tx_t stmmac_tso_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dma_desc *desc, *first, *mss_desc = NULL;
	struct stmmac_priv *priv = netdev_priv(dev);
	int desc_size, tmp_pay_len = 0, first_tx;
	int nfrags = skb_shinfo(skb)->nr_frags;
	u32 queue = skb_get_queue_mapping(skb);
	unsigned int first_entry, tx_packets;
	struct stmmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	u8 proto_hdr_len, hdr;
	u32 pay_len, mss;
	dma_addr_t des;
	int i;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	/* Compute header lengths */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		proto_hdr_len = skb_transport_offset(skb) + sizeof(struct udphdr);
		hdr = sizeof(struct udphdr);
	} else {
		proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		hdr = tcp_hdrlen(skb);
	}

	/* Desc availability based on threshold should be enough safe */
	if (unlikely(stmmac_tx_avail(priv, queue) <
		(((skb->len - proto_hdr_len) / TSO_MAX_BUFF_SIZE + 1)))) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	pay_len = skb_headlen(skb) - proto_hdr_len; /* no frags */

	mss = skb_shinfo(skb)->gso_size;

	/* set new MSS value if needed */
	if (mss != tx_q->mss) {
		if (tx_q->tbs & STMMAC_TBS_AVAIL)
			mss_desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			mss_desc = &tx_q->dma_tx[tx_q->cur_tx];

		stmmac_set_mss(priv, mss_desc, mss);
		tx_q->mss = mss;
		tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx,
						priv->dma_tx_size);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	if (netif_msg_tx_queued(priv)) {
		pr_info("%s: hdrlen %d, hdr_len %d, pay_len %d, mss %d\n",
			__func__, hdr, proto_hdr_len, pay_len, mss);
		pr_info("\tskb->len %d, skb->data_len %d\n", skb->len,
			skb->data_len);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = stmmac_vlan_insert(priv, skb, tx_q);

	first_entry = tx_q->cur_tx;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	if (tx_q->tbs & STMMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[first_entry].basic;
	else
		desc = &tx_q->dma_tx[first_entry];
	first = desc;

	if (has_vlan)
		stmmac_set_desc_vlan(priv, first, STMMAC_VLAN_INSERT);

	/* first descriptor: fill Headers on Buf1 */
	des = dma_map_single(priv->device, skb->data, skb_headlen(skb),
			     DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, des))
		goto dma_map_err;

	tx_q->tx_skbuff_dma[first_entry].buf = des;
	tx_q->tx_skbuff_dma[first_entry].len = skb_headlen(skb);

	if (priv->dma_cap.addr64 <= 32) {
		first->des0 = cpu_to_le32(des);

		/* Fill start of payload in buff2 of first descriptor */
		if (pay_len)
			first->des1 = cpu_to_le32(des + proto_hdr_len);

		/* If needed take extra descriptors to fill the remaining payload */
		tmp_pay_len = pay_len - TSO_MAX_BUFF_SIZE;
	} else {
		stmmac_set_desc_addr(priv, first, des);
		tmp_pay_len = pay_len;
		des += proto_hdr_len;
		pay_len = 0;
	}

	stmmac_tso_allocator(priv, des, tmp_pay_len, (nfrags == 0), queue);

	/* Prepare fragments */
	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		des = skb_frag_dma_map(priv->device, frag, 0,
				       skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		stmmac_tso_allocator(priv, des, skb_frag_size(frag),
				     (i == nfrags - 1), queue);

		tx_q->tx_skbuff_dma[tx_q->cur_tx].buf = des;
		tx_q->tx_skbuff_dma[tx_q->cur_tx].len = skb_frag_size(frag);
		tx_q->tx_skbuff_dma[tx_q->cur_tx].map_as_page = true;
	}

	tx_q->tx_skbuff_dma[tx_q->cur_tx].last_segment = true;

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[tx_q->cur_tx] = skb;

	/* Manage tx mitigation */
	tx_packets = (tx_q->cur_tx + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;

	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;

	if (set_ic) {
		if (tx_q->tbs & STMMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[tx_q->cur_tx].basic;
		else
			desc = &tx_q->dma_tx[tx_q->cur_tx];

		tx_q->tx_count_frames = 0;
		stmmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and stmmac_tx_clean may clean up to this descriptor.
	 */
	tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, priv->dma_tx_size);

	if (unlikely(stmmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;
	priv->xstats.tx_tso_frames++;
	priv->xstats.tx_tso_nfrags += nfrags;

	if (priv->sarc_type)
		stmmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     priv->hwts_tx_en)) {
		/* declare that device is doing timestamping */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		stmmac_enable_tx_timestamp(priv, first);
	}

	/* Complete the first descriptor before granting the DMA */
	stmmac_prepare_tso_tx_desc(priv, first, 1,
			proto_hdr_len,
			pay_len,
			1, tx_q->tx_skbuff_dma[first_entry].last_segment,
			hdr / 4, (skb->len - proto_hdr_len));

	/* If context desc is used to change MSS */
	if (mss_desc) {
		/* Make sure that first descriptor has been completely
		 * written, including its own bit. This is because MSS is
		 * actually before first descriptor, so we need to make
		 * sure that MSS's own bit is the last thing written.
		 */
		dma_wmb();
		stmmac_set_tx_owner(priv, mss_desc);
	}

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	if (netif_msg_pktdata(priv)) {
		pr_info("%s: curr=%d dirty=%d f=%d, e=%d, f_p=%p, nfrags %d\n",
			__func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			tx_q->cur_tx, first, nfrags);
		pr_info(">>> frame to be transmitted: ");
		print_pkt(skb->data, skb_headlen(skb));
	}

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	if (tx_q->tbs & STMMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
	stmmac_tx_timer_arm(priv, queue);

	return NETDEV_TX_OK;

dma_map_err:
	dev_err(priv->device, "Tx dma map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

/**
 *  stmmac_xmit - Tx entry point of the driver
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : this is the tx entry point of the driver.
 *  It programs the chain or the ring and supports oversized frames
 *  and SG feature.
 */
static netdev_tx_t stmmac_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int first_entry, tx_packets, enh_desc;
	struct stmmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int i, csum_insertion = 0, is_jumbo = 0;
	u32 queue = skb_get_queue_mapping(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	int gso = skb_shinfo(skb)->gso_type;
	struct dma_edesc *tbs_desc = NULL;
	int entry, desc_size, first_tx;
	struct dma_desc *desc, *first;
	struct stmmac_tx_queue *tx_q;
	bool has_vlan, set_ic;
	dma_addr_t des;

	tx_q = &priv->tx_queue[queue];
	first_tx = tx_q->cur_tx;

	if (priv->tx_path_in_lpi_mode)
		stmmac_disable_eee_mode(priv);

	/* Manage oversized TCP frames for GMAC4 device */
	if (skb_is_gso(skb) && priv->tso) {
		if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
			return stmmac_tso_xmit(skb, dev);
		if (priv->plat->has_gmac4 && (gso & SKB_GSO_UDP_L4))
			return stmmac_tso_xmit(skb, dev);
	}

	if (unlikely(stmmac_tx_avail(priv, queue) < nfrags + 1)) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		return NETDEV_TX_BUSY;
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = stmmac_vlan_insert(priv, skb, tx_q);

	entry = tx_q->cur_tx;
	first_entry = entry;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);

	if (likely(priv->extend_desc))
		desc = (struct dma_desc *)(tx_q->dma_etx + entry);
	else if (tx_q->tbs & STMMAC_TBS_AVAIL)
		desc = &tx_q->dma_entx[entry].basic;
	else
		desc = tx_q->dma_tx + entry;

	first = desc;

	if (has_vlan)
		stmmac_set_desc_vlan(priv, first, STMMAC_VLAN_INSERT);

	enh_desc = priv->plat->enh_desc;
	/* To program the descriptors according to the size of the frame */
	if (enh_desc)
		is_jumbo = stmmac_is_jumbo_frm(priv, skb->len, enh_desc);

	if (unlikely(is_jumbo)) {
		entry = stmmac_jumbo_frm(priv, tx_q, skb, csum_insertion);
		if (unlikely(entry < 0) && (entry != -EINVAL))
			goto dma_map_err;
	}

	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);
		bool last_segment = (i == (nfrags - 1));

		entry = STMMAC_GET_ENTRY(entry, priv->dma_tx_size);
		WARN_ON(tx_q->tx_skbuff[entry]);

		if (likely(priv->extend_desc))
			desc = (struct dma_desc *)(tx_q->dma_etx + entry);
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = tx_q->dma_tx + entry;

		des = skb_frag_dma_map(priv->device, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err; /* should reuse desc w/o issues */

		tx_q->tx_skbuff_dma[entry].buf = des;

		stmmac_set_desc_addr(priv, desc, des);

		tx_q->tx_skbuff_dma[entry].map_as_page = true;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].last_segment = last_segment;

		/* Prepare the descriptor and set the own bit too */
		stmmac_prepare_tx_desc(priv, desc, 0, len, csum_insertion,
				priv->mode, 1, last_segment, skb->len);
	}

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[entry] = skb;

	/* According to the coalesce parameter the IC bit for the latest
	 * segment is reset and the timer re-started to clean the tx status.
	 * This approach takes care about the fragments: desc is the first
	 * element in case of no SG.
	 */
	tx_packets = (entry + 1) - first_tx;
	tx_q->tx_count_frames += tx_packets;

	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && priv->hwts_tx_en)
		set_ic = true;
	else if (!priv->tx_coal_frames)
		set_ic = false;
	else if (tx_packets > priv->tx_coal_frames)
		set_ic = true;
	else if ((tx_q->tx_count_frames % priv->tx_coal_frames) < tx_packets)
		set_ic = true;
	else
		set_ic = false;

	if (set_ic) {
		if (likely(priv->extend_desc))
			desc = &tx_q->dma_etx[entry].basic;
		else if (tx_q->tbs & STMMAC_TBS_AVAIL)
			desc = &tx_q->dma_entx[entry].basic;
		else
			desc = &tx_q->dma_tx[entry];

		tx_q->tx_count_frames = 0;
		stmmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and stmmac_tx_clean may clean up to this descriptor.
	 */
	entry = STMMAC_GET_ENTRY(entry, priv->dma_tx_size);
	tx_q->cur_tx = entry;

	if (netif_msg_pktdata(priv)) {
		netdev_dbg(priv->dev,
			   "%s: curr=%d dirty=%d f=%d, e=%d, first=%p, nfrags=%d",
			   __func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			   entry, first, nfrags);

		netdev_dbg(priv->dev, ">>> frame to be transmitted: ");
		print_pkt(skb->data, skb->len);
	}

	if (unlikely(stmmac_tx_avail(priv, queue) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;

	if (priv->sarc_type)
		stmmac_set_desc_sarc(priv, first, priv->sarc_type);

	skb_tx_timestamp(skb);

	/* Ready to fill the first descriptor and set the OWN bit w/o any
	 * problems because all the descriptors are actually ready to be
	 * passed to the DMA engine.
	 */
	if (likely(!is_jumbo)) {
		bool last_segment = (nfrags == 0);

		des = dma_map_single(priv->device, skb->data,
				     nopaged_len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->device, des))
			goto dma_map_err;

		tx_q->tx_skbuff_dma[first_entry].buf = des;

		stmmac_set_desc_addr(priv, first, des);

		tx_q->tx_skbuff_dma[first_entry].len = nopaged_len;
		tx_q->tx_skbuff_dma[first_entry].last_segment = last_segment;

		if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			     priv->hwts_tx_en)) {
			/* declare that device is doing timestamping */
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			stmmac_enable_tx_timestamp(priv, first);
		}

		/* Prepare the first descriptor setting the OWN bit too */
		stmmac_prepare_tx_desc(priv, first, 1, nopaged_len,
				csum_insertion, priv->mode, 0, last_segment,
				skb->len);
	}

	if (tx_q->tbs & STMMAC_TBS_EN) {
		struct timespec64 ts = ns_to_timespec64(skb->tstamp);

		tbs_desc = &tx_q->dma_entx[first_entry];
		stmmac_set_desc_tbs(priv, tbs_desc, ts.tv_sec, ts.tv_nsec);
	}

	stmmac_set_tx_owner(priv, first);

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	stmmac_enable_dma_transmission(priv, priv->ioaddr);

	if (likely(priv->extend_desc))
		desc_size = sizeof(struct dma_extended_desc);
	else if (tx_q->tbs & STMMAC_TBS_AVAIL)
		desc_size = sizeof(struct dma_edesc);
	else
		desc_size = sizeof(struct dma_desc);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
	stmmac_tx_timer_arm(priv, queue);

	return NETDEV_TX_OK;

dma_map_err:
	netdev_err(priv->dev, "Tx DMA map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void stmmac_rx_vlan(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_ethhdr *veth;
	__be16 vlan_proto;
	u16 vlanid;

	veth = (struct vlan_ethhdr *)skb->data;
	vlan_proto = veth->h_vlan_proto;

	if ((vlan_proto == htons(ETH_P_8021Q) &&
	     dev->features & NETIF_F_HW_VLAN_CTAG_RX) ||
	    (vlan_proto == htons(ETH_P_8021AD) &&
	     dev->features & NETIF_F_HW_VLAN_STAG_RX)) {
		/* pop the vlan tag */
		vlanid = ntohs(veth->h_vlan_TCI);
		memmove(skb->data + VLAN_HLEN, veth, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
		__vlan_hwaccel_put_tag(skb, vlan_proto, vlanid);
	}
}

/**
 * stmmac_rx_refill - refill used skb preallocated buffers
 * @priv: driver private structure
 * @queue: RX queue index
 * Description : this is to reallocate the skb for the reception process
 * that is based on zero-copy.
 */
static inline void stmmac_rx_refill(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	int len, dirty = stmmac_rx_dirty(priv, queue);
	unsigned int entry = rx_q->dirty_rx;

	len = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE) * PAGE_SIZE;

	while (dirty-- > 0) {
		struct stmmac_rx_buffer *buf = &rx_q->buf_pool[entry];
		struct dma_desc *p;
		bool use_rx_wd;

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		if (!buf->page) {
			buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->page)
				break;
		}

		if (priv->sph && !buf->sec_page) {
			buf->sec_page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->sec_page)
				break;

			buf->sec_addr = page_pool_get_dma_addr(buf->sec_page);

			dma_sync_single_for_device(priv->device, buf->sec_addr,
						   len, DMA_FROM_DEVICE);
		}

		buf->addr = page_pool_get_dma_addr(buf->page);

		/* Sync whole allocation to device. This will invalidate old
		 * data.
		 */
		dma_sync_single_for_device(priv->device, buf->addr, len,
					   DMA_FROM_DEVICE);

		stmmac_set_desc_addr(priv, p, buf->addr);
		if (priv->sph)
			stmmac_set_desc_sec_addr(priv, p, buf->sec_addr, true);
		else
			stmmac_set_desc_sec_addr(priv, p, buf->sec_addr, false);
		stmmac_refill_desc3(priv, rx_q, p);

		rx_q->rx_count_frames++;
		rx_q->rx_count_frames += priv->rx_coal_frames;
		if (rx_q->rx_count_frames > priv->rx_coal_frames)
			rx_q->rx_count_frames = 0;

		use_rx_wd = !priv->rx_coal_frames;
		use_rx_wd |= rx_q->rx_count_frames > 0;
		if (!priv->use_riwt)
			use_rx_wd = false;

		dma_wmb();
		stmmac_set_rx_owner(priv, p, use_rx_wd);

		entry = STMMAC_GET_ENTRY(entry, priv->dma_rx_size);
	}
	rx_q->dirty_rx = entry;
	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (rx_q->dirty_rx * sizeof(struct dma_desc));
	stmmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, queue);
}

static unsigned int stmmac_rx_buf1_len(struct stmmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	unsigned int plen = 0, hlen = 0;
	int coe = priv->hw->rx_csum;

	/* Not first descriptor, buffer is always zero */
	if (priv->sph && len)
		return 0;

	/* First descriptor, get split header length */
	stmmac_get_rx_header_len(priv, p, &hlen);
	if (priv->sph && hlen) {
		priv->xstats.rx_split_hdr_pkt_n++;
		return hlen;
	}

	/* First descriptor, not last descriptor and not split header */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = stmmac_get_rx_frame_len(priv, p, coe);

	/* First descriptor and last descriptor and not split header */
	return min_t(unsigned int, priv->dma_buf_sz, plen);
}

static unsigned int stmmac_rx_buf2_len(struct stmmac_priv *priv,
				       struct dma_desc *p,
				       int status, unsigned int len)
{
	int coe = priv->hw->rx_csum;
	unsigned int plen = 0;

	/* Not split header, buffer is not available */
	if (!priv->sph)
		return 0;

	/* Not last descriptor */
	if (status & rx_not_ls)
		return priv->dma_buf_sz;

	plen = stmmac_get_rx_frame_len(priv, p, coe);

	/* Last descriptor */
	return plen - len;
}

/**
 * stmmac_rx - manage the receive process
 * @priv: driver private structure
 * @limit: napi bugget
 * @queue: RX queue index.
 * Description :  this the function called by the napi poll method.
 * It gets all the frames inside the ring.
 */
static int stmmac_rx(struct stmmac_priv *priv, int limit, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];
	struct stmmac_channel *ch = &priv->channel[queue];
	unsigned int count = 0, error = 0, len = 0;
	int status = 0, coe = priv->hw->rx_csum;
	unsigned int next_entry = rx_q->cur_rx;
	unsigned int desc_size;
	struct sk_buff *skb = NULL;

	if (netif_msg_rx_status(priv)) {
		void *rx_head;

		netdev_dbg(priv->dev, "%s: descriptor ring:\n", __func__);
		if (priv->extend_desc) {
			rx_head = (void *)rx_q->dma_erx;
			desc_size = sizeof(struct dma_extended_desc);
		} else {
			rx_head = (void *)rx_q->dma_rx;
			desc_size = sizeof(struct dma_desc);
		}

		stmmac_display_ring(priv, rx_head, priv->dma_rx_size, true,
				    rx_q->dma_rx_phy, desc_size);
	}
	while (count < limit) {
		unsigned int buf1_len = 0, buf2_len = 0;
		enum pkt_hash_types hash_type;
		struct stmmac_rx_buffer *buf;
		struct dma_desc *np, *p;
		int entry;
		u32 hash;

		if (!count && rx_q->state_saved) {
			skb = rx_q->state.skb;
			error = rx_q->state.error;
			len = rx_q->state.len;
		} else {
			rx_q->state_saved = false;
			skb = NULL;
			error = 0;
			len = 0;
		}

		if (count >= limit)
			break;

read_again:
		buf1_len = 0;
		buf2_len = 0;
		entry = next_entry;
		buf = &rx_q->buf_pool[entry];

		if (priv->extend_desc)
			p = (struct dma_desc *)(rx_q->dma_erx + entry);
		else
			p = rx_q->dma_rx + entry;

		/* read the status of the incoming frame */
		status = stmmac_rx_status(priv, &priv->dev->stats,
				&priv->xstats, p);
		/* check if managed by the DMA otherwise go ahead */
		if (unlikely(status & dma_own))
			break;

		rx_q->cur_rx = STMMAC_GET_ENTRY(rx_q->cur_rx,
						priv->dma_rx_size);
		next_entry = rx_q->cur_rx;

		if (priv->extend_desc)
			np = (struct dma_desc *)(rx_q->dma_erx + next_entry);
		else
			np = rx_q->dma_rx + next_entry;

		prefetch(np);

		if (priv->extend_desc)
			stmmac_rx_extended_status(priv, &priv->dev->stats,
					&priv->xstats, rx_q->dma_erx + entry);
		if (unlikely(status == discard_frame)) {
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
			error = 1;
			if (!priv->hwts_rx_en)
				priv->dev->stats.rx_errors++;
		}

		if (unlikely(error && (status & rx_not_ls)))
			goto read_again;
		if (unlikely(error)) {
			dev_kfree_skb(skb);
			skb = NULL;
			count++;
			continue;
		}

		/* Buffer is good. Go on. */

		prefetch(page_address(buf->page));
		if (buf->sec_page)
			prefetch(page_address(buf->sec_page));

		buf1_len = stmmac_rx_buf1_len(priv, p, status, len);
		len += buf1_len;
		buf2_len = stmmac_rx_buf2_len(priv, p, status, len);
		len += buf2_len;

		/* ACS is set; GMAC core strips PAD/FCS for IEEE 802.3
		 * Type frames (LLC/LLC-SNAP)
		 *
		 * llc_snap is never checked in GMAC >= 4, so this ACS
		 * feature is always disabled and packets need to be
		 * stripped manually.
		 */
		if (likely(!(status & rx_not_ls)) &&
		    (likely(priv->synopsys_id >= DWMAC_CORE_4_00) ||
		     unlikely(status != llc_snap))) {
			if (buf2_len)
				buf2_len -= ETH_FCS_LEN;
			else
				buf1_len -= ETH_FCS_LEN;

			len -= ETH_FCS_LEN;
		}

		if (!skb) {
			skb = napi_alloc_skb(&ch->rx_napi, buf1_len);
			if (!skb) {
				priv->dev->stats.rx_dropped++;
				count++;
				goto drain_data;
			}

			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_copy_to_linear_data(skb, page_address(buf->page),
						buf1_len);
			skb_put(skb, buf1_len);

			/* Data payload copied into SKB, page ready for recycle */
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
		} else if (buf1_len) {
			dma_sync_single_for_cpu(priv->device, buf->addr,
						buf1_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->page, 0, buf1_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
			page_pool_release_page(rx_q->page_pool, buf->page);
			buf->page = NULL;
		}

		if (buf2_len) {
			dma_sync_single_for_cpu(priv->device, buf->sec_addr,
						buf2_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->sec_page, 0, buf2_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
			page_pool_release_page(rx_q->page_pool, buf->sec_page);
			buf->sec_page = NULL;
		}

drain_data:
		if (likely(status & rx_not_ls))
			goto read_again;
		if (!skb)
			continue;

		/* Got entire packet into SKB. Finish it. */

		stmmac_get_rx_hwtstamp(priv, p, np, skb);
		stmmac_rx_vlan(priv->dev, skb);
		skb->protocol = eth_type_trans(skb, priv->dev);

		if (unlikely(!coe))
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (!stmmac_get_rx_hash(priv, p, &hash, &hash_type))
			skb_set_hash(skb, hash, hash_type);

		skb_record_rx_queue(skb, queue);
		napi_gro_receive(&ch->rx_napi, skb);
		skb = NULL;

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += len;
		count++;
	}

	if (status & rx_not_ls || skb) {
		rx_q->state_saved = true;
		rx_q->state.skb = skb;
		rx_q->state.error = error;
		rx_q->state.len = len;
	}

	stmmac_rx_refill(priv, queue);

	priv->xstats.rx_pkt_n += count;

	return count;
}

static int stmmac_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct stmmac_channel *ch =
		container_of(napi, struct stmmac_channel, rx_napi);
	struct stmmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll++;

	work_done = stmmac_rx(priv, budget, chan);
	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		stmmac_enable_dma_irq(priv, priv->ioaddr, chan, 1, 0);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

static int stmmac_napi_poll_tx(struct napi_struct *napi, int budget)
{
	struct stmmac_channel *ch =
		container_of(napi, struct stmmac_channel, tx_napi);
	struct stmmac_priv *priv = ch->priv_data;
	u32 chan = ch->index;
	int work_done;

	priv->xstats.napi_poll++;

	work_done = stmmac_tx_clean(priv, priv->dma_tx_size, chan);
	work_done = min(work_done, budget);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&ch->lock, flags);
		stmmac_enable_dma_irq(priv, priv->ioaddr, chan, 0, 1);
		spin_unlock_irqrestore(&ch->lock, flags);
	}

	return work_done;
}

/**
 *  stmmac_tx_timeout
 *  @dev : Pointer to net device structure
 *  @txqueue: the index of the hanging transmit queue
 *  Description: this function is called when a packet transmission fails to
 *   complete within a reasonable time. The driver will mark the error in the
 *   netdev structure and arrange for the device to be reset to a sane state
 *   in order to transmit a new packet.
 */
static void stmmac_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	stmmac_global_err(priv);
}

/**
 *  stmmac_set_rx_mode - entry point for multicast addressing
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled.
 *  Return value:
 *  void.
 */
static void stmmac_set_rx_mode(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	stmmac_set_filter(priv, priv->hw, dev);
}

/**
 *  stmmac_change_mtu - entry point to change MTU size for the device.
 *  @dev : device pointer.
 *  @new_mtu : the new MTU size for the device.
 *  Description: the Maximum Transfer Unit (MTU) is used by the network layer
 *  to drive packet transmission. Ethernet has an MTU of 1500 octets
 *  (ETH_DATA_LEN). This value can be changed with ifconfig.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_change_mtu(struct net_device *dev, int new_mtu)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int txfifosz = priv->plat->tx_fifo_size;
	const int mtu = new_mtu;

	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	txfifosz /= priv->plat->tx_queues_to_use;

	if (netif_running(dev)) {
		netdev_err(priv->dev, "must be stopped to change its MTU\n");
		return -EBUSY;
	}

	new_mtu = STMMAC_ALIGN(new_mtu);

	/* If condition true, FIFO is too small or MTU too large */
	if ((txfifosz < new_mtu) || (new_mtu > BUF_SIZE_16KiB))
		return -EINVAL;

	dev->mtu = mtu;

	netdev_update_features(dev);

	return 0;
}

static netdev_features_t stmmac_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	if (priv->plat->rx_coe == STMMAC_RX_COE_NONE)
		features &= ~NETIF_F_RXCSUM;

	if (!priv->plat->tx_coe)
		features &= ~NETIF_F_CSUM_MASK;

	/* Some GMAC devices have a bugged Jumbo frame support that
	 * needs to have the Tx COE disabled for oversized frames
	 * (due to limited buffer sizes). In this case we disable
	 * the TX csum insertion in the TDES and not use SF.
	 */
	if (priv->plat->bugged_jumbo && (dev->mtu > ETH_DATA_LEN))
		features &= ~NETIF_F_CSUM_MASK;

	/* Disable tso if asked by ethtool */
	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		if (features & NETIF_F_TSO)
			priv->tso = true;
		else
			priv->tso = false;
	}

	return features;
}

static int stmmac_set_features(struct net_device *netdev,
			       netdev_features_t features)
{
	struct stmmac_priv *priv = netdev_priv(netdev);
	bool sph_en;
	u32 chan;

	/* Keep the COE Type in case of csum is supporting */
	if (features & NETIF_F_RXCSUM)
		priv->hw->rx_csum = priv->plat->rx_coe;
	else
		priv->hw->rx_csum = 0;
	/* No check needed because rx_coe has been set before and it will be
	 * fixed in case of issue.
	 */
	stmmac_rx_ipc(priv, priv->hw);

	sph_en = (priv->hw->rx_csum > 0) && priv->sph;
	for (chan = 0; chan < priv->plat->rx_queues_to_use; chan++)
		stmmac_enable_sph(priv, priv->ioaddr, sph_en, chan);

	return 0;
}

/**
 *  stmmac_interrupt - main ISR
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer (must be valid).
 *  Description: this is the main driver interrupt service routine.
 *  It can call:
 *  o DMA service routine (to manage incoming frame reception and transmission
 *    status)
 *  o Core interrupts to manage: remote wake-up, management counter, LPI
 *    interrupts.
 */
static irqreturn_t stmmac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 queues_count;
	u32 queue;
	bool xmac;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;
	queues_count = (rx_cnt > tx_cnt) ? rx_cnt : tx_cnt;

	if (priv->irq_wake)
		pm_wakeup_event(priv->device, 0);

	/* Check if adapter is up */
	if (test_bit(STMMAC_DOWN, &priv->state))
		return IRQ_HANDLED;
	/* Check if a fatal error happened */
	if (stmmac_safety_feat_interrupt(priv))
		return IRQ_HANDLED;

	/* To handle GMAC own interrupts */
	if ((priv->plat->has_gmac) || xmac) {
		int status = stmmac_host_irq_status(priv, priv->hw, &priv->xstats);

		if (unlikely(status)) {
			/* For LPI we need to save the tx status */
			if (status & CORE_IRQ_TX_PATH_IN_LPI_MODE)
				priv->tx_path_in_lpi_mode = true;
			if (status & CORE_IRQ_TX_PATH_EXIT_LPI_MODE)
				priv->tx_path_in_lpi_mode = false;
		}

		for (queue = 0; queue < queues_count; queue++) {
			status = stmmac_host_mtl_irq_status(priv, priv->hw,
							    queue);
		}

		/* PCS link status */
		if (priv->hw->pcs) {
			if (priv->xstats.pcs_link)
				netif_carrier_on(dev);
			else
				netif_carrier_off(dev);
		}
	}

	/* To handle DMA interrupts */
	stmmac_dma_interrupt(priv);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled.
 */
static void stmmac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	stmmac_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/**
 *  stmmac_ioctl - Entry point for the Ioctl
 *  @dev: Device pointer.
 *  @rq: An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd: IOCTL command
 *  Description:
 *  Currently it supports the phy_mii_ioctl(...) and HW time stamping.
 */
static int stmmac_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct stmmac_priv *priv = netdev_priv (dev);
	int ret = -EOPNOTSUPP;

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		ret = phylink_mii_ioctl(priv->phylink, rq, cmd);
		break;
	case SIOCSHWTSTAMP:
		ret = stmmac_hwtstamp_set(dev, rq);
		break;
	case SIOCGHWTSTAMP:
		ret = stmmac_hwtstamp_get(dev, rq);
		break;
	default:
		break;
	}

	return ret;
}

static int stmmac_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				    void *cb_priv)
{
	struct stmmac_priv *priv = cb_priv;
	int ret = -EOPNOTSUPP;

	if (!tc_cls_can_offload_and_chain0(priv->dev, type_data))
		return ret;

	stmmac_disable_all_queues(priv);

	switch (type) {
	case TC_SETUP_CLSU32:
		ret = stmmac_tc_setup_cls_u32(priv, priv, type_data);
		break;
	case TC_SETUP_CLSFLOWER:
		ret = stmmac_tc_setup_cls(priv, priv, type_data);
		break;
	default:
		break;
	}

	stmmac_enable_all_queues(priv);
	return ret;
}

static LIST_HEAD(stmmac_block_cb_list);

static int stmmac_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			   void *type_data)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &stmmac_block_cb_list,
						  stmmac_setup_tc_block_cb,
						  priv, priv, true);
	case TC_SETUP_QDISC_CBS:
		return stmmac_tc_setup_cbs(priv, priv, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return stmmac_tc_setup_taprio(priv, priv, type_data);
	case TC_SETUP_QDISC_ETF:
		return stmmac_tc_setup_etf(priv, priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static u16 stmmac_select_queue(struct net_device *dev, struct sk_buff *skb,
			       struct net_device *sb_dev)
{
	int gso = skb_shinfo(skb)->gso_type;

	if (gso & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6 | SKB_GSO_UDP_L4)) {
		/*
		 * There is no way to determine the number of TSO/USO
		 * capable Queues. Let's use always the Queue 0
		 * because if TSO/USO is supported then at least this
		 * one will be capable.
		 */
		return 0;
	}

	return netdev_pick_tx(dev, skb, NULL) % dev->real_num_tx_queues;
}

static int stmmac_set_mac_address(struct net_device *ndev, void *addr)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = 0;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;

	stmmac_set_umac_addr(priv, priv->hw, ndev->dev_addr, 0);

	return ret;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *stmmac_fs_dir;

static void sysfs_display_ring(void *head, int size, int extend_desc,
			       struct seq_file *seq, dma_addr_t dma_phy_addr)
{
	int i;
	struct dma_extended_desc *ep = (struct dma_extended_desc *)head;
	struct dma_desc *p = (struct dma_desc *)head;
	dma_addr_t dma_addr;

	for (i = 0; i < size; i++) {
		if (extend_desc) {
			dma_addr = dma_phy_addr + i * sizeof(*ep);
			seq_printf(seq, "%d [%pad]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, &dma_addr,
				   le32_to_cpu(ep->basic.des0),
				   le32_to_cpu(ep->basic.des1),
				   le32_to_cpu(ep->basic.des2),
				   le32_to_cpu(ep->basic.des3));
			ep++;
		} else {
			dma_addr = dma_phy_addr + i * sizeof(*p);
			seq_printf(seq, "%d [%pad]: 0x%x 0x%x 0x%x 0x%x\n",
				   i, &dma_addr,
				   le32_to_cpu(p->des0), le32_to_cpu(p->des1),
				   le32_to_cpu(p->des2), le32_to_cpu(p->des3));
			p++;
		}
		seq_printf(seq, "\n");
	}
}

static int stmmac_rings_status_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_count = priv->plat->rx_queues_to_use;
	u32 tx_count = priv->plat->tx_queues_to_use;
	u32 queue;

	if ((dev->flags & IFF_UP) == 0)
		return 0;

	for (queue = 0; queue < rx_count; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		seq_printf(seq, "RX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_erx,
					   priv->dma_rx_size, 1, seq, rx_q->dma_rx_phy);
		} else {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)rx_q->dma_rx,
					   priv->dma_rx_size, 0, seq, rx_q->dma_rx_phy);
		}
	}

	for (queue = 0; queue < tx_count; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

		seq_printf(seq, "TX Queue %d:\n", queue);

		if (priv->extend_desc) {
			seq_printf(seq, "Extended descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_etx,
					   priv->dma_tx_size, 1, seq, tx_q->dma_tx_phy);
		} else if (!(tx_q->tbs & STMMAC_TBS_AVAIL)) {
			seq_printf(seq, "Descriptor ring:\n");
			sysfs_display_ring((void *)tx_q->dma_tx,
					   priv->dma_tx_size, 0, seq, tx_q->dma_tx_phy);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(stmmac_rings_status);

static int stmmac_dma_cap_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct stmmac_priv *priv = netdev_priv(dev);

	if (!priv->hw_cap_support) {
		seq_printf(seq, "DMA HW features not supported\n");
		return 0;
	}

	seq_printf(seq, "==============================\n");
	seq_printf(seq, "\tDMA HW features\n");
	seq_printf(seq, "==============================\n");

	seq_printf(seq, "\t10/100 Mbps: %s\n",
		   (priv->dma_cap.mbps_10_100) ? "Y" : "N");
	seq_printf(seq, "\t1000 Mbps: %s\n",
		   (priv->dma_cap.mbps_1000) ? "Y" : "N");
	seq_printf(seq, "\tHalf duplex: %s\n",
		   (priv->dma_cap.half_duplex) ? "Y" : "N");
	seq_printf(seq, "\tHash Filter: %s\n",
		   (priv->dma_cap.hash_filter) ? "Y" : "N");
	seq_printf(seq, "\tMultiple MAC address registers: %s\n",
		   (priv->dma_cap.multi_addr) ? "Y" : "N");
	seq_printf(seq, "\tPCS (TBI/SGMII/RTBI PHY interfaces): %s\n",
		   (priv->dma_cap.pcs) ? "Y" : "N");
	seq_printf(seq, "\tSMA (MDIO) Interface: %s\n",
		   (priv->dma_cap.sma_mdio) ? "Y" : "N");
	seq_printf(seq, "\tPMT Remote wake up: %s\n",
		   (priv->dma_cap.pmt_remote_wake_up) ? "Y" : "N");
	seq_printf(seq, "\tPMT Magic Frame: %s\n",
		   (priv->dma_cap.pmt_magic_frame) ? "Y" : "N");
	seq_printf(seq, "\tRMON module: %s\n",
		   (priv->dma_cap.rmon) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2002 Time Stamp: %s\n",
		   (priv->dma_cap.time_stamp) ? "Y" : "N");
	seq_printf(seq, "\tIEEE 1588-2008 Advanced Time Stamp: %s\n",
		   (priv->dma_cap.atime_stamp) ? "Y" : "N");
	seq_printf(seq, "\t802.3az - Energy-Efficient Ethernet (EEE): %s\n",
		   (priv->dma_cap.eee) ? "Y" : "N");
	seq_printf(seq, "\tAV features: %s\n", (priv->dma_cap.av) ? "Y" : "N");
	seq_printf(seq, "\tChecksum Offload in TX: %s\n",
		   (priv->dma_cap.tx_coe) ? "Y" : "N");
	if (priv->synopsys_id >= DWMAC_CORE_4_00) {
		seq_printf(seq, "\tIP Checksum Offload in RX: %s\n",
			   (priv->dma_cap.rx_coe) ? "Y" : "N");
	} else {
		seq_printf(seq, "\tIP Checksum Offload (type1) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type1) ? "Y" : "N");
		seq_printf(seq, "\tIP Checksum Offload (type2) in RX: %s\n",
			   (priv->dma_cap.rx_coe_type2) ? "Y" : "N");
	}
	seq_printf(seq, "\tRXFIFO > 2048bytes: %s\n",
		   (priv->dma_cap.rxfifo_over_2048) ? "Y" : "N");
	seq_printf(seq, "\tNumber of Additional RX channel: %d\n",
		   priv->dma_cap.number_rx_channel);
	seq_printf(seq, "\tNumber of Additional TX channel: %d\n",
		   priv->dma_cap.number_tx_channel);
	seq_printf(seq, "\tNumber of Additional RX queues: %d\n",
		   priv->dma_cap.number_rx_queues);
	seq_printf(seq, "\tNumber of Additional TX queues: %d\n",
		   priv->dma_cap.number_tx_queues);
	seq_printf(seq, "\tEnhanced descriptors: %s\n",
		   (priv->dma_cap.enh_desc) ? "Y" : "N");
	seq_printf(seq, "\tTX Fifo Size: %d\n", priv->dma_cap.tx_fifo_size);
	seq_printf(seq, "\tRX Fifo Size: %d\n", priv->dma_cap.rx_fifo_size);
	seq_printf(seq, "\tHash Table Size: %d\n", priv->dma_cap.hash_tb_sz);
	seq_printf(seq, "\tTSO: %s\n", priv->dma_cap.tsoen ? "Y" : "N");
	seq_printf(seq, "\tNumber of PPS Outputs: %d\n",
		   priv->dma_cap.pps_out_num);
	seq_printf(seq, "\tSafety Features: %s\n",
		   priv->dma_cap.asp ? "Y" : "N");
	seq_printf(seq, "\tFlexible RX Parser: %s\n",
		   priv->dma_cap.frpsel ? "Y" : "N");
	seq_printf(seq, "\tEnhanced Addressing: %d\n",
		   priv->dma_cap.addr64);
	seq_printf(seq, "\tReceive Side Scaling: %s\n",
		   priv->dma_cap.rssen ? "Y" : "N");
	seq_printf(seq, "\tVLAN Hash Filtering: %s\n",
		   priv->dma_cap.vlhash ? "Y" : "N");
	seq_printf(seq, "\tSplit Header: %s\n",
		   priv->dma_cap.sphen ? "Y" : "N");
	seq_printf(seq, "\tVLAN TX Insertion: %s\n",
		   priv->dma_cap.vlins ? "Y" : "N");
	seq_printf(seq, "\tDouble VLAN: %s\n",
		   priv->dma_cap.dvlan ? "Y" : "N");
	seq_printf(seq, "\tNumber of L3/L4 Filters: %d\n",
		   priv->dma_cap.l3l4fnum);
	seq_printf(seq, "\tARP Offloading: %s\n",
		   priv->dma_cap.arpoffsel ? "Y" : "N");
	seq_printf(seq, "\tEnhancements to Scheduled Traffic (EST): %s\n",
		   priv->dma_cap.estsel ? "Y" : "N");
	seq_printf(seq, "\tFrame Preemption (FPE): %s\n",
		   priv->dma_cap.fpesel ? "Y" : "N");
	seq_printf(seq, "\tTime-Based Scheduling (TBS): %s\n",
		   priv->dma_cap.tbssel ? "Y" : "N");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(stmmac_dma_cap);

/* Use network device events to rename debugfs file entries.
 */
static int stmmac_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct stmmac_priv *priv = netdev_priv(dev);

	if (dev->netdev_ops != &stmmac_netdev_ops)
		goto done;

	switch (event) {
	case NETDEV_CHANGENAME:
		if (priv->dbgfs_dir)
			priv->dbgfs_dir = debugfs_rename(stmmac_fs_dir,
							 priv->dbgfs_dir,
							 stmmac_fs_dir,
							 dev->name);
		break;
	}
done:
	return NOTIFY_DONE;
}

static struct notifier_block stmmac_notifier = {
	.notifier_call = stmmac_device_event,
};

static void stmmac_init_fs(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	rtnl_lock();

	/* Create per netdev entries */
	priv->dbgfs_dir = debugfs_create_dir(dev->name, stmmac_fs_dir);

	/* Entry to report DMA RX/TX rings */
	debugfs_create_file("descriptors_status", 0444, priv->dbgfs_dir, dev,
			    &stmmac_rings_status_fops);

	/* Entry to report the DMA HW features */
	debugfs_create_file("dma_cap", 0444, priv->dbgfs_dir, dev,
			    &stmmac_dma_cap_fops);

	rtnl_unlock();
}

static void stmmac_exit_fs(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	debugfs_remove_recursive(priv->dbgfs_dir);
}
#endif /* CONFIG_DEBUG_FS */

static u32 stmmac_vid_crc32_le(__le16 vid_le)
{
	unsigned char *data = (unsigned char *)&vid_le;
	unsigned char data_byte = 0;
	u32 crc = ~0x0;
	u32 temp = 0;
	int i, bits;

	bits = get_bitmask_order(VLAN_VID_MASK);
	for (i = 0; i < bits; i++) {
		if ((i % 8) == 0)
			data_byte = data[i / 8];

		temp = ((crc & 1) ^ data_byte) & 1;
		crc >>= 1;
		data_byte >>= 1;

		if (temp)
			crc ^= 0xedb88320;
	}

	return crc;
}

static int stmmac_vlan_update(struct stmmac_priv *priv, bool is_double)
{
	u32 crc, hash = 0;
	__le16 pmatch = 0;
	int count = 0;
	u16 vid = 0;

	for_each_set_bit(vid, priv->active_vlans, VLAN_N_VID) {
		__le16 vid_le = cpu_to_le16(vid);
		crc = bitrev32(~stmmac_vid_crc32_le(vid_le)) >> 28;
		hash |= (1 << crc);
		count++;
	}

	if (!priv->dma_cap.vlhash) {
		if (count > 2) /* VID = 0 always passes filter */
			return -EOPNOTSUPP;

		pmatch = cpu_to_le16(vid);
		hash = 0;
	}

	return stmmac_update_vlan_hash(priv, priv->hw, hash, pmatch, is_double);
}

static int stmmac_vlan_rx_add_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	bool is_double = false;
	int ret;

	if (be16_to_cpu(proto) == ETH_P_8021AD)
		is_double = true;

	set_bit(vid, priv->active_vlans);
	ret = stmmac_vlan_update(priv, is_double);
	if (ret) {
		clear_bit(vid, priv->active_vlans);
		return ret;
	}

	if (priv->hw->num_vlan) {
		ret = stmmac_add_hw_vlan_rx_fltr(priv, ndev, priv->hw, proto, vid);
		if (ret)
			return ret;
	}

	return 0;
}

static int stmmac_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	bool is_double = false;
	int ret;

	if (be16_to_cpu(proto) == ETH_P_8021AD)
		is_double = true;

	clear_bit(vid, priv->active_vlans);

	if (priv->hw->num_vlan) {
		ret = stmmac_del_hw_vlan_rx_fltr(priv, ndev, priv->hw, proto, vid);
		if (ret)
			return ret;
	}

	return stmmac_vlan_update(priv, is_double);
}

static const struct net_device_ops stmmac_netdev_ops = {
	.ndo_open = stmmac_open,
	.ndo_start_xmit = stmmac_xmit,
	.ndo_stop = stmmac_release,
	.ndo_change_mtu = stmmac_change_mtu,
	.ndo_fix_features = stmmac_fix_features,
	.ndo_set_features = stmmac_set_features,
	.ndo_set_rx_mode = stmmac_set_rx_mode,
	.ndo_tx_timeout = stmmac_tx_timeout,
	.ndo_do_ioctl = stmmac_ioctl,
	.ndo_setup_tc = stmmac_setup_tc,
	.ndo_select_queue = stmmac_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = stmmac_poll_controller,
#endif
	.ndo_set_mac_address = stmmac_set_mac_address,
	.ndo_vlan_rx_add_vid = stmmac_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = stmmac_vlan_rx_kill_vid,
};

static void stmmac_reset_subtask(struct stmmac_priv *priv)
{
	if (!test_and_clear_bit(STMMAC_RESET_REQUESTED, &priv->state))
		return;
	if (test_bit(STMMAC_DOWN, &priv->state))
		return;

	netdev_err(priv->dev, "Reset adapter.\n");

	rtnl_lock();
	netif_trans_update(priv->dev);
	while (test_and_set_bit(STMMAC_RESETING, &priv->state))
		usleep_range(1000, 2000);

	set_bit(STMMAC_DOWN, &priv->state);
	dev_close(priv->dev);
	dev_open(priv->dev, NULL);
	clear_bit(STMMAC_DOWN, &priv->state);
	clear_bit(STMMAC_RESETING, &priv->state);
	rtnl_unlock();
}

static void stmmac_service_task(struct work_struct *work)
{
	struct stmmac_priv *priv = container_of(work, struct stmmac_priv,
			service_task);

	stmmac_reset_subtask(priv);
	clear_bit(STMMAC_SERVICE_SCHED, &priv->state);
}

/**
 *  stmmac_hw_init - Init the MAC device
 *  @priv: driver private structure
 *  Description: this function is to configure the MAC device according to
 *  some platform parameters or the HW capability register. It prepares the
 *  driver to use either ring or chain modes and to setup either enhanced or
 *  normal descriptors.
 */
static int stmmac_hw_init(struct stmmac_priv *priv)
{
	int ret;

	/* dwmac-sun8i only work in chain mode */
	if (priv->plat->has_sun8i)
		chain_mode = 1;
	priv->chain_mode = chain_mode;

	/* Initialize HW Interface */
	ret = stmmac_hwif_init(priv);
	if (ret)
		return ret;

	/* Get the HW capability (new GMAC newer than 3.50a) */
	priv->hw_cap_support = stmmac_get_hw_features(priv);
	if (priv->hw_cap_support) {
		dev_info(priv->device, "DMA HW capability register supported\n");

		/* We can override some gmac/dma configuration fields: e.g.
		 * enh_desc, tx_coe (e.g. that are passed through the
		 * platform) with the values from the HW capability
		 * register (if supported).
		 */
		priv->plat->enh_desc = priv->dma_cap.enh_desc;
		priv->plat->pmt = priv->dma_cap.pmt_remote_wake_up;
		priv->hw->pmt = priv->plat->pmt;
		if (priv->dma_cap.hash_tb_sz) {
			priv->hw->multicast_filter_bins =
					(BIT(priv->dma_cap.hash_tb_sz) << 5);
			priv->hw->mcast_bits_log2 =
					ilog2(priv->hw->multicast_filter_bins);
		}

		/* TXCOE doesn't work in thresh DMA mode */
		if (priv->plat->force_thresh_dma_mode)
			priv->plat->tx_coe = 0;
		else
			priv->plat->tx_coe = priv->dma_cap.tx_coe;

		/* In case of GMAC4 rx_coe is from HW cap register. */
		priv->plat->rx_coe = priv->dma_cap.rx_coe;

		if (priv->dma_cap.rx_coe_type2)
			priv->plat->rx_coe = STMMAC_RX_COE_TYPE2;
		else if (priv->dma_cap.rx_coe_type1)
			priv->plat->rx_coe = STMMAC_RX_COE_TYPE1;

	} else {
		dev_info(priv->device, "No HW DMA feature register supported\n");
	}

	if (priv->plat->rx_coe) {
		priv->hw->rx_csum = priv->plat->rx_coe;
		dev_info(priv->device, "RX Checksum Offload Engine supported\n");
		if (priv->synopsys_id < DWMAC_CORE_4_00)
			dev_info(priv->device, "COE Type %d\n", priv->hw->rx_csum);
	}
	if (priv->plat->tx_coe)
		dev_info(priv->device, "TX Checksum insertion supported\n");

	if (priv->plat->pmt) {
		dev_info(priv->device, "Wake-Up On Lan supported\n");
		device_set_wakeup_capable(priv->device, 1);
	}

	if (priv->dma_cap.tsoen)
		dev_info(priv->device, "TSO supported\n");

	priv->hw->vlan_fail_q_en = priv->plat->vlan_fail_q_en;
	priv->hw->vlan_fail_q = priv->plat->vlan_fail_q;

	/* Run HW quirks, if any */
	if (priv->hwif_quirks) {
		ret = priv->hwif_quirks(priv);
		if (ret)
			return ret;
	}

	/* Rx Watchdog is available in the COREs newer than the 3.40.
	 * In some case, for example on bugged HW this feature
	 * has to be disable and this can be done by passing the
	 * riwt_off field from the platform.
	 */
	if (((priv->synopsys_id >= DWMAC_CORE_3_50) ||
	    (priv->plat->has_xgmac)) && (!priv->plat->riwt_off)) {
		priv->use_riwt = 1;
		dev_info(priv->device,
			 "Enable RX Mitigation via HW Watchdog Timer\n");
	}

	return 0;
}

static void stmmac_napi_add(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 queue, maxq;

	maxq = max(priv->plat->rx_queues_to_use, priv->plat->tx_queues_to_use);

	for (queue = 0; queue < maxq; queue++) {
		struct stmmac_channel *ch = &priv->channel[queue];

		ch->priv_data = priv;
		ch->index = queue;
		spin_lock_init(&ch->lock);

		if (queue < priv->plat->rx_queues_to_use) {
			netif_napi_add(dev, &ch->rx_napi, stmmac_napi_poll_rx,
				       NAPI_POLL_WEIGHT);
		}
		if (queue < priv->plat->tx_queues_to_use) {
			netif_tx_napi_add(dev, &ch->tx_napi,
					  stmmac_napi_poll_tx,
					  NAPI_POLL_WEIGHT);
		}
	}
}

static void stmmac_napi_del(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 queue, maxq;

	maxq = max(priv->plat->rx_queues_to_use, priv->plat->tx_queues_to_use);

	for (queue = 0; queue < maxq; queue++) {
		struct stmmac_channel *ch = &priv->channel[queue];

		if (queue < priv->plat->rx_queues_to_use)
			netif_napi_del(&ch->rx_napi);
		if (queue < priv->plat->tx_queues_to_use)
			netif_napi_del(&ch->tx_napi);
	}
}

int stmmac_reinit_queues(struct net_device *dev, u32 rx_cnt, u32 tx_cnt)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (netif_running(dev))
		stmmac_release(dev);

	stmmac_napi_del(dev);

	priv->plat->rx_queues_to_use = rx_cnt;
	priv->plat->tx_queues_to_use = tx_cnt;

	stmmac_napi_add(dev);

	if (netif_running(dev))
		ret = stmmac_open(dev);

	return ret;
}

int stmmac_reinit_ringparam(struct net_device *dev, u32 rx_size, u32 tx_size)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (netif_running(dev))
		stmmac_release(dev);

	priv->dma_rx_size = rx_size;
	priv->dma_tx_size = tx_size;

	if (netif_running(dev))
		ret = stmmac_open(dev);

	return ret;
}

/**
 * stmmac_dvr_probe
 * @device: device pointer
 * @plat_dat: platform data pointer
 * @res: stmmac resource pointer
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 * Return:
 * returns 0 on success, otherwise errno.
 */
int stmmac_dvr_probe(struct device *device,
		     struct plat_stmmacenet_data *plat_dat,
		     struct stmmac_resources *res)
{
	struct net_device *ndev = NULL;
	struct stmmac_priv *priv;
	u32 rxq;
	int i, ret = 0;

	ndev = devm_alloc_etherdev_mqs(device, sizeof(struct stmmac_priv),
				       MTL_MAX_TX_QUEUES, MTL_MAX_RX_QUEUES);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, device);

	priv = netdev_priv(ndev);
	priv->device = device;
	priv->dev = ndev;

	stmmac_set_ethtool_ops(ndev);
	priv->pause = pause;
	priv->plat = plat_dat;
	priv->ioaddr = res->addr;
	priv->dev->base_addr = (unsigned long)res->addr;

	priv->dev->irq = res->irq;
	priv->wol_irq = res->wol_irq;
	priv->lpi_irq = res->lpi_irq;

	if (!IS_ERR_OR_NULL(res->mac))
		memcpy(priv->dev->dev_addr, res->mac, ETH_ALEN);

	dev_set_drvdata(device, priv->dev);

	/* Verify driver arguments */
	stmmac_verify_args();

	/* Allocate workqueue */
	priv->wq = create_singlethread_workqueue("stmmac_wq");
	if (!priv->wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->service_task, stmmac_service_task);

	/* Override with kernel parameters if supplied XXX CRS XXX
	 * this needs to have multiple instances
	 */
	if ((phyaddr >= 0) && (phyaddr <= 31))
		priv->plat->phy_addr = phyaddr;

	if (priv->plat->stmmac_rst) {
		ret = reset_control_assert(priv->plat->stmmac_rst);
		reset_control_deassert(priv->plat->stmmac_rst);
		/* Some reset controllers have only reset callback instead of
		 * assert + deassert callbacks pair.
		 */
		if (ret == -ENOTSUPP)
			reset_control_reset(priv->plat->stmmac_rst);
	}

	/* Init MAC and get the capabilities */
	ret = stmmac_hw_init(priv);
	if (ret)
		goto error_hw_init;

	stmmac_check_ether_addr(priv);

	ndev->netdev_ops = &stmmac_netdev_ops;

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			    NETIF_F_RXCSUM;

	ret = stmmac_tc_init(priv, priv);
	if (!ret) {
		ndev->hw_features |= NETIF_F_HW_TC;
	}

	if ((priv->plat->tso_en) && (priv->dma_cap.tsoen)) {
		ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		if (priv->plat->has_gmac4)
			ndev->hw_features |= NETIF_F_GSO_UDP_L4;
		priv->tso = true;
		dev_info(priv->device, "TSO feature enabled\n");
	}

	if (priv->dma_cap.sphen) {
		ndev->hw_features |= NETIF_F_GRO;
		priv->sph = true;
		dev_info(priv->device, "SPH feature enabled\n");
	}

	/* The current IP register MAC_HW_Feature1[ADDR64] only define
	 * 32/40/64 bit width, but some SOC support others like i.MX8MP
	 * support 34 bits but it map to 40 bits width in MAC_HW_Feature1[ADDR64].
	 * So overwrite dma_cap.addr64 according to HW real design.
	 */
	if (priv->plat->addr64)
		priv->dma_cap.addr64 = priv->plat->addr64;

	if (priv->dma_cap.addr64) {
		ret = dma_set_mask_and_coherent(device,
				DMA_BIT_MASK(priv->dma_cap.addr64));
		if (!ret) {
			dev_info(priv->device, "Using %d bits DMA width\n",
				 priv->dma_cap.addr64);

			/*
			 * If more than 32 bits can be addressed, make sure to
			 * enable enhanced addressing mode.
			 */
			if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
				priv->plat->dma_cfg->eame = true;
		} else {
			ret = dma_set_mask_and_coherent(device, DMA_BIT_MASK(32));
			if (ret) {
				dev_err(priv->device, "Failed to set DMA Mask\n");
				goto error_hw_init;
			}

			priv->dma_cap.addr64 = 32;
		}
	}

	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
#ifdef STMMAC_VLAN_TAG_USED
	/* Both mac100 and gmac support receive VLAN tag detection */
	ndev->features |= NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_STAG_RX;
	if (priv->dma_cap.vlhash) {
		ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		ndev->features |= NETIF_F_HW_VLAN_STAG_FILTER;
	}
	if (priv->dma_cap.vlins) {
		ndev->features |= NETIF_F_HW_VLAN_CTAG_TX;
		if (priv->dma_cap.dvlan)
			ndev->features |= NETIF_F_HW_VLAN_STAG_TX;
	}
#endif
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	/* Initialize RSS */
	rxq = priv->plat->rx_queues_to_use;
	netdev_rss_key_fill(priv->rss.key, sizeof(priv->rss.key));
	for (i = 0; i < ARRAY_SIZE(priv->rss.table); i++)
		priv->rss.table[i] = ethtool_rxfh_indir_default(i, rxq);

	if (priv->dma_cap.rssen && priv->plat->rss_en)
		ndev->features |= NETIF_F_RXHASH;

	/* MTU range: 46 - hw-specific max */
	ndev->min_mtu = ETH_ZLEN - ETH_HLEN;
	if (priv->plat->has_xgmac)
		ndev->max_mtu = XGMAC_JUMBO_LEN;
	else if ((priv->plat->enh_desc) || (priv->synopsys_id >= DWMAC_CORE_4_00))
		ndev->max_mtu = JUMBO_LEN;
	else
		ndev->max_mtu = SKB_MAX_HEAD(NET_SKB_PAD + NET_IP_ALIGN);
	/* Will not overwrite ndev->max_mtu if plat->maxmtu > ndev->max_mtu
	 * as well as plat->maxmtu < ndev->min_mtu which is a invalid range.
	 */
	if ((priv->plat->maxmtu < ndev->max_mtu) &&
	    (priv->plat->maxmtu >= ndev->min_mtu))
		ndev->max_mtu = priv->plat->maxmtu;
	else if (priv->plat->maxmtu < ndev->min_mtu)
		dev_warn(priv->device,
			 "%s: warning: maxmtu having invalid value (%d)\n",
			 __func__, priv->plat->maxmtu);

	if (flow_ctrl)
		priv->flow_ctrl = FLOW_AUTO;	/* RX/TX pause on */

	/* Setup channels NAPI */
	stmmac_napi_add(ndev);

	mutex_init(&priv->lock);

	/* If a specific clk_csr value is passed from the platform
	 * this means that the CSR Clock Range selection cannot be
	 * changed at run-time and it is fixed. Viceversa the driver'll try to
	 * set the MDC clock dynamically according to the csr actual
	 * clock input.
	 */
	if (priv->plat->clk_csr >= 0)
		priv->clk_csr = priv->plat->clk_csr;
	else
		stmmac_clk_csr_set(priv);

	stmmac_check_pcs_mode(priv);

	if (priv->hw->pcs != STMMAC_PCS_TBI &&
	    priv->hw->pcs != STMMAC_PCS_RTBI) {
		/* MDIO bus Registration */
		ret = stmmac_mdio_register(ndev);
		if (ret < 0) {
			dev_err(priv->device,
				"%s: MDIO bus (id: %d) registration failed",
				__func__, priv->plat->bus_id);
			goto error_mdio_register;
		}
	}

	ret = stmmac_phy_setup(priv);
	if (ret) {
		netdev_err(ndev, "failed to setup phy (%d)\n", ret);
		goto error_phy_setup;
	}

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->device, "%s: ERROR %i registering the device\n",
			__func__, ret);
		goto error_netdev_register;
	}

	if (priv->plat->serdes_powerup) {
		ret = priv->plat->serdes_powerup(ndev,
						 priv->plat->bsp_priv);

		if (ret < 0)
			goto error_serdes_powerup;
	}

#ifdef CONFIG_DEBUG_FS
	stmmac_init_fs(ndev);
#endif

	return ret;

error_serdes_powerup:
	unregister_netdev(ndev);
error_netdev_register:
	phylink_destroy(priv->phylink);
error_phy_setup:
	if (priv->hw->pcs != STMMAC_PCS_TBI &&
	    priv->hw->pcs != STMMAC_PCS_RTBI)
		stmmac_mdio_unregister(ndev);
error_mdio_register:
	stmmac_napi_del(ndev);
error_hw_init:
	destroy_workqueue(priv->wq);

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_dvr_probe);

/**
 * stmmac_dvr_remove
 * @dev: device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
int stmmac_dvr_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	netdev_info(priv->dev, "%s: removing driver", __func__);

	stmmac_stop_all_dma(priv);
	stmmac_mac_set(priv, priv->ioaddr, false);
	netif_carrier_off(ndev);
	unregister_netdev(ndev);

	/* Serdes power down needs to happen after VLAN filter
	 * is deleted that is triggered by unregister_netdev().
	 */
	if (priv->plat->serdes_powerdown)
		priv->plat->serdes_powerdown(ndev, priv->plat->bsp_priv);

#ifdef CONFIG_DEBUG_FS
	stmmac_exit_fs(ndev);
#endif
	phylink_destroy(priv->phylink);
	if (priv->plat->stmmac_rst)
		reset_control_assert(priv->plat->stmmac_rst);
	clk_disable_unprepare(priv->plat->pclk);
	clk_disable_unprepare(priv->plat->stmmac_clk);
	if (priv->hw->pcs != STMMAC_PCS_TBI &&
	    priv->hw->pcs != STMMAC_PCS_RTBI)
		stmmac_mdio_unregister(ndev);
	destroy_workqueue(priv->wq);
	mutex_destroy(&priv->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_dvr_remove);

/**
 * stmmac_suspend - suspend callback
 * @dev: device pointer
 * Description: this is the function to suspend the device and it is called
 * by the platform driver to stop the network queue, release the resources,
 * program the PMT register (for WoL), clean and release driver resources.
 */
int stmmac_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	u32 chan;

	if (!ndev || !netif_running(ndev))
		return 0;

	phylink_mac_change(priv->phylink, false);

	mutex_lock(&priv->lock);

	netif_device_detach(ndev);

	stmmac_disable_all_queues(priv);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		del_timer_sync(&priv->tx_queue[chan].txtimer);

	if (priv->eee_enabled) {
		priv->tx_path_in_lpi_mode = false;
		del_timer_sync(&priv->eee_ctrl_timer);
	}

	/* Stop TX/RX DMA */
	stmmac_stop_all_dma(priv);

	if (priv->plat->serdes_powerdown)
		priv->plat->serdes_powerdown(ndev, priv->plat->bsp_priv);

	/* Enable Power down mode by programming the PMT regs */
	if (device_may_wakeup(priv->device) && priv->plat->pmt) {
		stmmac_pmt(priv, priv->hw, priv->wolopts);
		priv->irq_wake = 1;
	} else {
		mutex_unlock(&priv->lock);
		rtnl_lock();
		if (device_may_wakeup(priv->device))
			phylink_speed_down(priv->phylink, false);
		phylink_stop(priv->phylink);
		rtnl_unlock();
		mutex_lock(&priv->lock);

		stmmac_mac_set(priv, priv->ioaddr, false);
		pinctrl_pm_select_sleep_state(priv->device);
		/* Disable clock in case of PWM is off */
		clk_disable_unprepare(priv->plat->clk_ptp_ref);
		clk_disable_unprepare(priv->plat->pclk);
		clk_disable_unprepare(priv->plat->stmmac_clk);
	}
	mutex_unlock(&priv->lock);

	priv->speed = SPEED_UNKNOWN;
	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_suspend);

/**
 * stmmac_reset_queues_param - reset queue parameters
 * @priv: device pointer
 */
static void stmmac_reset_queues_param(struct stmmac_priv *priv)
{
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 queue;

	for (queue = 0; queue < rx_cnt; queue++) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue[queue];

		rx_q->cur_rx = 0;
		rx_q->dirty_rx = 0;
	}

	for (queue = 0; queue < tx_cnt; queue++) {
		struct stmmac_tx_queue *tx_q = &priv->tx_queue[queue];

		tx_q->cur_tx = 0;
		tx_q->dirty_tx = 0;
		tx_q->mss = 0;

		netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, queue));
	}
}

/**
 * stmmac_resume - resume callback
 * @dev: device pointer
 * Description: when resume this function is invoked to setup the DMA and CORE
 * in a usable state.
 */
int stmmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	if (!netif_running(ndev))
		return 0;

	/* Power Down bit, into the PM register, is cleared
	 * automatically as soon as a magic packet or a Wake-up frame
	 * is received. Anyway, it's better to manually clear
	 * this bit because it can generate problems while resuming
	 * from another devices (e.g. serial console).
	 */
	if (device_may_wakeup(priv->device) && priv->plat->pmt) {
		mutex_lock(&priv->lock);
		stmmac_pmt(priv, priv->hw, 0);
		mutex_unlock(&priv->lock);
		priv->irq_wake = 0;
	} else {
		pinctrl_pm_select_default_state(priv->device);
		/* enable the clk previously disabled */
		clk_prepare_enable(priv->plat->stmmac_clk);
		clk_prepare_enable(priv->plat->pclk);
		if (priv->plat->clk_ptp_ref)
			clk_prepare_enable(priv->plat->clk_ptp_ref);
		/* reset the phy so that it's ready */
		if (priv->mii)
			stmmac_mdio_reset(priv->mii);
	}

	if (priv->plat->serdes_powerup) {
		ret = priv->plat->serdes_powerup(ndev,
						 priv->plat->bsp_priv);

		if (ret < 0)
			return ret;
	}

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		rtnl_lock();
		phylink_start(priv->phylink);
		/* We may have called phylink_speed_down before */
		phylink_speed_up(priv->phylink);
		rtnl_unlock();
	}

	rtnl_lock();
	mutex_lock(&priv->lock);

	stmmac_reset_queues_param(priv);

	stmmac_free_tx_skbufs(priv);
	stmmac_clear_descriptors(priv);

	stmmac_hw_setup(ndev, false);
	stmmac_init_coalesce(priv);
	stmmac_set_rx_mode(ndev);

	stmmac_restore_hw_vlan_rx_fltr(priv, ndev, priv->hw);

	stmmac_enable_all_queues(priv);

	mutex_unlock(&priv->lock);
	rtnl_unlock();

	phylink_mac_change(priv->phylink, true);

	netif_device_attach(ndev);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_resume);

#ifndef MODULE
static int __init stmmac_cmdline_opt(char *str)
{
	char *opt;

	if (!str || !*str)
		return -EINVAL;
	while ((opt = strsep(&str, ",")) != NULL) {
		if (!strncmp(opt, "debug:", 6)) {
			if (kstrtoint(opt + 6, 0, &debug))
				goto err;
		} else if (!strncmp(opt, "phyaddr:", 8)) {
			if (kstrtoint(opt + 8, 0, &phyaddr))
				goto err;
		} else if (!strncmp(opt, "buf_sz:", 7)) {
			if (kstrtoint(opt + 7, 0, &buf_sz))
				goto err;
		} else if (!strncmp(opt, "tc:", 3)) {
			if (kstrtoint(opt + 3, 0, &tc))
				goto err;
		} else if (!strncmp(opt, "watchdog:", 9)) {
			if (kstrtoint(opt + 9, 0, &watchdog))
				goto err;
		} else if (!strncmp(opt, "flow_ctrl:", 10)) {
			if (kstrtoint(opt + 10, 0, &flow_ctrl))
				goto err;
		} else if (!strncmp(opt, "pause:", 6)) {
			if (kstrtoint(opt + 6, 0, &pause))
				goto err;
		} else if (!strncmp(opt, "eee_timer:", 10)) {
			if (kstrtoint(opt + 10, 0, &eee_timer))
				goto err;
		} else if (!strncmp(opt, "chain_mode:", 11)) {
			if (kstrtoint(opt + 11, 0, &chain_mode))
				goto err;
		}
	}
	return 0;

err:
	pr_err("%s: ERROR broken module parameter conversion", __func__);
	return -EINVAL;
}

__setup("stmmaceth=", stmmac_cmdline_opt);
#endif /* MODULE */

static int __init stmmac_init(void)
{
#ifdef CONFIG_DEBUG_FS
	/* Create debugfs main directory if it doesn't exist yet */
	if (!stmmac_fs_dir)
		stmmac_fs_dir = debugfs_create_dir(STMMAC_RESOURCE_NAME, NULL);
	register_netdevice_notifier(&stmmac_notifier);
#endif

	return 0;
}

static void __exit stmmac_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	unregister_netdevice_notifier(&stmmac_notifier);
	debugfs_remove_recursive(stmmac_fs_dir);
#endif
}

module_init(stmmac_init)
module_exit(stmmac_exit)

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet device driver");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");

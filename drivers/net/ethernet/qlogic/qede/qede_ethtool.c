// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/capability.h>
#include <linux/vmalloc.h>
#include <linux/phylink.h>

#include "qede.h"
#include "qede_ptp.h"

#define QEDE_RQSTAT_OFFSET(stat_name) \
	 (offsetof(struct qede_rx_queue, stat_name))
#define QEDE_RQSTAT_STRING(stat_name) (#stat_name)
#define QEDE_RQSTAT(stat_name) \
	 {QEDE_RQSTAT_OFFSET(stat_name), QEDE_RQSTAT_STRING(stat_name)}

#define QEDE_SELFTEST_POLL_COUNT 100
#define QEDE_DUMP_VERSION	0x1
#define QEDE_DUMP_NVM_ARG_COUNT	2

static const struct {
	u64 offset;
	char string[ETH_GSTRING_LEN];
} qede_rqstats_arr[] = {
	QEDE_RQSTAT(rcv_pkts),
	QEDE_RQSTAT(rx_hw_errors),
	QEDE_RQSTAT(rx_alloc_errors),
	QEDE_RQSTAT(rx_ip_frags),
	QEDE_RQSTAT(xdp_no_pass),
};

#define QEDE_NUM_RQSTATS ARRAY_SIZE(qede_rqstats_arr)
#define QEDE_TQSTAT_OFFSET(stat_name) \
	(offsetof(struct qede_tx_queue, stat_name))
#define QEDE_TQSTAT_STRING(stat_name) (#stat_name)
#define QEDE_TQSTAT(stat_name) \
	{QEDE_TQSTAT_OFFSET(stat_name), QEDE_TQSTAT_STRING(stat_name)}
#define QEDE_NUM_TQSTATS ARRAY_SIZE(qede_tqstats_arr)
static const struct {
	u64 offset;
	char string[ETH_GSTRING_LEN];
} qede_tqstats_arr[] = {
	QEDE_TQSTAT(xmit_pkts),
	QEDE_TQSTAT(stopped_cnt),
	QEDE_TQSTAT(tx_mem_alloc_err),
};

#define QEDE_STAT_OFFSET(stat_name, type, base) \
	(offsetof(type, stat_name) + (base))
#define QEDE_STAT_STRING(stat_name)	(#stat_name)
#define _QEDE_STAT(stat_name, type, base, attr) \
	{QEDE_STAT_OFFSET(stat_name, type, base), \
	 QEDE_STAT_STRING(stat_name), \
	 attr}
#define QEDE_STAT(stat_name) \
	_QEDE_STAT(stat_name, struct qede_stats_common, 0, 0x0)
#define QEDE_PF_STAT(stat_name) \
	_QEDE_STAT(stat_name, struct qede_stats_common, 0, \
		   BIT(QEDE_STAT_PF_ONLY))
#define QEDE_PF_BB_STAT(stat_name) \
	_QEDE_STAT(stat_name, struct qede_stats_bb, \
		   offsetof(struct qede_stats, bb), \
		   BIT(QEDE_STAT_PF_ONLY) | BIT(QEDE_STAT_BB_ONLY))
#define QEDE_PF_AH_STAT(stat_name) \
	_QEDE_STAT(stat_name, struct qede_stats_ah, \
		   offsetof(struct qede_stats, ah), \
		   BIT(QEDE_STAT_PF_ONLY) | BIT(QEDE_STAT_AH_ONLY))
static const struct {
	u64 offset;
	char string[ETH_GSTRING_LEN];
	unsigned long attr;
#define QEDE_STAT_PF_ONLY	0
#define QEDE_STAT_BB_ONLY	1
#define QEDE_STAT_AH_ONLY	2
} qede_stats_arr[] = {
	QEDE_STAT(rx_ucast_bytes),
	QEDE_STAT(rx_mcast_bytes),
	QEDE_STAT(rx_bcast_bytes),
	QEDE_STAT(rx_ucast_pkts),
	QEDE_STAT(rx_mcast_pkts),
	QEDE_STAT(rx_bcast_pkts),

	QEDE_STAT(tx_ucast_bytes),
	QEDE_STAT(tx_mcast_bytes),
	QEDE_STAT(tx_bcast_bytes),
	QEDE_STAT(tx_ucast_pkts),
	QEDE_STAT(tx_mcast_pkts),
	QEDE_STAT(tx_bcast_pkts),

	QEDE_PF_STAT(rx_64_byte_packets),
	QEDE_PF_STAT(rx_65_to_127_byte_packets),
	QEDE_PF_STAT(rx_128_to_255_byte_packets),
	QEDE_PF_STAT(rx_256_to_511_byte_packets),
	QEDE_PF_STAT(rx_512_to_1023_byte_packets),
	QEDE_PF_STAT(rx_1024_to_1518_byte_packets),
	QEDE_PF_BB_STAT(rx_1519_to_1522_byte_packets),
	QEDE_PF_BB_STAT(rx_1519_to_2047_byte_packets),
	QEDE_PF_BB_STAT(rx_2048_to_4095_byte_packets),
	QEDE_PF_BB_STAT(rx_4096_to_9216_byte_packets),
	QEDE_PF_BB_STAT(rx_9217_to_16383_byte_packets),
	QEDE_PF_AH_STAT(rx_1519_to_max_byte_packets),
	QEDE_PF_STAT(tx_64_byte_packets),
	QEDE_PF_STAT(tx_65_to_127_byte_packets),
	QEDE_PF_STAT(tx_128_to_255_byte_packets),
	QEDE_PF_STAT(tx_256_to_511_byte_packets),
	QEDE_PF_STAT(tx_512_to_1023_byte_packets),
	QEDE_PF_STAT(tx_1024_to_1518_byte_packets),
	QEDE_PF_BB_STAT(tx_1519_to_2047_byte_packets),
	QEDE_PF_BB_STAT(tx_2048_to_4095_byte_packets),
	QEDE_PF_BB_STAT(tx_4096_to_9216_byte_packets),
	QEDE_PF_BB_STAT(tx_9217_to_16383_byte_packets),
	QEDE_PF_AH_STAT(tx_1519_to_max_byte_packets),
	QEDE_PF_STAT(rx_mac_crtl_frames),
	QEDE_PF_STAT(tx_mac_ctrl_frames),
	QEDE_PF_STAT(rx_pause_frames),
	QEDE_PF_STAT(tx_pause_frames),
	QEDE_PF_STAT(rx_pfc_frames),
	QEDE_PF_STAT(tx_pfc_frames),

	QEDE_PF_STAT(rx_crc_errors),
	QEDE_PF_STAT(rx_align_errors),
	QEDE_PF_STAT(rx_carrier_errors),
	QEDE_PF_STAT(rx_oversize_packets),
	QEDE_PF_STAT(rx_jabbers),
	QEDE_PF_STAT(rx_undersize_packets),
	QEDE_PF_STAT(rx_fragments),
	QEDE_PF_BB_STAT(tx_lpi_entry_count),
	QEDE_PF_BB_STAT(tx_total_collisions),
	QEDE_PF_STAT(brb_truncates),
	QEDE_PF_STAT(brb_discards),
	QEDE_STAT(no_buff_discards),
	QEDE_PF_STAT(mftag_filter_discards),
	QEDE_PF_STAT(mac_filter_discards),
	QEDE_PF_STAT(gft_filter_drop),
	QEDE_STAT(tx_err_drop_pkts),
	QEDE_STAT(ttl0_discard),
	QEDE_STAT(packet_too_big_discard),

	QEDE_STAT(coalesced_pkts),
	QEDE_STAT(coalesced_events),
	QEDE_STAT(coalesced_aborts_num),
	QEDE_STAT(non_coalesced_pkts),
	QEDE_STAT(coalesced_bytes),

	QEDE_STAT(link_change_count),
	QEDE_STAT(ptp_skip_txts),
};

#define QEDE_NUM_STATS	ARRAY_SIZE(qede_stats_arr)
#define QEDE_STAT_IS_PF_ONLY(i) \
	test_bit(QEDE_STAT_PF_ONLY, &qede_stats_arr[i].attr)
#define QEDE_STAT_IS_BB_ONLY(i) \
	test_bit(QEDE_STAT_BB_ONLY, &qede_stats_arr[i].attr)
#define QEDE_STAT_IS_AH_ONLY(i) \
	test_bit(QEDE_STAT_AH_ONLY, &qede_stats_arr[i].attr)

enum {
	QEDE_PRI_FLAG_CMT,
	QEDE_PRI_FLAG_SMART_AN_SUPPORT, /* MFW supports SmartAN */
	QEDE_PRI_FLAG_RECOVER_ON_ERROR,
	QEDE_PRI_FLAG_LEN,
};

static const char qede_private_arr[QEDE_PRI_FLAG_LEN][ETH_GSTRING_LEN] = {
	"Coupled-Function",
	"SmartAN capable",
	"Recover on error",
};

enum qede_ethtool_tests {
	QEDE_ETHTOOL_INT_LOOPBACK,
	QEDE_ETHTOOL_INTERRUPT_TEST,
	QEDE_ETHTOOL_MEMORY_TEST,
	QEDE_ETHTOOL_REGISTER_TEST,
	QEDE_ETHTOOL_CLOCK_TEST,
	QEDE_ETHTOOL_NVRAM_TEST,
	QEDE_ETHTOOL_TEST_MAX
};

static const char qede_tests_str_arr[QEDE_ETHTOOL_TEST_MAX][ETH_GSTRING_LEN] = {
	"Internal loopback (offline)",
	"Interrupt (online)\t",
	"Memory (online)\t\t",
	"Register (online)\t",
	"Clock (online)\t\t",
	"Nvram (online)\t\t",
};

/* Forced speed capabilities maps */

struct qede_forced_speed_map {
	u32		speed;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(caps);

	const u32	*cap_arr;
	u32		arr_size;
};

#define QEDE_FORCED_SPEED_MAP(value)					\
{									\
	.speed		= SPEED_##value,				\
	.cap_arr	= qede_forced_speed_##value,			\
	.arr_size	= ARRAY_SIZE(qede_forced_speed_##value),	\
}

static const u32 qede_forced_speed_1000[] __initconst = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
};

static const u32 qede_forced_speed_10000[] __initconst = {
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
};

static const u32 qede_forced_speed_20000[] __initconst = {
	ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT,
};

static const u32 qede_forced_speed_25000[] __initconst = {
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

static const u32 qede_forced_speed_40000[] __initconst = {
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
};

static const u32 qede_forced_speed_50000[] __initconst = {
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

static const u32 qede_forced_speed_100000[] __initconst = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

static struct qede_forced_speed_map qede_forced_speed_maps[] __ro_after_init = {
	QEDE_FORCED_SPEED_MAP(1000),
	QEDE_FORCED_SPEED_MAP(10000),
	QEDE_FORCED_SPEED_MAP(20000),
	QEDE_FORCED_SPEED_MAP(25000),
	QEDE_FORCED_SPEED_MAP(40000),
	QEDE_FORCED_SPEED_MAP(50000),
	QEDE_FORCED_SPEED_MAP(100000),
};

void __init qede_forced_speed_maps_init(void)
{
	struct qede_forced_speed_map *map;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(qede_forced_speed_maps); i++) {
		map = qede_forced_speed_maps + i;

		linkmode_set_bit_array(map->cap_arr, map->arr_size, map->caps);
		map->cap_arr = NULL;
		map->arr_size = 0;
	}
}

/* Ethtool callbacks */

static void qede_get_strings_stats_txq(struct qede_dev *edev,
				       struct qede_tx_queue *txq, u8 **buf)
{
	int i;

	for (i = 0; i < QEDE_NUM_TQSTATS; i++) {
		if (txq->is_xdp)
			sprintf(*buf, "%d [XDP]: %s",
				QEDE_TXQ_XDP_TO_IDX(edev, txq),
				qede_tqstats_arr[i].string);
		else
			sprintf(*buf, "%d_%d: %s", txq->index, txq->cos,
				qede_tqstats_arr[i].string);
		*buf += ETH_GSTRING_LEN;
	}
}

static void qede_get_strings_stats_rxq(struct qede_dev *edev,
				       struct qede_rx_queue *rxq, u8 **buf)
{
	int i;

	for (i = 0; i < QEDE_NUM_RQSTATS; i++) {
		sprintf(*buf, "%d: %s", rxq->rxq_id,
			qede_rqstats_arr[i].string);
		*buf += ETH_GSTRING_LEN;
	}
}

static bool qede_is_irrelevant_stat(struct qede_dev *edev, int stat_index)
{
	return (IS_VF(edev) && QEDE_STAT_IS_PF_ONLY(stat_index)) ||
	       (QEDE_IS_BB(edev) && QEDE_STAT_IS_AH_ONLY(stat_index)) ||
	       (QEDE_IS_AH(edev) && QEDE_STAT_IS_BB_ONLY(stat_index));
}

static void qede_get_strings_stats(struct qede_dev *edev, u8 *buf)
{
	struct qede_fastpath *fp;
	int i;

	/* Account for queue statistics */
	for (i = 0; i < QEDE_QUEUE_CNT(edev); i++) {
		fp = &edev->fp_array[i];

		if (fp->type & QEDE_FASTPATH_RX)
			qede_get_strings_stats_rxq(edev, fp->rxq, &buf);

		if (fp->type & QEDE_FASTPATH_XDP)
			qede_get_strings_stats_txq(edev, fp->xdp_tx, &buf);

		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos)
				qede_get_strings_stats_txq(edev,
							   &fp->txq[cos], &buf);
		}
	}

	/* Account for non-queue statistics */
	for (i = 0; i < QEDE_NUM_STATS; i++) {
		if (qede_is_irrelevant_stat(edev, i))
			continue;
		strcpy(buf, qede_stats_arr[i].string);
		buf += ETH_GSTRING_LEN;
	}
}

static void qede_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_STATS:
		qede_get_strings_stats(edev, buf);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, qede_private_arr,
		       ETH_GSTRING_LEN * QEDE_PRI_FLAG_LEN);
		break;
	case ETH_SS_TEST:
		memcpy(buf, qede_tests_str_arr,
		       ETH_GSTRING_LEN * QEDE_ETHTOOL_TEST_MAX);
		break;
	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Unsupported stringset 0x%08x\n", stringset);
	}
}

static void qede_get_ethtool_stats_txq(struct qede_tx_queue *txq, u64 **buf)
{
	int i;

	for (i = 0; i < QEDE_NUM_TQSTATS; i++) {
		**buf = *((u64 *)(((void *)txq) + qede_tqstats_arr[i].offset));
		(*buf)++;
	}
}

static void qede_get_ethtool_stats_rxq(struct qede_rx_queue *rxq, u64 **buf)
{
	int i;

	for (i = 0; i < QEDE_NUM_RQSTATS; i++) {
		**buf = *((u64 *)(((void *)rxq) + qede_rqstats_arr[i].offset));
		(*buf)++;
	}
}

static void qede_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_fastpath *fp;
	int i;

	qede_fill_by_demand_stats(edev);

	/* Need to protect the access to the fastpath array */
	__qede_lock(edev);

	for (i = 0; i < QEDE_QUEUE_CNT(edev); i++) {
		fp = &edev->fp_array[i];

		if (fp->type & QEDE_FASTPATH_RX)
			qede_get_ethtool_stats_rxq(fp->rxq, &buf);

		if (fp->type & QEDE_FASTPATH_XDP)
			qede_get_ethtool_stats_txq(fp->xdp_tx, &buf);

		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos)
				qede_get_ethtool_stats_txq(&fp->txq[cos], &buf);
		}
	}

	for (i = 0; i < QEDE_NUM_STATS; i++) {
		if (qede_is_irrelevant_stat(edev, i))
			continue;
		*buf = *((u64 *)(((void *)&edev->stats) +
				 qede_stats_arr[i].offset));

		buf++;
	}

	__qede_unlock(edev);
}

static int qede_get_sset_count(struct net_device *dev, int stringset)
{
	struct qede_dev *edev = netdev_priv(dev);
	int num_stats = QEDE_NUM_STATS, i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < QEDE_NUM_STATS; i++)
			if (qede_is_irrelevant_stat(edev, i))
				num_stats--;

		/* Account for the Regular Tx statistics */
		num_stats += QEDE_TSS_COUNT(edev) * QEDE_NUM_TQSTATS *
				edev->dev_info.num_tc;

		/* Account for the Regular Rx statistics */
		num_stats += QEDE_RSS_COUNT(edev) * QEDE_NUM_RQSTATS;

		/* Account for XDP statistics [if needed] */
		if (edev->xdp_prog)
			num_stats += QEDE_RSS_COUNT(edev) * QEDE_NUM_TQSTATS;
		return num_stats;

	case ETH_SS_PRIV_FLAGS:
		return QEDE_PRI_FLAG_LEN;
	case ETH_SS_TEST:
		if (!IS_VF(edev))
			return QEDE_ETHTOOL_TEST_MAX;
		else
			return 0;
	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Unsupported stringset 0x%08x\n", stringset);
		return -EINVAL;
	}
}

static u32 qede_get_priv_flags(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	u32 flags = 0;

	if (edev->dev_info.common.num_hwfns > 1)
		flags |= BIT(QEDE_PRI_FLAG_CMT);

	if (edev->dev_info.common.smart_an)
		flags |= BIT(QEDE_PRI_FLAG_SMART_AN_SUPPORT);

	if (edev->err_flags & BIT(QEDE_ERR_IS_RECOVERABLE))
		flags |= BIT(QEDE_PRI_FLAG_RECOVER_ON_ERROR);

	return flags;
}

static int qede_set_priv_flags(struct net_device *dev, u32 flags)
{
	struct qede_dev *edev = netdev_priv(dev);
	u32 cflags = qede_get_priv_flags(dev);
	u32 dflags = flags ^ cflags;

	/* can only change RECOVER_ON_ERROR flag */
	if (dflags & ~BIT(QEDE_PRI_FLAG_RECOVER_ON_ERROR))
		return -EINVAL;

	if (flags & BIT(QEDE_PRI_FLAG_RECOVER_ON_ERROR))
		set_bit(QEDE_ERR_IS_RECOVERABLE, &edev->err_flags);
	else
		clear_bit(QEDE_ERR_IS_RECOVERABLE, &edev->err_flags);

	return 0;
}

static int qede_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *cmd)
{
	typeof(cmd->link_modes) *link_modes = &cmd->link_modes;
	struct ethtool_link_settings *base = &cmd->base;
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	__qede_lock(edev);

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	linkmode_copy(link_modes->supported, current_link.supported_caps);
	linkmode_copy(link_modes->advertising, current_link.advertised_caps);
	linkmode_copy(link_modes->lp_advertising, current_link.lp_caps);

	if ((edev->state == QEDE_STATE_OPEN) && (current_link.link_up)) {
		base->speed = current_link.speed;
		base->duplex = current_link.duplex;
	} else {
		base->speed = SPEED_UNKNOWN;
		base->duplex = DUPLEX_UNKNOWN;
	}

	__qede_unlock(edev);

	base->port = current_link.port;
	base->autoneg = (current_link.autoneg) ? AUTONEG_ENABLE :
			AUTONEG_DISABLE;

	return 0;
}

static int qede_set_link_ksettings(struct net_device *dev,
				   const struct ethtool_link_ksettings *cmd)
{
	const struct ethtool_link_settings *base = &cmd->base;
	struct qede_dev *edev = netdev_priv(dev);
	const struct qede_forced_speed_map *map;
	struct qed_link_output current_link;
	struct qed_link_params params;
	u32 i;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev, "Link settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}
	memset(&current_link, 0, sizeof(current_link));
	memset(&params, 0, sizeof(params));
	edev->ops->common->get_link(edev->cdev, &current_link);

	params.override_flags |= QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS;
	params.override_flags |= QED_LINK_OVERRIDE_SPEED_AUTONEG;

	if (base->autoneg == AUTONEG_ENABLE) {
		if (!phylink_test(current_link.supported_caps, Autoneg)) {
			DP_INFO(edev, "Auto negotiation is not supported\n");
			return -EOPNOTSUPP;
		}

		params.autoneg = true;
		params.forced_speed = 0;

		linkmode_copy(params.adv_speeds, cmd->link_modes.advertising);
	} else {		/* forced speed */
		params.override_flags |= QED_LINK_OVERRIDE_SPEED_FORCED_SPEED;
		params.autoneg = false;
		params.forced_speed = base->speed;

		for (i = 0; i < ARRAY_SIZE(qede_forced_speed_maps); i++) {
			map = qede_forced_speed_maps + i;

			if (base->speed != map->speed ||
			    !linkmode_intersects(current_link.supported_caps,
						 map->caps))
				continue;

			linkmode_and(params.adv_speeds,
				     current_link.supported_caps, map->caps);
			goto set_link;
		}

		DP_INFO(edev, "Unsupported speed %u\n", base->speed);
		return -EINVAL;
	}

set_link:
	params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
}

static void qede_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	char mfw[ETHTOOL_FWVERS_LEN], storm[ETHTOOL_FWVERS_LEN];
	struct qede_dev *edev = netdev_priv(ndev);
	char mbi[ETHTOOL_FWVERS_LEN];

	strlcpy(info->driver, "qede", sizeof(info->driver));

	snprintf(storm, ETHTOOL_FWVERS_LEN, "%d.%d.%d.%d",
		 edev->dev_info.common.fw_major,
		 edev->dev_info.common.fw_minor,
		 edev->dev_info.common.fw_rev,
		 edev->dev_info.common.fw_eng);

	snprintf(mfw, ETHTOOL_FWVERS_LEN, "%d.%d.%d.%d",
		 (edev->dev_info.common.mfw_rev >> 24) & 0xFF,
		 (edev->dev_info.common.mfw_rev >> 16) & 0xFF,
		 (edev->dev_info.common.mfw_rev >> 8) & 0xFF,
		 edev->dev_info.common.mfw_rev & 0xFF);

	if ((strlen(storm) + strlen(DRV_MODULE_VERSION) + strlen("[storm]  ")) <
	    sizeof(info->version))
		snprintf(info->version, sizeof(info->version),
			 "%s [storm %s]", DRV_MODULE_VERSION, storm);
	else
		snprintf(info->version, sizeof(info->version),
			 "%s %s", DRV_MODULE_VERSION, storm);

	if (edev->dev_info.common.mbi_version) {
		snprintf(mbi, ETHTOOL_FWVERS_LEN, "%d.%d.%d",
			 (edev->dev_info.common.mbi_version &
			  QED_MBI_VERSION_2_MASK) >> QED_MBI_VERSION_2_OFFSET,
			 (edev->dev_info.common.mbi_version &
			  QED_MBI_VERSION_1_MASK) >> QED_MBI_VERSION_1_OFFSET,
			 (edev->dev_info.common.mbi_version &
			  QED_MBI_VERSION_0_MASK) >> QED_MBI_VERSION_0_OFFSET);
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "mbi %s [mfw %s]", mbi, mfw);
	} else {
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "mfw %s", mfw);
	}

	strlcpy(info->bus_info, pci_name(edev->pdev), sizeof(info->bus_info));
}

static void qede_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct qede_dev *edev = netdev_priv(ndev);

	if (edev->dev_info.common.wol_support) {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = edev->wol_enabled ? WAKE_MAGIC : 0;
	}
}

static int qede_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct qede_dev *edev = netdev_priv(ndev);
	bool wol_requested;
	int rc;

	if (wol->wolopts & ~WAKE_MAGIC) {
		DP_INFO(edev,
			"Can't support WoL options other than magic-packet\n");
		return -EINVAL;
	}

	wol_requested = !!(wol->wolopts & WAKE_MAGIC);
	if (wol_requested == edev->wol_enabled)
		return 0;

	/* Need to actually change configuration */
	if (!edev->dev_info.common.wol_support) {
		DP_INFO(edev, "Device doesn't support WoL\n");
		return -EINVAL;
	}

	rc = edev->ops->common->update_wol(edev->cdev, wol_requested);
	if (!rc)
		edev->wol_enabled = wol_requested;

	return rc;
}

static u32 qede_get_msglevel(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	return ((u32)edev->dp_level << QED_LOG_LEVEL_SHIFT) | edev->dp_module;
}

static void qede_set_msglevel(struct net_device *ndev, u32 level)
{
	struct qede_dev *edev = netdev_priv(ndev);
	u32 dp_module = 0;
	u8 dp_level = 0;

	qede_config_debug(level, &dp_module, &dp_level);

	edev->dp_level = dp_level;
	edev->dp_module = dp_module;
	edev->ops->common->update_msglvl(edev->cdev,
					 dp_module, dp_level);
}

static int qede_nway_reset(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;
	struct qed_link_params link_params;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev, "Link settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	if (!netif_running(dev))
		return 0;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);
	if (!current_link.link_up)
		return 0;

	/* Toggle the link */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = false;
	edev->ops->common->set_link(edev->cdev, &link_params);
	link_params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &link_params);

	return 0;
}

static u32 qede_get_link(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	return current_link.link_up;
}

static int qede_flash_device(struct net_device *dev,
			     struct ethtool_flash *flash)
{
	struct qede_dev *edev = netdev_priv(dev);

	return edev->ops->common->nvm_flash(edev->cdev, flash->data);
}

static int qede_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coal)
{
	void *rx_handle = NULL, *tx_handle = NULL;
	struct qede_dev *edev = netdev_priv(dev);
	u16 rx_coal, tx_coal, i, rc = 0;
	struct qede_fastpath *fp;

	rx_coal = QED_DEFAULT_RX_USECS;
	tx_coal = QED_DEFAULT_TX_USECS;

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	__qede_lock(edev);
	if (edev->state == QEDE_STATE_OPEN) {
		for_each_queue(i) {
			fp = &edev->fp_array[i];

			if (fp->type & QEDE_FASTPATH_RX) {
				rx_handle = fp->rxq->handle;
				break;
			}
		}

		rc = edev->ops->get_coalesce(edev->cdev, &rx_coal, rx_handle);
		if (rc) {
			DP_INFO(edev, "Read Rx coalesce error\n");
			goto out;
		}

		for_each_queue(i) {
			struct qede_tx_queue *txq;

			fp = &edev->fp_array[i];

			/* All TX queues of given fastpath uses same
			 * coalescing value, so no need to iterate over
			 * all TCs, TC0 txq should suffice.
			 */
			if (fp->type & QEDE_FASTPATH_TX) {
				txq = QEDE_FP_TC0_TXQ(fp);
				tx_handle = txq->handle;
				break;
			}
		}

		rc = edev->ops->get_coalesce(edev->cdev, &tx_coal, tx_handle);
		if (rc)
			DP_INFO(edev, "Read Tx coalesce error\n");
	}

out:
	__qede_unlock(edev);

	coal->rx_coalesce_usecs = rx_coal;
	coal->tx_coalesce_usecs = tx_coal;

	return rc;
}

int qede_set_coalesce(struct net_device *dev, struct ethtool_coalesce *coal)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_fastpath *fp;
	int i, rc = 0;
	u16 rxc, txc;

	if (!netif_running(dev)) {
		DP_INFO(edev, "Interface is down\n");
		return -EINVAL;
	}

	if (coal->rx_coalesce_usecs > QED_COALESCE_MAX ||
	    coal->tx_coalesce_usecs > QED_COALESCE_MAX) {
		DP_INFO(edev,
			"Can't support requested %s coalesce value [max supported value %d]\n",
			coal->rx_coalesce_usecs > QED_COALESCE_MAX ? "rx" :
			"tx", QED_COALESCE_MAX);
		return -EINVAL;
	}

	rxc = (u16)coal->rx_coalesce_usecs;
	txc = (u16)coal->tx_coalesce_usecs;
	for_each_queue(i) {
		fp = &edev->fp_array[i];

		if (edev->fp_array[i].type & QEDE_FASTPATH_RX) {
			rc = edev->ops->common->set_coalesce(edev->cdev,
							     rxc, 0,
							     fp->rxq->handle);
			if (rc) {
				DP_INFO(edev,
					"Set RX coalesce error, rc = %d\n", rc);
				return rc;
			}
			edev->coal_entry[i].rxc = rxc;
			edev->coal_entry[i].isvalid = true;
		}

		if (edev->fp_array[i].type & QEDE_FASTPATH_TX) {
			struct qede_tx_queue *txq;

			/* All TX queues of given fastpath uses same
			 * coalescing value, so no need to iterate over
			 * all TCs, TC0 txq should suffice.
			 */
			txq = QEDE_FP_TC0_TXQ(fp);

			rc = edev->ops->common->set_coalesce(edev->cdev,
							     0, txc,
							     txq->handle);
			if (rc) {
				DP_INFO(edev,
					"Set TX coalesce error, rc = %d\n", rc);
				return rc;
			}
			edev->coal_entry[i].txc = txc;
			edev->coal_entry[i].isvalid = true;
		}
	}

	return rc;
}

static void qede_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct qede_dev *edev = netdev_priv(dev);

	ering->rx_max_pending = NUM_RX_BDS_MAX;
	ering->rx_pending = edev->q_num_rx_buffers;
	ering->tx_max_pending = NUM_TX_BDS_MAX;
	ering->tx_pending = edev->q_num_tx_buffers;
}

static int qede_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct qede_dev *edev = netdev_priv(dev);

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "Set ring params command parameters: rx_pending = %d, tx_pending = %d\n",
		   ering->rx_pending, ering->tx_pending);

	/* Validate legality of configuration */
	if (ering->rx_pending > NUM_RX_BDS_MAX ||
	    ering->rx_pending < NUM_RX_BDS_MIN ||
	    ering->tx_pending > NUM_TX_BDS_MAX ||
	    ering->tx_pending < NUM_TX_BDS_MIN) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Can only support Rx Buffer size [0%08x,...,0x%08x] and Tx Buffer size [0x%08x,...,0x%08x]\n",
			   NUM_RX_BDS_MIN, NUM_RX_BDS_MAX,
			   NUM_TX_BDS_MIN, NUM_TX_BDS_MAX);
		return -EINVAL;
	}

	/* Change ring size and re-load */
	edev->q_num_rx_buffers = ering->rx_pending;
	edev->q_num_tx_buffers = ering->tx_pending;

	qede_reload(edev, NULL, false);

	return 0;
}

static void qede_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	if (current_link.pause_config & QED_LINK_PAUSE_AUTONEG_ENABLE)
		epause->autoneg = true;
	if (current_link.pause_config & QED_LINK_PAUSE_RX_ENABLE)
		epause->rx_pause = true;
	if (current_link.pause_config & QED_LINK_PAUSE_TX_ENABLE)
		epause->tx_pause = true;

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "ethtool_pauseparam: cmd %d  autoneg %d  rx_pause %d  tx_pause %d\n",
		   epause->cmd, epause->autoneg, epause->rx_pause,
		   epause->tx_pause);
}

static int qede_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *epause)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_params params;
	struct qed_link_output current_link;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev,
			"Pause settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	memset(&params, 0, sizeof(params));
	params.override_flags |= QED_LINK_OVERRIDE_PAUSE_CONFIG;

	if (epause->autoneg) {
		if (!phylink_test(current_link.supported_caps, Autoneg)) {
			DP_INFO(edev, "autoneg not supported\n");
			return -EINVAL;
		}

		params.pause_config |= QED_LINK_PAUSE_AUTONEG_ENABLE;
	}

	if (epause->rx_pause)
		params.pause_config |= QED_LINK_PAUSE_RX_ENABLE;
	if (epause->tx_pause)
		params.pause_config |= QED_LINK_PAUSE_TX_ENABLE;

	params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
}

static void qede_get_regs(struct net_device *ndev,
			  struct ethtool_regs *regs, void *buffer)
{
	struct qede_dev *edev = netdev_priv(ndev);

	regs->version = 0;
	memset(buffer, 0, regs->len);

	if (edev->ops && edev->ops->common)
		edev->ops->common->dbg_all_data(edev->cdev, buffer);
}

static int qede_get_regs_len(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	if (edev->ops && edev->ops->common)
		return edev->ops->common->dbg_all_data_size(edev->cdev);
	else
		return -EINVAL;
}

static void qede_update_mtu(struct qede_dev *edev,
			    struct qede_reload_args *args)
{
	edev->ndev->mtu = args->u.mtu;
}

/* Netdevice NDOs */
int qede_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct qede_reload_args args;

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "Configuring MTU size of %d\n", new_mtu);

	if (new_mtu > PAGE_SIZE)
		ndev->features &= ~NETIF_F_GRO_HW;

	/* Set the mtu field and re-start the interface if needed */
	args.u.mtu = new_mtu;
	args.func = &qede_update_mtu;
	qede_reload(edev, &args, false);
#if IS_ENABLED(CONFIG_QED_RDMA)
	qede_rdma_event_change_mtu(edev);
#endif
	edev->ops->common->update_mtu(edev->cdev, new_mtu);

	return 0;
}

static void qede_get_channels(struct net_device *dev,
			      struct ethtool_channels *channels)
{
	struct qede_dev *edev = netdev_priv(dev);

	channels->max_combined = QEDE_MAX_RSS_CNT(edev);
	channels->max_rx = QEDE_MAX_RSS_CNT(edev);
	channels->max_tx = QEDE_MAX_RSS_CNT(edev);
	channels->combined_count = QEDE_QUEUE_CNT(edev) - edev->fp_num_tx -
					edev->fp_num_rx;
	channels->tx_count = edev->fp_num_tx;
	channels->rx_count = edev->fp_num_rx;
}

static int qede_set_channels(struct net_device *dev,
			     struct ethtool_channels *channels)
{
	struct qede_dev *edev = netdev_priv(dev);
	u32 count;

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "set-channels command parameters: rx = %d, tx = %d, other = %d, combined = %d\n",
		   channels->rx_count, channels->tx_count,
		   channels->other_count, channels->combined_count);

	count = channels->rx_count + channels->tx_count +
			channels->combined_count;

	/* We don't support `other' channels */
	if (channels->other_count) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "command parameters not supported\n");
		return -EINVAL;
	}

	if (!(channels->combined_count || (channels->rx_count &&
					   channels->tx_count))) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "need to request at least one transmit and one receive channel\n");
		return -EINVAL;
	}

	if (count > QEDE_MAX_RSS_CNT(edev)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "requested channels = %d max supported channels = %d\n",
			   count, QEDE_MAX_RSS_CNT(edev));
		return -EINVAL;
	}

	/* Check if there was a change in the active parameters */
	if ((count == QEDE_QUEUE_CNT(edev)) &&
	    (channels->tx_count == edev->fp_num_tx) &&
	    (channels->rx_count == edev->fp_num_rx)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "No change in active parameters\n");
		return 0;
	}

	/* We need the number of queues to be divisible between the hwfns */
	if ((count % edev->dev_info.common.num_hwfns) ||
	    (channels->tx_count % edev->dev_info.common.num_hwfns) ||
	    (channels->rx_count % edev->dev_info.common.num_hwfns)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Number of channels must be divisible by %04x\n",
			   edev->dev_info.common.num_hwfns);
		return -EINVAL;
	}

	/* Set number of queues and reload if necessary */
	edev->req_queues = count;
	edev->req_num_tx = channels->tx_count;
	edev->req_num_rx = channels->rx_count;
	/* Reset the indirection table if rx queue count is updated */
	if ((edev->req_queues - edev->req_num_tx) != QEDE_RSS_COUNT(edev)) {
		edev->rss_params_inited &= ~QEDE_RSS_INDIR_INITED;
		memset(edev->rss_ind_table, 0, sizeof(edev->rss_ind_table));
	}

	qede_reload(edev, NULL, false);

	return 0;
}

static int qede_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct qede_dev *edev = netdev_priv(dev);

	return qede_ptp_get_ts_info(edev, info);
}

static int qede_set_phys_id(struct net_device *dev,
			    enum ethtool_phys_id_state state)
{
	struct qede_dev *edev = netdev_priv(dev);
	u8 led_state = 0;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		led_state = QED_LED_MODE_ON;
		break;

	case ETHTOOL_ID_OFF:
		led_state = QED_LED_MODE_OFF;
		break;

	case ETHTOOL_ID_INACTIVE:
		led_state = QED_LED_MODE_RESTORE;
		break;
	}

	edev->ops->common->set_led(edev->cdev, led_state);

	return 0;
}

static int qede_get_rss_flags(struct qede_dev *edev, struct ethtool_rxnfc *info)
{
	info->data = RXH_IP_SRC | RXH_IP_DST;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (edev->rss_caps & QED_RSS_IPV4_UDP)
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V6_FLOW:
		if (edev->rss_caps & QED_RSS_IPV6_UDP)
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		info->data = 0;
		break;
	}

	return 0;
}

static int qede_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			  u32 *rule_locs)
{
	struct qede_dev *edev = netdev_priv(dev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = QEDE_RSS_COUNT(edev);
		break;
	case ETHTOOL_GRXFH:
		rc = qede_get_rss_flags(edev, info);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		info->rule_cnt = qede_get_arfs_filter_count(edev);
		info->data = QEDE_RFS_MAX_FLTR;
		break;
	case ETHTOOL_GRXCLSRULE:
		rc = qede_get_cls_rule_entry(edev, info);
		break;
	case ETHTOOL_GRXCLSRLALL:
		rc = qede_get_cls_rule_all(edev, info, rule_locs);
		break;
	default:
		DP_ERR(edev, "Command parameters not supported\n");
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int qede_set_rss_flags(struct qede_dev *edev, struct ethtool_rxnfc *info)
{
	struct qed_update_vport_params *vport_update_params;
	u8 set_caps = 0, clr_caps = 0;
	int rc = 0;

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Set rss flags command parameters: flow type = %d, data = %llu\n",
		   info->flow_type, info->data);

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		/* For TCP only 4-tuple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST |
				  RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	case UDP_V4_FLOW:
		/* For UDP either 2-tuple hash or 4-tuple hash is supported */
		if (info->data == (RXH_IP_SRC | RXH_IP_DST |
				   RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			set_caps = QED_RSS_IPV4_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple enabled\n");
		} else if (info->data == (RXH_IP_SRC | RXH_IP_DST)) {
			clr_caps = QED_RSS_IPV4_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple disabled\n");
		} else {
			return -EINVAL;
		}
		break;
	case UDP_V6_FLOW:
		/* For UDP either 2-tuple hash or 4-tuple hash is supported */
		if (info->data == (RXH_IP_SRC | RXH_IP_DST |
				   RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			set_caps = QED_RSS_IPV6_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple enabled\n");
		} else if (info->data == (RXH_IP_SRC | RXH_IP_DST)) {
			clr_caps = QED_RSS_IPV6_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple disabled\n");
		} else {
			return -EINVAL;
		}
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		/* For IP only 2-tuple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST)) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IP_USER_FLOW:
	case ETHER_FLOW:
		/* RSS is not supported for these protocols */
		if (info->data) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	default:
		return -EINVAL;
	}

	/* No action is needed if there is no change in the rss capability */
	if (edev->rss_caps == ((edev->rss_caps & ~clr_caps) | set_caps))
		return 0;

	/* Update internal configuration */
	edev->rss_caps = ((edev->rss_caps & ~clr_caps) | set_caps);
	edev->rss_params_inited |= QEDE_RSS_CAPS_INITED;

	/* Re-configure if possible */
	__qede_lock(edev);
	if (edev->state == QEDE_STATE_OPEN) {
		vport_update_params = vzalloc(sizeof(*vport_update_params));
		if (!vport_update_params) {
			__qede_unlock(edev);
			return -ENOMEM;
		}
		qede_fill_rss_params(edev, &vport_update_params->rss_params,
				     &vport_update_params->update_rss_flg);
		rc = edev->ops->vport_update(edev->cdev, vport_update_params);
		vfree(vport_update_params);
	}
	__qede_unlock(edev);

	return rc;
}

static int qede_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info)
{
	struct qede_dev *edev = netdev_priv(dev);
	int rc;

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		rc = qede_set_rss_flags(edev, info);
		break;
	case ETHTOOL_SRXCLSRLINS:
		rc = qede_add_cls_rule(edev, info);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		rc = qede_delete_flow_filter(edev, info->fs.location);
		break;
	default:
		DP_INFO(edev, "Command parameters not supported\n");
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static u32 qede_get_rxfh_indir_size(struct net_device *dev)
{
	return QED_RSS_IND_TABLE_SIZE;
}

static u32 qede_get_rxfh_key_size(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);

	return sizeof(edev->rss_key);
}

static int qede_get_rxfh(struct net_device *dev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct qede_dev *edev = netdev_priv(dev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++)
		indir[i] = edev->rss_ind_table[i];

	if (key)
		memcpy(key, edev->rss_key, qede_get_rxfh_key_size(dev));

	return 0;
}

static int qede_set_rxfh(struct net_device *dev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct qed_update_vport_params *vport_update_params;
	struct qede_dev *edev = netdev_priv(dev);
	int i, rc = 0;

	if (edev->dev_info.common.num_hwfns > 1) {
		DP_INFO(edev,
			"RSS configuration is not supported for 100G devices\n");
		return -EOPNOTSUPP;
	}

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!indir && !key)
		return 0;

	if (indir) {
		for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++)
			edev->rss_ind_table[i] = indir[i];
		edev->rss_params_inited |= QEDE_RSS_INDIR_INITED;
	}

	if (key) {
		memcpy(&edev->rss_key, key, qede_get_rxfh_key_size(dev));
		edev->rss_params_inited |= QEDE_RSS_KEY_INITED;
	}

	__qede_lock(edev);
	if (edev->state == QEDE_STATE_OPEN) {
		vport_update_params = vzalloc(sizeof(*vport_update_params));
		if (!vport_update_params) {
			__qede_unlock(edev);
			return -ENOMEM;
		}
		qede_fill_rss_params(edev, &vport_update_params->rss_params,
				     &vport_update_params->update_rss_flg);
		rc = edev->ops->vport_update(edev->cdev, vport_update_params);
		vfree(vport_update_params);
	}
	__qede_unlock(edev);

	return rc;
}

/* This function enables the interrupt generation and the NAPI on the device */
static void qede_netif_start(struct qede_dev *edev)
{
	int i;

	if (!netif_running(edev->ndev))
		return;

	for_each_queue(i) {
		/* Update and reenable interrupts */
		qed_sb_ack(edev->fp_array[i].sb_info, IGU_INT_ENABLE, 1);
		napi_enable(&edev->fp_array[i].napi);
	}
}

/* This function disables the NAPI and the interrupt generation on the device */
static void qede_netif_stop(struct qede_dev *edev)
{
	int i;

	for_each_queue(i) {
		napi_disable(&edev->fp_array[i].napi);
		/* Disable interrupts */
		qed_sb_ack(edev->fp_array[i].sb_info, IGU_INT_DISABLE, 0);
	}
}

static int qede_selftest_transmit_traffic(struct qede_dev *edev,
					  struct sk_buff *skb)
{
	struct qede_tx_queue *txq = NULL;
	struct eth_tx_1st_bd *first_bd;
	dma_addr_t mapping;
	int i, idx;
	u16 val;

	for_each_queue(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];

		if (fp->type & QEDE_FASTPATH_TX) {
			txq = QEDE_FP_TC0_TXQ(fp);
			break;
		}
	}

	if (!txq) {
		DP_NOTICE(edev, "Tx path is not available\n");
		return -1;
	}

	/* Fill the entry in the SW ring and the BDs in the FW ring */
	idx = txq->sw_tx_prod;
	txq->sw_tx_ring.skbs[idx].skb = skb;
	first_bd = qed_chain_produce(&txq->tx_pbl);
	memset(first_bd, 0, sizeof(*first_bd));
	val = 1 << ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT;
	first_bd->data.bd_flags.bitfields = val;
	val = skb->len & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK;
	val = val << ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT;
	first_bd->data.bitfields |= cpu_to_le16(val);

	/* Map skb linear data for DMA and set in the first BD */
	mapping = dma_map_single(&edev->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		DP_NOTICE(edev, "SKB mapping failed\n");
		return -ENOMEM;
	}
	BD_SET_UNMAP_ADDR_LEN(first_bd, mapping, skb_headlen(skb));

	/* update the first BD with the actual num BDs */
	first_bd->data.nbds = 1;
	txq->sw_tx_prod = (txq->sw_tx_prod + 1) % txq->num_tx_buffers;
	/* 'next page' entries are counted in the producer value */
	val = qed_chain_get_prod_idx(&txq->tx_pbl);
	txq->tx_db.data.bd_prod = cpu_to_le16(val);

	/* wmb makes sure that the BDs data is updated before updating the
	 * producer, otherwise FW may read old data from the BDs.
	 */
	wmb();
	barrier();
	writel(txq->tx_db.raw, txq->doorbell_addr);

	for (i = 0; i < QEDE_SELFTEST_POLL_COUNT; i++) {
		if (qede_txq_has_work(txq))
			break;
		usleep_range(100, 200);
	}

	if (!qede_txq_has_work(txq)) {
		DP_NOTICE(edev, "Tx completion didn't happen\n");
		return -1;
	}

	first_bd = (struct eth_tx_1st_bd *)qed_chain_consume(&txq->tx_pbl);
	dma_unmap_single(&edev->pdev->dev, BD_UNMAP_ADDR(first_bd),
			 BD_UNMAP_LEN(first_bd), DMA_TO_DEVICE);
	txq->sw_tx_cons = (txq->sw_tx_cons + 1) % txq->num_tx_buffers;
	txq->sw_tx_ring.skbs[idx].skb = NULL;

	return 0;
}

static int qede_selftest_receive_traffic(struct qede_dev *edev)
{
	u16 sw_rx_index, len;
	struct eth_fast_path_rx_reg_cqe *fp_cqe;
	struct qede_rx_queue *rxq = NULL;
	struct sw_rx_data *sw_rx_data;
	union eth_rx_cqe *cqe;
	int i, iter, rc = 0;
	u8 *data_ptr;

	for_each_queue(i) {
		if (edev->fp_array[i].type & QEDE_FASTPATH_RX) {
			rxq = edev->fp_array[i].rxq;
			break;
		}
	}

	if (!rxq) {
		DP_NOTICE(edev, "Rx path is not available\n");
		return -1;
	}

	/* The packet is expected to receive on rx-queue 0 even though RSS is
	 * enabled. This is because the queue 0 is configured as the default
	 * queue and that the loopback traffic is not IP.
	 */
	for (iter = 0; iter < QEDE_SELFTEST_POLL_COUNT; iter++) {
		if (!qede_has_rx_work(rxq)) {
			usleep_range(100, 200);
			continue;
		}

		/* Get the CQE from the completion ring */
		cqe = (union eth_rx_cqe *)qed_chain_consume(&rxq->rx_comp_ring);

		/* Get the data from the SW ring */
		sw_rx_index = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
		sw_rx_data = &rxq->sw_rx_ring[sw_rx_index];
		fp_cqe = &cqe->fast_path_regular;
		len =  le16_to_cpu(fp_cqe->len_on_first_bd);
		data_ptr = (u8 *)(page_address(sw_rx_data->data) +
				  fp_cqe->placement_offset +
				  sw_rx_data->page_offset +
				  rxq->rx_headroom);
		if (ether_addr_equal(data_ptr,  edev->ndev->dev_addr) &&
		    ether_addr_equal(data_ptr + ETH_ALEN,
				     edev->ndev->dev_addr)) {
			for (i = ETH_HLEN; i < len; i++)
				if (data_ptr[i] != (unsigned char)(i & 0xff)) {
					rc = -1;
					break;
				}

			qede_recycle_rx_bd_ring(rxq, 1);
			qed_chain_recycle_consumed(&rxq->rx_comp_ring);
			break;
		}

		DP_INFO(edev, "Not the transmitted packet\n");
		qede_recycle_rx_bd_ring(rxq, 1);
		qed_chain_recycle_consumed(&rxq->rx_comp_ring);
	}

	if (iter == QEDE_SELFTEST_POLL_COUNT) {
		DP_NOTICE(edev, "Failed to receive the traffic\n");
		return -1;
	}

	qede_update_rx_prod(edev, rxq);

	return rc;
}

static int qede_selftest_run_loopback(struct qede_dev *edev, u32 loopback_mode)
{
	struct qed_link_params link_params;
	struct sk_buff *skb = NULL;
	int rc = 0, i;
	u32 pkt_size;
	u8 *packet;

	if (!netif_running(edev->ndev)) {
		DP_NOTICE(edev, "Interface is down\n");
		return -EINVAL;
	}

	qede_netif_stop(edev);

	/* Bring up the link in Loopback mode */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	link_params.override_flags = QED_LINK_OVERRIDE_LOOPBACK_MODE;
	link_params.loopback_mode = loopback_mode;
	edev->ops->common->set_link(edev->cdev, &link_params);

	/* Wait for loopback configuration to apply */
	msleep_interruptible(500);

	/* Setting max packet size to 1.5K to avoid data being split over
	 * multiple BDs in cases where MTU > PAGE_SIZE.
	 */
	pkt_size = (((edev->ndev->mtu < ETH_DATA_LEN) ?
		     edev->ndev->mtu : ETH_DATA_LEN) + ETH_HLEN);

	skb = netdev_alloc_skb(edev->ndev, pkt_size);
	if (!skb) {
		DP_INFO(edev, "Can't allocate skb\n");
		rc = -ENOMEM;
		goto test_loopback_exit;
	}
	packet = skb_put(skb, pkt_size);
	ether_addr_copy(packet, edev->ndev->dev_addr);
	ether_addr_copy(packet + ETH_ALEN, edev->ndev->dev_addr);
	memset(packet + (2 * ETH_ALEN), 0x77, (ETH_HLEN - (2 * ETH_ALEN)));
	for (i = ETH_HLEN; i < pkt_size; i++)
		packet[i] = (unsigned char)(i & 0xff);

	rc = qede_selftest_transmit_traffic(edev, skb);
	if (rc)
		goto test_loopback_exit;

	rc = qede_selftest_receive_traffic(edev);
	if (rc)
		goto test_loopback_exit;

	DP_VERBOSE(edev, NETIF_MSG_RX_STATUS, "Loopback test successful\n");

test_loopback_exit:
	dev_kfree_skb(skb);

	/* Bring up the link in Normal mode */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	link_params.override_flags = QED_LINK_OVERRIDE_LOOPBACK_MODE;
	link_params.loopback_mode = QED_LINK_LOOPBACK_NONE;
	edev->ops->common->set_link(edev->cdev, &link_params);

	/* Wait for loopback configuration to apply */
	msleep_interruptible(500);

	qede_netif_start(edev);

	return rc;
}

static void qede_self_test(struct net_device *dev,
			   struct ethtool_test *etest, u64 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Self-test command parameters: offline = %d, external_lb = %d\n",
		   (etest->flags & ETH_TEST_FL_OFFLINE),
		   (etest->flags & ETH_TEST_FL_EXTERNAL_LB) >> 2);

	memset(buf, 0, sizeof(u64) * QEDE_ETHTOOL_TEST_MAX);

	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		if (qede_selftest_run_loopback(edev,
					       QED_LINK_LOOPBACK_INT_PHY)) {
			buf[QEDE_ETHTOOL_INT_LOOPBACK] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
	}

	if (edev->ops->common->selftest->selftest_interrupt(edev->cdev)) {
		buf[QEDE_ETHTOOL_INTERRUPT_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_memory(edev->cdev)) {
		buf[QEDE_ETHTOOL_MEMORY_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_register(edev->cdev)) {
		buf[QEDE_ETHTOOL_REGISTER_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_clock(edev->cdev)) {
		buf[QEDE_ETHTOOL_CLOCK_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_nvram(edev->cdev)) {
		buf[QEDE_ETHTOOL_NVRAM_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
}

static int qede_set_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna,
			    const void *data)
{
	struct qede_dev *edev = netdev_priv(dev);
	u32 val;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		val = *(u32 *)data;
		if (val < QEDE_MIN_PKT_LEN || val > QEDE_RX_HDR_SIZE) {
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "Invalid rx copy break value, range is [%u, %u]",
				   QEDE_MIN_PKT_LEN, QEDE_RX_HDR_SIZE);
			return -EINVAL;
		}

		edev->rx_copybreak = *(u32 *)data;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qede_get_tunable(struct net_device *dev,
			    const struct ethtool_tunable *tuna, void *data)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = edev->rx_copybreak;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qede_get_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	if (!current_link.eee_supported) {
		DP_INFO(edev, "EEE is not supported\n");
		return -EOPNOTSUPP;
	}

	if (current_link.eee.adv_caps & QED_EEE_1G_ADV)
		edata->advertised = ADVERTISED_1000baseT_Full;
	if (current_link.eee.adv_caps & QED_EEE_10G_ADV)
		edata->advertised |= ADVERTISED_10000baseT_Full;
	if (current_link.sup_caps & QED_EEE_1G_ADV)
		edata->supported = ADVERTISED_1000baseT_Full;
	if (current_link.sup_caps & QED_EEE_10G_ADV)
		edata->supported |= ADVERTISED_10000baseT_Full;
	if (current_link.eee.lp_adv_caps & QED_EEE_1G_ADV)
		edata->lp_advertised = ADVERTISED_1000baseT_Full;
	if (current_link.eee.lp_adv_caps & QED_EEE_10G_ADV)
		edata->lp_advertised |= ADVERTISED_10000baseT_Full;

	edata->tx_lpi_timer = current_link.eee.tx_lpi_timer;
	edata->eee_enabled = current_link.eee.enable;
	edata->tx_lpi_enabled = current_link.eee.tx_lpi_enable;
	edata->eee_active = current_link.eee_active;

	return 0;
}

static int qede_set_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;
	struct qed_link_params params;

	if (!edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev, "Link settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	if (!current_link.eee_supported) {
		DP_INFO(edev, "EEE is not supported\n");
		return -EOPNOTSUPP;
	}

	memset(&params, 0, sizeof(params));
	params.override_flags |= QED_LINK_OVERRIDE_EEE_CONFIG;

	if (!(edata->advertised & (ADVERTISED_1000baseT_Full |
				   ADVERTISED_10000baseT_Full)) ||
	    ((edata->advertised & (ADVERTISED_1000baseT_Full |
				   ADVERTISED_10000baseT_Full)) !=
	     edata->advertised)) {
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Invalid advertised capabilities %d\n",
			   edata->advertised);
		return -EINVAL;
	}

	if (edata->advertised & ADVERTISED_1000baseT_Full)
		params.eee.adv_caps = QED_EEE_1G_ADV;
	if (edata->advertised & ADVERTISED_10000baseT_Full)
		params.eee.adv_caps |= QED_EEE_10G_ADV;
	params.eee.enable = edata->eee_enabled;
	params.eee.tx_lpi_enable = edata->tx_lpi_enabled;
	params.eee.tx_lpi_timer = edata->tx_lpi_timer;

	params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
}

static u32 qede_link_to_ethtool_fec(u32 link_fec)
{
	u32 eth_fec = 0;

	if (link_fec & QED_FEC_MODE_NONE)
		eth_fec |= ETHTOOL_FEC_OFF;
	if (link_fec & QED_FEC_MODE_FIRECODE)
		eth_fec |= ETHTOOL_FEC_BASER;
	if (link_fec & QED_FEC_MODE_RS)
		eth_fec |= ETHTOOL_FEC_RS;
	if (link_fec & QED_FEC_MODE_AUTO)
		eth_fec |= ETHTOOL_FEC_AUTO;
	if (link_fec & QED_FEC_MODE_UNSUPPORTED)
		eth_fec |= ETHTOOL_FEC_NONE;

	return eth_fec;
}

static u32 qede_ethtool_to_link_fec(u32 eth_fec)
{
	u32 link_fec = 0;

	if (eth_fec & ETHTOOL_FEC_OFF)
		link_fec |= QED_FEC_MODE_NONE;
	if (eth_fec & ETHTOOL_FEC_BASER)
		link_fec |= QED_FEC_MODE_FIRECODE;
	if (eth_fec & ETHTOOL_FEC_RS)
		link_fec |= QED_FEC_MODE_RS;
	if (eth_fec & ETHTOOL_FEC_AUTO)
		link_fec |= QED_FEC_MODE_AUTO;
	if (eth_fec & ETHTOOL_FEC_NONE)
		link_fec |= QED_FEC_MODE_UNSUPPORTED;

	return link_fec;
}

static int qede_get_fecparam(struct net_device *dev,
			     struct ethtool_fecparam *fecparam)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output curr_link;

	memset(&curr_link, 0, sizeof(curr_link));
	edev->ops->common->get_link(edev->cdev, &curr_link);

	fecparam->active_fec = qede_link_to_ethtool_fec(curr_link.active_fec);
	fecparam->fec = qede_link_to_ethtool_fec(curr_link.sup_fec);

	return 0;
}

static int qede_set_fecparam(struct net_device *dev,
			     struct ethtool_fecparam *fecparam)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_params params;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev, "Link settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	memset(&params, 0, sizeof(params));
	params.override_flags |= QED_LINK_OVERRIDE_FEC_CONFIG;
	params.fec = qede_ethtool_to_link_fec(fecparam->fec);
	params.link_up = true;

	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
}

static int qede_get_module_info(struct net_device *dev,
				struct ethtool_modinfo *modinfo)
{
	struct qede_dev *edev = netdev_priv(dev);
	u8 buf[4];
	int rc;

	/* Read first 4 bytes to find the sfp type */
	rc = edev->ops->common->read_module_eeprom(edev->cdev, buf,
						   QED_I2C_DEV_ADDR_A0, 0, 4);
	if (rc) {
		DP_ERR(edev, "Failed reading EEPROM data %d\n", rc);
		return rc;
	}

	switch (buf[0]) {
	case 0x3: /* SFP, SFP+, SFP-28 */
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case 0xc: /* QSFP */
	case 0xd: /* QSFP+ */
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case 0x11: /* QSFP-28 */
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	default:
		DP_ERR(edev, "Unknown transceiver type 0x%x\n", buf[0]);
		return -EINVAL;
	}

	return 0;
}

static int qede_get_module_eeprom(struct net_device *dev,
				  struct ethtool_eeprom *ee, u8 *data)
{
	struct qede_dev *edev = netdev_priv(dev);
	u32 start_addr = ee->offset, size = 0;
	u8 *buf = data;
	int rc = 0;

	/* Read A0 section */
	if (ee->offset < ETH_MODULE_SFF_8079_LEN) {
		/* Limit transfer size to the A0 section boundary */
		if (ee->offset + ee->len > ETH_MODULE_SFF_8079_LEN)
			size = ETH_MODULE_SFF_8079_LEN - ee->offset;
		else
			size = ee->len;

		rc = edev->ops->common->read_module_eeprom(edev->cdev, buf,
							   QED_I2C_DEV_ADDR_A0,
							   start_addr, size);
		if (rc) {
			DP_ERR(edev, "Failed reading A0 section  %d\n", rc);
			return rc;
		}

		buf += size;
		start_addr += size;
	}

	/* Read A2 section */
	if (start_addr >= ETH_MODULE_SFF_8079_LEN &&
	    start_addr < ETH_MODULE_SFF_8472_LEN) {
		size = ee->len - size;
		/* Limit transfer size to the A2 section boundary */
		if (start_addr + size > ETH_MODULE_SFF_8472_LEN)
			size = ETH_MODULE_SFF_8472_LEN - start_addr;
		start_addr -= ETH_MODULE_SFF_8079_LEN;
		rc = edev->ops->common->read_module_eeprom(edev->cdev, buf,
							   QED_I2C_DEV_ADDR_A2,
							   start_addr, size);
		if (rc) {
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "Failed reading A2 section %d\n", rc);
			return 0;
		}
	}

	return rc;
}

static int qede_set_dump(struct net_device *dev, struct ethtool_dump *val)
{
	struct qede_dev *edev = netdev_priv(dev);
	int rc = 0;

	if (edev->dump_info.cmd == QEDE_DUMP_CMD_NONE) {
		if (val->flag > QEDE_DUMP_CMD_MAX) {
			DP_ERR(edev, "Invalid command %d\n", val->flag);
			return -EINVAL;
		}
		edev->dump_info.cmd = val->flag;
		edev->dump_info.num_args = 0;
		return 0;
	}

	if (edev->dump_info.num_args == QEDE_DUMP_MAX_ARGS) {
		DP_ERR(edev, "Arg count = %d\n", edev->dump_info.num_args);
		return -EINVAL;
	}

	switch (edev->dump_info.cmd) {
	case QEDE_DUMP_CMD_NVM_CFG:
		edev->dump_info.args[edev->dump_info.num_args] = val->flag;
		edev->dump_info.num_args++;
		break;
	case QEDE_DUMP_CMD_GRCDUMP:
		rc = edev->ops->common->set_grc_config(edev->cdev,
						       val->flag, 1);
		break;
	default:
		break;
	}

	return rc;
}

static int qede_get_dump_flag(struct net_device *dev,
			      struct ethtool_dump *dump)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!edev->ops || !edev->ops->common) {
		DP_ERR(edev, "Edev ops not populated\n");
		return -EINVAL;
	}

	dump->version = QEDE_DUMP_VERSION;
	switch (edev->dump_info.cmd) {
	case QEDE_DUMP_CMD_NVM_CFG:
		dump->flag = QEDE_DUMP_CMD_NVM_CFG;
		dump->len = edev->ops->common->read_nvm_cfg_len(edev->cdev,
						edev->dump_info.args[0]);
		break;
	case QEDE_DUMP_CMD_GRCDUMP:
		dump->flag = QEDE_DUMP_CMD_GRCDUMP;
		dump->len = edev->ops->common->dbg_all_data_size(edev->cdev);
		break;
	default:
		DP_ERR(edev, "Invalid cmd = %d\n", edev->dump_info.cmd);
		return -EINVAL;
	}

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "dump->version = 0x%x dump->flag = %d dump->len = %d\n",
		   dump->version, dump->flag, dump->len);
	return 0;
}

static int qede_get_dump_data(struct net_device *dev,
			      struct ethtool_dump *dump, void *buf)
{
	struct qede_dev *edev = netdev_priv(dev);
	int rc = 0;

	if (!edev->ops || !edev->ops->common) {
		DP_ERR(edev, "Edev ops not populated\n");
		rc = -EINVAL;
		goto err;
	}

	switch (edev->dump_info.cmd) {
	case QEDE_DUMP_CMD_NVM_CFG:
		if (edev->dump_info.num_args != QEDE_DUMP_NVM_ARG_COUNT) {
			DP_ERR(edev, "Arg count = %d required = %d\n",
			       edev->dump_info.num_args,
			       QEDE_DUMP_NVM_ARG_COUNT);
			rc = -EINVAL;
			goto err;
		}
		rc =  edev->ops->common->read_nvm_cfg(edev->cdev, (u8 **)&buf,
						      edev->dump_info.args[0],
						      edev->dump_info.args[1]);
		break;
	case QEDE_DUMP_CMD_GRCDUMP:
		memset(buf, 0, dump->len);
		rc = edev->ops->common->dbg_all_data(edev->cdev, buf);
		break;
	default:
		DP_ERR(edev, "Invalid cmd = %d\n", edev->dump_info.cmd);
		rc = -EINVAL;
		break;
	}

err:
	edev->dump_info.cmd = QEDE_DUMP_CMD_NONE;
	edev->dump_info.num_args = 0;
	memset(edev->dump_info.args, 0, sizeof(edev->dump_info.args));

	return rc;
}

int qede_set_per_coalesce(struct net_device *dev, u32 queue,
			  struct ethtool_coalesce *coal)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_fastpath *fp;
	u16 rxc, txc;
	int rc = 0;

	if (coal->rx_coalesce_usecs > QED_COALESCE_MAX ||
	    coal->tx_coalesce_usecs > QED_COALESCE_MAX) {
		DP_INFO(edev,
			"Can't support requested %s coalesce value [max supported value %d]\n",
			coal->rx_coalesce_usecs > QED_COALESCE_MAX ? "rx"
								   : "tx",
			QED_COALESCE_MAX);
		return -EINVAL;
	}

	rxc = (u16)coal->rx_coalesce_usecs;
	txc = (u16)coal->tx_coalesce_usecs;

	__qede_lock(edev);
	if (queue >= edev->num_queues) {
		DP_INFO(edev, "Invalid queue\n");
		rc = -EINVAL;
		goto out;
	}

	if (edev->state != QEDE_STATE_OPEN) {
		rc = -EINVAL;
		goto out;
	}

	fp = &edev->fp_array[queue];

	if (edev->fp_array[queue].type & QEDE_FASTPATH_RX) {
		rc = edev->ops->common->set_coalesce(edev->cdev,
						     rxc, 0,
						     fp->rxq->handle);
		if (rc) {
			DP_INFO(edev,
				"Set RX coalesce error, rc = %d\n", rc);
			goto out;
		}
		edev->coal_entry[queue].rxc = rxc;
		edev->coal_entry[queue].isvalid = true;
	}

	if (edev->fp_array[queue].type & QEDE_FASTPATH_TX) {
		rc = edev->ops->common->set_coalesce(edev->cdev,
						     0, txc,
						     fp->txq->handle);
		if (rc) {
			DP_INFO(edev,
				"Set TX coalesce error, rc = %d\n", rc);
			goto out;
		}
		edev->coal_entry[queue].txc = txc;
		edev->coal_entry[queue].isvalid = true;
	}
out:
	__qede_unlock(edev);

	return rc;
}

static int qede_get_per_coalesce(struct net_device *dev,
				 u32 queue,
				 struct ethtool_coalesce *coal)
{
	void *rx_handle = NULL, *tx_handle = NULL;
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_fastpath *fp;
	u16 rx_coal, tx_coal;
	int rc = 0;

	rx_coal = QED_DEFAULT_RX_USECS;
	tx_coal = QED_DEFAULT_TX_USECS;

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	__qede_lock(edev);
	if (queue >= edev->num_queues) {
		DP_INFO(edev, "Invalid queue\n");
		rc = -EINVAL;
		goto out;
	}

	if (edev->state != QEDE_STATE_OPEN) {
		rc = -EINVAL;
		goto out;
	}

	fp = &edev->fp_array[queue];

	if (fp->type & QEDE_FASTPATH_RX)
		rx_handle = fp->rxq->handle;

	rc = edev->ops->get_coalesce(edev->cdev, &rx_coal,
				     rx_handle);
	if (rc) {
		DP_INFO(edev, "Read Rx coalesce error\n");
		goto out;
	}

	fp = &edev->fp_array[queue];
	if (fp->type & QEDE_FASTPATH_TX)
		tx_handle = fp->txq->handle;

	rc = edev->ops->get_coalesce(edev->cdev, &tx_coal,
				      tx_handle);
	if (rc)
		DP_INFO(edev, "Read Tx coalesce error\n");

out:
	__qede_unlock(edev);

	coal->rx_coalesce_usecs = rx_coal;
	coal->tx_coalesce_usecs = tx_coal;

	return rc;
}

static const struct ethtool_ops qede_ethtool_ops = {
	.supported_coalesce_params	= ETHTOOL_COALESCE_USECS,
	.get_link_ksettings		= qede_get_link_ksettings,
	.set_link_ksettings		= qede_set_link_ksettings,
	.get_drvinfo			= qede_get_drvinfo,
	.get_regs_len			= qede_get_regs_len,
	.get_regs			= qede_get_regs,
	.get_wol			= qede_get_wol,
	.set_wol			= qede_set_wol,
	.get_msglevel			= qede_get_msglevel,
	.set_msglevel			= qede_set_msglevel,
	.nway_reset			= qede_nway_reset,
	.get_link			= qede_get_link,
	.get_coalesce			= qede_get_coalesce,
	.set_coalesce			= qede_set_coalesce,
	.get_ringparam			= qede_get_ringparam,
	.set_ringparam			= qede_set_ringparam,
	.get_pauseparam			= qede_get_pauseparam,
	.set_pauseparam			= qede_set_pauseparam,
	.get_strings			= qede_get_strings,
	.set_phys_id			= qede_set_phys_id,
	.get_ethtool_stats		= qede_get_ethtool_stats,
	.get_priv_flags			= qede_get_priv_flags,
	.set_priv_flags			= qede_set_priv_flags,
	.get_sset_count			= qede_get_sset_count,
	.get_rxnfc			= qede_get_rxnfc,
	.set_rxnfc			= qede_set_rxnfc,
	.get_rxfh_indir_size		= qede_get_rxfh_indir_size,
	.get_rxfh_key_size		= qede_get_rxfh_key_size,
	.get_rxfh			= qede_get_rxfh,
	.set_rxfh			= qede_set_rxfh,
	.get_ts_info			= qede_get_ts_info,
	.get_channels			= qede_get_channels,
	.set_channels			= qede_set_channels,
	.self_test			= qede_self_test,
	.get_module_info		= qede_get_module_info,
	.get_module_eeprom		= qede_get_module_eeprom,
	.get_eee			= qede_get_eee,
	.set_eee			= qede_set_eee,
	.get_fecparam			= qede_get_fecparam,
	.set_fecparam			= qede_set_fecparam,
	.get_tunable			= qede_get_tunable,
	.set_tunable			= qede_set_tunable,
	.get_per_queue_coalesce		= qede_get_per_coalesce,
	.set_per_queue_coalesce		= qede_set_per_coalesce,
	.flash_device			= qede_flash_device,
	.get_dump_flag			= qede_get_dump_flag,
	.get_dump_data			= qede_get_dump_data,
	.set_dump			= qede_set_dump,
};

static const struct ethtool_ops qede_vf_ethtool_ops = {
	.supported_coalesce_params	= ETHTOOL_COALESCE_USECS,
	.get_link_ksettings		= qede_get_link_ksettings,
	.get_drvinfo			= qede_get_drvinfo,
	.get_msglevel			= qede_get_msglevel,
	.set_msglevel			= qede_set_msglevel,
	.get_link			= qede_get_link,
	.get_coalesce			= qede_get_coalesce,
	.set_coalesce			= qede_set_coalesce,
	.get_ringparam			= qede_get_ringparam,
	.set_ringparam			= qede_set_ringparam,
	.get_strings			= qede_get_strings,
	.get_ethtool_stats		= qede_get_ethtool_stats,
	.get_priv_flags			= qede_get_priv_flags,
	.get_sset_count			= qede_get_sset_count,
	.get_rxnfc			= qede_get_rxnfc,
	.set_rxnfc			= qede_set_rxnfc,
	.get_rxfh_indir_size		= qede_get_rxfh_indir_size,
	.get_rxfh_key_size		= qede_get_rxfh_key_size,
	.get_rxfh			= qede_get_rxfh,
	.set_rxfh			= qede_set_rxfh,
	.get_channels			= qede_get_channels,
	.set_channels			= qede_set_channels,
	.get_per_queue_coalesce		= qede_get_per_coalesce,
	.set_per_queue_coalesce		= qede_set_per_coalesce,
	.get_tunable			= qede_get_tunable,
	.set_tunable			= qede_set_tunable,
};

void qede_set_ethtool_ops(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (IS_VF(edev))
		dev->ethtool_ops = &qede_vf_ethtool_ops;
	else
		dev->ethtool_ops = &qede_ethtool_ops;
}

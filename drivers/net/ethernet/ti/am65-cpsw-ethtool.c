// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments K3 AM65 Ethernet Switch SubSystem Driver ethtool ops
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#include <linux/net_tstamp.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "am65-cpsw-nuss.h"
#include "am65-cpsw-qos.h"
#include "cpsw_ale.h"
#include "am65-cpts.h"

#define AM65_CPSW_REGDUMP_VER 0x1

enum {
	AM65_CPSW_REGDUMP_MOD_NUSS = 1,
	AM65_CPSW_REGDUMP_MOD_RGMII_STATUS = 2,
	AM65_CPSW_REGDUMP_MOD_MDIO = 3,
	AM65_CPSW_REGDUMP_MOD_CPSW = 4,
	AM65_CPSW_REGDUMP_MOD_CPSW_P0 = 5,
	AM65_CPSW_REGDUMP_MOD_CPSW_P1 = 6,
	AM65_CPSW_REGDUMP_MOD_CPSW_CPTS = 7,
	AM65_CPSW_REGDUMP_MOD_CPSW_ALE = 8,
	AM65_CPSW_REGDUMP_MOD_CPSW_ALE_TBL = 9,
	AM65_CPSW_REGDUMP_MOD_LAST,
};

/**
 * struct am65_cpsw_regdump_hdr - regdump record header
 *
 * @module_id: CPSW module ID
 * @len: CPSW module registers space length in u32
 */

struct am65_cpsw_regdump_hdr {
	u32 module_id;
	u32 len;
};

/**
 * struct am65_cpsw_regdump_item - regdump module description
 *
 * @hdr: CPSW module header
 * @start_ofs: CPSW module registers start addr
 * @end_ofs: CPSW module registers end addr
 *
 * Registers dump provided in the format:
 *  u32 : module ID
 *  u32 : dump length
 *  u32[..len]: registers values
 */
struct am65_cpsw_regdump_item {
	struct am65_cpsw_regdump_hdr hdr;
	u32 start_ofs;
	u32 end_ofs;
};

#define AM65_CPSW_REGDUMP_REC(mod, start, end) { \
	.hdr.module_id = (mod), \
	.hdr.len = (end + 4 - start) * 2 + \
		   sizeof(struct am65_cpsw_regdump_hdr), \
	.start_ofs = (start), \
	.end_ofs = end, \
}

static const struct am65_cpsw_regdump_item am65_cpsw_regdump[] = {
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_NUSS, 0x0, 0x1c),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_RGMII_STATUS, 0x30, 0x4c),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_MDIO, 0xf00, 0xffc),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW, 0x20000, 0x2011c),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW_P0, 0x21000, 0x21320),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW_P1, 0x22000, 0x223a4),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW_CPTS,
			      0x3d000, 0x3d048),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW_ALE, 0x3e000, 0x3e13c),
	AM65_CPSW_REGDUMP_REC(AM65_CPSW_REGDUMP_MOD_CPSW_ALE_TBL, 0, 0),
};

struct am65_cpsw_stats_regs {
	u32	rx_good_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	rx_pause_frames;		/* slave */
	u32	rx_crc_errors;
	u32	rx_align_code_errors;		/* slave */
	u32	rx_oversized_frames;
	u32	rx_jabber_frames;		/* slave */
	u32	rx_undersized_frames;
	u32	rx_fragments;			/* slave */
	u32	ale_drop;
	u32	ale_overrun_drop;
	u32	rx_octets;
	u32	tx_good_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	tx_pause_frames;		/* slave */
	u32	tx_deferred_frames;		/* slave */
	u32	tx_collision_frames;		/* slave */
	u32	tx_single_coll_frames;		/* slave */
	u32	tx_mult_coll_frames;		/* slave */
	u32	tx_excessive_collisions;	/* slave */
	u32	tx_late_collisions;		/* slave */
	u32	rx_ipg_error;			/* slave 10G only */
	u32	tx_carrier_sense_errors;	/* slave */
	u32	tx_octets;
	u32	tx_64B_frames;
	u32	tx_65_to_127B_frames;
	u32	tx_128_to_255B_frames;
	u32	tx_256_to_511B_frames;
	u32	tx_512_to_1023B_frames;
	u32	tx_1024B_frames;
	u32	net_octets;
	u32	rx_bottom_fifo_drop;
	u32	rx_port_mask_drop;
	u32	rx_top_fifo_drop;
	u32	ale_rate_limit_drop;
	u32	ale_vid_ingress_drop;
	u32	ale_da_eq_sa_drop;
	u32	ale_block_drop;			/* K3 */
	u32	ale_secure_drop;		/* K3 */
	u32	ale_auth_drop;			/* K3 */
	u32	ale_unknown_ucast;
	u32	ale_unknown_ucast_bytes;
	u32	ale_unknown_mcast;
	u32	ale_unknown_mcast_bytes;
	u32	ale_unknown_bcast;
	u32	ale_unknown_bcast_bytes;
	u32	ale_pol_match;
	u32	ale_pol_match_red;
	u32	ale_pol_match_yellow;
	u32	ale_mcast_sa_drop;		/* K3 */
	u32	ale_dual_vlan_drop;		/* K3 */
	u32	ale_len_err_drop;		/* K3 */
	u32	ale_ip_next_hdr_drop;		/* K3 */
	u32	ale_ipv4_frag_drop;		/* K3 */
	u32	__rsvd_1[24];
	u32	iet_rx_assembly_err;		/* K3 slave */
	u32	iet_rx_assembly_ok;		/* K3 slave */
	u32	iet_rx_smd_err;			/* K3 slave */
	u32	iet_rx_frag;			/* K3 slave */
	u32	iet_tx_hold;			/* K3 slave */
	u32	iet_tx_frag;			/* K3 slave */
	u32	__rsvd_2[9];
	u32	tx_mem_protect_err;
	/* following NU only */
	u32	tx_pri0;
	u32	tx_pri1;
	u32	tx_pri2;
	u32	tx_pri3;
	u32	tx_pri4;
	u32	tx_pri5;
	u32	tx_pri6;
	u32	tx_pri7;
	u32	tx_pri0_bcnt;
	u32	tx_pri1_bcnt;
	u32	tx_pri2_bcnt;
	u32	tx_pri3_bcnt;
	u32	tx_pri4_bcnt;
	u32	tx_pri5_bcnt;
	u32	tx_pri6_bcnt;
	u32	tx_pri7_bcnt;
	u32	tx_pri0_drop;
	u32	tx_pri1_drop;
	u32	tx_pri2_drop;
	u32	tx_pri3_drop;
	u32	tx_pri4_drop;
	u32	tx_pri5_drop;
	u32	tx_pri6_drop;
	u32	tx_pri7_drop;
	u32	tx_pri0_drop_bcnt;
	u32	tx_pri1_drop_bcnt;
	u32	tx_pri2_drop_bcnt;
	u32	tx_pri3_drop_bcnt;
	u32	tx_pri4_drop_bcnt;
	u32	tx_pri5_drop_bcnt;
	u32	tx_pri6_drop_bcnt;
	u32	tx_pri7_drop_bcnt;
};

struct am65_cpsw_ethtool_stat {
	char desc[ETH_GSTRING_LEN];
	int offset;
};

#define AM65_CPSW_STATS(prefix, field)			\
{							\
	#prefix#field,					\
	offsetof(struct am65_cpsw_stats_regs, field)	\
}

static const struct am65_cpsw_ethtool_stat am65_host_stats[] = {
	AM65_CPSW_STATS(p0_, rx_good_frames),
	AM65_CPSW_STATS(p0_, rx_broadcast_frames),
	AM65_CPSW_STATS(p0_, rx_multicast_frames),
	AM65_CPSW_STATS(p0_, rx_crc_errors),
	AM65_CPSW_STATS(p0_, rx_oversized_frames),
	AM65_CPSW_STATS(p0_, rx_undersized_frames),
	AM65_CPSW_STATS(p0_, ale_drop),
	AM65_CPSW_STATS(p0_, ale_overrun_drop),
	AM65_CPSW_STATS(p0_, rx_octets),
	AM65_CPSW_STATS(p0_, tx_good_frames),
	AM65_CPSW_STATS(p0_, tx_broadcast_frames),
	AM65_CPSW_STATS(p0_, tx_multicast_frames),
	AM65_CPSW_STATS(p0_, tx_octets),
	AM65_CPSW_STATS(p0_, tx_64B_frames),
	AM65_CPSW_STATS(p0_, tx_65_to_127B_frames),
	AM65_CPSW_STATS(p0_, tx_128_to_255B_frames),
	AM65_CPSW_STATS(p0_, tx_256_to_511B_frames),
	AM65_CPSW_STATS(p0_, tx_512_to_1023B_frames),
	AM65_CPSW_STATS(p0_, tx_1024B_frames),
	AM65_CPSW_STATS(p0_, net_octets),
	AM65_CPSW_STATS(p0_, rx_bottom_fifo_drop),
	AM65_CPSW_STATS(p0_, rx_port_mask_drop),
	AM65_CPSW_STATS(p0_, rx_top_fifo_drop),
	AM65_CPSW_STATS(p0_, ale_rate_limit_drop),
	AM65_CPSW_STATS(p0_, ale_vid_ingress_drop),
	AM65_CPSW_STATS(p0_, ale_da_eq_sa_drop),
	AM65_CPSW_STATS(p0_, ale_block_drop),
	AM65_CPSW_STATS(p0_, ale_secure_drop),
	AM65_CPSW_STATS(p0_, ale_auth_drop),
	AM65_CPSW_STATS(p0_, ale_unknown_ucast),
	AM65_CPSW_STATS(p0_, ale_unknown_ucast_bytes),
	AM65_CPSW_STATS(p0_, ale_unknown_mcast),
	AM65_CPSW_STATS(p0_, ale_unknown_mcast_bytes),
	AM65_CPSW_STATS(p0_, ale_unknown_bcast),
	AM65_CPSW_STATS(p0_, ale_unknown_bcast_bytes),
	AM65_CPSW_STATS(p0_, ale_pol_match),
	AM65_CPSW_STATS(p0_, ale_pol_match_red),
	AM65_CPSW_STATS(p0_, ale_pol_match_yellow),
	AM65_CPSW_STATS(p0_, ale_mcast_sa_drop),
	AM65_CPSW_STATS(p0_, ale_dual_vlan_drop),
	AM65_CPSW_STATS(p0_, ale_len_err_drop),
	AM65_CPSW_STATS(p0_, ale_ip_next_hdr_drop),
	AM65_CPSW_STATS(p0_, ale_ipv4_frag_drop),
	AM65_CPSW_STATS(p0_, tx_mem_protect_err),
	AM65_CPSW_STATS(p0_, tx_pri0),
	AM65_CPSW_STATS(p0_, tx_pri1),
	AM65_CPSW_STATS(p0_, tx_pri2),
	AM65_CPSW_STATS(p0_, tx_pri3),
	AM65_CPSW_STATS(p0_, tx_pri4),
	AM65_CPSW_STATS(p0_, tx_pri5),
	AM65_CPSW_STATS(p0_, tx_pri6),
	AM65_CPSW_STATS(p0_, tx_pri7),
	AM65_CPSW_STATS(p0_, tx_pri0_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri1_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri2_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri3_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri4_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri5_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri6_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri7_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri0_drop),
	AM65_CPSW_STATS(p0_, tx_pri1_drop),
	AM65_CPSW_STATS(p0_, tx_pri2_drop),
	AM65_CPSW_STATS(p0_, tx_pri3_drop),
	AM65_CPSW_STATS(p0_, tx_pri4_drop),
	AM65_CPSW_STATS(p0_, tx_pri5_drop),
	AM65_CPSW_STATS(p0_, tx_pri6_drop),
	AM65_CPSW_STATS(p0_, tx_pri7_drop),
	AM65_CPSW_STATS(p0_, tx_pri0_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri1_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri2_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri3_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri4_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri5_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri6_drop_bcnt),
	AM65_CPSW_STATS(p0_, tx_pri7_drop_bcnt),
};

static const struct am65_cpsw_ethtool_stat am65_slave_stats[] = {
	AM65_CPSW_STATS(, rx_good_frames),
	AM65_CPSW_STATS(, rx_broadcast_frames),
	AM65_CPSW_STATS(, rx_multicast_frames),
	AM65_CPSW_STATS(, rx_pause_frames),
	AM65_CPSW_STATS(, rx_crc_errors),
	AM65_CPSW_STATS(, rx_align_code_errors),
	AM65_CPSW_STATS(, rx_oversized_frames),
	AM65_CPSW_STATS(, rx_jabber_frames),
	AM65_CPSW_STATS(, rx_undersized_frames),
	AM65_CPSW_STATS(, rx_fragments),
	AM65_CPSW_STATS(, ale_drop),
	AM65_CPSW_STATS(, ale_overrun_drop),
	AM65_CPSW_STATS(, rx_octets),
	AM65_CPSW_STATS(, tx_good_frames),
	AM65_CPSW_STATS(, tx_broadcast_frames),
	AM65_CPSW_STATS(, tx_multicast_frames),
	AM65_CPSW_STATS(, tx_pause_frames),
	AM65_CPSW_STATS(, tx_deferred_frames),
	AM65_CPSW_STATS(, tx_collision_frames),
	AM65_CPSW_STATS(, tx_single_coll_frames),
	AM65_CPSW_STATS(, tx_mult_coll_frames),
	AM65_CPSW_STATS(, tx_excessive_collisions),
	AM65_CPSW_STATS(, tx_late_collisions),
	AM65_CPSW_STATS(, rx_ipg_error),
	AM65_CPSW_STATS(, tx_carrier_sense_errors),
	AM65_CPSW_STATS(, tx_octets),
	AM65_CPSW_STATS(, tx_64B_frames),
	AM65_CPSW_STATS(, tx_65_to_127B_frames),
	AM65_CPSW_STATS(, tx_128_to_255B_frames),
	AM65_CPSW_STATS(, tx_256_to_511B_frames),
	AM65_CPSW_STATS(, tx_512_to_1023B_frames),
	AM65_CPSW_STATS(, tx_1024B_frames),
	AM65_CPSW_STATS(, net_octets),
	AM65_CPSW_STATS(, rx_bottom_fifo_drop),
	AM65_CPSW_STATS(, rx_port_mask_drop),
	AM65_CPSW_STATS(, rx_top_fifo_drop),
	AM65_CPSW_STATS(, ale_rate_limit_drop),
	AM65_CPSW_STATS(, ale_vid_ingress_drop),
	AM65_CPSW_STATS(, ale_da_eq_sa_drop),
	AM65_CPSW_STATS(, ale_block_drop),
	AM65_CPSW_STATS(, ale_secure_drop),
	AM65_CPSW_STATS(, ale_auth_drop),
	AM65_CPSW_STATS(, ale_unknown_ucast),
	AM65_CPSW_STATS(, ale_unknown_ucast_bytes),
	AM65_CPSW_STATS(, ale_unknown_mcast),
	AM65_CPSW_STATS(, ale_unknown_mcast_bytes),
	AM65_CPSW_STATS(, ale_unknown_bcast),
	AM65_CPSW_STATS(, ale_unknown_bcast_bytes),
	AM65_CPSW_STATS(, ale_pol_match),
	AM65_CPSW_STATS(, ale_pol_match_red),
	AM65_CPSW_STATS(, ale_pol_match_yellow),
	AM65_CPSW_STATS(, ale_mcast_sa_drop),
	AM65_CPSW_STATS(, ale_dual_vlan_drop),
	AM65_CPSW_STATS(, ale_len_err_drop),
	AM65_CPSW_STATS(, ale_ip_next_hdr_drop),
	AM65_CPSW_STATS(, ale_ipv4_frag_drop),
	AM65_CPSW_STATS(, iet_rx_assembly_err),
	AM65_CPSW_STATS(, iet_rx_assembly_ok),
	AM65_CPSW_STATS(, iet_rx_smd_err),
	AM65_CPSW_STATS(, iet_rx_frag),
	AM65_CPSW_STATS(, iet_tx_hold),
	AM65_CPSW_STATS(, iet_tx_frag),
	AM65_CPSW_STATS(, tx_mem_protect_err),
	AM65_CPSW_STATS(, tx_pri0),
	AM65_CPSW_STATS(, tx_pri1),
	AM65_CPSW_STATS(, tx_pri2),
	AM65_CPSW_STATS(, tx_pri3),
	AM65_CPSW_STATS(, tx_pri4),
	AM65_CPSW_STATS(, tx_pri5),
	AM65_CPSW_STATS(, tx_pri6),
	AM65_CPSW_STATS(, tx_pri7),
	AM65_CPSW_STATS(, tx_pri0_bcnt),
	AM65_CPSW_STATS(, tx_pri1_bcnt),
	AM65_CPSW_STATS(, tx_pri2_bcnt),
	AM65_CPSW_STATS(, tx_pri3_bcnt),
	AM65_CPSW_STATS(, tx_pri4_bcnt),
	AM65_CPSW_STATS(, tx_pri5_bcnt),
	AM65_CPSW_STATS(, tx_pri6_bcnt),
	AM65_CPSW_STATS(, tx_pri7_bcnt),
	AM65_CPSW_STATS(, tx_pri0_drop),
	AM65_CPSW_STATS(, tx_pri1_drop),
	AM65_CPSW_STATS(, tx_pri2_drop),
	AM65_CPSW_STATS(, tx_pri3_drop),
	AM65_CPSW_STATS(, tx_pri4_drop),
	AM65_CPSW_STATS(, tx_pri5_drop),
	AM65_CPSW_STATS(, tx_pri6_drop),
	AM65_CPSW_STATS(, tx_pri7_drop),
	AM65_CPSW_STATS(, tx_pri0_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri1_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri2_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri3_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri4_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri5_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri6_drop_bcnt),
	AM65_CPSW_STATS(, tx_pri7_drop_bcnt),
};

/* Ethtool priv_flags */
static const char am65_cpsw_ethtool_priv_flags[][ETH_GSTRING_LEN] = {
#define	AM65_CPSW_PRIV_P0_RX_PTYPE_RROBIN	BIT(0)
	"p0-rx-ptype-rrobin",
};

static int am65_cpsw_ethtool_op_begin(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	int ret;

	ret = pm_runtime_resume_and_get(common->dev);
	if (ret < 0)
		dev_err(common->dev, "ethtool begin failed %d\n", ret);

	return ret;
}

static void am65_cpsw_ethtool_op_complete(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	int ret;

	ret = pm_runtime_put(common->dev);
	if (ret < 0 && ret != -EBUSY)
		dev_err(common->dev, "ethtool complete failed %d\n", ret);
}

static void am65_cpsw_get_drvinfo(struct net_device *ndev,
				  struct ethtool_drvinfo *info)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

	strscpy(info->driver, dev_driver_string(common->dev),
		sizeof(info->driver));
	strscpy(info->bus_info, dev_name(common->dev), sizeof(info->bus_info));
}

static u32 am65_cpsw_get_msglevel(struct net_device *ndev)
{
	struct am65_cpsw_ndev_priv *priv = am65_ndev_to_priv(ndev);

	return priv->msg_enable;
}

static void am65_cpsw_set_msglevel(struct net_device *ndev, u32 value)
{
	struct am65_cpsw_ndev_priv *priv = am65_ndev_to_priv(ndev);

	priv->msg_enable = value;
}

static void am65_cpsw_get_channels(struct net_device *ndev,
				   struct ethtool_channels *ch)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

	ch->max_rx = AM65_CPSW_MAX_RX_QUEUES;
	ch->max_tx = AM65_CPSW_MAX_TX_QUEUES;
	ch->rx_count = AM65_CPSW_MAX_RX_QUEUES;
	ch->tx_count = common->tx_ch_num;
}

static int am65_cpsw_set_channels(struct net_device *ndev,
				  struct ethtool_channels *chs)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

	if (!chs->rx_count || !chs->tx_count)
		return -EINVAL;

	/* Check if interface is up. Can change the num queues when
	 * the interface is down.
	 */
	if (common->usage_count)
		return -EBUSY;

	am65_cpsw_nuss_remove_tx_chns(common);

	return am65_cpsw_nuss_update_tx_chns(common, chs->tx_count);
}

static void
am65_cpsw_get_ringparam(struct net_device *ndev,
			struct ethtool_ringparam *ering,
			struct kernel_ethtool_ringparam *kernel_ering,
			struct netlink_ext_ack *extack)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

	/* not supported */
	ering->tx_pending = common->tx_chns[0].descs_num;
	ering->rx_pending = common->rx_chns.descs_num;
}

static void am65_cpsw_get_pauseparam(struct net_device *ndev,
				     struct ethtool_pauseparam *pause)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	phylink_ethtool_get_pauseparam(salve->phylink, pause);
}

static int am65_cpsw_set_pauseparam(struct net_device *ndev,
				    struct ethtool_pauseparam *pause)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_set_pauseparam(salve->phylink, pause);
}

static void am65_cpsw_get_wol(struct net_device *ndev,
			      struct ethtool_wolinfo *wol)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	phylink_ethtool_get_wol(salve->phylink, wol);
}

static int am65_cpsw_set_wol(struct net_device *ndev,
			     struct ethtool_wolinfo *wol)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_set_wol(salve->phylink, wol);
}

static int am65_cpsw_get_link_ksettings(struct net_device *ndev,
					struct ethtool_link_ksettings *ecmd)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_ksettings_get(salve->phylink, ecmd);
}

static int
am65_cpsw_set_link_ksettings(struct net_device *ndev,
			     const struct ethtool_link_ksettings *ecmd)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_ksettings_set(salve->phylink, ecmd);
}

static int am65_cpsw_get_eee(struct net_device *ndev, struct ethtool_eee *edata)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_get_eee(salve->phylink, edata);
}

static int am65_cpsw_set_eee(struct net_device *ndev, struct ethtool_eee *edata)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_set_eee(salve->phylink, edata);
}

static int am65_cpsw_nway_reset(struct net_device *ndev)
{
	struct am65_cpsw_slave_data *salve = am65_ndev_to_slave(ndev);

	return phylink_ethtool_nway_reset(salve->phylink);
}

static int am65_cpsw_get_regs_len(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	u32 ale_entries, i, regdump_len = 0;

	ale_entries = cpsw_ale_get_num_entries(common->ale);
	for (i = 0; i < ARRAY_SIZE(am65_cpsw_regdump); i++) {
		if (am65_cpsw_regdump[i].hdr.module_id ==
		    AM65_CPSW_REGDUMP_MOD_CPSW_ALE_TBL) {
			regdump_len += sizeof(struct am65_cpsw_regdump_hdr);
			regdump_len += ale_entries *
				       ALE_ENTRY_WORDS * sizeof(u32);
			continue;
		}
		regdump_len += am65_cpsw_regdump[i].hdr.len;
	}

	return regdump_len;
}

static void am65_cpsw_get_regs(struct net_device *ndev,
			       struct ethtool_regs *regs, void *p)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	u32 ale_entries, i, j, pos, *reg = p;

	/* update CPSW IP version */
	regs->version = AM65_CPSW_REGDUMP_VER;
	ale_entries = cpsw_ale_get_num_entries(common->ale);

	pos = 0;
	for (i = 0; i < ARRAY_SIZE(am65_cpsw_regdump); i++) {
		reg[pos++] = am65_cpsw_regdump[i].hdr.module_id;

		if (am65_cpsw_regdump[i].hdr.module_id ==
		    AM65_CPSW_REGDUMP_MOD_CPSW_ALE_TBL) {
			u32 ale_tbl_len = ale_entries *
					  ALE_ENTRY_WORDS * sizeof(u32) +
					  sizeof(struct am65_cpsw_regdump_hdr);
			reg[pos++] = ale_tbl_len;
			cpsw_ale_dump(common->ale, &reg[pos]);
			pos += ale_tbl_len;
			continue;
		}

		reg[pos++] = am65_cpsw_regdump[i].hdr.len;

		j = am65_cpsw_regdump[i].start_ofs;
		do {
			reg[pos++] = j;
			reg[pos++] = readl_relaxed(common->ss_base + j);
			j += sizeof(u32);
		} while (j <= am65_cpsw_regdump[i].end_ofs);
	}
}

static int am65_cpsw_get_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(am65_host_stats) +
		       ARRAY_SIZE(am65_slave_stats);
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(am65_cpsw_ethtool_priv_flags);
	default:
		return -EOPNOTSUPP;
	}
}

static void am65_cpsw_get_strings(struct net_device *ndev,
				  u32 stringset, u8 *data)
{
	const struct am65_cpsw_ethtool_stat *hw_stats;
	u32 i, num_stats;
	u8 *p = data;

	switch (stringset) {
	case ETH_SS_STATS:
		num_stats = ARRAY_SIZE(am65_host_stats);
		hw_stats = am65_host_stats;
		for (i = 0; i < num_stats; i++) {
			memcpy(p, hw_stats[i].desc, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		num_stats = ARRAY_SIZE(am65_slave_stats);
		hw_stats = am65_slave_stats;
		for (i = 0; i < num_stats; i++) {
			memcpy(p, hw_stats[i].desc, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		num_stats = ARRAY_SIZE(am65_cpsw_ethtool_priv_flags);

		for (i = 0; i < num_stats; i++) {
			memcpy(p, am65_cpsw_ethtool_priv_flags[i],
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void am65_cpsw_get_ethtool_stats(struct net_device *ndev,
					struct ethtool_stats *stats, u64 *data)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	const struct am65_cpsw_ethtool_stat *hw_stats;
	struct am65_cpsw_host *host_p;
	struct am65_cpsw_port *port;
	u32 i, num_stats;

	host_p = am65_common_get_host(common);
	port = am65_ndev_to_port(ndev);
	num_stats = ARRAY_SIZE(am65_host_stats);
	hw_stats = am65_host_stats;
	for (i = 0; i < num_stats; i++)
		*data++ = readl_relaxed(host_p->stat_base +
					hw_stats[i].offset);

	num_stats = ARRAY_SIZE(am65_slave_stats);
	hw_stats = am65_slave_stats;
	for (i = 0; i < num_stats; i++)
		*data++ = readl_relaxed(port->stat_base +
					hw_stats[i].offset);
}

static void am65_cpsw_get_eth_mac_stats(struct net_device *ndev,
					struct ethtool_eth_mac_stats *s)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_stats_regs __iomem *stats;

	stats = port->stat_base;

	if (s->src != ETHTOOL_MAC_STATS_SRC_AGGREGATE)
		return;

	s->FramesTransmittedOK = readl_relaxed(&stats->tx_good_frames);
	s->SingleCollisionFrames = readl_relaxed(&stats->tx_single_coll_frames);
	s->MultipleCollisionFrames = readl_relaxed(&stats->tx_mult_coll_frames);
	s->FramesReceivedOK = readl_relaxed(&stats->rx_good_frames);
	s->FrameCheckSequenceErrors = readl_relaxed(&stats->rx_crc_errors);
	s->AlignmentErrors = readl_relaxed(&stats->rx_align_code_errors);
	s->OctetsTransmittedOK = readl_relaxed(&stats->tx_octets);
	s->FramesWithDeferredXmissions = readl_relaxed(&stats->tx_deferred_frames);
	s->LateCollisions = readl_relaxed(&stats->tx_late_collisions);
	s->CarrierSenseErrors = readl_relaxed(&stats->tx_carrier_sense_errors);
	s->OctetsReceivedOK = readl_relaxed(&stats->rx_octets);
	s->MulticastFramesXmittedOK = readl_relaxed(&stats->tx_multicast_frames);
	s->BroadcastFramesXmittedOK = readl_relaxed(&stats->tx_broadcast_frames);
	s->MulticastFramesReceivedOK = readl_relaxed(&stats->rx_multicast_frames);
	s->BroadcastFramesReceivedOK = readl_relaxed(&stats->rx_broadcast_frames);
};

static int am65_cpsw_get_ethtool_ts_info(struct net_device *ndev,
					 struct ethtool_ts_info *info)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

	if (!IS_ENABLED(CONFIG_TI_K3_AM65_CPTS))
		return ethtool_op_get_ts_info(ndev, info);

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = am65_cpts_phc_index(common->cpts);
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);
	return 0;
}

static u32 am65_cpsw_get_ethtool_priv_flags(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	u32 priv_flags = 0;

	if (common->pf_p0_rx_ptype_rrobin)
		priv_flags |= AM65_CPSW_PRIV_P0_RX_PTYPE_RROBIN;

	return priv_flags;
}

static int am65_cpsw_set_ethtool_priv_flags(struct net_device *ndev, u32 flags)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	int rrobin;

	rrobin = !!(flags & AM65_CPSW_PRIV_P0_RX_PTYPE_RROBIN);

	if (common->usage_count)
		return -EBUSY;

	if (common->est_enabled && rrobin) {
		netdev_err(ndev,
			   "p0-rx-ptype-rrobin flag conflicts with QOS\n");
		return -EINVAL;
	}

	common->pf_p0_rx_ptype_rrobin = rrobin;

	return 0;
}

static void am65_cpsw_port_iet_rx_enable(struct am65_cpsw_port *port, bool enable)
{
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_CTL);
	if (enable)
		val |= AM65_CPSW_PN_CTL_IET_PORT_EN;
	else
		val &= ~AM65_CPSW_PN_CTL_IET_PORT_EN;

	writel(val, port->port_base + AM65_CPSW_PN_REG_CTL);
	am65_cpsw_iet_common_enable(port->common);
}

static void am65_cpsw_port_iet_tx_enable(struct am65_cpsw_port *port, bool enable)
{
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_IET_CTRL);
	if (enable)
		val |= AM65_CPSW_PN_IET_MAC_PENABLE;
	else
		val &= ~AM65_CPSW_PN_IET_MAC_PENABLE;

	writel(val, port->port_base + AM65_CPSW_PN_REG_IET_CTRL);
}

static int am65_cpsw_get_mm(struct net_device *ndev, struct ethtool_mm_state *state)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_ndev_priv *priv = netdev_priv(ndev);
	u32 port_ctrl, iet_ctrl, iet_status;
	u32 add_frag_size;

	if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_QOS))
		return -EOPNOTSUPP;

	mutex_lock(&priv->mm_lock);

	iet_ctrl = readl(port->port_base + AM65_CPSW_PN_REG_IET_CTRL);
	port_ctrl = readl(port->port_base + AM65_CPSW_PN_REG_CTL);

	state->tx_enabled = !!(iet_ctrl & AM65_CPSW_PN_IET_MAC_PENABLE);
	state->pmac_enabled = !!(port_ctrl & AM65_CPSW_PN_CTL_IET_PORT_EN);

	iet_status = readl(port->port_base + AM65_CPSW_PN_REG_IET_STATUS);

	if (iet_ctrl & AM65_CPSW_PN_IET_MAC_DISABLEVERIFY)
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_DISABLED;
	else if (iet_status & AM65_CPSW_PN_MAC_VERIFIED)
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED;
	else if (iet_status & AM65_CPSW_PN_MAC_VERIFY_FAIL)
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_FAILED;
	else
		state->verify_status = ETHTOOL_MM_VERIFY_STATUS_UNKNOWN;

	add_frag_size = AM65_CPSW_PN_IET_MAC_GET_ADDFRAGSIZE(iet_ctrl);
	state->tx_min_frag_size = ethtool_mm_frag_size_add_to_min(add_frag_size);

	/* Errata i2208: RX min fragment size cannot be less than 124 */
	state->rx_min_frag_size = 124;

	/* FPE active if common tx_enabled and verification success or disabled (forced) */
	state->tx_active = state->tx_enabled &&
			   (state->verify_status == ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED ||
			    state->verify_status == ETHTOOL_MM_VERIFY_STATUS_DISABLED);
	state->verify_enabled = !(iet_ctrl & AM65_CPSW_PN_IET_MAC_DISABLEVERIFY);

	state->verify_time = port->qos.iet.verify_time_ms;

	/* 802.3-2018 clause 30.14.1.6, says that the aMACMergeVerifyTime
	 * variable has a range between 1 and 128 ms inclusive. Limit to that.
	 */
	state->max_verify_time = 128;

	mutex_unlock(&priv->mm_lock);

	return 0;
}

static int am65_cpsw_set_mm(struct net_device *ndev, struct ethtool_mm_cfg *cfg,
			    struct netlink_ext_ack *extack)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_ndev_priv *priv = netdev_priv(ndev);
	struct am65_cpsw_iet *iet = &port->qos.iet;
	u32 val, add_frag_size;
	int err;

	if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_QOS))
		return -EOPNOTSUPP;

	err = ethtool_mm_frag_size_min_to_add(cfg->tx_min_frag_size, &add_frag_size, extack);
	if (err)
		return err;

	mutex_lock(&priv->mm_lock);

	if (cfg->pmac_enabled) {
		/* change TX & RX FIFO MAX_BLKS as per TRM recommendation */
		if (!iet->original_max_blks)
			iet->original_max_blks = readl(port->port_base + AM65_CPSW_PN_REG_MAX_BLKS);

		writel(AM65_CPSW_PN_TX_RX_MAX_BLKS_IET,
		       port->port_base + AM65_CPSW_PN_REG_MAX_BLKS);
	} else if (iet->original_max_blks) {
		/* restore RX & TX FIFO MAX_BLKS */
		writel(iet->original_max_blks,
		       port->port_base + AM65_CPSW_PN_REG_MAX_BLKS);
	}

	am65_cpsw_port_iet_rx_enable(port, cfg->pmac_enabled);
	am65_cpsw_port_iet_tx_enable(port, cfg->tx_enabled);

	val = readl(port->port_base + AM65_CPSW_PN_REG_IET_CTRL);
	if (cfg->verify_enabled) {
		val &= ~AM65_CPSW_PN_IET_MAC_DISABLEVERIFY;
		/* Reset Verify state machine. Verification won't start here.
		 * Verification will be done once link-up.
		 */
		val |= AM65_CPSW_PN_IET_MAC_LINKFAIL;
	} else {
		val |= AM65_CPSW_PN_IET_MAC_DISABLEVERIFY;
		/* Clear LINKFAIL to allow verify/response packets */
		val &= ~AM65_CPSW_PN_IET_MAC_LINKFAIL;
	}

	val &= ~AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_MASK;
	val |= AM65_CPSW_PN_IET_MAC_SET_ADDFRAGSIZE(add_frag_size);
	writel(val, port->port_base + AM65_CPSW_PN_REG_IET_CTRL);

	/* verify_timeout_count can only be set at valid link */
	port->qos.iet.verify_time_ms = cfg->verify_time;

	/* enable/disable preemption based on link status */
	am65_cpsw_iet_commit_preemptible_tcs(port);

	mutex_unlock(&priv->mm_lock);

	return 0;
}

static void am65_cpsw_get_mm_stats(struct net_device *ndev,
				   struct ethtool_mm_stats *s)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	void __iomem *base = port->stat_base;

	s->MACMergeFrameAssOkCount = readl(base + AM65_CPSW_STATN_IET_RX_ASSEMBLY_OK);
	s->MACMergeFrameAssErrorCount = readl(base + AM65_CPSW_STATN_IET_RX_ASSEMBLY_ERROR);
	s->MACMergeFrameSmdErrorCount = readl(base + AM65_CPSW_STATN_IET_RX_SMD_ERROR);
	/* CPSW Functional Spec states:
	 * "The IET stat aMACMergeFragCountRx is derived by adding the
	 *  Receive Assembly Error count to this value. i.e. AM65_CPSW_STATN_IET_RX_FRAG"
	 */
	s->MACMergeFragCountRx = readl(base + AM65_CPSW_STATN_IET_RX_FRAG) + s->MACMergeFrameAssErrorCount;
	s->MACMergeFragCountTx = readl(base + AM65_CPSW_STATN_IET_TX_FRAG);
	s->MACMergeHoldCount = readl(base + AM65_CPSW_STATN_IET_TX_HOLD);
}

const struct ethtool_ops am65_cpsw_ethtool_ops_slave = {
	.begin			= am65_cpsw_ethtool_op_begin,
	.complete		= am65_cpsw_ethtool_op_complete,
	.get_drvinfo		= am65_cpsw_get_drvinfo,
	.get_msglevel		= am65_cpsw_get_msglevel,
	.set_msglevel		= am65_cpsw_set_msglevel,
	.get_channels		= am65_cpsw_get_channels,
	.set_channels		= am65_cpsw_set_channels,
	.get_ringparam		= am65_cpsw_get_ringparam,
	.get_regs_len		= am65_cpsw_get_regs_len,
	.get_regs		= am65_cpsw_get_regs,
	.get_sset_count		= am65_cpsw_get_sset_count,
	.get_strings		= am65_cpsw_get_strings,
	.get_ethtool_stats	= am65_cpsw_get_ethtool_stats,
	.get_eth_mac_stats	= am65_cpsw_get_eth_mac_stats,
	.get_ts_info		= am65_cpsw_get_ethtool_ts_info,
	.get_priv_flags		= am65_cpsw_get_ethtool_priv_flags,
	.set_priv_flags		= am65_cpsw_set_ethtool_priv_flags,

	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= am65_cpsw_get_link_ksettings,
	.set_link_ksettings	= am65_cpsw_set_link_ksettings,
	.get_pauseparam		= am65_cpsw_get_pauseparam,
	.set_pauseparam		= am65_cpsw_set_pauseparam,
	.get_wol		= am65_cpsw_get_wol,
	.set_wol		= am65_cpsw_set_wol,
	.get_eee		= am65_cpsw_get_eee,
	.set_eee		= am65_cpsw_set_eee,
	.nway_reset		= am65_cpsw_nway_reset,
	.get_mm			= am65_cpsw_get_mm,
	.set_mm			= am65_cpsw_set_mm,
	.get_mm_stats		= am65_cpsw_get_mm_stats,
};

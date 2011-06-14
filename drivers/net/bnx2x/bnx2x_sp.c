#include <linux/version.h>
#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/crc32c.h>
#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include "bnx2x_sp.h"


/**
 * bnx2x_set_mac_addr_gen - set a MAC in a CAM for a few L2 Clients for E1x chips
 *
 * @bp:		driver handle
 * @set:	set or clear an entry (1 or 0)
 * @mac:	pointer to a buffer containing a MAC
 * @cl_bit_vec:	bit vector of clients to register a MAC for
 * @cam_offset:	offset in a CAM to use
 * @is_bcast:	is the set MAC a broadcast address (for E1 only)
 */
void bnx2x_set_mac_addr_gen(struct bnx2x *bp, int set, const u8 *mac,
			    u32 cl_bit_vec, u8 cam_offset,
			    u8 is_bcast)
{
	struct mac_configuration_cmd *config =
		(struct mac_configuration_cmd *)bnx2x_sp(bp, mac_config);
	int ramrod_flags = WAIT_RAMROD_COMMON;

	bp->set_mac_pending = 1;

	config->hdr.length = 1;
	config->hdr.offset = cam_offset;
	config->hdr.client_id = 0xff;
	/* Mark the single MAC configuration ramrod as opposed to a
	 * UC/MC list configuration).
	 */
	config->hdr.echo = 1;

	/* primary MAC */
	config->config_table[0].msb_mac_addr =
					swab16(*(u16 *)&mac[0]);
	config->config_table[0].middle_mac_addr =
					swab16(*(u16 *)&mac[2]);
	config->config_table[0].lsb_mac_addr =
					swab16(*(u16 *)&mac[4]);
	config->config_table[0].clients_bit_vector =
					cpu_to_le32(cl_bit_vec);
	config->config_table[0].vlan_id = 0;
	config->config_table[0].pf_id = BP_FUNC(bp);
	if (set)
		SET_FLAG(config->config_table[0].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_SET);
	else
		SET_FLAG(config->config_table[0].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_INVALIDATE);

	if (is_bcast)
		SET_FLAG(config->config_table[0].flags,
			MAC_CONFIGURATION_ENTRY_BROADCAST, 1);

	DP(NETIF_MSG_IFUP, "%s MAC (%04x:%04x:%04x)  PF_ID %d  CLID mask %d\n",
	   (set ? "setting" : "clearing"),
	   config->config_table[0].msb_mac_addr,
	   config->config_table[0].middle_mac_addr,
	   config->config_table[0].lsb_mac_addr, BP_FUNC(bp), cl_bit_vec);

	mb();

	bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_SET_MAC, 0,
		      U64_HI(bnx2x_sp_mapping(bp, mac_config)),
		      U64_LO(bnx2x_sp_mapping(bp, mac_config)), 1);

	/* Wait for a completion */
	bnx2x_wait_ramrod(bp, 0, 0, &bp->set_mac_pending, ramrod_flags);
}


static inline u8 bnx2x_e1_cam_mc_offset(struct bnx2x *bp)
{
	return CHIP_REV_IS_SLOW(bp) ?
		(BNX2X_MAX_EMUL_MULTI * (1 + BP_PORT(bp))) :
		(BNX2X_MAX_MULTICAST * (1 + BP_PORT(bp)));
}

/* set mc list, do not wait as wait implies sleep and
 * set_rx_mode can be invoked from non-sleepable context.
 *
 * Instead we use the same ramrod data buffer each time we need
 * to configure a list of addresses, and use the fact that the
 * list of MACs is changed in an incremental way and that the
 * function is called under the netif_addr_lock. A temporary
 * inconsistent CAM configuration (possible in case of a very fast
 * sequence of add/del/add on the host side) will shortly be
 * restored by the handler of the last ramrod.
 */
int bnx2x_set_e1_mc_list(struct bnx2x *bp)
{
	int i = 0, old;
	struct net_device *dev = bp->dev;
	u8 offset = bnx2x_e1_cam_mc_offset(bp);
	struct netdev_hw_addr *ha;
	struct mac_configuration_cmd *config_cmd = bnx2x_sp(bp, mcast_config);
	dma_addr_t config_cmd_map = bnx2x_sp_mapping(bp, mcast_config);

	if (netdev_mc_count(dev) > BNX2X_MAX_MULTICAST)
		return -EINVAL;

	netdev_for_each_mc_addr(ha, dev) {
		/* copy mac */
		config_cmd->config_table[i].msb_mac_addr =
			swab16(*(u16 *)&bnx2x_mc_addr(ha)[0]);
		config_cmd->config_table[i].middle_mac_addr =
			swab16(*(u16 *)&bnx2x_mc_addr(ha)[2]);
		config_cmd->config_table[i].lsb_mac_addr =
			swab16(*(u16 *)&bnx2x_mc_addr(ha)[4]);

		config_cmd->config_table[i].vlan_id = 0;
		config_cmd->config_table[i].pf_id = BP_FUNC(bp);
		config_cmd->config_table[i].clients_bit_vector =
			cpu_to_le32(1 << BP_L_ID(bp));

		SET_FLAG(config_cmd->config_table[i].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_SET);

		DP(NETIF_MSG_IFUP,
		   "setting MCAST[%d] (%04x:%04x:%04x)\n", i,
		   config_cmd->config_table[i].msb_mac_addr,
		   config_cmd->config_table[i].middle_mac_addr,
		   config_cmd->config_table[i].lsb_mac_addr);
		i++;
	}
	old = config_cmd->hdr.length;
	if (old > i) {
		for (; i < old; i++) {
			if (CAM_IS_INVALID(config_cmd->
					   config_table[i])) {
				/* already invalidated */
				break;
			}
			/* invalidate */
			SET_FLAG(config_cmd->config_table[i].flags,
				MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
				T_ETH_MAC_COMMAND_INVALIDATE);
		}
	}

	wmb();

	config_cmd->hdr.length = i;
	config_cmd->hdr.offset = offset;
	config_cmd->hdr.client_id = 0xff;
	/* Mark that this ramrod doesn't use bp->set_mac_pending for
	 * synchronization.
	 */
	config_cmd->hdr.echo = 0;

	mb();

	return bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_SET_MAC, 0,
		   U64_HI(config_cmd_map), U64_LO(config_cmd_map), 1);
}

void bnx2x_invalidate_e1_mc_list(struct bnx2x *bp)
{
	int i;
	struct mac_configuration_cmd *config_cmd = bnx2x_sp(bp, mcast_config);
	dma_addr_t config_cmd_map = bnx2x_sp_mapping(bp, mcast_config);
	int ramrod_flags = WAIT_RAMROD_COMMON;
	u8 offset = bnx2x_e1_cam_mc_offset(bp);

	for (i = 0; i < BNX2X_MAX_MULTICAST; i++)
		SET_FLAG(config_cmd->config_table[i].flags,
			MAC_CONFIGURATION_ENTRY_ACTION_TYPE,
			T_ETH_MAC_COMMAND_INVALIDATE);

	wmb();

	config_cmd->hdr.length = BNX2X_MAX_MULTICAST;
	config_cmd->hdr.offset = offset;
	config_cmd->hdr.client_id = 0xff;
	/* We'll wait for a completion this time... */
	config_cmd->hdr.echo = 1;

	bp->set_mac_pending = 1;

	mb();

	bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_SET_MAC, 0,
		      U64_HI(config_cmd_map), U64_LO(config_cmd_map), 1);

	/* Wait for a completion */
	bnx2x_wait_ramrod(bp, 0, 0, &bp->set_mac_pending,
				ramrod_flags);

}

/* Accept one or more multicasts */
int bnx2x_set_e1h_mc_list(struct bnx2x *bp)
{
	struct net_device *dev = bp->dev;
	struct netdev_hw_addr *ha;
	u32 mc_filter[MC_HASH_SIZE];
	u32 crc, bit, regidx;
	int i;

	memset(mc_filter, 0, 4 * MC_HASH_SIZE);

	netdev_for_each_mc_addr(ha, dev) {
		DP(NETIF_MSG_IFUP, "Adding mcast MAC: %pM\n",
		   bnx2x_mc_addr(ha));

		crc = crc32c_le(0, bnx2x_mc_addr(ha),
				ETH_ALEN);
		bit = (crc >> 24) & 0xff;
		regidx = bit >> 5;
		bit &= 0x1f;
		mc_filter[regidx] |= (1 << bit);
	}

	for (i = 0; i < MC_HASH_SIZE; i++)
		REG_WR(bp, MC_HASH_OFFSET(bp, i),
		       mc_filter[i]);

	return 0;
}

void bnx2x_invalidate_e1h_mc_list(struct bnx2x *bp)
{
	int i;

	for (i = 0; i < MC_HASH_SIZE; i++)
		REG_WR(bp, MC_HASH_OFFSET(bp, i), 0);
}

/* must be called under rtnl_lock */
void bnx2x_rxq_set_mac_filters(struct bnx2x *bp, u16 cl_id, u32 filters)
{
	u32 mask = (1 << cl_id);

	/* initial seeting is BNX2X_ACCEPT_NONE */
	u8 drop_all_ucast = 1, drop_all_bcast = 1, drop_all_mcast = 1;
	u8 accp_all_ucast = 0, accp_all_bcast = 0, accp_all_mcast = 0;
	u8 unmatched_unicast = 0;

	if (filters & BNX2X_ACCEPT_UNMATCHED_UCAST)
		unmatched_unicast = 1;

	if (filters & BNX2X_PROMISCUOUS_MODE) {
		/* promiscious - accept all, drop none */
		drop_all_ucast = drop_all_bcast = drop_all_mcast = 0;
		accp_all_ucast = accp_all_bcast = accp_all_mcast = 1;
		if (IS_MF_SI(bp)) {
			/*
			 * SI mode defines to accept in promiscuos mode
			 * only unmatched packets
			 */
			unmatched_unicast = 1;
			accp_all_ucast = 0;
		}
	}
	if (filters & BNX2X_ACCEPT_UNICAST) {
		/* accept matched ucast */
		drop_all_ucast = 0;
	}
	if (filters & BNX2X_ACCEPT_MULTICAST)
		/* accept matched mcast */
		drop_all_mcast = 0;

	if (filters & BNX2X_ACCEPT_ALL_UNICAST) {
		/* accept all mcast */
		drop_all_ucast = 0;
		accp_all_ucast = 1;
	}
	if (filters & BNX2X_ACCEPT_ALL_MULTICAST) {
		/* accept all mcast */
		drop_all_mcast = 0;
		accp_all_mcast = 1;
	}
	if (filters & BNX2X_ACCEPT_BROADCAST) {
		/* accept (all) bcast */
		drop_all_bcast = 0;
		accp_all_bcast = 1;
	}

	bp->mac_filters.ucast_drop_all = drop_all_ucast ?
		bp->mac_filters.ucast_drop_all | mask :
		bp->mac_filters.ucast_drop_all & ~mask;

	bp->mac_filters.mcast_drop_all = drop_all_mcast ?
		bp->mac_filters.mcast_drop_all | mask :
		bp->mac_filters.mcast_drop_all & ~mask;

	bp->mac_filters.bcast_drop_all = drop_all_bcast ?
		bp->mac_filters.bcast_drop_all | mask :
		bp->mac_filters.bcast_drop_all & ~mask;

	bp->mac_filters.ucast_accept_all = accp_all_ucast ?
		bp->mac_filters.ucast_accept_all | mask :
		bp->mac_filters.ucast_accept_all & ~mask;

	bp->mac_filters.mcast_accept_all = accp_all_mcast ?
		bp->mac_filters.mcast_accept_all | mask :
		bp->mac_filters.mcast_accept_all & ~mask;

	bp->mac_filters.bcast_accept_all = accp_all_bcast ?
		bp->mac_filters.bcast_accept_all | mask :
		bp->mac_filters.bcast_accept_all & ~mask;

	bp->mac_filters.unmatched_unicast = unmatched_unicast ?
		bp->mac_filters.unmatched_unicast | mask :
		bp->mac_filters.unmatched_unicast & ~mask;
}

void bnx2x_set_storm_rx_mode(struct bnx2x *bp)
{
	int mode = bp->rx_mode;
	int port = BP_PORT(bp);
	u16 cl_id;
	u32 def_q_filters = 0;

	/* All but management unicast packets should pass to the host as well */
	u32 llh_mask =
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_BRCST |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_MLCST |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_VLAN |
		NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_NO_VLAN;

	switch (mode) {
	case BNX2X_RX_MODE_NONE: /* no Rx */
		def_q_filters = BNX2X_ACCEPT_NONE;
#ifdef BCM_CNIC
		if (!NO_FCOE(bp)) {
			cl_id = bnx2x_fcoe(bp, cl_id);
			bnx2x_rxq_set_mac_filters(bp, cl_id, BNX2X_ACCEPT_NONE);
		}
#endif
		break;

	case BNX2X_RX_MODE_NORMAL:
		def_q_filters |= BNX2X_ACCEPT_UNICAST | BNX2X_ACCEPT_BROADCAST |
				BNX2X_ACCEPT_MULTICAST;
#ifdef BCM_CNIC
		if (!NO_FCOE(bp)) {
			cl_id = bnx2x_fcoe(bp, cl_id);
			bnx2x_rxq_set_mac_filters(bp, cl_id,
						  BNX2X_ACCEPT_UNICAST |
						  BNX2X_ACCEPT_MULTICAST);
		}
#endif
		break;

	case BNX2X_RX_MODE_ALLMULTI:
		def_q_filters |= BNX2X_ACCEPT_UNICAST | BNX2X_ACCEPT_BROADCAST |
				BNX2X_ACCEPT_ALL_MULTICAST;
#ifdef BCM_CNIC
		/*
		 *  Prevent duplication of multicast packets by configuring FCoE
		 *  L2 Client to receive only matched unicast frames.
		 */
		if (!NO_FCOE(bp)) {
			cl_id = bnx2x_fcoe(bp, cl_id);
			bnx2x_rxq_set_mac_filters(bp, cl_id,
						  BNX2X_ACCEPT_UNICAST);
		}
#endif
		break;

	case BNX2X_RX_MODE_PROMISC:
		def_q_filters |= BNX2X_PROMISCUOUS_MODE;
#ifdef BCM_CNIC
		/*
		 *  Prevent packets duplication by configuring DROP_ALL for FCoE
		 *  L2 Client.
		 */
		if (!NO_FCOE(bp)) {
			cl_id = bnx2x_fcoe(bp, cl_id);
			bnx2x_rxq_set_mac_filters(bp, cl_id, BNX2X_ACCEPT_NONE);
		}
#endif
		/* pass management unicast packets as well */
		llh_mask |= NIG_LLH0_BRB1_DRV_MASK_REG_LLH0_BRB1_DRV_MASK_UNCST;
		break;

	default:
		BNX2X_ERR("BAD rx mode (%d)\n", mode);
		break;
	}

	cl_id = BP_L_ID(bp);
	bnx2x_rxq_set_mac_filters(bp, cl_id, def_q_filters);

	REG_WR(bp,
	       (port ? NIG_REG_LLH1_BRB1_DRV_MASK :
		       NIG_REG_LLH0_BRB1_DRV_MASK), llh_mask);

	DP(NETIF_MSG_IFUP, "rx mode %d\n"
		"drop_ucast 0x%x\ndrop_mcast 0x%x\ndrop_bcast 0x%x\n"
		"accp_ucast 0x%x\naccp_mcast 0x%x\naccp_bcast 0x%x\n"
		"unmatched_ucast 0x%x\n", mode,
		bp->mac_filters.ucast_drop_all,
		bp->mac_filters.mcast_drop_all,
		bp->mac_filters.bcast_drop_all,
		bp->mac_filters.ucast_accept_all,
		bp->mac_filters.mcast_accept_all,
		bp->mac_filters.bcast_accept_all,
		bp->mac_filters.unmatched_unicast
	);

	storm_memset_mac_filters(bp, &bp->mac_filters, BP_FUNC(bp));
}

/* RSS configuration */
static inline void __storm_memset_dma_mapping(struct bnx2x *bp,
				       u32 addr, dma_addr_t mapping)
{
	REG_WR(bp,  addr, U64_LO(mapping));
	REG_WR(bp,  addr + 4, U64_HI(mapping));
}

static inline void __storm_fill(struct bnx2x *bp,
				       u32 addr, size_t size, u32 val)
{
	int i;
	for (i = 0; i < size/4; i++)
		REG_WR(bp,  addr + (i * 4), val);
}

static inline void storm_memset_ustats_zero(struct bnx2x *bp,
					    u8 port, u16 stat_id)
{
	size_t size = sizeof(struct ustorm_per_client_stats);

	u32 addr = BAR_USTRORM_INTMEM +
			USTORM_PER_COUNTER_ID_STATS_OFFSET(port, stat_id);

	__storm_fill(bp, addr, size, 0);
}

static inline void storm_memset_tstats_zero(struct bnx2x *bp,
					    u8 port, u16 stat_id)
{
	size_t size = sizeof(struct tstorm_per_client_stats);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stat_id);

	__storm_fill(bp, addr, size, 0);
}

static inline void storm_memset_xstats_zero(struct bnx2x *bp,
					    u8 port, u16 stat_id)
{
	size_t size = sizeof(struct xstorm_per_client_stats);

	u32 addr = BAR_XSTRORM_INTMEM +
			XSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stat_id);

	__storm_fill(bp, addr, size, 0);
}


static inline void storm_memset_spq_addr(struct bnx2x *bp,
					 dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = XSEM_REG_FAST_MEMORY +
			XSTORM_SPQ_PAGE_BASE_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static inline void storm_memset_xstats_flags(struct bnx2x *bp,
				struct stats_indication_flags *flags,
				u16 abs_fid)
{
	size_t size = sizeof(struct stats_indication_flags);

	u32 addr = BAR_XSTRORM_INTMEM + XSTORM_STATS_FLAGS_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)flags);
}

static inline void storm_memset_tstats_flags(struct bnx2x *bp,
				struct stats_indication_flags *flags,
				u16 abs_fid)
{
	size_t size = sizeof(struct stats_indication_flags);

	u32 addr = BAR_TSTRORM_INTMEM + TSTORM_STATS_FLAGS_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)flags);
}

static inline void storm_memset_ustats_flags(struct bnx2x *bp,
				struct stats_indication_flags *flags,
				u16 abs_fid)
{
	size_t size = sizeof(struct stats_indication_flags);

	u32 addr = BAR_USTRORM_INTMEM + USTORM_STATS_FLAGS_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)flags);
}

static inline void storm_memset_cstats_flags(struct bnx2x *bp,
				struct stats_indication_flags *flags,
				u16 abs_fid)
{
	size_t size = sizeof(struct stats_indication_flags);

	u32 addr = BAR_CSTRORM_INTMEM + CSTORM_STATS_FLAGS_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)flags);
}

static inline void storm_memset_xstats_addr(struct bnx2x *bp,
					   dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = BAR_XSTRORM_INTMEM +
		XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static inline void storm_memset_tstats_addr(struct bnx2x *bp,
					   dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = BAR_TSTRORM_INTMEM +
		TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static inline void storm_memset_ustats_addr(struct bnx2x *bp,
					   dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = BAR_USTRORM_INTMEM +
		USTORM_ETH_STATS_QUERY_ADDR_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static inline void storm_memset_cstats_addr(struct bnx2x *bp,
					   dma_addr_t mapping, u16 abs_fid)
{
	u32 addr = BAR_CSTRORM_INTMEM +
		CSTORM_ETH_STATS_QUERY_ADDR_OFFSET(abs_fid);

	__storm_memset_dma_mapping(bp, addr, mapping);
}

static inline void storm_memset_vf_to_pf(struct bnx2x *bp, u16 abs_fid,
					 u16 pf_id)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
}

static inline void storm_memset_func_en(struct bnx2x *bp, u16 abs_fid,
					u8 enable)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
}

static inline void storm_memset_func_cfg(struct bnx2x *bp,
				struct tstorm_eth_function_common_config *tcfg,
				u16 abs_fid)
{
	size_t size = sizeof(struct tstorm_eth_function_common_config);

	u32 addr = BAR_TSTRORM_INTMEM +
			TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(abs_fid);

	__storm_memset_struct(bp, addr, size, (u32 *)tcfg);
}

void bnx2x_func_init(struct bnx2x *bp, struct bnx2x_func_init_params *p)
{
	struct tstorm_eth_function_common_config tcfg = {0};
	u16 rss_flgs;

	/* tpa */
	if (p->func_flgs & FUNC_FLG_TPA)
		tcfg.config_flags |=
		TSTORM_ETH_FUNCTION_COMMON_CONFIG_ENABLE_TPA;

	/* set rss flags */
	rss_flgs = (p->rss->mode <<
		TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE_SHIFT);

	if (p->rss->cap & RSS_IPV4_CAP)
		rss_flgs |= RSS_IPV4_CAP_MASK;
	if (p->rss->cap & RSS_IPV4_TCP_CAP)
		rss_flgs |= RSS_IPV4_TCP_CAP_MASK;
	if (p->rss->cap & RSS_IPV6_CAP)
		rss_flgs |= RSS_IPV6_CAP_MASK;
	if (p->rss->cap & RSS_IPV6_TCP_CAP)
		rss_flgs |= RSS_IPV6_TCP_CAP_MASK;

	tcfg.config_flags |= rss_flgs;
	tcfg.rss_result_mask = p->rss->result_mask;

	storm_memset_func_cfg(bp, &tcfg, p->func_id);

	/* Enable the function in the FW */
	storm_memset_vf_to_pf(bp, p->func_id, p->pf_id);
	storm_memset_func_en(bp, p->func_id, 1);

	/* statistics */
	if (p->func_flgs & FUNC_FLG_STATS) {
		struct stats_indication_flags stats_flags = {0};
		stats_flags.collect_eth = 1;

		storm_memset_xstats_flags(bp, &stats_flags, p->func_id);
		storm_memset_xstats_addr(bp, p->fw_stat_map, p->func_id);

		storm_memset_tstats_flags(bp, &stats_flags, p->func_id);
		storm_memset_tstats_addr(bp, p->fw_stat_map, p->func_id);

		storm_memset_ustats_flags(bp, &stats_flags, p->func_id);
		storm_memset_ustats_addr(bp, p->fw_stat_map, p->func_id);

		storm_memset_cstats_flags(bp, &stats_flags, p->func_id);
		storm_memset_cstats_addr(bp, p->fw_stat_map, p->func_id);
	}

	/* spq */
	if (p->func_flgs & FUNC_FLG_SPQ) {
		storm_memset_spq_addr(bp, p->spq_map, p->func_id);
		REG_WR(bp, XSEM_REG_FAST_MEMORY +
		       XSTORM_SPQ_PROD_OFFSET(p->func_id), p->spq_prod);
	}
}

static void bnx2x_fill_cl_init_data(struct bnx2x *bp,
				    struct bnx2x_client_init_params *params,
				    u8 activate,
				    struct client_init_ramrod_data *data)
{
	/* Clear the buffer */
	memset(data, 0, sizeof(*data));

	/* general */
	data->general.client_id = params->rxq_params.cl_id;
	data->general.statistics_counter_id = params->rxq_params.stat_id;
	data->general.statistics_en_flg =
		(params->rxq_params.flags & QUEUE_FLG_STATS) ? 1 : 0;
	data->general.is_fcoe_flg =
		(params->ramrod_params.flags & CLIENT_IS_FCOE) ? 1 : 0;
	data->general.activate_flg = activate;
	data->general.sp_client_id = params->rxq_params.spcl_id;

	/* Rx data */
	data->rx.tpa_en_flg =
		(params->rxq_params.flags & QUEUE_FLG_TPA) ? 1 : 0;
	data->rx.vmqueue_mode_en_flg = 0;
	data->rx.cache_line_alignment_log_size =
		params->rxq_params.cache_line_log;
	data->rx.enable_dynamic_hc =
		(params->rxq_params.flags & QUEUE_FLG_DHC) ? 1 : 0;
	data->rx.max_sges_for_packet = params->rxq_params.max_sges_pkt;
	data->rx.client_qzone_id = params->rxq_params.cl_qzone_id;
	data->rx.max_agg_size = params->rxq_params.tpa_agg_sz;

	/* We don't set drop flags */
	data->rx.drop_ip_cs_err_flg = 0;
	data->rx.drop_tcp_cs_err_flg = 0;
	data->rx.drop_ttl0_flg = 0;
	data->rx.drop_udp_cs_err_flg = 0;

	data->rx.inner_vlan_removal_enable_flg =
		(params->rxq_params.flags & QUEUE_FLG_VLAN) ? 1 : 0;
	data->rx.outer_vlan_removal_enable_flg =
		(params->rxq_params.flags & QUEUE_FLG_OV) ? 1 : 0;
	data->rx.status_block_id = params->rxq_params.fw_sb_id;
	data->rx.rx_sb_index_number = params->rxq_params.sb_cq_index;
	data->rx.bd_buff_size = cpu_to_le16(params->rxq_params.buf_sz);
	data->rx.sge_buff_size = cpu_to_le16(params->rxq_params.sge_buf_sz);
	data->rx.mtu = cpu_to_le16(params->rxq_params.mtu);
	data->rx.bd_page_base.lo =
		cpu_to_le32(U64_LO(params->rxq_params.dscr_map));
	data->rx.bd_page_base.hi =
		cpu_to_le32(U64_HI(params->rxq_params.dscr_map));
	data->rx.sge_page_base.lo =
		cpu_to_le32(U64_LO(params->rxq_params.sge_map));
	data->rx.sge_page_base.hi =
		cpu_to_le32(U64_HI(params->rxq_params.sge_map));
	data->rx.cqe_page_base.lo =
		cpu_to_le32(U64_LO(params->rxq_params.rcq_map));
	data->rx.cqe_page_base.hi =
		cpu_to_le32(U64_HI(params->rxq_params.rcq_map));
	data->rx.is_leading_rss =
		(params->ramrod_params.flags & CLIENT_IS_LEADING_RSS) ? 1 : 0;
	data->rx.is_approx_mcast = data->rx.is_leading_rss;

	/* Tx data */
	data->tx.enforce_security_flg = 0; /* VF specific */
	data->tx.tx_status_block_id = params->txq_params.fw_sb_id;
	data->tx.tx_sb_index_number = params->txq_params.sb_cq_index;
	data->tx.mtu = 0; /* VF specific */
	data->tx.tx_bd_page_base.lo =
		cpu_to_le32(U64_LO(params->txq_params.dscr_map));
	data->tx.tx_bd_page_base.hi =
		cpu_to_le32(U64_HI(params->txq_params.dscr_map));

	/* flow control data */
	data->fc.cqe_pause_thr_low = cpu_to_le16(params->pause.rcq_th_lo);
	data->fc.cqe_pause_thr_high = cpu_to_le16(params->pause.rcq_th_hi);
	data->fc.bd_pause_thr_low = cpu_to_le16(params->pause.bd_th_lo);
	data->fc.bd_pause_thr_high = cpu_to_le16(params->pause.bd_th_hi);
	data->fc.sge_pause_thr_low = cpu_to_le16(params->pause.sge_th_lo);
	data->fc.sge_pause_thr_high = cpu_to_le16(params->pause.sge_th_hi);
	data->fc.rx_cos_mask = cpu_to_le16(params->pause.pri_map);

	data->fc.safc_group_num = params->txq_params.cos;
	data->fc.safc_group_en_flg =
		(params->txq_params.flags & QUEUE_FLG_COS) ? 1 : 0;
	data->fc.traffic_type =
		(params->ramrod_params.flags & CLIENT_IS_FCOE) ?
		LLFC_TRAFFIC_TYPE_FCOE : LLFC_TRAFFIC_TYPE_NW;
}


int bnx2x_setup_fw_client(struct bnx2x *bp,
			  struct bnx2x_client_init_params *params,
			  u8 activate,
			  struct client_init_ramrod_data *data,
			  dma_addr_t data_mapping)
{
	u16 hc_usec;
	int ramrod = RAMROD_CMD_ID_ETH_CLIENT_SETUP;
	int ramrod_flags = 0, rc;

	/* HC and context validation values */
	hc_usec = params->txq_params.hc_rate ?
		1000000 / params->txq_params.hc_rate : 0;
	bnx2x_update_coalesce_sb_index(bp,
			params->txq_params.fw_sb_id,
			params->txq_params.sb_cq_index,
			!(params->txq_params.flags & QUEUE_FLG_HC),
			hc_usec);

	*(params->ramrod_params.pstate) = BNX2X_FP_STATE_OPENING;

	hc_usec = params->rxq_params.hc_rate ?
		1000000 / params->rxq_params.hc_rate : 0;
	bnx2x_update_coalesce_sb_index(bp,
			params->rxq_params.fw_sb_id,
			params->rxq_params.sb_cq_index,
			!(params->rxq_params.flags & QUEUE_FLG_HC),
			hc_usec);

	bnx2x_set_ctx_validation(params->rxq_params.cxt,
				 params->rxq_params.cid);

	/* zero stats */
	if (params->txq_params.flags & QUEUE_FLG_STATS)
		storm_memset_xstats_zero(bp, BP_PORT(bp),
					 params->txq_params.stat_id);

	if (params->rxq_params.flags & QUEUE_FLG_STATS) {
		storm_memset_ustats_zero(bp, BP_PORT(bp),
					 params->rxq_params.stat_id);
		storm_memset_tstats_zero(bp, BP_PORT(bp),
					 params->rxq_params.stat_id);
	}

	/* Fill the ramrod data */
	bnx2x_fill_cl_init_data(bp, params, activate, data);

	/* SETUP ramrod.
	 *
	 * bnx2x_sp_post() takes a spin_lock thus no other explict memory
	 * barrier except from mmiowb() is needed to impose a
	 * proper ordering of memory operations.
	 */
	mmiowb();


	bnx2x_sp_post(bp, ramrod, params->ramrod_params.cid,
		      U64_HI(data_mapping), U64_LO(data_mapping), 0);

	/* Wait for completion */
	rc = bnx2x_wait_ramrod(bp, params->ramrod_params.state,
				 params->ramrod_params.index,
				 params->ramrod_params.pstate,
				 ramrod_flags);
	return rc;
}

void bnx2x_push_indir_table(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	int i;

	if (bp->multi_mode == ETH_RSS_MODE_DISABLED)
		return;

	for (i = 0; i < TSTORM_INDIRECTION_TABLE_SIZE; i++)
		REG_WR8(bp, BAR_TSTRORM_INTMEM +
			TSTORM_INDIRECTION_TABLE_OFFSET(func) + i,
			bp->fp->cl_id + bp->rx_indir_table[i]);
}

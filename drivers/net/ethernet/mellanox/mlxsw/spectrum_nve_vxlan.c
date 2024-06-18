// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/random.h>
#include <net/vxlan.h>

#include "reg.h"
#include "spectrum.h"
#include "spectrum_nve.h"

#define MLXSW_SP_NVE_VXLAN_IPV4_SUPPORTED_FLAGS (VXLAN_F_UDP_ZERO_CSUM_TX | \
						 VXLAN_F_LEARN | \
						 VXLAN_F_LOCALBYPASS)
#define MLXSW_SP_NVE_VXLAN_IPV6_SUPPORTED_FLAGS (VXLAN_F_IPV6 | \
						 VXLAN_F_UDP_ZERO_CSUM6_TX | \
						 VXLAN_F_UDP_ZERO_CSUM6_RX | \
						 VXLAN_F_LOCALBYPASS)

static bool mlxsw_sp_nve_vxlan_ipv4_flags_check(const struct vxlan_config *cfg,
						struct netlink_ext_ack *extack)
{
	if (!(cfg->flags & VXLAN_F_UDP_ZERO_CSUM_TX)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Zero UDP checksum must be allowed for TX");
		return false;
	}

	if (cfg->flags & ~MLXSW_SP_NVE_VXLAN_IPV4_SUPPORTED_FLAGS) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Unsupported flag");
		return false;
	}

	return true;
}

static bool mlxsw_sp_nve_vxlan_ipv6_flags_check(const struct vxlan_config *cfg,
						struct netlink_ext_ack *extack)
{
	if (!(cfg->flags & VXLAN_F_UDP_ZERO_CSUM6_TX)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Zero UDP checksum must be allowed for TX");
		return false;
	}

	if (!(cfg->flags & VXLAN_F_UDP_ZERO_CSUM6_RX)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Zero UDP checksum must be allowed for RX");
		return false;
	}

	if (cfg->flags & ~MLXSW_SP_NVE_VXLAN_IPV6_SUPPORTED_FLAGS) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Unsupported flag");
		return false;
	}

	return true;
}

static bool mlxsw_sp_nve_vxlan_can_offload(const struct mlxsw_sp_nve *nve,
					   const struct mlxsw_sp_nve_params *params,
					   struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(params->dev);
	struct vxlan_config *cfg = &vxlan->cfg;

	if (vxlan_addr_multicast(&cfg->remote_ip)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Multicast destination IP is not supported");
		return false;
	}

	if (vxlan_addr_any(&cfg->saddr)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Source address must be specified");
		return false;
	}

	if (cfg->remote_ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Local interface is not supported");
		return false;
	}

	if (cfg->port_min || cfg->port_max) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Only default UDP source port range is supported");
		return false;
	}

	if (cfg->tos != 1) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: TOS must be configured to inherit");
		return false;
	}

	if (cfg->flags & VXLAN_F_TTL_INHERIT) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: TTL must not be configured to inherit");
		return false;
	}

	switch (cfg->saddr.sa.sa_family) {
	case AF_INET:
		if (!mlxsw_sp_nve_vxlan_ipv4_flags_check(cfg, extack))
			return false;
		break;
	case AF_INET6:
		if (!mlxsw_sp_nve_vxlan_ipv6_flags_check(cfg, extack))
			return false;
		break;
	}

	if (cfg->ttl == 0) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: TTL must not be configured to 0");
		return false;
	}

	if (cfg->label != 0) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: Flow label must be configured to 0");
		return false;
	}

	return true;
}

static bool mlxsw_sp1_nve_vxlan_can_offload(const struct mlxsw_sp_nve *nve,
					    const struct mlxsw_sp_nve_params *params,
					    struct netlink_ext_ack *extack)
{
	if (params->ethertype == ETH_P_8021AD) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: 802.1ad bridge is not supported with VxLAN");
		return false;
	}

	return mlxsw_sp_nve_vxlan_can_offload(nve, params, extack);
}

static void
mlxsw_sp_nve_vxlan_ul_proto_sip_config(const struct vxlan_config *cfg,
				       struct mlxsw_sp_nve_config *config)
{
	switch (cfg->saddr.sa.sa_family) {
	case AF_INET:
		config->ul_proto = MLXSW_SP_L3_PROTO_IPV4;
		config->ul_sip.addr4 = cfg->saddr.sin.sin_addr.s_addr;
		break;
	case AF_INET6:
		config->ul_proto = MLXSW_SP_L3_PROTO_IPV6;
		config->ul_sip.addr6 = cfg->saddr.sin6.sin6_addr;
		break;
	}
}

static void mlxsw_sp_nve_vxlan_config(const struct mlxsw_sp_nve *nve,
				      const struct mlxsw_sp_nve_params *params,
				      struct mlxsw_sp_nve_config *config)
{
	struct vxlan_dev *vxlan = netdev_priv(params->dev);
	struct vxlan_config *cfg = &vxlan->cfg;

	config->type = MLXSW_SP_NVE_TYPE_VXLAN;
	config->ttl = cfg->ttl;
	config->flowlabel = cfg->label;
	config->learning_en = cfg->flags & VXLAN_F_LEARN ? 1 : 0;
	config->ul_tb_id = RT_TABLE_MAIN;
	mlxsw_sp_nve_vxlan_ul_proto_sip_config(cfg, config);
	config->udp_dport = cfg->dst_port;
}

static void
mlxsw_sp_nve_vxlan_config_prepare(char *tngcr_pl,
				  const struct mlxsw_sp_nve_config *config)
{
	struct in6_addr addr6;
	u8 udp_sport;

	mlxsw_reg_tngcr_pack(tngcr_pl, MLXSW_REG_TNGCR_TYPE_VXLAN, true,
			     config->ttl);
	/* VxLAN driver's default UDP source port range is 32768 (0x8000)
	 * to 60999 (0xee47). Set the upper 8 bits of the UDP source port
	 * to a random number between 0x80 and 0xee
	 */
	get_random_bytes(&udp_sport, sizeof(udp_sport));
	udp_sport = (udp_sport % (0xee - 0x80 + 1)) + 0x80;
	mlxsw_reg_tngcr_nve_udp_sport_prefix_set(tngcr_pl, udp_sport);

	switch (config->ul_proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_reg_tngcr_usipv4_set(tngcr_pl,
					   be32_to_cpu(config->ul_sip.addr4));
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		addr6 = config->ul_sip.addr6;
		mlxsw_reg_tngcr_usipv6_memcpy_to(tngcr_pl,
						 (const char *)&addr6);
		break;
	}
}

static int
mlxsw_sp1_nve_vxlan_config_set(struct mlxsw_sp *mlxsw_sp,
			       const struct mlxsw_sp_nve_config *config)
{
	char tngcr_pl[MLXSW_REG_TNGCR_LEN];
	u16 ul_vr_id;
	int err;

	err = mlxsw_sp_router_tb_id_vr_id(mlxsw_sp, config->ul_tb_id,
					  &ul_vr_id);
	if (err)
		return err;

	mlxsw_sp_nve_vxlan_config_prepare(tngcr_pl, config);
	mlxsw_reg_tngcr_learn_enable_set(tngcr_pl, config->learning_en);
	mlxsw_reg_tngcr_underlay_virtual_router_set(tngcr_pl, ul_vr_id);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tngcr), tngcr_pl);
}

static void mlxsw_sp1_nve_vxlan_config_clear(struct mlxsw_sp *mlxsw_sp)
{
	char tngcr_pl[MLXSW_REG_TNGCR_LEN];

	mlxsw_reg_tngcr_pack(tngcr_pl, MLXSW_REG_TNGCR_TYPE_VXLAN, false, 0);

	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tngcr), tngcr_pl);
}

static int mlxsw_sp1_nve_vxlan_rtdp_set(struct mlxsw_sp *mlxsw_sp,
					unsigned int tunnel_index)
{
	char rtdp_pl[MLXSW_REG_RTDP_LEN];

	mlxsw_reg_rtdp_pack(rtdp_pl, MLXSW_REG_RTDP_TYPE_NVE, tunnel_index);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtdp), rtdp_pl);
}

static int mlxsw_sp1_nve_vxlan_init(struct mlxsw_sp_nve *nve,
				    const struct mlxsw_sp_nve_config *config)
{
	struct mlxsw_sp *mlxsw_sp = nve->mlxsw_sp;
	int err;

	err = mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, config->udp_dport);
	if (err)
		return err;

	err = mlxsw_sp_parsing_depth_inc(mlxsw_sp);
	if (err)
		goto err_parsing_depth_inc;

	err = mlxsw_sp1_nve_vxlan_config_set(mlxsw_sp, config);
	if (err)
		goto err_config_set;

	err = mlxsw_sp1_nve_vxlan_rtdp_set(mlxsw_sp, nve->tunnel_index);
	if (err)
		goto err_rtdp_set;

	err = mlxsw_sp_router_nve_promote_decap(mlxsw_sp, config->ul_tb_id,
						config->ul_proto,
						&config->ul_sip,
						nve->tunnel_index);
	if (err)
		goto err_promote_decap;

	return 0;

err_promote_decap:
err_rtdp_set:
	mlxsw_sp1_nve_vxlan_config_clear(mlxsw_sp);
err_config_set:
	mlxsw_sp_parsing_depth_dec(mlxsw_sp);
err_parsing_depth_inc:
	mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, 0);
	return err;
}

static void mlxsw_sp1_nve_vxlan_fini(struct mlxsw_sp_nve *nve)
{
	struct mlxsw_sp_nve_config *config = &nve->config;
	struct mlxsw_sp *mlxsw_sp = nve->mlxsw_sp;

	mlxsw_sp_router_nve_demote_decap(mlxsw_sp, config->ul_tb_id,
					 config->ul_proto, &config->ul_sip);
	mlxsw_sp1_nve_vxlan_config_clear(mlxsw_sp);
	mlxsw_sp_parsing_depth_dec(mlxsw_sp);
	mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, 0);
}

static int
mlxsw_sp_nve_vxlan_fdb_replay(const struct net_device *nve_dev, __be32 vni,
			      struct netlink_ext_ack *extack)
{
	if (WARN_ON(!netif_is_vxlan(nve_dev)))
		return -EINVAL;
	return vxlan_fdb_replay(nve_dev, vni, &mlxsw_sp_switchdev_notifier,
				extack);
}

static void
mlxsw_sp_nve_vxlan_clear_offload(const struct net_device *nve_dev, __be32 vni)
{
	if (WARN_ON(!netif_is_vxlan(nve_dev)))
		return;
	vxlan_fdb_clear_offload(nve_dev, vni);
}

const struct mlxsw_sp_nve_ops mlxsw_sp1_nve_vxlan_ops = {
	.type		= MLXSW_SP_NVE_TYPE_VXLAN,
	.can_offload	= mlxsw_sp1_nve_vxlan_can_offload,
	.nve_config	= mlxsw_sp_nve_vxlan_config,
	.init		= mlxsw_sp1_nve_vxlan_init,
	.fini		= mlxsw_sp1_nve_vxlan_fini,
	.fdb_replay	= mlxsw_sp_nve_vxlan_fdb_replay,
	.fdb_clear_offload = mlxsw_sp_nve_vxlan_clear_offload,
};

static int mlxsw_sp2_nve_vxlan_learning_set(struct mlxsw_sp *mlxsw_sp,
					    bool learning_en)
{
	char tnpc_pl[MLXSW_REG_TNPC_LEN];

	mlxsw_reg_tnpc_pack(tnpc_pl, MLXSW_REG_TUNNEL_PORT_NVE,
			    learning_en);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tnpc), tnpc_pl);
}

static int
mlxsw_sp2_nve_decap_ethertype_set(struct mlxsw_sp *mlxsw_sp)
{
	char spvid_pl[MLXSW_REG_SPVID_LEN] = {};

	mlxsw_reg_spvid_tport_set(spvid_pl, true);
	mlxsw_reg_spvid_local_port_set(spvid_pl,
				       MLXSW_REG_TUNNEL_PORT_NVE);
	mlxsw_reg_spvid_egr_et_set_set(spvid_pl, true);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvid), spvid_pl);
}

static int
mlxsw_sp2_nve_vxlan_config_set(struct mlxsw_sp *mlxsw_sp,
			       const struct mlxsw_sp_nve_config *config)
{
	char tngcr_pl[MLXSW_REG_TNGCR_LEN];
	char spvtr_pl[MLXSW_REG_SPVTR_LEN];
	u16 ul_rif_index;
	int err;

	err = mlxsw_sp_router_ul_rif_get(mlxsw_sp, config->ul_tb_id,
					 &ul_rif_index);
	if (err)
		return err;
	mlxsw_sp->nve->ul_rif_index = ul_rif_index;

	err = mlxsw_sp2_nve_vxlan_learning_set(mlxsw_sp, config->learning_en);
	if (err)
		goto err_vxlan_learning_set;

	mlxsw_sp_nve_vxlan_config_prepare(tngcr_pl, config);
	mlxsw_reg_tngcr_underlay_rif_set(tngcr_pl, ul_rif_index);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tngcr), tngcr_pl);
	if (err)
		goto err_tngcr_write;

	mlxsw_reg_spvtr_pack(spvtr_pl, true, MLXSW_REG_TUNNEL_PORT_NVE,
			     MLXSW_REG_SPVTR_IPVID_MODE_ALWAYS_PUSH_VLAN);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvtr), spvtr_pl);
	if (err)
		goto err_spvtr_write;

	err = mlxsw_sp2_nve_decap_ethertype_set(mlxsw_sp);
	if (err)
		goto err_decap_ethertype_set;

	return 0;

err_decap_ethertype_set:
	mlxsw_reg_spvtr_pack(spvtr_pl, true, MLXSW_REG_TUNNEL_PORT_NVE,
			     MLXSW_REG_SPVTR_IPVID_MODE_IEEE_COMPLIANT_PVID);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvtr), spvtr_pl);
err_spvtr_write:
	mlxsw_reg_tngcr_pack(tngcr_pl, MLXSW_REG_TNGCR_TYPE_VXLAN, false, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tngcr), tngcr_pl);
err_tngcr_write:
	mlxsw_sp2_nve_vxlan_learning_set(mlxsw_sp, false);
err_vxlan_learning_set:
	mlxsw_sp_router_ul_rif_put(mlxsw_sp, ul_rif_index);
	return err;
}

static void mlxsw_sp2_nve_vxlan_config_clear(struct mlxsw_sp *mlxsw_sp)
{
	char spvtr_pl[MLXSW_REG_SPVTR_LEN];
	char tngcr_pl[MLXSW_REG_TNGCR_LEN];

	mlxsw_reg_spvtr_pack(spvtr_pl, true, MLXSW_REG_TUNNEL_PORT_NVE,
			     MLXSW_REG_SPVTR_IPVID_MODE_IEEE_COMPLIANT_PVID);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvtr), spvtr_pl);
	mlxsw_reg_tngcr_pack(tngcr_pl, MLXSW_REG_TNGCR_TYPE_VXLAN, false, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tngcr), tngcr_pl);
	mlxsw_sp2_nve_vxlan_learning_set(mlxsw_sp, false);
	mlxsw_sp_router_ul_rif_put(mlxsw_sp, mlxsw_sp->nve->ul_rif_index);
}

static int mlxsw_sp2_nve_vxlan_rtdp_set(struct mlxsw_sp *mlxsw_sp,
					unsigned int tunnel_index,
					u16 ul_rif_index)
{
	char rtdp_pl[MLXSW_REG_RTDP_LEN];

	mlxsw_reg_rtdp_pack(rtdp_pl, MLXSW_REG_RTDP_TYPE_NVE, tunnel_index);
	mlxsw_reg_rtdp_egress_router_interface_set(rtdp_pl, ul_rif_index);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtdp), rtdp_pl);
}

static int mlxsw_sp2_nve_vxlan_init(struct mlxsw_sp_nve *nve,
				    const struct mlxsw_sp_nve_config *config)
{
	struct mlxsw_sp *mlxsw_sp = nve->mlxsw_sp;
	int err;

	err = mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, config->udp_dport);
	if (err)
		return err;

	err = mlxsw_sp_parsing_depth_inc(mlxsw_sp);
	if (err)
		goto err_parsing_depth_inc;

	err = mlxsw_sp2_nve_vxlan_config_set(mlxsw_sp, config);
	if (err)
		goto err_config_set;

	err = mlxsw_sp2_nve_vxlan_rtdp_set(mlxsw_sp, nve->tunnel_index,
					   nve->ul_rif_index);
	if (err)
		goto err_rtdp_set;

	err = mlxsw_sp_router_nve_promote_decap(mlxsw_sp, config->ul_tb_id,
						config->ul_proto,
						&config->ul_sip,
						nve->tunnel_index);
	if (err)
		goto err_promote_decap;

	return 0;

err_promote_decap:
err_rtdp_set:
	mlxsw_sp2_nve_vxlan_config_clear(mlxsw_sp);
err_config_set:
	mlxsw_sp_parsing_depth_dec(mlxsw_sp);
err_parsing_depth_inc:
	mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, 0);
	return err;
}

static void mlxsw_sp2_nve_vxlan_fini(struct mlxsw_sp_nve *nve)
{
	struct mlxsw_sp_nve_config *config = &nve->config;
	struct mlxsw_sp *mlxsw_sp = nve->mlxsw_sp;

	mlxsw_sp_router_nve_demote_decap(mlxsw_sp, config->ul_tb_id,
					 config->ul_proto, &config->ul_sip);
	mlxsw_sp2_nve_vxlan_config_clear(mlxsw_sp);
	mlxsw_sp_parsing_depth_dec(mlxsw_sp);
	mlxsw_sp_parsing_vxlan_udp_dport_set(mlxsw_sp, 0);
}

const struct mlxsw_sp_nve_ops mlxsw_sp2_nve_vxlan_ops = {
	.type		= MLXSW_SP_NVE_TYPE_VXLAN,
	.can_offload	= mlxsw_sp_nve_vxlan_can_offload,
	.nve_config	= mlxsw_sp_nve_vxlan_config,
	.init		= mlxsw_sp2_nve_vxlan_init,
	.fini		= mlxsw_sp2_nve_vxlan_fini,
	.fdb_replay	= mlxsw_sp_nve_vxlan_fdb_replay,
	.fdb_clear_offload = mlxsw_sp_nve_vxlan_clear_offload,
};

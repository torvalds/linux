/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef AM65_CPSW_QOS_H_
#define AM65_CPSW_QOS_H_

#include <linux/netdevice.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

struct am65_cpsw_common;
struct am65_cpsw_port;

struct am65_cpsw_est {
	int buf;
	/* has to be the last one */
	struct tc_taprio_qopt_offload taprio;
};

struct am65_cpsw_mqprio {
	struct tc_mqprio_qopt_offload mqprio_hw;
	u64 max_rate_total;

	unsigned enable:1;
	unsigned shaper_en:1;
	unsigned shaper_susp:1;
	unsigned tc0_q:3;
};

struct am65_cpsw_ale_ratelimit {
	unsigned long cookie;
	u64 rate_packet_ps;
};

struct am65_cpsw_qos {
	struct am65_cpsw_est *est_admin;
	struct am65_cpsw_est *est_oper;
	ktime_t link_down_time;
	int link_speed;
	struct am65_cpsw_mqprio mqprio;

	struct am65_cpsw_ale_ratelimit ale_bc_ratelimit;
	struct am65_cpsw_ale_ratelimit ale_mc_ratelimit;
};

int am65_cpsw_qos_ndo_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			       void *type_data);
void am65_cpsw_qos_link_up(struct net_device *ndev, int link_speed);
void am65_cpsw_qos_link_down(struct net_device *ndev);
void am65_cpsw_qos_mqprio_init(struct am65_cpsw_port *port);
int am65_cpsw_qos_ndo_tx_p0_set_maxrate(struct net_device *ndev, int queue, u32 rate_mbps);
void am65_cpsw_qos_tx_p0_rate_init(struct am65_cpsw_common *common);

#endif /* AM65_CPSW_QOS_H_ */

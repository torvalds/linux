/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef AM65_CPSW_QOS_H_
#define AM65_CPSW_QOS_H_

#include <linux/netdevice.h>
#include <net/pkt_sched.h>

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
	bool shaper_en;
};

struct am65_cpsw_iet {
	u8 preemptible_tcs;
	u32 original_max_blks;
	int verify_time_ms;
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
	struct am65_cpsw_iet iet;

	struct am65_cpsw_ale_ratelimit ale_bc_ratelimit;
	struct am65_cpsw_ale_ratelimit ale_mc_ratelimit;
};

#define AM65_CPSW_REG_CTL			0x004
#define AM65_CPSW_PN_REG_CTL			0x004
#define AM65_CPSW_PN_REG_FIFO_STATUS		0x050
#define AM65_CPSW_PN_REG_EST_CTL		0x060
#define AM65_CPSW_PN_REG_PRI_CIR(pri)		(0x140 + 4 * (pri))
#define AM65_CPSW_P0_REG_PRI_EIR(pri)		(0x160 + 4 * (pri))

#define AM65_CPSW_PN_REG_CTL			0x004
#define AM65_CPSW_PN_REG_TX_PRI_MAP		0x018
#define AM65_CPSW_PN_REG_RX_PRI_MAP		0x020
#define AM65_CPSW_PN_REG_FIFO_STATUS		0x050
#define AM65_CPSW_PN_REG_EST_CTL		0x060
#define AM65_CPSW_PN_REG_PRI_CIR(pri)		(0x140 + 4 * (pri))
#define AM65_CPSW_PN_REG_PRI_EIR(pri)		(0x160 + 4 * (pri))

/* AM65_CPSW_REG_CTL register fields */
#define AM65_CPSW_CTL_EST_EN			BIT(18)

/* AM65_CPSW_PN_REG_CTL register fields */
#define AM65_CPSW_PN_CTL_EST_PORT_EN		BIT(17)

/* AM65_CPSW_PN_REG_EST_CTL register fields */
#define AM65_CPSW_PN_EST_ONEBUF			BIT(0)
#define AM65_CPSW_PN_EST_BUFSEL			BIT(1)
#define AM65_CPSW_PN_EST_TS_EN			BIT(2)
#define AM65_CPSW_PN_EST_TS_FIRST		BIT(3)
#define AM65_CPSW_PN_EST_ONEPRI			BIT(4)
#define AM65_CPSW_PN_EST_TS_PRI_MSK		GENMASK(7, 5)

/* AM65_CPSW_PN_REG_FIFO_STATUS register fields */
#define AM65_CPSW_PN_FST_TX_PRI_ACTIVE_MSK	GENMASK(7, 0)
#define AM65_CPSW_PN_FST_TX_E_MAC_ALLOW_MSK	GENMASK(15, 8)
#define AM65_CPSW_PN_FST_EST_CNT_ERR		BIT(16)
#define AM65_CPSW_PN_FST_EST_ADD_ERR		BIT(17)
#define AM65_CPSW_PN_FST_EST_BUFACT		BIT(18)

/* EST FETCH COMMAND RAM */
#define AM65_CPSW_FETCH_RAM_CMD_NUM		0x80
#define AM65_CPSW_FETCH_CNT_MSK			GENMASK(21, 8)
#define AM65_CPSW_FETCH_CNT_MAX			(AM65_CPSW_FETCH_CNT_MSK >> 8)
#define AM65_CPSW_FETCH_CNT_OFFSET		8
#define AM65_CPSW_FETCH_ALLOW_MSK		GENMASK(7, 0)
#define AM65_CPSW_FETCH_ALLOW_MAX		AM65_CPSW_FETCH_ALLOW_MSK

/* number of priority queues per port FIFO */
#define AM65_CPSW_PN_FIFO_PRIO_NUM		8

#if IS_ENABLED(CONFIG_TI_AM65_CPSW_QOS)
int am65_cpsw_qos_ndo_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			       void *type_data);
void am65_cpsw_qos_link_up(struct net_device *ndev, int link_speed);
void am65_cpsw_qos_link_down(struct net_device *ndev);
int am65_cpsw_qos_ndo_tx_p0_set_maxrate(struct net_device *ndev, int queue, u32 rate_mbps);
void am65_cpsw_qos_tx_p0_rate_init(struct am65_cpsw_common *common);
void am65_cpsw_iet_commit_preemptible_tcs(struct am65_cpsw_port *port);
void am65_cpsw_iet_common_enable(struct am65_cpsw_common *common);
#else
static inline int am65_cpsw_qos_ndo_setup_tc(struct net_device *ndev,
					     enum tc_setup_type type,
					     void *type_data)
{
	return -EOPNOTSUPP;
}

static inline void am65_cpsw_qos_link_up(struct net_device *ndev,
					 int link_speed)
{ }

static inline void am65_cpsw_qos_link_down(struct net_device *ndev)
{ }

static inline int am65_cpsw_qos_ndo_tx_p0_set_maxrate(struct net_device *ndev,
						      int queue,
						      u32 rate_mbps)
{
	return 0;
}

static inline void am65_cpsw_qos_tx_p0_rate_init(struct am65_cpsw_common *common)
{ }
static inline void am65_cpsw_iet_commit_preemptible_tcs(struct am65_cpsw_port *port)
{ }
static inline void am65_cpsw_iet_common_enable(struct am65_cpsw_common *common)
{ }
#endif

#define AM65_CPSW_REG_CTL			0x004
#define AM65_CPSW_PN_REG_CTL			0x004
#define AM65_CPSW_PN_REG_MAX_BLKS		0x008
#define AM65_CPSW_PN_REG_TX_PRI_MAP		0x018
#define AM65_CPSW_PN_REG_RX_PRI_MAP		0x020
#define AM65_CPSW_PN_REG_IET_CTRL		0x040
#define AM65_CPSW_PN_REG_IET_STATUS		0x044
#define AM65_CPSW_PN_REG_IET_VERIFY		0x048
#define AM65_CPSW_PN_REG_FIFO_STATUS		0x050
#define AM65_CPSW_PN_REG_EST_CTL		0x060
#define AM65_CPSW_PN_REG_PRI_CIR(pri)		(0x140 + 4 * (pri))
#define AM65_CPSW_PN_REG_PRI_EIR(pri)		(0x160 + 4 * (pri))

/* AM65_CPSW_REG_CTL register fields */
#define AM65_CPSW_CTL_IET_EN			BIT(17)
#define AM65_CPSW_CTL_EST_EN			BIT(18)

/* AM65_CPSW_PN_REG_CTL register fields */
#define AM65_CPSW_PN_CTL_IET_PORT_EN		BIT(16)
#define AM65_CPSW_PN_CTL_EST_PORT_EN		BIT(17)

/* AM65_CPSW_PN_REG_EST_CTL register fields */
#define AM65_CPSW_PN_EST_ONEBUF			BIT(0)
#define AM65_CPSW_PN_EST_BUFSEL			BIT(1)
#define AM65_CPSW_PN_EST_TS_EN			BIT(2)
#define AM65_CPSW_PN_EST_TS_FIRST		BIT(3)
#define AM65_CPSW_PN_EST_ONEPRI			BIT(4)
#define AM65_CPSW_PN_EST_TS_PRI_MSK		GENMASK(7, 5)

/* AM65_CPSW_PN_REG_IET_CTRL register fields */
#define AM65_CPSW_PN_IET_MAC_PENABLE		BIT(0)
#define AM65_CPSW_PN_IET_MAC_DISABLEVERIFY	BIT(2)
#define AM65_CPSW_PN_IET_MAC_LINKFAIL		BIT(3)
#define AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_MASK	GENMASK(10, 8)
#define AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_OFFSET	8
#define AM65_CPSW_PN_IET_MAC_PREMPT_MASK		GENMASK(23, 16)
#define AM65_CPSW_PN_IET_MAC_PREMPT_OFFSET		16

#define AM65_CPSW_PN_IET_MAC_SET_ADDFRAGSIZE(n)	(((n) << AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_OFFSET) & \
						  AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_MASK)
#define AM65_CPSW_PN_IET_MAC_GET_ADDFRAGSIZE(n)	(((n) & AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_MASK) >> \
						  AM65_CPSW_PN_IET_MAC_MAC_ADDFRAGSIZE_OFFSET)
#define AM65_CPSW_PN_IET_MAC_SET_PREEMPT(n)	(((n) << AM65_CPSW_PN_IET_MAC_PREMPT_OFFSET) & \
						 AM65_CPSW_PN_IET_MAC_PREMPT_MASK)
#define AM65_CPSW_PN_IET_MAC_GET_PREEMPT(n)	(((n) & AM65_CPSW_PN_IET_MAC_PREMPT_MASK) >> \
						 AM65_CPSW_PN_IET_MAC_PREMPT_OFFSET)

/* AM65_CPSW_PN_REG_IET_STATUS register fields */
#define AM65_CPSW_PN_MAC_STATUS			GENMASK(3, 0)
#define AM65_CPSW_PN_MAC_VERIFIED		BIT(0)
#define AM65_CPSW_PN_MAC_VERIFY_FAIL		BIT(1)
#define AM65_CPSW_PN_MAC_RESPOND_ERR		BIT(2)
#define AM65_CPSW_PN_MAC_VERIFY_ERR		BIT(3)

/* AM65_CPSW_PN_REG_IET_VERIFY register fields */
#define AM65_CPSW_PN_MAC_VERIFY_CNT_MASK	GENMASK(23, 0)
#define AM65_CPSW_PN_MAC_GET_VERIFY_CNT(n)	((n) & AM65_CPSW_PN_MAC_VERIFY_CNT_MASK)
/* 10 msec converted to NSEC */
#define AM65_CPSW_IET_VERIFY_CNT_MS		(10)
#define AM65_CPSW_IET_VERIFY_CNT_NS		(AM65_CPSW_IET_VERIFY_CNT_MS * \
						 NSEC_PER_MSEC)

/* AM65_CPSW_PN_REG_FIFO_STATUS register fields */
#define AM65_CPSW_PN_FST_TX_PRI_ACTIVE_MSK	GENMASK(7, 0)
#define AM65_CPSW_PN_FST_TX_E_MAC_ALLOW_MSK	GENMASK(15, 8)
#define AM65_CPSW_PN_FST_EST_CNT_ERR		BIT(16)
#define AM65_CPSW_PN_FST_EST_ADD_ERR		BIT(17)
#define AM65_CPSW_PN_FST_EST_BUFACT		BIT(18)

/* EST FETCH COMMAND RAM */
#define AM65_CPSW_FETCH_RAM_CMD_NUM		0x80
#define AM65_CPSW_FETCH_CNT_MSK			GENMASK(21, 8)
#define AM65_CPSW_FETCH_CNT_MAX			(AM65_CPSW_FETCH_CNT_MSK >> 8)
#define AM65_CPSW_FETCH_CNT_OFFSET		8
#define AM65_CPSW_FETCH_ALLOW_MSK		GENMASK(7, 0)
#define AM65_CPSW_FETCH_ALLOW_MAX		AM65_CPSW_FETCH_ALLOW_MSK

/* AM65_CPSW_PN_REG_MAX_BLKS fields for IET and No IET cases */
/* 7 blocks for pn_rx_max_blks, 13 for pn_tx_max_blks*/
#define AM65_CPSW_PN_TX_RX_MAX_BLKS_IET		0xD07

/* Slave IET Stats. register offsets */
#define AM65_CPSW_STATN_IET_RX_ASSEMBLY_ERROR	0x140
#define AM65_CPSW_STATN_IET_RX_ASSEMBLY_OK	0x144
#define AM65_CPSW_STATN_IET_RX_SMD_ERROR	0x148
#define AM65_CPSW_STATN_IET_RX_FRAG		0x14c
#define AM65_CPSW_STATN_IET_TX_HOLD		0x150
#define AM65_CPSW_STATN_IET_TX_FRAG		0x154

/* number of priority queues per port FIFO */
#define AM65_CPSW_PN_FIFO_PRIO_NUM		8

#endif /* AM65_CPSW_QOS_H_ */

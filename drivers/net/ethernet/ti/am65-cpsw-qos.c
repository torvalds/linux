// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments K3 AM65 Ethernet QoS submodule
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 * quality of service module includes:
 * Enhanced Scheduler Traffic (EST - P802.1Qbv/D2.2)
 */

#include <linux/pm_runtime.h>
#include <linux/time.h>
#include <net/pkt_cls.h>

#include "am65-cpsw-nuss.h"
#include "am65-cpsw-qos.h"
#include "am65-cpts.h"
#include "cpsw_ale.h"

#define AM65_CPSW_REG_CTL			0x004
#define AM65_CPSW_PN_REG_CTL			0x004
#define AM65_CPSW_PN_REG_FIFO_STATUS		0x050
#define AM65_CPSW_PN_REG_EST_CTL		0x060
#define AM65_CPSW_PN_REG_PRI_CIR(pri)		(0x140 + 4 * (pri))

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

enum timer_act {
	TACT_PROG,		/* need program timer */
	TACT_NEED_STOP,		/* need stop first */
	TACT_SKIP_PROG,		/* just buffer can be updated */
};

static int am65_cpsw_port_est_enabled(struct am65_cpsw_port *port)
{
	return port->qos.est_oper || port->qos.est_admin;
}

static void am65_cpsw_est_enable(struct am65_cpsw_common *common, int enable)
{
	u32 val;

	val = readl(common->cpsw_base + AM65_CPSW_REG_CTL);

	if (enable)
		val |= AM65_CPSW_CTL_EST_EN;
	else
		val &= ~AM65_CPSW_CTL_EST_EN;

	writel(val, common->cpsw_base + AM65_CPSW_REG_CTL);
	common->est_enabled = enable;
}

static void am65_cpsw_port_est_enable(struct am65_cpsw_port *port, int enable)
{
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_CTL);
	if (enable)
		val |= AM65_CPSW_PN_CTL_EST_PORT_EN;
	else
		val &= ~AM65_CPSW_PN_CTL_EST_PORT_EN;

	writel(val, port->port_base + AM65_CPSW_PN_REG_CTL);
}

/* target new EST RAM buffer, actual toggle happens after cycle completion */
static void am65_cpsw_port_est_assign_buf_num(struct net_device *ndev,
					      int buf_num)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_EST_CTL);
	if (buf_num)
		val |= AM65_CPSW_PN_EST_BUFSEL;
	else
		val &= ~AM65_CPSW_PN_EST_BUFSEL;

	writel(val, port->port_base + AM65_CPSW_PN_REG_EST_CTL);
}

/* am65_cpsw_port_est_is_swapped() - Indicate if h/w is transitioned
 * admin -> oper or not
 *
 * Return true if already transitioned. i.e oper is equal to admin and buf
 * numbers match (est_oper->buf match with est_admin->buf).
 * false if before transition. i.e oper is not equal to admin, (i.e a
 * previous admin command is waiting to be transitioned to oper state
 * and est_oper->buf not match with est_oper->buf).
 */
static int am65_cpsw_port_est_is_swapped(struct net_device *ndev, int *oper,
					 int *admin)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_FIFO_STATUS);
	*oper = !!(val & AM65_CPSW_PN_FST_EST_BUFACT);

	val = readl(port->port_base + AM65_CPSW_PN_REG_EST_CTL);
	*admin = !!(val & AM65_CPSW_PN_EST_BUFSEL);

	return *admin == *oper;
}

/* am65_cpsw_port_est_get_free_buf_num() - Get free buffer number for
 * Admin to program the new schedule.
 *
 * Logic as follows:-
 * If oper is same as admin, return the other buffer (!oper) as the admin
 * buffer.  If oper is not the same, driver let the current oper to continue
 * as it is in the process of transitioning from admin -> oper. So keep the
 * oper by selecting the same oper buffer by writing to EST_BUFSEL bit in
 * EST CTL register. In the second iteration they will match and code returns.
 * The actual buffer to write command is selected later before it is ready
 * to update the schedule.
 */
static int am65_cpsw_port_est_get_free_buf_num(struct net_device *ndev)
{
	int oper, admin;
	int roll = 2;

	while (roll--) {
		if (am65_cpsw_port_est_is_swapped(ndev, &oper, &admin))
			return !oper;

		/* admin is not set, so hinder transition as it's not allowed
		 * to touch memory in-flight, by targeting same oper buf.
		 */
		am65_cpsw_port_est_assign_buf_num(ndev, oper);

		dev_info(&ndev->dev,
			 "Prev. EST admin cycle is in transit %d -> %d\n",
			 oper, admin);
	}

	return admin;
}

static void am65_cpsw_admin_to_oper(struct net_device *ndev)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	devm_kfree(&ndev->dev, port->qos.est_oper);

	port->qos.est_oper = port->qos.est_admin;
	port->qos.est_admin = NULL;
}

static void am65_cpsw_port_est_get_buf_num(struct net_device *ndev,
					   struct am65_cpsw_est *est_new)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 val;

	val = readl(port->port_base + AM65_CPSW_PN_REG_EST_CTL);
	val &= ~AM65_CPSW_PN_EST_ONEBUF;
	writel(val, port->port_base + AM65_CPSW_PN_REG_EST_CTL);

	est_new->buf = am65_cpsw_port_est_get_free_buf_num(ndev);

	/* rolled buf num means changed buf while configuring */
	if (port->qos.est_oper && port->qos.est_admin &&
	    est_new->buf == port->qos.est_oper->buf)
		am65_cpsw_admin_to_oper(ndev);
}

static void am65_cpsw_est_set(struct net_device *ndev, int enable)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_common *common = port->common;
	int common_enable = 0;
	int i;

	am65_cpsw_port_est_enable(port, enable);

	for (i = 0; i < common->port_num; i++)
		common_enable |= am65_cpsw_port_est_enabled(&common->ports[i]);

	common_enable |= enable;
	am65_cpsw_est_enable(common, common_enable);
}

/* This update is supposed to be used in any routine before getting real state
 * of admin -> oper transition, particularly it's supposed to be used in some
 * generic routine for providing real state to Taprio Qdisc.
 */
static void am65_cpsw_est_update_state(struct net_device *ndev)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int oper, admin;

	if (!port->qos.est_admin)
		return;

	if (!am65_cpsw_port_est_is_swapped(ndev, &oper, &admin))
		return;

	am65_cpsw_admin_to_oper(ndev);
}

/* Fetch command count it's number of bytes in Gigabit mode or nibbles in
 * 10/100Mb mode. So, having speed and time in ns, recalculate ns to number of
 * bytes/nibbles that can be sent while transmission on given speed.
 */
static int am65_est_cmd_ns_to_cnt(u64 ns, int link_speed)
{
	u64 temp;

	temp = ns * link_speed;
	if (link_speed < SPEED_1000)
		temp <<= 1;

	return DIV_ROUND_UP(temp, 8 * 1000);
}

static void __iomem *am65_cpsw_est_set_sched_cmds(void __iomem *addr,
						  int fetch_cnt,
						  int fetch_allow)
{
	u32 prio_mask, cmd_fetch_cnt, cmd;

	do {
		if (fetch_cnt > AM65_CPSW_FETCH_CNT_MAX) {
			fetch_cnt -= AM65_CPSW_FETCH_CNT_MAX;
			cmd_fetch_cnt = AM65_CPSW_FETCH_CNT_MAX;
		} else {
			cmd_fetch_cnt = fetch_cnt;
			/* fetch count can't be less than 16? */
			if (cmd_fetch_cnt && cmd_fetch_cnt < 16)
				cmd_fetch_cnt = 16;

			fetch_cnt = 0;
		}

		prio_mask = fetch_allow & AM65_CPSW_FETCH_ALLOW_MSK;
		cmd = (cmd_fetch_cnt << AM65_CPSW_FETCH_CNT_OFFSET) | prio_mask;

		writel(cmd, addr);
		addr += 4;
	} while (fetch_cnt);

	return addr;
}

static int am65_cpsw_est_calc_cmd_num(struct net_device *ndev,
				      struct tc_taprio_qopt_offload *taprio,
				      int link_speed)
{
	int i, cmd_cnt, cmd_sum = 0;
	u32 fetch_cnt;

	for (i = 0; i < taprio->num_entries; i++) {
		if (taprio->entries[i].command != TC_TAPRIO_CMD_SET_GATES) {
			dev_err(&ndev->dev, "Only SET command is supported");
			return -EINVAL;
		}

		fetch_cnt = am65_est_cmd_ns_to_cnt(taprio->entries[i].interval,
						   link_speed);

		cmd_cnt = DIV_ROUND_UP(fetch_cnt, AM65_CPSW_FETCH_CNT_MAX);
		if (!cmd_cnt)
			cmd_cnt++;

		cmd_sum += cmd_cnt;

		if (!fetch_cnt)
			break;
	}

	return cmd_sum;
}

static int am65_cpsw_est_check_scheds(struct net_device *ndev,
				      struct am65_cpsw_est *est_new)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int cmd_num;

	cmd_num = am65_cpsw_est_calc_cmd_num(ndev, &est_new->taprio,
					     port->qos.link_speed);
	if (cmd_num < 0)
		return cmd_num;

	if (cmd_num > AM65_CPSW_FETCH_RAM_CMD_NUM / 2) {
		dev_err(&ndev->dev, "No fetch RAM");
		return -ENOMEM;
	}

	return 0;
}

static void am65_cpsw_est_set_sched_list(struct net_device *ndev,
					 struct am65_cpsw_est *est_new)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 fetch_cnt, fetch_allow, all_fetch_allow = 0;
	void __iomem *ram_addr, *max_ram_addr;
	struct tc_taprio_sched_entry *entry;
	int i, ram_size;

	ram_addr = port->fetch_ram_base;
	ram_size = AM65_CPSW_FETCH_RAM_CMD_NUM * 2;
	ram_addr += est_new->buf * ram_size;

	max_ram_addr = ram_size + ram_addr;
	for (i = 0; i < est_new->taprio.num_entries; i++) {
		entry = &est_new->taprio.entries[i];

		fetch_cnt = am65_est_cmd_ns_to_cnt(entry->interval,
						   port->qos.link_speed);
		fetch_allow = entry->gate_mask;
		if (fetch_allow > AM65_CPSW_FETCH_ALLOW_MAX)
			dev_dbg(&ndev->dev, "fetch_allow > 8 bits: %d\n",
				fetch_allow);

		ram_addr = am65_cpsw_est_set_sched_cmds(ram_addr, fetch_cnt,
							fetch_allow);

		if (!fetch_cnt && i < est_new->taprio.num_entries - 1) {
			dev_info(&ndev->dev,
				 "next scheds after %d have no impact", i + 1);
			break;
		}

		all_fetch_allow |= fetch_allow;
	}

	/* end cmd, enabling non-timed queues for potential over cycle time */
	if (ram_addr < max_ram_addr)
		writel(~all_fetch_allow & AM65_CPSW_FETCH_ALLOW_MSK, ram_addr);
}

/*
 * Enable ESTf periodic output, set cycle start time and interval.
 */
static int am65_cpsw_timer_set(struct net_device *ndev,
			       struct am65_cpsw_est *est_new)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_common *common = port->common;
	struct am65_cpts *cpts = common->cpts;
	struct am65_cpts_estf_cfg cfg;

	cfg.ns_period = est_new->taprio.cycle_time;
	cfg.ns_start = est_new->taprio.base_time;

	return am65_cpts_estf_enable(cpts, port->port_id - 1, &cfg);
}

static void am65_cpsw_timer_stop(struct net_device *ndev)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpts *cpts = port->common->cpts;

	am65_cpts_estf_disable(cpts, port->port_id - 1);
}

static enum timer_act am65_cpsw_timer_act(struct net_device *ndev,
					  struct am65_cpsw_est *est_new)
{
	struct tc_taprio_qopt_offload *taprio_oper, *taprio_new;
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpts *cpts = port->common->cpts;
	u64 cur_time;
	s64 diff;

	if (!port->qos.est_oper)
		return TACT_PROG;

	taprio_new = &est_new->taprio;
	taprio_oper = &port->qos.est_oper->taprio;

	if (taprio_new->cycle_time != taprio_oper->cycle_time)
		return TACT_NEED_STOP;

	/* in order to avoid timer reset get base_time form oper taprio */
	if (!taprio_new->base_time && taprio_oper)
		taprio_new->base_time = taprio_oper->base_time;

	if (taprio_new->base_time == taprio_oper->base_time)
		return TACT_SKIP_PROG;

	/* base times are cycle synchronized */
	diff = taprio_new->base_time - taprio_oper->base_time;
	diff = diff < 0 ? -diff : diff;
	if (diff % taprio_new->cycle_time)
		return TACT_NEED_STOP;

	cur_time = am65_cpts_ns_gettime(cpts);
	if (taprio_new->base_time <= cur_time + taprio_new->cycle_time)
		return TACT_SKIP_PROG;

	/* TODO: Admin schedule at future time is not currently supported */
	return TACT_NEED_STOP;
}

static void am65_cpsw_stop_est(struct net_device *ndev)
{
	am65_cpsw_est_set(ndev, 0);
	am65_cpsw_timer_stop(ndev);
}

static void am65_cpsw_purge_est(struct net_device *ndev)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	am65_cpsw_stop_est(ndev);

	devm_kfree(&ndev->dev, port->qos.est_admin);
	devm_kfree(&ndev->dev, port->qos.est_oper);

	port->qos.est_oper = NULL;
	port->qos.est_admin = NULL;
}

static int am65_cpsw_configure_taprio(struct net_device *ndev,
				      struct am65_cpsw_est *est_new)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpts *cpts = common->cpts;
	int ret = 0, tact = TACT_PROG;

	am65_cpsw_est_update_state(ndev);

	if (est_new->taprio.cmd == TAPRIO_CMD_DESTROY) {
		am65_cpsw_stop_est(ndev);
		return ret;
	}

	ret = am65_cpsw_est_check_scheds(ndev, est_new);
	if (ret < 0)
		return ret;

	tact = am65_cpsw_timer_act(ndev, est_new);
	if (tact == TACT_NEED_STOP) {
		dev_err(&ndev->dev,
			"Can't toggle estf timer, stop taprio first");
		return -EINVAL;
	}

	if (tact == TACT_PROG)
		am65_cpsw_timer_stop(ndev);

	if (!est_new->taprio.base_time)
		est_new->taprio.base_time = am65_cpts_ns_gettime(cpts);

	am65_cpsw_port_est_get_buf_num(ndev, est_new);
	am65_cpsw_est_set_sched_list(ndev, est_new);
	am65_cpsw_port_est_assign_buf_num(ndev, est_new->buf);

	am65_cpsw_est_set(ndev, est_new->taprio.cmd == TAPRIO_CMD_REPLACE);

	if (tact == TACT_PROG) {
		ret = am65_cpsw_timer_set(ndev, est_new);
		if (ret) {
			dev_err(&ndev->dev, "Failed to set cycle time");
			return ret;
		}
	}

	return ret;
}

static void am65_cpsw_cp_taprio(struct tc_taprio_qopt_offload *from,
				struct tc_taprio_qopt_offload *to)
{
	int i;

	*to = *from;
	for (i = 0; i < from->num_entries; i++)
		to->entries[i] = from->entries[i];
}

static int am65_cpsw_set_taprio(struct net_device *ndev, void *type_data)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct tc_taprio_qopt_offload *taprio = type_data;
	struct am65_cpsw_est *est_new;
	int ret = 0;

	if (taprio->cycle_time_extension) {
		dev_err(&ndev->dev, "Failed to set cycle time extension");
		return -EOPNOTSUPP;
	}

	est_new = devm_kzalloc(&ndev->dev,
			       struct_size(est_new, taprio.entries, taprio->num_entries),
			       GFP_KERNEL);
	if (!est_new)
		return -ENOMEM;

	am65_cpsw_cp_taprio(taprio, &est_new->taprio);
	ret = am65_cpsw_configure_taprio(ndev, est_new);
	if (!ret) {
		if (taprio->cmd == TAPRIO_CMD_REPLACE) {
			devm_kfree(&ndev->dev, port->qos.est_admin);

			port->qos.est_admin = est_new;
		} else {
			devm_kfree(&ndev->dev, est_new);
			am65_cpsw_purge_est(ndev);
		}
	} else {
		devm_kfree(&ndev->dev, est_new);
	}

	return ret;
}

static void am65_cpsw_est_link_up(struct net_device *ndev, int link_speed)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	ktime_t cur_time;
	s64 delta;

	port->qos.link_speed = link_speed;
	if (!am65_cpsw_port_est_enabled(port))
		return;

	if (port->qos.link_down_time) {
		cur_time = ktime_get();
		delta = ktime_us_delta(cur_time, port->qos.link_down_time);
		if (delta > USEC_PER_SEC) {
			dev_err(&ndev->dev,
				"Link has been lost too long, stopping TAS");
			goto purge_est;
		}
	}

	return;

purge_est:
	am65_cpsw_purge_est(ndev);
}

static int am65_cpsw_setup_taprio(struct net_device *ndev, void *type_data)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct tc_taprio_qopt_offload *taprio = type_data;
	struct am65_cpsw_common *common = port->common;

	if (taprio->cmd != TAPRIO_CMD_REPLACE &&
	    taprio->cmd != TAPRIO_CMD_DESTROY)
		return -EOPNOTSUPP;

	if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_TAS))
		return -ENODEV;

	if (!netif_running(ndev)) {
		dev_err(&ndev->dev, "interface is down, link speed unknown\n");
		return -ENETDOWN;
	}

	if (common->pf_p0_rx_ptype_rrobin) {
		dev_err(&ndev->dev,
			"p0-rx-ptype-rrobin flag conflicts with taprio qdisc\n");
		return -EINVAL;
	}

	if (port->qos.link_speed == SPEED_UNKNOWN)
		return -ENOLINK;

	return am65_cpsw_set_taprio(ndev, type_data);
}

static int am65_cpsw_tc_query_caps(struct net_device *ndev, void *type_data)
{
	struct tc_query_caps_base *base = type_data;

	switch (base->type) {
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_TAS))
			return -EOPNOTSUPP;

		caps->gate_mask_per_txq = true;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static int am65_cpsw_qos_clsflower_add_policer(struct am65_cpsw_port *port,
					       struct netlink_ext_ack *extack,
					       struct flow_cls_offload *cls,
					       u64 rate_pkt_ps)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	static const u8 mc_mac[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct am65_cpsw_qos *qos = &port->qos;
	struct flow_match_eth_addrs match;
	int ret;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		NL_SET_ERR_MSG_MOD(extack, "Not matching on eth address");
		return -EOPNOTSUPP;
	}

	flow_rule_match_eth_addrs(rule, &match);

	if (!is_zero_ether_addr(match.mask->src)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on source MAC not supported");
		return -EOPNOTSUPP;
	}

	if (is_broadcast_ether_addr(match.key->dst) &&
	    is_broadcast_ether_addr(match.mask->dst)) {
		ret = cpsw_ale_rx_ratelimit_bc(port->common->ale, port->port_id, rate_pkt_ps);
		if (ret)
			return ret;

		qos->ale_bc_ratelimit.cookie = cls->cookie;
		qos->ale_bc_ratelimit.rate_packet_ps = rate_pkt_ps;
	} else if (ether_addr_equal_unaligned(match.key->dst, mc_mac) &&
		   ether_addr_equal_unaligned(match.mask->dst, mc_mac)) {
		ret = cpsw_ale_rx_ratelimit_mc(port->common->ale, port->port_id, rate_pkt_ps);
		if (ret)
			return ret;

		qos->ale_mc_ratelimit.cookie = cls->cookie;
		qos->ale_mc_ratelimit.rate_packet_ps = rate_pkt_ps;
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Not supported matching key");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int am65_cpsw_qos_clsflower_policer_validate(const struct flow_action *action,
						    const struct flow_action_entry *act,
						    struct netlink_ext_ack *extack)
{
	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	if (act->police.rate_bytes_ps || act->police.peakrate_bytes_ps ||
	    act->police.avrate || act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when bytes per second/peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int am65_cpsw_qos_configure_clsflower(struct am65_cpsw_port *port,
					     struct flow_cls_offload *cls)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	const struct flow_action_entry *act;
	int i, ret;

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_POLICE:
			ret = am65_cpsw_qos_clsflower_policer_validate(&rule->action, act, extack);
			if (ret)
				return ret;

			return am65_cpsw_qos_clsflower_add_policer(port, extack, cls,
								   act->police.rate_pkt_ps);
		default:
			NL_SET_ERR_MSG_MOD(extack,
					   "Action not supported");
			return -EOPNOTSUPP;
		}
	}
	return -EOPNOTSUPP;
}

static int am65_cpsw_qos_delete_clsflower(struct am65_cpsw_port *port, struct flow_cls_offload *cls)
{
	struct am65_cpsw_qos *qos = &port->qos;

	if (cls->cookie == qos->ale_bc_ratelimit.cookie) {
		qos->ale_bc_ratelimit.cookie = 0;
		qos->ale_bc_ratelimit.rate_packet_ps = 0;
		cpsw_ale_rx_ratelimit_bc(port->common->ale, port->port_id, 0);
	}

	if (cls->cookie == qos->ale_mc_ratelimit.cookie) {
		qos->ale_mc_ratelimit.cookie = 0;
		qos->ale_mc_ratelimit.rate_packet_ps = 0;
		cpsw_ale_rx_ratelimit_mc(port->common->ale, port->port_id, 0);
	}

	return 0;
}

static int am65_cpsw_qos_setup_tc_clsflower(struct am65_cpsw_port *port,
					    struct flow_cls_offload *cls_flower)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return am65_cpsw_qos_configure_clsflower(port, cls_flower);
	case FLOW_CLS_DESTROY:
		return am65_cpsw_qos_delete_clsflower(port, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}

static int am65_cpsw_qos_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct am65_cpsw_port *port = cb_priv;

	if (!tc_cls_can_offload_and_chain0(port->ndev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return am65_cpsw_qos_setup_tc_clsflower(port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(am65_cpsw_qos_block_cb_list);

static int am65_cpsw_qos_setup_tc_block(struct net_device *ndev, struct flow_block_offload *f)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	return flow_block_cb_setup_simple(f, &am65_cpsw_qos_block_cb_list,
					  am65_cpsw_qos_setup_tc_block_cb,
					  port, port, true);
}

int am65_cpsw_qos_ndo_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			       void *type_data)
{
	switch (type) {
	case TC_QUERY_CAPS:
		return am65_cpsw_tc_query_caps(ndev, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return am65_cpsw_setup_taprio(ndev, type_data);
	case TC_SETUP_BLOCK:
		return am65_cpsw_qos_setup_tc_block(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

void am65_cpsw_qos_link_up(struct net_device *ndev, int link_speed)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_TAS))
		return;

	am65_cpsw_est_link_up(ndev, link_speed);
	port->qos.link_down_time = 0;
}

void am65_cpsw_qos_link_down(struct net_device *ndev)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	if (!IS_ENABLED(CONFIG_TI_AM65_CPSW_TAS))
		return;

	if (!port->qos.link_down_time)
		port->qos.link_down_time = ktime_get();

	port->qos.link_speed = SPEED_UNKNOWN;
}

static u32
am65_cpsw_qos_tx_rate_calc(u32 rate_mbps, unsigned long bus_freq)
{
	u32 ir;

	bus_freq /= 1000000;
	ir = DIV_ROUND_UP(((u64)rate_mbps * 32768),  bus_freq);
	return ir;
}

static void
am65_cpsw_qos_tx_p0_rate_apply(struct am65_cpsw_common *common,
			       int tx_ch, u32 rate_mbps)
{
	struct am65_cpsw_host *host = am65_common_get_host(common);
	u32 ch_cir;
	int i;

	ch_cir = am65_cpsw_qos_tx_rate_calc(rate_mbps, common->bus_freq);
	writel(ch_cir, host->port_base + AM65_CPSW_PN_REG_PRI_CIR(tx_ch));

	/* update rates for every port tx queues */
	for (i = 0; i < common->port_num; i++) {
		struct net_device *ndev = common->ports[i].ndev;

		if (!ndev)
			continue;
		netdev_get_tx_queue(ndev, tx_ch)->tx_maxrate = rate_mbps;
	}
}

int am65_cpsw_qos_ndo_tx_p0_set_maxrate(struct net_device *ndev,
					int queue, u32 rate_mbps)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct am65_cpsw_common *common = port->common;
	struct am65_cpsw_tx_chn *tx_chn;
	u32 ch_rate, tx_ch_rate_msk_new;
	u32 ch_msk = 0;
	int ret;

	dev_dbg(common->dev, "apply TX%d rate limiting %uMbps tx_rate_msk%x\n",
		queue, rate_mbps, common->tx_ch_rate_msk);

	if (common->pf_p0_rx_ptype_rrobin) {
		dev_err(common->dev, "TX Rate Limiting failed - rrobin mode\n");
		return -EINVAL;
	}

	ch_rate = netdev_get_tx_queue(ndev, queue)->tx_maxrate;
	if (ch_rate == rate_mbps)
		return 0;

	ret = pm_runtime_get_sync(common->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(common->dev);
		return ret;
	}
	ret = 0;

	tx_ch_rate_msk_new = common->tx_ch_rate_msk;
	if (rate_mbps && !(tx_ch_rate_msk_new & BIT(queue))) {
		tx_ch_rate_msk_new |= BIT(queue);
		ch_msk = GENMASK(common->tx_ch_num - 1, queue);
		ch_msk = tx_ch_rate_msk_new ^ ch_msk;
	} else if (!rate_mbps) {
		tx_ch_rate_msk_new &= ~BIT(queue);
		ch_msk = queue ? GENMASK(queue - 1, 0) : 0;
		ch_msk = tx_ch_rate_msk_new & ch_msk;
	}

	if (ch_msk) {
		dev_err(common->dev, "TX rate limiting has to be enabled sequentially hi->lo tx_rate_msk:%x tx_rate_msk_new:%x\n",
			common->tx_ch_rate_msk, tx_ch_rate_msk_new);
		ret = -EINVAL;
		goto exit_put;
	}

	tx_chn = &common->tx_chns[queue];
	tx_chn->rate_mbps = rate_mbps;
	common->tx_ch_rate_msk = tx_ch_rate_msk_new;

	if (!common->usage_count)
		/* will be applied on next netif up */
		goto exit_put;

	am65_cpsw_qos_tx_p0_rate_apply(common, queue, rate_mbps);

exit_put:
	pm_runtime_put(common->dev);
	return ret;
}

void am65_cpsw_qos_tx_p0_rate_init(struct am65_cpsw_common *common)
{
	struct am65_cpsw_host *host = am65_common_get_host(common);
	int tx_ch;

	for (tx_ch = 0; tx_ch < common->tx_ch_num; tx_ch++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[tx_ch];
		u32 ch_cir;

		if (!tx_chn->rate_mbps)
			continue;

		ch_cir = am65_cpsw_qos_tx_rate_calc(tx_chn->rate_mbps,
						    common->bus_freq);
		writel(ch_cir,
		       host->port_base + AM65_CPSW_PN_REG_PRI_CIR(tx_ch));
	}
}

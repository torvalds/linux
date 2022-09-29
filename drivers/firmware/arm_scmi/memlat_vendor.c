// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_memlat.h>
#include "common.h"

#define MAX_MAP_ENTRIES 14

#define SCMI_VENDOR_MSG_START   (3)
#define SCMI_VENDOR_MSG_MODULE_START   (16)
#define SCMI_MAX_RX_SIZE	128
#define SCMI_MAX_GET_DATA_SIZE	124
#define INVALID_IDX		0xFF

enum scmi_memlat_protocol_cmd {
	MEMLAT_SET_LOG_LEVEL = SCMI_VENDOR_MSG_START,
	MEMLAT_FLUSH_LOGBUF,
	MEMLAT_SET_MEM_GROUP = SCMI_VENDOR_MSG_MODULE_START,
	MEMLAT_SET_MONITOR,
	MEMLAT_SET_COMMON_EV_MAP,
	MEMLAT_SET_GRP_EV_MAP,
	MEMLAT_ADAPTIVE_LOW_FREQ,
	MEMLAT_ADAPTIVE_HIGH_FREQ,
	MEMLAT_GET_ADAPTIVE_CUR_FREQ,
	MEMLAT_IPM_CEIL,
	MEMLAT_FE_STALL_FLOOR,
	MEMLAT_BE_STALL_FLOOR,
	MEMLAT_WB_PCT,
	MEMLAT_IPM_FILTER,
	MEMLAT_FREQ_SCALE_PCT,
	MEMLAT_FREQ_SCALE_LIMIT_MHZ,
	MEMLAT_SAMPLE_MS,
	MEMLAT_MON_FREQ_MAP,
	MEMLAT_SET_MIN_FREQ,
	MEMLAT_SET_MAX_FREQ,
	MEMLAT_GET_CUR_FREQ,
	MEMLAT_START_TIMER,
	MEMLAT_STOP_TIMER,
	MEMLAT_GET_TIMESTAMP,
	MEMLAT_MAX_MSG
};

struct node_msg {
	uint32_t cpumask;
	uint32_t hw_type;
	uint32_t mon_type;
	uint32_t mon_idx;
	char mon_name[MAX_NAME_LEN];
};

struct scalar_param_msg {
	uint32_t hw_type;
	uint32_t mon_idx;
	uint32_t val;
};

struct map_table {
	uint32_t v1;
	uint32_t v2;
};

struct map_param_msg {
	uint32_t hw_type;
	uint32_t mon_idx;
	uint32_t nr_rows;
	struct map_table tbl[MAX_MAP_ENTRIES];
};

struct ev_map_msg {
	uint32_t num_evs;
	uint32_t hw_type;
	uint8_t cid[MAX_EV_CNTRS];
};

static int scmi_set_ev_map(const struct scmi_protocol_handle *ph, u32 hw_type,
			   void *buf, u32 msg_id, u32 num_evs)
{
	int ret, i = 0;
	struct scmi_xfer *t;
	struct ev_map_msg *msg;
	uint8_t *src = buf;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg), sizeof(*msg),
				      &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->num_evs = cpu_to_le32(num_evs);
	msg->hw_type = cpu_to_le32(hw_type);

	for (i = 0; i < num_evs; i++)
		msg->cid[i] = src[i];

	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_set_grp_map(const struct scmi_protocol_handle *ph, u32 hw_type,
			void *buf, u32 num_evs)
{
	return scmi_set_ev_map(ph, hw_type, buf, MEMLAT_SET_GRP_EV_MAP, num_evs);
}

static int scmi_set_common_map(const struct scmi_protocol_handle *ph, void *buf, u32 num_evs)
{
	return scmi_set_ev_map(ph, INVALID_IDX, buf, MEMLAT_SET_COMMON_EV_MAP, num_evs);
}

static int scmi_set_memgrp_mon(const struct scmi_protocol_handle *ph,
			       u32 cpus_mpidr, u32 hw_type, u32 mon_type,
			       u32 mon_idx, const char *mon_name, u32 msg_id)
{
	int ret = 0;
	struct scmi_xfer *t;
	struct node_msg *msg;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg), sizeof(*msg),
				      &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->cpumask = cpu_to_le32(cpus_mpidr);
	msg->hw_type = cpu_to_le32(hw_type);
	msg->mon_type = cpu_to_le32(mon_type);
	msg->mon_idx = cpu_to_le32(mon_idx);
	if (mon_name)
		snprintf(msg->mon_name, MAX_NAME_LEN, mon_name);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_set_mon(const struct scmi_protocol_handle *ph, u32 cpus_mpidr,
			u32 hw_type, u32 mon_type, u32 mon_idx, const char *mon_name)
{
	return scmi_set_memgrp_mon(ph, cpus_mpidr, hw_type, mon_type,
				   mon_idx, mon_name, MEMLAT_SET_MONITOR);
}

static int scmi_set_mem_grp(const struct scmi_protocol_handle *ph,
			    u32 cpus_mpidr, u32 hw_type)
{
	return scmi_set_memgrp_mon(ph, cpus_mpidr, hw_type, 0,
				   0, NULL, MEMLAT_SET_MEM_GROUP);
}

static int scmi_freq_map(const struct scmi_protocol_handle *ph, u32 hw_type,
			 u32 mon_idx, u32 nr_rows, void *buf)
{
	int ret, i = 0;
	struct scmi_xfer *t;
	struct map_param_msg *msg;
	struct map_table *tbl, *src = buf;

	if (nr_rows > MAX_MAP_ENTRIES)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, MEMLAT_MON_FREQ_MAP, sizeof(*msg),
				      sizeof(*msg), &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->hw_type = cpu_to_le32(hw_type);
	msg->mon_idx = cpu_to_le32(mon_idx);
	msg->nr_rows = cpu_to_le32(nr_rows);
	tbl = msg->tbl;

	for (i = 0; i < nr_rows; i++) {
		tbl[i].v1 = cpu_to_le32(src[i].v1);
		tbl[i].v2 = cpu_to_le32(src[i].v2);
	}
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_set_tunable(const struct scmi_protocol_handle *ph,
			    u32 hw_type, u32 msg_id, u32 mon_idx, u32 val)
{
	int ret = 0;
	struct scmi_xfer *t;
	struct scalar_param_msg *msg;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg),
				      sizeof(*msg), &t);
	if (ret)
		return ret;
	msg = t->tx.buf;
	msg->hw_type = cpu_to_le32(hw_type);
	msg->mon_idx = cpu_to_le32(mon_idx);
	msg->val = cpu_to_le32(val);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

#define scmi_send_cmd(name, _msg_id)					\
static int scmi_##name(const struct scmi_protocol_handle *ph,		\
		       u32 hw_type, u32 mon_idx, u32 val)		\
{									\
	return scmi_set_tunable(ph, hw_type, _msg_id, mon_idx, val);	\
}									\

scmi_send_cmd(ipm_ceil, MEMLAT_IPM_CEIL);
scmi_send_cmd(fe_stall_floor, MEMLAT_FE_STALL_FLOOR);
scmi_send_cmd(be_stall_floor, MEMLAT_BE_STALL_FLOOR);
scmi_send_cmd(wb_pct_thres, MEMLAT_WB_PCT);
scmi_send_cmd(wb_filter_ipm, MEMLAT_IPM_FILTER);
scmi_send_cmd(freq_scale_pct, MEMLAT_FREQ_SCALE_PCT);
scmi_send_cmd(freq_scale_limit_mhz, MEMLAT_FREQ_SCALE_LIMIT_MHZ);
scmi_send_cmd(min_freq, MEMLAT_SET_MIN_FREQ);
scmi_send_cmd(max_freq, MEMLAT_SET_MAX_FREQ);
scmi_send_cmd(adaptive_low_freq, MEMLAT_ADAPTIVE_LOW_FREQ);
scmi_send_cmd(adaptive_high_freq, MEMLAT_ADAPTIVE_HIGH_FREQ);

static int scmi_send_start_stop(const struct scmi_protocol_handle *ph, u32 msg_id)
{
	int ret = 0;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, msg_id, 0, 0, &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_stop_timer(const struct scmi_protocol_handle *ph)
{
	return scmi_send_start_stop(ph, MEMLAT_STOP_TIMER);
}

static int scmi_start_timer(const struct scmi_protocol_handle *ph)
{
	return scmi_send_start_stop(ph, MEMLAT_START_TIMER);
}

static int scmi_flush_cpucp_log(const struct scmi_protocol_handle *ph)
{
	return scmi_send_start_stop(ph, MEMLAT_FLUSH_LOGBUF);
}

static int scmi_set_global_var(const struct scmi_protocol_handle *ph, u32 val, u32 msg_id)
{
	int ret = 0;
	struct scmi_xfer *t;
	u32 *ptr;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(u32), sizeof(u32), &t);
	if (ret)
		return ret;
	ptr = (u32 *)t->tx.buf;
	*ptr = cpu_to_le32(val);
	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_set_log_level(const struct scmi_protocol_handle *ph, u32 val)
{
	return scmi_set_global_var(ph, val, MEMLAT_SET_LOG_LEVEL);
}

static int scmi_set_sample_ms(const struct scmi_protocol_handle *ph, u32 val)
{
	return scmi_set_global_var(ph, val, MEMLAT_SAMPLE_MS);
}

static int scmi_get_timestamp(const struct scmi_protocol_handle *ph, void *buf)
{
	int ret = 0;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, MEMLAT_GET_TIMESTAMP, sizeof(u32),
				      SCMI_MAX_RX_SIZE, &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	if (t->rx.len != sizeof(u64))
		return -EMSGSIZE;

	memcpy(buf, t->rx.buf, t->rx.len);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_get_freq(const struct scmi_protocol_handle *ph, uint32_t hw_type,
			 uint32_t mon_idx, void *buf, uint32_t msg_id)
{
	int ret = 0;
	struct scmi_xfer *t;
	struct scalar_param_msg *msg;

	ret = ph->xops->xfer_get_init(ph, msg_id, sizeof(*msg),
				      SCMI_MAX_RX_SIZE, &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->hw_type = cpu_to_le32(hw_type);
	msg->mon_idx = cpu_to_le32(mon_idx);
	ret = ph->xops->do_xfer(ph, t);
	if (t->rx.len != sizeof(u32))
		return -EMSGSIZE;

	memcpy(buf, t->rx.buf, t->rx.len);
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_get_cur_freq(const struct scmi_protocol_handle *ph, uint32_t hw_type,
			     uint32_t mon_idx, void *buf)
{
	return scmi_get_freq(ph, hw_type, mon_idx, buf, MEMLAT_GET_CUR_FREQ);
}

static int scmi_get_adaptive_cur_freq(const struct scmi_protocol_handle *ph, uint32_t hw_type,
				  uint32_t mon_idx, void *buf)
{
	return scmi_get_freq(ph, hw_type, mon_idx, buf, MEMLAT_GET_ADAPTIVE_CUR_FREQ);
}

static struct scmi_memlat_vendor_ops memlat_proto_ops = {
	.set_mem_grp = scmi_set_mem_grp,
	.freq_map = scmi_freq_map,
	.set_mon = scmi_set_mon,
	.set_common_ev_map = scmi_set_common_map,
	.set_grp_ev_map = scmi_set_grp_map,
	.adaptive_low_freq = scmi_adaptive_low_freq,
	.adaptive_high_freq = scmi_adaptive_high_freq,
	.get_adaptive_cur_freq = scmi_get_adaptive_cur_freq,
	.ipm_ceil = scmi_ipm_ceil,
	.fe_stall_floor = scmi_fe_stall_floor,
	.be_stall_floor = scmi_be_stall_floor,
	.sample_ms = scmi_set_sample_ms,
	.wb_filter_ipm = scmi_wb_filter_ipm,
	.wb_pct_thres = scmi_wb_pct_thres,
	.freq_scale_pct = scmi_freq_scale_pct,
	.freq_scale_limit_mhz = scmi_freq_scale_limit_mhz,
	.min_freq = scmi_min_freq,
	.max_freq = scmi_max_freq,
	.get_cur_freq = scmi_get_cur_freq,
	.start_timer = scmi_start_timer,
	.stop_timer = scmi_stop_timer,
	.set_log_level = scmi_set_log_level,
	.flush_cpucp_log = scmi_flush_cpucp_log,
	.get_timestamp = scmi_get_timestamp,
};

static int scmi_memlat_vendor_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;

	ph->xops->version_get(ph, &version);

	dev_dbg(ph->dev, "memlat version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	return 0;
}

static const struct scmi_protocol scmi_memlat_vendor = {
	.id = SCMI_PROTOCOL_MEMLAT,
	.owner = THIS_MODULE,
	.instance_init = &scmi_memlat_vendor_protocol_init,
	.ops = &memlat_proto_ops,
};
module_scmi_protocol(scmi_memlat_vendor);

MODULE_DESCRIPTION("SCMI memlat vendor Protocol");
MODULE_LICENSE("GPL");

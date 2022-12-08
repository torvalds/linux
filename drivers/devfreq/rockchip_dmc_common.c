// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip dmc common functions.
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/module.h>
#include <soc/rockchip/rockchip_dmc.h>

#define msch_rl_to_dmcfreq(work) container_of(to_delayed_work(work), \
					      struct rockchip_dmcfreq, \
					      msch_rl_work)
#define MSCH_RL_DELAY_TIME	50 /* ms */

static struct dmcfreq_common_info *common_info;
static DECLARE_RWSEM(rockchip_dmcfreq_sem);

void rockchip_dmcfreq_lock(void)
{
	down_read(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_lock);

void rockchip_dmcfreq_lock_nested(void)
{
	down_read_nested(&rockchip_dmcfreq_sem, SINGLE_DEPTH_NESTING);
}
EXPORT_SYMBOL(rockchip_dmcfreq_lock_nested);

void rockchip_dmcfreq_unlock(void)
{
	up_read(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_unlock);

int rockchip_dmcfreq_write_trylock(void)
{
	return down_write_trylock(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_write_trylock);

void rockchip_dmcfreq_write_unlock(void)
{
	up_write(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_write_unlock);

static void set_msch_rl(unsigned int readlatency)

{
	rockchip_dmcfreq_lock();
	dev_dbg(common_info->dev, "rl 0x%x -> 0x%x\n",
		common_info->read_latency, readlatency);
	if (!common_info->set_msch_readlatency(readlatency))
		common_info->read_latency = readlatency;
	else
		dev_err(common_info->dev, "failed to set msch rl\n");
	rockchip_dmcfreq_unlock();
}

static void set_msch_rl_work(struct work_struct *work)
{
	set_msch_rl(0);
	common_info->is_msch_rl_work_started = false;
}

int rockchip_dmcfreq_vop_bandwidth_init(struct dmcfreq_common_info *info)
{
	if (info->set_msch_readlatency)
		INIT_DELAYED_WORK(&info->msch_rl_work, set_msch_rl_work);
	common_info = info;

	return 0;
}
EXPORT_SYMBOL(rockchip_dmcfreq_vop_bandwidth_init);

void rockchip_dmcfreq_vop_bandwidth_update(struct dmcfreq_vop_info *vop_info)
{
	unsigned long vop_last_rate, target = 0;
	unsigned int readlatency = 0;
	int i;

	if (!common_info)
		return;

	dev_dbg(common_info->dev, "line bw=%u, frame bw=%u, pn=%u, pn_4k=%u\n",
		vop_info->line_bw_mbyte, vop_info->frame_bw_mbyte,
		vop_info->plane_num, vop_info->plane_num_4k);

	if (!common_info->vop_pn_rl_tbl || !common_info->set_msch_readlatency)
		goto vop_bw_tbl;
	for (i = 0; common_info->vop_pn_rl_tbl[i].rl != DMCFREQ_TABLE_END; i++) {
		if (vop_info->plane_num >= common_info->vop_pn_rl_tbl[i].pn)
			readlatency = common_info->vop_pn_rl_tbl[i].rl;
	}
	dev_dbg(common_info->dev, "pn=%u\n", vop_info->plane_num);
	if (readlatency) {
		cancel_delayed_work_sync(&common_info->msch_rl_work);
		common_info->is_msch_rl_work_started = false;
		if (common_info->read_latency != readlatency)
			set_msch_rl(readlatency);
	} else if (common_info->read_latency &&
		   !common_info->is_msch_rl_work_started) {
		common_info->is_msch_rl_work_started = true;
		schedule_delayed_work(&common_info->msch_rl_work,
				      msecs_to_jiffies(MSCH_RL_DELAY_TIME));
	}

vop_bw_tbl:
	if (!common_info->auto_freq_en || !common_info->vop_bw_tbl)
		goto vop_frame_bw_tbl;

	for (i = 0; common_info->vop_bw_tbl[i].freq != DMCFREQ_TABLE_END; i++) {
		if (vop_info->line_bw_mbyte >= common_info->vop_bw_tbl[i].min)
			target = common_info->vop_bw_tbl[i].freq;
	}

vop_frame_bw_tbl:
	if (!common_info->auto_freq_en || !common_info->vop_frame_bw_tbl)
		goto next;
	for (i = 0; common_info->vop_frame_bw_tbl[i].freq != DMCFREQ_TABLE_END;
	     i++) {
		if (vop_info->frame_bw_mbyte >= common_info->vop_frame_bw_tbl[i].min) {
			if (target < common_info->vop_frame_bw_tbl[i].freq)
				target = common_info->vop_frame_bw_tbl[i].freq;
		}
	}

next:
	if (vop_info->plane_num_4k && target < common_info->vop_4k_rate)
		target = common_info->vop_4k_rate;

	vop_last_rate = common_info->vop_req_rate;
	common_info->vop_req_rate = target;

	if (target > vop_last_rate) {
		mutex_lock(&common_info->devfreq->lock);
		update_devfreq(common_info->devfreq);
		mutex_unlock(&common_info->devfreq->lock);
	}
}
EXPORT_SYMBOL(rockchip_dmcfreq_vop_bandwidth_update);

int rockchip_dmcfreq_vop_bandwidth_request(struct dmcfreq_vop_info *vop_info)
{
	unsigned long target = 0;
	int i;

	if (!common_info || !common_info->auto_freq_en ||
	    !common_info->vop_bw_tbl)
		return 0;

	for (i = 0; common_info->vop_bw_tbl[i].freq != DMCFREQ_TABLE_END; i++) {
		if (vop_info->line_bw_mbyte <= common_info->vop_bw_tbl[i].max) {
			target = common_info->vop_bw_tbl[i].freq;
			break;
		}
	}

	if (!target)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(rockchip_dmcfreq_vop_bandwidth_request);

MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("rockchip dmcfreq driver with devfreq framework");
MODULE_LICENSE("GPL v2");

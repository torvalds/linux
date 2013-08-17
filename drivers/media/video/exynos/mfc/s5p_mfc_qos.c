/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_qos.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include <plat/cpu.h>

#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_reg.h"

#ifdef CONFIG_MFC_USE_BUS_DEVFREQ
enum {
	MFC_QOS_ADD,
	MFC_QOS_UPDATE,
	MFC_QOS_REMOVE,
};

void mfc_qos_operate(struct s5p_mfc_ctx *ctx, int opr_type, int idx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_platdata *pdata = dev->pdata;
	struct s5p_mfc_qos *qos_table = pdata->qos_table;
	struct s5p_mfc_pm *pm = &dev->pm;
	int ret;

	switch (opr_type) {
	case MFC_QOS_ADD:
		ret = clk_enable(pm->clock);
		if (ret < 0)
			mfc_err("Failed to enable clock\n");

		pm_qos_add_request(&dev->qos_req_int,
				PM_QOS_DEVICE_THROUGHPUT,
				qos_table[idx].freq_int);
		pm_qos_add_request(&dev->qos_req_mif,
				PM_QOS_BUS_THROUGHPUT,
				qos_table[idx].freq_mif);

		if (!ret)
			clk_disable(pm->clock);

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		pm_qos_add_request(&dev->qos_req_cpu,
				PM_QOS_CPU_FREQ_MIN,
				qos_table[idx].freq_cpu);
#endif

		atomic_set(&dev->qos_req_cur, idx + 1);
		mfc_debug(5, "QoS request: %d\n", idx + 1);
		break;
	case MFC_QOS_UPDATE:
		ret = clk_enable(pm->clock);
		if (ret < 0)
			mfc_err("Failed to enable clock\n");

		pm_qos_update_request(&dev->qos_req_int,
				qos_table[idx].freq_int);
		pm_qos_update_request(&dev->qos_req_mif,
				qos_table[idx].freq_mif);

		if (!ret)
			clk_disable(pm->clock);

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		pm_qos_update_request(&dev->qos_req_cpu,
				qos_table[idx].freq_cpu);
#endif

		atomic_set(&dev->qos_req_cur, idx + 1);
		mfc_debug(5, "QoS update: %d\n", idx + 1);
		break;
	case MFC_QOS_REMOVE:
		ret = clk_enable(pm->clock);
		if (ret < 0)
			mfc_err("Failed to enable clock\n");

		pm_qos_remove_request(&dev->qos_req_int);
		pm_qos_remove_request(&dev->qos_req_mif);

		if (!ret)
			clk_disable(pm->clock);

#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		pm_qos_remove_request(&dev->qos_req_cpu);
#endif

		atomic_set(&dev->qos_req_cur, 0);
		mfc_debug(5, "QoS remove\n");
		break;
	default:
		mfc_err("Unknown request for opr [%d]\n", opr_type);
		break;
	}
}

void mfc_qos_add_or_update(struct s5p_mfc_ctx *ctx, int total_mb)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_platdata *pdata = dev->pdata;
	struct s5p_mfc_qos *qos_table = pdata->qos_table;
	int i;

	for (i = (pdata->num_qos_steps - 1); i >= 0; i--) {
		mfc_debug(7, "QoS index: %d\n", i + 1);
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		mfc_debug(7, "\tint: %d, mif: %d, cpu: %d\n",
				qos_table[i].freq_int,
				qos_table[i].freq_mif,
				qos_table[i].freq_cpu);
#endif
		if (total_mb > qos_table[i].thrd_mb) {
			if (atomic_read(&dev->qos_req_cur) == 0)
				mfc_qos_operate(ctx, MFC_QOS_ADD, i);
			else if (atomic_read(&dev->qos_req_cur) != (i + 1))
				mfc_qos_operate(ctx, MFC_QOS_UPDATE, i);

			break;
		}
	}
}

static inline int get_ctx_mb(struct s5p_mfc_ctx *ctx)
{
	int mb_width, mb_height, fps;

	mb_width = (ctx->img_width + 15) / 16;
	mb_height = (ctx->img_height + 15) / 16;
	fps = ctx->framerate / 1000;

	return mb_width * mb_height * fps;
}

void s5p_mfc_qos_on(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_ctx *qos_ctx;
	int found = 0, total_mb = 0;
	/* TODO: cpu lock is not separated yet */
	int need_cpulock = 0;

	list_for_each_entry(qos_ctx, &dev->qos_queue, qos_list) {
		total_mb += get_ctx_mb(qos_ctx);
		if (qos_ctx == ctx)
			found = 1;
	}

	if (!found) {
		list_add_tail(&ctx->qos_list, &dev->qos_queue);
		total_mb += get_ctx_mb(ctx);
	}

	/* TODO: need_cpulock will be used for cpu lock */
	list_for_each_entry(qos_ctx, &dev->qos_queue, qos_list) {
		if (ctx->type == MFCINST_DECODER)
			need_cpulock++;
	}

	mfc_qos_add_or_update(ctx, total_mb);
}

void s5p_mfc_qos_off(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_ctx *qos_ctx;
	int found = 0, total_mb = 0;

	if (list_empty(&dev->qos_queue)) {
		if (atomic_read(&dev->qos_req_cur) != 0) {
			mfc_err("MFC request count is wrong!\n");
			mfc_qos_operate(ctx, MFC_QOS_REMOVE, 0);
		}

		return;
	}

	list_for_each_entry(qos_ctx, &dev->qos_queue, qos_list) {
		total_mb += get_ctx_mb(qos_ctx);
		if (qos_ctx == ctx)
			found = 1;
	}

	if (found) {
		list_del(&ctx->qos_list);
		total_mb -= get_ctx_mb(ctx);
	}

	if (list_empty(&dev->qos_queue))
		mfc_qos_operate(ctx, MFC_QOS_REMOVE, 0);
	else
		mfc_qos_add_or_update(ctx, total_mb);
}
#endif

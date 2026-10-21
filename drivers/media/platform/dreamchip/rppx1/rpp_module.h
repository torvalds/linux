/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#ifndef __RPPX1_MODULE_H__
#define __RPPX1_MODULE_H__

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>

#include <linux/media/dreamchip/rppx1-config.h>

#include <media/rppx1.h>

struct rpp_module_ops;

enum rpp_raw_pattern {
	RPP_RGGB = 0,
	RPP_GRBG,
	RPP_GBRG,
	RPP_BGGR,
};

struct rpp_module {
	struct rppx1 *rpp;
	u32 base;

	const struct rpp_module_ops *ops;

	union {
		struct {
			enum rpp_raw_pattern raw_pattern;
		} acq;
	} info;
};

int rpp_module_probe(struct rpp_module *mod, struct rppx1 *rpp,
		     const struct rpp_module_ops *ops, u32 base);

void rpp_module_write(struct rpp_module *mod, u32 offset, u32 value);
u32 rpp_module_read(struct rpp_module *mod, u32 offset);
void rpp_module_clrset(struct rpp_module *mod, u32 offset, u32 mask, u32 value);

union rppx1_params_block {
	struct v4l2_isp_block_header header;
	struct rppx1_bls_params bls;
	struct rppx1_lin_params lin;
	struct rppx1_lsc_params lsc;
	struct rppx1_awbg_params awbg;
	struct rppx1_ccor_params ccor;
	struct rppx1_hist_params hist;
	struct rppx1_exm_params exm;
	struct rppx1_wbmeas_params wbmeas;
	struct rppx1_ga_params ga;
};

union rppx1_stats_block {
	struct v4l2_isp_block_header header;
	struct rppx1_hist_stats hist;
	struct rppx1_exm_stats exm;
	struct rppx1_wbmeas_stats wbmeas;
};

struct rpp_module_ops {
	int (*probe)(struct rpp_module *mod);
	int (*start)(struct rpp_module *mod, const struct v4l2_mbus_framefmt *fmt);

	int (*fill_params)(struct rpp_module *mod,
			   const union rppx1_params_block *block,
			   rppx1_reg_write write, void *priv);
	int (*fill_stats)(struct rpp_module *mod,
			  union rppx1_stats_block *block);
};

extern const struct rpp_module_ops rppx1_acq_ops;
extern const struct rpp_module_ops rppx1_awbg_ops;
extern const struct rpp_module_ops rppx1_bd_ops;
extern const struct rpp_module_ops rppx1_bdrgb_ops;
extern const struct rpp_module_ops rppx1_bls_ops;
extern const struct rpp_module_ops rppx1_cac_ops;
extern const struct rpp_module_ops rppx1_ccor_ops;
extern const struct rpp_module_ops rppx1_ccor_csm_ops;
extern const struct rpp_module_ops rppx1_db_ops;
extern const struct rpp_module_ops rppx1_dpcc_ops;
extern const struct rpp_module_ops rppx1_exm_ops;
extern const struct rpp_module_ops rppx1_ga_ops;
extern const struct rpp_module_ops rppx1_hist256_ops;
extern const struct rpp_module_ops rppx1_hist_ops;
extern const struct rpp_module_ops rppx1_is_ops;
extern const struct rpp_module_ops rppx1_lin_ops;
extern const struct rpp_module_ops rppx1_lsc_ops;
extern const struct rpp_module_ops rppx1_ltm_ops;
extern const struct rpp_module_ops rppx1_ltmmeas_ops;
extern const struct rpp_module_ops rppx1_outif_ops;
extern const struct rpp_module_ops rppx1_outregs_ops;
extern const struct rpp_module_ops rppx1_rmapmeas_ops;
extern const struct rpp_module_ops rppx1_rmap_ops;
extern const struct rpp_module_ops rppx1_shrp_ops;
extern const struct rpp_module_ops rppx1_wbmeas_ops;
extern const struct rpp_module_ops rppx1_xyz2luv_ops;

#define rpp_module_call(mod, op, args...)				\
	({								\
		struct rpp_module *__mod = (mod);			\
		int __result;						\
		if (!__mod)						\
			__result = -ENODEV;				\
		else if (!__mod->ops->op)				\
			__result = 0;					\
		else							\
			__result = __mod->ops->op(__mod, ##args);	\
		__result;						\
	})

#endif /* __RPPX1_MODULE_H__ */

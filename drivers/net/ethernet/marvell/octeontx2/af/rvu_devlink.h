/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RVU Devlink
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef RVU_DEVLINK_H
#define  RVU_DEVLINK_H

#define RVU_REPORTERS(_name)  \
static const struct devlink_health_reporter_ops  rvu_ ## _name ## _reporter_ops =  { \
	.name = #_name, \
	.recover = rvu_ ## _name ## _recover, \
	.dump = rvu_ ## _name ## _dump, \
}

enum npa_af_rvu_health {
	NPA_AF_RVU_INTR,
	NPA_AF_RVU_GEN,
	NPA_AF_RVU_ERR,
	NPA_AF_RVU_RAS,
};

struct rvu_npa_event_ctx {
	u64 npa_af_rvu_int;
	u64 npa_af_rvu_gen;
	u64 npa_af_rvu_err;
	u64 npa_af_rvu_ras;
};

struct rvu_npa_health_reporters {
	struct rvu_npa_event_ctx *npa_event_ctx;
	struct devlink_health_reporter *rvu_hw_npa_intr_reporter;
	struct work_struct              intr_work;
	struct devlink_health_reporter *rvu_hw_npa_gen_reporter;
	struct work_struct              gen_work;
	struct devlink_health_reporter *rvu_hw_npa_err_reporter;
	struct work_struct             err_work;
	struct devlink_health_reporter *rvu_hw_npa_ras_reporter;
	struct work_struct              ras_work;
};

struct rvu_devlink {
	struct devlink *dl;
	struct rvu *rvu;
	struct workqueue_struct *devlink_wq;
	struct rvu_npa_health_reporters *rvu_npa_health_reporter;
};

/* Devlink APIs */
int rvu_register_dl(struct rvu *rvu);
void rvu_unregister_dl(struct rvu *rvu);

#endif /* RVU_DEVLINK_H */

/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2016 - 2021 Intel Corporation */
#ifndef IRDMA_PROTOS_H
#define IRDMA_PROTOS_H

#define PAUSE_TIMER_VAL		0xffff
#define REFRESH_THRESHOLD	0x7fff
#define HIGH_THRESHOLD		0x800
#define LOW_THRESHOLD		0x200
#define ALL_TC2PFC		0xff
#define CQP_COMPL_WAIT_TIME_MS	10
#define CQP_TIMEOUT_THRESHOLD	500
#define CQP_DEF_CMPL_TIMEOUT_THRESHOLD	2500

/* init operations */
int irdma_sc_dev_init(enum irdma_vers ver, struct irdma_sc_dev *dev,
		      struct irdma_device_init_info *info);
void irdma_sc_rt_init(struct irdma_sc_dev *dev);
void irdma_sc_cqp_post_sq(struct irdma_sc_cqp *cqp);
__le64 *irdma_sc_cqp_get_next_send_wqe(struct irdma_sc_cqp *cqp, u64 scratch);
int irdma_sc_mr_fast_register(struct irdma_sc_qp *qp,
			      struct irdma_fast_reg_stag_info *info,
			      bool post_sq);
/* HMC/FPM functions */
int irdma_sc_init_iw_hmc(struct irdma_sc_dev *dev, u8 hmc_fn_id);
/* stats misc */
int irdma_cqp_gather_stats_cmd(struct irdma_sc_dev *dev,
			       struct irdma_vsi_pestat *pestat, bool wait);
void irdma_cqp_gather_stats_gen1(struct irdma_sc_dev *dev,
				 struct irdma_vsi_pestat *pestat);
void irdma_hw_stats_read_all(struct irdma_vsi_pestat *stats,
			     const u64 *hw_stats_regs);
int irdma_cqp_ws_node_cmd(struct irdma_sc_dev *dev, u8 cmd,
			  struct irdma_ws_node_info *node_info);
int irdma_cqp_ceq_cmd(struct irdma_sc_dev *dev, struct irdma_sc_ceq *sc_ceq,
		      u8 op);
int irdma_cqp_aeq_cmd(struct irdma_sc_dev *dev, struct irdma_sc_aeq *sc_aeq,
		      u8 op);
int irdma_cqp_stats_inst_cmd(struct irdma_sc_vsi *vsi, u8 cmd,
			     struct irdma_stats_inst_info *stats_info);
u16 irdma_alloc_ws_node_id(struct irdma_sc_dev *dev);
void irdma_free_ws_node_id(struct irdma_sc_dev *dev, u16 node_id);
void irdma_update_stats(struct irdma_dev_hw_stats *hw_stats,
			struct irdma_gather_stats *gather_stats,
			struct irdma_gather_stats *last_gather_stats,
			const struct irdma_hw_stat_map *map, u16 max_stat_idx);

/* vsi functions */
int irdma_vsi_stats_init(struct irdma_sc_vsi *vsi,
			 struct irdma_vsi_stats_info *info);
void irdma_vsi_stats_free(struct irdma_sc_vsi *vsi);
void irdma_sc_vsi_init(struct irdma_sc_vsi *vsi,
		       struct irdma_vsi_init_info *info);
int irdma_sc_add_cq_ctx(struct irdma_sc_ceq *ceq, struct irdma_sc_cq *cq);
void irdma_sc_remove_cq_ctx(struct irdma_sc_ceq *ceq, struct irdma_sc_cq *cq);
/* misc L2 param change functions */
void irdma_change_l2params(struct irdma_sc_vsi *vsi,
			   struct irdma_l2params *l2params);
void irdma_sc_suspend_resume_qps(struct irdma_sc_vsi *vsi, u8 suspend);
int irdma_cqp_qp_suspend_resume(struct irdma_sc_qp *qp, u8 cmd);
void irdma_qp_add_qos(struct irdma_sc_qp *qp);
void irdma_qp_rem_qos(struct irdma_sc_qp *qp);
struct irdma_sc_qp *irdma_get_qp_from_list(struct list_head *head,
					   struct irdma_sc_qp *qp);
void irdma_reinitialize_ieq(struct irdma_sc_vsi *vsi);
/* terminate functions*/
void irdma_terminate_send_fin(struct irdma_sc_qp *qp);

void irdma_terminate_connection(struct irdma_sc_qp *qp,
				struct irdma_aeqe_info *info);

void irdma_terminate_received(struct irdma_sc_qp *qp,
			      struct irdma_aeqe_info *info);
/* dynamic memory allocation */
/* misc */
u8 irdma_get_encoded_wqe_size(u32 wqsize, enum irdma_queue_type queue_type);
void irdma_modify_qp_to_err(struct irdma_sc_qp *sc_qp);
int irdma_sc_static_hmc_pages_allocated(struct irdma_sc_cqp *cqp, u64 scratch,
					u8 hmc_fn_id, bool post_sq,
					bool poll_registers);
int irdma_cfg_fpm_val(struct irdma_sc_dev *dev, u32 qp_count);
int irdma_get_rdma_features(struct irdma_sc_dev *dev);
void free_sd_mem(struct irdma_sc_dev *dev);
int irdma_process_cqp_cmd(struct irdma_sc_dev *dev,
			  struct cqp_cmds_info *pcmdinfo);
int irdma_process_bh(struct irdma_sc_dev *dev);
int irdma_cqp_sds_cmd(struct irdma_sc_dev *dev,
		      struct irdma_update_sds_info *info);
int irdma_alloc_query_fpm_buf(struct irdma_sc_dev *dev,
			      struct irdma_dma_mem *mem);
int irdma_cqp_manage_hmc_fcn_cmd(struct irdma_sc_dev *dev,
				 struct irdma_hmc_fcn_info *hmcfcninfo,
				 u16 *pmf_idx);
void irdma_add_dev_ref(struct irdma_sc_dev *dev);
void irdma_put_dev_ref(struct irdma_sc_dev *dev);
void *irdma_remove_cqp_head(struct irdma_sc_dev *dev);
#endif /* IRDMA_PROTOS_H */

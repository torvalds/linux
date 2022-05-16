/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#ifndef I40IW_P_H
#define I40IW_P_H

#define PAUSE_TIMER_VALUE       0xFFFF
#define REFRESH_THRESHOLD       0x7FFF
#define HIGH_THRESHOLD          0x800
#define LOW_THRESHOLD           0x200
#define ALL_TC2PFC              0xFF
#define CQP_COMPL_WAIT_TIME     0x3E8
#define CQP_TIMEOUT_THRESHOLD   5

void i40iw_debug_buf(struct i40iw_sc_dev *dev, enum i40iw_debug_flag mask,
		     char *desc, u64 *buf, u32 size);
/* init operations */
enum i40iw_status_code i40iw_device_init(struct i40iw_sc_dev *dev,
					 struct i40iw_device_init_info *info);

void i40iw_sc_cqp_post_sq(struct i40iw_sc_cqp *cqp);

u64 *i40iw_sc_cqp_get_next_send_wqe(struct i40iw_sc_cqp *cqp, u64 scratch);

void i40iw_check_cqp_progress(struct i40iw_cqp_timeout *cqp_timeout, struct i40iw_sc_dev *dev);

enum i40iw_status_code i40iw_sc_mr_fast_register(struct i40iw_sc_qp *qp,
						 struct i40iw_fast_reg_stag_info *info,
						 bool post_sq);

void i40iw_insert_wqe_hdr(u64 *wqe, u64 header);

/* HMC/FPM functions */
enum i40iw_status_code i40iw_sc_init_iw_hmc(struct i40iw_sc_dev *dev,
					    u8 hmc_fn_id);

enum i40iw_status_code i40iw_pf_init_vfhmc(struct i40iw_sc_dev *dev, u8 vf_hmc_fn_id,
					   u32 *vf_cnt_array);

/* stats functions */
void i40iw_hw_stats_refresh_all(struct i40iw_vsi_pestat *stats);
void i40iw_hw_stats_read_all(struct i40iw_vsi_pestat *stats, struct i40iw_dev_hw_stats *stats_values);
void i40iw_hw_stats_read_32(struct i40iw_vsi_pestat *stats,
			    enum i40iw_hw_stats_index_32b index,
			    u64 *value);
void i40iw_hw_stats_read_64(struct i40iw_vsi_pestat *stats,
			    enum i40iw_hw_stats_index_64b index,
			    u64 *value);
void i40iw_hw_stats_init(struct i40iw_vsi_pestat *stats, u8 index, bool is_pf);

/* vsi misc functions */
enum i40iw_status_code i40iw_vsi_stats_init(struct i40iw_sc_vsi *vsi, struct i40iw_vsi_stats_info *info);
void i40iw_vsi_stats_free(struct i40iw_sc_vsi *vsi);
void i40iw_sc_vsi_init(struct i40iw_sc_vsi *vsi, struct i40iw_vsi_init_info *info);

void i40iw_change_l2params(struct i40iw_sc_vsi *vsi, struct i40iw_l2params *l2params);
void i40iw_qp_add_qos(struct i40iw_sc_qp *qp);
void i40iw_qp_rem_qos(struct i40iw_sc_qp *qp);
void i40iw_terminate_send_fin(struct i40iw_sc_qp *qp);

void i40iw_terminate_connection(struct i40iw_sc_qp *qp, struct i40iw_aeqe_info *info);

void i40iw_terminate_received(struct i40iw_sc_qp *qp, struct i40iw_aeqe_info *info);

enum i40iw_status_code i40iw_sc_suspend_qp(struct i40iw_sc_cqp *cqp,
					   struct i40iw_sc_qp *qp, u64 scratch);

enum i40iw_status_code i40iw_sc_resume_qp(struct i40iw_sc_cqp *cqp,
					  struct i40iw_sc_qp *qp, u64 scratch);

enum i40iw_status_code i40iw_sc_static_hmc_pages_allocated(struct i40iw_sc_cqp *cqp,
							   u64 scratch, u8 hmc_fn_id,
							   bool post_sq,
							   bool poll_registers);

enum i40iw_status_code i40iw_config_fpm_values(struct i40iw_sc_dev *dev, u32 qp_count);
enum i40iw_status_code i40iw_get_rdma_features(struct i40iw_sc_dev *dev);

void free_sd_mem(struct i40iw_sc_dev *dev);

enum i40iw_status_code i40iw_process_cqp_cmd(struct i40iw_sc_dev *dev,
					     struct cqp_commands_info *pcmdinfo);

enum i40iw_status_code i40iw_process_bh(struct i40iw_sc_dev *dev);

/* prototype for functions used for dynamic memory allocation */
enum i40iw_status_code i40iw_allocate_dma_mem(struct i40iw_hw *hw,
					      struct i40iw_dma_mem *mem, u64 size,
					      u32 alignment);
void i40iw_free_dma_mem(struct i40iw_hw *hw, struct i40iw_dma_mem *mem);
enum i40iw_status_code i40iw_allocate_virt_mem(struct i40iw_hw *hw,
					       struct i40iw_virt_mem *mem, u32 size);
enum i40iw_status_code i40iw_free_virt_mem(struct i40iw_hw *hw,
					   struct i40iw_virt_mem *mem);
u8 i40iw_get_encoded_wqe_size(u32 wqsize, bool cqpsq);
void i40iw_reinitialize_ieq(struct i40iw_sc_dev *dev);

#endif

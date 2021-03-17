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

#ifndef I40IW_OSDEP_H
#define I40IW_OSDEP_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <net/tcp.h>
#include <crypto/hash.h>
/* get readq/writeq support for 32 bit kernels, use the low-first version */
#include <linux/io-64-nonatomic-lo-hi.h>

#define STATS_TIMER_DELAY 1000

static inline void set_64bit_val(u64 *wqe_words, u32 byte_index, u64 value)
{
	wqe_words[byte_index >> 3] = value;
}

/**
 * set_32bit_val - set 32 value to hw wqe
 * @wqe_words: wqe addr to write
 * @byte_index: index in wqe
 * @value: value to write
 **/
static inline void set_32bit_val(u32 *wqe_words, u32 byte_index, u32 value)
{
	wqe_words[byte_index >> 2] = value;
}

/**
 * get_64bit_val - read 64 bit value from wqe
 * @wqe_words: wqe addr
 * @byte_index: index to read from
 * @value: read value
 **/
static inline void get_64bit_val(u64 *wqe_words, u32 byte_index, u64 *value)
{
	*value = wqe_words[byte_index >> 3];
}

/**
 * get_32bit_val - read 32 bit value from wqe
 * @wqe_words: wqe addr
 * @byte_index: index to reaad from
 * @value: return 32 bit value
 **/
static inline void get_32bit_val(u32 *wqe_words, u32 byte_index, u32 *value)
{
	*value = wqe_words[byte_index >> 2];
}

struct i40iw_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
} __packed;

struct i40iw_virt_mem {
	void *va;
	u32 size;
} __packed;

#define i40iw_debug(h, m, s, ...)                               \
do {                                                            \
	if (((m) & (h)->debug_mask))                            \
		pr_info("i40iw " s, ##__VA_ARGS__);             \
} while (0)

#define i40iw_flush(a)          readl((a)->hw_addr + I40E_GLGEN_STAT)

#define I40E_GLHMC_VFSDCMD(_i)  (0x000C8000 + ((_i) * 4)) \
				/* _i=0...31 */
#define I40E_GLHMC_VFSDCMD_MAX_INDEX    31
#define I40E_GLHMC_VFSDCMD_PMSDIDX_SHIFT  0
#define I40E_GLHMC_VFSDCMD_PMSDIDX_MASK  (0xFFF \
					  << I40E_GLHMC_VFSDCMD_PMSDIDX_SHIFT)
#define I40E_GLHMC_VFSDCMD_PF_SHIFT       16
#define I40E_GLHMC_VFSDCMD_PF_MASK        (0xF << I40E_GLHMC_VFSDCMD_PF_SHIFT)
#define I40E_GLHMC_VFSDCMD_VF_SHIFT       20
#define I40E_GLHMC_VFSDCMD_VF_MASK        (0x1FF << I40E_GLHMC_VFSDCMD_VF_SHIFT)
#define I40E_GLHMC_VFSDCMD_PMF_TYPE_SHIFT 29
#define I40E_GLHMC_VFSDCMD_PMF_TYPE_MASK  (0x3 \
					   << I40E_GLHMC_VFSDCMD_PMF_TYPE_SHIFT)
#define I40E_GLHMC_VFSDCMD_PMSDWR_SHIFT   31
#define I40E_GLHMC_VFSDCMD_PMSDWR_MASK  (0x1 << I40E_GLHMC_VFSDCMD_PMSDWR_SHIFT)

#define I40E_GLHMC_VFSDDATAHIGH(_i)     (0x000C8200 + ((_i) * 4)) \
				/* _i=0...31 */
#define I40E_GLHMC_VFSDDATAHIGH_MAX_INDEX       31
#define I40E_GLHMC_VFSDDATAHIGH_PMSDDATAHIGH_SHIFT 0
#define I40E_GLHMC_VFSDDATAHIGH_PMSDDATAHIGH_MASK  (0xFFFFFFFF \
			<< I40E_GLHMC_VFSDDATAHIGH_PMSDDATAHIGH_SHIFT)

#define I40E_GLHMC_VFSDDATALOW(_i)      (0x000C8100 + ((_i) * 4)) \
				/* _i=0...31 */
#define I40E_GLHMC_VFSDDATALOW_MAX_INDEX        31
#define I40E_GLHMC_VFSDDATALOW_PMSDVALID_SHIFT   0
#define I40E_GLHMC_VFSDDATALOW_PMSDVALID_MASK  (0x1 \
			<< I40E_GLHMC_VFSDDATALOW_PMSDVALID_SHIFT)
#define I40E_GLHMC_VFSDDATALOW_PMSDTYPE_SHIFT    1
#define I40E_GLHMC_VFSDDATALOW_PMSDTYPE_MASK  (0x1 \
			<< I40E_GLHMC_VFSDDATALOW_PMSDTYPE_SHIFT)
#define I40E_GLHMC_VFSDDATALOW_PMSDBPCOUNT_SHIFT 2
#define I40E_GLHMC_VFSDDATALOW_PMSDBPCOUNT_MASK  (0x3FF \
			<< I40E_GLHMC_VFSDDATALOW_PMSDBPCOUNT_SHIFT)
#define I40E_GLHMC_VFSDDATALOW_PMSDDATALOW_SHIFT 12
#define I40E_GLHMC_VFSDDATALOW_PMSDDATALOW_MASK  (0xFFFFF \
			<< I40E_GLHMC_VFSDDATALOW_PMSDDATALOW_SHIFT)

#define I40E_GLPE_FWLDSTATUS                     0x0000D200
#define I40E_GLPE_FWLDSTATUS_LOAD_REQUESTED_SHIFT 0
#define I40E_GLPE_FWLDSTATUS_LOAD_REQUESTED_MASK  (0x1 \
			<< I40E_GLPE_FWLDSTATUS_LOAD_REQUESTED_SHIFT)
#define I40E_GLPE_FWLDSTATUS_DONE_SHIFT           1
#define I40E_GLPE_FWLDSTATUS_DONE_MASK  (0x1 << I40E_GLPE_FWLDSTATUS_DONE_SHIFT)
#define I40E_GLPE_FWLDSTATUS_CQP_FAIL_SHIFT       2
#define I40E_GLPE_FWLDSTATUS_CQP_FAIL_MASK  (0x1 \
			 << I40E_GLPE_FWLDSTATUS_CQP_FAIL_SHIFT)
#define I40E_GLPE_FWLDSTATUS_TEP_FAIL_SHIFT       3
#define I40E_GLPE_FWLDSTATUS_TEP_FAIL_MASK  (0x1 \
			 << I40E_GLPE_FWLDSTATUS_TEP_FAIL_SHIFT)
#define I40E_GLPE_FWLDSTATUS_OOP_FAIL_SHIFT       4
#define I40E_GLPE_FWLDSTATUS_OOP_FAIL_MASK  (0x1 \
			 << I40E_GLPE_FWLDSTATUS_OOP_FAIL_SHIFT)

struct i40iw_sc_dev;
struct i40iw_sc_qp;
struct i40iw_puda_buf;
struct i40iw_puda_completion_info;
struct i40iw_update_sds_info;
struct i40iw_hmc_fcn_info;
struct i40iw_virtchnl_work_info;
struct i40iw_manage_vf_pble_info;
struct i40iw_device;
struct i40iw_hmc_info;
struct i40iw_hw;

u8 __iomem *i40iw_get_hw_addr(void *dev);
void i40iw_ieq_mpa_crc_ae(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp);
enum i40iw_status_code i40iw_vf_wait_vchnl_resp(struct i40iw_sc_dev *dev);
bool i40iw_vf_clear_to_send(struct i40iw_sc_dev *dev);
enum i40iw_status_code i40iw_ieq_check_mpacrc(struct shash_desc *desc, void *addr,
					      u32 length, u32 value);
struct i40iw_sc_qp *i40iw_ieq_get_qp(struct i40iw_sc_dev *dev, struct i40iw_puda_buf *buf);
void i40iw_ieq_update_tcpip_info(struct i40iw_puda_buf *buf, u16 length, u32 seqnum);
void i40iw_free_hash_desc(struct shash_desc *);
enum i40iw_status_code i40iw_init_hash_desc(struct shash_desc **);
enum i40iw_status_code i40iw_puda_get_tcpip_info(struct i40iw_puda_completion_info *info,
						 struct i40iw_puda_buf *buf);
enum i40iw_status_code i40iw_cqp_sds_cmd(struct i40iw_sc_dev *dev,
					 struct i40iw_update_sds_info *info);
enum i40iw_status_code i40iw_cqp_manage_hmc_fcn_cmd(struct i40iw_sc_dev *dev,
						    struct i40iw_hmc_fcn_info *hmcfcninfo);
enum i40iw_status_code i40iw_cqp_query_fpm_values_cmd(struct i40iw_sc_dev *dev,
						      struct i40iw_dma_mem *values_mem,
						      u8 hmc_fn_id);
enum i40iw_status_code i40iw_cqp_commit_fpm_values_cmd(struct i40iw_sc_dev *dev,
						       struct i40iw_dma_mem *values_mem,
						       u8 hmc_fn_id);
enum i40iw_status_code i40iw_alloc_query_fpm_buf(struct i40iw_sc_dev *dev,
						 struct i40iw_dma_mem *mem);
enum i40iw_status_code i40iw_cqp_manage_vf_pble_bp(struct i40iw_sc_dev *dev,
						   struct i40iw_manage_vf_pble_info *info);
void i40iw_cqp_spawn_worker(struct i40iw_sc_dev *dev,
			    struct i40iw_virtchnl_work_info *work_info, u32 iw_vf_idx);
void *i40iw_remove_head(struct list_head *list);
void i40iw_qp_suspend_resume(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp, bool suspend);

void i40iw_term_modify_qp(struct i40iw_sc_qp *qp, u8 next_state, u8 term, u8 term_len);
void i40iw_terminate_done(struct i40iw_sc_qp *qp, int timeout_occurred);
void i40iw_terminate_start_timer(struct i40iw_sc_qp *qp);
void i40iw_terminate_del_timer(struct i40iw_sc_qp *qp);

enum i40iw_status_code i40iw_hw_manage_vf_pble_bp(struct i40iw_device *iwdev,
						  struct i40iw_manage_vf_pble_info *info,
						  bool wait);
struct i40iw_sc_vsi;
void i40iw_hw_stats_start_timer(struct i40iw_sc_vsi *vsi);
void i40iw_hw_stats_stop_timer(struct i40iw_sc_vsi *vsi);
#define i40iw_mmiowb() do { } while (0)
void i40iw_wr32(struct i40iw_hw *hw, u32 reg, u32 value);
u32  i40iw_rd32(struct i40iw_hw *hw, u32 reg);
#endif				/* _I40IW_OSDEP_H_ */

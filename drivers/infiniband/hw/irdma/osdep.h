/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2021 Intel Corporation */
#ifndef IRDMA_OSDEP_H
#define IRDMA_OSDEP_H

#include <linux/pci.h>
#include <linux/bitfield.h>
#include <crypto/hash.h>
#include <rdma/ib_verbs.h>

#define STATS_TIMER_DELAY	60000

struct irdma_dma_info {
	dma_addr_t *dmaaddrs;
};

struct irdma_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
} __packed;

struct irdma_virt_mem {
	void *va;
	u32 size;
} __packed;

struct irdma_sc_vsi;
struct irdma_sc_dev;
struct irdma_sc_qp;
struct irdma_puda_buf;
struct irdma_puda_cmpl_info;
struct irdma_update_sds_info;
struct irdma_hmc_fcn_info;
struct irdma_manage_vf_pble_info;
struct irdma_hw;
struct irdma_pci_f;

struct ib_device *to_ibdev(struct irdma_sc_dev *dev);
u8 __iomem *irdma_get_hw_addr(void *dev);
void irdma_ieq_mpa_crc_ae(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp);
enum irdma_status_code irdma_vf_wait_vchnl_resp(struct irdma_sc_dev *dev);
bool irdma_vf_clear_to_send(struct irdma_sc_dev *dev);
void irdma_add_dev_ref(struct irdma_sc_dev *dev);
void irdma_put_dev_ref(struct irdma_sc_dev *dev);
enum irdma_status_code irdma_ieq_check_mpacrc(struct shash_desc *desc,
					      void *addr, u32 len, u32 val);
struct irdma_sc_qp *irdma_ieq_get_qp(struct irdma_sc_dev *dev,
				     struct irdma_puda_buf *buf);
void irdma_send_ieq_ack(struct irdma_sc_qp *qp);
void irdma_ieq_update_tcpip_info(struct irdma_puda_buf *buf, u16 len,
				 u32 seqnum);
void irdma_free_hash_desc(struct shash_desc *hash_desc);
enum irdma_status_code irdma_init_hash_desc(struct shash_desc **hash_desc);
enum irdma_status_code
irdma_puda_get_tcpip_info(struct irdma_puda_cmpl_info *info,
			  struct irdma_puda_buf *buf);
enum irdma_status_code irdma_cqp_sds_cmd(struct irdma_sc_dev *dev,
					 struct irdma_update_sds_info *info);
enum irdma_status_code
irdma_cqp_manage_hmc_fcn_cmd(struct irdma_sc_dev *dev,
			     struct irdma_hmc_fcn_info *hmcfcninfo,
			     u16 *pmf_idx);
enum irdma_status_code
irdma_cqp_query_fpm_val_cmd(struct irdma_sc_dev *dev,
			    struct irdma_dma_mem *val_mem, u8 hmc_fn_id);
enum irdma_status_code
irdma_cqp_commit_fpm_val_cmd(struct irdma_sc_dev *dev,
			     struct irdma_dma_mem *val_mem, u8 hmc_fn_id);
enum irdma_status_code irdma_alloc_query_fpm_buf(struct irdma_sc_dev *dev,
						 struct irdma_dma_mem *mem);
void *irdma_remove_cqp_head(struct irdma_sc_dev *dev);
void irdma_term_modify_qp(struct irdma_sc_qp *qp, u8 next_state, u8 term,
			  u8 term_len);
void irdma_terminate_done(struct irdma_sc_qp *qp, int timeout_occurred);
void irdma_terminate_start_timer(struct irdma_sc_qp *qp);
void irdma_terminate_del_timer(struct irdma_sc_qp *qp);
void irdma_hw_stats_start_timer(struct irdma_sc_vsi *vsi);
void irdma_hw_stats_stop_timer(struct irdma_sc_vsi *vsi);
void wr32(struct irdma_hw *hw, u32 reg, u32 val);
u32 rd32(struct irdma_hw *hw, u32 reg);
u64 rd64(struct irdma_hw *hw, u32 reg);
enum irdma_status_code irdma_map_vm_page_list(struct irdma_hw *hw, void *va,
					      dma_addr_t *pg_dma, u32 pg_cnt);
void irdma_unmap_vm_page_list(struct irdma_hw *hw, dma_addr_t *pg_dma, u32 pg_cnt);
#endif /* IRDMA_OSDEP_H */

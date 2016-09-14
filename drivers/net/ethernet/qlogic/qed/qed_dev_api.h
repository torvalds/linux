/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_DEV_API_H
#define _QED_DEV_API_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_if.h>
#include "qed_int.h"

/**
 * @brief qed_init_dp - initialize the debug level
 *
 * @param cdev
 * @param dp_module
 * @param dp_level
 */
void qed_init_dp(struct qed_dev *cdev,
		 u32 dp_module,
		 u8 dp_level);

/**
 * @brief qed_init_struct - initialize the device structure to
 *        its defaults
 *
 * @param cdev
 */
void qed_init_struct(struct qed_dev *cdev);

/**
 * @brief qed_resc_free -
 *
 * @param cdev
 */
void qed_resc_free(struct qed_dev *cdev);

/**
 * @brief qed_resc_alloc -
 *
 * @param cdev
 *
 * @return int
 */
int qed_resc_alloc(struct qed_dev *cdev);

/**
 * @brief qed_resc_setup -
 *
 * @param cdev
 */
void qed_resc_setup(struct qed_dev *cdev);

/**
 * @brief qed_hw_init -
 *
 * @param cdev
 * @param p_tunn
 * @param b_hw_start
 * @param int_mode - interrupt mode [msix, inta, etc.] to use.
 * @param allow_npar_tx_switch - npar tx switching to be used
 *	  for vports configured for tx-switching.
 * @param bin_fw_data - binary fw data pointer in binary fw file.
 *			Pass NULL if not using binary fw file.
 *
 * @return int
 */
int qed_hw_init(struct qed_dev *cdev,
		struct qed_tunn_start_params *p_tunn,
		bool b_hw_start,
		enum qed_int_mode int_mode,
		bool allow_npar_tx_switch,
		const u8 *bin_fw_data);

/**
 * @brief qed_hw_timers_stop_all - stop the timers HW block
 *
 * @param cdev
 *
 * @return void
 */
void qed_hw_timers_stop_all(struct qed_dev *cdev);

/**
 * @brief qed_hw_stop -
 *
 * @param cdev
 *
 * @return int
 */
int qed_hw_stop(struct qed_dev *cdev);

/**
 * @brief qed_hw_stop_fastpath -should be called incase
 *		slowpath is still required for the device,
 *		but fastpath is not.
 *
 * @param cdev
 *
 */
void qed_hw_stop_fastpath(struct qed_dev *cdev);

/**
 * @brief qed_hw_start_fastpath -restart fastpath traffic,
 *		only if hw_stop_fastpath was called
 *
 * @param cdev
 *
 */
void qed_hw_start_fastpath(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_hw_reset -
 *
 * @param cdev
 *
 * @return int
 */
int qed_hw_reset(struct qed_dev *cdev);

/**
 * @brief qed_hw_prepare -
 *
 * @param cdev
 * @param personality - personality to initialize
 *
 * @return int
 */
int qed_hw_prepare(struct qed_dev *cdev,
		   int personality);

/**
 * @brief qed_hw_remove -
 *
 * @param cdev
 */
void qed_hw_remove(struct qed_dev *cdev);

/**
 * @brief qed_ptt_acquire - Allocate a PTT window
 *
 * Should be called at the entry point to the driver (at the beginning of an
 * exported function)
 *
 * @param p_hwfn
 *
 * @return struct qed_ptt
 */
struct qed_ptt *qed_ptt_acquire(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ptt_release - Release PTT Window
 *
 * Should be called at the end of a flow - at the end of the function that
 * acquired the PTT.
 *
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_ptt_release(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);
void qed_reset_vport_stats(struct qed_dev *cdev);

enum qed_dmae_address_type_t {
	QED_DMAE_ADDRESS_HOST_VIRT,
	QED_DMAE_ADDRESS_HOST_PHYS,
	QED_DMAE_ADDRESS_GRC
};

/* value of flags If QED_DMAE_FLAG_RW_REPL_SRC flag is set and the
 * source is a block of length DMAE_MAX_RW_SIZE and the
 * destination is larger, the source block will be duplicated as
 * many times as required to fill the destination block. This is
 * used mostly to write a zeroed buffer to destination address
 * using DMA
 */
#define QED_DMAE_FLAG_RW_REPL_SRC	0x00000001
#define QED_DMAE_FLAG_VF_SRC		0x00000002
#define QED_DMAE_FLAG_VF_DST		0x00000004
#define QED_DMAE_FLAG_COMPLETION_DST	0x00000008

struct qed_dmae_params {
	u32 flags; /* consists of QED_DMAE_FLAG_* values */
	u8 src_vfid;
	u8 dst_vfid;
};

/**
 * @brief qed_dmae_host2grc - copy data from source addr to
 * dmae registers using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_addr
 * @param grc_addr (dmae_data_offset)
 * @param size_in_dwords
 * @param flags (one of the flags defined above)
 */
int
qed_dmae_host2grc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt,
		  u64 source_addr,
		  u32 grc_addr,
		  u32 size_in_dwords,
		  u32 flags);

 /**
 * @brief qed_dmae_grc2host - Read data from dmae data offset
 * to source address using the given ptt
 *
 * @param p_ptt
 * @param grc_addr (dmae_data_offset)
 * @param dest_addr
 * @param size_in_dwords
 * @param flags - one of the flags defined above
 */
int qed_dmae_grc2host(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		      u32 grc_addr, dma_addr_t dest_addr, u32 size_in_dwords,
		      u32 flags);

/**
 * @brief qed_dmae_host2host - copy data from to source address
 * to a destination adress (for SRIOV) using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_addr
 * @param dest_addr
 * @param size_in_dwords
 * @param params
 */
int qed_dmae_host2host(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       dma_addr_t source_addr,
		       dma_addr_t dest_addr,
		       u32 size_in_dwords, struct qed_dmae_params *p_params);

/**
 * @brief qed_chain_alloc - Allocate and initialize a chain
 *
 * @param p_hwfn
 * @param intended_use
 * @param mode
 * @param num_elems
 * @param elem_size
 * @param p_chain
 *
 * @return int
 */
int
qed_chain_alloc(struct qed_dev *cdev,
		enum qed_chain_use_mode intended_use,
		enum qed_chain_mode mode,
		enum qed_chain_cnt_type cnt_type,
		u32 num_elems, size_t elem_size, struct qed_chain *p_chain);

/**
 * @brief qed_chain_free - Free chain DMA memory
 *
 * @param p_hwfn
 * @param p_chain
 */
void qed_chain_free(struct qed_dev *cdev, struct qed_chain *p_chain);

/**
 * @@brief qed_fw_l2_queue - Get absolute L2 queue ID
 *
 *  @param p_hwfn
 *  @param src_id - relative to p_hwfn
 *  @param dst_id - absolute per engine
 *
 *  @return int
 */
int qed_fw_l2_queue(struct qed_hwfn *p_hwfn,
		    u16 src_id,
		    u16 *dst_id);

/**
 * @@brief qed_fw_vport - Get absolute vport ID
 *
 *  @param p_hwfn
 *  @param src_id - relative to p_hwfn
 *  @param dst_id - absolute per engine
 *
 *  @return int
 */
int qed_fw_vport(struct qed_hwfn *p_hwfn,
		 u8 src_id,
		 u8 *dst_id);

/**
 * @@brief qed_fw_rss_eng - Get absolute RSS engine ID
 *
 *  @param p_hwfn
 *  @param src_id - relative to p_hwfn
 *  @param dst_id - absolute per engine
 *
 *  @return int
 */
int qed_fw_rss_eng(struct qed_hwfn *p_hwfn,
		   u8 src_id,
		   u8 *dst_id);

/**
 * *@brief Cleanup of previous driver remains prior to load
 *
 * @param p_hwfn
 * @param p_ptt
 * @param id - For PF, engine-relative. For VF, PF-relative.
 * @param is_vf - true iff cleanup is made for a VF.
 *
 * @return int
 */
int qed_final_cleanup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, u16 id, bool is_vf);

/**
 * @brief qed_set_rxq_coalesce - Configure coalesce parameters for an Rx queue
 * The fact that we can configure coalescing to up to 511, but on varying
 * accuracy [the bigger the value the less accurate] up to a mistake of 3usec
 * for the highest values.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param coalesce - Coalesce value in micro seconds.
 * @param qid - Queue index.
 * @param qid - SB Id
 *
 * @return int
 */
int qed_set_rxq_coalesce(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u16 coalesce, u8 qid, u16 sb_id);

/**
 * @brief qed_set_txq_coalesce - Configure coalesce parameters for a Tx queue
 * While the API allows setting coalescing per-qid, all tx queues sharing a
 * SB should be in same range [i.e., either 0-0x7f, 0x80-0xff or 0x100-0x1ff]
 * otherwise configuration would break.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param coalesce - Coalesce value in micro seconds.
 * @param qid - Queue index.
 * @param qid - SB Id
 *
 * @return int
 */
int qed_set_txq_coalesce(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u16 coalesce, u8 qid, u16 sb_id);
#endif

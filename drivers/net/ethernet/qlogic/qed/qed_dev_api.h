/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

enum qed_override_force_load {
	QED_OVERRIDE_FORCE_LOAD_NONE,
	QED_OVERRIDE_FORCE_LOAD_ALWAYS,
	QED_OVERRIDE_FORCE_LOAD_NEVER,
};

struct qed_drv_load_params {
	/* Indicates whether the driver is running over a crash kernel.
	 * As part of the load request, this will be used for providing the
	 * driver role to the MFW.
	 * In case of a crash kernel over PDA - this should be set to false.
	 */
	bool is_crash_kernel;

	/* The timeout value that the MFW should use when locking the engine for
	 * the driver load process.
	 * A value of '0' means the default value, and '255' means no timeout.
	 */
	u8 mfw_timeout_val;
#define QED_LOAD_REQ_LOCK_TO_DEFAULT    0
#define QED_LOAD_REQ_LOCK_TO_NONE       255

	/* Avoid engine reset when first PF loads on it */
	bool avoid_eng_reset;

	/* Allow overriding the default force load behavior */
	enum qed_override_force_load override_force_load;
};

struct qed_hw_init_params {
	/* Tunneling parameters */
	struct qed_tunnel_info *p_tunn;

	bool b_hw_start;

	/* Interrupt mode [msix, inta, etc.] to use */
	enum qed_int_mode int_mode;

	/* NPAR tx switching to be used for vports for tx-switching */
	bool allow_npar_tx_switch;

	/* Binary fw data pointer in binary fw file */
	const u8 *bin_fw_data;

	/* Driver load parameters */
	struct qed_drv_load_params *p_drv_load_params;
};

/**
 * @brief qed_hw_init -
 *
 * @param cdev
 * @param p_params
 *
 * @return int
 */
int qed_hw_init(struct qed_dev *cdev, struct qed_hw_init_params *p_params);

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
 * @return int
 */
int qed_hw_stop_fastpath(struct qed_dev *cdev);

/**
 * @brief qed_hw_start_fastpath -restart fastpath traffic,
 *		only if hw_stop_fastpath was called
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_hw_start_fastpath(struct qed_hwfn *p_hwfn);


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
 * @brief qed_llh_add_mac_filter - configures a MAC filter in llh
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_filter - MAC to add
 */
int qed_llh_add_mac_filter(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u8 *p_filter);

/**
 * @brief qed_llh_remove_mac_filter - removes a MAC filter from llh
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_filter - MAC to remove
 */
void qed_llh_remove_mac_filter(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt, u8 *p_filter);

enum qed_llh_port_filter_type_t {
	QED_LLH_FILTER_ETHERTYPE,
	QED_LLH_FILTER_TCP_SRC_PORT,
	QED_LLH_FILTER_TCP_DEST_PORT,
	QED_LLH_FILTER_TCP_SRC_AND_DEST_PORT,
	QED_LLH_FILTER_UDP_SRC_PORT,
	QED_LLH_FILTER_UDP_DEST_PORT,
	QED_LLH_FILTER_UDP_SRC_AND_DEST_PORT
};

/**
 * @brief qed_llh_add_protocol_filter - configures a protocol filter in llh
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_port_or_eth_type - source port or ethertype to add
 * @param dest_port - destination port to add
 * @param type - type of filters and comparing
 */
int
qed_llh_add_protocol_filter(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u16 source_port_or_eth_type,
			    u16 dest_port,
			    enum qed_llh_port_filter_type_t type);

/**
 * @brief qed_llh_remove_protocol_filter - remove a protocol filter in llh
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_port_or_eth_type - source port or ethertype to add
 * @param dest_port - destination port to add
 * @param type - type of filters and comparing
 */
void
qed_llh_remove_protocol_filter(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u16 source_port_or_eth_type,
			       u16 dest_port,
			       enum qed_llh_port_filter_type_t type);

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
			 u16 coalesce, u16 qid, u16 sb_id);

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
			 u16 coalesce, u16 qid, u16 sb_id);

const char *qed_hw_get_resc_name(enum qed_resources res_id);
#endif

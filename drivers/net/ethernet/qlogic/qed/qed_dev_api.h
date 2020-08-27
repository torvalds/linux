/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
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

/**
 * @brief qed_dmae_host2grc - copy data from source addr to
 * dmae registers using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_addr
 * @param grc_addr (dmae_data_offset)
 * @param size_in_dwords
 * @param p_params (default parameters will be used in case of NULL)
 */
int
qed_dmae_host2grc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt,
		  u64 source_addr,
		  u32 grc_addr,
		  u32 size_in_dwords,
		  struct qed_dmae_params *p_params);

 /**
 * @brief qed_dmae_grc2host - Read data from dmae data offset
 * to source address using the given ptt
 *
 * @param p_ptt
 * @param grc_addr (dmae_data_offset)
 * @param dest_addr
 * @param size_in_dwords
 * @param p_params (default parameters will be used in case of NULL)
 */
int qed_dmae_grc2host(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		      u32 grc_addr, dma_addr_t dest_addr, u32 size_in_dwords,
		      struct qed_dmae_params *p_params);

/**
 * @brief qed_dmae_host2host - copy data from to source address
 * to a destination adress (for SRIOV) using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param source_addr
 * @param dest_addr
 * @param size_in_dwords
 * @param p_params (default parameters will be used in case of NULL)
 */
int qed_dmae_host2host(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       dma_addr_t source_addr,
		       dma_addr_t dest_addr,
		       u32 size_in_dwords, struct qed_dmae_params *p_params);

int qed_chain_alloc(struct qed_dev *cdev, struct qed_chain *chain,
		    struct qed_chain_init_params *params);
void qed_chain_free(struct qed_dev *cdev, struct qed_chain *chain);

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
 * @brief qed_llh_get_num_ppfid - Return the allocated number of LLH filter
 *	banks that are allocated to the PF.
 *
 * @param cdev
 *
 * @return u8 - Number of LLH filter banks
 */
u8 qed_llh_get_num_ppfid(struct qed_dev *cdev);

enum qed_eng {
	QED_ENG0,
	QED_ENG1,
	QED_BOTH_ENG,
};

/**
 * @brief qed_llh_set_ppfid_affinity - Set the engine affinity for the given
 *	LLH filter bank.
 *
 * @param cdev
 * @param ppfid - relative within the allocated ppfids ('0' is the default one).
 * @param eng
 *
 * @return int
 */
int qed_llh_set_ppfid_affinity(struct qed_dev *cdev,
			       u8 ppfid, enum qed_eng eng);

/**
 * @brief qed_llh_set_roce_affinity - Set the RoCE engine affinity
 *
 * @param cdev
 * @param eng
 *
 * @return int
 */
int qed_llh_set_roce_affinity(struct qed_dev *cdev, enum qed_eng eng);

/**
 * @brief qed_llh_add_mac_filter - Add a LLH MAC filter into the given filter
 *	bank.
 *
 * @param cdev
 * @param ppfid - relative within the allocated ppfids ('0' is the default one).
 * @param mac_addr - MAC to add
 */
int qed_llh_add_mac_filter(struct qed_dev *cdev,
			   u8 ppfid, u8 mac_addr[ETH_ALEN]);

/**
 * @brief qed_llh_remove_mac_filter - Remove a LLH MAC filter from the given
 *	filter bank.
 *
 * @param p_ptt
 * @param p_filter - MAC to remove
 */
void qed_llh_remove_mac_filter(struct qed_dev *cdev,
			       u8 ppfid, u8 mac_addr[ETH_ALEN]);

enum qed_llh_prot_filter_type_t {
	QED_LLH_FILTER_ETHERTYPE,
	QED_LLH_FILTER_TCP_SRC_PORT,
	QED_LLH_FILTER_TCP_DEST_PORT,
	QED_LLH_FILTER_TCP_SRC_AND_DEST_PORT,
	QED_LLH_FILTER_UDP_SRC_PORT,
	QED_LLH_FILTER_UDP_DEST_PORT,
	QED_LLH_FILTER_UDP_SRC_AND_DEST_PORT
};

/**
 * @brief qed_llh_add_protocol_filter - Add a LLH protocol filter into the
 *	given filter bank.
 *
 * @param cdev
 * @param ppfid - relative within the allocated ppfids ('0' is the default one).
 * @param type - type of filters and comparing
 * @param source_port_or_eth_type - source port or ethertype to add
 * @param dest_port - destination port to add
 * @param type - type of filters and comparing
 */
int
qed_llh_add_protocol_filter(struct qed_dev *cdev,
			    u8 ppfid,
			    enum qed_llh_prot_filter_type_t type,
			    u16 source_port_or_eth_type, u16 dest_port);

/**
 * @brief qed_llh_remove_protocol_filter - Remove a LLH protocol filter from
 *	the given filter bank.
 *
 * @param cdev
 * @param ppfid - relative within the allocated ppfids ('0' is the default one).
 * @param type - type of filters and comparing
 * @param source_port_or_eth_type - source port or ethertype to add
 * @param dest_port - destination port to add
 */
void
qed_llh_remove_protocol_filter(struct qed_dev *cdev,
			       u8 ppfid,
			       enum qed_llh_prot_filter_type_t type,
			       u16 source_port_or_eth_type, u16 dest_port);

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
 * @brief qed_get_queue_coalesce - Retrieve coalesce value for a given queue.
 *
 * @param p_hwfn
 * @param p_coal - store coalesce value read from the hardware.
 * @param p_handle
 *
 * @return int
 **/
int qed_get_queue_coalesce(struct qed_hwfn *p_hwfn, u16 *coal, void *handle);

/**
 * @brief qed_set_queue_coalesce - Configure coalesce parameters for Rx and
 *    Tx queue. The fact that we can configure coalescing to up to 511, but on
 *    varying accuracy [the bigger the value the less accurate] up to a mistake
 *    of 3usec for the highest values.
 *    While the API allows setting coalescing per-qid, all queues sharing a SB
 *    should be in same range [i.e., either 0-0x7f, 0x80-0xff or 0x100-0x1ff]
 *    otherwise configuration would break.
 *
 *
 * @param rx_coal - Rx Coalesce value in micro seconds.
 * @param tx_coal - TX Coalesce value in micro seconds.
 * @param p_handle
 *
 * @return int
 **/
int
qed_set_queue_coalesce(u16 rx_coal, u16 tx_coal, void *p_handle);

/**
 * @brief qed_pglueb_set_pfid_enable - Enable or disable PCI BUS MASTER
 *
 * @param p_hwfn
 * @param p_ptt
 * @param b_enable - true/false
 *
 * @return int
 */
int qed_pglueb_set_pfid_enable(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt, bool b_enable);

/**
 * @brief db_recovery_add - add doorbell information to the doorbell
 * recovery mechanism.
 *
 * @param cdev
 * @param db_addr - doorbell address
 * @param db_data - address of where db_data is stored
 * @param db_width - doorbell is 32b pr 64b
 * @param db_space - doorbell recovery addresses are user or kernel space
 */
int qed_db_recovery_add(struct qed_dev *cdev,
			void __iomem *db_addr,
			void *db_data,
			enum qed_db_rec_width db_width,
			enum qed_db_rec_space db_space);

/**
 * @brief db_recovery_del - remove doorbell information from the doorbell
 * recovery mechanism. db_data serves as key (db_addr is not unique).
 *
 * @param cdev
 * @param db_addr - doorbell address
 * @param db_data - address where db_data is stored. Serves as key for the
 *                  entry to delete.
 */
int qed_db_recovery_del(struct qed_dev *cdev,
			void __iomem *db_addr, void *db_data);


const char *qed_hw_get_resc_name(enum qed_resources res_id);
#endif

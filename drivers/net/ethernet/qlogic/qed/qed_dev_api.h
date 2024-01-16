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
 * qed_init_dp(): Initialize the debug level.
 *
 * @cdev: Qed dev pointer.
 * @dp_module: Module debug parameter.
 * @dp_level: Module debug level.
 *
 * Return: Void.
 */
void qed_init_dp(struct qed_dev *cdev,
		 u32 dp_module,
		 u8 dp_level);

/**
 * qed_init_struct(): Initialize the device structure to
 *                    its defaults.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Void.
 */
void qed_init_struct(struct qed_dev *cdev);

/**
 * qed_resc_free: Free device resources.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Void.
 */
void qed_resc_free(struct qed_dev *cdev);

/**
 * qed_resc_alloc(): Alloc device resources.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_resc_alloc(struct qed_dev *cdev);

/**
 * qed_resc_setup(): Setup device resources.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Void.
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
 * qed_hw_init(): Init Qed hardware.
 *
 * @cdev: Qed dev pointer.
 * @p_params: Pointers to params.
 *
 * Return: Int.
 */
int qed_hw_init(struct qed_dev *cdev, struct qed_hw_init_params *p_params);

/**
 * qed_hw_timers_stop_all(): Stop the timers HW block.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: void.
 */
void qed_hw_timers_stop_all(struct qed_dev *cdev);

/**
 * qed_hw_stop(): Stop Qed hardware.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: int.
 */
int qed_hw_stop(struct qed_dev *cdev);

/**
 * qed_hw_stop_fastpath(): Should be called incase
 *		           slowpath is still required for the device,
 *		           but fastpath is not.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_hw_stop_fastpath(struct qed_dev *cdev);

/**
 * qed_hw_start_fastpath(): Restart fastpath traffic,
 *		            only if hw_stop_fastpath was called.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Int.
 */
int qed_hw_start_fastpath(struct qed_hwfn *p_hwfn);

/**
 * qed_hw_prepare(): Prepare Qed hardware.
 *
 * @cdev: Qed dev pointer.
 * @personality: Personality to initialize.
 *
 * Return: Int.
 */
int qed_hw_prepare(struct qed_dev *cdev,
		   int personality);

/**
 * qed_hw_remove(): Remove Qed hardware.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Void.
 */
void qed_hw_remove(struct qed_dev *cdev);

/**
 * qed_ptt_acquire(): Allocate a PTT window.
 *
 * @p_hwfn: HW device data.
 *
 * Return: struct qed_ptt.
 *
 * Should be called at the entry point to the driver (at the beginning of an
 * exported function).
 */
struct qed_ptt *qed_ptt_acquire(struct qed_hwfn *p_hwfn);

/**
 * qed_ptt_acquire_context(): Allocate a PTT window honoring the context
 *			      atomicy.
 *
 * @p_hwfn: HW device data.
 * @is_atomic: Hint from the caller - if the func can sleep or not.
 *
 * Context: The function should not sleep in case is_atomic == true.
 * Return: struct qed_ptt.
 *
 * Should be called at the entry point to the driver
 * (at the beginning of an exported function).
 */
struct qed_ptt *qed_ptt_acquire_context(struct qed_hwfn *p_hwfn,
					bool is_atomic);

/**
 * qed_ptt_release(): Release PTT Window.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 *
 * Should be called at the end of a flow - at the end of the function that
 * acquired the PTT.
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
 * qed_dmae_host2grc(): Copy data from source addr to
 *                      dmae registers using the given ptt.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @source_addr: Source address.
 * @grc_addr: GRC address (dmae_data_offset).
 * @size_in_dwords: Size.
 * @p_params: (default parameters will be used in case of NULL).
 *
 * Return: Int.
 */
int
qed_dmae_host2grc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt,
		  u64 source_addr,
		  u32 grc_addr,
		  u32 size_in_dwords,
		  struct qed_dmae_params *p_params);

 /**
 * qed_dmae_grc2host(): Read data from dmae data offset
 *                      to source address using the given ptt.
 *
 * @p_ptt: P_ptt.
 * @grc_addr: GRC address (dmae_data_offset).
 * @dest_addr: Destination Address.
 * @size_in_dwords: Size.
 * @p_params: (default parameters will be used in case of NULL).
 *
 * Return: Int.
 */
int qed_dmae_grc2host(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		      u32 grc_addr, dma_addr_t dest_addr, u32 size_in_dwords,
		      struct qed_dmae_params *p_params);

/**
 * qed_dmae_host2host(): Copy data from to source address
 *                       to a destination adrress (for SRIOV) using the given
 *                       ptt.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @source_addr: Source address.
 * @dest_addr: Destination address.
 * @size_in_dwords: size.
 * @p_params: (default parameters will be used in case of NULL).
 *
 * Return: Int.
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
 * qed_fw_l2_queue(): Get absolute L2 queue ID.
 *
 * @p_hwfn: HW device data.
 * @src_id: Relative to p_hwfn.
 * @dst_id: Absolute per engine.
 *
 * Return: Int.
 */
int qed_fw_l2_queue(struct qed_hwfn *p_hwfn,
		    u16 src_id,
		    u16 *dst_id);

/**
 * qed_fw_vport(): Get absolute vport ID.
 *
 * @p_hwfn: HW device data.
 * @src_id: Relative to p_hwfn.
 * @dst_id: Absolute per engine.
 *
 * Return: Int.
 */
int qed_fw_vport(struct qed_hwfn *p_hwfn,
		 u8 src_id,
		 u8 *dst_id);

/**
 * qed_fw_rss_eng(): Get absolute RSS engine ID.
 *
 * @p_hwfn: HW device data.
 * @src_id: Relative to p_hwfn.
 * @dst_id: Absolute per engine.
 *
 * Return: Int.
 */
int qed_fw_rss_eng(struct qed_hwfn *p_hwfn,
		   u8 src_id,
		   u8 *dst_id);

/**
 * qed_llh_get_num_ppfid(): Return the allocated number of LLH filter
 *	                    banks that are allocated to the PF.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: u8 Number of LLH filter banks.
 */
u8 qed_llh_get_num_ppfid(struct qed_dev *cdev);

enum qed_eng {
	QED_ENG0,
	QED_ENG1,
	QED_BOTH_ENG,
};

/**
 * qed_llh_set_ppfid_affinity(): Set the engine affinity for the given
 *	                         LLH filter bank.
 *
 * @cdev: Qed dev pointer.
 * @ppfid: Relative within the allocated ppfids ('0' is the default one).
 * @eng: Engine.
 *
 * Return: Int.
 */
int qed_llh_set_ppfid_affinity(struct qed_dev *cdev,
			       u8 ppfid, enum qed_eng eng);

/**
 * qed_llh_set_roce_affinity(): Set the RoCE engine affinity.
 *
 * @cdev: Qed dev pointer.
 * @eng: Engine.
 *
 * Return: Int.
 */
int qed_llh_set_roce_affinity(struct qed_dev *cdev, enum qed_eng eng);

/**
 * qed_llh_add_mac_filter(): Add a LLH MAC filter into the given filter
 *	                     bank.
 *
 * @cdev: Qed dev pointer.
 * @ppfid: Relative within the allocated ppfids ('0' is the default one).
 * @mac_addr: MAC to add.
 *
 * Return: Int.
 */
int qed_llh_add_mac_filter(struct qed_dev *cdev,
			   u8 ppfid, const u8 mac_addr[ETH_ALEN]);

/**
 * qed_llh_remove_mac_filter(): Remove a LLH MAC filter from the given
 *	                        filter bank.
 *
 * @cdev: Qed dev pointer.
 * @ppfid: Ppfid.
 * @mac_addr: MAC to remove
 *
 * Return: Void.
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
 * qed_llh_add_protocol_filter(): Add a LLH protocol filter into the
 *	                          given filter bank.
 *
 * @cdev: Qed dev pointer.
 * @ppfid: Relative within the allocated ppfids ('0' is the default one).
 * @type: Type of filters and comparing.
 * @source_port_or_eth_type: Source port or ethertype to add.
 * @dest_port: Destination port to add.
 *
 * Return: Int.
 */
int
qed_llh_add_protocol_filter(struct qed_dev *cdev,
			    u8 ppfid,
			    enum qed_llh_prot_filter_type_t type,
			    u16 source_port_or_eth_type, u16 dest_port);

/**
 * qed_llh_remove_protocol_filter(): Remove a LLH protocol filter from
 *	                             the given filter bank.
 *
 * @cdev: Qed dev pointer.
 * @ppfid: Relative within the allocated ppfids ('0' is the default one).
 * @type: Type of filters and comparing.
 * @source_port_or_eth_type: Source port or ethertype to add.
 * @dest_port: Destination port to add.
 */
void
qed_llh_remove_protocol_filter(struct qed_dev *cdev,
			       u8 ppfid,
			       enum qed_llh_prot_filter_type_t type,
			       u16 source_port_or_eth_type, u16 dest_port);

/**
 * qed_final_cleanup(): Cleanup of previous driver remains prior to load.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @id: For PF, engine-relative. For VF, PF-relative.
 * @is_vf: True iff cleanup is made for a VF.
 *
 * Return: Int.
 */
int qed_final_cleanup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, u16 id, bool is_vf);

/**
 * qed_get_queue_coalesce(): Retrieve coalesce value for a given queue.
 *
 * @p_hwfn: HW device data.
 * @coal: Store coalesce value read from the hardware.
 * @handle: P_handle.
 *
 * Return: Int.
 **/
int qed_get_queue_coalesce(struct qed_hwfn *p_hwfn, u16 *coal, void *handle);

/**
 * qed_set_queue_coalesce(): Configure coalesce parameters for Rx and
 *    Tx queue. The fact that we can configure coalescing to up to 511, but on
 *    varying accuracy [the bigger the value the less accurate] up to a mistake
 *    of 3usec for the highest values.
 *    While the API allows setting coalescing per-qid, all queues sharing a SB
 *    should be in same range [i.e., either 0-0x7f, 0x80-0xff or 0x100-0x1ff]
 *    otherwise configuration would break.
 *
 * @rx_coal: Rx Coalesce value in micro seconds.
 * @tx_coal: TX Coalesce value in micro seconds.
 * @p_handle: P_handle.
 *
 * Return: Int.
 **/
int
qed_set_queue_coalesce(u16 rx_coal, u16 tx_coal, void *p_handle);

/**
 * qed_pglueb_set_pfid_enable(): Enable or disable PCI BUS MASTER.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @b_enable: True/False.
 *
 * Return: Int.
 */
int qed_pglueb_set_pfid_enable(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt, bool b_enable);

/**
 * qed_db_recovery_add(): add doorbell information to the doorbell
 *                    recovery mechanism.
 *
 * @cdev: Qed dev pointer.
 * @db_addr: Doorbell address.
 * @db_data: Address of where db_data is stored.
 * @db_width: Doorbell is 32b pr 64b.
 * @db_space: Doorbell recovery addresses are user or kernel space.
 *
 * Return: Int.
 */
int qed_db_recovery_add(struct qed_dev *cdev,
			void __iomem *db_addr,
			void *db_data,
			enum qed_db_rec_width db_width,
			enum qed_db_rec_space db_space);

/**
 * qed_db_recovery_del() - remove doorbell information from the doorbell
 * recovery mechanism. db_data serves as key (db_addr is not unique).
 *
 * @cdev: Qed dev pointer.
 * @db_addr: doorbell address.
 * @db_data: address where db_data is stored. Serves as key for the
 *                  entry to delete.
 *
 * Return: Int.
 */
int qed_db_recovery_del(struct qed_dev *cdev,
			void __iomem *db_addr, void *db_data);

const char *qed_hw_get_resc_name(enum qed_resources res_id);
#endif

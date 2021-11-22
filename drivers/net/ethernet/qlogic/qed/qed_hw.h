/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_HW_H
#define _QED_HW_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_dev_api.h"

/* Forward decleration */
struct qed_ptt;

enum reserved_ptts {
	RESERVED_PTT_EDIAG,
	RESERVED_PTT_USER_SPACE,
	RESERVED_PTT_MAIN,
	RESERVED_PTT_DPC,
	RESERVED_PTT_MAX
};

enum _dmae_cmd_dst_mask {
	DMAE_CMD_DST_MASK_NONE	= 0,
	DMAE_CMD_DST_MASK_PCIE	= 1,
	DMAE_CMD_DST_MASK_GRC	= 2
};

enum _dmae_cmd_src_mask {
	DMAE_CMD_SRC_MASK_PCIE	= 0,
	DMAE_CMD_SRC_MASK_GRC	= 1
};

enum _dmae_cmd_crc_mask {
	DMAE_CMD_COMP_CRC_EN_MASK_NONE	= 0,
	DMAE_CMD_COMP_CRC_EN_MASK_SET	= 1
};

/* definitions for DMA constants */
#define DMAE_GO_VALUE   0x1

#define DMAE_COMPLETION_VAL     0xD1AE
#define DMAE_CMD_ENDIANITY      0x2

#define DMAE_CMD_SIZE   14
#define DMAE_CMD_SIZE_TO_FILL   (DMAE_CMD_SIZE - 5)
#define DMAE_MIN_WAIT_TIME      0x2
#define DMAE_MAX_CLIENTS        32

/**
 * qed_gtt_init(): Initialize GTT windows.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_gtt_init(struct qed_hwfn *p_hwfn);

/**
 * qed_ptt_invalidate(): Forces all ptt entries to be re-configured
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_ptt_invalidate(struct qed_hwfn *p_hwfn);

/**
 * qed_ptt_pool_alloc(): Allocate and initialize PTT pool.
 *
 * @p_hwfn: HW device data.
 *
 * Return: struct _qed_status - success (0), negative - error.
 */
int qed_ptt_pool_alloc(struct qed_hwfn *p_hwfn);

/**
 * qed_ptt_pool_free(): Free PTT pool.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_ptt_pool_free(struct qed_hwfn *p_hwfn);

/**
 * qed_ptt_get_hw_addr(): Get PTT's GRC/HW address.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt
 *
 * Return: u32.
 */
u32 qed_ptt_get_hw_addr(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt);

/**
 * qed_ptt_get_bar_addr(): Get PPT's external BAR address.
 *
 * @p_ptt: P_ptt
 *
 * Return: u32.
 */
u32 qed_ptt_get_bar_addr(struct qed_ptt *p_ptt);

/**
 * qed_ptt_set_win(): Set PTT Window's GRC BAR address
 *
 * @p_hwfn: HW device data.
 * @new_hw_addr: New HW address.
 * @p_ptt: P_Ptt
 *
 * Return: Void.
 */
void qed_ptt_set_win(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u32 new_hw_addr);

/**
 * qed_get_reserved_ptt(): Get a specific reserved PTT.
 *
 * @p_hwfn: HW device data.
 * @ptt_idx: Ptt Index.
 *
 * Return: struct qed_ptt *.
 */
struct qed_ptt *qed_get_reserved_ptt(struct qed_hwfn *p_hwfn,
				     enum reserved_ptts ptt_idx);

/**
 * qed_wr(): Write value to BAR using the given ptt.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @val: Val.
 * @hw_addr: HW address
 *
 * Return: Void.
 */
void qed_wr(struct qed_hwfn *p_hwfn,
	    struct qed_ptt *p_ptt,
	    u32 hw_addr,
	    u32 val);

/**
 * qed_rd(): Read value from BAR using the given ptt.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @hw_addr: HW address
 *
 * Return: Void.
 */
u32 qed_rd(struct qed_hwfn *p_hwfn,
	   struct qed_ptt *p_ptt,
	   u32 hw_addr);

/**
 * qed_memcpy_from(): Copy n bytes from BAR using the given ptt.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @dest: Destination.
 * @hw_addr: HW address.
 * @n: N
 *
 * Return: Void.
 */
void qed_memcpy_from(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     void *dest,
		     u32 hw_addr,
		     size_t n);

/**
 * qed_memcpy_to(): Copy n bytes to BAR using the given  ptt
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @hw_addr: HW address.
 * @src: Source.
 * @n: N
 *
 * Return: Void.
 */
void qed_memcpy_to(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   u32 hw_addr,
		   void *src,
		   size_t n);
/**
 * qed_fid_pretend(): pretend to another function when
 *                    accessing the ptt window. There is no way to unpretend
 *                    a function. The only way to cancel a pretend is to
 *                    pretend back to the original function.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @fid: fid field of pxp_pretend structure. Can contain
 *        either pf / vf, port/path fields are don't care.
 *
 * Return: Void.
 */
void qed_fid_pretend(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u16 fid);

/**
 * qed_port_pretend(): Pretend to another port when accessing the ptt window
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @port_id: The port to pretend to
 *
 * Return: Void.
 */
void qed_port_pretend(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u8 port_id);

/**
 * qed_port_unpretend(): Cancel any previously set port pretend
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
void qed_port_unpretend(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt);

/**
 * qed_port_fid_pretend(): Pretend to another port and another function
 *                         when accessing the ptt window
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @port_id: The port to pretend to
 * @fid: fid field of pxp_pretend structure. Can contain either pf / vf.
 *
 * Return: Void.
 */
void qed_port_fid_pretend(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 port_id, u16 fid);

/**
 * qed_vfid_to_concrete(): Build a concrete FID for a given VF ID
 *
 * @p_hwfn: HW device data.
 * @vfid: VFID.
 *
 * Return: Void.
 */
u32 qed_vfid_to_concrete(struct qed_hwfn *p_hwfn, u8 vfid);

/**
 * qed_dmae_idx_to_go_cmd(): Map the idx to dmae cmd
 *    this is declared here since other files will require it.
 *
 * @idx: Index
 *
 * Return: Void.
 */
u32 qed_dmae_idx_to_go_cmd(u8 idx);

/**
 * qed_dmae_info_alloc(): Init the dmae_info structure
 *                        which is part of p_hwfn.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Int.
 */
int qed_dmae_info_alloc(struct qed_hwfn *p_hwfn);

/**
 * qed_dmae_info_free(): Free the dmae_info structure
 *                       which is part of p_hwfn.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_dmae_info_free(struct qed_hwfn *p_hwfn);

union qed_qm_pq_params {
	struct {
		u8 q_idx;
	} iscsi;

	struct {
		u8 tc;
	}	core;

	struct {
		u8	is_vf;
		u8	vf_id;
		u8	tc;
	}	eth;

	struct {
		u8 dcqcn;
		u8 qpid;	/* roce relative */
	} roce;
};

int qed_init_fw_data(struct qed_dev *cdev,
		     const u8 *fw_data);

int qed_dmae_sanity(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, const char *phase);

#define QED_HW_ERR_MAX_STR_SIZE 256

/**
 * qed_hw_err_notify(): Notify upper layer driver and management FW
 *                      about a HW error.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @err_type: Err Type.
 * @fmt: Debug data buffer to send to the MFW
 * @...: buffer format args
 *
 * Return void.
 */
void __printf(4, 5) __cold qed_hw_err_notify(struct qed_hwfn *p_hwfn,
					     struct qed_ptt *p_ptt,
					     enum qed_hw_err_type err_type,
					     const char *fmt, ...);
#endif

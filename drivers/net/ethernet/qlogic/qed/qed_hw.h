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
 * @brief qed_gtt_init - Initialize GTT windows
 *
 * @param p_hwfn
 */
void qed_gtt_init(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ptt_invalidate - Forces all ptt entries to be re-configured
 *
 * @param p_hwfn
 */
void qed_ptt_invalidate(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ptt_pool_alloc - Allocate and initialize PTT pool
 *
 * @param p_hwfn
 *
 * @return struct _qed_status - success (0), negative - error.
 */
int qed_ptt_pool_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ptt_pool_free -
 *
 * @param p_hwfn
 */
void qed_ptt_pool_free(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_ptt_get_hw_addr - Get PTT's GRC/HW address
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return u32
 */
u32 qed_ptt_get_hw_addr(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt);

/**
 * @brief qed_ptt_get_bar_addr - Get PPT's external BAR address
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return u32
 */
u32 qed_ptt_get_bar_addr(struct qed_ptt *p_ptt);

/**
 * @brief qed_ptt_set_win - Set PTT Window's GRC BAR address
 *
 * @param p_hwfn
 * @param new_hw_addr
 * @param p_ptt
 */
void qed_ptt_set_win(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u32 new_hw_addr);

/**
 * @brief qed_get_reserved_ptt - Get a specific reserved PTT
 *
 * @param p_hwfn
 * @param ptt_idx
 *
 * @return struct qed_ptt *
 */
struct qed_ptt *qed_get_reserved_ptt(struct qed_hwfn *p_hwfn,
				     enum reserved_ptts ptt_idx);

/**
 * @brief qed_wr - Write value to BAR using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param val
 * @param hw_addr
 */
void qed_wr(struct qed_hwfn *p_hwfn,
	    struct qed_ptt *p_ptt,
	    u32 hw_addr,
	    u32 val);

/**
 * @brief qed_rd - Read value from BAR using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param val
 * @param hw_addr
 */
u32 qed_rd(struct qed_hwfn *p_hwfn,
	   struct qed_ptt *p_ptt,
	   u32 hw_addr);

/**
 * @brief qed_memcpy_from - copy n bytes from BAR using the given
 *        ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param dest
 * @param hw_addr
 * @param n
 */
void qed_memcpy_from(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     void *dest,
		     u32 hw_addr,
		     size_t n);

/**
 * @brief qed_memcpy_to - copy n bytes to BAR using the given
 *        ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param hw_addr
 * @param src
 * @param n
 */
void qed_memcpy_to(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   u32 hw_addr,
		   void *src,
		   size_t n);
/**
 * @brief qed_fid_pretend - pretend to another function when
 *        accessing the ptt window. There is no way to unpretend
 *        a function. The only way to cancel a pretend is to
 *        pretend back to the original function.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param fid - fid field of pxp_pretend structure. Can contain
 *            either pf / vf, port/path fields are don't care.
 */
void qed_fid_pretend(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u16 fid);

/**
 * @brief qed_port_pretend - pretend to another port when
 *        accessing the ptt window
 *
 * @param p_hwfn
 * @param p_ptt
 * @param port_id - the port to pretend to
 */
void qed_port_pretend(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u8 port_id);

/**
 * @brief qed_port_unpretend - cancel any previously set port
 *        pretend
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_port_unpretend(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt);

/**
 * @brief qed_port_fid_pretend - pretend to another port and another function
 *        when accessing the ptt window
 *
 * @param p_hwfn
 * @param p_ptt
 * @param port_id - the port to pretend to
 * @param fid - fid field of pxp_pretend structure. Can contain either pf / vf.
 */
void qed_port_fid_pretend(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 port_id, u16 fid);

/**
 * @brief qed_vfid_to_concrete - build a concrete FID for a
 *        given VF ID
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfid
 */
u32 qed_vfid_to_concrete(struct qed_hwfn *p_hwfn, u8 vfid);

/**
 * @brief qed_dmae_idx_to_go_cmd - map the idx to dmae cmd
 * this is declared here since other files will require it.
 * @param idx
 */
u32 qed_dmae_idx_to_go_cmd(u8 idx);

/**
 * @brief qed_dmae_info_alloc - Init the dmae_info structure
 * which is part of p_hwfn.
 * @param p_hwfn
 */
int qed_dmae_info_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_dmae_info_free - Free the dmae_info structure
 * which is part of p_hwfn
 *
 * @param p_hwfn
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

#endif

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

#ifndef _QED_INIT_OPS_H
#define _QED_INIT_OPS_H

#include <linux/types.h>
#include <linux/slab.h>
#include "qed.h"

/**
 * @brief qed_init_iro_array - init iro_arr.
 *
 *
 * @param cdev
 */
void qed_init_iro_array(struct qed_dev *cdev);

/**
 * @brief qed_init_run - Run the init-sequence.
 *
 *
 * @param p_hwfn
 * @param p_ptt
 * @param phase
 * @param phase_id
 * @param modes
 * @return _qed_status_t
 */
int qed_init_run(struct qed_hwfn *p_hwfn,
		 struct qed_ptt *p_ptt,
		 int phase,
		 int phase_id,
		 int modes);

/**
 * @brief qed_init_hwfn_allocate - Allocate RT array, Store 'values' ptrs.
 *
 *
 * @param p_hwfn
 *
 * @return _qed_status_t
 */
int qed_init_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_init_hwfn_deallocate
 *
 *
 * @param p_hwfn
 */
void qed_init_free(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_init_store_rt_reg - Store a configuration value in the RT array.
 *
 *
 * @param p_hwfn
 * @param rt_offset
 * @param val
 */
void qed_init_store_rt_reg(struct qed_hwfn *p_hwfn,
			   u32 rt_offset,
			   u32 val);

#define STORE_RT_REG(hwfn, offset, val)	\
	qed_init_store_rt_reg(hwfn, offset, val)

#define OVERWRITE_RT_REG(hwfn, offset, val) \
	qed_init_store_rt_reg(hwfn, offset, val)

/**
 * @brief
 *
 *
 * @param p_hwfn
 * @param rt_offset
 * @param val
 * @param size
 */
void qed_init_store_rt_agg(struct qed_hwfn *p_hwfn,
			   u32 rt_offset,
			   u32 *val,
			   size_t size);

#define STORE_RT_REG_AGG(hwfn, offset, val) \
	qed_init_store_rt_agg(hwfn, offset, (u32 *)&val, sizeof(val))

/**
 * @brief
 *      Initialize GTT global windows and set admin window
 *      related params of GTT/PTT to default values.
 *
 * @param p_hwfn
 */
void qed_gtt_init(struct qed_hwfn *p_hwfn);
#endif

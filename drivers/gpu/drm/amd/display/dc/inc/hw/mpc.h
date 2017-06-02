/* Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DC_MPC_H__
#define __DC_MPC_H__

/* This structure define the mpc tree configuration
 * num_pipes - number of pipes of the tree
 * opp_id - instance id of OPP to drive MPC
 * dpp- array of DPP index
 * mpcc - array of MPCC index
 * mode	- the most bottom layer MPCC mode control.
 *  All other layers need to be program to 3
 *
 * The connection will be:
 * mpcc[num_pipes-1]->mpcc[num_pipes-2]->...->mpcc[1]->mpcc[0]->OPP[opp_id]
 * dpp[0]->mpcc[0]
 * dpp[1]->mpcc[1]
 * ...
 * dpp[num_pipes-1]->mpcc[num_pipes-1]
 * mpcc[0] is the most top layer of MPC tree,
 * mpcc[num_pipes-1] is the most bottom layer.
 */

struct mpc_tree_cfg {
	uint8_t num_pipes;
	uint8_t opp_id;
	/* dpp pipes for blend */
	uint8_t dpp[6];
	/* mpcc insatnces for blend */
	uint8_t mpcc[6];
	bool per_pixel_alpha[6];
};

struct mpcc_blnd_cfg {
	/* 0- perpixel alpha, 1- perpixel alpha combined with global gain,
	 * 2- global alpha
	 */
	uint8_t alpha_mode;
	uint8_t global_gain;
	uint8_t global_alpha;
	bool overlap_only;
	bool pre_multiplied_alpha;
};

struct mpcc_sm_cfg {
	bool enable;
	/* 0-single plane, 2-row subsampling, 4-column subsampling,
	 * 6-checkboard subsampling
	 */
	uint8_t sm_mode;
	bool frame_alt; /* 0- disable, 1- enable */
	bool field_alt; /* 0- disable, 1- enable */
	/* 0-no force, 2-force frame polarity from top,
	 * 3-force frame polarity from bottom
	 */
	uint8_t force_next_frame_porlarity;
	/* 0-no force, 2-force field polarity from top,
	 * 3-force field polarity from bottom
	 */
	uint8_t force_next_field_polarity;
};

struct mpcc_vupdate_lock_cfg {
	bool cfg_lock;
	bool adr_lock;
	bool adr_cfg_lock;
	bool cur0_lock;
	bool cur1_lock;
};

struct mpc {
	struct dc_context *ctx;
};

#endif

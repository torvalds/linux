/* Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DC_PG_CNTL_H__
#define __DC_PG_CNTL_H__

#include "dc.h"
#include "dc_types.h"
#include "hw_shared.h"

struct pg_cntl {
	struct dc_context *ctx;
	const struct pg_cntl_funcs *funcs;
	bool pg_pipe_res_enable[PG_HW_PIPE_RESOURCES_NUM_ELEMENT][MAX_PIPES];
	bool pg_res_enable[PG_HW_RESOURCES_NUM_ELEMENT];
};

struct pg_cntl_funcs {
	void (*dsc_pg_control)(struct pg_cntl *pg_cntl, unsigned int dsc_inst, bool power_on);
	void (*hubp_dpp_pg_control)(struct pg_cntl *pg_cntl, unsigned int hubp_dpp_inst, bool power_on);
	void (*hpo_pg_control)(struct pg_cntl *pg_cntl, bool power_on);
	void (*io_clk_pg_control)(struct pg_cntl *pg_cntl, bool power_on);
	void (*plane_otg_pg_control)(struct pg_cntl *pg_cntl, bool power_on);
	void (*mpcc_pg_control)(struct pg_cntl *pg_cntl, unsigned int mpcc_inst, bool power_on);
	void (*opp_pg_control)(struct pg_cntl *pg_cntl, unsigned int opp_inst, bool power_on);
	void (*optc_pg_control)(struct pg_cntl *pg_cntl, unsigned int optc_inst, bool power_on);
	void (*dwb_pg_control)(struct pg_cntl *pg_cntl, bool power_on);
	void (*init_pg_status)(struct pg_cntl *pg_cntl);

	void (*set_force_poweron_domain22)(struct pg_cntl *pg_cntl, bool power_on);
};

#endif //__DC_PG_CNTL_H__

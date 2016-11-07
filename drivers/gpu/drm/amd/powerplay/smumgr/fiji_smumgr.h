/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */
#ifndef _FIJI_SMUMANAGER_H_
#define _FIJI_SMUMANAGER_H_

#include "smu73_discrete.h"
#include <pp_endian.h>
#include "smu7_smumgr.h"



struct fiji_smu_avfs {
	enum AVFS_BTC_STATUS AvfsBtcStatus;
	uint32_t           AvfsBtcParam;
};


struct fiji_smumgr {
	struct smu7_smumgr                   smu7_data;

	struct fiji_smu_avfs avfs;
	struct SMU73_Discrete_DpmTable       smc_state_table;
	struct SMU73_Discrete_Ulv            ulv_setting;
	struct SMU73_Discrete_PmFuses  power_tune_table;
	const struct fiji_pt_defaults  *power_tune_defaults;
	uint32_t        activity_target[SMU73_MAX_LEVELS_GRAPHICS];

};



#endif


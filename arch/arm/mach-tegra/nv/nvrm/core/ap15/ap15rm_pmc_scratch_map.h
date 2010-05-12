/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @file
 * @brief <b>nVIDIA Driver Development Kit:
 *           Power Management Controller (PMC) scratch registers fields
 *           definitions</b>
 *
 * @b Description: Defines SW-allocated fields in the PMC scratch registers
 *  shared by boot and power management code in RM and OAL.
 *
 */


#ifndef INCLUDED_AP15RM_PMC_SCRATCH_MAP_H
#define INCLUDED_AP15RM_PMC_SCRATCH_MAP_H

/*
 * Scratch registers offsets are part of the HW specification in the below
 * include file. Scratch registers fields are defined in this header via
 * bit ranges compatible with nvrm_drf macros.
 */
#include "ap15/arapbpm.h"

// Register APBDEV_PMC_SCRATCH0_0 (this is the only scratch register cleared on reset)
//

// RM clients combined power state (bits 4-7)
#define APBDEV_PMC_SCRATCH0_0_RM_PWR_STATE_RANGE        11:8
#define APBDEV_PMC_SCRATCH0_0_RM_LOAD_TRANSPORT_RANGE   15:12
#define APBDEV_PMC_SCRATCH0_0_RM_DFS_FLAG_RANGE         27:16
#define APBDEV_PMC_SCRATCH0_0_UPDATE_MODE_FLAG_RANGE     29:28
#define APBDEV_PMC_SCRATCH0_0_OAL_RTC_INIT_RANGE        30:30
#define APBDEV_PMC_SCRATCH0_0_RST_PWR_DET_RANGE         31:31

// Register APBDEV_PMC_SCRATCH20_0, used to store the ODM customer data from the BCT
#define APBDEV_PMC_SCRATCH20_0_BCT_ODM_DATA_RANGE       31:0

// Register APBDEV_PMC_SCRATCH21_0
//
#define APBDEV_PMC_SCRATCH21_0_LP2_TIME_US              31:0

#endif // INCLUDED_AP15RM_PMC_SCRATCH_MAP_H

/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef AMDGPU_DM_AMDGPU_DM_DMUB_H_
#define AMDGPU_DM_AMDGPU_DM_DMUB_H_

#include "amdgpu.h"

void dm_dmub_aux_setconfig_callback(struct amdgpu_device *adev,
				    struct dmub_notification *notify);
void dm_dmub_aux_fused_io_callback(struct amdgpu_device *adev,
				   struct dmub_notification *notify);
bool dm_register_dmub_notify_callback(struct amdgpu_device *adev,
				      enum dmub_notification_type type,
				      dmub_notify_interrupt_callback_t callback,
				      bool dmub_int_thread_offload);
int dm_dmub_hw_init(struct amdgpu_device *adev);
void dm_dmub_hw_resume(struct amdgpu_device *adev);
enum dmub_ips_disable_type dm_get_default_ips_mode(struct amdgpu_device *adev);
int dm_dmub_sw_init(struct amdgpu_device *adev);
int dm_init_microcode(struct amdgpu_device *adev);

#define FIRMWARE_RENOIR_DMUB		"amdgpu/renoir_dmcub.bin"
#define FIRMWARE_SIENNA_CICHLID_DMUB	"amdgpu/sienna_cichlid_dmcub.bin"
#define FIRMWARE_NAVY_FLOUNDER_DMUB	"amdgpu/navy_flounder_dmcub.bin"
#define FIRMWARE_GREEN_SARDINE_DMUB	"amdgpu/green_sardine_dmcub.bin"
#define FIRMWARE_VANGOGH_DMUB		"amdgpu/vangogh_dmcub.bin"
#define FIRMWARE_DIMGREY_CAVEFISH_DMUB	"amdgpu/dimgrey_cavefish_dmcub.bin"
#define FIRMWARE_BEIGE_GOBY_DMUB	"amdgpu/beige_goby_dmcub.bin"
#define FIRMWARE_YELLOW_CARP_DMUB	"amdgpu/yellow_carp_dmcub.bin"
#define FIRMWARE_DCN_314_DMUB		"amdgpu/dcn_3_1_4_dmcub.bin"
#define FIRMWARE_DCN_315_DMUB		"amdgpu/dcn_3_1_5_dmcub.bin"
#define FIRMWARE_DCN316_DMUB		"amdgpu/dcn_3_1_6_dmcub.bin"
#define FIRMWARE_DCN_V3_2_0_DMCUB	"amdgpu/dcn_3_2_0_dmcub.bin"
#define FIRMWARE_DCN_V3_2_1_DMCUB	"amdgpu/dcn_3_2_1_dmcub.bin"
#define FIRMWARE_DCN_35_DMUB		"amdgpu/dcn_3_5_dmcub.bin"
#define FIRMWARE_DCN_351_DMUB		"amdgpu/dcn_3_5_1_dmcub.bin"
#define FIRMWARE_DCN_36_DMUB		"amdgpu/dcn_3_6_dmcub.bin"
#define FIRMWARE_DCN_401_DMUB		"amdgpu/dcn_4_0_1_dmcub.bin"
#define FIRMWARE_DCN_42_DMUB		"amdgpu/dcn_4_2_dmcub.bin"
#define FIRMWARE_DCN_42B_DMUB		"amdgpu/dcn_4_2_1_dmcub.bin"
#define FIRMWARE_RAVEN_DMCU		"amdgpu/raven_dmcu.bin"
#define FIRMWARE_NAVI12_DMCU		"amdgpu/navi12_dmcu.bin"

#endif /* AMDGPU_DM_AMDGPU_DM_DMUB_H_ */

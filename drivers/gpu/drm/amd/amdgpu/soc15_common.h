/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __SOC15_COMMON_H__
#define __SOC15_COMMON_H__

struct nbio_hdp_flush_reg {
	u32 hdp_flush_req_offset;
	u32 hdp_flush_done_offset;
	u32 ref_and_mask_cp0;
	u32 ref_and_mask_cp1;
	u32 ref_and_mask_cp2;
	u32 ref_and_mask_cp3;
	u32 ref_and_mask_cp4;
	u32 ref_and_mask_cp5;
	u32 ref_and_mask_cp6;
	u32 ref_and_mask_cp7;
	u32 ref_and_mask_cp8;
	u32 ref_and_mask_cp9;
	u32 ref_and_mask_sdma0;
	u32 ref_and_mask_sdma1;
};

struct nbio_pcie_index_data {
	u32 index_offset;
	u32 data_offset;
};
// Register Access Macro
#define SOC15_REG_OFFSET(ip, inst, reg)       (0 == reg##_BASE_IDX ? ip##_BASE__INST##inst##_SEG0 + reg : \
                                                (1 == reg##_BASE_IDX ? ip##_BASE__INST##inst##_SEG1 + reg : \
                                                    (2 == reg##_BASE_IDX ? ip##_BASE__INST##inst##_SEG2 + reg : \
                                                        (3 == reg##_BASE_IDX ? ip##_BASE__INST##inst##_SEG3 + reg : \
                                                            (ip##_BASE__INST##inst##_SEG4 + reg)))))

#endif



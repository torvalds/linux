/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __DCN30_CLK_MGR_H__
#define __DCN30_CLK_MGR_H__

//CLK1_CLK_PLL_REQ
#ifndef CLK11_CLK1_CLK_PLL_REQ__FbMult_int__SHIFT
#define CLK11_CLK1_CLK_PLL_REQ__FbMult_int__SHIFT                                                                   0x0
#define CLK11_CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT                                                                  0xc
#define CLK11_CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT                                                                  0x10
#define CLK11_CLK1_CLK_PLL_REQ__FbMult_int_MASK                                                                     0x000001FFL
#define CLK11_CLK1_CLK_PLL_REQ__PllSpineDiv_MASK                                                                    0x0000F000L
#define CLK11_CLK1_CLK_PLL_REQ__FbMult_frac_MASK                                                                    0xFFFF0000L
//CLK1_CLK0_DFS_CNTL
#define CLK11_CLK1_CLK0_DFS_CNTL__CLK0_DIVIDER__SHIFT                                                               0x0
#define CLK11_CLK1_CLK0_DFS_CNTL__CLK0_DIVIDER_MASK                                                                 0x0000007FL
/*DPREF clock related*/
#define CLK0_CLK3_DFS_CNTL__CLK3_DIVIDER__SHIFT                                                               0x0
#define CLK0_CLK3_DFS_CNTL__CLK3_DIVIDER_MASK                                                                 0x0000007FL
#define CLK1_CLK3_DFS_CNTL__CLK3_DIVIDER__SHIFT                                                               0x0
#define CLK1_CLK3_DFS_CNTL__CLK3_DIVIDER_MASK                                                                 0x0000007FL
#define CLK2_CLK3_DFS_CNTL__CLK3_DIVIDER__SHIFT                                                               0x0
#define CLK2_CLK3_DFS_CNTL__CLK3_DIVIDER_MASK                                                                 0x0000007FL
#define CLK3_CLK3_DFS_CNTL__CLK3_DIVIDER__SHIFT                                                               0x0
#define CLK3_CLK3_DFS_CNTL__CLK3_DIVIDER_MASK                                                                 0x0000007FL

//CLK3_0_CLK3_CLK_PLL_REQ
#define CLK3_0_CLK3_CLK_PLL_REQ__FbMult_int__SHIFT                                                            0x0
#define CLK3_0_CLK3_CLK_PLL_REQ__PllSpineDiv__SHIFT                                                           0xc
#define CLK3_0_CLK3_CLK_PLL_REQ__FbMult_frac__SHIFT                                                           0x10
#define CLK3_0_CLK3_CLK_PLL_REQ__FbMult_int_MASK                                                              0x000001FFL
#define CLK3_0_CLK3_CLK_PLL_REQ__PllSpineDiv_MASK                                                             0x0000F000L
#define CLK3_0_CLK3_CLK_PLL_REQ__FbMult_frac_MASK                                                             0xFFFF0000L

#define mmCLK0_CLK2_DFS_CNTL                            0x16C55
#define mmCLK00_CLK0_CLK2_DFS_CNTL                      0x16C55
#define mmCLK01_CLK0_CLK2_DFS_CNTL                      0x16E55
#define mmCLK02_CLK0_CLK2_DFS_CNTL                      0x17055

#define mmCLK0_CLK3_DFS_CNTL                            0x16C60
#define mmCLK00_CLK0_CLK3_DFS_CNTL                      0x16C60
#define mmCLK01_CLK0_CLK3_DFS_CNTL                      0x16E60
#define mmCLK02_CLK0_CLK3_DFS_CNTL                      0x17060
#define mmCLK03_CLK0_CLK3_DFS_CNTL                      0x17260

#define mmCLK0_CLK_PLL_REQ                              0x16C10
#define mmCLK00_CLK0_CLK_PLL_REQ                        0x16C10
#define mmCLK01_CLK0_CLK_PLL_REQ                        0x16E10
#define mmCLK02_CLK0_CLK_PLL_REQ                        0x17010
#define mmCLK03_CLK0_CLK_PLL_REQ                        0x17210

#define mmCLK1_CLK_PLL_REQ                              0x1B00D
#define mmCLK10_CLK1_CLK_PLL_REQ                        0x1B00D
#define mmCLK11_CLK1_CLK_PLL_REQ                        0x1B20D
#define mmCLK12_CLK1_CLK_PLL_REQ                        0x1B40D
#define mmCLK13_CLK1_CLK_PLL_REQ                        0x1B60D

#define mmCLK2_CLK_PLL_REQ                              0x17E0D

/*AMCLK*/

#define mmCLK11_CLK1_CLK0_DFS_CNTL                      0x1B23F
#define mmCLK11_CLK1_CLK_PLL_REQ                        0x1B20D

#endif
void dcn3_init_clocks(struct clk_mgr *clk_mgr_base);

void dcn3_clk_mgr_construct(struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg);

void dcn3_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr);

#endif //__DCN30_CLK_MGR_H__

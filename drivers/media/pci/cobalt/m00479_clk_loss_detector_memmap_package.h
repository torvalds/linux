/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef M00479_CLK_LOSS_DETECTOR_MEMMAP_PACKAGE_H
#define M00479_CLK_LOSS_DETECTOR_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00479_CLK_LOSS_DETECTOR_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00479_clk_loss_detector_regmap {
	/* Control module */
	uint32_t ctrl;             /* Reg 0x0000, Default=0x0 */
	uint32_t status;           /* Reg 0x0004 */
	/* Number of ref clk cycles before checking the clock under test */
	uint32_t ref_clk_cnt_val;  /* Reg 0x0008, Default=0xc4 */
	/* Number of test clk cycles required in the ref_clk_cnt_val period
	 * to ensure that the test clock is performing as expected */
	uint32_t test_clk_cnt_val; /* Reg 0x000c, Default=0xa */
};

#define M00479_CLK_LOSS_DETECTOR_REG_CTRL_OFST 0
#define M00479_CLK_LOSS_DETECTOR_REG_STATUS_OFST 4
#define M00479_CLK_LOSS_DETECTOR_REG_REF_CLK_CNT_VAL_OFST 8
#define M00479_CLK_LOSS_DETECTOR_REG_TEST_CLK_CNT_VAL_OFST 12

/*******************************************************************
 * Bit Mask for register
 * M00479_CLK_LOSS_DETECTOR_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* ctrl [0:0] */
#define M00479_CTRL_BITMAP_ENABLE_OFST          (0)
#define M00479_CTRL_BITMAP_ENABLE_MSK           (0x1 << M00479_CTRL_BITMAP_ENABLE_OFST)
/* status [0:0] */
#define M00479_STATUS_BITMAP_CLOCK_MISSING_OFST (0)
#define M00479_STATUS_BITMAP_CLOCK_MISSING_MSK  (0x1 << M00479_STATUS_BITMAP_CLOCK_MISSING_OFST)

#endif /*M00479_CLK_LOSS_DETECTOR_MEMMAP_PACKAGE_H*/

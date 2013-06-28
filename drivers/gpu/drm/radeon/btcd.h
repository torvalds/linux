/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef _BTCD_H_
#define _BTCD_H_

/* pm registers */

#define GENERAL_PWRMGT                                  0x63c
#       define GLOBAL_PWRMGT_EN                         (1 << 0)
#       define STATIC_PM_EN                             (1 << 1)
#       define THERMAL_PROTECTION_DIS                   (1 << 2)
#       define THERMAL_PROTECTION_TYPE                  (1 << 3)
#       define ENABLE_GEN2PCIE                          (1 << 4)
#       define ENABLE_GEN2XSP                           (1 << 5)
#       define SW_SMIO_INDEX(x)                         ((x) << 6)
#       define SW_SMIO_INDEX_MASK                       (3 << 6)
#       define SW_SMIO_INDEX_SHIFT                      6
#       define LOW_VOLT_D2_ACPI                         (1 << 8)
#       define LOW_VOLT_D3_ACPI                         (1 << 9)
#       define VOLT_PWRMGT_EN                           (1 << 10)
#       define BACKBIAS_PAD_EN                          (1 << 18)
#       define BACKBIAS_VALUE                           (1 << 19)
#       define DYN_SPREAD_SPECTRUM_EN                   (1 << 23)
#       define AC_DC_SW                                 (1 << 24)

#define	CG_BIF_REQ_AND_RSP				0x7f4
#define		CG_CLIENT_REQ(x)			((x) << 0)
#define		CG_CLIENT_REQ_MASK			(0xff << 0)
#define		CG_CLIENT_REQ_SHIFT			0
#define		CG_CLIENT_RESP(x)			((x) << 8)
#define		CG_CLIENT_RESP_MASK			(0xff << 8)
#define		CG_CLIENT_RESP_SHIFT			8
#define		CLIENT_CG_REQ(x)			((x) << 16)
#define		CLIENT_CG_REQ_MASK			(0xff << 16)
#define		CLIENT_CG_REQ_SHIFT			16
#define		CLIENT_CG_RESP(x)			((x) << 24)
#define		CLIENT_CG_RESP_MASK			(0xff << 24)
#define		CLIENT_CG_RESP_SHIFT			24

#define	SCLK_PSKIP_CNTL					0x8c0
#define		PSKIP_ON_ALLOW_STOP_HI(x)		((x) << 16)
#define		PSKIP_ON_ALLOW_STOP_HI_MASK		(0xff << 16)
#define		PSKIP_ON_ALLOW_STOP_HI_SHIFT		16

#define	CG_ULV_CONTROL					0x8c8
#define	CG_ULV_PARAMETER				0x8cc

#define	MC_ARB_DRAM_TIMING				0x2774
#define	MC_ARB_DRAM_TIMING2				0x2778

#define	MC_ARB_RFSH_RATE				0x27b0
#define		POWERMODE0(x)				((x) << 0)
#define		POWERMODE0_MASK				(0xff << 0)
#define		POWERMODE0_SHIFT			0
#define		POWERMODE1(x)				((x) << 8)
#define		POWERMODE1_MASK				(0xff << 8)
#define		POWERMODE1_SHIFT			8
#define		POWERMODE2(x)				((x) << 16)
#define		POWERMODE2_MASK				(0xff << 16)
#define		POWERMODE2_SHIFT			16
#define		POWERMODE3(x)				((x) << 24)
#define		POWERMODE3_MASK				(0xff << 24)
#define		POWERMODE3_SHIFT			24

#define MC_ARB_BURST_TIME                               0x2808
#define		STATE0(x)				((x) << 0)
#define		STATE0_MASK				(0x1f << 0)
#define		STATE0_SHIFT				0
#define		STATE1(x)				((x) << 5)
#define		STATE1_MASK				(0x1f << 5)
#define		STATE1_SHIFT				5
#define		STATE2(x)				((x) << 10)
#define		STATE2_MASK				(0x1f << 10)
#define		STATE2_SHIFT				10
#define		STATE3(x)				((x) << 15)
#define		STATE3_MASK				(0x1f << 15)
#define		STATE3_SHIFT				15

#define MC_SEQ_RAS_TIMING                               0x28a0
#define MC_SEQ_CAS_TIMING                               0x28a4
#define MC_SEQ_MISC_TIMING                              0x28a8
#define MC_SEQ_MISC_TIMING2                             0x28ac

#define MC_SEQ_RD_CTL_D0                                0x28b4
#define MC_SEQ_RD_CTL_D1                                0x28b8
#define MC_SEQ_WR_CTL_D0                                0x28bc
#define MC_SEQ_WR_CTL_D1                                0x28c0

#define MC_PMG_AUTO_CFG                                 0x28d4

#define MC_SEQ_STATUS_M                                 0x29f4
#       define PMG_PWRSTATE                             (1 << 16)

#define MC_SEQ_MISC0                                    0x2a00
#define         MC_SEQ_MISC0_GDDR5_SHIFT                28
#define         MC_SEQ_MISC0_GDDR5_MASK                 0xf0000000
#define         MC_SEQ_MISC0_GDDR5_VALUE                5
#define MC_SEQ_MISC1                                    0x2a04
#define MC_SEQ_RESERVE_M                                0x2a08
#define MC_PMG_CMD_EMRS                                 0x2a0c

#define MC_SEQ_MISC3                                    0x2a2c

#define MC_SEQ_MISC5                                    0x2a54
#define MC_SEQ_MISC6                                    0x2a58

#define MC_SEQ_MISC7                                    0x2a64

#define MC_SEQ_CG                                       0x2a68
#define		CG_SEQ_REQ(x)				((x) << 0)
#define		CG_SEQ_REQ_MASK				(0xff << 0)
#define		CG_SEQ_REQ_SHIFT			0
#define		CG_SEQ_RESP(x)				((x) << 8)
#define		CG_SEQ_RESP_MASK			(0xff << 8)
#define		CG_SEQ_RESP_SHIFT			8
#define		SEQ_CG_REQ(x)				((x) << 16)
#define		SEQ_CG_REQ_MASK				(0xff << 16)
#define		SEQ_CG_REQ_SHIFT			16
#define		SEQ_CG_RESP(x)				((x) << 24)
#define		SEQ_CG_RESP_MASK			(0xff << 24)
#define		SEQ_CG_RESP_SHIFT			24
#define MC_SEQ_RAS_TIMING_LP                            0x2a6c
#define MC_SEQ_CAS_TIMING_LP                            0x2a70
#define MC_SEQ_MISC_TIMING_LP                           0x2a74
#define MC_SEQ_MISC_TIMING2_LP                          0x2a78
#define MC_SEQ_WR_CTL_D0_LP                             0x2a7c
#define MC_SEQ_WR_CTL_D1_LP                             0x2a80
#define MC_SEQ_PMG_CMD_EMRS_LP                          0x2a84
#define MC_SEQ_PMG_CMD_MRS_LP                           0x2a88

#define MC_PMG_CMD_MRS                                  0x2aac

#define MC_SEQ_RD_CTL_D0_LP                             0x2b1c
#define MC_SEQ_RD_CTL_D1_LP                             0x2b20

#define MC_PMG_CMD_MRS1                                 0x2b44
#define MC_SEQ_PMG_CMD_MRS1_LP                          0x2b48

#define	LB_SYNC_RESET_SEL				0x6b28
#define		LB_SYNC_RESET_SEL_MASK			(3 << 0)
#define		LB_SYNC_RESET_SEL_SHIFT			0

/* PCIE link stuff */
#define PCIE_LC_SPEED_CNTL                                0xa4 /* PCIE_P */
#       define LC_GEN2_EN_STRAP                           (1 << 0)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_EN           (1 << 1)
#       define LC_FORCE_EN_HW_SPEED_CHANGE                (1 << 5)
#       define LC_FORCE_DIS_HW_SPEED_CHANGE               (1 << 6)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_MASK      (0x3 << 8)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_SHIFT     3
#       define LC_CURRENT_DATA_RATE                       (1 << 11)
#       define LC_HW_VOLTAGE_IF_CONTROL(x)                ((x) << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_MASK              (3 << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_SHIFT             12
#       define LC_VOLTAGE_TIMER_SEL_MASK                  (0xf << 14)
#       define LC_CLR_FAILED_SPD_CHANGE_CNT               (1 << 21)
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 23)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 24)

#endif

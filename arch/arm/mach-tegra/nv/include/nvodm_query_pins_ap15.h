/*
 * Copyright (c) 2008-2009 NVIDIA Corporation.
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

/** @file
 * <b>NVIDIA Tegra ODM Kit:
 *        Pin configurations for NVIDIA APX 2300, APX 2500, Tegra 600 and Tegra 650 processors</b>
 * 
 * @b Description: Defines the names and configurable settings for pin electrical
 *                 attributes, such as drive strength and slew.
 */

// This is an auto-generated file.  Do not edit.
// Regenerate with "genpadconfig.py ap15 drivers/hwinc/ap15/arapb_misc.h"

#ifndef INCLUDED_NVODM_QUERY_PINS_AP15_H
#define INCLUDED_NVODM_QUERY_PINS_AP15_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * This specifies the list of pin configuration registers supported by
 * AP15-compatible products.  This should be used to generate the pin
 * pin-attribute query array.
 * @see NvOdmQueryPinAttributes.
 * @ingroup nvodm_pins
 * @{
 */

typedef enum
{

    /// Pin configuration registers for NVIDIA APX 2300 products
    NvOdmPinRegister_Apx2300_PullUpDown_A = 0x100000A0UL,
    NvOdmPinRegister_Apx2300_PullUpDown_B = 0x100000A4UL,
    NvOdmPinRegister_Apx2300_PullUpDown_C = 0x100000A8UL,
    NvOdmPinRegister_Apx2300_PullUpDown_D = 0x100000ACUL,
    NvOdmPinRegister_Apx2300_PullUpDown_E = 0x100000B0UL,
    NvOdmPinRegister_Apx2300_PadCtrl_AOCFG1 = 0x10000868UL,
    NvOdmPinRegister_Apx2300_PadCtrl_AOCFG2 = 0x1000086CUL,
    NvOdmPinRegister_Apx2300_PadCtrl_ATCFG1 = 0x10000870UL,
    NvOdmPinRegister_Apx2300_PadCtrl_ATCFG2 = 0x10000874UL,
    NvOdmPinRegister_Apx2300_PadCtrl_CDEV1CFG = 0x10000878UL,
    NvOdmPinRegister_Apx2300_PadCtrl_CDEV2CFG = 0x1000087CUL,
    NvOdmPinRegister_Apx2300_PadCtrl_CSUSCFG = 0x10000880UL,
    NvOdmPinRegister_Apx2300_PadCtrl_DAP1CFG = 0x10000884UL,
    NvOdmPinRegister_Apx2300_PadCtrl_DAP2CFG = 0x10000888UL,
    NvOdmPinRegister_Apx2300_PadCtrl_DAP3CFG = 0x1000088CUL,
    NvOdmPinRegister_Apx2300_PadCtrl_DAP4CFG = 0x10000890UL,
    NvOdmPinRegister_Apx2300_PadCtrl_DBGCFG = 0x10000894UL,
    NvOdmPinRegister_Apx2300_PadCtrl_LCDCFG1 = 0x10000898UL,
    NvOdmPinRegister_Apx2300_PadCtrl_LCDCFG2 = 0x1000089CUL,
    NvOdmPinRegister_Apx2300_PadCtrl_SDIO2CFG = 0x100008A0UL,
    NvOdmPinRegister_Apx2300_PadCtrl_SDIO3CFG = 0x100008A4UL,
    NvOdmPinRegister_Apx2300_PadCtrl_SPICFG = 0x100008A8UL,
    NvOdmPinRegister_Apx2300_PadCtrl_UAACFG = 0x100008ACUL,
    NvOdmPinRegister_Apx2300_PadCtrl_UABCFG = 0x100008B0UL,
    NvOdmPinRegister_Apx2300_PadCtrl_UART2CFG = 0x100008B4UL,
    NvOdmPinRegister_Apx2300_PadCtrl_UART3CFG = 0x100008B8UL,
    NvOdmPinRegister_Apx2300_PadCtrl_VICFG1 = 0x100008BCUL,
    NvOdmPinRegister_Apx2300_PadCtrl_VICFG2 = 0x100008C0UL,
    NvOdmPinRegister_Apx2300_PadCtrl_XM2CFGA = 0x100008C4UL,
    NvOdmPinRegister_Apx2300_PadCtrl_XM2CFGC = 0x100008C8UL,
    NvOdmPinRegister_Apx2300_PadCtrl_XM2CFGD = 0x100008CCUL,
    NvOdmPinRegister_Apx2300_PadCtrl_XM2CLKCFG = 0x100008D0UL,
    NvOdmPinRegister_Apx2300_PadCtrl_MEMCOMP = 0x100008D4UL,

    /// Pin configuration registers for NVIDIA APX 2500 products
    NvOdmPinRegister_Apx2500_PullUpDown_A = 0x100000A0UL,
    NvOdmPinRegister_Apx2500_PullUpDown_B = 0x100000A4UL,
    NvOdmPinRegister_Apx2500_PullUpDown_C = 0x100000A8UL,
    NvOdmPinRegister_Apx2500_PullUpDown_D = 0x100000ACUL,
    NvOdmPinRegister_Apx2500_PullUpDown_E = 0x100000B0UL,
    NvOdmPinRegister_Apx2500_PadCtrl_AOCFG1 = 0x10000868UL,
    NvOdmPinRegister_Apx2500_PadCtrl_AOCFG2 = 0x1000086CUL,
    NvOdmPinRegister_Apx2500_PadCtrl_ATCFG1 = 0x10000870UL,
    NvOdmPinRegister_Apx2500_PadCtrl_ATCFG2 = 0x10000874UL,
    NvOdmPinRegister_Apx2500_PadCtrl_CDEV1CFG = 0x10000878UL,
    NvOdmPinRegister_Apx2500_PadCtrl_CDEV2CFG = 0x1000087CUL,
    NvOdmPinRegister_Apx2500_PadCtrl_CSUSCFG = 0x10000880UL,
    NvOdmPinRegister_Apx2500_PadCtrl_DAP1CFG = 0x10000884UL,
    NvOdmPinRegister_Apx2500_PadCtrl_DAP2CFG = 0x10000888UL,
    NvOdmPinRegister_Apx2500_PadCtrl_DAP3CFG = 0x1000088CUL,
    NvOdmPinRegister_Apx2500_PadCtrl_DAP4CFG = 0x10000890UL,
    NvOdmPinRegister_Apx2500_PadCtrl_DBGCFG = 0x10000894UL,
    NvOdmPinRegister_Apx2500_PadCtrl_LCDCFG1 = 0x10000898UL,
    NvOdmPinRegister_Apx2500_PadCtrl_LCDCFG2 = 0x1000089CUL,
    NvOdmPinRegister_Apx2500_PadCtrl_SDIO2CFG = 0x100008A0UL,
    NvOdmPinRegister_Apx2500_PadCtrl_SDIO3CFG = 0x100008A4UL,
    NvOdmPinRegister_Apx2500_PadCtrl_SPICFG = 0x100008A8UL,
    NvOdmPinRegister_Apx2500_PadCtrl_UAACFG = 0x100008ACUL,
    NvOdmPinRegister_Apx2500_PadCtrl_UABCFG = 0x100008B0UL,
    NvOdmPinRegister_Apx2500_PadCtrl_UART2CFG = 0x100008B4UL,
    NvOdmPinRegister_Apx2500_PadCtrl_UART3CFG = 0x100008B8UL,
    NvOdmPinRegister_Apx2500_PadCtrl_VICFG1 = 0x100008BCUL,
    NvOdmPinRegister_Apx2500_PadCtrl_VICFG2 = 0x100008C0UL,
    NvOdmPinRegister_Apx2500_PadCtrl_XM2CFGA = 0x100008C4UL,
    NvOdmPinRegister_Apx2500_PadCtrl_XM2CFGC = 0x100008C8UL,
    NvOdmPinRegister_Apx2500_PadCtrl_XM2CFGD = 0x100008CCUL,
    NvOdmPinRegister_Apx2500_PadCtrl_XM2CLKCFG = 0x100008D0UL,
    NvOdmPinRegister_Apx2500_PadCtrl_MEMCOMP = 0x100008D4UL,

    /// Pin configuration registers for NVIDIA Tegra 600 products
    NvOdmPinRegister_Tegra600_PullUpDown_A = 0x100000A0UL,
    NvOdmPinRegister_Tegra600_PullUpDown_B = 0x100000A4UL,
    NvOdmPinRegister_Tegra600_PullUpDown_C = 0x100000A8UL,
    NvOdmPinRegister_Tegra600_PullUpDown_D = 0x100000ACUL,
    NvOdmPinRegister_Tegra600_PullUpDown_E = 0x100000B0UL,
    NvOdmPinRegister_Tegra600_PadCtrl_AOCFG1 = 0x10000868UL,
    NvOdmPinRegister_Tegra600_PadCtrl_AOCFG2 = 0x1000086CUL,
    NvOdmPinRegister_Tegra600_PadCtrl_ATCFG1 = 0x10000870UL,
    NvOdmPinRegister_Tegra600_PadCtrl_ATCFG2 = 0x10000874UL,
    NvOdmPinRegister_Tegra600_PadCtrl_CDEV1CFG = 0x10000878UL,
    NvOdmPinRegister_Tegra600_PadCtrl_CDEV2CFG = 0x1000087CUL,
    NvOdmPinRegister_Tegra600_PadCtrl_CSUSCFG = 0x10000880UL,
    NvOdmPinRegister_Tegra600_PadCtrl_DAP1CFG = 0x10000884UL,
    NvOdmPinRegister_Tegra600_PadCtrl_DAP2CFG = 0x10000888UL,
    NvOdmPinRegister_Tegra600_PadCtrl_DAP3CFG = 0x1000088CUL,
    NvOdmPinRegister_Tegra600_PadCtrl_DAP4CFG = 0x10000890UL,
    NvOdmPinRegister_Tegra600_PadCtrl_DBGCFG = 0x10000894UL,
    NvOdmPinRegister_Tegra600_PadCtrl_LCDCFG1 = 0x10000898UL,
    NvOdmPinRegister_Tegra600_PadCtrl_LCDCFG2 = 0x1000089CUL,
    NvOdmPinRegister_Tegra600_PadCtrl_SDIO2CFG = 0x100008A0UL,
    NvOdmPinRegister_Tegra600_PadCtrl_SDIO3CFG = 0x100008A4UL,
    NvOdmPinRegister_Tegra600_PadCtrl_SPICFG = 0x100008A8UL,
    NvOdmPinRegister_Tegra600_PadCtrl_UAACFG = 0x100008ACUL,
    NvOdmPinRegister_Tegra600_PadCtrl_UABCFG = 0x100008B0UL,
    NvOdmPinRegister_Tegra600_PadCtrl_UART2CFG = 0x100008B4UL,
    NvOdmPinRegister_Tegra600_PadCtrl_UART3CFG = 0x100008B8UL,
    NvOdmPinRegister_Tegra600_PadCtrl_VICFG1 = 0x100008BCUL,
    NvOdmPinRegister_Tegra600_PadCtrl_VICFG2 = 0x100008C0UL,
    NvOdmPinRegister_Tegra600_PadCtrl_XM2CFGA = 0x100008C4UL,
    NvOdmPinRegister_Tegra600_PadCtrl_XM2CFGC = 0x100008C8UL,
    NvOdmPinRegister_Tegra600_PadCtrl_XM2CFGD = 0x100008CCUL,
    NvOdmPinRegister_Tegra600_PadCtrl_XM2CLKCFG = 0x100008D0UL,
    NvOdmPinRegister_Tegra600_PadCtrl_MEMCOMP = 0x100008D4UL,

    /// Pin configuration registers for NVIDIA Tegra 650 products
    NvOdmPinRegister_Tegra650_PullUpDown_A = 0x100000A0UL,
    NvOdmPinRegister_Tegra650_PullUpDown_B = 0x100000A4UL,
    NvOdmPinRegister_Tegra650_PullUpDown_C = 0x100000A8UL,
    NvOdmPinRegister_Tegra650_PullUpDown_D = 0x100000ACUL,
    NvOdmPinRegister_Tegra650_PullUpDown_E = 0x100000B0UL,
    NvOdmPinRegister_Tegra650_PadCtrl_AOCFG1 = 0x10000868UL,
    NvOdmPinRegister_Tegra650_PadCtrl_AOCFG2 = 0x1000086CUL,
    NvOdmPinRegister_Tegra650_PadCtrl_ATCFG1 = 0x10000870UL,
    NvOdmPinRegister_Tegra650_PadCtrl_ATCFG2 = 0x10000874UL,
    NvOdmPinRegister_Tegra650_PadCtrl_CDEV1CFG = 0x10000878UL,
    NvOdmPinRegister_Tegra650_PadCtrl_CDEV2CFG = 0x1000087CUL,
    NvOdmPinRegister_Tegra650_PadCtrl_CSUSCFG = 0x10000880UL,
    NvOdmPinRegister_Tegra650_PadCtrl_DAP1CFG = 0x10000884UL,
    NvOdmPinRegister_Tegra650_PadCtrl_DAP2CFG = 0x10000888UL,
    NvOdmPinRegister_Tegra650_PadCtrl_DAP3CFG = 0x1000088CUL,
    NvOdmPinRegister_Tegra650_PadCtrl_DAP4CFG = 0x10000890UL,
    NvOdmPinRegister_Tegra650_PadCtrl_DBGCFG = 0x10000894UL,
    NvOdmPinRegister_Tegra650_PadCtrl_LCDCFG1 = 0x10000898UL,
    NvOdmPinRegister_Tegra650_PadCtrl_LCDCFG2 = 0x1000089CUL,
    NvOdmPinRegister_Tegra650_PadCtrl_SDIO2CFG = 0x100008A0UL,
    NvOdmPinRegister_Tegra650_PadCtrl_SDIO3CFG = 0x100008A4UL,
    NvOdmPinRegister_Tegra650_PadCtrl_SPICFG = 0x100008A8UL,
    NvOdmPinRegister_Tegra650_PadCtrl_UAACFG = 0x100008ACUL,
    NvOdmPinRegister_Tegra650_PadCtrl_UABCFG = 0x100008B0UL,
    NvOdmPinRegister_Tegra650_PadCtrl_UART2CFG = 0x100008B4UL,
    NvOdmPinRegister_Tegra650_PadCtrl_UART3CFG = 0x100008B8UL,
    NvOdmPinRegister_Tegra650_PadCtrl_VICFG1 = 0x100008BCUL,
    NvOdmPinRegister_Tegra650_PadCtrl_VICFG2 = 0x100008C0UL,
    NvOdmPinRegister_Tegra650_PadCtrl_XM2CFGA = 0x100008C4UL,
    NvOdmPinRegister_Tegra650_PadCtrl_XM2CFGC = 0x100008C8UL,
    NvOdmPinRegister_Tegra650_PadCtrl_XM2CFGD = 0x100008CCUL,
    NvOdmPinRegister_Tegra650_PadCtrl_XM2CLKCFG = 0x100008D0UL,
    NvOdmPinRegister_Tegra650_PadCtrl_MEMCOMP = 0x100008D4UL,

    NvOdmPinRegister_Force32 = 0x7fffffffUL,
} NvOdmPinRegister;

/*
 * C pre-processor macros are provided below to help ODMs specify
 * pin electrical attributes in a more readable and maintainable fashion
 * than hardcoding hexadecimal numbers directly.  Please refer to the
 * Electrical, Thermal and Mechanical data sheet for your product for more
 * detailed information regarding the effects these values have
 */

/**
 * Use this macro to program the PullUpDown_A register.
 *
 * @param ATA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param ATB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param ATC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param ATD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param ATE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DAP1 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DAP2 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DAP3 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DAP4 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DTF : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GPV : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PULLUPDOWN_A(ATA, ATB, ATC, ATD, ATE, DAP1, DAP2, DAP3, DAP4, DTA, DTB, DTC, DTD, DTE, DTF, GPV) \
    ((((ATA)&3UL) << 0) | (((ATB)&3UL) << 2) | (((ATC)&3UL) << 4) | \
     (((ATD)&3UL) << 6) | (((ATE)&3UL) << 8) | (((DAP1)&3UL) << 10) | \
     (((DAP2)&3UL) << 12) | (((DAP3)&3UL) << 14) | (((DAP4)&3UL) << 16) | \
     (((DTA)&3UL) << 18) | (((DTB)&3UL) << 20) | (((DTC)&3UL) << 22) | \
     (((DTD)&3UL) << 24) | (((DTE)&3UL) << 26) | (((DTF)&3UL) << 28) | \
     (((GPV)&3UL) << 30))

/**
 * Use this macro to program the PullUpDown_B register.
 *
 * @param RM : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param I2CP : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PTA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GPU7 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPDI : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPDO : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GPU : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXK : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PULLUPDOWN_B(RM, I2CP, PTA, GPU7, KBCA, KBCB, KBCC, KBCD, SPDI, SPDO, GPU, SLXA, SLXB, SLXC, SLXD, SLXK) \
    ((((RM)&3UL) << 0) | (((I2CP)&3UL) << 2) | (((PTA)&3UL) << 4) | \
     (((GPU7)&3UL) << 6) | (((KBCA)&3UL) << 8) | (((KBCB)&3UL) << 10) | \
     (((KBCC)&3UL) << 12) | (((KBCD)&3UL) << 14) | (((SPDI)&3UL) << 16) | \
     (((SPDO)&3UL) << 18) | (((GPU)&3UL) << 20) | (((SLXA)&3UL) << 22) | \
     (((SLXB)&3UL) << 24) | (((SLXC)&3UL) << 26) | (((SLXD)&3UL) << 28) | \
     (((SLXK)&3UL) << 30))

/**
 * Use this macro to program the PullUpDown_C register.
 *
 * @param CDEV1 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param CDEV2 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPID : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIF : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIG : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SPIH : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param IRTX : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param IRRX : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2A : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2C : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2D : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2S : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PULLUPDOWN_C(CDEV1, CDEV2, SPIA, SPIB, SPIC, SPID, SPIE, SPIF, SPIG, SPIH, IRTX, IRRX, XM2A, XM2C, XM2D, XM2S) \
    ((((CDEV1)&3UL) << 0) | (((CDEV2)&3UL) << 2) | (((SPIA)&3UL) << 4) | \
     (((SPIB)&3UL) << 6) | (((SPIC)&3UL) << 8) | (((SPID)&3UL) << 10) | \
     (((SPIE)&3UL) << 12) | (((SPIF)&3UL) << 14) | (((SPIG)&3UL) << 16) | \
     (((SPIH)&3UL) << 18) | (((IRTX)&3UL) << 20) | (((IRRX)&3UL) << 22) | \
     (((XM2A)&3UL) << 24) | (((XM2C)&3UL) << 26) | (((XM2D)&3UL) << 28) | \
     (((XM2S)&3UL) << 30))

/**
 * Use this macro to program the PullUpDown_D register.
 *
 * @param UAA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UAB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UAC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UAD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UCA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UCB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LD17_0 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LD19_18 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LD21_20 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LD23_22 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LS : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param LC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param CSUS : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PULLUPDOWN_D(UAA, UAB, UAC, UAD, UCA, UCB, LD17_0, LD19_18, LD21_20, LD23_22, LS, LC, CSUS, SDB, SDC, SDD) \
    ((((UAA)&3UL) << 0) | (((UAB)&3UL) << 2) | (((UAC)&3UL) << 4) | \
     (((UAD)&3UL) << 6) | (((UCA)&3UL) << 8) | (((UCB)&3UL) << 10) | \
     (((LD17_0)&3UL) << 12) | (((LD19_18)&3UL) << 14) | (((LD21_20)&3UL) << 16) | \
     (((LD23_22)&3UL) << 18) | (((LS)&3UL) << 20) | (((LC)&3UL) << 22) | \
     (((CSUS)&3UL) << 24) | (((SDB)&3UL) << 26) | (((SDC)&3UL) << 28) | \
     (((SDD)&3UL) << 30))

/**
 * Use this macro to program the PullUpDown_E register.
 *
 * @param KBCF : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PULLUPDOWN_E(KBCF, KBCE, PMCA, PMCB) \
    ((((KBCF)&3UL) << 0) | (((KBCE)&3UL) << 2) | (((PMCA)&3UL) << 4) | \
     (((PMCB)&3UL) << 6))

/**
 * Use this macro to program the PadCtrl_AOCFG1, PadCtrl_AOCFG2, 
 * PadCtrl_ATCFG1, PadCtrl_ATCFG2, PadCtrl_CDEV1CFG, PadCtrl_CDEV2CFG, 
 * PadCtrl_CSUSCFG, PadCtrl_DAP1CFG, PadCtrl_DAP2CFG, PadCtrl_DAP3CFG, 
 * PadCtrl_DAP4CFG, PadCtrl_DBGCFG, PadCtrl_LCDCFG1, PadCtrl_LCDCFG2, 
 * PadCtrl_SDIO2CFG, PadCtrl_SDIO3CFG, PadCtrl_SPICFG, PadCtrl_UAACFG, 
 * PadCtrl_UABCFG, PadCtrl_UART2CFG, PadCtrl_UART3CFG, PadCtrl_VICFG1 and 
 * PadCtrl_VICFG2 registers.
 *
 * @param HSM_EN : Enable high-speed mode (0 = disable).  Valid Range 0 - 1
 * @param SCHMT_EN : Schmitt trigger enable (0 = disable).  Valid Range 0 - 1
 * @param LPMD : Low-power current/impedance selection (0 = 400 ohm, 1 = 200 ohm, 2 = 100 ohm, 3 = 50 ohm).  Valid Range 0 - 3
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 3
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PADCTRL_AOCFG1(HSM_EN, SCHMT_EN, LPMD, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((HSM_EN)&1UL) << 2) | (((SCHMT_EN)&1UL) << 3) | (((LPMD)&3UL) << 4) | \
     (((CAL_DRVDN)&31UL) << 12) | (((CAL_DRVUP)&31UL) << 20) | \
     (((CAL_DRVDN_SLWR)&3UL) << 28) | (((CAL_DRVUP_SLWF)&3UL) << 30))

/**
 * Use this macro to program the PadCtrl_XM2CFGA register.
 *
 * @param HSM_EN : Enable high-speed mode (0 = disable).  Valid Range 0 - 1
 * @param LPMD : Low-power current/impedance selection (0 = 400 ohm, 1 = 200 ohm, 2 = 100 ohm, 3 = 50 ohm).  Valid Range 0 - 3
 * @param VREF_EN : VRef enable.  Valid Range 0 - 1
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 3
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PADCTRL_XM2CFGA(HSM_EN, LPMD, VREF_EN, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((HSM_EN)&1UL) << 2) | (((LPMD)&3UL) << 4) | (((VREF_EN)&1UL) << 6) | \
     (((CAL_DRVDN)&31UL) << 12) | (((CAL_DRVUP)&31UL) << 20) | \
     (((CAL_DRVDN_SLWR)&3UL) << 28) | (((CAL_DRVUP_SLWF)&3UL) << 30))

/**
 * Use this macro to program the PadCtrl_XM2CFGC, PadCtrl_XM2CFGD and 
 * PadCtrl_XM2CLKCFG registers.
 *
 * @param HSM_EN : Enable high-speed mode (0 = disable).  Valid Range 0 - 1
 * @param SCHMT_EN : Schmitt trigger enable (0 = disable).  Valid Range 0 - 1
 * @param LPMD : Low-power current/impedance selection (0 = 400 ohm, 1 = 200 ohm, 2 = 100 ohm, 3 = 50 ohm).  Valid Range 0 - 3
 * @param VREF_EN : VRef enable.  Valid Range 0 - 1
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 3
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP15_PADCTRL_XM2CFGC(HSM_EN, SCHMT_EN, LPMD, VREF_EN, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((HSM_EN)&1UL) << 2) | (((SCHMT_EN)&1UL) << 3) | (((LPMD)&3UL) << 4) | \
     (((VREF_EN)&1UL) << 6) | (((CAL_DRVDN)&31UL) << 12) | \
     (((CAL_DRVUP)&31UL) << 20) | (((CAL_DRVDN_SLWR)&3UL) << 28) | \
     (((CAL_DRVUP_SLWF)&3UL) << 30))

/**
 * Use this macro to program the PadCtrl_MEMCOMP register.
 *
 * @param E_HSM : Enable high-speed mode (0 = disable).  Valid Range 0 - 1
 * @param COMPPAD_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param COMPPAD_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 */

#define NVODM_QUERY_PIN_AP15_PADCTRL_MEMCOMP(E_HSM, COMPPAD_DRVDN, COMPPAD_DRVUP) \
    ((((E_HSM)&1UL) << 2) | (((COMPPAD_DRVDN)&31UL) << 12) | \
     (((COMPPAD_DRVUP)&31UL) << 20))

#ifdef __cplusplus
}
#endif

/** @} */
#endif // INCLUDED_NVODM_QUERY_PINS_AP15_H


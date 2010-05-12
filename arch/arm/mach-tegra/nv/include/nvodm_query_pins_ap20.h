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
 *        Pin configurations for NVIDIA AP20 processors</b>
 * 
 * @b Description: Defines the names and configurable settings for pin electrical
 *                 attributes, such as drive strength and slew.
 */

// This is an auto-generated file.  Do not edit.
// Regenerate with "genpadconfig.py ap20 drivers/hwinc/ap20/arapb_misc.h"

#ifndef INCLUDED_NVODM_QUERY_PINS_AP20_H
#define INCLUDED_NVODM_QUERY_PINS_AP20_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * This specifies the list of pin configuration registers supported by
 * AP20-compatible products.  This should be used to generate the pin
 * pin-attribute query array.
 * @see NvOdmQueryPinAttributes.
 * @ingroup nvodm_pins
 * @{
 */

typedef enum
{

    /// Pin configuration registers for NVIDIA AP20 products
    NvOdmPinRegister_Ap20_PullUpDown_A = 0x200000A0UL,
    NvOdmPinRegister_Ap20_PullUpDown_B = 0x200000A4UL,
    NvOdmPinRegister_Ap20_PullUpDown_C = 0x200000A8UL,
    NvOdmPinRegister_Ap20_PullUpDown_D = 0x200000ACUL,
    NvOdmPinRegister_Ap20_PullUpDown_E = 0x200000B0UL,
    NvOdmPinRegister_Ap20_PadCtrl_AOCFG1PADCTRL = 0x20000868UL,
    NvOdmPinRegister_Ap20_PadCtrl_AOCFG2PADCTRL = 0x2000086CUL,
    NvOdmPinRegister_Ap20_PadCtrl_ATCFG1PADCTRL = 0x20000870UL,
    NvOdmPinRegister_Ap20_PadCtrl_ATCFG2PADCTRL = 0x20000874UL,
    NvOdmPinRegister_Ap20_PadCtrl_CDEV1CFGPADCTRL = 0x20000878UL,
    NvOdmPinRegister_Ap20_PadCtrl_CDEV2CFGPADCTRL = 0x2000087CUL,
    NvOdmPinRegister_Ap20_PadCtrl_CSUSCFGPADCTRL = 0x20000880UL,
    NvOdmPinRegister_Ap20_PadCtrl_DAP1CFGPADCTRL = 0x20000884UL,
    NvOdmPinRegister_Ap20_PadCtrl_DAP2CFGPADCTRL = 0x20000888UL,
    NvOdmPinRegister_Ap20_PadCtrl_DAP3CFGPADCTRL = 0x2000088CUL,
    NvOdmPinRegister_Ap20_PadCtrl_DAP4CFGPADCTRL = 0x20000890UL,
    NvOdmPinRegister_Ap20_PadCtrl_DBGCFGPADCTRL = 0x20000894UL,
    NvOdmPinRegister_Ap20_PadCtrl_LCDCFG1PADCTRL = 0x20000898UL,
    NvOdmPinRegister_Ap20_PadCtrl_LCDCFG2PADCTRL = 0x2000089CUL,
    NvOdmPinRegister_Ap20_PadCtrl_SDIO2CFGPADCTRL = 0x200008A0UL,
    NvOdmPinRegister_Ap20_PadCtrl_SDIO3CFGPADCTRL = 0x200008A4UL,
    NvOdmPinRegister_Ap20_PadCtrl_SPICFGPADCTRL = 0x200008A8UL,
    NvOdmPinRegister_Ap20_PadCtrl_UAACFGPADCTRL = 0x200008ACUL,
    NvOdmPinRegister_Ap20_PadCtrl_UABCFGPADCTRL = 0x200008B0UL,
    NvOdmPinRegister_Ap20_PadCtrl_UART2CFGPADCTRL = 0x200008B4UL,
    NvOdmPinRegister_Ap20_PadCtrl_UART3CFGPADCTRL = 0x200008B8UL,
    NvOdmPinRegister_Ap20_PadCtrl_VICFG1PADCTRL = 0x200008BCUL,
    NvOdmPinRegister_Ap20_PadCtrl_VICFG2PADCTRL = 0x200008C0UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CFGAPADCTRL = 0x200008C4UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CFGCPADCTRL = 0x200008C8UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CFGDPADCTRL = 0x200008CCUL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CLKCFGPADCTRL = 0x200008D0UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2COMPPADCTRL = 0x200008D4UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2VTTGENPADCTRL = 0x200008D8UL,
    NvOdmPinRegister_Ap20_PadCtrl_SDIO1CFGPADCTRL = 0x200008E0UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CFGCPADCTRL2 = 0x200008E4UL,
    NvOdmPinRegister_Ap20_PadCtrl_XM2CFGDPADCTRL2 = 0x200008E8UL,
    NvOdmPinRegister_Ap20_PadCtrl_CRTCFGPADCTRL = 0x200008ECUL,
    NvOdmPinRegister_Ap20_PadCtrl_DDCCFGPADCTRL = 0x200008F0UL,
    NvOdmPinRegister_Ap20_PadCtrl_GMACFGPADCTRL = 0x200008F4UL,
    NvOdmPinRegister_Ap20_PadCtrl_GMBCFGPADCTRL = 0x200008F8UL,
    NvOdmPinRegister_Ap20_PadCtrl_GMCCFGPADCTRL = 0x200008FCUL,
    NvOdmPinRegister_Ap20_PadCtrl_GMDCFGPADCTRL = 0x20000900UL,
    NvOdmPinRegister_Ap20_PadCtrl_GMECFGPADCTRL = 0x20000904UL,
    NvOdmPinRegister_Ap20_PadCtrl_OWRCFGPADCTRL = 0x20000908UL,
    NvOdmPinRegister_Ap20_PadCtrl_UADCFGPADCTRL = 0x2000090CUL,

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

#define NVODM_QUERY_PIN_AP20_PULLUPDOWN_A(ATA, ATB, ATC, ATD, ATE, DAP1, DAP2, DAP3, DAP4, DTA, DTB, DTC, DTD, DTE, DTF, GPV) \
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
 * @param CRTP : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SLXK : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP20_PULLUPDOWN_B(RM, I2CP, PTA, GPU7, KBCA, KBCB, KBCC, KBCD, SPDI, SPDO, GPU, SLXA, CRTP, SLXC, SLXD, SLXK) \
    ((((RM)&3UL) << 0) | (((I2CP)&3UL) << 2) | (((PTA)&3UL) << 4) | \
     (((GPU7)&3UL) << 6) | (((KBCA)&3UL) << 8) | (((KBCB)&3UL) << 10) | \
     (((KBCC)&3UL) << 12) | (((KBCD)&3UL) << 14) | (((SPDI)&3UL) << 16) | \
     (((SPDO)&3UL) << 18) | (((GPU)&3UL) << 20) | (((SLXA)&3UL) << 22) | \
     (((CRTP)&3UL) << 24) | (((SLXC)&3UL) << 26) | (((SLXD)&3UL) << 28) | \
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
 * @param GME : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2D : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param XM2C : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP20_PULLUPDOWN_C(CDEV1, CDEV2, SPIA, SPIB, SPIC, SPID, SPIE, SPIF, SPIG, SPIH, IRTX, IRRX, GME, XM2D, XM2C) \
    ((((CDEV1)&3UL) << 0) | (((CDEV2)&3UL) << 2) | (((SPIA)&3UL) << 4) | \
     (((SPIB)&3UL) << 6) | (((SPIC)&3UL) << 8) | (((SPID)&3UL) << 10) | \
     (((SPIE)&3UL) << 12) | (((SPIF)&3UL) << 14) | (((SPIG)&3UL) << 16) | \
     (((SPIH)&3UL) << 18) | (((IRTX)&3UL) << 20) | (((IRRX)&3UL) << 22) | \
     (((GME)&3UL) << 24) | (((XM2D)&3UL) << 28) | (((XM2C)&3UL) << 30))

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
 * @param DDRC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP20_PULLUPDOWN_D(UAA, UAB, UAC, UAD, UCA, UCB, LD17_0, LD19_18, LD21_20, LD23_22, LS, LC, CSUS, DDRC, SDC, SDD) \
    ((((UAA)&3UL) << 0) | (((UAB)&3UL) << 2) | (((UAC)&3UL) << 4) | \
     (((UAD)&3UL) << 6) | (((UCA)&3UL) << 8) | (((UCB)&3UL) << 10) | \
     (((LD17_0)&3UL) << 12) | (((LD19_18)&3UL) << 14) | (((LD21_20)&3UL) << 16) | \
     (((LD23_22)&3UL) << 18) | (((LS)&3UL) << 20) | (((LC)&3UL) << 22) | \
     (((CSUS)&3UL) << 24) | (((DDRC)&3UL) << 26) | (((SDC)&3UL) << 28) | \
     (((SDD)&3UL) << 30))

/**
 * Use this macro to program the PullUpDown_E register.
 *
 * @param KBCF : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param KBCE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param PMCE : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param CK32 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param UDA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param SDIO1 : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GMA : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GMB : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GMC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param GMD : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param DDC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 * @param OWC : Configure internal pull-up/down (0 = normal, 1 = pull-down, 2 = pull-up).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP20_PULLUPDOWN_E(KBCF, KBCE, PMCA, PMCB, PMCC, PMCD, PMCE, CK32, UDA, SDIO1, GMA, GMB, GMC, GMD, DDC, OWC) \
    ((((KBCF)&3UL) << 0) | (((KBCE)&3UL) << 2) | (((PMCA)&3UL) << 4) | \
     (((PMCB)&3UL) << 6) | (((PMCC)&3UL) << 8) | (((PMCD)&3UL) << 10) | \
     (((PMCE)&3UL) << 12) | (((CK32)&3UL) << 14) | (((UDA)&3UL) << 16) | \
     (((SDIO1)&3UL) << 18) | (((GMA)&3UL) << 20) | (((GMB)&3UL) << 22) | \
     (((GMC)&3UL) << 24) | (((GMD)&3UL) << 26) | (((DDC)&3UL) << 28) | \
     (((OWC)&3UL) << 30))

/**
 * Use this macro to program the PadCtrl_AOCFG1PADCTRL, 
 * PadCtrl_AOCFG2PADCTRL, PadCtrl_ATCFG1PADCTRL, PadCtrl_ATCFG2PADCTRL, 
 * PadCtrl_CDEV1CFGPADCTRL, PadCtrl_CDEV2CFGPADCTRL, PadCtrl_CSUSCFGPADCTRL, 
 * PadCtrl_DAP1CFGPADCTRL, PadCtrl_DAP2CFGPADCTRL, PadCtrl_DAP3CFGPADCTRL, 
 * PadCtrl_DAP4CFGPADCTRL, PadCtrl_DBGCFGPADCTRL, PadCtrl_LCDCFG1PADCTRL, 
 * PadCtrl_LCDCFG2PADCTRL, PadCtrl_SDIO2CFGPADCTRL, PadCtrl_SDIO3CFGPADCTRL, 
 * PadCtrl_SPICFGPADCTRL, PadCtrl_UAACFGPADCTRL, PadCtrl_UABCFGPADCTRL, 
 * PadCtrl_UART2CFGPADCTRL, PadCtrl_UART3CFGPADCTRL, PadCtrl_VICFG1PADCTRL, 
 * PadCtrl_VICFG2PADCTRL, PadCtrl_SDIO1CFGPADCTRL, PadCtrl_CRTCFGPADCTRL, 
 * PadCtrl_DDCCFGPADCTRL, PadCtrl_GMACFGPADCTRL, PadCtrl_GMBCFGPADCTRL, 
 * PadCtrl_GMCCFGPADCTRL, PadCtrl_GMDCFGPADCTRL, PadCtrl_GMECFGPADCTRL, 
 * PadCtrl_OWRCFGPADCTRL and PadCtrl_UADCFGPADCTRL registers.
 *
 * @param HSM_EN : Enable high-speed mode (0 = disable).  Valid Range 0 - 1
 * @param SCHMT_EN : Schmitt trigger enable (0 = disable).  Valid Range 0 - 1
 * @param LPMD : Low-power current/impedance selection (0 = 400 ohm, 1 = 200 ohm, 2 = 100 ohm, 3 = 50 ohm).  Valid Range 0 - 3
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 3
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 3
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_AOCFG1PADCTRL(HSM_EN, SCHMT_EN, LPMD, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((HSM_EN)&1UL) << 2) | (((SCHMT_EN)&1UL) << 3) | (((LPMD)&3UL) << 4) | \
     (((CAL_DRVDN)&31UL) << 12) | (((CAL_DRVUP)&31UL) << 20) | \
     (((CAL_DRVDN_SLWR)&3UL) << 28) | (((CAL_DRVUP_SLWF)&3UL) << 30))

/**
 * Use this macro to program the PadCtrl_XM2CFGAPADCTRL register.
 *
 * @param BYPASS_EN : .  Valid Range 0 - 1
 * @param PREEMP_EN : .  Valid Range 0 - 1
 * @param CLK_SEL : .  Valid Range 0 - 1
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 15
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 15
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2CFGAPADCTRL(BYPASS_EN, PREEMP_EN, CLK_SEL, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((BYPASS_EN)&1UL) << 4) | (((PREEMP_EN)&1UL) << 5) | \
     (((CLK_SEL)&1UL) << 6) | (((CAL_DRVDN)&31UL) << 14) | \
     (((CAL_DRVUP)&31UL) << 19) | (((CAL_DRVDN_SLWR)&15UL) << 24) | \
     (((CAL_DRVUP_SLWF)&15UL) << 28))

/**
 * Use this macro to program the PadCtrl_XM2CFGCPADCTRL and 
 * PadCtrl_XM2CFGDPADCTRL registers.
 *
 * @param SCHMT_EN : Schmitt trigger enable (0 = disable).  Valid Range 0 - 1
 * @param CAL_DRVDN_TERM : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP_TERM : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 15
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 15
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2CFGCPADCTRL(SCHMT_EN, CAL_DRVDN_TERM, CAL_DRVUP_TERM, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((SCHMT_EN)&1UL) << 3) | (((CAL_DRVDN_TERM)&31UL) << 4) | \
     (((CAL_DRVUP_TERM)&31UL) << 9) | (((CAL_DRVDN)&31UL) << 14) | \
     (((CAL_DRVUP)&31UL) << 19) | (((CAL_DRVDN_SLWR)&15UL) << 24) | \
     (((CAL_DRVUP_SLWF)&15UL) << 28))

/**
 * Use this macro to program the PadCtrl_XM2CLKCFGPADCTRL register.
 *
 * @param BYPASS_EN : .  Valid Range 0 - 1
 * @param PREEMP_EN : .  Valid Range 0 - 1
 * @param CAL_BYPASS_EN : .  Valid Range 0 - 1
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 * @param CAL_DRVDN_SLWR : Pull-up slew control (0 = max).  Valid Range 0 - 15
 * @param CAL_DRVUP_SLWF : Pull-down slew control (0 = max).  Valid Range 0 - 15
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2CLKCFGPADCTRL(BYPASS_EN, PREEMP_EN, CAL_BYPASS_EN, CAL_DRVDN, CAL_DRVUP, CAL_DRVDN_SLWR, CAL_DRVUP_SLWF) \
    ((((BYPASS_EN)&1UL) << 1) | (((PREEMP_EN)&1UL) << 2) | \
     (((CAL_BYPASS_EN)&1UL) << 3) | (((CAL_DRVDN)&31UL) << 14) | \
     (((CAL_DRVUP)&31UL) << 19) | (((CAL_DRVDN_SLWR)&15UL) << 24) | \
     (((CAL_DRVUP_SLWF)&15UL) << 28))

/**
 * Use this macro to program the PadCtrl_XM2COMPPADCTRL register.
 *
 * @param VREF_SEL : .  Valid Range 0 - 15
 * @param TESTOUT_EN : .  Valid Range 0 - 1
 * @param BIAS_SEL : .  Valid Range 0 - 7
 * @param DRVDN : Pull-down drive strength.  Valid Range 0 - 31
 * @param DRVUP : Pull-up drive strength.  Valid Range 0 - 31
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2COMPPADCTRL(VREF_SEL, TESTOUT_EN, BIAS_SEL, DRVDN, DRVUP) \
    ((((VREF_SEL)&15UL) << 0) | (((TESTOUT_EN)&1UL) << 4) | \
     (((BIAS_SEL)&7UL) << 5) | (((DRVDN)&31UL) << 12) | (((DRVUP)&31UL) << 20))

/**
 * Use this macro to program the PadCtrl_XM2VTTGENPADCTRL register.
 *
 * @param SHORT : .  Valid Range 0 - 1
 * @param SHORT_PWRGND : .  Valid Range 0 - 1
 * @param VCLAMP_LEVEL : .  Valid Range 0 - 7
 * @param VAUXP_LEVEL : .  Valid Range 0 - 7
 * @param CAL_DRVDN : Pull-down drive strength.  Valid Range 0 - 7
 * @param CAL_DRVUP : Pull-up drive strength.  Valid Range 0 - 7
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2VTTGENPADCTRL(SHORT, SHORT_PWRGND, VCLAMP_LEVEL, VAUXP_LEVEL, CAL_DRVDN, CAL_DRVUP) \
    ((((SHORT)&1UL) << 0) | (((SHORT_PWRGND)&1UL) << 1) | \
     (((VCLAMP_LEVEL)&7UL) << 8) | (((VAUXP_LEVEL)&7UL) << 12) | \
     (((CAL_DRVDN)&7UL) << 16) | (((CAL_DRVUP)&7UL) << 24))

/**
 * Use this macro to program the PadCtrl_XM2CFGCPADCTRL2 register.
 *
 * @param RX_FT_REC_EN : .  Valid Range 0 - 1
 * @param BYPASS_EN : .  Valid Range 0 - 1
 * @param PREEMP_EN : .  Valid Range 0 - 1
 * @param CTT_HIZ_EN : .  Valid Range 0 - 1
 * @param VREF_DQS_EN : .  Valid Range 0 - 1
 * @param VREF_DQ_EN : .  Valid Range 0 - 1
 * @param CLKSEL_DQ : .  Valid Range 0 - 1
 * @param CLKSEL_DQS : .  Valid Range 0 - 1
 * @param VREF_DQS : .  Valid Range 0 - 15
 * @param VREF_DQ : .  Valid Range 0 - 15
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2CFGCPADCTRL2(RX_FT_REC_EN, BYPASS_EN, PREEMP_EN, CTT_HIZ_EN, VREF_DQS_EN, VREF_DQ_EN, CLKSEL_DQ, CLKSEL_DQS, VREF_DQS, VREF_DQ) \
    ((((RX_FT_REC_EN)&1UL) << 0) | (((BYPASS_EN)&1UL) << 1) | \
     (((PREEMP_EN)&1UL) << 2) | (((CTT_HIZ_EN)&1UL) << 3) | \
     (((VREF_DQS_EN)&1UL) << 4) | (((VREF_DQ_EN)&1UL) << 5) | \
     (((CLKSEL_DQ)&1UL) << 6) | (((CLKSEL_DQS)&1UL) << 7) | \
     (((VREF_DQS)&15UL) << 16) | (((VREF_DQ)&15UL) << 24))

/**
 * Use this macro to program the PadCtrl_XM2CFGDPADCTRL2 register.
 *
 * @param RX_FT_REC : .  Valid Range 0 - 1
 * @param BYPASS : .  Valid Range 0 - 1
 * @param PREEMP : .  Valid Range 0 - 1
 * @param CTT_HIZ : .  Valid Range 0 - 1
 */

#define NVODM_QUERY_PIN_AP20_PADCTRL_XM2CFGDPADCTRL2(RX_FT_REC, BYPASS, PREEMP, CTT_HIZ) \
    ((((RX_FT_REC)&1UL) << 0) | (((BYPASS)&1UL) << 1) | (((PREEMP)&1UL) << 2) | \
     (((CTT_HIZ)&1UL) << 3))

#ifdef __cplusplus
}
#endif

/** @} */
#endif // INCLUDED_NVODM_QUERY_PINS_AP20_H


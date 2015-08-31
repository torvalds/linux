/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef MXC_VDEC_H
#define MXC_VDEC_H

/*** define base address ***/
#define VDEC_BASE vdec_regbase
#define AFE_BASE vafe_regbase

/* AFE - Register offsets */
#define AFE_BLOCK_ID_OFFSET				0x00000000
#define AFE_PDBUF_OFFSET				0x00000004
#define AFE_SWRST_OFFSET				0x00000008
#define AFE_TSTSEL_OFFSET				0x0000000c
#define AFE_TSTMSC_OFFSET				0x00000010
#define AFE_ENPADIO_OFFSET				0x00000014
#define AFE_BGREG_OFFSET				0x00000018
#define AFE_ACCESSAR_ID_OFFSET			0x00000400
#define AFE_PDADC_OFFSET				0x00000404
#define AFE_PDSARH_OFFSET				0x00000408
#define AFE_PDSARL_OFFSET				0x0000040C
#define AFE_PDADCRFH_OFFSET				0x00000410
#define AFE_PDADCRFL_OFFSET				0x00000414
#define AFE_ACCTST_OFFSET				0x00000418
#define AFE_ADCGN_OFFSET				0x0000041C
#define AFE_ICTRL_OFFSET				0x00000420
#define AFE_ICTLSTG_OFFSET				0x00000424
#define AFE_RCTRLSTG_OFFSET				0x00000428
#define AFE_TCTRLSTG_OFFSET				0x0000042c
#define AFE_REFMOD_OFFSET				0x00000430
#define AFE_REFTRIML_OFFSET				0x00000434
#define AFE_REFTRIMH_OFFSET				0x00000438
#define AFE_ADCR_OFFSET					0x0000043c
#define AFE_DUMMY0_OFFSET				0x00000440
#define AFE_DUMMY1_OFFSET				0x00000444
#define AFE_DUMMY2_OFFSET				0x00000448
#define AFE_DACAMP_OFFSET				0x0000044c
#define AFE_CLMPTST_OFFSET				0x00000450
#define AFE_CLMPDAT_OFFSET				0x00000454
#define AFE_CLMPAMP_OFFSET				0x00000458
#define AFE_CLAMP_OFFSET				0x0000045c
#define AFE_INPBUF_OFFSET				0x00000460
#define AFE_INPFLT_OFFSET				0x00000464
#define AFE_ADCDGN_OFFSET				0x00000468
#define AFE_OFFDRV_OFFSET				0x0000046c
#define AFE_INPCONFIG_OFFSET			0x00000470
#define AFE_PROGDELAY_OFFSET			0x00000474
#define AFE_ADCOMT_OFFSET				0x00000478
#define AFE_ALGDELAY_OFFSET				0x0000047c
#define AFE_ACC_ID_OFFSET				0x00000800
#define AFE_ACCSTA_OFFSET				0x00000804
#define AFE_ACCNOSLI_OFFSET				0x00000808
#define AFE_ACCCALCON_OFFSET			0x0000080c
#define AFE_BWEWRICTRL_OFFSET			0x00000810
#define AFE_SELSLI_OFFSET				0x00000814
#define AFE_SELBYT_OFFSET				0x00000818
#define AFE_REDVAL_OFFSET				0x00000820
#define AFE_WRIBYT_OFFSET				0x00000824

/* AFE Register per module */
#define AFE_BLOCK_ID			(AFE_BASE + AFE_BLOCK_ID_OFFSET)
#define AFE_PDBUF				(AFE_BASE + AFE_PDBUF_OFFSET)
#define AFE_SWRST				(AFE_BASE + AFE_SWRST_OFFSET)
#define AFE_TSTSEL				(AFE_BASE + AFE_TSTSEL_OFFSET)
#define AFE_TSTMSC				(AFE_BASE + AFE_TSTMSC_OFFSET)
#define AFE_ENPADIO				(AFE_BASE + AFE_ENPADIO_OFFSET)
#define AFE_BGREG				(AFE_BASE + AFE_BGREG_OFFSET)
#define AFE_ACCESSAR_ID			(AFE_BASE + AFE_ACCESSAR_ID_OFFSET)
#define AFE_PDADC				(AFE_BASE + AFE_PDADC_OFFSET)
#define AFE_PDSARH			    (AFE_BASE + AFE_PDSARH_OFFSET)
#define AFE_PDSARL			    (AFE_BASE + AFE_PDSARL_OFFSET)
#define AFE_PDADCRFH			(AFE_BASE + AFE_PDADCRFH_OFFSET)
#define AFE_PDADCRFL			(AFE_BASE + AFE_PDADCRFL_OFFSET)
#define AFE_ACCTST				(AFE_BASE + AFE_ACCTST_OFFSET)
#define AFE_ADCGN				(AFE_BASE + AFE_ADCGN_OFFSET)
#define AFE_ICTRL				(AFE_BASE + AFE_ICTRL_OFFSET)
#define AFE_ICTLSTG				(AFE_BASE + AFE_ICTLSTG_OFFSET)
#define AFE_RCTRLSTG			(AFE_BASE + AFE_RCTRLSTG_OFFSET)
#define AFE_TCTRLSTG			(AFE_BASE + AFE_TCTRLSTG_OFFSET)
#define AFE_REFMOD				(AFE_BASE + AFE_REFMOD_OFFSET)
#define AFE_REFTRIML			(AFE_BASE + AFE_REFTRIML_OFFSET)
#define AFE_REFTRIMH			(AFE_BASE + AFE_REFTRIMH_OFFSET)
#define AFE_ADCR				(AFE_BASE + AFE_ADCR_OFFSET)
#define AFE_DUMMY0				(AFE_BASE + AFE_DUMMY0_OFFSET)
#define AFE_DUMMY1				(AFE_BASE + AFE_DUMMY1_OFFSET)
#define AFE_DUMMY2				(AFE_BASE + AFE_DUMMY2_OFFSET)
#define AFE_DACAMP				(AFE_BASE + AFE_DACAMP_OFFSET)
#define AFE_CLMPTST				(AFE_BASE + AFE_CLMPTST_OFFSET)
#define AFE_CLMPDAT				(AFE_BASE + AFE_CLMPDAT_OFFSET)
#define AFE_CLMPAMP				(AFE_BASE + AFE_CLMPAMP_OFFSET)
#define AFE_CLAMP				(AFE_BASE + AFE_CLAMP_OFFSET)
#define AFE_INPBUF				(AFE_BASE + AFE_INPBUF_OFFSET)
#define AFE_INPFLT				(AFE_BASE + AFE_INPFLT_OFFSET)
#define AFE_ADCDGN				(AFE_BASE + AFE_ADCDGN_OFFSET)
#define AFE_OFFDRV				(AFE_BASE + AFE_OFFDRV_OFFSET)
#define AFE_INPCONFIG			(AFE_BASE + AFE_INPCONFIG_OFFSET)
#define AFE_PROGDELAY			(AFE_BASE + AFE_PROGDELAY_OFFSET)
#define AFE_ADCOMT				(AFE_BASE + AFE_ADCOMT_OFFSET)
#define AFE_ALGDELAY			(AFE_BASE + AFE_ALGDELAY_OFFSET)
#define AFE_ACC_ID				(AFE_BASE + AFE_ACC_ID_OFFSET)
#define AFE_ACCSTA				(AFE_BASE + AFE_ACCSTA_OFFSET)
#define AFE_ACCNOSLI			(AFE_BASE + AFE_ACCNOSLI_OFFSET)
#define AFE_ACCCALCON			(AFE_BASE + AFE_ACCCALCON_OFFSET)
#define AFE_BWEWRICTRL			(AFE_BASE + AFE_BWEWRICTRL_OFFSET)
#define AFE_SELSLI				(AFE_BASE + AFE_SELSLI_OFFSET)
#define AFE_SELBYT				(AFE_BASE + AFE_SELBYT_OFFSET)
#define AFE_REDVAL				(AFE_BASE + AFE_REDVAL_OFFSET)
#define AFE_WRIBYT				(AFE_BASE + AFE_WRIBYT_OFFSET)

/* VDEC - Register offsets */
#define VDEC_CFC1_OFFSET                0x00000000
#define VDEC_CFC2_OFFSET                0x00000004
#define VDEC_BRSTGT_OFFSET              0x00000024
#define VDEC_HZPOS_OFFSET               0x00000040
#define VDEC_VRTPOS_OFFSET              0x00000044
#define VDEC_HVSHIFT_OFFSET             0x00000054
#define VDEC_HSIGS_OFFSET               0x00000058
#define VDEC_HSIGE_OFFSET               0x0000005C
#define VDEC_VSCON1_OFFSET              0x00000060
#define VDEC_VSCON2_OFFSET              0x00000064
#define VDEC_YCDEL_OFFSET               0x0000006C
#define VDEC_AFTCLP_OFFSET              0x00000070
#define VDEC_DCOFF_OFFSET               0x00000078
#define VDEC_CSID_OFFSET                0x00000084
#define VDEC_CBGN_OFFSET                0x00000088
#define VDEC_CRGN_OFFSET                0x0000008C
#define VDEC_CNTR_OFFSET                0x00000090
#define VDEC_BRT_OFFSET                 0x00000094
#define VDEC_HUE_OFFSET                 0x00000098
#define VDEC_CHBTH_OFFSET               0x0000009C
#define VDEC_SHPIMP_OFFSET              0x000000A4
#define VDEC_CHPLLIM_OFFSET             0x000000A8
#define VDEC_VIDMOD_OFFSET              0x000000AC
#define VDEC_VIDSTS_OFFSET              0x000000B0
#define VDEC_NOISE_OFFSET               0x000000B4
#define VDEC_STDDBG_OFFSET              0x000000B8
#define VDEC_MANOVR_OFFSET              0x000000BC
#define VDEC_VSSGTH_OFFSET              0x000000C8
#define VDEC_DBGFBH_OFFSET              0x000000D0
#define VDEC_DBGFBL_OFFSET              0x000000D4
#define VDEC_HACTS_OFFSET               0x000000D8
#define VDEC_HACTE_OFFSET               0x000000DC
#define VDEC_VACTS_OFFSET               0x000000E0
#define VDEC_VACTE_OFFSET               0x000000E4
#define VDEC_HSTIP_OFFSET               0x000000EC
#define VDEC_BLSCRY_OFFSET              0x000000F4
#define VDEC_BLSCRCR_OFFSET             0x000000F8
#define VDEC_BLSCRCB_OFFSET             0x000000FC
#define VDEC_LMAGC1_OFFSET              0x00000100
#define VDEC_LMAGC2_OFFSET              0x00000104
#define VDEC_CHAGC1_OFFSET              0x00000108
#define VDEC_CHAGC2_OFFSET              0x0000010C
#define VDEC_MINTH_OFFSET               0x00000114
#define VDEC_VFRQOH_OFFSET              0x0000011C
#define VDEC_VFRQOL_OFFSET              0x00000120
#define VDEC_THSH1_OFFSET               0x00000124
#define VDEC_THSH2_OFFSET               0x00000128
#define VDEC_NCHTH_OFFSET               0x0000012C
#define VDEC_TH1F_OFFSET                0x00000130

/* VDEC Register per module */
#define VDEC_CFC1                        (VDEC_BASE + VDEC_CFC1_OFFSET)
#define VDEC_CFC2                        (VDEC_BASE + VDEC_CFC2_OFFSET)
#define VDEC_BRSTGT                      (VDEC_BASE + VDEC_BRSTGT_OFFSET)
#define VDEC_HZPOS                       (VDEC_BASE + VDEC_HZPOS_OFFSET)
#define VDEC_VRTPOS                      (VDEC_BASE + VDEC_VRTPOS_OFFSET)
#define VDEC_HVSHIFT                     (VDEC_BASE + VDEC_HVSHIFT_OFFSET)
#define VDEC_HSIGS                       (VDEC_BASE + VDEC_HSIGS_OFFSET)
#define VDEC_HSIGE                       (VDEC_BASE + VDEC_HSIGE_OFFSET)
#define VDEC_VSCON1                      (VDEC_BASE + VDEC_VSCON1_OFFSET)
#define VDEC_VSCON2                      (VDEC_BASE + VDEC_VSCON2_OFFSET)
#define VDEC_YCDEL                       (VDEC_BASE + VDEC_YCDEL_OFFSET)
#define VDEC_AFTCLP                      (VDEC_BASE + VDEC_AFTCLP_OFFSET)
#define VDEC_DCOFF                       (VDEC_BASE + VDEC_DCOFF_OFFSET)
#define VDEC_CSID                        (VDEC_BASE + VDEC_CSID_OFFSET)
#define VDEC_CBGN                        (VDEC_BASE + VDEC_CBGN_OFFSET)
#define VDEC_CRGN                        (VDEC_BASE + VDEC_CRGN_OFFSET)
#define VDEC_CNTR                        (VDEC_BASE + VDEC_CNTR_OFFSET)
#define VDEC_BRT                         (VDEC_BASE + VDEC_BRT_OFFSET)
#define VDEC_HUE                         (VDEC_BASE + VDEC_HUE_OFFSET)
#define VDEC_CHBTH                       (VDEC_BASE + VDEC_CHBTH_OFFSET)
#define VDEC_SHPIMP                      (VDEC_BASE + VDEC_SHPIMP_OFFSET)
#define VDEC_CHPLLIM                     (VDEC_BASE + VDEC_CHPLLIM_OFFSET)
#define VDEC_VIDMOD                      (VDEC_BASE + VDEC_VIDMOD_OFFSET)
#define VDEC_VIDSTS                      (VDEC_BASE + VDEC_VIDSTS_OFFSET)
#define VDEC_NOISE                       (VDEC_BASE + VDEC_NOISE_OFFSET)
#define VDEC_STDDBG                      (VDEC_BASE + VDEC_STDDBG_OFFSET)
#define VDEC_MANOVR                      (VDEC_BASE + VDEC_MANOVR_OFFSET)
#define VDEC_VSSGTH                      (VDEC_BASE + VDEC_VSSGTH_OFFSET)
#define VDEC_DBGFBH                      (VDEC_BASE + VDEC_DBGFBH_OFFSET)
#define VDEC_DBGFBL                      (VDEC_BASE + VDEC_DBGFBL_OFFSET)
#define VDEC_HACTS                       (VDEC_BASE + VDEC_HACTS_OFFSET)
#define VDEC_HACTE                       (VDEC_BASE + VDEC_HACTE_OFFSET)
#define VDEC_VACTS                       (VDEC_BASE + VDEC_VACTS_OFFSET)
#define VDEC_VACTE                       (VDEC_BASE + VDEC_VACTE_OFFSET)
#define VDEC_HSTIP                       (VDEC_BASE + VDEC_HSTIP_OFFSET)
#define VDEC_BLSCRY                      (VDEC_BASE + VDEC_BLSCRY_OFFSET)
#define VDEC_BLSCRCR                     (VDEC_BASE + VDEC_BLSCRCR_OFFSET)
#define VDEC_BLSCRCB                     (VDEC_BASE + VDEC_BLSCRCB_OFFSET)
#define VDEC_LMAGC1                      (VDEC_BASE + VDEC_LMAGC1_OFFSET)
#define VDEC_LMAGC2                      (VDEC_BASE + VDEC_LMAGC2_OFFSET)
#define VDEC_CHAGC1                      (VDEC_BASE + VDEC_CHAGC1_OFFSET)
#define VDEC_CHAGC2                      (VDEC_BASE + VDEC_CHAGC2_OFFSET)
#define VDEC_MINTH                       (VDEC_BASE + VDEC_MINTH_OFFSET)
#define VDEC_VFRQOH                      (VDEC_BASE + VDEC_VFRQOH_OFFSET)
#define VDEC_VFRQOL                      (VDEC_BASE + VDEC_VFRQOL_OFFSET)
#define VDEC_THSH1                       (VDEC_BASE + VDEC_THSH1_OFFSET)
#define VDEC_THSH2                       (VDEC_BASE + VDEC_THSH2_OFFSET)
#define VDEC_NCHTH                       (VDEC_BASE + VDEC_NCHTH_OFFSET)
#define VDEC_TH1F                        (VDEC_BASE + VDEC_TH1F_OFFSET)

#define VDEC_VIDMOD_SIGNAL_MASK           0x0F
#define VDEC_VIDMOD_SIGNAL_DETECT         0x0F

#define VDEC_VIDMOD_M625_SHIFT            4
#define VDEC_VIDMOD_M625_MASK             (1 << VDEC_VIDMOD_M625_SHIFT)

#define VDEC_VIDMOD_PAL_SHIFT             7
#define VDEC_VIDMOD_PAL_MASK              (1 << VDEC_VIDMOD_PAL_SHIFT)
/*** define base address ***/

#endif

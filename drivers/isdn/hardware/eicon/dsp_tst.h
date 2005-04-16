/* $Id: dsp_tst.h,v 1.1.2.2 2001/02/08 12:25:43 armin Exp $ */

#ifndef __DIVA_PRI_HOST_TEST_DSPS_H__
#define __DIVA_PRI_HOST_TEST_DSPS_H__

/*
   DSP registers on maestra pri
   */
#define DSP1_PORT       (0x00)
#define DSP2_PORT       (0x8)
#define DSP3_PORT       (0x800)
#define DSP4_PORT       (0x808)
#define DSP5_PORT       (0x810)
#define DSP6_PORT       (0x818)
#define DSP7_PORT       (0x820)
#define DSP8_PORT       (0x828)
#define DSP9_PORT       (0x830)
#define DSP10_PORT      (0x840)
#define DSP11_PORT      (0x848)
#define DSP12_PORT      (0x850)
#define DSP13_PORT      (0x858)
#define DSP14_PORT      (0x860)
#define DSP15_PORT      (0x868)
#define DSP16_PORT      (0x870)
#define DSP17_PORT      (0x1000)
#define DSP18_PORT      (0x1008)
#define DSP19_PORT      (0x1010)
#define DSP20_PORT      (0x1018)
#define DSP21_PORT      (0x1020)
#define DSP22_PORT      (0x1028)
#define DSP23_PORT      (0x1030)
#define DSP24_PORT      (0x1040)
#define DSP25_PORT      (0x1048)
#define DSP26_PORT      (0x1050)
#define DSP27_PORT      (0x1058)
#define DSP28_PORT      (0x1060)
#define DSP29_PORT      (0x1068)
#define DSP30_PORT      (0x1070)
#define DSP_ADR_OFFS    0x80

/*------------------------------------------------------------------
		Dsp related definitions
  ------------------------------------------------------------------ */
#define DSP_SIGNATURE_PROBE_WORD 0x5a5a
#define dsp_make_address_ex(pm,address) ((word)((pm) ? (address) : (address) + 0x4000))

#endif

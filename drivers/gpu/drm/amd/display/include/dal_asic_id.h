/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_ASIC_ID_H__
#define __DAL_ASIC_ID_H__

/*
 * ASIC internal revision ID
 */

/* DCE80 (based on ci_id.h in Perforce) */
#define	CI_BONAIRE_M_A0 0x14
#define	CI_BONAIRE_M_A1	0x15
#define	CI_HAWAII_P_A0	0x28

#define CI_UNKNOWN	0xFF

#define ASIC_REV_IS_BONAIRE_M(rev) \
	((rev >= CI_BONAIRE_M_A0) && (rev < CI_HAWAII_P_A0))

#define ASIC_REV_IS_HAWAII_P(rev) \
	(rev >= CI_HAWAII_P_A0)

/* KV1 with Spectre GFX core, 8-8-1-2 (CU-Pix-Primitive-RB) */
#define KV_SPECTRE_A0 0x01

/* KV2 with Spooky GFX core, including downgraded from Spectre core,
 * 3-4-1-1 (CU-Pix-Primitive-RB) */
#define KV_SPOOKY_A0 0x41

/* KB with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
#define KB_KALINDI_A0 0x81

/* KB with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
#define KB_KALINDI_A1 0x82

/* BV with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
#define BV_KALINDI_A2 0x85

/* ML with Godavari GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
#define ML_GODAVARI_A0 0xA1

/* ML with Godavari GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
#define ML_GODAVARI_A1 0xA2

#define KV_UNKNOWN 0xFF

#define ASIC_REV_IS_KALINDI(rev) \
	((rev >= KB_KALINDI_A0) && (rev < KV_UNKNOWN))

#define ASIC_REV_IS_BHAVANI(rev) \
	((rev >= BV_KALINDI_A2) && (rev < ML_GODAVARI_A0))

#define ASIC_REV_IS_GODAVARI(rev) \
	((rev >= ML_GODAVARI_A0) && (rev < KV_UNKNOWN))

/* VI Family */
/* DCE10 */
#define VI_TONGA_P_A0 20
#define VI_TONGA_P_A1 21
#define VI_FIJI_P_A0 60

/* DCE112 */
#define VI_POLARIS10_P_A0 80
#define VI_POLARIS11_M_A0 90
#define VI_POLARIS12_V_A0 100
#define VI_VEGAM_A0 110

#define VI_UNKNOWN 0xFF

#define ASIC_REV_IS_TONGA_P(eChipRev) ((eChipRev >= VI_TONGA_P_A0) && \
		(eChipRev < 40))
#define ASIC_REV_IS_FIJI_P(eChipRev) ((eChipRev >= VI_FIJI_P_A0) && \
		(eChipRev < 80))

#define ASIC_REV_IS_POLARIS10_P(eChipRev) ((eChipRev >= VI_POLARIS10_P_A0) && \
		(eChipRev < VI_POLARIS11_M_A0))
#define ASIC_REV_IS_POLARIS11_M(eChipRev) ((eChipRev >= VI_POLARIS11_M_A0) &&  \
		(eChipRev < VI_POLARIS12_V_A0))
#define ASIC_REV_IS_POLARIS12_V(eChipRev) ((eChipRev >= VI_POLARIS12_V_A0) && \
		(eChipRev < VI_VEGAM_A0))
#define ASIC_REV_IS_VEGAM(eChipRev) (eChipRev >= VI_VEGAM_A0)

/* DCE11 */
#define CZ_CARRIZO_A0 0x01

#define STONEY_A0 0x61
#define CZ_UNKNOWN 0xFF

#define ASIC_REV_IS_STONEY(rev) \
	((rev >= STONEY_A0) && (rev < CZ_UNKNOWN))

/* DCE12 */
#define AI_UNKNOWN 0xFF

#define AI_GREENLAND_P_A0 1
#define AI_GREENLAND_P_A1 2
#define AI_UNKNOWN 0xFF

#define AI_VEGA12_P_A0 20
#define AI_VEGA20_P_A0 40
#define ASICREV_IS_GREENLAND_M(eChipRev)  (eChipRev < AI_VEGA12_P_A0)
#define ASICREV_IS_GREENLAND_P(eChipRev)  (eChipRev < AI_VEGA12_P_A0)

#define ASICREV_IS_VEGA12_P(eChipRev) ((eChipRev >= AI_VEGA12_P_A0) && (eChipRev < AI_VEGA20_P_A0))
#define ASICREV_IS_VEGA20_P(eChipRev) ((eChipRev >= AI_VEGA20_P_A0) && (eChipRev < AI_UNKNOWN))

/* DCN1_0 */
#define INTERNAL_REV_RAVEN_A0             0x00    /* First spin of Raven */
#define RAVEN_A0 0x01
#define RAVEN_B0 0x21
#define PICASSO_A0 0x41
/* DCN1_01 */
#define RAVEN2_A0 0x81
#define RAVEN1_F0 0xF0
#define RAVEN_UNKNOWN 0xFF

#define ASICREV_IS_RAVEN(eChipRev) ((eChipRev >= RAVEN_A0) && eChipRev < RAVEN_UNKNOWN)
#define ASICREV_IS_PICASSO(eChipRev) ((eChipRev >= PICASSO_A0) && (eChipRev < RAVEN2_A0))
#define ASICREV_IS_RAVEN2(eChipRev) ((eChipRev >= RAVEN2_A0) && (eChipRev < 0xF0))


#define ASICREV_IS_RV1_F0(eChipRev) ((eChipRev >= RAVEN1_F0) && (eChipRev < RAVEN_UNKNOWN))


#define FAMILY_RV 142 /* DCN 1*/

/*
 * ASIC chip ID
 */
/* DCE80 */
#define DEVICE_ID_KALINDI_9834 0x9834
#define DEVICE_ID_TEMASH_9839 0x9839
#define DEVICE_ID_TEMASH_983D 0x983D

/* Asic Family IDs for different asic family. */
#define FAMILY_CI 120 /* Sea Islands: Hawaii (P), Bonaire (M) */
#define FAMILY_KV 125 /* Fusion => Kaveri: Spectre, Spooky; Kabini: Kalindi */
#define FAMILY_VI 130 /* Volcanic Islands: Iceland (V), Tonga (M) */
#define FAMILY_CZ 135 /* Carrizo */

#define FAMILY_AI 141

#define	FAMILY_UNKNOWN 0xFF



#endif /* __DAL_ASIC_ID_H__ */

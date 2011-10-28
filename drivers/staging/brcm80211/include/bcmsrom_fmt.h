/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_bcmsrom_fmt_h_
#define	_bcmsrom_fmt_h_

/* Maximum srom: 6 Kilobits == 768 bytes */
#define	SROM_MAX		768
#define SROM_MAXW		384
#define VARS_MAX		4096

/* PCI fields */
#define PCI_F0DEVID		48

#define	SROM_WORDS		64

#define SROM3_SWRGN_OFF		28	/* s/w region offset in words */

#define	SROM_SSID		2

#define	SROM_WL1LHMAXP		29

#define	SROM_WL1LPAB0		30
#define	SROM_WL1LPAB1		31
#define	SROM_WL1LPAB2		32

#define	SROM_WL1HPAB0		33
#define	SROM_WL1HPAB1		34
#define	SROM_WL1HPAB2		35

#define	SROM_MACHI_IL0		36
#define	SROM_MACMID_IL0		37
#define	SROM_MACLO_IL0		38
#define	SROM_MACHI_ET0		39
#define	SROM_MACMID_ET0		40
#define	SROM_MACLO_ET0		41
#define	SROM_MACHI_ET1		42
#define	SROM_MACMID_ET1		43
#define	SROM_MACLO_ET1		44
#define	SROM3_MACHI		37
#define	SROM3_MACMID		38
#define	SROM3_MACLO		39

#define	SROM_BXARSSI2G		40
#define	SROM_BXARSSI5G		41

#define	SROM_TRI52G		42
#define	SROM_TRI5GHL		43

#define	SROM_RXPO52G		45

#define	SROM2_ENETPHY		45

#define	SROM_AABREV		46
/* Fields in AABREV */
#define	SROM_BR_MASK		0x00ff
#define	SROM_CC_MASK		0x0f00
#define	SROM_CC_SHIFT		8
#define	SROM_AA0_MASK		0x3000
#define	SROM_AA0_SHIFT		12
#define	SROM_AA1_MASK		0xc000
#define	SROM_AA1_SHIFT		14

#define	SROM_WL0PAB0		47
#define	SROM_WL0PAB1		48
#define	SROM_WL0PAB2		49

#define	SROM_LEDBH10		50
#define	SROM_LEDBH32		51

#define	SROM_WL10MAXP		52

#define	SROM_WL1PAB0		53
#define	SROM_WL1PAB1		54
#define	SROM_WL1PAB2		55

#define	SROM_ITT		56

#define	SROM_BFL		57
#define	SROM_BFL2		28
#define	SROM3_BFL2		61

#define	SROM_AG10		58

#define	SROM_CCODE		59

#define	SROM_OPO		60

#define	SROM3_LEDDC		62

#define	SROM_CRCREV		63

/* SROM Rev 4: Reallocate the software part of the srom to accommodate
 * MIMO features. It assumes up to two PCIE functions and 440 bytes
 * of usable srom i.e. the usable storage in chips with OTP that
 * implements hardware redundancy.
 */

#define	SROM4_WORDS		220

#define	SROM4_SIGN		32
#define	SROM4_SIGNATURE		0x5372

#define	SROM4_BREV		33

#define	SROM4_BFL0		34
#define	SROM4_BFL1		35
#define	SROM4_BFL2		36
#define	SROM4_BFL3		37
#define	SROM5_BFL0		37
#define	SROM5_BFL1		38
#define	SROM5_BFL2		39
#define	SROM5_BFL3		40

#define	SROM4_MACHI		38
#define	SROM4_MACMID		39
#define	SROM4_MACLO		40
#define	SROM5_MACHI		41
#define	SROM5_MACMID		42
#define	SROM5_MACLO		43

#define	SROM4_CCODE		41
#define	SROM4_REGREV		42
#define	SROM5_CCODE		34
#define	SROM5_REGREV		35

#define	SROM4_LEDBH10		43
#define	SROM4_LEDBH32		44
#define	SROM5_LEDBH10		59
#define	SROM5_LEDBH32		60

#define	SROM4_LEDDC		45
#define	SROM5_LEDDC		45

#define	SROM4_AA		46
#define	SROM4_AA2G_MASK		0x00ff
#define	SROM4_AA2G_SHIFT	0
#define	SROM4_AA5G_MASK		0xff00
#define	SROM4_AA5G_SHIFT	8

#define	SROM4_AG10		47
#define	SROM4_AG32		48

#define	SROM4_TXPID2G		49
#define	SROM4_TXPID5G		51
#define	SROM4_TXPID5GL		53
#define	SROM4_TXPID5GH		55

#define SROM4_TXRXC		61
#define SROM4_TXCHAIN_MASK	0x000f
#define SROM4_TXCHAIN_SHIFT	0
#define SROM4_RXCHAIN_MASK	0x00f0
#define SROM4_RXCHAIN_SHIFT	4
#define SROM4_SWITCH_MASK	0xff00
#define SROM4_SWITCH_SHIFT	8

/* Per-path fields */
#define	MAX_PATH_SROM		4
#define	SROM4_PATH0		64
#define	SROM4_PATH1		87
#define	SROM4_PATH2		110
#define	SROM4_PATH3		133

#define	SROM4_2G_ITT_MAXP	0
#define	SROM4_2G_PA		1
#define	SROM4_5G_ITT_MAXP	5
#define	SROM4_5GLH_MAXP		6
#define	SROM4_5G_PA		7
#define	SROM4_5GL_PA		11
#define	SROM4_5GH_PA		15

/* Fields in the ITT_MAXP and 5GLH_MAXP words */
#define	B2G_MAXP_MASK		0xff
#define	B2G_ITT_SHIFT		8
#define	B5G_MAXP_MASK		0xff
#define	B5G_ITT_SHIFT		8
#define	B5GH_MAXP_MASK		0xff
#define	B5GL_MAXP_SHIFT		8

/* All the miriad power offsets */
#define	SROM4_2G_CCKPO		156
#define	SROM4_2G_OFDMPO		157
#define	SROM4_5G_OFDMPO		159
#define	SROM4_5GL_OFDMPO	161
#define	SROM4_5GH_OFDMPO	163
#define	SROM4_2G_MCSPO		165
#define	SROM4_5G_MCSPO		173
#define	SROM4_5GL_MCSPO		181
#define	SROM4_5GH_MCSPO		189
#define	SROM4_CDDPO		197
#define	SROM4_STBCPO		198
#define	SROM4_BW40PO		199
#define	SROM4_BWDUPPO		200

#define	SROM4_CRCREV		219

/* SROM Rev 8: Make space for a 48word hardware header for PCIe rev >= 6.
 * This is acombined srom for both MIMO and SISO boards, usable in
 * the .130 4Kilobit OTP with hardware redundancy.
 */

#define	SROM8_SIGN		64

#define	SROM8_BREV		65

#define	SROM8_BFL0		66
#define	SROM8_BFL1		67
#define	SROM8_BFL2		68
#define	SROM8_BFL3		69

#define	SROM8_MACHI		70
#define	SROM8_MACMID		71
#define	SROM8_MACLO		72

#define	SROM8_CCODE		73
#define	SROM8_REGREV		74

#define	SROM8_LEDBH10		75
#define	SROM8_LEDBH32		76

#define	SROM8_LEDDC		77

#define	SROM8_AA		78

#define	SROM8_AG10		79
#define	SROM8_AG32		80

#define	SROM8_TXRXC		81

#define	SROM8_BXARSSI2G		82
#define	SROM8_BXARSSI5G		83
#define	SROM8_TRI52G		84
#define	SROM8_TRI5GHL		85
#define	SROM8_RXPO52G		86

#define SROM8_FEM2G		87
#define SROM8_FEM5G		88
#define SROM8_FEM_ANTSWLUT_MASK		0xf800
#define SROM8_FEM_ANTSWLUT_SHIFT	11
#define SROM8_FEM_TR_ISO_MASK		0x0700
#define SROM8_FEM_TR_ISO_SHIFT		8
#define SROM8_FEM_PDET_RANGE_MASK	0x00f8
#define SROM8_FEM_PDET_RANGE_SHIFT	3
#define SROM8_FEM_EXTPA_GAIN_MASK	0x0006
#define SROM8_FEM_EXTPA_GAIN_SHIFT	1
#define SROM8_FEM_TSSIPOS_MASK		0x0001
#define SROM8_FEM_TSSIPOS_SHIFT		0

#define SROM8_THERMAL		89

/* Temp sense related entries */
#define SROM8_MPWR_RAWTS		90
#define SROM8_TS_SLP_OPT_CORRX	91
/* FOC: freiquency offset correction, HWIQ: H/W IOCAL enable, IQSWP: IQ CAL swap disable */
#define SROM8_FOC_HWIQ_IQSWP	92

/* Temperature delta for PHY calibration */
#define SROM8_PHYCAL_TEMPDELTA	93

/* Per-path offsets & fields */
#define	SROM8_PATH0		96
#define	SROM8_PATH1		112
#define	SROM8_PATH2		128
#define	SROM8_PATH3		144

#define	SROM8_2G_ITT_MAXP	0
#define	SROM8_2G_PA		1
#define	SROM8_5G_ITT_MAXP	4
#define	SROM8_5GLH_MAXP		5
#define	SROM8_5G_PA		6
#define	SROM8_5GL_PA		9
#define	SROM8_5GH_PA		12

/* All the miriad power offsets */
#define	SROM8_2G_CCKPO		160

#define	SROM8_2G_OFDMPO		161
#define	SROM8_5G_OFDMPO		163
#define	SROM8_5GL_OFDMPO	165
#define	SROM8_5GH_OFDMPO	167

#define	SROM8_2G_MCSPO		169
#define	SROM8_5G_MCSPO		177
#define	SROM8_5GL_MCSPO		185
#define	SROM8_5GH_MCSPO		193

#define	SROM8_CDDPO		201
#define	SROM8_STBCPO		202
#define	SROM8_BW40PO		203
#define	SROM8_BWDUPPO		204

/* SISO PA parameters are in the path0 spaces */
#define	SROM8_SISO		96

/* Legacy names for SISO PA paramters */
#define	SROM8_W0_ITTMAXP	(SROM8_SISO + SROM8_2G_ITT_MAXP)
#define	SROM8_W0_PAB0		(SROM8_SISO + SROM8_2G_PA)
#define	SROM8_W0_PAB1		(SROM8_SISO + SROM8_2G_PA + 1)
#define	SROM8_W0_PAB2		(SROM8_SISO + SROM8_2G_PA + 2)
#define	SROM8_W1_ITTMAXP	(SROM8_SISO + SROM8_5G_ITT_MAXP)
#define	SROM8_W1_MAXP_LCHC	(SROM8_SISO + SROM8_5GLH_MAXP)
#define	SROM8_W1_PAB0		(SROM8_SISO + SROM8_5G_PA)
#define	SROM8_W1_PAB1		(SROM8_SISO + SROM8_5G_PA + 1)
#define	SROM8_W1_PAB2		(SROM8_SISO + SROM8_5G_PA + 2)
#define	SROM8_W1_PAB0_LC	(SROM8_SISO + SROM8_5GL_PA)
#define	SROM8_W1_PAB1_LC	(SROM8_SISO + SROM8_5GL_PA + 1)
#define	SROM8_W1_PAB2_LC	(SROM8_SISO + SROM8_5GL_PA + 2)
#define	SROM8_W1_PAB0_HC	(SROM8_SISO + SROM8_5GH_PA)
#define	SROM8_W1_PAB1_HC	(SROM8_SISO + SROM8_5GH_PA + 1)
#define	SROM8_W1_PAB2_HC	(SROM8_SISO + SROM8_5GH_PA + 2)

#define	SROM8_CRCREV		219

/* SROM REV 9 */
#define SROM9_2GPO_CCKBW20	160
#define SROM9_2GPO_CCKBW20UL	161
#define SROM9_2GPO_LOFDMBW20	162
#define SROM9_2GPO_LOFDMBW20UL	164

#define SROM9_5GLPO_LOFDMBW20	166
#define SROM9_5GLPO_LOFDMBW20UL	168
#define SROM9_5GMPO_LOFDMBW20	170
#define SROM9_5GMPO_LOFDMBW20UL	172
#define SROM9_5GHPO_LOFDMBW20	174
#define SROM9_5GHPO_LOFDMBW20UL	176

#define SROM9_2GPO_MCSBW20	178
#define SROM9_2GPO_MCSBW20UL	180
#define SROM9_2GPO_MCSBW40	182

#define SROM9_5GLPO_MCSBW20	184
#define SROM9_5GLPO_MCSBW20UL	186
#define SROM9_5GLPO_MCSBW40	188
#define SROM9_5GMPO_MCSBW20	190
#define SROM9_5GMPO_MCSBW20UL	192
#define SROM9_5GMPO_MCSBW40	194
#define SROM9_5GHPO_MCSBW20	196
#define SROM9_5GHPO_MCSBW20UL	198
#define SROM9_5GHPO_MCSBW40	200

#define SROM9_PO_MCS32		202
#define SROM9_PO_LOFDM40DUP	203

#define SROM9_REV_CRC		219

typedef struct {
	u8 tssipos;		/* TSSI positive slope, 1: positive, 0: negative */
	u8 extpagain;	/* Ext PA gain-type: full-gain: 0, pa-lite: 1, no_pa: 2 */
	u8 pdetrange;	/* support 32 combinations of different Pdet dynamic ranges */
	u8 triso;		/* TR switch isolation */
	u8 antswctrllut;	/* antswctrl lookup table configuration: 32 possible choices */
} srom_fem_t;

#endif				/* _bcmsrom_fmt_h_ */

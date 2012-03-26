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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/etherdevice.h>
#include <linux/crc8.h>
#include <stdarg.h>

#include <chipcommon.h>
#include <brcmu_utils.h>
#include "pub.h"
#include "nicpci.h"
#include "aiutils.h"
#include "otp.h"
#include "srom.h"
#include "soc.h"

/*
 * SROM CRC8 polynomial value:
 *
 * x^8 + x^7 +x^6 + x^4 + x^2 + 1
 */
#define SROM_CRC8_POLY		0xAB

/* Maximum srom: 6 Kilobits == 768 bytes */
#define	SROM_MAX		768

/* PCI fields */
#define PCI_F0DEVID		48

#define	SROM_WORDS		64

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
#define	SROM_MACHI_ET1		42
#define	SROM_MACMID_ET1		43
#define	SROM_MACLO_ET1		44

#define	SROM_BXARSSI2G		40
#define	SROM_BXARSSI5G		41

#define	SROM_TRI52G		42
#define	SROM_TRI5GHL		43

#define	SROM_RXPO52G		45

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

#define	SROM_AG10		58

#define	SROM_CCODE		59

#define	SROM_OPO		60

#define	SROM_CRCREV		63

#define	SROM4_WORDS		220

#define SROM4_TXCHAIN_MASK	0x000f
#define SROM4_RXCHAIN_MASK	0x00f0
#define SROM4_SWITCH_MASK	0xff00

/* Per-path fields */
#define	MAX_PATH_SROM		4

#define	SROM4_CRCREV		219

/* SROM Rev 8: Make space for a 48word hardware header for PCIe rev >= 6.
 * This is acombined srom for both MIMO and SISO boards, usable in
 * the .130 4Kilobit OTP with hardware redundancy.
 */
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
/* FOC: freiquency offset correction, HWIQ: H/W IOCAL enable,
 * IQSWP: IQ CAL swap disable */
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

/* SROM flags (see sromvar_t) */

/* value continues as described by the next entry */
#define SRFL_MORE	1
#define	SRFL_NOFFS	2	/* value bits can't be all one's */
#define	SRFL_PRHEX	4	/* value is in hexdecimal format */
#define	SRFL_PRSIGN	8	/* value is in signed decimal format */
#define	SRFL_CCODE	0x10	/* value is in country code format */
#define	SRFL_ETHADDR	0x20	/* value is an Ethernet address */
#define SRFL_LEDDC	0x40	/* value is an LED duty cycle */
/* do not generate a nvram param, entry is for mfgc */
#define SRFL_NOVAR	0x80

/* Max. nvram variable table size */
#define	MAXSZ_NVRAM_VARS	4096

/*
 * indicates type of value.
 */
enum brcms_srom_var_type {
	BRCMS_SROM_STRING,
	BRCMS_SROM_SNUMBER,
	BRCMS_SROM_UNUMBER
};

/*
 * storage type for srom variable.
 *
 * var_list: for linked list operations.
 * varid: identifier of the variable.
 * var_type: type of variable.
 * buf: variable value when var_type == BRCMS_SROM_STRING.
 * uval: unsigned variable value when var_type == BRCMS_SROM_UNUMBER.
 * sval: signed variable value when var_type == BRCMS_SROM_SNUMBER.
 */
struct brcms_srom_list_head {
	struct list_head var_list;
	enum brcms_srom_id varid;
	enum brcms_srom_var_type var_type;
	union {
		char buf[0];
		u32 uval;
		s32 sval;
	};
};

struct brcms_sromvar {
	enum brcms_srom_id varid;
	u32 revmask;
	u32 flags;
	u16 off;
	u16 mask;
};

struct brcms_varbuf {
	char *base;		/* pointer to buffer base */
	char *buf;		/* pointer to current position */
	unsigned int size;	/* current (residual) size in bytes */
};

/*
 * Assumptions:
 * - Ethernet address spans across 3 consecutive words
 *
 * Table rules:
 * - Add multiple entries next to each other if a value spans across multiple
 *   words (even multiple fields in the same word) with each entry except the
 *   last having it's SRFL_MORE bit set.
 * - Ethernet address entry does not follow above rule and must not have
 *   SRFL_MORE bit set. Its SRFL_ETHADDR bit implies it takes multiple words.
 * - The last entry's name field must be NULL to indicate the end of the table.
 *   Other entries must have non-NULL name.
 */
static const struct brcms_sromvar pci_sromvars[] = {
	{BRCMS_SROM_DEVID, 0xffffff00, SRFL_PRHEX | SRFL_NOVAR, PCI_F0DEVID,
	 0xffff},
	{BRCMS_SROM_BOARDREV, 0xffffff00, SRFL_PRHEX, SROM8_BREV, 0xffff},
	{BRCMS_SROM_BOARDFLAGS, 0xffffff00, SRFL_PRHEX | SRFL_MORE, SROM8_BFL0,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_BFL1, 0xffff},
	{BRCMS_SROM_BOARDFLAGS2, 0xffffff00, SRFL_PRHEX | SRFL_MORE, SROM8_BFL2,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_BFL3, 0xffff},
	{BRCMS_SROM_BOARDTYPE, 0xfffffffc, SRFL_PRHEX, SROM_SSID, 0xffff},
	{BRCMS_SROM_BOARDNUM, 0xffffff00, 0, SROM8_MACLO, 0xffff},
	{BRCMS_SROM_REGREV, 0xffffff00, 0, SROM8_REGREV, 0x00ff},
	{BRCMS_SROM_LEDBH0, 0xffffff00, SRFL_NOFFS, SROM8_LEDBH10, 0x00ff},
	{BRCMS_SROM_LEDBH1, 0xffffff00, SRFL_NOFFS, SROM8_LEDBH10, 0xff00},
	{BRCMS_SROM_LEDBH2, 0xffffff00, SRFL_NOFFS, SROM8_LEDBH32, 0x00ff},
	{BRCMS_SROM_LEDBH3, 0xffffff00, SRFL_NOFFS, SROM8_LEDBH32, 0xff00},
	{BRCMS_SROM_PA0B0, 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB0, 0xffff},
	{BRCMS_SROM_PA0B1, 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB1, 0xffff},
	{BRCMS_SROM_PA0B2, 0xffffff00, SRFL_PRHEX, SROM8_W0_PAB2, 0xffff},
	{BRCMS_SROM_PA0ITSSIT, 0xffffff00, 0, SROM8_W0_ITTMAXP, 0xff00},
	{BRCMS_SROM_PA0MAXPWR, 0xffffff00, 0, SROM8_W0_ITTMAXP, 0x00ff},
	{BRCMS_SROM_OPO, 0xffffff00, 0, SROM8_2G_OFDMPO, 0x00ff},
	{BRCMS_SROM_AA2G, 0xffffff00, 0, SROM8_AA, 0x00ff},
	{BRCMS_SROM_AA5G, 0xffffff00, 0, SROM8_AA, 0xff00},
	{BRCMS_SROM_AG0, 0xffffff00, 0, SROM8_AG10, 0x00ff},
	{BRCMS_SROM_AG1, 0xffffff00, 0, SROM8_AG10, 0xff00},
	{BRCMS_SROM_AG2, 0xffffff00, 0, SROM8_AG32, 0x00ff},
	{BRCMS_SROM_AG3, 0xffffff00, 0, SROM8_AG32, 0xff00},
	{BRCMS_SROM_PA1B0, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0, 0xffff},
	{BRCMS_SROM_PA1B1, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1, 0xffff},
	{BRCMS_SROM_PA1B2, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2, 0xffff},
	{BRCMS_SROM_PA1LOB0, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0_LC, 0xffff},
	{BRCMS_SROM_PA1LOB1, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1_LC, 0xffff},
	{BRCMS_SROM_PA1LOB2, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2_LC, 0xffff},
	{BRCMS_SROM_PA1HIB0, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB0_HC, 0xffff},
	{BRCMS_SROM_PA1HIB1, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB1_HC, 0xffff},
	{BRCMS_SROM_PA1HIB2, 0xffffff00, SRFL_PRHEX, SROM8_W1_PAB2_HC, 0xffff},
	{BRCMS_SROM_PA1ITSSIT, 0xffffff00, 0, SROM8_W1_ITTMAXP, 0xff00},
	{BRCMS_SROM_PA1MAXPWR, 0xffffff00, 0, SROM8_W1_ITTMAXP, 0x00ff},
	{BRCMS_SROM_PA1LOMAXPWR, 0xffffff00, 0, SROM8_W1_MAXP_LCHC, 0xff00},
	{BRCMS_SROM_PA1HIMAXPWR, 0xffffff00, 0, SROM8_W1_MAXP_LCHC, 0x00ff},
	{BRCMS_SROM_BXA2G, 0xffffff00, 0, SROM8_BXARSSI2G, 0x1800},
	{BRCMS_SROM_RSSISAV2G, 0xffffff00, 0, SROM8_BXARSSI2G, 0x0700},
	{BRCMS_SROM_RSSISMC2G, 0xffffff00, 0, SROM8_BXARSSI2G, 0x00f0},
	{BRCMS_SROM_RSSISMF2G, 0xffffff00, 0, SROM8_BXARSSI2G, 0x000f},
	{BRCMS_SROM_BXA5G, 0xffffff00, 0, SROM8_BXARSSI5G, 0x1800},
	{BRCMS_SROM_RSSISAV5G, 0xffffff00, 0, SROM8_BXARSSI5G, 0x0700},
	{BRCMS_SROM_RSSISMC5G, 0xffffff00, 0, SROM8_BXARSSI5G, 0x00f0},
	{BRCMS_SROM_RSSISMF5G, 0xffffff00, 0, SROM8_BXARSSI5G, 0x000f},
	{BRCMS_SROM_TRI2G, 0xffffff00, 0, SROM8_TRI52G, 0x00ff},
	{BRCMS_SROM_TRI5G, 0xffffff00, 0, SROM8_TRI52G, 0xff00},
	{BRCMS_SROM_TRI5GL, 0xffffff00, 0, SROM8_TRI5GHL, 0x00ff},
	{BRCMS_SROM_TRI5GH, 0xffffff00, 0, SROM8_TRI5GHL, 0xff00},
	{BRCMS_SROM_RXPO2G, 0xffffff00, SRFL_PRSIGN, SROM8_RXPO52G, 0x00ff},
	{BRCMS_SROM_RXPO5G, 0xffffff00, SRFL_PRSIGN, SROM8_RXPO52G, 0xff00},
	{BRCMS_SROM_TXCHAIN, 0xffffff00, SRFL_NOFFS, SROM8_TXRXC,
	 SROM4_TXCHAIN_MASK},
	{BRCMS_SROM_RXCHAIN, 0xffffff00, SRFL_NOFFS, SROM8_TXRXC,
	 SROM4_RXCHAIN_MASK},
	{BRCMS_SROM_ANTSWITCH, 0xffffff00, SRFL_NOFFS, SROM8_TXRXC,
	 SROM4_SWITCH_MASK},
	{BRCMS_SROM_TSSIPOS2G, 0xffffff00, 0, SROM8_FEM2G,
	 SROM8_FEM_TSSIPOS_MASK},
	{BRCMS_SROM_EXTPAGAIN2G, 0xffffff00, 0, SROM8_FEM2G,
	 SROM8_FEM_EXTPA_GAIN_MASK},
	{BRCMS_SROM_PDETRANGE2G, 0xffffff00, 0, SROM8_FEM2G,
	 SROM8_FEM_PDET_RANGE_MASK},
	{BRCMS_SROM_TRISO2G, 0xffffff00, 0, SROM8_FEM2G, SROM8_FEM_TR_ISO_MASK},
	{BRCMS_SROM_ANTSWCTL2G, 0xffffff00, 0, SROM8_FEM2G,
	 SROM8_FEM_ANTSWLUT_MASK},
	{BRCMS_SROM_TSSIPOS5G, 0xffffff00, 0, SROM8_FEM5G,
	 SROM8_FEM_TSSIPOS_MASK},
	{BRCMS_SROM_EXTPAGAIN5G, 0xffffff00, 0, SROM8_FEM5G,
	 SROM8_FEM_EXTPA_GAIN_MASK},
	{BRCMS_SROM_PDETRANGE5G, 0xffffff00, 0, SROM8_FEM5G,
	 SROM8_FEM_PDET_RANGE_MASK},
	{BRCMS_SROM_TRISO5G, 0xffffff00, 0, SROM8_FEM5G, SROM8_FEM_TR_ISO_MASK},
	{BRCMS_SROM_ANTSWCTL5G, 0xffffff00, 0, SROM8_FEM5G,
	 SROM8_FEM_ANTSWLUT_MASK},
	{BRCMS_SROM_TEMPTHRESH, 0xffffff00, 0, SROM8_THERMAL, 0xff00},
	{BRCMS_SROM_TEMPOFFSET, 0xffffff00, 0, SROM8_THERMAL, 0x00ff},

	{BRCMS_SROM_CCODE, 0xffffff00, SRFL_CCODE, SROM8_CCODE, 0xffff},
	{BRCMS_SROM_MACADDR, 0xffffff00, SRFL_ETHADDR, SROM8_MACHI, 0xffff},
	{BRCMS_SROM_LEDDC, 0xffffff00, SRFL_NOFFS | SRFL_LEDDC, SROM8_LEDDC,
	 0xffff},
	{BRCMS_SROM_RAWTEMPSENSE, 0xffffff00, SRFL_PRHEX, SROM8_MPWR_RAWTS,
	 0x01ff},
	{BRCMS_SROM_MEASPOWER, 0xffffff00, SRFL_PRHEX, SROM8_MPWR_RAWTS,
	 0xfe00},
	{BRCMS_SROM_TEMPSENSE_SLOPE, 0xffffff00, SRFL_PRHEX,
	 SROM8_TS_SLP_OPT_CORRX, 0x00ff},
	{BRCMS_SROM_TEMPCORRX, 0xffffff00, SRFL_PRHEX, SROM8_TS_SLP_OPT_CORRX,
	 0xfc00},
	{BRCMS_SROM_TEMPSENSE_OPTION, 0xffffff00, SRFL_PRHEX,
	 SROM8_TS_SLP_OPT_CORRX, 0x0300},
	{BRCMS_SROM_FREQOFFSET_CORR, 0xffffff00, SRFL_PRHEX,
	 SROM8_FOC_HWIQ_IQSWP, 0x000f},
	{BRCMS_SROM_IQCAL_SWP_DIS, 0xffffff00, SRFL_PRHEX, SROM8_FOC_HWIQ_IQSWP,
	 0x0010},
	{BRCMS_SROM_HW_IQCAL_EN, 0xffffff00, SRFL_PRHEX, SROM8_FOC_HWIQ_IQSWP,
	 0x0020},
	{BRCMS_SROM_PHYCAL_TEMPDELTA, 0xffffff00, 0, SROM8_PHYCAL_TEMPDELTA,
	 0x00ff},

	{BRCMS_SROM_CCK2GPO, 0x00000100, 0, SROM8_2G_CCKPO, 0xffff},
	{BRCMS_SROM_OFDM2GPO, 0x00000100, SRFL_MORE, SROM8_2G_OFDMPO, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_2G_OFDMPO + 1, 0xffff},
	{BRCMS_SROM_OFDM5GPO, 0x00000100, SRFL_MORE, SROM8_5G_OFDMPO, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_5G_OFDMPO + 1, 0xffff},
	{BRCMS_SROM_OFDM5GLPO, 0x00000100, SRFL_MORE, SROM8_5GL_OFDMPO, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_5GL_OFDMPO + 1, 0xffff},
	{BRCMS_SROM_OFDM5GHPO, 0x00000100, SRFL_MORE, SROM8_5GH_OFDMPO, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM8_5GH_OFDMPO + 1, 0xffff},
	{BRCMS_SROM_MCS2GPO0, 0x00000100, 0, SROM8_2G_MCSPO, 0xffff},
	{BRCMS_SROM_MCS2GPO1, 0x00000100, 0, SROM8_2G_MCSPO + 1, 0xffff},
	{BRCMS_SROM_MCS2GPO2, 0x00000100, 0, SROM8_2G_MCSPO + 2, 0xffff},
	{BRCMS_SROM_MCS2GPO3, 0x00000100, 0, SROM8_2G_MCSPO + 3, 0xffff},
	{BRCMS_SROM_MCS2GPO4, 0x00000100, 0, SROM8_2G_MCSPO + 4, 0xffff},
	{BRCMS_SROM_MCS2GPO5, 0x00000100, 0, SROM8_2G_MCSPO + 5, 0xffff},
	{BRCMS_SROM_MCS2GPO6, 0x00000100, 0, SROM8_2G_MCSPO + 6, 0xffff},
	{BRCMS_SROM_MCS2GPO7, 0x00000100, 0, SROM8_2G_MCSPO + 7, 0xffff},
	{BRCMS_SROM_MCS5GPO0, 0x00000100, 0, SROM8_5G_MCSPO, 0xffff},
	{BRCMS_SROM_MCS5GPO1, 0x00000100, 0, SROM8_5G_MCSPO + 1, 0xffff},
	{BRCMS_SROM_MCS5GPO2, 0x00000100, 0, SROM8_5G_MCSPO + 2, 0xffff},
	{BRCMS_SROM_MCS5GPO3, 0x00000100, 0, SROM8_5G_MCSPO + 3, 0xffff},
	{BRCMS_SROM_MCS5GPO4, 0x00000100, 0, SROM8_5G_MCSPO + 4, 0xffff},
	{BRCMS_SROM_MCS5GPO5, 0x00000100, 0, SROM8_5G_MCSPO + 5, 0xffff},
	{BRCMS_SROM_MCS5GPO6, 0x00000100, 0, SROM8_5G_MCSPO + 6, 0xffff},
	{BRCMS_SROM_MCS5GPO7, 0x00000100, 0, SROM8_5G_MCSPO + 7, 0xffff},
	{BRCMS_SROM_MCS5GLPO0, 0x00000100, 0, SROM8_5GL_MCSPO, 0xffff},
	{BRCMS_SROM_MCS5GLPO1, 0x00000100, 0, SROM8_5GL_MCSPO + 1, 0xffff},
	{BRCMS_SROM_MCS5GLPO2, 0x00000100, 0, SROM8_5GL_MCSPO + 2, 0xffff},
	{BRCMS_SROM_MCS5GLPO3, 0x00000100, 0, SROM8_5GL_MCSPO + 3, 0xffff},
	{BRCMS_SROM_MCS5GLPO4, 0x00000100, 0, SROM8_5GL_MCSPO + 4, 0xffff},
	{BRCMS_SROM_MCS5GLPO5, 0x00000100, 0, SROM8_5GL_MCSPO + 5, 0xffff},
	{BRCMS_SROM_MCS5GLPO6, 0x00000100, 0, SROM8_5GL_MCSPO + 6, 0xffff},
	{BRCMS_SROM_MCS5GLPO7, 0x00000100, 0, SROM8_5GL_MCSPO + 7, 0xffff},
	{BRCMS_SROM_MCS5GHPO0, 0x00000100, 0, SROM8_5GH_MCSPO, 0xffff},
	{BRCMS_SROM_MCS5GHPO1, 0x00000100, 0, SROM8_5GH_MCSPO + 1, 0xffff},
	{BRCMS_SROM_MCS5GHPO2, 0x00000100, 0, SROM8_5GH_MCSPO + 2, 0xffff},
	{BRCMS_SROM_MCS5GHPO3, 0x00000100, 0, SROM8_5GH_MCSPO + 3, 0xffff},
	{BRCMS_SROM_MCS5GHPO4, 0x00000100, 0, SROM8_5GH_MCSPO + 4, 0xffff},
	{BRCMS_SROM_MCS5GHPO5, 0x00000100, 0, SROM8_5GH_MCSPO + 5, 0xffff},
	{BRCMS_SROM_MCS5GHPO6, 0x00000100, 0, SROM8_5GH_MCSPO + 6, 0xffff},
	{BRCMS_SROM_MCS5GHPO7, 0x00000100, 0, SROM8_5GH_MCSPO + 7, 0xffff},
	{BRCMS_SROM_CDDPO, 0x00000100, 0, SROM8_CDDPO, 0xffff},
	{BRCMS_SROM_STBCPO, 0x00000100, 0, SROM8_STBCPO, 0xffff},
	{BRCMS_SROM_BW40PO, 0x00000100, 0, SROM8_BW40PO, 0xffff},
	{BRCMS_SROM_BWDUPPO, 0x00000100, 0, SROM8_BWDUPPO, 0xffff},

	/* power per rate from sromrev 9 */
	{BRCMS_SROM_CCKBW202GPO, 0xfffffe00, 0, SROM9_2GPO_CCKBW20, 0xffff},
	{BRCMS_SROM_CCKBW20UL2GPO, 0xfffffe00, 0, SROM9_2GPO_CCKBW20UL, 0xffff},
	{BRCMS_SROM_LEGOFDMBW202GPO, 0xfffffe00, SRFL_MORE,
	 SROM9_2GPO_LOFDMBW20, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_2GPO_LOFDMBW20 + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW20UL2GPO, 0xfffffe00, SRFL_MORE,
	 SROM9_2GPO_LOFDMBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_2GPO_LOFDMBW20UL + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW205GLPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GLPO_LOFDMBW20, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GLPO_LOFDMBW20 + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW20UL5GLPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GLPO_LOFDMBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GLPO_LOFDMBW20UL + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW205GMPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GMPO_LOFDMBW20, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GMPO_LOFDMBW20 + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW20UL5GMPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GMPO_LOFDMBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GMPO_LOFDMBW20UL + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW205GHPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GHPO_LOFDMBW20, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GHPO_LOFDMBW20 + 1, 0xffff},
	{BRCMS_SROM_LEGOFDMBW20UL5GHPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GHPO_LOFDMBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GHPO_LOFDMBW20UL + 1, 0xffff},
	{BRCMS_SROM_MCSBW202GPO, 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW20,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_2GPO_MCSBW20 + 1, 0xffff},
	{BRCMS_SROM_MCSBW20UL2GPO, 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW20UL,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_2GPO_MCSBW20UL + 1, 0xffff},
	{BRCMS_SROM_MCSBW402GPO, 0xfffffe00, SRFL_MORE, SROM9_2GPO_MCSBW40,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_2GPO_MCSBW40 + 1, 0xffff},
	{BRCMS_SROM_MCSBW205GLPO, 0xfffffe00, SRFL_MORE, SROM9_5GLPO_MCSBW20,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GLPO_MCSBW20 + 1, 0xffff},
	{BRCMS_SROM_MCSBW20UL5GLPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GLPO_MCSBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GLPO_MCSBW20UL + 1, 0xffff},
	{BRCMS_SROM_MCSBW405GLPO, 0xfffffe00, SRFL_MORE, SROM9_5GLPO_MCSBW40,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GLPO_MCSBW40 + 1, 0xffff},
	{BRCMS_SROM_MCSBW205GMPO, 0xfffffe00, SRFL_MORE, SROM9_5GMPO_MCSBW20,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GMPO_MCSBW20 + 1, 0xffff},
	{BRCMS_SROM_MCSBW20UL5GMPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GMPO_MCSBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GMPO_MCSBW20UL + 1, 0xffff},
	{BRCMS_SROM_MCSBW405GMPO, 0xfffffe00, SRFL_MORE, SROM9_5GMPO_MCSBW40,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GMPO_MCSBW40 + 1, 0xffff},
	{BRCMS_SROM_MCSBW205GHPO, 0xfffffe00, SRFL_MORE, SROM9_5GHPO_MCSBW20,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GHPO_MCSBW20 + 1, 0xffff},
	{BRCMS_SROM_MCSBW20UL5GHPO, 0xfffffe00, SRFL_MORE,
	 SROM9_5GHPO_MCSBW20UL, 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GHPO_MCSBW20UL + 1, 0xffff},
	{BRCMS_SROM_MCSBW405GHPO, 0xfffffe00, SRFL_MORE, SROM9_5GHPO_MCSBW40,
	 0xffff},
	{BRCMS_SROM_CONT, 0, 0, SROM9_5GHPO_MCSBW40 + 1, 0xffff},
	{BRCMS_SROM_MCS32PO, 0xfffffe00, 0, SROM9_PO_MCS32, 0xffff},
	{BRCMS_SROM_LEGOFDM40DUPPO, 0xfffffe00, 0, SROM9_PO_LOFDM40DUP, 0xffff},

	{BRCMS_SROM_NULL, 0, 0, 0, 0}
};

static const struct brcms_sromvar perpath_pci_sromvars[] = {
	{BRCMS_SROM_MAXP2GA0, 0xffffff00, 0, SROM8_2G_ITT_MAXP, 0x00ff},
	{BRCMS_SROM_ITT2GA0, 0xffffff00, 0, SROM8_2G_ITT_MAXP, 0xff00},
	{BRCMS_SROM_ITT5GA0, 0xffffff00, 0, SROM8_5G_ITT_MAXP, 0xff00},
	{BRCMS_SROM_PA2GW0A0, 0xffffff00, SRFL_PRHEX, SROM8_2G_PA, 0xffff},
	{BRCMS_SROM_PA2GW1A0, 0xffffff00, SRFL_PRHEX, SROM8_2G_PA + 1, 0xffff},
	{BRCMS_SROM_PA2GW2A0, 0xffffff00, SRFL_PRHEX, SROM8_2G_PA + 2, 0xffff},
	{BRCMS_SROM_MAXP5GA0, 0xffffff00, 0, SROM8_5G_ITT_MAXP, 0x00ff},
	{BRCMS_SROM_MAXP5GHA0, 0xffffff00, 0, SROM8_5GLH_MAXP, 0x00ff},
	{BRCMS_SROM_MAXP5GLA0, 0xffffff00, 0, SROM8_5GLH_MAXP, 0xff00},
	{BRCMS_SROM_PA5GW0A0, 0xffffff00, SRFL_PRHEX, SROM8_5G_PA, 0xffff},
	{BRCMS_SROM_PA5GW1A0, 0xffffff00, SRFL_PRHEX, SROM8_5G_PA + 1, 0xffff},
	{BRCMS_SROM_PA5GW2A0, 0xffffff00, SRFL_PRHEX, SROM8_5G_PA + 2, 0xffff},
	{BRCMS_SROM_PA5GLW0A0, 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA, 0xffff},
	{BRCMS_SROM_PA5GLW1A0, 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA + 1,
	 0xffff},
	{BRCMS_SROM_PA5GLW2A0, 0xffffff00, SRFL_PRHEX, SROM8_5GL_PA + 2,
	 0xffff},
	{BRCMS_SROM_PA5GHW0A0, 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA, 0xffff},
	{BRCMS_SROM_PA5GHW1A0, 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA + 1,
	 0xffff},
	{BRCMS_SROM_PA5GHW2A0, 0xffffff00, SRFL_PRHEX, SROM8_5GH_PA + 2,
	 0xffff},
	{BRCMS_SROM_NULL, 0, 0, 0, 0}
};

/* crc table has the same contents for every device instance, so it can be
 * shared between devices. */
static u8 brcms_srom_crc8_table[CRC8_TABLE_SIZE];

static uint mask_shift(u16 mask)
{
	uint i;
	for (i = 0; i < (sizeof(mask) << 3); i++) {
		if (mask & (1 << i))
			return i;
	}
	return 0;
}

static uint mask_width(u16 mask)
{
	int i;
	for (i = (sizeof(mask) << 3) - 1; i >= 0; i--) {
		if (mask & (1 << i))
			return (uint) (i - mask_shift(mask) + 1);
	}
	return 0;
}

static inline void le16_to_cpu_buf(u16 *buf, uint nwords)
{
	while (nwords--)
		*(buf + nwords) = le16_to_cpu(*(__le16 *)(buf + nwords));
}

static inline void cpu_to_le16_buf(u16 *buf, uint nwords)
{
	while (nwords--)
		*(__le16 *)(buf + nwords) = cpu_to_le16(*(buf + nwords));
}

/*
 * convert binary srom data into linked list of srom variable items.
 */
static int
_initvars_srom_pci(u8 sromrev, u16 *srom, struct list_head *var_list)
{
	struct brcms_srom_list_head *entry;
	enum brcms_srom_id id;
	u16 w;
	u32 val = 0;
	const struct brcms_sromvar *srv;
	uint width;
	uint flags;
	u32 sr = (1 << sromrev);
	uint p;
	uint pb =  SROM8_PATH0;
	const uint psz = SROM8_PATH1 - SROM8_PATH0;

	/* first store the srom revision */
	entry = kzalloc(sizeof(struct brcms_srom_list_head), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->varid = BRCMS_SROM_REV;
	entry->var_type = BRCMS_SROM_UNUMBER;
	entry->uval = sromrev;
	list_add(&entry->var_list, var_list);

	for (srv = pci_sromvars; srv->varid != BRCMS_SROM_NULL; srv++) {
		enum brcms_srom_var_type type;
		u8 ea[ETH_ALEN];
		u8 extra_space = 0;

		if ((srv->revmask & sr) == 0)
			continue;

		flags = srv->flags;
		id = srv->varid;

		/* This entry is for mfgc only. Don't generate param for it, */
		if (flags & SRFL_NOVAR)
			continue;

		if (flags & SRFL_ETHADDR) {
			/*
			 * stored in string format XX:XX:XX:XX:XX:XX (17 chars)
			 */
			ea[0] = (srom[srv->off] >> 8) & 0xff;
			ea[1] = srom[srv->off] & 0xff;
			ea[2] = (srom[srv->off + 1] >> 8) & 0xff;
			ea[3] = srom[srv->off + 1] & 0xff;
			ea[4] = (srom[srv->off + 2] >> 8) & 0xff;
			ea[5] = srom[srv->off + 2] & 0xff;
			/* 17 characters + string terminator - union size */
			extra_space = 18 - sizeof(s32);
			type = BRCMS_SROM_STRING;
		} else {
			w = srom[srv->off];
			val = (w & srv->mask) >> mask_shift(srv->mask);
			width = mask_width(srv->mask);

			while (srv->flags & SRFL_MORE) {
				srv++;
				if (srv->off == 0)
					continue;

				w = srom[srv->off];
				val +=
				    ((w & srv->mask) >> mask_shift(srv->
								   mask)) <<
				    width;
				width += mask_width(srv->mask);
			}

			if ((flags & SRFL_NOFFS)
			    && ((int)val == (1 << width) - 1))
				continue;

			if (flags & SRFL_CCODE) {
				type = BRCMS_SROM_STRING;
			} else if (flags & SRFL_LEDDC) {
				/* LED Powersave duty cycle has to be scaled:
				 *(oncount >> 24) (offcount >> 8)
				 */
				u32 w32 = /* oncount */
					  (((val >> 8) & 0xff) << 24) |
					  /* offcount */
					  (((val & 0xff)) << 8);
				type = BRCMS_SROM_UNUMBER;
				val = w32;
			} else if ((flags & SRFL_PRSIGN)
				 && (val & (1 << (width - 1)))) {
				type = BRCMS_SROM_SNUMBER;
				val |= ~0 << width;
			} else
				type = BRCMS_SROM_UNUMBER;
		}

		entry = kzalloc(sizeof(struct brcms_srom_list_head) +
				extra_space, GFP_KERNEL);
		if (!entry)
			return -ENOMEM;
		entry->varid = id;
		entry->var_type = type;
		if (flags & SRFL_ETHADDR) {
			snprintf(entry->buf, 18, "%pM", ea);
		} else if (flags & SRFL_CCODE) {
			if (val == 0)
				entry->buf[0] = '\0';
			else
				snprintf(entry->buf, 3, "%c%c",
					 (val >> 8), (val & 0xff));
		} else {
			entry->uval = val;
		}

		list_add(&entry->var_list, var_list);
	}

	for (p = 0; p < MAX_PATH_SROM; p++) {
		for (srv = perpath_pci_sromvars;
		     srv->varid != BRCMS_SROM_NULL; srv++) {
			if ((srv->revmask & sr) == 0)
				continue;

			if (srv->flags & SRFL_NOVAR)
				continue;

			w = srom[pb + srv->off];
			val = (w & srv->mask) >> mask_shift(srv->mask);
			width = mask_width(srv->mask);

			/* Cheating: no per-path var is more than
			 * 1 word */
			if ((srv->flags & SRFL_NOFFS)
			    && ((int)val == (1 << width) - 1))
				continue;

			entry =
			    kzalloc(sizeof(struct brcms_srom_list_head),
				    GFP_KERNEL);
			if (!entry)
				return -ENOMEM;
			entry->varid = srv->varid+p;
			entry->var_type = BRCMS_SROM_UNUMBER;
			entry->uval = val;
			list_add(&entry->var_list, var_list);
		}
		pb += psz;
	}
	return 0;
}

/*
 * The crc check is done on a little-endian array, we need
 * to switch the bytes around before checking crc (and
 * then switch it back).
 */
static int do_crc_check(u16 *buf, unsigned nwords)
{
	u8 crc;

	cpu_to_le16_buf(buf, nwords);
	crc = crc8(brcms_srom_crc8_table, (void *)buf, nwords << 1, CRC8_INIT_VALUE);
	le16_to_cpu_buf(buf, nwords);

	return crc == CRC8_GOOD_VALUE(brcms_srom_crc8_table);
}

/*
 * Read in and validate sprom.
 * Return 0 on success, nonzero on error.
 */
static int
sprom_read_pci(struct si_pub *sih, u16 *buf, uint nwords, bool check_crc)
{
	int err = 0;
	uint i;
	struct bcma_device *core;
	uint sprom_offset;

	/* determine core to read */
	if (ai_get_ccrev(sih) < 32) {
		core = ai_findcore(sih, BCMA_CORE_80211, 0);
		sprom_offset = PCI_BAR0_SPROM_OFFSET;
	} else {
		core = ai_findcore(sih, BCMA_CORE_CHIPCOMMON, 0);
		sprom_offset = CHIPCREGOFFS(sromotp);
	}

	/* read the sprom */
	for (i = 0; i < nwords; i++)
		buf[i] = bcma_read16(core, sprom_offset+i*2);

	if (buf[0] == 0xffff)
		/*
		 * The hardware thinks that an srom that starts with
		 * 0xffff is blank, regardless of the rest of the
		 * content, so declare it bad.
		 */
		return -ENODATA;

	if (check_crc && !do_crc_check(buf, nwords))
		err = -EIO;

	return err;
}

static int otp_read_pci(struct si_pub *sih, u16 *buf, uint nwords)
{
	u8 *otp;
	uint sz = OTP_SZ_MAX / 2;	/* size in words */
	int err = 0;

	otp = kzalloc(OTP_SZ_MAX, GFP_ATOMIC);
	if (otp == NULL)
		return -ENOMEM;

	err = otp_read_region(sih, OTP_HW_RGN, (u16 *) otp, &sz);

	sz = min_t(uint, sz, nwords);
	memcpy(buf, otp, sz * 2);

	kfree(otp);

	/* Check CRC */
	if (buf[0] == 0xffff)
		/* The hardware thinks that an srom that starts with 0xffff
		 * is blank, regardless of the rest of the content, so declare
		 * it bad.
		 */
		return -ENODATA;

	/* fixup the endianness so crc8 will pass */
	cpu_to_le16_buf(buf, sz);
	if (crc8(brcms_srom_crc8_table, (u8 *) buf, sz * 2,
		 CRC8_INIT_VALUE) != CRC8_GOOD_VALUE(brcms_srom_crc8_table))
		err = -EIO;
	else
		/* now correct the endianness of the byte array */
		le16_to_cpu_buf(buf, sz);

	return err;
}

/*
 * Initialize nonvolatile variable table from sprom.
 * Return 0 on success, nonzero on error.
 */
int srom_var_init(struct si_pub *sih)
{
	u16 *srom;
	u8 sromrev = 0;
	u32 sr;
	int err = 0;

	/*
	 * Apply CRC over SROM content regardless SROM is present or not.
	 */
	srom = kmalloc(SROM_MAX, GFP_ATOMIC);
	if (!srom)
		return -ENOMEM;

	crc8_populate_lsb(brcms_srom_crc8_table, SROM_CRC8_POLY);
	if (ai_is_sprom_available(sih)) {
		err = sprom_read_pci(sih, srom, SROM4_WORDS, true);

		if (err == 0)
			/* srom read and passed crc */
			/* top word of sprom contains version and crc8 */
			sromrev = srom[SROM4_CRCREV] & 0xff;
	} else {
		/* Use OTP if SPROM not available */
		err = otp_read_pci(sih, srom, SROM4_WORDS);
		if (err == 0)
			/* OTP only contain SROM rev8/rev9 for now */
			sromrev = srom[SROM4_CRCREV] & 0xff;
	}

	if (!err) {
		struct si_info *sii = (struct si_info *)sih;

		/* Bitmask for the sromrev */
		sr = 1 << sromrev;

		/*
		 * srom version check: Current valid versions: 8, 9
		 */
		if ((sr & 0x300) == 0) {
			err = -EINVAL;
			goto errout;
		}

		INIT_LIST_HEAD(&sii->var_list);

		/* parse SROM into name=value pairs. */
		err = _initvars_srom_pci(sromrev, srom, &sii->var_list);
		if (err)
			srom_free_vars(sih);
	}

errout:
	kfree(srom);
	return err;
}

void srom_free_vars(struct si_pub *sih)
{
	struct si_info *sii;
	struct brcms_srom_list_head *entry, *next;

	sii = (struct si_info *)sih;
	list_for_each_entry_safe(entry, next, &sii->var_list, var_list) {
		list_del(&entry->var_list);
		kfree(entry);
	}
}

/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *getvar(struct si_pub *sih, enum brcms_srom_id id)
{
	struct si_info *sii;
	struct brcms_srom_list_head *entry;

	sii = (struct si_info *)sih;

	list_for_each_entry(entry, &sii->var_list, var_list)
		if (entry->varid == id)
			return &entry->buf[0];

	/* nothing found */
	return NULL;
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.-
 */
int getintvar(struct si_pub *sih, enum brcms_srom_id id)
{
	struct si_info *sii;
	struct brcms_srom_list_head *entry;
	unsigned long res;

	sii = (struct si_info *)sih;

	list_for_each_entry(entry, &sii->var_list, var_list)
		if (entry->varid == id) {
			if (entry->var_type == BRCMS_SROM_SNUMBER ||
			    entry->var_type == BRCMS_SROM_UNUMBER)
				return (int)entry->sval;
			else if (!kstrtoul(&entry->buf[0], 0, &res))
				return (int)res;
		}

	return 0;
}

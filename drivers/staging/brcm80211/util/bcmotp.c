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

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linuxver.h>
#include <bcmdevs.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmendian.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <bcmotp.h>
#include "siutils_priv.h"

/*
 * There are two different OTP controllers so far:
 * 	1. new IPX OTP controller:	chipc 21, >=23
 * 	2. older HND OTP controller:	chipc 12, 17, 22
 *
 * Define BCMHNDOTP to include support for the HND OTP controller.
 * Define BCMIPXOTP to include support for the IPX OTP controller.
 *
 * NOTE 1: More than one may be defined
 * NOTE 2: If none are defined, the default is to include them all.
 */

#if !defined(BCMHNDOTP) && !defined(BCMIPXOTP)
#define BCMHNDOTP	1
#define BCMIPXOTP	1
#endif

#define OTPTYPE_HND(ccrev)	((ccrev) < 21 || (ccrev) == 22)
#define OTPTYPE_IPX(ccrev)	((ccrev) == 21 || (ccrev) >= 23)

#define OTPP_TRIES	10000000	/* # of tries for OTPP */

#ifdef BCMIPXOTP
#define MAXNUMRDES		9	/* Maximum OTP redundancy entries */
#endif

/* OTP common function type */
typedef int (*otp_status_t) (void *oh);
typedef int (*otp_size_t) (void *oh);
typedef void *(*otp_init_t) (si_t *sih);
typedef uint16(*otp_read_bit_t) (void *oh, chipcregs_t *cc, uint off);
typedef int (*otp_read_region_t) (si_t *sih, int region, uint16 *data,
				  uint *wlen);
typedef int (*otp_nvread_t) (void *oh, char *data, uint *len);

/* OTP function struct */
typedef struct otp_fn_s {
	otp_size_t size;
	otp_read_bit_t read_bit;
	otp_init_t init;
	otp_read_region_t read_region;
	otp_nvread_t nvread;
	otp_status_t status;
} otp_fn_t;

typedef struct {
	uint ccrev;		/* chipc revision */
	otp_fn_t *fn;		/* OTP functions */
	si_t *sih;		/* Saved sb handle */
	osl_t *osh;

#ifdef BCMIPXOTP
	/* IPX OTP section */
	uint16 wsize;		/* Size of otp in words */
	uint16 rows;		/* Geometry */
	uint16 cols;		/* Geometry */
	uint32 status;		/* Flag bits (lock/prog/rv).
				 * (Reflected only when OTP is power cycled)
				 */
	uint16 hwbase;		/* hardware subregion offset */
	uint16 hwlim;		/* hardware subregion boundary */
	uint16 swbase;		/* software subregion offset */
	uint16 swlim;		/* software subregion boundary */
	uint16 fbase;		/* fuse subregion offset */
	uint16 flim;		/* fuse subregion boundary */
	int otpgu_base;		/* offset to General Use Region */
#endif				/* BCMIPXOTP */

#ifdef BCMHNDOTP
	/* HND OTP section */
	uint size;		/* Size of otp in bytes */
	uint hwprot;		/* Hardware protection bits */
	uint signvalid;		/* Signature valid bits */
	int boundary;		/* hw/sw boundary */
#endif				/* BCMHNDOTP */
} otpinfo_t;

static otpinfo_t otpinfo;

/*
 * IPX OTP Code
 *
 *   Exported functions:
 *	ipxotp_status()
 *	ipxotp_size()
 *	ipxotp_init()
 *	ipxotp_read_bit()
 *	ipxotp_read_region()
 *	ipxotp_nvread()
 *
 */

#ifdef BCMIPXOTP

#define HWSW_RGN(rgn)		(((rgn) == OTP_HW_RGN) ? "h/w" : "s/w")

/* OTP layout */
/* CC revs 21, 24 and 27 OTP General Use Region word offset */
#define REVA4_OTPGU_BASE	12

/* CC revs 23, 25, 26, 28 and above OTP General Use Region word offset */
#define REVB8_OTPGU_BASE	20

/* CC rev 36 OTP General Use Region word offset */
#define REV36_OTPGU_BASE	12

/* Subregion word offsets in General Use region */
#define OTPGU_HSB_OFF		0
#define OTPGU_SFB_OFF		1
#define OTPGU_CI_OFF		2
#define OTPGU_P_OFF		3
#define OTPGU_SROM_OFF		4

/* Flag bit offsets in General Use region  */
#define OTPGU_HWP_OFF		60
#define OTPGU_SWP_OFF		61
#define OTPGU_CIP_OFF		62
#define OTPGU_FUSEP_OFF		63
#define OTPGU_CIP_MSK		0x4000
#define OTPGU_P_MSK		0xf000
#define OTPGU_P_SHIFT		(OTPGU_HWP_OFF % 16)

/* OTP Size */
#define OTP_SZ_FU_324		((ROUNDUP(324, 8))/8)	/* 324 bits */
#define OTP_SZ_FU_288		(288/8)	/* 288 bits */
#define OTP_SZ_FU_216		(216/8)	/* 216 bits */
#define OTP_SZ_FU_72		(72/8)	/* 72 bits */
#define OTP_SZ_CHECKSUM		(16/8)	/* 16 bits */
#define OTP4315_SWREG_SZ	178	/* 178 bytes */
#define OTP_SZ_FU_144		(144/8)	/* 144 bits */

static int ipxotp_status(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	return (int)(oi->status);
}

/* Return size in bytes */
static int ipxotp_size(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	return (int)oi->wsize * 2;
}

static uint16 ipxotp_otpr(void *oh, chipcregs_t *cc, uint wn)
{
	otpinfo_t *oi;

	oi = (otpinfo_t *) oh;

	ASSERT(wn < oi->wsize);
	ASSERT(cc != NULL);

	return R_REG(oi->osh, &cc->sromotp[wn]);
}

static uint16 ipxotp_read_bit(void *oh, chipcregs_t *cc, uint off)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint k, row, col;
	uint32 otpp, st;

	row = off / oi->cols;
	col = off % oi->cols;

	otpp = OTPP_START_BUSY |
	    ((OTPPOC_READ << OTPP_OC_SHIFT) & OTPP_OC_MASK) |
	    ((row << OTPP_ROW_SHIFT) & OTPP_ROW_MASK) |
	    ((col << OTPP_COL_SHIFT) & OTPP_COL_MASK);
	W_REG(oi->osh, &cc->otpprog, otpp);

	for (k = 0;
	     ((st = R_REG(oi->osh, &cc->otpprog)) & OTPP_START_BUSY)
	     && (k < OTPP_TRIES); k++)
		;
	if (k >= OTPP_TRIES) {
		return 0xffff;
	}
	if (st & OTPP_READERR) {
		return 0xffff;
	}
	st = (st & OTPP_VALUE_MASK) >> OTPP_VALUE_SHIFT;

	return (int)st;
}

/* Calculate max HW/SW region byte size by substracting fuse region and checksum size,
 * osizew is oi->wsize (OTP size - GU size) in words
 */
static int ipxotp_max_rgnsz(si_t *sih, int osizew)
{
	int ret = 0;

	switch (CHIPID(sih->chip)) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
		ret = osizew * 2 - OTP_SZ_FU_72 - OTP_SZ_CHECKSUM;
		break;
	case BCM4313_CHIP_ID:
		ret = osizew * 2 - OTP_SZ_FU_72 - OTP_SZ_CHECKSUM;
		break;
	default:
		ASSERT(0);	/* Don't konw about this chip */
	}

	return ret;
}

static void BCMNMIATTACHFN(_ipxotp_init) (otpinfo_t *oi, chipcregs_t *cc)
{
	uint k;
	uint32 otpp, st;

	/* record word offset of General Use Region for various chipcommon revs */
	if (oi->sih->ccrev == 21 || oi->sih->ccrev == 24
	    || oi->sih->ccrev == 27) {
		oi->otpgu_base = REVA4_OTPGU_BASE;
	} else if (oi->sih->ccrev == 36) {
		/* OTP size greater than equal to 2KB (128 words), otpgu_base is similar to rev23 */
		if (oi->wsize >= 128)
			oi->otpgu_base = REVB8_OTPGU_BASE;
		else
			oi->otpgu_base = REV36_OTPGU_BASE;
	} else if (oi->sih->ccrev == 23 || oi->sih->ccrev >= 25) {
		oi->otpgu_base = REVB8_OTPGU_BASE;
	}

	/* First issue an init command so the status is up to date */
	otpp =
	    OTPP_START_BUSY | ((OTPPOC_INIT << OTPP_OC_SHIFT) & OTPP_OC_MASK);

	W_REG(oi->osh, &cc->otpprog, otpp);
	for (k = 0;
	     ((st = R_REG(oi->osh, &cc->otpprog)) & OTPP_START_BUSY)
	     && (k < OTPP_TRIES); k++)
		;
	if (k >= OTPP_TRIES) {
		return;
	}

	/* Read OTP lock bits and subregion programmed indication bits */
	oi->status = R_REG(oi->osh, &cc->otpstatus);

	if ((CHIPID(oi->sih->chip) == BCM43224_CHIP_ID)
	    || (CHIPID(oi->sih->chip) == BCM43225_CHIP_ID)) {
		uint32 p_bits;
		p_bits =
		    (ipxotp_otpr(oi, cc, oi->otpgu_base + OTPGU_P_OFF) &
		     OTPGU_P_MSK)
		    >> OTPGU_P_SHIFT;
		oi->status |= (p_bits << OTPS_GUP_SHIFT);
	}

	/*
	 * h/w region base and fuse region limit are fixed to the top and
	 * the bottom of the general use region. Everything else can be flexible.
	 */
	oi->hwbase = oi->otpgu_base + OTPGU_SROM_OFF;
	oi->hwlim = oi->wsize;
	if (oi->status & OTPS_GUP_HW) {
		oi->hwlim =
		    ipxotp_otpr(oi, cc, oi->otpgu_base + OTPGU_HSB_OFF) / 16;
		oi->swbase = oi->hwlim;
	} else
		oi->swbase = oi->hwbase;

	/* subtract fuse and checksum from beginning */
	oi->swlim = ipxotp_max_rgnsz(oi->sih, oi->wsize) / 2;

	if (oi->status & OTPS_GUP_SW) {
		oi->swlim =
		    ipxotp_otpr(oi, cc, oi->otpgu_base + OTPGU_SFB_OFF) / 16;
		oi->fbase = oi->swlim;
	} else
		oi->fbase = oi->swbase;

	oi->flim = oi->wsize;
}

static void *BCMNMIATTACHFN(ipxotp_init) (si_t *sih)
{
	uint idx;
	chipcregs_t *cc;
	otpinfo_t *oi;

	/* Make sure we're running IPX OTP */
	ASSERT(OTPTYPE_IPX(sih->ccrev));
	if (!OTPTYPE_IPX(sih->ccrev))
		return NULL;

	/* Make sure OTP is not disabled */
	if (si_is_otp_disabled(sih)) {
		return NULL;
	}

	/* Make sure OTP is powered up */
	if (!si_is_otp_powered(sih)) {
		return NULL;
	}

	oi = &otpinfo;

	/* Check for otp size */
	switch ((sih->cccaps & CC_CAP_OTPSIZE) >> CC_CAP_OTPSIZE_SHIFT) {
	case 0:
		/* Nothing there */
		return NULL;
	case 1:		/* 32x64 */
		oi->rows = 32;
		oi->cols = 64;
		oi->wsize = 128;
		break;
	case 2:		/* 64x64 */
		oi->rows = 64;
		oi->cols = 64;
		oi->wsize = 256;
		break;
	case 5:		/* 96x64 */
		oi->rows = 96;
		oi->cols = 64;
		oi->wsize = 384;
		break;
	case 7:		/* 16x64 *//* 1024 bits */
		oi->rows = 16;
		oi->cols = 64;
		oi->wsize = 64;
		break;
	default:
		/* Don't know the geometry */
		return NULL;
	}

	/* Retrieve OTP region info */
	idx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	_ipxotp_init(oi, cc);

	si_setcoreidx(sih, idx);

	return (void *)oi;
}

static int ipxotp_read_region(void *oh, int region, uint16 *data, uint *wlen)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint idx;
	chipcregs_t *cc;
	uint base, i, sz;

	/* Validate region selection */
	switch (region) {
	case OTP_HW_RGN:
		sz = (uint) oi->hwlim - oi->hwbase;
		if (!(oi->status & OTPS_GUP_HW)) {
			*wlen = sz;
			return BCME_NOTFOUND;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return BCME_BUFTOOSHORT;
		}
		base = oi->hwbase;
		break;
	case OTP_SW_RGN:
		sz = ((uint) oi->swlim - oi->swbase);
		if (!(oi->status & OTPS_GUP_SW)) {
			*wlen = sz;
			return BCME_NOTFOUND;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return BCME_BUFTOOSHORT;
		}
		base = oi->swbase;
		break;
	case OTP_CI_RGN:
		sz = OTPGU_CI_SZ;
		if (!(oi->status & OTPS_GUP_CI)) {
			*wlen = sz;
			return BCME_NOTFOUND;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return BCME_BUFTOOSHORT;
		}
		base = oi->otpgu_base + OTPGU_CI_OFF;
		break;
	case OTP_FUSE_RGN:
		sz = (uint) oi->flim - oi->fbase;
		if (!(oi->status & OTPS_GUP_FUSE)) {
			*wlen = sz;
			return BCME_NOTFOUND;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return BCME_BUFTOOSHORT;
		}
		base = oi->fbase;
		break;
	case OTP_ALL_RGN:
		sz = ((uint) oi->flim - oi->hwbase);
		if (!(oi->status & (OTPS_GUP_HW | OTPS_GUP_SW))) {
			*wlen = sz;
			return BCME_NOTFOUND;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return BCME_BUFTOOSHORT;
		}
		base = oi->hwbase;
		break;
	default:
		return BCME_BADARG;
	}

	idx = si_coreidx(oi->sih);
	cc = si_setcoreidx(oi->sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	/* Read the data */
	for (i = 0; i < sz; i++)
		data[i] = ipxotp_otpr(oh, cc, base + i);

	si_setcoreidx(oi->sih, idx);
	*wlen = sz;
	return 0;
}

static int ipxotp_nvread(void *oh, char *data, uint *len)
{
	return BCME_UNSUPPORTED;
}

static otp_fn_t ipxotp_fn = {
	(otp_size_t) ipxotp_size,
	(otp_read_bit_t) ipxotp_read_bit,

	(otp_init_t) ipxotp_init,
	(otp_read_region_t) ipxotp_read_region,
	(otp_nvread_t) ipxotp_nvread,

	(otp_status_t) ipxotp_status
};

#endif				/* BCMIPXOTP */

/*
 * HND OTP Code
 *
 *   Exported functions:
 *	hndotp_status()
 *	hndotp_size()
 *	hndotp_init()
 *	hndotp_read_bit()
 *	hndotp_read_region()
 *	hndotp_nvread()
 *
 */

#ifdef BCMHNDOTP

/* Fields in otpstatus */
#define	OTPS_PROGFAIL		0x80000000
#define	OTPS_PROTECT		0x00000007
#define	OTPS_HW_PROTECT		0x00000001
#define	OTPS_SW_PROTECT		0x00000002
#define	OTPS_CID_PROTECT	0x00000004
#define	OTPS_RCEV_MSK		0x00003f00
#define	OTPS_RCEV_SHIFT		8

/* Fields in the otpcontrol register */
#define	OTPC_RECWAIT		0xff000000
#define	OTPC_PROGWAIT		0x00ffff00
#define	OTPC_PRW_SHIFT		8
#define	OTPC_MAXFAIL		0x00000038
#define	OTPC_VSEL		0x00000006
#define	OTPC_SELVL		0x00000001

/* OTP regions (Word offsets from otp size) */
#define	OTP_SWLIM_OFF	(-4)
#define	OTP_CIDBASE_OFF	0
#define	OTP_CIDLIM_OFF	4

/* Predefined OTP words (Word offset from otp size) */
#define	OTP_BOUNDARY_OFF (-4)
#define	OTP_HWSIGN_OFF	(-3)
#define	OTP_SWSIGN_OFF	(-2)
#define	OTP_CIDSIGN_OFF	(-1)
#define	OTP_CID_OFF	0
#define	OTP_PKG_OFF	1
#define	OTP_FID_OFF	2
#define	OTP_RSV_OFF	3
#define	OTP_LIM_OFF	4
#define	OTP_RD_OFF	4	/* Redundancy row starts here */
#define	OTP_RC0_OFF	28	/* Redundancy control word 1 */
#define	OTP_RC1_OFF	32	/* Redundancy control word 2 */
#define	OTP_RC_LIM_OFF	36	/* Redundancy control word end */

#define	OTP_HW_REGION	OTPS_HW_PROTECT
#define	OTP_SW_REGION	OTPS_SW_PROTECT
#define	OTP_CID_REGION	OTPS_CID_PROTECT

#if OTP_HW_REGION != OTP_HW_RGN
#error "incompatible OTP_HW_RGN"
#endif
#if OTP_SW_REGION != OTP_SW_RGN
#error "incompatible OTP_SW_RGN"
#endif
#if OTP_CID_REGION != OTP_CI_RGN
#error "incompatible OTP_CI_RGN"
#endif

/* Redundancy entry definitions */
#define	OTP_RCE_ROW_SZ		6
#define	OTP_RCE_SIGN_MASK	0x7fff
#define	OTP_RCE_ROW_MASK	0x3f
#define	OTP_RCE_BITS		21
#define	OTP_RCE_SIGN_SZ		15
#define	OTP_RCE_BIT0		1

#define	OTP_WPR		4
#define	OTP_SIGNATURE	0x578a
#define	OTP_MAGIC	0x4e56

static int hndotp_status(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	return (int)(oi->hwprot | oi->signvalid);
}

static int hndotp_size(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	return (int)(oi->size);
}

static uint16 hndotp_otpr(void *oh, chipcregs_t *cc, uint wn)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	osl_t *osh;
	volatile uint16 *ptr;

	ASSERT(wn < ((oi->size / 2) + OTP_RC_LIM_OFF));
	ASSERT(cc != NULL);

	osh = si_osh(oi->sih);

	ptr = (volatile uint16 *)((volatile char *)cc + CC_SROM_OTP);
	return R_REG(osh, &ptr[wn]);
}

static uint16 hndotp_otproff(void *oh, chipcregs_t *cc, int woff)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	osl_t *osh;
	volatile uint16 *ptr;

	ASSERT(woff >= (-((int)oi->size / 2)));
	ASSERT(woff < OTP_LIM_OFF);
	ASSERT(cc != NULL);

	osh = si_osh(oi->sih);

	ptr = (volatile uint16 *)((volatile char *)cc + CC_SROM_OTP);

	return R_REG(osh, &ptr[(oi->size / 2) + woff]);
}

static uint16 hndotp_read_bit(void *oh, chipcregs_t *cc, uint idx)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint k, row, col;
	uint32 otpp, st;
	osl_t *osh;

	osh = si_osh(oi->sih);
	row = idx / 65;
	col = idx % 65;

	otpp = OTPP_START_BUSY | OTPP_READ |
	    ((row << OTPP_ROW_SHIFT) & OTPP_ROW_MASK) | (col & OTPP_COL_MASK);

	W_REG(osh, &cc->otpprog, otpp);
	st = R_REG(osh, &cc->otpprog);
	for (k = 0;
	     ((st & OTPP_START_BUSY) == OTPP_START_BUSY) && (k < OTPP_TRIES);
	     k++)
		st = R_REG(osh, &cc->otpprog);

	if (k >= OTPP_TRIES) {
		return 0xffff;
	}
	if (st & OTPP_READERR) {
		return 0xffff;
	}
	st = (st & OTPP_VALUE_MASK) >> OTPP_VALUE_SHIFT;
	return (uint16) st;
}

static void *BCMNMIATTACHFN(hndotp_init) (si_t *sih)
{
	uint idx;
	chipcregs_t *cc;
	otpinfo_t *oi;
	uint32 cap = 0, clkdiv, otpdiv = 0;
	void *ret = NULL;
	osl_t *osh;

	oi = &otpinfo;

	idx = si_coreidx(sih);
	osh = si_osh(oi->sih);

	/* Check for otp */
	cc = si_setcoreidx(sih, SI_CC_IDX);
	if (cc != NULL) {
		cap = R_REG(osh, &cc->capabilities);
		if ((cap & CC_CAP_OTPSIZE) == 0) {
			/* Nothing there */
			goto out;
		}

		/* As of right now, support only 4320a2, 4311a1 and 4312 */
		ASSERT((oi->ccrev == 12) || (oi->ccrev == 17)
		       || (oi->ccrev == 22));
		if (!
		    ((oi->ccrev == 12) || (oi->ccrev == 17)
		     || (oi->ccrev == 22)))
			return NULL;

		/* Read the OTP byte size. chipcommon rev >= 18 has RCE so the size is
		 * 8 row (64 bytes) smaller
		 */
		oi->size =
		    1 << (((cap & CC_CAP_OTPSIZE) >> CC_CAP_OTPSIZE_SHIFT)
			  + CC_CAP_OTPSIZE_BASE);
		if (oi->ccrev >= 18)
			oi->size -= ((OTP_RC0_OFF - OTP_BOUNDARY_OFF) * 2);

		oi->hwprot = (int)(R_REG(osh, &cc->otpstatus) & OTPS_PROTECT);
		oi->boundary = -1;

		/* Check the region signature */
		if (hndotp_otproff(oi, cc, OTP_HWSIGN_OFF) == OTP_SIGNATURE) {
			oi->signvalid |= OTP_HW_REGION;
			oi->boundary = hndotp_otproff(oi, cc, OTP_BOUNDARY_OFF);
		}

		if (hndotp_otproff(oi, cc, OTP_SWSIGN_OFF) == OTP_SIGNATURE)
			oi->signvalid |= OTP_SW_REGION;

		if (hndotp_otproff(oi, cc, OTP_CIDSIGN_OFF) == OTP_SIGNATURE)
			oi->signvalid |= OTP_CID_REGION;

		/* Set OTP clkdiv for stability */
		if (oi->ccrev == 22)
			otpdiv = 12;

		if (otpdiv) {
			clkdiv = R_REG(osh, &cc->clkdiv);
			clkdiv =
			    (clkdiv & ~CLKD_OTP) | (otpdiv << CLKD_OTP_SHIFT);
			W_REG(osh, &cc->clkdiv, clkdiv);
		}
		OSL_DELAY(10);

		ret = (void *)oi;
	}

 out:				/* All done */
	si_setcoreidx(sih, idx);

	return ret;
}

static int hndotp_read_region(void *oh, int region, uint16 *data, uint *wlen)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint32 idx, st;
	chipcregs_t *cc;
	int i;

	/* Only support HW region (no active chips use HND OTP SW region) */
	ASSERT(region == OTP_HW_REGION);

	/* Region empty? */
	st = oi->hwprot | oi->signvalid;
	if ((st & region) == 0)
		return BCME_NOTFOUND;

	*wlen =
	    ((int)*wlen < oi->boundary / 2) ? *wlen : (uint) oi->boundary / 2;

	idx = si_coreidx(oi->sih);
	cc = si_setcoreidx(oi->sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	for (i = 0; i < (int)*wlen; i++)
		data[i] = hndotp_otpr(oh, cc, i);

	si_setcoreidx(oi->sih, idx);

	return 0;
}

static int hndotp_nvread(void *oh, char *data, uint *len)
{
	int rc = 0;
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint32 base, bound, lim = 0, st;
	int i, chunk, gchunks, tsz = 0;
	uint32 idx;
	chipcregs_t *cc;
	uint offset;
	uint16 *rawotp = NULL;

	/* save the orig core */
	idx = si_coreidx(oi->sih);
	cc = si_setcoreidx(oi->sih, SI_CC_IDX);
	ASSERT(cc != NULL);

	st = hndotp_status(oh);
	if (!(st & (OTP_HW_REGION | OTP_SW_REGION))) {
		rc = -1;
		goto out;
	}

	/* Read the whole otp so we can easily manipulate it */
	lim = hndotp_size(oh);
	rawotp = MALLOC(si_osh(oi->sih), lim);
	if (rawotp == NULL) {
		rc = -2;
		goto out;
	}
	for (i = 0; i < (int)(lim / 2); i++)
		rawotp[i] = hndotp_otpr(oh, cc, i);

	if ((st & OTP_HW_REGION) == 0) {
		/* This could be a programming failure in the first
		 * chunk followed by one or more good chunks
		 */
		for (i = 0; i < (int)(lim / 2); i++)
			if (rawotp[i] == OTP_MAGIC)
				break;

		if (i < (int)(lim / 2)) {
			base = i;
			bound = (i * 2) + rawotp[i + 1];
		} else {
			rc = -3;
			goto out;
		}
	} else {
		bound = rawotp[(lim / 2) + OTP_BOUNDARY_OFF];

		/* There are two cases: 1) The whole otp is used as nvram
		 * and 2) There is a hardware header followed by nvram.
		 */
		if (rawotp[0] == OTP_MAGIC) {
			base = 0;
		} else
			base = bound;
	}

	/* Find and copy the data */

	chunk = 0;
	gchunks = 0;
	i = base / 2;
	offset = 0;
	while ((i < (int)(lim / 2)) && (rawotp[i] == OTP_MAGIC)) {
		int dsz, rsz = rawotp[i + 1];

		if (((i * 2) + rsz) >= (int)lim) {
			/* Bad length, try to find another chunk anyway */
			rsz = 6;
		}
		if (hndcrc16((uint8 *) &rawotp[i], rsz,
			     CRC16_INIT_VALUE) == CRC16_GOOD_VALUE) {
			/* Good crc, copy the vars */
			gchunks++;
			dsz = rsz - 6;
			tsz += dsz;
			if (offset + dsz >= *len) {
				goto out;
			}
			bcopy((char *)&rawotp[i + 2], &data[offset], dsz);
			offset += dsz;
			/* Remove extra null characters at the end */
			while (offset > 1 &&
			       data[offset - 1] == 0 && data[offset - 2] == 0)
				offset--;
			i += rsz / 2;
		} else {
			/* bad length or crc didn't check, try to find the next set */
			if (rawotp[i + (rsz / 2)] == OTP_MAGIC) {
				/* Assume length is good */
				i += rsz / 2;
			} else {
				while (++i < (int)(lim / 2))
					if (rawotp[i] == OTP_MAGIC)
						break;
			}
		}
		chunk++;
	}

	*len = offset;

 out:
	if (rawotp)
		MFREE(si_osh(oi->sih), rawotp, lim);
	si_setcoreidx(oi->sih, idx);

	return rc;
}

static otp_fn_t hndotp_fn = {
	(otp_size_t) hndotp_size,
	(otp_read_bit_t) hndotp_read_bit,

	(otp_init_t) hndotp_init,
	(otp_read_region_t) hndotp_read_region,
	(otp_nvread_t) hndotp_nvread,

	(otp_status_t) hndotp_status
};

#endif				/* BCMHNDOTP */

/*
 * Common Code: Compiled for IPX / HND / AUTO
 *	otp_status()
 *	otp_size()
 *	otp_read_bit()
 *	otp_init()
 * 	otp_read_region()
 * 	otp_nvread()
 */

int otp_status(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;

	return oi->fn->status(oh);
}

int otp_size(void *oh)
{
	otpinfo_t *oi = (otpinfo_t *) oh;

	return oi->fn->size(oh);
}

uint16 otp_read_bit(void *oh, uint offset)
{
	otpinfo_t *oi = (otpinfo_t *) oh;
	uint idx = si_coreidx(oi->sih);
	chipcregs_t *cc = si_setcoreidx(oi->sih, SI_CC_IDX);
	uint16 readBit = (uint16) oi->fn->read_bit(oh, cc, offset);
	si_setcoreidx(oi->sih, idx);
	return readBit;
}

void *BCMNMIATTACHFN(otp_init) (si_t *sih)
{
	otpinfo_t *oi;
	void *ret = NULL;

	oi = &otpinfo;
	bzero(oi, sizeof(otpinfo_t));

	oi->ccrev = sih->ccrev;

#ifdef BCMIPXOTP
	if (OTPTYPE_IPX(oi->ccrev))
		oi->fn = &ipxotp_fn;
#endif

#ifdef BCMHNDOTP
	if (OTPTYPE_HND(oi->ccrev))
		oi->fn = &hndotp_fn;
#endif

	if (oi->fn == NULL) {
		return NULL;
	}

	oi->sih = sih;
	oi->osh = si_osh(oi->sih);

	ret = (oi->fn->init) (sih);

	return ret;
}

int
BCMNMIATTACHFN(otp_read_region) (si_t *sih, int region, uint16 *data,
				 uint *wlen) {
	bool wasup = FALSE;
	void *oh;
	int err = 0;

	wasup = si_is_otp_powered(sih);
	if (!wasup)
		si_otp_power(sih, TRUE);

	if (!si_is_otp_powered(sih) || si_is_otp_disabled(sih)) {
		err = BCME_NOTREADY;
		goto out;
	}

	oh = otp_init(sih);
	if (oh == NULL) {
		err = BCME_ERROR;
		goto out;
	}

	err = (((otpinfo_t *) oh)->fn->read_region) (oh, region, data, wlen);

 out:
	if (!wasup)
		si_otp_power(sih, FALSE);

	return err;
}

int otp_nvread(void *oh, char *data, uint *len)
{
	otpinfo_t *oi = (otpinfo_t *) oh;

	return oi->fn->nvread(oh, data, len);
}

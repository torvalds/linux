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

#include <linux/io.h>
#include <linux/errno.h>

#include <brcm_hw_ids.h>
#include <chipcommon.h>
#include "aiutils.h"
#include "otp.h"

#define OTPS_GUP_MASK		0x00000f00
#define OTPS_GUP_SHIFT		8
#define OTPS_GUP_HW		0x00000100	/* h/w subregion is programmed */
#define OTPS_GUP_SW		0x00000200	/* s/w subregion is programmed */
#define OTPS_GUP_CI		0x00000400	/* chipid/pkgopt subregion is programmed */
#define OTPS_GUP_FUSE		0x00000800	/* fuse subregion is programmed */

/* Fields in otpprog in rev >= 21 */
#define OTPP_COL_MASK		0x000000ff
#define OTPP_COL_SHIFT		0
#define OTPP_ROW_MASK		0x0000ff00
#define OTPP_ROW_SHIFT		8
#define OTPP_OC_MASK		0x0f000000
#define OTPP_OC_SHIFT		24
#define OTPP_READERR		0x10000000
#define OTPP_VALUE_MASK		0x20000000
#define OTPP_VALUE_SHIFT	29
#define OTPP_START_BUSY		0x80000000
#define	OTPP_READ		0x40000000

/* Opcodes for OTPP_OC field */
#define OTPPOC_READ		0
#define OTPPOC_BIT_PROG		1
#define OTPPOC_VERIFY		3
#define OTPPOC_INIT		4
#define OTPPOC_SET		5
#define OTPPOC_RESET		6
#define OTPPOC_OCST		7
#define OTPPOC_ROW_LOCK		8
#define OTPPOC_PRESCN_TEST	9

#define OTPTYPE_IPX(ccrev)	((ccrev) == 21 || (ccrev) >= 23)

#define OTPP_TRIES	10000000	/* # of tries for OTPP */

#define MAXNUMRDES		9	/* Maximum OTP redundancy entries */

/* OTP common function type */
typedef int (*otp_status_t) (void *oh);
typedef int (*otp_size_t) (void *oh);
typedef void *(*otp_init_t) (struct si_pub *sih);
typedef u16(*otp_read_bit_t) (void *oh, chipcregs_t *cc, uint off);
typedef int (*otp_read_region_t) (struct si_pub *sih, int region, u16 *data,
				  uint *wlen);
typedef int (*otp_nvread_t) (void *oh, char *data, uint *len);

/* OTP function struct */
struct otp_fn_s {
	otp_size_t size;
	otp_read_bit_t read_bit;
	otp_init_t init;
	otp_read_region_t read_region;
	otp_nvread_t nvread;
	otp_status_t status;
};

struct otpinfo {
	uint ccrev;		/* chipc revision */
	struct otp_fn_s *fn;		/* OTP functions */
	struct si_pub *sih;		/* Saved sb handle */

	/* IPX OTP section */
	u16 wsize;		/* Size of otp in words */
	u16 rows;		/* Geometry */
	u16 cols;		/* Geometry */
	u32 status;		/* Flag bits (lock/prog/rv).
				 * (Reflected only when OTP is power cycled)
				 */
	u16 hwbase;		/* hardware subregion offset */
	u16 hwlim;		/* hardware subregion boundary */
	u16 swbase;		/* software subregion offset */
	u16 swlim;		/* software subregion boundary */
	u16 fbase;		/* fuse subregion offset */
	u16 flim;		/* fuse subregion boundary */
	int otpgu_base;		/* offset to General Use Region */
};

static struct otpinfo otpinfo;

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
#define OTP_SZ_FU_324		((roundup(324, 8))/8)	/* 324 bits */
#define OTP_SZ_FU_288		(288/8)	/* 288 bits */
#define OTP_SZ_FU_216		(216/8)	/* 216 bits */
#define OTP_SZ_FU_72		(72/8)	/* 72 bits */
#define OTP_SZ_CHECKSUM		(16/8)	/* 16 bits */
#define OTP4315_SWREG_SZ	178	/* 178 bytes */
#define OTP_SZ_FU_144		(144/8)	/* 144 bits */

static int ipxotp_status(void *oh)
{
	struct otpinfo *oi = (struct otpinfo *) oh;
	return (int)(oi->status);
}

/* Return size in bytes */
static int ipxotp_size(void *oh)
{
	struct otpinfo *oi = (struct otpinfo *) oh;
	return (int)oi->wsize * 2;
}

static u16 ipxotp_otpr(void *oh, chipcregs_t *cc, uint wn)
{
	struct otpinfo *oi;

	oi = (struct otpinfo *) oh;

	return R_REG(&cc->sromotp[wn]);
}

static u16 ipxotp_read_bit(void *oh, chipcregs_t *cc, uint off)
{
	struct otpinfo *oi = (struct otpinfo *) oh;
	uint k, row, col;
	u32 otpp, st;

	row = off / oi->cols;
	col = off % oi->cols;

	otpp = OTPP_START_BUSY |
	    ((OTPPOC_READ << OTPP_OC_SHIFT) & OTPP_OC_MASK) |
	    ((row << OTPP_ROW_SHIFT) & OTPP_ROW_MASK) |
	    ((col << OTPP_COL_SHIFT) & OTPP_COL_MASK);
	W_REG(&cc->otpprog, otpp);

	for (k = 0;
	     ((st = R_REG(&cc->otpprog)) & OTPP_START_BUSY)
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

/* Calculate max HW/SW region byte size by subtracting fuse region and checksum size,
 * osizew is oi->wsize (OTP size - GU size) in words
 */
static int ipxotp_max_rgnsz(struct si_pub *sih, int osizew)
{
	int ret = 0;

	switch (sih->chip) {
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
		ret = osizew * 2 - OTP_SZ_FU_72 - OTP_SZ_CHECKSUM;
		break;
	case BCM4313_CHIP_ID:
		ret = osizew * 2 - OTP_SZ_FU_72 - OTP_SZ_CHECKSUM;
		break;
	default:
		break;	/* Don't know about this chip */
	}

	return ret;
}

static void _ipxotp_init(struct otpinfo *oi, chipcregs_t *cc)
{
	uint k;
	u32 otpp, st;

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

	W_REG(&cc->otpprog, otpp);
	for (k = 0;
	     ((st = R_REG(&cc->otpprog)) & OTPP_START_BUSY)
	     && (k < OTPP_TRIES); k++)
		;
	if (k >= OTPP_TRIES) {
		return;
	}

	/* Read OTP lock bits and subregion programmed indication bits */
	oi->status = R_REG(&cc->otpstatus);

	if ((oi->sih->chip == BCM43224_CHIP_ID)
	    || (oi->sih->chip == BCM43225_CHIP_ID)) {
		u32 p_bits;
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

static void *ipxotp_init(struct si_pub *sih)
{
	uint idx;
	chipcregs_t *cc;
	struct otpinfo *oi;

	/* Make sure we're running IPX OTP */
	if (!OTPTYPE_IPX(sih->ccrev))
		return NULL;

	/* Make sure OTP is not disabled */
	if (ai_is_otp_disabled(sih))
		return NULL;

	/* OTP is always powered */
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
	idx = ai_coreidx(sih);
	cc = ai_setcoreidx(sih, SI_CC_IDX);

	_ipxotp_init(oi, cc);

	ai_setcoreidx(sih, idx);

	return (void *)oi;
}

static int ipxotp_read_region(void *oh, int region, u16 *data, uint *wlen)
{
	struct otpinfo *oi = (struct otpinfo *) oh;
	uint idx;
	chipcregs_t *cc;
	uint base, i, sz;

	/* Validate region selection */
	switch (region) {
	case OTP_HW_RGN:
		sz = (uint) oi->hwlim - oi->hwbase;
		if (!(oi->status & OTPS_GUP_HW)) {
			*wlen = sz;
			return -ENODATA;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return -EOVERFLOW;
		}
		base = oi->hwbase;
		break;
	case OTP_SW_RGN:
		sz = ((uint) oi->swlim - oi->swbase);
		if (!(oi->status & OTPS_GUP_SW)) {
			*wlen = sz;
			return -ENODATA;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return -EOVERFLOW;
		}
		base = oi->swbase;
		break;
	case OTP_CI_RGN:
		sz = OTPGU_CI_SZ;
		if (!(oi->status & OTPS_GUP_CI)) {
			*wlen = sz;
			return -ENODATA;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return -EOVERFLOW;
		}
		base = oi->otpgu_base + OTPGU_CI_OFF;
		break;
	case OTP_FUSE_RGN:
		sz = (uint) oi->flim - oi->fbase;
		if (!(oi->status & OTPS_GUP_FUSE)) {
			*wlen = sz;
			return -ENODATA;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return -EOVERFLOW;
		}
		base = oi->fbase;
		break;
	case OTP_ALL_RGN:
		sz = ((uint) oi->flim - oi->hwbase);
		if (!(oi->status & (OTPS_GUP_HW | OTPS_GUP_SW))) {
			*wlen = sz;
			return -ENODATA;
		}
		if (*wlen < sz) {
			*wlen = sz;
			return -EOVERFLOW;
		}
		base = oi->hwbase;
		break;
	default:
		return -EINVAL;
	}

	idx = ai_coreidx(oi->sih);
	cc = ai_setcoreidx(oi->sih, SI_CC_IDX);

	/* Read the data */
	for (i = 0; i < sz; i++)
		data[i] = ipxotp_otpr(oh, cc, base + i);

	ai_setcoreidx(oi->sih, idx);
	*wlen = sz;
	return 0;
}

static int ipxotp_nvread(void *oh, char *data, uint *len)
{
	return -ENOTSUPP;
}

static struct otp_fn_s ipxotp_fn = {
	(otp_size_t) ipxotp_size,
	(otp_read_bit_t) ipxotp_read_bit,

	(otp_init_t) ipxotp_init,
	(otp_read_region_t) ipxotp_read_region,
	(otp_nvread_t) ipxotp_nvread,

	(otp_status_t) ipxotp_status
};

/*
 *	otp_status()
 *	otp_size()
 *	otp_read_bit()
 *	otp_init()
 *	otp_read_region()
 *	otp_nvread()
 */

int otp_status(void *oh)
{
	struct otpinfo *oi = (struct otpinfo *) oh;

	return oi->fn->status(oh);
}

int otp_size(void *oh)
{
	struct otpinfo *oi = (struct otpinfo *) oh;

	return oi->fn->size(oh);
}

u16 otp_read_bit(void *oh, uint offset)
{
	struct otpinfo *oi = (struct otpinfo *) oh;
	uint idx = ai_coreidx(oi->sih);
	chipcregs_t *cc = ai_setcoreidx(oi->sih, SI_CC_IDX);
	u16 readBit = (u16) oi->fn->read_bit(oh, cc, offset);
	ai_setcoreidx(oi->sih, idx);
	return readBit;
}

void *otp_init(struct si_pub *sih)
{
	struct otpinfo *oi;
	void *ret = NULL;

	oi = &otpinfo;
	memset(oi, 0, sizeof(struct otpinfo));

	oi->ccrev = sih->ccrev;

	if (OTPTYPE_IPX(oi->ccrev))
		oi->fn = &ipxotp_fn;

	if (oi->fn == NULL) {
		return NULL;
	}

	oi->sih = sih;

	ret = (oi->fn->init) (sih);

	return ret;
}

int
otp_read_region(struct si_pub *sih, int region, u16 *data,
				 uint *wlen) {
	void *oh;
	int err = 0;

	if (ai_is_otp_disabled(sih)) {
		err = -EPERM;
		goto out;
	}

	oh = otp_init(sih);
	if (oh == NULL) {
		err = -EBADE;
		goto out;
	}

	err = (((struct otpinfo *) oh)->fn->read_region)
						(oh, region, data, wlen);

 out:
	return err;
}

int otp_nvread(void *oh, char *data, uint *len)
{
	struct otpinfo *oi = (struct otpinfo *) oh;

	return oi->fn->nvread(oh, data, len);
}

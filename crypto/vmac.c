/*
 * VMAC: Message Authentication Code using Universal Hashing
 *
 * Reference: https://tools.ietf.org/html/draft-krovetz-vmac-01
 *
 * Copyright (c) 2009, Intel Corporation.
 * Copyright (c) 2018, Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

/*
 * Derived from:
 *	VMAC and VHASH Implementation by Ted Krovetz (tdk@acm.org) and Wei Dai.
 *	This implementation is herby placed in the public domain.
 *	The authors offers no warranty. Use at your own risk.
 *	Last modified: 17 APR 08, 1700 PDT
 */

#include <asm/unaligned.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <asm/byteorder.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>

/*
 * User definable settings.
 */
#define VMAC_TAG_LEN	64
#define VMAC_KEY_SIZE	128/* Must be 128, 192 or 256			*/
#define VMAC_KEY_LEN	(VMAC_KEY_SIZE/8)
#define VMAC_NHBYTES	128/* Must 2^i for any 3 < i < 13 Standard = 128*/
#define VMAC_NONCEBYTES	16

/* per-transform (per-key) context */
struct vmac_tfm_ctx {
	struct crypto_cipher *cipher;
	u64 nhkey[(VMAC_NHBYTES/8)+2*(VMAC_TAG_LEN/64-1)];
	u64 polykey[2*VMAC_TAG_LEN/64];
	u64 l3key[2*VMAC_TAG_LEN/64];
};

/* per-request context */
struct vmac_desc_ctx {
	union {
		u8 partial[VMAC_NHBYTES];	/* partial block */
		__le64 partial_words[VMAC_NHBYTES / 8];
	};
	unsigned int partial_size;	/* size of the partial block */
	bool first_block_processed;
	u64 polytmp[2*VMAC_TAG_LEN/64];	/* running total of L2-hash */
	union {
		u8 bytes[VMAC_NONCEBYTES];
		__be64 pads[VMAC_NONCEBYTES / 8];
	} nonce;
	unsigned int nonce_size; /* nonce bytes filled so far */
};

/*
 * Constants and masks
 */
#define UINT64_C(x) x##ULL
static const u64 p64   = UINT64_C(0xfffffffffffffeff);	/* 2^64 - 257 prime  */
static const u64 m62   = UINT64_C(0x3fffffffffffffff);	/* 62-bit mask       */
static const u64 m63   = UINT64_C(0x7fffffffffffffff);	/* 63-bit mask       */
static const u64 m64   = UINT64_C(0xffffffffffffffff);	/* 64-bit mask       */
static const u64 mpoly = UINT64_C(0x1fffffff1fffffff);	/* Poly key mask     */

#define pe64_to_cpup le64_to_cpup		/* Prefer little endian */

#ifdef __LITTLE_ENDIAN
#define INDEX_HIGH 1
#define INDEX_LOW 0
#else
#define INDEX_HIGH 0
#define INDEX_LOW 1
#endif

/*
 * The following routines are used in this implementation. They are
 * written via macros to simulate zero-overhead call-by-reference.
 *
 * MUL64: 64x64->128-bit multiplication
 * PMUL64: assumes top bits cleared on inputs
 * ADD128: 128x128->128-bit addition
 */

#define ADD128(rh, rl, ih, il)						\
	do {								\
		u64 _il = (il);						\
		(rl) += (_il);						\
		if ((rl) < (_il))					\
			(rh)++;						\
		(rh) += (ih);						\
	} while (0)

#define MUL32(i1, i2)	((u64)(u32)(i1)*(u32)(i2))

#define PMUL64(rh, rl, i1, i2)	/* Assumes m doesn't overflow */	\
	do {								\
		u64 _i1 = (i1), _i2 = (i2);				\
		u64 m = MUL32(_i1, _i2>>32) + MUL32(_i1>>32, _i2);	\
		rh = MUL32(_i1>>32, _i2>>32);				\
		rl = MUL32(_i1, _i2);					\
		ADD128(rh, rl, (m >> 32), (m << 32));			\
	} while (0)

#define MUL64(rh, rl, i1, i2)						\
	do {								\
		u64 _i1 = (i1), _i2 = (i2);				\
		u64 m1 = MUL32(_i1, _i2>>32);				\
		u64 m2 = MUL32(_i1>>32, _i2);				\
		rh = MUL32(_i1>>32, _i2>>32);				\
		rl = MUL32(_i1, _i2);					\
		ADD128(rh, rl, (m1 >> 32), (m1 << 32));			\
		ADD128(rh, rl, (m2 >> 32), (m2 << 32));			\
	} while (0)

/*
 * For highest performance the L1 NH and L2 polynomial hashes should be
 * carefully implemented to take advantage of one's target architecture.
 * Here these two hash functions are defined multiple time; once for
 * 64-bit architectures, once for 32-bit SSE2 architectures, and once
 * for the rest (32-bit) architectures.
 * For each, nh_16 *must* be defined (works on multiples of 16 bytes).
 * Optionally, nh_vmac_nhbytes can be defined (for multiples of
 * VMAC_NHBYTES), and nh_16_2 and nh_vmac_nhbytes_2 (versions that do two
 * NH computations at once).
 */

#ifdef CONFIG_64BIT

#define nh_16(mp, kp, nw, rh, rl)					\
	do {								\
		int i; u64 th, tl;					\
		rh = rl = 0;						\
		for (i = 0; i < nw; i += 2) {				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+1]);	\
			ADD128(rh, rl, th, tl);				\
		}							\
	} while (0)

#define nh_16_2(mp, kp, nw, rh, rl, rh1, rl1)				\
	do {								\
		int i; u64 th, tl;					\
		rh1 = rl1 = rh = rl = 0;				\
		for (i = 0; i < nw; i += 2) {				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+1]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i+2],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+3]);	\
			ADD128(rh1, rl1, th, tl);			\
		}							\
	} while (0)

#if (VMAC_NHBYTES >= 64) /* These versions do 64-bytes of message at a time */
#define nh_vmac_nhbytes(mp, kp, nw, rh, rl)				\
	do {								\
		int i; u64 th, tl;					\
		rh = rl = 0;						\
		for (i = 0; i < nw; i += 8) {				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+1]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+2)+(kp)[i+2],	\
				pe64_to_cpup((mp)+i+3)+(kp)[i+3]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+4)+(kp)[i+4],	\
				pe64_to_cpup((mp)+i+5)+(kp)[i+5]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+6)+(kp)[i+6],	\
				pe64_to_cpup((mp)+i+7)+(kp)[i+7]);	\
			ADD128(rh, rl, th, tl);				\
		}							\
	} while (0)

#define nh_vmac_nhbytes_2(mp, kp, nw, rh, rl, rh1, rl1)			\
	do {								\
		int i; u64 th, tl;					\
		rh1 = rl1 = rh = rl = 0;				\
		for (i = 0; i < nw; i += 8) {				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+1]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i)+(kp)[i+2],	\
				pe64_to_cpup((mp)+i+1)+(kp)[i+3]);	\
			ADD128(rh1, rl1, th, tl);			\
			MUL64(th, tl, pe64_to_cpup((mp)+i+2)+(kp)[i+2],	\
				pe64_to_cpup((mp)+i+3)+(kp)[i+3]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+2)+(kp)[i+4],	\
				pe64_to_cpup((mp)+i+3)+(kp)[i+5]);	\
			ADD128(rh1, rl1, th, tl);			\
			MUL64(th, tl, pe64_to_cpup((mp)+i+4)+(kp)[i+4],	\
				pe64_to_cpup((mp)+i+5)+(kp)[i+5]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+4)+(kp)[i+6],	\
				pe64_to_cpup((mp)+i+5)+(kp)[i+7]);	\
			ADD128(rh1, rl1, th, tl);			\
			MUL64(th, tl, pe64_to_cpup((mp)+i+6)+(kp)[i+6],	\
				pe64_to_cpup((mp)+i+7)+(kp)[i+7]);	\
			ADD128(rh, rl, th, tl);				\
			MUL64(th, tl, pe64_to_cpup((mp)+i+6)+(kp)[i+8],	\
				pe64_to_cpup((mp)+i+7)+(kp)[i+9]);	\
			ADD128(rh1, rl1, th, tl);			\
		}							\
	} while (0)
#endif

#define poly_step(ah, al, kh, kl, mh, ml)				\
	do {								\
		u64 t1h, t1l, t2h, t2l, t3h, t3l, z = 0;		\
		/* compute ab*cd, put bd into result registers */	\
		PMUL64(t3h, t3l, al, kh);				\
		PMUL64(t2h, t2l, ah, kl);				\
		PMUL64(t1h, t1l, ah, 2*kh);				\
		PMUL64(ah, al, al, kl);					\
		/* add 2 * ac to result */				\
		ADD128(ah, al, t1h, t1l);				\
		/* add together ad + bc */				\
		ADD128(t2h, t2l, t3h, t3l);				\
		/* now (ah,al), (t2l,2*t2h) need summing */		\
		/* first add the high registers, carrying into t2h */	\
		ADD128(t2h, ah, z, t2l);				\
		/* double t2h and add top bit of ah */			\
		t2h = 2 * t2h + (ah >> 63);				\
		ah &= m63;						\
		/* now add the low registers */				\
		ADD128(ah, al, mh, ml);					\
		ADD128(ah, al, z, t2h);					\
	} while (0)

#else /* ! CONFIG_64BIT */

#ifndef nh_16
#define nh_16(mp, kp, nw, rh, rl)					\
	do {								\
		u64 t1, t2, m1, m2, t;					\
		int i;							\
		rh = rl = t = 0;					\
		for (i = 0; i < nw; i += 2)  {				\
			t1 = pe64_to_cpup(mp+i) + kp[i];		\
			t2 = pe64_to_cpup(mp+i+1) + kp[i+1];		\
			m2 = MUL32(t1 >> 32, t2);			\
			m1 = MUL32(t1, t2 >> 32);			\
			ADD128(rh, rl, MUL32(t1 >> 32, t2 >> 32),	\
				MUL32(t1, t2));				\
			rh += (u64)(u32)(m1 >> 32)			\
				+ (u32)(m2 >> 32);			\
			t += (u64)(u32)m1 + (u32)m2;			\
		}							\
		ADD128(rh, rl, (t >> 32), (t << 32));			\
	} while (0)
#endif

static void poly_step_func(u64 *ahi, u64 *alo,
			const u64 *kh, const u64 *kl,
			const u64 *mh, const u64 *ml)
{
#define a0 (*(((u32 *)alo)+INDEX_LOW))
#define a1 (*(((u32 *)alo)+INDEX_HIGH))
#define a2 (*(((u32 *)ahi)+INDEX_LOW))
#define a3 (*(((u32 *)ahi)+INDEX_HIGH))
#define k0 (*(((u32 *)kl)+INDEX_LOW))
#define k1 (*(((u32 *)kl)+INDEX_HIGH))
#define k2 (*(((u32 *)kh)+INDEX_LOW))
#define k3 (*(((u32 *)kh)+INDEX_HIGH))

	u64 p, q, t;
	u32 t2;

	p = MUL32(a3, k3);
	p += p;
	p += *(u64 *)mh;
	p += MUL32(a0, k2);
	p += MUL32(a1, k1);
	p += MUL32(a2, k0);
	t = (u32)(p);
	p >>= 32;
	p += MUL32(a0, k3);
	p += MUL32(a1, k2);
	p += MUL32(a2, k1);
	p += MUL32(a3, k0);
	t |= ((u64)((u32)p & 0x7fffffff)) << 32;
	p >>= 31;
	p += (u64)(((u32 *)ml)[INDEX_LOW]);
	p += MUL32(a0, k0);
	q =  MUL32(a1, k3);
	q += MUL32(a2, k2);
	q += MUL32(a3, k1);
	q += q;
	p += q;
	t2 = (u32)(p);
	p >>= 32;
	p += (u64)(((u32 *)ml)[INDEX_HIGH]);
	p += MUL32(a0, k1);
	p += MUL32(a1, k0);
	q =  MUL32(a2, k3);
	q += MUL32(a3, k2);
	q += q;
	p += q;
	*(u64 *)(alo) = (p << 32) | t2;
	p >>= 32;
	*(u64 *)(ahi) = p + t;

#undef a0
#undef a1
#undef a2
#undef a3
#undef k0
#undef k1
#undef k2
#undef k3
}

#define poly_step(ah, al, kh, kl, mh, ml)				\
	poly_step_func(&(ah), &(al), &(kh), &(kl), &(mh), &(ml))

#endif  /* end of specialized NH and poly definitions */

/* At least nh_16 is defined. Defined others as needed here */
#ifndef nh_16_2
#define nh_16_2(mp, kp, nw, rh, rl, rh2, rl2)				\
	do { 								\
		nh_16(mp, kp, nw, rh, rl);				\
		nh_16(mp, ((kp)+2), nw, rh2, rl2);			\
	} while (0)
#endif
#ifndef nh_vmac_nhbytes
#define nh_vmac_nhbytes(mp, kp, nw, rh, rl)				\
	nh_16(mp, kp, nw, rh, rl)
#endif
#ifndef nh_vmac_nhbytes_2
#define nh_vmac_nhbytes_2(mp, kp, nw, rh, rl, rh2, rl2)			\
	do {								\
		nh_vmac_nhbytes(mp, kp, nw, rh, rl);			\
		nh_vmac_nhbytes(mp, ((kp)+2), nw, rh2, rl2);		\
	} while (0)
#endif

static u64 l3hash(u64 p1, u64 p2, u64 k1, u64 k2, u64 len)
{
	u64 rh, rl, t, z = 0;

	/* fully reduce (p1,p2)+(len,0) mod p127 */
	t = p1 >> 63;
	p1 &= m63;
	ADD128(p1, p2, len, t);
	/* At this point, (p1,p2) is at most 2^127+(len<<64) */
	t = (p1 > m63) + ((p1 == m63) && (p2 == m64));
	ADD128(p1, p2, z, t);
	p1 &= m63;

	/* compute (p1,p2)/(2^64-2^32) and (p1,p2)%(2^64-2^32) */
	t = p1 + (p2 >> 32);
	t += (t >> 32);
	t += (u32)t > 0xfffffffeu;
	p1 += (t >> 32);
	p2 += (p1 << 32);

	/* compute (p1+k1)%p64 and (p2+k2)%p64 */
	p1 += k1;
	p1 += (0 - (p1 < k1)) & 257;
	p2 += k2;
	p2 += (0 - (p2 < k2)) & 257;

	/* compute (p1+k1)*(p2+k2)%p64 */
	MUL64(rh, rl, p1, p2);
	t = rh >> 56;
	ADD128(t, rl, z, rh);
	rh <<= 8;
	ADD128(t, rl, z, rh);
	t += t << 8;
	rl += t;
	rl += (0 - (rl < t)) & 257;
	rl += (0 - (rl > p64-1)) & 257;
	return rl;
}

/* L1 and L2-hash one or more VMAC_NHBYTES-byte blocks */
static void vhash_blocks(const struct vmac_tfm_ctx *tctx,
			 struct vmac_desc_ctx *dctx,
			 const __le64 *mptr, unsigned int blocks)
{
	const u64 *kptr = tctx->nhkey;
	const u64 pkh = tctx->polykey[0];
	const u64 pkl = tctx->polykey[1];
	u64 ch = dctx->polytmp[0];
	u64 cl = dctx->polytmp[1];
	u64 rh, rl;

	if (!dctx->first_block_processed) {
		dctx->first_block_processed = true;
		nh_vmac_nhbytes(mptr, kptr, VMAC_NHBYTES/8, rh, rl);
		rh &= m62;
		ADD128(ch, cl, rh, rl);
		mptr += (VMAC_NHBYTES/sizeof(u64));
		blocks--;
	}

	while (blocks--) {
		nh_vmac_nhbytes(mptr, kptr, VMAC_NHBYTES/8, rh, rl);
		rh &= m62;
		poly_step(ch, cl, pkh, pkl, rh, rl);
		mptr += (VMAC_NHBYTES/sizeof(u64));
	}

	dctx->polytmp[0] = ch;
	dctx->polytmp[1] = cl;
}

static int vmac_setkey(struct crypto_shash *tfm,
		       const u8 *key, unsigned int keylen)
{
	struct vmac_tfm_ctx *tctx = crypto_shash_ctx(tfm);
	__be64 out[2];
	u8 in[16] = { 0 };
	unsigned int i;
	int err;

	if (keylen != VMAC_KEY_LEN)
		return -EINVAL;

	err = crypto_cipher_setkey(tctx->cipher, key, keylen);
	if (err)
		return err;

	/* Fill nh key */
	in[0] = 0x80;
	for (i = 0; i < ARRAY_SIZE(tctx->nhkey); i += 2) {
		crypto_cipher_encrypt_one(tctx->cipher, (u8 *)out, in);
		tctx->nhkey[i] = be64_to_cpu(out[0]);
		tctx->nhkey[i+1] = be64_to_cpu(out[1]);
		in[15]++;
	}

	/* Fill poly key */
	in[0] = 0xC0;
	in[15] = 0;
	for (i = 0; i < ARRAY_SIZE(tctx->polykey); i += 2) {
		crypto_cipher_encrypt_one(tctx->cipher, (u8 *)out, in);
		tctx->polykey[i] = be64_to_cpu(out[0]) & mpoly;
		tctx->polykey[i+1] = be64_to_cpu(out[1]) & mpoly;
		in[15]++;
	}

	/* Fill ip key */
	in[0] = 0xE0;
	in[15] = 0;
	for (i = 0; i < ARRAY_SIZE(tctx->l3key); i += 2) {
		do {
			crypto_cipher_encrypt_one(tctx->cipher, (u8 *)out, in);
			tctx->l3key[i] = be64_to_cpu(out[0]);
			tctx->l3key[i+1] = be64_to_cpu(out[1]);
			in[15]++;
		} while (tctx->l3key[i] >= p64 || tctx->l3key[i+1] >= p64);
	}

	return 0;
}

static int vmac_init(struct shash_desc *desc)
{
	const struct vmac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct vmac_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->partial_size = 0;
	dctx->first_block_processed = false;
	memcpy(dctx->polytmp, tctx->polykey, sizeof(dctx->polytmp));
	dctx->nonce_size = 0;
	return 0;
}

static int vmac_update(struct shash_desc *desc, const u8 *p, unsigned int len)
{
	const struct vmac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct vmac_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int n;

	/* Nonce is passed as first VMAC_NONCEBYTES bytes of data */
	if (dctx->nonce_size < VMAC_NONCEBYTES) {
		n = min(len, VMAC_NONCEBYTES - dctx->nonce_size);
		memcpy(&dctx->nonce.bytes[dctx->nonce_size], p, n);
		dctx->nonce_size += n;
		p += n;
		len -= n;
	}

	if (dctx->partial_size) {
		n = min(len, VMAC_NHBYTES - dctx->partial_size);
		memcpy(&dctx->partial[dctx->partial_size], p, n);
		dctx->partial_size += n;
		p += n;
		len -= n;
		if (dctx->partial_size == VMAC_NHBYTES) {
			vhash_blocks(tctx, dctx, dctx->partial_words, 1);
			dctx->partial_size = 0;
		}
	}

	if (len >= VMAC_NHBYTES) {
		n = round_down(len, VMAC_NHBYTES);
		/* TODO: 'p' may be misaligned here */
		vhash_blocks(tctx, dctx, (const __le64 *)p, n / VMAC_NHBYTES);
		p += n;
		len -= n;
	}

	if (len) {
		memcpy(dctx->partial, p, len);
		dctx->partial_size = len;
	}

	return 0;
}

static u64 vhash_final(const struct vmac_tfm_ctx *tctx,
		       struct vmac_desc_ctx *dctx)
{
	unsigned int partial = dctx->partial_size;
	u64 ch = dctx->polytmp[0];
	u64 cl = dctx->polytmp[1];

	/* L1 and L2-hash the final block if needed */
	if (partial) {
		/* Zero-pad to next 128-bit boundary */
		unsigned int n = round_up(partial, 16);
		u64 rh, rl;

		memset(&dctx->partial[partial], 0, n - partial);
		nh_16(dctx->partial_words, tctx->nhkey, n / 8, rh, rl);
		rh &= m62;
		if (dctx->first_block_processed)
			poly_step(ch, cl, tctx->polykey[0], tctx->polykey[1],
				  rh, rl);
		else
			ADD128(ch, cl, rh, rl);
	}

	/* L3-hash the 128-bit output of L2-hash */
	return l3hash(ch, cl, tctx->l3key[0], tctx->l3key[1], partial * 8);
}

static int vmac_final(struct shash_desc *desc, u8 *out)
{
	const struct vmac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct vmac_desc_ctx *dctx = shash_desc_ctx(desc);
	int index;
	u64 hash, pad;

	if (dctx->nonce_size != VMAC_NONCEBYTES)
		return -EINVAL;

	/*
	 * The VMAC specification requires a nonce at least 1 bit shorter than
	 * the block cipher's block length, so we actually only accept a 127-bit
	 * nonce.  We define the unused bit to be the first one and require that
	 * it be 0, so the needed prepending of a 0 bit is implicit.
	 */
	if (dctx->nonce.bytes[0] & 0x80)
		return -EINVAL;

	/* Finish calculating the VHASH of the message */
	hash = vhash_final(tctx, dctx);

	/* Generate pseudorandom pad by encrypting the nonce */
	BUILD_BUG_ON(VMAC_NONCEBYTES != 2 * (VMAC_TAG_LEN / 8));
	index = dctx->nonce.bytes[VMAC_NONCEBYTES - 1] & 1;
	dctx->nonce.bytes[VMAC_NONCEBYTES - 1] &= ~1;
	crypto_cipher_encrypt_one(tctx->cipher, dctx->nonce.bytes,
				  dctx->nonce.bytes);
	pad = be64_to_cpu(dctx->nonce.pads[index]);

	/* The VMAC is the sum of VHASH and the pseudorandom pad */
	put_unaligned_be64(hash + pad, out);
	return 0;
}

static int vmac_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct crypto_cipher_spawn *spawn = crypto_instance_ctx(inst);
	struct vmac_tfm_ctx *tctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	tctx->cipher = cipher;
	return 0;
}

static void vmac_exit_tfm(struct crypto_tfm *tfm)
{
	struct vmac_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(tctx->cipher);
}

static int vmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_cipher_spawn *spawn;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	err = crypto_grab_cipher(spawn, shash_crypto_instance(inst),
				 crypto_attr_alg_name(tb[1]), 0, 0);
	if (err)
		goto err_free_inst;
	alg = crypto_spawn_cipher_alg(spawn);

	err = -EINVAL;
	if (alg->cra_blocksize != VMAC_NONCEBYTES)
		goto err_free_inst;

	err = crypto_inst_setname(shash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	inst->alg.base.cra_ctxsize = sizeof(struct vmac_tfm_ctx);
	inst->alg.base.cra_init = vmac_init_tfm;
	inst->alg.base.cra_exit = vmac_exit_tfm;

	inst->alg.descsize = sizeof(struct vmac_desc_ctx);
	inst->alg.digestsize = VMAC_TAG_LEN / 8;
	inst->alg.init = vmac_init;
	inst->alg.update = vmac_update;
	inst->alg.final = vmac_final;
	inst->alg.setkey = vmac_setkey;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static struct crypto_template vmac64_tmpl = {
	.name = "vmac64",
	.create = vmac_create,
	.module = THIS_MODULE,
};

static int __init vmac_module_init(void)
{
	return crypto_register_template(&vmac64_tmpl);
}

static void __exit vmac_module_exit(void)
{
	crypto_unregister_template(&vmac64_tmpl);
}

subsys_initcall(vmac_module_init);
module_exit(vmac_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VMAC hash algorithm");
MODULE_ALIAS_CRYPTO("vmac64");

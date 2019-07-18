/*
 * ppp_mppe.c - interface MPPE to the PPP code.
 * This version is for use with Linux kernel 2.6.14+
 *
 * By Frank Cusack <fcusack@fcusack.com>.
 * Copyright (c) 2002,2003,2004 Google, Inc.
 * All rights reserved.
 *
 * License:
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this product
 * may be distributed under the terms of the GNU General Public License (GPL),
 * in which case the provisions of the GPL apply INSTEAD OF those given above.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Changelog:
 *      08/12/05 - Matt Domsch <Matt_Domsch@dell.com>
 *                 Only need extra skb padding on transmit, not receive.
 *      06/18/04 - Matt Domsch <Matt_Domsch@dell.com>, Oleg Makarenko <mole@quadra.ru>
 *                 Use Linux kernel 2.6 arc4 and sha1 routines rather than
 *                 providing our own.
 *      2/15/04 - TS: added #include <version.h> and testing for Kernel
 *                    version before using
 *                    MOD_DEC_USAGE_COUNT/MOD_INC_USAGE_COUNT which are
 *                    deprecated in 2.6
 */

#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>
#include <linux/scatterlist.h>
#include <asm/unaligned.h>

#include "ppp_mppe.h"

MODULE_AUTHOR("Frank Cusack <fcusack@fcusack.com>");
MODULE_DESCRIPTION("Point-to-Point Protocol Microsoft Point-to-Point Encryption support");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("ppp-compress-" __stringify(CI_MPPE));
MODULE_SOFTDEP("pre: arc4");
MODULE_VERSION("1.0.2");

static unsigned int
setup_sg(struct scatterlist *sg, const void *address, unsigned int length)
{
	sg_set_buf(sg, address, length);
	return length;
}

#define SHA1_PAD_SIZE 40

/*
 * kernel crypto API needs its arguments to be in kmalloc'd memory, not in the module
 * static data area.  That means sha_pad needs to be kmalloc'd.
 */

struct sha_pad {
	unsigned char sha_pad1[SHA1_PAD_SIZE];
	unsigned char sha_pad2[SHA1_PAD_SIZE];
};
static struct sha_pad *sha_pad;

static inline void sha_pad_init(struct sha_pad *shapad)
{
	memset(shapad->sha_pad1, 0x00, sizeof(shapad->sha_pad1));
	memset(shapad->sha_pad2, 0xF2, sizeof(shapad->sha_pad2));
}

/*
 * State for an MPPE (de)compressor.
 */
struct ppp_mppe_state {
	struct crypto_sync_skcipher *arc4;
	struct shash_desc *sha1;
	unsigned char *sha1_digest;
	unsigned char master_key[MPPE_MAX_KEY_LEN];
	unsigned char session_key[MPPE_MAX_KEY_LEN];
	unsigned keylen;	/* key length in bytes             */
	/* NB: 128-bit == 16, 40-bit == 8! */
	/* If we want to support 56-bit,   */
	/* the unit has to change to bits  */
	unsigned char bits;	/* MPPE control bits */
	unsigned ccount;	/* 12-bit coherency count (seqno)  */
	unsigned stateful;	/* stateful mode flag */
	int discard;		/* stateful mode packet loss flag */
	int sanity_errors;	/* take down LCP if too many */
	int unit;
	int debug;
	struct compstat stats;
};

/* struct ppp_mppe_state.bits definitions */
#define MPPE_BIT_A	0x80	/* Encryption table were (re)inititalized */
#define MPPE_BIT_B	0x40	/* MPPC only (not implemented) */
#define MPPE_BIT_C	0x20	/* MPPC only (not implemented) */
#define MPPE_BIT_D	0x10	/* This is an encrypted frame */

#define MPPE_BIT_FLUSHED	MPPE_BIT_A
#define MPPE_BIT_ENCRYPTED	MPPE_BIT_D

#define MPPE_BITS(p) ((p)[4] & 0xf0)
#define MPPE_CCOUNT(p) ((((p)[4] & 0x0f) << 8) + (p)[5])
#define MPPE_CCOUNT_SPACE 0x1000	/* The size of the ccount space */

#define MPPE_OVHD	2	/* MPPE overhead/packet */
#define SANITY_MAX	1600	/* Max bogon factor we will tolerate */

/*
 * Key Derivation, from RFC 3078, RFC 3079.
 * Equivalent to Get_Key() for MS-CHAP as described in RFC 3079.
 */
static void get_new_key_from_sha(struct ppp_mppe_state * state)
{
	crypto_shash_init(state->sha1);
	crypto_shash_update(state->sha1, state->master_key,
			    state->keylen);
	crypto_shash_update(state->sha1, sha_pad->sha_pad1,
			    sizeof(sha_pad->sha_pad1));
	crypto_shash_update(state->sha1, state->session_key,
			    state->keylen);
	crypto_shash_update(state->sha1, sha_pad->sha_pad2,
			    sizeof(sha_pad->sha_pad2));
	crypto_shash_final(state->sha1, state->sha1_digest);
}

/*
 * Perform the MPPE rekey algorithm, from RFC 3078, sec. 7.3.
 * Well, not what's written there, but rather what they meant.
 */
static void mppe_rekey(struct ppp_mppe_state * state, int initial_key)
{
	struct scatterlist sg_in[1], sg_out[1];
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, state->arc4);

	skcipher_request_set_sync_tfm(req, state->arc4);
	skcipher_request_set_callback(req, 0, NULL, NULL);

	get_new_key_from_sha(state);
	if (!initial_key) {
		crypto_sync_skcipher_setkey(state->arc4, state->sha1_digest,
					    state->keylen);
		sg_init_table(sg_in, 1);
		sg_init_table(sg_out, 1);
		setup_sg(sg_in, state->sha1_digest, state->keylen);
		setup_sg(sg_out, state->session_key, state->keylen);
		skcipher_request_set_crypt(req, sg_in, sg_out, state->keylen,
					   NULL);
		if (crypto_skcipher_encrypt(req))
    		    printk(KERN_WARNING "mppe_rekey: cipher_encrypt failed\n");
	} else {
		memcpy(state->session_key, state->sha1_digest, state->keylen);
	}
	if (state->keylen == 8) {
		/* See RFC 3078 */
		state->session_key[0] = 0xd1;
		state->session_key[1] = 0x26;
		state->session_key[2] = 0x9e;
	}
	crypto_sync_skcipher_setkey(state->arc4, state->session_key,
				    state->keylen);
	skcipher_request_zero(req);
}

/*
 * Allocate space for a (de)compressor.
 */
static void *mppe_alloc(unsigned char *options, int optlen)
{
	struct ppp_mppe_state *state;
	struct crypto_shash *shash;
	unsigned int digestsize;

	if (optlen != CILEN_MPPE + sizeof(state->master_key) ||
	    options[0] != CI_MPPE || options[1] != CILEN_MPPE)
		goto out;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		goto out;


	state->arc4 = crypto_alloc_sync_skcipher("ecb(arc4)", 0, 0);
	if (IS_ERR(state->arc4)) {
		state->arc4 = NULL;
		goto out_free;
	}

	shash = crypto_alloc_shash("sha1", 0, 0);
	if (IS_ERR(shash))
		goto out_free;

	state->sha1 = kmalloc(sizeof(*state->sha1) +
				     crypto_shash_descsize(shash),
			      GFP_KERNEL);
	if (!state->sha1) {
		crypto_free_shash(shash);
		goto out_free;
	}
	state->sha1->tfm = shash;

	digestsize = crypto_shash_digestsize(shash);
	if (digestsize < MPPE_MAX_KEY_LEN)
		goto out_free;

	state->sha1_digest = kmalloc(digestsize, GFP_KERNEL);
	if (!state->sha1_digest)
		goto out_free;

	/* Save keys. */
	memcpy(state->master_key, &options[CILEN_MPPE],
	       sizeof(state->master_key));
	memcpy(state->session_key, state->master_key,
	       sizeof(state->master_key));

	/*
	 * We defer initial key generation until mppe_init(), as mppe_alloc()
	 * is called frequently during negotiation.
	 */

	return (void *)state;

out_free:
	kfree(state->sha1_digest);
	if (state->sha1) {
		crypto_free_shash(state->sha1->tfm);
		kzfree(state->sha1);
	}
	crypto_free_sync_skcipher(state->arc4);
	kfree(state);
out:
	return NULL;
}

/*
 * Deallocate space for a (de)compressor.
 */
static void mppe_free(void *arg)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;
	if (state) {
		kfree(state->sha1_digest);
		crypto_free_shash(state->sha1->tfm);
		kzfree(state->sha1);
		crypto_free_sync_skcipher(state->arc4);
		kfree(state);
	}
}

/*
 * Initialize (de)compressor state.
 */
static int
mppe_init(void *arg, unsigned char *options, int optlen, int unit, int debug,
	  const char *debugstr)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;
	unsigned char mppe_opts;

	if (optlen != CILEN_MPPE ||
	    options[0] != CI_MPPE || options[1] != CILEN_MPPE)
		return 0;

	MPPE_CI_TO_OPTS(&options[2], mppe_opts);
	if (mppe_opts & MPPE_OPT_128)
		state->keylen = 16;
	else if (mppe_opts & MPPE_OPT_40)
		state->keylen = 8;
	else {
		printk(KERN_WARNING "%s[%d]: unknown key length\n", debugstr,
		       unit);
		return 0;
	}
	if (mppe_opts & MPPE_OPT_STATEFUL)
		state->stateful = 1;

	/* Generate the initial session key. */
	mppe_rekey(state, 1);

	if (debug) {
		printk(KERN_DEBUG "%s[%d]: initialized with %d-bit %s mode\n",
		       debugstr, unit, (state->keylen == 16) ? 128 : 40,
		       (state->stateful) ? "stateful" : "stateless");
		printk(KERN_DEBUG
		       "%s[%d]: keys: master: %*phN initial session: %*phN\n",
		       debugstr, unit,
		       (int)sizeof(state->master_key), state->master_key,
		       (int)sizeof(state->session_key), state->session_key);
	}

	/*
	 * Initialize the coherency count.  The initial value is not specified
	 * in RFC 3078, but we can make a reasonable assumption that it will
	 * start at 0.  Setting it to the max here makes the comp/decomp code
	 * do the right thing (determined through experiment).
	 */
	state->ccount = MPPE_CCOUNT_SPACE - 1;

	/*
	 * Note that even though we have initialized the key table, we don't
	 * set the FLUSHED bit.  This is contrary to RFC 3078, sec. 3.1.
	 */
	state->bits = MPPE_BIT_ENCRYPTED;

	state->unit = unit;
	state->debug = debug;

	return 1;
}

static int
mppe_comp_init(void *arg, unsigned char *options, int optlen, int unit,
	       int hdrlen, int debug)
{
	/* ARGSUSED */
	return mppe_init(arg, options, optlen, unit, debug, "mppe_comp_init");
}

/*
 * We received a CCP Reset-Request (actually, we are sending a Reset-Ack),
 * tell the compressor to rekey.  Note that we MUST NOT rekey for
 * every CCP Reset-Request; we only rekey on the next xmit packet.
 * We might get multiple CCP Reset-Requests if our CCP Reset-Ack is lost.
 * So, rekeying for every CCP Reset-Request is broken as the peer will not
 * know how many times we've rekeyed.  (If we rekey and THEN get another
 * CCP Reset-Request, we must rekey again.)
 */
static void mppe_comp_reset(void *arg)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;

	state->bits |= MPPE_BIT_FLUSHED;
}

/*
 * Compress (encrypt) a packet.
 * It's strange to call this a compressor, since the output is always
 * MPPE_OVHD + 2 bytes larger than the input.
 */
static int
mppe_compress(void *arg, unsigned char *ibuf, unsigned char *obuf,
	      int isize, int osize)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, state->arc4);
	int proto;
	int err;
	struct scatterlist sg_in[1], sg_out[1];

	/*
	 * Check that the protocol is in the range we handle.
	 */
	proto = PPP_PROTOCOL(ibuf);
	if (proto < 0x0021 || proto > 0x00fa)
		return 0;

	/* Make sure we have enough room to generate an encrypted packet. */
	if (osize < isize + MPPE_OVHD + 2) {
		/* Drop the packet if we should encrypt it, but can't. */
		printk(KERN_DEBUG "mppe_compress[%d]: osize too small! "
		       "(have: %d need: %d)\n", state->unit,
		       osize, osize + MPPE_OVHD + 2);
		return -1;
	}

	osize = isize + MPPE_OVHD + 2;

	/*
	 * Copy over the PPP header and set control bits.
	 */
	obuf[0] = PPP_ADDRESS(ibuf);
	obuf[1] = PPP_CONTROL(ibuf);
	put_unaligned_be16(PPP_COMP, obuf + 2);
	obuf += PPP_HDRLEN;

	state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
	if (state->debug >= 7)
		printk(KERN_DEBUG "mppe_compress[%d]: ccount %d\n", state->unit,
		       state->ccount);
	put_unaligned_be16(state->ccount, obuf);

	if (!state->stateful ||	/* stateless mode     */
	    ((state->ccount & 0xff) == 0xff) ||	/* "flag" packet      */
	    (state->bits & MPPE_BIT_FLUSHED)) {	/* CCP Reset-Request  */
		/* We must rekey */
		if (state->debug && state->stateful)
			printk(KERN_DEBUG "mppe_compress[%d]: rekeying\n",
			       state->unit);
		mppe_rekey(state, 0);
		state->bits |= MPPE_BIT_FLUSHED;
	}
	obuf[0] |= state->bits;
	state->bits &= ~MPPE_BIT_FLUSHED;	/* reset for next xmit */

	obuf += MPPE_OVHD;
	ibuf += 2;		/* skip to proto field */
	isize -= 2;

	/* Encrypt packet */
	sg_init_table(sg_in, 1);
	sg_init_table(sg_out, 1);
	setup_sg(sg_in, ibuf, isize);
	setup_sg(sg_out, obuf, osize);

	skcipher_request_set_sync_tfm(req, state->arc4);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg_in, sg_out, isize, NULL);
	err = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
	if (err) {
		printk(KERN_DEBUG "crypto_cypher_encrypt failed\n");
		return -1;
	}

	state->stats.unc_bytes += isize;
	state->stats.unc_packets++;
	state->stats.comp_bytes += osize;
	state->stats.comp_packets++;

	return osize;
}

/*
 * Since every frame grows by MPPE_OVHD + 2 bytes, this is always going
 * to look bad ... and the longer the link is up the worse it will get.
 */
static void mppe_comp_stats(void *arg, struct compstat *stats)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;

	*stats = state->stats;
}

static int
mppe_decomp_init(void *arg, unsigned char *options, int optlen, int unit,
		 int hdrlen, int mru, int debug)
{
	/* ARGSUSED */
	return mppe_init(arg, options, optlen, unit, debug, "mppe_decomp_init");
}

/*
 * We received a CCP Reset-Ack.  Just ignore it.
 */
static void mppe_decomp_reset(void *arg)
{
	/* ARGSUSED */
	return;
}

/*
 * Decompress (decrypt) an MPPE packet.
 */
static int
mppe_decompress(void *arg, unsigned char *ibuf, int isize, unsigned char *obuf,
		int osize)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, state->arc4);
	unsigned ccount;
	int flushed = MPPE_BITS(ibuf) & MPPE_BIT_FLUSHED;
	struct scatterlist sg_in[1], sg_out[1];

	if (isize <= PPP_HDRLEN + MPPE_OVHD) {
		if (state->debug)
			printk(KERN_DEBUG
			       "mppe_decompress[%d]: short pkt (%d)\n",
			       state->unit, isize);
		return DECOMP_ERROR;
	}

	/*
	 * Make sure we have enough room to decrypt the packet.
	 * Note that for our test we only subtract 1 byte whereas in
	 * mppe_compress() we added 2 bytes (+MPPE_OVHD);
	 * this is to account for possible PFC.
	 */
	if (osize < isize - MPPE_OVHD - 1) {
		printk(KERN_DEBUG "mppe_decompress[%d]: osize too small! "
		       "(have: %d need: %d)\n", state->unit,
		       osize, isize - MPPE_OVHD - 1);
		return DECOMP_ERROR;
	}
	osize = isize - MPPE_OVHD - 2;	/* assume no PFC */

	ccount = MPPE_CCOUNT(ibuf);
	if (state->debug >= 7)
		printk(KERN_DEBUG "mppe_decompress[%d]: ccount %d\n",
		       state->unit, ccount);

	/* sanity checks -- terminate with extreme prejudice */
	if (!(MPPE_BITS(ibuf) & MPPE_BIT_ENCRYPTED)) {
		printk(KERN_DEBUG
		       "mppe_decompress[%d]: ENCRYPTED bit not set!\n",
		       state->unit);
		state->sanity_errors += 100;
		goto sanity_error;
	}
	if (!state->stateful && !flushed) {
		printk(KERN_DEBUG "mppe_decompress[%d]: FLUSHED bit not set in "
		       "stateless mode!\n", state->unit);
		state->sanity_errors += 100;
		goto sanity_error;
	}
	if (state->stateful && ((ccount & 0xff) == 0xff) && !flushed) {
		printk(KERN_DEBUG "mppe_decompress[%d]: FLUSHED bit not set on "
		       "flag packet!\n", state->unit);
		state->sanity_errors += 100;
		goto sanity_error;
	}

	/*
	 * Check the coherency count.
	 */

	if (!state->stateful) {
		/* Discard late packet */
		if ((ccount - state->ccount) % MPPE_CCOUNT_SPACE
						> MPPE_CCOUNT_SPACE / 2) {
			state->sanity_errors++;
			goto sanity_error;
		}

		/* RFC 3078, sec 8.1.  Rekey for every packet. */
		while (state->ccount != ccount) {
			mppe_rekey(state, 0);
			state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
		}
	} else {
		/* RFC 3078, sec 8.2. */
		if (!state->discard) {
			/* normal state */
			state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
			if (ccount != state->ccount) {
				/*
				 * (ccount > state->ccount)
				 * Packet loss detected, enter the discard state.
				 * Signal the peer to rekey (by sending a CCP Reset-Request).
				 */
				state->discard = 1;
				return DECOMP_ERROR;
			}
		} else {
			/* discard state */
			if (!flushed) {
				/* ccp.c will be silent (no additional CCP Reset-Requests). */
				return DECOMP_ERROR;
			} else {
				/* Rekey for every missed "flag" packet. */
				while ((ccount & ~0xff) !=
				       (state->ccount & ~0xff)) {
					mppe_rekey(state, 0);
					state->ccount =
					    (state->ccount +
					     256) % MPPE_CCOUNT_SPACE;
				}

				/* reset */
				state->discard = 0;
				state->ccount = ccount;
				/*
				 * Another problem with RFC 3078 here.  It implies that the
				 * peer need not send a Reset-Ack packet.  But RFC 1962
				 * requires it.  Hopefully, M$ does send a Reset-Ack; even
				 * though it isn't required for MPPE synchronization, it is
				 * required to reset CCP state.
				 */
			}
		}
		if (flushed)
			mppe_rekey(state, 0);
	}

	/*
	 * Fill in the first part of the PPP header.  The protocol field
	 * comes from the decrypted data.
	 */
	obuf[0] = PPP_ADDRESS(ibuf);	/* +1 */
	obuf[1] = PPP_CONTROL(ibuf);	/* +1 */
	obuf += 2;
	ibuf += PPP_HDRLEN + MPPE_OVHD;
	isize -= PPP_HDRLEN + MPPE_OVHD;	/* -6 */
	/* net osize: isize-4 */

	/*
	 * Decrypt the first byte in order to check if it is
	 * a compressed or uncompressed protocol field.
	 */
	sg_init_table(sg_in, 1);
	sg_init_table(sg_out, 1);
	setup_sg(sg_in, ibuf, 1);
	setup_sg(sg_out, obuf, 1);

	skcipher_request_set_sync_tfm(req, state->arc4);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg_in, sg_out, 1, NULL);
	if (crypto_skcipher_decrypt(req)) {
		printk(KERN_DEBUG "crypto_cypher_decrypt failed\n");
		osize = DECOMP_ERROR;
		goto out_zap_req;
	}

	/*
	 * Do PFC decompression.
	 * This would be nicer if we were given the actual sk_buff
	 * instead of a char *.
	 */
	if ((obuf[0] & 0x01) != 0) {
		obuf[1] = obuf[0];
		obuf[0] = 0;
		obuf++;
		osize++;
	}

	/* And finally, decrypt the rest of the packet. */
	setup_sg(sg_in, ibuf + 1, isize - 1);
	setup_sg(sg_out, obuf + 1, osize - 1);
	skcipher_request_set_crypt(req, sg_in, sg_out, isize - 1, NULL);
	if (crypto_skcipher_decrypt(req)) {
		printk(KERN_DEBUG "crypto_cypher_decrypt failed\n");
		osize = DECOMP_ERROR;
		goto out_zap_req;
	}

	state->stats.unc_bytes += osize;
	state->stats.unc_packets++;
	state->stats.comp_bytes += isize;
	state->stats.comp_packets++;

	/* good packet credit */
	state->sanity_errors >>= 1;

out_zap_req:
	skcipher_request_zero(req);
	return osize;

sanity_error:
	if (state->sanity_errors < SANITY_MAX)
		return DECOMP_ERROR;
	else
		/* Take LCP down if the peer is sending too many bogons.
		 * We don't want to do this for a single or just a few
		 * instances since it could just be due to packet corruption.
		 */
		return DECOMP_FATALERROR;
}

/*
 * Incompressible data has arrived (this should never happen!).
 * We should probably drop the link if the protocol is in the range
 * of what should be encrypted.  At the least, we should drop this
 * packet.  (How to do this?)
 */
static void mppe_incomp(void *arg, unsigned char *ibuf, int icnt)
{
	struct ppp_mppe_state *state = (struct ppp_mppe_state *) arg;

	if (state->debug &&
	    (PPP_PROTOCOL(ibuf) >= 0x0021 && PPP_PROTOCOL(ibuf) <= 0x00fa))
		printk(KERN_DEBUG
		       "mppe_incomp[%d]: incompressible (unencrypted) data! "
		       "(proto %04x)\n", state->unit, PPP_PROTOCOL(ibuf));

	state->stats.inc_bytes += icnt;
	state->stats.inc_packets++;
	state->stats.unc_bytes += icnt;
	state->stats.unc_packets++;
}

/*************************************************************
 * Module interface table
 *************************************************************/

/*
 * Procedures exported to if_ppp.c.
 */
static struct compressor ppp_mppe = {
	.compress_proto = CI_MPPE,
	.comp_alloc     = mppe_alloc,
	.comp_free      = mppe_free,
	.comp_init      = mppe_comp_init,
	.comp_reset     = mppe_comp_reset,
	.compress       = mppe_compress,
	.comp_stat      = mppe_comp_stats,
	.decomp_alloc   = mppe_alloc,
	.decomp_free    = mppe_free,
	.decomp_init    = mppe_decomp_init,
	.decomp_reset   = mppe_decomp_reset,
	.decompress     = mppe_decompress,
	.incomp         = mppe_incomp,
	.decomp_stat    = mppe_comp_stats,
	.owner          = THIS_MODULE,
	.comp_extra     = MPPE_PAD,
};

/*
 * ppp_mppe_init()
 *
 * Prior to allowing load, try to load the arc4 and sha1 crypto
 * libraries.  The actual use will be allocated later, but
 * this way the module will fail to insmod if they aren't available.
 */

static int __init ppp_mppe_init(void)
{
	int answer;
	if (!(crypto_has_skcipher("ecb(arc4)", 0, CRYPTO_ALG_ASYNC) &&
	      crypto_has_ahash("sha1", 0, CRYPTO_ALG_ASYNC)))
		return -ENODEV;

	sha_pad = kmalloc(sizeof(struct sha_pad), GFP_KERNEL);
	if (!sha_pad)
		return -ENOMEM;
	sha_pad_init(sha_pad);

	answer = ppp_register_compressor(&ppp_mppe);

	if (answer == 0)
		printk(KERN_INFO "PPP MPPE Compression module registered\n");
	else
		kfree(sha_pad);

	return answer;
}

static void __exit ppp_mppe_cleanup(void)
{
	ppp_unregister_compressor(&ppp_mppe);
	kfree(sha_pad);
}

module_init(ppp_mppe_init);
module_exit(ppp_mppe_cleanup);

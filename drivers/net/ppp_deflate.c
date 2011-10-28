/*
 *  ==FILEVERSION 980319==
 *
 * ppp_deflate.c - interface the zlib procedures for Deflate compression
 * and decompression (as used by gzip) to the PPP code.
 * This version is for use with Linux kernel 1.3.X.
 *
 * Copyright (c) 1994 The Australian National University.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied. The Australian National University
 * makes no representations about the suitability of this software for
 * any purpose.
 *
 * IN NO EVENT SHALL THE AUSTRALIAN NATIONAL UNIVERSITY BE LIABLE TO ANY
 * PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE AUSTRALIAN NATIONAL UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * THE AUSTRALIAN NATIONAL UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUSTRALIAN NATIONAL UNIVERSITY HAS NO
 * OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS,
 * OR MODIFICATIONS.
 *
 * From: deflate.c,v 1.1 1996/01/18 03:17:48 paulus Exp
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/string.h>

#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>

#include <linux/zlib.h>

/*
 * State for a Deflate (de)compressor.
 */
struct ppp_deflate_state {
    int		seqno;
    int		w_size;
    int		unit;
    int		mru;
    int		debug;
    z_stream	strm;
    struct compstat stats;
};

#define DEFLATE_OVHD	2		/* Deflate overhead/packet */

static void	*z_comp_alloc(unsigned char *options, int opt_len);
static void	*z_decomp_alloc(unsigned char *options, int opt_len);
static void	z_comp_free(void *state);
static void	z_decomp_free(void *state);
static int	z_comp_init(void *state, unsigned char *options,
				 int opt_len,
				 int unit, int hdrlen, int debug);
static int	z_decomp_init(void *state, unsigned char *options,
				   int opt_len,
				   int unit, int hdrlen, int mru, int debug);
static int	z_compress(void *state, unsigned char *rptr,
				unsigned char *obuf,
				int isize, int osize);
static void	z_incomp(void *state, unsigned char *ibuf, int icnt);
static int	z_decompress(void *state, unsigned char *ibuf,
				int isize, unsigned char *obuf, int osize);
static void	z_comp_reset(void *state);
static void	z_decomp_reset(void *state);
static void	z_comp_stats(void *state, struct compstat *stats);

/**
 *	z_comp_free - free the memory used by a compressor
 *	@arg:	pointer to the private state for the compressor.
 */
static void z_comp_free(void *arg)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	if (state) {
		zlib_deflateEnd(&state->strm);
		vfree(state->strm.workspace);
		kfree(state);
	}
}

/**
 *	z_comp_alloc - allocate space for a compressor.
 *	@options: pointer to CCP option data
 *	@opt_len: length of the CCP option at @options.
 *
 *	The @options pointer points to the a buffer containing the
 *	CCP option data for the compression being negotiated.  It is
 *	formatted according to RFC1979, and describes the window
 *	size that the peer is requesting that we use in compressing
 *	data to be sent to it.
 *
 *	Returns the pointer to the private state for the compressor,
 *	or NULL if we could not allocate enough memory.
 */
static void *z_comp_alloc(unsigned char *options, int opt_len)
{
	struct ppp_deflate_state *state;
	int w_size;

	if (opt_len != CILEN_DEFLATE ||
	    (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT) ||
	    options[1] != CILEN_DEFLATE ||
	    DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL ||
	    options[3] != DEFLATE_CHK_SEQUENCE)
		return NULL;
	w_size = DEFLATE_SIZE(options[2]);
	if (w_size < DEFLATE_MIN_SIZE || w_size > DEFLATE_MAX_SIZE)
		return NULL;

	state = kzalloc(sizeof(*state),
						     GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->strm.next_in   = NULL;
	state->w_size         = w_size;
	state->strm.workspace = vmalloc(zlib_deflate_workspacesize());
	if (state->strm.workspace == NULL)
		goto out_free;

	if (zlib_deflateInit2(&state->strm, Z_DEFAULT_COMPRESSION,
			 DEFLATE_METHOD_VAL, -w_size, 8, Z_DEFAULT_STRATEGY)
	    != Z_OK)
		goto out_free;
	return (void *) state;

out_free:
	z_comp_free(state);
	return NULL;
}

/**
 *	z_comp_init - initialize a previously-allocated compressor.
 *	@arg:	pointer to the private state for the compressor
 *	@options: pointer to the CCP option data describing the
 *		compression that was negotiated with the peer
 *	@opt_len: length of the CCP option data at @options
 *	@unit:	PPP unit number for diagnostic messages
 *	@hdrlen: ignored (present for backwards compatibility)
 *	@debug:	debug flag; if non-zero, debug messages are printed.
 *
 *	The CCP options described by @options must match the options
 *	specified when the compressor was allocated.  The compressor
 *	history is reset.  Returns 0 for failure (CCP options don't
 *	match) or 1 for success.
 */
static int z_comp_init(void *arg, unsigned char *options, int opt_len,
		       int unit, int hdrlen, int debug)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	if (opt_len < CILEN_DEFLATE ||
	    (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT) ||
	    options[1] != CILEN_DEFLATE ||
	    DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL ||
	    DEFLATE_SIZE(options[2]) != state->w_size ||
	    options[3] != DEFLATE_CHK_SEQUENCE)
		return 0;

	state->seqno = 0;
	state->unit  = unit;
	state->debug = debug;

	zlib_deflateReset(&state->strm);

	return 1;
}

/**
 *	z_comp_reset - reset a previously-allocated compressor.
 *	@arg:	pointer to private state for the compressor.
 *
 *	This clears the history for the compressor and makes it
 *	ready to start emitting a new compressed stream.
 */
static void z_comp_reset(void *arg)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	state->seqno = 0;
	zlib_deflateReset(&state->strm);
}

/**
 *	z_compress - compress a PPP packet with Deflate compression.
 *	@arg:	pointer to private state for the compressor
 *	@rptr:	uncompressed packet (input)
 *	@obuf:	compressed packet (output)
 *	@isize:	size of uncompressed packet
 *	@osize:	space available at @obuf
 *
 *	Returns the length of the compressed packet, or 0 if the
 *	packet is incompressible.
 */
static int z_compress(void *arg, unsigned char *rptr, unsigned char *obuf,
	       int isize, int osize)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;
	int r, proto, off, olen, oavail;
	unsigned char *wptr;

	/*
	 * Check that the protocol is in the range we handle.
	 */
	proto = PPP_PROTOCOL(rptr);
	if (proto > 0x3fff || proto == 0xfd || proto == 0xfb)
		return 0;

	/* Don't generate compressed packets which are larger than
	   the uncompressed packet. */
	if (osize > isize)
		osize = isize;

	wptr = obuf;

	/*
	 * Copy over the PPP header and store the 2-byte sequence number.
	 */
	wptr[0] = PPP_ADDRESS(rptr);
	wptr[1] = PPP_CONTROL(rptr);
	wptr[2] = PPP_COMP >> 8;
	wptr[3] = PPP_COMP;
	wptr += PPP_HDRLEN;
	wptr[0] = state->seqno >> 8;
	wptr[1] = state->seqno;
	wptr += DEFLATE_OVHD;
	olen = PPP_HDRLEN + DEFLATE_OVHD;
	state->strm.next_out = wptr;
	state->strm.avail_out = oavail = osize - olen;
	++state->seqno;

	off = (proto > 0xff) ? 2 : 3;	/* skip 1st proto byte if 0 */
	rptr += off;
	state->strm.next_in = rptr;
	state->strm.avail_in = (isize - off);

	for (;;) {
		r = zlib_deflate(&state->strm, Z_PACKET_FLUSH);
		if (r != Z_OK) {
			if (state->debug)
				printk(KERN_ERR
				       "z_compress: deflate returned %d\n", r);
			break;
		}
		if (state->strm.avail_out == 0) {
			olen += oavail;
			state->strm.next_out = NULL;
			state->strm.avail_out = oavail = 1000000;
		} else {
			break;		/* all done */
		}
	}
	olen += oavail - state->strm.avail_out;

	/*
	 * See if we managed to reduce the size of the packet.
	 */
	if (olen < isize) {
		state->stats.comp_bytes += olen;
		state->stats.comp_packets++;
	} else {
		state->stats.inc_bytes += isize;
		state->stats.inc_packets++;
		olen = 0;
	}
	state->stats.unc_bytes += isize;
	state->stats.unc_packets++;

	return olen;
}

/**
 *	z_comp_stats - return compression statistics for a compressor
 *		or decompressor.
 *	@arg:	pointer to private space for the (de)compressor
 *	@stats:	pointer to a struct compstat to receive the result.
 */
static void z_comp_stats(void *arg, struct compstat *stats)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	*stats = state->stats;
}

/**
 *	z_decomp_free - Free the memory used by a decompressor.
 *	@arg:	pointer to private space for the decompressor.
 */
static void z_decomp_free(void *arg)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	if (state) {
		zlib_inflateEnd(&state->strm);
		kfree(state->strm.workspace);
		kfree(state);
	}
}

/**
 *	z_decomp_alloc - allocate space for a decompressor.
 *	@options: pointer to CCP option data
 *	@opt_len: length of the CCP option at @options.
 *
 *	The @options pointer points to the a buffer containing the
 *	CCP option data for the compression being negotiated.  It is
 *	formatted according to RFC1979, and describes the window
 *	size that we are requesting the peer to use in compressing
 *	data to be sent to us.
 *
 *	Returns the pointer to the private state for the decompressor,
 *	or NULL if we could not allocate enough memory.
 */
static void *z_decomp_alloc(unsigned char *options, int opt_len)
{
	struct ppp_deflate_state *state;
	int w_size;

	if (opt_len != CILEN_DEFLATE ||
	    (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT) ||
	    options[1] != CILEN_DEFLATE ||
	    DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL ||
	    options[3] != DEFLATE_CHK_SEQUENCE)
		return NULL;
	w_size = DEFLATE_SIZE(options[2]);
	if (w_size < DEFLATE_MIN_SIZE || w_size > DEFLATE_MAX_SIZE)
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->w_size         = w_size;
	state->strm.next_out  = NULL;
	state->strm.workspace = kmalloc(zlib_inflate_workspacesize(),
					GFP_KERNEL|__GFP_REPEAT);
	if (state->strm.workspace == NULL)
		goto out_free;

	if (zlib_inflateInit2(&state->strm, -w_size) != Z_OK)
		goto out_free;
	return (void *) state;

out_free:
	z_decomp_free(state);
	return NULL;
}

/**
 *	z_decomp_init - initialize a previously-allocated decompressor.
 *	@arg:	pointer to the private state for the decompressor
 *	@options: pointer to the CCP option data describing the
 *		compression that was negotiated with the peer
 *	@opt_len: length of the CCP option data at @options
 *	@unit:	PPP unit number for diagnostic messages
 *	@hdrlen: ignored (present for backwards compatibility)
 *	@mru:	maximum length of decompressed packets
 *	@debug:	debug flag; if non-zero, debug messages are printed.
 *
 *	The CCP options described by @options must match the options
 *	specified when the decompressor was allocated.  The decompressor
 *	history is reset.  Returns 0 for failure (CCP options don't
 *	match) or 1 for success.
 */
static int z_decomp_init(void *arg, unsigned char *options, int opt_len,
			 int unit, int hdrlen, int mru, int debug)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	if (opt_len < CILEN_DEFLATE ||
	    (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT) ||
	    options[1] != CILEN_DEFLATE ||
	    DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL ||
	    DEFLATE_SIZE(options[2]) != state->w_size ||
	    options[3] != DEFLATE_CHK_SEQUENCE)
		return 0;

	state->seqno = 0;
	state->unit  = unit;
	state->debug = debug;
	state->mru   = mru;

	zlib_inflateReset(&state->strm);

	return 1;
}

/**
 *	z_decomp_reset - reset a previously-allocated decompressor.
 *	@arg:	pointer to private state for the decompressor.
 *
 *	This clears the history for the decompressor and makes it
 *	ready to receive a new compressed stream.
 */
static void z_decomp_reset(void *arg)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;

	state->seqno = 0;
	zlib_inflateReset(&state->strm);
}

/**
 *	z_decompress - decompress a Deflate-compressed packet.
 *	@arg:	pointer to private state for the decompressor
 *	@ibuf:	pointer to input (compressed) packet data
 *	@isize:	length of input packet
 *	@obuf:	pointer to space for output (decompressed) packet
 *	@osize:	amount of space available at @obuf
 *
 * Because of patent problems, we return DECOMP_ERROR for errors
 * found by inspecting the input data and for system problems, but
 * DECOMP_FATALERROR for any errors which could possibly be said to
 * be being detected "after" decompression.  For DECOMP_ERROR,
 * we can issue a CCP reset-request; for DECOMP_FATALERROR, we may be
 * infringing a patent of Motorola's if we do, so we take CCP down
 * instead.
 *
 * Given that the frame has the correct sequence number and a good FCS,
 * errors such as invalid codes in the input most likely indicate a
 * bug, so we return DECOMP_FATALERROR for them in order to turn off
 * compression, even though they are detected by inspecting the input.
 */
static int z_decompress(void *arg, unsigned char *ibuf, int isize,
		 unsigned char *obuf, int osize)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;
	int olen, seq, r;
	int decode_proto, overflow;
	unsigned char overflow_buf[1];

	if (isize <= PPP_HDRLEN + DEFLATE_OVHD) {
		if (state->debug)
			printk(KERN_DEBUG "z_decompress%d: short pkt (%d)\n",
			       state->unit, isize);
		return DECOMP_ERROR;
	}

	/* Check the sequence number. */
	seq = (ibuf[PPP_HDRLEN] << 8) + ibuf[PPP_HDRLEN+1];
	if (seq != (state->seqno & 0xffff)) {
		if (state->debug)
			printk(KERN_DEBUG "z_decompress%d: bad seq # %d, expected %d\n",
			       state->unit, seq, state->seqno & 0xffff);
		return DECOMP_ERROR;
	}
	++state->seqno;

	/*
	 * Fill in the first part of the PPP header.  The protocol field
	 * comes from the decompressed data.
	 */
	obuf[0] = PPP_ADDRESS(ibuf);
	obuf[1] = PPP_CONTROL(ibuf);
	obuf[2] = 0;

	/*
	 * Set up to call inflate.  We set avail_out to 1 initially so we can
	 * look at the first byte of the output and decide whether we have
	 * a 1-byte or 2-byte protocol field.
	 */
	state->strm.next_in = ibuf + PPP_HDRLEN + DEFLATE_OVHD;
	state->strm.avail_in = isize - (PPP_HDRLEN + DEFLATE_OVHD);
	state->strm.next_out = obuf + 3;
	state->strm.avail_out = 1;
	decode_proto = 1;
	overflow = 0;

	/*
	 * Call inflate, supplying more input or output as needed.
	 */
	for (;;) {
		r = zlib_inflate(&state->strm, Z_PACKET_FLUSH);
		if (r != Z_OK) {
			if (state->debug)
				printk(KERN_DEBUG "z_decompress%d: inflate returned %d (%s)\n",
				       state->unit, r, (state->strm.msg? state->strm.msg: ""));
			return DECOMP_FATALERROR;
		}
		if (state->strm.avail_out != 0)
			break;		/* all done */
		if (decode_proto) {
			state->strm.avail_out = osize - PPP_HDRLEN;
			if ((obuf[3] & 1) == 0) {
				/* 2-byte protocol field */
				obuf[2] = obuf[3];
				--state->strm.next_out;
				++state->strm.avail_out;
			}
			decode_proto = 0;
		} else if (!overflow) {
			/*
			 * We've filled up the output buffer; the only way to
			 * find out whether inflate has any more characters
			 * left is to give it another byte of output space.
			 */
			state->strm.next_out = overflow_buf;
			state->strm.avail_out = 1;
			overflow = 1;
		} else {
			if (state->debug)
				printk(KERN_DEBUG "z_decompress%d: ran out of mru\n",
				       state->unit);
			return DECOMP_FATALERROR;
		}
	}

	if (decode_proto) {
		if (state->debug)
			printk(KERN_DEBUG "z_decompress%d: didn't get proto\n",
			       state->unit);
		return DECOMP_ERROR;
	}

	olen = osize + overflow - state->strm.avail_out;
	state->stats.unc_bytes += olen;
	state->stats.unc_packets++;
	state->stats.comp_bytes += isize;
	state->stats.comp_packets++;

	return olen;
}

/**
 *	z_incomp - add incompressible input data to the history.
 *	@arg:	pointer to private state for the decompressor
 *	@ibuf:	pointer to input packet data
 *	@icnt:	length of input data.
 */
static void z_incomp(void *arg, unsigned char *ibuf, int icnt)
{
	struct ppp_deflate_state *state = (struct ppp_deflate_state *) arg;
	int proto, r;

	/*
	 * Check that the protocol is one we handle.
	 */
	proto = PPP_PROTOCOL(ibuf);
	if (proto > 0x3fff || proto == 0xfd || proto == 0xfb)
		return;

	++state->seqno;

	/*
	 * We start at the either the 1st or 2nd byte of the protocol field,
	 * depending on whether the protocol value is compressible.
	 */
	state->strm.next_in = ibuf + 3;
	state->strm.avail_in = icnt - 3;
	if (proto > 0xff) {
		--state->strm.next_in;
		++state->strm.avail_in;
	}

	r = zlib_inflateIncomp(&state->strm);
	if (r != Z_OK) {
		/* gak! */
		if (state->debug) {
			printk(KERN_DEBUG "z_incomp%d: inflateIncomp returned %d (%s)\n",
			       state->unit, r, (state->strm.msg? state->strm.msg: ""));
		}
		return;
	}

	/*
	 * Update stats.
	 */
	state->stats.inc_bytes += icnt;
	state->stats.inc_packets++;
	state->stats.unc_bytes += icnt;
	state->stats.unc_packets++;
}

/*************************************************************
 * Module interface table
 *************************************************************/

/* These are in ppp_generic.c */
extern int  ppp_register_compressor   (struct compressor *cp);
extern void ppp_unregister_compressor (struct compressor *cp);

/*
 * Procedures exported to if_ppp.c.
 */
static struct compressor ppp_deflate = {
	.compress_proto =	CI_DEFLATE,
	.comp_alloc =		z_comp_alloc,
	.comp_free =		z_comp_free,
	.comp_init =		z_comp_init,
	.comp_reset =		z_comp_reset,
	.compress =		z_compress,
	.comp_stat =		z_comp_stats,
	.decomp_alloc =		z_decomp_alloc,
	.decomp_free =		z_decomp_free,
	.decomp_init =		z_decomp_init,
	.decomp_reset =		z_decomp_reset,
	.decompress =		z_decompress,
	.incomp =		z_incomp,
	.decomp_stat =		z_comp_stats,
	.owner =		THIS_MODULE
};

static struct compressor ppp_deflate_draft = {
	.compress_proto =	CI_DEFLATE_DRAFT,
	.comp_alloc =		z_comp_alloc,
	.comp_free =		z_comp_free,
	.comp_init =		z_comp_init,
	.comp_reset =		z_comp_reset,
	.compress =		z_compress,
	.comp_stat =		z_comp_stats,
	.decomp_alloc =		z_decomp_alloc,
	.decomp_free =		z_decomp_free,
	.decomp_init =		z_decomp_init,
	.decomp_reset =		z_decomp_reset,
	.decompress =		z_decompress,
	.incomp =		z_incomp,
	.decomp_stat =		z_comp_stats,
	.owner =		THIS_MODULE
};

static int __init deflate_init(void)
{
        int answer = ppp_register_compressor(&ppp_deflate);
        if (answer == 0)
                printk(KERN_INFO
		       "PPP Deflate Compression module registered\n");
	ppp_register_compressor(&ppp_deflate_draft);
        return answer;
}

static void __exit deflate_cleanup(void)
{
	ppp_unregister_compressor(&ppp_deflate);
	ppp_unregister_compressor(&ppp_deflate_draft);
}

module_init(deflate_init);
module_exit(deflate_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("ppp-compress-" __stringify(CI_DEFLATE));
MODULE_ALIAS("ppp-compress-" __stringify(CI_DEFLATE_DRAFT));

/* vi: set sw=4 ts=4: */
/*
 * uncompress for busybox -- (c) 2002 Robert Griebl
 *
 * based on the original compress42.c source
 * (see disclaimer below)
 */
/* (N)compress42.c - File compression ala IEEE Computer, Mar 1992.
 *
 * Authors:
 *   Spencer W. Thomas   (decvax!harpo!utah-cs!utah-gr!thomas)
 *   Jim McKie           (decvax!mcvax!jim)
 *   Steve Davies        (decvax!vax135!petsd!peora!srd)
 *   Ken Turkowski       (decvax!decwrl!turtlevax!ken)
 *   James A. Woods      (decvax!ihnp4!ames!jaw)
 *   Joe Orost           (decvax!vax135!petsd!joe)
 *   Dave Mack           (csu@alembic.acs.com)
 *   Peter Jannesen, Network Communication Systems
 *                       (peter@ncs.nl)
 *
 * marc@suse.de : a small security fix for a buffer overflow
 *
 * [... History snipped ...]
 */
#include "libbb.h"
#include "bb_archive.h"


/* Default input buffer size */
#define IBUFSIZ 2048

/* Default output buffer size */
#define OBUFSIZ 2048

/* Defines for third byte of header */
#define BIT_MASK        0x1f    /* Mask for 'number of compresssion bits'       */
                                /* Masks 0x20 and 0x40 are free.                */
                                /* I think 0x20 should mean that there is       */
                                /* a fourth header byte (for expansion).        */
#define BLOCK_MODE      0x80    /* Block compression if table is full and       */
                                /* compression rate is dropping flush tables    */
                                /* the next two codes should not be changed lightly, as they must not   */
                                /* lie within the contiguous general code space.                        */
#define FIRST   257     /* first free entry */
#define CLEAR   256     /* table clear output code */

#define INIT_BITS 9     /* initial number of bits/code */


/* machine variants which require cc -Dmachine:  pdp11, z8000, DOS */
#define HBITS      17   /* 50% occupancy */
#define HSIZE      (1<<HBITS)
#define HMASK      (HSIZE-1)    /* unused */
#define HPRIME     9941         /* unused */
#define BITS       16
#define BITS_STR   "16"
#undef  MAXSEG_64K              /* unused */
#define MAXCODE(n) (1L << (n))

#define htabof(i)               htab[i]
#define codetabof(i)            codetab[i]
#define tab_prefixof(i)         codetabof(i)
#define tab_suffixof(i)         ((unsigned char *)(htab))[i]
#define de_stack                ((unsigned char *)&(htab[HSIZE-1]))
#define clear_tab_prefixof()    memset(codetab, 0, 256)

/*
 * Decompress stdin to stdout.  This routine adapts to the codes in the
 * file building the "string" table on-the-fly; requiring no table to
 * be stored in the compressed file.
 */

IF_DESKTOP(long long) int FAST_FUNC
unpack_Z_stream(transformer_state_t *xstate)
{
	IF_DESKTOP(long long total_written = 0;)
	IF_DESKTOP(long long) int retval = -1;
	unsigned char *stackp;
	int finchar;
	long oldcode;
	long incode;
	int inbits;
	int posbits;
	int outpos;
	int insize;
	int bitmask;
	long free_ent;
	long maxcode;
	long maxmaxcode;
	int n_bits;
	int rsize = 0;
	unsigned char *inbuf; /* were eating insane amounts of stack - */
	unsigned char *outbuf; /* bad for some embedded targets */
	unsigned char *htab;
	unsigned short *codetab;

	/* Hmm, these were statics - why?! */
	/* user settable max # bits/code */
	int maxbits; /* = BITS; */
	/* block compress mode -C compatible with 2.0 */
	int block_mode; /* = BLOCK_MODE; */

	if (check_signature16(xstate, COMPRESS_MAGIC))
		return -1;

	inbuf = xzalloc(IBUFSIZ + 64);
	outbuf = xzalloc(OBUFSIZ + 2048);
	htab = xzalloc(HSIZE);  /* wasn't zeroed out before, maybe can xmalloc? */
	codetab = xzalloc(HSIZE * sizeof(codetab[0]));

	insize = 0;

	/* xread isn't good here, we have to return - caller may want
	 * to do some cleanup (e.g. delete incomplete unpacked file etc) */
	if (full_read(xstate->src_fd, inbuf, 1) != 1) {
		bb_error_msg("short read");
		goto err;
	}

	maxbits = inbuf[0] & BIT_MASK;
	block_mode = inbuf[0] & BLOCK_MODE;
	maxmaxcode = MAXCODE(maxbits);

	if (maxbits > BITS) {
		bb_error_msg("compressed with %d bits, can only handle "
				BITS_STR" bits", maxbits);
		goto err;
	}

	n_bits = INIT_BITS;
	maxcode = MAXCODE(INIT_BITS) - 1;
	bitmask = (1 << INIT_BITS) - 1;
	oldcode = -1;
	finchar = 0;
	outpos = 0;
	posbits = 0 << 3;

	free_ent = ((block_mode) ? FIRST : 256);

	/* As above, initialize the first 256 entries in the table. */
	/*clear_tab_prefixof(); - done by xzalloc */

	{
		int i;
		for (i = 255; i >= 0; --i)
			tab_suffixof(i) = (unsigned char) i;
	}

	do {
 resetbuf:
		{
			int i;
			int e;
			int o;

			o = posbits >> 3;
			e = insize - o;

			for (i = 0; i < e; ++i)
				inbuf[i] = inbuf[i + o];

			insize = e;
			posbits = 0;
		}

		if (insize < (int) (IBUFSIZ + 64) - IBUFSIZ) {
			rsize = safe_read(xstate->src_fd, inbuf + insize, IBUFSIZ);
			if (rsize < 0)
				bb_error_msg_and_die(bb_msg_read_error);
			insize += rsize;
		}

		inbits = ((rsize > 0) ? (insize - insize % n_bits) << 3 :
				  (insize << 3) - (n_bits - 1));

		while (inbits > posbits) {
			long code;

			if (free_ent > maxcode) {
				posbits =
					((posbits - 1) +
					 ((n_bits << 3) -
					  (posbits - 1 + (n_bits << 3)) % (n_bits << 3)));
				++n_bits;
				if (n_bits == maxbits) {
					maxcode = maxmaxcode;
				} else {
					maxcode = MAXCODE(n_bits) - 1;
				}
				bitmask = (1 << n_bits) - 1;
				goto resetbuf;
			}
			{
				unsigned char *p = &inbuf[posbits >> 3];
				code = ((p[0]
					| ((long) (p[1]) << 8)
					| ((long) (p[2]) << 16)) >> (posbits & 0x7)) & bitmask;
			}
			posbits += n_bits;

			if (oldcode == -1) {
				if (code >= 256)
					bb_error_msg_and_die("corrupted data"); /* %ld", code); */
				oldcode = code;
				finchar = (int) oldcode;
				outbuf[outpos++] = (unsigned char) finchar;
				continue;
			}

			if (code == CLEAR && block_mode) {
				clear_tab_prefixof();
				free_ent = FIRST - 1;
				posbits =
					((posbits - 1) +
					 ((n_bits << 3) -
					  (posbits - 1 + (n_bits << 3)) % (n_bits << 3)));
				n_bits = INIT_BITS;
				maxcode = MAXCODE(INIT_BITS) - 1;
				bitmask = (1 << INIT_BITS) - 1;
				goto resetbuf;
			}

			incode = code;
			stackp = de_stack;

			/* Special case for KwKwK string. */
			if (code >= free_ent) {
				if (code > free_ent) {
/*
					unsigned char *p;

					posbits -= n_bits;
					p = &inbuf[posbits >> 3];
					bb_error_msg
						("insize:%d posbits:%d inbuf:%02X %02X %02X %02X %02X (%d)",
						insize, posbits, p[-1], p[0], p[1], p[2], p[3],
						(posbits & 07));
*/
					bb_error_msg("corrupted data");
					goto err;
				}

				*--stackp = (unsigned char) finchar;
				code = oldcode;
			}

			/* Generate output characters in reverse order */
			while (code >= 256) {
				if (stackp <= &htabof(0))
					bb_error_msg_and_die("corrupted data");
				*--stackp = tab_suffixof(code);
				code = tab_prefixof(code);
			}

			finchar = tab_suffixof(code);
			*--stackp = (unsigned char) finchar;

			/* And put them out in forward order */
			{
				int i;

				i = de_stack - stackp;
				if (outpos + i >= OBUFSIZ) {
					do {
						if (i > OBUFSIZ - outpos) {
							i = OBUFSIZ - outpos;
						}

						if (i > 0) {
							memcpy(outbuf + outpos, stackp, i);
							outpos += i;
						}

						if (outpos >= OBUFSIZ) {
							xtransformer_write(xstate, outbuf, outpos);
							IF_DESKTOP(total_written += outpos;)
							outpos = 0;
						}
						stackp += i;
						i = de_stack - stackp;
					} while (i > 0);
				} else {
					memcpy(outbuf + outpos, stackp, i);
					outpos += i;
				}
			}

			/* Generate the new entry. */
			if (free_ent < maxmaxcode) {
				tab_prefixof(free_ent) = (unsigned short) oldcode;
				tab_suffixof(free_ent) = (unsigned char) finchar;
				free_ent++;
			}

			/* Remember previous code.  */
			oldcode = incode;
		}
	} while (rsize > 0);

	if (outpos > 0) {
		xtransformer_write(xstate, outbuf, outpos);
		IF_DESKTOP(total_written += outpos;)
	}

	retval = IF_DESKTOP(total_written) + 0;
 err:
	free(inbuf);
	free(outbuf);
	free(htab);
	free(codetab);
	return retval;
}

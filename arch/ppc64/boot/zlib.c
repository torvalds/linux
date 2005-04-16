/*
 * This file is derived from various .h and .c files from the zlib-0.95
 * distribution by Jean-loup Gailly and Mark Adler, with some additions
 * by Paul Mackerras to aid in implementing Deflate compression and
 * decompression for PPP packets.  See zlib.h for conditions of
 * distribution and use.
 *
 * Changes that have been made include:
 * - changed functions not used outside this file to "local"
 * - added minCompression parameter to deflateInit2
 * - added Z_PACKET_FLUSH (see zlib.h for details)
 * - added inflateIncomp
 *
  Copyright (C) 1995 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  gzip@prep.ai.mit.edu    madler@alumni.caltech.edu

 *
 * 
 */

/*+++++*/
/* zutil.h -- internal interface and configuration of the compression library
 * Copyright (C) 1995 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* From: zutil.h,v 1.9 1995/05/03 17:27:12 jloup Exp */

#define _Z_UTIL_H

#include "zlib.h"

#ifndef local
#  define local static
#endif
/* compile with -Dlocal if your debugger can't find static symbols */

#define FAR

typedef unsigned char  uch;
typedef uch FAR uchf;
typedef unsigned short ush;
typedef ush FAR ushf;
typedef unsigned long  ulg;

extern char *z_errmsg[]; /* indexed by 1-zlib_error */

#define ERR_RETURN(strm,err) return (strm->msg=z_errmsg[1-err], err)
/* To be used only when the state is known to be valid */

#ifndef NULL
#define NULL	((void *) 0)
#endif

        /* common constants */

#define DEFLATED   8

#ifndef DEF_WBITS
#  define DEF_WBITS MAX_WBITS
#endif
/* default windowBits for decompression. MAX_WBITS is for compression only */

#if MAX_MEM_LEVEL >= 8
#  define DEF_MEM_LEVEL 8
#else
#  define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#endif
/* default memLevel */

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES    2
/* The three kinds of block type */

#define MIN_MATCH  3
#define MAX_MATCH  258
/* The minimum and maximum match lengths */

         /* functions */

extern void *memcpy(void *, const void *, unsigned long);
#define zmemcpy memcpy

/* Diagnostic functions */
#ifdef DEBUG_ZLIB
#  include <stdio.h>
#  ifndef verbose
#    define verbose 0
#  endif
#  define Assert(cond,msg) {if(!(cond)) z_error(msg);}
#  define Trace(x) fprintf x
#  define Tracev(x) {if (verbose) fprintf x ;}
#  define Tracevv(x) {if (verbose>1) fprintf x ;}
#  define Tracec(c,x) {if (verbose && (c)) fprintf x ;}
#  define Tracecv(c,x) {if (verbose>1 && (c)) fprintf x ;}
#else
#  define Assert(cond,msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c,x)
#  define Tracecv(c,x)
#endif


typedef uLong (*check_func) OF((uLong check, Bytef *buf, uInt len));

/* voidpf zcalloc OF((voidpf opaque, unsigned items, unsigned size)); */
/* void   zcfree  OF((voidpf opaque, voidpf ptr)); */

#define ZALLOC(strm, items, size) \
           (*((strm)->zalloc))((strm)->opaque, (items), (size))
#define ZFREE(strm, addr, size)	\
	   (*((strm)->zfree))((strm)->opaque, (voidpf)(addr), (size))
#define TRY_FREE(s, p, n) {if (p) ZFREE(s, p, n);}

/* deflate.h -- internal compression state
 * Copyright (C) 1995 Jean-loup Gailly
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/*+++++*/
/* infblock.h -- header to use infblock.c
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

struct inflate_blocks_state;
typedef struct inflate_blocks_state FAR inflate_blocks_statef;

local inflate_blocks_statef * inflate_blocks_new OF((
    z_stream *z,
    check_func c,               /* check function */
    uInt w));                   /* window size */

local int inflate_blocks OF((
    inflate_blocks_statef *,
    z_stream *,
    int));                      /* initial return code */

local void inflate_blocks_reset OF((
    inflate_blocks_statef *,
    z_stream *,
    uLongf *));                  /* check value on output */

local int inflate_blocks_free OF((
    inflate_blocks_statef *,
    z_stream *,
    uLongf *));                  /* check value on output */

local int inflate_addhistory OF((
    inflate_blocks_statef *,
    z_stream *));

local int inflate_packet_flush OF((
    inflate_blocks_statef *));

/*+++++*/
/* inftrees.h -- header to use inftrees.c
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* Huffman code lookup table entry--this entry is four bytes for machines
   that have 16-bit pointers (e.g. PC's in the small or medium model). */

typedef struct inflate_huft_s FAR inflate_huft;

struct inflate_huft_s {
  union {
    struct {
      Byte Exop;        /* number of extra bits or operation */
      Byte Bits;        /* number of bits in this code or subcode */
    } what;
    uInt Nalloc;	/* number of these allocated here */
    Bytef *pad;         /* pad structure to a power of 2 (4 bytes for */
  } word;               /*  16-bit, 8 bytes for 32-bit machines) */
  union {
    uInt Base;          /* literal, length base, or distance base */
    inflate_huft *Next; /* pointer to next level of table */
  } more;
};

#ifdef DEBUG_ZLIB
  local uInt inflate_hufts;
#endif

local int inflate_trees_bits OF((
    uIntf *,                    /* 19 code lengths */
    uIntf *,                    /* bits tree desired/actual depth */
    inflate_huft * FAR *,       /* bits tree result */
    z_stream *));               /* for zalloc, zfree functions */

local int inflate_trees_dynamic OF((
    uInt,                       /* number of literal/length codes */
    uInt,                       /* number of distance codes */
    uIntf *,                    /* that many (total) code lengths */
    uIntf *,                    /* literal desired/actual bit depth */
    uIntf *,                    /* distance desired/actual bit depth */
    inflate_huft * FAR *,       /* literal/length tree result */
    inflate_huft * FAR *,       /* distance tree result */
    z_stream *));               /* for zalloc, zfree functions */

local int inflate_trees_fixed OF((
    uIntf *,                    /* literal desired/actual bit depth */
    uIntf *,                    /* distance desired/actual bit depth */
    inflate_huft * FAR *,       /* literal/length tree result */
    inflate_huft * FAR *));     /* distance tree result */

local int inflate_trees_free OF((
    inflate_huft *,             /* tables to free */
    z_stream *));               /* for zfree function */


/*+++++*/
/* infcodes.h -- header to use infcodes.c
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

struct inflate_codes_state;
typedef struct inflate_codes_state FAR inflate_codes_statef;

local inflate_codes_statef *inflate_codes_new OF((
    uInt, uInt,
    inflate_huft *, inflate_huft *,
    z_stream *));

local int inflate_codes OF((
    inflate_blocks_statef *,
    z_stream *,
    int));

local void inflate_codes_free OF((
    inflate_codes_statef *,
    z_stream *));


/*+++++*/
/* inflate.c -- zlib interface to inflate modules
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* inflate private state */
struct internal_state {

  /* mode */
  enum {
      METHOD,   /* waiting for method byte */
      FLAG,     /* waiting for flag byte */
      BLOCKS,   /* decompressing blocks */
      CHECK4,   /* four check bytes to go */
      CHECK3,   /* three check bytes to go */
      CHECK2,   /* two check bytes to go */
      CHECK1,   /* one check byte to go */
      DONE,     /* finished check, done */
      BAD}      /* got an error--stay here */
    mode;               /* current inflate mode */

  /* mode dependent information */
  union {
    uInt method;        /* if FLAGS, method byte */
    struct {
      uLong was;                /* computed check value */
      uLong need;               /* stream check value */
    } check;            /* if CHECK, check values to compare */
    uInt marker;        /* if BAD, inflateSync's marker bytes count */
  } sub;        /* submode */

  /* mode independent information */
  int  nowrap;          /* flag for no wrapper */
  uInt wbits;           /* log2(window size)  (8..15, defaults to 15) */
  inflate_blocks_statef 
    *blocks;            /* current inflate_blocks state */

};


int inflateReset(
	z_stream *z
)
{
  uLong c;

  if (z == Z_NULL || z->state == Z_NULL)
    return Z_STREAM_ERROR;
  z->total_in = z->total_out = 0;
  z->msg = Z_NULL;
  z->state->mode = z->state->nowrap ? BLOCKS : METHOD;
  inflate_blocks_reset(z->state->blocks, z, &c);
  Trace((stderr, "inflate: reset\n"));
  return Z_OK;
}


int inflateEnd(
	z_stream *z
)
{
  uLong c;

  if (z == Z_NULL || z->state == Z_NULL || z->zfree == Z_NULL)
    return Z_STREAM_ERROR;
  if (z->state->blocks != Z_NULL)
    inflate_blocks_free(z->state->blocks, z, &c);
  ZFREE(z, z->state, sizeof(struct internal_state));
  z->state = Z_NULL;
  Trace((stderr, "inflate: end\n"));
  return Z_OK;
}


int inflateInit2(
	z_stream *z,
	int w
)
{
  /* initialize state */
  if (z == Z_NULL)
    return Z_STREAM_ERROR;
/*  if (z->zalloc == Z_NULL) z->zalloc = zcalloc; */
/*  if (z->zfree == Z_NULL) z->zfree = zcfree; */
  if ((z->state = (struct internal_state FAR *)
       ZALLOC(z,1,sizeof(struct internal_state))) == Z_NULL)
    return Z_MEM_ERROR;
  z->state->blocks = Z_NULL;

  /* handle undocumented nowrap option (no zlib header or check) */
  z->state->nowrap = 0;
  if (w < 0)
  {
    w = - w;
    z->state->nowrap = 1;
  }

  /* set window size */
  if (w < 8 || w > 15)
  {
    inflateEnd(z);
    return Z_STREAM_ERROR;
  }
  z->state->wbits = (uInt)w;

  /* create inflate_blocks state */
  if ((z->state->blocks =
       inflate_blocks_new(z, z->state->nowrap ? Z_NULL : adler32, 1 << w))
      == Z_NULL)
  {
    inflateEnd(z);
    return Z_MEM_ERROR;
  }
  Trace((stderr, "inflate: allocated\n"));

  /* reset state */
  inflateReset(z);
  return Z_OK;
}


int inflateInit(
	z_stream *z
)
{
  return inflateInit2(z, DEF_WBITS);
}


#define NEEDBYTE {if(z->avail_in==0)goto empty;r=Z_OK;}
#define NEXTBYTE (z->avail_in--,z->total_in++,*z->next_in++)

int inflate(
	z_stream *z,
	int f	
)
{
  int r;
  uInt b;

  if (z == Z_NULL || z->next_in == Z_NULL)
    return Z_STREAM_ERROR;
  r = Z_BUF_ERROR;
  while (1) switch (z->state->mode)
  {
    case METHOD:
      NEEDBYTE
      if (((z->state->sub.method = NEXTBYTE) & 0xf) != DEFLATED)
      {
        z->state->mode = BAD;
        z->msg = "unknown compression method";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      if ((z->state->sub.method >> 4) + 8 > z->state->wbits)
      {
        z->state->mode = BAD;
        z->msg = "invalid window size";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      z->state->mode = FLAG;
    case FLAG:
      NEEDBYTE
      if ((b = NEXTBYTE) & 0x20)
      {
        z->state->mode = BAD;
        z->msg = "invalid reserved bit";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      if (((z->state->sub.method << 8) + b) % 31)
      {
        z->state->mode = BAD;
        z->msg = "incorrect header check";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      Trace((stderr, "inflate: zlib header ok\n"));
      z->state->mode = BLOCKS;
    case BLOCKS:
      r = inflate_blocks(z->state->blocks, z, r);
      if (f == Z_PACKET_FLUSH && z->avail_in == 0 && z->avail_out != 0)
	  r = inflate_packet_flush(z->state->blocks);
      if (r == Z_DATA_ERROR)
      {
        z->state->mode = BAD;
        z->state->sub.marker = 0;       /* can try inflateSync */
        break;
      }
      if (r != Z_STREAM_END)
        return r;
      r = Z_OK;
      inflate_blocks_reset(z->state->blocks, z, &z->state->sub.check.was);
      if (z->state->nowrap)
      {
        z->state->mode = DONE;
        break;
      }
      z->state->mode = CHECK4;
    case CHECK4:
      NEEDBYTE
      z->state->sub.check.need = (uLong)NEXTBYTE << 24;
      z->state->mode = CHECK3;
    case CHECK3:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 16;
      z->state->mode = CHECK2;
    case CHECK2:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 8;
      z->state->mode = CHECK1;
    case CHECK1:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE;

      if (z->state->sub.check.was != z->state->sub.check.need)
      {
        z->state->mode = BAD;
        z->msg = "incorrect data check";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      Trace((stderr, "inflate: zlib check ok\n"));
      z->state->mode = DONE;
    case DONE:
      return Z_STREAM_END;
    case BAD:
      return Z_DATA_ERROR;
    default:
      return Z_STREAM_ERROR;
  }

 empty:
  if (f != Z_PACKET_FLUSH)
    return r;
  z->state->mode = BAD;
  z->state->sub.marker = 0;       /* can try inflateSync */
  return Z_DATA_ERROR;
}

/*
 * This subroutine adds the data at next_in/avail_in to the output history
 * without performing any output.  The output buffer must be "caught up";
 * i.e. no pending output (hence s->read equals s->write), and the state must
 * be BLOCKS (i.e. we should be willing to see the start of a series of
 * BLOCKS).  On exit, the output will also be caught up, and the checksum
 * will have been updated if need be.
 */

int inflateIncomp(
	z_stream *z
)
{
    if (z->state->mode != BLOCKS)
	return Z_DATA_ERROR;
    return inflate_addhistory(z->state->blocks, z);
}


int inflateSync(
	z_stream *z
)
{
  uInt n;       /* number of bytes to look at */
  Bytef *p;     /* pointer to bytes */
  uInt m;       /* number of marker bytes found in a row */
  uLong r, w;   /* temporaries to save total_in and total_out */

  /* set up */
  if (z == Z_NULL || z->state == Z_NULL)
    return Z_STREAM_ERROR;
  if (z->state->mode != BAD)
  {
    z->state->mode = BAD;
    z->state->sub.marker = 0;
  }
  if ((n = z->avail_in) == 0)
    return Z_BUF_ERROR;
  p = z->next_in;
  m = z->state->sub.marker;

  /* search */
  while (n && m < 4)
  {
    if (*p == (Byte)(m < 2 ? 0 : 0xff))
      m++;
    else if (*p)
      m = 0;
    else
      m = 4 - m;
    p++, n--;
  }

  /* restore */
  z->total_in += p - z->next_in;
  z->next_in = p;
  z->avail_in = n;
  z->state->sub.marker = m;

  /* return no joy or set up to restart on a new block */
  if (m != 4)
    return Z_DATA_ERROR;
  r = z->total_in;  w = z->total_out;
  inflateReset(z);
  z->total_in = r;  z->total_out = w;
  z->state->mode = BLOCKS;
  return Z_OK;
}

#undef NEEDBYTE
#undef NEXTBYTE

/*+++++*/
/* infutil.h -- types and macros common to blocks and codes
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* inflate blocks semi-private state */
struct inflate_blocks_state {

  /* mode */
  enum {
      TYPE,     /* get type bits (3, including end bit) */
      LENS,     /* get lengths for stored */
      STORED,   /* processing stored block */
      TABLE,    /* get table lengths */
      BTREE,    /* get bit lengths tree for a dynamic block */
      DTREE,    /* get length, distance trees for a dynamic block */
      CODES,    /* processing fixed or dynamic block */
      DRY,      /* output remaining window bytes */
      DONEB,     /* finished last block, done */
      BADB}      /* got a data error--stuck here */
    mode;               /* current inflate_block mode */

  /* mode dependent information */
  union {
    uInt left;          /* if STORED, bytes left to copy */
    struct {
      uInt table;               /* table lengths (14 bits) */
      uInt index;               /* index into blens (or border) */
      uIntf *blens;             /* bit lengths of codes */
      uInt bb;                  /* bit length tree depth */
      inflate_huft *tb;         /* bit length decoding tree */
      int nblens;		/* # elements allocated at blens */
    } trees;            /* if DTREE, decoding info for trees */
    struct {
      inflate_huft *tl, *td;    /* trees to free */
      inflate_codes_statef 
         *codes;
    } decode;           /* if CODES, current state */
  } sub;                /* submode */
  uInt last;            /* true if this block is the last block */

  /* mode independent information */
  uInt bitk;            /* bits in bit buffer */
  uLong bitb;           /* bit buffer */
  Bytef *window;        /* sliding window */
  Bytef *end;           /* one byte after sliding window */
  Bytef *read;          /* window read pointer */
  Bytef *write;         /* window write pointer */
  check_func checkfn;   /* check function */
  uLong check;          /* check on output */

};


/* defines for inflate input/output */
/*   update pointers and return */
#define UPDBITS {s->bitb=b;s->bitk=k;}
#define UPDIN {z->avail_in=n;z->total_in+=p-z->next_in;z->next_in=p;}
#define UPDOUT {s->write=q;}
#define UPDATE {UPDBITS UPDIN UPDOUT}
#define LEAVE {UPDATE return inflate_flush(s,z,r);}
/*   get bytes and bits */
#define LOADIN {p=z->next_in;n=z->avail_in;b=s->bitb;k=s->bitk;}
#define NEEDBYTE {if(n)r=Z_OK;else LEAVE}
#define NEXTBYTE (n--,*p++)
#define NEEDBITS(j) {while(k<(j)){NEEDBYTE;b|=((uLong)NEXTBYTE)<<k;k+=8;}}
#define DUMPBITS(j) {b>>=(j);k-=(j);}
/*   output bytes */
#define WAVAIL (q<s->read?s->read-q-1:s->end-q)
#define LOADOUT {q=s->write;m=WAVAIL;}
#define WRAP {if(q==s->end&&s->read!=s->window){q=s->window;m=WAVAIL;}}
#define FLUSH {UPDOUT r=inflate_flush(s,z,r); LOADOUT}
#define NEEDOUT {if(m==0){WRAP if(m==0){FLUSH WRAP if(m==0) LEAVE}}r=Z_OK;}
#define OUTBYTE(a) {*q++=(Byte)(a);m--;}
/*   load local pointers */
#define LOAD {LOADIN LOADOUT}

/* And'ing with mask[n] masks the lower n bits */
local uInt inflate_mask[] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

/* copy as much as possible from the sliding window to the output area */
local int inflate_flush OF((
    inflate_blocks_statef *,
    z_stream *,
    int));

/*+++++*/
/* inffast.h -- header to use inffast.c
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

local int inflate_fast OF((
    uInt,
    uInt,
    inflate_huft *,
    inflate_huft *,
    inflate_blocks_statef *,
    z_stream *));


/*+++++*/
/* infblock.c -- interpret and process block types to last block
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* Table for deflate from PKZIP's appnote.txt. */
local uInt border[] = { /* Order of the bit length code lengths */
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/*
   Notes beyond the 1.93a appnote.txt:

   1. Distance pointers never point before the beginning of the output
      stream.
   2. Distance pointers can point back across blocks, up to 32k away.
   3. There is an implied maximum of 7 bits for the bit length table and
      15 bits for the actual data.
   4. If only one code exists, then it is encoded using one bit.  (Zero
      would be more efficient, but perhaps a little confusing.)  If two
      codes exist, they are coded using one bit each (0 and 1).
   5. There is no way of sending zero distance codes--a dummy must be
      sent if there are none.  (History: a pre 2.0 version of PKZIP would
      store blocks with no distance codes, but this was discovered to be
      too harsh a criterion.)  Valid only for 1.93a.  2.04c does allow
      zero distance codes, which is sent as one code of zero bits in
      length.
   6. There are up to 286 literal/length codes.  Code 256 represents the
      end-of-block.  Note however that the static length tree defines
      288 codes just to fill out the Huffman codes.  Codes 286 and 287
      cannot be used though, since there is no length base or extra bits
      defined for them.  Similarily, there are up to 30 distance codes.
      However, static trees define 32 codes (all 5 bits) to fill out the
      Huffman codes, but the last two had better not show up in the data.
   7. Unzip can check dynamic Huffman blocks for complete code sets.
      The exception is that a single code would not be complete (see #4).
   8. The five bits following the block type is really the number of
      literal codes sent minus 257.
   9. Length codes 8,16,16 are interpreted as 13 length codes of 8 bits
      (1+6+6).  Therefore, to output three times the length, you output
      three codes (1+1+1), whereas to output four times the same length,
      you only need two codes (1+3).  Hmm.
  10. In the tree reconstruction algorithm, Code = Code + Increment
      only if BitLength(i) is not zero.  (Pretty obvious.)
  11. Correction: 4 Bits: # of Bit Length codes - 4     (4 - 19)
  12. Note: length code 284 can represent 227-258, but length code 285
      really is 258.  The last length deserves its own, short code
      since it gets used a lot in very redundant files.  The length
      258 is special since 258 - 3 (the min match length) is 255.
  13. The literal/length and distance code bit lengths are read as a
      single stream of lengths.  It is possible (and advantageous) for
      a repeat code (16, 17, or 18) to go across the boundary between
      the two sets of lengths.
 */


local void inflate_blocks_reset(
	inflate_blocks_statef *s,
	z_stream *z,
	uLongf *c
)
{
  if (s->checkfn != Z_NULL)
    *c = s->check;
  if (s->mode == BTREE || s->mode == DTREE)
    ZFREE(z, s->sub.trees.blens, s->sub.trees.nblens * sizeof(uInt));
  if (s->mode == CODES)
  {
    inflate_codes_free(s->sub.decode.codes, z);
    inflate_trees_free(s->sub.decode.td, z);
    inflate_trees_free(s->sub.decode.tl, z);
  }
  s->mode = TYPE;
  s->bitk = 0;
  s->bitb = 0;
  s->read = s->write = s->window;
  if (s->checkfn != Z_NULL)
    s->check = (*s->checkfn)(0L, Z_NULL, 0);
  Trace((stderr, "inflate:   blocks reset\n"));
}


local inflate_blocks_statef *inflate_blocks_new(
	z_stream *z,
	check_func c,
	uInt w
)
{
  inflate_blocks_statef *s;

  if ((s = (inflate_blocks_statef *)ZALLOC
       (z,1,sizeof(struct inflate_blocks_state))) == Z_NULL)
    return s;
  if ((s->window = (Bytef *)ZALLOC(z, 1, w)) == Z_NULL)
  {
    ZFREE(z, s, sizeof(struct inflate_blocks_state));
    return Z_NULL;
  }
  s->end = s->window + w;
  s->checkfn = c;
  s->mode = TYPE;
  Trace((stderr, "inflate:   blocks allocated\n"));
  inflate_blocks_reset(s, z, &s->check);
  return s;
}


local int inflate_blocks(
	inflate_blocks_statef *s,
	z_stream *z,
	int r
)
{
  uInt t;               /* temporary storage */
  uLong b;              /* bit buffer */
  uInt k;               /* bits in bit buffer */
  Bytef *p;             /* input data pointer */
  uInt n;               /* bytes available there */
  Bytef *q;             /* output window write pointer */
  uInt m;               /* bytes to end of window or read pointer */

  /* copy input/output information to locals (UPDATE macro restores) */
  LOAD

  /* process input based on current state */
  while (1) switch (s->mode)
  {
    case TYPE:
      NEEDBITS(3)
      t = (uInt)b & 7;
      s->last = t & 1;
      switch (t >> 1)
      {
        case 0:                         /* stored */
          Trace((stderr, "inflate:     stored block%s\n",
                 s->last ? " (last)" : ""));
          DUMPBITS(3)
          t = k & 7;                    /* go to byte boundary */
          DUMPBITS(t)
          s->mode = LENS;               /* get length of stored block */
          break;
        case 1:                         /* fixed */
          Trace((stderr, "inflate:     fixed codes block%s\n",
                 s->last ? " (last)" : ""));
          {
            uInt bl, bd;
            inflate_huft *tl, *td;

            inflate_trees_fixed(&bl, &bd, &tl, &td);
            s->sub.decode.codes = inflate_codes_new(bl, bd, tl, td, z);
            if (s->sub.decode.codes == Z_NULL)
            {
              r = Z_MEM_ERROR;
              LEAVE
            }
            s->sub.decode.tl = Z_NULL;  /* don't try to free these */
            s->sub.decode.td = Z_NULL;
          }
          DUMPBITS(3)
          s->mode = CODES;
          break;
        case 2:                         /* dynamic */
          Trace((stderr, "inflate:     dynamic codes block%s\n",
                 s->last ? " (last)" : ""));
          DUMPBITS(3)
          s->mode = TABLE;
          break;
        case 3:                         /* illegal */
          DUMPBITS(3)
          s->mode = BADB;
          z->msg = "invalid block type";
          r = Z_DATA_ERROR;
          LEAVE
      }
      break;
    case LENS:
      NEEDBITS(32)
      if (((~b) >> 16) != (b & 0xffff))
      {
        s->mode = BADB;
        z->msg = "invalid stored block lengths";
        r = Z_DATA_ERROR;
        LEAVE
      }
      s->sub.left = (uInt)b & 0xffff;
      b = k = 0;                      /* dump bits */
      Tracev((stderr, "inflate:       stored length %u\n", s->sub.left));
      s->mode = s->sub.left ? STORED : TYPE;
      break;
    case STORED:
      if (n == 0)
        LEAVE
      NEEDOUT
      t = s->sub.left;
      if (t > n) t = n;
      if (t > m) t = m;
      zmemcpy(q, p, t);
      p += t;  n -= t;
      q += t;  m -= t;
      if ((s->sub.left -= t) != 0)
        break;
      Tracev((stderr, "inflate:       stored end, %lu total out\n",
              z->total_out + (q >= s->read ? q - s->read :
              (s->end - s->read) + (q - s->window))));
      s->mode = s->last ? DRY : TYPE;
      break;
    case TABLE:
      NEEDBITS(14)
      s->sub.trees.table = t = (uInt)b & 0x3fff;
#ifndef PKZIP_BUG_WORKAROUND
      if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29)
      {
        s->mode = BADB;
        z->msg = "too many length or distance symbols";
        r = Z_DATA_ERROR;
        LEAVE
      }
#endif
      t = 258 + (t & 0x1f) + ((t >> 5) & 0x1f);
      if (t < 19)
        t = 19;
      if ((s->sub.trees.blens = (uIntf*)ZALLOC(z, t, sizeof(uInt))) == Z_NULL)
      {
        r = Z_MEM_ERROR;
        LEAVE
      }
      s->sub.trees.nblens = t;
      DUMPBITS(14)
      s->sub.trees.index = 0;
      Tracev((stderr, "inflate:       table sizes ok\n"));
      s->mode = BTREE;
    case BTREE:
      while (s->sub.trees.index < 4 + (s->sub.trees.table >> 10))
      {
        NEEDBITS(3)
        s->sub.trees.blens[border[s->sub.trees.index++]] = (uInt)b & 7;
        DUMPBITS(3)
      }
      while (s->sub.trees.index < 19)
        s->sub.trees.blens[border[s->sub.trees.index++]] = 0;
      s->sub.trees.bb = 7;
      t = inflate_trees_bits(s->sub.trees.blens, &s->sub.trees.bb,
                             &s->sub.trees.tb, z);
      if (t != Z_OK)
      {
        r = t;
        if (r == Z_DATA_ERROR)
          s->mode = BADB;
        LEAVE
      }
      s->sub.trees.index = 0;
      Tracev((stderr, "inflate:       bits tree ok\n"));
      s->mode = DTREE;
    case DTREE:
      while (t = s->sub.trees.table,
             s->sub.trees.index < 258 + (t & 0x1f) + ((t >> 5) & 0x1f))
      {
        inflate_huft *h;
        uInt i, j, c;

        t = s->sub.trees.bb;
        NEEDBITS(t)
        h = s->sub.trees.tb + ((uInt)b & inflate_mask[t]);
        t = h->word.what.Bits;
        c = h->more.Base;
        if (c < 16)
        {
          DUMPBITS(t)
          s->sub.trees.blens[s->sub.trees.index++] = c;
        }
        else /* c == 16..18 */
        {
          i = c == 18 ? 7 : c - 14;
          j = c == 18 ? 11 : 3;
          NEEDBITS(t + i)
          DUMPBITS(t)
          j += (uInt)b & inflate_mask[i];
          DUMPBITS(i)
          i = s->sub.trees.index;
          t = s->sub.trees.table;
          if (i + j > 258 + (t & 0x1f) + ((t >> 5) & 0x1f) ||
              (c == 16 && i < 1))
          {
            s->mode = BADB;
            z->msg = "invalid bit length repeat";
            r = Z_DATA_ERROR;
            LEAVE
          }
          c = c == 16 ? s->sub.trees.blens[i - 1] : 0;
          do {
            s->sub.trees.blens[i++] = c;
          } while (--j);
          s->sub.trees.index = i;
        }
      }
      inflate_trees_free(s->sub.trees.tb, z);
      s->sub.trees.tb = Z_NULL;
      {
        uInt bl, bd;
        inflate_huft *tl, *td;
        inflate_codes_statef *c;

        bl = 9;         /* must be <= 9 for lookahead assumptions */
        bd = 6;         /* must be <= 9 for lookahead assumptions */
        t = s->sub.trees.table;
        t = inflate_trees_dynamic(257 + (t & 0x1f), 1 + ((t >> 5) & 0x1f),
                                  s->sub.trees.blens, &bl, &bd, &tl, &td, z);
        if (t != Z_OK)
        {
          if (t == (uInt)Z_DATA_ERROR)
            s->mode = BADB;
          r = t;
          LEAVE
        }
        Tracev((stderr, "inflate:       trees ok\n"));
        if ((c = inflate_codes_new(bl, bd, tl, td, z)) == Z_NULL)
        {
          inflate_trees_free(td, z);
          inflate_trees_free(tl, z);
          r = Z_MEM_ERROR;
          LEAVE
        }
        ZFREE(z, s->sub.trees.blens, s->sub.trees.nblens * sizeof(uInt));
        s->sub.decode.codes = c;
        s->sub.decode.tl = tl;
        s->sub.decode.td = td;
      }
      s->mode = CODES;
    case CODES:
      UPDATE
      if ((r = inflate_codes(s, z, r)) != Z_STREAM_END)
        return inflate_flush(s, z, r);
      r = Z_OK;
      inflate_codes_free(s->sub.decode.codes, z);
      inflate_trees_free(s->sub.decode.td, z);
      inflate_trees_free(s->sub.decode.tl, z);
      LOAD
      Tracev((stderr, "inflate:       codes end, %lu total out\n",
              z->total_out + (q >= s->read ? q - s->read :
              (s->end - s->read) + (q - s->window))));
      if (!s->last)
      {
        s->mode = TYPE;
        break;
      }
      if (k > 7)              /* return unused byte, if any */
      {
        Assert(k < 16, "inflate_codes grabbed too many bytes")
        k -= 8;
        n++;
        p--;                    /* can always return one */
      }
      s->mode = DRY;
    case DRY:
      FLUSH
      if (s->read != s->write)
        LEAVE
      s->mode = DONEB;
    case DONEB:
      r = Z_STREAM_END;
      LEAVE
    case BADB:
      r = Z_DATA_ERROR;
      LEAVE
    default:
      r = Z_STREAM_ERROR;
      LEAVE
  }
}


local int inflate_blocks_free(
	inflate_blocks_statef *s,
	z_stream *z,
	uLongf *c
)
{
  inflate_blocks_reset(s, z, c);
  ZFREE(z, s->window, s->end - s->window);
  ZFREE(z, s, sizeof(struct inflate_blocks_state));
  Trace((stderr, "inflate:   blocks freed\n"));
  return Z_OK;
}

/*
 * This subroutine adds the data at next_in/avail_in to the output history
 * without performing any output.  The output buffer must be "caught up";
 * i.e. no pending output (hence s->read equals s->write), and the state must
 * be BLOCKS (i.e. we should be willing to see the start of a series of
 * BLOCKS).  On exit, the output will also be caught up, and the checksum
 * will have been updated if need be.
 */
local int inflate_addhistory(
	inflate_blocks_statef *s,
	z_stream *z
)
{
    uLong b;              /* bit buffer */  /* NOT USED HERE */
    uInt k;               /* bits in bit buffer */ /* NOT USED HERE */
    uInt t;               /* temporary storage */
    Bytef *p;             /* input data pointer */
    uInt n;               /* bytes available there */
    Bytef *q;             /* output window write pointer */
    uInt m;               /* bytes to end of window or read pointer */

    if (s->read != s->write)
	return Z_STREAM_ERROR;
    if (s->mode != TYPE)
	return Z_DATA_ERROR;

    /* we're ready to rock */
    LOAD
    /* while there is input ready, copy to output buffer, moving
     * pointers as needed.
     */
    while (n) {
	t = n;  /* how many to do */
	/* is there room until end of buffer? */
	if (t > m) t = m;
	/* update check information */
	if (s->checkfn != Z_NULL)
	    s->check = (*s->checkfn)(s->check, q, t);
	zmemcpy(q, p, t);
	q += t;
	p += t;
	n -= t;
	z->total_out += t;
	s->read = q;    /* drag read pointer forward */
/*      WRAP  */ 	/* expand WRAP macro by hand to handle s->read */
	if (q == s->end) {
	    s->read = q = s->window;
	    m = WAVAIL;
	}
    }
    UPDATE
    return Z_OK;
}


/*
 * At the end of a Deflate-compressed PPP packet, we expect to have seen
 * a `stored' block type value but not the (zero) length bytes.
 */
local int inflate_packet_flush(
	inflate_blocks_statef *s
)
{
    if (s->mode != LENS)
	return Z_DATA_ERROR;
    s->mode = TYPE;
    return Z_OK;
}


/*+++++*/
/* inftrees.c -- generate Huffman trees for efficient decoding
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* simplify the use of the inflate_huft type with some defines */
#define base more.Base
#define next more.Next
#define exop word.what.Exop
#define bits word.what.Bits


local int huft_build OF((
    uIntf *,            /* code lengths in bits */
    uInt,               /* number of codes */
    uInt,               /* number of "simple" codes */
    uIntf *,            /* list of base values for non-simple codes */
    uIntf *,            /* list of extra bits for non-simple codes */
    inflate_huft * FAR*,/* result: starting table */
    uIntf *,            /* maximum lookup bits (returns actual) */
    z_stream *));       /* for zalloc function */

local voidpf falloc OF((
    voidpf,             /* opaque pointer (not used) */
    uInt,               /* number of items */
    uInt));             /* size of item */

local void ffree OF((
    voidpf q,           /* opaque pointer (not used) */
    voidpf p,           /* what to free (not used) */
    uInt n));		/* number of bytes (not used) */

/* Tables for deflate from PKZIP's appnote.txt. */
local uInt cplens[] = { /* Copy lengths for literal codes 257..285 */
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
        /* actually lengths - 2; also see note #13 above about 258 */
local uInt cplext[] = { /* Extra bits for literal codes 257..285 */
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 192, 192}; /* 192==invalid */
local uInt cpdist[] = { /* Copy offsets for distance codes 0..29 */
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
local uInt cpdext[] = { /* Extra bits for distance codes */
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
        12, 12, 13, 13};

/*
   Huffman code decoding is performed using a multi-level table lookup.
   The fastest way to decode is to simply build a lookup table whose
   size is determined by the longest code.  However, the time it takes
   to build this table can also be a factor if the data being decoded
   is not very long.  The most common codes are necessarily the
   shortest codes, so those codes dominate the decoding time, and hence
   the speed.  The idea is you can have a shorter table that decodes the
   shorter, more probable codes, and then point to subsidiary tables for
   the longer codes.  The time it costs to decode the longer codes is
   then traded against the time it takes to make longer tables.

   This results of this trade are in the variables lbits and dbits
   below.  lbits is the number of bits the first level table for literal/
   length codes can decode in one step, and dbits is the same thing for
   the distance codes.  Subsequent tables are also less than or equal to
   those sizes.  These values may be adjusted either when all of the
   codes are shorter than that, in which case the longest code length in
   bits is used, or when the shortest code is *longer* than the requested
   table size, in which case the length of the shortest code in bits is
   used.

   There are two different values for the two tables, since they code a
   different number of possibilities each.  The literal/length table
   codes 286 possible values, or in a flat code, a little over eight
   bits.  The distance table codes 30 possible values, or a little less
   than five bits, flat.  The optimum values for speed end up being
   about one bit more than those, so lbits is 8+1 and dbits is 5+1.
   The optimum values may differ though from machine to machine, and
   possibly even between compilers.  Your mileage may vary.
 */


/* If BMAX needs to be larger than 16, then h and x[] should be uLong. */
#define BMAX 15         /* maximum bit length of any code */
#define N_MAX 288       /* maximum number of codes in any set */

#ifdef DEBUG_ZLIB
  uInt inflate_hufts;
#endif

local int huft_build(
	uIntf *b,               /* code lengths in bits (all assumed <= BMAX) */
	uInt n,                 /* number of codes (assumed <= N_MAX) */
	uInt s,                 /* number of simple-valued codes (0..s-1) */
	uIntf *d,               /* list of base values for non-simple codes */
	uIntf *e,               /* list of extra bits for non-simple codes */
	inflate_huft * FAR *t,  /* result: starting table */
	uIntf *m,               /* maximum lookup bits, returns actual */
	z_stream *zs            /* for zalloc function */
)
/* Given a list of code lengths and a maximum table size, make a set of
   tables to decode that set of codes.  Return Z_OK on success, Z_BUF_ERROR
   if the given code set is incomplete (the tables are still built in this
   case), Z_DATA_ERROR if the input is invalid (all zero length codes or an
   over-subscribed set of lengths), or Z_MEM_ERROR if not enough memory. */
{

  uInt a;                       /* counter for codes of length k */
  uInt c[BMAX+1];               /* bit length count table */
  uInt f;                       /* i repeats in table every f entries */
  int g;                        /* maximum code length */
  int h;                        /* table level */
  register uInt i;              /* counter, current code */
  register uInt j;              /* counter */
  register int k;               /* number of bits in current code */
  int l;                        /* bits per table (returned in m) */
  register uIntf *p;            /* pointer into c[], b[], or v[] */
  inflate_huft *q;              /* points to current table */
  struct inflate_huft_s r;      /* table entry for structure assignment */
  inflate_huft *u[BMAX];        /* table stack */
  uInt v[N_MAX];                /* values in order of bit length */
  register int w;               /* bits before this table == (l * h) */
  uInt x[BMAX+1];               /* bit offsets, then code stack */
  uIntf *xp;                    /* pointer into x */
  int y;                        /* number of dummy codes added */
  uInt z;                       /* number of entries in current table */


  /* Generate counts for each bit length */
  p = c;
#define C0 *p++ = 0;
#define C2 C0 C0 C0 C0
#define C4 C2 C2 C2 C2
  C4                            /* clear c[]--assume BMAX+1 is 16 */
  p = b;  i = n;
  do {
    c[*p++]++;                  /* assume all entries <= BMAX */
  } while (--i);
  if (c[0] == n)                /* null input--all zero length codes */
  {
    *t = (inflate_huft *)Z_NULL;
    *m = 0;
    return Z_OK;
  }


  /* Find minimum and maximum length, bound *m by those */
  l = *m;
  for (j = 1; j <= BMAX; j++)
    if (c[j])
      break;
  k = j;                        /* minimum code length */
  if ((uInt)l < j)
    l = j;
  for (i = BMAX; i; i--)
    if (c[i])
      break;
  g = i;                        /* maximum code length */
  if ((uInt)l > i)
    l = i;
  *m = l;


  /* Adjust last length count to fill out codes, if needed */
  for (y = 1 << j; j < i; j++, y <<= 1)
    if ((y -= c[j]) < 0)
      return Z_DATA_ERROR;
  if ((y -= c[i]) < 0)
    return Z_DATA_ERROR;
  c[i] += y;


  /* Generate starting offsets into the value table for each length */
  x[1] = j = 0;
  p = c + 1;  xp = x + 2;
  while (--i) {                 /* note that i == g from above */
    *xp++ = (j += *p++);
  }


  /* Make a table of values in order of bit lengths */
  p = b;  i = 0;
  do {
    if ((j = *p++) != 0)
      v[x[j]++] = i;
  } while (++i < n);


  /* Generate the Huffman codes and for each, make the table entries */
  x[0] = i = 0;                 /* first Huffman code is zero */
  p = v;                        /* grab values in bit order */
  h = -1;                       /* no tables yet--level -1 */
  w = -l;                       /* bits decoded == (l * h) */
  u[0] = (inflate_huft *)Z_NULL;        /* just to keep compilers happy */
  q = (inflate_huft *)Z_NULL;   /* ditto */
  z = 0;                        /* ditto */

  /* go through the bit lengths (k already is bits in shortest code) */
  for (; k <= g; k++)
  {
    a = c[k];
    while (a--)
    {
      /* here i is the Huffman code of length k bits for value *p */
      /* make tables up to required level */
      while (k > w + l)
      {
        h++;
        w += l;                 /* previous table always l bits */

        /* compute minimum size table less than or equal to l bits */
        z = (z = g - w) > (uInt)l ? l : z;      /* table size upper limit */
        if ((f = 1 << (j = k - w)) > a + 1)     /* try a k-w bit table */
        {                       /* too few codes for k-w bit table */
          f -= a + 1;           /* deduct codes from patterns left */
          xp = c + k;
          if (j < z)
            while (++j < z)     /* try smaller tables up to z bits */
            {
              if ((f <<= 1) <= *++xp)
                break;          /* enough codes to use up j bits */
              f -= *xp;         /* else deduct codes from patterns */
            }
        }
        z = 1 << j;             /* table entries for j-bit table */

        /* allocate and link in new table */
        if ((q = (inflate_huft *)ZALLOC
             (zs,z + 1,sizeof(inflate_huft))) == Z_NULL)
        {
          if (h)
            inflate_trees_free(u[0], zs);
          return Z_MEM_ERROR;   /* not enough memory */
        }
	q->word.Nalloc = z + 1;
#ifdef DEBUG_ZLIB
        inflate_hufts += z + 1;
#endif
        *t = q + 1;             /* link to list for huft_free() */
        *(t = &(q->next)) = Z_NULL;
        u[h] = ++q;             /* table starts after link */

        /* connect to last table, if there is one */
        if (h)
        {
          x[h] = i;             /* save pattern for backing up */
          r.bits = (Byte)l;     /* bits to dump before this table */
          r.exop = (Byte)j;     /* bits in this table */
          r.next = q;           /* pointer to this table */
          j = i >> (w - l);     /* (get around Turbo C bug) */
          u[h-1][j] = r;        /* connect to last table */
        }
      }

      /* set up table entry in r */
      r.bits = (Byte)(k - w);
      if (p >= v + n)
        r.exop = 128 + 64;      /* out of values--invalid code */
      else if (*p < s)
      {
        r.exop = (Byte)(*p < 256 ? 0 : 32 + 64);     /* 256 is end-of-block */
        r.base = *p++;          /* simple code is just the value */
      }
      else
      {
        r.exop = (Byte)e[*p - s] + 16 + 64; /* non-simple--look up in lists */
        r.base = d[*p++ - s];
      }

      /* fill code-like entries with r */
      f = 1 << (k - w);
      for (j = i >> w; j < z; j += f)
        q[j] = r;

      /* backwards increment the k-bit code i */
      for (j = 1 << (k - 1); i & j; j >>= 1)
        i ^= j;
      i ^= j;

      /* backup over finished tables */
      while ((i & ((1 << w) - 1)) != x[h])
      {
        h--;                    /* don't need to update q */
        w -= l;
      }
    }
  }


  /* Return Z_BUF_ERROR if we were given an incomplete table */
  return y != 0 && g != 1 ? Z_BUF_ERROR : Z_OK;
}


local int inflate_trees_bits(
	uIntf *c,               /* 19 code lengths */
	uIntf *bb,              /* bits tree desired/actual depth */
	inflate_huft * FAR *tb, /* bits tree result */
	z_stream *z             /* for zfree function */
)
{
  int r;

  r = huft_build(c, 19, 19, (uIntf*)Z_NULL, (uIntf*)Z_NULL, tb, bb, z);
  if (r == Z_DATA_ERROR)
    z->msg = "oversubscribed dynamic bit lengths tree";
  else if (r == Z_BUF_ERROR)
  {
    inflate_trees_free(*tb, z);
    z->msg = "incomplete dynamic bit lengths tree";
    r = Z_DATA_ERROR;
  }
  return r;
}


local int inflate_trees_dynamic(
	uInt nl,                /* number of literal/length codes */
	uInt nd,                /* number of distance codes */
	uIntf *c,               /* that many (total) code lengths */
	uIntf *bl,              /* literal desired/actual bit depth */
	uIntf *bd,              /* distance desired/actual bit depth */
	inflate_huft * FAR *tl, /* literal/length tree result */
	inflate_huft * FAR *td, /* distance tree result */
	z_stream *z             /* for zfree function */
)
{
  int r;

  /* build literal/length tree */
  if ((r = huft_build(c, nl, 257, cplens, cplext, tl, bl, z)) != Z_OK)
  {
    if (r == Z_DATA_ERROR)
      z->msg = "oversubscribed literal/length tree";
    else if (r == Z_BUF_ERROR)
    {
      inflate_trees_free(*tl, z);
      z->msg = "incomplete literal/length tree";
      r = Z_DATA_ERROR;
    }
    return r;
  }

  /* build distance tree */
  if ((r = huft_build(c + nl, nd, 0, cpdist, cpdext, td, bd, z)) != Z_OK)
  {
    if (r == Z_DATA_ERROR)
      z->msg = "oversubscribed literal/length tree";
    else if (r == Z_BUF_ERROR) {
#ifdef PKZIP_BUG_WORKAROUND
      r = Z_OK;
    }
#else
      inflate_trees_free(*td, z);
      z->msg = "incomplete literal/length tree";
      r = Z_DATA_ERROR;
    }
    inflate_trees_free(*tl, z);
    return r;
#endif
  }

  /* done */
  return Z_OK;
}


/* build fixed tables only once--keep them here */
local int fixed_lock = 0;
local int fixed_built = 0;
#define FIXEDH 530      /* number of hufts used by fixed tables */
local uInt fixed_left = FIXEDH;
local inflate_huft fixed_mem[FIXEDH];
local uInt fixed_bl;
local uInt fixed_bd;
local inflate_huft *fixed_tl;
local inflate_huft *fixed_td;


local voidpf falloc(
	voidpf q,       /* opaque pointer (not used) */
	uInt n,         /* number of items */
	uInt s          /* size of item */
)
{
  Assert(s == sizeof(inflate_huft) && n <= fixed_left,
         "inflate_trees falloc overflow");
  if (q) s++; /* to make some compilers happy */
  fixed_left -= n;
  return (voidpf)(fixed_mem + fixed_left);
}


local void ffree(
	voidpf q,
	voidpf p,
	uInt n
)
{
  Assert(0, "inflate_trees ffree called!");
  if (q) q = p; /* to make some compilers happy */
}


local int inflate_trees_fixed(
	uIntf *bl,               /* literal desired/actual bit depth */
	uIntf *bd,               /* distance desired/actual bit depth */
	inflate_huft * FAR *tl,  /* literal/length tree result */
	inflate_huft * FAR *td   /* distance tree result */
)
{
  /* build fixed tables if not built already--lock out other instances */
  while (++fixed_lock > 1)
    fixed_lock--;
  if (!fixed_built)
  {
    int k;              /* temporary variable */
    unsigned c[288];    /* length list for huft_build */
    z_stream z;         /* for falloc function */

    /* set up fake z_stream for memory routines */
    z.zalloc = falloc;
    z.zfree = ffree;
    z.opaque = Z_NULL;

    /* literal table */
    for (k = 0; k < 144; k++)
      c[k] = 8;
    for (; k < 256; k++)
      c[k] = 9;
    for (; k < 280; k++)
      c[k] = 7;
    for (; k < 288; k++)
      c[k] = 8;
    fixed_bl = 7;
    huft_build(c, 288, 257, cplens, cplext, &fixed_tl, &fixed_bl, &z);

    /* distance table */
    for (k = 0; k < 30; k++)
      c[k] = 5;
    fixed_bd = 5;
    huft_build(c, 30, 0, cpdist, cpdext, &fixed_td, &fixed_bd, &z);

    /* done */
    fixed_built = 1;
  }
  fixed_lock--;
  *bl = fixed_bl;
  *bd = fixed_bd;
  *tl = fixed_tl;
  *td = fixed_td;
  return Z_OK;
}


local int inflate_trees_free(
	inflate_huft *t,        /* table to free */
	z_stream *z             /* for zfree function */
)
/* Free the malloc'ed tables built by huft_build(), which makes a linked
   list of the tables it made, with the links in a dummy first entry of
   each table. */
{
  register inflate_huft *p, *q;

  /* Go through linked list, freeing from the malloced (t[-1]) address. */
  p = t;
  while (p != Z_NULL)
  {
    q = (--p)->next;
    ZFREE(z, p, p->word.Nalloc * sizeof(inflate_huft));
    p = q;
  } 
  return Z_OK;
}

/*+++++*/
/* infcodes.c -- process literals and length/distance pairs
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* simplify the use of the inflate_huft type with some defines */
#define base more.Base
#define next more.Next
#define exop word.what.Exop
#define bits word.what.Bits

/* inflate codes private state */
struct inflate_codes_state {

  /* mode */
  enum {        /* waiting for "i:"=input, "o:"=output, "x:"=nothing */
      START,    /* x: set up for LEN */
      LEN,      /* i: get length/literal/eob next */
      LENEXT,   /* i: getting length extra (have base) */
      DIST,     /* i: get distance next */
      DISTEXT,  /* i: getting distance extra */
      COPY,     /* o: copying bytes in window, waiting for space */
      LIT,      /* o: got literal, waiting for output space */
      WASH,     /* o: got eob, possibly still output waiting */
      END,      /* x: got eob and all data flushed */
      BADCODE}  /* x: got error */
    mode;               /* current inflate_codes mode */

  /* mode dependent information */
  uInt len;
  union {
    struct {
      inflate_huft *tree;       /* pointer into tree */
      uInt need;                /* bits needed */
    } code;             /* if LEN or DIST, where in tree */
    uInt lit;           /* if LIT, literal */
    struct {
      uInt get;                 /* bits to get for extra */
      uInt dist;                /* distance back to copy from */
    } copy;             /* if EXT or COPY, where and how much */
  } sub;                /* submode */

  /* mode independent information */
  Byte lbits;           /* ltree bits decoded per branch */
  Byte dbits;           /* dtree bits decoder per branch */
  inflate_huft *ltree;          /* literal/length/eob tree */
  inflate_huft *dtree;          /* distance tree */

};


local inflate_codes_statef *inflate_codes_new(
	uInt bl,
	uInt bd,
	inflate_huft *tl,
	inflate_huft *td,
	z_stream *z
)
{
  inflate_codes_statef *c;

  if ((c = (inflate_codes_statef *)
       ZALLOC(z,1,sizeof(struct inflate_codes_state))) != Z_NULL)
  {
    c->mode = START;
    c->lbits = (Byte)bl;
    c->dbits = (Byte)bd;
    c->ltree = tl;
    c->dtree = td;
    Tracev((stderr, "inflate:       codes new\n"));
  }
  return c;
}


local int inflate_codes(
	inflate_blocks_statef *s,
	z_stream *z,
	int r
)
{
  uInt j;               /* temporary storage */
  inflate_huft *t;      /* temporary pointer */
  uInt e;               /* extra bits or operation */
  uLong b;              /* bit buffer */
  uInt k;               /* bits in bit buffer */
  Bytef *p;             /* input data pointer */
  uInt n;               /* bytes available there */
  Bytef *q;             /* output window write pointer */
  uInt m;               /* bytes to end of window or read pointer */
  Bytef *f;             /* pointer to copy strings from */
  inflate_codes_statef *c = s->sub.decode.codes;  /* codes state */

  /* copy input/output information to locals (UPDATE macro restores) */
  LOAD

  /* process input and output based on current state */
  while (1) switch (c->mode)
  {             /* waiting for "i:"=input, "o:"=output, "x:"=nothing */
    case START:         /* x: set up for LEN */
#ifndef SLOW
      if (m >= 258 && n >= 10)
      {
        UPDATE
        r = inflate_fast(c->lbits, c->dbits, c->ltree, c->dtree, s, z);
        LOAD
        if (r != Z_OK)
        {
          c->mode = r == Z_STREAM_END ? WASH : BADCODE;
          break;
        }
      }
#endif /* !SLOW */
      c->sub.code.need = c->lbits;
      c->sub.code.tree = c->ltree;
      c->mode = LEN;
    case LEN:           /* i: get length/literal/eob next */
      j = c->sub.code.need;
      NEEDBITS(j)
      t = c->sub.code.tree + ((uInt)b & inflate_mask[j]);
      DUMPBITS(t->bits)
      e = (uInt)(t->exop);
      if (e == 0)               /* literal */
      {
        c->sub.lit = t->base;
        Tracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                 "inflate:         literal '%c'\n" :
                 "inflate:         literal 0x%02x\n", t->base));
        c->mode = LIT;
        break;
      }
      if (e & 16)               /* length */
      {
        c->sub.copy.get = e & 15;
        c->len = t->base;
        c->mode = LENEXT;
        break;
      }
      if ((e & 64) == 0)        /* next table */
      {
        c->sub.code.need = e;
        c->sub.code.tree = t->next;
        break;
      }
      if (e & 32)               /* end of block */
      {
        Tracevv((stderr, "inflate:         end of block\n"));
        c->mode = WASH;
        break;
      }
      c->mode = BADCODE;        /* invalid code */
      z->msg = "invalid literal/length code";
      r = Z_DATA_ERROR;
      LEAVE
    case LENEXT:        /* i: getting length extra (have base) */
      j = c->sub.copy.get;
      NEEDBITS(j)
      c->len += (uInt)b & inflate_mask[j];
      DUMPBITS(j)
      c->sub.code.need = c->dbits;
      c->sub.code.tree = c->dtree;
      Tracevv((stderr, "inflate:         length %u\n", c->len));
      c->mode = DIST;
    case DIST:          /* i: get distance next */
      j = c->sub.code.need;
      NEEDBITS(j)
      t = c->sub.code.tree + ((uInt)b & inflate_mask[j]);
      DUMPBITS(t->bits)
      e = (uInt)(t->exop);
      if (e & 16)               /* distance */
      {
        c->sub.copy.get = e & 15;
        c->sub.copy.dist = t->base;
        c->mode = DISTEXT;
        break;
      }
      if ((e & 64) == 0)        /* next table */
      {
        c->sub.code.need = e;
        c->sub.code.tree = t->next;
        break;
      }
      c->mode = BADCODE;        /* invalid code */
      z->msg = "invalid distance code";
      r = Z_DATA_ERROR;
      LEAVE
    case DISTEXT:       /* i: getting distance extra */
      j = c->sub.copy.get;
      NEEDBITS(j)
      c->sub.copy.dist += (uInt)b & inflate_mask[j];
      DUMPBITS(j)
      Tracevv((stderr, "inflate:         distance %u\n", c->sub.copy.dist));
      c->mode = COPY;
    case COPY:          /* o: copying bytes in window, waiting for space */
#ifndef __TURBOC__ /* Turbo C bug for following expression */
      f = (uInt)(q - s->window) < c->sub.copy.dist ?
          s->end - (c->sub.copy.dist - (q - s->window)) :
          q - c->sub.copy.dist;
#else
      f = q - c->sub.copy.dist;
      if ((uInt)(q - s->window) < c->sub.copy.dist)
        f = s->end - (c->sub.copy.dist - (q - s->window));
#endif
      while (c->len)
      {
        NEEDOUT
        OUTBYTE(*f++)
        if (f == s->end)
          f = s->window;
        c->len--;
      }
      c->mode = START;
      break;
    case LIT:           /* o: got literal, waiting for output space */
      NEEDOUT
      OUTBYTE(c->sub.lit)
      c->mode = START;
      break;
    case WASH:          /* o: got eob, possibly more output */
      FLUSH
      if (s->read != s->write)
        LEAVE
      c->mode = END;
    case END:
      r = Z_STREAM_END;
      LEAVE
    case BADCODE:       /* x: got error */
      r = Z_DATA_ERROR;
      LEAVE
    default:
      r = Z_STREAM_ERROR;
      LEAVE
  }
}


local void inflate_codes_free(
	inflate_codes_statef *c,
	z_stream *z
)
{
  ZFREE(z, c, sizeof(struct inflate_codes_state));
  Tracev((stderr, "inflate:       codes free\n"));
}

/*+++++*/
/* inflate_util.c -- data and routines common to blocks and codes
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* copy as much as possible from the sliding window to the output area */
local int inflate_flush(
	inflate_blocks_statef *s,
	z_stream *z,
	int r
)
{
  uInt n;
  Bytef *p, *q;

  /* local copies of source and destination pointers */
  p = z->next_out;
  q = s->read;

  /* compute number of bytes to copy as far as end of window */
  n = (uInt)((q <= s->write ? s->write : s->end) - q);
  if (n > z->avail_out) n = z->avail_out;
  if (n && r == Z_BUF_ERROR) r = Z_OK;

  /* update counters */
  z->avail_out -= n;
  z->total_out += n;

  /* update check information */
  if (s->checkfn != Z_NULL)
    s->check = (*s->checkfn)(s->check, q, n);

  /* copy as far as end of window */
  zmemcpy(p, q, n);
  p += n;
  q += n;

  /* see if more to copy at beginning of window */
  if (q == s->end)
  {
    /* wrap pointers */
    q = s->window;
    if (s->write == s->end)
      s->write = s->window;

    /* compute bytes to copy */
    n = (uInt)(s->write - q);
    if (n > z->avail_out) n = z->avail_out;
    if (n && r == Z_BUF_ERROR) r = Z_OK;

    /* update counters */
    z->avail_out -= n;
    z->total_out += n;

    /* update check information */
    if (s->checkfn != Z_NULL)
      s->check = (*s->checkfn)(s->check, q, n);

    /* copy */
    zmemcpy(p, q, n);
    p += n;
    q += n;
  }

  /* update pointers */
  z->next_out = p;
  s->read = q;

  /* done */
  return r;
}


/*+++++*/
/* inffast.c -- process literals and length/distance pairs fast
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* simplify the use of the inflate_huft type with some defines */
#define base more.Base
#define next more.Next
#define exop word.what.Exop
#define bits word.what.Bits

/* macros for bit input with no checking and for returning unused bytes */
#define GRABBITS(j) {while(k<(j)){b|=((uLong)NEXTBYTE)<<k;k+=8;}}
#define UNGRAB {n+=(c=k>>3);p-=c;k&=7;}

/* Called with number of bytes left to write in window at least 258
   (the maximum string length) and number of input bytes available
   at least ten.  The ten bytes are six bytes for the longest length/
   distance pair plus four bytes for overloading the bit buffer. */

local int inflate_fast(
	uInt bl,
	uInt bd,
	inflate_huft *tl,
	inflate_huft *td,
	inflate_blocks_statef *s,
	z_stream *z
)
{
  inflate_huft *t;      /* temporary pointer */
  uInt e;               /* extra bits or operation */
  uLong b;              /* bit buffer */
  uInt k;               /* bits in bit buffer */
  Bytef *p;             /* input data pointer */
  uInt n;               /* bytes available there */
  Bytef *q;             /* output window write pointer */
  uInt m;               /* bytes to end of window or read pointer */
  uInt ml;              /* mask for literal/length tree */
  uInt md;              /* mask for distance tree */
  uInt c;               /* bytes to copy */
  uInt d;               /* distance back to copy from */
  Bytef *r;             /* copy source pointer */

  /* load input, output, bit values */
  LOAD

  /* initialize masks */
  ml = inflate_mask[bl];
  md = inflate_mask[bd];

  /* do until not enough input or output space for fast loop */
  do {                          /* assume called with m >= 258 && n >= 10 */
    /* get literal/length code */
    GRABBITS(20)                /* max bits for literal/length code */
    if ((e = (t = tl + ((uInt)b & ml))->exop) == 0)
    {
      DUMPBITS(t->bits)
      Tracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                "inflate:         * literal '%c'\n" :
                "inflate:         * literal 0x%02x\n", t->base));
      *q++ = (Byte)t->base;
      m--;
      continue;
    }
    do {
      DUMPBITS(t->bits)
      if (e & 16)
      {
        /* get extra bits for length */
        e &= 15;
        c = t->base + ((uInt)b & inflate_mask[e]);
        DUMPBITS(e)
        Tracevv((stderr, "inflate:         * length %u\n", c));

        /* decode distance base of block to copy */
        GRABBITS(15);           /* max bits for distance code */
        e = (t = td + ((uInt)b & md))->exop;
        do {
          DUMPBITS(t->bits)
          if (e & 16)
          {
            /* get extra bits to add to distance base */
            e &= 15;
            GRABBITS(e)         /* get extra bits (up to 13) */
            d = t->base + ((uInt)b & inflate_mask[e]);
            DUMPBITS(e)
            Tracevv((stderr, "inflate:         * distance %u\n", d));

            /* do the copy */
            m -= c;
            if ((uInt)(q - s->window) >= d)     /* offset before dest */
            {                                   /*  just copy */
              r = q - d;
              *q++ = *r++;  c--;        /* minimum count is three, */
              *q++ = *r++;  c--;        /*  so unroll loop a little */
            }
            else                        /* else offset after destination */
            {
              e = d - (q - s->window);  /* bytes from offset to end */
              r = s->end - e;           /* pointer to offset */
              if (c > e)                /* if source crosses, */
              {
                c -= e;                 /* copy to end of window */
                do {
                  *q++ = *r++;
                } while (--e);
                r = s->window;          /* copy rest from start of window */
              }
            }
            do {                        /* copy all or what's left */
              *q++ = *r++;
            } while (--c);
            break;
          }
          else if ((e & 64) == 0)
            e = (t = t->next + ((uInt)b & inflate_mask[e]))->exop;
          else
          {
            z->msg = "invalid distance code";
            UNGRAB
            UPDATE
            return Z_DATA_ERROR;
          }
        } while (1);
        break;
      }
      if ((e & 64) == 0)
      {
        if ((e = (t = t->next + ((uInt)b & inflate_mask[e]))->exop) == 0)
        {
          DUMPBITS(t->bits)
          Tracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                    "inflate:         * literal '%c'\n" :
                    "inflate:         * literal 0x%02x\n", t->base));
          *q++ = (Byte)t->base;
          m--;
          break;
        }
      }
      else if (e & 32)
      {
        Tracevv((stderr, "inflate:         * end of block\n"));
        UNGRAB
        UPDATE
        return Z_STREAM_END;
      }
      else
      {
        z->msg = "invalid literal/length code";
        UNGRAB
        UPDATE
        return Z_DATA_ERROR;
      }
    } while (1);
  } while (m >= 258 && n >= 10);

  /* not enough input or output--restore pointers and return */
  UNGRAB
  UPDATE
  return Z_OK;
}


/*+++++*/
/* zutil.c -- target dependent utility functions for the compression library
 * Copyright (C) 1995 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* From: zutil.c,v 1.8 1995/05/03 17:27:12 jloup Exp */

char *zlib_version = ZLIB_VERSION;

char *z_errmsg[] = {
"stream end",          /* Z_STREAM_END    1 */
"",                    /* Z_OK            0 */
"file error",          /* Z_ERRNO        (-1) */
"stream error",        /* Z_STREAM_ERROR (-2) */
"data error",          /* Z_DATA_ERROR   (-3) */
"insufficient memory", /* Z_MEM_ERROR    (-4) */
"buffer error",        /* Z_BUF_ERROR    (-5) */
""};


/*+++++*/
/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* From: adler32.c,v 1.6 1995/05/03 17:27:08 jloup Exp */

#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define DO1(buf)  {s1 += *buf++; s2 += s1;}
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);
#define DO16(buf) DO8(buf); DO8(buf);

/* ========================================================================= */
uLong adler32(
	uLong adler,
	Bytef *buf,
	uInt len
)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;
    int k;

    if (buf == Z_NULL) return 1L;

    while (len > 0) {
        k = len < NMAX ? len : NMAX;
        len -= k;
        while (k >= 16) {
            DO16(buf);
            k -= 16;
        }
        if (k != 0) do {
            DO1(buf);
        } while (--k);
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

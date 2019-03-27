/*
 * /src/NTP/ntp4-dev/libntp/ieee754io.c,v 4.12 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * ieee754io.c,v 4.12 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Jul 13 09:12:02 1997 $
 *
 * Copyright (c) 1997-2005 by Frank Kardel <kardel <AT> ntp.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "l_stdlib.h"
#include "ntp_stdlib.h"
#include "ntp_fp.h"
#include "ieee754io.h"

static unsigned char get_byte (unsigned char *, offsets_t, int *);
#ifdef __not_yet__
static void put_byte (unsigned char *, offsets_t, int *, unsigned char);
#endif

#ifdef LIBDEBUG

#include "lib_strbuf.h"

static char *
fmt_blong(
	  unsigned long val,
	  int cnt
	  )
{
  char *buf, *s;
  int i = cnt;

  val <<= 32 - cnt;
  LIB_GETBUF(buf);
  s = buf;
  
  while (i--)
    {
      if (val & 0x80000000)
	{
	  *s++ = '1';
	}
      else
	{
	  *s++ = '0';
	}
      val <<= 1;
    }
  *s = '\0';
  return buf;
}

static char *
fmt_flt(
	unsigned int sign,
	unsigned long mh,
	unsigned long ml,
	unsigned long ch
	)
{
	char *buf;

	LIB_GETBUF(buf);
	snprintf(buf, LIB_BUFLENGTH, "%c %s %s %s", sign ? '-' : '+',
		 fmt_blong(ch, 11),
		 fmt_blong(mh, 20),
		 fmt_blong(ml, 32));

	return buf;
}

static char *
fmt_hex(
	unsigned char *bufp,
	int length
	)
{
	char *	buf;
	char	hex[4];
	int	i;

	LIB_GETBUF(buf);
	buf[0] = '\0';
	for (i = 0; i < length; i++) {
		snprintf(hex, sizeof(hex), "%02x", bufp[i]);
		strlcat(buf, hex, LIB_BUFLENGTH);
	}

	return buf;
}

#endif

static unsigned char
get_byte(
	 unsigned char *bufp,
	 offsets_t offset,
	 int *fieldindex
	 )
{
  unsigned char val;

  val     = *(bufp + offset[*fieldindex]);
#ifdef LIBDEBUG
  if (debug > 4)
    printf("fetchieee754: getbyte(0x%08x, %d) = 0x%02x\n", (unsigned int)(bufp)+offset[*fieldindex], *fieldindex, val);
#endif
  (*fieldindex)++;
  return val;
}

#ifdef __not_yet__
static void
put_byte(
	 unsigned char *bufp,
	 offsets_t offsets,
	 int *fieldindex,
	 unsigned char val
	 )
{
  *(bufp + offsets[*fieldindex]) = val;
  (*fieldindex)++;
}
#endif

/*
 * make conversions to and from external IEEE754 formats and internal
 * NTP FP format.
 */
int
fetch_ieee754(
	      unsigned char **buffpp,
	      int size,
	      l_fp *lfpp,
	      offsets_t offsets
	      )
{
  unsigned char *bufp = *buffpp;
  unsigned int sign;
  unsigned int bias;
  unsigned int maxexp;
  int mbits;
  u_long mantissa_low;
  u_long mantissa_high;
  u_long characteristic;
  long exponent;
#ifdef LIBDEBUG
  int length;
#endif
  unsigned char val;
  int fieldindex = 0;
  
  switch (size)
    {
    case IEEE_DOUBLE:
#ifdef LIBDEBUG
      length = 8;
#endif
      mbits  = 52;
      bias   = 1023;
      maxexp = 2047;
      break;

    case IEEE_SINGLE:
#ifdef LIBDEBUG
      length = 4;
#endif
      mbits  = 23;
      bias   = 127;
      maxexp = 255;
      break;

    default:
      return IEEE_BADCALL;
    }
  
  val = get_byte(bufp, offsets, &fieldindex); /* fetch sign byte & first part of characteristic */
  
  sign     = (val & 0x80) != 0;
  characteristic = (val & 0x7F);

  val = get_byte(bufp, offsets, &fieldindex); /* fetch rest of characteristic and start of mantissa */
  
  switch (size)
    {
    case IEEE_SINGLE:
      characteristic <<= 1;
      characteristic  |= (val & 0x80) != 0; /* grab last characteristic bit */

      mantissa_high  = 0;

      mantissa_low   = (val &0x7F) << 16;
      mantissa_low  |= (u_long)get_byte(bufp, offsets, &fieldindex) << 8;
      mantissa_low  |= get_byte(bufp, offsets, &fieldindex);
      break;
      
    case IEEE_DOUBLE:
      characteristic <<= 4;
      characteristic  |= (val & 0xF0) >> 4; /* grab lower characteristic bits */

      mantissa_high  = (val & 0x0F) << 16;
      mantissa_high |= (u_long)get_byte(bufp, offsets, &fieldindex) << 8;
      mantissa_high |= get_byte(bufp, offsets, &fieldindex);

      mantissa_low   = (u_long)get_byte(bufp, offsets, &fieldindex) << 24;
      mantissa_low  |= (u_long)get_byte(bufp, offsets, &fieldindex) << 16;
      mantissa_low  |= (u_long)get_byte(bufp, offsets, &fieldindex) << 8;
      mantissa_low  |= get_byte(bufp, offsets, &fieldindex);
      break;
      
    default:
      return IEEE_BADCALL;
    }
#ifdef LIBDEBUG
  if (debug > 4)
  {
    double d;
    float f;

    if (size == IEEE_SINGLE)
      {
	int i;

	for (i = 0; i < length; i++)
	  {
	    *((unsigned char *)(&f)+i) = *(*buffpp + offsets[i]);
	  }
	d = f;
      }
    else
      {
	int i;

	for (i = 0; i < length; i++)
	  {
	    *((unsigned char *)(&d)+i) = *(*buffpp + offsets[i]);
	  }
      }
    
    printf("fetchieee754: FP: %s -> %s -> %e(=%s)\n", fmt_hex(*buffpp, length),
	   fmt_flt(sign, mantissa_high, mantissa_low, characteristic),
	   d, fmt_hex((unsigned char *)&d, length));
  }
#endif

  *buffpp += fieldindex;
  
  /*
   * detect funny numbers
   */
  if (characteristic == maxexp)
    {
      /*
       * NaN or Infinity
       */
      if (mantissa_low || mantissa_high)
	{
	  /*
	   * NaN
	   */
	  return IEEE_NAN;
	}
      else
	{
	  /*
	   * +Inf or -Inf
	   */
	  return sign ? IEEE_NEGINFINITY : IEEE_POSINFINITY;
	}
    }
  else
    {
      /*
       * collect real numbers
       */

      L_CLR(lfpp);

      /*
       * check for overflows
       */
      exponent = characteristic - bias;

      if (exponent > 31)	/* sorry - hardcoded */
	{
	  /*
	   * overflow only in respect to NTP-FP representation
	   */
	  return sign ? IEEE_NEGOVERFLOW : IEEE_POSOVERFLOW;
	}
      else
	{
	  int frac_offset;	/* where the fraction starts */
	  
	  frac_offset = mbits - exponent;

	  if (characteristic == 0) 
	    {
	      /*
	       * de-normalized or tiny number - fits only as 0
	       */
	      return IEEE_OK;
	    }
	  else
	    {
	      /*
	       * adjust for implied 1
	       */
	      if (mbits > 31)
		mantissa_high |= 1 << (mbits - 32);
	      else
		mantissa_low  |= 1 << mbits;

	      /*
	       * take mantissa apart - if only all machine would support
	       * 64 bit operations 8-(
	       */
	      if (frac_offset > mbits)
		{
		  lfpp->l_ui = 0; /* only fractional number */
		  frac_offset -= mbits + 1; /* will now contain right shift count - 1*/
		  if (mbits > 31)
		    {
		      lfpp->l_uf   = mantissa_high << (63 - mbits);
		      lfpp->l_uf  |= mantissa_low  >> (mbits - 33);
		      lfpp->l_uf >>= frac_offset;
		    }
		  else
		    {
		      lfpp->l_uf = mantissa_low >> frac_offset;
		    }
		}
	      else
		{
		  if (frac_offset > 32)
		    {
		      /*
		       * must split in high word
		       */
		      lfpp->l_ui  =  mantissa_high >> (frac_offset - 32);
		      lfpp->l_uf  = (mantissa_high & ((1 << (frac_offset - 32)) - 1)) << (64 - frac_offset);
		      lfpp->l_uf |=  mantissa_low  >> (frac_offset - 32);
		    }
		  else
		    {
		      /*
		       * must split in low word
		       */
		      lfpp->l_ui  =  mantissa_high << (32 - frac_offset);
		      lfpp->l_ui |= (mantissa_low >> frac_offset) & ((1 << (32 - frac_offset)) - 1);
		      lfpp->l_uf  = (mantissa_low & ((1 << frac_offset) - 1)) << (32 - frac_offset);
		    }
		}
	      
	      /*
	       * adjust for sign
	       */
	      if (sign)
		{
		  L_NEG(lfpp);
		}
	      
	      return IEEE_OK;
	    }
	}
    }
}
  
int
put_ieee754(
	    unsigned char **bufpp,
	    int size,
	    l_fp *lfpp,
	    offsets_t offsets
	    )
{
  l_fp outlfp;
#ifdef LIBDEBUG
  unsigned int sign;
  unsigned int bias;
#endif
/*unsigned int maxexp;*/
  int mbits;
  int msb;
  u_long mantissa_low = 0;
  u_long mantissa_high = 0;
#ifdef LIBDEBUG
  u_long characteristic = 0;
  long exponent;
#endif
/*int length;*/
  unsigned long mask;
  
  outlfp = *lfpp;

  switch (size)
    {
    case IEEE_DOUBLE:
    /*length = 8;*/
      mbits  = 52;
#ifdef LIBDEBUG
      bias   = 1023;
#endif
    /*maxexp = 2047;*/
      break;

    case IEEE_SINGLE:
    /*length = 4;*/
      mbits  = 23;
#ifdef LIBDEBUG
      bias   = 127;
#endif
    /*maxexp = 255;*/
      break;

    default:
      return IEEE_BADCALL;
    }
  
  /*
   * find sign
   */
  if (L_ISNEG(&outlfp))
    {
      L_NEG(&outlfp);
#ifdef LIBDEBUG
      sign = 1;
#endif
    }
  else
    {
#ifdef LIBDEBUG
      sign = 0;
#endif
    }

  if (L_ISZERO(&outlfp))
    {
#ifdef LIBDEBUG
      exponent = mantissa_high = mantissa_low = 0; /* true zero */
#endif
    }
  else
    {
      /*
       * find number of significant integer bits
       */
      mask = 0x80000000;
      if (outlfp.l_ui)
	{
	  msb = 63;
	  while (mask && ((outlfp.l_ui & mask) == 0))
	    {
	      mask >>= 1;
	      msb--;
	    }
	}
      else
	{
	  msb = 31;
	  while (mask && ((outlfp.l_uf & mask) == 0))
	    {
	      mask >>= 1;
	      msb--;
	    }
	}
  
      switch (size)
	{
	case IEEE_SINGLE:
	  mantissa_high = 0;
	  if (msb >= 32)
	    {
	      mantissa_low  = (outlfp.l_ui & ((1 << (msb - 32)) - 1)) << (mbits - (msb - 32));
	      mantissa_low |=  outlfp.l_uf >> (mbits - (msb - 32));
	    }
	  else
	    {
	      mantissa_low  = (outlfp.l_uf << (mbits - msb)) & ((1 << mbits) - 1);
	    }
	  break;
	  
	case IEEE_DOUBLE:
	  if (msb >= 32)
	    {
	      mantissa_high  = (outlfp.l_ui << (mbits - msb)) & ((1 << (mbits - 32)) - 1);
	      mantissa_high |=  outlfp.l_uf >> (32 - (mbits - msb));
	      mantissa_low   = (outlfp.l_ui & ((1 << (msb - mbits)) - 1)) << (32 - (msb - mbits));
	      mantissa_low  |=  outlfp.l_uf >> (msb - mbits);
	    }
	  else
	    {
	      mantissa_high  = outlfp.l_uf << (mbits - 32 - msb);
	      mantissa_low   = outlfp.l_uf << (mbits - 32);
	    }
	}

#ifdef LIBDEBUG
      exponent = msb - 32;
      characteristic = exponent + bias;

      if (debug > 4)
	printf("FP: %s\n", fmt_flt(sign, mantissa_high, mantissa_low, characteristic));
#endif
    }
  return IEEE_OK;
}


#if defined(DEBUG) && defined(LIBDEBUG)
int main(
	 int argc,
	 char **argv
	 )
{
  static offsets_t native_off = { 0, 1, 2, 3, 4, 5, 6, 7 };
  double f = 1.0;
  double *f_p = &f;
  l_fp fp;
  
  if (argc == 2)
    {
      if (sscanf(argv[1], "%lf", &f) != 1)
	{
	  printf("cannot convert %s to a float\n", argv[1]);
	  return 1;
	}
    }
  
  printf("double: %s %s\n", fmt_blong(*(unsigned long *)&f, 32), fmt_blong(*(unsigned long *)((char *)(&f)+4), 32));
  printf("fetch from %f = %d\n", f, fetch_ieee754((void *)&f_p, IEEE_DOUBLE, &fp, native_off));
  printf("fp [%s %s] = %s\n", fmt_blong(fp.l_ui, 32), fmt_blong(fp.l_uf, 32), mfptoa(fp.l_ui, fp.l_uf, 15));
  f_p = &f;
  put_ieee754((void *)&f_p, IEEE_DOUBLE, &fp, native_off);
  
  return 0;
}

#endif
/*
 * History:
 *
 * ieee754io.c,v
 * Revision 4.12  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.11  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.8  1999/02/21 12:17:36  kardel
 * 4.91f reconcilation
 *
 * Revision 4.7  1999/02/21 11:26:03  kardel
 * renamed index to fieldindex to avoid index() name clash
 *
 * Revision 4.6  1998/11/15 20:27:52  kardel
 * Release 4.0.73e13 reconcilation
 *
 * Revision 4.5  1998/08/16 19:01:51  kardel
 * debug information only compile for LIBDEBUG case
 *
 * Revision 4.4  1998/08/09 09:39:28  kardel
 * Release 4.0.73e2 reconcilation
 *
 * Revision 4.3  1998/06/13 11:56:19  kardel
 * disabled putbute() for the time being
 *
 * Revision 4.2  1998/06/12 15:16:58  kardel
 * ansi2knr compatibility
 *
 * Revision 4.1  1998/05/24 07:59:56  kardel
 * conditional debug support
 *
 * Revision 4.0  1998/04/10 19:46:29  kardel
 * Start 4.0 release version numbering
 *
 * Revision 1.1  1998/04/10 19:27:46  kardel
 * initial NTP VERSION 4 integration of PARSE with GPS166 binary support
 *
 * Revision 1.1  1997/10/06 21:05:45  kardel
 * new parse structure
 *
 */

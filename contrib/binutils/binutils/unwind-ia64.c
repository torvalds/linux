/* unwind-ia64.c -- utility routines to dump IA-64 unwind info for readelf.
   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of GNU Binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "unwind-ia64.h"
#include <stdio.h>
#include <string.h>

#if __GNUC__ >= 2
/* Define BFD64 here, even if our default architecture is 32 bit ELF
   as this will allow us to read in and parse 64bit and 32bit ELF files.
   Only do this if we believe that the compiler can support a 64 bit
   data type.  For now we only rely on GCC being able to do this.  */
#define BFD64
#endif
#include "bfd.h"

static bfd_vma unw_rlen = 0;

static void unw_print_brmask (char *, unsigned int);
static void unw_print_grmask (char *, unsigned int);
static void unw_print_frmask (char *, unsigned int);
static void unw_print_abreg (char *, unsigned int);
static void unw_print_xyreg (char *, unsigned int, unsigned int);

static void
unw_print_brmask (char *cp, unsigned int mask)
{
  int sep = 0;
  int i;

  for (i = 0; mask && (i < 5); ++i)
    {
      if (mask & 1)
	{
	  if (sep)
	    *cp++ = ',';
	  *cp++ = 'b';
	  *cp++ = i + 1 + '0';
	  sep = 1;
	}
      mask >>= 1;
    }
  *cp = '\0';
}

static void
unw_print_grmask (char *cp, unsigned int mask)
{
  int sep = 0;
  int i;

  for (i = 0; i < 4; ++i)
    {
      if (mask & 1)
	{
	  if (sep)
	    *cp++ = ',';
	  *cp++ = 'r';
	  *cp++ = i + 4 + '0';
	  sep = 1;
	}
      mask >>= 1;
    }
  *cp = '\0';
}

static void
unw_print_frmask (char *cp, unsigned int mask)
{
  int sep = 0;
  int i;

  for (i = 0; i < 20; ++i)
    {
      if (mask & 1)
	{
	  if (sep)
	    *cp++ = ',';
	  *cp++ = 'f';
	  if (i < 4)
	    *cp++ = i + 2 + '0';
	  else
	    {
	      *cp++ = (i + 2) / 10 + 1 + '0';
	      *cp++ = (i + 2) % 10 + '0';
	    }
	  sep = 1;
	}
      mask >>= 1;
    }
  *cp = '\0';
}

static void
unw_print_abreg (char *cp, unsigned int abreg)
{
  static const char *special_reg[16] =
  {
    "pr", "psp", "@priunat", "rp", "ar.bsp", "ar.bspstore", "ar.rnat",
    "ar.unat", "ar.fpsr", "ar.pfs", "ar.lc",
    "Unknown11", "Unknown12", "Unknown13", "Unknown14", "Unknown15"
  };

  switch ((abreg >> 5) & 0x3)
    {
    case 0: /* gr */
      sprintf (cp, "r%u", (abreg & 0x1f));
      break;

    case 1: /* fr */
      sprintf (cp, "f%u", (abreg & 0x1f));
      break;

    case 2: /* br */
      sprintf (cp, "b%u", (abreg & 0x1f));
      break;

    case 3: /* special */
      strcpy (cp, special_reg[abreg & 0xf]);
      break;
    }
}

static void
unw_print_xyreg (char *cp, unsigned int x, unsigned int ytreg)
{
  switch ((x << 1) | ((ytreg >> 7) & 1))
    {
    case 0: /* gr */
      sprintf (cp, "r%u", (ytreg & 0x1f));
      break;

    case 1: /* fr */
      sprintf (cp, "f%u", (ytreg & 0x1f));
      break;

    case 2: /* br */
      sprintf (cp, "b%u", (ytreg & 0x1f));
      break;
    }
}

#define UNW_REG_BSP		"bsp"
#define UNW_REG_BSPSTORE	"bspstore"
#define UNW_REG_FPSR		"fpsr"
#define UNW_REG_LC		"lc"
#define UNW_REG_PFS		"pfs"
#define UNW_REG_PR		"pr"
#define UNW_REG_PSP		"psp"
#define UNW_REG_RNAT		"rnat"
#define UNW_REG_RP		"rp"
#define UNW_REG_UNAT		"unat"

typedef bfd_vma unw_word;

#define UNW_DEC_BAD_CODE(code)			\
    printf ("Unknown code 0x%02x\n", code)

#define UNW_DEC_PROLOGUE(fmt, body, rlen, arg)					\
  do										\
    {										\
      unw_rlen = rlen;								\
      *(int *)arg = body;							\
      printf ("    %s:%s(rlen=%lu)\n",						\
	      fmt, body ? "body" : "prologue", (unsigned long) rlen);		\
    }										\
  while (0)

#define UNW_DEC_PROLOGUE_GR(fmt, rlen, mask, grsave, arg)			\
  do										\
    {										\
      char regname[16], maskstr[64], *sep;					\
										\
      unw_rlen = rlen;								\
      *(int *)arg = 0;								\
										\
      maskstr[0] = '\0';							\
      sep = "";									\
      if (mask & 0x8)								\
	{									\
	  strcat (maskstr, "rp");						\
	  sep = ",";								\
	}									\
      if (mask & 0x4)								\
	{									\
	  strcat (maskstr, sep);						\
	  strcat (maskstr, "ar.pfs");						\
	  sep = ",";								\
	}									\
      if (mask & 0x2)								\
	{									\
	  strcat (maskstr, sep);						\
	  strcat (maskstr, "psp");						\
	  sep = ",";								\
	}									\
      if (mask & 0x1)								\
	{									\
	  strcat (maskstr, sep);						\
	  strcat (maskstr, "pr");						\
	}									\
      sprintf (regname, "r%u", grsave);						\
      printf ("    %s:prologue_gr(mask=[%s],grsave=%s,rlen=%lu)\n",		\
	      fmt, maskstr, regname, (unsigned long) rlen);			\
    }										\
  while (0)

#define UNW_DEC_FR_MEM(fmt, frmask, arg)			\
  do								\
    {								\
      char frstr[200];						\
								\
      unw_print_frmask (frstr, frmask);				\
      printf ("\t%s:fr_mem(frmask=[%s])\n", fmt, frstr);	\
    }								\
  while (0)

#define UNW_DEC_GR_MEM(fmt, grmask, arg)			\
  do								\
    {								\
      char grstr[200];						\
								\
      unw_print_grmask (grstr, grmask);				\
      printf ("\t%s:gr_mem(grmask=[%s])\n", fmt, grstr);	\
    }								\
  while (0)

#define UNW_DEC_FRGR_MEM(fmt, grmask, frmask, arg)				\
  do										\
    {										\
      char frstr[200], grstr[20];						\
										\
      unw_print_grmask (grstr, grmask);						\
      unw_print_frmask (frstr, frmask);						\
      printf ("\t%s:frgr_mem(grmask=[%s],frmask=[%s])\n", fmt, grstr, frstr);	\
    }										\
  while (0)

#define UNW_DEC_BR_MEM(fmt, brmask, arg)				\
  do									\
    {									\
      char brstr[20];							\
									\
      unw_print_brmask (brstr, brmask);					\
      printf ("\t%s:br_mem(brmask=[%s])\n", fmt, brstr);		\
    }									\
  while (0)

#define UNW_DEC_BR_GR(fmt, brmask, gr, arg)				\
  do									\
    {									\
      char brstr[20];							\
									\
      unw_print_brmask (brstr, brmask);					\
      printf ("\t%s:br_gr(brmask=[%s],gr=r%u)\n", fmt, brstr, gr);	\
    }									\
  while (0)

#define UNW_DEC_REG_GR(fmt, src, dst, arg)		\
  printf ("\t%s:%s_gr(reg=r%u)\n", fmt, src, dst)

#define UNW_DEC_RP_BR(fmt, dst, arg)		\
  printf ("\t%s:rp_br(reg=b%u)\n", fmt, dst)

#define UNW_DEC_REG_WHEN(fmt, reg, t, arg)				\
  printf ("\t%s:%s_when(t=%lu)\n", fmt, reg, (unsigned long) t)

#define UNW_DEC_REG_SPREL(fmt, reg, spoff, arg)		\
  printf ("\t%s:%s_sprel(spoff=0x%lx)\n",		\
	  fmt, reg, 4*(unsigned long)spoff)

#define UNW_DEC_REG_PSPREL(fmt, reg, pspoff, arg)		\
  printf ("\t%s:%s_psprel(pspoff=0x10-0x%lx)\n",		\
	  fmt, reg, 4*(unsigned long)pspoff)

#define UNW_DEC_GR_GR(fmt, grmask, gr, arg)				\
  do									\
    {									\
      char grstr[20];							\
									\
      unw_print_grmask (grstr, grmask);					\
      printf ("\t%s:gr_gr(grmask=[%s],r%u)\n", fmt, grstr, gr);		\
    }									\
  while (0)

#define UNW_DEC_ABI(fmt, abi, context, arg)			\
  do								\
    {								\
      static const char *abiname[] =				\
      {								\
	"@svr4", "@hpux", "@nt"					\
      };							\
      char buf[20];						\
      const char *abistr = buf;					\
								\
      if (abi < 3)						\
	abistr = abiname[abi];					\
      else							\
	sprintf (buf, "0x%x", abi);				\
      printf ("\t%s:unwabi(abi=%s,context=0x%02x)\n",		\
	      fmt, abistr, context);				\
    }								\
  while (0)

#define UNW_DEC_PRIUNAT_GR(fmt, r, arg)		\
  printf ("\t%s:priunat_gr(reg=r%u)\n", fmt, r)

#define UNW_DEC_PRIUNAT_WHEN_GR(fmt, t, arg)				\
  printf ("\t%s:priunat_when_gr(t=%lu)\n", fmt, (unsigned long) t)

#define UNW_DEC_PRIUNAT_WHEN_MEM(fmt, t, arg)				\
  printf ("\t%s:priunat_when_mem(t=%lu)\n", fmt, (unsigned long) t)

#define UNW_DEC_PRIUNAT_PSPREL(fmt, pspoff, arg)		\
  printf ("\t%s:priunat_psprel(pspoff=0x10-0x%lx)\n",		\
	  fmt, 4*(unsigned long)pspoff)

#define UNW_DEC_PRIUNAT_SPREL(fmt, spoff, arg)		\
  printf ("\t%s:priunat_sprel(spoff=0x%lx)\n",		\
	  fmt, 4*(unsigned long)spoff)

#define UNW_DEC_MEM_STACK_F(fmt, t, size, arg)		\
  printf ("\t%s:mem_stack_f(t=%lu,size=%lu)\n",		\
	  fmt, (unsigned long) t, 16*(unsigned long)size)

#define UNW_DEC_MEM_STACK_V(fmt, t, arg)				\
  printf ("\t%s:mem_stack_v(t=%lu)\n", fmt, (unsigned long) t)

#define UNW_DEC_SPILL_BASE(fmt, pspoff, arg)			\
  printf ("\t%s:spill_base(pspoff=0x10-0x%lx)\n",		\
	  fmt, 4*(unsigned long)pspoff)

#define UNW_DEC_SPILL_MASK(fmt, dp, arg)					\
  do										\
    {										\
      static const char *spill_type = "-frb";					\
      unsigned const char *imaskp = dp;					\
      unsigned char mask = 0;							\
      bfd_vma insn = 0;								\
										\
      printf ("\t%s:spill_mask(imask=[", fmt);					\
      for (insn = 0; insn < unw_rlen; ++insn)					\
	{									\
	  if ((insn % 4) == 0)							\
	    mask = *imaskp++;							\
	  if (insn > 0 && (insn % 3) == 0)					\
	    putchar (',');							\
	  putchar (spill_type[(mask >> (2 * (3 - (insn & 0x3)))) & 0x3]);	\
	}									\
      printf ("])\n");								\
      dp = imaskp;								\
    }										\
  while (0)

#define UNW_DEC_SPILL_SPREL(fmt, t, abreg, spoff, arg)				\
  do										\
    {										\
      char regname[20];								\
										\
      unw_print_abreg (regname, abreg);						\
      printf ("\t%s:spill_sprel(reg=%s,t=%lu,spoff=0x%lx)\n",			\
	      fmt, regname, (unsigned long) t, 4*(unsigned long)off);		\
    }										\
  while (0)

#define UNW_DEC_SPILL_PSPREL(fmt, t, abreg, pspoff, arg)			\
  do										\
    {										\
      char regname[20];								\
										\
      unw_print_abreg (regname, abreg);						\
      printf ("\t%s:spill_psprel(reg=%s,t=%lu,pspoff=0x10-0x%lx)\n",		\
	      fmt, regname, (unsigned long) t, 4*(unsigned long)pspoff);	\
    }										\
  while (0)

#define UNW_DEC_RESTORE(fmt, t, abreg, arg)			\
  do								\
    {								\
      char regname[20];						\
								\
      unw_print_abreg (regname, abreg);				\
      printf ("\t%s:restore(t=%lu,reg=%s)\n",			\
	      fmt, (unsigned long) t, regname);			\
    }								\
  while (0)

#define UNW_DEC_SPILL_REG(fmt, t, abreg, x, ytreg, arg)		\
  do								\
    {								\
      char abregname[20], tregname[20];				\
								\
      unw_print_abreg (abregname, abreg);			\
      unw_print_xyreg (tregname, x, ytreg);			\
      printf ("\t%s:spill_reg(t=%lu,reg=%s,treg=%s)\n",		\
	      fmt, (unsigned long) t, abregname, tregname);	\
    }								\
  while (0)

#define UNW_DEC_SPILL_SPREL_P(fmt, qp, t, abreg, spoff, arg)			    \
  do										    \
    {										    \
      char regname[20];								    \
										    \
      unw_print_abreg (regname, abreg);						    \
      printf ("\t%s:spill_sprel_p(qp=p%u,t=%lu,reg=%s,spoff=0x%lx)\n",		    \
	      fmt, qp, (unsigned long) t, regname, 4 * (unsigned long)spoff);	    \
    }										    \
  while (0)

#define UNW_DEC_SPILL_PSPREL_P(fmt, qp, t, abreg, pspoff, arg)		\
  do									\
    {									\
      char regname[20];							\
									\
      unw_print_abreg (regname, abreg);					\
      printf ("\t%s:spill_psprel_p(qp=p%u,t=%lu,reg=%s,pspoff=0x10-0x%lx)\n",\
	      fmt, qp, (unsigned long) t, regname, 4*(unsigned long)pspoff);\
    }									\
  while (0)

#define UNW_DEC_RESTORE_P(fmt, qp, t, abreg, arg)			\
  do									\
    {									\
      char regname[20];							\
									\
      unw_print_abreg (regname, abreg);					\
      printf ("\t%s:restore_p(qp=p%u,t=%lu,reg=%s)\n",			\
	      fmt, qp, (unsigned long) t, regname);			\
    }									\
  while (0)

#define UNW_DEC_SPILL_REG_P(fmt, qp, t, abreg, x, ytreg, arg)		\
  do									\
    {									\
      char regname[20], tregname[20];					\
									\
      unw_print_abreg (regname, abreg);					\
      unw_print_xyreg (tregname, x, ytreg);				\
      printf ("\t%s:spill_reg_p(qp=p%u,t=%lu,reg=%s,treg=%s)\n",	\
	      fmt, qp, (unsigned long) t, regname, tregname);		\
    }									\
  while (0)

#define UNW_DEC_LABEL_STATE(fmt, label, arg)				\
  printf ("\t%s:label_state(label=%lu)\n", fmt, (unsigned long) label)

#define UNW_DEC_COPY_STATE(fmt, label, arg)				\
  printf ("\t%s:copy_state(label=%lu)\n", fmt, (unsigned long) label)

#define UNW_DEC_EPILOGUE(fmt, t, ecount, arg)		\
  printf ("\t%s:epilogue(t=%lu,ecount=%lu)\n",		\
	  fmt, (unsigned long) t, (unsigned long) ecount)

/*
 * Generic IA-64 unwind info decoder.
 *
 * This file is used both by the Linux kernel and objdump.  Please
 * keep the two copies of this file in sync (modulo differences in the
 * prototypes...).
 *
 * You need to customize the decoder by defining the following
 * macros/constants before including this file:
 *
 *  Types:
 *	unw_word	Unsigned integer type with at least 64 bits
 *
 *  Register names:
 *	UNW_REG_BSP
 *	UNW_REG_BSPSTORE
 *	UNW_REG_FPSR
 *	UNW_REG_LC
 *	UNW_REG_PFS
 *	UNW_REG_PR
 *	UNW_REG_RNAT
 *	UNW_REG_PSP
 *	UNW_REG_RP
 *	UNW_REG_UNAT
 *
 *  Decoder action macros:
 *	UNW_DEC_BAD_CODE(code)
 *	UNW_DEC_ABI(fmt,abi,context,arg)
 *	UNW_DEC_BR_GR(fmt,brmask,gr,arg)
 *	UNW_DEC_BR_MEM(fmt,brmask,arg)
 *	UNW_DEC_COPY_STATE(fmt,label,arg)
 *	UNW_DEC_EPILOGUE(fmt,t,ecount,arg)
 *	UNW_DEC_FRGR_MEM(fmt,grmask,frmask,arg)
 *	UNW_DEC_FR_MEM(fmt,frmask,arg)
 *	UNW_DEC_GR_GR(fmt,grmask,gr,arg)
 *	UNW_DEC_GR_MEM(fmt,grmask,arg)
 *	UNW_DEC_LABEL_STATE(fmt,label,arg)
 *	UNW_DEC_MEM_STACK_F(fmt,t,size,arg)
 *	UNW_DEC_MEM_STACK_V(fmt,t,arg)
 *	UNW_DEC_PRIUNAT_GR(fmt,r,arg)
 *	UNW_DEC_PRIUNAT_WHEN_GR(fmt,t,arg)
 *	UNW_DEC_PRIUNAT_WHEN_MEM(fmt,t,arg)
 *	UNW_DEC_PRIUNAT_WHEN_PSPREL(fmt,pspoff,arg)
 *	UNW_DEC_PRIUNAT_WHEN_SPREL(fmt,spoff,arg)
 *	UNW_DEC_PROLOGUE(fmt,body,rlen,arg)
 *	UNW_DEC_PROLOGUE_GR(fmt,rlen,mask,grsave,arg)
 *	UNW_DEC_REG_PSPREL(fmt,reg,pspoff,arg)
 *	UNW_DEC_REG_REG(fmt,src,dst,arg)
 *	UNW_DEC_REG_SPREL(fmt,reg,spoff,arg)
 *	UNW_DEC_REG_WHEN(fmt,reg,t,arg)
 *	UNW_DEC_RESTORE(fmt,t,abreg,arg)
 *	UNW_DEC_RESTORE_P(fmt,qp,t,abreg,arg)
 *	UNW_DEC_SPILL_BASE(fmt,pspoff,arg)
 *	UNW_DEC_SPILL_MASK(fmt,imaskp,arg)
 *	UNW_DEC_SPILL_PSPREL(fmt,t,abreg,pspoff,arg)
 *	UNW_DEC_SPILL_PSPREL_P(fmt,qp,t,abreg,pspoff,arg)
 *	UNW_DEC_SPILL_REG(fmt,t,abreg,x,ytreg,arg)
 *	UNW_DEC_SPILL_REG_P(fmt,qp,t,abreg,x,ytreg,arg)
 *	UNW_DEC_SPILL_SPREL(fmt,t,abreg,spoff,arg)
 *	UNW_DEC_SPILL_SPREL_P(fmt,qp,t,abreg,pspoff,arg)
 */

static unw_word unw_decode_uleb128 (const unsigned char **);
static const unsigned char *unw_decode_x1
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_x2
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_x3
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_x4
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_r1
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_r2
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_r3
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_p1
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_p2_p5
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_p6
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_p7_p10
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_b1
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_b2
  (const unsigned char *, unsigned int, void *);
static const unsigned char *unw_decode_b3_x4
  (const unsigned char *, unsigned int, void *);

static unw_word
unw_decode_uleb128 (const unsigned char **dpp)
{
  unsigned shift = 0;
  unw_word byte, result = 0;
  const unsigned char *bp = *dpp;

  while (1)
    {
      byte = *bp++;
      result |= (byte & 0x7f) << shift;

      if ((byte & 0x80) == 0)
	break;

      shift += 7;
    }

  *dpp = bp;

  return result;
}

static const unsigned char *
unw_decode_x1 (const unsigned char *dp, unsigned int code ATTRIBUTE_UNUSED,
	       void *arg ATTRIBUTE_UNUSED)
{
  unsigned char byte1, abreg;
  unw_word t, off;

  byte1 = *dp++;
  t = unw_decode_uleb128 (&dp);
  off = unw_decode_uleb128 (&dp);
  abreg = (byte1 & 0x7f);
  if (byte1 & 0x80)
    UNW_DEC_SPILL_SPREL ("X1", t, abreg, off, arg);
  else
    UNW_DEC_SPILL_PSPREL ("X1", t, abreg, off, arg);
  return dp;
}

static const unsigned char *
unw_decode_x2 (const unsigned char *dp, unsigned int code ATTRIBUTE_UNUSED,
	       void *arg ATTRIBUTE_UNUSED)
{
  unsigned char byte1, byte2, abreg, x, ytreg;
  unw_word t;

  byte1 = *dp++;
  byte2 = *dp++;
  t = unw_decode_uleb128 (&dp);
  abreg = (byte1 & 0x7f);
  ytreg = byte2;
  x = (byte1 >> 7) & 1;
  if ((byte1 & 0x80) == 0 && ytreg == 0)
    UNW_DEC_RESTORE ("X2", t, abreg, arg);
  else
    UNW_DEC_SPILL_REG ("X2", t, abreg, x, ytreg, arg);
  return dp;
}

static const unsigned char *
unw_decode_x3 (const unsigned char *dp, unsigned int code ATTRIBUTE_UNUSED,
	       void *arg ATTRIBUTE_UNUSED)
{
  unsigned char byte1, byte2, abreg, qp;
  unw_word t, off;

  byte1 = *dp++;
  byte2 = *dp++;
  t = unw_decode_uleb128 (&dp);
  off = unw_decode_uleb128 (&dp);

  qp = (byte1 & 0x3f);
  abreg = (byte2 & 0x7f);

  if (byte1 & 0x80)
    UNW_DEC_SPILL_SPREL_P ("X3", qp, t, abreg, off, arg);
  else
    UNW_DEC_SPILL_PSPREL_P ("X3", qp, t, abreg, off, arg);
  return dp;
}

static const unsigned char *
unw_decode_x4 (const unsigned char *dp, unsigned int code ATTRIBUTE_UNUSED,
	       void *arg ATTRIBUTE_UNUSED)
{
  unsigned char byte1, byte2, byte3, qp, abreg, x, ytreg;
  unw_word t;

  byte1 = *dp++;
  byte2 = *dp++;
  byte3 = *dp++;
  t = unw_decode_uleb128 (&dp);

  qp = (byte1 & 0x3f);
  abreg = (byte2 & 0x7f);
  x = (byte2 >> 7) & 1;
  ytreg = byte3;

  if ((byte2 & 0x80) == 0 && byte3 == 0)
    UNW_DEC_RESTORE_P ("X4", qp, t, abreg, arg);
  else
    UNW_DEC_SPILL_REG_P ("X4", qp, t, abreg, x, ytreg, arg);
  return dp;
}

static const unsigned char *
unw_decode_r1 (const unsigned char *dp, unsigned int code, void *arg)
{
  int body = (code & 0x20) != 0;
  unw_word rlen;

  rlen = (code & 0x1f);
  UNW_DEC_PROLOGUE ("R1", body, rlen, arg);
  return dp;
}

static const unsigned char *
unw_decode_r2 (const unsigned char *dp, unsigned int code, void *arg)
{
  unsigned char byte1, mask, grsave;
  unw_word rlen;

  byte1 = *dp++;

  mask = ((code & 0x7) << 1) | ((byte1 >> 7) & 1);
  grsave = (byte1 & 0x7f);
  rlen = unw_decode_uleb128 (& dp);
  UNW_DEC_PROLOGUE_GR ("R2", rlen, mask, grsave, arg);
  return dp;
}

static const unsigned char *
unw_decode_r3 (const unsigned char *dp, unsigned int code, void *arg)
{
  unw_word rlen;

  rlen = unw_decode_uleb128 (& dp);
  UNW_DEC_PROLOGUE ("R3", ((code & 0x3) == 1), rlen, arg);
  return dp;
}

static const unsigned char *
unw_decode_p1 (const unsigned char *dp, unsigned int code,
	       void *arg ATTRIBUTE_UNUSED)
{
  unsigned char brmask = (code & 0x1f);

  UNW_DEC_BR_MEM ("P1", brmask, arg);
  return dp;
}

static const unsigned char *
unw_decode_p2_p5 (const unsigned char *dp, unsigned int code,
		  void *arg ATTRIBUTE_UNUSED)
{
  if ((code & 0x10) == 0)
    {
      unsigned char byte1 = *dp++;

      UNW_DEC_BR_GR ("P2", ((code & 0xf) << 1) | ((byte1 >> 7) & 1),
		     (byte1 & 0x7f), arg);
    }
  else if ((code & 0x08) == 0)
    {
      unsigned char byte1 = *dp++, r, dst;

      r = ((code & 0x7) << 1) | ((byte1 >> 7) & 1);
      dst = (byte1 & 0x7f);
      switch (r)
	{
	case 0:
	  UNW_DEC_REG_GR ("P3", UNW_REG_PSP, dst, arg);
	  break;
	case 1:
	  UNW_DEC_REG_GR ("P3", UNW_REG_RP, dst, arg);
	  break;
	case 2:
	  UNW_DEC_REG_GR ("P3", UNW_REG_PFS, dst, arg);
	  break;
	case 3:
	  UNW_DEC_REG_GR ("P3", UNW_REG_PR, dst, arg);
	  break;
	case 4:
	  UNW_DEC_REG_GR ("P3", UNW_REG_UNAT, dst, arg);
	  break;
	case 5:
	  UNW_DEC_REG_GR ("P3", UNW_REG_LC, dst, arg);
	  break;
	case 6:
	  UNW_DEC_RP_BR ("P3", dst, arg);
	  break;
	case 7:
	  UNW_DEC_REG_GR ("P3", UNW_REG_RNAT, dst, arg);
	  break;
	case 8:
	  UNW_DEC_REG_GR ("P3", UNW_REG_BSP, dst, arg);
	  break;
	case 9:
	  UNW_DEC_REG_GR ("P3", UNW_REG_BSPSTORE, dst, arg);
	  break;
	case 10:
	  UNW_DEC_REG_GR ("P3", UNW_REG_FPSR, dst, arg);
	  break;
	case 11:
	  UNW_DEC_PRIUNAT_GR ("P3", dst, arg);
	  break;
	default:
	  UNW_DEC_BAD_CODE (r);
	  break;
	}
    }
  else if ((code & 0x7) == 0)
    UNW_DEC_SPILL_MASK ("P4", dp, arg);
  else if ((code & 0x7) == 1)
    {
      unw_word grmask, frmask, byte1, byte2, byte3;

      byte1 = *dp++;
      byte2 = *dp++;
      byte3 = *dp++;
      grmask = ((byte1 >> 4) & 0xf);
      frmask = ((byte1 & 0xf) << 16) | (byte2 << 8) | byte3;
      UNW_DEC_FRGR_MEM ("P5", grmask, frmask, arg);
    }
  else
    UNW_DEC_BAD_CODE (code);

  return dp;
}

static const unsigned char *
unw_decode_p6 (const unsigned char *dp, unsigned int code,
	       void *arg ATTRIBUTE_UNUSED)
{
  int gregs = (code & 0x10) != 0;
  unsigned char mask = (code & 0x0f);

  if (gregs)
    UNW_DEC_GR_MEM ("P6", mask, arg);
  else
    UNW_DEC_FR_MEM ("P6", mask, arg);
  return dp;
}

static const unsigned char *
unw_decode_p7_p10 (const unsigned char *dp, unsigned int code, void *arg)
{
  unsigned char r, byte1, byte2;
  unw_word t, size;

  if ((code & 0x10) == 0)
    {
      r = (code & 0xf);
      t = unw_decode_uleb128 (&dp);
      switch (r)
	{
	case 0:
	  size = unw_decode_uleb128 (&dp);
	  UNW_DEC_MEM_STACK_F ("P7", t, size, arg);
	  break;

	case 1:
	  UNW_DEC_MEM_STACK_V ("P7", t, arg);
	  break;
	case 2:
	  UNW_DEC_SPILL_BASE ("P7", t, arg);
	  break;
	case 3:
	  UNW_DEC_REG_SPREL ("P7", UNW_REG_PSP, t, arg);
	  break;
	case 4:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_RP, t, arg);
	  break;
	case 5:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_RP, t, arg);
	  break;
	case 6:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_PFS, t, arg);
	  break;
	case 7:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_PFS, t, arg);
	  break;
	case 8:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_PR, t, arg);
	  break;
	case 9:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_PR, t, arg);
	  break;
	case 10:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_LC, t, arg);
	  break;
	case 11:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_LC, t, arg);
	  break;
	case 12:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_UNAT, t, arg);
	  break;
	case 13:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_UNAT, t, arg);
	  break;
	case 14:
	  UNW_DEC_REG_WHEN ("P7", UNW_REG_FPSR, t, arg);
	  break;
	case 15:
	  UNW_DEC_REG_PSPREL ("P7", UNW_REG_FPSR, t, arg);
	  break;
	default:
	  UNW_DEC_BAD_CODE (r);
	  break;
	}
    }
  else
    {
      switch (code & 0xf)
	{
	case 0x0:		/* p8 */
	  {
	    r = *dp++;
	    t = unw_decode_uleb128 (&dp);
	    switch (r)
	      {
	      case 1:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_RP, t, arg);
		break;
	      case 2:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_PFS, t, arg);
		break;
	      case 3:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_PR, t, arg);
		break;
	      case 4:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_LC, t, arg);
		break;
	      case 5:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_UNAT, t, arg);
		break;
	      case 6:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_FPSR, t, arg);
		break;
	      case 7:
		UNW_DEC_REG_WHEN ("P8", UNW_REG_BSP, t, arg);
		break;
	      case 8:
		UNW_DEC_REG_PSPREL ("P8", UNW_REG_BSP, t, arg);
		break;
	      case 9:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_BSP, t, arg);
		break;
	      case 10:
		UNW_DEC_REG_WHEN ("P8", UNW_REG_BSPSTORE, t, arg);
		break;
	      case 11:
		UNW_DEC_REG_PSPREL ("P8", UNW_REG_BSPSTORE, t, arg);
		break;
	      case 12:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_BSPSTORE, t, arg);
		break;
	      case 13:
		UNW_DEC_REG_WHEN ("P8", UNW_REG_RNAT, t, arg);
		break;
	      case 14:
		UNW_DEC_REG_PSPREL ("P8", UNW_REG_RNAT, t, arg);
		break;
	      case 15:
		UNW_DEC_REG_SPREL ("P8", UNW_REG_RNAT, t, arg);
		break;
	      case 16:
		UNW_DEC_PRIUNAT_WHEN_GR ("P8", t, arg);
		break;
	      case 17:
		UNW_DEC_PRIUNAT_PSPREL ("P8", t, arg);
		break;
	      case 18:
		UNW_DEC_PRIUNAT_SPREL ("P8", t, arg);
		break;
	      case 19:
		UNW_DEC_PRIUNAT_WHEN_MEM ("P8", t, arg);
		break;
	      default:
		UNW_DEC_BAD_CODE (r);
		break;
	      }
	  }
	  break;

	case 0x1:
	  byte1 = *dp++;
	  byte2 = *dp++;
	  UNW_DEC_GR_GR ("P9", (byte1 & 0xf), (byte2 & 0x7f), arg);
	  break;

	case 0xf:		/* p10 */
	  byte1 = *dp++;
	  byte2 = *dp++;
	  UNW_DEC_ABI ("P10", byte1, byte2, arg);
	  break;

	case 0x9:
	  return unw_decode_x1 (dp, code, arg);

	case 0xa:
	  return unw_decode_x2 (dp, code, arg);

	case 0xb:
	  return unw_decode_x3 (dp, code, arg);

	case 0xc:
	  return unw_decode_x4 (dp, code, arg);

	default:
	  UNW_DEC_BAD_CODE (code);
	  break;
	}
    }
  return dp;
}

static const unsigned char *
unw_decode_b1 (const unsigned char *dp, unsigned int code,
	       void *arg ATTRIBUTE_UNUSED)
{
  unw_word label = (code & 0x1f);

  if ((code & 0x20) != 0)
    UNW_DEC_COPY_STATE ("B1", label, arg);
  else
    UNW_DEC_LABEL_STATE ("B1", label, arg);
  return dp;
}

static const unsigned char *
unw_decode_b2 (const unsigned char *dp, unsigned int code,
	       void *arg ATTRIBUTE_UNUSED)
{
  unw_word t;

  t = unw_decode_uleb128 (& dp);
  UNW_DEC_EPILOGUE ("B2", t, (code & 0x1f), arg);
  return dp;
}

static const unsigned char *
unw_decode_b3_x4 (const unsigned char *dp, unsigned int code, void *arg)
{
  unw_word t, ecount, label;

  if ((code & 0x10) == 0)
    {
      t = unw_decode_uleb128 (&dp);
      ecount = unw_decode_uleb128 (&dp);
      UNW_DEC_EPILOGUE ("B3", t, ecount, arg);
    }
  else if ((code & 0x07) == 0)
    {
      label = unw_decode_uleb128 (&dp);
      if ((code & 0x08) != 0)
	UNW_DEC_COPY_STATE ("B4", label, arg);
      else
	UNW_DEC_LABEL_STATE ("B4", label, arg);
    }
  else
    switch (code & 0x7)
      {
      case 1:
	return unw_decode_x1 (dp, code, arg);
      case 2:
	return unw_decode_x2 (dp, code, arg);
      case 3:
	return unw_decode_x3 (dp, code, arg);
      case 4:
	return unw_decode_x4 (dp, code, arg);
      default:
	UNW_DEC_BAD_CODE (code);
	break;
      }
  return dp;
}

typedef const unsigned char *(*unw_decoder)
     (const unsigned char *, unsigned int, void *);

static unw_decoder unw_decode_table[2][8] =
  {
    /* prologue table: */
    {
      unw_decode_r1,		/* 0 */
      unw_decode_r1,
      unw_decode_r2,
      unw_decode_r3,
      unw_decode_p1,		/* 4 */
      unw_decode_p2_p5,
      unw_decode_p6,
      unw_decode_p7_p10
    },
    {
      unw_decode_r1,		/* 0 */
      unw_decode_r1,
      unw_decode_r2,
      unw_decode_r3,
      unw_decode_b1,		/* 4 */
      unw_decode_b1,
      unw_decode_b2,
      unw_decode_b3_x4
    }
  };

/* Decode one descriptor and return address of next descriptor.  */
const unsigned char *
unw_decode (const unsigned char *dp, int inside_body,
	    void *ptr_inside_body)
{
  unw_decoder decoder;
  unsigned char code;

  code = *dp++;
  decoder = unw_decode_table[inside_body][code >> 5];
  return (*decoder) (dp, code, ptr_inside_body);
}

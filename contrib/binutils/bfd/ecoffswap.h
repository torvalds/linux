/* Generic ECOFF swapping routines, for BFD.
   Copyright 1992, 1993, 1994, 1995, 1996, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Written by Cygnus Support.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* NOTE: This is a header file, but it contains executable routines.
   This is done this way because these routines are substantially
   similar, but are not identical, for all ECOFF targets.

   These are routines to swap the ECOFF symbolic information in and
   out.  The routines are defined statically.  You can set breakpoints
   on them in gdb by naming the including source file; e.g.,
   'coff-mips.c':ecoff_swap_hdr_in.

   Before including this header file, one of ECOFF_32, ECOFF_64,
   ECOFF_SIGNED_32 or ECOFF_SIGNED_64 must be defined.  These are
   checked when swapping information that depends upon the target
   size.  This code works for 32 bit and 64 bit ECOFF, but may need to
   be generalized in the future.

   Some header file which defines the external forms of these
   structures must also be included before including this header file.
   Currently this is either coff/mips.h or coff/alpha.h.

   If the symbol TEST is defined when this file is compiled, a
   comparison is made to ensure that, in fact, the output is
   bit-for-bit the same as the input.  Of course, this symbol should
   only be defined when deliberately testing the code on a machine
   with the proper byte sex and such.  */

#ifdef ECOFF_32
#define ECOFF_GET_OFF H_GET_32
#define ECOFF_PUT_OFF H_PUT_32
#endif
#ifdef ECOFF_64
#define ECOFF_GET_OFF H_GET_64
#define ECOFF_PUT_OFF H_PUT_64
#endif
#ifdef ECOFF_SIGNED_32
#define ECOFF_GET_OFF H_GET_S32
#define ECOFF_PUT_OFF H_PUT_S32
#endif
#ifdef ECOFF_SIGNED_64
#define ECOFF_GET_OFF H_GET_S64
#define ECOFF_PUT_OFF H_PUT_S64
#endif

/* ECOFF auxiliary information swapping routines.  These are the same
   for all ECOFF targets, so they are defined in ecofflink.c.  */

extern void _bfd_ecoff_swap_tir_in
  (int, const struct tir_ext *, TIR *);
extern void _bfd_ecoff_swap_tir_out
  (int, const TIR *, struct tir_ext *);
extern void _bfd_ecoff_swap_rndx_in
  (int, const struct rndx_ext *, RNDXR *);
extern void _bfd_ecoff_swap_rndx_out
  (int, const RNDXR *, struct rndx_ext *);

/* Prototypes for functions defined in this file.  */

static void ecoff_swap_hdr_in (bfd *, void *, HDRR *);
static void ecoff_swap_hdr_out (bfd *, const HDRR *, void *);
static void ecoff_swap_fdr_in (bfd *, void *, FDR *);
static void ecoff_swap_fdr_out (bfd *, const FDR *, void *);
static void ecoff_swap_pdr_in (bfd *, void *, PDR *);
static void ecoff_swap_pdr_out (bfd *, const PDR *, void *);
static void ecoff_swap_sym_in (bfd *, void *, SYMR *);
static void ecoff_swap_sym_out (bfd *, const SYMR *, void *);
static void ecoff_swap_ext_in (bfd *, void *, EXTR *);
static void ecoff_swap_ext_out (bfd *, const EXTR *, void *);
static void ecoff_swap_rfd_in (bfd *, void *, RFDT *);
static void ecoff_swap_rfd_out (bfd *, const RFDT *, void *);
static void ecoff_swap_opt_in (bfd *, void *, OPTR *);
static void ecoff_swap_opt_out (bfd *, const OPTR *, void *);
static void ecoff_swap_dnr_in (bfd *, void *, DNR *);
static void ecoff_swap_dnr_out (bfd *, const DNR *, void *);

/* Swap in the symbolic header.  */

static void
ecoff_swap_hdr_in (bfd *abfd, void * ext_copy, HDRR *intern)
{
  struct hdr_ext ext[1];

  *ext = *(struct hdr_ext *) ext_copy;

  intern->magic         = H_GET_S16     (abfd, ext->h_magic);
  intern->vstamp        = H_GET_S16     (abfd, ext->h_vstamp);
  intern->ilineMax      = H_GET_32      (abfd, ext->h_ilineMax);
  intern->cbLine        = ECOFF_GET_OFF (abfd, ext->h_cbLine);
  intern->cbLineOffset  = ECOFF_GET_OFF (abfd, ext->h_cbLineOffset);
  intern->idnMax        = H_GET_32      (abfd, ext->h_idnMax);
  intern->cbDnOffset    = ECOFF_GET_OFF (abfd, ext->h_cbDnOffset);
  intern->ipdMax        = H_GET_32      (abfd, ext->h_ipdMax);
  intern->cbPdOffset    = ECOFF_GET_OFF (abfd, ext->h_cbPdOffset);
  intern->isymMax       = H_GET_32      (abfd, ext->h_isymMax);
  intern->cbSymOffset   = ECOFF_GET_OFF (abfd, ext->h_cbSymOffset);
  intern->ioptMax       = H_GET_32      (abfd, ext->h_ioptMax);
  intern->cbOptOffset   = ECOFF_GET_OFF (abfd, ext->h_cbOptOffset);
  intern->iauxMax       = H_GET_32      (abfd, ext->h_iauxMax);
  intern->cbAuxOffset   = ECOFF_GET_OFF (abfd, ext->h_cbAuxOffset);
  intern->issMax        = H_GET_32      (abfd, ext->h_issMax);
  intern->cbSsOffset    = ECOFF_GET_OFF (abfd, ext->h_cbSsOffset);
  intern->issExtMax     = H_GET_32      (abfd, ext->h_issExtMax);
  intern->cbSsExtOffset = ECOFF_GET_OFF (abfd, ext->h_cbSsExtOffset);
  intern->ifdMax        = H_GET_32      (abfd, ext->h_ifdMax);
  intern->cbFdOffset    = ECOFF_GET_OFF (abfd, ext->h_cbFdOffset);
  intern->crfd          = H_GET_32      (abfd, ext->h_crfd);
  intern->cbRfdOffset   = ECOFF_GET_OFF (abfd, ext->h_cbRfdOffset);
  intern->iextMax       = H_GET_32      (abfd, ext->h_iextMax);
  intern->cbExtOffset   = ECOFF_GET_OFF (abfd, ext->h_cbExtOffset);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out the symbolic header.  */

static void
ecoff_swap_hdr_out (bfd *abfd, const HDRR *intern_copy, void * ext_ptr)
{
  struct hdr_ext *ext = (struct hdr_ext *) ext_ptr;
  HDRR intern[1];

  *intern = *intern_copy;

  H_PUT_S16     (abfd, intern->magic,         ext->h_magic);
  H_PUT_S16     (abfd, intern->vstamp,        ext->h_vstamp);
  H_PUT_32      (abfd, intern->ilineMax,      ext->h_ilineMax);
  ECOFF_PUT_OFF (abfd, intern->cbLine,        ext->h_cbLine);
  ECOFF_PUT_OFF (abfd, intern->cbLineOffset,  ext->h_cbLineOffset);
  H_PUT_32      (abfd, intern->idnMax,        ext->h_idnMax);
  ECOFF_PUT_OFF (abfd, intern->cbDnOffset,    ext->h_cbDnOffset);
  H_PUT_32      (abfd, intern->ipdMax,        ext->h_ipdMax);
  ECOFF_PUT_OFF (abfd, intern->cbPdOffset,    ext->h_cbPdOffset);
  H_PUT_32      (abfd, intern->isymMax,       ext->h_isymMax);
  ECOFF_PUT_OFF (abfd, intern->cbSymOffset,   ext->h_cbSymOffset);
  H_PUT_32      (abfd, intern->ioptMax,       ext->h_ioptMax);
  ECOFF_PUT_OFF (abfd, intern->cbOptOffset,   ext->h_cbOptOffset);
  H_PUT_32      (abfd, intern->iauxMax,       ext->h_iauxMax);
  ECOFF_PUT_OFF (abfd, intern->cbAuxOffset,   ext->h_cbAuxOffset);
  H_PUT_32      (abfd, intern->issMax,        ext->h_issMax);
  ECOFF_PUT_OFF (abfd, intern->cbSsOffset,    ext->h_cbSsOffset);
  H_PUT_32      (abfd, intern->issExtMax,     ext->h_issExtMax);
  ECOFF_PUT_OFF (abfd, intern->cbSsExtOffset, ext->h_cbSsExtOffset);
  H_PUT_32      (abfd, intern->ifdMax,        ext->h_ifdMax);
  ECOFF_PUT_OFF (abfd, intern->cbFdOffset,    ext->h_cbFdOffset);
  H_PUT_32      (abfd, intern->crfd,          ext->h_crfd);
  ECOFF_PUT_OFF (abfd, intern->cbRfdOffset,   ext->h_cbRfdOffset);
  H_PUT_32      (abfd, intern->iextMax,       ext->h_iextMax);
  ECOFF_PUT_OFF (abfd, intern->cbExtOffset,   ext->h_cbExtOffset);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in the file descriptor record.  */

static void
ecoff_swap_fdr_in (bfd *abfd, void * ext_copy, FDR *intern)
{
  struct fdr_ext ext[1];

  *ext = *(struct fdr_ext *) ext_copy;

  intern->adr           = ECOFF_GET_OFF (abfd, ext->f_adr);
  intern->rss           = H_GET_32 (abfd, ext->f_rss);
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  if (intern->rss == (signed long) 0xffffffff)
    intern->rss = -1;
#endif
  intern->issBase       = H_GET_32 (abfd, ext->f_issBase);
  intern->cbSs          = ECOFF_GET_OFF (abfd, ext->f_cbSs);
  intern->isymBase      = H_GET_32 (abfd, ext->f_isymBase);
  intern->csym          = H_GET_32 (abfd, ext->f_csym);
  intern->ilineBase     = H_GET_32 (abfd, ext->f_ilineBase);
  intern->cline         = H_GET_32 (abfd, ext->f_cline);
  intern->ioptBase      = H_GET_32 (abfd, ext->f_ioptBase);
  intern->copt          = H_GET_32 (abfd, ext->f_copt);
#if defined (ECOFF_32) || defined (ECOFF_SIGNED_32)
  intern->ipdFirst      = H_GET_16 (abfd, ext->f_ipdFirst);
  intern->cpd           = H_GET_16 (abfd, ext->f_cpd);
#endif
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  intern->ipdFirst      = H_GET_32 (abfd, ext->f_ipdFirst);
  intern->cpd           = H_GET_32 (abfd, ext->f_cpd);
#endif
  intern->iauxBase      = H_GET_32 (abfd, ext->f_iauxBase);
  intern->caux          = H_GET_32 (abfd, ext->f_caux);
  intern->rfdBase       = H_GET_32 (abfd, ext->f_rfdBase);
  intern->crfd          = H_GET_32 (abfd, ext->f_crfd);

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      intern->lang       = ((ext->f_bits1[0] & FDR_BITS1_LANG_BIG)
			    >> FDR_BITS1_LANG_SH_BIG);
      intern->fMerge     = 0 != (ext->f_bits1[0] & FDR_BITS1_FMERGE_BIG);
      intern->fReadin    = 0 != (ext->f_bits1[0] & FDR_BITS1_FREADIN_BIG);
      intern->fBigendian = 0 != (ext->f_bits1[0] & FDR_BITS1_FBIGENDIAN_BIG);
      intern->glevel     = ((ext->f_bits2[0] & FDR_BITS2_GLEVEL_BIG)
			    >> FDR_BITS2_GLEVEL_SH_BIG);
    }
  else
    {
      intern->lang       = ((ext->f_bits1[0] & FDR_BITS1_LANG_LITTLE)
			    >> FDR_BITS1_LANG_SH_LITTLE);
      intern->fMerge     = 0 != (ext->f_bits1[0] & FDR_BITS1_FMERGE_LITTLE);
      intern->fReadin    = 0 != (ext->f_bits1[0] & FDR_BITS1_FREADIN_LITTLE);
      intern->fBigendian = 0 != (ext->f_bits1[0] & FDR_BITS1_FBIGENDIAN_LITTLE);
      intern->glevel     = ((ext->f_bits2[0] & FDR_BITS2_GLEVEL_LITTLE)
			    >> FDR_BITS2_GLEVEL_SH_LITTLE);
    }
  intern->reserved = 0;

  intern->cbLineOffset  = ECOFF_GET_OFF (abfd, ext->f_cbLineOffset);
  intern->cbLine        = ECOFF_GET_OFF (abfd, ext->f_cbLine);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out the file descriptor record.  */

static void
ecoff_swap_fdr_out (bfd *abfd, const FDR *intern_copy, void * ext_ptr)
{
  struct fdr_ext *ext = (struct fdr_ext *) ext_ptr;
  FDR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  ECOFF_PUT_OFF (abfd, intern->adr,       ext->f_adr);
  H_PUT_32      (abfd, intern->rss,       ext->f_rss);
  H_PUT_32      (abfd, intern->issBase,   ext->f_issBase);
  ECOFF_PUT_OFF (abfd, intern->cbSs,      ext->f_cbSs);
  H_PUT_32      (abfd, intern->isymBase,  ext->f_isymBase);
  H_PUT_32      (abfd, intern->csym,      ext->f_csym);
  H_PUT_32      (abfd, intern->ilineBase, ext->f_ilineBase);
  H_PUT_32      (abfd, intern->cline,     ext->f_cline);
  H_PUT_32      (abfd, intern->ioptBase,  ext->f_ioptBase);
  H_PUT_32      (abfd, intern->copt,      ext->f_copt);
#if defined (ECOFF_32) || defined (ECOFF_SIGNED_32)
  H_PUT_16      (abfd, intern->ipdFirst,  ext->f_ipdFirst);
  H_PUT_16      (abfd, intern->cpd,       ext->f_cpd);
#endif
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  H_PUT_32      (abfd, intern->ipdFirst,  ext->f_ipdFirst);
  H_PUT_32      (abfd, intern->cpd,       ext->f_cpd);
#endif
  H_PUT_32      (abfd, intern->iauxBase,  ext->f_iauxBase);
  H_PUT_32      (abfd, intern->caux,      ext->f_caux);
  H_PUT_32      (abfd, intern->rfdBase,   ext->f_rfdBase);
  H_PUT_32      (abfd, intern->crfd,      ext->f_crfd);

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      ext->f_bits1[0] = (((intern->lang << FDR_BITS1_LANG_SH_BIG)
			  & FDR_BITS1_LANG_BIG)
			 | (intern->fMerge ? FDR_BITS1_FMERGE_BIG : 0)
			 | (intern->fReadin ? FDR_BITS1_FREADIN_BIG : 0)
			 | (intern->fBigendian ? FDR_BITS1_FBIGENDIAN_BIG : 0));
      ext->f_bits2[0] = ((intern->glevel << FDR_BITS2_GLEVEL_SH_BIG)
			 & FDR_BITS2_GLEVEL_BIG);
      ext->f_bits2[1] = 0;
      ext->f_bits2[2] = 0;
    }
  else
    {
      ext->f_bits1[0] = (((intern->lang << FDR_BITS1_LANG_SH_LITTLE)
			  & FDR_BITS1_LANG_LITTLE)
			 | (intern->fMerge ? FDR_BITS1_FMERGE_LITTLE : 0)
			 | (intern->fReadin ? FDR_BITS1_FREADIN_LITTLE : 0)
			 | (intern->fBigendian ? FDR_BITS1_FBIGENDIAN_LITTLE : 0));
      ext->f_bits2[0] = ((intern->glevel << FDR_BITS2_GLEVEL_SH_LITTLE)
			 & FDR_BITS2_GLEVEL_LITTLE);
      ext->f_bits2[1] = 0;
      ext->f_bits2[2] = 0;
    }

  ECOFF_PUT_OFF (abfd, intern->cbLineOffset, ext->f_cbLineOffset);
  ECOFF_PUT_OFF (abfd, intern->cbLine, ext->f_cbLine);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in the procedure descriptor record.  */

static void
ecoff_swap_pdr_in (bfd *abfd, void * ext_copy, PDR *intern)
{
  struct pdr_ext ext[1];

  *ext = *(struct pdr_ext *) ext_copy;

  memset ((void *) intern, 0, sizeof (*intern));

  intern->adr           = ECOFF_GET_OFF (abfd, ext->p_adr);
  intern->isym          = H_GET_32 (abfd, ext->p_isym);
  intern->iline         = H_GET_32 (abfd, ext->p_iline);
  intern->regmask       = H_GET_32 (abfd, ext->p_regmask);
  intern->regoffset     = H_GET_S32 (abfd, ext->p_regoffset);
  intern->iopt          = H_GET_S32 (abfd, ext->p_iopt);
  intern->fregmask      = H_GET_32 (abfd, ext->p_fregmask);
  intern->fregoffset    = H_GET_S32 (abfd, ext->p_fregoffset);
  intern->frameoffset   = H_GET_S32 (abfd, ext->p_frameoffset);
  intern->framereg      = H_GET_16 (abfd, ext->p_framereg);
  intern->pcreg         = H_GET_16 (abfd, ext->p_pcreg);
  intern->lnLow         = H_GET_32 (abfd, ext->p_lnLow);
  intern->lnHigh        = H_GET_32 (abfd, ext->p_lnHigh);
  intern->cbLineOffset  = ECOFF_GET_OFF (abfd, ext->p_cbLineOffset);

#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  if (intern->isym == (signed long) 0xffffffff)
    intern->isym = -1;
  if (intern->iline == (signed long) 0xffffffff)
    intern->iline = -1;

  intern->gp_prologue = H_GET_8 (abfd, ext->p_gp_prologue);
  if (bfd_header_big_endian (abfd))
    {
      intern->gp_used = 0 != (ext->p_bits1[0] & PDR_BITS1_GP_USED_BIG);
      intern->reg_frame = 0 != (ext->p_bits1[0] & PDR_BITS1_REG_FRAME_BIG);
      intern->prof = 0 != (ext->p_bits1[0] & PDR_BITS1_PROF_BIG);
      intern->reserved = (((ext->p_bits1[0] & PDR_BITS1_RESERVED_BIG)
			   << PDR_BITS1_RESERVED_SH_LEFT_BIG)
			  | ((ext->p_bits2[0] & PDR_BITS2_RESERVED_BIG)
			     >> PDR_BITS2_RESERVED_SH_BIG));
    }
  else
    {
      intern->gp_used = 0 != (ext->p_bits1[0] & PDR_BITS1_GP_USED_LITTLE);
      intern->reg_frame = 0 != (ext->p_bits1[0] & PDR_BITS1_REG_FRAME_LITTLE);
      intern->prof = 0 != (ext->p_bits1[0] & PDR_BITS1_PROF_LITTLE);
      intern->reserved = (((ext->p_bits1[0] & PDR_BITS1_RESERVED_LITTLE)
			   >> PDR_BITS1_RESERVED_SH_LITTLE)
			  | ((ext->p_bits2[0] & PDR_BITS2_RESERVED_LITTLE)
			     << PDR_BITS2_RESERVED_SH_LEFT_LITTLE));
    }
  intern->localoff = H_GET_8 (abfd, ext->p_localoff);
#endif

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out the procedure descriptor record.  */

static void
ecoff_swap_pdr_out (bfd *abfd, const PDR *intern_copy, void * ext_ptr)
{
  struct pdr_ext *ext = (struct pdr_ext *) ext_ptr;
  PDR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  ECOFF_PUT_OFF (abfd, intern->adr,          ext->p_adr);
  H_PUT_32      (abfd, intern->isym,         ext->p_isym);
  H_PUT_32      (abfd, intern->iline,        ext->p_iline);
  H_PUT_32      (abfd, intern->regmask,      ext->p_regmask);
  H_PUT_32      (abfd, intern->regoffset,    ext->p_regoffset);
  H_PUT_32      (abfd, intern->iopt,         ext->p_iopt);
  H_PUT_32      (abfd, intern->fregmask,     ext->p_fregmask);
  H_PUT_32      (abfd, intern->fregoffset,   ext->p_fregoffset);
  H_PUT_32      (abfd, intern->frameoffset,  ext->p_frameoffset);
  H_PUT_16      (abfd, intern->framereg,     ext->p_framereg);
  H_PUT_16      (abfd, intern->pcreg,        ext->p_pcreg);
  H_PUT_32      (abfd, intern->lnLow,        ext->p_lnLow);
  H_PUT_32      (abfd, intern->lnHigh,       ext->p_lnHigh);
  ECOFF_PUT_OFF (abfd, intern->cbLineOffset, ext->p_cbLineOffset);

#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  H_PUT_8       (abfd, intern->gp_prologue,  ext->p_gp_prologue);

  if (bfd_header_big_endian (abfd))
    {
      ext->p_bits1[0] = ((intern->gp_used ? PDR_BITS1_GP_USED_BIG : 0)
			 | (intern->reg_frame ? PDR_BITS1_REG_FRAME_BIG : 0)
			 | (intern->prof ? PDR_BITS1_PROF_BIG : 0)
			 | ((intern->reserved
			     >> PDR_BITS1_RESERVED_SH_LEFT_BIG)
			    & PDR_BITS1_RESERVED_BIG));
      ext->p_bits2[0] = ((intern->reserved << PDR_BITS2_RESERVED_SH_BIG)
			 & PDR_BITS2_RESERVED_BIG);
    }
  else
    {
      ext->p_bits1[0] = ((intern->gp_used ? PDR_BITS1_GP_USED_LITTLE : 0)
			 | (intern->reg_frame ? PDR_BITS1_REG_FRAME_LITTLE : 0)
			 | (intern->prof ? PDR_BITS1_PROF_LITTLE : 0)
			 | ((intern->reserved << PDR_BITS1_RESERVED_SH_LITTLE)
			    & PDR_BITS1_RESERVED_LITTLE));
      ext->p_bits2[0] = ((intern->reserved >>
			  PDR_BITS2_RESERVED_SH_LEFT_LITTLE)
			 & PDR_BITS2_RESERVED_LITTLE);
    }
  H_PUT_8 (abfd, intern->localoff, ext->p_localoff);
#endif

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in a symbol record.  */

static void
ecoff_swap_sym_in (bfd *abfd, void * ext_copy, SYMR *intern)
{
  struct sym_ext ext[1];

  *ext = *(struct sym_ext *) ext_copy;

  intern->iss           = H_GET_32 (abfd, ext->s_iss);
  intern->value         = ECOFF_GET_OFF (abfd, ext->s_value);

#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  if (intern->iss == (signed long) 0xffffffff)
    intern->iss = -1;
#endif  

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      intern->st          =  (ext->s_bits1[0] & SYM_BITS1_ST_BIG)
					     >> SYM_BITS1_ST_SH_BIG;
      intern->sc          = ((ext->s_bits1[0] & SYM_BITS1_SC_BIG)
					     << SYM_BITS1_SC_SH_LEFT_BIG)
			  | ((ext->s_bits2[0] & SYM_BITS2_SC_BIG)
					     >> SYM_BITS2_SC_SH_BIG);
      intern->reserved    = 0 != (ext->s_bits2[0] & SYM_BITS2_RESERVED_BIG);
      intern->index       = ((ext->s_bits2[0] & SYM_BITS2_INDEX_BIG)
					     << SYM_BITS2_INDEX_SH_LEFT_BIG)
			  | (ext->s_bits3[0] << SYM_BITS3_INDEX_SH_LEFT_BIG)
			  | (ext->s_bits4[0] << SYM_BITS4_INDEX_SH_LEFT_BIG);
    }
  else
    {
      intern->st          =  (ext->s_bits1[0] & SYM_BITS1_ST_LITTLE)
					     >> SYM_BITS1_ST_SH_LITTLE;
      intern->sc          = ((ext->s_bits1[0] & SYM_BITS1_SC_LITTLE)
					     >> SYM_BITS1_SC_SH_LITTLE)
			  | ((ext->s_bits2[0] & SYM_BITS2_SC_LITTLE)
					     << SYM_BITS2_SC_SH_LEFT_LITTLE);
      intern->reserved    = 0 != (ext->s_bits2[0] & SYM_BITS2_RESERVED_LITTLE);
      intern->index       = ((ext->s_bits2[0] & SYM_BITS2_INDEX_LITTLE)
					     >> SYM_BITS2_INDEX_SH_LITTLE)
			  | (ext->s_bits3[0] << SYM_BITS3_INDEX_SH_LEFT_LITTLE)
			  | ((unsigned int) ext->s_bits4[0]
			     << SYM_BITS4_INDEX_SH_LEFT_LITTLE);
    }

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out a symbol record.  */

static void
ecoff_swap_sym_out (bfd *abfd, const SYMR *intern_copy, void * ext_ptr)
{
  struct sym_ext *ext = (struct sym_ext *) ext_ptr;
  SYMR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  H_PUT_32 (abfd, intern->iss, ext->s_iss);
  ECOFF_PUT_OFF (abfd, intern->value, ext->s_value);

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      ext->s_bits1[0] = (((intern->st << SYM_BITS1_ST_SH_BIG)
			  & SYM_BITS1_ST_BIG)
			 | ((intern->sc >> SYM_BITS1_SC_SH_LEFT_BIG)
			    & SYM_BITS1_SC_BIG));
      ext->s_bits2[0] = (((intern->sc << SYM_BITS2_SC_SH_BIG)
			  & SYM_BITS2_SC_BIG)
			 | (intern->reserved ? SYM_BITS2_RESERVED_BIG : 0)
			 | ((intern->index >> SYM_BITS2_INDEX_SH_LEFT_BIG)
			    & SYM_BITS2_INDEX_BIG));
      ext->s_bits3[0] = (intern->index >> SYM_BITS3_INDEX_SH_LEFT_BIG) & 0xff;
      ext->s_bits4[0] = (intern->index >> SYM_BITS4_INDEX_SH_LEFT_BIG) & 0xff;
    }
  else
    {
      ext->s_bits1[0] = (((intern->st << SYM_BITS1_ST_SH_LITTLE)
			  & SYM_BITS1_ST_LITTLE)
			 | ((intern->sc << SYM_BITS1_SC_SH_LITTLE)
			    & SYM_BITS1_SC_LITTLE));
      ext->s_bits2[0] = (((intern->sc >> SYM_BITS2_SC_SH_LEFT_LITTLE)
			  & SYM_BITS2_SC_LITTLE)
			 | (intern->reserved ? SYM_BITS2_RESERVED_LITTLE : 0)
			 | ((intern->index << SYM_BITS2_INDEX_SH_LITTLE)
			    & SYM_BITS2_INDEX_LITTLE));
      ext->s_bits3[0] = (intern->index >> SYM_BITS3_INDEX_SH_LEFT_LITTLE) & 0xff;
      ext->s_bits4[0] = (intern->index >> SYM_BITS4_INDEX_SH_LEFT_LITTLE) & 0xff;
    }

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in an external symbol record.  */

static void
ecoff_swap_ext_in (bfd *abfd, void * ext_copy, EXTR *intern)
{
  struct ext_ext ext[1];

  *ext = *(struct ext_ext *) ext_copy;

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      intern->jmptbl      = 0 != (ext->es_bits1[0] & EXT_BITS1_JMPTBL_BIG);
      intern->cobol_main  = 0 != (ext->es_bits1[0] & EXT_BITS1_COBOL_MAIN_BIG);
      intern->weakext     = 0 != (ext->es_bits1[0] & EXT_BITS1_WEAKEXT_BIG);
    }
  else
    {
      intern->jmptbl      = 0 != (ext->es_bits1[0] & EXT_BITS1_JMPTBL_LITTLE);
      intern->cobol_main  = 0 != (ext->es_bits1[0] & EXT_BITS1_COBOL_MAIN_LITTLE);
      intern->weakext     = 0 != (ext->es_bits1[0] & EXT_BITS1_WEAKEXT_LITTLE);
    }
  intern->reserved = 0;

#if defined (ECOFF_32) || defined (ECOFF_SIGNED_32)
  intern->ifd = H_GET_S16 (abfd, ext->es_ifd);
#endif
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  intern->ifd = H_GET_S32 (abfd, ext->es_ifd);
#endif

  ecoff_swap_sym_in (abfd, &ext->es_asym, &intern->asym);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out an external symbol record.  */

static void
ecoff_swap_ext_out (bfd *abfd, const EXTR *intern_copy, void * ext_ptr)
{
  struct ext_ext *ext = (struct ext_ext *) ext_ptr;
  EXTR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  /* Now the fun stuff...  */
  if (bfd_header_big_endian (abfd))
    {
      ext->es_bits1[0] = ((intern->jmptbl ? EXT_BITS1_JMPTBL_BIG : 0)
			  | (intern->cobol_main ? EXT_BITS1_COBOL_MAIN_BIG : 0)
			  | (intern->weakext ? EXT_BITS1_WEAKEXT_BIG : 0));
      ext->es_bits2[0] = 0;
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
      ext->es_bits2[1] = 0;
      ext->es_bits2[2] = 0;
#endif
    }
  else
    {
      ext->es_bits1[0] = ((intern->jmptbl ? EXT_BITS1_JMPTBL_LITTLE : 0)
			  | (intern->cobol_main ? EXT_BITS1_COBOL_MAIN_LITTLE : 0)
			  | (intern->weakext ? EXT_BITS1_WEAKEXT_LITTLE : 0));
      ext->es_bits2[0] = 0;
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
      ext->es_bits2[1] = 0;
      ext->es_bits2[2] = 0;
#endif
    }

#if defined (ECOFF_32) || defined (ECOFF_SIGNED_32)
  H_PUT_S16 (abfd, intern->ifd, ext->es_ifd);
#endif
#if defined (ECOFF_64) || defined (ECOFF_SIGNED_64)
  H_PUT_S32 (abfd, intern->ifd, ext->es_ifd);
#endif

  ecoff_swap_sym_out (abfd, &intern->asym, &ext->es_asym);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in a relative file descriptor.  */

static void
ecoff_swap_rfd_in (bfd *abfd, void * ext_ptr, RFDT *intern)
{
  struct rfd_ext *ext = (struct rfd_ext *) ext_ptr;

  *intern = H_GET_32 (abfd, ext->rfd);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out a relative file descriptor.  */

static void
ecoff_swap_rfd_out (bfd *abfd, const RFDT *intern, void * ext_ptr)
{
  struct rfd_ext *ext = (struct rfd_ext *) ext_ptr;

  H_PUT_32 (abfd, *intern, ext->rfd);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in an optimization symbol.  */

static void
ecoff_swap_opt_in (bfd *abfd, void * ext_copy, OPTR * intern)
{
  struct opt_ext ext[1];

  *ext = *(struct opt_ext *) ext_copy;

  if (bfd_header_big_endian (abfd))
    {
      intern->ot = ext->o_bits1[0];
      intern->value = (((unsigned int) ext->o_bits2[0]
			<< OPT_BITS2_VALUE_SH_LEFT_BIG)
		       | ((unsigned int) ext->o_bits3[0]
			  << OPT_BITS2_VALUE_SH_LEFT_BIG)
		       | ((unsigned int) ext->o_bits4[0]
			  << OPT_BITS2_VALUE_SH_LEFT_BIG));
    }
  else
    {
      intern->ot = ext->o_bits1[0];
      intern->value = ((ext->o_bits2[0] << OPT_BITS2_VALUE_SH_LEFT_LITTLE)
		       | (ext->o_bits3[0] << OPT_BITS2_VALUE_SH_LEFT_LITTLE)
		       | (ext->o_bits4[0] << OPT_BITS2_VALUE_SH_LEFT_LITTLE));
    }

  _bfd_ecoff_swap_rndx_in (bfd_header_big_endian (abfd),
			   &ext->o_rndx, &intern->rndx);

  intern->offset = H_GET_32 (abfd, ext->o_offset);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out an optimization symbol.  */

static void
ecoff_swap_opt_out (bfd *abfd, const OPTR *intern_copy, void * ext_ptr)
{
  struct opt_ext *ext = (struct opt_ext *) ext_ptr;
  OPTR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  if (bfd_header_big_endian (abfd))
    {
      ext->o_bits1[0] = intern->ot;
      ext->o_bits2[0] = intern->value >> OPT_BITS2_VALUE_SH_LEFT_BIG;
      ext->o_bits3[0] = intern->value >> OPT_BITS3_VALUE_SH_LEFT_BIG;
      ext->o_bits4[0] = intern->value >> OPT_BITS4_VALUE_SH_LEFT_BIG;
    }
  else
    {
      ext->o_bits1[0] = intern->ot;
      ext->o_bits2[0] = intern->value >> OPT_BITS2_VALUE_SH_LEFT_LITTLE;
      ext->o_bits3[0] = intern->value >> OPT_BITS3_VALUE_SH_LEFT_LITTLE;
      ext->o_bits4[0] = intern->value >> OPT_BITS4_VALUE_SH_LEFT_LITTLE;
    }

  _bfd_ecoff_swap_rndx_out (bfd_header_big_endian (abfd),
			    &intern->rndx, &ext->o_rndx);

  H_PUT_32 (abfd, intern->value, ext->o_offset);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap in a dense number.  */

static void
ecoff_swap_dnr_in (bfd *abfd, void * ext_copy, DNR *intern)
{
  struct dnr_ext ext[1];

  *ext = *(struct dnr_ext *) ext_copy;

  intern->rfd = H_GET_32 (abfd, ext->d_rfd);
  intern->index = H_GET_32 (abfd, ext->d_index);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

/* Swap out a dense number.  */

static void
ecoff_swap_dnr_out (bfd *abfd, const DNR *intern_copy, void * ext_ptr)
{
  struct dnr_ext *ext = (struct dnr_ext *) ext_ptr;
  DNR intern[1];

  /* Make it reasonable to do in-place.  */
  *intern = *intern_copy;

  H_PUT_32 (abfd, intern->rfd, ext->d_rfd);
  H_PUT_32 (abfd, intern->index, ext->d_index);

#ifdef TEST
  if (memcmp ((char *) ext, (char *) intern, sizeof (*intern)) != 0)
    abort ();
#endif
}

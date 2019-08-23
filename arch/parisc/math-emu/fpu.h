/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */
/*
 * BEGIN_DESC
 * 
 *  File: 
 *      @(#)	pa/fp/fpu.h		$Revision: 1.1 $
 * 
 *  Purpose:
 *      <<please update with a synopis of the functionality provided by this file>>
 * 
 * 
 * END_DESC  
*/

#ifdef __NO_PA_HDRS
    PA header file -- do not include this header file for non-PA builds.
#endif


#ifndef _MACHINE_FPU_INCLUDED /* allows multiple inclusion */
#define _MACHINE_FPU_INCLUDED

#if 0
#ifndef _SYS_STDSYMS_INCLUDED
#    include <sys/stdsyms.h>
#endif   /* _SYS_STDSYMS_INCLUDED  */
#include  <machine/pdc/pdc_rqsts.h>
#endif

#define PA83_FPU_FLAG    0x00000001
#define PA89_FPU_FLAG    0x00000002
#define PA2_0_FPU_FLAG   0x00000010

#define TIMEX_EXTEN_FLAG 0x00000004

#define ROLEX_EXTEN_FLAG 0x00000008
#define COPR_FP 	0x00000080	/* Floating point -- Coprocessor 0 */
#define SFU_MPY_DIVIDE	0x00008000	/* Multiply/Divide __ SFU 0 */


#define EM_FPU_TYPE_OFFSET 272

/* version of EMULATION software for COPR,0,0 instruction */
#define EMULATION_VERSION 4

/*
 * The only was to differeniate between TIMEX and ROLEX (or PCX-S and PCX-T)
 * is thorough the potential type field from the PDC_MODEL call.  The 
 * following flags are used at assist this differeniation.
 */

#define ROLEX_POTENTIAL_KEY_FLAGS	PDC_MODEL_CPU_KEY_WORD_TO_IO
#define TIMEX_POTENTIAL_KEY_FLAGS	(PDC_MODEL_CPU_KEY_QUAD_STORE | \
					 PDC_MODEL_CPU_KEY_RECIP_SQRT)


#endif /* ! _MACHINE_FPU_INCLUDED */

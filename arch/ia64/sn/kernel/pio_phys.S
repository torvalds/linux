/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2005 Silicon Graphics, Inc. All rights reserved.
 *
 * This file contains macros used to access MMR registers via
 * uncached physical addresses.
 *      pio_phys_read_mmr  - read an MMR
 *      pio_phys_write_mmr - write an MMR
 *      pio_atomic_phys_write_mmrs - atomically write 1 or 2 MMRs with psr.ic=0
 *              Second MMR will be skipped if address is NULL
 *
 * Addresses passed to these routines should be uncached physical addresses
 * 	ie., 0x80000....
 */



#include <asm/asmmacro.h>
#include <asm/page.h>

GLOBAL_ENTRY(pio_phys_read_mmr)
	.prologue
	.regstk 1,0,0,0
	.body
	mov r2=psr
	rsm psr.i | psr.dt
	;;
	srlz.d
	ld8.acq r8=[r32]
	;;
	mov psr.l=r2;;
	srlz.d
	br.ret.sptk.many rp
END(pio_phys_read_mmr)

GLOBAL_ENTRY(pio_phys_write_mmr)
	.prologue
	.regstk 2,0,0,0
	.body
	mov r2=psr
	rsm psr.i | psr.dt
	;;
	srlz.d
	st8.rel [r32]=r33
	;;
	mov psr.l=r2;;
	srlz.d
	br.ret.sptk.many rp
END(pio_phys_write_mmr)

GLOBAL_ENTRY(pio_atomic_phys_write_mmrs)
	.prologue
	.regstk 4,0,0,0
	.body
	mov r2=psr
	cmp.ne p9,p0=r34,r0;
	rsm psr.i | psr.dt | psr.ic
	;;
	srlz.d
	st8.rel [r32]=r33
(p9)	st8.rel [r34]=r35
	;;
	mov psr.l=r2;;
	srlz.d
	br.ret.sptk.many rp
END(pio_atomic_phys_write_mmrs)



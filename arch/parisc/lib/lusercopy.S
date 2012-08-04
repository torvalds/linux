/*
 *    User Space Access Routines
 *
 *    Copyright (C) 2000-2002 Hewlett-Packard (John Marvin)
 *    Copyright (C) 2000 Richard Hirst <rhirst with parisc-linux.org>
 *    Copyright (C) 2001 Matthieu Delahaye <delahaym at esiee.fr>
 *    Copyright (C) 2003 Randolph Chung <tausq with parisc-linux.org>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * These routines still have plenty of room for optimization
 * (word & doubleword load/store, dual issue, store hints, etc.).
 */

/*
 * The following routines assume that space register 3 (sr3) contains
 * the space id associated with the current users address space.
 */


	.text
	
#include <asm/assembly.h>
#include <asm/errno.h>
#include <linux/linkage.h>

	/*
	 * get_sr gets the appropriate space value into
	 * sr1 for kernel/user space access, depending
	 * on the flag stored in the task structure.
	 */

	.macro  get_sr
	mfctl       %cr30,%r1
	ldw         TI_SEGMENT(%r1),%r22
	mfsp        %sr3,%r1
	or,<>       %r22,%r0,%r0
	copy        %r0,%r1
	mtsp        %r1,%sr1
	.endm

	.macro fixup_branch lbl
	ldil	    L%\lbl, %r1
	ldo	    R%\lbl(%r1), %r1
	bv          %r0(%r1)
	.endm

	/*
	 * unsigned long lclear_user(void *to, unsigned long n)
	 *
	 * Returns 0 for success.
	 * otherwise, returns number of bytes not transferred.
	 */

ENTRY(lclear_user)
	.proc
	.callinfo NO_CALLS
	.entry
	comib,=,n   0,%r25,$lclu_done
	get_sr
$lclu_loop:
	addib,<>    -1,%r25,$lclu_loop
1:      stbs,ma     %r0,1(%sr1,%r26)

$lclu_done:
	bv          %r0(%r2)
	copy        %r25,%r28
	.exit
ENDPROC(lclear_user)

	.section .fixup,"ax"
2:      fixup_branch $lclu_done
	ldo        1(%r25),%r25
	.previous

	.section __ex_table,"aw"
	ASM_ULONG_INSN 1b,2b
	.previous

	.procend

	/*
	 * long lstrnlen_user(char *s, long n)
	 *
	 * Returns 0 if exception before zero byte or reaching N,
	 *         N+1 if N would be exceeded,
	 *         else strlen + 1 (i.e. includes zero byte).
	 */

ENTRY(lstrnlen_user)
	.proc
	.callinfo NO_CALLS
	.entry
	comib,=     0,%r25,$lslen_nzero
	copy	    %r26,%r24
	get_sr
1:      ldbs,ma     1(%sr1,%r26),%r1
$lslen_loop:
	comib,=,n   0,%r1,$lslen_done
	addib,<>    -1,%r25,$lslen_loop
2:      ldbs,ma     1(%sr1,%r26),%r1
$lslen_done:
	bv          %r0(%r2)
	sub	    %r26,%r24,%r28
	.exit

$lslen_nzero:
	b           $lslen_done
	ldo         1(%r26),%r26 /* special case for N == 0 */
ENDPROC(lstrnlen_user)

	.section .fixup,"ax"
3:      fixup_branch $lslen_done
	copy        %r24,%r26    /* reset r26 so 0 is returned on fault */
	.previous

	.section __ex_table,"aw"
	ASM_ULONG_INSN 1b,3b
	ASM_ULONG_INSN 2b,3b
	.previous

	.procend

	.end

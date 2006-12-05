/*
 * relocate_kernel.S for kexec
 * Created by <nschichan@corp.free.fr> on Thu Oct 12 17:49:57 2006
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/page.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>

	.globl relocate_new_kernel
relocate_new_kernel:

	PTR_L	s0, kexec_indirection_page
	PTR_L	s1, kexec_start_address

process_entry:
	PTR_L	s2, (s0)
	PTR_ADD	s0, s0, SZREG

	/* destination page */
	and	s3, s2, 0x1
	beq	s3, zero, 1f
	and	s4, s2, ~0x1	/* store destination addr in s4 */
	move	a0, s4
	b	process_entry

1:
	/* indirection page, update s0  */
	and	s3, s2, 0x2
	beq	s3, zero, 1f
	and	s0, s2, ~0x2
	b	process_entry

1:
	/* done page */
	and	s3, s2, 0x4
	beq	s3, zero, 1f
	b	done
1:
	/* source page */
	and	s3, s2, 0x8
	beq	s3, zero, process_entry
	and	s2, s2, ~0x8
	li	s6, (1 << PAGE_SHIFT) / SZREG

copy_word:
	/* copy page word by word */
	REG_L	s5, (s2)
	REG_S	s5, (s4)
	INT_ADD	s4, s4, SZREG
	INT_ADD	s2, s2, SZREG
	INT_SUB	s6, s6, 1
	beq	s6, zero, process_entry
	b	copy_word
	b	process_entry

done:
	/* jump to kexec_start_address */
	j	s1

	.globl kexec_start_address
kexec_start_address:
	.long	0x0

	.globl kexec_indirection_page
kexec_indirection_page:
	.long	0x0

relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel

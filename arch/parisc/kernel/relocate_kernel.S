/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/kexec.h>

#include <asm/assembly.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/psw.h>

.level PA_ASM_LEVEL

.macro	kexec_param name
.align 8
ENTRY(kexec\()_\name)
#ifdef CONFIG_64BIT
	.dword 0
#else
	.word 0
#endif

ENTRY(kexec\()_\name\()_offset)
	.word kexec\()_\name - relocate_new_kernel
.endm

.text

/* args:
 * r26 - kimage->head
 * r25 - start address of kernel
 * r24 - physical address of relocate code
 */

ENTRY_CFI(relocate_new_kernel)
0:	copy	%arg1, %rp
	/* disable I and Q bit, so we are allowed to execute RFI */
	rsm PSW_SM_I, %r0
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	rsm PSW_SM_Q, %r0
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	/*
	 * After return-from-interrupt, we want to run without Code/Data
	 * translation enabled just like on a normal boot.
	 */

	/* calculate new physical execution address */
	ldo	1f-0b(%arg2), %r1
	mtctl	%r0, %cr17 /* IIASQ */
	mtctl	%r0, %cr17 /* IIASQ */
	mtctl	%r1, %cr18 /* IIAOQ */
	ldo	4(%r1),%r1
	mtctl	%r1, %cr18 /* IIAOQ */
#ifdef CONFIG_64BIT
	depdi,z	1, PSW_W_BIT, 1, %r1
	mtctl	%r1, %cr22 /* IPSW */
#else
	mtctl	%r0, %cr22 /* IPSW */
#endif
	/* lets go... */
	rfi
1:	nop
	nop

.Lloop:
	LDREG,ma	REG_SZ(%arg0), %r3
	/* If crash kernel, no copy needed */
	cmpib,COND(=),n 0,%r3,boot

	bb,<,n		%r3, 31 - IND_DONE_BIT, boot
	bb,>=,n		%r3, 31 - IND_INDIRECTION_BIT, .Lnotind
	/* indirection, load and restart */
	movb		%r3, %arg0, .Lloop
	depi		0, 31, PAGE_SHIFT, %arg0

.Lnotind:
	bb,>=,n		%r3, 31 - IND_DESTINATION_BIT, .Lnotdest
	b		.Lloop
	copy		%r3, %r20

.Lnotdest:
	bb,>=		%r3, 31 - IND_SOURCE_BIT, .Lloop
	depi		0, 31, PAGE_SHIFT, %r3
	copy		%r3, %r21

	/* copy page */
	copy		%r0, %r18
	zdepi		1, 31 - PAGE_SHIFT, 1, %r18
	add		%r20, %r18, %r17

	depi		0, 31, PAGE_SHIFT, %r20
.Lcopy:
	copy		%r20, %r12
	LDREG,ma	REG_SZ(%r21), %r8
	LDREG,ma	REG_SZ(%r21), %r9
	LDREG,ma	REG_SZ(%r21), %r10
	LDREG,ma	REG_SZ(%r21), %r11
	STREG,ma	%r8, REG_SZ(%r20)
	STREG,ma	%r9, REG_SZ(%r20)
	STREG,ma	%r10, REG_SZ(%r20)
	STREG,ma	%r11, REG_SZ(%r20)

#ifndef CONFIG_64BIT
	LDREG,ma	REG_SZ(%r21), %r8
	LDREG,ma	REG_SZ(%r21), %r9
	LDREG,ma	REG_SZ(%r21), %r10
	LDREG,ma	REG_SZ(%r21), %r11
	STREG,ma	%r8, REG_SZ(%r20)
	STREG,ma	%r9, REG_SZ(%r20)
	STREG,ma	%r10, REG_SZ(%r20)
	STREG,ma	%r11, REG_SZ(%r20)
#endif

	fdc		%r0(%r12)
	cmpb,COND(<<)	%r20,%r17,.Lcopy
	fic		(%sr4, %r12)
	b,n		.Lloop

boot:
	mtctl	%r0, %cr15

	LDREG	kexec_free_mem-0b(%arg2), %arg0
	LDREG	kexec_cmdline-0b(%arg2), %arg1
	LDREG	kexec_initrd_end-0b(%arg2), %arg3
	LDREG	kexec_initrd_start-0b(%arg2), %arg2
	bv,n %r0(%rp)

ENDPROC_CFI(relocate_new_kernel);

ENTRY(relocate_new_kernel_size)
       .word relocate_new_kernel_size - relocate_new_kernel

kexec_param cmdline
kexec_param initrd_start
kexec_param initrd_end
kexec_param free_mem

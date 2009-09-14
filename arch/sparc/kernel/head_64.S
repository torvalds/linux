/* head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996, 1997, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 David Sitsky (David.Sitsky@anu.edu.au)
 * Copyright (C) 1997, 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/version.h>
#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/processor.h>
#include <asm/lsu.h>
#include <asm/dcr.h>
#include <asm/dcu.h>
#include <asm/head.h>
#include <asm/ttable.h>
#include <asm/mmu.h>
#include <asm/cpudata.h>
#include <asm/pil.h>
#include <asm/estate.h>
#include <asm/sfafsr.h>
#include <asm/unistd.h>
	
/* This section from from _start to sparc64_boot_end should fit into
 * 0x0000000000404000 to 0x0000000000408000.
 */
	.text
	.globl	start, _start, stext, _stext
_start:
start:
_stext:
stext:
! 0x0000000000404000
	b	sparc64_boot
	 flushw					/* Flush register file.      */

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
        .global root_flags, ram_flags, root_dev
        .global sparc_ramdisk_image, sparc_ramdisk_size
	.global sparc_ramdisk_image64

        .ascii  "HdrS"
        .word   LINUX_VERSION_CODE

	/* History:
	 *
	 * 0x0300 : Supports being located at other than 0x4000
	 * 0x0202 : Supports kernel params string
	 * 0x0201 : Supports reboot_command
	 */
	.half   0x0301          /* HdrS version */

root_flags:
        .half   1
root_dev:
        .half   0
ram_flags:
        .half   0
sparc_ramdisk_image:
        .word   0
sparc_ramdisk_size:
        .word   0
        .xword  reboot_command
	.xword	bootstr_info
sparc_ramdisk_image64:
	.xword	0
	.word	_end

	/* PROM cif handler code address is in %o4.  */
sparc64_boot:
	mov	%o4, %l7

	/* We need to remap the kernel.  Use position independant
	 * code to remap us to KERNBASE.
	 *
	 * SILO can invoke us with 32-bit address masking enabled,
	 * so make sure that's clear.
	 */
	rdpr	%pstate, %g1
	andn	%g1, PSTATE_AM, %g1
	wrpr	%g1, 0x0, %pstate
	ba,a,pt	%xcc, 1f

	.globl	prom_finddev_name, prom_chosen_path, prom_root_node
	.globl	prom_getprop_name, prom_mmu_name, prom_peer_name
	.globl	prom_callmethod_name, prom_translate_name, prom_root_compatible
	.globl	prom_map_name, prom_unmap_name, prom_mmu_ihandle_cache
	.globl	prom_boot_mapped_pc, prom_boot_mapping_mode
	.globl	prom_boot_mapping_phys_high, prom_boot_mapping_phys_low
	.globl	prom_compatible_name, prom_cpu_path, prom_cpu_compatible
	.globl	is_sun4v, sun4v_chip_type, prom_set_trap_table_name
prom_peer_name:
	.asciz	"peer"
prom_compatible_name:
	.asciz	"compatible"
prom_finddev_name:
	.asciz	"finddevice"
prom_chosen_path:
	.asciz	"/chosen"
prom_cpu_path:
	.asciz	"/cpu"
prom_getprop_name:
	.asciz	"getprop"
prom_mmu_name:
	.asciz	"mmu"
prom_callmethod_name:
	.asciz	"call-method"
prom_translate_name:
	.asciz	"translate"
prom_map_name:
	.asciz	"map"
prom_unmap_name:
	.asciz	"unmap"
prom_set_trap_table_name:
	.asciz	"SUNW,set-trap-table"
prom_sun4v_name:
	.asciz	"sun4v"
prom_niagara_prefix:
	.asciz	"SUNW,UltraSPARC-T"
	.align	4
prom_root_compatible:
	.skip	64
prom_cpu_compatible:
	.skip	64
prom_root_node:
	.word	0
prom_mmu_ihandle_cache:
	.word	0
prom_boot_mapped_pc:
	.word	0
prom_boot_mapping_mode:
	.word	0
	.align	8
prom_boot_mapping_phys_high:
	.xword	0
prom_boot_mapping_phys_low:
	.xword	0
is_sun4v:
	.word	0
sun4v_chip_type:
	.word	SUN4V_CHIP_INVALID
1:
	rd	%pc, %l0

	mov	(1b - prom_peer_name), %l1
	sub	%l0, %l1, %l1
	mov	0, %l2

	/* prom_root_node = prom_peer(0) */
	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "peer"
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 1
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l2, [%sp + 2047 + 128 + 0x18]	! arg1, 0
	stx	%g0, [%sp + 2047 + 128 + 0x20]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	ldx	[%sp + 2047 + 128 + 0x20], %l4	! prom root node
	mov	(1b - prom_root_node), %l1
	sub	%l0, %l1, %l1
	stw	%l4, [%l1]

	mov	(1b - prom_getprop_name), %l1
	mov	(1b - prom_compatible_name), %l2
	mov	(1b - prom_root_compatible), %l5
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	sub	%l0, %l5, %l5

	/* prom_getproperty(prom_root_node, "compatible",
	 *                  &prom_root_compatible, 64)
	 */
	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "getprop"
	mov	4, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 4
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l4, [%sp + 2047 + 128 + 0x18]	! arg1, prom_root_node
	stx	%l2, [%sp + 2047 + 128 + 0x20]	! arg2, "compatible"
	stx	%l5, [%sp + 2047 + 128 + 0x28]	! arg3, &prom_root_compatible
	mov	64, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x30]	! arg4, size
	stx	%g0, [%sp + 2047 + 128 + 0x38]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	mov	(1b - prom_finddev_name), %l1
	mov	(1b - prom_chosen_path), %l2
	mov	(1b - prom_boot_mapped_pc), %l3
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	sub	%l0, %l3, %l3
	stw	%l0, [%l3]
	sub	%sp, (192 + 128), %sp

	/* chosen_node = prom_finddevice("/chosen") */
	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "finddevice"
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 1
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l2, [%sp + 2047 + 128 + 0x18]	! arg1, "/chosen"
	stx	%g0, [%sp + 2047 + 128 + 0x20]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	ldx	[%sp + 2047 + 128 + 0x20], %l4	! chosen device node

	mov	(1b - prom_getprop_name), %l1
	mov	(1b - prom_mmu_name), %l2
	mov	(1b - prom_mmu_ihandle_cache), %l5
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	sub	%l0, %l5, %l5

	/* prom_mmu_ihandle_cache = prom_getint(chosen_node, "mmu") */
	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "getprop"
	mov	4, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 4
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l4, [%sp + 2047 + 128 + 0x18]	! arg1, chosen_node
	stx	%l2, [%sp + 2047 + 128 + 0x20]	! arg2, "mmu"
	stx	%l5, [%sp + 2047 + 128 + 0x28]	! arg3, &prom_mmu_ihandle_cache
	mov	4, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x30]	! arg4, sizeof(arg3)
	stx	%g0, [%sp + 2047 + 128 + 0x38]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	mov	(1b - prom_callmethod_name), %l1
	mov	(1b - prom_translate_name), %l2
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	lduw	[%l5], %l5			! prom_mmu_ihandle_cache

	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "call-method"
	mov	3, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 3
	mov	5, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 5
	stx	%l2, [%sp + 2047 + 128 + 0x18]	! arg1: "translate"
	stx	%l5, [%sp + 2047 + 128 + 0x20]	! arg2: prom_mmu_ihandle_cache
	/* PAGE align */
	srlx	%l0, 13, %l3
	sllx	%l3, 13, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x28]	! arg3: vaddr, our PC
	stx	%g0, [%sp + 2047 + 128 + 0x30]	! res1
	stx	%g0, [%sp + 2047 + 128 + 0x38]	! res2
	stx	%g0, [%sp + 2047 + 128 + 0x40]	! res3
	stx	%g0, [%sp + 2047 + 128 + 0x48]	! res4
	stx	%g0, [%sp + 2047 + 128 + 0x50]	! res5
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	ldx	[%sp + 2047 + 128 + 0x40], %l1	! translation mode
	mov	(1b - prom_boot_mapping_mode), %l4
	sub	%l0, %l4, %l4
	stw	%l1, [%l4]
	mov	(1b - prom_boot_mapping_phys_high), %l4
	sub	%l0, %l4, %l4
	ldx	[%sp + 2047 + 128 + 0x48], %l2	! physaddr high
	stx	%l2, [%l4 + 0x0]
	ldx	[%sp + 2047 + 128 + 0x50], %l3	! physaddr low
	/* 4MB align */
	srlx	%l3, 22, %l3
	sllx	%l3, 22, %l3
	stx	%l3, [%l4 + 0x8]

	/* Leave service as-is, "call-method" */
	mov	7, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 7
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	mov	(1b - prom_map_name), %l3
	sub	%l0, %l3, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x18]	! arg1: "map"
	/* Leave arg2 as-is, prom_mmu_ihandle_cache */
	mov	-1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x28]	! arg3: mode (-1 default)
	/* 4MB align the kernel image size. */
	set	(_end - KERNBASE), %l3
	set	((4 * 1024 * 1024) - 1), %l4
	add	%l3, %l4, %l3
	andn	%l3, %l4, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x30]	! arg4: roundup(ksize, 4MB)
	sethi	%hi(KERNBASE), %l3
	stx	%l3, [%sp + 2047 + 128 + 0x38]	! arg5: vaddr (KERNBASE)
	stx	%g0, [%sp + 2047 + 128 + 0x40]	! arg6: empty
	mov	(1b - prom_boot_mapping_phys_low), %l3
	sub	%l0, %l3, %l3
	ldx	[%l3], %l3
	stx	%l3, [%sp + 2047 + 128 + 0x48]	! arg7: phys addr
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	add	%sp, (192 + 128), %sp

	sethi	%hi(prom_root_compatible), %g1
	or	%g1, %lo(prom_root_compatible), %g1
	sethi	%hi(prom_sun4v_name), %g7
	or	%g7, %lo(prom_sun4v_name), %g7
	mov	5, %g3
90:	ldub	[%g7], %g2
	ldub	[%g1], %g4
	cmp	%g2, %g4
	bne,pn	%icc, 80f
	 add	%g7, 1, %g7
	subcc	%g3, 1, %g3
	bne,pt	%xcc, 90b
	 add	%g1, 1, %g1

	sethi	%hi(is_sun4v), %g1
	or	%g1, %lo(is_sun4v), %g1
	mov	1, %g7
	stw	%g7, [%g1]

	/* cpu_node = prom_finddevice("/cpu") */
	mov	(1b - prom_finddev_name), %l1
	mov	(1b - prom_cpu_path), %l2
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	sub	%sp, (192 + 128), %sp

	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "finddevice"
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 1
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l2, [%sp + 2047 + 128 + 0x18]	! arg1, "/cpu"
	stx	%g0, [%sp + 2047 + 128 + 0x20]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	ldx	[%sp + 2047 + 128 + 0x20], %l4	! cpu device node

	mov	(1b - prom_getprop_name), %l1
	mov	(1b - prom_compatible_name), %l2
	mov	(1b - prom_cpu_compatible), %l5
	sub	%l0, %l1, %l1
	sub	%l0, %l2, %l2
	sub	%l0, %l5, %l5

	/* prom_getproperty(cpu_node, "compatible",
	 *                  &prom_cpu_compatible, 64)
	 */
	stx	%l1, [%sp + 2047 + 128 + 0x00]	! service, "getprop"
	mov	4, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x08]	! num_args, 4
	mov	1, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x10]	! num_rets, 1
	stx	%l4, [%sp + 2047 + 128 + 0x18]	! arg1, cpu_node
	stx	%l2, [%sp + 2047 + 128 + 0x20]	! arg2, "compatible"
	stx	%l5, [%sp + 2047 + 128 + 0x28]	! arg3, &prom_cpu_compatible
	mov	64, %l3
	stx	%l3, [%sp + 2047 + 128 + 0x30]	! arg4, size
	stx	%g0, [%sp + 2047 + 128 + 0x38]	! ret1
	call	%l7
	 add	%sp, (2047 + 128), %o0		! argument array

	add	%sp, (192 + 128), %sp

	sethi	%hi(prom_cpu_compatible), %g1
	or	%g1, %lo(prom_cpu_compatible), %g1
	sethi	%hi(prom_niagara_prefix), %g7
	or	%g7, %lo(prom_niagara_prefix), %g7
	mov	17, %g3
90:	ldub	[%g7], %g2
	ldub	[%g1], %g4
	cmp	%g2, %g4
	bne,pn	%icc, 4f
	 add	%g7, 1, %g7
	subcc	%g3, 1, %g3
	bne,pt	%xcc, 90b
	 add	%g1, 1, %g1

	sethi	%hi(prom_cpu_compatible), %g1
	or	%g1, %lo(prom_cpu_compatible), %g1
	ldub	[%g1 + 17], %g2
	cmp	%g2, '1'
	be,pt	%xcc, 5f
	 mov	SUN4V_CHIP_NIAGARA1, %g4
	cmp	%g2, '2'
	be,pt	%xcc, 5f
	 mov	SUN4V_CHIP_NIAGARA2, %g4
4:
	mov	SUN4V_CHIP_UNKNOWN, %g4
5:	sethi	%hi(sun4v_chip_type), %g2
	or	%g2, %lo(sun4v_chip_type), %g2
	stw	%g4, [%g2]

80:
	BRANCH_IF_SUN4V(g1, jump_to_sun4u_init)
	BRANCH_IF_CHEETAH_BASE(g1,g7,cheetah_boot)
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g7,cheetah_plus_boot)
	ba,pt	%xcc, spitfire_boot
	 nop

cheetah_plus_boot:
	/* Preserve OBP chosen DCU and DCR register settings.  */
	ba,pt	%xcc, cheetah_generic_boot
	 nop

cheetah_boot:
	mov	DCR_BPE | DCR_RPE | DCR_SI | DCR_IFPOE | DCR_MS, %g1
	wr	%g1, %asr18

	sethi	%uhi(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g7
	or	%g7, %ulo(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g7
	sllx	%g7, 32, %g7
	or	%g7, DCU_DM | DCU_IM | DCU_DC | DCU_IC, %g7
	stxa	%g7, [%g0] ASI_DCU_CONTROL_REG
	membar	#Sync

cheetah_generic_boot:
	mov	TSB_EXTENSION_P, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	mov	TSB_EXTENSION_S, %g3
	stxa	%g0, [%g3] ASI_DMMU
	membar	#Sync

	mov	TSB_EXTENSION_N, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	ba,a,pt	%xcc, jump_to_sun4u_init

spitfire_boot:
	/* Typically PROM has already enabled both MMU's and both on-chip
	 * caches, but we do it here anyway just to be paranoid.
	 */
	mov	(LSU_CONTROL_IC|LSU_CONTROL_DC|LSU_CONTROL_IM|LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL
	membar	#Sync

jump_to_sun4u_init:
	/*
	 * Make sure we are in privileged mode, have address masking,
         * using the ordinary globals and have enabled floating
         * point.
	 *
	 * Again, typically PROM has left %pil at 13 or similar, and
	 * (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE) in %pstate.
         */
	wrpr    %g0, (PSTATE_PRIV|PSTATE_PEF|PSTATE_IE), %pstate
	wr	%g0, 0, %fprs

	set	sun4u_init, %g2
	jmpl    %g2 + %g0, %g0
	 nop

	__REF
sun4u_init:
	BRANCH_IF_SUN4V(g1, sun4v_init)

	/* Set ctx 0 */
	mov		PRIMARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_DMMU
	membar		#Sync

	mov		SECONDARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_DMMU
	membar	#Sync

	ba,pt		%xcc, sun4u_continue
	 nop

sun4v_init:
	/* Set ctx 0 */
	mov		PRIMARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_MMU
	membar		#Sync

	mov		SECONDARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_MMU
	membar		#Sync
	ba,pt		%xcc, niagara_tlb_fixup
	 nop

sun4u_continue:
	BRANCH_IF_ANY_CHEETAH(g1, g7, cheetah_tlb_fixup)

	ba,pt	%xcc, spitfire_tlb_fixup
	 nop

niagara_tlb_fixup:
	mov	3, %g2		/* Set TLB type to hypervisor. */
	sethi	%hi(tlb_type), %g1
	stw	%g2, [%g1 + %lo(tlb_type)]

	/* Patch copy/clear ops.  */
	sethi	%hi(sun4v_chip_type), %g1
	lduw	[%g1 + %lo(sun4v_chip_type)], %g1
	cmp	%g1, SUN4V_CHIP_NIAGARA1
	be,pt	%xcc, niagara_patch
	 cmp	%g1, SUN4V_CHIP_NIAGARA2
	be,pt	%xcc, niagara2_patch
	 nop

	call	generic_patch_copyops
	 nop
	call	generic_patch_bzero
	 nop
	call	generic_patch_pageops
	 nop

	ba,a,pt	%xcc, 80f
niagara2_patch:
	call	niagara2_patch_copyops
	 nop
	call	niagara_patch_bzero
	 nop
	call	niagara2_patch_pageops
	 nop

	ba,a,pt	%xcc, 80f

niagara_patch:
	call	niagara_patch_copyops
	 nop
	call	niagara_patch_bzero
	 nop
	call	niagara_patch_pageops
	 nop

80:
	/* Patch TLB/cache ops.  */
	call	hypervisor_patch_cachetlbops
	 nop

	ba,pt	%xcc, tlb_fixup_done
	 nop

cheetah_tlb_fixup:
	mov	2, %g2		/* Set TLB type to cheetah+. */
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g7,1f)

	mov	1, %g2		/* Set TLB type to cheetah. */

1:	sethi	%hi(tlb_type), %g1
	stw	%g2, [%g1 + %lo(tlb_type)]

	/* Patch copy/page operations to cheetah optimized versions. */
	call	cheetah_patch_copyops
	 nop
	call	cheetah_patch_copy_page
	 nop
	call	cheetah_patch_cachetlbops
	 nop

	ba,pt	%xcc, tlb_fixup_done
	 nop

spitfire_tlb_fixup:
	/* Set TLB type to spitfire. */
	mov	0, %g2
	sethi	%hi(tlb_type), %g1
	stw	%g2, [%g1 + %lo(tlb_type)]

tlb_fixup_done:
	sethi	%hi(init_thread_union), %g6
	or	%g6, %lo(init_thread_union), %g6
	ldx	[%g6 + TI_TASK], %g4
	mov	%sp, %l6

	wr	%g0, ASI_P, %asi
	mov	1, %g1
	sllx	%g1, THREAD_SHIFT, %g1
	sub	%g1, (STACKFRAME_SZ + STACK_BIAS), %g1
	add	%g6, %g1, %sp
	mov	0, %fp

	/* Set per-cpu pointer initially to zero, this makes
	 * the boot-cpu use the in-kernel-image per-cpu areas
	 * before setup_per_cpu_area() is invoked.
	 */
	clr	%g5

	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl

	/* Clear the bss */
	sethi	%hi(__bss_start), %o0
	or	%o0, %lo(__bss_start), %o0
	sethi	%hi(_end), %o1
	or	%o1, %lo(_end), %o1
	call	__bzero
	 sub	%o1, %o0, %o1

#ifdef CONFIG_LOCKDEP
	/* We have this call this super early, as even prom_init can grab
	 * spinlocks and thus call into the lockdep code.
	 */
	call	lockdep_init
	 nop
#endif

	mov	%l6, %o1			! OpenPROM stack
	call	prom_init
	 mov	%l7, %o0			! OpenPROM cif handler

	/* Initialize current_thread_info()->cpu as early as possible.
	 * In order to do that accurately we have to patch up the get_cpuid()
	 * assembler sequences.  And that, in turn, requires that we know
	 * if we are on a Starfire box or not.  While we're here, patch up
	 * the sun4v sequences as well.
	 */
	call	check_if_starfire
	 nop
	call	per_cpu_patch
	 nop
	call	sun4v_patch
	 nop

#ifdef CONFIG_SMP
	call	hard_smp_processor_id
	 nop
	cmp	%o0, NR_CPUS
	blu,pt	%xcc, 1f
	 nop
	call	boot_cpu_id_too_large
	 nop
	/* Not reached... */

1:
#else
	mov	0, %o0
#endif
	sth	%o0, [%g6 + TI_CPU]

	call	prom_init_report
	 nop

	/* Off we go.... */
	call	start_kernel
	 nop
	/* Not reached... */

	.previous

	/* This is meant to allow the sharing of this code between
	 * boot processor invocation (via setup_tba() below) and
	 * secondary processor startup (via trampoline.S).  The
	 * former does use this code, the latter does not yet due
	 * to some complexities.  That should be fixed up at some
	 * point.
	 *
	 * There used to be enormous complexity wrt. transferring
	 * over from the firmware's trap table to the Linux kernel's.
	 * For example, there was a chicken & egg problem wrt. building
	 * the OBP page tables, yet needing to be on the Linux kernel
	 * trap table (to translate PAGE_OFFSET addresses) in order to
	 * do that.
	 *
	 * We now handle OBP tlb misses differently, via linear lookups
	 * into the prom_trans[] array.  So that specific problem no
	 * longer exists.  Yet, unfortunately there are still some issues
	 * preventing trampoline.S from using this code... ho hum.
	 */
	.globl	setup_trap_table
setup_trap_table:
	save	%sp, -192, %sp

	/* Force interrupts to be disabled. */
	rdpr	%pstate, %l0
	andn	%l0, PSTATE_IE, %o1
	wrpr	%o1, 0x0, %pstate
	rdpr	%pil, %l1
	wrpr	%g0, PIL_NORMAL_MAX, %pil

	/* Make the firmware call to jump over to the Linux trap table.  */
	sethi	%hi(is_sun4v), %o0
	lduw	[%o0 + %lo(is_sun4v)], %o0
	brz,pt	%o0, 1f
	 nop

	TRAP_LOAD_TRAP_BLOCK(%g2, %g3)
	add	%g2, TRAP_PER_CPU_FAULT_INFO, %g2
	stxa	%g2, [%g0] ASI_SCRATCHPAD

	/* Compute physical address:
	 *
	 * paddr = kern_base + (mmfsa_vaddr - KERNBASE)
	 */
	sethi	%hi(KERNBASE), %g3
	sub	%g2, %g3, %g2
	sethi	%hi(kern_base), %g3
	ldx	[%g3 + %lo(kern_base)], %g3
	add	%g2, %g3, %o1
	sethi	%hi(sparc64_ttable_tl0), %o0

	set	prom_set_trap_table_name, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x00]
	mov	2, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x08]
	mov	0, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x10]
	stx	%o0, [%sp + 2047 + 128 + 0x18]
	stx	%o1, [%sp + 2047 + 128 + 0x20]
	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x08], %o1
	call	%o1
	 add	%sp, (2047 + 128), %o0

	ba,pt	%xcc, 2f
	 nop

1:	sethi	%hi(sparc64_ttable_tl0), %o0
	set	prom_set_trap_table_name, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x00]
	mov	1, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x08]
	mov	0, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x10]
	stx	%o0, [%sp + 2047 + 128 + 0x18]
	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x08], %o1
	call	%o1
	 add	%sp, (2047 + 128), %o0

	/* Start using proper page size encodings in ctx register.  */
2:	sethi	%hi(sparc64_kern_pri_context), %g3
	ldx	[%g3 + %lo(sparc64_kern_pri_context)], %g2

	mov		PRIMARY_CONTEXT, %g1

661:	stxa		%g2, [%g1] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g1] ASI_MMU
	.previous

	membar	#Sync

	BRANCH_IF_SUN4V(o2, 1f)

	/* Kill PROM timer */
	sethi	%hi(0x80000000), %o2
	sllx	%o2, 32, %o2
	wr	%o2, 0, %tick_cmpr

	BRANCH_IF_ANY_CHEETAH(o2, o3, 1f)

	ba,pt	%xcc, 2f
	 nop

	/* Disable STICK_INT interrupts. */
1:
	sethi	%hi(0x80000000), %o2
	sllx	%o2, 32, %o2
	wr	%o2, %asr25

2:
	wrpr	%g0, %g0, %wstate

	call	init_irqwork_curcpu
	 nop

	/* Now we can restore interrupt state. */
	wrpr	%l0, 0, %pstate
	wrpr	%l1, 0x0, %pil

	ret
	 restore

	.globl	setup_tba
setup_tba:
	save	%sp, -192, %sp

	/* The boot processor is the only cpu which invokes this
	 * routine, the other cpus set things up via trampoline.S.
	 * So save the OBP trap table address here.
	 */
	rdpr	%tba, %g7
	sethi	%hi(prom_tba), %o1
	or	%o1, %lo(prom_tba), %o1
	stx	%g7, [%o1]

	call	setup_trap_table
	 nop

	ret
	 restore
sparc64_boot_end:

#include "etrap_64.S"
#include "rtrap_64.S"
#include "winfixup.S"
#include "fpu_traps.S"
#include "ivec.S"
#include "getsetcc.S"
#include "utrap.S"
#include "spiterrs.S"
#include "cherrs.S"
#include "misctrap.S"
#include "syscalls.S"
#include "helpers.S"
#include "hvcalls.S"
#include "sun4v_tlb_miss.S"
#include "sun4v_ivec.S"
#include "ktlb.S"
#include "tsb.S"

/*
 * The following skip makes sure the trap table in ttable.S is aligned
 * on a 32K boundary as required by the v9 specs for TBA register.
 *
 * We align to a 32K boundary, then we have the 32K kernel TSB,
 * the 64K kernel 4MB TSB, and then the 32K aligned trap table.
 */
1:
	.skip	0x4000 + _start - 1b

! 0x0000000000408000

	.globl	swapper_tsb
swapper_tsb:
	.skip	(32 * 1024)

	.globl	swapper_4m_tsb
swapper_4m_tsb:
	.skip	(64 * 1024)

! 0x0000000000420000

	/* Some care needs to be exercised if you try to move the
	 * location of the trap table relative to other things.  For
	 * one thing there are br* instructions in some of the
	 * trap table entires which branch back to code in ktlb.S
	 * Those instructions can only handle a signed 16-bit
	 * displacement.
	 *
	 * There is a binutils bug (bugzilla #4558) which causes
	 * the relocation overflow checks for such instructions to
	 * not be done correctly.  So bintuils will not notice the
	 * error and will instead write junk into the relocation and
	 * you'll have an unbootable kernel.
	 */
#include "ttable.S"

! 0x0000000000428000

#include "systbls_64.S"

	.data
	.align	8
	.globl	prom_tba, tlb_type
prom_tba:	.xword	0
tlb_type:	.word	0	/* Must NOT end up in BSS */
	.section	".fixup",#alloc,#execinstr

	.globl	__ret_efault, __retl_efault, __ret_one, __retl_one
ENTRY(__ret_efault)
	ret
	 restore %g0, -EFAULT, %o0
ENDPROC(__ret_efault)

ENTRY(__retl_efault)
	retl
	 mov	-EFAULT, %o0
ENDPROC(__retl_efault)

ENTRY(__retl_one)
	retl
	 mov	1, %o0
ENDPROC(__retl_one)

ENTRY(__ret_one_asi)
	wr	%g0, ASI_AIUS, %asi
	ret
	 restore %g0, 1, %o0
ENDPROC(__ret_one_asi)

ENTRY(__retl_one_asi)
	wr	%g0, ASI_AIUS, %asi
	retl
	 mov	1, %o0
ENDPROC(__retl_one_asi)

ENTRY(__retl_o1)
	retl
	 mov	%o1, %o0
ENDPROC(__retl_o1)

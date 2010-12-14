/*
 * CRISv32 kernel startup code.
 *
 * Copyright (C) 2003, Axis Communications AB
 */

#define ASSEMBLER_MACROS_ONLY

/*
 * The macros found in mmu_defs_asm.h uses the ## concatenation operator, so
 * -traditional must not be used when assembling this file.
 */
#include <arch/memmap.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/intr_vect.h>
#include <hwregs/asm/mmu_defs_asm.h>
#include <hwregs/asm/reg_map_asm.h>
#include <mach/startup.inc>

#define CRAMFS_MAGIC 0x28cd3d45
#define JHEAD_MAGIC 0x1FF528A6
#define JHEAD_SIZE 8
#define RAM_INIT_MAGIC 0x56902387
#define COMMAND_LINE_MAGIC 0x87109563
#define NAND_BOOT_MAGIC 0x9a9db001

	;; NOTE: R8 and R9 carry information from the decompressor (if the
	;; kernel was compressed). They must not be used in the code below
	;; until they are read!

	;; Exported symbols.
	.global etrax_irv
	.global romfs_start
	.global romfs_length
	.global romfs_in_flash
	.global nand_boot
	.global swapper_pg_dir

	;; Dummy section to make it bootable with current VCS simulator
#ifdef CONFIG_ETRAX_VCS_SIM
	.section ".boot", "ax"
	ba tstart
	nop
#endif

	.text
tstart:
	;; This is the entry point of the kernel. The CPU is currently in
	;; supervisor mode.
	;;
	;; 0x00000000 if flash.
	;; 0x40004000 if DRAM.
	;;
	di

	START_CLOCKS

	SETUP_WAIT_STATES

	GIO_INIT

#ifdef CONFIG_SMP
secondary_cpu_entry: /* Entry point for secondary CPUs */
	di
#endif

	;; Setup and enable the MMU. Use same configuration for both the data
	;; and the instruction MMU.
	;;
	;; Note; 3 cycles is needed for a bank-select to take effect. Further;
	;; bank 1 is the instruction MMU, bank 2 is the data MMU.

#ifdef CONFIG_CRIS_MACH_ARTPEC3
	move.d	REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 8)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 4)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_d, 5)     \
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb), $r0
#elif !defined(CONFIG_ETRAX_VCS_SIM)
	move.d	REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 8)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 4)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb), $r0
#else
	;; Map the virtual DRAM to the RW eprom area at address 0.
	;; Also map 0xa for the hook calls,
	move.d	REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 8)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 4)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb)   \
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_a, 0xa), $r0
#endif

	;; Temporary map of 0x40 -> 0x40 and 0x00 -> 0x00.
	move.d	REG_FIELD(mmu, rw_mm_kbase_lo, base_4, 4)  \
		| REG_FIELD(mmu, rw_mm_kbase_lo, base_0, 0), $r1

	;; Enable certain page protections and setup linear mapping
	;; for f,e,c,b,4,0.

	;; ARTPEC-3:
	;; c,d used for linear kernel mapping, up to 512 MB
	;; e used for vmalloc
	;; f unused, but page mapped to get page faults

	;; ETRAX FS:
	;; c used for linear kernel mapping, up to 256 MB
	;; d used for vmalloc
	;; e,f used for memory-mapped NOR flash

#ifdef CONFIG_CRIS_MACH_ARTPEC3
	move.d	REG_STATE(mmu, rw_mm_cfg, we, on)		\
		| REG_STATE(mmu, rw_mm_cfg, acc, on)		\
		| REG_STATE(mmu, rw_mm_cfg, ex, on)		\
		| REG_STATE(mmu, rw_mm_cfg, inv, on)            \
		| REG_STATE(mmu, rw_mm_cfg, seg_f, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_e, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_d, linear)      \
		| REG_STATE(mmu, rw_mm_cfg, seg_c, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_b, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_a, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_9, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_8, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_7, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_6, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_5, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_4, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_3, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_2, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_1, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_0, linear), $r2
#elif !defined(CONFIG_ETRAX_VCS_SIM)
	move.d	REG_STATE(mmu, rw_mm_cfg, we, on)		\
		| REG_STATE(mmu, rw_mm_cfg, acc, on)		\
		| REG_STATE(mmu, rw_mm_cfg, ex, on)		\
		| REG_STATE(mmu, rw_mm_cfg, inv, on)		\
		| REG_STATE(mmu, rw_mm_cfg, seg_f, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_e, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_d, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_c, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_b, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_a, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_9, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_8, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_7, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_6, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_5, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_4, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_3, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_2, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_1, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_0, linear), $r2
#else
	move.d	REG_STATE(mmu, rw_mm_cfg, we, on)		\
		| REG_STATE(mmu, rw_mm_cfg, acc, on)		\
		| REG_STATE(mmu, rw_mm_cfg, ex, on)		\
		| REG_STATE(mmu, rw_mm_cfg, inv, on)		\
		| REG_STATE(mmu, rw_mm_cfg, seg_f, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_e, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_d, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_c, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_b, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_a, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_9, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_8, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_7, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_6, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_5, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_4, linear)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_3, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_2, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_1, page)	\
		| REG_STATE(mmu, rw_mm_cfg, seg_0, linear), $r2
#endif

	;; Update instruction MMU.
	move	1, $srs
	nop
	nop
	nop
	move	$r0, $s2	; kbase_hi.
	move	$r1, $s1	; kbase_lo.
	move	$r2, $s0	; mm_cfg, virtual memory configuration.

	;; Update data MMU.
	move	2, $srs
	nop
	nop
	nop
	move	$r0, $s2	; kbase_hi.
	move	$r1, $s1	; kbase_lo
	move	$r2, $s0	; mm_cfg, virtual memory configuration.

	;; Enable data and instruction MMU.
	move	0, $srs
	moveq	0xf, $r0	;  IMMU, DMMU, DCache, Icache on
	nop
	nop
	nop
	move	$r0, $s0
	nop
	nop
	nop

#ifdef CONFIG_SMP
	;; Read CPU ID
	move    0, $srs
	nop
	nop
	nop
	move    $s12, $r0
	cmpq    0, $r0
	beq	master_cpu
	nop
slave_cpu:
	; Time to boot-up. Get stack location provided by master CPU.
	move.d  smp_init_current_idle_thread, $r1
	move.d  [$r1], $sp
	add.d	8192, $sp
	move.d	ebp_start, $r0	; Defined in linker-script.
	move	$r0, $ebp
	jsr	smp_callin
	nop
master_cpu:
	/* Set up entry point for secondary CPUs. The boot ROM has set up
	 * EBP at start of internal memory. The CPU will get there
	 * later when we issue an IPI to them... */
	move.d MEM_INTMEM_START + IPI_INTR_VECT * 4, $r0
	move.d secondary_cpu_entry, $r1
	move.d $r1, [$r0]
#endif
#ifndef CONFIG_ETRAX_VCS_SIM
	; Check if starting from DRAM (network->RAM boot or unpacked
	; compressed kernel), or directly from flash.
	lapcq	., $r0
	and.d	0x7fffffff, $r0 ; Mask off the non-cache bit.
	cmp.d	0x10000, $r0	; Arbitrary, something above this code.
	blo	_inflash0
	nop
#endif

	jump	_inram		; Jump to cached RAM.
	nop

	;; Jumpgate.
_inflash0:
	jump _inflash
	nop

	;; Put the following in a section so that storage for it can be
	;; reclaimed after init is finished.
	.section ".init.text", "ax"

_inflash:

	;; Initialize DRAM.
	cmp.d	RAM_INIT_MAGIC, $r8 ; Already initialized?
	beq	_dram_initialized
	nop

#if defined CONFIG_ETRAXFS
#include "../mach-fs/dram_init.S"
#elif defined CONFIG_CRIS_MACH_ARTPEC3
#include "../mach-a3/dram_init.S"
#else
#error Only ETRAXFS and ARTPEC-3 supported!
#endif


_dram_initialized:
	;; Copy the text and data section to DRAM. This depends on that the
	;; variables used below are correctly set up by the linker script.
	;; The calculated value stored in R4 is used below.
	;; Leave the cramfs file system (piggybacked after the kernel) in flash.
	moveq	0, $r0		; Source.
	move.d	text_start, $r1	; Destination.
	move.d	__vmlinux_end, $r2
	move.d	$r2, $r4
	sub.d	$r1, $r4
1:	move.w	[$r0+], $r3
	move.w	$r3, [$r1+]
	cmp.d	$r2, $r1
	blo	1b
	nop

	;; Check for cramfs.
	moveq	0, $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]
	move.d	[$r4], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne 1f
	nop

	;; Set length and start of cramfs, set romfs_in_flash flag
	addoq	+4, $r4, $acr
	move.d	[$acr], $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]
	add.d	0xf0000000, $r4	; Add cached flash start in virtual memory.
	move.d	romfs_start, $r1
	move.d	$r4, [$r1]
1:	moveq	1, $r0
	move.d	romfs_in_flash, $r1
	move.d	$r0, [$r1]

	jump	_start_it	; Jump to cached code.
	nop

_inram:
	;; Check if booting from NAND flash; if so, set appropriate flags
	;; and move on.
	cmp.d	NAND_BOOT_MAGIC, $r12
	bne	move_cramfs	; not nand, jump
	moveq	1, $r0
	move.d	nand_boot, $r1	; tell axisflashmap we're booting from NAND
	move.d	$r0, [$r1]
	moveq	0, $r0		; tell axisflashmap romfs is not in
	move.d	romfs_in_flash, $r1 ; (directly accessed) flash
	move.d	$r0, [$r1]
	jump	_start_it	; continue with boot
	nop

move_cramfs:
	;; kernel is in DRAM.
	;; Must figure out if there is a piggybacked rootfs image or not.
	;; Set romfs_length to 0 => no rootfs image available by default.
	moveq	0, $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]

#ifndef CONFIG_ETRAX_VCS_SIM
	;; The kernel could have been unpacked to DRAM by the loader, but
	;; the cramfs image could still be in the flash immediately
	;; following the compressed kernel image. The loader passes the address
	;; of the byte succeeding the last compressed byte in the flash in
	;; register R9 when starting the kernel.
	cmp.d	0x0ffffff8, $r9
	bhs	_no_romfs_in_flash ; R9 points outside the flash area.
	nop
#else
	ba _no_romfs_in_flash
	nop
#endif
	;; cramfs rootfs might to be in flash. Check for it.
	move.d	[$r9], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne	_no_romfs_in_flash
	nop

	;; found cramfs in flash. set address and size, and romfs_in_flash flag.
	addoq	+4, $r9, $acr
	move.d	[$acr], $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]
	add.d	0xf0000000, $r9	; Add cached flash start in virtual memory.
	move.d	romfs_start, $r1
	move.d	$r9, [$r1]
	moveq	1, $r0
	move.d	romfs_in_flash, $r1
	move.d	$r0, [$r1]

	jump	_start_it	; Jump to cached code.
	nop

_no_romfs_in_flash:
	;; No romfs in flash, so look for cramfs, or jffs2 with jhead,
	;; after kernel in RAM, as is the case with network->RAM boot.
	;; For cramfs, partition starts with magic and length.
	;; For jffs2, a jhead is prepended which contains with magic and length.
	;; The jhead is not part of the jffs2 partition however.
#ifndef CONFIG_ETRAXFS_SIM
	move.d	__bss_start, $r0
#else
	move.d	__end, $r0
#endif
	move.d	[$r0], $r1
	cmp.d	CRAMFS_MAGIC, $r1 ; cramfs magic?
	beq	2f		  ; yes, jump
	nop
	cmp.d	JHEAD_MAGIC, $r1 ; jffs2 (jhead) magic?
	bne	4f		; no, skip copy
	nop
	addq	4, $r0		; location of jffs2 size
	move.d	[$r0+], $r2	; fetch jffs2 size -> r2
				; r0 now points to start of jffs2
	ba	3f
	nop
2:
	addoq	+4, $r0, $acr	; location of cramfs size
	move.d	[$acr], $r2	; fetch cramfs size -> r2
				; r0 still points to start of cramfs
3:
	;; Now, move the root fs to after kernel's BSS

	move.d	_end, $r1	; start of cramfs -> r1
	move.d	romfs_start, $r3
	move.d	$r1, [$r3]	; store at romfs_start (for axisflashmap)
	move.d	romfs_length, $r3
	move.d	$r2, [$r3]	; store size at romfs_length

#ifndef CONFIG_ETRAX_VCS_SIM
	add.d	$r2, $r0	; copy from end and downwards
	add.d	$r2, $r1

	lsrq	1, $r2		; Size is in bytes, we copy words.
	addq    1, $r2
1:
	move.w	[$r0], $r3
	move.w	$r3, [$r1]
	subq	2, $r0
	subq	2, $r1
	subq	1, $r2
	bne	1b
	nop
#endif

4:
	;; BSS move done.
	;; Clear romfs_in_flash flag, as we now know romfs is in DRAM
	;; Also clear nand_boot flag; if we got here, we know we've not
	;; booted from NAND flash.
	moveq	0, $r0
	move.d	romfs_in_flash, $r1
	move.d	$r0, [$r1]
	moveq	0, $r0
	move.d	nand_boot, $r1
	move.d	$r0, [$r1]

	jump	_start_it	; Jump to cached code.
	nop

_start_it:

	;; Check if kernel command line is supplied
	cmp.d	COMMAND_LINE_MAGIC, $r10
	bne	no_command_line
	nop

	move.d	256, $r13
	move.d  cris_command_line, $r10
	or.d	0x80000000, $r11 ; Make it virtual
1:
	move.b  [$r11+], $r1
	move.b  $r1, [$r10+]
	subq	1, $r13
	bne	1b
	nop

no_command_line:

	;; The kernel stack contains a task structure for each task. This
	;; the initial kernel stack is in the same page as the init_task,
	;; but starts at the top of the page, i.e. + 8192 bytes.
	move.d	init_thread_union + 8192, $sp
	move.d	ebp_start, $r0	; Defined in linker-script.
	move	$r0, $ebp
	move.d	etrax_irv, $r1	; Set the exception base register and pointer.
	move.d	$r0, [$r1]

#ifndef CONFIG_ETRAX_VCS_SIM
	;; Clear the BSS region from _bss_start to _end.
	move.d	__bss_start, $r0
	move.d	_end, $r1
1:	clear.d	[$r0+]
	cmp.d	$r1, $r0
	blo 1b
	nop
#endif

#ifdef CONFIG_ETRAX_VCS_SIM
	/* Set the watchdog timeout to something big. Will be removed when */
	/* watchdog can be disabled with command line option */
	move.d  0x7fffffff, $r10
	jsr     CPU_WATCHDOG_TIMEOUT
	nop
#endif

	; Initialize registers to increase determinism
	move.d __bss_start, $r0
	movem [$r0], $r13

#ifdef CONFIG_ETRAX_L2CACHE
	jsr	l2cache_init
	nop
#endif

	jump	start_kernel	; Jump to start_kernel() in init/main.c.
	nop

	.data
etrax_irv:
	.dword 0

; Variables for communication with the Axis flash map driver (axisflashmap),
; and for setting up memory in arch/cris/kernel/setup.c .

; romfs_start is set to the start of the root file system, if it exists
; in directly accessible memory (i.e. NOR Flash when booting from Flash,
; or RAM when booting directly from a network-downloaded RAM image)
romfs_start:
	.dword 0

; romfs_length is set to the size of the root file system image, if it exists
; in directly accessible memory (see romfs_start). Otherwise it is set to 0.
romfs_length:
	.dword 0

; romfs_in_flash is set to 1 if the root file system resides in directly
; accessible flash memory (i.e. NOR flash). It is set to 0 for RAM boot
; or NAND flash boot.
romfs_in_flash:
	.dword 0

; nand_boot is set to 1 when the kernel has been booted from NAND flash
nand_boot:
	.dword 0

swapper_pg_dir = 0xc0002000

	.section ".init.data", "aw"

#if defined CONFIG_ETRAXFS
#include "../mach-fs/hw_settings.S"
#elif defined CONFIG_CRIS_MACH_ARTPEC3
#include "../mach-a3/hw_settings.S"
#else
#error Only ETRAXFS and ARTPEC-3 supported!
#endif

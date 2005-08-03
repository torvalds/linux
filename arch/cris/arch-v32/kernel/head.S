/*
 * CRISv32 kernel startup code.
 *
 * Copyright (C) 2003, Axis Communications AB
 */

#include <linux/config.h>

#define ASSEMBLER_MACROS_ONLY

/*
 * The macros found in mmu_defs_asm.h uses the ## concatenation operator, so
 * -traditional must not be used when assembling this file.
 */
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/asm/mmu_defs_asm.h>
#include <asm/arch/hwregs/asm/reg_map_asm.h>
#include <asm/arch/hwregs/asm/config_defs_asm.h>
#include <asm/arch/hwregs/asm/bif_core_defs_asm.h>

#define CRAMFS_MAGIC 0x28cd3d45
#define RAM_INIT_MAGIC 0x56902387
#define COMMAND_LINE_MAGIC 0x87109563

	;; NOTE: R8 and R9 carry information from the decompressor (if the
	;; kernel was compressed). They must not be used in the code below
	;; until they are read!

	;; Exported symbols.
	.global etrax_irv
	.global romfs_start
	.global romfs_length
	.global romfs_in_flash
	.global swapper_pg_dir
	.global crisv32_nand_boot
	.global crisv32_nand_cramfs_offset

	;; Dummy section to make it bootable with current VCS simulator
#ifdef CONFIG_ETRAXFS_SIM
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

	;; Start clocks for used blocks.
	move.d REG_ADDR(config, regi_config, rw_clk_ctrl), $r1
	move.d [$r1], $r0
	or.d   REG_STATE(config, rw_clk_ctrl, cpu, yes) | \
	       REG_STATE(config, rw_clk_ctrl, bif, yes) | \
	       REG_STATE(config, rw_clk_ctrl, fix_io, yes), $r0
	move.d $r0, [$r1]

	;; Set up waitstates etc
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_grp1_cfg), $r0
	move.d   CONFIG_ETRAX_MEM_GRP1_CONFIG, $r1
	move.d   $r1, [$r0]
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_grp2_cfg), $r0
	move.d   CONFIG_ETRAX_MEM_GRP2_CONFIG, $r1
	move.d   $r1, [$r0]
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_grp3_cfg), $r0
	move.d   CONFIG_ETRAX_MEM_GRP3_CONFIG, $r1
	move.d   $r1, [$r0]
	move.d   REG_ADDR(bif_core, regi_bif_core, rw_grp4_cfg), $r0
	move.d   CONFIG_ETRAX_MEM_GRP4_CONFIG, $r1
	move.d   $r1, [$r0]

#ifdef CONFIG_ETRAXFS_SIM
	;; Set up minimal flash waitstates
	move.d 0, $r10
	move.d REG_ADDR(bif_core, regi_bif_core, rw_grp1_cfg), $r11
	move.d $r10, [$r11]
#endif

	;; Setup and enable the MMU. Use same configuration for both the data
	;; and the instruction MMU.
	;;
	;; Note; 3 cycles is needed for a bank-select to take effect. Further;
	;; bank 1 is the instruction MMU, bank 2 is the data MMU.
#ifndef CONFIG_ETRAXFS_SIM
	move.d	REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 8)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 4)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb), $r0
#else
	;; Map the virtual DRAM to the RW eprom area at address 0.
	;; Also map 0xa for the hook calls,
	move.d	REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 8)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 0)	\
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb)   \
		| REG_FIELD(mmu, rw_mm_kbase_hi, base_a, 0xa), $r0
#endif

	;; Temporary map of 0x40 -> 0x40 and 0x00 -> 0x00.
	move.d	REG_FIELD(mmu, rw_mm_kbase_lo, base_4, 4)  \
		| REG_FIELD(mmu, rw_mm_kbase_lo, base_0, 0), $r1

	;; Enable certain page protections and setup linear mapping
	;; for f,e,c,b,4,0.
#ifndef CONFIG_ETRAXFS_SIM
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
	move    $s10, $r0
	cmpq    0, $r0
	beq	master_cpu
	nop
slave_cpu:
	; A slave waits for cpu_now_booting to be equal to CPU ID.
	move.d	cpu_now_booting, $r1
slave_wait:
	cmp.d	[$r1], $r0
	bne	slave_wait
	nop
	; Time to boot-up. Get stack location provided by master CPU.
	move.d  smp_init_current_idle_thread, $r1
	move.d  [$r1], $sp
	add.d	8192, $sp
	move.d	ebp_start, $r0	; Defined in linker-script.
	move	$r0, $ebp
	jsr	smp_callin
	nop
master_cpu:
#endif
#ifndef CONFIG_ETRAXFS_SIM
	;; Check if starting from DRAM or flash.
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

#include "../lib/dram_init.S"

_dram_initialized:
	;; Copy the text and data section to DRAM. This depends on that the
	;; variables used below are correctly set up by the linker script.
	;; The calculated value stored in R4 is used below.
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

	;; Keep CRAMFS in flash.
	moveq	0, $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]
	move.d	[$r4], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne 1f
	nop

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
	;; Check if booting from NAND flash (in that case we just remember the offset
	;; into the flash where cramfs should be).
	move.d	REG_ADDR(config, regi_config, r_bootsel), $r0
	move.d	[$r0], $r0
	and.d	REG_MASK(config, r_bootsel, boot_mode), $r0
	cmp.d	REG_STATE(config, r_bootsel, boot_mode, nand), $r0
	bne	move_cramfs
	moveq	1,$r0
	move.d	crisv32_nand_boot, $r1
	move.d	$r0, [$r1]
	move.d	crisv32_nand_cramfs_offset, $r1
	move.d	$r9, [$r1]
	moveq	1, $r0
	move.d	romfs_in_flash, $r1
	move.d	$r0, [$r1]
	jump	_start_it
	nop

move_cramfs:
	;; Move the cramfs after BSS.
	moveq	0, $r0
	move.d	romfs_length, $r1
	move.d	$r0, [$r1]

#ifndef CONFIG_ETRAXFS_SIM
	;; The kernel could have been unpacked to DRAM by the loader, but
	;; the cramfs image could still be inte the flash immediately
	;; following the compressed kernel image. The loaded passes the address
	;; of the bute succeeding the last compressed byte in the flash in
	;; register R9 when starting the kernel.
	cmp.d	0x0ffffff8, $r9
	bhs	_no_romfs_in_flash ; R9 points outside the flash area.
	nop
#else
	ba _no_romfs_in_flash
	nop
#endif
	move.d	[$r9], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne	_no_romfs_in_flash
	nop

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
	;; Look for cramfs.
#ifndef CONFIG_ETRAXFS_SIM
	move.d	__vmlinux_end, $r0
#else
	move.d	__end, $r0
#endif
	move.d	[$r0], $r1
	cmp.d	CRAMFS_MAGIC, $r1
	bne	2f
	nop

	addoq	+4, $r0, $acr
	move.d	[$acr], $r2
	move.d	_end, $r1
	move.d	romfs_start, $r3
	move.d	$r1, [$r3]
	move.d	romfs_length, $r3
	move.d	$r2, [$r3]

#ifndef CONFIG_ETRAXFS_SIM
	add.d	$r2, $r0
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

2:
	moveq	0, $r0
	move.d	romfs_in_flash, $r1
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
	move.b  [$r11+], $r12
	move.b  $r12, [$r10+]
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

#ifndef CONFIG_ETRAXFS_SIM
	;; Clear the BSS region from _bss_start to _end.
	move.d	__bss_start, $r0
	move.d	_end, $r1
1:	clear.d	[$r0+]
	cmp.d	$r1, $r0
	blo 1b
	nop
#endif

#ifdef CONFIG_ETRAXFS_SIM
	/* Set the watchdog timeout to something big. Will be removed when */
	/* watchdog can be disabled with command line option */
	move.d  0x7fffffff, $r10
	jsr     CPU_WATCHDOG_TIMEOUT
	nop
#endif

	; Initialize registers to increase determinism
	move.d __bss_start, $r0
	movem [$r0], $r13

	jump	start_kernel	; Jump to start_kernel() in init/main.c.
	nop

	.data
etrax_irv:
	.dword 0
romfs_start:
	.dword 0
romfs_length:
	.dword 0
romfs_in_flash:
	.dword 0
crisv32_nand_boot:
	.dword 0
crisv32_nand_cramfs_offset:
	.dword 0

swapper_pg_dir = 0xc0002000

	.section ".init.data", "aw"

#include "../lib/hw_settings.S"

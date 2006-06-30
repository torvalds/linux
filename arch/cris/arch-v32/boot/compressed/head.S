/*
 *  Code that sets up the DRAM registers, calls the
 *  decompressor to unpack the piggybacked kernel, and jumps.
 *
 *  Copyright (C) 1999 - 2003, Axis Communications AB
 */

#define ASSEMBLER_MACROS_ONLY
#include <asm/arch/hwregs/asm/reg_map_asm.h>
#include <asm/arch/hwregs/asm/gio_defs_asm.h>
#include <asm/arch/hwregs/asm/config_defs_asm.h>

#define RAM_INIT_MAGIC 0x56902387
#define COMMAND_LINE_MAGIC 0x87109563

	;; Exported symbols

	.globl	input_data

	.text
start:
	di

	;; Start clocks for used blocks.
	move.d REG_ADDR(config, regi_config, rw_clk_ctrl), $r1
	move.d [$r1], $r0
	or.d   REG_STATE(config, rw_clk_ctrl, cpu, yes) | \
	       REG_STATE(config, rw_clk_ctrl, bif, yes) | \
	       REG_STATE(config, rw_clk_ctrl, fix_io, yes), $r0
	move.d $r0, [$r1]

	;; If booting from NAND flash we first have to copy some
	;; data from NAND flash to internal RAM to get the code
	;; that initializes the SDRAM. Lets copy 20 KB. This
	;; code executes at 0x38010000 if booting from NAND and
	;; we are guaranted that at least 0x200 bytes are good so
	;; lets start from there. The first 8192 bytes in the nand
	;; flash is spliced with zeroes and is thus 16384 bytes.
	move.d 0x38010200, $r10
	move.d 0x14200, $r11	; Start offset in NAND flash 0x10200 + 16384
	move.d 0x5000, $r12	; Length of copy

	;; Before this code the tools add a partitiontable so the PC
	;; has an offset from the linked address.
offset1:
	lapcq  ., $r13		; get PC
	add.d	first_copy_complete-offset1, $r13

#include "../../lib/nand_init.S"

first_copy_complete:
	;; Initialze the DRAM registers.
	cmp.d	RAM_INIT_MAGIC, $r8	; Already initialized?
	beq	dram_init_finished
	nop

#include "../../lib/dram_init.S"

dram_init_finished:
	lapcq  ., $r13		; get PC
	add.d	second_copy_complete-dram_init_finished, $r13

	move.d REG_ADDR(config, regi_config, r_bootsel), $r0
	move.d [$r0], $r0
	and.d  REG_MASK(config, r_bootsel, boot_mode), $r0
	cmp.d  REG_STATE(config, r_bootsel, boot_mode, nand), $r0
	bne second_copy_complete ; No NAND boot
	nop

	;; Copy 2MB from NAND flash to SDRAM (at 2-4MB into the SDRAM)
	move.d 0x40204000, $r10
	move.d 0x8000, $r11
	move.d 0x200000, $r12
	ba copy_nand_to_ram
	nop
second_copy_complete:

	;; Initiate the PA port.
	move.d	CONFIG_ETRAX_DEF_GIO_PA_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pa_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PA_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pa_oe), $r1
	move.d	$r0, [$r1]

	;; Setup the stack to a suitably high address.
	;; We assume 8 MB is the minimum DRAM and put
	;; the SP at the top for now.

	move.d	0x40800000, $sp

	;; Figure out where the compressed piggyback image is
	;; in the flash (since we wont try to copy it to DRAM
	;; before unpacking). It is at _edata, but in flash.
	;; Use (_edata - herami) as offset to the current PC.

	move.d REG_ADDR(config, regi_config, r_bootsel), $r0
	move.d [$r0], $r0
	and.d  REG_MASK(config, r_bootsel, boot_mode), $r0
	cmp.d  REG_STATE(config, r_bootsel, boot_mode, nand), $r0
	beq hereami2
	nop
hereami:
	lapcq	., $r5		; get PC
	and.d	0x7fffffff, $r5	; strip any non-cache bit
	move.d	$r5, $r0	; save for later - flash address of 'herami'
	add.d	_edata, $r5
	sub.d	hereami, $r5	; r5 = flash address of '_edata'
	move.d	hereami, $r1	; destination
	ba 2f
	nop
hereami2:
	lapcq	., $r5		; get PC
	and.d	0x00ffffff, $r5	; strip any non-cache bit
	move.d  $r5, $r6
	or.d    0x40200000, $r6
	move.d	$r6, $r0	; save for later - flash address of 'herami'
	add.d	_edata, $r5
	sub.d	hereami2, $r5	; r5 = flash address of '_edata'
	add.d   0x40200000, $r5
	move.d	hereami2, $r1	; destination
2:
	;; Copy text+data to DRAM

	move.d	_edata, $r2	; end destination
1:	move.w	[$r0+], $r3
	move.w	$r3, [$r1+]
	cmp.d	$r2, $r1
	bcs	1b
	nop

	move.d	input_data, $r0 ; for the decompressor
	move.d	$r5, [$r0]	; for the decompressor

	;; Clear the decompressors BSS (between _edata and _end)

	moveq	0, $r0
	move.d	_edata, $r1
	move.d	_end, $r2
1:	move.w	$r0, [$r1+]
	cmp.d	$r2, $r1
	bcs	1b
	nop

	;;  Save command line magic and address.
	move.d	_cmd_line_magic, $r12
	move.d  $r10, [$r12]
	move.d	_cmd_line_addr, $r12
	move.d  $r11, [$r12]

	;; Do the decompression and save compressed size in _inptr

	jsr	decompress_kernel
	nop

	;; Restore command line magic and address.
	move.d  _cmd_line_magic, $r10
	move.d  [$r10], $r10
	move.d  _cmd_line_addr, $r11
	move.d  [$r11], $r11

	;; Put start address of root partition in r9 so the kernel can use it
	;; when mounting from flash
	move.d  input_data, $r0
	move.d	[$r0], $r9		; flash address of compressed kernel
	move.d  inptr, $r0
	add.d	[$r0], $r9		; size of compressed kernel
	cmp.d   0x40200000, $r9
	blo	enter_kernel
	nop
	sub.d   0x40200000, $r9
	add.d   0x4000, $r9

enter_kernel:
	;; Enter the decompressed kernel
	move.d	RAM_INIT_MAGIC, $r8	; Tell kernel that DRAM is initialized
	jump	0x40004000	; kernel is linked to this address
	nop

	.data

input_data:
	.dword	0		; used by the decompressor
_cmd_line_magic:
	.dword 0
_cmd_line_addr:
	.dword 0
is_nand_boot:
	.dword  0

#include "../../lib/hw_settings.S"

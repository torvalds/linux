/* head.S: kernel entry point for FR-V kernel
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/spr-regs.h>
#include <asm/mb86943a.h>
#include <asm/cache.h>
#include "head.inc"

###############################################################################
#
# void _boot(unsigned long magic, char *command_line) __attribute__((noreturn))
#
# - if magic is 0xdead1eaf, then command_line is assumed to point to the kernel
#   command line string
#
###############################################################################
	.section	.text.head,"ax"
	.balign		4

	.globl		_boot, __head_reference
        .type		_boot,@function
_boot:
__head_reference:
	sethi.p		%hi(LED_ADDR),gr30
	setlo		%lo(LED_ADDR),gr30

	LEDS		0x0000

	# calculate reference address for PC-relative stuff
	call		0f
0:	movsg		lr,gr26
	addi		gr26,#__head_reference-0b,gr26

	# invalidate and disable both of the caches and turn off the memory access checking
	dcef		@(gr0,gr0),1
	bar

	sethi.p		%hi(~(HSR0_ICE|HSR0_DCE|HSR0_CBM|HSR0_EIMMU|HSR0_EDMMU)),gr4
	setlo		%lo(~(HSR0_ICE|HSR0_DCE|HSR0_CBM|HSR0_EIMMU|HSR0_EDMMU)),gr4
	movsg		hsr0,gr5
	and		gr4,gr5,gr5
	movgs		gr5,hsr0
	movsg		hsr0,gr5

	LEDS		0x0001

	icei		@(gr0,gr0),1
	dcei		@(gr0,gr0),1
	bar

	# turn the instruction cache back on
	sethi.p		%hi(HSR0_ICE),gr4
	setlo		%lo(HSR0_ICE),gr4
	movsg		hsr0,gr5
	or		gr4,gr5,gr5
	movgs		gr5,hsr0
	movsg		hsr0,gr5

	bar

	LEDS		0x0002

	# retrieve the parameters (including command line) before we overwrite them
	sethi.p		%hi(0xdead1eaf),gr7
	setlo		%lo(0xdead1eaf),gr7
	subcc		gr7,gr8,gr0,icc0
	bne		icc0,#0,__head_no_parameters

	sethi.p		%hi(redboot_command_line-1),gr6
	setlo		%lo(redboot_command_line-1),gr6
	sethi.p		%hi(__head_reference),gr4
	setlo		%lo(__head_reference),gr4
	sub		gr6,gr4,gr6
	add.p		gr6,gr26,gr6
	subi		gr9,#1,gr9
	setlos.p	#511,gr4
	setlos		#1,gr5

__head_copy_cmdline:
	ldubu.p		@(gr9,gr5),gr16
	subicc		gr4,#1,gr4,icc0
	stbu.p		gr16,@(gr6,gr5)
	subicc		gr16,#0,gr0,icc1
	bls		icc0,#0,__head_end_cmdline
	bne		icc1,#1,__head_copy_cmdline
__head_end_cmdline:
	stbu		gr0,@(gr6,gr5)
__head_no_parameters:

###############################################################################
#
# we need to relocate the SDRAM to 0x00000000 (linux) or 0xC0000000 (uClinux)
# - note that we're going to have to run entirely out of the icache whilst
#   fiddling with the SDRAM controller registers
#
###############################################################################
#ifdef CONFIG_MMU
	call		__head_fr451_describe_sdram

#else
	movsg		psr,gr5
	srli		gr5,#28,gr5
	subicc		gr5,#3,gr0,icc0
	beq		icc0,#0,__head_fr551_sdram

	call		__head_fr401_describe_sdram
	bra		__head_do_sdram

__head_fr551_sdram:
	call		__head_fr555_describe_sdram
	LEDS		0x000d

__head_do_sdram:
#endif

	# preload the registers with invalid values in case any DBR/DARS are marked not present
	sethi.p		%hi(0xfe000000),gr17		; unused SDRAM DBR value
	setlo		%lo(0xfe000000),gr17
	or.p		gr17,gr0,gr20
	or		gr17,gr0,gr21
	or.p		gr17,gr0,gr22
	or		gr17,gr0,gr23

	# consult the SDRAM controller CS address registers
	cld		@(gr14,gr0 ),gr20,	cc0,#1	; DBR0 / DARS0
	cld		@(gr14,gr11),gr21,	cc1,#1	; DBR1 / DARS1
	cld		@(gr14,gr12),gr22,	cc2,#1	; DBR2 / DARS2
	cld.p		@(gr14,gr13),gr23,	cc3,#1	; DBR3 / DARS3

	sll		gr20,gr15,gr20			; shift values up for FR551
	sll		gr21,gr15,gr21
	sll		gr22,gr15,gr22
	sll		gr23,gr15,gr23

	LEDS		0x0003

	# assume the lowest valid CS line to be the SDRAM base and get its address
	subcc		gr20,gr17,gr0,icc0
	subcc.p		gr21,gr17,gr0,icc1
	subcc		gr22,gr17,gr0,icc2
	subcc.p		gr23,gr17,gr0,icc3
	ckne		icc0,cc4			; T if DBR0 != 0xfe000000
	ckne		icc1,cc5
	ckne		icc2,cc6
	ckne		icc3,cc7
	cor		gr23,gr0,gr24,		cc7,#1	; GR24 = SDRAM base
	cor		gr22,gr0,gr24,		cc6,#1
	cor		gr21,gr0,gr24,		cc5,#1
	cor		gr20,gr0,gr24,		cc4,#1

	# calculate the displacement required to get the SDRAM into the right place in memory
	sethi.p		%hi(__sdram_base),gr16
	setlo		%lo(__sdram_base),gr16
	sub		gr16,gr24,gr16			; delta = __sdram_base - DBRx

	# calculate the new values to go in the controller regs
	cadd.p		gr20,gr16,gr20,		cc4,#1	; DCS#0 (new) = DCS#0 (old) + delta
	cadd		gr21,gr16,gr21,		cc5,#1
	cadd.p		gr22,gr16,gr22,		cc6,#1
	cadd		gr23,gr16,gr23,		cc7,#1

	srl		gr20,gr15,gr20			; shift values down for FR551
	srl		gr21,gr15,gr21
	srl		gr22,gr15,gr22
	srl		gr23,gr15,gr23

	# work out the address at which the reg updater resides and lock it into icache
	# also work out the address the updater will jump to when finished
	sethi.p		%hi(__head_move_sdram-__head_reference),gr18
	setlo		%lo(__head_move_sdram-__head_reference),gr18
	sethi.p		%hi(__head_sdram_moved-__head_reference),gr19
	setlo		%lo(__head_sdram_moved-__head_reference),gr19
	add.p		gr18,gr26,gr18
	add		gr19,gr26,gr19
	add.p		gr19,gr16,gr19			; moved = addr + (__sdram_base - DBRx)
	add		gr18,gr5,gr4			; two cachelines probably required

	icpl		gr18,gr0,#1			; load and lock the cachelines
	icpl		gr4,gr0,#1
	LEDS		0x0004
	membar
	bar
	jmpl		@(gr18,gr0)

	.balign		L1_CACHE_BYTES
__head_move_sdram:
	cst		gr20,@(gr14,gr0 ),	cc4,#1
	cst		gr21,@(gr14,gr11),	cc5,#1
	cst		gr22,@(gr14,gr12),	cc6,#1
	cst		gr23,@(gr14,gr13),	cc7,#1
	cld		@(gr14,gr0 ),gr20,	cc4,#1
	cld		@(gr14,gr11),gr21,	cc5,#1
	cld		@(gr14,gr12),gr22,	cc4,#1
	cld		@(gr14,gr13),gr23,	cc7,#1
	bar
	membar
	jmpl		@(gr19,gr0)

	.balign		L1_CACHE_BYTES
__head_sdram_moved:
	icul		gr18
	add		gr18,gr5,gr4
	icul		gr4
	icei		@(gr0,gr0),1
	dcei		@(gr0,gr0),1

	LEDS		0x0005

	# recalculate reference address
	call		0f
0:	movsg		lr,gr26
	addi		gr26,#__head_reference-0b,gr26


###############################################################################
#
# move the kernel image down to the bottom of the SDRAM
#
###############################################################################
	sethi.p		%hi(__kernel_image_size_no_bss+15),gr4
	setlo		%lo(__kernel_image_size_no_bss+15),gr4
	srli.p		gr4,#4,gr4			; count
	or		gr26,gr26,gr16			; source

	sethi.p		%hi(__sdram_base),gr17		; destination
	setlo		%lo(__sdram_base),gr17

	setlos		#8,gr5
	sub.p		gr16,gr5,gr16			; adjust src for LDDU
	sub		gr17,gr5,gr17			; adjust dst for LDDU

	sethi.p		%hi(__head_move_kernel-__head_reference),gr18
	setlo		%lo(__head_move_kernel-__head_reference),gr18
	sethi.p		%hi(__head_kernel_moved-__head_reference+__sdram_base),gr19
	setlo		%lo(__head_kernel_moved-__head_reference+__sdram_base),gr19
	add		gr18,gr26,gr18
	icpl		gr18,gr0,#1
	jmpl		@(gr18,gr0)

	.balign		32
__head_move_kernel:
	lddu		@(gr16,gr5),gr10
	lddu		@(gr16,gr5),gr12
	stdu.p		gr10,@(gr17,gr5)
	subicc		gr4,#1,gr4,icc0
	stdu.p		gr12,@(gr17,gr5)
	bhi		icc0,#0,__head_move_kernel
	jmpl		@(gr19,gr0)

	.balign		32
__head_kernel_moved:
	icul		gr18
	icei		@(gr0,gr0),1
	dcei		@(gr0,gr0),1

	LEDS		0x0006

	# recalculate reference address
	call		0f
0:	movsg		lr,gr26
	addi		gr26,#__head_reference-0b,gr26


###############################################################################
#
# rearrange the iomem map and set the protection registers
#
###############################################################################

#ifdef CONFIG_MMU
	LEDS		0x3301
	call		__head_fr451_set_busctl
	LEDS		0x3303
	call		__head_fr451_survey_sdram
	LEDS		0x3305
	call		__head_fr451_set_protection

#else
	movsg		psr,gr5
	srli		gr5,#PSR_IMPLE_SHIFT,gr5
	subicc		gr5,#PSR_IMPLE_FR551,gr0,icc0
	beq		icc0,#0,__head_fr555_memmap
	subicc		gr5,#PSR_IMPLE_FR451,gr0,icc0
	beq		icc0,#0,__head_fr451_memmap

	LEDS		0x3101
	call		__head_fr401_set_busctl
	LEDS		0x3103
	call		__head_fr401_survey_sdram
	LEDS		0x3105
	call		__head_fr401_set_protection
	bra		__head_done_memmap

__head_fr451_memmap:
	LEDS		0x3301
	call		__head_fr401_set_busctl
	LEDS		0x3303
	call		__head_fr401_survey_sdram
	LEDS		0x3305
	call		__head_fr451_set_protection
	bra		__head_done_memmap

__head_fr555_memmap:
	LEDS		0x3501
	call		__head_fr555_set_busctl
	LEDS		0x3503
	call		__head_fr555_survey_sdram
	LEDS		0x3505
	call		__head_fr555_set_protection

__head_done_memmap:
#endif
	LEDS		0x0007

###############################################################################
#
# turn the data cache and MMU on
# - for the FR451 this'll mean that the window through which the kernel is
#   viewed will change
#
###############################################################################

#ifdef CONFIG_MMU
#define MMUMODE		HSR0_EIMMU|HSR0_EDMMU|HSR0_EXMMU|HSR0_EDAT|HSR0_XEDAT
#else
#define MMUMODE		HSR0_EIMMU|HSR0_EDMMU
#endif

	movsg		hsr0,gr5

	sethi.p		%hi(MMUMODE),gr4
	setlo		%lo(MMUMODE),gr4
	or		gr4,gr5,gr5

#if defined(CONFIG_FRV_DEFL_CACHE_WTHRU)
	sethi.p		%hi(HSR0_DCE|HSR0_CBM_WRITE_THRU),gr4
	setlo		%lo(HSR0_DCE|HSR0_CBM_WRITE_THRU),gr4
#elif defined(CONFIG_FRV_DEFL_CACHE_WBACK)
	sethi.p		%hi(HSR0_DCE|HSR0_CBM_COPY_BACK),gr4
	setlo		%lo(HSR0_DCE|HSR0_CBM_COPY_BACK),gr4
#elif defined(CONFIG_FRV_DEFL_CACHE_WBEHIND)
	sethi.p		%hi(HSR0_DCE|HSR0_CBM_COPY_BACK),gr4
	setlo		%lo(HSR0_DCE|HSR0_CBM_COPY_BACK),gr4

	movsg		psr,gr6
	srli		gr6,#24,gr6
	cmpi		gr6,#0x50,icc0		// FR451
	beq		icc0,#0,0f
	cmpi		gr6,#0x40,icc0		// FR405
	bne		icc0,#0,1f
0:
	# turn off write-allocate
	sethi.p		%hi(HSR0_NWA),gr6
	setlo		%lo(HSR0_NWA),gr6
	or		gr4,gr6,gr4
1:

#else
#error No default cache configuration set
#endif

	or		gr4,gr5,gr5
	movgs		gr5,hsr0
	bar

	LEDS		0x0008

	sethi.p		%hi(__head_mmu_enabled),gr19
	setlo		%lo(__head_mmu_enabled),gr19
	jmpl		@(gr19,gr0)

__head_mmu_enabled:
	icei		@(gr0,gr0),#1
	dcei		@(gr0,gr0),#1

	LEDS		0x0009

#ifdef CONFIG_MMU
	call		__head_fr451_finalise_protection
#endif

	LEDS		0x000a

###############################################################################
#
# set up the runtime environment
#
###############################################################################

	# clear the BSS area
	sethi.p		%hi(__bss_start),gr4
	setlo		%lo(__bss_start),gr4
	sethi.p		%hi(_end),gr5
	setlo		%lo(_end),gr5
	or.p		gr0,gr0,gr18
	or		gr0,gr0,gr19

0:
	stdi		gr18,@(gr4,#0)
	stdi		gr18,@(gr4,#8)
	stdi		gr18,@(gr4,#16)
	stdi.p		gr18,@(gr4,#24)
	addi		gr4,#24,gr4
	subcc		gr5,gr4,gr0,icc0
	bhi		icc0,#2,0b

	LEDS		0x000b

	# save the SDRAM details
	sethi.p		%hi(__sdram_old_base),gr4
	setlo		%lo(__sdram_old_base),gr4
	st		gr24,@(gr4,gr0)

	sethi.p		%hi(__sdram_base),gr5
	setlo		%lo(__sdram_base),gr5
	sethi.p		%hi(memory_start),gr4
	setlo		%lo(memory_start),gr4
	st		gr5,@(gr4,gr0)

	add		gr25,gr5,gr25
	sethi.p		%hi(memory_end),gr4
	setlo		%lo(memory_end),gr4
	st		gr25,@(gr4,gr0)

	# point the TBR at the kernel trap table
	sethi.p		%hi(__entry_kerneltrap_table),gr4
	setlo		%lo(__entry_kerneltrap_table),gr4
	movgs		gr4,tbr

	# set up the exception frame for init
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	sethi.p		%hi(_gp),gr16
	setlo		%lo(_gp),gr16
	sethi.p		%hi(__entry_usertrap_table),gr4
	setlo		%lo(__entry_usertrap_table),gr4

	lddi		@(gr28,#0),gr28		; load __frame & current
	ldi.p		@(gr29,#4),gr15		; set current_thread

	or		gr0,gr0,fp
	or		gr28,gr0,sp

	sti.p		gr4,@(gr28,REG_TBR)
	setlos		#ISR_EDE|ISR_DTT_DIVBYZERO|ISR_EMAM_EXCEPTION,gr5
	movgs		gr5,isr

	# turn on and off various CPU services
	movsg		psr,gr22
	sethi.p		%hi(#PSR_EM|PSR_EF|PSR_CM|PSR_NEM),gr4
	setlo		%lo(#PSR_EM|PSR_EF|PSR_CM|PSR_NEM),gr4
	or		gr22,gr4,gr22
	movgs		gr22,psr

	andi		gr22,#~(PSR_PIL|PSR_PS|PSR_S),gr22
	ori		gr22,#PSR_ET,gr22
	sti		gr22,@(gr28,REG_PSR)


###############################################################################
#
# set up the registers and jump into the kernel
#
###############################################################################

	LEDS		0x000c

	# initialise the processor and the peripherals
	#call		SYMBOL_NAME(processor_init)
	#call		SYMBOL_NAME(unit_init)
	#LEDS		0x0aff

	sethi.p		#0xe5e5,gr3
	setlo		#0xe5e5,gr3
	or.p		gr3,gr0,gr4
	or		gr3,gr0,gr5
	or.p		gr3,gr0,gr6
	or		gr3,gr0,gr7
	or.p		gr3,gr0,gr8
	or		gr3,gr0,gr9
	or.p		gr3,gr0,gr10
	or		gr3,gr0,gr11
	or.p		gr3,gr0,gr12
	or		gr3,gr0,gr13
	or.p		gr3,gr0,gr14
	or		gr3,gr0,gr17
	or.p		gr3,gr0,gr18
	or		gr3,gr0,gr19
	or.p		gr3,gr0,gr20
	or		gr3,gr0,gr21
	or.p		gr3,gr0,gr23
	or		gr3,gr0,gr24
	or.p		gr3,gr0,gr25
	or		gr3,gr0,gr26
	or.p		gr3,gr0,gr27
#	or		gr3,gr0,gr30
	or		gr3,gr0,gr31
	movgs		gr0,lr
	movgs		gr0,lcr
	movgs		gr0,ccr
	movgs		gr0,cccr

#ifdef CONFIG_MMU
	movgs		gr3,scr2
	movgs		gr3,scr3
#endif

	LEDS		0x0fff

	# invoke the debugging stub if present
	# - arch/frv/kernel/debug-stub.c will shift control directly to init/main.c
	#   (it will not return here)
	break
	.globl		__debug_stub_init_break
__debug_stub_init_break:

	# however, if you need to use an ICE, and don't care about using any userspace
	# debugging tools (such as the ptrace syscall), you can just step over the break
	# above and get to the kernel this way
	# look at arch/frv/kernel/debug-stub.c: debug_stub_init() to see what you've missed
	call		start_kernel

	.globl		__head_end
__head_end:
	.size		_boot, .-_boot

	# provide a point for GDB to place a break
	.section	.text.start,"ax"
	.globl		_start
	.balign		4
_start:
	call		_boot

	.previous
###############################################################################
#
# split a tile off of the region defined by GR8-GR9
#
#	ENTRY:			EXIT:
# GR4	-			IAMPR value representing tile
# GR5	-			DAMPR value representing tile
# GR6	-			IAMLR value representing tile
# GR7	-			DAMLR value representing tile
# GR8	region base pointer	[saved]
# GR9	region top pointer	updated to exclude new tile
# GR11	xAMLR mask		[saved]
# GR25	SDRAM size		[saved]
# GR30	LED address		[saved]
#
# - GR8 and GR9 should be rounded up/down to the nearest megabyte before calling
#
###############################################################################
	.globl		__head_split_region
	.type		__head_split_region,@function
__head_split_region:
	subcc.p		gr9,gr8,gr4,icc0
	setlos		#31,gr5
	scan.p		gr4,gr0,gr6
	beq		icc0,#0,__head_region_empty
	sub.p		gr5,gr6,gr6			; bit number of highest set bit (1MB=>20)
	setlos		#1,gr4
	sll.p		gr4,gr6,gr4			; size of region (1 << bitno)
	subi		gr6,#17,gr6			; 1MB => 0x03
	slli.p		gr6,#4,gr6			; 1MB => 0x30
	sub		gr9,gr4,gr9			; move uncovered top down

	or		gr9,gr6,gr4
	ori		gr4,#xAMPRx_S_USER|xAMPRx_C_CACHED|xAMPRx_V,gr4
	or.p		gr4,gr0,gr5

	and		gr4,gr11,gr6
	and.p		gr5,gr11,gr7
	bralr

__head_region_empty:
	or.p		gr0,gr0,gr4
	or		gr0,gr0,gr5
	or.p		gr0,gr0,gr6
	or		gr0,gr0,gr7
	bralr
	.size		__head_split_region, .-__head_split_region

###############################################################################
#
# write the 32-bit hex number in GR8 to ttyS0
#
###############################################################################
#if 0
	.globl		__head_write_to_ttyS0
	.type		__head_write_to_ttyS0,@function
__head_write_to_ttyS0:
	sethi.p		%hi(0xfeff9c00),gr31
	setlo		%lo(0xfeff9c00),gr31
	setlos		#8,gr20

0:	ldubi		@(gr31,#5*8),gr21
	andi		gr21,#0x60,gr21
	subicc		gr21,#0x60,gr21,icc0
	bne		icc0,#0,0b

1:	srli		gr8,#28,gr21
	slli		gr8,#4,gr8

	addi		gr21,#'0',gr21
	subicc		gr21,#'9',gr0,icc0
	bls		icc0,#2,2f
	addi		gr21,#'A'-'0'-10,gr21
2:
	stbi		gr21,@(gr31,#0*8)
	subicc		gr20,#1,gr20,icc0
	bhi		icc0,#2,1b

	setlos		#'\r',gr21
	stbi		gr21,@(gr31,#0*8)

	setlos		#'\n',gr21
	stbi		gr21,@(gr31,#0*8)

3:	ldubi		@(gr31,#5*8),gr21
	andi		gr21,#0x60,gr21
	subicc		gr21,#0x60,gr21,icc0
	bne		icc0,#0,3b
	bralr

	.size		__head_write_to_ttyS0, .-__head_write_to_ttyS0
#endif

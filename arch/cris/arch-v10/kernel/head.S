/* $Id: head.S,v 1.10 2005/06/20 05:12:54 starvik Exp $
 * 
 * Head of the kernel - alter with care
 *
 * Copyright (C) 2000, 2001 Axis Communications AB
 *
 * Authors:	Bjorn Wesen (bjornw@axis.com)
 * 
 * $Log: head.S,v $
 * Revision 1.10  2005/06/20 05:12:54  starvik
 * Remove unnecessary diff to kernel.org tree
 *
 * Revision 1.9  2004/12/13 12:21:51  starvik
 * Added I/O and DMA allocators from Linux 2.4
 *
 * Revision 1.8  2004/11/22 11:41:14  starvik
 * Kernel command line may be supplied to kernel. Not used by Axis but may
 * be used by customers.
 *
 * Revision 1.7  2004/05/14 07:58:01  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.6  2003/04/28 05:31:46  starvik
 * Added section attributes
 *
 * Revision 1.5  2002/12/11 15:42:02  starvik
 * Extracted v10 (ETRAX 100LX) specific stuff from arch/cris/kernel/*.c
 *
 * Revision 1.4  2002/11/07 09:00:44  starvik
 * Names changed for init sections
 * init_task_union -> init_thread_union
 *
 * Revision 1.3  2002/02/05 15:38:23  bjornw
 * Oops.. non-CRAMFS_MAGIC should jump over the copying, not into it...
 *
 * Revision 1.2  2001/12/18 13:35:19  bjornw
 * Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 * Revision 1.43  2001/11/08 15:09:43  starvik
 * Only start MII clock if Ethernet is configured
 *
 * Revision 1.42  2001/11/08 14:37:34  starvik
 * Start MII clock early to make sure that it is running at tranceiver reset
 *
 * Revision 1.41  2001/10/29 14:55:58  pkj
 * Corrected pa$r0 to par0.
 *
 * Revision 1.40  2001/10/03 14:59:57  pkj
 * Added support for resetting the Bluetooth hardware.
 *
 * Revision 1.39  2001/10/01 14:45:03  bjornw
 * Removed underscores and added register prefixes
 *
 * Revision 1.38  2001/09/21 07:14:11  jonashg
 * Made root filesystem (cramfs) use mtdblock driver when booting from flash.
 *
 * Revision 1.37  2001/09/11 13:44:29  orjanf
 * Decouple usage of serial ports for debug and kgdb.
 *
 * Revision 1.36  2001/06/29 12:39:31  pkj
 * Added support for mirroring the first flash to just below the
 * second one, to make them look consecutive to cramfs.
 *
 * Revision 1.35  2001/06/25 14:07:00  hp
 * 	Fix review comment.
 * 	* head.S: Use IO_STATE, IO_FIELD and IO_MASK constructs instead of
 * 	magic numbers.  Add comment that -traditional must not be used.
 * 	* entry.S (SYMBOL_NAME): Change redefinition to use ## concatenation.
 * 	Correct and update comment.
 * 	* Makefile (.S.o): Don't use -traditional.  Add comment why the
 * 	toplevel rule can't be used (now that there's a reason).
 *
 * Revision 1.34  2001/05/15 07:08:14  hp
 * Tweak "notice" to reflect that both r8 r9 are used
 *
 * Revision 1.33  2001/05/15 06:40:05  hp
 * Put bulk of code in .text.init, data in .data.init
 *
 * Revision 1.32  2001/05/15 06:18:56  hp
 * Execute review comment: s/bcc/bhs/g; s/bcs/blo/g
 *
 * Revision 1.31  2001/05/15 06:08:40  hp
 * Add sentence about autodetecting the bit31-MMU-bug
 *
 * Revision 1.30  2001/05/15 06:00:05  hp
 * Update comment: LOW_MAP is not forced on xsim anymore.
 *
 * Revision 1.29  2001/04/18 12:51:59  orjanf
 * * Reverted review change regarding the use of bcs/bcc.
 * * Removed non-working LED-clearing code.
 *
 * Revision 1.28  2001/04/17 13:58:39  orjanf
 * * Renamed CONFIG_KGDB to CONFIG_ETRAX_KGDB.
 *
 * Revision 1.27  2001/04/17 11:42:35  orjanf
 * Changed according to review:
 * * Added comment explaining memory map bug.
 * * Changed bcs and bcc to blo and bhs, respectively.
 * * Removed mentioning of Stallone and Olga boards.
 *
 * Revision 1.26  2001/04/06 12:31:07  jonashg
 * Check for cramfs in flash before RAM instead of RAM before flash.
 *
 * Revision 1.25  2001/04/04 06:23:53  starvik
 * Initialize DRAM if not already initialized
 *
 * Revision 1.24  2001/04/03 11:12:00  starvik
 * Removed dram init (done by rescue or etrax100boot
 * Corrected include
 *
 * Revision 1.23  2001/04/03 09:53:03  starvik
 * Include hw_settings.S
 *
 * Revision 1.22  2001/03/26 14:23:26  bjornw
 * Namechange of some config options
 *
 * Revision 1.21  2001/03/08 12:14:41  bjornw
 * * Config name for ETRAX IDE was renamed
 * * Removed G27 auto-setting when JULIETTE is chosen (need to make this
 *   a new config option later)
 *
 * Revision 1.20  2001/02/23 12:47:56  bjornw
 * MMU regs during LOW_MAP updated to reflect a newer reality
 *
 * Revision 1.19  2001/02/19 11:12:07  bjornw
 * Changed comment header format
 *
 * Revision 1.18  2001/02/15 07:25:38  starvik
 * Added support for synchronous serial ports
 *
 * Revision 1.17  2001/02/08 15:53:13  starvik
 * Last commit removed some important ifdefs
 *
 * Revision 1.16  2001/02/08 15:20:38  starvik
 * Include dram_init.S as inline
 *
 * Revision 1.15  2001/01/29 18:12:01  bjornw
 * Corrected some comments
 *
 * Revision 1.14  2001/01/29 13:11:29  starvik
 * Include dram_init.S (with DRAM/SDRAM initialization)
 *
 * Revision 1.13  2001/01/23 14:54:57  markusl
 * Updated for USB
 * i.e. added r_gen_config settings
 *
 * Revision 1.12  2001/01/19 16:16:29  perf
 * Added temporary mapping of 0x0c->0x0c to avoid flash loading confusion.
 * Renamed serial options from ETRAX100 to ETRAX.
 *
 * Revision 1.11  2001/01/16 16:31:38  bjornw
 * * Changed name and semantics of running_from_flash to romfs_in_flash,
 *   set by head.S to indicate to setup.c whether there is a cramfs image
 *   after the kernels BSS or not. Should work for all three boot-cases
 *   (DRAM with cramfs in DRAM, DRAM with cramfs in flash (compressed boot),
 *    and flash with cramfs in flash)
 *
 * Revision 1.10  2001/01/16 14:12:21  bjornw
 * * Check for cramfs start passed in r9 from the decompressor, if all other
 *   cramfs options fail (if we boot from DRAM but don't find a cramfs image
 *   after the kernel in DRAM, it is probably still in the flash)
 * * Check magic in cramfs detection when booting from flash directly
 *
 * Revision 1.9  2001/01/15 17:17:02  bjornw
 * * Corrected the code that detects the cramfs lengths
 * * Added a comment saying that the above does not work due to other
 *   reasons..
 *
 * Revision 1.8  2001/01/15 16:27:51  jonashg
 * Made boot after flashing work.
 * * end destination is __vmlinux_end in RAM.
 * * _romfs_start moved because of virtual memory.
 *
 * Revision 1.7  2000/11/21 13:55:29  bjornw
 * Use CONFIG_CRIS_LOW_MAP for the low VM map instead of explicit CPU type
 *
 * Revision 1.6  2000/10/06 12:36:55  bjornw
 * Forgot swapper_pg_dir when changing memory map..
 *
 * Revision 1.5  2000/10/04 16:49:30  bjornw
 * * Fixed memory mapping in LX
 * * Check for cramfs instead of romfs
 *
 */
	
#define ASSEMBLER_MACROS_ONLY
/* The IO_* macros use the ## token concatenation operator, so
   -traditional must not be used when assembling this file.  */
#include <asm/arch/sv_addr_ag.h>

#define CRAMFS_MAGIC 0x28cd3d45
#define RAM_INIT_MAGIC 0x56902387
#define COMMAND_LINE_MAGIC 0x87109563

#define START_ETHERNET_CLOCK IO_STATE(R_NETWORK_GEN_CONFIG, enable, on) |\
                             IO_STATE(R_NETWORK_GEN_CONFIG, phy, mii_clk)
				
	;; exported symbols
		
	.globl	etrax_irv
	.globl	romfs_start
	.globl	romfs_length
	.globl	romfs_in_flash
	.globl  swapper_pg_dir
					
	.text

	;; This is the entry point of the kernel. We are in supervisor mode.
	;; 0x00000000 if Flash, 0x40004000 if DRAM
	;; since etrax actually starts at address 2 when booting from flash, we
	;; put a nop (2 bytes) here first so we dont accidentally skip the di
	;;
	;; NOTICE! The registers r8 and r9 are used as parameters carrying
	;; information from the decompressor (if the kernel was compressed). 
	;; They should not be used in the code below until read.
	
	nop	
	di

	;; First setup the kseg_c mapping from where the kernel is linked
	;; to 0x40000000 (where the actual DRAM resides) otherwise
	;; we cannot do very much! See arch/cris/README.mm
	;;
	;; Notice that since we're potentially running at 0x00 or 0x40 right now,
	;; we will get a fault as soon as we enable the MMU if we dont
	;; temporarily map those segments linearily.
	;;
	;; Due to a bug in Etrax-100 LX version 1 we need to map the memory
	;; slightly different.  The bug is that you can't remap bit 31 of
	;; an address.  Though we can check the version register for
	;; whether the bug is present, some constants would then have to
	;; be variables, so we don't.  The drawback is that you can "only" map
	;; 1G per process with CONFIG_CRIS_LOW_MAP.

#ifdef CONFIG_CRIS_LOW_MAP
	; kseg mappings, temporary map of 0xc0->0x40
 	move.d	  IO_FIELD (R_MMU_KBASE_HI, base_c, 4)		\
		| IO_FIELD (R_MMU_KBASE_HI, base_b, 0xb)	\
		| IO_FIELD (R_MMU_KBASE_HI, base_9, 9)		\
		| IO_FIELD (R_MMU_KBASE_HI, base_8, 8), $r0
	move.d	$r0, [R_MMU_KBASE_HI]

	; temporary map of 0x40->0x40 and 0x60->0x40 
 	move.d	  IO_FIELD (R_MMU_KBASE_LO, base_6, 4)		\
		| IO_FIELD (R_MMU_KBASE_LO, base_4, 4), $r0
	move.d	$r0, [R_MMU_KBASE_LO]

	; mmu enable, segs e,c,b,a,6,5,4,0 segment mapped
 	move.d	  IO_STATE (R_MMU_CONFIG, mmu_enable, enable)	\
		| IO_STATE (R_MMU_CONFIG, inv_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, acc_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, we_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, seg_f, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_e, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_d, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_c, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_b, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_a, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_9, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_8, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_7, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_6, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_5, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_4, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_3, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_2, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_1, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_0, seg), $r0
	move.d	$r0, [R_MMU_CONFIG]
#else
	; kseg mappings
 	move.d	  IO_FIELD (R_MMU_KBASE_HI, base_e, 8)		\
		| IO_FIELD (R_MMU_KBASE_HI, base_c, 4)		\
		| IO_FIELD (R_MMU_KBASE_HI, base_b, 0xb), $r0
	move.d	$r0, [R_MMU_KBASE_HI]

	; temporary map of 0x40->0x40 and 0x00->0x00 
	move.d	  IO_FIELD (R_MMU_KBASE_LO, base_4, 4), $r0
	move.d	$r0, [R_MMU_KBASE_LO]

	; mmu enable, segs f,e,c,b,4,0 segment mapped
 	move.d	  IO_STATE (R_MMU_CONFIG, mmu_enable, enable)	\
		| IO_STATE (R_MMU_CONFIG, inv_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, acc_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, we_excp, enable)	\
		| IO_STATE (R_MMU_CONFIG, seg_f, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_e, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_d, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_c, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_b, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_a, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_9, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_8, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_7, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_6, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_5, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_4, seg)		\
		| IO_STATE (R_MMU_CONFIG, seg_3, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_2, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_1, page)		\
		| IO_STATE (R_MMU_CONFIG, seg_0, seg), $r0
	move.d	$r0, [R_MMU_CONFIG]
#endif

	;; Now we need to sort out the segments and their locations in RAM or
	;; Flash. The image in the Flash (or in DRAM) consists of 3 pieces:
	;; 1) kernel text, 2) kernel data, 3) ROM filesystem image
	;; But the linker has linked the kernel to expect this layout in
	;; DRAM memory:
	;; 1) kernel text, 2) kernel data, 3) kernel BSS
	;; (the location of the ROM filesystem is determined by the krom driver)
	;; If we boot this from Flash, we want to keep the ROM filesystem in
	;; the flash, we want to copy the text and need to copy the data to DRAM.
	;; But if we boot from DRAM, we need to move the ROMFS image
	;; from its position after kernel data, to after kernel BSS, BEFORE the
	;; kernel starts using the BSS area (since its "overlayed" with the ROMFS)
	;;
	;; In both cases, we start in un-cached mode, and need to jump into a
	;; cached PC after we're done fiddling around with the segments.
	;; 
	;; arch/etrax100/etrax100.ld sets some symbols that define the start
	;; and end of each segment.

	;; Check if we start from DRAM or FLASH by testing PC
	
	move.d	$pc,$r0
	and.d	0x7fffffff,$r0	; get rid of the non-cache bit
	cmp.d	0x10000,$r0	; arbitrary... just something above this code
	blo	_inflash0
	nop

	jump	_inram		; enter cached ram

	;; Jumpgate for branches.
_inflash0:
	jump	_inflash

	;; Put this in a suitable section where we can reclaim storage
	;; after init.
	.section ".init.text", "ax"
_inflash:
#ifdef CONFIG_ETRAX_ETHERNET	
	;; Start MII clock to make sure it is running when tranceiver is reset
	move.d START_ETHERNET_CLOCK, $r0
	move.d $r0, [R_NETWORK_GEN_CONFIG]
#endif

	;; Set up waitstates etc according to kernel configuration.
#ifndef CONFIG_SVINTO_SIM
	move.d   CONFIG_ETRAX_DEF_R_WAITSTATES, $r0
	move.d   $r0, [R_WAITSTATES]

	move.d   CONFIG_ETRAX_DEF_R_BUS_CONFIG, $r0
	move.d   $r0, [R_BUS_CONFIG]
#endif

	;; We need to initialze DRAM registers before we start using the DRAM

	cmp.d	RAM_INIT_MAGIC, $r8	; Already initialized?
	beq	_dram_init_finished
	nop
	
#include "../lib/dram_init.S"

_dram_init_finished:		
	;; Copy text+data to DRAM
	;; This is fragile - the calculation of r4 as the image size depends
	;; on that the labels below actually are the first and last positions
	;; in the linker-script.
	;;
	;; Then the locating of the cramfs image depends on the aforementioned
	;; image being located in the flash at 0. This is most often not true,
	;; thus the following does not work (normally there is a rescue-block
	;; between the physical start of the flash and the flash-image start,
	;; and when run with compression, the kernel is actually unpacked to
	;; DRAM and we never get here in the first place :))
	
	moveq	0, $r0			; source
	move.d	text_start, $r1		; destination
	move.d	__vmlinux_end, $r2	; end destination
	move.d	$r2, $r4
	sub.d	$r1, $r4		; r4=__vmlinux_end in flash, used below
1:	move.w	[$r0+], $r3
	move.w	$r3, [$r1+]
	cmp.d	$r2, $r1
	blo	1b
	nop

	;; We keep the cramfs in the flash.
	;; There might be none, but that does not matter because
	;; we don't do anything than read some bytes here.

	moveq	0, $r0
	move.d	$r0, [romfs_length] ; default if there is no cramfs

	move.d  [$r4], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne	1f
	nop
	move.d	[$r4 + 4], $r0	; cramfs_super.size
	move.d	$r0, [romfs_length]
#ifdef CONFIG_CRIS_LOW_MAP
	add.d   0x50000000, $r4	; add flash start in virtual memory (cached)
#else
	add.d   0xf0000000, $r4	; add flash start in virtual memory (cached)
#endif
	move.d	$r4, [romfs_start]
1:	
	moveq	1, $r0
	move.d	$r0, [romfs_in_flash]
		
	jump	_start_it	; enter code, cached this time

_inram:
	;; Move the ROM fs to after BSS end. This assumes that the cramfs
	;; second longword contains the length of the cramfs

	moveq	0, $r0
	move.d	$r0, [romfs_length] ; default if there is no cramfs
	
	;; The kernel could have been unpacked to DRAM by the loader, but
	;; the cramfs image could still be in the Flash directly after the
	;; compressed kernel image. The loader passes the address of the
	;; byte succeeding the last compressed byte in the flash in the
	;; register r9 when starting the kernel. Check if r9 points to a
	;; decent cramfs image!
	;; (Notice that if this is not booted from the loader, r9 will be
	;;  garbage but we do sanity checks on it, the chance that it points
	;;  to a cramfs magic is small.. )
	
	cmp.d	0x0ffffff8, $r9
	bhs	_no_romfs_in_flash	; r9 points outside the flash area
	nop
	move.d	[$r9], $r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, $r0
	bne	_no_romfs_in_flash
	nop
	move.d	[$r9+4], $r0	; cramfs_super.length
	move.d	$r0, [romfs_length]
#ifdef CONFIG_CRIS_LOW_MAP
	add.d   0x50000000, $r9	; add flash start in virtual memory (cached)
#else
	add.d   0xf0000000, $r9	; add flash start in virtual memory (cached)
#endif
	move.d	$r9, [romfs_start]

	moveq	1, $r0
	move.d	$r0, [romfs_in_flash]

	jump	_start_it	; enter code, cached this time

_no_romfs_in_flash:
	
	;; Check if there is a cramfs (magic value).
	;; Notice that we check for cramfs magic value - which is
	;; the "rom fs" we'll possibly use in 2.4 if not JFFS (which does
	;; not need this mechanism anyway)

	move.d	__vmlinux_end, $r0; the image will be after the vmlinux end address
	move.d	[$r0], $r1	; cramfs assumes same endian on host/target
	cmp.d	CRAMFS_MAGIC, $r1; magic value in cramfs superblock
	bne	2f
	nop

	;; Ok. What is its size ? 
	
	move.d	[$r0 + 4], $r2	; cramfs_super.size (again, no need to swapwb)

	;; We want to copy it to the end of the BSS

	move.d	_end, $r1

	;; Remember values so cramfs and setup can find this info

	move.d	$r1, [romfs_start]	; new romfs location
	move.d	$r2, [romfs_length]

	;; We need to copy it backwards, since they can be overlapping

	add.d	$r2, $r0
	add.d	$r2, $r1
		
	;; Go ahead. Make my loop.

	lsrq	1, $r2		; size is in bytes, we copy words

1:	move.w	[$r0=$r0-2],$r3
	move.w	$r3,[$r1=$r1-2]
	subq	1, $r2
	bne	1b
	nop

2:		
	;; Dont worry that the BSS is tainted. It will be cleared later.

	moveq	0, $r0
	move.d	$r0, [romfs_in_flash]

	jump	_start_it	; better skip the additional cramfs check below
	
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

	;; the kernel stack is overlayed with the task structure for each
	;; task. thus the initial kernel stack is in the same page as the
	;; init_task (but starts in the top of the page, size 8192)
	move.d	init_thread_union + 8192, $sp
	move.d	ibr_start,$r0	; this symbol is set by the linker script 
	move	$r0,$ibr
	move.d	$r0,[etrax_irv]	; set the interrupt base register and pointer
	
	;; Clear BSS region, from _bss_start to _end

	move.d	__bss_start, $r0
	move.d	_end, $r1
1:	clear.d	[$r0+]
	cmp.d	$r1, $r0
	blo	1b
	nop
	
#ifdef CONFIG_BLK_DEV_ETRAXIDE
	;; disable ATA before enabling it in genconfig below
	moveq	0,$r0
	move.d	$r0,[R_ATA_CTRL_DATA]
	move.d	$r0,[R_ATA_TRANSFER_CNT]
	move.d	$r0,[R_ATA_CONFIG]
#if 0
	move.d	R_PORT_G_DATA, $r1
	move.d	$r0, [$r1]; assert ATA bus-reset
	nop
	nop
	nop
	nop
	nop
	nop
	move.d	0x08000000,$r0
	move.d	$r0,[$r1]
#endif
#endif

#ifdef CONFIG_JULIETTE
	;; configure external DMA channel 0 before enabling it in genconfig
	
	moveq	0,$r0
	move.d	$r0,[R_EXT_DMA_0_ADDR]
	; cnt enable, word size, output, stop, size 0
	move.d	  IO_STATE (R_EXT_DMA_0_CMD, cnt, enable)	\
		| IO_STATE (R_EXT_DMA_0_CMD, rqpol, ahigh)	\
		| IO_STATE (R_EXT_DMA_0_CMD, apol, ahigh)	\
		| IO_STATE (R_EXT_DMA_0_CMD, rq_ack, burst)	\
		| IO_STATE (R_EXT_DMA_0_CMD, wid, word)		\
		| IO_STATE (R_EXT_DMA_0_CMD, dir, output)	\
		| IO_STATE (R_EXT_DMA_0_CMD, run, stop)		\
		| IO_FIELD (R_EXT_DMA_0_CMD, trf_count, 0),$r0
	move.d	$r0,[R_EXT_DMA_0_CMD]

	;; reset dma4 and wait for completion
	
	moveq	IO_STATE (R_DMA_CH4_CMD, cmd, reset),$r0
	move.b	$r0,[R_DMA_CH4_CMD]
1:	move.b	[R_DMA_CH4_CMD],$r0
	and.b	IO_MASK (R_DMA_CH4_CMD, cmd),$r0
	cmp.b	IO_STATE (R_DMA_CH4_CMD, cmd, reset),$r0
	beq	1b
	nop

	;; reset dma5 and wait for completion
	
	moveq	IO_STATE (R_DMA_CH5_CMD, cmd, reset),$r0
	move.b	$r0,[R_DMA_CH5_CMD]
1:	move.b	[R_DMA_CH5_CMD],$r0
	and.b	IO_MASK (R_DMA_CH5_CMD, cmd),$r0
	cmp.b	IO_STATE (R_DMA_CH5_CMD, cmd, reset),$r0
	beq	1b
	nop
#endif	
			
	;; Etrax product HW genconfig setup

	moveq	0,$r0

	;; Init interfaces (disable them).
	or.d	  IO_STATE (R_GEN_CONFIG, scsi0, disable) \
		| IO_STATE (R_GEN_CONFIG, ata, disable) \
		| IO_STATE (R_GEN_CONFIG, par0, disable) \
		| IO_STATE (R_GEN_CONFIG, ser2, disable) \
		| IO_STATE (R_GEN_CONFIG, mio, disable) \
		| IO_STATE (R_GEN_CONFIG, scsi1, disable) \
		| IO_STATE (R_GEN_CONFIG, scsi0w, disable) \
		| IO_STATE (R_GEN_CONFIG, par1, disable) \
		| IO_STATE (R_GEN_CONFIG, ser3, disable) \
		| IO_STATE (R_GEN_CONFIG, mio_w, disable) \
		| IO_STATE (R_GEN_CONFIG, usb1, disable) \
		| IO_STATE (R_GEN_CONFIG, usb2, disable) \
		| IO_STATE (R_GEN_CONFIG, par_w, disable),$r0

	;; Init DMA channel muxing (set to unused clients).
	or.d	  IO_STATE (R_GEN_CONFIG, dma2, ata)	\
		| IO_STATE (R_GEN_CONFIG, dma3, ata) \
		| IO_STATE (R_GEN_CONFIG, dma4, scsi1) \
		| IO_STATE (R_GEN_CONFIG, dma5, scsi1) \
		| IO_STATE (R_GEN_CONFIG, dma6, unused) \
		| IO_STATE (R_GEN_CONFIG, dma7, unused) \
		| IO_STATE (R_GEN_CONFIG, dma8, usb) \
		| IO_STATE (R_GEN_CONFIG, dma9, usb),$r0


#if defined(CONFIG_ETRAX_DEF_R_PORT_G0_DIR_OUT)
        or.d      IO_STATE (R_GEN_CONFIG, g0dir, out),$r0
#endif

#if defined(CONFIG_ETRAX_DEF_R_PORT_G8_15_DIR_OUT)
        or.d      IO_STATE (R_GEN_CONFIG, g8_15dir, out),$r0
#endif
#if defined(CONFIG_ETRAX_DEF_R_PORT_G16_23_DIR_OUT)
       or.d      IO_STATE (R_GEN_CONFIG, g16_23dir, out),$r0
#endif

#if defined(CONFIG_ETRAX_DEF_R_PORT_G24_DIR_OUT)
       or.d      IO_STATE (R_GEN_CONFIG, g24dir, out),$r0
#endif

	move.d	$r0,[genconfig_shadow] ; init a shadow register of R_GEN_CONFIG

#ifndef CONFIG_SVINTO_SIM
	move.d	$r0,[R_GEN_CONFIG]

#if 0
	moveq	4,$r0
	move.b	$r0,[R_DMA_CH6_CMD]	; reset (ser0 dma out)
	move.b	$r0,[R_DMA_CH7_CMD]	; reset (ser0 dma in)
1:	move.b	[R_DMA_CH6_CMD],$r0	; wait for reset cycle to finish
	and.b	7,$r0
	cmp.b	4,$r0
	beq	1b
	nop
1:	move.b	[R_DMA_CH7_CMD],$r0	; wait for reset cycle to finish
	and.b	7,$r0
	cmp.b	4,$r0
	beq	1b
	nop
#endif
	
	moveq	IO_STATE (R_DMA_CH8_CMD, cmd, reset),$r0
	move.b	$r0,[R_DMA_CH8_CMD]	; reset (ser1 dma out)
	move.b	$r0,[R_DMA_CH9_CMD]	; reset (ser1 dma in)
1:	move.b	[R_DMA_CH8_CMD],$r0	; wait for reset cycle to finish
	andq	IO_MASK (R_DMA_CH8_CMD, cmd),$r0
	cmpq	IO_STATE (R_DMA_CH8_CMD, cmd, reset),$r0
	beq	1b
	nop
1:	move.b	[R_DMA_CH9_CMD],$r0	; wait for reset cycle to finish
	andq	IO_MASK (R_DMA_CH9_CMD, cmd),$r0
	cmpq	IO_STATE (R_DMA_CH9_CMD, cmd, reset),$r0
	beq	1b
	nop

	;; setup port PA and PB default initial directions and data
	;; including their shadow registers
		
	move.b	CONFIG_ETRAX_DEF_R_PORT_PA_DIR,$r0
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_PA7)
	or.b	IO_STATE (R_PORT_PA_DIR, dir7, output),$r0
#endif
	move.b	$r0,[port_pa_dir_shadow]
	move.b	$r0,[R_PORT_PA_DIR]
	move.b	CONFIG_ETRAX_DEF_R_PORT_PA_DATA,$r0
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_PA7)
#if defined(CONFIG_BLUETOOTH_RESET_ACTIVE_HIGH)
	and.b	~(1 << 7),$r0
#else
	or.b	(1 << 7),$r0
#endif
#endif
	move.b	$r0,[port_pa_data_shadow]
	move.b	$r0,[R_PORT_PA_DATA]
	
	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_CONFIG,$r0
	move.b	$r0,[port_pb_config_shadow]
	move.b	$r0,[R_PORT_PB_CONFIG]
	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_DIR,$r0
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_PB5)
	or.b	IO_STATE (R_PORT_PB_DIR, dir5, output),$r0
#endif
	move.b	$r0,[port_pb_dir_shadow]
	move.b	$r0,[R_PORT_PB_DIR]
	move.b	CONFIG_ETRAX_DEF_R_PORT_PB_DATA,$r0
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_PB5)
#if defined(CONFIG_BLUETOOTH_RESET_ACTIVE_HIGH)
	and.b	~(1 << 5),$r0
#else
	or.b	(1 << 5),$r0
#endif
#endif
	move.b	$r0,[port_pb_data_shadow]
	move.b	$r0,[R_PORT_PB_DATA]

	moveq   0, $r0
	move.d  $r0,[port_pb_i2c_shadow]
	move.d  $r0, [R_PORT_PB_I2C]

	moveq	0,$r0
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_G10)
#if defined(CONFIG_BLUETOOTH_RESET_ACTIVE_HIGH)
	and.d	~(1 << 10),$r0
#else
	or.d	(1 << 10),$r0
#endif
#endif
#if defined(CONFIG_BLUETOOTH) && defined(CONFIG_BLUETOOTH_RESET_G11)
#if defined(CONFIG_BLUETOOTH_RESET_ACTIVE_HIGH)
	and.d	~(1 << 11),$r0
#else
	or.d	(1 << 11),$r0
#endif
#endif
	move.d	$r0,[port_g_data_shadow]
	move.d	$r0,[R_PORT_G_DATA]
	
	;; setup the serial port 0 at 115200 baud for debug purposes
	
	moveq	  IO_STATE (R_SERIAL0_XOFF, tx_stop, enable)		\
		| IO_STATE (R_SERIAL0_XOFF, auto_xoff, disable)		\
		| IO_FIELD (R_SERIAL0_XOFF, xoff_char, 0),$r0
	move.d	$r0,[R_SERIAL0_XOFF] 

	; 115.2kbaud for both transmit and receive
	move.b	  IO_STATE (R_SERIAL0_BAUD, tr_baud, c115k2Hz)		\
		| IO_STATE (R_SERIAL0_BAUD, rec_baud, c115k2Hz),$r0
	move.b	$r0,[R_SERIAL0_BAUD]

	; Set up and enable the serial0 receiver.
	move.b	  IO_STATE (R_SERIAL0_REC_CTRL, dma_err, stop)		\
		| IO_STATE (R_SERIAL0_REC_CTRL, rec_enable, enable)	\
		| IO_STATE (R_SERIAL0_REC_CTRL, rts_, active)		\
		| IO_STATE (R_SERIAL0_REC_CTRL, sampling, middle)	\
		| IO_STATE (R_SERIAL0_REC_CTRL, rec_stick_par, normal)	\
		| IO_STATE (R_SERIAL0_REC_CTRL, rec_par, even)		\
		| IO_STATE (R_SERIAL0_REC_CTRL, rec_par_en, disable)	\
		| IO_STATE (R_SERIAL0_REC_CTRL, rec_bitnr, rec_8bit),$r0
	move.b	$r0,[R_SERIAL0_REC_CTRL] 
	
	; Set up and enable the serial0 transmitter.
	move.b	  IO_FIELD (R_SERIAL0_TR_CTRL, txd, 0)			\
		| IO_STATE (R_SERIAL0_TR_CTRL, tr_enable, enable)	\
		| IO_STATE (R_SERIAL0_TR_CTRL, auto_cts, disabled)	\
		| IO_STATE (R_SERIAL0_TR_CTRL, stop_bits, one_bit)	\
		| IO_STATE (R_SERIAL0_TR_CTRL, tr_stick_par, normal)	\
		| IO_STATE (R_SERIAL0_TR_CTRL, tr_par, even)		\
		| IO_STATE (R_SERIAL0_TR_CTRL, tr_par_en, disable)	\
		| IO_STATE (R_SERIAL0_TR_CTRL, tr_bitnr, tr_8bit),$r0
	move.b	$r0,[R_SERIAL0_TR_CTRL]

	;; setup the serial port 1 at 115200 baud for debug purposes
	
	moveq	  IO_STATE (R_SERIAL1_XOFF, tx_stop, enable)		\
		| IO_STATE (R_SERIAL1_XOFF, auto_xoff, disable)		\
		| IO_FIELD (R_SERIAL1_XOFF, xoff_char, 0),$r0
	move.d	$r0,[R_SERIAL1_XOFF] 

	; 115.2kbaud for both transmit and receive
	move.b	  IO_STATE (R_SERIAL1_BAUD, tr_baud, c115k2Hz)		\
		| IO_STATE (R_SERIAL1_BAUD, rec_baud, c115k2Hz),$r0
	move.b	$r0,[R_SERIAL1_BAUD]

	; Set up and enable the serial1 receiver.
	move.b	  IO_STATE (R_SERIAL1_REC_CTRL, dma_err, stop)		\
		| IO_STATE (R_SERIAL1_REC_CTRL, rec_enable, enable)	\
		| IO_STATE (R_SERIAL1_REC_CTRL, rts_, active)		\
		| IO_STATE (R_SERIAL1_REC_CTRL, sampling, middle)	\
		| IO_STATE (R_SERIAL1_REC_CTRL, rec_stick_par, normal)	\
		| IO_STATE (R_SERIAL1_REC_CTRL, rec_par, even)		\
		| IO_STATE (R_SERIAL1_REC_CTRL, rec_par_en, disable)	\
		| IO_STATE (R_SERIAL1_REC_CTRL, rec_bitnr, rec_8bit),$r0
	move.b	$r0,[R_SERIAL1_REC_CTRL] 
	
	; Set up and enable the serial1 transmitter.
	move.b	  IO_FIELD (R_SERIAL1_TR_CTRL, txd, 0)			\
		| IO_STATE (R_SERIAL1_TR_CTRL, tr_enable, enable)	\
		| IO_STATE (R_SERIAL1_TR_CTRL, auto_cts, disabled)	\
		| IO_STATE (R_SERIAL1_TR_CTRL, stop_bits, one_bit)	\
		| IO_STATE (R_SERIAL1_TR_CTRL, tr_stick_par, normal)	\
		| IO_STATE (R_SERIAL1_TR_CTRL, tr_par, even)		\
		| IO_STATE (R_SERIAL1_TR_CTRL, tr_par_en, disable)	\
		| IO_STATE (R_SERIAL1_TR_CTRL, tr_bitnr, tr_8bit),$r0
	move.b	$r0,[R_SERIAL1_TR_CTRL]

	
#ifdef CONFIG_ETRAX_SERIAL_PORT3	
	;; setup the serial port 3 at 115200 baud for debug purposes
	
	moveq	  IO_STATE (R_SERIAL3_XOFF, tx_stop, enable)		\
		| IO_STATE (R_SERIAL3_XOFF, auto_xoff, disable)		\
		| IO_FIELD (R_SERIAL3_XOFF, xoff_char, 0),$r0
	move.d	$r0,[R_SERIAL3_XOFF] 

	; 115.2kbaud for both transmit and receive
	move.b	  IO_STATE (R_SERIAL3_BAUD, tr_baud, c115k2Hz)		\
		| IO_STATE (R_SERIAL3_BAUD, rec_baud, c115k2Hz),$r0
	move.b	$r0,[R_SERIAL3_BAUD]

	; Set up and enable the serial3 receiver.
	move.b	  IO_STATE (R_SERIAL3_REC_CTRL, dma_err, stop)		\
		| IO_STATE (R_SERIAL3_REC_CTRL, rec_enable, enable)	\
		| IO_STATE (R_SERIAL3_REC_CTRL, rts_, active)		\
		| IO_STATE (R_SERIAL3_REC_CTRL, sampling, middle)	\
		| IO_STATE (R_SERIAL3_REC_CTRL, rec_stick_par, normal)	\
		| IO_STATE (R_SERIAL3_REC_CTRL, rec_par, even)		\
		| IO_STATE (R_SERIAL3_REC_CTRL, rec_par_en, disable)	\
		| IO_STATE (R_SERIAL3_REC_CTRL, rec_bitnr, rec_8bit),$r0
	move.b	$r0,[R_SERIAL3_REC_CTRL] 
	
	; Set up and enable the serial3 transmitter.
	move.b	  IO_FIELD (R_SERIAL3_TR_CTRL, txd, 0)			\
		| IO_STATE (R_SERIAL3_TR_CTRL, tr_enable, enable)	\
		| IO_STATE (R_SERIAL3_TR_CTRL, auto_cts, disabled)	\
		| IO_STATE (R_SERIAL3_TR_CTRL, stop_bits, one_bit)	\
		| IO_STATE (R_SERIAL3_TR_CTRL, tr_stick_par, normal)	\
		| IO_STATE (R_SERIAL3_TR_CTRL, tr_par, even)		\
		| IO_STATE (R_SERIAL3_TR_CTRL, tr_par_en, disable)	\
		| IO_STATE (R_SERIAL3_TR_CTRL, tr_bitnr, tr_8bit),$r0
	move.b	$r0,[R_SERIAL3_TR_CTRL]
#endif
	
#endif /* CONFIG_SVINTO_SIM */

	jump	start_kernel	; jump into the C-function start_kernel in init/main.c
		
	.data
etrax_irv:	
	.dword	0
romfs_start:
	.dword	0
romfs_length:
	.dword	0
romfs_in_flash:
	.dword	0
	
	;; put some special pages at the beginning of the kernel aligned
	;; to page boundaries - the kernel cannot start until after this

#ifdef CONFIG_CRIS_LOW_MAP
swapper_pg_dir = 0x60002000
#else	
swapper_pg_dir = 0xc0002000
#endif

	.section ".init.data", "aw"
#include "../lib/hw_settings.S"

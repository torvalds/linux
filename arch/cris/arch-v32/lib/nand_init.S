##=============================================================================
##
##      nand_init.S
##
##      The bootrom copies data from the NAND flash to the internal RAM but
##      due to a bug/feature we can only trust the 256 first bytes. So this
##      code copies more data from NAND flash to internal RAM. Obvioulsy this
##      code must fit in the first 256 bytes so alter with care.
##
##	Some notes about the bug/feature for future reference:
##        The bootrom copies the first 127 KB from NAND flash to internal
##        memory. The problem is that it does a bytewise copy. NAND flashes
##        does autoincrement on the address so for a 16-bite device each
##        read/write increases the address by two. So the copy loop in the
##        bootrom will discard every second byte. This is solved by inserting
##        zeroes in every second byte in the first erase block.
##
##        The bootrom also incorrectly assumes that it can read the flash
##        linear with only one read command but the flash will actually
##        switch between normal area and spare area if you do that so we
##        can't trust more than the first 256 bytes.
##
##=============================================================================

#include <arch/hwregs/asm/reg_map_asm.h>
#include <arch/hwregs/asm/gio_defs_asm.h>
#include <arch/hwregs/asm/pinmux_defs_asm.h>
#include <arch/hwregs/asm/bif_core_defs_asm.h>
#include <arch/hwregs/asm/config_defs_asm.h>

;; There are 8-bit NAND flashes and 16-bit NAND flashes.
;; We need to treat them slightly different.
#if CONFIG_ETRAX_FLASH_BUSWIDTH==2
#define PAGE_SIZE 256
#else
#error 2
#define PAGE_SIZE 512
#endif
#define ERASE_BLOCK 16384

;; GPIO pins connected to NAND flash
#define CE 4
#define CLE 5
#define ALE 6
#define BY 7

;; Address space for NAND flash
#define NAND_RD_ADDR 0x90000000
#define NAND_WR_ADDR 0x94000000

#define READ_CMD 0x00

;; Readability macros
#define CSP_MASK \
	REG_MASK(bif_core, rw_grp3_cfg, gated_csp0) | \
	REG_MASK(bif_core, rw_grp3_cfg, gated_csp1)
#define CSP_VAL \
	REG_STATE(bif_core, rw_grp3_cfg, gated_csp0, rd) | \
	REG_STATE(bif_core, rw_grp3_cfg, gated_csp1, wr)

;;----------------------------------------------------------------------------
;; Macros to set/clear GPIO bits

.macro SET x
	or.b   (1<<\x),$r9
	move.d $r9, [$r2]
.endm

.macro CLR x
	and.b  ~(1<<\x),$r9
	move.d $r9, [$r2]
.endm

;;----------------------------------------------------------------------------

nand_boot:
	;; Check if nand boot was selected
	move.d REG_ADDR(config, regi_config, r_bootsel), $r0
	move.d [$r0], $r0
	and.d  REG_MASK(config, r_bootsel, boot_mode), $r0
	cmp.d  REG_STATE(config, r_bootsel, boot_mode, nand), $r0
	bne normal_boot ; No NAND boot
	nop

copy_nand_to_ram:
	;; copy_nand_to_ram
	;; Arguments
	;;   r10 - destination
	;;   r11 - source offset
	;;   r12 - size
	;;   r13 - Address to jump to after completion
	;; Note : r10-r12 are clobbered on return
	;; Registers used:
	;;   r0 - NAND_RD_ADDR
	;;   r1 - NAND_WR_ADDR
	;;   r2 - reg_gio_rw_pa_dout
	;;   r3 - reg_gio_r_pa_din
	;;   r4 - tmp
	;;   r5 - byte counter within a page
	;;   r6 - reg_pinmux_rw_pa
	;;   r7 - reg_gio_rw_pa_oe
	;;   r8 - reg_bif_core_rw_grp3_cfg
	;;   r9 - reg_gio_rw_pa_dout shadow
	move.d 0x90000000, $r0
	move.d 0x94000000, $r1
	move.d REG_ADDR(gio, regi_gio, rw_pa_dout), $r2
	move.d REG_ADDR(gio, regi_gio, r_pa_din), $r3
	move.d REG_ADDR(pinmux, regi_pinmux, rw_pa), $r6
	move.d REG_ADDR(gio, regi_gio, rw_pa_oe), $r7
	move.d REG_ADDR(bif_core, regi_bif_core, rw_grp3_cfg), $r8

#if CONFIG_ETRAX_FLASH_BUSWIDTH==2
	lsrq	1, $r11
#endif
	;; Set up GPIO
	move.d [$r2], $r9
	move.d [$r7], $r4
	or.b (1<<ALE) | (1 << CLE) | (1<<CE), $r4
	move.d $r4, [$r7]

	;; Set up bif
	move.d [$r8], $r4
	and.d CSP_MASK, $r4
	or.d CSP_VAL, $r4
	move.d $r4, [$r8]

1:	;; Copy one page
	CLR CE
	SET CLE
	moveq	READ_CMD, $r4
	move.b	$r4, [$r1]
	moveq	20, $r4
2:	bne	2b
	subq	1, $r4
	CLR CLE
	SET ALE
	clear.w [$r1] 		; Column address = 0
	move.d	$r11, $r4
	lsrq	8, $r4
	move.b	$r4, [$r1]	; Row address
	lsrq	8, $r4
	move.b	$r4, [$r1]	; Row address
	moveq	20, $r4
2:	bne	2b
	subq	1, $r4
	CLR ALE
2:	move.d	[$r3], $r4
	and.d	1 << BY, $r4
	beq 2b
	movu.w  PAGE_SIZE, $r5
2:	; Copy one byte/word
#if CONFIG_ETRAX_FLASH_BUSWIDTH==2
	move.w  [$r0], $r4
#else
	move.b  [$r0], $r4
#endif
	subq	1, $r5
	bne	2b
#if CONFIG_ETRAX_FLASH_BUSWIDTH==2
	move.w  $r4, [$r10+]
	subu.w	PAGE_SIZE*2, $r12
#else
	move.b  $r4, [$r10+]
	subu.w	PAGE_SIZE, $r12
#endif
	bpl	1b
	addu.w	PAGE_SIZE, $r11

	;; End of copy
	jump	$r13
	nop

	;; This will warn if the code above is too large. If you consider
	;; to remove this you don't understand the bug/feature.
	.org 256
	.org ERASE_BLOCK

normal_boot:

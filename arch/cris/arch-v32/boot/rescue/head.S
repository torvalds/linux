/* $Id: head.S,v 1.4 2004/11/01 16:10:28 starvik Exp $
 *
 * This used to be the rescue code but now that is handled by the
 * RedBoot based RFL instead. Nothing to see here, move along.
 */

#include <asm/arch/hwregs/reg_map_asm.h>
#include <asm/arch/hwregs/config_defs_asm.h>

	.text

	;; Start clocks for used blocks.
	move.d REG_ADDR(config, regi_config, rw_clk_ctrl), $r1
	move.d [$r1], $r0
	or.d   REG_STATE(config, rw_clk_ctrl, cpu, yes) | \
	       REG_STATE(config, rw_clk_ctrl, bif, yes) | \
	       REG_STATE(config, rw_clk_ctrl, fix_io, yes), $r0
	move.d $r0, [$r1]

	;; Copy 68KB NAND flash to Internal RAM (if NAND boot)
	move.d 0x38004000, $r10
	move.d 0x8000, $r11
	move.d 0x11000, $r12
	move.d copy_complete, $r13
	and.d  0x000fffff, $r13
	or.d   0x38000000, $r13

#include "../../lib/nand_init.S"

	;; No NAND found
	move.d	CONFIG_ETRAX_PTABLE_SECTOR, $r10
	jump	$r10 ; Jump to decompresser
	nop

copy_complete:
	move.d	0x38000000 + CONFIG_ETRAX_PTABLE_SECTOR, $r10
	jump	$r10 ; Jump to decompresser
	nop

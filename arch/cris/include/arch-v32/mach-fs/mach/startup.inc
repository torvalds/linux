#ifndef STARTUP_INC_INCLUDED
#define STARTUP_INC_INCLUDED

#include <hwregs/asm/reg_map_asm.h>
#include <hwregs/asm/bif_core_defs_asm.h>
#include <hwregs/asm/gio_defs_asm.h>
#include <hwregs/asm/config_defs_asm.h>

	.macro GIO_INIT
	move.d	CONFIG_ETRAX_DEF_GIO_PA_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pa_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PA_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pa_oe), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PB_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pb_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PB_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pb_oe), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PC_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pc_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PC_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pc_oe), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PD_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pd_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PD_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pd_oe), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PE_OUT, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pe_dout), $r1
	move.d	$r0, [$r1]

	move.d	CONFIG_ETRAX_DEF_GIO_PE_OE, $r0
	move.d	REG_ADDR(gio, regi_gio, rw_pe_oe), $r1
	move.d	$r0, [$r1]
	.endm

	.macro START_CLOCKS
	move.d REG_ADDR(config, regi_config, rw_clk_ctrl), $r1
	move.d [$r1], $r0
	or.d   REG_STATE(config, rw_clk_ctrl, cpu, yes) | \
	       REG_STATE(config, rw_clk_ctrl, bif, yes) | \
	       REG_STATE(config, rw_clk_ctrl, fix_io, yes), $r0
	move.d $r0, [$r1]
	.endm

	.macro SETUP_WAIT_STATES
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
	.endm

#endif

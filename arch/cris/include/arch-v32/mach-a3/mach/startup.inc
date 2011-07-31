#ifndef STARTUP_INC_INCLUDED
#define STARTUP_INC_INCLUDED

#include <hwregs/asm/reg_map_asm.h>
#include <hwregs/asm/gio_defs_asm.h>
#include <hwregs/asm/pio_defs_asm.h>
#include <hwregs/asm/clkgen_defs_asm.h>
#include <hwregs/asm/pinmux_defs_asm.h>

	.macro GIO_SET_P BITS, OUTREG
	bmi	1f		; btstq: bit -> N flag
	nop
	or.d	\BITS, \OUTREG
1:
	.endm

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

	move.d	0xFFFFFFFF, $r0
	move.d	REG_ADDR(pinmux, regi_pinmux, rw_gio_pa), $r1
	move.d	$r0, [$r1]
	move.d	REG_ADDR(pinmux, regi_pinmux, rw_gio_pc), $r1
	move.d	$r0, [$r1]

	;; If eth_mdio, eth, geth bits are set in hwprot, don't
	;; set them to gpio, as this means they have been configured
	;; earlier and shouldn't be changed.
	move.d	0xFC000000, $r2 ; pins 25..0 are eth_mdio, eth, geth
	move.d	REG_ADDR(pinmux, regi_pinmux, rw_hwprot), $r1
	move.d	[$r1], $r0
	btstq	REG_BIT(pinmux, rw_hwprot, eth), $r0
	GIO_SET_P 0x00FFFF00, $r2		;; pins 8..23 are eth
	btstq	REG_BIT(pinmux, rw_hwprot, eth_mdio), $r0
	GIO_SET_P 0x03000000, $r2		;; pins 24..25 are eth_mdio
	btstq	REG_BIT(pinmux, rw_hwprot, geth), $r0
	GIO_SET_P 0x000000FF, $r2		;; pins 0..7 are geth
	move.d	REG_ADDR(pinmux, regi_pinmux, rw_gio_pb), $r1
	move.d	$r2, [$r1]
	.endm

	.macro START_CLOCKS
	move.d REG_ADDR(clkgen, regi_clkgen, rw_clk_ctrl), $r1
	move.d [$r1], $r0
	or.d REG_STATE(clkgen, rw_clk_ctrl, cpu, yes) | \
	     REG_STATE(clkgen, rw_clk_ctrl, ddr2, yes) | \
	     REG_STATE(clkgen, rw_clk_ctrl, memarb_bar_ddr, yes), $r0
	move.d $r0, [$r1]
	.endm

	.macro SETUP_WAIT_STATES
	move.d REG_ADDR(pio, regi_pio, rw_ce0_cfg), $r0
	move.d CONFIG_ETRAX_PIO_CE0_CFG, $r1
	move.d $r1, [$r0]
	move.d REG_ADDR(pio, regi_pio, rw_ce1_cfg), $r0
	move.d CONFIG_ETRAX_PIO_CE1_CFG, $r1
	move.d $r1, [$r0]
	move.d REG_ADDR(pio, regi_pio, rw_ce2_cfg), $r0
	move.d CONFIG_ETRAX_PIO_CE2_CFG, $r1
	move.d $r1, [$r0]
	.endm
#endif

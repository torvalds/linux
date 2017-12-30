/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This table is used by some tools to extract hardware parameters.
 * The table should be included in the kernel and the decompressor.
 * Don't forget to update the tools if you change this table.
 *
 * Copyright (C) 2001-2007 Axis Communications AB
 *
 * Authors:  Mikael Starvik <starvik@axis.com>
 */

#include <hwregs/asm/reg_map_asm.h>
#include <hwregs/asm/ddr2_defs_asm.h>
#include <hwregs/asm/gio_defs_asm.h>

	.ascii "HW_PARAM_MAGIC" ; Magic number
	.dword 0xc0004000	; Kernel start address

	; Debug port
#ifdef CONFIG_ETRAX_DEBUG_PORT0
	.dword 0
#elif defined(CONFIG_ETRAX_DEBUG_PORT1)
	.dword 1
#elif defined(CONFIG_ETRAX_DEBUG_PORT2)
	.dword 2
#elif defined(CONFIG_ETRAX_DEBUG_PORT3)
	.dword 3
#else
	.dword 4 ; No debug
#endif

	; Register values
	.dword REG_ADDR(ddr2, regi_ddr2_ctrl, rw_cfg)
	.dword CONFIG_ETRAX_DDR2_CONFIG
	.dword REG_ADDR(ddr2, regi_ddr2_ctrl, rw_latency)
	.dword CONFIG_ETRAX_DDR2_LATENCY
	.dword REG_ADDR(ddr2, regi_ddr2_ctrl, rw_timing)
	.dword CONFIG_ETRAX_DDR2_TIMING
	.dword CONFIG_ETRAX_DDR2_MRS

	.dword REG_ADDR(gio, regi_gio, rw_pa_dout)
	.dword CONFIG_ETRAX_DEF_GIO_PA_OUT
	.dword REG_ADDR(gio, regi_gio, rw_pa_oe)
	.dword CONFIG_ETRAX_DEF_GIO_PA_OE
	.dword REG_ADDR(gio, regi_gio, rw_pb_dout)
	.dword CONFIG_ETRAX_DEF_GIO_PB_OUT
	.dword REG_ADDR(gio, regi_gio, rw_pb_oe)
	.dword CONFIG_ETRAX_DEF_GIO_PB_OE
	.dword REG_ADDR(gio, regi_gio, rw_pc_dout)
	.dword CONFIG_ETRAX_DEF_GIO_PC_OUT
	.dword REG_ADDR(gio, regi_gio, rw_pc_oe)
	.dword CONFIG_ETRAX_DEF_GIO_PC_OE

	.dword 0 ; No more register values

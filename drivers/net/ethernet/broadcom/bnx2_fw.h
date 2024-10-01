/* bnx2_fw.h: QLogic bnx2 network driver.
 *
 * Copyright (c) 2004, 2005, 2006, 2007 Broadcom Corporation
 * Copyright (c) 2014-2015 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

/* Initialized Values for the Completion Processor. */
static const struct cpu_reg cpu_reg_com = {
	.mode = BNX2_COM_CPU_MODE,
	.mode_value_halt = BNX2_COM_CPU_MODE_SOFT_HALT,
	.mode_value_sstep = BNX2_COM_CPU_MODE_STEP_ENA,
	.state = BNX2_COM_CPU_STATE,
	.state_value_clear = 0xffffff,
	.gpr0 = BNX2_COM_CPU_REG_FILE,
	.evmask = BNX2_COM_CPU_EVENT_MASK,
	.pc = BNX2_COM_CPU_PROGRAM_COUNTER,
	.inst = BNX2_COM_CPU_INSTRUCTION,
	.bp = BNX2_COM_CPU_HW_BREAKPOINT,
	.spad_base = BNX2_COM_SCRATCH,
	.mips_view_base = 0x8000000,
};

/* Initialized Values the Command Processor. */
static const struct cpu_reg cpu_reg_cp = {
	.mode = BNX2_CP_CPU_MODE,
	.mode_value_halt = BNX2_CP_CPU_MODE_SOFT_HALT,
	.mode_value_sstep = BNX2_CP_CPU_MODE_STEP_ENA,
	.state = BNX2_CP_CPU_STATE,
	.state_value_clear = 0xffffff,
	.gpr0 = BNX2_CP_CPU_REG_FILE,
	.evmask = BNX2_CP_CPU_EVENT_MASK,
	.pc = BNX2_CP_CPU_PROGRAM_COUNTER,
	.inst = BNX2_CP_CPU_INSTRUCTION,
	.bp = BNX2_CP_CPU_HW_BREAKPOINT,
	.spad_base = BNX2_CP_SCRATCH,
	.mips_view_base = 0x8000000,
};

/* Initialized Values for the RX Processor. */
static const struct cpu_reg cpu_reg_rxp = {
	.mode = BNX2_RXP_CPU_MODE,
	.mode_value_halt = BNX2_RXP_CPU_MODE_SOFT_HALT,
	.mode_value_sstep = BNX2_RXP_CPU_MODE_STEP_ENA,
	.state = BNX2_RXP_CPU_STATE,
	.state_value_clear = 0xffffff,
	.gpr0 = BNX2_RXP_CPU_REG_FILE,
	.evmask = BNX2_RXP_CPU_EVENT_MASK,
	.pc = BNX2_RXP_CPU_PROGRAM_COUNTER,
	.inst = BNX2_RXP_CPU_INSTRUCTION,
	.bp = BNX2_RXP_CPU_HW_BREAKPOINT,
	.spad_base = BNX2_RXP_SCRATCH,
	.mips_view_base = 0x8000000,
};

/* Initialized Values for the TX Patch-up Processor. */
static const struct cpu_reg cpu_reg_tpat = {
	.mode = BNX2_TPAT_CPU_MODE,
	.mode_value_halt = BNX2_TPAT_CPU_MODE_SOFT_HALT,
	.mode_value_sstep = BNX2_TPAT_CPU_MODE_STEP_ENA,
	.state = BNX2_TPAT_CPU_STATE,
	.state_value_clear = 0xffffff,
	.gpr0 = BNX2_TPAT_CPU_REG_FILE,
	.evmask = BNX2_TPAT_CPU_EVENT_MASK,
	.pc = BNX2_TPAT_CPU_PROGRAM_COUNTER,
	.inst = BNX2_TPAT_CPU_INSTRUCTION,
	.bp = BNX2_TPAT_CPU_HW_BREAKPOINT,
	.spad_base = BNX2_TPAT_SCRATCH,
	.mips_view_base = 0x8000000,
};

/* Initialized Values for the TX Processor. */
static const struct cpu_reg cpu_reg_txp = {
	.mode = BNX2_TXP_CPU_MODE,
	.mode_value_halt = BNX2_TXP_CPU_MODE_SOFT_HALT,
	.mode_value_sstep = BNX2_TXP_CPU_MODE_STEP_ENA,
	.state = BNX2_TXP_CPU_STATE,
	.state_value_clear = 0xffffff,
	.gpr0 = BNX2_TXP_CPU_REG_FILE,
	.evmask = BNX2_TXP_CPU_EVENT_MASK,
	.pc = BNX2_TXP_CPU_PROGRAM_COUNTER,
	.inst = BNX2_TXP_CPU_INSTRUCTION,
	.bp = BNX2_TXP_CPU_HW_BREAKPOINT,
	.spad_base = BNX2_TXP_SCRATCH,
	.mips_view_base = 0x8000000,
};

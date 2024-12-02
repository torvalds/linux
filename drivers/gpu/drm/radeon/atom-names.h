/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author: Stanislaw Skowronek
 */

#ifndef ATOM_NAMES_H
#define ATOM_NAMES_H

#include "atom.h"

#ifdef ATOM_DEBUG

#define ATOM_OP_NAMES_CNT 123
static char *atom_op_names[ATOM_OP_NAMES_CNT] = {
"RESERVED", "MOVE_REG", "MOVE_PS", "MOVE_WS", "MOVE_FB", "MOVE_PLL",
"MOVE_MC", "AND_REG", "AND_PS", "AND_WS", "AND_FB", "AND_PLL", "AND_MC",
"OR_REG", "OR_PS", "OR_WS", "OR_FB", "OR_PLL", "OR_MC", "SHIFT_LEFT_REG",
"SHIFT_LEFT_PS", "SHIFT_LEFT_WS", "SHIFT_LEFT_FB", "SHIFT_LEFT_PLL",
"SHIFT_LEFT_MC", "SHIFT_RIGHT_REG", "SHIFT_RIGHT_PS", "SHIFT_RIGHT_WS",
"SHIFT_RIGHT_FB", "SHIFT_RIGHT_PLL", "SHIFT_RIGHT_MC", "MUL_REG",
"MUL_PS", "MUL_WS", "MUL_FB", "MUL_PLL", "MUL_MC", "DIV_REG", "DIV_PS",
"DIV_WS", "DIV_FB", "DIV_PLL", "DIV_MC", "ADD_REG", "ADD_PS", "ADD_WS",
"ADD_FB", "ADD_PLL", "ADD_MC", "SUB_REG", "SUB_PS", "SUB_WS", "SUB_FB",
"SUB_PLL", "SUB_MC", "SET_ATI_PORT", "SET_PCI_PORT", "SET_SYS_IO_PORT",
"SET_REG_BLOCK", "SET_FB_BASE", "COMPARE_REG", "COMPARE_PS",
"COMPARE_WS", "COMPARE_FB", "COMPARE_PLL", "COMPARE_MC", "SWITCH",
"JUMP", "JUMP_EQUAL", "JUMP_BELOW", "JUMP_ABOVE", "JUMP_BELOW_OR_EQUAL",
"JUMP_ABOVE_OR_EQUAL", "JUMP_NOT_EQUAL", "TEST_REG", "TEST_PS", "TEST_WS",
"TEST_FB", "TEST_PLL", "TEST_MC", "DELAY_MILLISEC", "DELAY_MICROSEC",
"CALL_TABLE", "REPEAT", "CLEAR_REG", "CLEAR_PS", "CLEAR_WS", "CLEAR_FB",
"CLEAR_PLL", "CLEAR_MC", "NOP", "EOT", "MASK_REG", "MASK_PS", "MASK_WS",
"MASK_FB", "MASK_PLL", "MASK_MC", "POST_CARD", "BEEP", "SAVE_REG",
"RESTORE_REG", "SET_DATA_BLOCK", "XOR_REG", "XOR_PS", "XOR_WS", "XOR_FB",
"XOR_PLL", "XOR_MC", "SHL_REG", "SHL_PS", "SHL_WS", "SHL_FB", "SHL_PLL",
"SHL_MC", "SHR_REG", "SHR_PS", "SHR_WS", "SHR_FB", "SHR_PLL", "SHR_MC",
"DEBUG", "CTB_DS",
};

#define ATOM_TABLE_NAMES_CNT 74
static char *atom_table_names[ATOM_TABLE_NAMES_CNT] = {
"ASIC_Init", "GetDisplaySurfaceSize", "ASIC_RegistersInit",
"VRAM_BlockVenderDetection", "SetClocksRatio", "MemoryControllerInit",
"GPIO_PinInit", "MemoryParamAdjust", "DVOEncoderControl",
"GPIOPinControl", "SetEngineClock", "SetMemoryClock", "SetPixelClock",
"DynamicClockGating", "ResetMemoryDLL", "ResetMemoryDevice",
"MemoryPLLInit", "EnableMemorySelfRefresh", "AdjustMemoryController",
"EnableASIC_StaticPwrMgt", "ASIC_StaticPwrMgtStatusChange",
"DAC_LoadDetection", "TMDS2EncoderControl", "LCD1OutputControl",
"DAC1EncoderControl", "DAC2EncoderControl", "DVOOutputControl",
"CV1OutputControl", "SetCRTC_DPM_State", "TVEncoderControl",
"TMDS1EncoderControl", "LVDSEncoderControl", "TV1OutputControl",
"EnableScaler", "BlankCRTC", "EnableCRTC", "GetPixelClock",
"EnableVGA_Render", "EnableVGA_Access", "SetCRTC_Timing",
"SetCRTC_OverScan", "SetCRTC_Replication", "SelectCRTC_Source",
"EnableGraphSurfaces", "UpdateCRTC_DoubleBufferRegisters",
"LUT_AutoFill", "EnableHW_IconCursor", "GetMemoryClock",
"GetEngineClock", "SetCRTC_UsingDTDTiming", "TVBootUpStdPinDetection",
"DFP2OutputControl", "VRAM_BlockDetectionByStrap", "MemoryCleanUp",
"ReadEDIDFromHWAssistedI2C", "WriteOneByteToHWAssistedI2C",
"ReadHWAssistedI2CStatus", "SpeedFanControl", "PowerConnectorDetection",
"MC_Synchronization", "ComputeMemoryEnginePLL", "MemoryRefreshConversion",
"VRAM_GetCurrentInfoBlock", "DynamicMemorySettings", "MemoryTraining",
"EnableLVDS_SS", "DFP1OutputControl", "SetVoltage", "CRT1OutputControl",
"CRT2OutputControl", "SetupHWAssistedI2CStatus", "ClockSource",
"MemoryDeviceInit", "EnableYUV",
};

#define ATOM_IO_NAMES_CNT 5
static char *atom_io_names[ATOM_IO_NAMES_CNT] = {
"MM", "PLL", "MC", "PCIE", "PCIE PORT",
};

#else

#define ATOM_OP_NAMES_CNT 0
#define ATOM_TABLE_NAMES_CNT 0
#define ATOM_IO_NAMES_CNT 0

#endif

#endif

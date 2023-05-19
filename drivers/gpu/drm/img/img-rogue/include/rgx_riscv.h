/*************************************************************************/ /*!
@File           rgx_riscv.h
@Title
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Platform       RGX
@Description    RGX RISCV definitions, kernel/user space
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(RGX_RISCV_H)
#define RGX_RISCV_H

#include "km/rgxdefs_km.h"


/* Utility defines to convert regions to virtual addresses and remaps */
#define RGXRISCVFW_GET_REGION_BASE(r)           IMG_UINT32_C((r) << 28)
#define RGXRISCVFW_GET_REGION(a)                IMG_UINT32_C((a) >> 28)
#define RGXRISCVFW_MAX_REGION_SIZE              IMG_UINT32_C(1 << 28)
#define RGXRISCVFW_GET_REMAP(r)                 (RGX_CR_FWCORE_ADDR_REMAP_CONFIG0 + ((r) * 8U))

/* RISCV remap output is aligned to 4K */
#define RGXRISCVFW_REMAP_CONFIG_DEVVADDR_ALIGN  (0x1000U)

/*
 * FW bootloader defines
 */
#define RGXRISCVFW_BOOTLDR_CODE_REGION          IMG_UINT32_C(0xC)
#define RGXRISCVFW_BOOTLDR_DATA_REGION          IMG_UINT32_C(0x5)
#define RGXRISCVFW_BOOTLDR_CODE_BASE            (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_BOOTLDR_CODE_REGION))
#define RGXRISCVFW_BOOTLDR_DATA_BASE            (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_BOOTLDR_DATA_REGION))
#define RGXRISCVFW_BOOTLDR_CODE_REMAP           (RGXRISCVFW_GET_REMAP(RGXRISCVFW_BOOTLDR_CODE_REGION))
#define RGXRISCVFW_BOOTLDR_DATA_REMAP           (RGXRISCVFW_GET_REMAP(RGXRISCVFW_BOOTLDR_DATA_REGION))

/* Bootloader data offset in dwords from the beginning of the FW data allocation */
#define RGXRISCVFW_BOOTLDR_CONF_OFFSET          (0x0)

/*
 * FW coremem region defines
 */
#define RGXRISCVFW_COREMEM_REGION               IMG_UINT32_C(0x8)
#define RGXRISCVFW_COREMEM_MAX_SIZE             IMG_UINT32_C(0x10000000) /* 256 MB */
#define RGXRISCVFW_COREMEM_BASE                 (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_COREMEM_REGION))
#define RGXRISCVFW_COREMEM_END                  (RGXRISCVFW_COREMEM_BASE + RGXRISCVFW_COREMEM_MAX_SIZE - 1U)


/*
 * Host-FW shared data defines
 */
#define RGXRISCVFW_SHARED_CACHED_DATA_REGION    (0x6UL)
#define RGXRISCVFW_SHARED_UNCACHED_DATA_REGION  (0xDUL)
#define RGXRISCVFW_SHARED_CACHED_DATA_BASE      (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_SHARED_CACHED_DATA_REGION))
#define RGXRISCVFW_SHARED_UNCACHED_DATA_BASE    (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_SHARED_UNCACHED_DATA_REGION))
#define RGXRISCVFW_SHARED_CACHED_DATA_REMAP     (RGXRISCVFW_GET_REMAP(RGXRISCVFW_SHARED_CACHED_DATA_REGION))
#define RGXRISCVFW_SHARED_UNCACHED_DATA_REMAP   (RGXRISCVFW_GET_REMAP(RGXRISCVFW_SHARED_UNCACHED_DATA_REGION))


/*
 * GPU SOCIF access defines
 */
#define RGXRISCVFW_SOCIF_REGION                 (0x2U)
#define RGXRISCVFW_SOCIF_BASE                   (RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_SOCIF_REGION))


/* The things that follow are excluded when compiling assembly sources */
#if !defined(RGXRISCVFW_ASSEMBLY_CODE)
#include "img_types.h"

#define RGXFW_PROCESSOR_RISCV       "RISCV"
#define RGXRISCVFW_CORE_ID_VALUE    (0x00450B02U)
#define RGXRISCVFW_MISA_ADDR        (0x301U)
#define RGXRISCVFW_MISA_VALUE       (0x40001104U)
#define RGXRISCVFW_MSCRATCH_ADDR    (0x340U)

typedef struct
{
	IMG_UINT64 ui64CorememCodeDevVAddr;
	IMG_UINT64 ui64CorememDataDevVAddr;
	IMG_UINT32 ui32CorememCodeFWAddr;
	IMG_UINT32 ui32CorememDataFWAddr;
	IMG_UINT32 ui32CorememCodeSize;
	IMG_UINT32 ui32CorememDataSize;
	IMG_UINT32 ui32Flags;
	IMG_UINT32 ui32Reserved;
} RGXRISCVFW_BOOT_DATA;

/*
 * List of registers to be printed in debug dump.
 * First column:  register names (general purpose or control/status registers)
 * Second column: register number to be used in abstract access register command
 * (see RISC-V debug spec v0.13)
 */
#define RGXRISCVFW_DEBUG_DUMP_REGISTERS \
	X(pc,        0x7b1) /* dpc */ \
	X(ra,       0x1001) \
	X(sp,       0x1002) \
	X(mepc,      0x341) \
	X(mcause,    0x342) \
	X(mdseac,    0xfc0) \
	X(mstatus,   0x300) \
	X(mie,       0x304) \
	X(mip,       0x344) \
	X(mscratch,  0x340) \
	X(mbvnc0,    0xffe) \
	X(mbvnc1,    0xfff) \
	X(micect,    0x7f0) \
	X(mdcect,    0x7f3) \
	X(mdcrfct,   0x7f4) \

typedef struct
{
#define X(name, address) \
	IMG_UINT32 name;

	RGXRISCVFW_DEBUG_DUMP_REGISTERS
#undef X
} RGXRISCVFW_STATE;


#define RGXRISCVFW_MCAUSE_INTERRUPT  (1U << 31)

#define RGXRISCVFW_MCAUSE_TABLE \
	X(0x00000000U, IMG_FALSE, "NMI pin assertion") /* Also reset value */ \
	X(0x00000001U, IMG_TRUE,  "Instruction access fault") \
	X(0x00000002U, IMG_TRUE,  "Illegal instruction") \
	X(0x00000003U, IMG_TRUE,  "Breakpoint") \
	X(0x00000004U, IMG_TRUE,  "Load address misaligned") \
	X(0x00000005U, IMG_TRUE,  "Load access fault") \
	X(0x00000006U, IMG_TRUE,  "Store/AMO address misaligned") \
	X(0x00000007U, IMG_TRUE,  "Store/AMO access fault") \
	X(0x0000000BU, IMG_TRUE,  "Environment call from M-mode (FW assert)") \
	X(0x80000007U, IMG_FALSE, "Machine timer interrupt") \
	X(0x8000000BU, IMG_FALSE, "Machine external interrupt") \
	X(0x8000001EU, IMG_FALSE, "Machine correctable error local interrupt") \
	X(0xF0000000U, IMG_TRUE,  "Machine D-bus store error NMI") \
	X(0xF0000001U, IMG_TRUE,  "Machine D-bus non-blocking load error NMI") \
	X(0xF0000002U, IMG_TRUE,  "dCache unrecoverable NMI")


/* Debug module HW defines */
#define RGXRISCVFW_DMI_COMMAND_ACCESS_REGISTER (0U)
#define RGXRISCVFW_DMI_COMMAND_ACCESS_MEMORY   (2U)
#define RGXRISCVFW_DMI_COMMAND_AAxSIZE_32BIT   (2UL << 20)
#define RGXRISCVFW_DMI_COMMAND_WRITE           (1UL << 16)
#define RGXRISCVFW_DMI_COMMAND_READ            (0UL << 16)
#define RGXRISCVFW_DMI_SBCS_SBACCESS_32BIT     (2U)

/* Abstract command error codes (descriptions from RISC-V debug spec v0.13) */
typedef IMG_UINT32 RGXRISCVFW_ABSTRACT_CMD_ERR;

/* No error. */
#define RISCV_ABSTRACT_CMD_NO_ERROR 0U

/*
 * An abstract command was executing while command, abstractcs, or abstractauto
 * was written, or when one of the data or progbuf registers was read or
 * written. This status is only written if cmderr contains 0.
 */
#define RISCV_ABSTRACT_CMD_BUSY 1U

/*
 * The requested command is not supported, regardless of whether
 * the hart is running or not.
 */
#define RISCV_ABSTRACT_CMD_NOT_SUPPORTED 2U

/*
 * An exception occurred while executing the command
 * (e.g. while executing the Program Buffer).
 */
#define RISCV_ABSTRACT_CMD_EXCEPTION 3U

/*
 * The abstract command couldn't execute because the hart wasn't in the required
 * state (running/halted), or unavailable.
 */
#define RISCV_ABSTRACT_CMD_HALT_RESUME 4U

/*
 * The abstract command failed due to a bus error
 * (e.g. alignment, access size, or timeout).
 */
#define RISCV_ABSTRACT_CMD_BUS_ERROR 5U

/* The command failed for another reason. */
#define RISCV_ABSTRACT_CMD_OTHER_ERROR 7U


/* System Bus error codes (descriptions from RISC-V debug spec v0.13) */
typedef IMG_UINT32 RGXRISCVFW_SYSBUS_ERR;

/* There was no bus error. */
#define RISCV_SYSBUS_NO_ERROR 0U

/* There was a timeout. */
#define RISCV_SYSBUS_TIMEOUT 1U

/* A bad address was accessed. */
#define RISCV_SYSBUS_BAD_ADDRESS 2U

/* There was an alignment error. */
#define RISCV_SYSBUS_BAD_ALIGNMENT 3U

/* An access of unsupported size was requested. */
#define RISCV_SYSBUS_UNSUPPORTED_SIZE 4U

/* Other. */
#define RISCV_SYSBUS_OTHER_ERROR 7U


#endif /* RGXRISCVFW_ASSEMBLY_CODE */

#endif /* RGX_RISCV_H */

/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_NVRM_RELOCATION_TABLE_H
#define INCLUDED_NVRM_RELOCATION_TABLE_H

#include "nvcommon.h"
#include "nvrm_init.h"

/**
 * The AP family supports a Relocation Table which lists the devices in the
 * system, their version numbers, and their physical base addressess and
 * aperture size.  Interrupt information is also stored in the table.
 *
 * The relcation table format:
 *
 * +-------------------( 32 bits )-------------------------------------+
 * |                    table version                                  |
 * +-------------------------------------------------------------------+
 * |               [ device table entries ]                            |
 * +-------------------------------------------------------------------+
 * |                    null (0)                                       |
 * +-------------------------------------------------------------------+
 * |                [ irq table entries ]                              |
 * +-------------------------------------------------------------------+
 * |                    null (0)                                       |
 * +-------------------------------------------------------------------+
 *
 * The device table entry format:
 *
 * +-------------------( 32 bits )-------------------------------------+
 * | id [31:16] | major [15:12] | minor [11:8] | res [7:4] | bar [3:0] |
 * |-------------------------------------------------------------------|
 * |                    start address                                  |
 * |-------------------------------------------------------------------|
 * |                    length                                         |
 * +-------------------------------------------------------------------+
 *
 * The irq entry format:
 *
 * +-------------------( 32 bits )-----------------------------------------+
 * |V[31]|rsvd[30:29]|IntDevIdx[28:20]|rsvd[19:17]|DevIdx[16:8]|IntNum[7:0]|
 * +-----------------------------------------------------------------------+
 *
 * Every entry (whether valid or not) will always contain an Interrupt
 * Controller Device Index (IntDevIdx), a Device Index (DevIdx), and an
 * Interrupt Number (IntNum) value. Whether or not that entry actually
 * corresponds to an interrupt source is determined by the valid (V) bit.
 * If the valid bit is 1, the interrupt number corresponds to an actual
 * interrupt source. If the valid bit is zero, this entry represents an
 * interrupt source that was present in a prior SOC but that is no longer
 * used. The slot for that interrupt in the interrupt map table must be
 * preseved because "indexed" interrupts are determined positionally.
 * Removal of an interrupt would change the positional assignment of all
 * following interrupt numbers and would break forward compatibility.
 */

#define NVRM_DEVICE_UNKNOWN     ((NvU32)-2)
#define NVRM_DEVICE_ERROR       ((NvU32)-3)

// The module index in the NvRmModule table is invalid; this is not an error.
#define NVRM_MODULE_INVALID     (0xFFFF)

// Number of interrupt controllers
#define NVRM_MAX_MAIN_INTR_CTLRS    5

// Number of DMA transmit interrupt controllers
#define NVRM_MAX_DRQ_INTR_CTLRS     2

// Number of Arbitration Grant interrupt controllers
#define NVRM_ARB_GNT_INTR_CTLRS     1

// Number of interrupt controllers of all types
#define NVRM_MAX_INTERRUPT_CTLRS    (NVRM_MAX_MAIN_INTR_CTLRS + \
    NVRM_MAX_DRQ_INTR_CTLRS + NVRM_ARB_GNT_INTR_CTLRS)

// Relative position of first DMA transmit interrupt controller
#define NVRM_FIRST_DRQ_INTR_CTLR    (NVRM_MAX_MAIN_INTR_CTLRS)

// Relative position of first Arbitration Grant interrupt controller
#define NVRM_FIRST_ARB_INTR_CTLR    (NVRM_MAX_MAIN_INTR_CTLRS + \
    NVRM_MAX_DRQ_INTR_CTLRS)

// Number of IRQs per interrupt controller (main, DRQ, & ARB)
#define NVRM_IRQS_PER_INTR_CTLR     32

// Number of IRQs per GPIO controller
#define NVRM_IRQS_PER_GPIO_CTLR     32

// Number of IRQs per AHB DMA channel
#define NVRM_IRQS_PER_AHB_DMA_CHAN  1

// Number of IRQs per APB DMA channel
#define NVRM_IRQS_PER_APB_DMA_CHAN  1

// Invalid IRQ valid
#define NVRM_IRQ_INVALID            0xFFFF

// Maximum number of interrupts per device
#define NVRM_MAX_DEVICE_IRQS        8

// Maximum number of IRQs
#define NVRM_MAX_IRQS               500

// Maximum number of devices that can generate IRQs
// !!!CHECKME!!! CHECK THE SIZING OF THIS VALUE
#define NVRM_MAX_IRQ_DEVICES        96

// Maximum number of DMA channels 
#define NVRM_MAX_DMA_CHANNELS       32

// This is the Maximum number of instance of all modules on any chip
// supported by Rm.
// Need to increase this value when more modules are added in the up comming
// chips.
#define NVRM_MAX_MODULE_INSTANCES   256

/**
 * Device IRQ assignments structure.
 */
typedef struct NvRmModuleIrqMapRec
{
    /* Number of IRQs owned by this device */
    NvU16 IrqCount;

    /* Maximum instance IRQ index */
    NvU16 IndexMax;

    /* Base IRQ for subcontroller "index" IRQ fanout */
    NvU16 IndexBase;

    /* IRQs owned by this device */
    NvU16 Irq[NVRM_MAX_DEVICE_IRQS];
} NvRmModuleIrqMap;

/**
 * System IRQ assignments structure.
 */
typedef struct NvRmIrqMapRec
{
    /* Number of devices owning IRQs */
    NvU32 DeviceCount;

    /* Device IRQ mapping */
    NvRmModuleIrqMap DeviceIrq[NVRM_MAX_IRQ_DEVICES];
} NvRmIrqMap;

/**
 * Some hardware modules may be instantiated multiple times - all hw modules
 * are mapped into this structure.
 */
typedef struct NvRmModuleInstanceRec
{
    /* the base address of the module instance */
    NvRmPhysAddr PhysAddr;

    /* length of the aperture */
    NvU32 Length;

    /* bar number */
    // FIXME: not supported properly - each bar is reported as a different
    // hardware module instance.
    NvU8 Bar;

    /* hardware version */
    NvU8 MajorVersion;
    NvU8 MinorVersion;

    /* power group */
    NvU8 DevPowerGroup;

    /* the original index into the relocation table */
    NvU8 DevIdx;

    /* hardware device id */
    NvU32 DeviceId;

    /* Irq mapping for this module instance */
    NvRmModuleIrqMap *IrqMap;

    /* virtual address: will be mapped by a later mechanism.  this is here
     * as a space optimization.
     */
    void *VirtAddr;

    /* Module specific data like clocks, resets etc.. */
    void *ModuleData;
} NvRmModuleInstance;

/**
 * Module index table.  Each index points to the first module instance in the
 * NvRmModuleInstance table. The NvRmModule table itself is indexed by module
 * id.
 */
typedef struct NvRmModuleRec
{
    /* offset into the NvRmModuleInstance table */
    NvU16 Index;
} NvRmModule;

/**
 * Maps relocation table device ids to software module ids.
 * NVRM_DEVICE_UNKNOWN for unknown ids (will keep parsing table),
 * or NVRM_DEVICE_ERROR if something bad happened
 * (will stop parsing the table).
 *
 * NVRM_DEVICE_UNKOWN can be used to cull the device list to save space by
 * not allocating memory for devices that won't be used.
 */
NvU32 NvRmPrivDevToModuleID(NvU32 devid);

/**
 * Parse the relocation table.
 *
 * The module instance table (NvRmModuleInstance) will be allocated to exactly
 * match the number of hardware modules in the system rather than using a
 * worst-case number of instances for all hardware modules.
 *
 * The module table should be allocated prior to this function and should be
 * sized to the maximum number of module ids.
 *
 * The irq map will not be allocated (statically sized).
 *
 * The instance array will be null terminated -- the last instance will contain
 * zero in all of its fields.
 *
 * @param hDevice The resource manager instance
 * @param table The relocation table
 * @param instances Out param - will contain the allocated instance table
 * @param instanceLast Out param - will contain the last allocated instance + 1
 * @param modules Out param - will contain the allocated module table
 * @param irqs The irq table - will be filled in by the parser
 */
NvError
NvRmPrivRelocationTableParse(
    const NvU32 *table,
    NvRmModuleInstance **instances,
    NvRmModuleInstance **instanceLast,
    NvRmModule *modules,
    NvRmIrqMap *irqs );

#endif

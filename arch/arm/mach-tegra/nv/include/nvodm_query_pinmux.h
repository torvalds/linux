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

/**
 * @file
 * <b>NVIDIA Tegra ODM Kit:
 *         Pin-Mux Query Interface</b>
 *
 * @b Description: Defines the ODM query interface for Pin-Mux configurations.
 */

#ifndef INCLUDED_NVODM_QUERY_PINMUX_H
#define INCLUDED_NVODM_QUERY_PINMUX_H

/**
 * @defgroup nvodm_pinmux PinMux Query Interface
 * This is the ODM query interface for pin mux configurations.
 * 
 * Pin-mux configurations are logical definitions. Each I/O module defines
 * their configurations (simply an enum), which may be found in the ODM 
 * adaptation headers.
 * 
 * Every platform defines a unique set of configuration tables. There exists a
 * configuration table for each I/O module and each entry in the table
 * represents the configuration for an I/O module instance.
 * 
 * This interface is used to query the pin-mux configuration tables defined by
 * the ODM, because these configurations are platform-specific.
 * @ingroup nvodm_query
 * @{
 */

#include "nvcommon.h"
#include "nvodm_modules.h"

#define NVODM_QUERY_PINMAP_MULTIPLEXED 0x40000000UL

#if defined(__cplusplus)
extern "C"
{
#endif

/* --- Pin-mux Configurations (for each controller) --- */

/**
 * Defines the ATA pin-mux configurations.
 */
typedef enum
{
    NvOdmAtaPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmAtaPinMap_Force32 = 0x7FFFFFFF,
} NvOdmAtaPinMap;

/**
 * Defines the external clock (CDEV, CSUS) pin-mux configurations.
 */
typedef enum
{
    NvOdmExternalClockPinMap_Config1 = 1,
    NvOdmExternalClockPinMap_Config2,
    NvOdmExternalClockPinMap_Config3,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmExternalClockPinMap_Force32 = 0x7FFFFFFF
} NvOdmExternalClockPinMap;

/**
 * Defines the CRT pin-mux configurations.
 */
typedef enum
{
    NvOdmCrtPinMap_Config1 = 1,
    NvOdmCrtPinMap_Config2,
    NvOdmCrtPinMap_Config3,
    NvOdmCrtPinMap_Config4,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NVOdmCrtPinMap_Force32 = 0x7FFFFFFF,
} NvOdmCrtPinMap;

/**
 * Defines the DAP pin-mux configurations.
 */
typedef enum
{
    NvOdmDapPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmDapPinMap_Force32 = 0x7FFFFFFF,
} NvOdmDapPinMap;

/**
 * Defines the display pin-mux configurations.
 */
typedef enum
{
    NvOdmDisplayPinMap_Config1 = 1,
    NvOdmDisplayPinMap_Config2,
    NvOdmDisplayPinMap_Config3,
    NvOdmDisplayPinMap_Config4,
    NvOdmDisplayPinMap_Config5,
    NvOdmDisplayPinMap_Config6,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmDisplayPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmDisplayPinMap;

/**
 * Defines the blacklight PWM pin-mux configurations.
 */
typedef enum
{
    NvOdmBacklightPwmPinMap_Config1 = 1,
    NvOdmBacklightPwmPinMap_Config2,
    NvOdmBacklightPwmPinMap_Config3,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmBacklightPwmPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmBacklightPwmPinMap;

/**
 * Defines the HDCP pin-mux configurations.
 */
typedef enum
{
    NvOdmHdcpPinMap_Config1 = 1,
    NvOdmHdcpPinMap_Config2,
    NvOdmHdcpPinMap_Config3,
    NvOdmHdcpPinMap_Config4,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmHdcpPinMap_Force32 = 0x7FFFFFFF,
} NvOdmHdcpPinMap;

/**
 * Defines the HDCMI pin-mux configurations.
 */
typedef enum
{
    NvOdmHdmiPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmHdmiPinMap_Force32 = 0x7FFFFFFF,
} NvOdmHdmiPinMap;

/**
 * Defines the HSI pin-mux configurations.
 */
typedef enum
{
    NvOdmHsiPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmHsiPinMap_Force32 = 0x7FFFFFFF,
} NvOdmHsiPinMap;

/**
 * Defines the HSMMC pin-mux configurations.
 */
typedef enum
{
    NvOdmHsmmcPinMap_Config1 = 1,
    NvOdmHsmmcPinMap_Config2,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmHsmmcPinMap_Force32 = 0x7FFFFFFF,
} NvOdmHsmmcPinMap;

/**
 * Defines the OWR pin-mux configurations.
 */
typedef enum
{
    NvOdmOwrPinMap_Config1 = 1,
    NvOdmOwrPinMap_Config2,
    NvOdmOwrPinMap_Config3,

    /**
     * This configuration disables (tristates) OWR pins. This option may be
     * used to change which pins an attached OWR device is using at runtime.
     * In some cases, one device might set up OWR, communicate across this bus,
     * and then set the OWR bus configuration to "multiplexed" so that another
     * device can opt to use OWR with its own configurations at a later time.
     */
    NvOdmOwrPinMap_Multiplexed = NVODM_QUERY_PINMAP_MULTIPLEXED,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmOwrPinMap_Force32 = 0x7FFFFFFF,
} NvOdmOwrPinMap;

/**
 * Defines I2C pin-mux configurations.
 */
typedef enum
{
    NvOdmI2cPinMap_Config1 = 1,
    NvOdmI2cPinMap_Config2,
    NvOdmI2cPinMap_Config3,
    NvOdmI2cPinMap_Config4,

    /**
     * This configuration disables (tristates) I2C pins. This option may be
     * used to change which pins an attached I2C device is using at runtime.
     * 
     * In some cases, one device might set up I2C, communicate across this bus,
     * and then set the I2C bus configuration to "multiplexed" so that another
     * device can opt to use I2C with its own configurations at a later time.
     *
     * This option is only supported on the I2C_2 controller (AP15, AP16, AP20).
     */
    NvOdmI2cPinMap_Multiplexed = NVODM_QUERY_PINMAP_MULTIPLEXED,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmI2cPinMap_Force32 = 0x7FFFFFFF,
} NvOdmI2cPinMap;

/**
 * Defines the I2C PMU pin-mux configurations.
 */
typedef enum
{
    NvOdmI2cPmuPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmI2cPmuPinMap_Force32 = 0x7FFFFFFF,
} NvOdmI2cPmuPinMap;

/**
 * Defines the PWM pin-mux configurations.
 */
typedef enum
{
    NvOdmPwmPinMap_Config1 = 1,
    NvOdmPwmPinMap_Config2,
    NvOdmPwmPinMap_Config3,
    NvOdmPwmPinMap_Config4,
    NvOdmPwmPinMap_Config5,
    NvOdmPwmPinMap_Config6,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmPwmPinMap_Force32 = 0x7FFFFFFF,
} NvOdmPwmPinMap;

/**
 * Defines KBD pin-mux configurations.
 */
typedef enum
{
    NvOdmKbdPinMap_Config1 = 1,
    NvOdmKbdPinMap_Config2,
    NvOdmKbdPinMap_Config3,
    NvOdmKbdPinMap_Config4,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmKbdPinMap_Forc32 = 0x7FFFFFFF,
} NvOdmKbdPinMap;

/**
 * Defines MIO pin-mux configurations.
 */
typedef enum
{
    NvOdmMioPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmMioPinMap_Forc32 = 0x7FFFFFFF,
} NvOdmMioPinMap;

/**
 * Defines NAND pin-mux configurations.
 */
typedef enum
{
    NvOdmNandPinMap_Config1 = 1,
    NvOdmNandPinMap_Config2,
    NvOdmNandPinMap_Config3,
    NvOdmNandPinMap_Config4,
    NvOdmNandPinMap_Config5,
    NvOdmNandPinMap_Config6,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmNandPinMap_Force32 = 0x7FFFFFFF,
} NvOdmNandPinMap;

/**
 * Defines the SDIO pin-mux configurations.
 */
typedef enum
{
    NvOdmSdioPinMap_Config1 = 1,
    NvOdmSdioPinMap_Config2,
    NvOdmSdioPinMap_Config3,
    NvOdmSdioPinMap_Config4,
    NvOdmSdioPinMap_Config5,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmSdioPinMap_Force32 = 0x7FFFFFFF,
} NvOdmSdioPinMap;

/**
 * Defines the SFLASH pin-mux configurations.
 */
typedef enum
{
    NvOdmSflashPinMap_Config1 = 1,
    NvOdmSflashPinMap_Config2,
    NvOdmSflashPinMap_Config3,
    NvOdmSflashPinMap_Config4,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmSflashPinMap_Force32 = 0x7FFFFFFF,
} NvOdmSflashPinMap;

/**
 * Defines the SPDIF pin-mux configurations.
 */
typedef enum
{
    NvOdmSpdifPinMap_Config1 = 1,   /**< Default SPDIF configuration. */
    NvOdmSpdifPinMap_Config2,
    NvOdmSpdifPinMap_Config3,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmSpdifPinMap_Force32 = 0x7FFFFFFF,
} NvOdmSpdifPinMap;

/**
 * Defines the SPI pin-mux configurations.
 */
typedef enum
{
    NvOdmSpiPinMap_Config1 = 1,
    NvOdmSpiPinMap_Config2,
    NvOdmSpiPinMap_Config3,
    NvOdmSpiPinMap_Config4,
    NvOdmSpiPinMap_Config5,
    NvOdmSpiPinMap_Config6,

    /**
     * This configuration disables (tristates) SPI pins. This option may be
     * used to change which pins an attached SPI device is using at runtime.
     * 
     * In some cases, one device might set up SPI, communicate across this bus,
     * and then set the SPI bus configuration to "multiplexed" so that another
     * device can opt to use SPI with its own configurations at a later time.
     *
     * This option is only supported on SPI_3 (AP15, AP16).
     */
    NvOdmSpiPinMap_Multiplexed = NVODM_QUERY_PINMAP_MULTIPLEXED,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmSpiPinMap_Force32 = 0x7FFFFFFF,
} NvOdmSpiPinMap;

/**
 * Defines the TV-out pin-mux configurations.
 */
typedef enum
{
    NvOdmTvoPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmTvoPinMap_Force32 = 0x7FFFFFFF,
} NvOdmTvoPinMap;

/**
 * Defines the USB-ULPI pin-mux configurations.
 */
typedef enum
{
    NvOdmUsbPinMap_Config1 = 1,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmUsbPinMap_Force32 = 0x7FFFFFFF,
} NvOdmUsbPinMap;


/**
 * Defines the TWC pin-mux configurations.
 */
typedef enum
{
    NvOdmTwcPinMap_Config1 = 1,
    NvOdmTwcPinMap_Config2,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmTwcPinMap_Force32 = 0x7FFFFFFF,
} NvOdmTwcPinMap;

/**
 * Defines the UART pin-mux configurations.
 */
typedef enum
{
    NvOdmUartPinMap_Config1 = 1,
    NvOdmUartPinMap_Config2,
    NvOdmUartPinMap_Config3,
    NvOdmUartPinMap_Config4,
    NvOdmUartPinMap_Config5,
    NvOdmUartPinMap_Config6,
    NvOdmUartPinMap_Config7,

    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmUartPinMap_Force32 = 0x7FFFFFFF,
} NvOdmUartPinMap;

/**
 * Defines the video input pin-mux configurations.
 */
typedef enum
{
    NvOdmVideoInputPinMap_Config1 = 1,
    NvOdmVideoInputPinMap_Config2,
    /** Ignore -- Forces compilers to make 32-bit enums. */
    NvOdmVideoInputPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmVideoInputPinMap;

/**
 * Defines the PCI-Express pin-mux configurations.
 */
typedef enum
{
    NvOdmPciExpressPinMap_Config1 = 1,
    NvOdmPciExpressPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmPciExpressPinMap;

/**
 * Defines the SyncNor / OneNAND pin-mux configurations.
 */
typedef enum
{
    NvOdmSyncNorPinMap_Config1 = 1,
    NvOdmSyncNorPinMap_Config2,
    NvOdmSyncNorPinMap_Config3,
    NvOdmSyncNorPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmSyncNorPinMap;

/**
 * Defines the PTM pin-mux configurations.
 */
typedef enum
{
    NvOdmPtmPinMap_Config1 = 1,
    NvOdmPtmPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmPtmPinMap;

/**
 * Defines the one-wire pin-mux configurations.
 */
typedef enum
{
    NvOdmOneWirePinMap_Config1 = 1,
    NvOdmOneWirePinMap_Config2,
    NvOdmOneWirePinMap_Config3,
    NvOdmOneWirePinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmOneWirePinMap;


/**
 * Defines the ULPI pin-mux configurations.
 */
typedef enum
{
    NvOdmUlpiPinMap_Config1 = 1,
    NvOdmUlpiPinMap_Force32 = 0x7FFFFFFFUL,
} NvOdmUlpiPinMap;

/* --- Pin-mux API --- */

/**
 * Gets the pinmux configuration table for a given module.
 *
 * @param IoModule The I/O module to query.
 * @param pPinMuxConfigTable A const pointer to the module's configuration
 *  table. Each entry in the table represents the configuration for the I/O
 *  module instance, where the instance indices start from 0.
 * @param pCount A pointer to a variable that this function sets to the
 *  number of entires in the configuration table.
 */
void
NvOdmQueryPinMux(
    NvOdmIoModule IoModule,
    const NvU32 **pPinMuxConfigTable,
    NvU32 *pCount);

/**
 * Gets the maximum clock speed for a given module as imposed by a board.
 *
 * @param IoModule The I/O module to query.
 * @param pClockSpeedLimits A const pointer to the module's clock speed limit.
 *  Each entry in the array represents the clock speed limit for the I/O
 *  module instance, where the instance indices start from 0.
 * @param pCount A pointer to a variable that this function sets to the
 *  number of entries in the \a pClockSpeedLimits array.
 */

void
NvOdmQueryClockLimits(
    NvOdmIoModule IoModule,
    const NvU32 **pClockSpeedLimits,
    NvU32 *pCount);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif  // INCLUDED_NVODM_QUERY_PINMUX_H

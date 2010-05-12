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
 *         Memory Controller Query Interface</b>
 *
 * @b Description: Defines the ODM query interface for Memory Controller.
 */

#ifndef INCLUDED_NVODM_QUERY_MEMC_H
#define INCLUDED_NVODM_QUERY_MEMC_H

/**
 * @defgroup nvodm_memc Memory Controller Query Interface
 * This is the ODM query interface for memory controller.
 * @ingroup nvodm_query
 * @{
 */

#include "nvcommon.h"
#include "nvodm_query.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Holds the configuration parameters for asynchronous memory like NOR flash
 * or Memory Mapped I/O (MIO).
 */

typedef struct
{
    /// Holds TRUE for enabling access time extension using ROM busy pin.
    NvBool isRomBusyEnable;

    /// Holds the dead time in nano seconds between the end of a write access and
    /// the start of the following access (write or read) for NOR/MIO memory.
    NvU32 WriteDeadTime;

    /// Holds the access time in nano seconds for which write signal is asserted
    /// during a write access for MIO/NOR memory.
    NvU32 WriteAccessTime;

    /// Holds the dead time in nano seconds between the end of a read access and
    /// the start of the following access (write or read) for MIO/NOR memory.
    NvU32 ReadDeadTime;

    /// Holds the access time in nano seconds for which read signal is asserted
    /// during a read access.
    NvU32 ReadAccessTime;

} NvOdmAsynchMemConfig;

/**
 * Holds synchronous memory (SDRAM) controller configuration parameters for the
 * specified SDRAM frequency and controller core voltage. This structure is
 * assigned fixed revision 1.0.
 */
typedef struct NvOdmSdramControllerConfigRec
{
    /// Holds the SDRAM frequency in kHz.
    NvU32 SdramKHz;

    /// Holds minimum core voltage in mV for memory controller operations at
    /// the specified SDRAM frequency. Actual core voltage can be set higher by
    /// DVFS depending on the operation requirements for other SoC modules.
    NvU32 EmcCoreVoltageMv;
 
    /// Holds the memory controller timing parameter 0.
    NvU32 EmcTiming0;
 
    /// Holds the memory controller timing parameter 1.
    NvU32 EmcTiming1;
 
    /// Holds the memory controller timing parameter 2.
    NvU32 EmcTiming2;
 
    /// Holds the memory controller timing parameter 3.
    NvU32 EmcTiming3;
 
    /// Holds the memory controller timing parameter 4.
    NvU32 EmcTiming4;
 
    /// Holds the memory controller timing parameter 5.
    NvU32 EmcTiming5;
 
    /// Holds the memory controller FBIO configuration parameter 6.
    NvU32 EmcFbioCfg6;
 
    /// Holds the memory controller FBIO QSIB delay parameter.
    NvU32 EmcFbioDqsibDly;
 
    /// Holds the emory controller FBIO QUSE delay parameter.
    NvU32 EmcFbioQuseDly;
} NvOdmSdramControllerConfig;

/// Defines revision for basic memory controller configuration structure,
/// i.e., 0x10 is Rev 1.0.
#define NV_EMC_BASIC_REV    (0x10)

/// Defines maximum number of advanced memory controller timing parameters.
#define NV_EMC_ADV_PARAM_NUM_MAX    (50)

/**
 * Holds synchronous memory (SDRAM) advanced controller configuration
 * parameters for the specified SDRAM frequency and controller core voltage.
 * The revision of this structure is started with 2.0, and it is embedded as
 * the structure field.
 */
typedef struct NvOdmSdramControllerConfigAdvRec
{
    /// Holds revision of this structure, e.g., 0x20 is Rev 2.0.
    NvU32 Revision;

    /// Holds the SDRAM frequency in kHz.
    NvU32 SdramKHz;

    /// Holds minimum core voltage in mV for memory controller operations at
    /// the specified SDRAM frequency. Actual core voltage can be set higher by
    /// DVFS depending on the operation requirements for other SoC modules.
    NvU32 EmcCoreVoltageMv;

    /// Holds the number of advanced memory controller timing parameters.
    NvU32 EmcTimingParamNum;
 
    /// Holds the advanced memory controller timing parameters.
    NvU32 EmcTimingParameters[NV_EMC_ADV_PARAM_NUM_MAX];
} NvOdmSdramControllerConfigAdv;

/**
 * Gets the device memory controller configuration.
 *
 * @note This function is called early from the boot process where
 * global variables are not yet valid. Care must be taken not to
 * use global variables in the implementation of this function.
 *
 * @note The implementation of this function must not make reference to
 * any global or static variables of any kind whatsoever.
 *
 * @see NvOdmAsynchMemConfig
 *
 * @param ChipSelect The chip select for which configuration
 * is required:
 * - 0 means chip select A
 * - 1 means chip select B
 * - 2 means chip select C
 * - and so on.
 *
 * @param pMemConfig A pointer to the returned NOR memory configuration.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 *
 */
NvBool NvOdmQueryAsynchMemConfig(NvU32 ChipSelect, NvOdmAsynchMemConfig *pMemConfig);


/**
 * Gets the configuration table that provides SDRAM controller parameters for
 * the selected set of SDRAM frequencies and controller core voltages. This
 * table is used by the memory controller DVFS.
 * 
 * @sa NvOdmSdramControllerConfig structure description for the format of each
 * table entry, revision 1.0.
 * @sa NvOdmSdramControllerConfigAdv structure description for the format of
 * each table entry, revision 2.0.
 * 
 * @note The maximum scaled SDRAM frequency Fmax is limited by boot configuration
 * of memory:
 * <pre>
 *    PLL - PLLM: Fmax = (PLLM boot output frequency)/2 
 * </pre>
 * The minimum scaled SDRAM frequency is fixed as
 * <pre>
 *    Fmin = 12MHz
 * </pre>
 *
 * @par SDRAM Frequency Ladders
 * 
 * Revision 1.0 - Only entries for Fmax and evenly
 * divided from Fmax SDRAM frequencies above Fmin are used by DVFS (e.g. Fmax,
 * Fmax/2, Fmax/4, Fmax/6, etc). All other entries are ignored. Hence, one
 * table can contain entries for all different PLLM configurations used for the
 * particular ODM platform, and DVFS will automatically select the frequency ladder
 * based on the boot settings. For example, the table can mix entries for Fmax
 * = 166MHz ladder (166/83/41.5/27.6) and Fmax = 133MHz ladder (133/66.5/33.25/
 * 21.16). The table is not required to be sorted in any way.
 * 
 * Revision 2.0 - Only entries for Fmax and .... 
 * ladders 
 * 
 * The memory controller DVFS is enabled, provided all of the following
 * conditions are true:
 * - This function returns a non-NULL pointer to the table.
 * - The table includes an entry for Fmax SDRAM frequency.
 * - The table includes an entry for boot SDRAM frequency (if boot configuration
 * utilizes EMC divider to set initial SDRAM frequency different from Fmax).
 * This condition is applicable only to Revision 1.0 configuration.
 * If any of the above conditions are not met, memory controller DVFS will be
 * disabled and boot SDRAM configuration is preserved during run time.
 * 
 * @param pEntries A pointer to a variable which this function sets to the
 * number of entires in the configuration table.
 * @param pRevision A pointer to a variable which this function sets to the
 * revision number of the configuration table entry structure.
 *
 * @return A const pointer to the configuration table, or NULL if EMC DVFS
 *   is disabled.
 */
const void*
NvOdmQuerySdramControllerConfigGet(NvU32 *pEntries, NvU32 *pRevision); 

#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_QUERY_MEMC_H

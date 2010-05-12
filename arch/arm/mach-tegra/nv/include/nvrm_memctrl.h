/*
 * Copyright (c) 2009 NVIDIA Corporation.
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

#ifndef INCLUDED_nvrm_memctrl_H
#define INCLUDED_nvrm_memctrl_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvcommon.h"
#include "nvassert.h"
#include "nvos.h"

/*
 * @ingroup nvrm_memctrl
 * @{
 */

/**
 * NvRmDeviceHandle is an opaque handle to an RM device.
 */

/**
 * Start collecting statistics for specified clients. (2 normal clients and 1 llc client)
 *
 * @param rm the RM handle is stored here.
 * @param client_id_0 the ID of the first client
 * @param client_id_1 the ID of the second client
 * @param llc_client_id the ID of the llc client
 *
 */

 void McStat_Start( 
    NvRmDeviceHandle rm,
    NvU32 client_id_0,
    NvU32 client_id_1,
    NvU32 llc_client_id );

/**
 * Stop the counter for collecting statistics for specified clinets
 * @param rm the RM handle is stored here
 * @param client_0_cycles pointer to the number of cycles of client_0
 * @param client_1_cycles pointer to the number of cycles of client_1
 * @param llc_client_cycles pointer to the number of cycles of llc client
 * @param llc_client_clocks pointer to the llc client's clock
 * @param mc_clocks pointer to the memory controller's clock
 */

 void McStat_Stop( 
    NvRmDeviceHandle rm,
    NvU32 * client_0_cycles,
    NvU32 * client_1_cycles,
    NvU32 * llc_client_cycles,
    NvU32 * llc_client_clocks,
    NvU32 * mc_clocks );

/**
 * Print out the collected memory control stat data
 * @param client_id_0 the first client's ID
 * @param client_0_cycles the number of cycles of client_0 from start to stop
 * @param client_id_1 the second client's ID
 * @param client_1_cycles the number of cycles of client_1 from start to stop
 * @param llc_client_id the ID of llc client
 * @param llc_client_clocks the clocks of llc client
 * @param llc_client_cycles the number of cycles of llc client
 * @param mc_clocks the memory controller's clock
 */

 void McStat_Report( 
    NvU32 client_id_0,
    NvU32 client_0_cycles,
    NvU32 client_id_1,
    NvU32 client_1_cycles,
    NvU32 llc_client_id,
    NvU32 llc_client_clocks,
    NvU32 llc_client_cycles,
    NvU32 mc_clocks );

/**
 * Read the data of specified module and bit field 
 * @param modId the specified module ID
 * @param start_index the start index of the required data
 * @param length the length of the data
 * @param value pointer to the variable that will store the data specified
 *
 * @retval NvSuccess Indicate the the data is read successfully
 */

 NvError ReadObsData( 
    NvRmDeviceHandle rm,
    NvRmModuleID modId,
    NvU32 start_index,
    NvU32 length,
    NvU32 * value );

/**
 * Starts CPU performance monitors for the specified list of events
 * (if monitors were already running they are restarted).
 * 
 * @param hRmDevice The RM device handle.
 * @param pEventListSize Pointer to the event list size. On entry specifies
 *  list size allocated by the client, on exit - actual number of event monitors
 *  started. If entry size is 0, maximum number of monitored events is returned.
 * @param pEventList Pointer to the list of events to be monitored. Ignored
 *  if input list size is 0. Monitors run status is not affected in this case.
 * 
 * @note No event validation is performed. It is caller responsibility to pass
 *  valid event codes. See ARM control coprocessor CP15 specification for the
 *  list of event numbers and the respective event definitions.
 * 
 * @retval NvSuccess if monitoring start function completed successfully.
 * @retval NvError_NotSupported if core performance monitoring is not supported.
 */                               

 NvError NvRmCorePerfMonStart( 
    NvRmDeviceHandle hRmDevice,
    NvU32 * pEventListSize,
    NvU32 * pEventList );

/**
 * Stops CPU performance monitors and returns event counts.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCountListSize Pointer to the count list size. On entry specifies
 *  list size allocated by the client, on exit - actual number of event counts
 *  returned.
 * @param pCountList Pointer to the list filled in by this function with event
 *  counts since performance monitoring started. The order of returned counts
 *  is the same as the order of events specified by NvRmCorePerfMonStart()
 *  call. If input list size exceeds number of started event monitors the extra
 *  counts are meaningless. If input list size is 0, this parameter is ignored,
 *  and no event counts are returned.
 * @param pTotalCycleCount Pointer to the total number of CPU clock cycles
 *  since performance monitoring started.
 * 
 * @retval NvSuccess if monitoring results are retrieved successfully.
 * @retval NvError_InvalidState if core performance monitoring has not been
 *  started or monitor overflow has occurred.
 * @retval NvError_NotSupported if core performance monitoring is not supported.
 */                               

 NvError NvRmCorePerfMonStop( 
    NvRmDeviceHandle hRmDevice,
    NvU32 * pCountListSize,
    NvU32 * pCountList,
    NvU32 * pTotalCycleCount );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif

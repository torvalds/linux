/*
* \file       ocsd_c_api_cust_impl.h
* \brief      OpenCSD : Custom decoder implementation common API definitions
*
* \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
*/

/*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following disclaimer in the documentation
* and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
* may be used to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef ARM_OCSD_C_API_CUST_IMPL_H_INCLUDED
#define ARM_OCSD_C_API_CUST_IMPL_H_INCLUDED

#include "opencsd/c_api/ocsd_c_api_types.h"
#include "opencsd/c_api/ocsd_c_api_custom.h"

/** @addtogroup ocsd_ext_dcd
@{*/

/**@name External decoder - Inline utility functions.
   @brief inline functions used in decoders to call the various library callback functionality.

   Functions manipulate and use the ocsd_extern_dcd_cb_fns structure to call into the library, 
   with appropriate checking for initialisation and usage flags.

@{*/

static inline ocsd_datapath_resp_t lib_cb_GenElemOp(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_trc_index_t index_sop,
    const uint8_t trc_chan_id,
    const ocsd_generic_trace_elem *elem)
{
    if (callbacks->fn_gen_elem_out)
        return callbacks->fn_gen_elem_out(callbacks->lib_context, index_sop, trc_chan_id, elem);
    return OCSD_RESP_FATAL_NOT_INIT;
}

static inline ocsd_err_t lib_cb_LogError(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_err_severity_t filter_level,
    const ocsd_err_t code,
    const ocsd_trc_index_t idx,
    const uint8_t chan_id,
    const char *pMsg)
{
    if (callbacks->fn_log_error)
    {
        callbacks->fn_log_error(callbacks->lib_context, filter_level, code, idx, chan_id, pMsg);
        return OCSD_OK;
    }
    return OCSD_ERR_NOT_INIT;
}

static inline ocsd_err_t lib_cb_LogMsg(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_err_severity_t filter_level,
    const char *pMsg)
{
    if (callbacks->fn_log_msg)
    {
        callbacks->fn_log_msg(callbacks->lib_context, filter_level, pMsg);
        return OCSD_OK;
    }
    return OCSD_ERR_NOT_INIT;
}

static inline ocsd_err_t lib_cb_DecodeArmInst(const ocsd_extern_dcd_cb_fns *callbacks,
    ocsd_instr_info *instr_info)
{
    if (callbacks->fn_arm_instruction_decode)
        return callbacks->fn_arm_instruction_decode(callbacks->lib_context, instr_info);
    return OCSD_ERR_NOT_INIT;
}

static inline ocsd_err_t lib_cb_MemAccess(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_vaddr_t address,
    const uint8_t cs_trace_id,
    const ocsd_mem_space_acc_t mem_space,
    uint32_t *num_bytes,
    uint8_t *p_buffer)
{
    if (callbacks->fn_memory_access)
        return callbacks->fn_memory_access(callbacks->lib_context, address, cs_trace_id, mem_space, num_bytes, p_buffer);
    return OCSD_ERR_NOT_INIT;
}

static inline void lib_cb_PktMon(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_datapath_op_t op,
    const ocsd_trc_index_t index_sop,
    const void *pkt,
    const uint32_t size,
    const uint8_t *p_data)
{
    if (callbacks->packetCBFlags & OCSD_CUST_DCD_PKT_CB_USE_MON)
    {
        if (callbacks->fn_packet_mon)
            callbacks->fn_packet_mon(callbacks->lib_context, op, index_sop, pkt, size, p_data);
    }
}

static inline int lib_cb_usePktMon(const ocsd_extern_dcd_cb_fns *callbacks)
{
    return (callbacks->packetCBFlags & OCSD_CUST_DCD_PKT_CB_USE_MON);
}

/* callback function to connect to the packet sink interface, on the main decode
data path - used if decoder created as packet processor only */
static inline ocsd_datapath_resp_t lib_cb_PktDataSink(const ocsd_extern_dcd_cb_fns *callbacks,
    const ocsd_datapath_op_t op,
    const ocsd_trc_index_t index_sop,
    const void *pkt)
{
    if (callbacks->packetCBFlags & OCSD_CUST_DCD_PKT_CB_USE_SINK)
    {
        if (callbacks->fn_packet_data_sink)
            return callbacks->fn_packet_data_sink(callbacks->lib_context, op, index_sop, pkt);
        else
            return OCSD_RESP_FATAL_NOT_INIT;
    }
    return OCSD_RESP_CONT;
}

static inline int lib_cb_usePktSink(const ocsd_extern_dcd_cb_fns *callbacks)
{
    return (callbacks->packetCBFlags & OCSD_CUST_DCD_PKT_CB_USE_SINK);
}

static inline void lib_cb_updatePktCBFlags(ocsd_extern_dcd_cb_fns *callbacks, const int newFlags)
{
    callbacks->packetCBFlags = newFlags;
}

/** @}*/

/** @}*/

#endif /* ARM_OCSD_C_API_CUST_IMPL_H_INCLUDED */

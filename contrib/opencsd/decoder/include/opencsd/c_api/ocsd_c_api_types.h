/*!
 * \file       ocsd_c_api_types.h
 * \brief      OpenCSD : Trace Decoder "C" API types.
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
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

#ifndef ARM_OCSD_C_API_TYPES_H_INCLUDED
#define ARM_OCSD_C_API_TYPES_H_INCLUDED

/* select the library types that are C compatible - the interface data types */
#include "opencsd/ocsd_if_types.h"
#include "opencsd/trc_gen_elem_types.h"
#include "opencsd/trc_pkt_types.h"

/* pull in the protocol decoder types. */
#include "opencsd/etmv3/trc_pkt_types_etmv3.h"
#include "opencsd/etmv4/trc_pkt_types_etmv4.h"
#include "opencsd/ptm/trc_pkt_types_ptm.h"
#include "opencsd/stm/trc_pkt_types_stm.h"

/** @ingroup lib_c_api
@{*/


/* Specific C-API only types */

/** Handle to decode tree */
typedef void * dcd_tree_handle_t;

/** define invalid handle value for decode tree handle */
#define C_API_INVALID_TREE_HANDLE (dcd_tree_handle_t)0

/** Logger output printer - no output. */
#define C_API_MSGLOGOUT_FLG_NONE   0x0
/** Logger output printer - output to file. */
#define C_API_MSGLOGOUT_FLG_FILE   0x1
/** Logger output printer - output to stderr. */
#define C_API_MSGLOGOUT_FLG_STDERR 0x2
/** Logger output printer - output to stdout. */
#define C_API_MSGLOGOUT_FLG_STDOUT 0x4
/** Logger output printer - mask of valid flags. */
#define C_API_MSGLOGOUT_MASK       0x7

/** function pointer type for decoder outputs. all protocols, generic data element input */
typedef ocsd_datapath_resp_t (* FnTraceElemIn)( const void *p_context, 
                                                const ocsd_trc_index_t index_sop, 
                                                const uint8_t trc_chan_id, 
                                                const ocsd_generic_trace_elem *elem); 

/** function pointer type for packet processor packet output sink, packet analyser/decoder input - generic declaration */
typedef ocsd_datapath_resp_t (* FnDefPktDataIn)(const void *p_context, 
                                                const ocsd_datapath_op_t op, 
                                                const ocsd_trc_index_t index_sop, 
                                                const void *p_packet_in);

/** function pointer type for packet processor packet monitor sink, raw packet monitor / display input - generic declaration */
typedef void (* FnDefPktDataMon)(const void *p_context,
                                                 const ocsd_datapath_op_t op,
                                                 const ocsd_trc_index_t index_sop,
                                                 const void *p_packet_in,
                                                 const uint32_t size,
                                                 const uint8_t *p_data);

/** function pointer tyee for library default logger output to allow client to print zero terminated output string */
typedef void (* FnDefLoggerPrintStrCB)(const void *p_context, const char *psz_msg_str, const int str_len);

/** Callback interface type when attaching monitor/sink to packet processor */
typedef enum _ocsd_c_api_cb_types {
    OCSD_C_API_CB_PKT_SINK, /** Attach to the packet processor primary packet output (CB fn is FnDefPktDataIn) */
    OCSD_C_API_CB_PKT_MON,  /** Attach to the packet processor packet monitor output (CB fn is FnDefPktDataMon) */
} ocsd_c_api_cb_types;

/** @}*/

#endif // ARM_OCSD_C_API_TYPES_H_INCLUDED

/* End of File ocsd_c_api_types.h */

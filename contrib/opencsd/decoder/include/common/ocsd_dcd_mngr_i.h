/*
 * \file       ocsd_dcd_mngr_i.h
 * \brief      OpenCSD : Decoder manager interface.
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

#ifndef ARM_OCSD_DCD_MNGR_I_H_INCLUDED
#define ARM_OCSD_DCD_MNGR_I_H_INCLUDED

#include "opencsd/ocsd_if_types.h"
#include "common/trc_cs_config.h"
#include "common/trc_component.h"

#include "interfaces/trc_error_log_i.h"
#include "interfaces/trc_data_raw_in_i.h"
#include "interfaces/trc_instr_decode_i.h"
#include "interfaces/trc_tgt_mem_access_i.h"
#include "interfaces/trc_gen_elem_in_i.h"
#include "interfaces/trc_abs_typed_base_i.h"

class IDecoderMngr
{
public:
    IDecoderMngr() {};
    virtual ~IDecoderMngr() {};

// create and destroy decoders
    virtual ocsd_err_t createDecoder(const int create_flags, const int instID, const CSConfig *p_config,  TraceComponent **ppComponent) = 0;
    virtual ocsd_err_t destroyDecoder(TraceComponent *pComponent) = 0;

    //! Get the built in protocol type ID managed by this instance - extern for custom decoders
    virtual const ocsd_trace_protocol_t getProtocolType() const = 0;

// connect decoders to other components - (replace current /  0 pointer value to detach );
// compatible with all decoders
    //!attach error logger to ptk-processor, or both of pkt processor and pkt decoder pair
    virtual ocsd_err_t attachErrorLogger(TraceComponent *pComponent, ITraceErrorLog *pIErrorLog) = 0;

// pkt decoder only 
    //! attach instruction decoder to pkt decoder
    virtual ocsd_err_t attachInstrDecoder(TraceComponent *pComponent, IInstrDecode *pIInstrDec) = 0;

    //! attach memory accessor to pkt decoder
    virtual ocsd_err_t attachMemAccessor(TraceComponent *pComponent, ITargetMemAccess *pMemAccessor) = 0;

    //! attach generic output interface to pkt decoder
    virtual ocsd_err_t attachOutputSink(TraceComponent *pComponent, ITrcGenElemIn *pOutSink) = 0;

// pkt processor only
    //! attach a raw packet monitor to pkt processor (solo pkt processor, or pkt processor part of pair)
    virtual ocsd_err_t attachPktMonitor(TraceComponent *pComponent, ITrcTypedBase *pPktRawDataMon) = 0;

    //! attach a packet indexer to pkt processor (solo pkt processor, or pkt processor part of pair)
    virtual ocsd_err_t attachPktIndexer(TraceComponent *pComponent, ITrcTypedBase *pPktIndexer) = 0;

    //! attach a packet data sink to pkt processor output (solo pkt processor only - instead of decoder when pkt processor only created.)
    virtual ocsd_err_t attachPktSink(TraceComponent *pComponent, ITrcTypedBase *pPktDataInSink) = 0;

// data input connection interface
    //! get raw data input interface from packet processor
    virtual ocsd_err_t getDataInputI(TraceComponent *pComponent, ITrcDataIn **ppDataIn) = 0;

// create configuration from data structure
    virtual ocsd_err_t createConfigFromDataStruct(CSConfig **pConfigBase, const void *pDataStruct) = 0;

};

#endif // ARM_OCSD_DCD_MNGR_I_H_INCLUDED

/* End of File ocsd_dcd_mngr.h */
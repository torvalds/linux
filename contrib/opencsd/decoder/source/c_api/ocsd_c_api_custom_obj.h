/*
 * \file       ocsd_c_api_custom_obj.h
 * \brief      OpenCSD : 
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


#ifndef ARM_OCSD_C_API_CUSTOM_OBJ_H_INCLUDED
#define ARM_OCSD_C_API_CUSTOM_OBJ_H_INCLUDED

#include "opencsd/c_api/ocsd_c_api_custom.h"
#include "common/ocsd_dcd_mngr_i.h"

/***** Decoder manager interface ******************************/
class CustomDcdMngrWrapper : public IDecoderMngr
{
public:
    CustomDcdMngrWrapper();
    virtual ~CustomDcdMngrWrapper() {};

    // set the C-API decoder factory interface.
    void setAPIDcdFact(ocsd_extern_dcd_fact_t *p_dcd_fact);

// create and destroy decoders
    virtual ocsd_err_t createDecoder(const int create_flags, const int instID, const CSConfig *p_config,  TraceComponent **ppComponent);
    virtual ocsd_err_t destroyDecoder(TraceComponent *pComponent);

    //! Get the built in protocol type ID managed by this instance - extern for custom decoders
    virtual const ocsd_trace_protocol_t getProtocolType() const;

// connect decoders to other components - (replace current /  0 pointer value to detach );
// compatible with all decoders
    //!attach error logger to ptk-processor, or both of pkt processor and pkt decoder pair
    virtual ocsd_err_t attachErrorLogger(TraceComponent *pComponent, ITraceErrorLog *pIErrorLog);

// pkt decoder only 
    //! attach instruction decoder to pkt decoder
    virtual ocsd_err_t attachInstrDecoder(TraceComponent *pComponent, IInstrDecode *pIInstrDec);

    //! attach memory accessor to pkt decoder
    virtual ocsd_err_t attachMemAccessor(TraceComponent *pComponent, ITargetMemAccess *pMemAccessor);

    //! attach generic output interface to pkt decoder
    virtual ocsd_err_t attachOutputSink(TraceComponent *pComponent, ITrcGenElemIn *pOutSink);

// pkt processor only
    //! attach a raw packet monitor to pkt processor (solo pkt processor, or pkt processor part of pair)
    virtual ocsd_err_t attachPktMonitor(TraceComponent *pComponent, ITrcTypedBase *pPktRawDataMon);

    //! attach a packet indexer to pkt processor (solo pkt processor, or pkt processor part of pair)
    virtual ocsd_err_t attachPktIndexer(TraceComponent *pComponent, ITrcTypedBase *pPktIndexer);

    //! attach a packet data sink to pkt processor output (solo pkt processor only - instead of decoder when pkt processor only created.)
    virtual ocsd_err_t attachPktSink(TraceComponent *pComponent, ITrcTypedBase *pPktDataInSink);

// data input connection interface
    //! get raw data input interface from packet processor
    virtual ocsd_err_t getDataInputI(TraceComponent *pComponent, ITrcDataIn **ppDataIn);

// create configuration from data structure
    virtual ocsd_err_t createConfigFromDataStruct(CSConfig **pConfigBase, const void *pDataStruct);

// custom packet to string interface.
    void pktToString(const void *pkt, char *pStrBuffer, int bufSize);

private:

    ocsd_extern_dcd_fact_t m_dcd_fact;
};

/**** Decoder instance wrapper */
class CustomDecoderWrapper : public TraceComponent, public ITrcDataIn
{
public:
    CustomDecoderWrapper();
    virtual ~CustomDecoderWrapper();
    ocsd_extern_dcd_inst_t *getDecoderInstInfo() { return &m_decoder_inst; }

    virtual ocsd_datapath_resp_t TraceDataIn( const ocsd_datapath_op_t op,
                                              const ocsd_trc_index_t index,
                                              const uint32_t dataBlockSize,
                                              const uint8_t *pDataBlock,
                                              uint32_t *numBytesProcessed);

    void attachGenElemI(ITrcGenElemIn *pIF) { m_pGenElemIn = pIF; };
    void attachInstrDecI(IInstrDecode *pIF) { m_pIInstrDec = pIF; };
    void attachMemAccI(ITargetMemAccess *pIF) { m_pMemAccessor = pIF; };

    void attachPtkMonI(IPktRawDataMon<void> *pIF);
    void attachPtkSinkI(IPktDataIn<void> *pIF);

    void updateNameFromDcdInst();

    static void SetCallbacks(ocsd_extern_dcd_cb_fns &callbacks);

private:
    // declare the callback functions as friend functions.
    friend ocsd_datapath_resp_t GenElemOpCB( const void *lib_context,
                                                const ocsd_trc_index_t index_sop, 
                                                const uint8_t trc_chan_id, 
                                                const ocsd_generic_trace_elem *elem);

    friend void LogErrorCB(   const void *lib_context, 
                                const ocsd_err_severity_t filter_level, 
                                const ocsd_err_t code, 
                                const ocsd_trc_index_t idx, 
                                const uint8_t chan_id, 
                                const char *pMsg);

    friend void LogMsgCB(const void *lib_context,
        const ocsd_err_severity_t filter_level,
        const char *msg);
        
    friend ocsd_err_t DecodeArmInstCB(const void *lib_context, ocsd_instr_info *instr_info);

    friend ocsd_err_t MemAccessCB(const void *lib_context,
        const ocsd_vaddr_t address,
        const uint8_t cs_trace_id,
        const ocsd_mem_space_acc_t mem_space,
        uint32_t *num_bytes,
        uint8_t *p_buffer);

    friend void PktMonCB(const void *lib_context,
        const ocsd_datapath_op_t op,
        const ocsd_trc_index_t index_sop,
        const void *pkt,
        const uint32_t size,
        const uint8_t *p_data);

    friend ocsd_datapath_resp_t PktDataSinkCB(const void *lib_context,
        const ocsd_datapath_op_t op,
        const ocsd_trc_index_t index_sop,
        const void *pkt);

private:
    ITrcGenElemIn *m_pGenElemIn;        //!< generic element sink interface - output from decoder fed to common sink.
    IInstrDecode *m_pIInstrDec;         //!< arm instruction decode interface - decoder may want to use this.
    ITargetMemAccess *m_pMemAccessor;   //!< system memory accessor insterface - decoder may want to use this.
    IPktRawDataMon<void> *m_pPktMon;    //!< interface to packet monitor (full or packet only decode).
    IPktDataIn<void> *m_pPktIn;          //!< interface to packet sink (decode packets only).

    ocsd_extern_dcd_inst_t m_decoder_inst;    
};

/**** Decoder configuration wrapper - implements CSConfig base class interface ***/
class CustomConfigWrapper : public CSConfig
{
public:
    CustomConfigWrapper(const void *p_config) : m_p_config(p_config), m_CSID(0) {};   
    virtual ~CustomConfigWrapper() {};
    virtual const uint8_t getTraceID() const { return m_CSID; };
    void setCSID(const uint8_t CSID) { m_CSID = CSID; };
    const void *getConfig() { return m_p_config; };
private:
    const void *m_p_config;
    uint8_t m_CSID;
};

#endif // ARM_OCSD_C_API_CUSTOM_OBJ_H_INCLUDED

/* End of File ocsd_c_api_custom_obj.h */

/*
 * \file       ocsd_c_api_custom_obj.cpp
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

/* pull in the C++ decode library */
#include "opencsd.h"

#include "opencsd/c_api/opencsd_c_api.h"
#include "ocsd_c_api_custom_obj.h"
#include "common/ocsd_lib_dcd_register.h"

/***************** C-API functions ********************************/

/** register a custom decoder with the library */
OCSD_C_API ocsd_err_t ocsd_register_custom_decoder(const char *name, ocsd_extern_dcd_fact_t  *p_dcd_fact)
{
    ocsd_err_t err = OCSD_OK;
    OcsdLibDcdRegister *pRegister = OcsdLibDcdRegister::getDecoderRegister();
    
    // check not already registered
    if(pRegister->isRegisteredDecoder(name))
        return OCSD_ERR_DCDREG_NAME_REPEAT;

    // validate the factory interface structure
    if((p_dcd_fact->createDecoder == 0) || 
       (p_dcd_fact->destroyDecoder == 0) ||
       (p_dcd_fact->csidFromConfig == 0)
       )
        return OCSD_ERR_INVALID_PARAM_VAL;
    
    // create a wrapper.
    CustomDcdMngrWrapper *pWrapper = new (std::nothrow) CustomDcdMngrWrapper();
    if(pRegister == 0)
        return OCSD_ERR_MEM;

    p_dcd_fact->protocol_id = OcsdLibDcdRegister::getNextCustomProtocolID();
    if(p_dcd_fact->protocol_id < OCSD_PROTOCOL_END)
    {
        // fill out the wrapper and register it
        pWrapper->setAPIDcdFact(p_dcd_fact);
        err = pRegister->registerDecoderTypeByName(name,pWrapper);
        if(err != OCSD_OK)
            OcsdLibDcdRegister::releaseLastCustomProtocolID();
    }
    else
        err =  OCSD_ERR_DCDREG_TOOMANY; // too many decoders
    
    if(err != OCSD_OK)
        delete pWrapper;

    return err;
}

OCSD_C_API ocsd_err_t ocsd_deregister_decoders()
{   
    // destroys all builtin and custom decoders & library registration object.
    OcsdLibDcdRegister::deregisterAllDecoders();    
    return OCSD_OK;
}

OCSD_C_API ocsd_err_t ocsd_cust_protocol_to_str(const ocsd_trace_protocol_t pkt_protocol, const void *trc_pkt, char *buffer, const int buflen)
{
    OcsdLibDcdRegister *pRegister = OcsdLibDcdRegister::getDecoderRegister();
    IDecoderMngr *p_mngr = 0;
    if (OCSD_PROTOCOL_IS_CUSTOM(pkt_protocol) && (pRegister->getDecoderMngrByType(pkt_protocol, &p_mngr) == OCSD_OK))
    {
        CustomDcdMngrWrapper *pWrapper = static_cast<CustomDcdMngrWrapper *>(p_mngr);
        pWrapper->pktToString(trc_pkt, buffer, buflen);
        return OCSD_OK;
    }
    return OCSD_ERR_NO_PROTOCOL;
}

/***************** Decode Manager Wrapper *****************************/

CustomDcdMngrWrapper::CustomDcdMngrWrapper()
{
    m_dcd_fact.protocol_id = OCSD_PROTOCOL_END;    
}


    // set the C-API decoder factory interface
void CustomDcdMngrWrapper::setAPIDcdFact(ocsd_extern_dcd_fact_t *p_dcd_fact)
{
    m_dcd_fact = *p_dcd_fact;
}

// create and destroy decoders
ocsd_err_t CustomDcdMngrWrapper::createDecoder(const int create_flags, const int instID, const CSConfig *p_config,  TraceComponent **ppComponent)
{
    ocsd_err_t err = OCSD_OK;
    if(m_dcd_fact.protocol_id == OCSD_PROTOCOL_END)
        return OCSD_ERR_NOT_INIT;

    CustomDecoderWrapper *pComp = new (std::nothrow) CustomDecoderWrapper();
    *ppComponent = pComp;
    if (pComp == 0)
        return OCSD_ERR_MEM;

    ocsd_extern_dcd_cb_fns lib_callbacks;
    CustomDecoderWrapper::SetCallbacks(lib_callbacks);
    lib_callbacks.lib_context = pComp;
    lib_callbacks.packetCBFlags = 0;
    
    ocsd_extern_dcd_inst_t *pDecodeInst = pComp->getDecoderInstInfo();

    err = m_dcd_fact.createDecoder( create_flags,
        ((CustomConfigWrapper *)p_config)->getConfig(),
        &lib_callbacks,
        pDecodeInst);
    
    if (err == OCSD_OK)
    {
        // validate the decoder
        if ((pDecodeInst->fn_data_in == 0) ||
            (pDecodeInst->fn_update_pkt_mon == 0) ||
            (pDecodeInst->cs_id == 0) ||
            (pDecodeInst->decoder_handle == 0) ||
            (pDecodeInst->p_decoder_name == 0)
            )
        {
            err = OCSD_ERR_INVALID_PARAM_VAL;
        }
    }

    if (err != OCSD_OK)
        delete pComp;
    else
        pComp->updateNameFromDcdInst();
    return err;
}

ocsd_err_t CustomDcdMngrWrapper::destroyDecoder(TraceComponent *pComponent)
{
    CustomDecoderWrapper *pCustWrap = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(m_dcd_fact.protocol_id != OCSD_PROTOCOL_END)
        m_dcd_fact.destroyDecoder(pCustWrap->getDecoderInstInfo()->decoder_handle);
    delete pCustWrap;
    return OCSD_OK;
}

const ocsd_trace_protocol_t CustomDcdMngrWrapper::getProtocolType() const
{
    return m_dcd_fact.protocol_id;
}

ocsd_err_t CustomDcdMngrWrapper::createConfigFromDataStruct(CSConfig **pConfigBase, const void *pDataStruct)
{
    ocsd_err_t err = OCSD_OK;
    CustomConfigWrapper *pConfig = new (std::nothrow) CustomConfigWrapper(pDataStruct);
    if(!pConfig)
        return OCSD_ERR_MEM;

    if(m_dcd_fact.csidFromConfig == 0)
        return OCSD_ERR_NOT_INIT;

    unsigned char csid;
    err = m_dcd_fact.csidFromConfig(pDataStruct,&csid);
    if(err == OCSD_OK)
    {
        pConfig->setCSID(csid);        
        *pConfigBase = pConfig;
    }
    return err;
}

ocsd_err_t CustomDcdMngrWrapper::getDataInputI(TraceComponent *pComponent, ITrcDataIn **ppDataIn)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    *ppDataIn = pDecoder;
    return OCSD_OK;
}

// component connections
// all
ocsd_err_t CustomDcdMngrWrapper::attachErrorLogger(TraceComponent *pComponent, ITraceErrorLog *pIErrorLog)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if (pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    pDecoder->getErrorLogAttachPt()->replace_first(pIErrorLog);
    return OCSD_OK;
}

// full decoder
ocsd_err_t CustomDcdMngrWrapper::attachInstrDecoder(TraceComponent *pComponent, IInstrDecode *pIInstrDec)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    pDecoder->attachInstrDecI(pIInstrDec);
    return OCSD_OK; 
}

ocsd_err_t CustomDcdMngrWrapper::attachMemAccessor(TraceComponent *pComponent, ITargetMemAccess *pMemAccessor)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    pDecoder->attachMemAccI(pMemAccessor);
    return OCSD_OK;
}

ocsd_err_t CustomDcdMngrWrapper::attachOutputSink(TraceComponent *pComponent, ITrcGenElemIn *pOutSink)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    pDecoder->attachGenElemI(pOutSink);
    return OCSD_OK;
}

// pkt processor only
ocsd_err_t CustomDcdMngrWrapper::attachPktMonitor(TraceComponent *pComponent, ITrcTypedBase *pPktRawDataMon)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    IPktRawDataMon<void> *pIF = 0;
    if (pPktRawDataMon)
    {
        pIF = dynamic_cast<IPktRawDataMon<void> *>(pPktRawDataMon);
        if (!pIF)
            return OCSD_ERR_INVALID_PARAM_TYPE;
    }
    pDecoder->attachPtkMonI(pIF);   
    return OCSD_OK;
}

ocsd_err_t CustomDcdMngrWrapper::attachPktIndexer(TraceComponent *pComponent, ITrcTypedBase *pPktIndexer)
{
    // indexers for external custom will also be external and custom.
    return OCSD_ERR_DCD_INTERFACE_UNUSED;
}

ocsd_err_t CustomDcdMngrWrapper::attachPktSink(TraceComponent *pComponent, ITrcTypedBase *pPktDataInSink)
{
    CustomDecoderWrapper *pDecoder = dynamic_cast<CustomDecoderWrapper *>(pComponent);
    if(pDecoder == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;
    IPktDataIn<void> *pIF = 0;
    if (pPktDataInSink)
    {
        pIF = dynamic_cast<IPktDataIn<void> *>(pPktDataInSink);
        if(!pIF)
            return OCSD_ERR_INVALID_PARAM_TYPE;
    }
    pDecoder->attachPtkSinkI(pIF);
    return OCSD_OK;
}

void CustomDcdMngrWrapper::pktToString(const void *pkt, char *pStrBuffer, int bufSize)
{
    if (m_dcd_fact.pktToString)
        m_dcd_fact.pktToString(pkt, pStrBuffer, bufSize);
    else
        snprintf(pStrBuffer, bufSize, "CUSTOM_PKT[]: print unsupported; protocol(%d).",m_dcd_fact.protocol_id);
}

/************************** Decoder instance wrapper **************************************/

/* callback functions */
ocsd_datapath_resp_t GenElemOpCB( const void *lib_context,
                                                const ocsd_trc_index_t index_sop, 
                                                const uint8_t trc_chan_id, 
                                                const ocsd_generic_trace_elem *elem)
{
    if (lib_context && ((CustomDecoderWrapper *)lib_context)->m_pGenElemIn)
        return ((CustomDecoderWrapper *)lib_context)->m_pGenElemIn->TraceElemIn(index_sop,trc_chan_id,*(OcsdTraceElement *)elem);
    return OCSD_RESP_FATAL_NOT_INIT;
}

void LogErrorCB(const void *lib_context, const ocsd_err_severity_t filter_level, const ocsd_err_t code, const ocsd_trc_index_t idx, const uint8_t chan_id, const char *pMsg)
{
    if (lib_context)
    {
        if(pMsg)
            ((CustomDecoderWrapper *)lib_context)->LogError(ocsdError(filter_level, code, idx, chan_id, std::string(pMsg)));
        else
            ((CustomDecoderWrapper *)lib_context)->LogError(ocsdError(filter_level, code, idx, chan_id));
    }
}

void LogMsgCB(const void *lib_context, const ocsd_err_severity_t filter_level, const char *msg)
{
    if (lib_context && msg)
        ((CustomDecoderWrapper *)lib_context)->LogMessage(filter_level, std::string(msg));
}

ocsd_err_t DecodeArmInstCB(const void *lib_context, ocsd_instr_info *instr_info)
{
    if (lib_context && ((CustomDecoderWrapper *)lib_context)->m_pIInstrDec)
        return ((CustomDecoderWrapper *)lib_context)->m_pIInstrDec->DecodeInstruction(instr_info);
    return OCSD_ERR_ATTACH_INVALID_PARAM;
}

ocsd_err_t MemAccessCB(const void *lib_context,
    const ocsd_vaddr_t address,
    const uint8_t cs_trace_id,
    const ocsd_mem_space_acc_t mem_space,
    uint32_t *num_bytes,
    uint8_t *p_buffer)
{
    if (lib_context && ((CustomDecoderWrapper *)lib_context)->m_pMemAccessor)
        return ((CustomDecoderWrapper *)lib_context)->m_pMemAccessor->ReadTargetMemory(address, cs_trace_id, mem_space, num_bytes, p_buffer);
    return OCSD_ERR_INVALID_PARAM_VAL;
}

void PktMonCB(const void *lib_context,
    const ocsd_datapath_op_t op,
    const ocsd_trc_index_t index_sop,
    const void *pkt,
    const uint32_t size,
    const uint8_t *p_data)
{
    if (lib_context && ((CustomDecoderWrapper *)lib_context)->m_pPktMon)
        ((CustomDecoderWrapper *)lib_context)->m_pPktMon->RawPacketDataMon(op, index_sop, pkt, size, p_data);
}

ocsd_datapath_resp_t PktDataSinkCB(const void *lib_context,
    const ocsd_datapath_op_t op,
    const ocsd_trc_index_t index_sop,
    const void *pkt)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    if (lib_context && ((CustomDecoderWrapper *)lib_context)->m_pPktIn)
        resp = ((CustomDecoderWrapper *)lib_context)->m_pPktIn->PacketDataIn(op, index_sop, pkt);
    return resp;
}



/** decoder instance object */
CustomDecoderWrapper::CustomDecoderWrapper() : TraceComponent("extern_wrapper"),                                            
    m_pGenElemIn(0),
    m_pIInstrDec(0),
    m_pMemAccessor(0),
    m_pPktMon(0),
    m_pPktIn(0)
{
}

CustomDecoderWrapper::~CustomDecoderWrapper()
{
}

ocsd_datapath_resp_t CustomDecoderWrapper::TraceDataIn( const ocsd_datapath_op_t op,
                                              const ocsd_trc_index_t index,
                                              const uint32_t dataBlockSize,
                                              const uint8_t *pDataBlock,
                                              uint32_t *numBytesProcessed)
{
    if(m_decoder_inst.fn_data_in)
        return m_decoder_inst.fn_data_in( m_decoder_inst.decoder_handle,
                                          op,
                                          index,
                                          dataBlockSize,
                                          pDataBlock,
                                          numBytesProcessed);
    return OCSD_RESP_FATAL_NOT_INIT;
}

void CustomDecoderWrapper::attachPtkMonI(IPktRawDataMon<void>* pIF)
{
    m_pPktMon = pIF;
    int flags = (m_pPktMon ? OCSD_CUST_DCD_PKT_CB_USE_MON : 0) | (m_pPktIn ? OCSD_CUST_DCD_PKT_CB_USE_SINK : 0);
    m_decoder_inst.fn_update_pkt_mon(m_decoder_inst.decoder_handle, flags);
}

void CustomDecoderWrapper::attachPtkSinkI(IPktDataIn<void>* pIF)
{
    m_pPktIn = pIF;
    int flags = (m_pPktMon ? OCSD_CUST_DCD_PKT_CB_USE_MON : 0) | (m_pPktIn ? OCSD_CUST_DCD_PKT_CB_USE_SINK : 0);
    m_decoder_inst.fn_update_pkt_mon(m_decoder_inst.decoder_handle, flags);
}

void CustomDecoderWrapper::updateNameFromDcdInst()
{
    // create a unique component name from the decoder name + cs-id.
    std::string name_combined = m_decoder_inst.p_decoder_name;
    char num_buffer[32];
    sprintf(num_buffer, "_%04d", m_decoder_inst.cs_id);
    name_combined += (std::string)num_buffer;
    setComponentName(name_combined);
}

void CustomDecoderWrapper::SetCallbacks(ocsd_extern_dcd_cb_fns & callbacks)
{
    callbacks.fn_arm_instruction_decode = DecodeArmInstCB;
    callbacks.fn_gen_elem_out = GenElemOpCB;
    callbacks.fn_log_error = LogErrorCB;
    callbacks.fn_log_msg = LogMsgCB;
    callbacks.fn_memory_access = MemAccessCB;
    callbacks.fn_packet_data_sink = PktDataSinkCB;
    callbacks.fn_packet_mon = PktMonCB;
}

/* End of File ocsd_c_api_custom_obj.cpp */

/*
 * \file       ocsd_lib_dcd_register.cpp
 * \brief      OpenCSD : Library decoder register object
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

#include "common/ocsd_lib_dcd_register.h"

// include built-in decode manager headers
#include "opencsd/etmv4/trc_dcd_mngr_etmv4i.h"
#include "opencsd/etmv3/trc_dcd_mngr_etmv3.h"
#include "opencsd/ptm/trc_dcd_mngr_ptm.h"
#include "opencsd/stm/trc_dcd_mngr_stm.h"

// create array of built-in decoders to register with library 
static built_in_decoder_info_t sBuiltInArray[] = {
    CREATE_BUILTIN_ENTRY(DecoderMngrEtmV4I,OCSD_BUILTIN_DCD_ETMV4I),
    CREATE_BUILTIN_ENTRY(DecoderMngrEtmV3, OCSD_BUILTIN_DCD_ETMV3),
    CREATE_BUILTIN_ENTRY(DecoderMngrPtm, OCSD_BUILTIN_DCD_PTM),
    CREATE_BUILTIN_ENTRY(DecoderMngrStm, OCSD_BUILTIN_DCD_STM)
    //{ 0, 0, 0}
};

#define NUM_BUILTINS sizeof(sBuiltInArray) / sizeof(built_in_decoder_info_t)


OcsdLibDcdRegister *OcsdLibDcdRegister::m_p_libMngr = 0;
bool OcsdLibDcdRegister::m_b_registeredBuiltins = false;
ocsd_trace_protocol_t OcsdLibDcdRegister::m_nextCustomProtocolID = OCSD_PROTOCOL_CUSTOM_0;  

OcsdLibDcdRegister *OcsdLibDcdRegister::getDecoderRegister()
{
    if(m_p_libMngr == 0)
        m_p_libMngr = new (std::nothrow) OcsdLibDcdRegister();
    return m_p_libMngr;
}

const ocsd_trace_protocol_t OcsdLibDcdRegister::getNextCustomProtocolID()
{
    ocsd_trace_protocol_t ret = m_nextCustomProtocolID;
    if(m_nextCustomProtocolID < OCSD_PROTOCOL_END)
        m_nextCustomProtocolID = (ocsd_trace_protocol_t)(((int)m_nextCustomProtocolID)+1);
    return ret;
}

void OcsdLibDcdRegister::releaseLastCustomProtocolID()
{
    if(m_nextCustomProtocolID > OCSD_PROTOCOL_CUSTOM_0)
        m_nextCustomProtocolID = (ocsd_trace_protocol_t)(((int)m_nextCustomProtocolID)-1);
}

OcsdLibDcdRegister::OcsdLibDcdRegister()
{
    m_iter = m_decoder_mngrs.begin();
    m_pLastTypedDecoderMngr = 0;
}

OcsdLibDcdRegister::~OcsdLibDcdRegister()
{
    m_decoder_mngrs.clear();
    m_typed_decoder_mngrs.clear();
    m_pLastTypedDecoderMngr = 0;
}


const ocsd_err_t OcsdLibDcdRegister::registerDecoderTypeByName(const std::string &name, IDecoderMngr *p_decoder_fact)
{
    if(isRegisteredDecoder(name))
        return OCSD_ERR_DCDREG_NAME_REPEAT;
    m_decoder_mngrs.emplace(std::pair<const std::string, IDecoderMngr *>(name,p_decoder_fact));
    m_typed_decoder_mngrs.emplace(std::pair<const ocsd_trace_protocol_t, IDecoderMngr *>(p_decoder_fact->getProtocolType(),p_decoder_fact));
    return OCSD_OK;
}

void OcsdLibDcdRegister::registerBuiltInDecoders()
{    
    bool memFail = false;
    for(unsigned i = 0; i < NUM_BUILTINS; i++)
    {
        if(sBuiltInArray[i].PFn)
        {
            sBuiltInArray[i].pMngr = sBuiltInArray[i].PFn( sBuiltInArray[i].name);
            if(!sBuiltInArray[i].pMngr)
                memFail=true;
        }
    }
    m_b_registeredBuiltins = !memFail;
}

void OcsdLibDcdRegister::deregisterAllDecoders()
{
    if(m_b_registeredBuiltins)
    {
        for(unsigned i = 0; i < NUM_BUILTINS; i++)
            delete sBuiltInArray[i].pMngr;
        m_b_registeredBuiltins = false;
    }

    if(m_p_libMngr)
    {
        m_p_libMngr->deRegisterCustomDecoders();
        delete m_p_libMngr;
        m_p_libMngr = 0;
    }
}

void OcsdLibDcdRegister::deRegisterCustomDecoders()
{
    std::map<const ocsd_trace_protocol_t, IDecoderMngr *>::const_iterator iter = m_typed_decoder_mngrs.begin();
    while(iter != m_typed_decoder_mngrs.end())
    {
        IDecoderMngr *pMngr = iter->second;
        if(pMngr->getProtocolType() >= OCSD_PROTOCOL_CUSTOM_0)
            delete pMngr;
        iter++;
    }
}

const ocsd_err_t OcsdLibDcdRegister::getDecoderMngrByName(const std::string &name, IDecoderMngr **p_decoder_mngr)
{
    if(!m_b_registeredBuiltins)
    {
        registerBuiltInDecoders();
        if(!m_b_registeredBuiltins)
            return OCSD_ERR_MEM;
    }

    std::map<const std::string,  IDecoderMngr *>::const_iterator iter = m_decoder_mngrs.find(name);
    if(iter == m_decoder_mngrs.end())
        return OCSD_ERR_DCDREG_NAME_UNKNOWN;
    *p_decoder_mngr = iter->second;
    return OCSD_OK;
}

const ocsd_err_t OcsdLibDcdRegister::getDecoderMngrByType(const ocsd_trace_protocol_t decoderType, IDecoderMngr **p_decoder_mngr)
{
    if(!m_b_registeredBuiltins)
    {
        registerBuiltInDecoders();
        if(!m_b_registeredBuiltins)
            return OCSD_ERR_MEM;
    }

    if (m_pLastTypedDecoderMngr && (m_pLastTypedDecoderMngr->getProtocolType() == decoderType))
        *p_decoder_mngr = m_pLastTypedDecoderMngr;
    else
    {
        std::map<const ocsd_trace_protocol_t, IDecoderMngr *>::const_iterator iter = m_typed_decoder_mngrs.find(decoderType);
        if (iter == m_typed_decoder_mngrs.end())
            return OCSD_ERR_DCDREG_TYPE_UNKNOWN;
        *p_decoder_mngr = m_pLastTypedDecoderMngr = iter->second;
    }
    return OCSD_OK;
}

const bool OcsdLibDcdRegister::isRegisteredDecoder(const std::string &name)
{
    std::map<const std::string,  IDecoderMngr *>::const_iterator iter = m_decoder_mngrs.find(name);
    if(iter != m_decoder_mngrs.end())
        return true;
    return false;
}

const bool OcsdLibDcdRegister::isRegisteredDecoderType(const ocsd_trace_protocol_t decoderType)
{
    std::map<const ocsd_trace_protocol_t, IDecoderMngr *>::const_iterator iter = m_typed_decoder_mngrs.find(decoderType);
    if(iter !=  m_typed_decoder_mngrs.end())
        return true;
    return false;
}

const bool OcsdLibDcdRegister::getFirstNamedDecoder(std::string &name)
{
    m_iter = m_decoder_mngrs.begin();
    return getNextNamedDecoder(name);
}

const bool OcsdLibDcdRegister::getNextNamedDecoder(std::string &name)
{
    if(m_iter == m_decoder_mngrs.end())
        return false;
    name = m_iter->first;
    m_iter++;
    return true;
}

/* End of File ocsd_lib_dcd_register.cpp */

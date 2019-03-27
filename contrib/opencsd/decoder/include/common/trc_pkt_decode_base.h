/*!
 * \file       trc_pkt_decode_base.h
 * \brief      OpenCSD : Trace Packet decoder base class.
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

#ifndef ARM_TRC_PKT_DECODE_BASE_H_INCLUDED
#define ARM_TRC_PKT_DECODE_BASE_H_INCLUDED

#include "trc_component.h"
#include "comp_attach_pt_t.h"

#include "interfaces/trc_pkt_in_i.h"
#include "interfaces/trc_gen_elem_in_i.h"
#include "interfaces/trc_tgt_mem_access_i.h"
#include "interfaces/trc_instr_decode_i.h"

/** @defgroup ocsd_pkt_decode OpenCSD Library : Packet Decoders.

    @brief Classes providing Protocol Packet Decoding capability.

    Packet decoders convert incoming protocol packets from a packet processor,
    into generic trace elements to be output to an analysis program.

    Packet decoders can be:-
    - PE decoders - converting ETM or PTM packets into instruction and data trace elements
    - SW stimulus decoder - converting STM or ITM packets into software generated trace elements.
    - Bus decoders - converting HTM packets into bus transaction elements.

@{*/


class TrcPktDecodeI : public TraceComponent
{
public:
    TrcPktDecodeI(const char *component_name);
    TrcPktDecodeI(const char *component_name, int instIDNum);
    virtual ~TrcPktDecodeI() {};

    componentAttachPt<ITrcGenElemIn> *getTraceElemOutAttachPt() { return &m_trace_elem_out; };
    componentAttachPt<ITargetMemAccess> *getMemoryAccessAttachPt() { return &m_mem_access; };
    componentAttachPt<IInstrDecode> *getInstrDecodeAttachPt() { return &m_instr_decode; };

    void setUsesMemAccess(bool bUsesMemaccess) { m_uses_memaccess = bUsesMemaccess; };
    const bool getUsesMemAccess() const { return m_uses_memaccess; };

    void setUsesIDecode(bool bUsesIDecode) { m_uses_idecode = bUsesIDecode; };
    const bool getUsesIDecode() const { return m_uses_idecode; };

protected:

    /* implementation packet decoding interface */
    virtual ocsd_datapath_resp_t processPacket() = 0;
    virtual ocsd_datapath_resp_t onEOT() = 0;
    virtual ocsd_datapath_resp_t onReset() = 0;
    virtual ocsd_datapath_resp_t onFlush() = 0;
    virtual ocsd_err_t onProtocolConfig() = 0;
    virtual const uint8_t getCoreSightTraceID() = 0;

    const bool checkInit();

    /* data output */
    ocsd_datapath_resp_t outputTraceElement(const OcsdTraceElement &elem);    // use current index
    ocsd_datapath_resp_t outputTraceElementIdx(ocsd_trc_index_t idx, const OcsdTraceElement &elem); // use supplied index (where decoder caches elements) 

    /* target access */
    ocsd_err_t accessMemory(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, uint32_t *num_bytes, uint8_t *p_buffer);

    /* instruction decode */
    ocsd_err_t instrDecode(ocsd_instr_info *instr_info);

    componentAttachPt<ITrcGenElemIn> m_trace_elem_out;
    componentAttachPt<ITargetMemAccess> m_mem_access;
    componentAttachPt<IInstrDecode> m_instr_decode;

    ocsd_trc_index_t   m_index_curr_pkt;

    bool m_decode_init_ok;  //!< set true if all attachments in place for decode. (remove checks in main throughput paths)
    bool m_config_init_ok;  //!< set true if config set.

    std::string init_err_msg;    //!< error message for init error

    bool m_uses_memaccess;
    bool m_uses_idecode;

};

inline TrcPktDecodeI::TrcPktDecodeI(const char *component_name) : 
    TraceComponent(component_name),
    m_index_curr_pkt(0),
    m_decode_init_ok(false),
    m_config_init_ok(false),
    m_uses_memaccess(true),
    m_uses_idecode(true)
{
}

inline TrcPktDecodeI::TrcPktDecodeI(const char *component_name, int instIDNum) :
    TraceComponent(component_name, instIDNum),
    m_index_curr_pkt(0),
    m_decode_init_ok(false),
    m_config_init_ok(false),
    m_uses_memaccess(true),
    m_uses_idecode(true)
{
}

inline const bool TrcPktDecodeI::checkInit()
{
    if(!m_decode_init_ok)
    {
        if(!m_config_init_ok)
            init_err_msg = "No decoder configuration information";
        else if(!m_trace_elem_out.hasAttachedAndEnabled())
            init_err_msg = "No element output interface attached and enabled";
        else if(m_uses_memaccess && !m_mem_access.hasAttachedAndEnabled())
            init_err_msg = "No memory access interface attached and enabled";
        else if(m_uses_idecode && !m_instr_decode.hasAttachedAndEnabled())
            init_err_msg = "No instruction decoder interface attached and enabled";
        else
            m_decode_init_ok = true;
    }
    return m_decode_init_ok;
}

inline ocsd_datapath_resp_t TrcPktDecodeI::outputTraceElement(const OcsdTraceElement &elem)
{
    return m_trace_elem_out.first()->TraceElemIn(m_index_curr_pkt,getCoreSightTraceID(), elem);
}

inline ocsd_datapath_resp_t TrcPktDecodeI::outputTraceElementIdx(ocsd_trc_index_t idx, const OcsdTraceElement &elem)
{
    return m_trace_elem_out.first()->TraceElemIn(idx, getCoreSightTraceID(), elem);
}

inline ocsd_err_t TrcPktDecodeI::instrDecode(ocsd_instr_info *instr_info)
{
    if(m_uses_idecode)
        return m_instr_decode.first()->DecodeInstruction(instr_info);
    return OCSD_ERR_DCD_INTERFACE_UNUSED;
}

inline ocsd_err_t TrcPktDecodeI::accessMemory(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, uint32_t *num_bytes, uint8_t *p_buffer)
{
    if(m_uses_memaccess)
        return m_mem_access.first()->ReadTargetMemory(address,getCoreSightTraceID(),mem_space, num_bytes,p_buffer);
    return OCSD_ERR_DCD_INTERFACE_UNUSED;
}

/**********************************************************************/
template <class P, class Pc>
class TrcPktDecodeBase : public TrcPktDecodeI, public IPktDataIn<P>
{
public:
    TrcPktDecodeBase(const char *component_name);
    TrcPktDecodeBase(const char *component_name, int instIDNum);
    virtual ~TrcPktDecodeBase();

    virtual ocsd_datapath_resp_t PacketDataIn( const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index_sop,
                                                const P *p_packet_in);
    

    /* protocol configuration */
    ocsd_err_t setProtocolConfig(const Pc *config); 
    const Pc *  getProtocolConfig() const { return  m_config; };
    
protected:
    void ClearConfigObj();

    /* the protocol configuration */
    Pc *          m_config;
    /* the current input packet */
    const P *     m_curr_packet_in;
    
};


template <class P, class Pc> TrcPktDecodeBase<P, Pc>::TrcPktDecodeBase(const char *component_name) : 
    TrcPktDecodeI(component_name),
    m_config(0)
{
}

template <class P, class Pc> TrcPktDecodeBase<P, Pc>::TrcPktDecodeBase(const char *component_name, int instIDNum) : 
    TrcPktDecodeI(component_name,instIDNum),
    m_config(0)
{
}

template <class P, class Pc> TrcPktDecodeBase<P, Pc>::~TrcPktDecodeBase() 
{
    ClearConfigObj();
}

template <class P, class Pc> ocsd_datapath_resp_t TrcPktDecodeBase<P, Pc>::PacketDataIn( const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index_sop,
                                                const P *p_packet_in)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    if(!checkInit())
    {
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_NOT_INIT,init_err_msg));
        return OCSD_RESP_FATAL_NOT_INIT;
    }

    switch(op)
    {
    case OCSD_OP_DATA:
        if(p_packet_in == 0)
        {
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PARAM_VAL));
            resp = OCSD_RESP_FATAL_INVALID_PARAM;
        }
        else
        {
            m_curr_packet_in = p_packet_in;
            m_index_curr_pkt = index_sop;
            resp = processPacket();
        }
        break;

    case OCSD_OP_EOT:
        resp = onEOT();
        break;

    case OCSD_OP_FLUSH:
        resp = onFlush();
        break;

    case OCSD_OP_RESET:
        resp = onReset();
        break;

    default:
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PARAM_VAL));
        resp = OCSD_RESP_FATAL_INVALID_OP;
        break;
    }
    return resp;
}

    /* protocol configuration */
template <class P, class Pc>  ocsd_err_t TrcPktDecodeBase<P, Pc>::setProtocolConfig(const Pc *config)
{
    ocsd_err_t err = OCSD_ERR_INVALID_PARAM_VAL;
    if(config != 0)
    {
        ClearConfigObj(); // remove any current config
        m_config = new (std::nothrow) Pc(*config); // make a copy of the config - don't rely on the object passed in being valid outside the context of the call.
        if(m_config != 0)
        {
            err = onProtocolConfig();
            if(err == OCSD_OK)
                m_config_init_ok = true;
        }
        else
            err = OCSD_ERR_MEM;
    }
    return err;
}

template <class P, class Pc> void TrcPktDecodeBase<P, Pc>::ClearConfigObj()
{
    if(m_config)
    {
        delete m_config;
        m_config = 0;
    }
}

/** @}*/
#endif // ARM_TRC_PKT_DECODE_BASE_H_INCLUDED

/* End of File trc_pkt_decode_base.h */

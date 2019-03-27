/*!
 * \file       trc_pkt_proc_base.h
 * \brief      OpenCSD : Trace packet processor base class.
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

#ifndef ARM_TRC_PKT_PROC_BASE_H_INCLUDED
#define ARM_TRC_PKT_PROC_BASE_H_INCLUDED

#include "interfaces/trc_data_raw_in_i.h"
#include "interfaces/trc_pkt_in_i.h"
#include "interfaces/trc_pkt_raw_in_i.h"
#include "interfaces/trc_indexer_pkt_i.h"

#include "trc_component.h"
#include "comp_attach_pt_t.h"

/** @defgroup ocsd_pkt_proc  OpenCSD Library : Packet Processors.
    @brief Classes providing Protocol Packet Processing capability.

    Packet processors take an incoming byte stream and convert into discrete packets for the
    required trace protocol.
@{*/



/*!
 * @class TrcPktProcI   
 * @brief Base Packet processing interface
 *  
 * Defines the packet processing methods that protocol specific processors must 
 * implement.
 * 
 */
class TrcPktProcI : public TraceComponent, public ITrcDataIn
{
public:
    TrcPktProcI(const char *component_name);
    TrcPktProcI(const char *component_name, int instIDNum);
    virtual ~TrcPktProcI() {}; 

    /** Trace byte data input interface - from ITrcDataIn.
     */
    virtual ocsd_datapath_resp_t TraceDataIn(  const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed) = 0;

protected:

    /* implementation packet processing interface */

    /*! @brief Implementation function for the OCSD_OP_DATA operation */
    virtual ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed) = 0;
        
    virtual ocsd_datapath_resp_t onEOT() = 0;       //!< Implementation function for the OCSD_OP_EOT operation
    virtual ocsd_datapath_resp_t onReset() = 0;     //!< Implementation function for the OCSD_OP_RESET operation
    virtual ocsd_datapath_resp_t onFlush() = 0;     //!< Implementation function for the OCSD_OP_FLUSH operation
    virtual ocsd_err_t onProtocolConfig() = 0;      //!< Called when the configuration object is passed to the decoder.
    virtual const bool isBadPacket() const = 0;     //!< check if the current packet is an error / bad packet
};

inline TrcPktProcI::TrcPktProcI(const char *component_name) :
    TraceComponent(component_name)
{
}

inline TrcPktProcI::TrcPktProcI(const char *component_name, int instIDNum) :
    TraceComponent(component_name,instIDNum)
{
}

/*!
 * @class TrcPktProcBase
 * @brief Packet Processor base class. Provides common infrastructure and interconnections for packet processors.
 * 
 *  The class is a templated base class. 
 *  - P  - this is the packet object class.
 *  - Pt - this is the packet type class.
 *  - Pc - this is the packet configuration class.
 * 
 *  implementations will provide concrete classes for each of these to operate under the common infrastructures.
 *  The base provides the trace data in (ITrcDataIn) interface and operates on the incoming operation type.
 *  
 *  Implementions override the 'onFn()' and data process functions defined in TrcPktProcI, 
 *  with the base class ensuring consistent ordering of operations.
 * 
 */
template <class P, class Pt, class Pc> 
class TrcPktProcBase : public TrcPktProcI
{
public:
    TrcPktProcBase(const char *component_name);
    TrcPktProcBase(const char *component_name, int instIDNum);
    virtual ~TrcPktProcBase(); 

    /** Byte trace data input interface defined in ITrcDataIn
        
        The base class implementation processes the operation to call the 
        interface functions on TrcPktProcI.
     */
    virtual ocsd_datapath_resp_t TraceDataIn(  const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed);


/* component attachment points */ 

    //! Attachement point for the protocol packet output
    componentAttachPt<IPktDataIn<P>> *getPacketOutAttachPt()  { return &m_pkt_out_i; };
    //! Attachment point for the protocol packet monitor
    componentAttachPt<IPktRawDataMon<P>> *getRawPacketMonAttachPt() { return &m_pkt_raw_mon_i; };

    //! Attachment point for a packet indexer 
    componentAttachPt<ITrcPktIndexer<Pt>> *getTraceIDIndexerAttachPt() { return &m_pkt_indexer_i; };
   
/* protocol configuration */
    //!< Set the protocol specific configuration for the decoder.
    virtual ocsd_err_t setProtocolConfig(const Pc *config);
    //!< Get the configuration for the decoder.
    virtual const Pc *getProtocolConfig() const { return m_config; };

protected:

    /* data output functions */
    ocsd_datapath_resp_t outputDecodedPacket(const ocsd_trc_index_t index_sop, const P *pkt);
    
    void outputRawPacketToMonitor( const ocsd_trc_index_t index_sop,
                                   const P *pkt,
                                   const uint32_t size,
                                   const uint8_t *p_data);

    void indexPacket(const ocsd_trc_index_t index_sop, const Pt *packet_type);

    ocsd_datapath_resp_t outputOnAllInterfaces(const ocsd_trc_index_t index_sop, const P *pkt, const Pt *pkt_type, std::vector<uint8_t> &pktdata);

    ocsd_datapath_resp_t outputOnAllInterfaces(const ocsd_trc_index_t index_sop, const P *pkt, const Pt *pkt_type, const uint8_t *pktdata, uint32_t pktlen);

    /*! Let the derived class figure out if it needs to collate and send raw data.
        can improve wait for sync performance if we do not need to save and send unsynced data.    
    */
    const bool hasRawMon() const;   

    /* the protocol configuration */
    const Pc *m_config; 

    void ClearConfigObj();  // remove our copy of the config 

    const bool checkInit(); // return true if init (configured and at least one output sink attached), false otherwise.

private:
    /* decode control */
    ocsd_datapath_resp_t Reset(const ocsd_trc_index_t index);
    ocsd_datapath_resp_t Flush();
    ocsd_datapath_resp_t EOT();

    componentAttachPt<IPktDataIn<P>> m_pkt_out_i;    
    componentAttachPt<IPktRawDataMon<P>> m_pkt_raw_mon_i;

    componentAttachPt<ITrcPktIndexer<Pt>> m_pkt_indexer_i;

    bool m_b_is_init;
};

template<class P,class Pt, class Pc> TrcPktProcBase<P, Pt, Pc>::TrcPktProcBase(const char *component_name) : 
    TrcPktProcI(component_name),
    m_config(0),
    m_b_is_init(false)
{
}

template<class P,class Pt, class Pc> TrcPktProcBase<P, Pt, Pc>::TrcPktProcBase(const char *component_name, int instIDNum) : 
    TrcPktProcI(component_name, instIDNum),
    m_config(0),
    m_b_is_init(false)
{
}

template<class P,class Pt, class Pc> TrcPktProcBase<P, Pt, Pc>::~TrcPktProcBase()
{
    ClearConfigObj();
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::TraceDataIn(  const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    
    switch(op)
    {
    case OCSD_OP_DATA:
        if((dataBlockSize == 0) || (pDataBlock == 0) || (numBytesProcessed == 0))
        {
            if(numBytesProcessed)
                *numBytesProcessed = 0; // ensure processed bytes value set to 0.
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PARAM_VAL,"Packet Processor: Zero length data block or NULL pointer error\n"));
            resp = OCSD_RESP_FATAL_INVALID_PARAM;
        }
        else
            resp = processData(index,dataBlockSize,pDataBlock,numBytesProcessed);
        break;

    case OCSD_OP_EOT:
        resp = EOT();
        break;

    case OCSD_OP_FLUSH:
        resp = Flush();
        break;

    case OCSD_OP_RESET:
        resp = Reset(index);
        break;

    default:
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PARAM_VAL,"Packet Processor : Unknown Datapath operation\n"));
        resp = OCSD_RESP_FATAL_INVALID_OP;
        break;
    }
    return resp;
}


template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::Reset(const ocsd_trc_index_t index)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // reset the trace decoder attachment on main data path.
    if(m_pkt_out_i.hasAttachedAndEnabled())
        resp = m_pkt_out_i.first()->PacketDataIn(OCSD_OP_RESET,index,0);

    // reset the packet processor implmentation
    if(!OCSD_DATA_RESP_IS_FATAL(resp))
        resp = onReset();

    // packet monitor
    if(m_pkt_raw_mon_i.hasAttachedAndEnabled())
        m_pkt_raw_mon_i.first()->RawPacketDataMon(OCSD_OP_RESET,index,0,0,0);

    return resp;
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::Flush()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    ocsd_datapath_resp_t resplocal = OCSD_RESP_CONT;

    // the trace decoder attachment on main data path.
    if(m_pkt_out_i.hasAttachedAndEnabled())
        resp = m_pkt_out_i.first()->PacketDataIn(OCSD_OP_FLUSH,0,0); // flush up the data path first.

    // if the connected components are flushed, not flush this one.
    if(OCSD_DATA_RESP_IS_CONT(resp))
        resplocal = onFlush();      // local flush

    return (resplocal > resp) ?  resplocal : resp;
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::EOT()
{
    ocsd_datapath_resp_t resp = onEOT();   // local EOT - mark any part packet as incomplete type and prepare to send

    // the trace decoder attachment on main data path.
    if(m_pkt_out_i.hasAttachedAndEnabled() && !OCSD_DATA_RESP_IS_FATAL(resp))
        resp = m_pkt_out_i.first()->PacketDataIn(OCSD_OP_EOT,0,0);

    // packet monitor
    if(m_pkt_raw_mon_i.hasAttachedAndEnabled())
        m_pkt_raw_mon_i.first()->RawPacketDataMon(OCSD_OP_EOT,0,0,0,0);

    return resp;
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::outputDecodedPacket(const ocsd_trc_index_t index, const P *pkt)
{
     ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // bad packet filter.
    if((getComponentOpMode() & OCSD_OPFLG_PKTPROC_NOFWD_BAD_PKTS) && isBadPacket())
        return resp;

    // send a complete packet over the primary data path
    if(m_pkt_out_i.hasAttachedAndEnabled())
        resp = m_pkt_out_i.first()->PacketDataIn(OCSD_OP_DATA,index,pkt);
    return resp;
}

template<class P,class Pt, class Pc> void TrcPktProcBase<P, Pt, Pc>::outputRawPacketToMonitor( 
                                    const ocsd_trc_index_t index_sop,
                                    const P *pkt,
                                    const uint32_t size,
                                    const uint8_t *p_data)
{
    // never output 0 sized packets.
    if(size == 0)
        return;

    // bad packet filter.
    if((getComponentOpMode() & OCSD_OPFLG_PKTPROC_NOMON_BAD_PKTS) && isBadPacket())
        return;

    // packet monitor - this cannot return CONT / WAIT, but does get the raw packet data.
    if(m_pkt_raw_mon_i.hasAttachedAndEnabled())
        m_pkt_raw_mon_i.first()->RawPacketDataMon(OCSD_OP_DATA,index_sop,pkt,size,p_data);
}

template<class P,class Pt, class Pc> const bool TrcPktProcBase<P, Pt, Pc>::hasRawMon() const
{
    return m_pkt_raw_mon_i.hasAttachedAndEnabled();
}

template<class P,class Pt, class Pc> void TrcPktProcBase<P, Pt, Pc>::indexPacket(const ocsd_trc_index_t index_sop, const Pt *packet_type)
{
    // packet indexer - cannot return CONT / WAIT, just gets the current index and type.
    if(m_pkt_indexer_i.hasAttachedAndEnabled())
        m_pkt_indexer_i.first()->TracePktIndex(index_sop,packet_type);
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::outputOnAllInterfaces(const ocsd_trc_index_t index_sop, const P *pkt, const Pt *pkt_type, std::vector<uint8_t> &pktdata)
{
    indexPacket(index_sop,pkt_type);
    if(pktdata.size() > 0)  // prevent out of range errors for 0 length vector.
        outputRawPacketToMonitor(index_sop,pkt,(uint32_t)pktdata.size(),&pktdata[0]);
    return outputDecodedPacket(index_sop,pkt);
}

template<class P,class Pt, class Pc> ocsd_datapath_resp_t TrcPktProcBase<P, Pt, Pc>::outputOnAllInterfaces(const ocsd_trc_index_t index_sop, const P *pkt, const Pt *pkt_type, const uint8_t *pktdata, uint32_t pktlen)
{
    indexPacket(index_sop,pkt_type);
    outputRawPacketToMonitor(index_sop,pkt,pktlen,pktdata);
    return outputDecodedPacket(index_sop,pkt);
}

template<class P,class Pt, class Pc> ocsd_err_t TrcPktProcBase<P, Pt, Pc>::setProtocolConfig(const Pc *config)
{
    ocsd_err_t err = OCSD_ERR_INVALID_PARAM_VAL;
    if(config != 0)
    {
        ClearConfigObj();
        m_config = new (std::nothrow) Pc(*config);
        if(m_config != 0)
            err = onProtocolConfig();
        else
            err = OCSD_ERR_MEM;
    }
    return err;
}

template<class P,class Pt, class Pc> void TrcPktProcBase<P, Pt, Pc>::ClearConfigObj()
{
    if(m_config)
    {
        delete m_config;
        m_config = 0;
    }
}

template<class P,class Pt, class Pc> const bool TrcPktProcBase<P, Pt, Pc>::checkInit()
{
    if(!m_b_is_init)
    {
        if( (m_config != 0) &&
            (m_pkt_out_i.hasAttached() || m_pkt_raw_mon_i.hasAttached())
          )
            m_b_is_init = true;
    }
    return m_b_is_init;
}

/** @}*/

#endif // ARM_TRC_PKT_PROC_BASE_H_INCLUDED

/* End of File trc_pkt_proc_base.h */

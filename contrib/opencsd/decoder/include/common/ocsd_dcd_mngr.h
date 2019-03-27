/*
 * \file       ocsd_dcd_mngr.h
 * \brief      OpenCSD : Decoder manager base class.
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

#ifndef ARM_OCSD_DCD_MNGR_H_INCLUDED
#define ARM_OCSD_DCD_MNGR_H_INCLUDED

#include "opencsd/ocsd_if_types.h"
#include "common/ocsd_dcd_mngr_i.h"
#include "common/ocsd_lib_dcd_register.h"
#include "common/trc_pkt_decode_base.h"
#include "common/trc_pkt_proc_base.h"

template <class P, class Pt, class Pc>
class DecoderMngrBase : public IDecoderMngr
{
public:
    DecoderMngrBase(const std::string &decoderTypeName, ocsd_trace_protocol_t builtInProtocol);
    virtual ~DecoderMngrBase() {};

    // create decoder interface.
    virtual ocsd_err_t createDecoder(const int create_flags, const int instID, const CSConfig *p_config,  TraceComponent **p_component);
    virtual ocsd_err_t destroyDecoder(TraceComponent *p_component);

    virtual const ocsd_trace_protocol_t getProtocolType() const { return m_builtInProtocol; }

// common    
    virtual ocsd_err_t attachErrorLogger(TraceComponent *pComponent, ITraceErrorLog *pIErrorLog);
    
// pkt decoder
    virtual ocsd_err_t attachInstrDecoder(TraceComponent *pComponent, IInstrDecode *pIInstrDec);
    virtual ocsd_err_t attachMemAccessor(TraceComponent *pComponent, ITargetMemAccess *pMemAccessor);
    virtual ocsd_err_t attachOutputSink(TraceComponent *pComponent, ITrcGenElemIn *pOutSink);

// pkt processor
    virtual ocsd_err_t attachPktMonitor(TraceComponent *pComponent, ITrcTypedBase *pPktRawDataMon);
    virtual ocsd_err_t attachPktIndexer(TraceComponent *pComponent, ITrcTypedBase *pPktIndexer);
    virtual ocsd_err_t attachPktSink(TraceComponent *pComponent, ITrcTypedBase *pPktDataInSink);

// data input connection interface
    virtual ocsd_err_t getDataInputI(TraceComponent *pComponent, ITrcDataIn **ppDataIn);

// generate a Config object from opaque config struct pointer.
    virtual ocsd_err_t createConfigFromDataStruct(CSConfig **pConfigBase, const void *pDataStruct);

// implemented by decoder handler derived classes
    virtual TraceComponent *createPktProc(const bool useInstID, const int instID) = 0;
    virtual TraceComponent *createPktDecode(const bool useInstID, const int instID) { return 0; };
    virtual CSConfig *createConfig(const void *pDataStruct) = 0;

    
private:
    ocsd_trace_protocol_t m_builtInProtocol;    //!< Protocol ID if built in type.
};

template <class P, class Pt, class Pc>
DecoderMngrBase<P,Pt,Pc>::DecoderMngrBase(const std::string &decoderTypeName, ocsd_trace_protocol_t builtInProtocol)
{
    OcsdLibDcdRegister *pDcdReg = OcsdLibDcdRegister::getDecoderRegister();
    if(pDcdReg)
        pDcdReg->registerDecoderTypeByName(decoderTypeName,this);
    m_builtInProtocol = builtInProtocol;
}

template <class P, class Pt, class Pc>
ocsd_err_t  DecoderMngrBase<P,Pt,Pc>::createDecoder(const int create_flags, const int instID, const CSConfig *pConfig,  TraceComponent **ppTrcComp)
{
    TraceComponent *pkt_proc = 0;
    TraceComponent *pkt_dcd = 0;
    bool bUseInstID =  (create_flags & OCSD_CREATE_FLG_INST_ID) != 0;
    bool bDecoder = (create_flags & OCSD_CREATE_FLG_FULL_DECODER) != 0;
    bool bUnConfigured = (pConfig == 0);

    const Pc *pConf = dynamic_cast< const Pc * >(pConfig);

    // check inputs valid... 
    if((pConf == 0) && !bUnConfigured)
        return OCSD_ERR_INVALID_PARAM_TYPE;

     if((create_flags & (OCSD_CREATE_FLG_PACKET_PROC | OCSD_CREATE_FLG_FULL_DECODER)) == 0)
        return OCSD_ERR_INVALID_PARAM_VAL;

    // always need a packet processor
    pkt_proc = createPktProc(bUseInstID, instID);
    if(!pkt_proc)
        return OCSD_ERR_MEM;

    // set the configuration
    TrcPktProcBase<P,Pt,Pc> *pProcBase = dynamic_cast< TrcPktProcBase<P,Pt,Pc> *>(pkt_proc);       
    if(pProcBase == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    if(!bUnConfigured)
        pProcBase->setProtocolConfig(pConf);

    *ppTrcComp = pkt_proc;

    // may need a packet decoder
    if(bDecoder)
    {
        // create the decoder
        pkt_dcd = createPktDecode(bUseInstID, instID);
        if(!pkt_dcd)
            return OCSD_ERR_MEM;

        // get the decoder base
        TrcPktDecodeBase<P,Pc> *pBase = dynamic_cast< TrcPktDecodeBase<P,Pc> *>(pkt_dcd);       
        if(pBase == 0)
            return OCSD_ERR_INVALID_PARAM_TYPE;

        if(!bUnConfigured)
            pBase->setProtocolConfig(pConf);

        // associate decoder with packet processor
        // -> this means a TraceComponent with an associated component is a packet decoder.
        //    the associated component is the connected packet processor.
        pkt_dcd->setAssocComponent(pkt_proc);

        // connect packet processor and decoder
        pProcBase->getPacketOutAttachPt()->attach(pBase);

        *ppTrcComp = pkt_dcd;
    }
    return OCSD_OK;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::destroyDecoder(TraceComponent *pComponent)
{
    if(pComponent->getAssocComponent() != 0)
        delete pComponent->getAssocComponent();
    delete pComponent;
    return OCSD_OK;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachErrorLogger(TraceComponent *pComponent, ITraceErrorLog *pIErrorLog)
{
    return pComponent->getErrorLogAttachPt()->replace_first(pIErrorLog);
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachInstrDecoder(TraceComponent *pComponent, IInstrDecode *pIInstrDec)
{
    ocsd_err_t err = OCSD_ERR_DCD_INTERFACE_UNUSED;

    if(pComponent->getAssocComponent() == 0)    // no associated component - so this is a packet processor
        return OCSD_ERR_INVALID_PARAM_TYPE;

    TrcPktDecodeI *pDcdI = dynamic_cast< TrcPktDecodeI * >(pComponent);
    if(pDcdI == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    if(pDcdI->getUsesIDecode())
        err = pDcdI->getInstrDecodeAttachPt()->replace_first(pIInstrDec);

    return err;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachMemAccessor(TraceComponent *pComponent, ITargetMemAccess *pMemAccessor)
{
    ocsd_err_t err = OCSD_ERR_DCD_INTERFACE_UNUSED;

    if(pComponent->getAssocComponent() == 0)    // no associated component - so this is a packet processor
        return OCSD_ERR_INVALID_PARAM_TYPE;

    TrcPktDecodeI *pDcdI = dynamic_cast< TrcPktDecodeI * >(pComponent);
    if(pDcdI == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    if(pDcdI->getUsesMemAccess())
        err = pDcdI->getMemoryAccessAttachPt()->replace_first(pMemAccessor);        

    return err;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachOutputSink(TraceComponent *pComponent, ITrcGenElemIn *pOutSink)
{
    ocsd_err_t err = OCSD_ERR_INVALID_PARAM_TYPE;

    if(pComponent->getAssocComponent() == 0)    // no associated component - so this is a packet processor
        return err;

    TrcPktDecodeI *pDcdI = dynamic_cast< TrcPktDecodeI * >(pComponent);
    if(pDcdI == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    err = pDcdI->getTraceElemOutAttachPt()->replace_first(pOutSink);

    return err;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::getDataInputI(TraceComponent *pComponent, ITrcDataIn **ppDataIn)
{
    // find the packet processor
    TraceComponent *pPktProc = pComponent;
    if(pComponent->getAssocComponent() != 0)
        pPktProc = pComponent->getAssocComponent();

    TrcPktProcI *pPPI = dynamic_cast< TrcPktProcI * >(pPktProc);
    if(pPPI == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    *ppDataIn = pPPI;

    return OCSD_OK;
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachPktMonitor(TraceComponent *pComponent, ITrcTypedBase *pPktRawDataMon)
{
    // find the packet processor
    TraceComponent *pPktProc = pComponent;
    if(pComponent->getAssocComponent() != 0)
        pPktProc = pComponent->getAssocComponent();

    // get the packet processor
    TrcPktProcBase<P,Pt,Pc> *pPktProcBase = dynamic_cast< TrcPktProcBase<P,Pt,Pc> * >(pPktProc);
    if(pPktProcBase == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    // get the interface
    IPktRawDataMon<P> *p_If =  dynamic_cast< IPktRawDataMon<P> * >(pPktRawDataMon);
    if(p_If == 0)
        return  OCSD_ERR_INVALID_PARAM_TYPE;

    return pPktProcBase->getRawPacketMonAttachPt()->replace_first(p_If);
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachPktIndexer(TraceComponent *pComponent, ITrcTypedBase *pPktIndexer)
{
    // find the packet processor
    TraceComponent *pPktProc = pComponent;
    if(pComponent->getAssocComponent() != 0)
        pPktProc = pComponent->getAssocComponent();

    // get the packet processor
    TrcPktProcBase<P,Pt,Pc> *pPktProcBase = dynamic_cast< TrcPktProcBase<P,Pt,Pc> * >(pPktProc);
    if(pPktProcBase == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    // get the interface
    ITrcPktIndexer<Pt> *p_If =  dynamic_cast< ITrcPktIndexer<Pt> * >(pPktIndexer);
    if(p_If == 0)
        return  OCSD_ERR_INVALID_PARAM_TYPE;

    return pPktProcBase->getTraceIDIndexerAttachPt()->replace_first(p_If);
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::attachPktSink(TraceComponent *pComponent, ITrcTypedBase *pPktDataInSink)
{
    // must be solo packet processor
    if(pComponent->getAssocComponent() != 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    // interface must be the correct one.
    IPktDataIn<P> *pkt_in_i = dynamic_cast< IPktDataIn<P> * >(pPktDataInSink);
    if(pkt_in_i == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    // get the packet processor
    TrcPktProcBase<P,Pt,Pc> *pPktProcBase = dynamic_cast< TrcPktProcBase<P,Pt,Pc> * >(pComponent);
    if(pPktProcBase == 0)
        return OCSD_ERR_INVALID_PARAM_TYPE;

    // attach
    return  pPktProcBase->getPacketOutAttachPt()->replace_first(pkt_in_i);
}

template <class P, class Pt, class Pc>
ocsd_err_t DecoderMngrBase<P,Pt,Pc>::createConfigFromDataStruct(CSConfig **pConfigBase, const void *pDataStruct)
{
    CSConfig *pConfig = createConfig(pDataStruct);
    if(!pConfig)
        return OCSD_ERR_MEM;
    *pConfigBase = pConfig;
    return OCSD_OK;
}

/****************************************************************************************************/
/* Full decoder / packet process pair, templated base for creating decoder objects                  */
/****************************************************************************************************/

template<   class P,            // Packet class.
            class Pt,           // Packet enum type ID.
            class Pc,           // Processor config class.
            class PcSt,         // Processor config struct type
            class PktProc,      // Packet processor class.
            class PktDcd>       // Packet decoder class.
class DecodeMngrFullDcd : public DecoderMngrBase<P,Pt,Pc>
{
public:
    DecodeMngrFullDcd (const std::string &name, ocsd_trace_protocol_t builtInProtocol) 
        : DecoderMngrBase<P,Pt,Pc>(name,builtInProtocol) {};

    virtual ~DecodeMngrFullDcd() {};

    virtual TraceComponent *createPktProc(const bool useInstID, const int instID)
    {
        TraceComponent *pComp;
        if(useInstID)
            pComp = new (std::nothrow) PktProc(instID);
        else
            pComp = new (std::nothrow) PktProc();
        return pComp;
    }

    virtual TraceComponent *createPktDecode(const bool useInstID, const int instID)
    {
        TraceComponent *pComp;
        if(useInstID)
            pComp = new (std::nothrow)PktDcd(instID);
        else
            pComp = new (std::nothrow)PktDcd();
        return pComp;
    }

    virtual CSConfig *createConfig(const void *pDataStruct)
    {
       return new (std::nothrow) Pc((PcSt *)pDataStruct);
    }
};

/****************************************************************************************************/
/* Packet processor only, templated base for creating decoder objects                               */
/****************************************************************************************************/

template<   class P,            // Packet class.
            class Pt,           // Packet enum type ID.
            class Pc,           // Processor config class.
            class PcSt,         // Processor config struct type
            class PktProc>      // Packet processor class.
class DecodeMngrPktProc : public DecoderMngrBase<P,Pt,Pc>
{
public:
    DecodeMngrPktProc (const std::string &name, ocsd_trace_protocol_t builtInProtocol) 
        : DecoderMngrBase<P,Pt,Pc>(name,builtInProtocol) {};

    virtual ~DecodeMngrPktProc() {};

    virtual TraceComponent *createPktProc(const bool useInstID, const int instID)
    {
        TraceComponent *pComp;
        if(useInstID)
            pComp = new (std::nothrow) PktProc(instID);
        else
            pComp = new (std::nothrow) PktProc();
        return pComp;
    }

    virtual CSConfig *createConfig(const void *pDataStruct)
    {
       return new (std::nothrow) Pc((PcSt *)pDataStruct);
    }
};



#endif // ARM_OCSD_DCD_MNGR_H_INCLUDED

/* End of File ocsd_dcd_mngr.h */

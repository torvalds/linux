/*
 * \file       ocsd_c_api_obj.h
 * \brief      OpenCSD : C API callback objects. 
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

#ifndef ARM_OCSD_C_API_OBJ_H_INCLUDED
#define ARM_OCSD_C_API_OBJ_H_INCLUDED

#include "opencsd/c_api/ocsd_c_api_types.h"
#include "interfaces/trc_gen_elem_in_i.h"
#include "common/ocsd_msg_logger.h" 

class TraceElemCBBase
{
public:
    TraceElemCBBase() {};
    virtual ~TraceElemCBBase() {};
};

class GenTraceElemCBObj : public ITrcGenElemIn, public TraceElemCBBase
{
public:
    GenTraceElemCBObj(FnTraceElemIn pCBFn, const void *p_context);
    virtual ~GenTraceElemCBObj() {};

    virtual ocsd_datapath_resp_t TraceElemIn(const ocsd_trc_index_t index_sop,
                                              const uint8_t trc_chan_id,
                                              const OcsdTraceElement &elem);

private:
    FnTraceElemIn m_c_api_cb_fn;
    const void *m_p_cb_context;
};



template<class TrcPkt>
class PktCBObj : public IPktDataIn<TrcPkt>
{
public:
    PktCBObj( FnDefPktDataIn pCBFunc, const void *p_context)
    {
        m_c_api_cb_fn = pCBFunc;
        m_p_context = p_context;
    };

    virtual ~PktCBObj() {};

    virtual ocsd_datapath_resp_t PacketDataIn( const ocsd_datapath_op_t op,
                                               const ocsd_trc_index_t index_sop,
                                               const TrcPkt *p_packet_in)
    {
        const void *c_pkt_struct = 0;
        if(op == OCSD_OP_DATA)
            c_pkt_struct = p_packet_in->c_pkt(); // always output the c struct packet
        return m_c_api_cb_fn(m_p_context,op,index_sop,c_pkt_struct);
    };

private:
    FnDefPktDataIn m_c_api_cb_fn;
    const void *m_p_context;
};

// void specialisation for custom decoders that pass packets as const void * pointers
template<>
class PktCBObj<void> : public IPktDataIn<void>
{
public:
    PktCBObj(FnDefPktDataIn pCBFunc, const void *p_context)
    {
        m_c_api_cb_fn = pCBFunc;
        m_p_context = p_context;
    };

    virtual ~PktCBObj() {};

    virtual ocsd_datapath_resp_t PacketDataIn(const ocsd_datapath_op_t op,
        const ocsd_trc_index_t index_sop,
        const void *p_packet_in)
    {
        return m_c_api_cb_fn(m_p_context, op, index_sop, p_packet_in);
    };

private:
    FnDefPktDataIn m_c_api_cb_fn;
    const void *m_p_context;
};


template<class TrcPkt>
class PktMonCBObj : public IPktRawDataMon<TrcPkt>
{
public:
    PktMonCBObj( FnDefPktDataMon pCBFunc, const void *p_context)
    {
        m_c_api_cb_fn = pCBFunc;
        m_p_context = p_context;
    };

    virtual ~PktMonCBObj() {};

    virtual void RawPacketDataMon( const ocsd_datapath_op_t op,
                                               const ocsd_trc_index_t index_sop,
                                               const TrcPkt *p_packet_in,
                                               const uint32_t size,
                                               const uint8_t *p_data)
    {
        const void *c_pkt_struct = 0;
        if(op == OCSD_OP_DATA)
            c_pkt_struct = p_packet_in->c_pkt(); // always output the c struct packet
        m_c_api_cb_fn(m_p_context,op,index_sop,c_pkt_struct,size,p_data);
    };

private:
    FnDefPktDataMon m_c_api_cb_fn;
    const void *m_p_context;
};

// void specialisation for custom decoders that pass packets as const void * pointers
template<>
class PktMonCBObj<void> : public IPktRawDataMon<void>
{
public:
    PktMonCBObj(FnDefPktDataMon pCBFunc, const void *p_context)
    {
        m_c_api_cb_fn = pCBFunc;
        m_p_context = p_context;
    };

    virtual ~PktMonCBObj() {};
    virtual void RawPacketDataMon(const ocsd_datapath_op_t op,
        const ocsd_trc_index_t index_sop,
        const void *p_packet_in,
        const uint32_t size,
        const uint8_t *p_data)
    {
        m_c_api_cb_fn(m_p_context, op, index_sop, p_packet_in, size, p_data);
    };

private:
    FnDefPktDataMon m_c_api_cb_fn;
    const void *m_p_context;
};

/* handler for default string print CB object */
class DefLogStrCBObj : public ocsdMsgLogStrOutI
{
public:
    DefLogStrCBObj()
    {
        m_c_api_cb_fn = 0;
        m_p_context = 0;
    };

    virtual ~DefLogStrCBObj()
    {
        m_c_api_cb_fn = 0;
        m_p_context = 0;
    };

    void setCBFn(const void *p_context, FnDefLoggerPrintStrCB pCBFn)
    {
        m_c_api_cb_fn = pCBFn;
        m_p_context = p_context;
    };

    virtual void printOutStr(const std::string &outStr)
    {
        if(m_c_api_cb_fn)
            m_c_api_cb_fn(m_p_context, outStr.c_str(), outStr.length());
    }

private:
    FnDefLoggerPrintStrCB m_c_api_cb_fn;
    const void *m_p_context;
};

#endif // ARM_OCSD_C_API_OBJ_H_INCLUDED

/* End of File ocsd_c_api_obj.h */

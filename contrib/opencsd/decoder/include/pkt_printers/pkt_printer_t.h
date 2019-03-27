/*
 * \file       pkt_printer_t.h
 * \brief      OpenCSD : Test packet printer.
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

#ifndef ARM_PKT_PRINTER_T_H_INCLUDED
#define ARM_PKT_PRINTER_T_H_INCLUDED

#include "opencsd.h"

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

template<class P>
class PacketPrinter : public IPktDataIn<P>, public IPktRawDataMon<P>, public ItemPrinter
{
public:
    PacketPrinter(const uint8_t trcID);
    PacketPrinter(const uint8_t trcID, ocsdMsgLogger *pMsgLogger);
    virtual ~PacketPrinter();


    virtual ocsd_datapath_resp_t PacketDataIn( const ocsd_datapath_op_t op,
                                        const ocsd_trc_index_t index_sop,                                        
                                        const P *p_packet_in);

    virtual void RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const P *pkt,
                                   const uint32_t size,
                                   const uint8_t *p_data);


private:
    void printIdx_ID(const ocsd_trc_index_t index_sop);

    uint8_t m_trcID;
    bool m_bRawPrint;
    std::ostringstream m_oss;
    ocsd_datapath_resp_t m_last_resp;

};

template<class P> PacketPrinter<P>::PacketPrinter(uint8_t trcID) : 
    m_trcID(trcID),
    m_bRawPrint(false),
    m_last_resp(OCSD_RESP_CONT)
{
}

template<class P> PacketPrinter<P>::PacketPrinter(const uint8_t trcID, ocsdMsgLogger *pMsgLogger) :
    m_trcID(trcID),
    m_bRawPrint(false),
    m_last_resp(OCSD_RESP_CONT)
{
    setMessageLogger(pMsgLogger);
}

template<class P> PacketPrinter<P>::~PacketPrinter()
{
}

template<class P> ocsd_datapath_resp_t PacketPrinter<P>::PacketDataIn( const ocsd_datapath_op_t op,
                                        const ocsd_trc_index_t index_sop,
                                        const P *p_packet_in)
{
    std::string pktstr;
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    
    // wait / flush test verification
    if(!m_bRawPrint && (m_last_resp == OCSD_RESP_WAIT))
    {
        // expect a flush or a complete reset after a wait.
        if((op != OCSD_OP_FLUSH) || (op != OCSD_OP_RESET))
        {
            m_oss <<"ID:"<< std::hex << (uint32_t)m_trcID << "\tERROR: FLUSH operation expected after wait on trace decode path\n";
            itemPrintLine(m_oss.str());
            m_oss.str("");
            return OCSD_RESP_FATAL_INVALID_OP;
        }
    }

    switch(op)
    {
    case OCSD_OP_DATA:
        p_packet_in->toString(pktstr);
        if(!m_bRawPrint)
            printIdx_ID(index_sop);
        m_oss << ";\t" << pktstr << std::endl;

        // test the wait/flush response mechnism
        if(getTestWaits() && !m_bRawPrint)
        {
            decTestWaits();
            resp = OCSD_RESP_WAIT;
        }
        break;

    case OCSD_OP_EOT:
        m_oss <<"ID:"<< std::hex << (uint32_t)m_trcID << "\tEND OF TRACE DATA\n";
        break;

    case OCSD_OP_FLUSH:
        m_oss <<"ID:"<< std::hex << (uint32_t)m_trcID << "\tFLUSH operation on trace decode path\n";
        break;

    case OCSD_OP_RESET:
        m_oss <<"ID:"<< std::hex << (uint32_t)m_trcID << "\tRESET operation on trace decode path\n";
        break;
    }

    m_last_resp = resp;
    itemPrintLine(m_oss.str());
    m_oss.str("");    
    return resp;
}

template<class P> void PacketPrinter<P>::RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const P *pkt,
                                   const uint32_t size,
                                   const uint8_t *p_data)
{
    switch(op)
    {
    case OCSD_OP_DATA:
        printIdx_ID(index_sop);
        m_oss << "; [";
        if((size > 0) && (p_data != 0))
        {
            uint32_t data = 0;
            for(uint32_t i = 0; i < size; i++)
            {
                data = (uint32_t)(p_data[i] & 0xFF); 
                m_oss << "0x" << std::hex << std::setw(2) << std::setfill('0') << data << " ";
            }
        }
        m_oss << "]";
        m_bRawPrint = true;
        PacketDataIn(op,index_sop,pkt);
        m_bRawPrint = false;
        break;

    default:
        PacketDataIn(op,index_sop,pkt);
        break;
    }

}

template<class P> void PacketPrinter<P>::printIdx_ID(const ocsd_trc_index_t index_sop)
{
    m_oss << "Idx:" << std::dec << index_sop << "; ID:"<< std::hex << (uint32_t)m_trcID;
}

#endif // ARM_PKT_PRINTER_T_H_INCLUDED

/* End of File pkt_printer_t.h */

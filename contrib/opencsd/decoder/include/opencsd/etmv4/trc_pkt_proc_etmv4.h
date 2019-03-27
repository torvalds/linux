/*
 * \file       trc_pkt_proc_etmv4.h
 * \brief      OpenCSD : ETMv4 packet processor interface classes.
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

#ifndef ARM_TRC_PKT_PROC_ETMV4_H_INCLUDED
#define ARM_TRC_PKT_PROC_ETMV4_H_INCLUDED


#include "trc_pkt_types_etmv4.h"
#include "common/trc_pkt_proc_base.h"

class EtmV4IPktProcImpl;    /**< ETMv4 I channel packet processor */
class EtmV4DPktProcImpl;    /**< ETMv4 D channel packet processor */
class EtmV4ITrcPacket;
class EtmV4DTrcPacket;
class EtmV4Config;

/** @addtogroup ocsd_pkt_proc
@{*/

class TrcPktProcEtmV4I : public TrcPktProcBase< EtmV4ITrcPacket,  ocsd_etmv4_i_pkt_type, EtmV4Config>
{
public:
    TrcPktProcEtmV4I();
    TrcPktProcEtmV4I(int instIDNum);
    virtual ~TrcPktProcEtmV4I();

protected:
    /* implementation packet processing interface */
    virtual ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed);
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const bool isBadPacket() const;

    friend class EtmV4IPktProcImpl;

    EtmV4IPktProcImpl *m_pProcessor;
};


class TrcPktProcEtmV4D : public TrcPktProcBase< EtmV4DTrcPacket,  ocsd_etmv4_d_pkt_type, EtmV4Config>
{
public:
    TrcPktProcEtmV4D();
    TrcPktProcEtmV4D(int instIDNum);
    virtual ~TrcPktProcEtmV4D();

protected:
    /* implementation packet processing interface */
    virtual ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed);
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const bool isBadPacket() const;

    friend class EtmV4DPktProcImpl;

    EtmV4DPktProcImpl *m_pProcessor;
};

/** @}*/

#endif // ARM_TRC_PKT_PROC_ETMV4_H_INCLUDED

/* End of File trc_pkt_proc_etmv4.h */

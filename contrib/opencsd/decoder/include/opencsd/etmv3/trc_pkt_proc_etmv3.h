/*
 * \file       trc_pkt_proc_etmv3.h
 * \brief      OpenCSD : 
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

#ifndef ARM_TRC_PKT_PROC_ETMV3_H_INCLUDED
#define ARM_TRC_PKT_PROC_ETMV3_H_INCLUDED

#include "trc_pkt_types_etmv3.h"
#include "common/trc_pkt_proc_base.h"

class EtmV3PktProcImpl;
class EtmV3TrcPacket;
class EtmV3Config;

/** @addtogroup ocsd_pkt_proc
@{*/


class TrcPktProcEtmV3 : public TrcPktProcBase< EtmV3TrcPacket, ocsd_etmv3_pkt_type, EtmV3Config>
{
public:
    TrcPktProcEtmV3();
    TrcPktProcEtmV3(int instIDNum);
    virtual ~TrcPktProcEtmV3();

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

    friend class EtmV3PktProcImpl;

    EtmV3PktProcImpl *m_pProcessor;
};


#define ETMV3_OPFLG_UNFORMATTED_SOURCE  0x00010000 /**< Single ETM source from bypassed formatter - need to check for EOT markers */

/** @}*/

#endif // ARM_TRC_PKT_PROC_ETMV3_H_INCLUDED

/* End of File trc_pkt_proc_etm.h */

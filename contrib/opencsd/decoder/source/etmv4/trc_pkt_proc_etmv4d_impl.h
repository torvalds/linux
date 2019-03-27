/*
 * \file       trc_pkt_proc_etmv4d_impl.h
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

#ifndef ARM_TRC_PKT_PROC_ETMV4D_IMPL_H_INCLUDED
#define ARM_TRC_PKT_PROC_ETMV4D_IMPL_H_INCLUDED

#include "etmv4/trc_pkt_proc_etmv4.h"
#include "etmv4/trc_cmp_cfg_etmv4.h"

class EtmV4DPktProcImpl
{
public:
    EtmV4DPktProcImpl();
    ~EtmV4DPktProcImpl();

    void Initialise(TrcPktProcEtmV4D *p_interface);

    ocsd_err_t Configure(const EtmV4Config *p_config);


    ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                        const uint32_t dataBlockSize,
                                        const uint8_t *pDataBlock,
                                        uint32_t *numBytesProcessed);
    ocsd_datapath_resp_t onEOT();
    ocsd_datapath_resp_t onReset();
    ocsd_datapath_resp_t onFlush();

protected:

    bool m_isInit;
    TrcPktProcEtmV4D *m_interface;       /**< The interface to the other decode components */
    
    EtmV4Config m_config;
};


#endif // ARM_TRC_PKT_PROC_ETMV4D_IMPL_H_INCLUDED

/* End of File trc_pkt_proc_etmv4d_impl.h */

/*
 * \file       trc_pkt_in_i.h
 * \brief      OpenCSD : Interface for trace protocol packet input
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

#ifndef ARM_TRC_PKT_IN_I_H_INCLUDED
#define ARM_TRC_PKT_IN_I_H_INCLUDED

#include "trc_abs_typed_base_i.h"

/*!
 * @class IPktDataIn
 * @ingroup ocsd_interfaces
 * @brief Interface class providing an input for discrete protocol packets.
 *
 * Implemented by trace protocol packet decoders to convert packets into 
 * generic trace elements.
 *
 * Packet class used will contain information on the latest packet, 
 * and any intra-packet state.
 * 
 */
template<class P>
class IPktDataIn : public ITrcTypedBase
{
public:
    IPktDataIn() {}; /**< Default constructor. */
    virtual ~IPktDataIn() {}; /**< Default destructor. */

    /*!
     * Interface function to process a single protocol packet.
     * Pass a trace index for the start of packet and a pointer to a packet when the 
     * datapath operation is OCSD_OP_DATA.
     *
     * @param op : Datapath operation.
     * @param index_sop : Trace index for the start of the packet, 0 if not OCSD_OP_DATA.
     * @param *p_packet_in : Protocol Packet - when data path operation is OCSD_OP_DATA. null otherwise. 
     *
     * @return ocsd_datapath_resp_t  : Standard data path response.
     */
    virtual ocsd_datapath_resp_t PacketDataIn( const ocsd_datapath_op_t op,
                                                const ocsd_trc_index_t index_sop,
                                                const P *p_packet_in) = 0;


};

#endif // ARM_TRC_PKT_IN_I_H_INCLUDED

/* End of File trc_proc_pkt_in_i.h */

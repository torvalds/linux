/*
 * \file       trc_pkt_raw_in_i.h
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

#ifndef ARM_TRC_PKT_RAW_IN_I_H_INCLUDED
#define ARM_TRC_PKT_RAW_IN_I_H_INCLUDED

#include "trc_abs_typed_base_i.h"

/*!
 * @class IPktRawDataMon
 * 
 * @brief Interface class for packet processor monitor.
 *
 * @addtogroup ocsd_interfaces
 *
 * This interface provides a monitor point for the packet processor block.
 * The templated interface is called with a complete packet of the given
 * type, plus the raw packet bytes. Use for tools which need to display compplete
 * packets or require additional processing on raw packet data.
 *
 * This interface is not part of the data decode path and cannot provide feedback.
 * 
 */
template<class P>
class IPktRawDataMon : public ITrcTypedBase
{
public:
    IPktRawDataMon() {}; /**< Default constructor. */
    virtual ~IPktRawDataMon() {};  /**< Default destructor. */

    /*!
     * Interface monitor function called with a complete packet, or other
     * data path operation.
     *
     * @param op : Datapath operation
     * @param index_sop : start of packet index
     * @param *pkt : The expanded packet
     * @param size : size of packet data bytes
     * @param *p_data : the packet data bytes.
     *
     */
    virtual void RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const P *pkt,
                                   const uint32_t size,
                                   const uint8_t *p_data) = 0;
};

#endif // ARM_TRC_PKT_RAW_IN_I_H_INCLUDED

/* End of File trc_pkt_raw_in_i.h */

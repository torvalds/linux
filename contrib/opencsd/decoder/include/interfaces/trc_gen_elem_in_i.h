/*
 * \file       trc_gen_elem_in_i.h
 * \brief      OpenCSD : Generic Trace Element interface.
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

#ifndef ARM_TRC_GEN_ELEM_IN_I_H_INCLUDED
#define ARM_TRC_GEN_ELEM_IN_I_H_INCLUDED

class OcsdTraceElement;

/*!
 * @class ITrcGenElemIn
  
 * @brief Interface for the input of generic trace elements. 
 *
 * @ingroup ocsd_interfaces
 *
 * This interface is the principal output attachment point for the trace packet decoders.
 * 
 */
class ITrcGenElemIn
{
public:
    ITrcGenElemIn() {};  /**< Default constructor. */
    virtual ~ITrcGenElemIn() {}; /**< Default destructor. */


    /*!
     * Interface for analysis blocks that take generic trace elements as their input.
     * Final interface on the decode data path. The index provided is that for the generating
     * trace packet. Multiple generic elements may be produced from a single packet so they will 
     * all have the same start index.
     *
     * @param index_sop : Trace index for start of packet generating this element.
     * @param trc_chan_id : CoreSight Trace ID for this source.
     * @param &elem : Generic trace element generated from the deocde data path
     *
     * @return ocsd_datapath_resp_t  : Standard data path response.
     */
    virtual ocsd_datapath_resp_t TraceElemIn(const ocsd_trc_index_t index_sop,
                                              const uint8_t trc_chan_id,
                                              const OcsdTraceElement &elem) = 0;
};

#endif // ARM_TRC_GEN_ELEM_IN_I_H_INCLUDED

/* End of File trc_gen_elem_in_i.h */

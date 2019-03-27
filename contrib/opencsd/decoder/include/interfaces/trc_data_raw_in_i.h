/*
 * \file       trc_data_raw_in_i.h
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

#ifndef ARM_TRCDATA_RAW_IN_I_H_INCLUDED
#define ARM_TRCDATA_RAW_IN_I_H_INCLUDED

#include "opencsd/ocsd_if_types.h"

/** @class ITrcDataIn
  * 
  * @brief Interface to either trace data frame deformatter or packet processor.
  *
  * @ingroup ocsd_interfaces
  *
  * Interface class to a processor that can consume raw formatted trace byte stream from a trace reader 
  * or raw source buffer into a deformatter object.
  * 
  * Also used to interface a single trace source ID data stream into a packet processor.
  * 
  */
class ITrcDataIn {
public:
    ITrcDataIn() {};    /**< Default constructor. */
    virtual ~ITrcDataIn() {};   /**< Default destructor. */

    /*!
     * Data input method for a component on the Trace decode datapath.
     * Datapath operations passed to the component, which responds with data path response codes.
     * 
     * This API is for raw trace data, which can be:- 
     * - CoreSight formatted frame data for input to the frame deformatter.
     * - Single binary source data for input to a packet decoder. 
     *
     * @param op : Data path operation.
     * @param index : Byte index of start of pDataBlock data as offset from start of captured data. May be zero for none-data operation 
     * @param dataBlockSize : Size of data block. Zero for none-data operation.
     * @param *pDataBlock : pointer to data block. Null for none-data operation 
     * @param *numBytesProcessed : Pointer to count of data used by processor. Set by processor on data operation. Null for none-data operation
     *
     * @return ocsd_datapath_resp_t  : Standard data path response code.
     */
    virtual ocsd_datapath_resp_t TraceDataIn( const ocsd_datapath_op_t op,
                                                  const ocsd_trc_index_t index,
                                                  const uint32_t dataBlockSize,
                                                  const uint8_t *pDataBlock,
                                                  uint32_t *numBytesProcessed) = 0;

};

#endif // ARM_TRCDATA_RAW_IN_I_H_INCLUDED


/* End of File trc_data_raw_in_i.h */

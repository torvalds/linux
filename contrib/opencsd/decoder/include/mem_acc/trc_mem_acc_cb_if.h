/*!
 * \file       trc_mem_acc_cb_if.h
 * \brief      OpenCSD : Memory Accessor Callback Direct Interface
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

#ifndef ARM_TRC_MEM_ACC_CB_IF_H_INCLUDED
#define ARM_TRC_MEM_ACC_CB_IF_H_INCLUDED

#include "opencsd/ocsd_if_types.h"

/*!
 * @class TrcMemAccCBIF
 * @brief Interface class to implement memory accessor callbacks 
 * 
 *  Implement an object with this interface to use in a memory accessor callback type.
 *  Callback accesses the memory according to address and memory space.
 *  Use for trace decode memory access on live systems, or where the implemented accessor types 
 *  are not suitable for the memory data being accessed.
 * 
 */
class TrcMemAccCBIF
{
public:
    TrcMemAccCBIF() {};
    virtual ~TrcMemAccCBIF() {};

    /*!
     * Read bytes from via the accessor from the memory range. 
     *
     * @param s_address : Start address of the read.
     * @param memSpace  : memory space for this access. 
     * @param reqBytes : Number of bytes required.
     * @param *byteBuffer : Buffer to copy the bytes into.
     *
     * @return uint32_t : Number of bytes read, 0 if s_address out of range, or mem space not accessible.
     */
    virtual const uint32_t readBytes(const ocsd_vaddr_t s_address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer) = 0;
};

#endif // ARM_TRC_MEM_ACC_CB_IF_H_INCLUDED

/* End of File trc_mem_acc_cb_if.h */

/*
 * \file       trc_mem_acc_bufptr.h
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

#ifndef ARM_TRC_MEM_ACC_BUFPTR_H_INCLUDED
#define ARM_TRC_MEM_ACC_BUFPTR_H_INCLUDED

#include "mem_acc/trc_mem_acc_base.h"

/*!
 * @class TrcMemAccBufPtr:   
 * @brief Trace memory accessor for a memory buffer.
 *  
 * Wraps a memory buffer in an memory range accessor object.
 * Takes a copy of the buffer pointer which must remain valid 
 * for the lifetime of the object.
 * 
 */
class TrcMemAccBufPtr: public TrcMemAccessorBase
{
public:
    /*!
     * Construct the accessor.
     * uses the start address as the start of range and calculates the end address
     * according to the buffer size
     *
     * @param s_address : Start address in memory map represented by the data in the buffer.
     * @param *p_buffer : pointer to a buffer of binary data.
     * @param size : size of the buffer.
     *
     */
    TrcMemAccBufPtr(const ocsd_vaddr_t s_address, const uint8_t *p_buffer, const uint32_t size);

    virtual ~TrcMemAccBufPtr() {};  /**< default destructor */

    /** Memory access override - allow decoder to read bytes from the buffer. */
    virtual const uint32_t readBytes(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer);

private:
    const uint8_t *m_p_buffer;  /**< pointer to the memory buffer  */
    const uint32_t m_size;  /**< size of the memory buffer. */
};

#endif // ARM_TRC_MEM_ACC_BUFPTR_H_INCLUDED

/* End of File trc_mem_acc_bufptr.h */

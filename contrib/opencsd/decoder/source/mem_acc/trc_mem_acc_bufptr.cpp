/*
 * \file       trc_mem_acc_bufptr.cpp
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
#include <cstring>

#include "mem_acc/trc_mem_acc_bufptr.h"

TrcMemAccBufPtr::TrcMemAccBufPtr(const ocsd_vaddr_t s_address, const uint8_t *p_buffer, const uint32_t size) : 
    TrcMemAccessorBase(MEMACC_BUFPTR, s_address, s_address+size-1),
    m_p_buffer(p_buffer),
    m_size(size)
{
}

const uint32_t TrcMemAccBufPtr::readBytes(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint32_t reqBytes, uint8_t *byteBuffer)
{
    // mapper wlll filter memory spaces.
    uint32_t bytesRead = bytesInRange(address,reqBytes); // check bytes available
    if(bytesRead)
        memcpy(byteBuffer,m_p_buffer+address-m_startAddress,bytesRead);
    return bytesRead;
}

/* End of File trc_mem_acc_bufptr.cpp */

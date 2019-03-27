/*
 * \file       trc_tgt_mem_access_i.h
 * \brief      OpenCSD : Target memory read interface.
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

#ifndef ARM_TRC_TGT_MEM_ACCESS_I_H_INCLUDED
#define ARM_TRC_TGT_MEM_ACCESS_I_H_INCLUDED

/*!
 * @class ITargetMemAccess   
 * 
 * @brief Interface to target memory access.
 *
 * @ingroup ocsd_interfaces
 *
 * Read Target memory call is used by the decoder to access the memory location in the
 * target memory space for the next instruction(s) to be traced.
 *
 * Memory data returned is to be little-endian.
 *
 * The implementator of this interface could either use file(s) containing dumps of memory
 * locations from the target, be an elf file reader extracting code, or a live target 
 * connection, depending on the tool execution context.
 *
 *
 */
class ITargetMemAccess 
{
public:
    ITargetMemAccess() {}; /**< default interface constructor */
    virtual ~ITargetMemAccess() {}; /**< default interface destructor */

    /*!
     * Read a block of target memory into supplied buffer.
     *
     * Bytes read set less than bytes required, along with a success return code indicates full memory 
     * location not accessible. Function will return all accessible bytes from the address up to the point
     * where the first inaccessible location appears.
     * 
     * The cs_trace_id associates a memory read with a core. Different cores may have different memory spaces,
     * the memory access may take this into account. Access will first look in the registered memory areas
     * associated with the ID, failing that will look into any global memory spaces.
     *
     * @param address : Address to access.
     * @param cs_trace_id : protocol source trace ID.
     * @param mem_space : Memory space to access, (secure, non-secure, optionally with EL, or any).
     * @param num_bytes : [in] Number of bytes required. [out] Number of bytes actually read.
     * @param *p_buffer : Buffer to fill with the bytes.
     *
     * @return ocsd_err_t : OCSD_OK on successful access (including memory not available)
     */
    virtual ocsd_err_t ReadTargetMemory(   const ocsd_vaddr_t address, 
                                            const uint8_t cs_trace_id, 
                                            const ocsd_mem_space_acc_t mem_space, 
                                            uint32_t *num_bytes, 
                                            uint8_t *p_buffer) = 0;
};


#endif // ARM_TRC_TGT_MEM_ACCESS_I_H_INCLUDED

/* End of File trc_tgt_mem_access_i.h */

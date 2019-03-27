/*!
 * \file       trc_mem_acc_cb.h
 * \brief      OpenCSD : Callback trace memory accessor.
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

#ifndef ARM_TRC_MEM_ACC_CB_H_INCLUDED
#define ARM_TRC_MEM_ACC_CB_H_INCLUDED

#include "mem_acc/trc_mem_acc_base.h"
#include "mem_acc/trc_mem_acc_cb_if.h"

class TrcMemAccCB : public TrcMemAccessorBase
{
public:
    TrcMemAccCB(const ocsd_vaddr_t s_address, 
                const ocsd_vaddr_t e_address, 
                const ocsd_mem_space_acc_t mem_space);
                

    virtual ~TrcMemAccCB() {};
    
    /** Memory access override - allow decoder to read bytes from the buffer. */
    virtual const uint32_t readBytes(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer);
    
    void setCBIfClass(TrcMemAccCBIF *p_if);
    void setCBIfFn(Fn_MemAcc_CB p_fn, const void *p_context);

private:
    TrcMemAccCBIF *m_p_CBclass;     //<! callback class.
    Fn_MemAcc_CB m_p_CBfn;          //<! callback function.
    const void *m_p_cbfn_context;   //<! context pointer for callback function.
};

inline void TrcMemAccCB::setCBIfClass(TrcMemAccCBIF *p_if) 
{ 
    m_p_CBclass = p_if; 
    m_p_CBfn = 0;       // only one callback type per accessor.
    m_p_cbfn_context = 0;
}

inline void TrcMemAccCB::setCBIfFn(Fn_MemAcc_CB p_fn, const void *p_context) 
{ 
    m_p_CBfn = p_fn;
    m_p_cbfn_context = p_context;
    m_p_CBclass = 0;  // only one callback type per accessor.
}



#endif // ARM_TRC_MEM_ACC_CB_H_INCLUDED

/* End of File trc_mem_acc_cb.h */

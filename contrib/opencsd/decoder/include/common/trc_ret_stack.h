/*
* \file       trc_ret_stack.h
* \brief      OpenCSD : trace decoder return stack feature.
*
* \copyright  Copyright (c) 2017, ARM Limited. All Rights Reserved.
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

#ifndef ARM_TRC_RET_STACK_H_INCLUDED
#define ARM_TRC_RET_STACK_H_INCLUDED

#include "opencsd/ocsd_if_types.h"

// uncomment below for return stack logging
// #define TRC_RET_STACK_DEBUG

#ifdef TRC_RET_STACK_DEBUG
class TraceComponent;
#endif

typedef struct _retStackElement
{
    ocsd_vaddr_t ret_addr;
    ocsd_isa ret_isa;
} retStackElement;

class TrcAddrReturnStack
{
public:
    TrcAddrReturnStack();
    ~TrcAddrReturnStack() {};

    void set_active(bool active)
    {
        m_active = active;
    };

    bool is_active() const 
    {
        return m_active;
    };

    void push(const ocsd_vaddr_t addr, const ocsd_isa isa);
    ocsd_vaddr_t pop(ocsd_isa &isa);
    void flush();

    bool overflow() const 
    { 
        return (bool)(num_entries < 0); 
    };

    void set_pop_pending()
    {
        if (m_active)
            m_pop_pending = true;
    }

    void clear_pop_pending()
    {
        m_pop_pending = false;
    }

    bool pop_pending() const
    {
        return m_pop_pending;
    }; 

private:
    bool m_active;
    bool m_pop_pending; // flag for decoder to indicate a pop might be needed depending on the next packet (ETMv4)

    int head_idx;
    int num_entries;
    retStackElement m_stack[16];

#ifdef TRC_RET_STACK_DEBUG
public:
    void set_dbg_logger(TraceComponent *pLogger) { m_p_debug_logger = pLogger;  };
private:
    void LogOp(const char *pszOpString, ocsd_vaddr_t addr, int head_off, ocsd_isa isa);

    TraceComponent *m_p_debug_logger;
#endif  // TRC_RET_STACK_DEBUG
};

#endif // ARM_TRC_RET_STACK_H_INCLUDED

/* End of File trc_ret_stack.h */

/*
* \file       trc_ret_stack.cpp
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
#include "common/trc_ret_stack.h"

#ifdef TRC_RET_STACK_DEBUG
#include <sstream>
#include <iostream>
#include "common/trc_component.h"

#define LOG_POP(A,O,I) LogOp("Pop",A,O,I)
#define LOG_PUSH(A,O,I) LogOp("Push",A,O,I)
#define LOG_FLUSH() LogOp("Flush",0,-1000,(const ocsd_isa)0)

// uncomment for forced std::cout log, bypassing normal library debug logger.
// useful perhaps when perf is decoding w/o printing.
// #define FORCE_STD_COUT

#else
#define LOG_POP(A,O,I) 
#define LOG_PUSH(A,O,I)
#define LOG_FLUSH()
#endif

TrcAddrReturnStack::TrcAddrReturnStack() :
    m_active(false),
    m_pop_pending(false),
    head_idx(0),
    num_entries(0)
{
#ifdef TRC_RET_STACK_DEBUG
    m_p_debug_logger = 0;
#endif
}

void TrcAddrReturnStack::push(const ocsd_vaddr_t addr, const ocsd_isa isa)
{
    if (is_active())
    {
        head_idx++;
        head_idx &= 0xF;
        m_stack[head_idx].ret_addr = addr;
        m_stack[head_idx].ret_isa = isa;
        num_entries++;
        if (num_entries > 16)
            num_entries = 16;
        LOG_PUSH(addr,0,isa);
        m_pop_pending = false; 
    }
}

ocsd_vaddr_t TrcAddrReturnStack::pop(ocsd_isa &isa)
{
    ocsd_vaddr_t addr = (ocsd_vaddr_t)-1;
    if (is_active())
    {
        if (num_entries > 0)
        {
            addr = m_stack[head_idx].ret_addr;
            isa = m_stack[head_idx].ret_isa;
            head_idx--;
            head_idx &= 0xF;
        }
        num_entries--;
        LOG_POP(addr,1,isa);
        m_pop_pending = false;
    }
    return addr;
}


void  TrcAddrReturnStack::flush()
{
    num_entries = 0;
    m_pop_pending = false;
    LOG_FLUSH();
}

#ifdef TRC_RET_STACK_DEBUG
void TrcAddrReturnStack::LogOp(const char * pszOpString, ocsd_vaddr_t addr, int head_off, ocsd_isa isa)
{
    static const char *isa_names[] =
    { 
        "A32",      /**< V7 ARM 32, V8 AArch32 */
        "T32",      /**< Thumb2 -> 16/32 bit instructions */
        "A64",      /**< V8 AArch64 */
        "TEE",      /**< Thumb EE - unsupported */
        "JZL",      /**< Jazelle - unsupported in trace */
        "custom",       /**< Instruction set - custom arch decoder */
        "unknown"       /**< ISA not yet known */
    };

    if (m_p_debug_logger)
    {
        std::ostringstream oss;
        if(head_off == -1000)
        {
            oss << "Return stack " << pszOpString << "\n";
        }
        else
        {
            int name_idx = (int)isa;
            if (name_idx > 6)
                name_idx = 6;
            oss << "Return stack " << pszOpString << "[" << std::dec << (head_idx+head_off) << "](0x" << std::hex << addr << "), " << isa_names[name_idx] << ";";
            oss << "current entries = " << std::dec << num_entries << ";";
            oss << "new head idx = " << head_idx << ";";
            oss << "pop pend (pre op) = " << (m_pop_pending ? "true\n" : "false\n");
        }
#ifdef FORCE_STD_COUT        
        std::cout << oss.str();
        std::cout.flush();
#endif       
        m_p_debug_logger->LogDefMessage(oss.str());
    }
}
#endif

/* End of File trc_ret_stack.cpp */

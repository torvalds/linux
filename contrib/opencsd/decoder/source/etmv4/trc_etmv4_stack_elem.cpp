/*
* \file       trc_etmv4_stack_elem.cpp
* \brief      OpenCSD : ETMv4 decoder
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

#include "opencsd/etmv4/trc_etmv4_stack_elem.h"

/* implementation of P0 element stack in ETM v4 trace*/
TrcStackElemParam *EtmV4P0Stack::createParamElemNoParam(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index)
{
    std::vector<uint32_t> params;
    params.clear();
    return createParamElem(p0_type, isP0, root_pkt, root_index, params);
}

TrcStackElemParam *EtmV4P0Stack::createParamElem(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const std::vector<uint32_t> &params)
{
    TrcStackElemParam *pElem = new (std::nothrow) TrcStackElemParam(p0_type, isP0, root_pkt, root_index);
    if (pElem)
    {
        int param_idx = 0;
        int params_to_fill = params.size();
        while ((param_idx < 4) && params_to_fill)
        {
            pElem->setParam(params[param_idx], param_idx);
            param_idx++;
            params_to_fill--;
        }
        push_front(pElem);
    }
    return pElem;
}

TrcStackElemAtom *EtmV4P0Stack::createAtomElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const ocsd_pkt_atom &atom)
{
    TrcStackElemAtom *pElem = new (std::nothrow) TrcStackElemAtom(root_pkt, root_index);
    if (pElem)
    {
        pElem->setAtom(atom);
        push_front(pElem);
    }
    return pElem;
}

TrcStackElemExcept *EtmV4P0Stack::createExceptElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const bool bSame, const uint16_t excepNum)
{
    TrcStackElemExcept *pElem = new (std::nothrow) TrcStackElemExcept(root_pkt, root_index);
    if (pElem)
    {
        pElem->setExcepNum(excepNum);
        pElem->setPrevSame(bSame);
        push_front(pElem);
    }
    return pElem;
}

TrcStackElemCtxt *EtmV4P0Stack::createContextElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const etmv4_context_t &context)
{
    TrcStackElemCtxt *pElem = new (std::nothrow) TrcStackElemCtxt(root_pkt, root_index);
    if (pElem)
    {
        pElem->setContext(context);
        push_front(pElem);
    }
    return pElem;

}

TrcStackElemAddr *EtmV4P0Stack::createAddrElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const etmv4_addr_val_t &addr_val)
{
    TrcStackElemAddr *pElem = new (std::nothrow) TrcStackElemAddr(root_pkt, root_index);
    if (pElem)
    {
        pElem->setAddr(addr_val);
        push_front(pElem);
    }
    return pElem;
}

/* End of file trc_etmv4_stack_elem.cpp */

/*
 * \file       ocsd_pe_context.h
 * \brief      OpenCSD : Wrapper class for PE context 
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
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
#ifndef ARM_OCSD_PE_CONTEXT_H_INCLUDED
#define ARM_OCSD_PE_CONTEXT_H_INCLUDED

#include "opencsd/ocsd_if_types.h"

/*! @class OcsdPeContext 
 *  @brief Handler for the ocsd_pe_context structure.
 *
 *  Reads and writes structure values, enforcing interaction rules between values
 *  and flags.
 */
class OcsdPeContext 
{
public:
    OcsdPeContext();
    OcsdPeContext(const ocsd_pe_context *context);
    ~OcsdPeContext() {};

    OcsdPeContext &operator =(const OcsdPeContext &ctxt);
    OcsdPeContext &operator =(const ocsd_pe_context *context);

    void resetCtxt();

    void setSecLevel(const ocsd_sec_level sl) { m_context.security_level = sl; };
    void setEL(const ocsd_ex_level el) { m_context.exception_level = el; m_context.el_valid = el > ocsd_EL_unknown ? 1 : 0; };
    void setCtxtID(const uint32_t id) { m_context.context_id = id; m_context.ctxt_id_valid = 1; };
    void setVMID(const uint32_t id) { m_context.vmid = id; m_context.vmid_valid = 1; };
    void set64bit(const bool is64bit) { m_context.bits64 = is64bit ? 1 : 0; };

    const ocsd_sec_level getSecLevel() const { return m_context.security_level; };
    const ocsd_ex_level getEL() const { return m_context.exception_level; };
    const bool ELvalid() const { return (m_context.el_valid == 1); };
    const uint32_t getCtxtID() const { return (m_context.ctxt_id_valid == 1) ? m_context.context_id : 0; };
    const bool ctxtIDvalid() const { return (m_context.ctxt_id_valid == 1); };
    const uint32_t getVMID() const { return (m_context.vmid_valid == 1) ? m_context.vmid : 0; };
    const bool VMIDvalid() const {  return (m_context.vmid_valid == 1); };

    // only allow an immutable copy of the structure out to C-API land.
    operator const ocsd_pe_context &() const { return m_context; };

private:
    ocsd_pe_context m_context;
};

inline OcsdPeContext::OcsdPeContext()
{
    resetCtxt();
}

inline OcsdPeContext::OcsdPeContext(const ocsd_pe_context *context)
{
    m_context = *context;
}

inline void OcsdPeContext::resetCtxt()
{
    // initialise the context
    m_context.bits64 = 0;
    m_context.context_id = 0;
    m_context.ctxt_id_valid = 0;
    m_context.el_valid = 0;
    m_context.exception_level = ocsd_EL_unknown;
    m_context.security_level = ocsd_sec_secure;
    m_context.vmid = 0;
    m_context.vmid_valid = 0;
}

inline OcsdPeContext & OcsdPeContext::operator =(const OcsdPeContext &ctxt)
{
    m_context = ctxt;
    return *this;
}

inline OcsdPeContext & OcsdPeContext::operator =(const ocsd_pe_context *context)
{
    m_context = *context;
    return *this;
}


#endif // ARM_OCSD_PE_CONTEXT_H_INCLUDED

/* End of File ocsd_pe_context.h */

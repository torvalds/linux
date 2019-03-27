/*!
 * \file       trc_gen_elem.h
 * \brief      OpenCSD : Decoder Generic trace element output class.
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
#ifndef ARM_TRC_GEN_ELEM_H_INCLUDED
#define ARM_TRC_GEN_ELEM_H_INCLUDED

#include "opencsd/trc_gen_elem_types.h"
#include "trc_printable_elem.h"
#include "ocsd_pe_context.h"

/** @addtogroup gen_trc_elem 
@{*/

/*!
 * @class OcsdTraceElement
 * @brief Generic trace element class
 * 
 */
class OcsdTraceElement : public trcPrintableElem, public ocsd_generic_trace_elem
{
public:
    OcsdTraceElement();
    OcsdTraceElement(ocsd_gen_trc_elem_t type);
    virtual ~OcsdTraceElement() {};

    void init();

// set elements API

    void setType(const ocsd_gen_trc_elem_t type);       //!< set type and init flags
    void updateType(const ocsd_gen_trc_elem_t type);    //!< change type only - no init

    void setContext(const ocsd_pe_context &new_context) { context = new_context; };
    void setISA(const ocsd_isa isa_update);
    
    void setCycleCount(const uint32_t cycleCount);
    void setEvent(const event_t ev_type, const uint16_t number);
    void setTS(const uint64_t ts, const bool freqChange = false);

    void setExcepMarker() { excep_data_marker = 1; };
    void setExceptionNum(uint32_t excepNum) { exception_number = excepNum; };


    void setTraceOnReason(const trace_on_reason_t reason);

    void setAddrRange(const ocsd_vaddr_t  st_addr, const ocsd_vaddr_t en_addr);
    void setLastInstrInfo(const bool exec, const ocsd_instr_type last_i_type, const ocsd_instr_subtype last_i_subtype);
    void setAddrStart(const ocsd_vaddr_t  st_addr) { this->st_addr = st_addr; };

    void setSWTInfo(const ocsd_swt_info_t swt_info) { sw_trace_info = swt_info; };
    void setExtendedDataPtr(const void *data_ptr);

// stringize the element

    virtual void toString(std::string &str) const;

// get elements API

    OcsdTraceElement &operator =(const ocsd_generic_trace_elem* p_elem);

    const ocsd_gen_trc_elem_t getType() const { return elem_type; };

    // return current context
    const ocsd_pe_context &getContext() const {  return context; };

    
private:
    void printSWInfoPkt(std::ostringstream &oss) const;
    void clearPerPktData(); //!< clear flags that indicate validity / have values on a per packet basis

};

inline OcsdTraceElement::OcsdTraceElement(ocsd_gen_trc_elem_t type)    
{
    elem_type = type;
}

inline OcsdTraceElement::OcsdTraceElement()    
{
    elem_type = OCSD_GEN_TRC_ELEM_UNKNOWN;
}

inline void OcsdTraceElement::setCycleCount(const uint32_t cycleCount)
{
    cycle_count = cycleCount;
    has_cc = 1;
}

inline void OcsdTraceElement::setEvent(const event_t ev_type, const uint16_t number)
{
    trace_event.ev_type = (uint16_t)ev_type;
    trace_event.ev_number = ev_type == EVENT_NUMBERED ? number : 0;
}

inline void OcsdTraceElement::setAddrRange(const ocsd_vaddr_t  st_addr, const ocsd_vaddr_t en_addr)
{
    this->st_addr = st_addr;
    this->en_addr = en_addr;
}

inline void OcsdTraceElement::setLastInstrInfo(const bool exec, const ocsd_instr_type last_i_type, const ocsd_instr_subtype last_i_subtype)
{
    last_instr_exec = exec ? 1 : 0;
    this->last_i_type = last_i_type;
    this->last_i_subtype = last_i_subtype;
}

inline void OcsdTraceElement::setType(const ocsd_gen_trc_elem_t type) 
{ 
    // set the type and clear down the per element flags
    elem_type = type;

    clearPerPktData();
}

inline void OcsdTraceElement::updateType(const ocsd_gen_trc_elem_t type)
{
    elem_type = type;
}

inline void OcsdTraceElement::init()
{
    st_addr = en_addr = (ocsd_vaddr_t)-1;
    isa = ocsd_isa_unknown;

    cycle_count = 0;
    timestamp = 0;

    context.ctxt_id_valid = 0;
    context.vmid_valid = 0;
    context.el_valid = 0;

    last_i_type = OCSD_INSTR_OTHER;
    last_i_subtype = OCSD_S_INSTR_NONE;

    clearPerPktData();
}

inline void OcsdTraceElement::clearPerPktData()
{
    flag_bits = 0; // union with trace_on_reason / trace_event

    ptr_extended_data = 0;  // extended data pointer
}

inline void OcsdTraceElement::setTraceOnReason(const trace_on_reason_t reason)
{
    trace_on_reason = reason;
}

inline void OcsdTraceElement::setISA(const ocsd_isa isa_update)
{
    isa = isa_update;
    if(isa > ocsd_isa_unknown)
        isa = ocsd_isa_unknown;
}

inline void OcsdTraceElement::setTS(const uint64_t ts, const bool freqChange /*= false*/) 
{ 
    timestamp = ts; 
    cpu_freq_change = freqChange ? 1 : 0; 
    has_ts = 1;
}

inline void OcsdTraceElement::setExtendedDataPtr(const void *data_ptr)
{
    extended_data = 1;
    ptr_extended_data = data_ptr;
}


/** @}*/

#endif // ARM_TRC_GEN_ELEM_H_INCLUDED

/* End of File trc_gen_elem.h */

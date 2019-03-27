/*
 * \file       trc_mem_acc_mapper.h
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

#ifndef ARM_TRC_MEM_ACC_MAPPER_H_INCLUDED
#define ARM_TRC_MEM_ACC_MAPPER_H_INCLUDED

#include <vector>

#include "opencsd/ocsd_if_types.h"
#include "interfaces/trc_tgt_mem_access_i.h"
#include "interfaces/trc_error_log_i.h"
#include "mem_acc/trc_mem_acc_base.h"

typedef enum _memacc_mapper_t {
    MEMACC_MAP_GLOBAL,
} memacc_mapper_t;

class TrcMemAccMapper : public ITargetMemAccess
{
public:
    TrcMemAccMapper();
    TrcMemAccMapper(bool using_trace_id);
    virtual ~TrcMemAccMapper();

// decoder memory access interface
    virtual ocsd_err_t ReadTargetMemory(   const ocsd_vaddr_t address, 
                                            const uint8_t cs_trace_id, 
                                            const ocsd_mem_space_acc_t mem_space, 
                                            uint32_t *num_bytes, 
                                            uint8_t *p_buffer);

// mapper memory area configuration interface

    // add an accessor to this map
    virtual ocsd_err_t AddAccessor(TrcMemAccessorBase *p_accessor, const uint8_t cs_trace_id) = 0;

    // remove a specific accessor
    virtual ocsd_err_t RemoveAccessor(const TrcMemAccessorBase *p_accessor) = 0;


    // clear all attached accessors from the map
    void RemoveAllAccessors();

    // remove a single accessor based on address.
    ocsd_err_t RemoveAccessorByAddress(const ocsd_vaddr_t st_address, const ocsd_mem_space_acc_t mem_space, const uint8_t cs_trace_id = 0);
    
    // set the error log.
    void setErrorLog(ITraceErrorLog *err_log_i) { m_err_log = err_log_i;  };

    // print out the ranges in this mapper.
    virtual void logMappedRanges() = 0;

protected:
    virtual bool findAccessor(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t cs_trace_id) = 0;     // set m_acc_curr if found valid range, leave unchanged if not.
    virtual bool readFromCurrent(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t cs_trace_id) = 0;
    virtual TrcMemAccessorBase *getFirstAccessor() = 0;
    virtual TrcMemAccessorBase *getNextAccessor() = 0;
    virtual void clearAccessorList() = 0;

    void LogMessage(const std::string &msg);

    TrcMemAccessorBase *m_acc_curr;     // most recently used - try this first.
    uint8_t m_trace_id_curr;            // trace ID for the current accessor
    const bool m_using_trace_id;        // true if we are using separate memory spaces by TraceID.
    ITraceErrorLog *m_err_log;          // error log to print out mappings on request.
};


// address spaces common to all sources using this mapper.
// trace id unused.
class TrcMemAccMapGlobalSpace : public TrcMemAccMapper
{
public:
    TrcMemAccMapGlobalSpace();
    virtual ~TrcMemAccMapGlobalSpace();

    // mapper creation interface - prevent overlaps
    virtual ocsd_err_t AddAccessor(TrcMemAccessorBase *p_accessor, const uint8_t cs_trace_id);

    // print out the ranges in this mapper.
    virtual void logMappedRanges();

protected:
    virtual bool findAccessor(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t cs_trace_id); 
    virtual bool readFromCurrent(const ocsd_vaddr_t address,const ocsd_mem_space_acc_t mem_space,  const uint8_t cs_trace_id);    
    virtual TrcMemAccessorBase *getFirstAccessor();
    virtual TrcMemAccessorBase *getNextAccessor();
    virtual void clearAccessorList();
    virtual ocsd_err_t RemoveAccessor(const TrcMemAccessorBase *p_accessor);

    std::vector<TrcMemAccessorBase *> m_acc_global;
    std::vector<TrcMemAccessorBase *>::iterator m_acc_it;
};

#endif // ARM_TRC_MEM_ACC_MAPPER_H_INCLUDED

/* End of File trc_mem_acc_mapper.h */

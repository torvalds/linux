/*
 * \file       trc_component.cpp
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

#include "common/trc_component.h"

class errLogAttachMonitor : public IComponentAttachNotifier
{
public:
    errLogAttachMonitor() 
    {
        m_pComp = 0;
    };
    virtual ~ errLogAttachMonitor() {};
    virtual void attachNotify(const int num_attached)
    {
        if(m_pComp)
            m_pComp->do_attach_notify(num_attached);
    }
    void Init(TraceComponent *pComp)
    {
        m_pComp = pComp;
        if(m_pComp)
            m_pComp->getErrorLogAttachPt()->set_notifier(this);
    }
private:
    TraceComponent *m_pComp;
};

TraceComponent::TraceComponent(const std::string &name)
{
    Init(name);
}

TraceComponent::TraceComponent(const std::string &name, int instIDNum) 
{
    std::string name_combined = name;
    char num_buffer[32];
    sprintf(num_buffer,"_%04d",instIDNum);
    name_combined += (std::string)num_buffer;
    Init(name_combined);
}

TraceComponent::~TraceComponent()
{
}

void TraceComponent::Init(const std::string &name)
{
    m_errLogHandle = OCSD_INVALID_HANDLE;
    m_errVerbosity = OCSD_ERR_SEV_NONE;
    m_name = name;

    m_supported_op_flags = 0;
    m_op_flags = 0;
    m_assocComp = 0;

    m_pErrAttachMon = new (std::nothrow) errLogAttachMonitor();
    if(m_pErrAttachMon)
        m_pErrAttachMon->Init(this);
}

void TraceComponent::LogError(const ocsdError &Error) 
{
    if((m_errLogHandle != OCSD_INVALID_HANDLE) && 
        isLoggingErrorLevel(Error.getErrorSeverity()))
    {
        // ensure we have not disabled the attachPt
        if(m_error_logger.first())
            m_error_logger.first()->LogError(m_errLogHandle,&Error);
    }
}

void TraceComponent::LogMessage(const ocsd_err_severity_t filter_level, const std::string &msg)
{
    if ((m_errLogHandle != OCSD_INVALID_HANDLE) &&
        isLoggingErrorLevel(filter_level))
    {
        // ensure we have not disabled the attachPt
        if (m_error_logger.first())
            m_error_logger.first()->LogMessage(this->m_errLogHandle, filter_level, msg);
    }

}

void TraceComponent::do_attach_notify(const int num_attached)
{
    if(num_attached)
    {
        // ensure we have not disabled the attachPt
        if(m_error_logger.first())
        {
            m_errLogHandle = m_error_logger.first()->RegisterErrorSource(m_name);
            m_errVerbosity = m_error_logger.first()->GetErrorLogVerbosity();
        }
    }
    else
    {   
        m_errLogHandle = OCSD_INVALID_HANDLE;
    }
}

void TraceComponent::updateErrorLogLevel()
{
    if(m_error_logger.first())
    {
        m_errVerbosity = m_error_logger.first()->GetErrorLogVerbosity();
    }
}

ocsd_err_t TraceComponent::setComponentOpMode(uint32_t op_flags)
{
    if( (~m_supported_op_flags & op_flags) != 0)
        return OCSD_ERR_INVALID_PARAM_VAL;
    m_op_flags = op_flags;
    return OCSD_OK;
}

/* End of File trc_component.cpp */

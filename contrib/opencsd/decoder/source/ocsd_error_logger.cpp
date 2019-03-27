/*
 * \file       ocsd_error_logger.cpp
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

#include "common/ocsd_error_logger.h"

#include <iostream>
#include <sstream>

ocsdDefaultErrorLogger::ocsdDefaultErrorLogger() :
    m_Verbosity(OCSD_ERR_SEV_ERROR),
    m_output_logger(0),
    m_created_output_logger(false)
{
    m_lastErr = 0;
    for(int i = 0; i < 0x80; i++)
        m_lastErrID[i] = 0;
    m_error_sources.push_back("Gen_Err");    // handle 0
    m_error_sources.push_back("Gen_Warn");   // handle 1
    m_error_sources.push_back("Gen_Info");   // handle 2
}

ocsdDefaultErrorLogger::~ocsdDefaultErrorLogger()
{
    if(m_created_output_logger)
        delete m_output_logger;

    if(m_lastErr)
        delete m_lastErr;

    for(int i = 0; i < 0x80; i++)
        if(m_lastErrID[i] != 0) delete m_lastErrID[i];
}

bool ocsdDefaultErrorLogger::initErrorLogger(const ocsd_err_severity_t verbosity, bool bCreateOutputLogger /*= false*/)
{
    bool bInit = true;
    m_Verbosity = verbosity;
    if(bCreateOutputLogger)
    {
        m_output_logger = new (std::nothrow) ocsdMsgLogger();
        if(m_output_logger)
        {
            m_created_output_logger = true;
            m_output_logger->setLogOpts(ocsdMsgLogger::OUT_STDERR);
        }
        else
            bInit = false;
    }
    return bInit;
}

void ocsdDefaultErrorLogger::setOutputLogger(ocsdMsgLogger *pLogger)
{
    // if we created the current logger, delete it.
    if(m_output_logger && m_created_output_logger)
        delete m_output_logger;
    m_created_output_logger = false;
    m_output_logger = pLogger;
}

const ocsd_hndl_err_log_t ocsdDefaultErrorLogger::RegisterErrorSource(const std::string &component_name)
{
    ocsd_hndl_err_log_t handle = m_error_sources.size();
    m_error_sources.push_back(component_name);
    return handle;
}

void ocsdDefaultErrorLogger::LogError(const ocsd_hndl_err_log_t handle, const ocsdError *Error)
{
    // only log errors that match or exceed the current verbosity
    if(m_Verbosity >= Error->getErrorSeverity())
    {
        // print out only if required
        if(m_output_logger)
        {
            if(m_output_logger->isLogging())
            {
                std::string errStr = "unknown";
                if(handle < m_error_sources.size())
                    errStr = m_error_sources[handle];
                errStr += " : " + ocsdError::getErrorString(Error);
                m_output_logger->LogMsg(errStr);
            }
        }

        // log last error
        if(m_lastErr == 0)
            CreateErrorObj(&m_lastErr,Error);
        else
            *m_lastErr = Error;

        // log last error associated with an ID
        if(OCSD_IS_VALID_CS_SRC_ID(Error->getErrorChanID()))
        {
            if(m_lastErrID[Error->getErrorChanID()] == 0)
                CreateErrorObj(&m_lastErrID[Error->getErrorChanID()], Error);
            else
                *m_lastErrID[Error->getErrorChanID()] = Error;
        }
    }
}

void ocsdDefaultErrorLogger::LogMessage(const ocsd_hndl_err_log_t handle, const ocsd_err_severity_t filter_level, const std::string &msg )
{
    // only log errors that match or exceed the current verbosity
    if((m_Verbosity >= filter_level))
    {
        if(m_output_logger)
        {
            if(m_output_logger->isLogging())
            {
                std::string errStr = "unknown";
                if(handle < m_error_sources.size())
                    errStr = m_error_sources[handle];
                errStr += " : " + msg;
                m_output_logger->LogMsg(errStr);
            }
        }
    }
}

void ocsdDefaultErrorLogger::CreateErrorObj(ocsdError **ppErr, const ocsdError *p_from)
{
    *ppErr = new (std::nothrow) ocsdError(p_from);
}

/* End of File ocsd_error_logger.cpp */

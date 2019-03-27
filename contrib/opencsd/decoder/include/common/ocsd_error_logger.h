/*!
 * \file       ocsd_error_logger.h
 * \brief      OpenCSD : Library error logger.
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

#ifndef ARM_OCSD_ERROR_LOGGER_H_INCLUDED
#define ARM_OCSD_ERROR_LOGGER_H_INCLUDED

#include <string>
#include <vector>
#include <fstream>

#include "interfaces/trc_error_log_i.h"
#include "ocsd_error.h"
#include "ocsd_msg_logger.h"

class ocsdDefaultErrorLogger : public ITraceErrorLog
{
public:
    ocsdDefaultErrorLogger();
    virtual ~ocsdDefaultErrorLogger();

    bool initErrorLogger(const ocsd_err_severity_t verbosity, bool bCreateOutputLogger = false);
    
    virtual ocsdMsgLogger *getOutputLogger() { return m_output_logger; };
    virtual void setOutputLogger(ocsdMsgLogger *pLogger);
    
    virtual const ocsd_hndl_err_log_t RegisterErrorSource(const std::string &component_name);

    virtual void LogError(const ocsd_hndl_err_log_t handle, const ocsdError *Error);
    virtual void LogMessage(const ocsd_hndl_err_log_t handle, const ocsd_err_severity_t filter_level, const std::string &msg );

    virtual const ocsd_err_severity_t GetErrorLogVerbosity() const { return m_Verbosity; };

    virtual ocsdError *GetLastError() { return m_lastErr; };
    virtual ocsdError *GetLastIDError(const uint8_t chan_id)
    { 
        if(OCSD_IS_VALID_CS_SRC_ID(chan_id))
            return m_lastErrID[chan_id];
        return 0;
    };
    
private:
    void CreateErrorObj(ocsdError **ppErr, const ocsdError *p_from);

    ocsdError *m_lastErr;
    ocsdError *m_lastErrID[0x80];

    ocsd_err_severity_t m_Verbosity;

    ocsdMsgLogger *m_output_logger;   // pointer to a standard message output logger;
    bool m_created_output_logger;      // true if this class created it's own logger;

    std::vector<std::string> m_error_sources;
};


#endif // ARM_OCSD_ERROR_LOGGER_H_INCLUDED

/* End of File ocsd_error_logger.h */

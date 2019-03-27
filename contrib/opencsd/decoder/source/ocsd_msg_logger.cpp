/*
 * \file       ocsd_msg_logger.cpp
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

#include "common/ocsd_msg_logger.h"

#include <iostream>
#include <sstream>

#define MSGLOG_OUT_MASK (ocsdMsgLogger::OUT_FILE | ocsdMsgLogger::OUT_STDERR | ocsdMsgLogger::OUT_STDOUT | ocsdMsgLogger::OUT_STR_CB)

ocsdMsgLogger::ocsdMsgLogger() :
    m_outFlags(ocsdMsgLogger::OUT_STDOUT),
    m_logFileName("ocsd_trace_decode.log"),
	m_pOutStrI(0)
{
}

ocsdMsgLogger::~ocsdMsgLogger()
{
    m_out_file.close();
}

void ocsdMsgLogger::setLogOpts(int logOpts)
{
    m_outFlags = logOpts & (MSGLOG_OUT_MASK);
}

void ocsdMsgLogger::setLogFileName(const char *fileName)
{
    if (fileName == 0)
        m_logFileName = "";
    else
        m_logFileName = fileName;

    if(m_out_file.is_open())
        m_out_file.close();

    if (m_logFileName.length())
        m_outFlags |= (int)ocsdMsgLogger::OUT_FILE;
    else
        m_outFlags &= ~((int)ocsdMsgLogger::OUT_FILE);
}

void ocsdMsgLogger::setStrOutFn(ocsdMsgLogStrOutI *p_IstrOut)
{
	m_pOutStrI = p_IstrOut;
    if (p_IstrOut)
        m_outFlags |= (int)ocsdMsgLogger::OUT_STR_CB;
    else
        m_outFlags &= ~((int)ocsdMsgLogger::OUT_STR_CB);
}

void ocsdMsgLogger::LogMsg(const std::string &msg)
{
    if(m_outFlags & OUT_STDOUT)
    {
        std::cout << msg;
        std::cout.flush();
    }

    if(m_outFlags & OUT_STDERR)
    {
        std::cerr << msg;
        std::cerr.flush();
    }

    if(m_outFlags & OUT_FILE)
    {
        if(!m_out_file.is_open())
        {
            m_out_file.open(m_logFileName.c_str(),std::fstream::out | std::fstream::app);
        }
        m_out_file << msg;
        m_out_file.flush();
    }

	if (m_outFlags & OUT_STR_CB)
	{
		if (m_pOutStrI)
			m_pOutStrI->printOutStr(msg);
	}
}

const bool ocsdMsgLogger::isLogging() const
{
    return (bool)((m_outFlags & MSGLOG_OUT_MASK) != 0);
}

/* End of File ocsd_msg_logger.cpp */

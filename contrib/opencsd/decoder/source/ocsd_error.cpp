/*
 * \file       ocsd_error.cpp
 * \brief      OpenCSD : Library error class.
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

#include "common/ocsd_error.h"
#include <sstream>
#include <iomanip>

static const char *s_errorCodeDescs[][2] = {
    /* general return errors */
    {"OCSD_OK", "No Error."},
    {"OCSD_ERR_FAIL","General failure."},
    {"OCSD_ERR_MEM","Internal memory allocation error."},
    {"OCSD_ERR_NOT_INIT","Component not initialised."},
    {"OCSD_ERR_INVALID_ID","Invalid CoreSight Trace Source ID."},
    {"OCSD_ERR_BAD_HANDLE","Invalid handle passed to component."},
    {"OCSD_ERR_INVALID_PARAM_VAL","Invalid value parameter passed to component."},
    {"OCSD_ERR_INVALID_PARAM_TYPE","Type mismatch on abstract interface."},
    {"OCSD_ERR_FILE_ERROR","File access error"},
    {"OCSD_ERR_NO_PROTOCOL","Trace protocol unsupported"},
    /* attachment point errors */
    {"OCSD_ERR_ATTACH_TOO_MANY","Cannot attach - attach device limit reached."},
    {"OCSD_ERR_ATTACH_INVALID_PARAM"," Cannot attach - invalid parameter."},
    {"OCSD_ERR_ATTACH_COMP_NOT_FOUND","Cannot detach - component not found."},
    /* source reader errors */
    {"OCSD_ERR_RDR_FILE_NOT_FOUND","source reader - file not found."},
    {"OCSD_ERR_RDR_INVALID_INIT",  "source reader - invalid initialisation parameter."},
    {"OCSD_ERR_RDR_NO_DECODER", "source reader - not trace decoder set."},
    /* data path errors */
    {"OCSD_ERR_DATA_DECODE_FATAL", "A decoder in the data path has returned a fatal error."},
    /* frame deformatter errors */
    {"OCSD_ERR_DFMTR_NOTCONTTRACE", "Trace input to deformatter none-continuous"},
    /* packet processor errors - protocol issues etc */
    {"OCSD_ERR_BAD_PACKET_SEQ","Bad packet sequence"},
    {"OCSD_ERR_INVALID_PCKT_HDR","Invalid packet header"},
    {"OCSD_ERR_PKT_INTERP_FAIL","Interpreter failed - cannot recover - bad data or sequence"},
    /* packet decoder errors */
    {"OCSD_ERR_UNSUPPORTED_ISA","ISA not supported in decoder"},
    {"OCSD_ERR_HW_CFG_UNSUPP","Programmed trace configuration not supported by decodUer."},
    {"OCSD_ERR_UNSUPP_DECODE_PKT","Packet not supported in decoder"},
    {"OCSD_ERR_BAD_DECODE_PKT","Reserved or unknown packet in decoder."}, 
    {"OCSD_ERR_COMMIT_PKT_OVERRUN","Overrun in commit packet stack - tried to commit more than available"},
    {"OCSD_ERR_MEM_NACC","Unable to access required memory address."},
    {"OCSD_ERR_RET_STACK_OVERFLOW","Internal return stack overflow checks failed - popped more than we pushed."},
    /* decode tree errors */
    {"OCSD_ERR_DCDT_NO_FORMATTER","No formatter in use - operation not valid."},
    /* target memory access errors */
    {"OCSD_ERR_MEM_ACC_OVERLAP","Attempted to set an overlapping range in memory access map."},
    {"OCSD_ERR_MEM_ACC_FILE_NOT_FOUND","Memory access file could not be opened."},
    {"OCSD_ERR_MEM_ACC_FILE_DIFF_RANGE","Attempt to re-use the same memory access file for a different address range."},
    {"OCSD_ERR_MEM_ACC_RANGE_INVALID","Address range in accessor set to invalid values."},
    /* test errors - errors generated only by the test code, not the library */
    {"OCSD_ERR_TEST_SNAPSHOT_PARSE", "Test snapshot file parse error"},
    {"OCSD_ERR_TEST_SNAPSHOT_PARSE_INFO", "Test snapshot file parse information"},
    {"OCSD_ERR_TEST_SNAPSHOT_READ","test snapshot reader error"},
    {"OCSD_ERR_TEST_SS_TO_DECODER","test snapshot to decode tree conversion error"},
    /* decoder registration */
    {"OCSD_ERR_DCDREG_NAME_REPEAT","Attempted to register a decoder with the same name as another one."},
    {"OCSD_ERR_DCDREG_NAME_UNKNOWN","Attempted to find a decoder with a name that is not known in the library."},
    {"OCSD_ERR_DCDREG_TYPE_UNKNOWN","Attempted to find a decoder with a type that is not known in the library."},
    /* decoder config */
    {"OCSD_ERR_DCD_INTERFACE_UNUSED","Attempt to connect or use and inteface not supported by this decoder."},
    /* end marker*/
    {"OCSD_ERR_LAST", "No error - error code end marker"}
};

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(OCSD_BAD_TRC_INDEX),
    m_chan_ID(OCSD_BAD_CS_SRC_ID)
{
}

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code, const ocsd_trc_index_t idx) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(idx),
    m_chan_ID(OCSD_BAD_CS_SRC_ID)
{
}

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code, const ocsd_trc_index_t idx, const uint8_t chan_id) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(idx),
    m_chan_ID(chan_id)
{
}

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code, const std::string &msg) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(OCSD_BAD_TRC_INDEX),
    m_chan_ID(OCSD_BAD_CS_SRC_ID),
    m_err_message(msg)
{
}

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code, const ocsd_trc_index_t idx, const std::string &msg) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(idx),
    m_chan_ID(OCSD_BAD_CS_SRC_ID),
    m_err_message(msg)
{
}

ocsdError::ocsdError(const ocsd_err_severity_t sev_type, const ocsd_err_t code, const ocsd_trc_index_t idx, const uint8_t chan_id, const std::string &msg) :
    m_error_code(code),
    m_sev(sev_type),
    m_idx(idx),
    m_chan_ID(chan_id),
    m_err_message(msg)
{
}


ocsdError::ocsdError(const ocsdError *pError) :
    m_error_code(pError->getErrorCode()),
    m_sev(pError->getErrorSeverity()),
    m_idx(pError->getErrorIndex()),
    m_chan_ID(pError->getErrorChanID())
{
    setMessage(pError->getMessage());
}

ocsdError::ocsdError(const ocsdError &Error) :
    m_error_code(Error.getErrorCode()),
    m_sev(Error.getErrorSeverity()),
    m_idx(Error.getErrorIndex()),
    m_chan_ID(Error.getErrorChanID())
{
    setMessage(Error.getMessage());
}

ocsdError::ocsdError():
    m_error_code(OCSD_ERR_LAST),
    m_sev(OCSD_ERR_SEV_NONE),
    m_idx(OCSD_BAD_TRC_INDEX),
    m_chan_ID(OCSD_BAD_CS_SRC_ID)
{
}

ocsdError::~ocsdError()
{
}

const std::string ocsdError::getErrorString(const ocsdError &error)
{
    std::string szErrStr = "LIBRARY INTERNAL ERROR: Invalid Error Object";
    const char *sev_type_sz[] = {
        "NONE ",
        "ERROR:",
        "WARN :", 
        "INFO :"
    };

    switch(error.getErrorSeverity())
    {
    default:
    case OCSD_ERR_SEV_NONE:
        break;

    case OCSD_ERR_SEV_ERROR:
    case OCSD_ERR_SEV_WARN:
    case OCSD_ERR_SEV_INFO:
        szErrStr = sev_type_sz[(int)error.getErrorSeverity()];
        appendErrorDetails(szErrStr,error);
        break;
    }
    return szErrStr;
}

void ocsdError::appendErrorDetails(std::string &errStr, const ocsdError &error)
{
    int numerrstr = ((sizeof(s_errorCodeDescs) / sizeof(const char *)) / 2);
    int code = (int)error.getErrorCode();
    ocsd_trc_index_t idx = error.getErrorIndex();
    uint8_t chan_ID = error.getErrorChanID();
    std::ostringstream oss;

    oss << "0x" << std::hex << std::setfill('0') << std::setw(4) << code;
    if(code < numerrstr)
        oss << " (" << s_errorCodeDescs[code][0] << ") [" << s_errorCodeDescs[code][1] << "]; ";
    else
        oss << " (unknown); ";

    if(idx != OCSD_BAD_TRC_INDEX)
        oss << "TrcIdx=" << std::dec << idx << "; ";

    if(chan_ID != OCSD_BAD_CS_SRC_ID)
        oss << "CS ID=" << std::hex << std::setfill('0') << std::setw(2) << (uint16_t)chan_ID << "; ";

    oss << error.getMessage();
    errStr = oss.str();
}

/* End of File ocsd_error.cpp */

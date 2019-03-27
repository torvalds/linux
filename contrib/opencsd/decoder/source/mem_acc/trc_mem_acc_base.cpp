/*!
 * \file       trc_mem_acc_base.cpp
 * \brief      OpenCSD : Trace memory accessor base class.
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

#include "mem_acc/trc_mem_acc_base.h"
#include "mem_acc/trc_mem_acc_file.h"
#include "mem_acc/trc_mem_acc_cb.h"
#include "mem_acc/trc_mem_acc_bufptr.h"

#include <sstream>
#include <iomanip>

 /** Accessor Creation */
ocsd_err_t TrcMemAccFactory::CreateBufferAccessor(TrcMemAccessorBase **pAccessor, const ocsd_vaddr_t s_address, const uint8_t *p_buffer, const uint32_t size)
{
    ocsd_err_t err = OCSD_OK;
    TrcMemAccessorBase *pAcc = 0;
    pAcc = new (std::nothrow) TrcMemAccBufPtr(s_address,p_buffer,size);
    if(pAcc == 0)
        err = OCSD_ERR_MEM;
    *pAccessor = pAcc;
    return err;
}

ocsd_err_t TrcMemAccFactory::CreateFileAccessor(TrcMemAccessorBase **pAccessor, const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset /*= 0*/, size_t size /*= 0*/)
{
    ocsd_err_t err = OCSD_OK;
    TrcMemAccessorFile *pFileAccessor = 0;
    err = TrcMemAccessorFile::createFileAccessor(&pFileAccessor, pathToFile, startAddr, offset,size);
    *pAccessor = pFileAccessor;
    return err;
}

ocsd_err_t TrcMemAccFactory::CreateCBAccessor(TrcMemAccessorBase **pAccessor, const ocsd_vaddr_t s_address, const ocsd_vaddr_t e_address, const ocsd_mem_space_acc_t mem_space)
{
    ocsd_err_t err = OCSD_OK;
    TrcMemAccessorBase *pAcc = 0;
    pAcc = new (std::nothrow)  TrcMemAccCB(s_address,e_address,mem_space);
    if(pAcc == 0)
        err = OCSD_ERR_MEM;
    *pAccessor = pAcc;
    return err;
}

/** Accessor Destruction */
void TrcMemAccFactory::DestroyAccessor(TrcMemAccessorBase *pAccessor)
{
    switch(pAccessor->getType())
    {
    case TrcMemAccessorBase::MEMACC_FILE:
        TrcMemAccessorFile::destroyFileAccessor(dynamic_cast<TrcMemAccessorFile *>(pAccessor));
        break;

    case TrcMemAccessorBase::MEMACC_CB_IF:
    case TrcMemAccessorBase::MEMACC_BUFPTR:
    delete pAccessor;
        break;

    default:
        break;
    }
}


/* memory access info logging */
void TrcMemAccessorBase::getMemAccString(std::string &accStr) const
{
    std::ostringstream oss;

    switch(m_type)
    {
    case MEMACC_FILE:
        oss << "FileAcc; Range::0x";
        break;
         
    case MEMACC_BUFPTR:
        oss << "BuffAcc; Range::0x";
        break;

    case MEMACC_CB_IF:
        oss << "CB  Acc; Range::0x";
        break;

    default: 
        oss << "UnknAcc; Range::0x";
        break;
    }
    oss << std::hex << std::setw(2) << std::setfill('0') << m_startAddress << ":" << m_endAddress;
    oss << "; Mem Space::";
    switch(m_mem_space)
    {
    case OCSD_MEM_SPACE_EL1S: oss << "EL1S"; break;
    case OCSD_MEM_SPACE_EL1N: oss << "EL1N"; break;
    case OCSD_MEM_SPACE_EL2: oss << "EL2"; break;
    case OCSD_MEM_SPACE_EL3: oss << "EL3"; break;
    case OCSD_MEM_SPACE_S: oss << "Any S"; break;
    case OCSD_MEM_SPACE_N: oss << "Any NS"; break;
    case OCSD_MEM_SPACE_ANY: oss << "Any"; break;

    default:
        {
            uint8_t MSBits = (uint8_t)m_mem_space;
            if(MSBits & (uint8_t)OCSD_MEM_SPACE_EL1S)
                oss << "EL1S,";
            if(MSBits & (uint8_t)OCSD_MEM_SPACE_EL1N)
                oss << "EL1N,";
            if(MSBits & (uint8_t)OCSD_MEM_SPACE_EL2)
                oss << "EL2,";
            if(MSBits & (uint8_t)OCSD_MEM_SPACE_EL3)
                oss << "EL3,";
        }
        break;
    }
    accStr = oss.str();
}

/* End of File trc_mem_acc_base.cpp */

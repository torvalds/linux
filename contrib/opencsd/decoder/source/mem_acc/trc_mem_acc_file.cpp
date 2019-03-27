/*
 * \file       trc_mem_acc_file.cpp
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

#include "mem_acc/trc_mem_acc_file.h"

#include <sstream>
#include <iomanip>

/***************************************************/
/* protected construction and reference counting   */
/***************************************************/

TrcMemAccessorFile::TrcMemAccessorFile() : TrcMemAccessorBase(MEMACC_FILE)
{
    m_ref_count = 0;
    m_base_range_set = false;
    m_has_access_regions = false;
    m_file_size = 0;
}

TrcMemAccessorFile::~TrcMemAccessorFile()
{
    if(m_mem_file.is_open())
        m_mem_file.close();
    if(m_access_regions.size())
    {
        std::list<FileRegionMemAccessor *>::iterator it;
        it = m_access_regions.begin();
        while(it != m_access_regions.end())
        {
            delete (*it);
            it++;
        }
        m_access_regions.clear();
    }
}

ocsd_err_t TrcMemAccessorFile::initAccessor(const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset, size_t size)
{
    ocsd_err_t err = OCSD_OK;
    bool init = false;

    m_mem_file.open(pathToFile.c_str(), std::ifstream::binary | std::ifstream::ate);
    if(m_mem_file.is_open())
    {
        m_file_size = (ocsd_vaddr_t)m_mem_file.tellg() & ((ocsd_vaddr_t)~0x1);
        m_mem_file.seekg(0, m_mem_file.beg);
        // adding an offset of 0, sets the base range.
        if((offset == 0) && (size == 0))
        {
            init = AddOffsetRange(startAddr, ((size_t)m_file_size)-offset, offset);
        }
        else if((offset + size) <= m_file_size)
        {
            // if offset != 0, size must by != 0
            init = AddOffsetRange(startAddr, size, offset);
        }
        m_file_path = pathToFile;
    }
    else 
        err = OCSD_ERR_MEM_ACC_FILE_NOT_FOUND;
    if(!init)
        err = OCSD_ERR_NOT_INIT;
    return err;
}


FileRegionMemAccessor *TrcMemAccessorFile::getRegionForAddress(const ocsd_vaddr_t startAddr) const
{
    FileRegionMemAccessor *p_region = 0;
    if(m_has_access_regions)
    {
        std::list<FileRegionMemAccessor *>::const_iterator it;
        it = m_access_regions.begin();
        while((it != m_access_regions.end()) && (p_region == 0))
        {
            if((*it)->addrInRange(startAddr))
                p_region = *it;
            it++;
        }
    }
    return p_region;
}


/***************************************************/
/* static object creation                          */
/***************************************************/

std::map<std::string, TrcMemAccessorFile *> TrcMemAccessorFile::s_FileAccessorMap;

// return existing or create new accessor
ocsd_err_t TrcMemAccessorFile::createFileAccessor(TrcMemAccessorFile **p_acc, const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset /*= 0*/, size_t size /*= 0*/)
{
    ocsd_err_t err = OCSD_OK;
    TrcMemAccessorFile * acc = 0;
    std::map<std::string, TrcMemAccessorFile *>::iterator it = s_FileAccessorMap.find(pathToFile);
    if(it != s_FileAccessorMap.end())
    {
        acc = it->second;
        if(acc->addrStartOfRange(startAddr))
            acc->IncRefCount();
        else
        {
            err = OCSD_ERR_MEM_ACC_FILE_DIFF_RANGE;
            acc = 0;
        }
    }
    else
    { 
        acc = new (std::nothrow) TrcMemAccessorFile();
        if(acc != 0)
        {
            if((err = acc->initAccessor(pathToFile,startAddr, offset,size)) == OCSD_OK)
            {
                acc->IncRefCount();
                s_FileAccessorMap.insert(std::pair<std::string, TrcMemAccessorFile *>(pathToFile,acc));
            }
            else
            {
                delete acc;
                acc = 0;
            }
        }
        else
            err = OCSD_ERR_MEM;
    }
    *p_acc = acc;
    return err;
}

void TrcMemAccessorFile::destroyFileAccessor(TrcMemAccessorFile *p_accessor)
{
    if(p_accessor != 0)
    {
        p_accessor->DecRefCount();
        if(p_accessor->getRefCount() == 0)
        {
            std::map<std::string, TrcMemAccessorFile *>::iterator it = s_FileAccessorMap.find(p_accessor->getFilePath());
            if(it != s_FileAccessorMap.end())
            {
                s_FileAccessorMap.erase(it);
            }
            delete p_accessor;
        }
    }
}

const bool TrcMemAccessorFile::isExistingFileAccessor(const std::string &pathToFile)
{
    bool bExists = false;
    std::map<std::string, TrcMemAccessorFile *>::const_iterator it = s_FileAccessorMap.find(pathToFile);
    if(it != s_FileAccessorMap.end())
        bExists = true;
    return bExists;
}

TrcMemAccessorFile * TrcMemAccessorFile::getExistingFileAccessor(const std::string &pathToFile)
{
    TrcMemAccessorFile * p_acc = 0;
    std::map<std::string, TrcMemAccessorFile *>::iterator it = s_FileAccessorMap.find(pathToFile);
    if(it != s_FileAccessorMap.end())
        p_acc = it->second;
    return p_acc;
}



/***************************************************/
/* accessor instance functions                     */
/***************************************************/
const uint32_t TrcMemAccessorFile::readBytes(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint32_t reqBytes, uint8_t *byteBuffer)
{
    if(!m_mem_file.is_open())
        return 0;
    uint32_t bytesRead = 0;

    if(m_base_range_set)
    {
        bytesRead = TrcMemAccessorBase::bytesInRange(address,reqBytes);    // get avialable bytes in range.
        if(bytesRead)
        {
            ocsd_vaddr_t addr_pos = (ocsd_vaddr_t)m_mem_file.tellg();
            if((address - m_startAddress) != addr_pos)
                m_mem_file.seekg(address - m_startAddress);
            m_mem_file.read((char *)byteBuffer,bytesRead);
        }
    }

    if((bytesRead == 0) && m_has_access_regions)
    {
        bytesRead = bytesInRange(address,reqBytes);
        if(bytesRead)
        {
            FileRegionMemAccessor *p_region = getRegionForAddress(address);
            ocsd_vaddr_t addr_pos = (ocsd_vaddr_t)m_mem_file.tellg();
            if((address - p_region->regionStartAddress() + p_region->getOffset()) != addr_pos)
                m_mem_file.seekg(address - p_region->regionStartAddress() + p_region->getOffset());
             m_mem_file.read((char *)byteBuffer,bytesRead);
        }
    }
    return bytesRead;
}

bool TrcMemAccessorFile::AddOffsetRange(const ocsd_vaddr_t startAddr, const size_t size, const size_t offset)
{
    bool addOK = false;
    if(m_file_size == 0)    // must have set the file size
        return false;
    if(addrInRange(startAddr) || addrInRange(startAddr+size-1))  // cannot be overlapping
        return false;

    // now either set the base range or an offset range
    if(offset == 0)
    {
        if(!m_base_range_set)
        {
            setRange(startAddr, startAddr+size-1);
            m_base_range_set = true;
            addOK = true;
        }
    }
    else
    {
        if((offset + size) <= m_file_size)
        {
            FileRegionMemAccessor *frmacc = new (std::nothrow) FileRegionMemAccessor();
            if(frmacc)
            {
                frmacc->setOffset(offset);
                frmacc->setRange(startAddr,startAddr+size-1);
                m_access_regions.push_back(frmacc);
                m_access_regions.sort();
                // may need to trim the 0 offset base range...
                if(m_base_range_set)
                {
                    std::list<FileRegionMemAccessor *>::iterator it;
                    it = m_access_regions.begin();
                    size_t first_range_offset = (*it)->getOffset();
                    if((m_startAddress + first_range_offset - 1) > m_endAddress)
                        m_endAddress = m_startAddress + first_range_offset - 1;
                }
                addOK = true;
                m_has_access_regions = true;
            }        
        }
    }
    return addOK;
}

const bool TrcMemAccessorFile::addrInRange(const ocsd_vaddr_t s_address) const
{
    bool bInRange = false;
    if(m_base_range_set)
        bInRange = TrcMemAccessorBase::addrInRange(s_address);

    if(!bInRange && m_has_access_regions)
    {
        if(getRegionForAddress(s_address) != 0)
            bInRange = true;
    }
    return bInRange;
}

const bool TrcMemAccessorFile::addrStartOfRange(const ocsd_vaddr_t s_address) const
{
    bool bInRange = false;
    if(m_base_range_set)
        bInRange = TrcMemAccessorBase::addrStartOfRange(s_address);
    if(!bInRange && m_has_access_regions)
    {
        FileRegionMemAccessor *pRegion = getRegionForAddress(s_address);
        if(pRegion)
            bInRange = (pRegion->regionStartAddress() == s_address);
    }
    return bInRange;
}


    /* validate ranges */
const bool TrcMemAccessorFile::validateRange()
{
    bool bRangeValid = true;
    if(m_base_range_set)
        bRangeValid = TrcMemAccessorBase::validateRange();

    if(m_has_access_regions && bRangeValid)
    {
        std::list<FileRegionMemAccessor *>::const_iterator it;
        it = m_access_regions.begin();
        while((it != m_access_regions.end()) && bRangeValid)
        {
            bRangeValid = (*it)->validateRange();
            it++;
        }
    }
    return bRangeValid;
}

const uint32_t TrcMemAccessorFile::bytesInRange(const ocsd_vaddr_t s_address, const uint32_t reqBytes) const
{
    uint32_t bytesInRange = 0;
    if(m_base_range_set)
        bytesInRange = TrcMemAccessorBase::bytesInRange(s_address,reqBytes);

    if((bytesInRange == 0) && (m_has_access_regions))
    {
        FileRegionMemAccessor *p_region = getRegionForAddress(s_address);
        bytesInRange = p_region->bytesInRange(s_address,reqBytes);
    }

    return bytesInRange;
}
    
const bool TrcMemAccessorFile::overLapRange(const TrcMemAccessorBase *p_test_acc) const
{
    bool bOverLapRange = false;
    if(m_base_range_set)
        bOverLapRange = TrcMemAccessorBase::overLapRange(p_test_acc);

    if(!bOverLapRange && (m_has_access_regions))
    {
        std::list<FileRegionMemAccessor *>::const_iterator it;
        it = m_access_regions.begin();
        while((it != m_access_regions.end()) && !bOverLapRange)
        {
            bOverLapRange = (*it)->overLapRange(p_test_acc);
            it++;
        }
    }
    return bOverLapRange;
}

    /*! Override to handle ranges and offset accessors plus add in file name. */
void TrcMemAccessorFile::getMemAccString(std::string &accStr) const
{
    std::ostringstream oss;
    accStr = "";
    if(m_base_range_set)
    {
        TrcMemAccessorBase::getMemAccString(accStr);
    }

    if(m_has_access_regions)
    {
        std::string addStr;
        std::list<FileRegionMemAccessor *>::const_iterator it;
        it = m_access_regions.begin();
        while(it != m_access_regions.end())
        {
            (*it)->getMemAccString(addStr);
            if(accStr.length())
                accStr += "\n";
            accStr += addStr;
            it++;
        }
    }
    accStr += (std::string)"\nFilename=" + m_file_path;
}

/* End of File trc_mem_acc_file.cpp */

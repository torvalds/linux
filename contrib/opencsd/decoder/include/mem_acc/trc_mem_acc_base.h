/*!
 * \file       trc_mem_acc_base.h
 * \brief      OpenCSD : Memory accessor base class.
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

#ifndef ARM_TRC_MEM_ACC_BASE_H_INCLUDED
#define ARM_TRC_MEM_ACC_BASE_H_INCLUDED

#include "opencsd/ocsd_if_types.h"
#include <string>

/*!
 * @class TrcMemAccessorBase
 * @brief Memory range to access by trace decoder.
 * 
 * Represents a memory access range for the trace decoder.
 * Range inclusive from m_startAddress to m_endAddress. 
 * e.g. a 1k range from 0x1000 has start of 0x1000 and end of 0x13FF
 *
 * Derived classes provide specific access types such as binary files and memory buffers.
 * 
 */
class TrcMemAccessorBase
{
public:

    /** Describes the storage type of the underlying memory accessor */
    enum MemAccTypes {
        MEMACC_UNKNOWN,
        MEMACC_FILE,        //<! Binary data file accessor
        MEMACC_BUFPTR,      //<! memory buffer accessor
        MEMACC_CB_IF,       //<! callback interface accessor - use for live memory access
    };

    /** default constructor */
    TrcMemAccessorBase(MemAccTypes type);
    
    /** costruct with address range values */
    TrcMemAccessorBase(MemAccTypes type, ocsd_vaddr_t startAddr, ocsd_vaddr_t endAddr);
    
    /** default desctructor */
    virtual ~TrcMemAccessorBase() {};
       
    /*!
     * Set the inclusive address range of this accessor.
     *
     * @param startAddr : start address of the range.
     * @param endAddr : end address of the range.
     */
    void setRange(ocsd_vaddr_t startAddr, ocsd_vaddr_t endAddr);

    /*!
     * test if an address is in the inclusive range for this accessor
     *
     * @param s_address : Address to test.
     *
     * @return const bool  : true if the address is in range.
     */
    virtual const bool addrInRange(const ocsd_vaddr_t s_address) const;


    /*!
     * test if an address is the start of range for this accessor
     *
     * @param s_address : Address to test.
     *
     * @return const bool  : true if the address is start of range.
     */
    virtual const bool addrStartOfRange(const ocsd_vaddr_t s_address) const;

    /*!
     * Test number of bytes available from the start address, up to the number of requested bytes.
     * Tests if all the requested bytes are available from the supplied start address.
     * Returns the number available up to full requested amount.
     *
     * @param s_address : Start address within the range.
     * @param reqBytes : Number of bytes needed from the start address.
     *
     * @return const uint32_t  : Bytes available, up to reqBytes. 0 is s_address not in range.
     */
    virtual const uint32_t bytesInRange(const ocsd_vaddr_t s_address, const uint32_t reqBytes) const;
    
    /*!
     * test is supplied range accessor overlaps this range.
     *
     * @param *p_test_acc : Accessor to test for overlap.
     *
     * @return bool  : true if overlap, false if not.
     */
    virtual const bool overLapRange(const TrcMemAccessorBase *p_test_acc) const;

    /*!
     * Read bytes from via the accessor from the memory range. 
     *
     * @param s_address : Start address of the read.
     * @param memSpace  : memory space for this access. 
     * @param reqBytes : Number of bytes required.
     * @param *byteBuffer : Buffer to copy the bytes into.
     *
     * @return uint32_t : Number of bytes read, 0 if s_address out of range, or mem space not accessible.
     */
    virtual const uint32_t readBytes(const ocsd_vaddr_t s_address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer) = 0;

    /*!
     * Validate the address range - ensure addresses aligned, different, st < en etc.
     *
     * @return bool : true if valid range.
     */
    virtual const bool validateRange();


    const enum MemAccTypes getType() const { return m_type; };

    /* handle memory spaces */
    void setMemSpace(ocsd_mem_space_acc_t memSpace) { m_mem_space = memSpace; };
    const ocsd_mem_space_acc_t getMemSpace() const { return m_mem_space; };
    const bool inMemSpace(const ocsd_mem_space_acc_t mem_space) const { return (bool)(((uint8_t)m_mem_space & (uint8_t)mem_space) != 0); }; 
    
    /* memory access info logging */
    virtual void getMemAccString(std::string &accStr) const;

protected:
    ocsd_vaddr_t m_startAddress;   /**< accessible range start address */
    ocsd_vaddr_t m_endAddress;     /**< accessible range end address */
    const MemAccTypes m_type;       /**< memory accessor type */
    ocsd_mem_space_acc_t m_mem_space;
};

inline TrcMemAccessorBase::TrcMemAccessorBase(MemAccTypes accType, ocsd_vaddr_t startAddr, ocsd_vaddr_t endAddr) :
     m_startAddress(startAddr),
     m_endAddress(endAddr),
     m_type(accType),
     m_mem_space(OCSD_MEM_SPACE_ANY)
{
}

inline TrcMemAccessorBase::TrcMemAccessorBase(MemAccTypes accType) :
     m_startAddress(0),
     m_endAddress(0),
     m_type(accType),
     m_mem_space(OCSD_MEM_SPACE_ANY)
{
}

inline void TrcMemAccessorBase::setRange(ocsd_vaddr_t startAddr, ocsd_vaddr_t endAddr)
{
     m_startAddress = startAddr;
     m_endAddress = endAddr;
}

inline const bool TrcMemAccessorBase::addrInRange(const ocsd_vaddr_t s_address) const
{
    return (s_address >= m_startAddress) && (s_address <= m_endAddress);
}

inline const bool TrcMemAccessorBase::addrStartOfRange(const ocsd_vaddr_t s_address) const
{
    return (s_address == m_startAddress);
}


inline const uint32_t TrcMemAccessorBase::bytesInRange(const ocsd_vaddr_t s_address, const uint32_t reqBytes) const
{
    ocsd_vaddr_t bytesInRange = 0;
    if(addrInRange(s_address))  // start not in range, return 0.
    {
        // bytes available till end address.
        bytesInRange = m_endAddress - s_address + 1;
        if(bytesInRange > reqBytes)
            bytesInRange = reqBytes;
    }
    return (uint32_t)bytesInRange;
}

inline const bool TrcMemAccessorBase::overLapRange(const TrcMemAccessorBase *p_test_acc) const
{
    if( addrInRange(p_test_acc->m_startAddress) || 
        addrInRange(p_test_acc->m_endAddress)
        )
        return true;
    return false;
}

inline const bool TrcMemAccessorBase::validateRange()
{
    if(m_startAddress & 0x1) // at least hword aligned for thumb
        return false;
    if((m_endAddress + 1) & 0x1)
        return false;
    if(m_startAddress == m_endAddress) // zero length range.
        return false;   
    if(m_startAddress > m_endAddress) // values bakcwards  /  invalid
        return false;
    return true;
}


class TrcMemAccFactory
{
public:
    /** Accessor Creation */
    static ocsd_err_t CreateBufferAccessor(TrcMemAccessorBase **pAccessor, const ocsd_vaddr_t s_address, const uint8_t *p_buffer, const uint32_t size);
    static ocsd_err_t CreateFileAccessor(TrcMemAccessorBase **pAccessor, const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset = 0, size_t size = 0);
    static ocsd_err_t CreateCBAccessor(TrcMemAccessorBase **pAccessor, const ocsd_vaddr_t s_address, const ocsd_vaddr_t e_address, const ocsd_mem_space_acc_t mem_space);
    
    /** Accessor Destruction */
    static void DestroyAccessor(TrcMemAccessorBase *pAccessor);
private:
    TrcMemAccFactory() {};
    ~TrcMemAccFactory() {};
};

#endif // ARM_TRC_MEM_ACC_BASE_H_INCLUDED

/* End of File trc_mem_acc_base.h */

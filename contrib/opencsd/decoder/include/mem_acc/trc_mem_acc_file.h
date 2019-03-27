/*
 * \file       trc_mem_acc_file.h
 * \brief      OpenCSD :  Access binary target memory file
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

#ifndef ARM_TRC_MEM_ACC_FILE_H_INCLUDED
#define ARM_TRC_MEM_ACC_FILE_H_INCLUDED

#include <map>
#include <string>
#include <fstream>
#include <list>

#include "opencsd/ocsd_if_types.h"
#include "mem_acc/trc_mem_acc_base.h"

// an add-on region to a file - allows setting of a region at a none-zero offset for a file.
class FileRegionMemAccessor : public TrcMemAccessorBase
{
public:
    FileRegionMemAccessor() : TrcMemAccessorBase(MEMACC_FILE) {};
    virtual ~FileRegionMemAccessor() {};

    void setOffset(const size_t offset) { m_file_offset = offset; };
    const size_t getOffset() const { return m_file_offset; };

    bool operator<(const FileRegionMemAccessor& rhs) { return this->m_startAddress < rhs.m_startAddress; };

    // not going to use these objects to read bytes - defer to the file class for that.
    virtual const uint32_t readBytes(const ocsd_vaddr_t s_address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer) { return 0; };

    const ocsd_vaddr_t regionStartAddress() const { return m_startAddress; };

private:
    size_t m_file_offset;
};

/*!
 * @class TrcMemAccessorFile   
 * @brief Memory accessor for a binary file.
 * 
 * Memory accessor based on a binary file snapshot of some memory. 
 * 
 * Static creation code to allow reference counted accessor usable for 
 * multiple access maps attached to multiple source trees for the same system.
 */
class TrcMemAccessorFile : public TrcMemAccessorBase 
{
public:
    /** read bytes override - reads from file */
    virtual const uint32_t readBytes(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t memSpace, const uint32_t reqBytes, uint8_t *byteBuffer);

protected:
    TrcMemAccessorFile();   /**< protected default constructor */
    virtual ~ TrcMemAccessorFile(); /**< protected default destructor */

    /** increment reference counter */
    void IncRefCount() { m_ref_count++; };

    /** decrement reference counter */
    void DecRefCount() { m_ref_count--; };

    /** get current reference count */
    const int getRefCount() const { return  m_ref_count; };
        
    /*!
     * Initialise accessor with file name and path, and start address.
     * File opened and length calculated to determine end address for the range.
     *
     * @param &pathToFile : Binary file path and name
     * @param startAddr : system memory address associated with start of binary datain file.
     *
     * @return bool  : true if set up successfully, false if file could not be opened.
     */
    ocsd_err_t initAccessor(const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset, size_t size);

    /** get the file path */
    const std::string &getFilePath() const { return m_file_path; };

    /** get an offset region if extant for the address */
    FileRegionMemAccessor *getRegionForAddress(const ocsd_vaddr_t startAddr) const;

    /* validate ranges */
    virtual const bool validateRange();

public:

    /*!
     * File may contain multiple none-overlapping ranges in a single file.
     *
     * @param startAddr : Address for beginning of byte data.
     * @param size   : size of range in bytes.
     * @param offset : offset into file for that data.
     *
     * @return bool  : true if set successfully.
     */
    bool AddOffsetRange(const ocsd_vaddr_t startAddr, const size_t size, const size_t offset);

    /*!
     * Override in case we have multiple regions in the file.
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

    /*! Override to handle ranges and offset accessors plus add in file name. */
    virtual void getMemAccString(std::string &accStr) const;


    /*!
     * Create a file accessor based on the supplied path and address.
     * Keeps a list of file accessors created.
     *
     * File will be checked to ensure valid accessor can be created.
     *
     * If an accessor using the supplied file is currently in use then a reference to that
     * accessor will be returned and the accessor reference counter updated.
     *
     * @param &pathToFile : Path to binary file
     * @param startAddr : Start address of data represented by file.
     *
     * @return TrcMemAccessorFile * : pointer to accessor if successful, 0 if it could not be created.
     */
    static ocsd_err_t createFileAccessor(TrcMemAccessorFile **p_acc, const std::string &pathToFile, ocsd_vaddr_t startAddr, size_t offset = 0, size_t size = 0);

    /*!
     * Destroy supplied accessor. 
     * 
     * Reference counter decremented and checked and accessor destroyed if no longer in use.
     *
     * @param *p_accessor : File Accessor to destroy.
     */
    static void destroyFileAccessor(TrcMemAccessorFile *p_accessor);

    /*!
     * Test if any accessor is currently using the supplied file path
     *
     * @param &pathToFile : Path to test.
     *
     * @return bool : true if an accessor exists with this file path.
     */
    static const bool isExistingFileAccessor(const std::string &pathToFile);

    /*!
     * Get the accessor using the supplied file path
     * Use after createFileAccessor if additional memory ranges need
     * adding to an exiting file accessor.
     *
     * @param &pathToFile : Path to test.
     *
     * @return TrcMemAccessorFile * : none 0 if an accessor exists with this file path.
     */
    static TrcMemAccessorFile * getExistingFileAccessor(const std::string &pathToFile);




private:
    static std::map<std::string, TrcMemAccessorFile *> s_FileAccessorMap;   /**< map of file accessors in use. */

private:
    std::ifstream m_mem_file;   /**< input binary file stream */
    ocsd_vaddr_t m_file_size;  /**< size of the file */
    int m_ref_count;            /**< accessor reference count */
    std::string m_file_path;    /**< path to input file */
    std::list<FileRegionMemAccessor *> m_access_regions;    /**< additional regions in the file at non-zero offsets */
    bool m_base_range_set;      /**< true when offset 0 set */
    bool m_has_access_regions;  /**< true if single file contains multiple regions */
};

#endif // ARM_TRC_MEM_ACC_FILE_H_INCLUDED

/* End of File trc_mem_acc_file.h */

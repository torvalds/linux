/*!
 * \file       ocsd_dcd_tree.h
 * \brief      OpenCSD : Trace Decode Tree.
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

#ifndef ARM_OCSD_DCD_TREE_H_INCLUDED
#define ARM_OCSD_DCD_TREE_H_INCLUDED

#include <vector>
#include <list>

#include "opencsd.h"
#include "ocsd_dcd_tree_elem.h"

/** @defgroup dcd_tree OpenCSD Library : Trace Decode Tree.
    @brief Create a multi source decode tree for a single trace capture buffer.

    Use to create a connected set of decoder objects to decode a trace buffer.
    There may be multiple trace sources within the capture buffer.

@{*/

/*!
 * @class DecodeTree
 * @brief Class to manage the decoding of data from a single trace sink .
 * 
 *  Provides functionality to build a tree of decode objects capable of decoding 
 *  multiple trace sources within a single trace sink (capture buffer).
 * 
 */
class DecodeTree : public ITrcDataIn
{
public:
/** @name Creation and Destruction
@{*/
    DecodeTree();   //!< default constructor
    ~DecodeTree();  //!< default destructor

    /*!
     * @brief Create a decode tree.
     * Automatically creates a trace frame deformatter if required and a default error log component.
     *
     * @param src_type : Data stream source type, can be CoreSight frame formatted trace, or single demuxed trace data stream,
     * @param formatterCfgFlags : Configuration flags for trace de-formatter.
     *
     * @return DecodeTree * : pointer to the decode tree, 0 if creation failed.
     */
    static DecodeTree *CreateDecodeTree(const ocsd_dcd_tree_src_t src_type, const uint32_t formatterCfgFlags);

    /** @brief Destroy a decode tree */
    static void DestroyDecodeTree(DecodeTree *p_dcd_tree);

/** @}*/


/** @name Error and element Logging
@{*/
    /** @brief The library default error logger */
    static ocsdDefaultErrorLogger* getDefaultErrorLogger() { return &s_error_logger; };

    /** the current error logging interface in use */
    static ITraceErrorLog *getCurrentErrorLogI() { return s_i_error_logger; };

    /** set an alternate error logging interface. */
    static void setAlternateErrorLogger(ITraceErrorLog *p_error_logger);

    /** get the list of packet printers for this decode tree */
    std::vector<ItemPrinter *> &getPrinterList() { return m_printer_list; };

    /** add a protocol packet printer */
    ocsd_err_t addPacketPrinter(uint8_t CSID, bool bMonitor, ItemPrinter **ppPrinter);
    
    /** add a raw frame printer */
    ocsd_err_t addRawFramePrinter(RawFramePrinter **ppPrinter, uint32_t flags);

    /** add a generic element output printer */
    ocsd_err_t addGenElemPrinter(TrcGenericElementPrinter **ppPrinter);



/** @}*/


/** @name Trace Data Path
@{*/
    /** @brief Trace Data input interface (ITrcDataIn)
    
        Decode tree implements the data in interface : ITrcDataIn .
        Captured raw trace data is passed into the deformatter and decoders via this method.
    */
    virtual ocsd_datapath_resp_t TraceDataIn( const ocsd_datapath_op_t op,
                                               const ocsd_trc_index_t index,
                                               const uint32_t dataBlockSize,
                                               const uint8_t *pDataBlock,
                                               uint32_t *numBytesProcessed);

    /*!
     * @brief Decoded Trace output.
     *
     * Client trace analysis program attaches a generic trace element interface to 
     * receive the output from the trace decode operations.
     *
     * @param *i_gen_trace_elem : Pointer to the interface.
     */
    void setGenTraceElemOutI(ITrcGenElemIn *i_gen_trace_elem);

    /*! @brief Return the connected generic element interface */
    ITrcGenElemIn *getGenTraceElemOutI() const { return m_i_gen_elem_out; };

/** @}*/

/** @name Decoder Management
@{*/

    /*!
     * Creates a decoder that is registered with the library under the supplied name.
     * createFlags determine if a full packet processor / packet decoder pair or 
     * packet processor only is created.
     * Uses the supplied configuration structure.
     *
     * @param &decoderName : registered name of decoder
     * @param createFlags :  Decoder creation options. 
     * @param *pConfig : Pointer to a valid configuration structure for the named decoder.
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t createDecoder(const std::string &decoderName, const int createFlags, const CSConfig *pConfig);

    /*  */
    /*!
     * Remove a decoder / packet processor attached to an Trace ID output on the frame de-mux.
     *
     * Once removed another decoder can be created that has a CSConfig using that ID.
     *
     * @param CSID : Trace ID to remove.
     *
     * @return ocsd_err_t  :  Library error code or OCSD_OK if successful.
     */
    ocsd_err_t removeDecoder(const uint8_t CSID);


/* get decoder elements currently in use  */

    /*!
     * Find a decode tree element associated with a specific CoreSight trace ID.   *
     */
    DecodeTreeElement *getDecoderElement(const uint8_t CSID) const;
    /* iterate decoder elements */ 

    /*!
     * Decode tree iteration. Return the first tree element 0 if no elements avaiable.
     *
     * @param &elemID : CoreSight Trace ID associated with this element
     */
    DecodeTreeElement *getFirstElement(uint8_t &elemID);
    /*!
     * Return the next tree element - or 0 if no futher elements avaiable.
     *
     * @param &elemID : CoreSight Trace ID associated with this element
     */
    DecodeTreeElement *getNextElement(uint8_t &elemID);
    
/* set key interfaces - attach / replace on any existing tree components */

    /*!
     * Set an ARM instruction opcode decoder.
     *
     * @param *i_instr_decode : Pointer to the interface. 
     */
    void setInstrDecoder(IInstrDecode *i_instr_decode);
    /*!
     * Set a target memory access interface - used to access program image memory for instruction
     * trace decode.
     *
     * @param *i_mem_access : Pointer to the interface. 
     */
    void setMemAccessI(ITargetMemAccess *i_mem_access);


/** @}*/

/** @name Memory Access Mapper

    A memory mapper is used to organise a collection of memory accessor objects that contain the 
    memory images for different areas of traced instruction memory. These areas could be the executed 
    program and a set of loaded .so libraries for example - each of which would have code sections in 
    different memory locations.

    A memory accessor represents a snapshot of an area of memory as it appeared during trace capture, 
    for a given memory space. Memory spaces are described by the ocsd_mem_space_acc_t enum. The most
    general memory space is OCSD_MEM_SPACE_ANY. This represents memory that can be secure or none-secure,
    available at any exception level. 

    The memory mapper will not allow two accessors to overlap in the same memory space. 

    The trace decdoer will access memory with a memory space parameter that represents the current core
    state - the mapper will find the closest memory space match for the address.

    e.g. if the core is accessing secure EL3, then the most specialised matching space will be accessed.
    If an EL3 space matches that will be used, otherwise the any secure, and finally _ANY.

    It is no necessary for clients to register memory accessors for all spaces - _ANY will be sufficient 
    in many cases. 


@{*/

    /*  */ 
    /*!
     * This creates a memory mapper within the decode tree.
     *
     * @param type : defaults to MEMACC_MAP_GLOBAL (only type available at present)
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t createMemAccMapper(memacc_mapper_t type = MEMACC_MAP_GLOBAL);

    /*!
     * Get a pointer to the memory mapper. Allows a client to add memory accessors directly to the mapper.
     * @return TrcMemAccMapper  : Pointer to the mapper.
     */
    TrcMemAccMapper *getMemAccMapper() const { return m_default_mapper; };

    /*!
     * Set an external mapper rather than create a mapper in the decode tree.
     * Setting this will also destroy any internal mapper that was previously created.
     *
     * @param pMapper : pointer to the mapper to add.
     */
    void setExternMemAccMapper(TrcMemAccMapper * pMapper);

    /*!
     * Return true if a mapper has been set (internal or external
     */
    const bool hasMemAccMapper() const { return (bool)(m_default_mapper != 0); };

    void logMappedRanges();     //!< Log the mapped memory ranges to the default message logger.

/** @}*/

/** @name Memory Accessors
  A memory accessor represents a snapshot of an area of memory as it appeared during trace capture.

  Memory spaces represent either common global memory, or Secure / none-secure and EL specific spaces.

@{*/

    /*!
     * Creates a memory accessor for a memory block in the supplied buffer and adds to the current mapper.
     *
     * @param address : Start address for the memory block in the memory map. 
     * @param mem_space : Memory space 
     * @param *p_mem_buffer : start of the buffer.
     * @param mem_length : length of the buffer.
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t addBufferMemAcc(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t *p_mem_buffer, const uint32_t mem_length);
    
    /*!
     * Creates a memory accessor for a memory block supplied as a contiguous binary data file, and adds to the current mapper.
     *
     * @param address : Start address for the memory block in the memory map. 
     * @param mem_space : Memory space
     * @param &filepath : Path to the binary data file
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t addBinFileMemAcc(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const std::string &filepath);
    
    /*!
     * Creates a memory accessor for a memory block supplied as a one or more memory regions in a binary file.
     * Region structures are created that describe the memory start address, the offset within the binary file
     * for that address, and the length of the region. This accessor can be used to point to the code section 
     * in a program file for example.
     *
     * @param *region_array : array of valid memory regions in the file.
     * @param num_regions : number of regions
     * @param mem_space : Memory space
     * @param &filepath : Path to the binary data file
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t addBinFileRegionMemAcc(const ocsd_file_mem_region_t *region_array, const int num_regions, const ocsd_mem_space_acc_t mem_space, const std::string &filepath);
    
    /*!
     * This memory accessor allows the client to supply a callback function for the region 
     * defined by the start and end addresses. This can be used to supply a custom memory accessor, 
     * or to directly access memory if the decode is running live on a target system.
     *
     * @param st_address : start address of region.
     * @param en_address : end address of region.
     * @param mem_space : Memory space
     * @param p_cb_func : Callback function  
     * @param *p_context : client supplied context information
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t addCallbackMemAcc(const ocsd_vaddr_t st_address, const ocsd_vaddr_t en_address, const ocsd_mem_space_acc_t mem_space, Fn_MemAcc_CB p_cb_func, const void *p_context); 
    
    /*!
     * Remove the memory accessor from the map, that begins at the given address, for the memory space provided.
     *
     * @param address : Start address of the memory accessor.
     * @param mem_space : Memory space for the memory accessor.
     *
     * @return ocsd_err_t  : Library error code or OCSD_OK if successful.
     */
    ocsd_err_t removeMemAccByAddress(const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space);

/** @}*/

/** @name CoreSight Trace Frame De-mux
@{*/

    //! Get the Trace Frame de-mux. 
    TraceFormatterFrameDecoder *getFrameDeformatter() const { return m_frame_deformatter_root; };


    /*! @brief ID filtering - sets the output filter on the trace deformatter. 

        Only supplied IDs will be decoded.

        No effect if no decoder attached for the ID 

        @param ids : Vector of CS Trace IDs
    */
    ocsd_err_t setIDFilter(std::vector<uint8_t> &ids);  // only supplied IDs will be decoded

    ocsd_err_t clearIDFilter(); //!< remove filter, all IDs will be decoded

/** @}*/

private:
    bool initialise(const ocsd_dcd_tree_src_t type, uint32_t formatterCfgFlags);
    const bool usingFormatter() const { return (bool)(m_dcd_tree_type ==  OCSD_TRC_SRC_FRAME_FORMATTED); };
    void setSingleRoot(TrcPktProcI *pComp);
    ocsd_err_t createDecodeElement(const uint8_t CSID);
    void destroyDecodeElement(const uint8_t CSID);
    void destroyMemAccMapper();

    ocsd_dcd_tree_src_t m_dcd_tree_type;

    IInstrDecode *m_i_instr_decode;
    ITargetMemAccess *m_i_mem_access;
    ITrcGenElemIn *m_i_gen_elem_out;    //!< Output interface for generic elements from decoder.

    ITrcDataIn* m_i_decoder_root;   /*!< root decoder object interface - either deformatter or single packet processor */

    TraceFormatterFrameDecoder *m_frame_deformatter_root;

    DecodeTreeElement *m_decode_elements[0x80];

    uint8_t m_decode_elem_iter;

    TrcMemAccMapper *m_default_mapper;  //!< the mem acc mapper to use
    bool m_created_mapper;              //!< true if created by decode tree object

    std::vector<ItemPrinter *> m_printer_list;  //!< list of packet printers.

    /* global error logger  - all sources */ 
    static ITraceErrorLog *s_i_error_logger;
    static std::list<DecodeTree *> s_trace_dcd_trees;

    /**! default error logger */
    static ocsdDefaultErrorLogger s_error_logger;

    /**! default instruction decoder */
    static TrcIDecode s_instruction_decoder;
};

/** @}*/

#endif // ARM_OCSD_DCD_TREE_H_INCLUDED

/* End of File ocsd_dcd_tree.h */

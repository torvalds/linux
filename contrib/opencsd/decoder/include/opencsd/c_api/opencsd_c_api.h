/*!
 * \file       opencsd_c_api.h
 * \brief      OpenCSD : "C" API
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
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

#ifndef ARM_OPENCSD_C_API_H_INCLUDED
#define ARM_OPENCSD_C_API_H_INCLUDED

/** @defgroup lib_c_api OpenCSD Library : Library "C" API.
    @brief  "C" API for the OpenCSD Library

    Set of "C" wrapper functions for the OpenCSD library.

    Defines API, functions and callback types.
@{*/

/* ensure C bindings */

#if defined(WIN32)  /* windows bindings */
    /** Building the C-API DLL **/
    #ifdef _OCSD_C_API_DLL_EXPORT
        #ifdef __cplusplus
            #define OCSD_C_API extern "C" __declspec(dllexport)
        #else
            #define OCSD_C_API __declspec(dllexport)
        #endif
    #else   
        /** building or using the static C-API library **/
        #if defined(_LIB) || defined(OCSD_USE_STATIC_C_API)
            #ifdef __cplusplus
                #define OCSD_C_API extern "C"
            #else
                #define OCSD_C_API
            #endif
        #else
        /** using the C-API DLL **/
            #ifdef __cplusplus
                #define OCSD_C_API extern "C" __declspec(dllimport)
            #else
                #define OCSD_C_API __declspec(dllimport)
            #endif
        #endif
    #endif
#else           /* linux bindings */
    #ifdef __cplusplus
        #define OCSD_C_API extern "C"
    #else
        #define OCSD_C_API
    #endif
#endif

#include "ocsd_c_api_types.h"
#include "ocsd_c_api_custom.h"

/** @name Library Version API

@{*/
/** Get Library version. Return a 32 bit version in form MMMMnnpp - MMMM = major verison, nn = minor version, pp = patch version */ 
OCSD_C_API uint32_t ocsd_get_version(void);

/** Get library version string */
OCSD_C_API const char * ocsd_get_version_str(void);
/** @}*/

/*---------------------- Trace Decode Tree ----------------------------------------------------------------------------------*/

/** @name Library Decode Tree API
@{*/

/*!
 * Create a decode tree. 
 *
 * @param src_type : Type of tree - formatted input, or single source input
 * @param deformatterCfgFlags : Formatter flags - determine presence of frame syncs etc.
 *
 * @return dcd_tree_handle_t  : Handle to the decode tree. Handle value set to 0 if creation failed.
 */
OCSD_C_API dcd_tree_handle_t ocsd_create_dcd_tree(const ocsd_dcd_tree_src_t src_type, const uint32_t deformatterCfgFlags);

/*!
 * Destroy a decode tree.
 *
 * Also destroys all the associated processors and decoders for the tree.
 *
 * @param handle : Handle for decode tree to destroy.
 */
OCSD_C_API void ocsd_destroy_dcd_tree(const dcd_tree_handle_t handle);

/*!
 * Input trace data into the decoder. 
 * 
 * Large trace source buffers can be broken down into smaller fragments.
 *
 * @param handle : Handle to decode tree.
 * @param op : Datapath operation.
 * @param index : Trace buffer byte index for the start of the supplied data block.
 * @param dataBlockSize : Size of data block.
 * @param *pDataBlock : Pointer to data block.
 * @param *numBytesProcessed : Number of bytes actually processed by the decoder.
 *
 * @return ocsd_datapath_resp_t  : Datapath response code (CONT/WAIT/FATAL)
 */
OCSD_C_API ocsd_datapath_resp_t ocsd_dt_process_data(const dcd_tree_handle_t handle,
                                            const ocsd_datapath_op_t op,
                                            const ocsd_trc_index_t index,
                                            const uint32_t dataBlockSize,
                                            const uint8_t *pDataBlock,
                                            uint32_t *numBytesProcessed);


/*---------------------- Generic Trace Element Output  --------------------------------------------------------------*/

/*!
 * Set the trace element output callback function. 
 *
 * This function will be called for each decoded generic trace element generated by 
 * any full trace decoder in the decode tree.
 *
 * A single function is used for all trace source IDs in the decode tree.
 *
 * @param handle : Handle to decode tree.
 * @param pFn : Pointer to the callback function.
 * @param p_context : opaque context pointer value used in callback function.
 *
 * @return  ocsd_err_t  : Library error code -  OCSD_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_set_gen_elem_outfn(const dcd_tree_handle_t handle, FnTraceElemIn pFn, const void *p_context);

/*---------------------- Trace Decoders ----------------------------------------------------------------------------------*/
/*!
* Creates a decoder that is registered with the library under the supplied name.
* Flags determine if a full packet processor / packet decoder pair or 
* packet processor only is created.
* Uses the supplied configuration structure.
*
* @param handle : Handle to decode tree.
* @param *decoder_name : Registered name of the decoder to create. 
* @param create_flags : Decoder creation options. 
* @param *decoder_cfg : Pointer to a valid configuration structure for the named decoder.
* @param *pCSID : Pointer to location to return the configured CoreSight trace ID for the decoder.
*
* @return ocsd_err_t  : Library error code -  OCSD_OK if successful.
*/
OCSD_C_API ocsd_err_t ocsd_dt_create_decoder(const dcd_tree_handle_t handle,
                                             const char *decoder_name,
                                             const int create_flags,
                                             const void *decoder_cfg,
                                             unsigned char *pCSID
                                             );

/*!
* Remove a decoder from the tree and destroy it.
*
* @param handle :  Handle to decode tree.
* @param CSID : Configured CoreSight trace ID for the decoder.
*
* @return ocsd_err_t  : Library error code -  OCSD_OK if successful.
*/
OCSD_C_API ocsd_err_t ocsd_dt_remove_decoder(   const dcd_tree_handle_t handle, 
                                                const unsigned char CSID);


/*!
* Attach a callback function to the packet processor.
* 
* The callback_type defines the attachment point, either the main packet output
* (only if no decoder attached), or the packet monitor.
* 
* @param handle : Handle to decode tree.
* @param CSID : Configured CoreSight trace ID for the decoder.
* @param callback_type : Attachment point 
* @param p_fn_pkt_data_in : Pointer to the callback function.
* @param p_context : Opaque context pointer value used in callback function.
*
* @return ocsd_err_t  : Library error code -  OCSD_OK if successful.
*/
OCSD_C_API ocsd_err_t ocsd_dt_attach_packet_callback(  const dcd_tree_handle_t handle, 
                                                const unsigned char CSID,
                                                const ocsd_c_api_cb_types callback_type, 
                                                void *p_fn_callback_data,
                                                const void *p_context);




 
    
/** @}*/
/*---------------------- Memory Access for traced opcodes ----------------------------------------------------------------------------------*/
/** @name Library Memory Accessor configuration on decode tree.
    @brief Configure the memory regions available for decode.
    
    Full decode requires memory regions set up to allow access to the traced
    opcodes. Add memory buffers or binary file regions to a map of regions.

@{*/

/*!
 * Add a binary file based memory range accessor to the decode tree.
 *
 * Adds the entire binary file as a memory space to be accessed
 *
 * @param handle : Handle to decode tree.
 * @param address : Start address of memory area.
 * @param mem_space : Associated memory space.
 * @param *filepath : Path to binary data file.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const char *filepath); 

/*!
 * Add a binary file based memory range accessor to the decode tree.
 * 
 * Add a binary file that contains multiple regions of memory with differing 
 * offsets wihtin the file.
 * 
 * A linked list of file_mem_region_t structures is supplied. Each structure contains an
 * offset into the binary file, the start address for this offset and the size of the region.
 * 
 * @param handle : Handle to decode tree.
 * @param region_list : Array of memory regions in the file.
 * @param num_regions : Size of region array
 * @param mem_space : Associated memory space.
 * @param *filepath : Path to binary data file.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_region_mem_acc(const dcd_tree_handle_t handle, const ocsd_file_mem_region_t *region_array, const int num_regions, const ocsd_mem_space_acc_t mem_space, const char *filepath); 

/*!
 * Add a memory buffer based memory range accessor to the decode tree.
 *
 * @param handle : Handle to decode tree.
 * @param address : Start address of memory area.
 * @param mem_space : Associated memory space.
 * @param *p_mem_buffer : pointer to memory buffer.
 * @param mem_length : Size of memory buffer.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_add_buffer_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t *p_mem_buffer, const uint32_t mem_length); 


/*!
 * Add a memory access callback function. The decoder will call the function for opcode addresses in the 
 * address range supplied for the memory spaces covered.
 *
 * @param handle : Handle to decode tree.
 * @param st_address :  Start address of memory area covered by the callback.
 * @param en_address :  End address of the memory area covered by the callback. (inclusive)
 * @param mem_space : Memory space(s) covered by the callback.
 * @param p_cb_func : Callback function
 * @param p_context : opaque context pointer value used in callback function.
 *
 * @return OCSD_C_API ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_add_callback_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_vaddr_t en_address, const ocsd_mem_space_acc_t mem_space, Fn_MemAcc_CB p_cb_func, const void *p_context); 

/*!
 * Remove a memory accessor by address and memory space.
 *
 * @param handle : Handle to decode tree.
 * @param st_address : Start address of memory accessor. 
 * @param mem_space : Memory space(s) covered by the accessor.
 *
 * @return OCSD_C_API ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_remove_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_mem_space_acc_t mem_space);

/*
 *  Print the mapped memory accessor ranges to the configured logger.
 *
 * @param handle : Handle to decode tree.
 */
OCSD_C_API void ocsd_tl_log_mapped_mem_ranges(const dcd_tree_handle_t handle);

/** @}*/  

/** @name Library Default Error Log Object API
    @brief Configure the default error logging object in the library.

    Objects created by the decode trees will use this error logger. Configure for 
    desired error severity, and to enable print or logfile output.

@{*/

/*---------------------- Library Logging and debug ----------------------------------------------------------------------------------*/
/*!
 * Initialise the library error logger. 
 *
 * Choose severity of errors logger, and if the errors will be logged to screen and / or logfile.
 *
 * @param verbosity : Severity of errors that will be logged.
 * @param create_output_logger : Set to none-zero to create an output printer.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful. 
 */
OCSD_C_API ocsd_err_t ocsd_def_errlog_init(const ocsd_err_severity_t verbosity, const int create_output_logger);

/*!
 * Configure the output logger. Choose STDOUT, STDERR and/or log to file.
 * Optionally provide a log file name.
 *
 * @param output_flags : OR combination of required  C_API_MSGLOGOUT_FLG_* flags.
 * @param *log_file_name : optional filename if logging to file. Set to NULL if not needed.
 *
 * @return OCSD_C_API ocsd_err_t  : Library error code -  RCDTL_OK if successful. 
 */
OCSD_C_API ocsd_err_t ocsd_def_errlog_config_output(const int output_flags, const char *log_file_name);

/*!
 * Configure the library default error logger to send all strings it is outputting back to the client 
 * to allow printing within the client application. This is in additional to any other log destinations
 * set in ocsd_def_errlog_init().
 *
 * @param *p_context : opaque context pointer
 * @param p_str_print_cb : client callback function to "print" logstring. 
 */
OCSD_C_API ocsd_err_t ocsd_def_errlog_set_strprint_cb(const dcd_tree_handle_t handle, void *p_context, FnDefLoggerPrintStrCB p_str_print_cb);

/*!
 * Print a message via the library output printer - if enabled.
 *
 * @param *msg : Message to output.
 *
 */
OCSD_C_API void ocsd_def_errlog_msgout(const char *msg);


/** @}*/

/** @name Packet to string interface

@{*/

/*!
 * Take a packet structure and render a string representation of the packet data.
 * 
 * Returns a '0' terminated string of (buffer_size - 1) length or less.
 *
 * @param pkt_protocol : Packet protocol type - used to interpret the packet pointer
 * @param *p_pkt : pointer to a valid packet structure of protocol type. cast to void *.
 * @param *buffer : character buffer for string.
 * @param buffer_size : size of character buffer.
 *
 * @return  ocsd_err_t  : Library error code -  RCDTL_OK if successful. 
 */
OCSD_C_API ocsd_err_t ocsd_pkt_str(const ocsd_trace_protocol_t pkt_protocol, const void *p_pkt, char *buffer, const int buffer_size);

/*!
 * Get a string representation of the generic trace element.
 *
 * @param *p_pkt : pointer to valid generic element structure.
 * @param *buffer : character buffer for string.
 * @param buffer_size : size of character buffer.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful. 
 */
OCSD_C_API ocsd_err_t ocsd_gen_elem_str(const ocsd_generic_trace_elem *p_pkt, char *buffer, const int buffer_size);


/*!
 * Init a generic element with type, clearing any flags etc.
 */
OCSD_C_API void ocsd_gen_elem_init(ocsd_generic_trace_elem *p_pkt, const ocsd_gen_trc_elem_t elem_type);

/** @}*/

/** @name Library packet and data printer control API
    @brief Allows client to use libraries packet and data printers to log packets etc rather than attach callbacks 
    to packet output and use packet to string calls.
@{*/

/*!
 * Set a raw frame printer on the trace frame demuxer. Allows inspection of raw trace data frames for debug.
 * Prints via the library default error logging mechanisms.
 *
 * The flags input determines the data printed. OR combination of one or both of:
 * OCSD_DFRMTR_PACKED_RAW_OUT   : Output the undemuxed raw data frames.
 * OCSD_DFRMTR_UNPACKED_RAW_OUT : Output the raw data by trace ID after unpacking the frame.
 *
 * @param handle : Handle to decode tree.
 * @param flags : indicates type of raw frames to print. 
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_set_raw_frame_printer(const dcd_tree_handle_t handle, int flags);

/*!
 * Set a library printer on the generic element output of a full decoder.
 *
 * @param handle : Handle to decode tree.
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_set_gen_elem_printer(const dcd_tree_handle_t handle);

/*!
 * Attach a library printer to the packet processor. May be attached to the main packet output, or the monitor
 * output if the main packet output is to be attached to a packet decoder in the datapath.
 *
 * @param handle : Handle to decode tree.
 * @param cs_id  : Coresight trace ID for stream to print.
 * @param monitor: 0 to attach printer directly to datapath packet output, 1 to attach to packet monitor output  
 *
 * @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
 */
OCSD_C_API ocsd_err_t ocsd_dt_set_pkt_protocol_printer(const dcd_tree_handle_t handle, uint8_t cs_id, int monitor);

/** @}*/


/** @name Custom Decoder API functions

@{*/

/** Register a custom decoder with the library 

    @param *name : Name under which to register the decoder.
    @param *p_dcd_fact : Custom decoder factory structure.

    @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
*/
OCSD_C_API ocsd_err_t ocsd_register_custom_decoder(const char *name, ocsd_extern_dcd_fact_t *p_dcd_fact);

/** Clear all registered decoders - library cleanup 
    
    @return ocsd_err_t  : Library error code -  RCDTL_OK if successful.
*/
OCSD_C_API ocsd_err_t ocsd_deregister_decoders(void);

/** Get a string representation of a custom protocol packet.

    Specific function to extract the packet string for a custom protocol ID only. Custom IDs are allocated to decoder factories 
    during the ocsd_register_custom_decoder() process.

    This function is called by ocsd_pkt_str() when the incoming protocol is a custom ID.

    @param pkt_protocol : Packet protocol type - must be in the custom ID range ( >= OCSD_PROTOCOL_CUSTOM_0, < OCSD_PROTOCOL_END) 
    @param *p_pkt : pointer to a valid packet structure of protocol type. cast to void *.
    @param *buffer : character buffer for string.
    @param buffer_size : size of character buffer.

    @return ocsd_err_t  : Library error code -  RCDTL_OK if successful, OCSD_ERR_NO_PROTOCOL if input ID not in custom range or not in use.
*/
OCSD_C_API ocsd_err_t ocsd_cust_protocol_to_str(const ocsd_trace_protocol_t pkt_protocol, const void *trc_pkt, char *buffer, const int buflen);

/** @}*/


/** @}*/

#endif // ARM_OPENCSD_C_API_H_INCLUDED

/* End of File opencsd_c_api.h */

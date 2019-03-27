/*!
 * \file       opencsd/ocsd_if_types.h
 * \brief      OpenCSD : Standard Types used in the library interfaces.
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

#ifndef ARM_OCSD_IF_TYPES_H_INCLUDED
#define ARM_OCSD_IF_TYPES_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#if defined(_MSC_VER) && (_MSC_VER < 1900)
/** VS2010 does not support inttypes - remove when VS2010 support is dropped */
#define __PRI64_PREFIX "ll"
#define PRIX64 __PRI64_PREFIX "X"
#define PRIu64 __PRI64_PREFIX "u"
#define PRIu32 "u"
#else
#include <inttypes.h>
#endif


/** @defgroup ocsd_interfaces OpenCSD Library : Interfaces
    @brief Set of types, structures and virtual interface classes making up the primary API

  Set of component interfaces that connect various source reader and decode components into a 
  decode tree to allow trace decode for the trace data being output by the source reader.

@{*/



/** @name Trace Indexing and Channel IDs
@{*/
#ifdef ENABLE_LARGE_TRACE_SOURCES
typedef uint64_t ocsd_trc_index_t;   /**< Trace source index type - 64 bit size */
#define OCSD_TRC_IDX_STR PRIu64
#else
typedef uint32_t ocsd_trc_index_t;   /**< Trace source index type - 32 bit size */
#define OCSD_TRC_IDX_STR PRIu32
#endif

/** Invalid trace index value */
#define OCSD_BAD_TRC_INDEX           ((ocsd_trc_index_t)-1)
/** Invalid trace source ID value */
#define OCSD_BAD_CS_SRC_ID           ((uint8_t)-1)
/** macro returing true if trace source ID is in valid range (0x0 < ID < 0x70) */
#define OCSD_IS_VALID_CS_SRC_ID(id)      ((id > 0) && (id < 0x70))
/** macro returing true if trace source ID is in reserved range (ID == 0x0 || 0x70 <= ID <= 0x7F) */
#define OCSD_IS_RESERVED_CS_SRC_ID(id)   ((id == 0) || ((id >= 0x70) && (id <= 0x7F))
/** @}*/

/** @name General Library Return and Error Codes 
@{*/

/** Library Error return type */
typedef enum _ocsd_err_t {

    /* general return errors */
    OCSD_OK = 0,                   /**< No Error. */
    OCSD_ERR_FAIL,                 /**< General systemic failure. */
    OCSD_ERR_MEM,                  /**< Internal memory allocation error. */
    OCSD_ERR_NOT_INIT,             /**< Component not initialised or initialisation failure. */
    OCSD_ERR_INVALID_ID,           /**< Invalid CoreSight Trace Source ID.  */
    OCSD_ERR_BAD_HANDLE,           /**< Invalid handle passed to component. */
    OCSD_ERR_INVALID_PARAM_VAL,    /**< Invalid value parameter passed to component. */
    OCSD_ERR_INVALID_PARAM_TYPE,   /**< Type mismatch on abstract interface */
    OCSD_ERR_FILE_ERROR,           /**< File access error */
    OCSD_ERR_NO_PROTOCOL,          /**< Trace protocol unsupported */
    /* attachment point errors */
    OCSD_ERR_ATTACH_TOO_MANY,      /**< Cannot attach - attach device limit reached. */
    OCSD_ERR_ATTACH_INVALID_PARAM, /**< Cannot attach - invalid parameter. */
    OCSD_ERR_ATTACH_COMP_NOT_FOUND,/**< Cannot detach - component not found. */
    /* source reader errors */
    OCSD_ERR_RDR_FILE_NOT_FOUND,   /**< source reader - file not found. */
    OCSD_ERR_RDR_INVALID_INIT,     /**< source reader - invalid initialisation parameter. */
    OCSD_ERR_RDR_NO_DECODER,       /**< source reader - not trace decoder set. */
    /* data path errors */
    OCSD_ERR_DATA_DECODE_FATAL,    /**< A decoder in the data path has returned a fatal error. */
    /* frame deformatter errors */
    OCSD_ERR_DFMTR_NOTCONTTRACE,    /**< Trace input to deformatter none-continuous */
    /* packet processor errors - protocol issues etc */
    OCSD_ERR_BAD_PACKET_SEQ,        /**< Bad packet sequence */
    OCSD_ERR_INVALID_PCKT_HDR,      /**< Invalid packet header */
    OCSD_ERR_PKT_INTERP_FAIL,       /**< Interpreter failed - cannot recover - bad data or sequence */
    /* packet decoder errors */
    OCSD_ERR_UNSUPPORTED_ISA,          /**< ISA not supported in decoder. */
    OCSD_ERR_HW_CFG_UNSUPP,            /**< Programmed trace configuration not supported by decoder.*/
    OCSD_ERR_UNSUPP_DECODE_PKT,        /**< Packet not supported in decoder */
    OCSD_ERR_BAD_DECODE_PKT,           /**< reserved or unknown packet in decoder. */
    OCSD_ERR_COMMIT_PKT_OVERRUN,       /**< overrun in commit packet stack - tried to commit more than available */
    OCSD_ERR_MEM_NACC,                 /**< unable to access required memory address */
    OCSD_ERR_RET_STACK_OVERFLOW,       /**< internal return stack overflow checks failed - popped more than we pushed. */
    /* decode tree errors */
    OCSD_ERR_DCDT_NO_FORMATTER,         /**< No formatter in use - operation not valid. */
    /* target memory access errors */
    OCSD_ERR_MEM_ACC_OVERLAP,           /**< Attempted to set an overlapping range in memory access map */
    OCSD_ERR_MEM_ACC_FILE_NOT_FOUND,    /**< Memory access file could not be opened */
    OCSD_ERR_MEM_ACC_FILE_DIFF_RANGE,   /**< Attempt to re-use the same memory access file for a different address range */
    OCSD_ERR_MEM_ACC_RANGE_INVALID,     /**< Address range in accessor set to invalid values */
    /* test errors - errors generated only by the test code, not the library */
    OCSD_ERR_TEST_SNAPSHOT_PARSE,       /**< test snapshot file parse error */
    OCSD_ERR_TEST_SNAPSHOT_PARSE_INFO,  /**< test snapshot file parse information */
    OCSD_ERR_TEST_SNAPSHOT_READ,        /**< test snapshot reader error */
    OCSD_ERR_TEST_SS_TO_DECODER,        /**< test snapshot to decode tree conversion error */
    /* decoder registration */
    OCSD_ERR_DCDREG_NAME_REPEAT,        /**< attempted to register a decoder with the same name as another one */
    OCSD_ERR_DCDREG_NAME_UNKNOWN,       /**< attempted to find a decoder with a name that is not known in the library */
    OCSD_ERR_DCDREG_TYPE_UNKNOWN,       /**< attempted to find a decoder with a type that is not known in the library */
    OCSD_ERR_DCDREG_TOOMANY,            /**< attempted to register too many custom decoders */
    /* decoder config */
    OCSD_ERR_DCD_INTERFACE_UNUSED,      /**< Attempt to connect or use and inteface not supported by this decoder. */
    /* end marker*/
    OCSD_ERR_LAST
} ocsd_err_t;

/* component handle types */
typedef unsigned int ocsd_hndl_rdr_t;      /**< reader control handle */
typedef unsigned int ocsd_hndl_err_log_t;  /**< error logger connection handle */

/* common invalid handle type */
#define OCSD_INVALID_HANDLE (unsigned int)-1     /**< Global invalid handle value */

/*!  Error Severity Type
 * 
 *   Used to indicate the severity of an error, and also as the 
 *   error log verbosity level in the error logger.
 *   
 *   The logger will ignore errors with a severity value higher than the 
 *   current verbosity level.
 *
 *   The value OCSD_ERR_SEV_NONE can only be used as a verbosity level to switch off logging,
 *   not as a severity value on an error. The other values can be used as both error severity and
 *   logger verbosity values.
 */
typedef enum _ocsd_err_severity_t {
    OCSD_ERR_SEV_NONE,     /**< No error logging. */
    OCSD_ERR_SEV_ERROR,    /**< Most severe error - perhaps fatal. */
    OCSD_ERR_SEV_WARN,     /**< Warning level. Inconsistent or incorrect data seen but can carry on decode processing */
    OCSD_ERR_SEV_INFO,     /**< Information only message. Use for debugging code or suspect input data. */
} ocsd_err_severity_t;

/** @}*/

/** @name Trace Datapath 
@{*/

/** Trace Datapath operations.
  */
typedef enum _ocsd_datapath_op_t {
    OCSD_OP_DATA = 0, /**< Standard index + data packet */
    OCSD_OP_EOT,   /**< End of available trace data. No data packet. */
    OCSD_OP_FLUSH, /**< Flush existing data where possible, retain decode state. No data packet. */
    OCSD_OP_RESET, /**< Reset decode state - drop any existing partial data. No data packet. */
} ocsd_datapath_op_t;

/**
  * Trace Datapath responses
  */
typedef enum _ocsd_datapath_resp_t {
    OCSD_RESP_CONT,                /**< Continue processing */
    OCSD_RESP_WARN_CONT,           /**< Continue processing  : a component logged a warning. */
    OCSD_RESP_ERR_CONT,            /**< Continue processing  : a component logged an error.*/
    OCSD_RESP_WAIT,                /**< Pause processing */
    OCSD_RESP_WARN_WAIT,           /**< Pause processing : a component logged a warning. */
    OCSD_RESP_ERR_WAIT,            /**< Pause processing : a component logged an error. */
    OCSD_RESP_FATAL_NOT_INIT,      /**< Processing Fatal Error :  component unintialised. */
    OCSD_RESP_FATAL_INVALID_OP,    /**< Processing Fatal Error :  invalid data path operation. */
    OCSD_RESP_FATAL_INVALID_PARAM, /**< Processing Fatal Error :  invalid parameter in datapath call. */
    OCSD_RESP_FATAL_INVALID_DATA,  /**< Processing Fatal Error :  invalid trace data */
    OCSD_RESP_FATAL_SYS_ERR,       /**< Processing Fatal Error :  internal system error. */
} ocsd_datapath_resp_t;

/*! Macro returning true if datapath response value is FATAL. */
#define OCSD_DATA_RESP_IS_FATAL(x) (x >= OCSD_RESP_FATAL_NOT_INIT)
/*! Macro returning true if datapath response value indicates WARNING logged. */
#define OCSD_DATA_RESP_IS_WARN(x) ((x == OCSD_RESP_WARN_CONT) || (x == OCSD_RESP_WARN_WAIT))
/*! Macro returning true if datapath response value indicates ERROR logged. */
#define OCSD_DATA_RESP_IS_ERR(x) ((x == OCSD_RESP_ERR_CONT) || (x == OCSD_RESP_ERR_WAIT))
/*! Macro returning true if datapath response value indicates WARNING or ERROR logged. */
#define OCSD_DATA_RESP_IS_WARN_OR_ERR(x) (OCSD_DATA_RESP_IS_ERR(x) || OCSD_DATA_RESP_IS_WARN(x))
/*! Macro returning true if datapath response value is CONT. */
#define OCSD_DATA_RESP_IS_CONT(x) (x <  OCSD_RESP_WAIT)
/*! Macro returning true if datapath response value is WAIT. */
#define OCSD_DATA_RESP_IS_WAIT(x) ((x >= OCSD_RESP_WAIT) && (x < OCSD_RESP_FATAL_NOT_INIT))

/** @}*/

/** @name Trace Decode component types 
@{*/


/** Raw frame element data types 
    Data blocks types output from ITrcRawFrameIn. 
*/
typedef enum _rcdtl_rawframe_elem_t {
    OCSD_FRM_NONE,     /**< None data operation on data path. (EOT etc.) */
    OCSD_FRM_PACKED,   /**< Raw packed frame data */
    OCSD_FRM_HSYNC,    /**< HSYNC data */
    OCSD_FRM_FSYNC,    /**< Frame Sync Data */
    OCSD_FRM_ID_DATA,  /**< unpacked data for ID */
} ocsd_rawframe_elem_t;


/** Indicates if the trace source will be frame formatted or a single protocol source.
    Used in decode tree creation and configuration code.
*/
typedef enum _ocsd_dcd_tree_src_t {
    OCSD_TRC_SRC_FRAME_FORMATTED,  /**< input source is frame formatted. */
    OCSD_TRC_SRC_SINGLE,           /**< input source is from a single protocol generator. */
} ocsd_dcd_tree_src_t;

#define OCSD_DFRMTR_HAS_FSYNCS         0x01 /**< Deformatter Config : formatted data has fsyncs - input data 4 byte aligned */
#define OCSD_DFRMTR_HAS_HSYNCS         0x02 /**< Deformatter Config : formatted data has hsyncs - input data 2 byte aligned */
#define OCSD_DFRMTR_FRAME_MEM_ALIGN    0x04 /**< Deformatter Config : formatted frames are memory aligned, no syncs. Input data 16 byte frame aligned. */
#define OCSD_DFRMTR_PACKED_RAW_OUT     0x08 /**< Deformatter Config : output raw packed frame data if raw monitor attached. */
#define OCSD_DFRMTR_UNPACKED_RAW_OUT   0x10 /**< Deformatter Config : output raw unpacked frame data if raw monitor attached. */
#define OCSD_DFRMTR_RESET_ON_4X_FSYNC  0x20 /**< Deformatter Config : reset downstream decoders if frame aligned 4x consecutive fsyncs spotted. (perf workaround) */
#define OCSD_DFRMTR_VALID_MASK         0x3F /**< Deformatter Config : valid mask for deformatter configuration */

#define OCSD_DFRMTR_FRAME_SIZE         0x10 /**< CoreSight frame formatter frame size constant in bytes. */

/** @}*/

/** @name Trace Decode Component Name Prefixes 
 *
 *  Set of standard prefixes to be used for component names
@{*/

/** Component name prefix for trace source reader components */
#define OCSD_CMPNAME_PREFIX_SOURCE_READER "SRDR"
/** Component name prefix for trace frame deformatter component */
#define OCSD_CMPNAME_PREFIX_FRAMEDEFORMATTER "DFMT"
/** Component name prefix for trace packet processor. */
#define OCSD_CMPNAME_PREFIX_PKTPROC "PKTP"
/** Component name prefix for trace packet decoder. */
#define OCSD_CMPNAME_PREFIX_PKTDEC   "PDEC"

/** @}*/

/** @name Trace Decode Arch and Profile 
@{*/

/** Core Architecture Version */
typedef enum _ocsd_arch_version {
    ARCH_UNKNOWN,   /**< unknown architecture */
    ARCH_V7,        /**< V7 architecture */
    ARCH_V8,        /**< V8 architecture */
    ARCH_CUSTOM,    /**< None ARM, custom architecture */
} ocsd_arch_version_t;

/** Core Profile  */
typedef enum _ocsd_core_profile {
    profile_Unknown,    /**< Unknown profile */
    profile_CortexM,    /**< Cortex-M profile */
    profile_CortexR,    /**< Cortex-R profile */
    profile_CortexA,    /**< Cortex-A profile */
    profile_Custom,     /**< None ARM, custom arch profile */
} ocsd_core_profile_t;

/** Combined architecture and profile descriptor for a core */
typedef struct _ocsd_arch_profile_t {
    ocsd_arch_version_t arch;      /**< core architecture */
    ocsd_core_profile_t profile;   /**< core profile */
} ocsd_arch_profile_t;

/* may want to use a 32 bit v-addr when running on 32 bit only ARM platforms. */
#ifdef USE_32BIT_V_ADDR
typedef uint32_t ocsd_vaddr_t;     /**< 32 bit virtual addressing in library - use if compiling on 32 bit platforms */
#define OCSD_MAX_VA_BITSIZE 32     /**< 32 bit Virtual address bitsize macro */
#define OCSD_VA_MASK ~0UL          /**< 32 bit Virtual address bitsize mask */
#else
typedef uint64_t ocsd_vaddr_t;     /**< 64 bit virtual addressing in library */
#define OCSD_MAX_VA_BITSIZE 64     /**< 64 bit Virtual address bitsize macro */
#define OCSD_VA_MASK ~0ULL         /**< 64 bit Virtual address bitsize mask */
#endif

/** A bit mask for the first 'bits' consecutive bits of an address */ 
#define OCSD_BIT_MASK(bits) (bits == OCSD_MAX_VA_BITSIZE) ? OCSD_VA_MASK : ((ocsd_vaddr_t)1 << bits) - 1

/** @}*/

/** @name Instruction Decode Information
@{*/

/** Instruction Set Architecture type
 *
 */
typedef enum _ocsd_isa
{    
    ocsd_isa_arm,          /**< V7 ARM 32, V8 AArch32 */  
    ocsd_isa_thumb2,       /**< Thumb2 -> 16/32 bit instructions */  
    ocsd_isa_aarch64,      /**< V8 AArch64 */
    ocsd_isa_tee,          /**< Thumb EE - unsupported */  
    ocsd_isa_jazelle,      /**< Jazelle - unsupported in trace */  
    ocsd_isa_custom,       /**< Instruction set - custom arch decoder */
    ocsd_isa_unknown       /**< ISA not yet known */
} ocsd_isa;

/** Security level type
*/
typedef enum _ocsd_sec_level
{
    ocsd_sec_secure,   /**< Core is in secure state */
    ocsd_sec_nonsecure /**< Core is in non-secure state */
} ocsd_sec_level ;

/** Exception level type
*/
typedef enum _ocsd_ex_level
{
    ocsd_EL_unknown = -1, /**< EL unknown / unsupported in trace */
    ocsd_EL0 = 0,  /**< EL0 */
    ocsd_EL1,      /**< EL1 */
    ocsd_EL2,      /**< EL2 */
    ocsd_EL3,      /**< EL3 */
} ocsd_ex_level;


/** instruction types - significant for waypoint calculaitons */
typedef enum _ocsd_instr_type {
    OCSD_INSTR_OTHER,          /**< Other instruction - not significant for waypoints. */
    OCSD_INSTR_BR,             /**< Immediate Branch instruction */
    OCSD_INSTR_BR_INDIRECT,    /**< Indirect Branch instruction */
    OCSD_INSTR_ISB,            /**< Barrier : ISB instruction */
    OCSD_INSTR_DSB_DMB         /**< Barrier : DSB or DMB instruction */
} ocsd_instr_type;

/** instruction sub types - addiitonal information passed to the output packets
    for trace analysis tools.
 */
typedef enum _ocsd_instr_subtype {
    OCSD_S_INSTR_NONE,         /**< no subtype set */
    OCSD_S_INSTR_BR_LINK,      /**< branch with link */
    OCSD_S_INSTR_V8_RET,       /**< v8 ret instruction - subtype of BR_INDIRECT  */
    OCSD_S_INSTR_V8_ERET,      /**< v8 eret instruction - subtype of BR_INDIRECT */
} ocsd_instr_subtype;

/** Instruction decode request structure. 
 *
 *   Used in IInstrDecode  interface.
 *
 *   Caller fills in the input: information, callee then fills in the decoder: information.
 */
typedef struct _ocsd_instr_info {
    /* input information */
    ocsd_arch_profile_t pe_type;   /**< input: Core Arch and profile */
    ocsd_isa isa;                  /**< Input: Current ISA. */
    ocsd_vaddr_t instr_addr;       /**< Input: Instruction address. */
    uint32_t opcode;                /**< Input: Opcode at address. 16 bit opcodes will use MS 16bits of parameter. */
    uint8_t dsb_dmb_waypoints;      /**< Input: DMB and DSB are waypoints */

    /* instruction decode info */
    ocsd_instr_type type;          /**< Decoder: Current instruction type. */
    ocsd_vaddr_t branch_addr;      /**< Decoder: Calculated address of branch instrcution (direct branches only) */
    ocsd_isa next_isa;             /**< Decoder: ISA for next intruction. */
    uint8_t instr_size;             /**< Decoder : size of the decoded instruction */
    uint8_t is_conditional;         /**< Decoder : set to 1 if this instruction is conditional */
    uint8_t is_link;                /**< Decoder : is a branch with link instruction */
    uint8_t thumb_it_conditions;    /**< Decoder : return number of following instructions set with conditions by this Thumb IT instruction */
    ocsd_instr_subtype sub_type;   /**< Decoder : current instruction sub-type if known */
} ocsd_instr_info;


/** Core(PE) context structure 
    records current security state, exception level, VMID and ContextID for core.
*/
typedef struct _ocsd_pe_context {    
    ocsd_sec_level security_level;     /**< security state */
    ocsd_ex_level  exception_level;    /**< exception level */
    uint32_t        context_id;         /**< context ID */
    uint32_t        vmid;               /**< VMID */
    struct {
        uint32_t bits64:1;              /**< 1 if 64 bit operation */
        uint32_t ctxt_id_valid:1;       /**< 1 if context ID value valid */
        uint32_t vmid_valid:1;          /**< 1 if VMID value is valid */
        uint32_t el_valid:1;            /**< 1 if EL value is valid (ETMv4 traces EL, other protocols do not) */
    };
} ocsd_pe_context;


/** @}*/

/** @name Opcode Memory Access
    Types used when accessing memory storage for traced opcodes..
@{*/

/** memory space bitfield enum for available security states and exception levels used 
   when accessing memory. */
typedef enum _ocsd_mem_space_acc_t {
    OCSD_MEM_SPACE_EL1S = 0x1, /**<  S EL1/0 */
    OCSD_MEM_SPACE_EL1N = 0x2, /**< NS EL1/0 */
    OCSD_MEM_SPACE_EL2 =  0x4, /**< NS EL2   */
    OCSD_MEM_SPACE_EL3 =  0x8, /**<  S EL3   */
    OCSD_MEM_SPACE_S =    0x9, /**< Any  S   */
    OCSD_MEM_SPACE_N =    0x6, /**< Any NS   */
    OCSD_MEM_SPACE_ANY =  0xF, /**< Any sec level / EL - live system use current EL + sec state */
} ocsd_mem_space_acc_t;

/**
 * Callback function definition for callback function memory accessor type.
 *
 * When using callback memory accessor, the decoder will call this function to obtain the 
 * memory at the address for the current opcodes. The memory space will represent the current 
 * exception level and security context of the traced code.
 *
 * Return the number of bytes read, which can be less than the amount requested if this would take the
 * access address outside the range of addresses defined when this callback was registered with the decoder.
 *
 * Return 0 bytes if start address out of covered range, or memory space is not one of those defined as supported
 * when the callback was registered.
 *
 * @param p_context : opaque context pointer set by callback client.
 * @param address : start address of memory to be accessed
 * @param mem_space : memory space of accessed memory (current EL & security state)
 * @param reqBytes : number of bytes required
 * @param *byteBuffer : buffer for data.
 *
 * @return uint32_t  : Number of bytes actually read, or 0 for access error.
 */
typedef uint32_t  (* Fn_MemAcc_CB)(const void *p_context, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint32_t reqBytes, uint8_t *byteBuffer);


/** memory region type for adding multi-region binary files to memory access interface */
typedef struct _ocsd_file_mem_region {
    size_t                  file_offset;    /**< Offset from start of file for memory region */
    ocsd_vaddr_t           start_address;  /**< Start address of memory region */
    size_t                  region_size;    /**< size in bytes of memory region */
} ocsd_file_mem_region_t;

/** @}*/

/** @name Packet Processor Operation Control Flags
    common operational flags - bottom 16 bits,
    component specific - top 16 bits.
@{*/

#define OCSD_OPFLG_PKTPROC_NOFWD_BAD_PKTS  0x00000001  /**< don't forward bad packets up data path */
#define OCSD_OPFLG_PKTPROC_NOMON_BAD_PKTS  0x00000002  /**< don't forward bad packets to monitor interface */
#define OCSD_OPFLG_PKTPROC_ERR_BAD_PKTS    0x00000004  /**< throw error for bad packets - halt decoding. */
#define OCSD_OPFLG_PKTPROC_UNSYNC_ON_BAD_PKTS 0x00000008  /**< switch to unsynced state on bad packets - wait for next sync point */

/** mask to combine all common packet processor operational control flags */
#define OCSD_OPFLG_PKTPROC_COMMON (OCSD_OPFLG_PKTPROC_NOFWD_BAD_PKTS | \
                                    OCSD_OPFLG_PKTPROC_NOMON_BAD_PKTS | \
                                    OCSD_OPFLG_PKTPROC_ERR_BAD_PKTS | \
                                    OCSD_OPFLG_PKTPROC_UNSYNC_ON_BAD_PKTS  )

/** @}*/

/** @name Packet Decoder Operation Control Flags
    common operational flags - bottom 16 bits,
    component specific - top 16 bits.
@{*/

#define OCSD_OPFLG_PKTDEC_ERROR_BAD_PKTS  0x00000001  /**< throw error on bad packets input (default is to unsync and wait) */

/** mask to combine all common packet processor operational control flags */
#define OCSD_OPFLG_PKTDEC_COMMON (OCSD_OPFLG_PKTDEC_ERROR_BAD_PKTS)

/** @}*/

/** @name Decoder creation information

    Flags to use when creating decoders by name

    Builtin decoder names.

    Protocol type enum.
@{*/

#define OCSD_CREATE_FLG_PACKET_PROC     0x01    /**< Create packet processor only.              */
#define OCSD_CREATE_FLG_FULL_DECODER    0x02    /**< Create packet processor + decoder pair     */
#define OCSD_CREATE_FLG_INST_ID         0x04    /**< Use instance ID in decoder instance name   */

#define OCSD_BUILTIN_DCD_STM        "STM"       /**< STM decoder */
#define OCSD_BUILTIN_DCD_ETMV3      "ETMV3"     /**< ETMv3 decoder */
#define OCSD_BUILTIN_DCD_ETMV4I     "ETMV4I"    /**< ETMv4 instruction decoder */
#define OCSD_BUILTIN_DCD_ETMV4D     "ETMV4D"    /**< ETMv4 data decoder */
#define OCSD_BUILTIN_DCD_PTM        "PTM"       /**< PTM decoder */

/*! Trace Protocol Builtin Types + extern
 */
typedef enum _ocsd_trace_protocol_t {
    OCSD_PROTOCOL_UNKNOWN = 0, /**< Protocol unknown */

/* Built in library decoders */
    OCSD_PROTOCOL_ETMV3,   /**< ETMV3 instruction and data trace protocol decoder. */
    OCSD_PROTOCOL_ETMV4I,  /**< ETMV4 instruction trace protocol decoder. */
    OCSD_PROTOCOL_ETMV4D,  /**< ETMV4 data trace protocol decoder. */
    OCSD_PROTOCOL_PTM,     /**< PTM program flow instruction trace protocol decoder. */
    OCSD_PROTOCOL_STM,     /**< STM system trace protocol decoder. */

/* others to be added here */
    OCSD_PROTOCOL_BUILTIN_END,  /**< Invalid protocol - built-in protocol types end marker */

/* Custom / external decoders */
    OCSD_PROTOCOL_CUSTOM_0 = 100,   /**< Values from this onwards are assigned to external registered decoders */
    OCSD_PROTOCOL_CUSTOM_1,
    OCSD_PROTOCOL_CUSTOM_2,
    OCSD_PROTOCOL_CUSTOM_3,
    OCSD_PROTOCOL_CUSTOM_4,
    OCSD_PROTOCOL_CUSTOM_5,
    OCSD_PROTOCOL_CUSTOM_6,
    OCSD_PROTOCOL_CUSTOM_7,
    OCSD_PROTOCOL_CUSTOM_8,
    OCSD_PROTOCOL_CUSTOM_9,

    OCSD_PROTOCOL_END      /**< Invalid protocol - protocol types end marker */
} ocsd_trace_protocol_t;

/** Test if protocol type is a library built-in decoder */
#define OCSD_PROTOCOL_IS_BUILTIN(P) ((P > OCSD_PROTOCOL_UNKNOWN) && (P < OCSD_PROTOCOL_BUILTIN_END)) 

/** Test if protocol type is a custom external registered decoder */
#define OCSD_PROTOCOL_IS_CUSTOM(P)  ((P >= OCSD_PROTOCOL_CUSTOM_0) && (P < OCSD_PROTOCOL_END ))

/** @}*/


/** @name Software Trace Packets Info

    Contains the information for the generic software trace output packet.

    Software trace packet master and channel data.
    Payload info:  
        size - packet payload size in bits;
        marker - if this packet has a marker/flag
        timestamp - if this packet has a timestamp associated
        number of packets - packet processor can optionally correlate identically 
        sized packets on the same master / channel to be output as a single generic packet
        
    Payload output as separate LE buffer, of sufficient bytes to hold all the packets.
@{*/

typedef struct _ocsd_swt_info {
    uint16_t swt_master_id;
    uint16_t swt_channel_id;
    union {
        struct {
            uint32_t swt_payload_pkt_bitsize:8; /**< [bits 0:7 ] Packet size in bits of the payload packets */
            uint32_t swt_payload_num_packets:8; /**< [bits 8:15 ] number of consecutive packets of this type in the payload data */
            uint32_t swt_marker_packet:1;       /**< [bit 16 ] packet is marker / flag packet */ 
            uint32_t swt_has_timestamp:1;       /**< [bit 17 ] packet has timestamp. */
            uint32_t swt_marker_first:1;        /**< [bit 18 ] for multiple packet payloads, this indicates if any marker is on first or last packet */
            uint32_t swt_master_err:1;          /**< [bit 19 ] current master has error - payload is error code */
            uint32_t swt_global_err:1;          /**< [bit 20 ] global error - payload is error code - master and channel ID not valid */
            uint32_t swt_trigger_event:1;       /**< [bit 21 ] trigger event packet - payload is event number */
            uint32_t swt_frequency:1;           /**< [bit 22 ] frequency packet - payload is frequency */
            uint32_t swt_id_valid:1;            /**< [bit 23 ] master & channel ID has been set by input stream  */
        };
        uint32_t swt_flag_bits;
    };
} ocsd_swt_info_t;

/** mask for the swt_id_valid flag - need to retain between packets */
#define SWT_ID_VALID_MASK (0x1 << 23)

/** @}*/

/** @}*/
#endif // ARM_OCSD_IF_TYPES_H_INCLUDED

/* End of File opencsd/ocsd_if_types.h */

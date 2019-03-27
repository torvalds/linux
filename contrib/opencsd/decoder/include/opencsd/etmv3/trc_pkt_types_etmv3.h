/*
 * \file       trc_pkt_types_etmv3.h
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

#ifndef ARM_TRC_ETM3_PKT_TYPES_ETMV3_H_INCLUDED
#define ARM_TRC_ETM3_PKT_TYPES_ETMV3_H_INCLUDED

#include "opencsd/trc_pkt_types.h"

/** @addtogroup trc_pkts
@{*/

/** @name ETMv3 Packet Types
@{*/

typedef enum _ocsd_etmv3_pkt_type
{

// markers for unknown packets
        ETM3_PKT_NOERROR,        //!< no error in packet - supplimentary data.
		ETM3_PKT_NOTSYNC,        //!< no sync found yet
        ETM3_PKT_INCOMPLETE_EOT, //!< flushing incomplete/empty packet at end of trace.

// markers for valid packets
		ETM3_PKT_BRANCH_ADDRESS,
		ETM3_PKT_A_SYNC,
		ETM3_PKT_CYCLE_COUNT,
		ETM3_PKT_I_SYNC,
		ETM3_PKT_I_SYNC_CYCLE,
        ETM3_PKT_TRIGGER,
        ETM3_PKT_P_HDR,
		ETM3_PKT_STORE_FAIL,
		ETM3_PKT_OOO_DATA,
		ETM3_PKT_OOO_ADDR_PLC,
		ETM3_PKT_NORM_DATA,
		ETM3_PKT_DATA_SUPPRESSED,
		ETM3_PKT_VAL_NOT_TRACED,
		ETM3_PKT_IGNORE,
		ETM3_PKT_CONTEXT_ID,
        ETM3_PKT_VMID,
		ETM3_PKT_EXCEPTION_ENTRY,
		ETM3_PKT_EXCEPTION_EXIT,		
		ETM3_PKT_TIMESTAMP,

// internal processing types
		ETM3_PKT_BRANCH_OR_BYPASS_EOT,

// packet errors 
		ETM3_PKT_BAD_SEQUENCE,   //!< invalid sequence for packet type
        ETM3_PKT_BAD_TRACEMODE,  //!< invalid packet type for this trace mode.
		ETM3_PKT_RESERVED       //!< packet type reserved.

} ocsd_etmv3_pkt_type;

typedef struct _ocsd_etmv3_excep {
    ocsd_armv7_exception type; /**<  exception type. */
    uint16_t number;    /**< exception as number */
    struct {
        uint32_t present:1;      /**< exception present in packet */
        uint32_t cancel:1;       /**< exception cancels prev instruction traced. */
        uint32_t cm_type:1;
        uint32_t cm_resume:4;    /**< M class resume code */
        uint32_t cm_irq_n:9;     /**< M class IRQ n */
    } bits;
} ocsd_etmv3_excep;

typedef struct _etmv3_context_t {
    struct {
        uint32_t curr_alt_isa:1;     /**< current Alt ISA flag for Tee / T32 (used if not in present packet) */
        uint32_t curr_NS:1;          /**< current NS flag  (used if not in present packet) */
        uint32_t curr_Hyp:1;         /**< current Hyp flag  (used if not in present packet) */
        uint32_t updated:1;          /**< context updated */
        uint32_t updated_c:1;        /**< updated CtxtID */
        uint32_t updated_v:1;        /**< updated VMID */
    };
    uint32_t ctxtID;    /**< Context ID */
    uint8_t VMID;       /**< VMID */
} etmv3_context_t;


typedef struct _etmv3_data_t {

    uint32_t value;        /**< Data value */
    ocsd_pkt_vaddr addr;  /**< current data address */

    struct {
    uint32_t  ooo_tag:2;        /**< Out of order data tag. */
    uint32_t  be:1;             /**< data transfers big-endian */
    uint32_t  update_be:1;      /**< updated Be flag */
    uint32_t  update_addr:1;    /**< updated address */
    uint32_t  update_dval:1;    /**< updated data value */
    };
} etmv3_data_t;

typedef struct _etmv3_isync_t {
    ocsd_iSync_reason reason;
    struct {
        uint32_t has_cycle_count:1; /**< updated cycle count */
        uint32_t has_LSipAddress:1; /**< main address is load-store instuction, data address is overlapping instruction @ start of trace */
        uint32_t no_address:1;      /**< data only ISync */
    };
} etmv3_isync_t;

typedef struct _ocsd_etmv3_pkt
{
    ocsd_etmv3_pkt_type type;  /**< Primary packet type. */

    ocsd_isa curr_isa;         /**< current ISA */
    ocsd_isa prev_isa;         /**< ISA in previous packet */

    etmv3_context_t context;   /**< current context */
    ocsd_pkt_vaddr addr;       /**< current Addr */

    etmv3_isync_t isync_info;
    
    ocsd_etmv3_excep exception;
    
    ocsd_pkt_atom atom;         /**< atom elements - non zerom number indicates valid atom count */
    uint8_t p_hdr_fmt;          /**< if atom elements, associated phdr format */
    uint32_t cycle_count;       /**< cycle count associated with this packet (ETMv3 has counts in atom packets and as individual packets */
    
    uint64_t timestamp;         /**< current timestamp value */
    uint8_t ts_update_bits;     /**< bits of ts updated this packet (if TS packet) */
    
    etmv3_data_t data;          /**< data transfer values */

    ocsd_etmv3_pkt_type err_type;  /**< Basic packet type if primary type indicates error or incomplete. (header type) */

} ocsd_etmv3_pkt;

typedef struct _ocsd_etmv3_cfg 
{
    uint32_t               reg_idr;    /**< ID register */
    uint32_t               reg_ctrl;   /**< Control Register */
    uint32_t               reg_ccer;   /**< CCER register */
    uint32_t               reg_trc_id; /**< Trace Stream ID register */
    ocsd_arch_version_t    arch_ver;   /**< Architecture version */
    ocsd_core_profile_t    core_prof;  /**< Core Profile */
} ocsd_etmv3_cfg;


#define DATA_ADDR_EXPECTED_FLAG 0x20 /**< Bit set for data trace headers if data address packets follow */

/** @}*/
/** @}*/
#endif // ARM_TRC_ETM3_PKT_TYPES_ETMV3_H_INCLUDED

/* End of File trc_pkt_types_etmv3.h */

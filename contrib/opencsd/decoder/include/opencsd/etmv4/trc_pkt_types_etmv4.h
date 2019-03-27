/*
 * \file       trc_pkt_types_etmv4.h
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

#ifndef ARM_TRC_PKT_TYPES_ETMV4_H_INCLUDED
#define ARM_TRC_PKT_TYPES_ETMV4_H_INCLUDED

#include "opencsd/trc_pkt_types.h"

/** @addtogroup trc_pkts
@{*/

/** @name ETMv4 Packet Types
@{*/

/** I stream packets. */
typedef enum _ocsd_etmv4_i_pkt_type
{
/* state of decode markers */
		ETM4_PKT_I_NOTSYNC = 0x200,             /*!< no sync found yet.  */
        ETM4_PKT_I_INCOMPLETE_EOT,              /*!< flushing incomplete/empty packet at end of trace.*/
        ETM4_PKT_I_NO_ERR_TYPE,                 /*!< error type not set for packet. */

/* markers for unknown/bad packets */
		ETM4_PKT_I_BAD_SEQUENCE = 0x300,        /*!< invalid sequence for packet type. */
        ETM4_PKT_I_BAD_TRACEMODE,               /*!< invalid packet type for this trace mode. */
		ETM4_PKT_I_RESERVED,                    /*!< packet type reserved. */

/* I stream packet types. */
    /* extension header. */
        ETM4_PKT_I_EXTENSION = 0x00,            /*!< b00000000  */

    /* address amd context */
        ETM4_PKT_I_ADDR_CTXT_L_32IS0 = 0x82,    /*!< b10000010  */
        ETM4_PKT_I_ADDR_CTXT_L_32IS1,           /*!< b10000011  */
        /* unused encoding                           b10000100  */
        ETM4_PKT_I_ADDR_CTXT_L_64IS0 = 0x85,    /*!< b10000101  */
        ETM4_PKT_I_ADDR_CTXT_L_64IS1,           /*!< b10000110  */
        /* unused encoding                           b10000111  */
        ETM4_PKT_I_CTXT = 0x80,                 /*!< b1000000x  */
        ETM4_PKT_I_ADDR_MATCH = 0x90,           /*!< b10010000 to b10010010 */
        ETM4_PKT_I_ADDR_L_32IS0 = 0x9A,         /*!< b10011010  */
        ETM4_PKT_I_ADDR_L_32IS1,                /*!< b10011011  */
        /* unused encoding                           b10011100  */
        ETM4_PKT_I_ADDR_L_64IS0 = 0x9D,         /*!< b10011101  */
        ETM4_PKT_I_ADDR_L_64IS1,                /*!< b10011110  */
        /* unused encoding                           b10011111  */
        ETM4_PKT_I_ADDR_S_IS0 = 0x95,           /*!< b10010101  */
        ETM4_PKT_I_ADDR_S_IS1,                  /*!< b10010110  */
        /* unused encoding                           b10010111  
           unused encoding                           b10011000
           unused encoding                           b10011001 */

    /* Q packets */
        ETM4_PKT_I_Q = 0xA0,                    /*!< b1010xxxx  */

    /* Atom packets */
        ETM4_PKT_I_ATOM_F1 = 0xF6,              /*!< b1111011x */
        ETM4_PKT_I_ATOM_F2 = 0xD8,              /*!< b110110xx */
        ETM4_PKT_I_ATOM_F3 = 0xF8,              //!< b11111xxx
        ETM4_PKT_I_ATOM_F4 = 0xDC,              //!< b110111xx
        ETM4_PKT_I_ATOM_F5 = 0xD5,              //!< b11010101 - b11010111, b11110101
        ETM4_PKT_I_ATOM_F6 = 0xC0,              //!< b11000000 - b11010100, b11100000 - b11110100

    /* conditional instruction tracing */
        ETM4_PKT_I_COND_FLUSH = 0x43,           //!< b01000011
        ETM4_PKT_I_COND_I_F1 = 0x6C,            //!< b01101100
        ETM4_PKT_I_COND_I_F2 = 0x40,            //!< b01000000 - b01000010
        ETM4_PKT_I_COND_I_F3 = 0x6D,            //!< b01101101
        ETM4_PKT_I_COND_RES_F1 = 0x68,          //!< b0110111x, b011010xx
        ETM4_PKT_I_COND_RES_F2 = 0x48,          //!< b0100100x, b01001010, b0100110x, b01001110 
        ETM4_PKT_I_COND_RES_F3 = 0x50,          //!< b0101xxxx
        ETM4_PKT_I_COND_RES_F4 = 0x44,          //!< b0100010x, b01000110

    /* cycle count packets */
        ETM4_PKT_I_CCNT_F1 = 0x0E,             //!< b0000111x
        ETM4_PKT_I_CCNT_F2 = 0x0C,             //!< b0000110x
        ETM4_PKT_I_CCNT_F3 = 0x10,             //!< b0001xxxx
    // data synchronisation markers
        ETM4_PKT_I_NUM_DS_MKR = 0x20,          //!< b00100xxx
        ETM4_PKT_I_UNNUM_DS_MKR = 0x28,        //!< b00101000 - b00101100
    // event trace
        ETM4_PKT_I_EVENT = 0x70,               //!< b0111xxxx
    // Exceptions
        ETM4_PKT_I_EXCEPT = 0x06,              //!< b00000110
        ETM4_PKT_I_EXCEPT_RTN = 0x07,          //!< b00000111
    // timestamp
        ETM4_PKT_I_TIMESTAMP = 0x02,           //!< b0000001x
    // speculation 
        ETM4_PKT_I_CANCEL_F1 = 0x2E,           //!< b0010111x
        ETM4_PKT_I_CANCEL_F2 = 0x34,           //!< b001101xx
        ETM4_PKT_I_CANCEL_F3 = 0x38,           //!< b00111xxx
        ETM4_PKT_I_COMMIT = 0x2D,              //!< b00101101
        ETM4_PKT_I_MISPREDICT = 0x30,          //!< b001100xx
    // Sync
        ETM4_PKT_I_TRACE_INFO = 0x01,          //!< b00000001
        ETM4_PKT_I_TRACE_ON = 0x04,            //!< b00000100    
    // extension packets - follow 0x00 header
        ETM4_PKT_I_ASYNC = 0x100,              //!< b00000000
        ETM4_PKT_I_DISCARD = 0x103,            //!< b00000011
        ETM4_PKT_I_OVERFLOW = 0x105            //!< b00000101

} ocsd_etmv4_i_pkt_type;

typedef union _etmv4_trace_info_t {
    uint32_t val;   //!< trace info full value.
    struct {
        uint32_t cc_enabled:1;      //!< 1 if cycle count enabled
        uint32_t cond_enabled:3;    //!< conditional trace enabeld type
        uint32_t p0_load:1;         //!< 1 if tracing with P0 load elements (for data trace)
        uint32_t p0_store:1;        //1< 1 if tracing with P0 store elements (for data trace)
    } bits;         //!< bitfields for trace info value.
} etmv4_trace_info_t;

typedef struct _etmv4_context_t {
    struct {
        uint32_t EL:2;          //!< exception level.
        uint32_t SF:1;          //!< sixty four bit
        uint32_t NS:1;          //!< none secure
        uint32_t updated:1;     //!< updated this context packet (otherwise same as last time)
        uint32_t updated_c:1;   //!< updated CtxtID
        uint32_t updated_v:1;   //!< updated VMID
    };
    uint32_t ctxtID;   //!< Current ctxtID
    uint32_t VMID;     //!< current VMID
} etmv4_context_t;

/** a broadcast address value. */
typedef struct _etmv4_addr_val_t {
    ocsd_vaddr_t val;  //!< Address value.
    uint8_t isa;        //!< instruction set.
} etmv4_addr_val_t;

typedef struct _ocsd_etmv4_i_pkt
{
    ocsd_etmv4_i_pkt_type type;    /**< Trace packet type derived from header byte */

    //** intra-packet data - valid across packets.

    ocsd_pkt_vaddr v_addr;         //!< most recently broadcast address packet
    uint8_t         v_addr_ISA;     //!< ISA for the address packet. (0 = IS0 / 1 = IS1)

    etmv4_context_t context;        //!< current context for PE

    struct {
        uint64_t timestamp;         //!< current timestamp value
        uint8_t bits_changed;       //!< bits updated in this timestamp packet.
    } ts;

    uint32_t cc_threshold;      //!< cycle count threshold - from trace info.

    // single packet data - only valid for specific packet types on packet instance.
    ocsd_pkt_atom  atom;       //!< atom elements - number of atoms indicates validity of packet
    uint32_t cycle_count;       //!< cycle count

    uint32_t curr_spec_depth;   //!< current speculation depth
    uint32_t p0_key;            //!< current P0 key value for data packet synchronisation

    uint32_t commit_elements;  //<! commit elements indicated by this packet - valid dependent on the packet type.
    uint32_t cancel_elements;  //<! cancel elements indicated by this packet - valid dependent on the packet type.

    etmv4_trace_info_t trace_info;  //!< trace info structure - programmed configuration of trace capture.

    struct {
        uint32_t exceptionType:10;      //!< exception number
        uint32_t addr_interp:2;         //!< address value interpretation
        uint32_t m_fault_pending:1;     //!< M class fault pending.
        uint32_t m_type:1;              //!< 1 if M class exception.
    } exception_info;
    

    uint8_t addr_exact_match_idx;   //!< address match index in this packet.
    uint8_t dsm_val;    //!<  Data Sync Marker number, or unnumbered atom count - packet type determines.
    uint8_t event_val;  //!< Event value on event packet.

    struct {
        uint32_t cond_c_key;    
        uint8_t num_c_elem;
        struct {
            uint32_t cond_key_set:1;
            uint32_t f3_final_elem:1;
            uint32_t f2_cond_incr:1;
        };
    } cond_instr;

    struct {
        uint32_t cond_r_key_0;
        uint32_t cond_r_key_1;
        struct {
            uint32_t res_0:4;
            uint32_t res_1:4;
            uint32_t ci_0:1;
            uint32_t ci_1:1;
            uint32_t key_res_0_set:1;
            uint32_t key_res_1_set:1;
            uint32_t f2_key_incr:2;
            uint32_t f2f4_token:2;
            uint32_t f3_tokens:12;
        };
    } cond_result;

    struct {
        uint32_t q_count;
        struct {
            uint32_t addr_present:1;
            uint32_t addr_match:1;
            uint32_t count_present:1;
            uint32_t q_type:4;
        };
    } Q_pkt;

    //! valid bits for packet elements (addresses have their own valid bits).
    union {
        uint32_t val;
        struct {
            uint32_t context_valid:1;
            uint32_t ts_valid:1;
            uint32_t spec_depth_valid:1;
            uint32_t p0_key_valid:1;
            uint32_t cond_c_key_valid:1;
            uint32_t cond_r_key_valid:1;
            uint32_t trace_info_valid:1;
            uint32_t cc_thresh_valid:1;
            uint32_t cc_valid:1;
            uint32_t commit_elem_valid:1;
        } bits;
    } pkt_valid;

    // original header type when packet type changed to error on decode error.
    ocsd_etmv4_i_pkt_type err_type;

} ocsd_etmv4_i_pkt;


// D stream packets
typedef enum _ocsd_etmv4_d_pkt_type
{
// markers for unknown/bad packets
		ETM4_PKT_D_NOTSYNC = 0x200,        //!< no sync found yet
		ETM4_PKT_D_BAD_SEQUENCE,   //!< invalid sequence for packet type
        ETM4_PKT_D_BAD_TRACEMODE,  //!< invalid packet type for this trace mode.
		ETM4_PKT_D_RESERVED,       //!< packet type reserved.
        ETM4_PKT_D_INCOMPLETE_EOT, //!< flushing incomplete packet at end of trace.
        ETM4_PKT_D_NO_HEADER,      //!< waiting for a header byte
        ETM4_PKT_D_NO_ERR_TYPE,    //!< error packet has no header based type. Use with unknown/res packet types.
        
    // data sync markers
        ETM4_PKT_DNUM_DS_MKR = 0x111, // ext packet, b0001xxx1
    // extension header
        ETM4_PKT_D_EXTENSION = 0x00,           //!< b00000000 

        ETM4_PKT_DUNNUM_DS_MKR = 0x01,    //!< b00000001
    // event trace 
        ETM4_PKT_DEVENT = 0x04,           //!< b00000100
    // timestamp
        ETM4_PKT_DTIMESTAMP = 0x02,       //!< b00000010
    // P1 Data address 
        ETM4_PKT_DADDR_P1_F1 = 0x70,     //!< b0111xxxx
        ETM4_PKT_DADDR_P1_F2 = 0x80,     //!< b10xxxxxx
        ETM4_PKT_DADDR_P1_F3 = 0x14,     //!< b000101xx
        ETM4_PKT_DADDR_P1_F4 = 0x60,     //!< b0110xxxx
        ETM4_PKT_DADDR_P1_F5 = 0xF8,     //!< b11111xxx
        ETM4_PKT_DADDR_P1_F6 = 0xF6,     //!< b1111011x
        ETM4_PKT_DADDR_P1_F7 = 0xF5,     //!< b11110101
    // P2 Data value
        ETM4_PKT_DVAL_P2_F1 = 0x20,      //!< b0010xxxx
        ETM4_PKT_DVAL_P2_F2 = 0x30,      //!< b00110xxx
        ETM4_PKT_DVAL_P2_F3 = 0x40,      //!< b010xxxxx
        ETM4_PKT_DVAL_P2_F4 = 0x10,      //!< b000100xx
        ETM4_PKT_DVAL_P2_F5 = 0x18,      //!< b00011xxx
        ETM4_PKT_DVAL_P2_F6 = 0x38,      //!< b00111xxx
    // suppression
        ETM4_PKT_DSUPPRESSION = 0x03,     //!< b00000011
    // synchronisation- extension packets - follow 0x00 header
        ETM4_PKT_DTRACE_INFO = 0x101,      //!< b00000001

    // extension packets - follow 0x00 header
        ETM4_PKT_D_ASYNC = 0x100,              //!< b00000000
        ETM4_PKT_D_DISCARD = 0x103,            //!< b00000011
        ETM4_PKT_D_OVERFLOW = 0x105            //!< b00000101

} ocsd_etmv4_d_pkt_type;


typedef struct _ocsd_etmv4_d_pkt
{
    ocsd_etmv4_d_pkt_type type;

    ocsd_pkt_vaddr d_addr;

    uint64_t        pkt_val;    /**< Packet value -> data value, timestamp value, event value */

    ocsd_etmv4_d_pkt_type err_type;

} ocsd_etmv4_d_pkt;

typedef struct _ocsd_etmv4_cfg 
{
    uint32_t                reg_idr0;    /**< ID0 register */
    uint32_t                reg_idr1;    /**< ID1 register */
    uint32_t                reg_idr2;    /**< ID2 register */
    uint32_t                reg_idr8;
    uint32_t                reg_idr9;   
    uint32_t                reg_idr10;
    uint32_t                reg_idr11;
    uint32_t                reg_idr12;
    uint32_t                reg_idr13;
    uint32_t                reg_configr;  /**< Config Register */
    uint32_t                reg_traceidr;  /**< Trace Stream ID register */
    ocsd_arch_version_t    arch_ver;   /**< Architecture version */
    ocsd_core_profile_t    core_prof;  /**< Core Profile */
} ocsd_etmv4_cfg;

/** @}*/
/** @}*/
#endif // ARM_TRC_PKT_TYPES_ETMV4_H_INCLUDED

/* End of File trc_pkt_types_etmv4.h */


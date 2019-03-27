/*
 * \file       trc_pkt_types_stm.h
 * \brief      OpenCSD : STM decoder
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
#ifndef ARM_TRC_PKT_TYPES_STM_H_INCLUDED
#define ARM_TRC_PKT_TYPES_STM_H_INCLUDED

#include "opencsd/trc_pkt_types.h"

/** @addtogroup trc_pkts
@{*/

/** @name STM Packet Types
@{*/

/** STM protocol packet types.
    Contains both protocol packet types and markers for unsynced processor
    state and bad packet sequences.
*/
typedef enum _ocsd_stm_pkt_type
{
/* markers for unknown packets  / state*/
    STM_PKT_NOTSYNC,            /**< Not synchronised */
    STM_PKT_INCOMPLETE_EOT,     /**< Incomplete packet flushed at end of trace. */
    STM_PKT_NO_ERR_TYPE,        /**< No error in error packet marker. */

/* markers for valid packets*/
    STM_PKT_ASYNC,      /**< Alignment synchronisation packet */
    STM_PKT_VERSION,    /**< Version packet */
    STM_PKT_FREQ,       /**< Frequency packet */
    STM_PKT_NULL,       /**< Null packet */
    STM_PKT_TRIG,       /**< Trigger event packet. */

    STM_PKT_GERR,       /**< Global error packet - protocol error but unknown which master had error */
    STM_PKT_MERR,       /**< Master error packet - current master detected an error (e.g. dropped trace) */

    STM_PKT_M8,         /**< Set current master */
    STM_PKT_C8,         /**< Set lower 8 bits of current channel */
    STM_PKT_C16,        /**< Set current channel */

    STM_PKT_FLAG,       /**< Flag packet */

    STM_PKT_D4,         /**< 4 bit data payload packet */
    STM_PKT_D8,         /**< 8 bit data payload packet */
    STM_PKT_D16,        /**< 16 bit data payload packet */
    STM_PKT_D32,        /**< 32 bit data payload packet */
    STM_PKT_D64,        /**< 64 bit data payload packet */

/* packet errors.*/
    STM_PKT_BAD_SEQUENCE,   /**< Incorrect protocol sequence */
    STM_PKT_RESERVED,       /**< Reserved packet header / not supported by CS-STM */

} ocsd_stm_pkt_type;

/** STM timestamp encoding type.
    Extracted from STM version packet.
    CS-STM supports Natural binary and grey encodings.
*/
typedef enum _ocsd_stm_ts_type
{
    STM_TS_UNKNOWN,     /**< TS encoding unknown at present. */
    STM_TS_NATBINARY,   /**< TS encoding natural binary */
    STM_TS_GREY         /**< TS encoding grey coded. */
} ocsd_stm_ts_type;

/** STM trace packet

    Structure containing the packet data for a single STM packet, plus
    data persisting between packets (master, channel, last timestamp).
*/
typedef struct _ocsd_stm_pkt
{
    ocsd_stm_pkt_type type;        /**< STM packet type */

    uint8_t     master;             /**< current master */
    uint16_t    channel;            /**< current channel */
    
    uint64_t    timestamp;          /**< latest timestamp value -> as binary - packet processor does grey decoding */
    uint8_t     pkt_ts_bits;        /**< timestamp bits updated this packet */
    uint8_t     pkt_has_ts;         /**< current packet has associated timestamp (ts bits can be 0 if same value as last time) */
    
    ocsd_stm_ts_type ts_type;      /**< timestamp encoding type */

    uint8_t     pkt_has_marker;     /**< flag to indicate current packet has marker */

    union {
        uint8_t  D8;    /**< payload for D8 or D4 data packet, or parameter value for other packets with 8 bit value [VERSION, TRIG, xERR] */
        uint16_t D16;   /**< payload for D16 data packet, or reserved opcode in bad packet header (1-3 nibbles) */
        uint32_t D32;   /**< payload for D32 data packet, or parameter value for other packets with 32 bit value [FREQ] */
        uint64_t D64;   /**< payload for D64 data packet */
    } payload;

    ocsd_stm_pkt_type err_type;    /**< Initial type of packet if type indicates bad sequence. */

} ocsd_stm_pkt;

/** HW Event trace feature
    Defines if the STM supports or has enabled the HW event trace feature.
    This may not always be able to be determined by the registers, or the feature
    values can override if HW event trace is to be ignored.
*/
typedef enum _hw_event_feat {
    HwEvent_Unknown_Disabled,   /*!< status of HW event features not known - assume not present or disabled */
    HwEvent_Enabled,            /*!< HW event present and enabled - ignore Feat regs, assume hwev_mast value valid */
    HwEvent_UseRegisters        /*!< Feature Register values and enable bits used to determine HW event trace status */
} hw_event_feat_t;


/** STM hardware configuration.
    Contains hardware register values at time of trace capture and HW event feature
    field to enable and control decode of STM trace stream.
*/
typedef struct _ocsd_stm_cfg
{
    uint32_t reg_tcsr;          /**< Contains CoreSight trace ID, HWTEN */
    uint32_t reg_feat3r;        /**< defines number of masters */
    uint32_t reg_devid;         /**< defines number of channels per master */

    uint32_t reg_feat1r;        /**< defines HW trace features */
    uint32_t reg_hwev_mast;     /**< master ID for HW event trace */
    hw_event_feat_t hw_event;   /**< status of HW event trace */
} ocsd_stm_cfg;

/** @}*/
/** @}*/

#endif // ARM_TRC_PKT_TYPES_STM_H_INCLUDED

/* End of File trc_pkt_types_stm.h */

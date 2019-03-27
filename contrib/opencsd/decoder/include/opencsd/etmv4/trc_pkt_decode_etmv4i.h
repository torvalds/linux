/*
 * \file       trc_pkt_decode_etmv4i.h
 * \brief      OpenCSD : ETMv4 instruction decoder
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

#ifndef ARM_TRC_PKT_DECODE_ETMV4I_H_INCLUDED
#define ARM_TRC_PKT_DECODE_ETMV4I_H_INCLUDED

#include "common/trc_pkt_decode_base.h"
#include "opencsd/etmv4/trc_pkt_elem_etmv4i.h"
#include "opencsd/etmv4/trc_cmp_cfg_etmv4.h"
#include "common/trc_gen_elem.h"
#include "common/trc_ret_stack.h"
#include "opencsd/etmv4/trc_etmv4_stack_elem.h"

class TrcStackElem;
class TrcStackElemParam;
class TrcStackElemCtxt;

class TrcPktDecodeEtmV4I : public TrcPktDecodeBase<EtmV4ITrcPacket, EtmV4Config>
{
public:
    TrcPktDecodeEtmV4I();
    TrcPktDecodeEtmV4I(int instIDNum);
    virtual ~TrcPktDecodeEtmV4I();

protected:
    /* implementation packet decoding interface */
    virtual ocsd_datapath_resp_t processPacket();
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const uint8_t getCoreSightTraceID() { return m_CSID; };

    /* local decode methods */
    void initDecoder();      // initial state on creation (zeros all config)
    void resetDecoder();     // reset state to start of decode. (moves state, retains config)

    ocsd_datapath_resp_t decodePacket(bool &Complete);    // return true to indicate decode complete - can change FSM to commit state - return is false.
    ocsd_datapath_resp_t commitElements(bool &Complete);   // commit elements - may get wait response, or flag completion.
    ocsd_datapath_resp_t flushEOT();

    void doTraceInfoPacket();
    void updateContext(TrcStackElemCtxt *pCtxtElem);
    
    // process atom will output instruction trace, or no memory access trace elements. 
    ocsd_datapath_resp_t processAtom(const ocsd_atm_val, bool &bCont);

    // process an exception element - output instruction trace + exception generic type.
    ocsd_datapath_resp_t processException(); 

    // process a bad packet
    ocsd_datapath_resp_t handleBadPacket(const char *reason);

    ocsd_datapath_resp_t outputCC(TrcStackElemParam *pParamElem);
    ocsd_datapath_resp_t outputTS(TrcStackElemParam *pParamElem, bool withCC);
    ocsd_datapath_resp_t outputEvent(TrcStackElemParam *pParamElem);
     
private:
    void SetInstrInfoInAddrISA(const ocsd_vaddr_t addr_val, const uint8_t isa); 

    ocsd_err_t traceInstrToWP(bool &bWPFound, const bool traceToAddrNext = false, const ocsd_vaddr_t nextAddrMatch = 0);      //!< follow instructions from the current address to a WP. true if good, false if memory cannot be accessed.

    ocsd_datapath_resp_t returnStackPop();  // pop return stack and update instruction address.

//** intra packet state (see ETMv4 spec 6.2.1);

    // timestamping
    uint64_t m_timestamp;   // last broadcast global Timestamp.

    // state and context 
    uint32_t m_context_id;              // most recent context ID
    uint32_t m_vmid_id;                 // most recent VMID
    bool m_is_secure;                   // true if Secure
    bool m_is_64bit;                    // true if 64 bit

    // cycle counts 
    int m_cc_threshold;

    // speculative trace (unsupported at present in the decoder).
    int m_curr_spec_depth;                
    int m_max_spec_depth; 
    
    // data trace associative elements (unsupported at present in the decoder).
    int m_p0_key;
    int m_p0_key_max;

    // conditional non-branch trace - when data trace active (unsupported at present in the decoder)
    int m_cond_c_key;
    int m_cond_r_key;
    int m_cond_key_max_incr;

    uint8_t m_CSID; //!< Coresight trace ID for this decoder.

    bool m_IASize64;    //!< True if 64 bit instruction addresses supported.

//** Other processor state;

    // trace decode FSM
    typedef enum {
        NO_SYNC,        //!< pre start trace - init state or after reset or overflow, loss of sync.
        WAIT_SYNC,      //!< waiting for sync packet.
        WAIT_TINFO,     //!< waiting for trace info packet.
        DECODE_PKTS,    //!< processing packets - creating decode elements on stack
        COMMIT_ELEM,    //!< commit elements for execution - create generic trace elements and pass on.
    } processor_state_t;

    processor_state_t m_curr_state;

//** P0 element stack
    EtmV4P0Stack m_P0_stack;    //!< P0 decode element stack

    int m_P0_commit;    //!< number of elements to commit

    // packet decode state
    bool m_need_ctxt;   //!< need context to continue
    bool m_need_addr;   //!< need an address to continue
    bool m_except_pending_addr;    //!< next address packet is part of exception.

    // exception packet processing state (may need excep elem only, range+excep, range+
    typedef enum {
        EXCEP_POP, // start of processing read exception packets off the stack and analyze
        EXCEP_RANGE, // output a range element
        EXCEP_NACC,  // output a nacc element
        EXCEP_EXCEP, // output an ecxeption element.
    } excep_proc_state_t;

    excep_proc_state_t m_excep_proc;  //!< state of exception processing
    etmv4_addr_val_t m_excep_addr;    //!< excepiton return address.
    ocsd_trc_index_t m_excep_index;  //!< trace index for exception element

    ocsd_instr_info m_instr_info;  //!< instruction info for code follower - in address is the next to be decoded.

    bool m_mem_nacc_pending;    //!< need to output a memory access failure packet
    ocsd_vaddr_t m_nacc_addr;  //!< record unaccessible address 

    ocsd_pe_context m_pe_context;  //!< current context information
    etmv4_trace_info_t m_trace_info; //!< trace info for this trace run.

    bool m_prev_overflow;

    bool m_flush_EOT;           //!< true if doing an end of trace flush - cleans up lingering events / TS / CC

    TrcAddrReturnStack m_return_stack;

//** output element
    OcsdTraceElement m_output_elem;

};

#endif // ARM_TRC_PKT_DECODE_ETMV4I_H_INCLUDED

/* End of File trc_pkt_decode_etmv4i.h */

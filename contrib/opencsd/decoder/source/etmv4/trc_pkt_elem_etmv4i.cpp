/*
 * \file       trc_pkt_elem_etmv4i.cpp
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
#include <sstream>
#include <iomanip>

#include "opencsd/etmv4/trc_pkt_elem_etmv4i.h"

EtmV4ITrcPacket::EtmV4ITrcPacket()
{
}

EtmV4ITrcPacket::~EtmV4ITrcPacket()
{
}

void EtmV4ITrcPacket::initStartState()
{
    // clear packet state to start of trace (first sync or post discontinuity)

    // clear all valid bits
    pkt_valid.val = 0;

    // virtual address
    v_addr.pkt_bits = 0;
    v_addr.valid_bits = 0;
    v_addr_ISA = 0;

    // timestamp
    ts.bits_changed = 0;
    ts.timestamp = 0;    

    // per packet init
    initNextPacket();
}

void EtmV4ITrcPacket::initNextPacket()
{
    // clear valid bits for elements that are only valid over a single packet.
    pkt_valid.bits.cc_valid = 0;
    pkt_valid.bits.commit_elem_valid = 0;
    atom.num = 0;
    context.updated = 0;
    context.updated_v = 0;
    context.updated_c = 0;
    err_type = ETM4_PKT_I_NO_ERR_TYPE;
}

// printing
void EtmV4ITrcPacket::toString(std::string &str) const
{
    const char *name;
    const char *desc;
    std::string valStr, ctxtStr = "";

    name = packetTypeName(type, &desc);
    str = name + (std::string)" : " + desc;

    // extended descriptions
    switch (type)
    {
    case ETM4_PKT_I_BAD_SEQUENCE:
    case ETM4_PKT_I_INCOMPLETE_EOT:
        name = packetTypeName(err_type, 0);
        str += "[" + (std::string)name + "]";
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_32IS0:
    case ETM4_PKT_I_ADDR_CTXT_L_32IS1:
        contextStr(ctxtStr);
    case ETM4_PKT_I_ADDR_L_32IS0:
    case ETM4_PKT_I_ADDR_L_32IS1:
        trcPrintableElem::getValStr(valStr, (v_addr.size == VA_64BIT) ? 64 : 32, v_addr.valid_bits, v_addr.val, true, (v_addr.pkt_bits < 32) ? v_addr.pkt_bits : 0);
        str += "; Addr=" + valStr + "; " + ctxtStr;
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_64IS0:
    case ETM4_PKT_I_ADDR_CTXT_L_64IS1:
        contextStr(ctxtStr);
    case ETM4_PKT_I_ADDR_L_64IS0:
    case ETM4_PKT_I_ADDR_L_64IS1:
        trcPrintableElem::getValStr(valStr, (v_addr.size == VA_64BIT) ? 64 : 32, v_addr.valid_bits, v_addr.val, true, (v_addr.pkt_bits < 64) ? v_addr.pkt_bits : 0);
        str += "; Addr=" + valStr + "; " + ctxtStr;
        break;

    case ETM4_PKT_I_CTXT:
        contextStr(ctxtStr);
        str += "; " + ctxtStr;
        break;

    case ETM4_PKT_I_ADDR_S_IS0:
    case ETM4_PKT_I_ADDR_S_IS1:
        trcPrintableElem::getValStr(valStr, (v_addr.size == VA_64BIT) ? 64 : 32, v_addr.valid_bits, v_addr.val, true, v_addr.pkt_bits);
        str += "; Addr=" + valStr;
        break;

    case ETM4_PKT_I_ADDR_MATCH:
        addrMatchIdx(valStr);
        str += ", " + valStr;
        trcPrintableElem::getValStr(valStr, (v_addr.size == VA_64BIT) ? 64 : 32, v_addr.valid_bits, v_addr.val, true);
        str += "; Addr=" + valStr + "; " + ctxtStr;
        break;

    case ETM4_PKT_I_ATOM_F1:
    case ETM4_PKT_I_ATOM_F2:
    case ETM4_PKT_I_ATOM_F3:
    case ETM4_PKT_I_ATOM_F4:
    case ETM4_PKT_I_ATOM_F5:
    case ETM4_PKT_I_ATOM_F6:
        atomSeq(valStr);
        str += "; " + valStr;
        break;

    case ETM4_PKT_I_EXCEPT:
        exceptionInfo(valStr);
        str += "; " + valStr;
        break;

    case ETM4_PKT_I_TIMESTAMP:
        {
            std::ostringstream oss;
            oss << "; Updated val = " << std::hex << "0x" << ts.timestamp;
            if (pkt_valid.bits.cc_valid)
                oss << "; CC=" << std::hex << "0x" << cycle_count;
            str += oss.str();
        }
        break;

    case ETM4_PKT_I_TRACE_INFO:
        {
            std::ostringstream oss;
            oss << "; INFO=" << std::hex << "0x" << trace_info.val;
            if (trace_info.bits.cc_enabled)
                oss << "; CC_THRESHOLD=" << std::hex << "0x" << cc_threshold;
            str += oss.str();
        }
        break;

    case ETM4_PKT_I_CCNT_F1:
    case ETM4_PKT_I_CCNT_F2:
    case ETM4_PKT_I_CCNT_F3:
        {
            std::ostringstream oss;
            oss << "; Count=" << std::hex << "0x" << cycle_count;
            str += oss.str();
        }
        break;
    }
}

void EtmV4ITrcPacket::toStringFmt(const uint32_t fmtFlags, std::string &str) const
{
    toString(str);  // TBD add in formatted response.
}

const char *EtmV4ITrcPacket::packetTypeName(const ocsd_etmv4_i_pkt_type type, const char **ppDesc) const
{
    const char *pName = "I_RESERVED";
    const char *pDesc = "Reserved Packet Header";

    switch(type)
    {
    case ETM4_PKT_I_RESERVED: break; // default;

    case ETM4_PKT_I_NOTSYNC:
        pName = "I_NOT_SYNC";
        pDesc = "I Stream not synchronised";
        break;

    case ETM4_PKT_I_BAD_SEQUENCE:
        pName = "I_BAD_SEQUENCE";
        pDesc = "Invalid Sequence in packet.";
        break;

    case ETM4_PKT_I_BAD_TRACEMODE:
        pName = "I_BAD_TRACEMODE";
        pDesc = "Invalid Packet for trace mode.";
        break;

    case ETM4_PKT_I_INCOMPLETE_EOT:
        pName = "I_INCOMPLETE_EOT";
        pDesc = "Incomplete packet at end of trace.";
        break;

    case ETM4_PKT_I_NO_ERR_TYPE:
        pName = "I_NO_ERR_TYPE";
        pDesc = "No Error Type.";
        break;

    case ETM4_PKT_I_EXTENSION:
        pName = "I_EXTENSION";
        pDesc = "Extention packet header.";
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_32IS0:
        pName = "I_ADDR_CTXT_L_32IS0";
        pDesc = "Address & Context, Long, 32 bit, IS0.";
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_32IS1:
        pName = "I_ADDR_CTXT_L_32IS1";
        pDesc = "Address & Context, Long, 32 bit, IS0.";
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_64IS0:
        pName = "I_ADDR_CTXT_L_64IS0";
        pDesc = "Address & Context, Long, 64 bit, IS0.";
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_64IS1:
        pName = "I_ADDR_CTXT_L_64IS1";
        pDesc = "Address & Context, Long, 64 bit, IS1.";
        break;

    case ETM4_PKT_I_CTXT:
        pName = "I_CTXT";
        pDesc = "Context Packet.";
        break;

    case ETM4_PKT_I_ADDR_MATCH:
        pName = "I_ADDR_MATCH";
        pDesc = "Exact Address Match.";
        break;

    case ETM4_PKT_I_ADDR_L_32IS0:
        pName = "I_ADDR_L_32IS0";
        pDesc = "Address, Long, 32 bit, IS0.";
        break;

    case ETM4_PKT_I_ADDR_L_32IS1:
        pName = "I_ADDR_L_32IS1";
        pDesc = "Address, Long, 32 bit, IS1.";
        break;

    case ETM4_PKT_I_ADDR_L_64IS0:
        pName = "I_ADDR_L_64IS0";
        pDesc = "Address, Long, 64 bit, IS0.";
        break;

    case ETM4_PKT_I_ADDR_L_64IS1:
        pName = "I_ADDR_L_64IS1";
        pDesc = "Address, Long, 64 bit, IS1.";
        break;

    case ETM4_PKT_I_ADDR_S_IS0:
        pName = "I_ADDR_S_IS0";
        pDesc = "Address, Short, IS0.";
        break;

    case ETM4_PKT_I_ADDR_S_IS1:
        pName = "I_ADDR_S_IS1";
        pDesc = "Address, Short, IS1.";
        break;

    case ETM4_PKT_I_Q:       
        pName = "I_Q";
        pDesc = "Q Packet.";
        break;

    case ETM4_PKT_I_ATOM_F1:
        pName = "I_ATOM_F1";
        pDesc = "Atom format 1.";
        break;

    case ETM4_PKT_I_ATOM_F2:
        pName = "I_ATOM_F2";
        pDesc = "Atom format 2.";
        break;

    case ETM4_PKT_I_ATOM_F3:
        pName = "I_ATOM_F3";
        pDesc = "Atom format 3.";
        break;

    case ETM4_PKT_I_ATOM_F4:
        pName = "I_ATOM_F4";
        pDesc = "Atom format 4.";
        break;

    case ETM4_PKT_I_ATOM_F5:
        pName = "I_ATOM_F5";
        pDesc = "Atom format 5.";
        break;

    case ETM4_PKT_I_ATOM_F6:
        pName = "I_ATOM_F6";
        pDesc = "Atom format 6.";
        break;

    case ETM4_PKT_I_COND_FLUSH:
        pName = "I_COND_FLUSH";
        pDesc = "Conditional Flush.";
        break;

    case ETM4_PKT_I_COND_I_F1:
        pName = "I_COND_I_F1";
        pDesc = "Conditional Instruction, format 1.";
        break;

    case ETM4_PKT_I_COND_I_F2:
        pName = "I_COND_I_F2";
        pDesc = "Conditional Instruction, format 2.";
        break;

    case ETM4_PKT_I_COND_I_F3:
        pName = "I_COND_I_F3";
        pDesc = "Conditional Instruction, format 3.";
        break;

    case ETM4_PKT_I_COND_RES_F1:
        pName = "I_COND_RES_F1";
        pDesc = "Conditional Result, format 1.";
        break;

    case ETM4_PKT_I_COND_RES_F2:
        pName = "I_COND_RES_F2";
        pDesc = "Conditional Result, format 2.";
        break;

    case ETM4_PKT_I_COND_RES_F3:
        pName = "I_COND_RES_F3";
        pDesc = "Conditional Result, format 3.";
        break;

    case ETM4_PKT_I_COND_RES_F4:
        pName = "I_COND_RES_F4";
        pDesc = "Conditional Result, format 4.";
        break;

    case ETM4_PKT_I_CCNT_F1:
        pName = "I_CCNT_F1";
        pDesc = "Cycle Count format 1.";
        break;

    case ETM4_PKT_I_CCNT_F2:
        pName = "I_CCNT_F2";
        pDesc = "Cycle Count format 2.";
        break;

    case ETM4_PKT_I_CCNT_F3:
        pName = "I_CCNT_F3";
        pDesc = "Cycle Count format 3.";
        break;

    case ETM4_PKT_I_NUM_DS_MKR:
        pName = "I_NUM_DS_MKR";
        pDesc = "Data Synchronisation Marker - Numbered.";
        break;

    case ETM4_PKT_I_UNNUM_DS_MKR:
        pName = "I_UNNUM_DS_MKR";
        pDesc = "Data Synchronisation Marker - Unnumbered.";
        break;

    case ETM4_PKT_I_EVENT:
        pName = "I_EVENT";
        pDesc = "Trace Event.";
        break;

    case ETM4_PKT_I_EXCEPT:
        pName = "I_EXCEPT";
        pDesc = "Exception.";
        break;

    case ETM4_PKT_I_EXCEPT_RTN:
        pName = "I_EXCEPT_RTN";
        pDesc = "Exception Return.";
        break;

    case ETM4_PKT_I_TIMESTAMP:
        pName = "I_TIMESTAMP";
        pDesc = "Timestamp.";
        break;

    case ETM4_PKT_I_CANCEL_F1:
        pName = "I_CANCEL_F1";
        pDesc = "Cancel Format 1.";
        break;
    case ETM4_PKT_I_CANCEL_F2:
        pName = "I_CANCEL_F2";
        pDesc = "Cancel Format 2.";
        break;

    case ETM4_PKT_I_CANCEL_F3:
        pName = "I_CANCEL_F3";
        pDesc = "Cancel Format 3.";
        break;

    case ETM4_PKT_I_COMMIT:
        pName = "I_COMMIT";
        pDesc = "Commit";
        break;

    case ETM4_PKT_I_MISPREDICT:
        pName = "I_MISPREDICT";
        pDesc = "Mispredict.";
        break;

    case ETM4_PKT_I_TRACE_INFO:
        pName = "I_TRACE_INFO";
        pDesc = "Trace Info.";
        break;

    case ETM4_PKT_I_TRACE_ON:
        pName = "I_TRACE_ON";
        pDesc = "Trace On.";
        break;

    case ETM4_PKT_I_ASYNC:
        pName = "I_ASYNC";
        pDesc = "Alignment Synchronisation.";
        break;

    case ETM4_PKT_I_DISCARD:
        pName = "I_DISCARD";
        pDesc = "Discard.";
        break;
        
    case ETM4_PKT_I_OVERFLOW:
        pName = "I_OVERFLOW";
        pDesc = "Overflow.";
        break;
    }

    if(ppDesc) *ppDesc = pDesc;
    return pName;
}

void EtmV4ITrcPacket::contextStr(std::string &ctxtStr) const
{
    ctxtStr = "";
    if(pkt_valid.bits.context_valid)
    {
        std::ostringstream oss;
        if(context.updated)
        {           
            oss << "Ctxt: " << (context.SF ? "AArch64," : "AArch32, ") << "EL" << context.EL << ", " << (context.NS ? "NS; " : "S; ");
            if(context.updated_c)
            {
                oss << "CID=0x" << std::hex << std::setfill('0') << std::setw(8) << context.ctxtID << "; ";
            }
            if(context.updated_v)
            {
                oss << "VMID=0x" << std::hex << std::setfill('0') << std::setw(4) << context.VMID << "; ";
            }
        }
        else
        {
            oss << "Ctxt: Same";
        }
        ctxtStr = oss.str();
    }
}

void EtmV4ITrcPacket::atomSeq(std::string &valStr) const
{
    std::ostringstream oss;
    uint32_t bitpattern = atom.En_bits;
    for(int i = 0; i < atom.num; i++)
    {
        oss << ((bitpattern & 0x1) ? "E" : "N");
        bitpattern >>= 1;
    }
    valStr = oss.str();
}

void EtmV4ITrcPacket::addrMatchIdx(std::string &valStr) const
{
    std::ostringstream oss;
    oss << "[" << (uint16_t)addr_exact_match_idx << "]";
    valStr = oss.str();
}

void EtmV4ITrcPacket::exceptionInfo(std::string &valStr) const
{
    std::ostringstream oss;

    static const char *ARv8Excep[] = {
        "PE Reset", "Debug Halt", "Call", "Trap", 
        "System Error", "Reserved", "Inst Debug", "Data Debug",
        "Reserved", "Reserved", "Alignment", "Inst Fault", 
        "Data Fault", "Reserved", "IRQ", "FIQ"
    };

    static const char *MExcep[] = {
        "Reserved", "PE Reset", "NMI", "HardFault",
        "MemManage", "BusFault", "UsageFault", "Reserved",
        "Reserved","Reserved","Reserved","SVC",
        "DebugMonitor", "Reserved","PendSV","SysTick",
        "IRQ0","IRQ1","IRQ2","IRQ3",
        "IRQ4","IRQ5","IRQ6","IRQ7",
        "DebugHalt", "LazyFP Push", "Lockup", "Reserved",
        "Reserved","Reserved","Reserved","Reserved"
    };

    if(exception_info.m_type == 0)
    {
        if(exception_info.exceptionType < 0x10)
            oss << " " << ARv8Excep[exception_info.exceptionType] << ";";
        else
            oss << " Reserved;";

    }
    else
    {
        if(exception_info.exceptionType < 0x20)
            oss << " " << MExcep[exception_info.exceptionType] << ";";
        else if((exception_info.exceptionType >= 0x208) && (exception_info.exceptionType <= 0x3EF))
            oss << " IRQ" << (int)(exception_info.exceptionType - 0x200) << ";";
        else
            oss << " Reserved;";
        if(exception_info.m_fault_pending)
            oss << " Fault Pending;";
    }

    if(exception_info.addr_interp == 0x1)
        oss << " Ret Addr Follows;";
    else if(exception_info.addr_interp == 0x2)
        oss << " Ret Addr Follows, Match Prev;";            
    
    valStr = oss.str();
}

EtmV4ITrcPacket &EtmV4ITrcPacket::operator =(const ocsd_etmv4_i_pkt* p_pkt)
{
    *dynamic_cast<ocsd_etmv4_i_pkt *>(this) = *p_pkt;
    return *this;        
}

/* End of File trc_pkt_elem_etmv4i.cpp */

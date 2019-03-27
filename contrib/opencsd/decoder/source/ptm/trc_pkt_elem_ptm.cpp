/*
 * \file       trc_pkt_elem_ptm.cpp
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

#include "opencsd/ptm/trc_pkt_elem_ptm.h"

PtmTrcPacket::PtmTrcPacket()
{
}

PtmTrcPacket::~PtmTrcPacket()
{
}

PtmTrcPacket &PtmTrcPacket::operator =(const ocsd_ptm_pkt* p_pkt)
{
    *dynamic_cast<ocsd_ptm_pkt *>(this) = *p_pkt;
    return *this;        
}

void PtmTrcPacket::Clear()
{
    err_type = PTM_PKT_NOERROR;
    cycle_count = 0;
    cc_valid = 0;
    context.updated = 0;
    context.updated_c = 0;
    context.updated_v = 0;
    ts_update_bits = 0;
    atom.En_bits = 0;
    exception.bits.present = 0;
    prev_isa = curr_isa;    // mark ISA as not changed
}

void PtmTrcPacket::ResetState()
{
    type = PTM_PKT_NOTSYNC;

    context.ctxtID = 0;
    context.VMID = 0;
    context.curr_alt_isa = 0;
    context.curr_Hyp = 0;
    context.curr_NS = 0;

    addr.valid_bits = 0;
    addr.size = VA_32BIT;
    addr.val = 0;

    prev_isa = curr_isa = ocsd_isa_unknown;

    timestamp = 0; 

    Clear();
}

void PtmTrcPacket::UpdateAddress(const ocsd_vaddr_t partAddrVal, const int updateBits)
{
    ocsd_vaddr_t validMask = OCSD_VA_MASK;
    validMask >>= OCSD_MAX_VA_BITSIZE-updateBits;
    addr.pkt_bits = updateBits;
    addr.val &= ~validMask;
    addr.val |= (partAddrVal & validMask);
    if(updateBits > addr.valid_bits)
        addr.valid_bits = updateBits;    
}

void PtmTrcPacket::UpdateTimestamp(const uint64_t tsVal, const uint8_t updateBits)
{
    uint64_t validMask = ~0ULL;
    validMask >>= 64-updateBits;
    timestamp &= ~validMask;
    timestamp |= (tsVal & validMask);
    ts_update_bits = updateBits;
}
   
void PtmTrcPacket::SetCycleAccAtomFromPHdr(const uint8_t pHdr)
{
    atom.num = 1;
    atom.En_bits = (pHdr & 0x2) ? 0x0 : 0x1;
}

void PtmTrcPacket::SetAtomFromPHdr(const uint8_t pHdr)
{
    // how many atoms
    uint8_t atom_fmt_id = pHdr & 0xF0;
    if(atom_fmt_id == 0x80)
    {
        // format 1 or 2
        if((pHdr & 0x08) == 0x08)
            atom.num = 2;
        else
            atom.num = 1;
    }
    else if(atom_fmt_id == 0x90)
    {
        atom.num = 3;
    }
    else
    {
        if((pHdr & 0xE0) == 0xA0)
            atom.num = 4;
        else
            atom.num = 5;
    }
        
    // extract the E/N bits
    uint8_t atom_mask = 0x2;    // start @ bit 1 - newest instruction 
    atom.En_bits = 0;
    for(int i = 0; i < atom.num; i++)
    {
        atom.En_bits <<= 1;
        if(!(atom_mask & pHdr)) // 0 bit is an E in PTM -> a one in the standard atom bit type
            atom.En_bits |= 0x1;
        atom_mask <<= 1;
    }        
}

    // printing
void PtmTrcPacket::toString(std::string &str) const
{
    std::string temp1, temp2;
    std::ostringstream oss;

    packetTypeName(type, temp1,temp2);
    oss << temp1 << " : " << temp2 << "; ";

    // some packets require additional data.
    switch(type)
    {
    case PTM_PKT_BAD_SEQUENCE:
        packetTypeName(err_type, temp1,temp2);
        oss << "[" << temp1 << "]; ";
        break;

    case PTM_PKT_ATOM:
        getAtomStr(temp1);
        oss << temp1;
        break;

    case PTM_PKT_CONTEXT_ID:
        oss << "CtxtID=0x" << std::hex << std::setw(8) << std::setfill('0') << context.ctxtID << "; ";
        break;

    case PTM_PKT_VMID:
        oss << "VMID=0x" << std::hex << std::setw(2) << std::setfill('0') << context.VMID << "; ";
        break;

    case PTM_PKT_WPOINT_UPDATE:
    case PTM_PKT_BRANCH_ADDRESS:
        getBranchAddressStr(temp1);
        oss << temp1;
        break;

    case PTM_PKT_I_SYNC:
        getISyncStr(temp1);
        oss << temp1;
        break;

    case PTM_PKT_TIMESTAMP:
        getTSStr(temp1);
        oss << temp1;
        break;
    }

    str = oss.str();
}

void PtmTrcPacket::toStringFmt(const uint32_t fmtFlags, std::string &str) const
{
    toString(str);
}

void PtmTrcPacket::getAtomStr(std::string &valStr) const
{   
    std::ostringstream oss;
    uint32_t bitpattern = atom.En_bits; // arranged LSBit oldest, MSbit newest

    if(cc_valid)    // cycle accurate trace - single atom
    {
        std::string subStr;
        oss << ((bitpattern & 0x1) ? "E" : "N"); // in spec read L->R, oldest->newest
        oss << "; ";
        getCycleCountStr(subStr);
        oss << subStr;
    }
    else
    {
        // none cycle count
        for(int i = 0; i < atom.num; i++)
        {
            oss << ((bitpattern & 0x1) ? "E" : "N"); // in spec read L->R, oldest->newest
            bitpattern >>= 1;
        } 
        oss << "; ";
    }
    valStr = oss.str();
}

void PtmTrcPacket::getBranchAddressStr(std::string &valStr) const
{
    std::ostringstream oss;
    std::string subStr;

    // print address.
    trcPrintableElem::getValStr(subStr,32,addr.valid_bits,addr.val,true,addr.pkt_bits);
    oss << "Addr=" << subStr << "; ";

    // current ISA if changed.
    if(curr_isa != prev_isa)
    {
        getISAStr(subStr);
        oss << subStr;
    }

    // S / NS etc if changed.
    if(context.updated)
    {
        oss << (context.curr_NS ? "NS; " : "S; ");
        oss << (context.curr_Hyp ? "Hyp; " : "");
    }
    
    // exception? 
    if(exception.bits.present)
    {
        getExcepStr(subStr);
        oss << subStr;
    }

    if(cc_valid)
    {
        getCycleCountStr(subStr);
        oss << subStr;
    }
    valStr = oss.str();
}

void PtmTrcPacket::getISAStr(std::string &isaStr) const
{
    std::ostringstream oss;
    oss << "ISA=";
    switch(curr_isa)
    {
    case ocsd_isa_arm: 
        oss << "ARM(32); ";
        break;

    case ocsd_isa_thumb2:
        oss << "Thumb2; ";
        break;

    case ocsd_isa_aarch64:
        oss << "AArch64; ";
        break;

    case ocsd_isa_tee:
        oss << "ThumbEE; ";
        break;

    case ocsd_isa_jazelle:
        oss << "Jazelle; ";
        break;

    default:
    case ocsd_isa_unknown:
        oss << "Unknown; ";
        break;
    }
    isaStr = oss.str();
}

void PtmTrcPacket::getExcepStr(std::string &excepStr) const
{
    static const char *ARv7Excep[] = {
        "No Exception", "Debug Halt", "SMC", "Hyp", 
        "Async Data Abort", "Jazelle", "Reserved", "Reserved",
        "PE Reset", "Undefined Instr", "SVC", "Prefetch Abort", 
        "Data Fault", "Generic", "IRQ", "FIQ"
    };

    std::ostringstream oss;
    oss << "Excep=";
    if(exception.number < 16)
        oss << ARv7Excep[exception.number];
    else
        oss << "Unknown";
    oss << " [" << std::hex << std::setw(2) << std::setfill('0') << exception.number << "]; ";
    excepStr = oss.str();
}

void PtmTrcPacket::getISyncStr(std::string &valStr) const
{
    std::ostringstream oss;
    std::string tmpStr;
    static const char *reason[] = { "Periodic", "Trace Enable", "Restart Overflow", "Debug Exit" };
    
    // reason.
    oss << "(" << reason[(int)i_sync_reason] << "); ";

    // full address.
    oss << "Addr=0x" << std::hex << std::setfill('0') << std::setw(8) << (uint32_t)addr.val << "; ";
        
    oss << (context.curr_NS ? "NS; " : "S; ");
    oss << (context.curr_Hyp ? "Hyp; " : " ");
    
    if(context.updated_c)
    {
        oss << "CtxtID=" << std::hex  << std::setw(8) << std::setfill('0') << context.ctxtID << "; ";
    }
    
    getISAStr(tmpStr);
    oss << tmpStr;

    if(cc_valid)
    {
        getCycleCountStr(tmpStr);
        oss << tmpStr;
    } 
    valStr = oss.str();
}

void PtmTrcPacket::getTSStr(std::string &valStr) const
{
    std::string tmpStr;
    std::ostringstream oss;

    trcPrintableElem::getValStr(tmpStr,64,64,timestamp,true,ts_update_bits);
    oss << "TS=" << tmpStr + "(" << std::dec << timestamp << "); ";
    if(cc_valid)
    {
        getCycleCountStr(tmpStr);
        oss << tmpStr;
    } 
    valStr = oss.str();
}


void PtmTrcPacket::getCycleCountStr(std::string &subStr) const
{
    std::ostringstream oss;
    oss << "Cycles=" << std::dec << cycle_count << "; ";
    subStr = oss.str();
}


void PtmTrcPacket::packetTypeName(const ocsd_ptm_pkt_type pkt_type, std::string &name, std::string &desc) const
{
    switch(pkt_type)
    {
    case PTM_PKT_NOTSYNC:        //!< no sync found yet
        name = "NOTSYNC";
        desc = "PTM Not Synchronised";
        break;

    case PTM_PKT_INCOMPLETE_EOT:
        name = "INCOMPLETE_EOT";
        desc = "Incomplete packet flushed at end of trace";
        break;

    case PTM_PKT_NOERROR:
        name = "NO_ERROR";
        desc = "Error type not set";
        break;

    case PTM_PKT_BAD_SEQUENCE:
        name = "BAD_SEQUENCE";
        desc = "Invalid sequence in packet";
        break;

    case PTM_PKT_RESERVED:
        name = "RESERVED";
        desc = "Reserved Packet Header";
        break;

    case PTM_PKT_BRANCH_ADDRESS:
        name = "BRANCH_ADDRESS";
        desc = "Branch address packet";
        break;

    case PTM_PKT_A_SYNC:
        name = "ASYNC";
        desc = "Alignment Synchronisation Packet";
        break;

	case PTM_PKT_I_SYNC:
        name = "ISYNC";
        desc = "Instruction Synchronisation packet";
        break;

    case PTM_PKT_TRIGGER:
        name = "TRIGGER";
        desc = "Trigger Event packet";
        break;

	case PTM_PKT_WPOINT_UPDATE:
        name = "WP_UPDATE";
        desc = "Waypoint update packet";
        break;

	case PTM_PKT_IGNORE:
        name = "IGNORE";
        desc = "Ignore packet";
        break;

	case PTM_PKT_CONTEXT_ID:
        name = "CTXTID";
        desc = "Context ID packet";
        break;

    case PTM_PKT_VMID:
        name = "VMID";
        desc = "VM ID packet";
        break;

	case PTM_PKT_ATOM:
        name = "ATOM";
        desc = "Atom packet";
        break;

	case PTM_PKT_TIMESTAMP:
        name = "TIMESTAMP";
        desc = "Timestamp packet";
        break;

	case PTM_PKT_EXCEPTION_RET:
        name = "ERET";
        desc = "Exception return packet";
        break;

    default:
        name = "UNKNOWN";
        desc = "Unknown packet type";
        break;

	//PTM_PKT_BRANCH_OR_BYPASS_EOT, 
    //PTM_PKT_TPIU_PAD_EOB,  
    }
}

/* End of File trc_pkt_elem_ptm.cpp */

/*
 * \file       trc_idec_arminst.cpp
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

/*
Basic ARM/Thumb/A64 instruction decode, suitable for e.g. basic
block identification and trace decode.
*/

#include "i_dec/trc_idec_arminst.h"


#include <stddef.h>  /* for NULL */
#include <assert.h>


static ocsd_instr_subtype instr_sub_type = OCSD_S_INSTR_NONE;

ocsd_instr_subtype get_instr_subtype()
{
    return instr_sub_type;
}

void clear_instr_subtype()
{
    instr_sub_type = OCSD_S_INSTR_NONE;
}

int inst_ARM_is_direct_branch(uint32_t inst)
{
    int is_direct_branch = 1;
    if ((inst & 0xf0000000) == 0xf0000000) {
        /* NV space */
        if ((inst & 0xfe000000) == 0xfa000000){
            /* BLX (imm) */
        } else {
            is_direct_branch = 0;
        }
    } else if ((inst & 0x0e000000) == 0x0a000000) {
        /* B, BL */
    } else {
        is_direct_branch = 0;
    }
    return is_direct_branch;
}


int inst_ARM_is_indirect_branch(uint32_t inst)
{
    int is_indirect_branch = 1;
    if ((inst & 0xf0000000) == 0xf0000000) {
        /* NV space */
        if ((inst & 0xfe500000) == 0xf8100000) {
            /* RFE */
        } else {
            is_indirect_branch = 0;
        }
    } else if ((inst & 0x0ff000d0) == 0x01200010) {
        /* BLX (register), BX */
    } else if ((inst & 0x0e108000) == 0x08108000) {
        /* POP {...,pc} or LDMxx {...,pc} */
    } else if ((inst & 0x0e50f000) == 0x0410f000) {
        /* LDR PC,imm... inc. POP {PC} */
    } else if ((inst & 0x0e50f010) == 0x0610f000) {
        /* LDR PC,reg */
    } else if ((inst & 0x0fe0f000) == 0x01a0f000) {
        /* MOV PC,rx */
    } else if ((inst & 0x0f900080) == 0x01000000) {
        /* "Miscellaneous instructions" - in DP space */
        is_indirect_branch = 0;
    } else if ((inst & 0x0f9000f0) == 0x01800090) {
        /* Some extended loads and stores */
        is_indirect_branch = 0;
    } else if ((inst & 0x0fb0f000) == 0x0320f000) {
        /* MSR #imm */
        is_indirect_branch = 0;
    } else if ((inst & 0x0e00f000) == 0x0200f000) {
        /* DP PC,imm shift */
        if ((inst & 0x0f90f000) == 0x0310f000) {
            /* TST/CMP */
           is_indirect_branch = 0;
        }
    } else if ((inst & 0x0e00f000) == 0x0000f000) {
        /* DP PC,reg */
    } else {
        is_indirect_branch = 0;
    }
    return is_indirect_branch;
}


int inst_Thumb_is_direct_branch(uint32_t inst)
{
    int is_direct_branch = 1;
    if ((inst & 0xf0000000) == 0xd0000000 && (inst & 0x0e000000) != 0x0e000000) {
        /* B<c> (encoding T1) */
    } else if ((inst & 0xf8000000) == 0xe0000000) {
        /* B (encoding T2) */
    } else if ((inst & 0xf800d000) == 0xf0008000 && (inst & 0x03800000) != 0x03800000) {
        /* B (encoding T3) */
    } else if ((inst & 0xf8009000) == 0xf0009000) {
        /* B (encoding T4); BL (encoding T1) */
    } else if ((inst & 0xf800d001) == 0xf000c000) {
        /* BLX (imm) (encoding T2) */
    } else if ((inst & 0xf5000000) == 0xb1000000) {
        /* CB(NZ) */
    } else {
        is_direct_branch = 0;
    }
    return is_direct_branch;
}


int inst_Thumb_is_indirect_branch(uint32_t inst)
{
    /* See e.g. PFT Table 2-3 and Table 2-5 */
    int is_branch = 1;
    if ((inst & 0xff000000) == 0x47000000) {
        /* BX, BLX (reg) */
    } else if ((inst & 0xff000000) == 0xbd000000) {
        /* POP {pc} */
    } else if ((inst & 0xfd870000) == 0x44870000) {
        /* MOV PC,reg or ADD PC,reg */
    } else if ((inst & 0xfff0ffe0) == 0xe8d0f000) {
        /* TBB/TBH */
    } else if ((inst & 0xffd00000) == 0xe8100000) {
        /* RFE (T1) */
    } else if ((inst & 0xffd00000) == 0xe9900000) {
        /* RFE (T2) */
    } else if ((inst & 0xfff0d000) == 0xf3d08000) {
        /* SUBS PC,LR,#imm inc.ERET */
    } else if ((inst & 0xfff0f000) == 0xf8d0f000) {
        /* LDR PC,imm (T3) */
    } else if ((inst & 0xff7ff000) == 0xf85ff000) {
        /* LDR PC,literal (T2) */
    } else if ((inst & 0xfff0f800) == 0xf850f800) {
        /* LDR PC,imm (T4) */
    } else if ((inst & 0xfff0ffc0) == 0xf850f000) {
        /* LDR PC,reg (T2) */
    } else if ((inst & 0xfe508000) == 0xe8108000) {
        /* LDM PC */
    } else {
        is_branch = 0;
    }
    return is_branch;
}


int inst_A64_is_direct_branch(uint32_t inst)
{
    int is_direct_branch = 1;
    if ((inst & 0x7c000000) == 0x34000000) {
        /* CB, TB */
    } else if ((inst & 0xff000010) == 0x54000000) {
        /* B<cond> */
    } else if ((inst & 0x7c000000) == 0x14000000) {
        /* B, BL imm */
    } else {
        is_direct_branch = 0;
    }
    return is_direct_branch;
}


int inst_A64_is_indirect_branch(uint32_t inst)
{
    int is_indirect_branch = 1;
    if ((inst & 0xffdffc1f) == 0xd61f0000) {
        /* BR, BLR */
    } else if ((inst & 0xfffffc1f) == 0xd65f0000) {
        instr_sub_type = OCSD_S_INSTR_V8_RET;
        /* RET */
    } else if ((inst & 0xffffffff) == 0xd69f03e0) {
        /* ERET */
        instr_sub_type = OCSD_S_INSTR_V8_ERET;
    } else {
        is_indirect_branch = 0;
    }
    return is_indirect_branch;
}


int inst_ARM_branch_destination(uint32_t addr, uint32_t inst, uint32_t *pnpc)
{
    uint32_t npc;
    int is_direct_branch = 1;
    if ((inst & 0x0e000000) == 0x0a000000) {
        /*
          B:   cccc:1010:imm24
          BL:  cccc:1011:imm24
          BLX: 1111:101H:imm24
        */
        npc = addr + 8 + ((int32_t)((inst & 0xffffff) << 8) >> 6);
        if ((inst & 0xf0000000) == 0xf0000000) {
            npc |= 1;  /* indicate ISA is now Thumb */
            npc |= ((inst >> 23) & 2);   /* apply the H bit */
        }
    } else {
        is_direct_branch = 0;
    }
    if (is_direct_branch && pnpc != NULL) {
        *pnpc = npc;
    }
    return is_direct_branch;
}


int inst_Thumb_branch_destination(uint32_t addr, uint32_t inst, uint32_t *pnpc)
{
    uint32_t npc;
    int is_direct_branch = 1;
    if ((inst & 0xf0000000) == 0xd0000000 && (inst & 0x0e000000) != 0x0e000000) {
        /* B<c> (encoding T1) */
        npc = addr + 4 + ((int32_t)((inst & 0x00ff0000) << 8) >> 23);
        npc |= 1;
    } else if ((inst & 0xf8000000) == 0xe0000000) {
        /* B (encoding T2) */
        npc = addr + 4 + ((int32_t)((inst & 0x07ff0000) << 5) >> 20);
        npc |= 1;
    } else if ((inst & 0xf800d000) == 0xf0008000 && (inst & 0x03800000) != 0x03800000) {
        /* B (encoding T3) */
        npc = addr + 4 + ((int32_t)(((inst & 0x04000000) << 5) |
                                    ((inst & 0x0800) << 19) |
                                    ((inst & 0x2000) << 16) |
                                    ((inst & 0x003f0000) << 7) |
                                    ((inst & 0x000007ff) << 12)) >> 11);
        npc |= 1;
    } else if ((inst & 0xf8009000) == 0xf0009000) {
        /* B (encoding T4); BL (encoding T1) */
        uint32_t S = ((inst & 0x04000000) >> 26)-1;  /* ffffffff or 0 according to S bit */
        npc = addr + 4 + ((int32_t)(((inst & 0x04000000) << 5) |
                                    (((inst^S) & 0x2000) << 17) |
                                    (((inst^S) & 0x0800) << 18) |
                                    ((inst & 0x03ff0000) << 3) |
                                    ((inst & 0x000007ff) << 8)) >> 7);
        npc |= 1;
    } else if ((inst & 0xf800d001) == 0xf000c000) {
        /* BLX (encoding T2) */
        uint32_t S = ((inst & 0x04000000) >> 26)-1;  /* ffffffff or 0 according to S bit */
        addr &= 0xfffffffc;   /* Align(PC,4) */
        npc = addr + 4 + ((int32_t)(((inst & 0x04000000) << 5) |
                                    (((inst^S) & 0x2000) << 17) |
                                    (((inst^S) & 0x0800) << 18) |
                                    ((inst & 0x03ff0000) << 3) |
                                    ((inst & 0x000007fe) << 8)) >> 7);
        /* don't set the Thumb bit, as we're transferring to ARM */
    } else if ((inst & 0xf5000000) == 0xb1000000) {
        /* CB(NZ) */
        /* Note that it's zero-extended - always a forward branch */
        npc = addr + 4 + ((((inst & 0x02000000) << 6) |
                           ((inst & 0x00f80000) << 7)) >> 25);
        npc |= 1;
    } else {
        is_direct_branch = 0;
    }
    if (is_direct_branch && pnpc != NULL) {
        *pnpc = npc;
    }
    return is_direct_branch;
}


int inst_A64_branch_destination(uint64_t addr, uint32_t inst, uint64_t *pnpc)
{
    uint64_t npc;
    int is_direct_branch = 1;
    if ((inst & 0xff000010) == 0x54000000) {
        /* B<cond> */
        npc = addr + ((int32_t)((inst & 0x00ffffe0) << 8) >> 11);
    } else if ((inst & 0x7c000000) == 0x14000000) {
        /* B, BL imm */
        npc = addr + ((int32_t)((inst & 0x03ffffff) << 6) >> 4);
    } else if ((inst & 0x7e000000) == 0x34000000) {
        /* CB */
        npc = addr + ((int32_t)((inst & 0x00ffffe0) << 8) >> 11);
    } else if ((inst & 0x7e000000) == 0x36000000) {
        /* TB */
        npc = addr + ((int32_t)((inst & 0x0007ffe0) << 13) >> 16);
    } else {
        is_direct_branch = 0;
    }
    if (is_direct_branch && pnpc != NULL) {
        *pnpc = npc;
    }
    return is_direct_branch;
}

int inst_ARM_is_branch(uint32_t inst)
{
    return inst_ARM_is_indirect_branch(inst) ||
           inst_ARM_is_direct_branch(inst);
}


int inst_Thumb_is_branch(uint32_t inst)
{
    return inst_Thumb_is_indirect_branch(inst) ||
           inst_Thumb_is_direct_branch(inst);
}


int inst_A64_is_branch(uint32_t inst)
{
    return inst_A64_is_indirect_branch(inst) ||
           inst_A64_is_direct_branch(inst);
}


int inst_ARM_is_branch_and_link(uint32_t inst)
{
    int is_branch = 1;
    if ((inst & 0xf0000000) == 0xf0000000) {
        if ((inst & 0xfe000000) == 0xfa000000){
            instr_sub_type = OCSD_S_INSTR_BR_LINK;
            /* BLX (imm) */
        } else {
            is_branch = 0;
        }
    } else if ((inst & 0x0f000000) == 0x0b000000) {
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
        /* BL */
    } else if ((inst & 0x0ff000f0) == 0x01200030) {
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
        /* BLX (reg) */
    } else {
        is_branch = 0;
    }
    return is_branch;
}


int inst_Thumb_is_branch_and_link(uint32_t inst)
{
    int is_branch = 1;
    if ((inst & 0xff800000) == 0x47800000) {
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
        /* BLX (reg) */
    } else if ((inst & 0xf800c000) == 0xf000c000) {
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
        /* BL, BLX (imm) */
    } else {
        is_branch = 0;
    }
    return is_branch;
}


int inst_A64_is_branch_and_link(uint32_t inst)
{
    int is_branch = 1;
    if ((inst & 0xfffffc1f) == 0xd63f0000) {
        /* BLR */
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
    } else if ((inst & 0xfc000000) == 0x94000000) {
        /* BL */
        instr_sub_type = OCSD_S_INSTR_BR_LINK;
    } else {
        is_branch = 0;
    }
    return is_branch;
}


int inst_ARM_is_conditional(uint32_t inst)
{
    return (inst & 0xe0000000) != 0xe0000000;
}


int inst_Thumb_is_conditional(uint32_t inst)
{
    if ((inst & 0xf0000000) == 0xd0000000 && (inst & 0x0e000000) != 0x0e000000) {
        /* B<c> (encoding T1) */
        return 1;
    } else if ((inst & 0xf800d000) == 0xf0008000 && (inst & 0x03800000) != 0x03800000) {
        /* B<c> (encoding T3) */
        return 1;
    } else if ((inst & 0xf5000000) == 0xb1000000) {
        /* CB(N)Z */
        return 1;
    }
    return 0;
}


unsigned int inst_Thumb_is_IT(uint32_t inst)
{
    if ((inst & 0xff000000) == 0xbf000000 &&
        (inst & 0x000f0000) != 0x00000000) {
        if (inst & 0x00010000) {
            return 4;
        } else if (inst & 0x00020000) {
            return 3;
        } else if (inst & 0x00040000) {
            return 2;
        } else {
            assert(inst & 0x00080000);
            return 1;
        }
    } else {
        return 0;
    }
}


/*
Test whether an A64 instruction is conditional.

Instructions like CSEL, CSINV, CCMP are not classed as conditional.
They use the condition code but do one of two things with it,
neither a NOP.  The "intruction categories" section of ETMv4
lists no (non branch) conditional instructions for A64.
*/
int inst_A64_is_conditional(uint32_t inst)
{
    if ((inst & 0x7c000000) == 0x34000000) {
        /* CB, TB */
        return 1;
    } else if ((inst & 0xff000010) == 0x54000000) {
        /* B.cond */
        return 1;
    }
    return 0;
}


arm_barrier_t inst_ARM_barrier(uint32_t inst)
{
    if ((inst & 0xfff00000) == 0xf5700000) {
        switch (inst & 0xf0) {
        case 0x40:
            return ARM_BARRIER_DSB;
        case 0x50:
            return ARM_BARRIER_DMB;
        case 0x60:
            return ARM_BARRIER_ISB;
        default:
            return ARM_BARRIER_NONE;
        }
    } else if ((inst & 0x0fff0f00) == 0x0e070f00) {
        switch (inst & 0xff) {
        case 0x9a:
            return ARM_BARRIER_DSB;   /* mcr p15,0,Rt,c7,c10,4 */
        case 0xba:
            return ARM_BARRIER_DMB;   /* mcr p15,0,Rt,c7,c10,5 */
        case 0x95:
            return ARM_BARRIER_ISB;   /* mcr p15,0,Rt,c7,c5,4 */
        default:
            return ARM_BARRIER_NONE;
        }
    } else {
        return ARM_BARRIER_NONE;
    }
}


arm_barrier_t inst_Thumb_barrier(uint32_t inst)
{
    if ((inst & 0xffffff00) == 0xf3bf8f00) {
        switch (inst & 0xf0) {
        case 0x40:
            return ARM_BARRIER_DSB;
        case 0x50:
            return ARM_BARRIER_DMB;
        case 0x60:
            return ARM_BARRIER_ISB;
        default:
            return ARM_BARRIER_NONE;
        }
    } else if ((inst & 0xffff0f00) == 0xee070f00) {
        /* Thumb2 CP15 barriers are unlikely... 1156T2 only? */
        switch (inst & 0xff) {
        case 0x9a:
            return ARM_BARRIER_DSB;   /* mcr p15,0,Rt,c7,c10,4 */
        case 0xba:
            return ARM_BARRIER_DMB;   /* mcr p15,0,Rt,c7,c10,5 */
        case 0x95:
            return ARM_BARRIER_ISB;   /* mcr p15,0,Rt,c7,c5,4 */
        default:
            return ARM_BARRIER_NONE;
        }
        return ARM_BARRIER_NONE;
    } else {
        return ARM_BARRIER_NONE;
    }
}


arm_barrier_t inst_A64_barrier(uint32_t inst)
{
    if ((inst & 0xfffff09f) == 0xd503309f) {
        switch (inst & 0x60) {
        case 0x0:
            return ARM_BARRIER_DSB;
        case 0x20:
            return ARM_BARRIER_DMB;
        case 0x40:
            return ARM_BARRIER_ISB;
        default:
            return ARM_BARRIER_NONE;
        }
    } else {
        return ARM_BARRIER_NONE;
    }
}


int inst_ARM_is_UDF(uint32_t inst)
{
    return (inst & 0xfff000f0) == 0xe7f000f0;
}


int inst_Thumb_is_UDF(uint32_t inst)
{
    return (inst & 0xff000000) == 0xde000000 ||   /* T1 */
           (inst & 0xfff0f000) == 0xf7f0a000;     /* T2 */
}


int inst_A64_is_UDF(uint32_t inst)
{
    /* No A64 encodings are formally allocated as permanently undefined,
       but it is intended not to allocate any instructions in the 21-bit
       regions at the bottom or top of the range. */
    return (inst & 0xffe00000) == 0x00000000 ||
           (inst & 0xffe00000) == 0xffe00000;
}

/* End of File trc_idec_arminst.cpp */

/*
 * Copyright (C) 2016  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __VEGA10_SDMA_PKT_OPEN_H_
#define __VEGA10_SDMA_PKT_OPEN_H_

#define SDMA_OP_NOP  0
#define SDMA_OP_COPY  1
#define SDMA_OP_WRITE  2
#define SDMA_OP_INDIRECT  4
#define SDMA_OP_FENCE  5
#define SDMA_OP_TRAP  6
#define SDMA_OP_SEM  7
#define SDMA_OP_POLL_REGMEM  8
#define SDMA_OP_COND_EXE  9
#define SDMA_OP_ATOMIC  10
#define SDMA_OP_CONST_FILL  11
#define SDMA_OP_PTEPDE  12
#define SDMA_OP_TIMESTAMP  13
#define SDMA_OP_SRBM_WRITE  14
#define SDMA_OP_PRE_EXE  15
#define SDMA_OP_DUMMY_TRAP  16
#define SDMA_SUBOP_TIMESTAMP_SET  0
#define SDMA_SUBOP_TIMESTAMP_GET  1
#define SDMA_SUBOP_TIMESTAMP_GET_GLOBAL  2
#define SDMA_SUBOP_COPY_LINEAR  0
#define SDMA_SUBOP_COPY_LINEAR_SUB_WIND  4
#define SDMA_SUBOP_COPY_TILED  1
#define SDMA_SUBOP_COPY_TILED_SUB_WIND  5
#define SDMA_SUBOP_COPY_T2T_SUB_WIND  6
#define SDMA_SUBOP_COPY_SOA  3
#define SDMA_SUBOP_COPY_DIRTY_PAGE  7
#define SDMA_SUBOP_COPY_LINEAR_PHY  8
#define SDMA_SUBOP_WRITE_LINEAR  0
#define SDMA_SUBOP_WRITE_TILED  1
#define SDMA_SUBOP_PTEPDE_GEN  0
#define SDMA_SUBOP_PTEPDE_COPY  1
#define SDMA_SUBOP_PTEPDE_RMW  2
#define SDMA_SUBOP_PTEPDE_COPY_BACKWARDS  3
#define SDMA_SUBOP_DATA_FILL_MULTI  1
#define SDMA_SUBOP_POLL_REG_WRITE_MEM  1
#define SDMA_SUBOP_POLL_DBIT_WRITE_MEM  2
#define SDMA_SUBOP_POLL_MEM_VERIFY  3
#define HEADER_AGENT_DISPATCH  4
#define HEADER_BARRIER  5
#define SDMA_OP_AQL_COPY  0
#define SDMA_OP_AQL_BARRIER_OR  0
/* vm invalidation is only available for GC9.4.3/GC9.4.4/GC9.5.0 */
#define SDMA_OP_VM_INVALIDATE 8
#define SDMA_SUBOP_VM_INVALIDATE 4

/*define for op field*/
#define SDMA_PKT_HEADER_op_offset 0
#define SDMA_PKT_HEADER_op_mask   0x000000FF
#define SDMA_PKT_HEADER_op_shift  0
#define SDMA_PKT_HEADER_OP(x) (((x) & SDMA_PKT_HEADER_op_mask) << SDMA_PKT_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_HEADER_sub_op_offset 0
#define SDMA_PKT_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_HEADER_sub_op_shift  8
#define SDMA_PKT_HEADER_SUB_OP(x) (((x) & SDMA_PKT_HEADER_sub_op_mask) << SDMA_PKT_HEADER_sub_op_shift)


/*
** Definitions for SDMA_PKT_COPY_LINEAR packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_LINEAR_HEADER_op_offset 0
#define SDMA_PKT_COPY_LINEAR_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_LINEAR_HEADER_op_shift  0
#define SDMA_PKT_COPY_LINEAR_HEADER_OP(x) (((x) & SDMA_PKT_COPY_LINEAR_HEADER_op_mask) << SDMA_PKT_COPY_LINEAR_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_LINEAR_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_LINEAR_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_LINEAR_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_LINEAR_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_LINEAR_HEADER_sub_op_mask) << SDMA_PKT_COPY_LINEAR_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_COPY_LINEAR_HEADER_encrypt_offset 0
#define SDMA_PKT_COPY_LINEAR_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_COPY_LINEAR_HEADER_encrypt_shift  16
#define SDMA_PKT_COPY_LINEAR_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_COPY_LINEAR_HEADER_encrypt_mask) << SDMA_PKT_COPY_LINEAR_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_LINEAR_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_LINEAR_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_LINEAR_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_LINEAR_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_LINEAR_HEADER_tmz_mask) << SDMA_PKT_COPY_LINEAR_HEADER_tmz_shift)

/*define for broadcast field*/
#define SDMA_PKT_COPY_LINEAR_HEADER_broadcast_offset 0
#define SDMA_PKT_COPY_LINEAR_HEADER_broadcast_mask   0x00000001
#define SDMA_PKT_COPY_LINEAR_HEADER_broadcast_shift  27
#define SDMA_PKT_COPY_LINEAR_HEADER_BROADCAST(x) (((x) & SDMA_PKT_COPY_LINEAR_HEADER_broadcast_mask) << SDMA_PKT_COPY_LINEAR_HEADER_broadcast_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_LINEAR_COUNT_count_offset 1
#define SDMA_PKT_COPY_LINEAR_COUNT_count_mask   0x003FFFFF
#define SDMA_PKT_COPY_LINEAR_COUNT_count_shift  0
#define SDMA_PKT_COPY_LINEAR_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_LINEAR_COUNT_count_mask) << SDMA_PKT_COPY_LINEAR_COUNT_count_shift)

/*define for PARAMETER word*/
/*define for dst_sw field*/
#define SDMA_PKT_COPY_LINEAR_PARAMETER_dst_sw_offset 2
#define SDMA_PKT_COPY_LINEAR_PARAMETER_dst_sw_mask   0x00000003
#define SDMA_PKT_COPY_LINEAR_PARAMETER_dst_sw_shift  16
#define SDMA_PKT_COPY_LINEAR_PARAMETER_DST_SW(x) (((x) & SDMA_PKT_COPY_LINEAR_PARAMETER_dst_sw_mask) << SDMA_PKT_COPY_LINEAR_PARAMETER_dst_sw_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_LINEAR_PARAMETER_src_sw_offset 2
#define SDMA_PKT_COPY_LINEAR_PARAMETER_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_LINEAR_PARAMETER_src_sw_shift  24
#define SDMA_PKT_COPY_LINEAR_PARAMETER_SRC_SW(x) (((x) & SDMA_PKT_COPY_LINEAR_PARAMETER_src_sw_mask) << SDMA_PKT_COPY_LINEAR_PARAMETER_src_sw_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_offset 3
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_offset 4
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_offset 5
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_offset 6
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_COPY_DIRTY_PAGE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_op_offset 0
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_op_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_OP(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_HEADER_op_mask) << SDMA_PKT_COPY_DIRTY_PAGE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_HEADER_sub_op_mask) << SDMA_PKT_COPY_DIRTY_PAGE_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_HEADER_tmz_mask) << SDMA_PKT_COPY_DIRTY_PAGE_HEADER_tmz_shift)

/*define for all field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_all_offset 0
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_all_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_all_shift  31
#define SDMA_PKT_COPY_DIRTY_PAGE_HEADER_ALL(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_HEADER_all_mask) << SDMA_PKT_COPY_DIRTY_PAGE_HEADER_all_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_COUNT_count_offset 1
#define SDMA_PKT_COPY_DIRTY_PAGE_COUNT_count_mask   0x003FFFFF
#define SDMA_PKT_COPY_DIRTY_PAGE_COUNT_count_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_COUNT_count_mask) << SDMA_PKT_COPY_DIRTY_PAGE_COUNT_count_shift)

/*define for PARAMETER word*/
/*define for dst_sw field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sw_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sw_mask   0x00000003
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sw_shift  16
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_DST_SW(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sw_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sw_shift)

/*define for dst_gcc field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gcc_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gcc_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gcc_shift  19
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_DST_GCC(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gcc_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gcc_shift)

/*define for dst_sys field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sys_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sys_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sys_shift  20
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_DST_SYS(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sys_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_sys_shift)

/*define for dst_snoop field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_snoop_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_snoop_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_snoop_shift  22
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_DST_SNOOP(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_snoop_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_snoop_shift)

/*define for dst_gpa field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gpa_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gpa_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gpa_shift  23
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_DST_GPA(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gpa_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_dst_gpa_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sw_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sw_shift  24
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_SRC_SW(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sw_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sw_shift)

/*define for src_sys field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sys_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sys_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sys_shift  28
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_SRC_SYS(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sys_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_sys_shift)

/*define for src_snoop field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_snoop_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_snoop_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_snoop_shift  30
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_SRC_SNOOP(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_snoop_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_snoop_shift)

/*define for src_gpa field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_gpa_offset 2
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_gpa_mask   0x00000001
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_gpa_shift  31
#define SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_SRC_GPA(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_gpa_mask) << SDMA_PKT_COPY_DIRTY_PAGE_PARAMETER_src_gpa_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_src_addr_31_0_offset 3
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_src_addr_63_32_offset 4
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_DIRTY_PAGE_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_dst_addr_31_0_offset 5
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_dst_addr_63_32_offset 6
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_COPY_DIRTY_PAGE_DST_ADDR_HI_dst_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_COPY_PHYSICAL_LINEAR packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_op_offset 0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_op_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_OP(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_op_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_sub_op_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_tmz_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_HEADER_tmz_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_count_offset 1
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_count_mask   0x003FFFFF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_count_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_count_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_COUNT_count_shift)

/*define for PARAMETER word*/
/*define for dst_sw field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sw_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sw_mask   0x00000003
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sw_shift  16
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_SW(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sw_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sw_shift)

/*define for dst_gcc field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gcc_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gcc_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gcc_shift  19
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_GCC(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gcc_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gcc_shift)

/*define for dst_sys field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sys_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sys_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sys_shift  20
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_SYS(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sys_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_sys_shift)

/*define for dst_log field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_log_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_log_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_log_shift  21
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_LOG(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_log_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_log_shift)

/*define for dst_snoop field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_snoop_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_snoop_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_snoop_shift  22
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_SNOOP(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_snoop_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_snoop_shift)

/*define for dst_gpa field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gpa_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gpa_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gpa_shift  23
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_DST_GPA(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gpa_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_dst_gpa_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sw_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sw_shift  24
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_SRC_SW(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sw_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sw_shift)

/*define for src_gcc field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gcc_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gcc_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gcc_shift  27
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_SRC_GCC(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gcc_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gcc_shift)

/*define for src_sys field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sys_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sys_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sys_shift  28
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_SRC_SYS(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sys_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_sys_shift)

/*define for src_snoop field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_snoop_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_snoop_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_snoop_shift  30
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_SRC_SNOOP(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_snoop_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_snoop_shift)

/*define for src_gpa field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gpa_offset 2
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gpa_mask   0x00000001
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gpa_shift  31
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_SRC_GPA(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gpa_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_PARAMETER_src_gpa_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_src_addr_31_0_offset 3
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_src_addr_63_32_offset 4
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_dst_addr_31_0_offset 5
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_dst_addr_63_32_offset 6
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_COPY_PHYSICAL_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_COPY_BROADCAST_LINEAR packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_op_offset 0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_op_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_OP(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_op_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_sub_op_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_encrypt_offset 0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_encrypt_shift  16
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_encrypt_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_tmz_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_tmz_shift)

/*define for broadcast field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_broadcast_offset 0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_broadcast_mask   0x00000001
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_broadcast_shift  27
#define SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_BROADCAST(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_broadcast_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_HEADER_broadcast_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_count_offset 1
#define SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_count_mask   0x003FFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_count_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_count_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_COUNT_count_shift)

/*define for PARAMETER word*/
/*define for dst2_sw field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst2_sw_offset 2
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst2_sw_mask   0x00000003
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst2_sw_shift  8
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_DST2_SW(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst2_sw_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst2_sw_shift)

/*define for dst1_sw field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst1_sw_offset 2
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst1_sw_mask   0x00000003
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst1_sw_shift  16
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_DST1_SW(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst1_sw_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_dst1_sw_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_src_sw_offset 2
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_src_sw_shift  24
#define SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_SRC_SW(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_src_sw_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_PARAMETER_src_sw_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_src_addr_31_0_offset 3
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_src_addr_63_32_offset 4
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST1_ADDR_LO word*/
/*define for dst1_addr_31_0 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_dst1_addr_31_0_offset 5
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_dst1_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_dst1_addr_31_0_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_DST1_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_dst1_addr_31_0_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_LO_dst1_addr_31_0_shift)

/*define for DST1_ADDR_HI word*/
/*define for dst1_addr_63_32 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_dst1_addr_63_32_offset 6
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_dst1_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_dst1_addr_63_32_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_DST1_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_dst1_addr_63_32_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_DST1_ADDR_HI_dst1_addr_63_32_shift)

/*define for DST2_ADDR_LO word*/
/*define for dst2_addr_31_0 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_dst2_addr_31_0_offset 7
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_dst2_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_dst2_addr_31_0_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_DST2_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_dst2_addr_31_0_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_LO_dst2_addr_31_0_shift)

/*define for DST2_ADDR_HI word*/
/*define for dst2_addr_63_32 field*/
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_dst2_addr_63_32_offset 8
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_dst2_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_dst2_addr_63_32_shift  0
#define SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_DST2_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_dst2_addr_63_32_mask) << SDMA_PKT_COPY_BROADCAST_LINEAR_DST2_ADDR_HI_dst2_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_COPY_LINEAR_SUBWIN packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_op_offset 0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_op_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_OP(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_op_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_sub_op_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_tmz_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_tmz_shift)

/*define for elementsize field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_elementsize_offset 0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_elementsize_mask   0x00000007
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_elementsize_shift  29
#define SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_ELEMENTSIZE(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_elementsize_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_HEADER_elementsize_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_src_addr_31_0_offset 1
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_src_addr_63_32_offset 2
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DW_3 word*/
/*define for src_x field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_x_offset 3
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_x_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_x_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_SRC_X(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_x_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_x_shift)

/*define for src_y field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_y_offset 3
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_y_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_y_shift  16
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_SRC_Y(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_y_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_3_src_y_shift)

/*define for DW_4 word*/
/*define for src_z field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_z_offset 4
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_z_mask   0x000007FF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_z_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_SRC_Z(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_z_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_z_shift)

/*define for src_pitch field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_pitch_offset 4
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_pitch_mask   0x0007FFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_pitch_shift  13
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_SRC_PITCH(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_pitch_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_4_src_pitch_shift)

/*define for DW_5 word*/
/*define for src_slice_pitch field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_src_slice_pitch_offset 5
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_src_slice_pitch_mask   0x0FFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_src_slice_pitch_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_SRC_SLICE_PITCH(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_src_slice_pitch_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_5_src_slice_pitch_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_dst_addr_31_0_offset 6
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_dst_addr_63_32_offset 7
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for DW_8 word*/
/*define for dst_x field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_x_offset 8
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_x_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_x_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_DST_X(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_x_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_x_shift)

/*define for dst_y field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_y_offset 8
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_y_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_y_shift  16
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_DST_Y(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_y_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_8_dst_y_shift)

/*define for DW_9 word*/
/*define for dst_z field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_z_offset 9
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_z_mask   0x000007FF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_z_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_DST_Z(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_z_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_z_shift)

/*define for dst_pitch field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_pitch_offset 9
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_pitch_mask   0x0007FFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_pitch_shift  13
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_DST_PITCH(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_pitch_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_9_dst_pitch_shift)

/*define for DW_10 word*/
/*define for dst_slice_pitch field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_dst_slice_pitch_offset 10
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_dst_slice_pitch_mask   0x0FFFFFFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_dst_slice_pitch_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_DST_SLICE_PITCH(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_dst_slice_pitch_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_10_dst_slice_pitch_shift)

/*define for DW_11 word*/
/*define for rect_x field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_x_offset 11
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_x_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_x_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_RECT_X(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_x_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_x_shift)

/*define for rect_y field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_y_offset 11
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_y_mask   0x00003FFF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_y_shift  16
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_RECT_Y(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_y_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_11_rect_y_shift)

/*define for DW_12 word*/
/*define for rect_z field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_rect_z_offset 12
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_rect_z_mask   0x000007FF
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_rect_z_shift  0
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_RECT_Z(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_rect_z_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_rect_z_shift)

/*define for dst_sw field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_dst_sw_offset 12
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_dst_sw_mask   0x00000003
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_dst_sw_shift  16
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_DST_SW(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_dst_sw_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_dst_sw_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_src_sw_offset 12
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_src_sw_shift  24
#define SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_SRC_SW(x) (((x) & SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_src_sw_mask) << SDMA_PKT_COPY_LINEAR_SUBWIN_DW_12_src_sw_shift)


/*
** Definitions for SDMA_PKT_COPY_TILED packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_TILED_HEADER_op_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_TILED_HEADER_op_shift  0
#define SDMA_PKT_COPY_TILED_HEADER_OP(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_op_mask) << SDMA_PKT_COPY_TILED_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_TILED_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_TILED_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_TILED_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_sub_op_mask) << SDMA_PKT_COPY_TILED_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_COPY_TILED_HEADER_encrypt_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_COPY_TILED_HEADER_encrypt_shift  16
#define SDMA_PKT_COPY_TILED_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_encrypt_mask) << SDMA_PKT_COPY_TILED_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_TILED_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_TILED_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_TILED_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_tmz_mask) << SDMA_PKT_COPY_TILED_HEADER_tmz_shift)

/*define for mip_max field*/
#define SDMA_PKT_COPY_TILED_HEADER_mip_max_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_mip_max_mask   0x0000000F
#define SDMA_PKT_COPY_TILED_HEADER_mip_max_shift  20
#define SDMA_PKT_COPY_TILED_HEADER_MIP_MAX(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_mip_max_mask) << SDMA_PKT_COPY_TILED_HEADER_mip_max_shift)

/*define for detile field*/
#define SDMA_PKT_COPY_TILED_HEADER_detile_offset 0
#define SDMA_PKT_COPY_TILED_HEADER_detile_mask   0x00000001
#define SDMA_PKT_COPY_TILED_HEADER_detile_shift  31
#define SDMA_PKT_COPY_TILED_HEADER_DETILE(x) (((x) & SDMA_PKT_COPY_TILED_HEADER_detile_mask) << SDMA_PKT_COPY_TILED_HEADER_detile_shift)

/*define for TILED_ADDR_LO word*/
/*define for tiled_addr_31_0 field*/
#define SDMA_PKT_COPY_TILED_TILED_ADDR_LO_tiled_addr_31_0_offset 1
#define SDMA_PKT_COPY_TILED_TILED_ADDR_LO_tiled_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_TILED_ADDR_LO_tiled_addr_31_0_shift  0
#define SDMA_PKT_COPY_TILED_TILED_ADDR_LO_TILED_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_TILED_TILED_ADDR_LO_tiled_addr_31_0_mask) << SDMA_PKT_COPY_TILED_TILED_ADDR_LO_tiled_addr_31_0_shift)

/*define for TILED_ADDR_HI word*/
/*define for tiled_addr_63_32 field*/
#define SDMA_PKT_COPY_TILED_TILED_ADDR_HI_tiled_addr_63_32_offset 2
#define SDMA_PKT_COPY_TILED_TILED_ADDR_HI_tiled_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_TILED_ADDR_HI_tiled_addr_63_32_shift  0
#define SDMA_PKT_COPY_TILED_TILED_ADDR_HI_TILED_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_TILED_TILED_ADDR_HI_tiled_addr_63_32_mask) << SDMA_PKT_COPY_TILED_TILED_ADDR_HI_tiled_addr_63_32_shift)

/*define for DW_3 word*/
/*define for width field*/
#define SDMA_PKT_COPY_TILED_DW_3_width_offset 3
#define SDMA_PKT_COPY_TILED_DW_3_width_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_DW_3_width_shift  0
#define SDMA_PKT_COPY_TILED_DW_3_WIDTH(x) (((x) & SDMA_PKT_COPY_TILED_DW_3_width_mask) << SDMA_PKT_COPY_TILED_DW_3_width_shift)

/*define for DW_4 word*/
/*define for height field*/
#define SDMA_PKT_COPY_TILED_DW_4_height_offset 4
#define SDMA_PKT_COPY_TILED_DW_4_height_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_DW_4_height_shift  0
#define SDMA_PKT_COPY_TILED_DW_4_HEIGHT(x) (((x) & SDMA_PKT_COPY_TILED_DW_4_height_mask) << SDMA_PKT_COPY_TILED_DW_4_height_shift)

/*define for depth field*/
#define SDMA_PKT_COPY_TILED_DW_4_depth_offset 4
#define SDMA_PKT_COPY_TILED_DW_4_depth_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_DW_4_depth_shift  16
#define SDMA_PKT_COPY_TILED_DW_4_DEPTH(x) (((x) & SDMA_PKT_COPY_TILED_DW_4_depth_mask) << SDMA_PKT_COPY_TILED_DW_4_depth_shift)

/*define for DW_5 word*/
/*define for element_size field*/
#define SDMA_PKT_COPY_TILED_DW_5_element_size_offset 5
#define SDMA_PKT_COPY_TILED_DW_5_element_size_mask   0x00000007
#define SDMA_PKT_COPY_TILED_DW_5_element_size_shift  0
#define SDMA_PKT_COPY_TILED_DW_5_ELEMENT_SIZE(x) (((x) & SDMA_PKT_COPY_TILED_DW_5_element_size_mask) << SDMA_PKT_COPY_TILED_DW_5_element_size_shift)

/*define for swizzle_mode field*/
#define SDMA_PKT_COPY_TILED_DW_5_swizzle_mode_offset 5
#define SDMA_PKT_COPY_TILED_DW_5_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_COPY_TILED_DW_5_swizzle_mode_shift  3
#define SDMA_PKT_COPY_TILED_DW_5_SWIZZLE_MODE(x) (((x) & SDMA_PKT_COPY_TILED_DW_5_swizzle_mode_mask) << SDMA_PKT_COPY_TILED_DW_5_swizzle_mode_shift)

/*define for dimension field*/
#define SDMA_PKT_COPY_TILED_DW_5_dimension_offset 5
#define SDMA_PKT_COPY_TILED_DW_5_dimension_mask   0x00000003
#define SDMA_PKT_COPY_TILED_DW_5_dimension_shift  9
#define SDMA_PKT_COPY_TILED_DW_5_DIMENSION(x) (((x) & SDMA_PKT_COPY_TILED_DW_5_dimension_mask) << SDMA_PKT_COPY_TILED_DW_5_dimension_shift)

/*define for epitch field*/
#define SDMA_PKT_COPY_TILED_DW_5_epitch_offset 5
#define SDMA_PKT_COPY_TILED_DW_5_epitch_mask   0x0000FFFF
#define SDMA_PKT_COPY_TILED_DW_5_epitch_shift  16
#define SDMA_PKT_COPY_TILED_DW_5_EPITCH(x) (((x) & SDMA_PKT_COPY_TILED_DW_5_epitch_mask) << SDMA_PKT_COPY_TILED_DW_5_epitch_shift)

/*define for DW_6 word*/
/*define for x field*/
#define SDMA_PKT_COPY_TILED_DW_6_x_offset 6
#define SDMA_PKT_COPY_TILED_DW_6_x_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_DW_6_x_shift  0
#define SDMA_PKT_COPY_TILED_DW_6_X(x) (((x) & SDMA_PKT_COPY_TILED_DW_6_x_mask) << SDMA_PKT_COPY_TILED_DW_6_x_shift)

/*define for y field*/
#define SDMA_PKT_COPY_TILED_DW_6_y_offset 6
#define SDMA_PKT_COPY_TILED_DW_6_y_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_DW_6_y_shift  16
#define SDMA_PKT_COPY_TILED_DW_6_Y(x) (((x) & SDMA_PKT_COPY_TILED_DW_6_y_mask) << SDMA_PKT_COPY_TILED_DW_6_y_shift)

/*define for DW_7 word*/
/*define for z field*/
#define SDMA_PKT_COPY_TILED_DW_7_z_offset 7
#define SDMA_PKT_COPY_TILED_DW_7_z_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_DW_7_z_shift  0
#define SDMA_PKT_COPY_TILED_DW_7_Z(x) (((x) & SDMA_PKT_COPY_TILED_DW_7_z_mask) << SDMA_PKT_COPY_TILED_DW_7_z_shift)

/*define for linear_sw field*/
#define SDMA_PKT_COPY_TILED_DW_7_linear_sw_offset 7
#define SDMA_PKT_COPY_TILED_DW_7_linear_sw_mask   0x00000003
#define SDMA_PKT_COPY_TILED_DW_7_linear_sw_shift  16
#define SDMA_PKT_COPY_TILED_DW_7_LINEAR_SW(x) (((x) & SDMA_PKT_COPY_TILED_DW_7_linear_sw_mask) << SDMA_PKT_COPY_TILED_DW_7_linear_sw_shift)

/*define for tile_sw field*/
#define SDMA_PKT_COPY_TILED_DW_7_tile_sw_offset 7
#define SDMA_PKT_COPY_TILED_DW_7_tile_sw_mask   0x00000003
#define SDMA_PKT_COPY_TILED_DW_7_tile_sw_shift  24
#define SDMA_PKT_COPY_TILED_DW_7_TILE_SW(x) (((x) & SDMA_PKT_COPY_TILED_DW_7_tile_sw_mask) << SDMA_PKT_COPY_TILED_DW_7_tile_sw_shift)

/*define for LINEAR_ADDR_LO word*/
/*define for linear_addr_31_0 field*/
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_linear_addr_31_0_offset 8
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_linear_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_linear_addr_31_0_shift  0
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_LINEAR_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_linear_addr_31_0_mask) << SDMA_PKT_COPY_TILED_LINEAR_ADDR_LO_linear_addr_31_0_shift)

/*define for LINEAR_ADDR_HI word*/
/*define for linear_addr_63_32 field*/
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_linear_addr_63_32_offset 9
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_linear_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_linear_addr_63_32_shift  0
#define SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_LINEAR_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_linear_addr_63_32_mask) << SDMA_PKT_COPY_TILED_LINEAR_ADDR_HI_linear_addr_63_32_shift)

/*define for LINEAR_PITCH word*/
/*define for linear_pitch field*/
#define SDMA_PKT_COPY_TILED_LINEAR_PITCH_linear_pitch_offset 10
#define SDMA_PKT_COPY_TILED_LINEAR_PITCH_linear_pitch_mask   0x0007FFFF
#define SDMA_PKT_COPY_TILED_LINEAR_PITCH_linear_pitch_shift  0
#define SDMA_PKT_COPY_TILED_LINEAR_PITCH_LINEAR_PITCH(x) (((x) & SDMA_PKT_COPY_TILED_LINEAR_PITCH_linear_pitch_mask) << SDMA_PKT_COPY_TILED_LINEAR_PITCH_linear_pitch_shift)

/*define for LINEAR_SLICE_PITCH word*/
/*define for linear_slice_pitch field*/
#define SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_linear_slice_pitch_offset 11
#define SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_linear_slice_pitch_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_linear_slice_pitch_shift  0
#define SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_LINEAR_SLICE_PITCH(x) (((x) & SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_linear_slice_pitch_mask) << SDMA_PKT_COPY_TILED_LINEAR_SLICE_PITCH_linear_slice_pitch_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_TILED_COUNT_count_offset 12
#define SDMA_PKT_COPY_TILED_COUNT_count_mask   0x000FFFFF
#define SDMA_PKT_COPY_TILED_COUNT_count_shift  0
#define SDMA_PKT_COPY_TILED_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_TILED_COUNT_count_mask) << SDMA_PKT_COPY_TILED_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_COPY_L2T_BROADCAST packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_op_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_op_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_OP(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_op_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_sub_op_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_encrypt_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_encrypt_shift  16
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_encrypt_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_tmz_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_tmz_shift)

/*define for mip_max field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_mip_max_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_mip_max_mask   0x0000000F
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_mip_max_shift  20
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_MIP_MAX(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_mip_max_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_mip_max_shift)

/*define for videocopy field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_videocopy_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_videocopy_mask   0x00000001
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_videocopy_shift  26
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_VIDEOCOPY(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_videocopy_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_videocopy_shift)

/*define for broadcast field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_broadcast_offset 0
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_broadcast_mask   0x00000001
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_broadcast_shift  27
#define SDMA_PKT_COPY_L2T_BROADCAST_HEADER_BROADCAST(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_HEADER_broadcast_mask) << SDMA_PKT_COPY_L2T_BROADCAST_HEADER_broadcast_shift)

/*define for TILED_ADDR_LO_0 word*/
/*define for tiled_addr0_31_0 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_tiled_addr0_31_0_offset 1
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_tiled_addr0_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_tiled_addr0_31_0_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_TILED_ADDR0_31_0(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_tiled_addr0_31_0_mask) << SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_0_tiled_addr0_31_0_shift)

/*define for TILED_ADDR_HI_0 word*/
/*define for tiled_addr0_63_32 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_tiled_addr0_63_32_offset 2
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_tiled_addr0_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_tiled_addr0_63_32_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_TILED_ADDR0_63_32(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_tiled_addr0_63_32_mask) << SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_0_tiled_addr0_63_32_shift)

/*define for TILED_ADDR_LO_1 word*/
/*define for tiled_addr1_31_0 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_tiled_addr1_31_0_offset 3
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_tiled_addr1_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_tiled_addr1_31_0_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_TILED_ADDR1_31_0(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_tiled_addr1_31_0_mask) << SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_LO_1_tiled_addr1_31_0_shift)

/*define for TILED_ADDR_HI_1 word*/
/*define for tiled_addr1_63_32 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_tiled_addr1_63_32_offset 4
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_tiled_addr1_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_tiled_addr1_63_32_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_TILED_ADDR1_63_32(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_tiled_addr1_63_32_mask) << SDMA_PKT_COPY_L2T_BROADCAST_TILED_ADDR_HI_1_tiled_addr1_63_32_shift)

/*define for DW_5 word*/
/*define for width field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_5_width_offset 5
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_5_width_mask   0x00003FFF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_5_width_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_5_WIDTH(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_5_width_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_5_width_shift)

/*define for DW_6 word*/
/*define for height field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_height_offset 6
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_height_mask   0x00003FFF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_height_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_HEIGHT(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_6_height_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_6_height_shift)

/*define for depth field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_depth_offset 6
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_depth_mask   0x000007FF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_depth_shift  16
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_6_DEPTH(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_6_depth_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_6_depth_shift)

/*define for DW_7 word*/
/*define for element_size field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_element_size_offset 7
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_element_size_mask   0x00000007
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_element_size_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_ELEMENT_SIZE(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_7_element_size_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_7_element_size_shift)

/*define for swizzle_mode field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_swizzle_mode_offset 7
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_swizzle_mode_shift  3
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_SWIZZLE_MODE(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_7_swizzle_mode_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_7_swizzle_mode_shift)

/*define for dimension field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_dimension_offset 7
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_dimension_mask   0x00000003
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_dimension_shift  9
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_DIMENSION(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_7_dimension_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_7_dimension_shift)

/*define for epitch field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_epitch_offset 7
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_epitch_mask   0x0000FFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_epitch_shift  16
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_7_EPITCH(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_7_epitch_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_7_epitch_shift)

/*define for DW_8 word*/
/*define for x field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_x_offset 8
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_x_mask   0x00003FFF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_x_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_X(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_8_x_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_8_x_shift)

/*define for y field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_y_offset 8
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_y_mask   0x00003FFF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_y_shift  16
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_8_Y(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_8_y_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_8_y_shift)

/*define for DW_9 word*/
/*define for z field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_9_z_offset 9
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_9_z_mask   0x000007FF
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_9_z_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_9_Z(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_9_z_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_9_z_shift)

/*define for DW_10 word*/
/*define for dst2_sw field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_dst2_sw_offset 10
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_dst2_sw_mask   0x00000003
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_dst2_sw_shift  8
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_DST2_SW(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_10_dst2_sw_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_10_dst2_sw_shift)

/*define for linear_sw field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_linear_sw_offset 10
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_linear_sw_mask   0x00000003
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_linear_sw_shift  16
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_LINEAR_SW(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_10_linear_sw_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_10_linear_sw_shift)

/*define for tile_sw field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_tile_sw_offset 10
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_tile_sw_mask   0x00000003
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_tile_sw_shift  24
#define SDMA_PKT_COPY_L2T_BROADCAST_DW_10_TILE_SW(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_DW_10_tile_sw_mask) << SDMA_PKT_COPY_L2T_BROADCAST_DW_10_tile_sw_shift)

/*define for LINEAR_ADDR_LO word*/
/*define for linear_addr_31_0 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_linear_addr_31_0_offset 11
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_linear_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_linear_addr_31_0_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_LINEAR_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_linear_addr_31_0_mask) << SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_LO_linear_addr_31_0_shift)

/*define for LINEAR_ADDR_HI word*/
/*define for linear_addr_63_32 field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_linear_addr_63_32_offset 12
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_linear_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_linear_addr_63_32_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_LINEAR_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_linear_addr_63_32_mask) << SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_ADDR_HI_linear_addr_63_32_shift)

/*define for LINEAR_PITCH word*/
/*define for linear_pitch field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_linear_pitch_offset 13
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_linear_pitch_mask   0x0007FFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_linear_pitch_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_LINEAR_PITCH(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_linear_pitch_mask) << SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_PITCH_linear_pitch_shift)

/*define for LINEAR_SLICE_PITCH word*/
/*define for linear_slice_pitch field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_linear_slice_pitch_offset 14
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_linear_slice_pitch_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_linear_slice_pitch_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_LINEAR_SLICE_PITCH(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_linear_slice_pitch_mask) << SDMA_PKT_COPY_L2T_BROADCAST_LINEAR_SLICE_PITCH_linear_slice_pitch_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_L2T_BROADCAST_COUNT_count_offset 15
#define SDMA_PKT_COPY_L2T_BROADCAST_COUNT_count_mask   0x000FFFFF
#define SDMA_PKT_COPY_L2T_BROADCAST_COUNT_count_shift  0
#define SDMA_PKT_COPY_L2T_BROADCAST_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_L2T_BROADCAST_COUNT_count_mask) << SDMA_PKT_COPY_L2T_BROADCAST_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_COPY_T2T packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_T2T_HEADER_op_offset 0
#define SDMA_PKT_COPY_T2T_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_T2T_HEADER_op_shift  0
#define SDMA_PKT_COPY_T2T_HEADER_OP(x) (((x) & SDMA_PKT_COPY_T2T_HEADER_op_mask) << SDMA_PKT_COPY_T2T_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_T2T_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_T2T_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_T2T_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_T2T_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_T2T_HEADER_sub_op_mask) << SDMA_PKT_COPY_T2T_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_T2T_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_T2T_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_T2T_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_T2T_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_T2T_HEADER_tmz_mask) << SDMA_PKT_COPY_T2T_HEADER_tmz_shift)

/*define for mip_max field*/
#define SDMA_PKT_COPY_T2T_HEADER_mip_max_offset 0
#define SDMA_PKT_COPY_T2T_HEADER_mip_max_mask   0x0000000F
#define SDMA_PKT_COPY_T2T_HEADER_mip_max_shift  20
#define SDMA_PKT_COPY_T2T_HEADER_MIP_MAX(x) (((x) & SDMA_PKT_COPY_T2T_HEADER_mip_max_mask) << SDMA_PKT_COPY_T2T_HEADER_mip_max_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_COPY_T2T_SRC_ADDR_LO_src_addr_31_0_offset 1
#define SDMA_PKT_COPY_T2T_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_T2T_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_COPY_T2T_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_T2T_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_COPY_T2T_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_COPY_T2T_SRC_ADDR_HI_src_addr_63_32_offset 2
#define SDMA_PKT_COPY_T2T_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_T2T_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_COPY_T2T_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_T2T_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_COPY_T2T_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DW_3 word*/
/*define for src_x field*/
#define SDMA_PKT_COPY_T2T_DW_3_src_x_offset 3
#define SDMA_PKT_COPY_T2T_DW_3_src_x_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_3_src_x_shift  0
#define SDMA_PKT_COPY_T2T_DW_3_SRC_X(x) (((x) & SDMA_PKT_COPY_T2T_DW_3_src_x_mask) << SDMA_PKT_COPY_T2T_DW_3_src_x_shift)

/*define for src_y field*/
#define SDMA_PKT_COPY_T2T_DW_3_src_y_offset 3
#define SDMA_PKT_COPY_T2T_DW_3_src_y_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_3_src_y_shift  16
#define SDMA_PKT_COPY_T2T_DW_3_SRC_Y(x) (((x) & SDMA_PKT_COPY_T2T_DW_3_src_y_mask) << SDMA_PKT_COPY_T2T_DW_3_src_y_shift)

/*define for DW_4 word*/
/*define for src_z field*/
#define SDMA_PKT_COPY_T2T_DW_4_src_z_offset 4
#define SDMA_PKT_COPY_T2T_DW_4_src_z_mask   0x000007FF
#define SDMA_PKT_COPY_T2T_DW_4_src_z_shift  0
#define SDMA_PKT_COPY_T2T_DW_4_SRC_Z(x) (((x) & SDMA_PKT_COPY_T2T_DW_4_src_z_mask) << SDMA_PKT_COPY_T2T_DW_4_src_z_shift)

/*define for src_width field*/
#define SDMA_PKT_COPY_T2T_DW_4_src_width_offset 4
#define SDMA_PKT_COPY_T2T_DW_4_src_width_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_4_src_width_shift  16
#define SDMA_PKT_COPY_T2T_DW_4_SRC_WIDTH(x) (((x) & SDMA_PKT_COPY_T2T_DW_4_src_width_mask) << SDMA_PKT_COPY_T2T_DW_4_src_width_shift)

/*define for DW_5 word*/
/*define for src_height field*/
#define SDMA_PKT_COPY_T2T_DW_5_src_height_offset 5
#define SDMA_PKT_COPY_T2T_DW_5_src_height_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_5_src_height_shift  0
#define SDMA_PKT_COPY_T2T_DW_5_SRC_HEIGHT(x) (((x) & SDMA_PKT_COPY_T2T_DW_5_src_height_mask) << SDMA_PKT_COPY_T2T_DW_5_src_height_shift)

/*define for src_depth field*/
#define SDMA_PKT_COPY_T2T_DW_5_src_depth_offset 5
#define SDMA_PKT_COPY_T2T_DW_5_src_depth_mask   0x000007FF
#define SDMA_PKT_COPY_T2T_DW_5_src_depth_shift  16
#define SDMA_PKT_COPY_T2T_DW_5_SRC_DEPTH(x) (((x) & SDMA_PKT_COPY_T2T_DW_5_src_depth_mask) << SDMA_PKT_COPY_T2T_DW_5_src_depth_shift)

/*define for DW_6 word*/
/*define for src_element_size field*/
#define SDMA_PKT_COPY_T2T_DW_6_src_element_size_offset 6
#define SDMA_PKT_COPY_T2T_DW_6_src_element_size_mask   0x00000007
#define SDMA_PKT_COPY_T2T_DW_6_src_element_size_shift  0
#define SDMA_PKT_COPY_T2T_DW_6_SRC_ELEMENT_SIZE(x) (((x) & SDMA_PKT_COPY_T2T_DW_6_src_element_size_mask) << SDMA_PKT_COPY_T2T_DW_6_src_element_size_shift)

/*define for src_swizzle_mode field*/
#define SDMA_PKT_COPY_T2T_DW_6_src_swizzle_mode_offset 6
#define SDMA_PKT_COPY_T2T_DW_6_src_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_COPY_T2T_DW_6_src_swizzle_mode_shift  3
#define SDMA_PKT_COPY_T2T_DW_6_SRC_SWIZZLE_MODE(x) (((x) & SDMA_PKT_COPY_T2T_DW_6_src_swizzle_mode_mask) << SDMA_PKT_COPY_T2T_DW_6_src_swizzle_mode_shift)

/*define for src_dimension field*/
#define SDMA_PKT_COPY_T2T_DW_6_src_dimension_offset 6
#define SDMA_PKT_COPY_T2T_DW_6_src_dimension_mask   0x00000003
#define SDMA_PKT_COPY_T2T_DW_6_src_dimension_shift  9
#define SDMA_PKT_COPY_T2T_DW_6_SRC_DIMENSION(x) (((x) & SDMA_PKT_COPY_T2T_DW_6_src_dimension_mask) << SDMA_PKT_COPY_T2T_DW_6_src_dimension_shift)

/*define for src_epitch field*/
#define SDMA_PKT_COPY_T2T_DW_6_src_epitch_offset 6
#define SDMA_PKT_COPY_T2T_DW_6_src_epitch_mask   0x0000FFFF
#define SDMA_PKT_COPY_T2T_DW_6_src_epitch_shift  16
#define SDMA_PKT_COPY_T2T_DW_6_SRC_EPITCH(x) (((x) & SDMA_PKT_COPY_T2T_DW_6_src_epitch_mask) << SDMA_PKT_COPY_T2T_DW_6_src_epitch_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_COPY_T2T_DST_ADDR_LO_dst_addr_31_0_offset 7
#define SDMA_PKT_COPY_T2T_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_T2T_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_COPY_T2T_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_T2T_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_COPY_T2T_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_COPY_T2T_DST_ADDR_HI_dst_addr_63_32_offset 8
#define SDMA_PKT_COPY_T2T_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_T2T_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_COPY_T2T_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_T2T_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_COPY_T2T_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for DW_9 word*/
/*define for dst_x field*/
#define SDMA_PKT_COPY_T2T_DW_9_dst_x_offset 9
#define SDMA_PKT_COPY_T2T_DW_9_dst_x_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_9_dst_x_shift  0
#define SDMA_PKT_COPY_T2T_DW_9_DST_X(x) (((x) & SDMA_PKT_COPY_T2T_DW_9_dst_x_mask) << SDMA_PKT_COPY_T2T_DW_9_dst_x_shift)

/*define for dst_y field*/
#define SDMA_PKT_COPY_T2T_DW_9_dst_y_offset 9
#define SDMA_PKT_COPY_T2T_DW_9_dst_y_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_9_dst_y_shift  16
#define SDMA_PKT_COPY_T2T_DW_9_DST_Y(x) (((x) & SDMA_PKT_COPY_T2T_DW_9_dst_y_mask) << SDMA_PKT_COPY_T2T_DW_9_dst_y_shift)

/*define for DW_10 word*/
/*define for dst_z field*/
#define SDMA_PKT_COPY_T2T_DW_10_dst_z_offset 10
#define SDMA_PKT_COPY_T2T_DW_10_dst_z_mask   0x000007FF
#define SDMA_PKT_COPY_T2T_DW_10_dst_z_shift  0
#define SDMA_PKT_COPY_T2T_DW_10_DST_Z(x) (((x) & SDMA_PKT_COPY_T2T_DW_10_dst_z_mask) << SDMA_PKT_COPY_T2T_DW_10_dst_z_shift)

/*define for dst_width field*/
#define SDMA_PKT_COPY_T2T_DW_10_dst_width_offset 10
#define SDMA_PKT_COPY_T2T_DW_10_dst_width_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_10_dst_width_shift  16
#define SDMA_PKT_COPY_T2T_DW_10_DST_WIDTH(x) (((x) & SDMA_PKT_COPY_T2T_DW_10_dst_width_mask) << SDMA_PKT_COPY_T2T_DW_10_dst_width_shift)

/*define for DW_11 word*/
/*define for dst_height field*/
#define SDMA_PKT_COPY_T2T_DW_11_dst_height_offset 11
#define SDMA_PKT_COPY_T2T_DW_11_dst_height_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_11_dst_height_shift  0
#define SDMA_PKT_COPY_T2T_DW_11_DST_HEIGHT(x) (((x) & SDMA_PKT_COPY_T2T_DW_11_dst_height_mask) << SDMA_PKT_COPY_T2T_DW_11_dst_height_shift)

/*define for dst_depth field*/
#define SDMA_PKT_COPY_T2T_DW_11_dst_depth_offset 11
#define SDMA_PKT_COPY_T2T_DW_11_dst_depth_mask   0x000007FF
#define SDMA_PKT_COPY_T2T_DW_11_dst_depth_shift  16
#define SDMA_PKT_COPY_T2T_DW_11_DST_DEPTH(x) (((x) & SDMA_PKT_COPY_T2T_DW_11_dst_depth_mask) << SDMA_PKT_COPY_T2T_DW_11_dst_depth_shift)

/*define for DW_12 word*/
/*define for dst_element_size field*/
#define SDMA_PKT_COPY_T2T_DW_12_dst_element_size_offset 12
#define SDMA_PKT_COPY_T2T_DW_12_dst_element_size_mask   0x00000007
#define SDMA_PKT_COPY_T2T_DW_12_dst_element_size_shift  0
#define SDMA_PKT_COPY_T2T_DW_12_DST_ELEMENT_SIZE(x) (((x) & SDMA_PKT_COPY_T2T_DW_12_dst_element_size_mask) << SDMA_PKT_COPY_T2T_DW_12_dst_element_size_shift)

/*define for dst_swizzle_mode field*/
#define SDMA_PKT_COPY_T2T_DW_12_dst_swizzle_mode_offset 12
#define SDMA_PKT_COPY_T2T_DW_12_dst_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_COPY_T2T_DW_12_dst_swizzle_mode_shift  3
#define SDMA_PKT_COPY_T2T_DW_12_DST_SWIZZLE_MODE(x) (((x) & SDMA_PKT_COPY_T2T_DW_12_dst_swizzle_mode_mask) << SDMA_PKT_COPY_T2T_DW_12_dst_swizzle_mode_shift)

/*define for dst_dimension field*/
#define SDMA_PKT_COPY_T2T_DW_12_dst_dimension_offset 12
#define SDMA_PKT_COPY_T2T_DW_12_dst_dimension_mask   0x00000003
#define SDMA_PKT_COPY_T2T_DW_12_dst_dimension_shift  9
#define SDMA_PKT_COPY_T2T_DW_12_DST_DIMENSION(x) (((x) & SDMA_PKT_COPY_T2T_DW_12_dst_dimension_mask) << SDMA_PKT_COPY_T2T_DW_12_dst_dimension_shift)

/*define for dst_epitch field*/
#define SDMA_PKT_COPY_T2T_DW_12_dst_epitch_offset 12
#define SDMA_PKT_COPY_T2T_DW_12_dst_epitch_mask   0x0000FFFF
#define SDMA_PKT_COPY_T2T_DW_12_dst_epitch_shift  16
#define SDMA_PKT_COPY_T2T_DW_12_DST_EPITCH(x) (((x) & SDMA_PKT_COPY_T2T_DW_12_dst_epitch_mask) << SDMA_PKT_COPY_T2T_DW_12_dst_epitch_shift)

/*define for DW_13 word*/
/*define for rect_x field*/
#define SDMA_PKT_COPY_T2T_DW_13_rect_x_offset 13
#define SDMA_PKT_COPY_T2T_DW_13_rect_x_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_13_rect_x_shift  0
#define SDMA_PKT_COPY_T2T_DW_13_RECT_X(x) (((x) & SDMA_PKT_COPY_T2T_DW_13_rect_x_mask) << SDMA_PKT_COPY_T2T_DW_13_rect_x_shift)

/*define for rect_y field*/
#define SDMA_PKT_COPY_T2T_DW_13_rect_y_offset 13
#define SDMA_PKT_COPY_T2T_DW_13_rect_y_mask   0x00003FFF
#define SDMA_PKT_COPY_T2T_DW_13_rect_y_shift  16
#define SDMA_PKT_COPY_T2T_DW_13_RECT_Y(x) (((x) & SDMA_PKT_COPY_T2T_DW_13_rect_y_mask) << SDMA_PKT_COPY_T2T_DW_13_rect_y_shift)

/*define for DW_14 word*/
/*define for rect_z field*/
#define SDMA_PKT_COPY_T2T_DW_14_rect_z_offset 14
#define SDMA_PKT_COPY_T2T_DW_14_rect_z_mask   0x000007FF
#define SDMA_PKT_COPY_T2T_DW_14_rect_z_shift  0
#define SDMA_PKT_COPY_T2T_DW_14_RECT_Z(x) (((x) & SDMA_PKT_COPY_T2T_DW_14_rect_z_mask) << SDMA_PKT_COPY_T2T_DW_14_rect_z_shift)

/*define for dst_sw field*/
#define SDMA_PKT_COPY_T2T_DW_14_dst_sw_offset 14
#define SDMA_PKT_COPY_T2T_DW_14_dst_sw_mask   0x00000003
#define SDMA_PKT_COPY_T2T_DW_14_dst_sw_shift  16
#define SDMA_PKT_COPY_T2T_DW_14_DST_SW(x) (((x) & SDMA_PKT_COPY_T2T_DW_14_dst_sw_mask) << SDMA_PKT_COPY_T2T_DW_14_dst_sw_shift)

/*define for src_sw field*/
#define SDMA_PKT_COPY_T2T_DW_14_src_sw_offset 14
#define SDMA_PKT_COPY_T2T_DW_14_src_sw_mask   0x00000003
#define SDMA_PKT_COPY_T2T_DW_14_src_sw_shift  24
#define SDMA_PKT_COPY_T2T_DW_14_SRC_SW(x) (((x) & SDMA_PKT_COPY_T2T_DW_14_src_sw_mask) << SDMA_PKT_COPY_T2T_DW_14_src_sw_shift)


/*
** Definitions for SDMA_PKT_COPY_TILED_SUBWIN packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_op_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_op_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_OP(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_op_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_sub_op_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_tmz_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_tmz_shift)

/*define for mip_max field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_max_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_max_mask   0x0000000F
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_max_shift  20
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_MIP_MAX(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_max_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_max_shift)

/*define for mip_id field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_id_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_id_mask   0x0000000F
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_id_shift  24
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_MIP_ID(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_id_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_mip_id_shift)

/*define for detile field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_detile_offset 0
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_detile_mask   0x00000001
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_detile_shift  31
#define SDMA_PKT_COPY_TILED_SUBWIN_HEADER_DETILE(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_HEADER_detile_mask) << SDMA_PKT_COPY_TILED_SUBWIN_HEADER_detile_shift)

/*define for TILED_ADDR_LO word*/
/*define for tiled_addr_31_0 field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_tiled_addr_31_0_offset 1
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_tiled_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_tiled_addr_31_0_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_TILED_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_tiled_addr_31_0_mask) << SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_LO_tiled_addr_31_0_shift)

/*define for TILED_ADDR_HI word*/
/*define for tiled_addr_63_32 field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_tiled_addr_63_32_offset 2
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_tiled_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_tiled_addr_63_32_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_TILED_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_tiled_addr_63_32_mask) << SDMA_PKT_COPY_TILED_SUBWIN_TILED_ADDR_HI_tiled_addr_63_32_shift)

/*define for DW_3 word*/
/*define for tiled_x field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_x_offset 3
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_x_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_x_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_TILED_X(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_x_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_x_shift)

/*define for tiled_y field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_y_offset 3
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_y_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_y_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_3_TILED_Y(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_y_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_3_tiled_y_shift)

/*define for DW_4 word*/
/*define for tiled_z field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_tiled_z_offset 4
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_tiled_z_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_tiled_z_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_TILED_Z(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_4_tiled_z_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_4_tiled_z_shift)

/*define for width field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_width_offset 4
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_width_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_width_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_4_WIDTH(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_4_width_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_4_width_shift)

/*define for DW_5 word*/
/*define for height field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_height_offset 5
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_height_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_height_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_HEIGHT(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_5_height_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_5_height_shift)

/*define for depth field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_depth_offset 5
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_depth_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_depth_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_5_DEPTH(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_5_depth_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_5_depth_shift)

/*define for DW_6 word*/
/*define for element_size field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_element_size_offset 6
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_element_size_mask   0x00000007
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_element_size_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_ELEMENT_SIZE(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_6_element_size_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_6_element_size_shift)

/*define for swizzle_mode field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_swizzle_mode_offset 6
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_swizzle_mode_shift  3
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_SWIZZLE_MODE(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_6_swizzle_mode_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_6_swizzle_mode_shift)

/*define for dimension field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_dimension_offset 6
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_dimension_mask   0x00000003
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_dimension_shift  9
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_DIMENSION(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_6_dimension_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_6_dimension_shift)

/*define for epitch field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_epitch_offset 6
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_epitch_mask   0x0000FFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_epitch_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_6_EPITCH(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_6_epitch_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_6_epitch_shift)

/*define for LINEAR_ADDR_LO word*/
/*define for linear_addr_31_0 field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_linear_addr_31_0_offset 7
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_linear_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_linear_addr_31_0_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_LINEAR_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_linear_addr_31_0_mask) << SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_LO_linear_addr_31_0_shift)

/*define for LINEAR_ADDR_HI word*/
/*define for linear_addr_63_32 field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_linear_addr_63_32_offset 8
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_linear_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_linear_addr_63_32_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_LINEAR_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_linear_addr_63_32_mask) << SDMA_PKT_COPY_TILED_SUBWIN_LINEAR_ADDR_HI_linear_addr_63_32_shift)

/*define for DW_9 word*/
/*define for linear_x field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_x_offset 9
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_x_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_x_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_LINEAR_X(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_x_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_x_shift)

/*define for linear_y field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_y_offset 9
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_y_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_y_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_9_LINEAR_Y(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_y_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_9_linear_y_shift)

/*define for DW_10 word*/
/*define for linear_z field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_z_offset 10
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_z_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_z_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_LINEAR_Z(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_z_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_z_shift)

/*define for linear_pitch field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_pitch_offset 10
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_pitch_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_pitch_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_10_LINEAR_PITCH(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_pitch_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_10_linear_pitch_shift)

/*define for DW_11 word*/
/*define for linear_slice_pitch field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_11_linear_slice_pitch_offset 11
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_11_linear_slice_pitch_mask   0x0FFFFFFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_11_linear_slice_pitch_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_11_LINEAR_SLICE_PITCH(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_11_linear_slice_pitch_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_11_linear_slice_pitch_shift)

/*define for DW_12 word*/
/*define for rect_x field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_x_offset 12
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_x_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_x_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_RECT_X(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_x_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_x_shift)

/*define for rect_y field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_y_offset 12
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_y_mask   0x00003FFF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_y_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_12_RECT_Y(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_y_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_12_rect_y_shift)

/*define for DW_13 word*/
/*define for rect_z field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_rect_z_offset 13
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_rect_z_mask   0x000007FF
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_rect_z_shift  0
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_RECT_Z(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_13_rect_z_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_13_rect_z_shift)

/*define for linear_sw field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_linear_sw_offset 13
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_linear_sw_mask   0x00000003
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_linear_sw_shift  16
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_LINEAR_SW(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_13_linear_sw_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_13_linear_sw_shift)

/*define for tile_sw field*/
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_tile_sw_offset 13
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_tile_sw_mask   0x00000003
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_tile_sw_shift  24
#define SDMA_PKT_COPY_TILED_SUBWIN_DW_13_TILE_SW(x) (((x) & SDMA_PKT_COPY_TILED_SUBWIN_DW_13_tile_sw_mask) << SDMA_PKT_COPY_TILED_SUBWIN_DW_13_tile_sw_shift)


/*
** Definitions for SDMA_PKT_COPY_STRUCT packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COPY_STRUCT_HEADER_op_offset 0
#define SDMA_PKT_COPY_STRUCT_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COPY_STRUCT_HEADER_op_shift  0
#define SDMA_PKT_COPY_STRUCT_HEADER_OP(x) (((x) & SDMA_PKT_COPY_STRUCT_HEADER_op_mask) << SDMA_PKT_COPY_STRUCT_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COPY_STRUCT_HEADER_sub_op_offset 0
#define SDMA_PKT_COPY_STRUCT_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COPY_STRUCT_HEADER_sub_op_shift  8
#define SDMA_PKT_COPY_STRUCT_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COPY_STRUCT_HEADER_sub_op_mask) << SDMA_PKT_COPY_STRUCT_HEADER_sub_op_shift)

/*define for tmz field*/
#define SDMA_PKT_COPY_STRUCT_HEADER_tmz_offset 0
#define SDMA_PKT_COPY_STRUCT_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_COPY_STRUCT_HEADER_tmz_shift  18
#define SDMA_PKT_COPY_STRUCT_HEADER_TMZ(x) (((x) & SDMA_PKT_COPY_STRUCT_HEADER_tmz_mask) << SDMA_PKT_COPY_STRUCT_HEADER_tmz_shift)

/*define for detile field*/
#define SDMA_PKT_COPY_STRUCT_HEADER_detile_offset 0
#define SDMA_PKT_COPY_STRUCT_HEADER_detile_mask   0x00000001
#define SDMA_PKT_COPY_STRUCT_HEADER_detile_shift  31
#define SDMA_PKT_COPY_STRUCT_HEADER_DETILE(x) (((x) & SDMA_PKT_COPY_STRUCT_HEADER_detile_mask) << SDMA_PKT_COPY_STRUCT_HEADER_detile_shift)

/*define for SB_ADDR_LO word*/
/*define for sb_addr_31_0 field*/
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_sb_addr_31_0_offset 1
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_sb_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_sb_addr_31_0_shift  0
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_SB_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_sb_addr_31_0_mask) << SDMA_PKT_COPY_STRUCT_SB_ADDR_LO_sb_addr_31_0_shift)

/*define for SB_ADDR_HI word*/
/*define for sb_addr_63_32 field*/
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_sb_addr_63_32_offset 2
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_sb_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_sb_addr_63_32_shift  0
#define SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_SB_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_sb_addr_63_32_mask) << SDMA_PKT_COPY_STRUCT_SB_ADDR_HI_sb_addr_63_32_shift)

/*define for START_INDEX word*/
/*define for start_index field*/
#define SDMA_PKT_COPY_STRUCT_START_INDEX_start_index_offset 3
#define SDMA_PKT_COPY_STRUCT_START_INDEX_start_index_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_START_INDEX_start_index_shift  0
#define SDMA_PKT_COPY_STRUCT_START_INDEX_START_INDEX(x) (((x) & SDMA_PKT_COPY_STRUCT_START_INDEX_start_index_mask) << SDMA_PKT_COPY_STRUCT_START_INDEX_start_index_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_COPY_STRUCT_COUNT_count_offset 4
#define SDMA_PKT_COPY_STRUCT_COUNT_count_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_COUNT_count_shift  0
#define SDMA_PKT_COPY_STRUCT_COUNT_COUNT(x) (((x) & SDMA_PKT_COPY_STRUCT_COUNT_count_mask) << SDMA_PKT_COPY_STRUCT_COUNT_count_shift)

/*define for DW_5 word*/
/*define for stride field*/
#define SDMA_PKT_COPY_STRUCT_DW_5_stride_offset 5
#define SDMA_PKT_COPY_STRUCT_DW_5_stride_mask   0x000007FF
#define SDMA_PKT_COPY_STRUCT_DW_5_stride_shift  0
#define SDMA_PKT_COPY_STRUCT_DW_5_STRIDE(x) (((x) & SDMA_PKT_COPY_STRUCT_DW_5_stride_mask) << SDMA_PKT_COPY_STRUCT_DW_5_stride_shift)

/*define for linear_sw field*/
#define SDMA_PKT_COPY_STRUCT_DW_5_linear_sw_offset 5
#define SDMA_PKT_COPY_STRUCT_DW_5_linear_sw_mask   0x00000003
#define SDMA_PKT_COPY_STRUCT_DW_5_linear_sw_shift  16
#define SDMA_PKT_COPY_STRUCT_DW_5_LINEAR_SW(x) (((x) & SDMA_PKT_COPY_STRUCT_DW_5_linear_sw_mask) << SDMA_PKT_COPY_STRUCT_DW_5_linear_sw_shift)

/*define for struct_sw field*/
#define SDMA_PKT_COPY_STRUCT_DW_5_struct_sw_offset 5
#define SDMA_PKT_COPY_STRUCT_DW_5_struct_sw_mask   0x00000003
#define SDMA_PKT_COPY_STRUCT_DW_5_struct_sw_shift  24
#define SDMA_PKT_COPY_STRUCT_DW_5_STRUCT_SW(x) (((x) & SDMA_PKT_COPY_STRUCT_DW_5_struct_sw_mask) << SDMA_PKT_COPY_STRUCT_DW_5_struct_sw_shift)

/*define for LINEAR_ADDR_LO word*/
/*define for linear_addr_31_0 field*/
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_linear_addr_31_0_offset 6
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_linear_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_linear_addr_31_0_shift  0
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_LINEAR_ADDR_31_0(x) (((x) & SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_linear_addr_31_0_mask) << SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_LO_linear_addr_31_0_shift)

/*define for LINEAR_ADDR_HI word*/
/*define for linear_addr_63_32 field*/
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_linear_addr_63_32_offset 7
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_linear_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_linear_addr_63_32_shift  0
#define SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_LINEAR_ADDR_63_32(x) (((x) & SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_linear_addr_63_32_mask) << SDMA_PKT_COPY_STRUCT_LINEAR_ADDR_HI_linear_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_WRITE_UNTILED packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_WRITE_UNTILED_HEADER_op_offset 0
#define SDMA_PKT_WRITE_UNTILED_HEADER_op_mask   0x000000FF
#define SDMA_PKT_WRITE_UNTILED_HEADER_op_shift  0
#define SDMA_PKT_WRITE_UNTILED_HEADER_OP(x) (((x) & SDMA_PKT_WRITE_UNTILED_HEADER_op_mask) << SDMA_PKT_WRITE_UNTILED_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_WRITE_UNTILED_HEADER_sub_op_offset 0
#define SDMA_PKT_WRITE_UNTILED_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_WRITE_UNTILED_HEADER_sub_op_shift  8
#define SDMA_PKT_WRITE_UNTILED_HEADER_SUB_OP(x) (((x) & SDMA_PKT_WRITE_UNTILED_HEADER_sub_op_mask) << SDMA_PKT_WRITE_UNTILED_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_WRITE_UNTILED_HEADER_encrypt_offset 0
#define SDMA_PKT_WRITE_UNTILED_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_WRITE_UNTILED_HEADER_encrypt_shift  16
#define SDMA_PKT_WRITE_UNTILED_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_WRITE_UNTILED_HEADER_encrypt_mask) << SDMA_PKT_WRITE_UNTILED_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_WRITE_UNTILED_HEADER_tmz_offset 0
#define SDMA_PKT_WRITE_UNTILED_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_WRITE_UNTILED_HEADER_tmz_shift  18
#define SDMA_PKT_WRITE_UNTILED_HEADER_TMZ(x) (((x) & SDMA_PKT_WRITE_UNTILED_HEADER_tmz_mask) << SDMA_PKT_WRITE_UNTILED_HEADER_tmz_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_dst_addr_31_0_offset 1
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_WRITE_UNTILED_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_dst_addr_63_32_offset 2
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_WRITE_UNTILED_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for DW_3 word*/
/*define for count field*/
#define SDMA_PKT_WRITE_UNTILED_DW_3_count_offset 3
#define SDMA_PKT_WRITE_UNTILED_DW_3_count_mask   0x000FFFFF
#define SDMA_PKT_WRITE_UNTILED_DW_3_count_shift  0
#define SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(x) (((x) & SDMA_PKT_WRITE_UNTILED_DW_3_count_mask) << SDMA_PKT_WRITE_UNTILED_DW_3_count_shift)

/*define for sw field*/
#define SDMA_PKT_WRITE_UNTILED_DW_3_sw_offset 3
#define SDMA_PKT_WRITE_UNTILED_DW_3_sw_mask   0x00000003
#define SDMA_PKT_WRITE_UNTILED_DW_3_sw_shift  24
#define SDMA_PKT_WRITE_UNTILED_DW_3_SW(x) (((x) & SDMA_PKT_WRITE_UNTILED_DW_3_sw_mask) << SDMA_PKT_WRITE_UNTILED_DW_3_sw_shift)

/*define for DATA0 word*/
/*define for data0 field*/
#define SDMA_PKT_WRITE_UNTILED_DATA0_data0_offset 4
#define SDMA_PKT_WRITE_UNTILED_DATA0_data0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_UNTILED_DATA0_data0_shift  0
#define SDMA_PKT_WRITE_UNTILED_DATA0_DATA0(x) (((x) & SDMA_PKT_WRITE_UNTILED_DATA0_data0_mask) << SDMA_PKT_WRITE_UNTILED_DATA0_data0_shift)


/*
** Definitions for SDMA_PKT_WRITE_TILED packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_WRITE_TILED_HEADER_op_offset 0
#define SDMA_PKT_WRITE_TILED_HEADER_op_mask   0x000000FF
#define SDMA_PKT_WRITE_TILED_HEADER_op_shift  0
#define SDMA_PKT_WRITE_TILED_HEADER_OP(x) (((x) & SDMA_PKT_WRITE_TILED_HEADER_op_mask) << SDMA_PKT_WRITE_TILED_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_WRITE_TILED_HEADER_sub_op_offset 0
#define SDMA_PKT_WRITE_TILED_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_WRITE_TILED_HEADER_sub_op_shift  8
#define SDMA_PKT_WRITE_TILED_HEADER_SUB_OP(x) (((x) & SDMA_PKT_WRITE_TILED_HEADER_sub_op_mask) << SDMA_PKT_WRITE_TILED_HEADER_sub_op_shift)

/*define for encrypt field*/
#define SDMA_PKT_WRITE_TILED_HEADER_encrypt_offset 0
#define SDMA_PKT_WRITE_TILED_HEADER_encrypt_mask   0x00000001
#define SDMA_PKT_WRITE_TILED_HEADER_encrypt_shift  16
#define SDMA_PKT_WRITE_TILED_HEADER_ENCRYPT(x) (((x) & SDMA_PKT_WRITE_TILED_HEADER_encrypt_mask) << SDMA_PKT_WRITE_TILED_HEADER_encrypt_shift)

/*define for tmz field*/
#define SDMA_PKT_WRITE_TILED_HEADER_tmz_offset 0
#define SDMA_PKT_WRITE_TILED_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_WRITE_TILED_HEADER_tmz_shift  18
#define SDMA_PKT_WRITE_TILED_HEADER_TMZ(x) (((x) & SDMA_PKT_WRITE_TILED_HEADER_tmz_mask) << SDMA_PKT_WRITE_TILED_HEADER_tmz_shift)

/*define for mip_max field*/
#define SDMA_PKT_WRITE_TILED_HEADER_mip_max_offset 0
#define SDMA_PKT_WRITE_TILED_HEADER_mip_max_mask   0x0000000F
#define SDMA_PKT_WRITE_TILED_HEADER_mip_max_shift  20
#define SDMA_PKT_WRITE_TILED_HEADER_MIP_MAX(x) (((x) & SDMA_PKT_WRITE_TILED_HEADER_mip_max_mask) << SDMA_PKT_WRITE_TILED_HEADER_mip_max_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_WRITE_TILED_DST_ADDR_LO_dst_addr_31_0_offset 1
#define SDMA_PKT_WRITE_TILED_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_TILED_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_WRITE_TILED_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_WRITE_TILED_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_WRITE_TILED_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_WRITE_TILED_DST_ADDR_HI_dst_addr_63_32_offset 2
#define SDMA_PKT_WRITE_TILED_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_TILED_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_WRITE_TILED_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_WRITE_TILED_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_WRITE_TILED_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for DW_3 word*/
/*define for width field*/
#define SDMA_PKT_WRITE_TILED_DW_3_width_offset 3
#define SDMA_PKT_WRITE_TILED_DW_3_width_mask   0x00003FFF
#define SDMA_PKT_WRITE_TILED_DW_3_width_shift  0
#define SDMA_PKT_WRITE_TILED_DW_3_WIDTH(x) (((x) & SDMA_PKT_WRITE_TILED_DW_3_width_mask) << SDMA_PKT_WRITE_TILED_DW_3_width_shift)

/*define for DW_4 word*/
/*define for height field*/
#define SDMA_PKT_WRITE_TILED_DW_4_height_offset 4
#define SDMA_PKT_WRITE_TILED_DW_4_height_mask   0x00003FFF
#define SDMA_PKT_WRITE_TILED_DW_4_height_shift  0
#define SDMA_PKT_WRITE_TILED_DW_4_HEIGHT(x) (((x) & SDMA_PKT_WRITE_TILED_DW_4_height_mask) << SDMA_PKT_WRITE_TILED_DW_4_height_shift)

/*define for depth field*/
#define SDMA_PKT_WRITE_TILED_DW_4_depth_offset 4
#define SDMA_PKT_WRITE_TILED_DW_4_depth_mask   0x000007FF
#define SDMA_PKT_WRITE_TILED_DW_4_depth_shift  16
#define SDMA_PKT_WRITE_TILED_DW_4_DEPTH(x) (((x) & SDMA_PKT_WRITE_TILED_DW_4_depth_mask) << SDMA_PKT_WRITE_TILED_DW_4_depth_shift)

/*define for DW_5 word*/
/*define for element_size field*/
#define SDMA_PKT_WRITE_TILED_DW_5_element_size_offset 5
#define SDMA_PKT_WRITE_TILED_DW_5_element_size_mask   0x00000007
#define SDMA_PKT_WRITE_TILED_DW_5_element_size_shift  0
#define SDMA_PKT_WRITE_TILED_DW_5_ELEMENT_SIZE(x) (((x) & SDMA_PKT_WRITE_TILED_DW_5_element_size_mask) << SDMA_PKT_WRITE_TILED_DW_5_element_size_shift)

/*define for swizzle_mode field*/
#define SDMA_PKT_WRITE_TILED_DW_5_swizzle_mode_offset 5
#define SDMA_PKT_WRITE_TILED_DW_5_swizzle_mode_mask   0x0000001F
#define SDMA_PKT_WRITE_TILED_DW_5_swizzle_mode_shift  3
#define SDMA_PKT_WRITE_TILED_DW_5_SWIZZLE_MODE(x) (((x) & SDMA_PKT_WRITE_TILED_DW_5_swizzle_mode_mask) << SDMA_PKT_WRITE_TILED_DW_5_swizzle_mode_shift)

/*define for dimension field*/
#define SDMA_PKT_WRITE_TILED_DW_5_dimension_offset 5
#define SDMA_PKT_WRITE_TILED_DW_5_dimension_mask   0x00000003
#define SDMA_PKT_WRITE_TILED_DW_5_dimension_shift  9
#define SDMA_PKT_WRITE_TILED_DW_5_DIMENSION(x) (((x) & SDMA_PKT_WRITE_TILED_DW_5_dimension_mask) << SDMA_PKT_WRITE_TILED_DW_5_dimension_shift)

/*define for epitch field*/
#define SDMA_PKT_WRITE_TILED_DW_5_epitch_offset 5
#define SDMA_PKT_WRITE_TILED_DW_5_epitch_mask   0x0000FFFF
#define SDMA_PKT_WRITE_TILED_DW_5_epitch_shift  16
#define SDMA_PKT_WRITE_TILED_DW_5_EPITCH(x) (((x) & SDMA_PKT_WRITE_TILED_DW_5_epitch_mask) << SDMA_PKT_WRITE_TILED_DW_5_epitch_shift)

/*define for DW_6 word*/
/*define for x field*/
#define SDMA_PKT_WRITE_TILED_DW_6_x_offset 6
#define SDMA_PKT_WRITE_TILED_DW_6_x_mask   0x00003FFF
#define SDMA_PKT_WRITE_TILED_DW_6_x_shift  0
#define SDMA_PKT_WRITE_TILED_DW_6_X(x) (((x) & SDMA_PKT_WRITE_TILED_DW_6_x_mask) << SDMA_PKT_WRITE_TILED_DW_6_x_shift)

/*define for y field*/
#define SDMA_PKT_WRITE_TILED_DW_6_y_offset 6
#define SDMA_PKT_WRITE_TILED_DW_6_y_mask   0x00003FFF
#define SDMA_PKT_WRITE_TILED_DW_6_y_shift  16
#define SDMA_PKT_WRITE_TILED_DW_6_Y(x) (((x) & SDMA_PKT_WRITE_TILED_DW_6_y_mask) << SDMA_PKT_WRITE_TILED_DW_6_y_shift)

/*define for DW_7 word*/
/*define for z field*/
#define SDMA_PKT_WRITE_TILED_DW_7_z_offset 7
#define SDMA_PKT_WRITE_TILED_DW_7_z_mask   0x000007FF
#define SDMA_PKT_WRITE_TILED_DW_7_z_shift  0
#define SDMA_PKT_WRITE_TILED_DW_7_Z(x) (((x) & SDMA_PKT_WRITE_TILED_DW_7_z_mask) << SDMA_PKT_WRITE_TILED_DW_7_z_shift)

/*define for sw field*/
#define SDMA_PKT_WRITE_TILED_DW_7_sw_offset 7
#define SDMA_PKT_WRITE_TILED_DW_7_sw_mask   0x00000003
#define SDMA_PKT_WRITE_TILED_DW_7_sw_shift  24
#define SDMA_PKT_WRITE_TILED_DW_7_SW(x) (((x) & SDMA_PKT_WRITE_TILED_DW_7_sw_mask) << SDMA_PKT_WRITE_TILED_DW_7_sw_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_WRITE_TILED_COUNT_count_offset 8
#define SDMA_PKT_WRITE_TILED_COUNT_count_mask   0x000FFFFF
#define SDMA_PKT_WRITE_TILED_COUNT_count_shift  0
#define SDMA_PKT_WRITE_TILED_COUNT_COUNT(x) (((x) & SDMA_PKT_WRITE_TILED_COUNT_count_mask) << SDMA_PKT_WRITE_TILED_COUNT_count_shift)

/*define for DATA0 word*/
/*define for data0 field*/
#define SDMA_PKT_WRITE_TILED_DATA0_data0_offset 9
#define SDMA_PKT_WRITE_TILED_DATA0_data0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_TILED_DATA0_data0_shift  0
#define SDMA_PKT_WRITE_TILED_DATA0_DATA0(x) (((x) & SDMA_PKT_WRITE_TILED_DATA0_data0_mask) << SDMA_PKT_WRITE_TILED_DATA0_data0_shift)


/*
** Definitions for SDMA_PKT_PTEPDE_COPY packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_PTEPDE_COPY_HEADER_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_HEADER_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_HEADER_op_shift  0
#define SDMA_PKT_PTEPDE_COPY_HEADER_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_HEADER_op_mask) << SDMA_PKT_PTEPDE_COPY_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_PTEPDE_COPY_HEADER_sub_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_HEADER_sub_op_shift  8
#define SDMA_PKT_PTEPDE_COPY_HEADER_SUB_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_HEADER_sub_op_mask) << SDMA_PKT_PTEPDE_COPY_HEADER_sub_op_shift)

/*define for ptepde_op field*/
#define SDMA_PKT_PTEPDE_COPY_HEADER_ptepde_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_HEADER_ptepde_op_mask   0x00000001
#define SDMA_PKT_PTEPDE_COPY_HEADER_ptepde_op_shift  31
#define SDMA_PKT_PTEPDE_COPY_HEADER_PTEPDE_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_HEADER_ptepde_op_mask) << SDMA_PKT_PTEPDE_COPY_HEADER_ptepde_op_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_src_addr_31_0_offset 1
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_PTEPDE_COPY_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_src_addr_63_32_offset 2
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_PTEPDE_COPY_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_dst_addr_31_0_offset 3
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_PTEPDE_COPY_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_dst_addr_63_32_offset 4
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_PTEPDE_COPY_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for MASK_DW0 word*/
/*define for mask_dw0 field*/
#define SDMA_PKT_PTEPDE_COPY_MASK_DW0_mask_dw0_offset 5
#define SDMA_PKT_PTEPDE_COPY_MASK_DW0_mask_dw0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_MASK_DW0_mask_dw0_shift  0
#define SDMA_PKT_PTEPDE_COPY_MASK_DW0_MASK_DW0(x) (((x) & SDMA_PKT_PTEPDE_COPY_MASK_DW0_mask_dw0_mask) << SDMA_PKT_PTEPDE_COPY_MASK_DW0_mask_dw0_shift)

/*define for MASK_DW1 word*/
/*define for mask_dw1 field*/
#define SDMA_PKT_PTEPDE_COPY_MASK_DW1_mask_dw1_offset 6
#define SDMA_PKT_PTEPDE_COPY_MASK_DW1_mask_dw1_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_MASK_DW1_mask_dw1_shift  0
#define SDMA_PKT_PTEPDE_COPY_MASK_DW1_MASK_DW1(x) (((x) & SDMA_PKT_PTEPDE_COPY_MASK_DW1_mask_dw1_mask) << SDMA_PKT_PTEPDE_COPY_MASK_DW1_mask_dw1_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_PTEPDE_COPY_COUNT_count_offset 7
#define SDMA_PKT_PTEPDE_COPY_COUNT_count_mask   0x0007FFFF
#define SDMA_PKT_PTEPDE_COPY_COUNT_count_shift  0
#define SDMA_PKT_PTEPDE_COPY_COUNT_COUNT(x) (((x) & SDMA_PKT_PTEPDE_COPY_COUNT_count_mask) << SDMA_PKT_PTEPDE_COPY_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_PTEPDE_COPY_BACKWARDS packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_op_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_op_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_sub_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_sub_op_shift  8
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_SUB_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_sub_op_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_sub_op_shift)

/*define for pte_size field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_pte_size_offset 0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_pte_size_mask   0x00000003
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_pte_size_shift  28
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_PTE_SIZE(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_pte_size_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_pte_size_shift)

/*define for direction field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_direction_offset 0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_direction_mask   0x00000001
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_direction_shift  30
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_DIRECTION(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_direction_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_direction_shift)

/*define for ptepde_op field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_ptepde_op_offset 0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_ptepde_op_mask   0x00000001
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_ptepde_op_shift  31
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_PTEPDE_OP(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_ptepde_op_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_HEADER_ptepde_op_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_src_addr_31_0_offset 1
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_src_addr_63_32_offset 2
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_dst_addr_31_0_offset 3
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_dst_addr_63_32_offset 4
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for MASK_BIT_FOR_DW word*/
/*define for mask_first_xfer field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_first_xfer_offset 5
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_first_xfer_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_first_xfer_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_MASK_FIRST_XFER(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_first_xfer_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_first_xfer_shift)

/*define for mask_last_xfer field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_last_xfer_offset 5
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_last_xfer_mask   0x000000FF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_last_xfer_shift  8
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_MASK_LAST_XFER(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_last_xfer_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_MASK_BIT_FOR_DW_mask_last_xfer_shift)

/*define for COUNT_IN_32B_XFER word*/
/*define for count field*/
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_count_offset 6
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_count_mask   0x0001FFFF
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_count_shift  0
#define SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_COUNT(x) (((x) & SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_count_mask) << SDMA_PKT_PTEPDE_COPY_BACKWARDS_COUNT_IN_32B_XFER_count_shift)


/*
** Definitions for SDMA_PKT_PTEPDE_RMW packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_op_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_RMW_HEADER_op_shift  0
#define SDMA_PKT_PTEPDE_RMW_HEADER_OP(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_op_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_sub_op_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_PTEPDE_RMW_HEADER_sub_op_shift  8
#define SDMA_PKT_PTEPDE_RMW_HEADER_SUB_OP(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_sub_op_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_sub_op_shift)

/*define for gcc field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_gcc_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_gcc_mask   0x00000001
#define SDMA_PKT_PTEPDE_RMW_HEADER_gcc_shift  19
#define SDMA_PKT_PTEPDE_RMW_HEADER_GCC(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_gcc_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_gcc_shift)

/*define for sys field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_sys_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_sys_mask   0x00000001
#define SDMA_PKT_PTEPDE_RMW_HEADER_sys_shift  20
#define SDMA_PKT_PTEPDE_RMW_HEADER_SYS(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_sys_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_sys_shift)

/*define for snp field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_snp_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_snp_mask   0x00000001
#define SDMA_PKT_PTEPDE_RMW_HEADER_snp_shift  22
#define SDMA_PKT_PTEPDE_RMW_HEADER_SNP(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_snp_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_snp_shift)

/*define for gpa field*/
#define SDMA_PKT_PTEPDE_RMW_HEADER_gpa_offset 0
#define SDMA_PKT_PTEPDE_RMW_HEADER_gpa_mask   0x00000001
#define SDMA_PKT_PTEPDE_RMW_HEADER_gpa_shift  23
#define SDMA_PKT_PTEPDE_RMW_HEADER_GPA(x) (((x) & SDMA_PKT_PTEPDE_RMW_HEADER_gpa_mask) << SDMA_PKT_PTEPDE_RMW_HEADER_gpa_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_PTEPDE_RMW_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_PTEPDE_RMW_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_PTEPDE_RMW_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_PTEPDE_RMW_ADDR_LO_addr_31_0_mask) << SDMA_PKT_PTEPDE_RMW_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_PTEPDE_RMW_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_PTEPDE_RMW_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_PTEPDE_RMW_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_PTEPDE_RMW_ADDR_HI_addr_63_32_mask) << SDMA_PKT_PTEPDE_RMW_ADDR_HI_addr_63_32_shift)

/*define for MASK_LO word*/
/*define for mask_31_0 field*/
#define SDMA_PKT_PTEPDE_RMW_MASK_LO_mask_31_0_offset 3
#define SDMA_PKT_PTEPDE_RMW_MASK_LO_mask_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_MASK_LO_mask_31_0_shift  0
#define SDMA_PKT_PTEPDE_RMW_MASK_LO_MASK_31_0(x) (((x) & SDMA_PKT_PTEPDE_RMW_MASK_LO_mask_31_0_mask) << SDMA_PKT_PTEPDE_RMW_MASK_LO_mask_31_0_shift)

/*define for MASK_HI word*/
/*define for mask_63_32 field*/
#define SDMA_PKT_PTEPDE_RMW_MASK_HI_mask_63_32_offset 4
#define SDMA_PKT_PTEPDE_RMW_MASK_HI_mask_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_MASK_HI_mask_63_32_shift  0
#define SDMA_PKT_PTEPDE_RMW_MASK_HI_MASK_63_32(x) (((x) & SDMA_PKT_PTEPDE_RMW_MASK_HI_mask_63_32_mask) << SDMA_PKT_PTEPDE_RMW_MASK_HI_mask_63_32_shift)

/*define for VALUE_LO word*/
/*define for value_31_0 field*/
#define SDMA_PKT_PTEPDE_RMW_VALUE_LO_value_31_0_offset 5
#define SDMA_PKT_PTEPDE_RMW_VALUE_LO_value_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_VALUE_LO_value_31_0_shift  0
#define SDMA_PKT_PTEPDE_RMW_VALUE_LO_VALUE_31_0(x) (((x) & SDMA_PKT_PTEPDE_RMW_VALUE_LO_value_31_0_mask) << SDMA_PKT_PTEPDE_RMW_VALUE_LO_value_31_0_shift)

/*define for VALUE_HI word*/
/*define for value_63_32 field*/
#define SDMA_PKT_PTEPDE_RMW_VALUE_HI_value_63_32_offset 6
#define SDMA_PKT_PTEPDE_RMW_VALUE_HI_value_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_PTEPDE_RMW_VALUE_HI_value_63_32_shift  0
#define SDMA_PKT_PTEPDE_RMW_VALUE_HI_VALUE_63_32(x) (((x) & SDMA_PKT_PTEPDE_RMW_VALUE_HI_value_63_32_mask) << SDMA_PKT_PTEPDE_RMW_VALUE_HI_value_63_32_shift)


/*
** Definitions for SDMA_PKT_WRITE_INCR packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_WRITE_INCR_HEADER_op_offset 0
#define SDMA_PKT_WRITE_INCR_HEADER_op_mask   0x000000FF
#define SDMA_PKT_WRITE_INCR_HEADER_op_shift  0
#define SDMA_PKT_WRITE_INCR_HEADER_OP(x) (((x) & SDMA_PKT_WRITE_INCR_HEADER_op_mask) << SDMA_PKT_WRITE_INCR_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_WRITE_INCR_HEADER_sub_op_offset 0
#define SDMA_PKT_WRITE_INCR_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_WRITE_INCR_HEADER_sub_op_shift  8
#define SDMA_PKT_WRITE_INCR_HEADER_SUB_OP(x) (((x) & SDMA_PKT_WRITE_INCR_HEADER_sub_op_mask) << SDMA_PKT_WRITE_INCR_HEADER_sub_op_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_WRITE_INCR_DST_ADDR_LO_dst_addr_31_0_offset 1
#define SDMA_PKT_WRITE_INCR_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_WRITE_INCR_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_WRITE_INCR_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_WRITE_INCR_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_WRITE_INCR_DST_ADDR_HI_dst_addr_63_32_offset 2
#define SDMA_PKT_WRITE_INCR_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_WRITE_INCR_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_WRITE_INCR_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_WRITE_INCR_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for MASK_DW0 word*/
/*define for mask_dw0 field*/
#define SDMA_PKT_WRITE_INCR_MASK_DW0_mask_dw0_offset 3
#define SDMA_PKT_WRITE_INCR_MASK_DW0_mask_dw0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_MASK_DW0_mask_dw0_shift  0
#define SDMA_PKT_WRITE_INCR_MASK_DW0_MASK_DW0(x) (((x) & SDMA_PKT_WRITE_INCR_MASK_DW0_mask_dw0_mask) << SDMA_PKT_WRITE_INCR_MASK_DW0_mask_dw0_shift)

/*define for MASK_DW1 word*/
/*define for mask_dw1 field*/
#define SDMA_PKT_WRITE_INCR_MASK_DW1_mask_dw1_offset 4
#define SDMA_PKT_WRITE_INCR_MASK_DW1_mask_dw1_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_MASK_DW1_mask_dw1_shift  0
#define SDMA_PKT_WRITE_INCR_MASK_DW1_MASK_DW1(x) (((x) & SDMA_PKT_WRITE_INCR_MASK_DW1_mask_dw1_mask) << SDMA_PKT_WRITE_INCR_MASK_DW1_mask_dw1_shift)

/*define for INIT_DW0 word*/
/*define for init_dw0 field*/
#define SDMA_PKT_WRITE_INCR_INIT_DW0_init_dw0_offset 5
#define SDMA_PKT_WRITE_INCR_INIT_DW0_init_dw0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_INIT_DW0_init_dw0_shift  0
#define SDMA_PKT_WRITE_INCR_INIT_DW0_INIT_DW0(x) (((x) & SDMA_PKT_WRITE_INCR_INIT_DW0_init_dw0_mask) << SDMA_PKT_WRITE_INCR_INIT_DW0_init_dw0_shift)

/*define for INIT_DW1 word*/
/*define for init_dw1 field*/
#define SDMA_PKT_WRITE_INCR_INIT_DW1_init_dw1_offset 6
#define SDMA_PKT_WRITE_INCR_INIT_DW1_init_dw1_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_INIT_DW1_init_dw1_shift  0
#define SDMA_PKT_WRITE_INCR_INIT_DW1_INIT_DW1(x) (((x) & SDMA_PKT_WRITE_INCR_INIT_DW1_init_dw1_mask) << SDMA_PKT_WRITE_INCR_INIT_DW1_init_dw1_shift)

/*define for INCR_DW0 word*/
/*define for incr_dw0 field*/
#define SDMA_PKT_WRITE_INCR_INCR_DW0_incr_dw0_offset 7
#define SDMA_PKT_WRITE_INCR_INCR_DW0_incr_dw0_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_INCR_DW0_incr_dw0_shift  0
#define SDMA_PKT_WRITE_INCR_INCR_DW0_INCR_DW0(x) (((x) & SDMA_PKT_WRITE_INCR_INCR_DW0_incr_dw0_mask) << SDMA_PKT_WRITE_INCR_INCR_DW0_incr_dw0_shift)

/*define for INCR_DW1 word*/
/*define for incr_dw1 field*/
#define SDMA_PKT_WRITE_INCR_INCR_DW1_incr_dw1_offset 8
#define SDMA_PKT_WRITE_INCR_INCR_DW1_incr_dw1_mask   0xFFFFFFFF
#define SDMA_PKT_WRITE_INCR_INCR_DW1_incr_dw1_shift  0
#define SDMA_PKT_WRITE_INCR_INCR_DW1_INCR_DW1(x) (((x) & SDMA_PKT_WRITE_INCR_INCR_DW1_incr_dw1_mask) << SDMA_PKT_WRITE_INCR_INCR_DW1_incr_dw1_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_WRITE_INCR_COUNT_count_offset 9
#define SDMA_PKT_WRITE_INCR_COUNT_count_mask   0x0007FFFF
#define SDMA_PKT_WRITE_INCR_COUNT_count_shift  0
#define SDMA_PKT_WRITE_INCR_COUNT_COUNT(x) (((x) & SDMA_PKT_WRITE_INCR_COUNT_count_mask) << SDMA_PKT_WRITE_INCR_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_INDIRECT packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_INDIRECT_HEADER_op_offset 0
#define SDMA_PKT_INDIRECT_HEADER_op_mask   0x000000FF
#define SDMA_PKT_INDIRECT_HEADER_op_shift  0
#define SDMA_PKT_INDIRECT_HEADER_OP(x) (((x) & SDMA_PKT_INDIRECT_HEADER_op_mask) << SDMA_PKT_INDIRECT_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_INDIRECT_HEADER_sub_op_offset 0
#define SDMA_PKT_INDIRECT_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_INDIRECT_HEADER_sub_op_shift  8
#define SDMA_PKT_INDIRECT_HEADER_SUB_OP(x) (((x) & SDMA_PKT_INDIRECT_HEADER_sub_op_mask) << SDMA_PKT_INDIRECT_HEADER_sub_op_shift)

/*define for vmid field*/
#define SDMA_PKT_INDIRECT_HEADER_vmid_offset 0
#define SDMA_PKT_INDIRECT_HEADER_vmid_mask   0x0000000F
#define SDMA_PKT_INDIRECT_HEADER_vmid_shift  16
#define SDMA_PKT_INDIRECT_HEADER_VMID(x) (((x) & SDMA_PKT_INDIRECT_HEADER_vmid_mask) << SDMA_PKT_INDIRECT_HEADER_vmid_shift)

/*define for BASE_LO word*/
/*define for ib_base_31_0 field*/
#define SDMA_PKT_INDIRECT_BASE_LO_ib_base_31_0_offset 1
#define SDMA_PKT_INDIRECT_BASE_LO_ib_base_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_INDIRECT_BASE_LO_ib_base_31_0_shift  0
#define SDMA_PKT_INDIRECT_BASE_LO_IB_BASE_31_0(x) (((x) & SDMA_PKT_INDIRECT_BASE_LO_ib_base_31_0_mask) << SDMA_PKT_INDIRECT_BASE_LO_ib_base_31_0_shift)

/*define for BASE_HI word*/
/*define for ib_base_63_32 field*/
#define SDMA_PKT_INDIRECT_BASE_HI_ib_base_63_32_offset 2
#define SDMA_PKT_INDIRECT_BASE_HI_ib_base_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_INDIRECT_BASE_HI_ib_base_63_32_shift  0
#define SDMA_PKT_INDIRECT_BASE_HI_IB_BASE_63_32(x) (((x) & SDMA_PKT_INDIRECT_BASE_HI_ib_base_63_32_mask) << SDMA_PKT_INDIRECT_BASE_HI_ib_base_63_32_shift)

/*define for IB_SIZE word*/
/*define for ib_size field*/
#define SDMA_PKT_INDIRECT_IB_SIZE_ib_size_offset 3
#define SDMA_PKT_INDIRECT_IB_SIZE_ib_size_mask   0x000FFFFF
#define SDMA_PKT_INDIRECT_IB_SIZE_ib_size_shift  0
#define SDMA_PKT_INDIRECT_IB_SIZE_IB_SIZE(x) (((x) & SDMA_PKT_INDIRECT_IB_SIZE_ib_size_mask) << SDMA_PKT_INDIRECT_IB_SIZE_ib_size_shift)

/*define for CSA_ADDR_LO word*/
/*define for csa_addr_31_0 field*/
#define SDMA_PKT_INDIRECT_CSA_ADDR_LO_csa_addr_31_0_offset 4
#define SDMA_PKT_INDIRECT_CSA_ADDR_LO_csa_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_INDIRECT_CSA_ADDR_LO_csa_addr_31_0_shift  0
#define SDMA_PKT_INDIRECT_CSA_ADDR_LO_CSA_ADDR_31_0(x) (((x) & SDMA_PKT_INDIRECT_CSA_ADDR_LO_csa_addr_31_0_mask) << SDMA_PKT_INDIRECT_CSA_ADDR_LO_csa_addr_31_0_shift)

/*define for CSA_ADDR_HI word*/
/*define for csa_addr_63_32 field*/
#define SDMA_PKT_INDIRECT_CSA_ADDR_HI_csa_addr_63_32_offset 5
#define SDMA_PKT_INDIRECT_CSA_ADDR_HI_csa_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_INDIRECT_CSA_ADDR_HI_csa_addr_63_32_shift  0
#define SDMA_PKT_INDIRECT_CSA_ADDR_HI_CSA_ADDR_63_32(x) (((x) & SDMA_PKT_INDIRECT_CSA_ADDR_HI_csa_addr_63_32_mask) << SDMA_PKT_INDIRECT_CSA_ADDR_HI_csa_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_SEMAPHORE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_SEMAPHORE_HEADER_op_offset 0
#define SDMA_PKT_SEMAPHORE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_SEMAPHORE_HEADER_op_shift  0
#define SDMA_PKT_SEMAPHORE_HEADER_OP(x) (((x) & SDMA_PKT_SEMAPHORE_HEADER_op_mask) << SDMA_PKT_SEMAPHORE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_SEMAPHORE_HEADER_sub_op_offset 0
#define SDMA_PKT_SEMAPHORE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_SEMAPHORE_HEADER_sub_op_shift  8
#define SDMA_PKT_SEMAPHORE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_SEMAPHORE_HEADER_sub_op_mask) << SDMA_PKT_SEMAPHORE_HEADER_sub_op_shift)

/*define for write_one field*/
#define SDMA_PKT_SEMAPHORE_HEADER_write_one_offset 0
#define SDMA_PKT_SEMAPHORE_HEADER_write_one_mask   0x00000001
#define SDMA_PKT_SEMAPHORE_HEADER_write_one_shift  29
#define SDMA_PKT_SEMAPHORE_HEADER_WRITE_ONE(x) (((x) & SDMA_PKT_SEMAPHORE_HEADER_write_one_mask) << SDMA_PKT_SEMAPHORE_HEADER_write_one_shift)

/*define for signal field*/
#define SDMA_PKT_SEMAPHORE_HEADER_signal_offset 0
#define SDMA_PKT_SEMAPHORE_HEADER_signal_mask   0x00000001
#define SDMA_PKT_SEMAPHORE_HEADER_signal_shift  30
#define SDMA_PKT_SEMAPHORE_HEADER_SIGNAL(x) (((x) & SDMA_PKT_SEMAPHORE_HEADER_signal_mask) << SDMA_PKT_SEMAPHORE_HEADER_signal_shift)

/*define for mailbox field*/
#define SDMA_PKT_SEMAPHORE_HEADER_mailbox_offset 0
#define SDMA_PKT_SEMAPHORE_HEADER_mailbox_mask   0x00000001
#define SDMA_PKT_SEMAPHORE_HEADER_mailbox_shift  31
#define SDMA_PKT_SEMAPHORE_HEADER_MAILBOX(x) (((x) & SDMA_PKT_SEMAPHORE_HEADER_mailbox_mask) << SDMA_PKT_SEMAPHORE_HEADER_mailbox_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_SEMAPHORE_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_SEMAPHORE_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_SEMAPHORE_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_SEMAPHORE_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_SEMAPHORE_ADDR_LO_addr_31_0_mask) << SDMA_PKT_SEMAPHORE_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_SEMAPHORE_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_SEMAPHORE_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_SEMAPHORE_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_SEMAPHORE_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_SEMAPHORE_ADDR_HI_addr_63_32_mask) << SDMA_PKT_SEMAPHORE_ADDR_HI_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_FENCE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_FENCE_HEADER_op_offset 0
#define SDMA_PKT_FENCE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_FENCE_HEADER_op_shift  0
#define SDMA_PKT_FENCE_HEADER_OP(x) (((x) & SDMA_PKT_FENCE_HEADER_op_mask) << SDMA_PKT_FENCE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_FENCE_HEADER_sub_op_offset 0
#define SDMA_PKT_FENCE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_FENCE_HEADER_sub_op_shift  8
#define SDMA_PKT_FENCE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_FENCE_HEADER_sub_op_mask) << SDMA_PKT_FENCE_HEADER_sub_op_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_FENCE_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_FENCE_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_FENCE_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_FENCE_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_FENCE_ADDR_LO_addr_31_0_mask) << SDMA_PKT_FENCE_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_FENCE_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_FENCE_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_FENCE_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_FENCE_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_FENCE_ADDR_HI_addr_63_32_mask) << SDMA_PKT_FENCE_ADDR_HI_addr_63_32_shift)

/*define for DATA word*/
/*define for data field*/
#define SDMA_PKT_FENCE_DATA_data_offset 3
#define SDMA_PKT_FENCE_DATA_data_mask   0xFFFFFFFF
#define SDMA_PKT_FENCE_DATA_data_shift  0
#define SDMA_PKT_FENCE_DATA_DATA(x) (((x) & SDMA_PKT_FENCE_DATA_data_mask) << SDMA_PKT_FENCE_DATA_data_shift)


/*
** Definitions for SDMA_PKT_SRBM_WRITE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_SRBM_WRITE_HEADER_op_offset 0
#define SDMA_PKT_SRBM_WRITE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_SRBM_WRITE_HEADER_op_shift  0
#define SDMA_PKT_SRBM_WRITE_HEADER_OP(x) (((x) & SDMA_PKT_SRBM_WRITE_HEADER_op_mask) << SDMA_PKT_SRBM_WRITE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_SRBM_WRITE_HEADER_sub_op_offset 0
#define SDMA_PKT_SRBM_WRITE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_SRBM_WRITE_HEADER_sub_op_shift  8
#define SDMA_PKT_SRBM_WRITE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_SRBM_WRITE_HEADER_sub_op_mask) << SDMA_PKT_SRBM_WRITE_HEADER_sub_op_shift)

/*define for byte_en field*/
#define SDMA_PKT_SRBM_WRITE_HEADER_byte_en_offset 0
#define SDMA_PKT_SRBM_WRITE_HEADER_byte_en_mask   0x0000000F
#define SDMA_PKT_SRBM_WRITE_HEADER_byte_en_shift  28
#define SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(x) (((x) & SDMA_PKT_SRBM_WRITE_HEADER_byte_en_mask) << SDMA_PKT_SRBM_WRITE_HEADER_byte_en_shift)

/*define for ADDR word*/
/*define for addr field*/
#define SDMA_PKT_SRBM_WRITE_ADDR_addr_offset 1
#define SDMA_PKT_SRBM_WRITE_ADDR_addr_mask   0x0003FFFF
#define SDMA_PKT_SRBM_WRITE_ADDR_addr_shift  0
#define SDMA_PKT_SRBM_WRITE_ADDR_ADDR(x) (((x) & SDMA_PKT_SRBM_WRITE_ADDR_addr_mask) << SDMA_PKT_SRBM_WRITE_ADDR_addr_shift)

/*define for DATA word*/
/*define for data field*/
#define SDMA_PKT_SRBM_WRITE_DATA_data_offset 2
#define SDMA_PKT_SRBM_WRITE_DATA_data_mask   0xFFFFFFFF
#define SDMA_PKT_SRBM_WRITE_DATA_data_shift  0
#define SDMA_PKT_SRBM_WRITE_DATA_DATA(x) (((x) & SDMA_PKT_SRBM_WRITE_DATA_data_mask) << SDMA_PKT_SRBM_WRITE_DATA_data_shift)


/*
** Definitions for SDMA_PKT_PRE_EXE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_PRE_EXE_HEADER_op_offset 0
#define SDMA_PKT_PRE_EXE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_PRE_EXE_HEADER_op_shift  0
#define SDMA_PKT_PRE_EXE_HEADER_OP(x) (((x) & SDMA_PKT_PRE_EXE_HEADER_op_mask) << SDMA_PKT_PRE_EXE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_PRE_EXE_HEADER_sub_op_offset 0
#define SDMA_PKT_PRE_EXE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_PRE_EXE_HEADER_sub_op_shift  8
#define SDMA_PKT_PRE_EXE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_PRE_EXE_HEADER_sub_op_mask) << SDMA_PKT_PRE_EXE_HEADER_sub_op_shift)

/*define for dev_sel field*/
#define SDMA_PKT_PRE_EXE_HEADER_dev_sel_offset 0
#define SDMA_PKT_PRE_EXE_HEADER_dev_sel_mask   0x000000FF
#define SDMA_PKT_PRE_EXE_HEADER_dev_sel_shift  16
#define SDMA_PKT_PRE_EXE_HEADER_DEV_SEL(x) (((x) & SDMA_PKT_PRE_EXE_HEADER_dev_sel_mask) << SDMA_PKT_PRE_EXE_HEADER_dev_sel_shift)

/*define for EXEC_COUNT word*/
/*define for exec_count field*/
#define SDMA_PKT_PRE_EXE_EXEC_COUNT_exec_count_offset 1
#define SDMA_PKT_PRE_EXE_EXEC_COUNT_exec_count_mask   0x00003FFF
#define SDMA_PKT_PRE_EXE_EXEC_COUNT_exec_count_shift  0
#define SDMA_PKT_PRE_EXE_EXEC_COUNT_EXEC_COUNT(x) (((x) & SDMA_PKT_PRE_EXE_EXEC_COUNT_exec_count_mask) << SDMA_PKT_PRE_EXE_EXEC_COUNT_exec_count_shift)


/*
** Definitions for SDMA_PKT_COND_EXE packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_COND_EXE_HEADER_op_offset 0
#define SDMA_PKT_COND_EXE_HEADER_op_mask   0x000000FF
#define SDMA_PKT_COND_EXE_HEADER_op_shift  0
#define SDMA_PKT_COND_EXE_HEADER_OP(x) (((x) & SDMA_PKT_COND_EXE_HEADER_op_mask) << SDMA_PKT_COND_EXE_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_COND_EXE_HEADER_sub_op_offset 0
#define SDMA_PKT_COND_EXE_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_COND_EXE_HEADER_sub_op_shift  8
#define SDMA_PKT_COND_EXE_HEADER_SUB_OP(x) (((x) & SDMA_PKT_COND_EXE_HEADER_sub_op_mask) << SDMA_PKT_COND_EXE_HEADER_sub_op_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_COND_EXE_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_COND_EXE_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_COND_EXE_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_COND_EXE_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_COND_EXE_ADDR_LO_addr_31_0_mask) << SDMA_PKT_COND_EXE_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_COND_EXE_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_COND_EXE_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_COND_EXE_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_COND_EXE_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_COND_EXE_ADDR_HI_addr_63_32_mask) << SDMA_PKT_COND_EXE_ADDR_HI_addr_63_32_shift)

/*define for REFERENCE word*/
/*define for reference field*/
#define SDMA_PKT_COND_EXE_REFERENCE_reference_offset 3
#define SDMA_PKT_COND_EXE_REFERENCE_reference_mask   0xFFFFFFFF
#define SDMA_PKT_COND_EXE_REFERENCE_reference_shift  0
#define SDMA_PKT_COND_EXE_REFERENCE_REFERENCE(x) (((x) & SDMA_PKT_COND_EXE_REFERENCE_reference_mask) << SDMA_PKT_COND_EXE_REFERENCE_reference_shift)

/*define for EXEC_COUNT word*/
/*define for exec_count field*/
#define SDMA_PKT_COND_EXE_EXEC_COUNT_exec_count_offset 4
#define SDMA_PKT_COND_EXE_EXEC_COUNT_exec_count_mask   0x00003FFF
#define SDMA_PKT_COND_EXE_EXEC_COUNT_exec_count_shift  0
#define SDMA_PKT_COND_EXE_EXEC_COUNT_EXEC_COUNT(x) (((x) & SDMA_PKT_COND_EXE_EXEC_COUNT_exec_count_mask) << SDMA_PKT_COND_EXE_EXEC_COUNT_exec_count_shift)


/*
** Definitions for SDMA_PKT_CONSTANT_FILL packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_CONSTANT_FILL_HEADER_op_offset 0
#define SDMA_PKT_CONSTANT_FILL_HEADER_op_mask   0x000000FF
#define SDMA_PKT_CONSTANT_FILL_HEADER_op_shift  0
#define SDMA_PKT_CONSTANT_FILL_HEADER_OP(x) (((x) & SDMA_PKT_CONSTANT_FILL_HEADER_op_mask) << SDMA_PKT_CONSTANT_FILL_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_CONSTANT_FILL_HEADER_sub_op_offset 0
#define SDMA_PKT_CONSTANT_FILL_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_CONSTANT_FILL_HEADER_sub_op_shift  8
#define SDMA_PKT_CONSTANT_FILL_HEADER_SUB_OP(x) (((x) & SDMA_PKT_CONSTANT_FILL_HEADER_sub_op_mask) << SDMA_PKT_CONSTANT_FILL_HEADER_sub_op_shift)

/*define for sw field*/
#define SDMA_PKT_CONSTANT_FILL_HEADER_sw_offset 0
#define SDMA_PKT_CONSTANT_FILL_HEADER_sw_mask   0x00000003
#define SDMA_PKT_CONSTANT_FILL_HEADER_sw_shift  16
#define SDMA_PKT_CONSTANT_FILL_HEADER_SW(x) (((x) & SDMA_PKT_CONSTANT_FILL_HEADER_sw_mask) << SDMA_PKT_CONSTANT_FILL_HEADER_sw_shift)

/*define for fillsize field*/
#define SDMA_PKT_CONSTANT_FILL_HEADER_fillsize_offset 0
#define SDMA_PKT_CONSTANT_FILL_HEADER_fillsize_mask   0x00000003
#define SDMA_PKT_CONSTANT_FILL_HEADER_fillsize_shift  30
#define SDMA_PKT_CONSTANT_FILL_HEADER_FILLSIZE(x) (((x) & SDMA_PKT_CONSTANT_FILL_HEADER_fillsize_mask) << SDMA_PKT_CONSTANT_FILL_HEADER_fillsize_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_dst_addr_31_0_offset 1
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_CONSTANT_FILL_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_dst_addr_63_32_offset 2
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_CONSTANT_FILL_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for DATA word*/
/*define for src_data_31_0 field*/
#define SDMA_PKT_CONSTANT_FILL_DATA_src_data_31_0_offset 3
#define SDMA_PKT_CONSTANT_FILL_DATA_src_data_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_CONSTANT_FILL_DATA_src_data_31_0_shift  0
#define SDMA_PKT_CONSTANT_FILL_DATA_SRC_DATA_31_0(x) (((x) & SDMA_PKT_CONSTANT_FILL_DATA_src_data_31_0_mask) << SDMA_PKT_CONSTANT_FILL_DATA_src_data_31_0_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_PKT_CONSTANT_FILL_COUNT_count_offset 4
#define SDMA_PKT_CONSTANT_FILL_COUNT_count_mask   0x003FFFFF
#define SDMA_PKT_CONSTANT_FILL_COUNT_count_shift  0
#define SDMA_PKT_CONSTANT_FILL_COUNT_COUNT(x) (((x) & SDMA_PKT_CONSTANT_FILL_COUNT_count_mask) << SDMA_PKT_CONSTANT_FILL_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_DATA_FILL_MULTI packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_op_offset 0
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_op_mask   0x000000FF
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_op_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_OP(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_HEADER_op_mask) << SDMA_PKT_DATA_FILL_MULTI_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_sub_op_offset 0
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_sub_op_shift  8
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_SUB_OP(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_HEADER_sub_op_mask) << SDMA_PKT_DATA_FILL_MULTI_HEADER_sub_op_shift)

/*define for memlog_clr field*/
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_memlog_clr_offset 0
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_memlog_clr_mask   0x00000001
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_memlog_clr_shift  31
#define SDMA_PKT_DATA_FILL_MULTI_HEADER_MEMLOG_CLR(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_HEADER_memlog_clr_mask) << SDMA_PKT_DATA_FILL_MULTI_HEADER_memlog_clr_shift)

/*define for BYTE_STRIDE word*/
/*define for byte_stride field*/
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_byte_stride_offset 1
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_byte_stride_mask   0xFFFFFFFF
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_byte_stride_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_BYTE_STRIDE(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_byte_stride_mask) << SDMA_PKT_DATA_FILL_MULTI_BYTE_STRIDE_byte_stride_shift)

/*define for DMA_COUNT word*/
/*define for dma_count field*/
#define SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_dma_count_offset 2
#define SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_dma_count_mask   0xFFFFFFFF
#define SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_dma_count_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_DMA_COUNT(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_dma_count_mask) << SDMA_PKT_DATA_FILL_MULTI_DMA_COUNT_dma_count_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_dst_addr_31_0_offset 3
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_dst_addr_63_32_offset 4
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_PKT_DATA_FILL_MULTI_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for BYTE_COUNT word*/
/*define for count field*/
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_count_offset 5
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_count_mask   0x03FFFFFF
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_count_shift  0
#define SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_COUNT(x) (((x) & SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_count_mask) << SDMA_PKT_DATA_FILL_MULTI_BYTE_COUNT_count_shift)


/*
** Definitions for SDMA_PKT_POLL_REGMEM packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_POLL_REGMEM_HEADER_op_offset 0
#define SDMA_PKT_POLL_REGMEM_HEADER_op_mask   0x000000FF
#define SDMA_PKT_POLL_REGMEM_HEADER_op_shift  0
#define SDMA_PKT_POLL_REGMEM_HEADER_OP(x) (((x) & SDMA_PKT_POLL_REGMEM_HEADER_op_mask) << SDMA_PKT_POLL_REGMEM_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_POLL_REGMEM_HEADER_sub_op_offset 0
#define SDMA_PKT_POLL_REGMEM_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_POLL_REGMEM_HEADER_sub_op_shift  8
#define SDMA_PKT_POLL_REGMEM_HEADER_SUB_OP(x) (((x) & SDMA_PKT_POLL_REGMEM_HEADER_sub_op_mask) << SDMA_PKT_POLL_REGMEM_HEADER_sub_op_shift)

/*define for hdp_flush field*/
#define SDMA_PKT_POLL_REGMEM_HEADER_hdp_flush_offset 0
#define SDMA_PKT_POLL_REGMEM_HEADER_hdp_flush_mask   0x00000001
#define SDMA_PKT_POLL_REGMEM_HEADER_hdp_flush_shift  26
#define SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(x) (((x) & SDMA_PKT_POLL_REGMEM_HEADER_hdp_flush_mask) << SDMA_PKT_POLL_REGMEM_HEADER_hdp_flush_shift)

/*define for func field*/
#define SDMA_PKT_POLL_REGMEM_HEADER_func_offset 0
#define SDMA_PKT_POLL_REGMEM_HEADER_func_mask   0x00000007
#define SDMA_PKT_POLL_REGMEM_HEADER_func_shift  28
#define SDMA_PKT_POLL_REGMEM_HEADER_FUNC(x) (((x) & SDMA_PKT_POLL_REGMEM_HEADER_func_mask) << SDMA_PKT_POLL_REGMEM_HEADER_func_shift)

/*define for mem_poll field*/
#define SDMA_PKT_POLL_REGMEM_HEADER_mem_poll_offset 0
#define SDMA_PKT_POLL_REGMEM_HEADER_mem_poll_mask   0x00000001
#define SDMA_PKT_POLL_REGMEM_HEADER_mem_poll_shift  31
#define SDMA_PKT_POLL_REGMEM_HEADER_MEM_POLL(x) (((x) & SDMA_PKT_POLL_REGMEM_HEADER_mem_poll_mask) << SDMA_PKT_POLL_REGMEM_HEADER_mem_poll_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_POLL_REGMEM_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_POLL_REGMEM_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REGMEM_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_POLL_REGMEM_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_POLL_REGMEM_ADDR_LO_addr_31_0_mask) << SDMA_PKT_POLL_REGMEM_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_POLL_REGMEM_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_POLL_REGMEM_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REGMEM_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_POLL_REGMEM_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_POLL_REGMEM_ADDR_HI_addr_63_32_mask) << SDMA_PKT_POLL_REGMEM_ADDR_HI_addr_63_32_shift)

/*define for VALUE word*/
/*define for value field*/
#define SDMA_PKT_POLL_REGMEM_VALUE_value_offset 3
#define SDMA_PKT_POLL_REGMEM_VALUE_value_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REGMEM_VALUE_value_shift  0
#define SDMA_PKT_POLL_REGMEM_VALUE_VALUE(x) (((x) & SDMA_PKT_POLL_REGMEM_VALUE_value_mask) << SDMA_PKT_POLL_REGMEM_VALUE_value_shift)

/*define for MASK word*/
/*define for mask field*/
#define SDMA_PKT_POLL_REGMEM_MASK_mask_offset 4
#define SDMA_PKT_POLL_REGMEM_MASK_mask_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REGMEM_MASK_mask_shift  0
#define SDMA_PKT_POLL_REGMEM_MASK_MASK(x) (((x) & SDMA_PKT_POLL_REGMEM_MASK_mask_mask) << SDMA_PKT_POLL_REGMEM_MASK_mask_shift)

/*define for DW5 word*/
/*define for interval field*/
#define SDMA_PKT_POLL_REGMEM_DW5_interval_offset 5
#define SDMA_PKT_POLL_REGMEM_DW5_interval_mask   0x0000FFFF
#define SDMA_PKT_POLL_REGMEM_DW5_interval_shift  0
#define SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(x) (((x) & SDMA_PKT_POLL_REGMEM_DW5_interval_mask) << SDMA_PKT_POLL_REGMEM_DW5_interval_shift)

/*define for retry_count field*/
#define SDMA_PKT_POLL_REGMEM_DW5_retry_count_offset 5
#define SDMA_PKT_POLL_REGMEM_DW5_retry_count_mask   0x00000FFF
#define SDMA_PKT_POLL_REGMEM_DW5_retry_count_shift  16
#define SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(x) (((x) & SDMA_PKT_POLL_REGMEM_DW5_retry_count_mask) << SDMA_PKT_POLL_REGMEM_DW5_retry_count_shift)


/*
** Definitions for SDMA_PKT_POLL_REG_WRITE_MEM packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_op_offset 0
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_op_mask   0x000000FF
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_op_shift  0
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_OP(x) (((x) & SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_op_mask) << SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_sub_op_offset 0
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_sub_op_shift  8
#define SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_SUB_OP(x) (((x) & SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_sub_op_mask) << SDMA_PKT_POLL_REG_WRITE_MEM_HEADER_sub_op_shift)

/*define for SRC_ADDR word*/
/*define for addr_31_2 field*/
#define SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_addr_31_2_offset 1
#define SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_addr_31_2_mask   0x3FFFFFFF
#define SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_addr_31_2_shift  2
#define SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_ADDR_31_2(x) (((x) & SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_addr_31_2_mask) << SDMA_PKT_POLL_REG_WRITE_MEM_SRC_ADDR_addr_31_2_shift)

/*define for DST_ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_addr_31_0_offset 2
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_addr_31_0_mask) << SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_LO_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_addr_63_32_offset 3
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_addr_63_32_mask) << SDMA_PKT_POLL_REG_WRITE_MEM_DST_ADDR_HI_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_POLL_DBIT_WRITE_MEM packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_op_offset 0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_op_mask   0x000000FF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_op_shift  0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_OP(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_op_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_sub_op_offset 0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_sub_op_shift  8
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_SUB_OP(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_sub_op_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_sub_op_shift)

/*define for ea field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_ea_offset 0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_ea_mask   0x00000003
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_ea_shift  16
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_EA(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_ea_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_HEADER_ea_shift)

/*define for DST_ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_addr_31_0_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_LO_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_addr_63_32_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_DST_ADDR_HI_addr_63_32_shift)

/*define for START_PAGE word*/
/*define for addr_31_4 field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_addr_31_4_offset 3
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_addr_31_4_mask   0x0FFFFFFF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_addr_31_4_shift  4
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_ADDR_31_4(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_addr_31_4_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_START_PAGE_addr_31_4_shift)

/*define for PAGE_NUM word*/
/*define for page_num_31_0 field*/
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_page_num_31_0_offset 4
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_page_num_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_page_num_31_0_shift  0
#define SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_PAGE_NUM_31_0(x) (((x) & SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_page_num_31_0_mask) << SDMA_PKT_POLL_DBIT_WRITE_MEM_PAGE_NUM_page_num_31_0_shift)


/*
** Definitions for SDMA_PKT_POLL_MEM_VERIFY packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_op_offset 0
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_op_mask   0x000000FF
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_op_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_OP(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_HEADER_op_mask) << SDMA_PKT_POLL_MEM_VERIFY_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_sub_op_offset 0
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_sub_op_shift  8
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_SUB_OP(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_HEADER_sub_op_mask) << SDMA_PKT_POLL_MEM_VERIFY_HEADER_sub_op_shift)

/*define for mode field*/
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_mode_offset 0
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_mode_mask   0x00000001
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_mode_shift  31
#define SDMA_PKT_POLL_MEM_VERIFY_HEADER_MODE(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_HEADER_mode_mask) << SDMA_PKT_POLL_MEM_VERIFY_HEADER_mode_shift)

/*define for PATTERN word*/
/*define for pattern field*/
#define SDMA_PKT_POLL_MEM_VERIFY_PATTERN_pattern_offset 1
#define SDMA_PKT_POLL_MEM_VERIFY_PATTERN_pattern_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_PATTERN_pattern_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_PATTERN_PATTERN(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_PATTERN_pattern_mask) << SDMA_PKT_POLL_MEM_VERIFY_PATTERN_pattern_shift)

/*define for CMP0_ADDR_START_LO word*/
/*define for cmp0_start_31_0 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_cmp0_start_31_0_offset 2
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_cmp0_start_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_cmp0_start_31_0_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_CMP0_START_31_0(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_cmp0_start_31_0_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_LO_cmp0_start_31_0_shift)

/*define for CMP0_ADDR_START_HI word*/
/*define for cmp0_start_63_32 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_cmp0_start_63_32_offset 3
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_cmp0_start_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_cmp0_start_63_32_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_CMP0_START_63_32(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_cmp0_start_63_32_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_START_HI_cmp0_start_63_32_shift)

/*define for CMP0_ADDR_END_LO word*/
/*define for cmp1_end_31_0 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_cmp1_end_31_0_offset 4
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_cmp1_end_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_cmp1_end_31_0_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_CMP1_END_31_0(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_cmp1_end_31_0_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_LO_cmp1_end_31_0_shift)

/*define for CMP0_ADDR_END_HI word*/
/*define for cmp1_end_63_32 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_cmp1_end_63_32_offset 5
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_cmp1_end_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_cmp1_end_63_32_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_CMP1_END_63_32(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_cmp1_end_63_32_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP0_ADDR_END_HI_cmp1_end_63_32_shift)

/*define for CMP1_ADDR_START_LO word*/
/*define for cmp1_start_31_0 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_cmp1_start_31_0_offset 6
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_cmp1_start_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_cmp1_start_31_0_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_CMP1_START_31_0(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_cmp1_start_31_0_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_LO_cmp1_start_31_0_shift)

/*define for CMP1_ADDR_START_HI word*/
/*define for cmp1_start_63_32 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_cmp1_start_63_32_offset 7
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_cmp1_start_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_cmp1_start_63_32_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_CMP1_START_63_32(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_cmp1_start_63_32_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_START_HI_cmp1_start_63_32_shift)

/*define for CMP1_ADDR_END_LO word*/
/*define for cmp1_end_31_0 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_cmp1_end_31_0_offset 8
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_cmp1_end_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_cmp1_end_31_0_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_CMP1_END_31_0(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_cmp1_end_31_0_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_LO_cmp1_end_31_0_shift)

/*define for CMP1_ADDR_END_HI word*/
/*define for cmp1_end_63_32 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_cmp1_end_63_32_offset 9
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_cmp1_end_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_cmp1_end_63_32_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_CMP1_END_63_32(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_cmp1_end_63_32_mask) << SDMA_PKT_POLL_MEM_VERIFY_CMP1_ADDR_END_HI_cmp1_end_63_32_shift)

/*define for REC_ADDR_LO word*/
/*define for rec_31_0 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_rec_31_0_offset 10
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_rec_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_rec_31_0_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_REC_31_0(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_rec_31_0_mask) << SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_LO_rec_31_0_shift)

/*define for REC_ADDR_HI word*/
/*define for rec_63_32 field*/
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_rec_63_32_offset 11
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_rec_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_rec_63_32_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_REC_63_32(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_rec_63_32_mask) << SDMA_PKT_POLL_MEM_VERIFY_REC_ADDR_HI_rec_63_32_shift)

/*define for RESERVED word*/
/*define for reserved field*/
#define SDMA_PKT_POLL_MEM_VERIFY_RESERVED_reserved_offset 12
#define SDMA_PKT_POLL_MEM_VERIFY_RESERVED_reserved_mask   0xFFFFFFFF
#define SDMA_PKT_POLL_MEM_VERIFY_RESERVED_reserved_shift  0
#define SDMA_PKT_POLL_MEM_VERIFY_RESERVED_RESERVED(x) (((x) & SDMA_PKT_POLL_MEM_VERIFY_RESERVED_reserved_mask) << SDMA_PKT_POLL_MEM_VERIFY_RESERVED_reserved_shift)


/*
** Definitions for SDMA_PKT_ATOMIC packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_ATOMIC_HEADER_op_offset 0
#define SDMA_PKT_ATOMIC_HEADER_op_mask   0x000000FF
#define SDMA_PKT_ATOMIC_HEADER_op_shift  0
#define SDMA_PKT_ATOMIC_HEADER_OP(x) (((x) & SDMA_PKT_ATOMIC_HEADER_op_mask) << SDMA_PKT_ATOMIC_HEADER_op_shift)

/*define for loop field*/
#define SDMA_PKT_ATOMIC_HEADER_loop_offset 0
#define SDMA_PKT_ATOMIC_HEADER_loop_mask   0x00000001
#define SDMA_PKT_ATOMIC_HEADER_loop_shift  16
#define SDMA_PKT_ATOMIC_HEADER_LOOP(x) (((x) & SDMA_PKT_ATOMIC_HEADER_loop_mask) << SDMA_PKT_ATOMIC_HEADER_loop_shift)

/*define for tmz field*/
#define SDMA_PKT_ATOMIC_HEADER_tmz_offset 0
#define SDMA_PKT_ATOMIC_HEADER_tmz_mask   0x00000001
#define SDMA_PKT_ATOMIC_HEADER_tmz_shift  18
#define SDMA_PKT_ATOMIC_HEADER_TMZ(x) (((x) & SDMA_PKT_ATOMIC_HEADER_tmz_mask) << SDMA_PKT_ATOMIC_HEADER_tmz_shift)

/*define for atomic_op field*/
#define SDMA_PKT_ATOMIC_HEADER_atomic_op_offset 0
#define SDMA_PKT_ATOMIC_HEADER_atomic_op_mask   0x0000007F
#define SDMA_PKT_ATOMIC_HEADER_atomic_op_shift  25
#define SDMA_PKT_ATOMIC_HEADER_ATOMIC_OP(x) (((x) & SDMA_PKT_ATOMIC_HEADER_atomic_op_mask) << SDMA_PKT_ATOMIC_HEADER_atomic_op_shift)

/*define for ADDR_LO word*/
/*define for addr_31_0 field*/
#define SDMA_PKT_ATOMIC_ADDR_LO_addr_31_0_offset 1
#define SDMA_PKT_ATOMIC_ADDR_LO_addr_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_ADDR_LO_addr_31_0_shift  0
#define SDMA_PKT_ATOMIC_ADDR_LO_ADDR_31_0(x) (((x) & SDMA_PKT_ATOMIC_ADDR_LO_addr_31_0_mask) << SDMA_PKT_ATOMIC_ADDR_LO_addr_31_0_shift)

/*define for ADDR_HI word*/
/*define for addr_63_32 field*/
#define SDMA_PKT_ATOMIC_ADDR_HI_addr_63_32_offset 2
#define SDMA_PKT_ATOMIC_ADDR_HI_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_ADDR_HI_addr_63_32_shift  0
#define SDMA_PKT_ATOMIC_ADDR_HI_ADDR_63_32(x) (((x) & SDMA_PKT_ATOMIC_ADDR_HI_addr_63_32_mask) << SDMA_PKT_ATOMIC_ADDR_HI_addr_63_32_shift)

/*define for SRC_DATA_LO word*/
/*define for src_data_31_0 field*/
#define SDMA_PKT_ATOMIC_SRC_DATA_LO_src_data_31_0_offset 3
#define SDMA_PKT_ATOMIC_SRC_DATA_LO_src_data_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_SRC_DATA_LO_src_data_31_0_shift  0
#define SDMA_PKT_ATOMIC_SRC_DATA_LO_SRC_DATA_31_0(x) (((x) & SDMA_PKT_ATOMIC_SRC_DATA_LO_src_data_31_0_mask) << SDMA_PKT_ATOMIC_SRC_DATA_LO_src_data_31_0_shift)

/*define for SRC_DATA_HI word*/
/*define for src_data_63_32 field*/
#define SDMA_PKT_ATOMIC_SRC_DATA_HI_src_data_63_32_offset 4
#define SDMA_PKT_ATOMIC_SRC_DATA_HI_src_data_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_SRC_DATA_HI_src_data_63_32_shift  0
#define SDMA_PKT_ATOMIC_SRC_DATA_HI_SRC_DATA_63_32(x) (((x) & SDMA_PKT_ATOMIC_SRC_DATA_HI_src_data_63_32_mask) << SDMA_PKT_ATOMIC_SRC_DATA_HI_src_data_63_32_shift)

/*define for CMP_DATA_LO word*/
/*define for cmp_data_31_0 field*/
#define SDMA_PKT_ATOMIC_CMP_DATA_LO_cmp_data_31_0_offset 5
#define SDMA_PKT_ATOMIC_CMP_DATA_LO_cmp_data_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_CMP_DATA_LO_cmp_data_31_0_shift  0
#define SDMA_PKT_ATOMIC_CMP_DATA_LO_CMP_DATA_31_0(x) (((x) & SDMA_PKT_ATOMIC_CMP_DATA_LO_cmp_data_31_0_mask) << SDMA_PKT_ATOMIC_CMP_DATA_LO_cmp_data_31_0_shift)

/*define for CMP_DATA_HI word*/
/*define for cmp_data_63_32 field*/
#define SDMA_PKT_ATOMIC_CMP_DATA_HI_cmp_data_63_32_offset 6
#define SDMA_PKT_ATOMIC_CMP_DATA_HI_cmp_data_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_ATOMIC_CMP_DATA_HI_cmp_data_63_32_shift  0
#define SDMA_PKT_ATOMIC_CMP_DATA_HI_CMP_DATA_63_32(x) (((x) & SDMA_PKT_ATOMIC_CMP_DATA_HI_cmp_data_63_32_mask) << SDMA_PKT_ATOMIC_CMP_DATA_HI_cmp_data_63_32_shift)

/*define for LOOP_INTERVAL word*/
/*define for loop_interval field*/
#define SDMA_PKT_ATOMIC_LOOP_INTERVAL_loop_interval_offset 7
#define SDMA_PKT_ATOMIC_LOOP_INTERVAL_loop_interval_mask   0x00001FFF
#define SDMA_PKT_ATOMIC_LOOP_INTERVAL_loop_interval_shift  0
#define SDMA_PKT_ATOMIC_LOOP_INTERVAL_LOOP_INTERVAL(x) (((x) & SDMA_PKT_ATOMIC_LOOP_INTERVAL_loop_interval_mask) << SDMA_PKT_ATOMIC_LOOP_INTERVAL_loop_interval_shift)


/*
** Definitions for SDMA_PKT_TIMESTAMP_SET packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_TIMESTAMP_SET_HEADER_op_offset 0
#define SDMA_PKT_TIMESTAMP_SET_HEADER_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_SET_HEADER_op_shift  0
#define SDMA_PKT_TIMESTAMP_SET_HEADER_OP(x) (((x) & SDMA_PKT_TIMESTAMP_SET_HEADER_op_mask) << SDMA_PKT_TIMESTAMP_SET_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_TIMESTAMP_SET_HEADER_sub_op_offset 0
#define SDMA_PKT_TIMESTAMP_SET_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_SET_HEADER_sub_op_shift  8
#define SDMA_PKT_TIMESTAMP_SET_HEADER_SUB_OP(x) (((x) & SDMA_PKT_TIMESTAMP_SET_HEADER_sub_op_mask) << SDMA_PKT_TIMESTAMP_SET_HEADER_sub_op_shift)

/*define for INIT_DATA_LO word*/
/*define for init_data_31_0 field*/
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_init_data_31_0_offset 1
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_init_data_31_0_mask   0xFFFFFFFF
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_init_data_31_0_shift  0
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_INIT_DATA_31_0(x) (((x) & SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_init_data_31_0_mask) << SDMA_PKT_TIMESTAMP_SET_INIT_DATA_LO_init_data_31_0_shift)

/*define for INIT_DATA_HI word*/
/*define for init_data_63_32 field*/
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_init_data_63_32_offset 2
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_init_data_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_init_data_63_32_shift  0
#define SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_INIT_DATA_63_32(x) (((x) & SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_init_data_63_32_mask) << SDMA_PKT_TIMESTAMP_SET_INIT_DATA_HI_init_data_63_32_shift)


/*
** Definitions for SDMA_PKT_TIMESTAMP_GET packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_TIMESTAMP_GET_HEADER_op_offset 0
#define SDMA_PKT_TIMESTAMP_GET_HEADER_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_GET_HEADER_op_shift  0
#define SDMA_PKT_TIMESTAMP_GET_HEADER_OP(x) (((x) & SDMA_PKT_TIMESTAMP_GET_HEADER_op_mask) << SDMA_PKT_TIMESTAMP_GET_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_TIMESTAMP_GET_HEADER_sub_op_offset 0
#define SDMA_PKT_TIMESTAMP_GET_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_GET_HEADER_sub_op_shift  8
#define SDMA_PKT_TIMESTAMP_GET_HEADER_SUB_OP(x) (((x) & SDMA_PKT_TIMESTAMP_GET_HEADER_sub_op_mask) << SDMA_PKT_TIMESTAMP_GET_HEADER_sub_op_shift)

/*define for WRITE_ADDR_LO word*/
/*define for write_addr_31_3 field*/
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_write_addr_31_3_offset 1
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_write_addr_31_3_mask   0x1FFFFFFF
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_write_addr_31_3_shift  3
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_WRITE_ADDR_31_3(x) (((x) & SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_write_addr_31_3_mask) << SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_LO_write_addr_31_3_shift)

/*define for WRITE_ADDR_HI word*/
/*define for write_addr_63_32 field*/
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_write_addr_63_32_offset 2
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_write_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_write_addr_63_32_shift  0
#define SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_WRITE_ADDR_63_32(x) (((x) & SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_write_addr_63_32_mask) << SDMA_PKT_TIMESTAMP_GET_WRITE_ADDR_HI_write_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_TIMESTAMP_GET_GLOBAL packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_op_offset 0
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_op_shift  0
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_OP(x) (((x) & SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_op_mask) << SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_sub_op_offset 0
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_sub_op_shift  8
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_SUB_OP(x) (((x) & SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_sub_op_mask) << SDMA_PKT_TIMESTAMP_GET_GLOBAL_HEADER_sub_op_shift)

/*define for WRITE_ADDR_LO word*/
/*define for write_addr_31_3 field*/
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_write_addr_31_3_offset 1
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_write_addr_31_3_mask   0x1FFFFFFF
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_write_addr_31_3_shift  3
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_WRITE_ADDR_31_3(x) (((x) & SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_write_addr_31_3_mask) << SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_LO_write_addr_31_3_shift)

/*define for WRITE_ADDR_HI word*/
/*define for write_addr_63_32 field*/
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_write_addr_63_32_offset 2
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_write_addr_63_32_mask   0xFFFFFFFF
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_write_addr_63_32_shift  0
#define SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_WRITE_ADDR_63_32(x) (((x) & SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_write_addr_63_32_mask) << SDMA_PKT_TIMESTAMP_GET_GLOBAL_WRITE_ADDR_HI_write_addr_63_32_shift)


/*
** Definitions for SDMA_PKT_TRAP packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_TRAP_HEADER_op_offset 0
#define SDMA_PKT_TRAP_HEADER_op_mask   0x000000FF
#define SDMA_PKT_TRAP_HEADER_op_shift  0
#define SDMA_PKT_TRAP_HEADER_OP(x) (((x) & SDMA_PKT_TRAP_HEADER_op_mask) << SDMA_PKT_TRAP_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_TRAP_HEADER_sub_op_offset 0
#define SDMA_PKT_TRAP_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_TRAP_HEADER_sub_op_shift  8
#define SDMA_PKT_TRAP_HEADER_SUB_OP(x) (((x) & SDMA_PKT_TRAP_HEADER_sub_op_mask) << SDMA_PKT_TRAP_HEADER_sub_op_shift)

/*define for INT_CONTEXT word*/
/*define for int_context field*/
#define SDMA_PKT_TRAP_INT_CONTEXT_int_context_offset 1
#define SDMA_PKT_TRAP_INT_CONTEXT_int_context_mask   0x0FFFFFFF
#define SDMA_PKT_TRAP_INT_CONTEXT_int_context_shift  0
#define SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(x) (((x) & SDMA_PKT_TRAP_INT_CONTEXT_int_context_mask) << SDMA_PKT_TRAP_INT_CONTEXT_int_context_shift)


/*
** Definitions for SDMA_PKT_DUMMY_TRAP packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_DUMMY_TRAP_HEADER_op_offset 0
#define SDMA_PKT_DUMMY_TRAP_HEADER_op_mask   0x000000FF
#define SDMA_PKT_DUMMY_TRAP_HEADER_op_shift  0
#define SDMA_PKT_DUMMY_TRAP_HEADER_OP(x) (((x) & SDMA_PKT_DUMMY_TRAP_HEADER_op_mask) << SDMA_PKT_DUMMY_TRAP_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_DUMMY_TRAP_HEADER_sub_op_offset 0
#define SDMA_PKT_DUMMY_TRAP_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_DUMMY_TRAP_HEADER_sub_op_shift  8
#define SDMA_PKT_DUMMY_TRAP_HEADER_SUB_OP(x) (((x) & SDMA_PKT_DUMMY_TRAP_HEADER_sub_op_mask) << SDMA_PKT_DUMMY_TRAP_HEADER_sub_op_shift)

/*define for INT_CONTEXT word*/
/*define for int_context field*/
#define SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_int_context_offset 1
#define SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_int_context_mask   0x0FFFFFFF
#define SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_int_context_shift  0
#define SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_INT_CONTEXT(x) (((x) & SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_int_context_mask) << SDMA_PKT_DUMMY_TRAP_INT_CONTEXT_int_context_shift)


/*
** Definitions for SDMA_PKT_NOP packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_NOP_HEADER_op_offset 0
#define SDMA_PKT_NOP_HEADER_op_mask   0x000000FF
#define SDMA_PKT_NOP_HEADER_op_shift  0
#define SDMA_PKT_NOP_HEADER_OP(x) (((x) & SDMA_PKT_NOP_HEADER_op_mask) << SDMA_PKT_NOP_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_NOP_HEADER_sub_op_offset 0
#define SDMA_PKT_NOP_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_NOP_HEADER_sub_op_shift  8
#define SDMA_PKT_NOP_HEADER_SUB_OP(x) (((x) & SDMA_PKT_NOP_HEADER_sub_op_mask) << SDMA_PKT_NOP_HEADER_sub_op_shift)

/*define for count field*/
#define SDMA_PKT_NOP_HEADER_count_offset 0
#define SDMA_PKT_NOP_HEADER_count_mask   0x00003FFF
#define SDMA_PKT_NOP_HEADER_count_shift  16
#define SDMA_PKT_NOP_HEADER_COUNT(x) (((x) & SDMA_PKT_NOP_HEADER_count_mask) << SDMA_PKT_NOP_HEADER_count_shift)

/*define for DATA0 word*/
/*define for data0 field*/
#define SDMA_PKT_NOP_DATA0_data0_offset 1
#define SDMA_PKT_NOP_DATA0_data0_mask   0xFFFFFFFF
#define SDMA_PKT_NOP_DATA0_data0_shift  0
#define SDMA_PKT_NOP_DATA0_DATA0(x) (((x) & SDMA_PKT_NOP_DATA0_data0_mask) << SDMA_PKT_NOP_DATA0_data0_shift)


/*
** Definitions for SDMA_AQL_PKT_HEADER packet
*/

/*define for HEADER word*/
/*define for format field*/
#define SDMA_AQL_PKT_HEADER_HEADER_format_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_format_mask   0x000000FF
#define SDMA_AQL_PKT_HEADER_HEADER_format_shift  0
#define SDMA_AQL_PKT_HEADER_HEADER_FORMAT(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_format_mask) << SDMA_AQL_PKT_HEADER_HEADER_format_shift)

/*define for barrier field*/
#define SDMA_AQL_PKT_HEADER_HEADER_barrier_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_barrier_mask   0x00000001
#define SDMA_AQL_PKT_HEADER_HEADER_barrier_shift  8
#define SDMA_AQL_PKT_HEADER_HEADER_BARRIER(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_barrier_mask) << SDMA_AQL_PKT_HEADER_HEADER_barrier_shift)

/*define for acquire_fence_scope field*/
#define SDMA_AQL_PKT_HEADER_HEADER_acquire_fence_scope_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_acquire_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_HEADER_HEADER_acquire_fence_scope_shift  9
#define SDMA_AQL_PKT_HEADER_HEADER_ACQUIRE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_acquire_fence_scope_mask) << SDMA_AQL_PKT_HEADER_HEADER_acquire_fence_scope_shift)

/*define for release_fence_scope field*/
#define SDMA_AQL_PKT_HEADER_HEADER_release_fence_scope_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_release_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_HEADER_HEADER_release_fence_scope_shift  11
#define SDMA_AQL_PKT_HEADER_HEADER_RELEASE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_release_fence_scope_mask) << SDMA_AQL_PKT_HEADER_HEADER_release_fence_scope_shift)

/*define for reserved field*/
#define SDMA_AQL_PKT_HEADER_HEADER_reserved_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_reserved_mask   0x00000007
#define SDMA_AQL_PKT_HEADER_HEADER_reserved_shift  13
#define SDMA_AQL_PKT_HEADER_HEADER_RESERVED(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_reserved_mask) << SDMA_AQL_PKT_HEADER_HEADER_reserved_shift)

/*define for op field*/
#define SDMA_AQL_PKT_HEADER_HEADER_op_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_op_mask   0x0000000F
#define SDMA_AQL_PKT_HEADER_HEADER_op_shift  16
#define SDMA_AQL_PKT_HEADER_HEADER_OP(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_op_mask) << SDMA_AQL_PKT_HEADER_HEADER_op_shift)

/*define for subop field*/
#define SDMA_AQL_PKT_HEADER_HEADER_subop_offset 0
#define SDMA_AQL_PKT_HEADER_HEADER_subop_mask   0x00000007
#define SDMA_AQL_PKT_HEADER_HEADER_subop_shift  20
#define SDMA_AQL_PKT_HEADER_HEADER_SUBOP(x) (((x) & SDMA_AQL_PKT_HEADER_HEADER_subop_mask) << SDMA_AQL_PKT_HEADER_HEADER_subop_shift)


/*
** Definitions for SDMA_AQL_PKT_COPY_LINEAR packet
*/

/*define for HEADER word*/
/*define for format field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_format_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_format_mask   0x000000FF
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_format_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_FORMAT(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_format_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_format_shift)

/*define for barrier field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_barrier_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_barrier_mask   0x00000001
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_barrier_shift  8
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_BARRIER(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_barrier_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_barrier_shift)

/*define for acquire_fence_scope field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_acquire_fence_scope_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_acquire_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_acquire_fence_scope_shift  9
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_ACQUIRE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_acquire_fence_scope_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_acquire_fence_scope_shift)

/*define for release_fence_scope field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_release_fence_scope_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_release_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_release_fence_scope_shift  11
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_RELEASE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_release_fence_scope_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_release_fence_scope_shift)

/*define for reserved field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_reserved_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_reserved_mask   0x00000007
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_reserved_shift  13
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_RESERVED(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_reserved_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_reserved_shift)

/*define for op field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_op_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_op_mask   0x0000000F
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_op_shift  16
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_OP(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_op_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_op_shift)

/*define for subop field*/
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_subop_offset 0
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_subop_mask   0x00000007
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_subop_shift  20
#define SDMA_AQL_PKT_COPY_LINEAR_HEADER_SUBOP(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_HEADER_subop_mask) << SDMA_AQL_PKT_COPY_LINEAR_HEADER_subop_shift)

/*define for RESERVED_DW1 word*/
/*define for reserved_dw1 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_reserved_dw1_offset 1
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_reserved_dw1_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_reserved_dw1_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_RESERVED_DW1(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_reserved_dw1_mask) << SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW1_reserved_dw1_shift)

/*define for RETURN_ADDR_LO word*/
/*define for return_addr_31_0 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_return_addr_31_0_offset 2
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_return_addr_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_return_addr_31_0_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_RETURN_ADDR_31_0(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_return_addr_31_0_mask) << SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_LO_return_addr_31_0_shift)

/*define for RETURN_ADDR_HI word*/
/*define for return_addr_63_32 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_return_addr_63_32_offset 3
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_return_addr_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_return_addr_63_32_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_RETURN_ADDR_63_32(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_return_addr_63_32_mask) << SDMA_AQL_PKT_COPY_LINEAR_RETURN_ADDR_HI_return_addr_63_32_shift)

/*define for COUNT word*/
/*define for count field*/
#define SDMA_AQL_PKT_COPY_LINEAR_COUNT_count_offset 4
#define SDMA_AQL_PKT_COPY_LINEAR_COUNT_count_mask   0x003FFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_COUNT_count_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_COUNT_COUNT(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_COUNT_count_mask) << SDMA_AQL_PKT_COPY_LINEAR_COUNT_count_shift)

/*define for PARAMETER word*/
/*define for dst_sw field*/
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_dst_sw_offset 5
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_dst_sw_mask   0x00000003
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_dst_sw_shift  16
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_DST_SW(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_dst_sw_mask) << SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_dst_sw_shift)

/*define for src_sw field*/
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_src_sw_offset 5
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_src_sw_mask   0x00000003
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_src_sw_shift  24
#define SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_SRC_SW(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_src_sw_mask) << SDMA_AQL_PKT_COPY_LINEAR_PARAMETER_src_sw_shift)

/*define for SRC_ADDR_LO word*/
/*define for src_addr_31_0 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_offset 6
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_SRC_ADDR_31_0(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_mask) << SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_LO_src_addr_31_0_shift)

/*define for SRC_ADDR_HI word*/
/*define for src_addr_63_32 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_offset 7
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_SRC_ADDR_63_32(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_mask) << SDMA_AQL_PKT_COPY_LINEAR_SRC_ADDR_HI_src_addr_63_32_shift)

/*define for DST_ADDR_LO word*/
/*define for dst_addr_31_0 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_offset 8
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_DST_ADDR_31_0(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_mask) << SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_LO_dst_addr_31_0_shift)

/*define for DST_ADDR_HI word*/
/*define for dst_addr_63_32 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_offset 9
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_DST_ADDR_63_32(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_mask) << SDMA_AQL_PKT_COPY_LINEAR_DST_ADDR_HI_dst_addr_63_32_shift)

/*define for RESERVED_DW10 word*/
/*define for reserved_dw10 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_reserved_dw10_offset 10
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_reserved_dw10_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_reserved_dw10_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_RESERVED_DW10(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_reserved_dw10_mask) << SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW10_reserved_dw10_shift)

/*define for RESERVED_DW11 word*/
/*define for reserved_dw11 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_reserved_dw11_offset 11
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_reserved_dw11_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_reserved_dw11_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_RESERVED_DW11(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_reserved_dw11_mask) << SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW11_reserved_dw11_shift)

/*define for RESERVED_DW12 word*/
/*define for reserved_dw12 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_reserved_dw12_offset 12
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_reserved_dw12_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_reserved_dw12_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_RESERVED_DW12(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_reserved_dw12_mask) << SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW12_reserved_dw12_shift)

/*define for RESERVED_DW13 word*/
/*define for reserved_dw13 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_reserved_dw13_offset 13
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_reserved_dw13_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_reserved_dw13_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_RESERVED_DW13(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_reserved_dw13_mask) << SDMA_AQL_PKT_COPY_LINEAR_RESERVED_DW13_reserved_dw13_shift)

/*define for COMPLETION_SIGNAL_LO word*/
/*define for completion_signal_31_0 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_completion_signal_31_0_offset 14
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_completion_signal_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_completion_signal_31_0_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_COMPLETION_SIGNAL_31_0(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_completion_signal_31_0_mask) << SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_LO_completion_signal_31_0_shift)

/*define for COMPLETION_SIGNAL_HI word*/
/*define for completion_signal_63_32 field*/
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_completion_signal_63_32_offset 15
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_completion_signal_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_completion_signal_63_32_shift  0
#define SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_COMPLETION_SIGNAL_63_32(x) (((x) & SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_completion_signal_63_32_mask) << SDMA_AQL_PKT_COPY_LINEAR_COMPLETION_SIGNAL_HI_completion_signal_63_32_shift)


/*
** Definitions for SDMA_AQL_PKT_BARRIER_OR packet
*/

/*define for HEADER word*/
/*define for format field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_format_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_format_mask   0x000000FF
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_format_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_FORMAT(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_format_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_format_shift)

/*define for barrier field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_barrier_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_barrier_mask   0x00000001
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_barrier_shift  8
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_BARRIER(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_barrier_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_barrier_shift)

/*define for acquire_fence_scope field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_acquire_fence_scope_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_acquire_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_acquire_fence_scope_shift  9
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_ACQUIRE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_acquire_fence_scope_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_acquire_fence_scope_shift)

/*define for release_fence_scope field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_release_fence_scope_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_release_fence_scope_mask   0x00000003
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_release_fence_scope_shift  11
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_RELEASE_FENCE_SCOPE(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_release_fence_scope_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_release_fence_scope_shift)

/*define for reserved field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_reserved_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_reserved_mask   0x00000007
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_reserved_shift  13
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_RESERVED(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_reserved_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_reserved_shift)

/*define for op field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_op_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_op_mask   0x0000000F
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_op_shift  16
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_OP(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_op_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_op_shift)

/*define for subop field*/
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_subop_offset 0
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_subop_mask   0x00000007
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_subop_shift  20
#define SDMA_AQL_PKT_BARRIER_OR_HEADER_SUBOP(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_HEADER_subop_mask) << SDMA_AQL_PKT_BARRIER_OR_HEADER_subop_shift)

/*define for RESERVED_DW1 word*/
/*define for reserved_dw1 field*/
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_reserved_dw1_offset 1
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_reserved_dw1_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_reserved_dw1_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_RESERVED_DW1(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_reserved_dw1_mask) << SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW1_reserved_dw1_shift)

/*define for DEPENDENT_ADDR_0_LO word*/
/*define for dependent_addr_0_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_dependent_addr_0_31_0_offset 2
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_dependent_addr_0_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_dependent_addr_0_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_DEPENDENT_ADDR_0_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_dependent_addr_0_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_LO_dependent_addr_0_31_0_shift)

/*define for DEPENDENT_ADDR_0_HI word*/
/*define for dependent_addr_0_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_dependent_addr_0_63_32_offset 3
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_dependent_addr_0_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_dependent_addr_0_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_DEPENDENT_ADDR_0_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_dependent_addr_0_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_0_HI_dependent_addr_0_63_32_shift)

/*define for DEPENDENT_ADDR_1_LO word*/
/*define for dependent_addr_1_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_dependent_addr_1_31_0_offset 4
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_dependent_addr_1_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_dependent_addr_1_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_DEPENDENT_ADDR_1_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_dependent_addr_1_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_LO_dependent_addr_1_31_0_shift)

/*define for DEPENDENT_ADDR_1_HI word*/
/*define for dependent_addr_1_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_dependent_addr_1_63_32_offset 5
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_dependent_addr_1_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_dependent_addr_1_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_DEPENDENT_ADDR_1_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_dependent_addr_1_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_1_HI_dependent_addr_1_63_32_shift)

/*define for DEPENDENT_ADDR_2_LO word*/
/*define for dependent_addr_2_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_dependent_addr_2_31_0_offset 6
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_dependent_addr_2_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_dependent_addr_2_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_DEPENDENT_ADDR_2_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_dependent_addr_2_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_LO_dependent_addr_2_31_0_shift)

/*define for DEPENDENT_ADDR_2_HI word*/
/*define for dependent_addr_2_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_dependent_addr_2_63_32_offset 7
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_dependent_addr_2_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_dependent_addr_2_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_DEPENDENT_ADDR_2_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_dependent_addr_2_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_2_HI_dependent_addr_2_63_32_shift)

/*define for DEPENDENT_ADDR_3_LO word*/
/*define for dependent_addr_3_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_dependent_addr_3_31_0_offset 8
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_dependent_addr_3_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_dependent_addr_3_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_DEPENDENT_ADDR_3_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_dependent_addr_3_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_LO_dependent_addr_3_31_0_shift)

/*define for DEPENDENT_ADDR_3_HI word*/
/*define for dependent_addr_3_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_dependent_addr_3_63_32_offset 9
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_dependent_addr_3_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_dependent_addr_3_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_DEPENDENT_ADDR_3_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_dependent_addr_3_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_3_HI_dependent_addr_3_63_32_shift)

/*define for DEPENDENT_ADDR_4_LO word*/
/*define for dependent_addr_4_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_dependent_addr_4_31_0_offset 10
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_dependent_addr_4_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_dependent_addr_4_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_DEPENDENT_ADDR_4_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_dependent_addr_4_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_LO_dependent_addr_4_31_0_shift)

/*define for DEPENDENT_ADDR_4_HI word*/
/*define for dependent_addr_4_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_dependent_addr_4_63_32_offset 11
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_dependent_addr_4_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_dependent_addr_4_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_DEPENDENT_ADDR_4_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_dependent_addr_4_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_DEPENDENT_ADDR_4_HI_dependent_addr_4_63_32_shift)

/*define for RESERVED_DW12 word*/
/*define for reserved_dw12 field*/
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_reserved_dw12_offset 12
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_reserved_dw12_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_reserved_dw12_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_RESERVED_DW12(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_reserved_dw12_mask) << SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW12_reserved_dw12_shift)

/*define for RESERVED_DW13 word*/
/*define for reserved_dw13 field*/
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_reserved_dw13_offset 13
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_reserved_dw13_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_reserved_dw13_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_RESERVED_DW13(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_reserved_dw13_mask) << SDMA_AQL_PKT_BARRIER_OR_RESERVED_DW13_reserved_dw13_shift)

/*define for COMPLETION_SIGNAL_LO word*/
/*define for completion_signal_31_0 field*/
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_completion_signal_31_0_offset 14
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_completion_signal_31_0_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_completion_signal_31_0_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_COMPLETION_SIGNAL_31_0(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_completion_signal_31_0_mask) << SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_LO_completion_signal_31_0_shift)

/*define for COMPLETION_SIGNAL_HI word*/
/*define for completion_signal_63_32 field*/
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_completion_signal_63_32_offset 15
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_completion_signal_63_32_mask   0xFFFFFFFF
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_completion_signal_63_32_shift  0
#define SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_COMPLETION_SIGNAL_63_32(x) (((x) & SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_completion_signal_63_32_mask) << SDMA_AQL_PKT_BARRIER_OR_COMPLETION_SIGNAL_HI_completion_signal_63_32_shift)

/*
** Definitions for SDMA_PKT_VM_INVALIDATION packet
*/

/*define for HEADER word*/
/*define for op field*/
#define SDMA_PKT_VM_INVALIDATION_HEADER_op_offset 0
#define SDMA_PKT_VM_INVALIDATION_HEADER_op_mask   0x000000FF
#define SDMA_PKT_VM_INVALIDATION_HEADER_op_shift  0
#define SDMA_PKT_VM_INVALIDATION_HEADER_OP(x) ((x & SDMA_PKT_VM_INVALIDATION_HEADER_op_mask) << SDMA_PKT_VM_INVALIDATION_HEADER_op_shift)

/*define for sub_op field*/
#define SDMA_PKT_VM_INVALIDATION_HEADER_sub_op_offset 0
#define SDMA_PKT_VM_INVALIDATION_HEADER_sub_op_mask   0x000000FF
#define SDMA_PKT_VM_INVALIDATION_HEADER_sub_op_shift  8
#define SDMA_PKT_VM_INVALIDATION_HEADER_SUB_OP(x) ((x & SDMA_PKT_VM_INVALIDATION_HEADER_sub_op_mask) << SDMA_PKT_VM_INVALIDATION_HEADER_sub_op_shift)

/*define for xcc0_eng_id field*/
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc0_eng_id_offset 0
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc0_eng_id_mask   0x0000001F
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc0_eng_id_shift  16
#define SDMA_PKT_VM_INVALIDATION_HEADER_XCC0_ENG_ID(x) ((x & SDMA_PKT_VM_INVALIDATION_HEADER_xcc0_eng_id_mask) << SDMA_PKT_VM_INVALIDATION_HEADER_xcc0_eng_id_shift)

/*define for xcc1_eng_id field*/
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc1_eng_id_offset 0
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc1_eng_id_mask   0x0000001F
#define SDMA_PKT_VM_INVALIDATION_HEADER_xcc1_eng_id_shift  21
#define SDMA_PKT_VM_INVALIDATION_HEADER_XCC1_ENG_ID(x) ((x & SDMA_PKT_VM_INVALIDATION_HEADER_xcc1_eng_id_mask) << SDMA_PKT_VM_INVALIDATION_HEADER_xcc1_eng_id_shift)

/*define for mmhub_eng_id field*/
#define SDMA_PKT_VM_INVALIDATION_HEADER_mmhub_eng_id_offset 0
#define SDMA_PKT_VM_INVALIDATION_HEADER_mmhub_eng_id_mask   0x0000001F
#define SDMA_PKT_VM_INVALIDATION_HEADER_mmhub_eng_id_shift  26
#define SDMA_PKT_VM_INVALIDATION_HEADER_MMHUB_ENG_ID(x) ((x & SDMA_PKT_VM_INVALIDATION_HEADER_mmhub_eng_id_mask) << SDMA_PKT_VM_INVALIDATION_HEADER_mmhub_eng_id_shift)

/*define for INVALIDATEREQ word*/
/*define for invalidatereq field*/
#define SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_invalidatereq_offset 1
#define SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_invalidatereq_mask   0xFFFFFFFF
#define SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_invalidatereq_shift  0
#define SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_INVALIDATEREQ(x) ((x & SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_invalidatereq_mask) << SDMA_PKT_VM_INVALIDATION_INVALIDATEREQ_invalidatereq_shift)

/*define for ADDRESSRANGELO word*/
/*define for addressrangelo field*/
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_addressrangelo_offset 2
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_addressrangelo_mask   0xFFFFFFFF
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_addressrangelo_shift  0
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_ADDRESSRANGELO(x) ((x & SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_addressrangelo_mask) << SDMA_PKT_VM_INVALIDATION_ADDRESSRANGELO_addressrangelo_shift)

/*define for ADDRESSRANGEHI word*/
/*define for invalidateack field*/
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_invalidateack_offset 3
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_invalidateack_mask   0x0000FFFF
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_invalidateack_shift  0
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_INVALIDATEACK(x) ((x & SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_invalidateack_mask) << SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_invalidateack_shift)

/*define for addressrangehi field*/
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_addressrangehi_offset 3
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_addressrangehi_mask   0x0000001F
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_addressrangehi_shift  16
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_ADDRESSRANGEHI(x) ((x & SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_addressrangehi_mask) << SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_addressrangehi_shift)

/*define for reserved field*/
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_reserved_offset 3
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_reserved_mask   0x000001FF
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_reserved_shift  23
#define SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_RESERVED(x) ((x & SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_reserved_mask) << SDMA_PKT_VM_INVALIDATION_ADDRESSRANGEHI_reserved_shift)

#endif /* __SDMA_PKT_OPEN_H_ */

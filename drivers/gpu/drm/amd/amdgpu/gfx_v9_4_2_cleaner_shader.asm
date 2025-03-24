/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

// This shader is to clean LDS, SGPRs and VGPRs. It is  first 64 Dwords or 256 bytes of 192 Dwords cleaner shader.
//To turn this shader program on for complitaion change this to main and lower shader main to main_1

// MI200 : Clear SGPRs, VGPRs and LDS
//   Uses two kernels launched separately:
//   1. Clean VGPRs, LDS, and lower SGPRs
//        Launches one workgroup per CU, each workgroup with 4x wave64 per SIMD in the CU
//        Waves are "wave64" and have 128 VGPRs each, which uses all 512 VGPRs per SIMD
//        Waves in the workgroup share the 64KB of LDS
//        Each wave clears SGPRs 0 - 95. Because there are 4 waves/SIMD, this is physical SGPRs 0-383
//        Each wave clears 128 VGPRs, so all 512 in the SIMD
//        The first wave of the workgroup clears its 64KB of LDS
//        The shader starts with "S_BARRIER" to ensure SPI has launched all waves of the workgroup
//          before any wave in the workgroup could end.  Without this, it is possible not all SGPRs get cleared.
//    2. Clean remaining SGPRs
//        Launches a workgroup with 24 waves per workgroup, yielding 6 waves per SIMD in each CU
//        Waves are allocating 96 SGPRs
//          CP sets up SPI_RESOURCE_RESERVE_* registers to prevent these waves from allocating SGPRs 0-223.
//          As such, these 6 waves per SIMD are allocated physical SGPRs 224-799
//        Barriers do not work for >16 waves per workgroup, so we cannot start with S_BARRIER
//          Instead, the shader starts with an S_SETHALT 1. Once all waves are launched CP will send unhalt command
//        The shader then clears all SGPRs allocated to it, cleaning out physical SGPRs 224-799

shader main
  asic(MI200)
  type(CS)
  wave_size(64)
// Note: original source code from SQ team

//   (theorhetical fastest = ~512clks vgpr + 1536 lds + ~128 sgpr  = 2176 clks)

  s_cmp_eq_u32 s0, 1                                // Bit0 is set, sgpr0 is set then clear VGPRS and LDS as FW set COMPUTE_USER_DATA_3
  s_cbranch_scc0  label_0023                        // Clean VGPRs and LDS if sgpr0 of wave is set, scc = (s3 == 1)
  S_BARRIER

  s_movk_i32    m0, 0x0000
  s_mov_b32     s2, 0x00000078  // Loop 128/8=16 times  (loop unrolled for performance)
  //
  // CLEAR VGPRs
  //
  s_set_gpr_idx_on  s2, 0x8    // enable Dest VGPR indexing
label_0005:
  v_mov_b32     v0, 0
  v_mov_b32     v1, 0
  v_mov_b32     v2, 0
  v_mov_b32     v3, 0
  v_mov_b32     v4, 0
  v_mov_b32     v5, 0
  v_mov_b32     v6, 0
  v_mov_b32     v7, 0
  s_sub_u32     s2, s2, 8
  s_set_gpr_idx_idx  s2
  s_cbranch_scc0  label_0005
  s_set_gpr_idx_off

  //
  //

  s_mov_b32     s2, 0x80000000                      // Bit31 is first_wave
  s_and_b32     s2, s2, s1                          // sgpr0 has tg_size (first_wave) term as in ucode only COMPUTE_PGM_RSRC2.tg_size_en is set
  s_cbranch_scc0  label_clean_sgpr_1                // Clean LDS if its first wave of ThreadGroup/WorkGroup
  // CLEAR LDS
  //
  s_mov_b32 exec_lo, 0xffffffff
  s_mov_b32 exec_hi, 0xffffffff
  v_mbcnt_lo_u32_b32  v1, exec_hi, 0          // Set V1 to thread-ID (0..63)
  v_mbcnt_hi_u32_b32  v1, exec_lo, v1         // Set V1 to thread-ID (0..63)
  v_mul_u32_u24  v1, 0x00000008, v1           // * 8, so each thread is a double-dword address (8byte)
  s_mov_b32     s2, 0x00000003f               // 64 loop iterations
  s_mov_b32     m0, 0xffffffff
  // Clear all of LDS space
  // Each FirstWave of WorkGroup clears 64kbyte block

label_001F:
  ds_write2_b64  v1, v[2:3], v[2:3] offset1:32
  ds_write2_b64  v1, v[4:5], v[4:5] offset0:64 offset1:96
  v_add_co_u32     v1, vcc, 0x00000400, v1
  s_sub_u32     s2, s2, 1
  s_cbranch_scc0  label_001F
  //
  // CLEAR SGPRs
  //
label_clean_sgpr_1:
  s_mov_b32     m0, 0x0000005c   // Loop 96/4=24 times  (loop unrolled for performance)
  s_nop 0
label_sgpr_loop:
  s_movreld_b32     s0, 0
  s_movreld_b32     s1, 0
  s_movreld_b32     s2, 0
  s_movreld_b32     s3, 0
  s_sub_u32         m0, m0, 4
  s_cbranch_scc0  label_sgpr_loop

  //clear vcc, flat scratch
  s_mov_b32 flat_scratch_lo, 0   //clear  flat scratch lo SGPR
  s_mov_b32 flat_scratch_hi, 0   //clear  flat scratch hi SGPR
  s_mov_b64 vcc, 0               //clear vcc
  s_mov_b64 ttmp0, 0             //Clear ttmp0 and ttmp1
  s_mov_b64 ttmp2, 0             //Clear ttmp2 and ttmp3
  s_mov_b64 ttmp4, 0             //Clear ttmp4 and ttmp5
  s_mov_b64 ttmp6, 0             //Clear ttmp6 and ttmp7
  s_mov_b64 ttmp8, 0             //Clear ttmp8 and ttmp9
  s_mov_b64 ttmp10, 0            //Clear ttmp10 and ttmp11
  s_mov_b64 ttmp12, 0            //Clear ttmp12 and ttmp13
  s_mov_b64 ttmp14, 0            //Clear ttmp14 and ttmp15
s_endpgm

label_0023:

  s_sethalt 1

  s_mov_b32     m0, 0x0000005c   // Loop 96/4=24 times  (loop unrolled for performance)
  s_nop 0
label_sgpr_loop1:

  s_movreld_b32     s0, 0
  s_movreld_b32     s1, 0
  s_movreld_b32     s2, 0
  s_movreld_b32     s3, 0
  s_sub_u32         m0, m0, 4
  s_cbranch_scc0  label_sgpr_loop1

  //clear vcc, flat scratch
  s_mov_b32 flat_scratch_lo, 0   //clear  flat scratch lo SGPR
  s_mov_b32 flat_scratch_hi, 0   //clear  flat scratch hi SGPR
  s_mov_b64 vcc, 0xee            //clear vcc

s_endpgm
end


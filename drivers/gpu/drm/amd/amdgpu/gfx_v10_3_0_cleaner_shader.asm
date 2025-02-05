/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

// GFX10.3 : Clear SGPRs, VGPRs and LDS
//   Launch 32 waves per CU (16 per SIMD) as a workgroup (threadgroup) to fill every wave slot
//   Waves are "wave32" and have 64 VGPRs each, which uses all 1024 VGPRs per SIMD
//   Waves are launched in "CU" mode, and the workgroup shares 64KB of LDS (half of the WGP's LDS)
//      It takes 2 workgroups to use all of LDS: one on each CU of the WGP
//   Each wave clears SGPRs 0 - 107
//   Each wave clears VGPRs 0 - 63
//   The first wave of the workgroup clears its 64KB of LDS
//   The shader starts with "S_BARRIER" to ensure SPI has launched all waves of the workgroup
//       before any wave in the workgroup could end.  Without this, it is possible not all SGPRs get cleared.


shader main
  asic(GFX10)
  type(CS)
  wave_size(32)
// Note: original source code from SQ team

//
// Create 32 waves in a threadgroup (CS waves)
// Each allocates 64 VGPRs
// The workgroup allocates all of LDS (64kbytes)
//
// Takes about 2500 clocks to run.
//   (theorhetical fastest = 1024clks vgpr + 640lds = 1660 clks)
//
  S_BARRIER
  s_mov_b32     s2, 0x00000038  // Loop 64/8=8 times  (loop unrolled for performance)
  s_mov_b32     m0, 0
  //
  // CLEAR VGPRs
  //
label_0005:
  v_movreld_b32     v0, 0
  v_movreld_b32     v1, 0
  v_movreld_b32     v2, 0
  v_movreld_b32     v3, 0
  v_movreld_b32     v4, 0
  v_movreld_b32     v5, 0
  v_movreld_b32     v6, 0
  v_movreld_b32     v7, 0
  s_mov_b32         m0, s2
  s_sub_u32     s2, s2, 8
  s_cbranch_scc0  label_0005
  //
  s_mov_b32     s2, 0x80000000                     // Bit31 is first_wave
  s_and_b32     s2, s2, s0                                  // sgpr0 has tg_size (first_wave) term as in ucode only COMPUTE_PGM_RSRC2.tg_size_en is set
  s_cbranch_scc0  label_0023                         // Clean LDS if its first wave of ThreadGroup/WorkGroup
  // CLEAR LDS
  //
  s_mov_b32 exec_lo, 0xffffffff
  s_mov_b32 exec_hi, 0xffffffff
  v_mbcnt_lo_u32_b32  v1, exec_hi, 0          // Set V1 to thread-ID (0..63)
  v_mbcnt_hi_u32_b32  v1, exec_lo, v1        // Set V1 to thread-ID (0..63)
  v_mul_u32_u24  v1, 0x00000008, v1          // * 8, so each thread is a double-dword address (8byte)
  s_mov_b32     s2, 0x00000003f                    // 64 loop iterations
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
label_0023:
  s_mov_b32     m0, 0x00000068  // Loop 108/4=27 times  (loop unrolled for performance)
label_sgpr_loop:
  s_movreld_b32     s0, 0
  s_movreld_b32     s1, 0
  s_movreld_b32     s2, 0
  s_movreld_b32     s3, 0
  s_sub_u32         m0, m0, 4
  s_cbranch_scc0  label_sgpr_loop

  //clear vcc
  s_mov_b32 flat_scratch_lo, 0   //clear  flat scratch lo SGPR
  s_mov_b32 flat_scratch_hi, 0   //clear  flat scratch hi SGPR
  s_mov_b64 vcc, 0          //clear vcc
  s_mov_b64 ttmp0, 0        //Clear ttmp0 and ttmp1
  s_mov_b64 ttmp2, 0        //Clear ttmp2 and ttmp3
  s_mov_b64 ttmp4, 0        //Clear ttmp4 and ttmp5
  s_mov_b64 ttmp6, 0        //Clear ttmp6 and ttmp7
  s_mov_b64 ttmp8, 0        //Clear ttmp8 and ttmp9
  s_mov_b64 ttmp10, 0       //Clear ttmp10 and ttmp11
  s_mov_b64 ttmp12, 0       //Clear ttmp12 and ttmp13
  s_mov_b64 ttmp14, 0       //Clear ttmp14 and ttmp15

 s_endpgm

end



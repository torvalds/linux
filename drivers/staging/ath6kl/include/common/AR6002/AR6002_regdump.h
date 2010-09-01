//------------------------------------------------------------------------------
// Copyright (c) 2006-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef __AR6002_REGDUMP_H__
#define __AR6002_REGDUMP_H__

#if !defined(__ASSEMBLER__)
/*
 * XTensa CPU state
 * This must match the state saved by the target exception handler.
 */
struct XTensa_exception_frame_s {
    A_UINT32 xt_pc;
    A_UINT32 xt_ps;
    A_UINT32 xt_sar;
    A_UINT32 xt_vpri;
    A_UINT32 xt_a2;
    A_UINT32 xt_a3;
    A_UINT32 xt_a4;
    A_UINT32 xt_a5;
    A_UINT32 xt_exccause;
    A_UINT32 xt_lcount;
    A_UINT32 xt_lbeg;
    A_UINT32 xt_lend;

    A_UINT32 epc1, epc2, epc3, epc4;

    /* Extra info to simplify post-mortem stack walkback */
#define AR6002_REGDUMP_FRAMES 10
    struct {
        A_UINT32 a0;  /* pc */
        A_UINT32 a1;  /* sp */
        A_UINT32 a2;
        A_UINT32 a3;
    } wb[AR6002_REGDUMP_FRAMES];
};
typedef struct XTensa_exception_frame_s CPU_exception_frame_t; 
#define RD_SIZE sizeof(CPU_exception_frame_t)

#endif
#endif /* __AR6002_REGDUMP_H__ */

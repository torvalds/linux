//------------------------------------------------------------------------------
// <copyright file="regdump.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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

#ifndef __REGDUMP_H__
#define __REGDUMP_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

#if defined(AR6001)
#include "AR6001/AR6001_regdump.h"
#endif
#if defined(AR6002)
#include "AR6002/AR6002_regdump.h"
#endif

#if !defined(__ASSEMBLER__)
/*
 * Target CPU state at the time of failure is reflected
 * in a register dump, which the Host can fetch through
 * the diagnostic window.
 */
PREPACK struct register_dump_s {
    u32 target_id;               /* Target ID */
    u32 assline;                 /* Line number (if assertion failure) */
    u32 pc;                      /* Program Counter at time of exception */
    u32 badvaddr;                /* Virtual address causing exception */
    CPU_exception_frame_t exc_frame;  /* CPU-specific exception info */

    /* Could copy top of stack here, too.... */
} POSTPACK;
#endif /* __ASSEMBLER__ */

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __REGDUMP_H__ */

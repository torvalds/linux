//------------------------------------------------------------------------------
// <copyright file="ar6000_diag.h" company="Atheros">
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

#ifndef AR6000_DIAG_H_
#define AR6000_DIAG_H_


A_STATUS
ar6000_ReadRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);

A_STATUS
ar6000_WriteRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);

A_STATUS
ar6000_ReadDataDiag(HIF_DEVICE *hifDevice, A_UINT32 address,
                    A_UCHAR *data, A_UINT32 length);

A_STATUS
ar6000_WriteDataDiag(HIF_DEVICE *hifDevice, A_UINT32 address,
                     A_UCHAR *data, A_UINT32 length);

A_STATUS
ar6k_ReadTargetRegister(HIF_DEVICE *hifDevice, int regsel, A_UINT32 *regval);

void
ar6k_FetchTargetRegs(HIF_DEVICE *hifDevice, A_UINT32 *targregs);

#endif /*AR6000_DIAG_H_*/

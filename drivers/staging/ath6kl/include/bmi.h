//------------------------------------------------------------------------------
// <copyright file="bmi.h" company="Atheros">
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
// BMI declarations and prototypes
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _BMI_H_
#define _BMI_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Header files */
#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "hif.h"
#include "a_osapi.h"
#include "bmi_msg.h"

void
BMIInit(void);

void
BMICleanup(void);

int
BMIDone(HIF_DEVICE *device);

int
BMIGetTargetInfo(HIF_DEVICE *device, struct bmi_target_info *targ_info);

int
BMIReadMemory(HIF_DEVICE *device,
              A_UINT32 address,
              A_UCHAR *buffer,
              A_UINT32 length);

int
BMIWriteMemory(HIF_DEVICE *device,
               A_UINT32 address,
               A_UCHAR *buffer,
               A_UINT32 length);

int
BMIExecute(HIF_DEVICE *device,
           A_UINT32 address,
           A_UINT32 *param);

int
BMISetAppStart(HIF_DEVICE *device,
               A_UINT32 address);

int
BMIReadSOCRegister(HIF_DEVICE *device,
                   A_UINT32 address,
                   A_UINT32 *param);

int
BMIWriteSOCRegister(HIF_DEVICE *device,
                    A_UINT32 address,
                    A_UINT32 param);

int
BMIrompatchInstall(HIF_DEVICE *device,
                   A_UINT32 ROM_addr,
                   A_UINT32 RAM_addr,
                   A_UINT32 nbytes,
                   A_UINT32 do_activate,
                   A_UINT32 *patch_id);

int
BMIrompatchUninstall(HIF_DEVICE *device,
                     A_UINT32 rompatch_id);

int
BMIrompatchActivate(HIF_DEVICE *device,
                    A_UINT32 rompatch_count,
                    A_UINT32 *rompatch_list);

int
BMIrompatchDeactivate(HIF_DEVICE *device,
                      A_UINT32 rompatch_count,
                      A_UINT32 *rompatch_list);

int
BMILZStreamStart(HIF_DEVICE *device,
                 A_UINT32 address);

int
BMILZData(HIF_DEVICE *device,
          A_UCHAR *buffer,
          A_UINT32 length);

int
BMIFastDownload(HIF_DEVICE *device,
                A_UINT32 address,
                A_UCHAR *buffer,
                A_UINT32 length);

int
BMIRawWrite(HIF_DEVICE *device,
            A_UCHAR *buffer,
            A_UINT32 length);

int
BMIRawRead(HIF_DEVICE *device, 
           A_UCHAR *buffer, 
           A_UINT32 length, 
           A_BOOL want_timeout);

#ifdef __cplusplus
}
#endif

#endif /* _BMI_H_ */

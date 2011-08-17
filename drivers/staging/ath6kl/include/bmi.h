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
#include "hif.h"
#include "a_osapi.h"
#include "bmi_msg.h"

void
BMIInit(void);

void
BMICleanup(void);

int
BMIDone(struct hif_device *device);

int
BMIGetTargetInfo(struct hif_device *device, struct bmi_target_info *targ_info);

int
BMIReadMemory(struct hif_device *device,
              u32 address,
              u8 *buffer,
              u32 length);

int
BMIWriteMemory(struct hif_device *device,
               u32 address,
               u8 *buffer,
               u32 length);

int
BMIExecute(struct hif_device *device,
           u32 address,
           u32 *param);

int
BMISetAppStart(struct hif_device *device,
               u32 address);

int
BMIReadSOCRegister(struct hif_device *device,
                   u32 address,
                   u32 *param);

int
BMIWriteSOCRegister(struct hif_device *device,
                    u32 address,
                    u32 param);

int
BMIrompatchInstall(struct hif_device *device,
                   u32 ROM_addr,
                   u32 RAM_addr,
                   u32 nbytes,
                   u32 do_activate,
                   u32 *patch_id);

int
BMIrompatchUninstall(struct hif_device *device,
                     u32 rompatch_id);

int
BMIrompatchActivate(struct hif_device *device,
                    u32 rompatch_count,
                    u32 *rompatch_list);

int
BMIrompatchDeactivate(struct hif_device *device,
                      u32 rompatch_count,
                      u32 *rompatch_list);

int
BMILZStreamStart(struct hif_device *device,
                 u32 address);

int
BMILZData(struct hif_device *device,
          u8 *buffer,
          u32 length);

int
BMIFastDownload(struct hif_device *device,
                u32 address,
                u8 *buffer,
                u32 length);

int
BMIRawWrite(struct hif_device *device,
            u8 *buffer,
            u32 length);

int
BMIRawRead(struct hif_device *device, 
           u8 *buffer, 
           u32 length,
           bool want_timeout);

#ifdef __cplusplus
}
#endif

#endif /* _BMI_H_ */

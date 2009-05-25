/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#ifndef __IWM_FW_H__
#define __IWM_FW_H__

/**
 * struct iwm_fw_hdr_rec - An iwm firmware image is a
 * concatenation of various records. Each of them is
 * defined by an ID (aka op code), a length, and the
 * actual data.
 * @op_code: The record ID, see IWM_HDR_REC_OP_*
 *
 * @len: The record payload length
 *
 * @buf: The record payload
 */
struct iwm_fw_hdr_rec {
	u16 op_code;
	u16 len;
	u8 buf[0];
};

/* Header's definitions */
#define IWM_HDR_LEN                          (512)
#define IWM_HDR_BARKER_LEN                   (16)

/* Header's opcodes */
#define IWM_HDR_REC_OP_INVALID             (0x00)
#define IWM_HDR_REC_OP_BUILD_DATE          (0x01)
#define IWM_HDR_REC_OP_BUILD_TAG           (0x02)
#define IWM_HDR_REC_OP_SW_VER              (0x03)
#define IWM_HDR_REC_OP_HW_SKU              (0x04)
#define IWM_HDR_REC_OP_BUILD_OPT           (0x05)
#define IWM_HDR_REC_OP_MEM_DESC            (0x06)
#define IWM_HDR_REC_USERDEFS               (0x07)

/* Header's records length (in bytes) */
#define IWM_HDR_REC_LEN_BUILD_DATE           (4)
#define IWM_HDR_REC_LEN_BUILD_TAG            (64)
#define IWM_HDR_REC_LEN_SW_VER               (4)
#define IWM_HDR_REC_LEN_HW_SKU               (4)
#define IWM_HDR_REC_LEN_BUILD_OPT            (4)
#define IWM_HDR_REC_LEN_MEM_DESC             (12)
#define IWM_HDR_REC_LEN_USERDEF              (64)

#define IWM_BUILD_YEAR(date) ((date >> 16) & 0xffff)
#define IWM_BUILD_MONTH(date) ((date >> 8) & 0xff)
#define IWM_BUILD_DAY(date) (date & 0xff)

struct iwm_fw_img_desc {
	u32 offset;
	u32 address;
	u32 length;
};

struct iwm_fw_img_ver {
	u8 minor;
	u8 major;
	u16 reserved;
};

int iwm_load_fw(struct iwm_priv *iwm);

#endif

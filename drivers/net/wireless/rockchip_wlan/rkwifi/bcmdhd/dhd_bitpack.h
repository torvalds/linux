/*
 * Bit packing and Base64 utils for EWP
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef __BITPACK_H_
#define __BITPACK_H_

#define BYTE_SIZE(a) ((a + 7)/8)

extern int32 dhd_bit_pack(char *buf, int32 buf_len, int bit_offset, uint32 data, int32 bit_length);
extern int32 dhd_base64_encode(char* in_buf, int32 in_buf_len, char* out_buf, int32 out_buf_len);
#endif /* __BITPACK_H */

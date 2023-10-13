/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2011-2017 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SYNPDU_ERROR_H_
#define SYNPDU_ERROR_H_

/*
 * Common error definitions.  Be sure to update pdu_error_code when changing
 * anything in this list.
 */

#define CRYPTO_OK                      (0)
#define CRYPTO_FAILED                  (-1)
#define CRYPTO_INPROGRESS              (-2)
#define CRYPTO_INVALID_HANDLE          (-3)
#define CRYPTO_INVALID_CONTEXT         (-4)
#define CRYPTO_INVALID_SIZE            (-5)
#define CRYPTO_NOT_INITIALIZED         (-6)
#define CRYPTO_NO_MEM                  (-7)
#define CRYPTO_INVALID_ALG             (-8)
#define CRYPTO_INVALID_KEY_SIZE        (-9)
#define CRYPTO_INVALID_ARGUMENT        (-10)
#define CRYPTO_MODULE_DISABLED         (-11)
#define CRYPTO_NOT_IMPLEMENTED         (-12)
#define CRYPTO_INVALID_BLOCK_ALIGNMENT (-13)
#define CRYPTO_INVALID_MODE            (-14)
#define CRYPTO_INVALID_KEY             (-15)
#define CRYPTO_AUTHENTICATION_FAILED   (-16)
#define CRYPTO_INVALID_IV_SIZE         (-17)
#define CRYPTO_MEMORY_ERROR            (-18)
#define CRYPTO_LAST_ERROR              (-19)
#define CRYPTO_HALTED                  (-20)
#define CRYPTO_TIMEOUT                 (-21)
#define CRYPTO_SRM_FAILED              (-22)
#define CRYPTO_COMMON_ERROR_MAX        (-100)
#define CRYPTO_INVALID_ICV_KEY_SIZE    (-100)
#define CRYPTO_INVALID_PARAMETER_SIZE  (-101)
#define CRYPTO_SEQUENCE_OVERFLOW       (-102)
#define CRYPTO_DISABLED                (-103)
#define CRYPTO_INVALID_VERSION         (-104)
#define CRYPTO_FATAL                   (-105)
#define CRYPTO_INVALID_PAD             (-106)
#define CRYPTO_FIFO_FULL               (-107)
#define CRYPTO_INVALID_SEQUENCE        (-108)
#define CRYPTO_INVALID_FIRMWARE        (-109)
#define CRYPTO_NOT_FOUND               (-110)
#define CRYPTO_CMD_FIFO_INACTIVE       (-111)
#define CRYPTO_INVALID_PROTOCOL        (-112)
#define CRYPTO_REPLAY                  (-113)
#define CRYPTO_NOT_INSTANTIATED        (-114)
#define CRYPTO_RESEED_REQUIRED         (-115)

#endif

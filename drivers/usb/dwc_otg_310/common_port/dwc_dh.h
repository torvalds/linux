/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_dh.h $
 * $Revision: #4 $
 * $Date: 2010/09/28 $
 * $Change: 1596182 $
 *
 * Synopsys Portability Library Software and documentation
 * (hereinafter, "Software") is an Unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing
 * between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product
 * under any End User Software License Agreement or Agreement for
 * Licensed Product with Synopsys or any supplement thereto. You are
 * permitted to use and redistribute this Software in source and binary
 * forms, with or without modification, provided that redistributions
 * of source code must retain this notice. You may not view, use,
 * disclose, copy or distribute this file or any information contained
 * herein except pursuant to this license grant from Synopsys. If you
 * do not agree with this notice, including the disclaimer below, then
 * you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 * BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL
 * SYNOPSYS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================= */
#ifndef _DWC_DH_H_
#define _DWC_DH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dwc_os.h"

/** @file
 *
 * This file defines the common functions on device and host for performing
 * numeric association as defined in the WUSB spec.  They are only to be
 * used internally by the DWC UWB modules. */

extern int dwc_dh_sha256(uint8_t *message, uint32_t len, uint8_t *out);
extern int dwc_dh_hmac_sha256(uint8_t *message, uint32_t messagelen,
			      uint8_t *key, uint32_t keylen,
			      uint8_t *out);
extern int dwc_dh_modpow(void *mem_ctx, void *num, uint32_t num_len,
			 void *exp, uint32_t exp_len,
			 void *mod, uint32_t mod_len,
			 void *out);

/** Computes PKD or PKH, and SHA-256(PKd || Nd)
 *
 * PK = g^exp mod p.
 *
 * Input:
 * Nd = Number of digits on the device.
 *
 * Output:
 * exp = A 32-byte buffer to be filled with a randomly generated number.
 *       used as either A or B.
 * pk = A 384-byte buffer to be filled with the PKH or PKD.
 * hash = A 32-byte buffer to be filled with SHA-256(PK || ND).
 */
extern int dwc_dh_pk(void *mem_ctx, uint8_t nd, uint8_t *exp, uint8_t *pkd, uint8_t *hash);

/** Computes the DHKEY, and VD.
 *
 * If called from host, then it will comput DHKEY=PKD^exp % p.
 * If called from device, then it will comput DHKEY=PKH^exp % p.
 *
 * Input:
 * pkd = The PKD value.
 * pkh = The PKH value.
 * exp = The A value (if device) or B value (if host) generated in dwc_wudev_dh_pk.
 * is_host = Set to non zero if a WUSB host is calling this function.
 *
 * Output:

 * dd = A pointer to an buffer to be set to the displayed digits string to be shown
 *      to the user.  This buffer should be at 5 bytes long to hold 4 digits plus a
 *      null termination character.  This buffer can be used directly for display.
 * ck = A 16-byte buffer to be filled with the CK.
 * kdk = A 32-byte buffer to be filled with the KDK.
 */
extern int dwc_dh_derive_keys(void *mem_ctx, uint8_t nd, uint8_t *pkh, uint8_t *pkd,
			      uint8_t *exp, int is_host,
			      char *dd, uint8_t *ck, uint8_t *kdk);

#ifdef DH_TEST_VECTORS
extern void dwc_run_dh_test_vectors(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _DWC_DH_H_ */

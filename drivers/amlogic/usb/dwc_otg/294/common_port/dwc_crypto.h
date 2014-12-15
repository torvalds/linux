/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_crypto.h $
 * $Revision: #3 $
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

#ifndef _DWC_CRYPTO_H_
#define _DWC_CRYPTO_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 *
 * This file contains declarations for the WUSB Cryptographic routines as
 * defined in the WUSB spec.  They are only to be used internally by the DWC UWB
 * modules.
 */

#include "dwc_os.h"

int dwc_wusb_aes_encrypt(u8 *src, u8 *key, u8 *dst);

void dwc_wusb_cmf(u8 *key, u8 *nonce,
		  char *label, u8 *bytes, int len, u8 *result);
void dwc_wusb_prf(int prf_len, u8 *key,
		  u8 *nonce, char *label, u8 *bytes, int len, u8 *result);

/**
 * The PRF-64 function described in section 6.5 of the WUSB spec.
 *
 * @param key, nonce, label, bytes, len, result  Same as for dwc_prf().
 */
static inline void dwc_wusb_prf_64(u8 *key, u8 *nonce,
				   char *label, u8 *bytes, int len, u8 *result)
{
	dwc_wusb_prf(64, key, nonce, label, bytes, len, result);
}

/**
 * The PRF-128 function described in section 6.5 of the WUSB spec.
 *
 * @param key, nonce, label, bytes, len, result  Same as for dwc_prf().
 */
static inline void dwc_wusb_prf_128(u8 *key, u8 *nonce,
				    char *label, u8 *bytes, int len, u8 *result)
{
	dwc_wusb_prf(128, key, nonce, label, bytes, len, result);
}

/**
 * The PRF-256 function described in section 6.5 of the WUSB spec.
 *
 * @param key, nonce, label, bytes, len, result  Same as for dwc_prf().
 */
static inline void dwc_wusb_prf_256(u8 *key, u8 *nonce,
				    char *label, u8 *bytes, int len, u8 *result)
{
	dwc_wusb_prf(256, key, nonce, label, bytes, len, result);
}


void dwc_wusb_fill_ccm_nonce(uint16_t haddr, uint16_t daddr, uint8_t *tkid,
			       uint8_t *nonce);
void dwc_wusb_gen_nonce(uint16_t addr,
			  uint8_t *nonce);

void dwc_wusb_gen_key(uint8_t *ccm_nonce, uint8_t *mk,
			uint8_t *hnonce, uint8_t *dnonce,
			uint8_t *kck, uint8_t *ptk);


void dwc_wusb_gen_mic(uint8_t *ccm_nonce, uint8_t
			*kck, uint8_t *data, uint8_t *mic);

#ifdef __cplusplus
}
#endif

#endif /* _DWC_CRYPTO_H_ */

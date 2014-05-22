/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_crypto.c $
 * $Revision: #5 $
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

/** @file
 * This file contains the WUSB cryptographic routines.
 */

#ifdef DWC_CRYPTOLIB

#include "dwc_crypto.h"
#include "usb.h"

#ifdef DEBUG
static inline void dump_bytes(char *name, uint8_t *bytes, int len)
{
	int i;
	DWC_PRINTF("%s: ", name);
	for (i = 0; i < len; i++) {
		DWC_PRINTF("%02x ", bytes[i]);
	}
	DWC_PRINTF("\n");
}
#else
#define dump_bytes(x...)
#endif

/* Display a block */
void show_block(const u8 *blk, const char *prefix, const char *suffix, int a)
{
#ifdef DWC_DEBUG_CRYPTO
	int i, blksize = 16;

	DWC_DEBUG("%s", prefix);

	if (suffix == NULL) {
		suffix = "\n";
		blksize = a;
	}

	for (i = 0; i < blksize; i++)
		DWC_PRINT("%02x%s", *blk++, ((i & 3) == 3) ? "  " : " ");
	DWC_PRINT(suffix);
#endif
}

/**
 * Encrypts an array of bytes using the AES encryption engine.
 * If <code>dst</code> == <code>src</code>, then the bytes will be encrypted
 * in-place.
 *
 * @return  0 on success, negative error code on error.
 */
int dwc_wusb_aes_encrypt(u8 *src, u8 *key, u8 *dst)
{
	u8 block_t[16];
	DWC_MEMSET(block_t, 0, 16);

	return DWC_AES_CBC(src, 16, key, 16, block_t, dst);
}

/**
 * The CCM-MAC-FUNCTION described in section 6.5 of the WUSB spec.
 * This function takes a data string and returns the encrypted CBC
 * Counter-mode MIC.
 *
 * @param key     The 128-bit symmetric key.
 * @param nonce   The CCM nonce.
 * @param label   The unique 14-byte ASCII text label.
 * @param bytes   The byte array to be encrypted.
 * @param len     Length of the byte array.
 * @param result  Byte array to receive the 8-byte encrypted MIC.
 */
void dwc_wusb_cmf(u8 *key, u8 *nonce,
		  char *label, u8 *bytes, int len, u8 *result)
{
	u8 block_m[16];
	u8 block_x[16];
	u8 block_t[8];
	int idx, blkNum;
	u16 la = (u16)(len + 14);

	/* Set the AES-128 key */
	/* dwc_aes_setkey(tfm, key, 16); */

	/* Fill block B0 from flags = 0x59, N, and l(m) = 0 */
	block_m[0] = 0x59;
	for (idx = 0; idx < 13; idx++)
		block_m[idx + 1] = nonce[idx];
	block_m[14] = 0;
	block_m[15] = 0;

	/* Produce the CBC IV */
	dwc_wusb_aes_encrypt(block_m, key, block_x);
	show_block(block_m, "CBC IV in: ", "\n", 0);
	show_block(block_x, "CBC IV out:", "\n", 0);

	/* Fill block B1 from l(a) = Blen + 14, and A */
	block_x[0] ^= (u8)(la >> 8);
	block_x[1] ^= (u8)la;
	for (idx = 0; idx < 14; idx++)
		block_x[idx + 2] ^= label[idx];
	show_block(block_x, "After xor: ", "b1\n", 16);

	dwc_wusb_aes_encrypt(block_x, key, block_x);
	show_block(block_x, "After AES: ", "b1\n", 16);

	idx = 0;
	blkNum = 0;

	/* Fill remaining blocks with B */
	while (len-- > 0) {
		block_x[idx] ^= *bytes++;
		if (++idx >= 16) {
			idx = 0;
			show_block(block_x, "After xor: ", "\n", blkNum);
			dwc_wusb_aes_encrypt(block_x, key, block_x);
			show_block(block_x, "After AES: ", "\n", blkNum);
			blkNum++;
		}
	}

	/* Handle partial last block */
	if (idx > 0) {
		show_block(block_x, "After xor: ", "\n", blkNum);
		dwc_wusb_aes_encrypt(block_x, key, block_x);
		show_block(block_x, "After AES: ", "\n", blkNum);
	}

	/* Save the MIC tag */
	DWC_MEMCPY(block_t, block_x, 8);
	show_block(block_t, "MIC tag  : ", NULL, 8);

	/* Fill block A0 from flags = 0x01, N, and counter = 0 */
	block_m[0] = 0x01;
	block_m[14] = 0;
	block_m[15] = 0;

	/* Encrypt the counter */
	dwc_wusb_aes_encrypt(block_m, key, block_x);
	show_block(block_x, "CTR[MIC] : ", NULL, 8);

	/* XOR with MIC tag */
	for (idx = 0; idx < 8; idx++) {
		block_t[idx] ^= block_x[idx];
	}

	/* Return result to caller */
	DWC_MEMCPY(result, block_t, 8);
	show_block(result, "CCM-MIC  : ", NULL, 8);

}

/**
 * The PRF function described in section 6.5 of the WUSB spec. This function
 * concatenates MIC values returned from dwc_cmf() to create a value of
 * the requested length.
 *
 * @param prf_len  Length of the PRF function in bits (64, 128, or 256).
 * @param key, nonce, label, bytes, len  Same as for dwc_cmf().
 * @param result   Byte array to receive the result.
 */
void dwc_wusb_prf(int prf_len, u8 *key,
		  u8 *nonce, char *label, u8 *bytes, int len, u8 *result)
{
	int i;

	nonce[0] = 0;
	for (i = 0; i < prf_len >> 6; i++, nonce[0]++) {
		dwc_wusb_cmf(key, nonce, label, bytes, len, result);
		result += 8;
	}
}

/**
 * Fills in CCM Nonce per the WUSB spec.
 *
 * @param[in] haddr Host address.
 * @param[in] daddr Device address.
 * @param[in] tkid Session Key(PTK) identifier.
 * @param[out] nonce Pointer to where the CCM Nonce output is to be written.
 */
void dwc_wusb_fill_ccm_nonce(uint16_t haddr, uint16_t daddr, uint8_t *tkid,
			     uint8_t *nonce)
{

	DWC_DEBUG("%s %x %x\n", __func__, daddr, haddr);

	DWC_MEMSET(&nonce[0], 0, 16);

	DWC_MEMCPY(&nonce[6], tkid, 3);
	nonce[9] = daddr & 0xFF;
	nonce[10] = (daddr >> 8) & 0xFF;
	nonce[11] = haddr & 0xFF;
	nonce[12] = (haddr >> 8) & 0xFF;

	dump_bytes("CCM nonce", nonce, 16);
}

/**
 * Generates a 16-byte cryptographic-grade random number for the Host/Device
 * Nonce.
 */
void dwc_wusb_gen_nonce(uint16_t addr, uint8_t *nonce)
{
	uint8_t inonce[16];
	uint32_t temp[4];

	/* Fill in the Nonce */
	DWC_MEMSET(&inonce[0], 0, sizeof(inonce));
	inonce[9] = addr & 0xFF;
	inonce[10] = (addr >> 8) & 0xFF;
	inonce[11] = inonce[9];
	inonce[12] = inonce[10];

	/* Collect "randomness samples" */
	DWC_RANDOM_BYTES((uint8_t *)temp, 16);

	dwc_wusb_prf_128((uint8_t *)temp, nonce,
			 "Random Numbers", (uint8_t *)temp, sizeof(temp),
			 nonce);
}

/**
 * Generates the Session Key (PTK) and Key Confirmation Key (KCK) per the
 * WUSB spec.
 *
 * @param[in] ccm_nonce Pointer to CCM Nonce.
 * @param[in] mk Master Key to derive the session from
 * @param[in] hnonce Pointer to Host Nonce.
 * @param[in] dnonce Pointer to Device Nonce.
 * @param[out] kck Pointer to where the KCK output is to be written.
 * @param[out] ptk Pointer to where the PTK output is to be written.
 */
void dwc_wusb_gen_key(uint8_t *ccm_nonce, uint8_t *mk, uint8_t *hnonce,
		      uint8_t *dnonce, uint8_t *kck, uint8_t *ptk)
{
	uint8_t idata[32];
	uint8_t odata[32];

	dump_bytes("ck", mk, 16);
	dump_bytes("hnonce", hnonce, 16);
	dump_bytes("dnonce", dnonce, 16);

	/* The data is the HNonce and DNonce concatenated */
	DWC_MEMCPY(&idata[0], hnonce, 16);
	DWC_MEMCPY(&idata[16], dnonce, 16);

	dwc_wusb_prf_256(mk, ccm_nonce, "Pair-wise keys", idata, 32, odata);

	/* Low 16 bytes of the result is the KCK, high 16 is the PTK */
	DWC_MEMCPY(kck, &odata[0], 16);
	DWC_MEMCPY(ptk, &odata[16], 16);

	dump_bytes("kck", kck, 16);
	dump_bytes("ptk", ptk, 16);
}

/**
 * Generates the Message Integrity Code over the Handshake data per the
 * WUSB spec.
 *
 * @param ccm_nonce Pointer to CCM Nonce.
 * @param kck   Pointer to Key Confirmation Key.
 * @param data  Pointer to Handshake data to be checked.
 * @param mic   Pointer to where the MIC output is to be written.
 */
void dwc_wusb_gen_mic(uint8_t *ccm_nonce, uint8_t *kck,
		      uint8_t *data, uint8_t *mic)
{

	dwc_wusb_prf_64(kck, ccm_nonce, "out-of-bandMIC",
			data, WUSB_HANDSHAKE_LEN_FOR_MIC, mic);
}

#endif	/* DWC_CRYPTOLIB */

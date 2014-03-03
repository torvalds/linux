/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_dh.c $
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
#ifdef DWC_CRYPTOLIB

#ifndef CONFIG_MACH_IPMATE

#include "dwc_dh.h"
#include "dwc_modpow.h"

#ifdef DEBUG
/* This function prints out a buffer in the format described in the Association
 * Model specification. */
static void dh_dump(char *str, void *_num, int len)
{
	uint8_t *num = _num;
	int i;
	DWC_PRINTF("%s\n", str);
	for (i = 0; i < len; i ++) {
		DWC_PRINTF("%02x", num[i]);
		if (((i + 1) % 2) == 0) DWC_PRINTF(" ");
		if (((i + 1) % 26) == 0) DWC_PRINTF("\n");
	}

	DWC_PRINTF("\n");
}
#else
#define dh_dump(_x...) do {; } while(0)
#endif

/* Constant g value */
static __u32 dh_g[] = {
	0x02000000,
};

/* Constant p value */
static __u32 dh_p[] = {
	0xFFFFFFFF, 0xFFFFFFFF, 0xA2DA0FC9, 0x34C26821, 0x8B62C6C4, 0xD11CDC80, 0x084E0229, 0x74CC678A,
	0xA6BE0B02, 0x229B133B, 0x79084A51, 0xDD04348E, 0xB31995EF, 0x1B433ACD, 0x6D0A2B30, 0x37145FF2,
	0x6D35E14F, 0x45C2516D, 0x76B585E4, 0xC67E5E62, 0xE9424CF4, 0x6BED37A6, 0xB65CFF0B, 0xEDB706F4,
	0xFB6B38EE, 0xA59F895A, 0x11249FAE, 0xE61F4B7C, 0x51662849, 0x3D5BE4EC, 0xB87C00C2, 0x05BF63A1,
	0x3648DA98, 0x9AD3551C, 0xA83F1669, 0x5FCF24FD, 0x235D6583, 0x96ADA3DC, 0x56F3621C, 0xBB528520,
	0x0729D59E, 0x6D969670, 0x4E350C67, 0x0498BC4A, 0x086C74F1, 0x7C2118CA, 0x465E9032, 0x3BCE362E,
	0x2C779EE3, 0x03860E18, 0xA283279B, 0x8FA207EC, 0xF05DC5B5, 0xC9524C6F, 0xF6CB2BDE, 0x18175895,
	0x7C499539, 0xE56A95EA, 0x1826D215, 0x1005FA98, 0x5A8E7215, 0x2DC4AA8A, 0x0D1733AD, 0x337A5004,
	0xAB2155A8, 0x64BA1CDF, 0x0485FBEC, 0x0AEFDB58, 0x5771EA8A, 0x7D0C065D, 0x850F97B3, 0xC7E4E1A6,
	0x8CAEF5AB, 0xD73309DB, 0xE0948C1E, 0x9D61254A, 0x26D2E3CE, 0x6BEED21A, 0x06FA2FF1, 0x64088AD9,
	0x730276D8, 0x646AC83E, 0x182B1F52, 0x0C207B17, 0x5717E1BB, 0x6C5D617A, 0xC0880977, 0xE246D9BA,
	0xA04FE208, 0x31ABE574, 0xFC5BDB43, 0x8E10FDE0, 0x20D1824B, 0xCAD23AA9, 0xFFFFFFFF, 0xFFFFFFFF,
};

static void dh_swap_bytes(void *_in, void *_out, uint32_t len)
{
	uint8_t *in = _in;
	uint8_t *out = _out;
	int i;
	for (i=0; i<len; i++) {
		out[i] = in[len-1-i];
	}
}

/* Computes the modular exponentiation (num^exp % mod).  num, exp, and mod are
 * big endian numbers of size len, in bytes.  Each len value must be a multiple
 * of 4. */
int dwc_dh_modpow(void *mem_ctx, void *num, uint32_t num_len,
		  void *exp, uint32_t exp_len,
		  void *mod, uint32_t mod_len,
		  void *out)
{
	/* modpow() takes little endian numbers.  AM uses big-endian.  This
	 * function swaps bytes of numbers before passing onto modpow. */

	int retval = 0;
	uint32_t *result;

	uint32_t *bignum_num = dwc_alloc(mem_ctx, num_len + 4);
	uint32_t *bignum_exp = dwc_alloc(mem_ctx, exp_len + 4);
	uint32_t *bignum_mod = dwc_alloc(mem_ctx, mod_len + 4);

	dh_swap_bytes(num, &bignum_num[1], num_len);
	bignum_num[0] = num_len / 4;

	dh_swap_bytes(exp, &bignum_exp[1], exp_len);
	bignum_exp[0] = exp_len / 4;

	dh_swap_bytes(mod, &bignum_mod[1], mod_len);
	bignum_mod[0] = mod_len / 4;

	result = dwc_modpow(mem_ctx, bignum_num, bignum_exp, bignum_mod);
	if (!result) {
		retval = -1;
		goto dh_modpow_nomem;
	}

	dh_swap_bytes(&result[1], out, result[0] * 4);
	dwc_free(mem_ctx, result);

 dh_modpow_nomem:
	dwc_free(mem_ctx, bignum_num);
	dwc_free(mem_ctx, bignum_exp);
	dwc_free(mem_ctx, bignum_mod);
	return retval;
}


int dwc_dh_pk(void *mem_ctx, uint8_t nd, uint8_t *exp, uint8_t *pk, uint8_t *hash)
{
	int retval;
	uint8_t m3[385];

#ifndef DH_TEST_VECTORS
	DWC_RANDOM_BYTES(exp, 32);
#endif

	/* Compute the pkd */
	if ((retval = dwc_dh_modpow(mem_ctx, dh_g, 4,
				    exp, 32,
				    dh_p, 384, pk))) {
		return retval;
	}

	m3[384] = nd;
	DWC_MEMCPY(&m3[0], pk, 384);
	DWC_SHA256(m3, 385, hash);

 	dh_dump("PK", pk, 384);
 	dh_dump("SHA-256(M3)", hash, 32);
	return 0;
}

int dwc_dh_derive_keys(void *mem_ctx, uint8_t nd, uint8_t *pkh, uint8_t *pkd,
		       uint8_t *exp, int is_host,
		       char *dd, uint8_t *ck, uint8_t *kdk)
{
	int retval;
	uint8_t mv[784];
	uint8_t sha_result[32];
	uint8_t dhkey[384];
	uint8_t shared_secret[384];
	char *message;
	uint32_t vd;

	uint8_t *pk;

	if (is_host) {
		pk = pkd;
	}
	else {
		pk = pkh;
	}

	if ((retval = dwc_dh_modpow(mem_ctx, pk, 384,
				    exp, 32,
				    dh_p, 384, shared_secret))) {
		return retval;
	}
	dh_dump("Shared Secret", shared_secret, 384);

	DWC_SHA256(shared_secret, 384, dhkey);
	dh_dump("DHKEY", dhkey, 384);

	DWC_MEMCPY(&mv[0], pkd, 384);
	DWC_MEMCPY(&mv[384], pkh, 384);
	DWC_MEMCPY(&mv[768], "displayed digest", 16);
	dh_dump("MV", mv, 784);

	DWC_SHA256(mv, 784, sha_result);
	dh_dump("SHA-256(MV)", sha_result, 32);
	dh_dump("First 32-bits of SHA-256(MV)", sha_result, 4);

	dh_swap_bytes(sha_result, &vd, 4);
#ifdef DEBUG
	DWC_PRINTF("Vd (decimal) = %d\n", vd);
#endif

	switch (nd) {
	case 2:
		vd = vd % 100;
		DWC_SPRINTF(dd, "%02d", vd);
		break;
	case 3:
		vd = vd % 1000;
		DWC_SPRINTF(dd, "%03d", vd);
		break;
	case 4:
		vd = vd % 10000;
		DWC_SPRINTF(dd, "%04d", vd);
		break;
	}
#ifdef DEBUG
	DWC_PRINTF("Display Digits: %s\n", dd);
#endif

	message = "connection key";
	DWC_HMAC_SHA256(message, DWC_STRLEN(message), dhkey, 32, sha_result);
 	dh_dump("HMAC(SHA-256, DHKey, connection key)", sha_result, 32);
	DWC_MEMCPY(ck, sha_result, 16);

	message = "key derivation key";
	DWC_HMAC_SHA256(message, DWC_STRLEN(message), dhkey, 32, sha_result);
 	dh_dump("HMAC(SHA-256, DHKey, key derivation key)", sha_result, 32);
	DWC_MEMCPY(kdk, sha_result, 32);

	return 0;
}


#ifdef DH_TEST_VECTORS

static __u8 dh_a[] = {
	0x44, 0x00, 0x51, 0xd6,
	0xf0, 0xb5, 0x5e, 0xa9,
	0x67, 0xab, 0x31, 0xc6,
	0x8a, 0x8b, 0x5e, 0x37,
	0xd9, 0x10, 0xda, 0xe0,
	0xe2, 0xd4, 0x59, 0xa4,
	0x86, 0x45, 0x9c, 0xaa,
	0xdf, 0x36, 0x75, 0x16,
};

static __u8 dh_b[] = {
	0x5d, 0xae, 0xc7, 0x86,
	0x79, 0x80, 0xa3, 0x24,
	0x8c, 0xe3, 0x57, 0x8f,
	0xc7, 0x5f, 0x1b, 0x0f,
	0x2d, 0xf8, 0x9d, 0x30,
	0x6f, 0xa4, 0x52, 0xcd,
	0xe0, 0x7a, 0x04, 0x8a,
	0xde, 0xd9, 0x26, 0x56,
};

void dwc_run_dh_test_vectors(void *mem_ctx)
{
	uint8_t pkd[384];
	uint8_t pkh[384];
	uint8_t hashd[32];
	uint8_t hashh[32];
	uint8_t ck[16];
	uint8_t kdk[32];
	char dd[5];

	DWC_PRINTF("\n\n\nDH_TEST_VECTORS\n\n");

	/* compute the PKd and SHA-256(PKd || Nd) */
	DWC_PRINTF("Computing PKd\n");
	dwc_dh_pk(mem_ctx, 2, dh_a, pkd, hashd);

	/* compute the PKd and SHA-256(PKh || Nd) */
	DWC_PRINTF("Computing PKh\n");
	dwc_dh_pk(mem_ctx, 2, dh_b, pkh, hashh);

	/* compute the dhkey */
	dwc_dh_derive_keys(mem_ctx, 2, pkh, pkd, dh_a, 0, dd, ck, kdk);
}
#endif /* DH_TEST_VECTORS */

#endif /* !CONFIG_MACH_IPMATE */

#endif /* DWC_CRYPTOLIB */

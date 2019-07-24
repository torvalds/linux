// SPDX-License-Identifier: GPL-2.0
/*
 * Ultra Wide Band
 * AES-128 CCM Encryption
 *
 * Copyright (C) 2007 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * We don't do any encryption here; we use the Linux Kernel's AES-128
 * crypto modules to construct keys and payload blocks in a way
 * defined by WUSB1.0[6]. Check the erratas, as typos are are patched
 * there.
 *
 * Thanks a zillion to John Keys for his help and clarifications over
 * the designed-by-a-committee text.
 *
 * So the idea is that there is this basic Pseudo-Random-Function
 * defined in WUSB1.0[6.5] which is the core of everything. It works
 * by tweaking some blocks, AES crypting them and then xoring
 * something else with them (this seems to be called CBC(AES) -- can
 * you tell I know jack about crypto?). So we just funnel it into the
 * Linux Crypto API.
 *
 * We leave a crypto test module so we can verify that vectors match,
 * every now and then.
 *
 * Block size: 16 bytes -- AES seems to do things in 'block sizes'. I
 *             am learning a lot...
 *
 *             Conveniently, some data structures that need to be
 *             funneled through AES are...16 bytes in size!
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/uwb.h>
#include <linux/slab.h>
#include <linux/usb/wusb.h>
#include <linux/scatterlist.h>

static int debug_crypto_verify;

module_param(debug_crypto_verify, int, 0);
MODULE_PARM_DESC(debug_crypto_verify, "verify the key generation algorithms");

static void wusb_key_dump(const void *buf, size_t len)
{
	print_hex_dump(KERN_ERR, "  ", DUMP_PREFIX_OFFSET, 16, 1,
		       buf, len, 0);
}

/*
 * Block of data, as understood by AES-CCM
 *
 * The code assumes this structure is nothing but a 16 byte array
 * (packed in a struct to avoid common mess ups that I usually do with
 * arrays and enforcing type checking).
 */
struct aes_ccm_block {
	u8 data[16];
} __attribute__((packed));

/*
 * Counter-mode Blocks (WUSB1.0[6.4])
 *
 * According to CCM (or so it seems), for the purpose of calculating
 * the MIC, the message is broken in N counter-mode blocks, B0, B1,
 * ... BN.
 *
 * B0 contains flags, the CCM nonce and l(m).
 *
 * B1 contains l(a), the MAC header, the encryption offset and padding.
 *
 * If EO is nonzero, additional blocks are built from payload bytes
 * until EO is exhausted (FIXME: padding to 16 bytes, I guess). The
 * padding is not xmitted.
 */

/* WUSB1.0[T6.4] */
struct aes_ccm_b0 {
	u8 flags;	/* 0x59, per CCM spec */
	struct aes_ccm_nonce ccm_nonce;
	__be16 lm;
} __attribute__((packed));

/* WUSB1.0[T6.5] */
struct aes_ccm_b1 {
	__be16 la;
	u8 mac_header[10];
	__le16 eo;
	u8 security_reserved;	/* This is always zero */
	u8 padding;		/* 0 */
} __attribute__((packed));

/*
 * Encryption Blocks (WUSB1.0[6.4.4])
 *
 * CCM uses Ax blocks to generate a keystream with which the MIC and
 * the message's payload are encoded. A0 always encrypts/decrypts the
 * MIC. Ax (x>0) are used for the successive payload blocks.
 *
 * The x is the counter, and is increased for each block.
 */
struct aes_ccm_a {
	u8 flags;	/* 0x01, per CCM spec */
	struct aes_ccm_nonce ccm_nonce;
	__be16 counter;	/* Value of x */
} __attribute__((packed));

/* Scratch space for MAC calculations. */
struct wusb_mac_scratch {
	struct aes_ccm_b0 b0;
	struct aes_ccm_b1 b1;
	struct aes_ccm_a ax;
};

/*
 * CC-MAC function WUSB1.0[6.5]
 *
 * Take a data string and produce the encrypted CBC Counter-mode MIC
 *
 * Note the names for most function arguments are made to (more or
 * less) match those used in the pseudo-function definition given in
 * WUSB1.0[6.5].
 *
 * @tfm_cbc: CBC(AES) blkcipher handle (initialized)
 *
 * @tfm_aes: AES cipher handle (initialized)
 *
 * @mic: buffer for placing the computed MIC (Message Integrity
 *       Code). This is exactly 8 bytes, and we expect the buffer to
 *       be at least eight bytes in length.
 *
 * @key: 128 bit symmetric key
 *
 * @n: CCM nonce
 *
 * @a: ASCII string, 14 bytes long (I guess zero padded if needed;
 *     we use exactly 14 bytes).
 *
 * @b: data stream to be processed
 *
 * @blen: size of b...
 *
 * Still not very clear how this is done, but looks like this: we
 * create block B0 (as WUSB1.0[6.5] says), then we AES-crypt it with
 * @key. We bytewise xor B0 with B1 (1) and AES-crypt that. Then we
 * take the payload and divide it in blocks (16 bytes), xor them with
 * the previous crypto result (16 bytes) and crypt it, repeat the next
 * block with the output of the previous one, rinse wash. So we use
 * the CBC-MAC(AES) shash, that does precisely that. The IV (Initial
 * Vector) is 16 bytes and is set to zero, so
 *
 * (1) Created as 6.5 says, again, using as l(a) 'Blen + 14', and
 *     using the 14 bytes of @a to fill up
 *     b1.{mac_header,e0,security_reserved,padding}.
 *
 * NOTE: The definition of l(a) in WUSB1.0[6.5] vs the definition of
 *       l(m) is orthogonal, they bear no relationship, so it is not
 *       in conflict with the parameter's relation that
 *       WUSB1.0[6.4.2]) defines.
 *
 * NOTE: WUSB1.0[A.1]: Host Nonce is missing a nibble? (1e); fixed in
 *       first errata released on 2005/07.
 *
 * NOTE: we need to clean IV to zero at each invocation to make sure
 *       we start with a fresh empty Initial Vector, so that the CBC
 *       works ok.
 *
 * NOTE: blen is not aligned to a block size, we'll pad zeros, that's
 *       what sg[4] is for. Maybe there is a smarter way to do this.
 */
static int wusb_ccm_mac(struct crypto_shash *tfm_cbcmac,
			struct wusb_mac_scratch *scratch,
			void *mic,
			const struct aes_ccm_nonce *n,
			const struct aes_ccm_label *a, const void *b,
			size_t blen)
{
	SHASH_DESC_ON_STACK(desc, tfm_cbcmac);
	u8 iv[AES_BLOCK_SIZE];

	/*
	 * These checks should be compile time optimized out
	 * ensure @a fills b1's mac_header and following fields
	 */
	BUILD_BUG_ON(sizeof(*a) != sizeof(scratch->b1) - sizeof(scratch->b1.la));
	BUILD_BUG_ON(sizeof(scratch->b0) != sizeof(struct aes_ccm_block));
	BUILD_BUG_ON(sizeof(scratch->b1) != sizeof(struct aes_ccm_block));
	BUILD_BUG_ON(sizeof(scratch->ax) != sizeof(struct aes_ccm_block));

	/* Setup B0 */
	scratch->b0.flags = 0x59;	/* Format B0 */
	scratch->b0.ccm_nonce = *n;
	scratch->b0.lm = cpu_to_be16(0);	/* WUSB1.0[6.5] sez l(m) is 0 */

	/* Setup B1
	 *
	 * The WUSB spec is anything but clear! WUSB1.0[6.5]
	 * says that to initialize B1 from A with 'l(a) = blen +
	 * 14'--after clarification, it means to use A's contents
	 * for MAC Header, EO, sec reserved and padding.
	 */
	scratch->b1.la = cpu_to_be16(blen + 14);
	memcpy(&scratch->b1.mac_header, a, sizeof(*a));

	desc->tfm = tfm_cbcmac;
	crypto_shash_init(desc);
	crypto_shash_update(desc, (u8 *)&scratch->b0, sizeof(scratch->b0) +
						      sizeof(scratch->b1));
	crypto_shash_finup(desc, b, blen, iv);

	/* Now we crypt the MIC Tag (*iv) with Ax -- values per WUSB1.0[6.5]
	 * The procedure is to AES crypt the A0 block and XOR the MIC
	 * Tag against it; we only do the first 8 bytes and place it
	 * directly in the destination buffer.
	 */
	scratch->ax.flags = 0x01;		/* as per WUSB 1.0 spec */
	scratch->ax.ccm_nonce = *n;
	scratch->ax.counter = 0;

	/* reuse the CBC-MAC transform to perform the single block encryption */
	crypto_shash_digest(desc, (u8 *)&scratch->ax, sizeof(scratch->ax),
			    (u8 *)&scratch->ax);

	crypto_xor_cpy(mic, (u8 *)&scratch->ax, iv, 8);

	return 8;
}

/*
 * WUSB Pseudo Random Function (WUSB1.0[6.5])
 *
 * @b: buffer to the source data; cannot be a global or const local
 *     (will confuse the scatterlists)
 */
ssize_t wusb_prf(void *out, size_t out_size,
		 const u8 key[16], const struct aes_ccm_nonce *_n,
		 const struct aes_ccm_label *a,
		 const void *b, size_t blen, size_t len)
{
	ssize_t result, bytes = 0, bitr;
	struct aes_ccm_nonce n = *_n;
	struct crypto_shash *tfm_cbcmac;
	struct wusb_mac_scratch scratch;
	u64 sfn = 0;
	__le64 sfn_le;

	tfm_cbcmac = crypto_alloc_shash("cbcmac(aes)", 0, 0);
	if (IS_ERR(tfm_cbcmac)) {
		result = PTR_ERR(tfm_cbcmac);
		printk(KERN_ERR "E: can't load CBCMAC-AES: %d\n", (int)result);
		goto error_alloc_cbcmac;
	}

	result = crypto_shash_setkey(tfm_cbcmac, key, AES_BLOCK_SIZE);
	if (result < 0) {
		printk(KERN_ERR "E: can't set CBCMAC-AES key: %d\n", (int)result);
		goto error_setkey_cbcmac;
	}

	for (bitr = 0; bitr < (len + 63) / 64; bitr++) {
		sfn_le = cpu_to_le64(sfn++);
		memcpy(&n.sfn, &sfn_le, sizeof(n.sfn));	/* n.sfn++... */
		result = wusb_ccm_mac(tfm_cbcmac, &scratch, out + bytes,
				      &n, a, b, blen);
		if (result < 0)
			goto error_ccm_mac;
		bytes += result;
	}
	result = bytes;

error_ccm_mac:
error_setkey_cbcmac:
	crypto_free_shash(tfm_cbcmac);
error_alloc_cbcmac:
	return result;
}

/* WUSB1.0[A.2] test vectors */
static const u8 stv_hsmic_key[16] = {
	0x4b, 0x79, 0xa3, 0xcf, 0xe5, 0x53, 0x23, 0x9d,
	0xd7, 0xc1, 0x6d, 0x1c, 0x2d, 0xab, 0x6d, 0x3f
};

static const struct aes_ccm_nonce stv_hsmic_n = {
	.sfn = { 0 },
	.tkid = { 0x76, 0x98, 0x01,  },
	.dest_addr = { .data = { 0xbe, 0x00 } },
		.src_addr = { .data = { 0x76, 0x98 } },
};

/*
 * Out-of-band MIC Generation verification code
 *
 */
static int wusb_oob_mic_verify(void)
{
	int result;
	u8 mic[8];
	/* WUSB1.0[A.2] test vectors */
	static const struct usb_handshake stv_hsmic_hs = {
		.bMessageNumber = 2,
		.bStatus 	= 00,
		.tTKID 		= { 0x76, 0x98, 0x01 },
		.bReserved 	= 00,
		.CDID 		= { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
				    0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
				    0x3c, 0x3d, 0x3e, 0x3f },
		.nonce	 	= { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
				    0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
				    0x2c, 0x2d, 0x2e, 0x2f },
		.MIC	 	= { 0x75, 0x6a, 0x97, 0x51, 0x0c, 0x8c,
				    0x14, 0x7b },
	};
	size_t hs_size;

	result = wusb_oob_mic(mic, stv_hsmic_key, &stv_hsmic_n, &stv_hsmic_hs);
	if (result < 0)
		printk(KERN_ERR "E: WUSB OOB MIC test: failed: %d\n", result);
	else if (memcmp(stv_hsmic_hs.MIC, mic, sizeof(mic))) {
		printk(KERN_ERR "E: OOB MIC test: "
		       "mismatch between MIC result and WUSB1.0[A2]\n");
		hs_size = sizeof(stv_hsmic_hs) - sizeof(stv_hsmic_hs.MIC);
		printk(KERN_ERR "E: Handshake2 in: (%zu bytes)\n", hs_size);
		wusb_key_dump(&stv_hsmic_hs, hs_size);
		printk(KERN_ERR "E: CCM Nonce in: (%zu bytes)\n",
		       sizeof(stv_hsmic_n));
		wusb_key_dump(&stv_hsmic_n, sizeof(stv_hsmic_n));
		printk(KERN_ERR "E: MIC out:\n");
		wusb_key_dump(mic, sizeof(mic));
		printk(KERN_ERR "E: MIC out (from WUSB1.0[A.2]):\n");
		wusb_key_dump(stv_hsmic_hs.MIC, sizeof(stv_hsmic_hs.MIC));
		result = -EINVAL;
	} else
		result = 0;
	return result;
}

/*
 * Test vectors for Key derivation
 *
 * These come from WUSB1.0[6.5.1], the vectors in WUSB1.0[A.1]
 * (errata corrected in 2005/07).
 */
static const u8 stv_key_a1[16] __attribute__ ((__aligned__(4))) = {
	0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87,
	0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f
};

static const struct aes_ccm_nonce stv_keydvt_n_a1 = {
	.sfn = { 0 },
	.tkid = { 0x76, 0x98, 0x01,  },
	.dest_addr = { .data = { 0xbe, 0x00 } },
	.src_addr = { .data = { 0x76, 0x98 } },
};

static const struct wusb_keydvt_out stv_keydvt_out_a1 = {
	.kck = {
		0x4b, 0x79, 0xa3, 0xcf, 0xe5, 0x53, 0x23, 0x9d,
		0xd7, 0xc1, 0x6d, 0x1c, 0x2d, 0xab, 0x6d, 0x3f
	},
	.ptk = {
		0xc8, 0x70, 0x62, 0x82, 0xb6, 0x7c, 0xe9, 0x06,
		0x7b, 0xc5, 0x25, 0x69, 0xf2, 0x36, 0x61, 0x2d
	}
};

/*
 * Performa a test to make sure we match the vectors defined in
 * WUSB1.0[A.1](Errata2006/12)
 */
static int wusb_key_derive_verify(void)
{
	int result = 0;
	struct wusb_keydvt_out keydvt_out;
	/* These come from WUSB1.0[A.1] + 2006/12 errata */
	static const struct wusb_keydvt_in stv_keydvt_in_a1 = {
		.hnonce = {
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
		},
		.dnonce = {
			0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
			0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
		}
	};

	result = wusb_key_derive(&keydvt_out, stv_key_a1, &stv_keydvt_n_a1,
				 &stv_keydvt_in_a1);
	if (result < 0)
		printk(KERN_ERR "E: WUSB key derivation test: "
		       "derivation failed: %d\n", result);
	if (memcmp(&stv_keydvt_out_a1, &keydvt_out, sizeof(keydvt_out))) {
		printk(KERN_ERR "E: WUSB key derivation test: "
		       "mismatch between key derivation result "
		       "and WUSB1.0[A1] Errata 2006/12\n");
		printk(KERN_ERR "E: keydvt in: key\n");
		wusb_key_dump(stv_key_a1, sizeof(stv_key_a1));
		printk(KERN_ERR "E: keydvt in: nonce\n");
		wusb_key_dump(&stv_keydvt_n_a1, sizeof(stv_keydvt_n_a1));
		printk(KERN_ERR "E: keydvt in: hnonce & dnonce\n");
		wusb_key_dump(&stv_keydvt_in_a1, sizeof(stv_keydvt_in_a1));
		printk(KERN_ERR "E: keydvt out: KCK\n");
		wusb_key_dump(&keydvt_out.kck, sizeof(keydvt_out.kck));
		printk(KERN_ERR "E: keydvt out: PTK\n");
		wusb_key_dump(&keydvt_out.ptk, sizeof(keydvt_out.ptk));
		result = -EINVAL;
	} else
		result = 0;
	return result;
}

/*
 * Initialize crypto system
 *
 * FIXME: we do nothing now, other than verifying. Later on we'll
 * cache the encryption stuff, so that's why we have a separate init.
 */
int wusb_crypto_init(void)
{
	int result;

	if (debug_crypto_verify) {
		result = wusb_key_derive_verify();
		if (result < 0)
			return result;
		return wusb_oob_mic_verify();
	}
	return 0;
}

void wusb_crypto_exit(void)
{
	/* FIXME: free cached crypto transforms */
}

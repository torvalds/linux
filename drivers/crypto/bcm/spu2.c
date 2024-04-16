// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Broadcom
 */

/*
 * This file works with the SPU2 version of the SPU. SPU2 has different message
 * formats than the previous version of the SPU. All SPU message format
 * differences should be hidden in the spux.c,h files.
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include "util.h"
#include "spu.h"
#include "spu2.h"

#define SPU2_TX_STATUS_LEN  0	/* SPU2 has no STATUS in input packet */

/*
 * Controlled by pkt_stat_cnt field in CRYPTO_SS_SPU0_CORE_SPU2_CONTROL0
 * register. Defaults to 2.
 */
#define SPU2_RX_STATUS_LEN  2

enum spu2_proto_sel {
	SPU2_PROTO_RESV = 0,
	SPU2_MACSEC_SECTAG8_ECB = 1,
	SPU2_MACSEC_SECTAG8_SCB = 2,
	SPU2_MACSEC_SECTAG16 = 3,
	SPU2_MACSEC_SECTAG16_8_XPN = 4,
	SPU2_IPSEC = 5,
	SPU2_IPSEC_ESN = 6,
	SPU2_TLS_CIPHER = 7,
	SPU2_TLS_AEAD = 8,
	SPU2_DTLS_CIPHER = 9,
	SPU2_DTLS_AEAD = 10
};

static char *spu2_cipher_type_names[] = { "None", "AES128", "AES192", "AES256",
	"DES", "3DES"
};

static char *spu2_cipher_mode_names[] = { "ECB", "CBC", "CTR", "CFB", "OFB",
	"XTS", "CCM", "GCM"
};

static char *spu2_hash_type_names[] = { "None", "AES128", "AES192", "AES256",
	"Reserved", "Reserved", "MD5", "SHA1", "SHA224", "SHA256", "SHA384",
	"SHA512", "SHA512/224", "SHA512/256", "SHA3-224", "SHA3-256",
	"SHA3-384", "SHA3-512"
};

static char *spu2_hash_mode_names[] = { "CMAC", "CBC-MAC", "XCBC-MAC", "HMAC",
	"Rabin", "CCM", "GCM", "Reserved"
};

static char *spu2_ciph_type_name(enum spu2_cipher_type cipher_type)
{
	if (cipher_type >= SPU2_CIPHER_TYPE_LAST)
		return "Reserved";
	return spu2_cipher_type_names[cipher_type];
}

static char *spu2_ciph_mode_name(enum spu2_cipher_mode cipher_mode)
{
	if (cipher_mode >= SPU2_CIPHER_MODE_LAST)
		return "Reserved";
	return spu2_cipher_mode_names[cipher_mode];
}

static char *spu2_hash_type_name(enum spu2_hash_type hash_type)
{
	if (hash_type >= SPU2_HASH_TYPE_LAST)
		return "Reserved";
	return spu2_hash_type_names[hash_type];
}

static char *spu2_hash_mode_name(enum spu2_hash_mode hash_mode)
{
	if (hash_mode >= SPU2_HASH_MODE_LAST)
		return "Reserved";
	return spu2_hash_mode_names[hash_mode];
}

/*
 * Convert from a software cipher mode value to the corresponding value
 * for SPU2.
 */
static int spu2_cipher_mode_xlate(enum spu_cipher_mode cipher_mode,
				  enum spu2_cipher_mode *spu2_mode)
{
	switch (cipher_mode) {
	case CIPHER_MODE_ECB:
		*spu2_mode = SPU2_CIPHER_MODE_ECB;
		break;
	case CIPHER_MODE_CBC:
		*spu2_mode = SPU2_CIPHER_MODE_CBC;
		break;
	case CIPHER_MODE_OFB:
		*spu2_mode = SPU2_CIPHER_MODE_OFB;
		break;
	case CIPHER_MODE_CFB:
		*spu2_mode = SPU2_CIPHER_MODE_CFB;
		break;
	case CIPHER_MODE_CTR:
		*spu2_mode = SPU2_CIPHER_MODE_CTR;
		break;
	case CIPHER_MODE_CCM:
		*spu2_mode = SPU2_CIPHER_MODE_CCM;
		break;
	case CIPHER_MODE_GCM:
		*spu2_mode = SPU2_CIPHER_MODE_GCM;
		break;
	case CIPHER_MODE_XTS:
		*spu2_mode = SPU2_CIPHER_MODE_XTS;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * spu2_cipher_xlate() - Convert a cipher {alg/mode/type} triple to a SPU2
 * cipher type and mode.
 * @cipher_alg:  [in]  cipher algorithm value from software enumeration
 * @cipher_mode: [in]  cipher mode value from software enumeration
 * @cipher_type: [in]  cipher type value from software enumeration
 * @spu2_type:   [out] cipher type value used by spu2 hardware
 * @spu2_mode:   [out] cipher mode value used by spu2 hardware
 *
 * Return:  0 if successful
 */
static int spu2_cipher_xlate(enum spu_cipher_alg cipher_alg,
			     enum spu_cipher_mode cipher_mode,
			     enum spu_cipher_type cipher_type,
			     enum spu2_cipher_type *spu2_type,
			     enum spu2_cipher_mode *spu2_mode)
{
	int err;

	err = spu2_cipher_mode_xlate(cipher_mode, spu2_mode);
	if (err) {
		flow_log("Invalid cipher mode %d\n", cipher_mode);
		return err;
	}

	switch (cipher_alg) {
	case CIPHER_ALG_NONE:
		*spu2_type = SPU2_CIPHER_TYPE_NONE;
		break;
	case CIPHER_ALG_RC4:
		/* SPU2 does not support RC4 */
		err = -EINVAL;
		*spu2_type = SPU2_CIPHER_TYPE_NONE;
		break;
	case CIPHER_ALG_DES:
		*spu2_type = SPU2_CIPHER_TYPE_DES;
		break;
	case CIPHER_ALG_3DES:
		*spu2_type = SPU2_CIPHER_TYPE_3DES;
		break;
	case CIPHER_ALG_AES:
		switch (cipher_type) {
		case CIPHER_TYPE_AES128:
			*spu2_type = SPU2_CIPHER_TYPE_AES128;
			break;
		case CIPHER_TYPE_AES192:
			*spu2_type = SPU2_CIPHER_TYPE_AES192;
			break;
		case CIPHER_TYPE_AES256:
			*spu2_type = SPU2_CIPHER_TYPE_AES256;
			break;
		default:
			err = -EINVAL;
		}
		break;
	case CIPHER_ALG_LAST:
	default:
		err = -EINVAL;
		break;
	}

	if (err)
		flow_log("Invalid cipher alg %d or type %d\n",
			 cipher_alg, cipher_type);
	return err;
}

/*
 * Convert from a software hash mode value to the corresponding value
 * for SPU2. Note that HASH_MODE_NONE and HASH_MODE_XCBC have the same value.
 */
static int spu2_hash_mode_xlate(enum hash_mode hash_mode,
				enum spu2_hash_mode *spu2_mode)
{
	switch (hash_mode) {
	case HASH_MODE_XCBC:
		*spu2_mode = SPU2_HASH_MODE_XCBC_MAC;
		break;
	case HASH_MODE_CMAC:
		*spu2_mode = SPU2_HASH_MODE_CMAC;
		break;
	case HASH_MODE_HMAC:
		*spu2_mode = SPU2_HASH_MODE_HMAC;
		break;
	case HASH_MODE_CCM:
		*spu2_mode = SPU2_HASH_MODE_CCM;
		break;
	case HASH_MODE_GCM:
		*spu2_mode = SPU2_HASH_MODE_GCM;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * spu2_hash_xlate() - Convert a hash {alg/mode/type} triple to a SPU2 hash type
 * and mode.
 * @hash_alg:  [in] hash algorithm value from software enumeration
 * @hash_mode: [in] hash mode value from software enumeration
 * @hash_type: [in] hash type value from software enumeration
 * @ciph_type: [in] cipher type value from software enumeration
 * @spu2_type: [out] hash type value used by SPU2 hardware
 * @spu2_mode: [out] hash mode value used by SPU2 hardware
 *
 * Return:  0 if successful
 */
static int
spu2_hash_xlate(enum hash_alg hash_alg, enum hash_mode hash_mode,
		enum hash_type hash_type, enum spu_cipher_type ciph_type,
		enum spu2_hash_type *spu2_type, enum spu2_hash_mode *spu2_mode)
{
	int err;

	err = spu2_hash_mode_xlate(hash_mode, spu2_mode);
	if (err) {
		flow_log("Invalid hash mode %d\n", hash_mode);
		return err;
	}

	switch (hash_alg) {
	case HASH_ALG_NONE:
		*spu2_type = SPU2_HASH_TYPE_NONE;
		break;
	case HASH_ALG_MD5:
		*spu2_type = SPU2_HASH_TYPE_MD5;
		break;
	case HASH_ALG_SHA1:
		*spu2_type = SPU2_HASH_TYPE_SHA1;
		break;
	case HASH_ALG_SHA224:
		*spu2_type = SPU2_HASH_TYPE_SHA224;
		break;
	case HASH_ALG_SHA256:
		*spu2_type = SPU2_HASH_TYPE_SHA256;
		break;
	case HASH_ALG_SHA384:
		*spu2_type = SPU2_HASH_TYPE_SHA384;
		break;
	case HASH_ALG_SHA512:
		*spu2_type = SPU2_HASH_TYPE_SHA512;
		break;
	case HASH_ALG_AES:
		switch (ciph_type) {
		case CIPHER_TYPE_AES128:
			*spu2_type = SPU2_HASH_TYPE_AES128;
			break;
		case CIPHER_TYPE_AES192:
			*spu2_type = SPU2_HASH_TYPE_AES192;
			break;
		case CIPHER_TYPE_AES256:
			*spu2_type = SPU2_HASH_TYPE_AES256;
			break;
		default:
			err = -EINVAL;
		}
		break;
	case HASH_ALG_SHA3_224:
		*spu2_type = SPU2_HASH_TYPE_SHA3_224;
		break;
	case HASH_ALG_SHA3_256:
		*spu2_type = SPU2_HASH_TYPE_SHA3_256;
		break;
	case HASH_ALG_SHA3_384:
		*spu2_type = SPU2_HASH_TYPE_SHA3_384;
		break;
	case HASH_ALG_SHA3_512:
		*spu2_type = SPU2_HASH_TYPE_SHA3_512;
		break;
	case HASH_ALG_LAST:
	default:
		err = -EINVAL;
		break;
	}

	if (err)
		flow_log("Invalid hash alg %d or type %d\n",
			 hash_alg, hash_type);
	return err;
}

/* Dump FMD ctrl0. The ctrl0 input is in host byte order */
static void spu2_dump_fmd_ctrl0(u64 ctrl0)
{
	enum spu2_cipher_type ciph_type;
	enum spu2_cipher_mode ciph_mode;
	enum spu2_hash_type hash_type;
	enum spu2_hash_mode hash_mode;
	char *ciph_name;
	char *ciph_mode_name;
	char *hash_name;
	char *hash_mode_name;
	u8 cfb;
	u8 proto;

	packet_log(" FMD CTRL0 %#16llx\n", ctrl0);
	if (ctrl0 & SPU2_CIPH_ENCRYPT_EN)
		packet_log("  encrypt\n");
	else
		packet_log("  decrypt\n");

	ciph_type = (ctrl0 & SPU2_CIPH_TYPE) >> SPU2_CIPH_TYPE_SHIFT;
	ciph_name = spu2_ciph_type_name(ciph_type);
	packet_log("  Cipher type: %s\n", ciph_name);

	if (ciph_type != SPU2_CIPHER_TYPE_NONE) {
		ciph_mode = (ctrl0 & SPU2_CIPH_MODE) >> SPU2_CIPH_MODE_SHIFT;
		ciph_mode_name = spu2_ciph_mode_name(ciph_mode);
		packet_log("  Cipher mode: %s\n", ciph_mode_name);
	}

	cfb = (ctrl0 & SPU2_CFB_MASK) >> SPU2_CFB_MASK_SHIFT;
	packet_log("  CFB %#x\n", cfb);

	proto = (ctrl0 & SPU2_PROTO_SEL) >> SPU2_PROTO_SEL_SHIFT;
	packet_log("  protocol %#x\n", proto);

	if (ctrl0 & SPU2_HASH_FIRST)
		packet_log("  hash first\n");
	else
		packet_log("  cipher first\n");

	if (ctrl0 & SPU2_CHK_TAG)
		packet_log("  check tag\n");

	hash_type = (ctrl0 & SPU2_HASH_TYPE) >> SPU2_HASH_TYPE_SHIFT;
	hash_name = spu2_hash_type_name(hash_type);
	packet_log("  Hash type: %s\n", hash_name);

	if (hash_type != SPU2_HASH_TYPE_NONE) {
		hash_mode = (ctrl0 & SPU2_HASH_MODE) >> SPU2_HASH_MODE_SHIFT;
		hash_mode_name = spu2_hash_mode_name(hash_mode);
		packet_log("  Hash mode: %s\n", hash_mode_name);
	}

	if (ctrl0 & SPU2_CIPH_PAD_EN) {
		packet_log("  Cipher pad: %#2llx\n",
			   (ctrl0 & SPU2_CIPH_PAD) >> SPU2_CIPH_PAD_SHIFT);
	}
}

/* Dump FMD ctrl1. The ctrl1 input is in host byte order */
static void spu2_dump_fmd_ctrl1(u64 ctrl1)
{
	u8 hash_key_len;
	u8 ciph_key_len;
	u8 ret_iv_len;
	u8 iv_offset;
	u8 iv_len;
	u8 hash_tag_len;
	u8 ret_md;

	packet_log(" FMD CTRL1 %#16llx\n", ctrl1);
	if (ctrl1 & SPU2_TAG_LOC)
		packet_log("  Tag after payload\n");

	packet_log("  Msg includes ");
	if (ctrl1 & SPU2_HAS_FR_DATA)
		packet_log("FD ");
	if (ctrl1 & SPU2_HAS_AAD1)
		packet_log("AAD1 ");
	if (ctrl1 & SPU2_HAS_NAAD)
		packet_log("NAAD ");
	if (ctrl1 & SPU2_HAS_AAD2)
		packet_log("AAD2 ");
	if (ctrl1 & SPU2_HAS_ESN)
		packet_log("ESN ");
	packet_log("\n");

	hash_key_len = (ctrl1 & SPU2_HASH_KEY_LEN) >> SPU2_HASH_KEY_LEN_SHIFT;
	packet_log("  Hash key len %u\n", hash_key_len);

	ciph_key_len = (ctrl1 & SPU2_CIPH_KEY_LEN) >> SPU2_CIPH_KEY_LEN_SHIFT;
	packet_log("  Cipher key len %u\n", ciph_key_len);

	if (ctrl1 & SPU2_GENIV)
		packet_log("  Generate IV\n");

	if (ctrl1 & SPU2_HASH_IV)
		packet_log("  IV included in hash\n");

	if (ctrl1 & SPU2_RET_IV)
		packet_log("  Return IV in output before payload\n");

	ret_iv_len = (ctrl1 & SPU2_RET_IV_LEN) >> SPU2_RET_IV_LEN_SHIFT;
	packet_log("  Length of returned IV %u bytes\n",
		   ret_iv_len ? ret_iv_len : 16);

	iv_offset = (ctrl1 & SPU2_IV_OFFSET) >> SPU2_IV_OFFSET_SHIFT;
	packet_log("  IV offset %u\n", iv_offset);

	iv_len = (ctrl1 & SPU2_IV_LEN) >> SPU2_IV_LEN_SHIFT;
	packet_log("  Input IV len %u bytes\n", iv_len);

	hash_tag_len = (ctrl1 & SPU2_HASH_TAG_LEN) >> SPU2_HASH_TAG_LEN_SHIFT;
	packet_log("  Hash tag length %u bytes\n", hash_tag_len);

	packet_log("  Return ");
	ret_md = (ctrl1 & SPU2_RETURN_MD) >> SPU2_RETURN_MD_SHIFT;
	if (ret_md)
		packet_log("FMD ");
	if (ret_md == SPU2_RET_FMD_OMD)
		packet_log("OMD ");
	else if (ret_md == SPU2_RET_FMD_OMD_IV)
		packet_log("OMD IV ");
	if (ctrl1 & SPU2_RETURN_FD)
		packet_log("FD ");
	if (ctrl1 & SPU2_RETURN_AAD1)
		packet_log("AAD1 ");
	if (ctrl1 & SPU2_RETURN_NAAD)
		packet_log("NAAD ");
	if (ctrl1 & SPU2_RETURN_AAD2)
		packet_log("AAD2 ");
	if (ctrl1 & SPU2_RETURN_PAY)
		packet_log("Payload");
	packet_log("\n");
}

/* Dump FMD ctrl2. The ctrl2 input is in host byte order */
static void spu2_dump_fmd_ctrl2(u64 ctrl2)
{
	packet_log(" FMD CTRL2 %#16llx\n", ctrl2);

	packet_log("  AAD1 offset %llu length %llu bytes\n",
		   ctrl2 & SPU2_AAD1_OFFSET,
		   (ctrl2 & SPU2_AAD1_LEN) >> SPU2_AAD1_LEN_SHIFT);
	packet_log("  AAD2 offset %llu\n",
		   (ctrl2 & SPU2_AAD2_OFFSET) >> SPU2_AAD2_OFFSET_SHIFT);
	packet_log("  Payload offset %llu\n",
		   (ctrl2 & SPU2_PL_OFFSET) >> SPU2_PL_OFFSET_SHIFT);
}

/* Dump FMD ctrl3. The ctrl3 input is in host byte order */
static void spu2_dump_fmd_ctrl3(u64 ctrl3)
{
	packet_log(" FMD CTRL3 %#16llx\n", ctrl3);

	packet_log("  Payload length %llu bytes\n", ctrl3 & SPU2_PL_LEN);
	packet_log("  TLS length %llu bytes\n",
		   (ctrl3 & SPU2_TLS_LEN) >> SPU2_TLS_LEN_SHIFT);
}

static void spu2_dump_fmd(struct SPU2_FMD *fmd)
{
	spu2_dump_fmd_ctrl0(le64_to_cpu(fmd->ctrl0));
	spu2_dump_fmd_ctrl1(le64_to_cpu(fmd->ctrl1));
	spu2_dump_fmd_ctrl2(le64_to_cpu(fmd->ctrl2));
	spu2_dump_fmd_ctrl3(le64_to_cpu(fmd->ctrl3));
}

static void spu2_dump_omd(u8 *omd, u16 hash_key_len, u16 ciph_key_len,
			  u16 hash_iv_len, u16 ciph_iv_len)
{
	u8 *ptr = omd;

	packet_log(" OMD:\n");

	if (hash_key_len) {
		packet_log("  Hash Key Length %u bytes\n", hash_key_len);
		packet_dump("  KEY: ", ptr, hash_key_len);
		ptr += hash_key_len;
	}

	if (ciph_key_len) {
		packet_log("  Cipher Key Length %u bytes\n", ciph_key_len);
		packet_dump("  KEY: ", ptr, ciph_key_len);
		ptr += ciph_key_len;
	}

	if (hash_iv_len) {
		packet_log("  Hash IV Length %u bytes\n", hash_iv_len);
		packet_dump("  hash IV: ", ptr, hash_iv_len);
		ptr += ciph_key_len;
	}

	if (ciph_iv_len) {
		packet_log("  Cipher IV Length %u bytes\n", ciph_iv_len);
		packet_dump("  cipher IV: ", ptr, ciph_iv_len);
	}
}

/* Dump a SPU2 header for debug */
void spu2_dump_msg_hdr(u8 *buf, unsigned int buf_len)
{
	struct SPU2_FMD *fmd = (struct SPU2_FMD *)buf;
	u8 *omd;
	u64 ctrl1;
	u16 hash_key_len;
	u16 ciph_key_len;
	u16 hash_iv_len;
	u16 ciph_iv_len;
	u16 omd_len;

	packet_log("\n");
	packet_log("SPU2 message header %p len: %u\n", buf, buf_len);

	spu2_dump_fmd(fmd);
	omd = (u8 *)(fmd + 1);

	ctrl1 = le64_to_cpu(fmd->ctrl1);
	hash_key_len = (ctrl1 & SPU2_HASH_KEY_LEN) >> SPU2_HASH_KEY_LEN_SHIFT;
	ciph_key_len = (ctrl1 & SPU2_CIPH_KEY_LEN) >> SPU2_CIPH_KEY_LEN_SHIFT;
	hash_iv_len = 0;
	ciph_iv_len = (ctrl1 & SPU2_IV_LEN) >> SPU2_IV_LEN_SHIFT;
	spu2_dump_omd(omd, hash_key_len, ciph_key_len, hash_iv_len,
		      ciph_iv_len);

	/* Double check sanity */
	omd_len = hash_key_len + ciph_key_len + hash_iv_len + ciph_iv_len;
	if (FMD_SIZE + omd_len != buf_len) {
		packet_log
		    (" Packet parsed incorrectly. buf_len %u, sum of MD %zu\n",
		     buf_len, FMD_SIZE + omd_len);
	}
	packet_log("\n");
}

/**
 * spu2_fmd_init() - At setkey time, initialize the fixed meta data for
 * subsequent skcipher requests for this context.
 * @fmd:               Start of FMD field to be written
 * @spu2_type:         Cipher algorithm
 * @spu2_mode:         Cipher mode
 * @cipher_key_len:    Length of cipher key, in bytes
 * @cipher_iv_len:     Length of cipher initialization vector, in bytes
 *
 * Return:  0 (success)
 */
static int spu2_fmd_init(struct SPU2_FMD *fmd,
			 enum spu2_cipher_type spu2_type,
			 enum spu2_cipher_mode spu2_mode,
			 u32 cipher_key_len, u32 cipher_iv_len)
{
	u64 ctrl0;
	u64 ctrl1;
	u64 ctrl2;
	u64 ctrl3;
	u32 aad1_offset;
	u32 aad2_offset;
	u16 aad1_len = 0;
	u64 payload_offset;

	ctrl0 = (spu2_type << SPU2_CIPH_TYPE_SHIFT) |
	    (spu2_mode << SPU2_CIPH_MODE_SHIFT);

	ctrl1 = (cipher_key_len << SPU2_CIPH_KEY_LEN_SHIFT) |
	    ((u64)cipher_iv_len << SPU2_IV_LEN_SHIFT) |
	    ((u64)SPU2_RET_FMD_ONLY << SPU2_RETURN_MD_SHIFT) | SPU2_RETURN_PAY;

	/*
	 * AAD1 offset is from start of FD. FD length is always 0 for this
	 * driver. So AAD1_offset is always 0.
	 */
	aad1_offset = 0;
	aad2_offset = aad1_offset;
	payload_offset = 0;
	ctrl2 = aad1_offset |
	    (aad1_len << SPU2_AAD1_LEN_SHIFT) |
	    (aad2_offset << SPU2_AAD2_OFFSET_SHIFT) |
	    (payload_offset << SPU2_PL_OFFSET_SHIFT);

	ctrl3 = 0;

	fmd->ctrl0 = cpu_to_le64(ctrl0);
	fmd->ctrl1 = cpu_to_le64(ctrl1);
	fmd->ctrl2 = cpu_to_le64(ctrl2);
	fmd->ctrl3 = cpu_to_le64(ctrl3);

	return 0;
}

/**
 * spu2_fmd_ctrl0_write() - Write ctrl0 field in fixed metadata (FMD) field of
 * SPU request packet.
 * @fmd:            Start of FMD field to be written
 * @is_inbound:     true if decrypting. false if encrypting.
 * @auth_first:     true if alg authenticates before encrypting
 * @protocol:       protocol selector
 * @cipher_type:    cipher algorithm
 * @cipher_mode:    cipher mode
 * @auth_type:      authentication type
 * @auth_mode:      authentication mode
 */
static void spu2_fmd_ctrl0_write(struct SPU2_FMD *fmd,
				 bool is_inbound, bool auth_first,
				 enum spu2_proto_sel protocol,
				 enum spu2_cipher_type cipher_type,
				 enum spu2_cipher_mode cipher_mode,
				 enum spu2_hash_type auth_type,
				 enum spu2_hash_mode auth_mode)
{
	u64 ctrl0 = 0;

	if ((cipher_type != SPU2_CIPHER_TYPE_NONE) && !is_inbound)
		ctrl0 |= SPU2_CIPH_ENCRYPT_EN;

	ctrl0 |= ((u64)cipher_type << SPU2_CIPH_TYPE_SHIFT) |
	    ((u64)cipher_mode << SPU2_CIPH_MODE_SHIFT);

	if (protocol)
		ctrl0 |= (u64)protocol << SPU2_PROTO_SEL_SHIFT;

	if (auth_first)
		ctrl0 |= SPU2_HASH_FIRST;

	if (is_inbound && (auth_type != SPU2_HASH_TYPE_NONE))
		ctrl0 |= SPU2_CHK_TAG;

	ctrl0 |= (((u64)auth_type << SPU2_HASH_TYPE_SHIFT) |
		  ((u64)auth_mode << SPU2_HASH_MODE_SHIFT));

	fmd->ctrl0 = cpu_to_le64(ctrl0);
}

/**
 * spu2_fmd_ctrl1_write() - Write ctrl1 field in fixed metadata (FMD) field of
 * SPU request packet.
 * @fmd:            Start of FMD field to be written
 * @is_inbound:     true if decrypting. false if encrypting.
 * @assoc_size:     Length of additional associated data, in bytes
 * @auth_key_len:   Length of authentication key, in bytes
 * @cipher_key_len: Length of cipher key, in bytes
 * @gen_iv:         If true, hw generates IV and returns in response
 * @hash_iv:        IV participates in hash. Used for IPSEC and TLS.
 * @return_iv:      Return IV in output packet before payload
 * @ret_iv_len:     Length of IV returned from SPU, in bytes
 * @ret_iv_offset:  Offset into full IV of start of returned IV
 * @cipher_iv_len:  Length of input cipher IV, in bytes
 * @digest_size:    Length of digest (aka, hash tag or ICV), in bytes
 * @return_payload: Return payload in SPU response
 * @return_md : return metadata in SPU response
 *
 * Packet can have AAD2 w/o AAD1. For algorithms currently supported,
 * associated data goes in AAD2.
 */
static void spu2_fmd_ctrl1_write(struct SPU2_FMD *fmd, bool is_inbound,
				 u64 assoc_size,
				 u64 auth_key_len, u64 cipher_key_len,
				 bool gen_iv, bool hash_iv, bool return_iv,
				 u64 ret_iv_len, u64 ret_iv_offset,
				 u64 cipher_iv_len, u64 digest_size,
				 bool return_payload, bool return_md)
{
	u64 ctrl1 = 0;

	if (is_inbound && digest_size)
		ctrl1 |= SPU2_TAG_LOC;

	if (assoc_size) {
		ctrl1 |= SPU2_HAS_AAD2;
		ctrl1 |= SPU2_RETURN_AAD2;  /* need aad2 for gcm aes esp */
	}

	if (auth_key_len)
		ctrl1 |= ((auth_key_len << SPU2_HASH_KEY_LEN_SHIFT) &
			  SPU2_HASH_KEY_LEN);

	if (cipher_key_len)
		ctrl1 |= ((cipher_key_len << SPU2_CIPH_KEY_LEN_SHIFT) &
			  SPU2_CIPH_KEY_LEN);

	if (gen_iv)
		ctrl1 |= SPU2_GENIV;

	if (hash_iv)
		ctrl1 |= SPU2_HASH_IV;

	if (return_iv) {
		ctrl1 |= SPU2_RET_IV;
		ctrl1 |= ret_iv_len << SPU2_RET_IV_LEN_SHIFT;
		ctrl1 |= ret_iv_offset << SPU2_IV_OFFSET_SHIFT;
	}

	ctrl1 |= ((cipher_iv_len << SPU2_IV_LEN_SHIFT) & SPU2_IV_LEN);

	if (digest_size)
		ctrl1 |= ((digest_size << SPU2_HASH_TAG_LEN_SHIFT) &
			  SPU2_HASH_TAG_LEN);

	/* Let's ask for the output pkt to include FMD, but don't need to
	 * get keys and IVs back in OMD.
	 */
	if (return_md)
		ctrl1 |= ((u64)SPU2_RET_FMD_ONLY << SPU2_RETURN_MD_SHIFT);
	else
		ctrl1 |= ((u64)SPU2_RET_NO_MD << SPU2_RETURN_MD_SHIFT);

	/* Crypto API does not get assoc data back. So no need for AAD2. */

	if (return_payload)
		ctrl1 |= SPU2_RETURN_PAY;

	fmd->ctrl1 = cpu_to_le64(ctrl1);
}

/**
 * spu2_fmd_ctrl2_write() - Set the ctrl2 field in the fixed metadata field of
 * SPU2 header.
 * @fmd:            Start of FMD field to be written
 * @cipher_offset:  Number of bytes from Start of Packet (end of FD field) where
 *                  data to be encrypted or decrypted begins
 * @auth_key_len:   Length of authentication key, in bytes
 * @auth_iv_len:    Length of authentication initialization vector, in bytes
 * @cipher_key_len: Length of cipher key, in bytes
 * @cipher_iv_len:  Length of cipher IV, in bytes
 */
static void spu2_fmd_ctrl2_write(struct SPU2_FMD *fmd, u64 cipher_offset,
				 u64 auth_key_len, u64 auth_iv_len,
				 u64 cipher_key_len, u64 cipher_iv_len)
{
	u64 ctrl2;
	u64 aad1_offset;
	u64 aad2_offset;
	u16 aad1_len = 0;
	u64 payload_offset;

	/* AAD1 offset is from start of FD. FD length always 0. */
	aad1_offset = 0;

	aad2_offset = aad1_offset;
	payload_offset = cipher_offset;
	ctrl2 = aad1_offset |
	    (aad1_len << SPU2_AAD1_LEN_SHIFT) |
	    (aad2_offset << SPU2_AAD2_OFFSET_SHIFT) |
	    (payload_offset << SPU2_PL_OFFSET_SHIFT);

	fmd->ctrl2 = cpu_to_le64(ctrl2);
}

/**
 * spu2_fmd_ctrl3_write() - Set the ctrl3 field in FMD
 * @fmd:          Fixed meta data. First field in SPU2 msg header.
 * @payload_len:  Length of payload, in bytes
 */
static void spu2_fmd_ctrl3_write(struct SPU2_FMD *fmd, u64 payload_len)
{
	u64 ctrl3;

	ctrl3 = payload_len & SPU2_PL_LEN;

	fmd->ctrl3 = cpu_to_le64(ctrl3);
}

/**
 * spu2_ctx_max_payload() - Determine the maximum length of the payload for a
 * SPU message for a given cipher and hash alg context.
 * @cipher_alg:		The cipher algorithm
 * @cipher_mode:	The cipher mode
 * @blocksize:		The size of a block of data for this algo
 *
 * For SPU2, the hardware generally ignores the PayloadLen field in ctrl3 of
 * FMD and just keeps computing until it receives a DMA descriptor with the EOF
 * flag set. So we consider the max payload to be infinite. AES CCM is an
 * exception.
 *
 * Return: Max payload length in bytes
 */
u32 spu2_ctx_max_payload(enum spu_cipher_alg cipher_alg,
			 enum spu_cipher_mode cipher_mode,
			 unsigned int blocksize)
{
	if ((cipher_alg == CIPHER_ALG_AES) &&
	    (cipher_mode == CIPHER_MODE_CCM)) {
		u32 excess = SPU2_MAX_PAYLOAD % blocksize;

		return SPU2_MAX_PAYLOAD - excess;
	} else {
		return SPU_MAX_PAYLOAD_INF;
	}
}

/**
 * spu2_payload_length() -  Given a SPU2 message header, extract the payload
 * length.
 * @spu_hdr:  Start of SPU message header (FMD)
 *
 * Return: payload length, in bytes
 */
u32 spu2_payload_length(u8 *spu_hdr)
{
	struct SPU2_FMD *fmd = (struct SPU2_FMD *)spu_hdr;
	u32 pl_len;
	u64 ctrl3;

	ctrl3 = le64_to_cpu(fmd->ctrl3);
	pl_len = ctrl3 & SPU2_PL_LEN;

	return pl_len;
}

/**
 * spu2_response_hdr_len() - Determine the expected length of a SPU response
 * header.
 * @auth_key_len:  Length of authentication key, in bytes
 * @enc_key_len:   Length of encryption key, in bytes
 * @is_hash:       Unused
 *
 * For SPU2, includes just FMD. OMD is never requested.
 *
 * Return: Length of FMD, in bytes
 */
u16 spu2_response_hdr_len(u16 auth_key_len, u16 enc_key_len, bool is_hash)
{
	return FMD_SIZE;
}

/**
 * spu2_hash_pad_len() - Calculate the length of hash padding required to extend
 * data to a full block size.
 * @hash_alg:        hash algorithm
 * @hash_mode:       hash mode
 * @chunksize:       length of data, in bytes
 * @hash_block_size: size of a hash block, in bytes
 *
 * SPU2 hardware does all hash padding
 *
 * Return:  length of hash pad in bytes
 */
u16 spu2_hash_pad_len(enum hash_alg hash_alg, enum hash_mode hash_mode,
		      u32 chunksize, u16 hash_block_size)
{
	return 0;
}

/**
 * spu2_gcm_ccm_pad_len() -  Determine the length of GCM/CCM padding for either
 * the AAD field or the data.
 * @cipher_mode:  Unused
 * @data_size:    Unused
 *
 * Return:  0. Unlike SPU-M, SPU2 hardware does any GCM/CCM padding required.
 */
u32 spu2_gcm_ccm_pad_len(enum spu_cipher_mode cipher_mode,
			 unsigned int data_size)
{
	return 0;
}

/**
 * spu2_assoc_resp_len() - Determine the size of the AAD2 buffer needed to catch
 * associated data in a SPU2 output packet.
 * @cipher_mode:   cipher mode
 * @assoc_len:     length of additional associated data, in bytes
 * @iv_len:        length of initialization vector, in bytes
 * @is_encrypt:    true if encrypting. false if decrypt.
 *
 * Return: Length of buffer to catch associated data in response
 */
u32 spu2_assoc_resp_len(enum spu_cipher_mode cipher_mode,
			unsigned int assoc_len, unsigned int iv_len,
			bool is_encrypt)
{
	u32 resp_len = assoc_len;

	if (is_encrypt)
		/* gcm aes esp has to write 8-byte IV in response */
		resp_len += iv_len;
	return resp_len;
}

/**
 * spu2_aead_ivlen() - Calculate the length of the AEAD IV to be included
 * in a SPU request after the AAD and before the payload.
 * @cipher_mode:  cipher mode
 * @iv_len:   initialization vector length in bytes
 *
 * For SPU2, AEAD IV is included in OMD and does not need to be repeated
 * prior to the payload.
 *
 * Return: Length of AEAD IV in bytes
 */
u8 spu2_aead_ivlen(enum spu_cipher_mode cipher_mode, u16 iv_len)
{
	return 0;
}

/**
 * spu2_hash_type() - Determine the type of hash operation.
 * @src_sent:  The number of bytes in the current request that have already
 *             been sent to the SPU to be hashed.
 *
 * SPU2 always does a FULL hash operation
 */
enum hash_type spu2_hash_type(u32 src_sent)
{
	return HASH_TYPE_FULL;
}

/**
 * spu2_digest_size() - Determine the size of a hash digest to expect the SPU to
 * return.
 * @alg_digest_size: Number of bytes in the final digest for the given algo
 * @alg:             The hash algorithm
 * @htype:           Type of hash operation (init, update, full, etc)
 *
 */
u32 spu2_digest_size(u32 alg_digest_size, enum hash_alg alg,
		     enum hash_type htype)
{
	return alg_digest_size;
}

/**
 * spu2_create_request() - Build a SPU2 request message header, includint FMD and
 * OMD.
 * @spu_hdr: Start of buffer where SPU request header is to be written
 * @req_opts: SPU request message options
 * @cipher_parms: Parameters related to cipher algorithm
 * @hash_parms:   Parameters related to hash algorithm
 * @aead_parms:   Parameters related to AEAD operation
 * @data_size:    Length of data to be encrypted or authenticated. If AEAD, does
 *		  not include length of AAD.
 *
 * Construct the message starting at spu_hdr. Caller should allocate this buffer
 * in DMA-able memory at least SPU_HEADER_ALLOC_LEN bytes long.
 *
 * Return: the length of the SPU header in bytes. 0 if an error occurs.
 */
u32 spu2_create_request(u8 *spu_hdr,
			struct spu_request_opts *req_opts,
			struct spu_cipher_parms *cipher_parms,
			struct spu_hash_parms *hash_parms,
			struct spu_aead_parms *aead_parms,
			unsigned int data_size)
{
	struct SPU2_FMD *fmd;
	u8 *ptr;
	unsigned int buf_len;
	int err;
	enum spu2_cipher_type spu2_ciph_type = SPU2_CIPHER_TYPE_NONE;
	enum spu2_cipher_mode spu2_ciph_mode;
	enum spu2_hash_type spu2_auth_type = SPU2_HASH_TYPE_NONE;
	enum spu2_hash_mode spu2_auth_mode;
	bool return_md = true;
	enum spu2_proto_sel proto = SPU2_PROTO_RESV;

	/* size of the payload */
	unsigned int payload_len =
	    hash_parms->prebuf_len + data_size + hash_parms->pad_len -
	    ((req_opts->is_aead && req_opts->is_inbound) ?
	     hash_parms->digestsize : 0);

	/* offset of prebuf or data from start of AAD2 */
	unsigned int cipher_offset = aead_parms->assoc_size +
			aead_parms->aad_pad_len + aead_parms->iv_len;

	/* total size of the data following OMD (without STAT word padding) */
	unsigned int real_db_size = spu_real_db_size(aead_parms->assoc_size,
						 aead_parms->iv_len,
						 hash_parms->prebuf_len,
						 data_size,
						 aead_parms->aad_pad_len,
						 aead_parms->data_pad_len,
						 hash_parms->pad_len);
	unsigned int assoc_size = aead_parms->assoc_size;

	if (req_opts->is_aead &&
	    (cipher_parms->alg == CIPHER_ALG_AES) &&
	    (cipher_parms->mode == CIPHER_MODE_GCM))
		/*
		 * On SPU 2, aes gcm cipher first on encrypt, auth first on
		 * decrypt
		 */
		req_opts->auth_first = req_opts->is_inbound;

	/* and do opposite for ccm (auth 1st on encrypt) */
	if (req_opts->is_aead &&
	    (cipher_parms->alg == CIPHER_ALG_AES) &&
	    (cipher_parms->mode == CIPHER_MODE_CCM))
		req_opts->auth_first = !req_opts->is_inbound;

	flow_log("%s()\n", __func__);
	flow_log("  in:%u authFirst:%u\n",
		 req_opts->is_inbound, req_opts->auth_first);
	flow_log("  cipher alg:%u mode:%u type %u\n", cipher_parms->alg,
		 cipher_parms->mode, cipher_parms->type);
	flow_log("  is_esp: %s\n", req_opts->is_esp ? "yes" : "no");
	flow_log("    key: %d\n", cipher_parms->key_len);
	flow_dump("    key: ", cipher_parms->key_buf, cipher_parms->key_len);
	flow_log("    iv: %d\n", cipher_parms->iv_len);
	flow_dump("    iv: ", cipher_parms->iv_buf, cipher_parms->iv_len);
	flow_log("  auth alg:%u mode:%u type %u\n",
		 hash_parms->alg, hash_parms->mode, hash_parms->type);
	flow_log("  digestsize: %u\n", hash_parms->digestsize);
	flow_log("  authkey: %d\n", hash_parms->key_len);
	flow_dump("  authkey: ", hash_parms->key_buf, hash_parms->key_len);
	flow_log("  assoc_size:%u\n", assoc_size);
	flow_log("  prebuf_len:%u\n", hash_parms->prebuf_len);
	flow_log("  data_size:%u\n", data_size);
	flow_log("  hash_pad_len:%u\n", hash_parms->pad_len);
	flow_log("  real_db_size:%u\n", real_db_size);
	flow_log("  cipher_offset:%u payload_len:%u\n",
		 cipher_offset, payload_len);
	flow_log("  aead_iv: %u\n", aead_parms->iv_len);

	/* Convert to spu2 values for cipher alg, hash alg */
	err = spu2_cipher_xlate(cipher_parms->alg, cipher_parms->mode,
				cipher_parms->type,
				&spu2_ciph_type, &spu2_ciph_mode);

	/* If we are doing GCM hashing only - either via rfc4543 transform
	 * or because we happen to do GCM with AAD only and no payload - we
	 * need to configure hardware to use hash key rather than cipher key
	 * and put data into payload.  This is because unlike SPU-M, running
	 * GCM cipher with 0 size payload is not permitted.
	 */
	if ((req_opts->is_rfc4543) ||
	    ((spu2_ciph_mode == SPU2_CIPHER_MODE_GCM) &&
	    (payload_len == 0))) {
		/* Use hashing (only) and set up hash key */
		spu2_ciph_type = SPU2_CIPHER_TYPE_NONE;
		hash_parms->key_len = cipher_parms->key_len;
		memcpy(hash_parms->key_buf, cipher_parms->key_buf,
		       cipher_parms->key_len);
		cipher_parms->key_len = 0;

		if (req_opts->is_rfc4543)
			payload_len += assoc_size;
		else
			payload_len = assoc_size;
		cipher_offset = 0;
		assoc_size = 0;
	}

	if (err)
		return 0;

	flow_log("spu2 cipher type %s, cipher mode %s\n",
		 spu2_ciph_type_name(spu2_ciph_type),
		 spu2_ciph_mode_name(spu2_ciph_mode));

	err = spu2_hash_xlate(hash_parms->alg, hash_parms->mode,
			      hash_parms->type,
			      cipher_parms->type,
			      &spu2_auth_type, &spu2_auth_mode);
	if (err)
		return 0;

	flow_log("spu2 hash type %s, hash mode %s\n",
		 spu2_hash_type_name(spu2_auth_type),
		 spu2_hash_mode_name(spu2_auth_mode));

	fmd = (struct SPU2_FMD *)spu_hdr;

	spu2_fmd_ctrl0_write(fmd, req_opts->is_inbound, req_opts->auth_first,
			     proto, spu2_ciph_type, spu2_ciph_mode,
			     spu2_auth_type, spu2_auth_mode);

	spu2_fmd_ctrl1_write(fmd, req_opts->is_inbound, assoc_size,
			     hash_parms->key_len, cipher_parms->key_len,
			     false, false,
			     aead_parms->return_iv, aead_parms->ret_iv_len,
			     aead_parms->ret_iv_off,
			     cipher_parms->iv_len, hash_parms->digestsize,
			     !req_opts->bd_suppress, return_md);

	spu2_fmd_ctrl2_write(fmd, cipher_offset, hash_parms->key_len, 0,
			     cipher_parms->key_len, cipher_parms->iv_len);

	spu2_fmd_ctrl3_write(fmd, payload_len);

	ptr = (u8 *)(fmd + 1);
	buf_len = sizeof(struct SPU2_FMD);

	/* Write OMD */
	if (hash_parms->key_len) {
		memcpy(ptr, hash_parms->key_buf, hash_parms->key_len);
		ptr += hash_parms->key_len;
		buf_len += hash_parms->key_len;
	}
	if (cipher_parms->key_len) {
		memcpy(ptr, cipher_parms->key_buf, cipher_parms->key_len);
		ptr += cipher_parms->key_len;
		buf_len += cipher_parms->key_len;
	}
	if (cipher_parms->iv_len) {
		memcpy(ptr, cipher_parms->iv_buf, cipher_parms->iv_len);
		ptr += cipher_parms->iv_len;
		buf_len += cipher_parms->iv_len;
	}

	packet_dump("  SPU request header: ", spu_hdr, buf_len);

	return buf_len;
}

/**
 * spu2_cipher_req_init() - Build an skcipher SPU2 request message header,
 * including FMD and OMD.
 * @spu_hdr:       Location of start of SPU request (FMD field)
 * @cipher_parms:  Parameters describing cipher request
 *
 * Called at setkey time to initialize a msg header that can be reused for all
 * subsequent skcipher requests. Construct the message starting at spu_hdr.
 * Caller should allocate this buffer in DMA-able memory at least
 * SPU_HEADER_ALLOC_LEN bytes long.
 *
 * Return: the total length of the SPU header (FMD and OMD) in bytes. 0 if an
 * error occurs.
 */
u16 spu2_cipher_req_init(u8 *spu_hdr, struct spu_cipher_parms *cipher_parms)
{
	struct SPU2_FMD *fmd;
	u8 *omd;
	enum spu2_cipher_type spu2_type = SPU2_CIPHER_TYPE_NONE;
	enum spu2_cipher_mode spu2_mode;
	int err;

	flow_log("%s()\n", __func__);
	flow_log("  cipher alg:%u mode:%u type %u\n", cipher_parms->alg,
		 cipher_parms->mode, cipher_parms->type);
	flow_log("  cipher_iv_len: %u\n", cipher_parms->iv_len);
	flow_log("    key: %d\n", cipher_parms->key_len);
	flow_dump("    key: ", cipher_parms->key_buf, cipher_parms->key_len);

	/* Convert to spu2 values */
	err = spu2_cipher_xlate(cipher_parms->alg, cipher_parms->mode,
				cipher_parms->type, &spu2_type, &spu2_mode);
	if (err)
		return 0;

	flow_log("spu2 cipher type %s, cipher mode %s\n",
		 spu2_ciph_type_name(spu2_type),
		 spu2_ciph_mode_name(spu2_mode));

	/* Construct the FMD header */
	fmd = (struct SPU2_FMD *)spu_hdr;
	err = spu2_fmd_init(fmd, spu2_type, spu2_mode, cipher_parms->key_len,
			    cipher_parms->iv_len);
	if (err)
		return 0;

	/* Write cipher key to OMD */
	omd = (u8 *)(fmd + 1);
	if (cipher_parms->key_buf && cipher_parms->key_len)
		memcpy(omd, cipher_parms->key_buf, cipher_parms->key_len);

	packet_dump("  SPU request header: ", spu_hdr,
		    FMD_SIZE + cipher_parms->key_len + cipher_parms->iv_len);

	return FMD_SIZE + cipher_parms->key_len + cipher_parms->iv_len;
}

/**
 * spu2_cipher_req_finish() - Finish building a SPU request message header for a
 * block cipher request.
 * @spu_hdr:         Start of the request message header (MH field)
 * @spu_req_hdr_len: Length in bytes of the SPU request header
 * @is_inbound:      0 encrypt, 1 decrypt
 * @cipher_parms:    Parameters describing cipher operation to be performed
 * @data_size:       Length of the data in the BD field
 *
 * Assumes much of the header was already filled in at setkey() time in
 * spu_cipher_req_init().
 * spu_cipher_req_init() fills in the encryption key.
 */
void spu2_cipher_req_finish(u8 *spu_hdr,
			    u16 spu_req_hdr_len,
			    unsigned int is_inbound,
			    struct spu_cipher_parms *cipher_parms,
			    unsigned int data_size)
{
	struct SPU2_FMD *fmd;
	u8 *omd;		/* start of optional metadata */
	u64 ctrl0;
	u64 ctrl3;

	flow_log("%s()\n", __func__);
	flow_log(" in: %u\n", is_inbound);
	flow_log(" cipher alg: %u, cipher_type: %u\n", cipher_parms->alg,
		 cipher_parms->type);
	flow_log(" iv len: %d\n", cipher_parms->iv_len);
	flow_dump("    iv: ", cipher_parms->iv_buf, cipher_parms->iv_len);
	flow_log(" data_size: %u\n", data_size);

	fmd = (struct SPU2_FMD *)spu_hdr;
	omd = (u8 *)(fmd + 1);

	/*
	 * FMD ctrl0 was initialized at setkey time. update it to indicate
	 * whether we are encrypting or decrypting.
	 */
	ctrl0 = le64_to_cpu(fmd->ctrl0);
	if (is_inbound)
		ctrl0 &= ~SPU2_CIPH_ENCRYPT_EN;	/* decrypt */
	else
		ctrl0 |= SPU2_CIPH_ENCRYPT_EN;	/* encrypt */
	fmd->ctrl0 = cpu_to_le64(ctrl0);

	if (cipher_parms->alg && cipher_parms->iv_buf && cipher_parms->iv_len) {
		/* cipher iv provided so put it in here */
		memcpy(omd + cipher_parms->key_len, cipher_parms->iv_buf,
		       cipher_parms->iv_len);
	}

	ctrl3 = le64_to_cpu(fmd->ctrl3);
	data_size &= SPU2_PL_LEN;
	ctrl3 |= data_size;
	fmd->ctrl3 = cpu_to_le64(ctrl3);

	packet_dump("  SPU request header: ", spu_hdr, spu_req_hdr_len);
}

/**
 * spu2_request_pad() - Create pad bytes at the end of the data.
 * @pad_start:      Start of buffer where pad bytes are to be written
 * @gcm_padding:    Length of GCM padding, in bytes
 * @hash_pad_len:   Number of bytes of padding extend data to full block
 * @auth_alg:       Authentication algorithm
 * @auth_mode:      Authentication mode
 * @total_sent:     Length inserted at end of hash pad
 * @status_padding: Number of bytes of padding to align STATUS word
 *
 * There may be three forms of pad:
 *  1. GCM pad - for GCM mode ciphers, pad to 16-byte alignment
 *  2. hash pad - pad to a block length, with 0x80 data terminator and
 *                size at the end
 *  3. STAT pad - to ensure the STAT field is 4-byte aligned
 */
void spu2_request_pad(u8 *pad_start, u32 gcm_padding, u32 hash_pad_len,
		      enum hash_alg auth_alg, enum hash_mode auth_mode,
		      unsigned int total_sent, u32 status_padding)
{
	u8 *ptr = pad_start;

	/* fix data alignent for GCM */
	if (gcm_padding > 0) {
		flow_log("  GCM: padding to 16 byte alignment: %u bytes\n",
			 gcm_padding);
		memset(ptr, 0, gcm_padding);
		ptr += gcm_padding;
	}

	if (hash_pad_len > 0) {
		/* clear the padding section */
		memset(ptr, 0, hash_pad_len);

		/* terminate the data */
		*ptr = 0x80;
		ptr += (hash_pad_len - sizeof(u64));

		/* add the size at the end as required per alg */
		if (auth_alg == HASH_ALG_MD5)
			*(__le64 *)ptr = cpu_to_le64(total_sent * 8ull);
		else		/* SHA1, SHA2-224, SHA2-256 */
			*(__be64 *)ptr = cpu_to_be64(total_sent * 8ull);
		ptr += sizeof(u64);
	}

	/* pad to a 4byte alignment for STAT */
	if (status_padding > 0) {
		flow_log("  STAT: padding to 4 byte alignment: %u bytes\n",
			 status_padding);

		memset(ptr, 0, status_padding);
		ptr += status_padding;
	}
}

/**
 * spu2_xts_tweak_in_payload() - Indicate that SPU2 does NOT place the XTS
 * tweak field in the packet payload (it uses IV instead)
 *
 * Return: 0
 */
u8 spu2_xts_tweak_in_payload(void)
{
	return 0;
}

/**
 * spu2_tx_status_len() - Return the length of the STATUS field in a SPU
 * response message.
 *
 * Return: Length of STATUS field in bytes.
 */
u8 spu2_tx_status_len(void)
{
	return SPU2_TX_STATUS_LEN;
}

/**
 * spu2_rx_status_len() - Return the length of the STATUS field in a SPU
 * response message.
 *
 * Return: Length of STATUS field in bytes.
 */
u8 spu2_rx_status_len(void)
{
	return SPU2_RX_STATUS_LEN;
}

/**
 * spu2_status_process() - Process the status from a SPU response message.
 * @statp:  start of STATUS word
 *
 * Return:  0 - if status is good and response should be processed
 *         !0 - status indicates an error and response is invalid
 */
int spu2_status_process(u8 *statp)
{
	/* SPU2 status is 2 bytes by default - SPU_RX_STATUS_LEN */
	u16 status = le16_to_cpu(*(__le16 *)statp);

	if (status == 0)
		return 0;

	flow_log("rx status is %#x\n", status);
	if (status == SPU2_INVALID_ICV)
		return SPU_INVALID_ICV;

	return -EBADMSG;
}

/**
 * spu2_ccm_update_iv() - Update the IV as per the requirements for CCM mode.
 *
 * @digestsize:		Digest size of this request
 * @cipher_parms:	(pointer to) cipher parmaeters, includes IV buf & IV len
 * @assoclen:		Length of AAD data
 * @chunksize:		length of input data to be sent in this req
 * @is_encrypt:		true if this is an output/encrypt operation
 * @is_esp:		true if this is an ESP / RFC4309 operation
 *
 */
void spu2_ccm_update_iv(unsigned int digestsize,
			struct spu_cipher_parms *cipher_parms,
			unsigned int assoclen, unsigned int chunksize,
			bool is_encrypt, bool is_esp)
{
	int L;  /* size of length field, in bytes */

	/*
	 * In RFC4309 mode, L is fixed at 4 bytes; otherwise, IV from
	 * testmgr contains (L-1) in bottom 3 bits of first byte,
	 * per RFC 3610.
	 */
	if (is_esp)
		L = CCM_ESP_L_VALUE;
	else
		L = ((cipher_parms->iv_buf[0] & CCM_B0_L_PRIME) >>
		      CCM_B0_L_PRIME_SHIFT) + 1;

	/* SPU2 doesn't want these length bytes nor the first byte... */
	cipher_parms->iv_len -= (1 + L);
	memmove(cipher_parms->iv_buf, &cipher_parms->iv_buf[1],
		cipher_parms->iv_len);
}

/**
 * spu2_wordalign_padlen() - SPU2 does not require padding.
 * @data_size: length of data field in bytes
 *
 * Return: length of status field padding, in bytes (always 0 on SPU2)
 */
u32 spu2_wordalign_padlen(u32 data_size)
{
	return 0;
}

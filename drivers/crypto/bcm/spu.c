// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Broadcom
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include "util.h"
#include "spu.h"
#include "spum.h"
#include "cipher.h"

char *hash_alg_name[] = { "None", "md5", "sha1", "sha224", "sha256", "aes",
	"sha384", "sha512", "sha3_224", "sha3_256", "sha3_384", "sha3_512" };

char *aead_alg_name[] = { "ccm(aes)", "gcm(aes)", "authenc" };

/* Assumes SPU-M messages are in big endian */
void spum_dump_msg_hdr(u8 *buf, unsigned int buf_len)
{
	u8 *ptr = buf;
	struct SPUHEADER *spuh = (struct SPUHEADER *)buf;
	unsigned int hash_key_len = 0;
	unsigned int hash_state_len = 0;
	unsigned int cipher_key_len = 0;
	unsigned int iv_len;
	u32 pflags;
	u32 cflags;
	u32 ecf;
	u32 cipher_alg;
	u32 cipher_mode;
	u32 cipher_type;
	u32 hash_alg;
	u32 hash_mode;
	u32 hash_type;
	u32 sctx_size;   /* SCTX length in words */
	u32 sctx_pl_len; /* SCTX payload length in bytes */

	packet_log("\n");
	packet_log("SPU Message header %p len: %u\n", buf, buf_len);

	/* ========== Decode MH ========== */
	packet_log("  MH 0x%08x\n", be32_to_cpup((__be32 *)ptr));
	if (spuh->mh.flags & MH_SCTX_PRES)
		packet_log("    SCTX  present\n");
	if (spuh->mh.flags & MH_BDESC_PRES)
		packet_log("    BDESC present\n");
	if (spuh->mh.flags & MH_MFM_PRES)
		packet_log("    MFM   present\n");
	if (spuh->mh.flags & MH_BD_PRES)
		packet_log("    BD    present\n");
	if (spuh->mh.flags & MH_HASH_PRES)
		packet_log("    HASH  present\n");
	if (spuh->mh.flags & MH_SUPDT_PRES)
		packet_log("    SUPDT present\n");
	packet_log("    Opcode 0x%02x\n", spuh->mh.op_code);

	ptr += sizeof(spuh->mh) + sizeof(spuh->emh);  /* skip emh. unused */

	/* ========== Decode SCTX ========== */
	if (spuh->mh.flags & MH_SCTX_PRES) {
		pflags = be32_to_cpu(spuh->sa.proto_flags);
		packet_log("  SCTX[0] 0x%08x\n", pflags);
		sctx_size = pflags & SCTX_SIZE;
		packet_log("    Size %u words\n", sctx_size);

		cflags = be32_to_cpu(spuh->sa.cipher_flags);
		packet_log("  SCTX[1] 0x%08x\n", cflags);
		packet_log("    Inbound:%lu (1:decrypt/vrfy 0:encrypt/auth)\n",
			   (cflags & CIPHER_INBOUND) >> CIPHER_INBOUND_SHIFT);
		packet_log("    Order:%lu (1:AuthFirst 0:EncFirst)\n",
			   (cflags & CIPHER_ORDER) >> CIPHER_ORDER_SHIFT);
		packet_log("    ICV_IS_512:%lx\n",
			   (cflags & ICV_IS_512) >> ICV_IS_512_SHIFT);
		cipher_alg = (cflags & CIPHER_ALG) >> CIPHER_ALG_SHIFT;
		cipher_mode = (cflags & CIPHER_MODE) >> CIPHER_MODE_SHIFT;
		cipher_type = (cflags & CIPHER_TYPE) >> CIPHER_TYPE_SHIFT;
		packet_log("    Crypto Alg:%u Mode:%u Type:%u\n",
			   cipher_alg, cipher_mode, cipher_type);
		hash_alg = (cflags & HASH_ALG) >> HASH_ALG_SHIFT;
		hash_mode = (cflags & HASH_MODE) >> HASH_MODE_SHIFT;
		hash_type = (cflags & HASH_TYPE) >> HASH_TYPE_SHIFT;
		packet_log("    Hash   Alg:%x Mode:%x Type:%x\n",
			   hash_alg, hash_mode, hash_type);
		packet_log("    UPDT_Offset:%u\n", cflags & UPDT_OFST);

		ecf = be32_to_cpu(spuh->sa.ecf);
		packet_log("  SCTX[2] 0x%08x\n", ecf);
		packet_log("    WriteICV:%lu CheckICV:%lu ICV_SIZE:%u ",
			   (ecf & INSERT_ICV) >> INSERT_ICV_SHIFT,
			   (ecf & CHECK_ICV) >> CHECK_ICV_SHIFT,
			   (ecf & ICV_SIZE) >> ICV_SIZE_SHIFT);
		packet_log("BD_SUPPRESS:%lu\n",
			   (ecf & BD_SUPPRESS) >> BD_SUPPRESS_SHIFT);
		packet_log("    SCTX_IV:%lu ExplicitIV:%lu GenIV:%lu ",
			   (ecf & SCTX_IV) >> SCTX_IV_SHIFT,
			   (ecf & EXPLICIT_IV) >> EXPLICIT_IV_SHIFT,
			   (ecf & GEN_IV) >> GEN_IV_SHIFT);
		packet_log("IV_OV_OFST:%lu EXP_IV_SIZE:%u\n",
			   (ecf & IV_OFFSET) >> IV_OFFSET_SHIFT,
			   ecf & EXP_IV_SIZE);

		ptr += sizeof(struct SCTX);

		if (hash_alg && hash_mode) {
			char *name = "NONE";

			switch (hash_alg) {
			case HASH_ALG_MD5:
				hash_key_len = 16;
				name = "MD5";
				break;
			case HASH_ALG_SHA1:
				hash_key_len = 20;
				name = "SHA1";
				break;
			case HASH_ALG_SHA224:
				hash_key_len = 28;
				name = "SHA224";
				break;
			case HASH_ALG_SHA256:
				hash_key_len = 32;
				name = "SHA256";
				break;
			case HASH_ALG_SHA384:
				hash_key_len = 48;
				name = "SHA384";
				break;
			case HASH_ALG_SHA512:
				hash_key_len = 64;
				name = "SHA512";
				break;
			case HASH_ALG_AES:
				hash_key_len = 0;
				name = "AES";
				break;
			case HASH_ALG_NONE:
				break;
			}

			packet_log("    Auth Key Type:%s Length:%u Bytes\n",
				   name, hash_key_len);
			packet_dump("    KEY: ", ptr, hash_key_len);
			ptr += hash_key_len;
		} else if ((hash_alg == HASH_ALG_AES) &&
			   (hash_mode == HASH_MODE_XCBC)) {
			char *name = "NONE";

			switch (cipher_type) {
			case CIPHER_TYPE_AES128:
				hash_key_len = 16;
				name = "AES128-XCBC";
				break;
			case CIPHER_TYPE_AES192:
				hash_key_len = 24;
				name = "AES192-XCBC";
				break;
			case CIPHER_TYPE_AES256:
				hash_key_len = 32;
				name = "AES256-XCBC";
				break;
			}
			packet_log("    Auth Key Type:%s Length:%u Bytes\n",
				   name, hash_key_len);
			packet_dump("    KEY: ", ptr, hash_key_len);
			ptr += hash_key_len;
		}

		if (hash_alg && (hash_mode == HASH_MODE_NONE) &&
		    (hash_type == HASH_TYPE_UPDT)) {
			char *name = "NONE";

			switch (hash_alg) {
			case HASH_ALG_MD5:
				hash_state_len = 16;
				name = "MD5";
				break;
			case HASH_ALG_SHA1:
				hash_state_len = 20;
				name = "SHA1";
				break;
			case HASH_ALG_SHA224:
				hash_state_len = 32;
				name = "SHA224";
				break;
			case HASH_ALG_SHA256:
				hash_state_len = 32;
				name = "SHA256";
				break;
			case HASH_ALG_SHA384:
				hash_state_len = 48;
				name = "SHA384";
				break;
			case HASH_ALG_SHA512:
				hash_state_len = 64;
				name = "SHA512";
				break;
			case HASH_ALG_AES:
				hash_state_len = 0;
				name = "AES";
				break;
			case HASH_ALG_NONE:
				break;
			}

			packet_log("    Auth State Type:%s Length:%u Bytes\n",
				   name, hash_state_len);
			packet_dump("    State: ", ptr, hash_state_len);
			ptr += hash_state_len;
		}

		if (cipher_alg) {
			char *name = "NONE";

			switch (cipher_alg) {
			case CIPHER_ALG_DES:
				cipher_key_len = 8;
				name = "DES";
				break;
			case CIPHER_ALG_3DES:
				cipher_key_len = 24;
				name = "3DES";
				break;
			case CIPHER_ALG_AES:
				switch (cipher_type) {
				case CIPHER_TYPE_AES128:
					cipher_key_len = 16;
					name = "AES128";
					break;
				case CIPHER_TYPE_AES192:
					cipher_key_len = 24;
					name = "AES192";
					break;
				case CIPHER_TYPE_AES256:
					cipher_key_len = 32;
					name = "AES256";
					break;
				}
				break;
			case CIPHER_ALG_NONE:
				break;
			}

			packet_log("    Cipher Key Type:%s Length:%u Bytes\n",
				   name, cipher_key_len);

			/* XTS has two keys */
			if (cipher_mode == CIPHER_MODE_XTS) {
				packet_dump("    KEY2: ", ptr, cipher_key_len);
				ptr += cipher_key_len;
				packet_dump("    KEY1: ", ptr, cipher_key_len);
				ptr += cipher_key_len;

				cipher_key_len *= 2;
			} else {
				packet_dump("    KEY: ", ptr, cipher_key_len);
				ptr += cipher_key_len;
			}

			if (ecf & SCTX_IV) {
				sctx_pl_len = sctx_size * sizeof(u32) -
					sizeof(struct SCTX);
				iv_len = sctx_pl_len -
					(hash_key_len + hash_state_len +
					 cipher_key_len);
				packet_log("    IV Length:%u Bytes\n", iv_len);
				packet_dump("    IV: ", ptr, iv_len);
				ptr += iv_len;
			}
		}
	}

	/* ========== Decode BDESC ========== */
	if (spuh->mh.flags & MH_BDESC_PRES) {
		struct BDESC_HEADER *bdesc = (struct BDESC_HEADER *)ptr;

		packet_log("  BDESC[0] 0x%08x\n", be32_to_cpup((__be32 *)ptr));
		packet_log("    OffsetMAC:%u LengthMAC:%u\n",
			   be16_to_cpu(bdesc->offset_mac),
			   be16_to_cpu(bdesc->length_mac));
		ptr += sizeof(u32);

		packet_log("  BDESC[1] 0x%08x\n", be32_to_cpup((__be32 *)ptr));
		packet_log("    OffsetCrypto:%u LengthCrypto:%u\n",
			   be16_to_cpu(bdesc->offset_crypto),
			   be16_to_cpu(bdesc->length_crypto));
		ptr += sizeof(u32);

		packet_log("  BDESC[2] 0x%08x\n", be32_to_cpup((__be32 *)ptr));
		packet_log("    OffsetICV:%u OffsetIV:%u\n",
			   be16_to_cpu(bdesc->offset_icv),
			   be16_to_cpu(bdesc->offset_iv));
		ptr += sizeof(u32);
	}

	/* ========== Decode BD ========== */
	if (spuh->mh.flags & MH_BD_PRES) {
		struct BD_HEADER *bd = (struct BD_HEADER *)ptr;

		packet_log("  BD[0] 0x%08x\n", be32_to_cpup((__be32 *)ptr));
		packet_log("    Size:%ubytes PrevLength:%u\n",
			   be16_to_cpu(bd->size), be16_to_cpu(bd->prev_length));
		ptr += 4;
	}

	/* Double check sanity */
	if (buf + buf_len != ptr) {
		packet_log(" Packet parsed incorrectly. ");
		packet_log("buf:%p buf_len:%u buf+buf_len:%p ptr:%p\n",
			   buf, buf_len, buf + buf_len, ptr);
	}

	packet_log("\n");
}

/**
 * spum_ns2_ctx_max_payload() - Determine the max length of the payload for a
 * SPU message for a given cipher and hash alg context.
 * @cipher_alg:		The cipher algorithm
 * @cipher_mode:	The cipher mode
 * @blocksize:		The size of a block of data for this algo
 *
 * The max payload must be a multiple of the blocksize so that if a request is
 * too large to fit in a single SPU message, the request can be broken into
 * max_payload sized chunks. Each chunk must be a multiple of blocksize.
 *
 * Return: Max payload length in bytes
 */
u32 spum_ns2_ctx_max_payload(enum spu_cipher_alg cipher_alg,
			     enum spu_cipher_mode cipher_mode,
			     unsigned int blocksize)
{
	u32 max_payload = SPUM_NS2_MAX_PAYLOAD;
	u32 excess;

	/* In XTS on SPU-M, we'll need to insert tweak before input data */
	if (cipher_mode == CIPHER_MODE_XTS)
		max_payload -= SPU_XTS_TWEAK_SIZE;

	excess = max_payload % blocksize;

	return max_payload - excess;
}

/**
 * spum_nsp_ctx_max_payload() - Determine the max length of the payload for a
 * SPU message for a given cipher and hash alg context.
 * @cipher_alg:		The cipher algorithm
 * @cipher_mode:	The cipher mode
 * @blocksize:		The size of a block of data for this algo
 *
 * The max payload must be a multiple of the blocksize so that if a request is
 * too large to fit in a single SPU message, the request can be broken into
 * max_payload sized chunks. Each chunk must be a multiple of blocksize.
 *
 * Return: Max payload length in bytes
 */
u32 spum_nsp_ctx_max_payload(enum spu_cipher_alg cipher_alg,
			     enum spu_cipher_mode cipher_mode,
			     unsigned int blocksize)
{
	u32 max_payload = SPUM_NSP_MAX_PAYLOAD;
	u32 excess;

	/* In XTS on SPU-M, we'll need to insert tweak before input data */
	if (cipher_mode == CIPHER_MODE_XTS)
		max_payload -= SPU_XTS_TWEAK_SIZE;

	excess = max_payload % blocksize;

	return max_payload - excess;
}

/** spum_payload_length() - Given a SPU-M message header, extract the payload
 * length.
 * @spu_hdr:	Start of SPU header
 *
 * Assumes just MH, EMH, BD (no SCTX, BDESC. Works for response frames.
 *
 * Return: payload length in bytes
 */
u32 spum_payload_length(u8 *spu_hdr)
{
	struct BD_HEADER *bd;
	u32 pl_len;

	/* Find BD header.  skip MH, EMH */
	bd = (struct BD_HEADER *)(spu_hdr + 8);
	pl_len = be16_to_cpu(bd->size);

	return pl_len;
}

/**
 * spum_response_hdr_len() - Given the length of the hash key and encryption
 * key, determine the expected length of a SPU response header.
 * @auth_key_len:	authentication key length (bytes)
 * @enc_key_len:	encryption key length (bytes)
 * @is_hash:		true if response message is for a hash operation
 *
 * Return: length of SPU response header (bytes)
 */
u16 spum_response_hdr_len(u16 auth_key_len, u16 enc_key_len, bool is_hash)
{
	if (is_hash)
		return SPU_HASH_RESP_HDR_LEN;
	else
		return SPU_RESP_HDR_LEN;
}

/**
 * spum_hash_pad_len() - Calculate the length of hash padding required to extend
 * data to a full block size.
 * @hash_alg:   hash algorithm
 * @hash_mode:       hash mode
 * @chunksize:  length of data, in bytes
 * @hash_block_size:  size of a block of data for hash algorithm
 *
 * Reserve space for 1 byte (0x80) start of pad and the total length as u64
 *
 * Return:  length of hash pad in bytes
 */
u16 spum_hash_pad_len(enum hash_alg hash_alg, enum hash_mode hash_mode,
		      u32 chunksize, u16 hash_block_size)
{
	unsigned int length_len;
	unsigned int used_space_last_block;
	int hash_pad_len;

	/* AES-XCBC hash requires just padding to next block boundary */
	if ((hash_alg == HASH_ALG_AES) && (hash_mode == HASH_MODE_XCBC)) {
		used_space_last_block = chunksize % hash_block_size;
		hash_pad_len = hash_block_size - used_space_last_block;
		if (hash_pad_len >= hash_block_size)
			hash_pad_len -= hash_block_size;
		return hash_pad_len;
	}

	used_space_last_block = chunksize % hash_block_size + 1;
	if ((hash_alg == HASH_ALG_SHA384) || (hash_alg == HASH_ALG_SHA512))
		length_len = 2 * sizeof(u64);
	else
		length_len = sizeof(u64);

	used_space_last_block += length_len;
	hash_pad_len = hash_block_size - used_space_last_block;
	if (hash_pad_len < 0)
		hash_pad_len += hash_block_size;

	hash_pad_len += 1 + length_len;
	return hash_pad_len;
}

/**
 * spum_gcm_ccm_pad_len() - Determine the required length of GCM or CCM padding.
 * @cipher_mode:	Algo type
 * @data_size:		Length of plaintext (bytes)
 *
 * @Return: Length of padding, in bytes
 */
u32 spum_gcm_ccm_pad_len(enum spu_cipher_mode cipher_mode,
			 unsigned int data_size)
{
	u32 pad_len = 0;
	u32 m1 = SPU_GCM_CCM_ALIGN - 1;

	if ((cipher_mode == CIPHER_MODE_GCM) ||
	    (cipher_mode == CIPHER_MODE_CCM))
		pad_len = ((data_size + m1) & ~m1) - data_size;

	return pad_len;
}

/**
 * spum_assoc_resp_len() - Determine the size of the receive buffer required to
 * catch associated data.
 * @cipher_mode:	cipher mode
 * @assoc_len:		length of associated data (bytes)
 * @iv_len:		length of IV (bytes)
 * @is_encrypt:		true if encrypting. false if decrypting.
 *
 * Return: length of associated data in response message (bytes)
 */
u32 spum_assoc_resp_len(enum spu_cipher_mode cipher_mode,
			unsigned int assoc_len, unsigned int iv_len,
			bool is_encrypt)
{
	u32 buflen = 0;
	u32 pad;

	if (assoc_len)
		buflen = assoc_len;

	if (cipher_mode == CIPHER_MODE_GCM) {
		/* AAD needs to be padded in responses too */
		pad = spum_gcm_ccm_pad_len(cipher_mode, buflen);
		buflen += pad;
	}
	if (cipher_mode == CIPHER_MODE_CCM) {
		/*
		 * AAD needs to be padded in responses too
		 * for CCM, len + 2 needs to be 128-bit aligned.
		 */
		pad = spum_gcm_ccm_pad_len(cipher_mode, buflen + 2);
		buflen += pad;
	}

	return buflen;
}

/**
 * spu_aead_ivlen() - Calculate the length of the AEAD IV to be included
 * in a SPU request after the AAD and before the payload.
 * @cipher_mode:  cipher mode
 * @iv_ctr_len:   initialization vector length in bytes
 *
 * In Linux ~4.2 and later, the assoc_data sg includes the IV. So no need
 * to include the IV as a separate field in the SPU request msg.
 *
 * Return: Length of AEAD IV in bytes
 */
u8 spum_aead_ivlen(enum spu_cipher_mode cipher_mode, u16 iv_len)
{
	return 0;
}

/**
 * spum_hash_type() - Determine the type of hash operation.
 * @src_sent:  The number of bytes in the current request that have already
 *             been sent to the SPU to be hashed.
 *
 * We do not use HASH_TYPE_FULL for requests that fit in a single SPU message.
 * Using FULL causes failures (such as when the string to be hashed is empty).
 * For similar reasons, we never use HASH_TYPE_FIN. Instead, submit messages
 * as INIT or UPDT and do the hash padding in sw.
 */
enum hash_type spum_hash_type(u32 src_sent)
{
	return src_sent ? HASH_TYPE_UPDT : HASH_TYPE_INIT;
}

/**
 * spum_digest_size() - Determine the size of a hash digest to expect the SPU to
 * return.
 * alg_digest_size: Number of bytes in the final digest for the given algo
 * alg:             The hash algorithm
 * htype:           Type of hash operation (init, update, full, etc)
 *
 * When doing incremental hashing for an algorithm with a truncated hash
 * (e.g., SHA224), the SPU returns the full digest so that it can be fed back as
 * a partial result for the next chunk.
 */
u32 spum_digest_size(u32 alg_digest_size, enum hash_alg alg,
		     enum hash_type htype)
{
	u32 digestsize = alg_digest_size;

	/* SPU returns complete digest when doing incremental hash and truncated
	 * hash algo.
	 */
	if ((htype == HASH_TYPE_INIT) || (htype == HASH_TYPE_UPDT)) {
		if (alg == HASH_ALG_SHA224)
			digestsize = SHA256_DIGEST_SIZE;
		else if (alg == HASH_ALG_SHA384)
			digestsize = SHA512_DIGEST_SIZE;
	}
	return digestsize;
}

/**
 * spum_create_request() - Build a SPU request message header, up to and
 * including the BD header. Construct the message starting at spu_hdr. Caller
 * should allocate this buffer in DMA-able memory at least SPU_HEADER_ALLOC_LEN
 * bytes long.
 * @spu_hdr: Start of buffer where SPU request header is to be written
 * @req_opts: SPU request message options
 * @cipher_parms: Parameters related to cipher algorithm
 * @hash_parms:   Parameters related to hash algorithm
 * @aead_parms:   Parameters related to AEAD operation
 * @data_size:    Length of data to be encrypted or authenticated. If AEAD, does
 *		  not include length of AAD.

 * Return: the length of the SPU header in bytes. 0 if an error occurs.
 */
u32 spum_create_request(u8 *spu_hdr,
			struct spu_request_opts *req_opts,
			struct spu_cipher_parms *cipher_parms,
			struct spu_hash_parms *hash_parms,
			struct spu_aead_parms *aead_parms,
			unsigned int data_size)
{
	struct SPUHEADER *spuh;
	struct BDESC_HEADER *bdesc;
	struct BD_HEADER *bd;

	u8 *ptr;
	u32 protocol_bits = 0;
	u32 cipher_bits = 0;
	u32 ecf_bits = 0;
	u8 sctx_words = 0;
	unsigned int buf_len = 0;

	/* size of the cipher payload */
	unsigned int cipher_len = hash_parms->prebuf_len + data_size +
				hash_parms->pad_len;

	/* offset of prebuf or data from end of BD header */
	unsigned int cipher_offset = aead_parms->assoc_size +
		aead_parms->iv_len + aead_parms->aad_pad_len;

	/* total size of the DB data (without STAT word padding) */
	unsigned int real_db_size = spu_real_db_size(aead_parms->assoc_size,
						 aead_parms->iv_len,
						 hash_parms->prebuf_len,
						 data_size,
						 aead_parms->aad_pad_len,
						 aead_parms->data_pad_len,
						 hash_parms->pad_len);

	unsigned int auth_offset = 0;
	unsigned int offset_iv = 0;

	/* size/offset of the auth payload */
	unsigned int auth_len;

	auth_len = real_db_size;

	if (req_opts->is_aead && req_opts->is_inbound)
		cipher_len -= hash_parms->digestsize;

	if (req_opts->is_aead && req_opts->is_inbound)
		auth_len -= hash_parms->digestsize;

	if ((hash_parms->alg == HASH_ALG_AES) &&
	    (hash_parms->mode == HASH_MODE_XCBC)) {
		auth_len -= hash_parms->pad_len;
		cipher_len -= hash_parms->pad_len;
	}

	flow_log("%s()\n", __func__);
	flow_log("  in:%u authFirst:%u\n",
		 req_opts->is_inbound, req_opts->auth_first);
	flow_log("  %s. cipher alg:%u mode:%u type %u\n",
		 spu_alg_name(cipher_parms->alg, cipher_parms->mode),
		 cipher_parms->alg, cipher_parms->mode, cipher_parms->type);
	flow_log("    key: %d\n", cipher_parms->key_len);
	flow_dump("    key: ", cipher_parms->key_buf, cipher_parms->key_len);
	flow_log("    iv: %d\n", cipher_parms->iv_len);
	flow_dump("    iv: ", cipher_parms->iv_buf, cipher_parms->iv_len);
	flow_log("  auth alg:%u mode:%u type %u\n",
		 hash_parms->alg, hash_parms->mode, hash_parms->type);
	flow_log("  digestsize: %u\n", hash_parms->digestsize);
	flow_log("  authkey: %d\n", hash_parms->key_len);
	flow_dump("  authkey: ", hash_parms->key_buf, hash_parms->key_len);
	flow_log("  assoc_size:%u\n", aead_parms->assoc_size);
	flow_log("  prebuf_len:%u\n", hash_parms->prebuf_len);
	flow_log("  data_size:%u\n", data_size);
	flow_log("  hash_pad_len:%u\n", hash_parms->pad_len);
	flow_log("  real_db_size:%u\n", real_db_size);
	flow_log(" auth_offset:%u auth_len:%u cipher_offset:%u cipher_len:%u\n",
		 auth_offset, auth_len, cipher_offset, cipher_len);
	flow_log("  aead_iv: %u\n", aead_parms->iv_len);

	/* starting out: zero the header (plus some) */
	ptr = spu_hdr;
	memset(ptr, 0, sizeof(struct SPUHEADER));

	/* format master header word */
	/* Do not set the next bit even though the datasheet says to */
	spuh = (struct SPUHEADER *)ptr;
	ptr += sizeof(struct SPUHEADER);
	buf_len += sizeof(struct SPUHEADER);

	spuh->mh.op_code = SPU_CRYPTO_OPERATION_GENERIC;
	spuh->mh.flags |= (MH_SCTX_PRES | MH_BDESC_PRES | MH_BD_PRES);

	/* Format sctx word 0 (protocol_bits) */
	sctx_words = 3;		/* size in words */

	/* Format sctx word 1 (cipher_bits) */
	if (req_opts->is_inbound)
		cipher_bits |= CIPHER_INBOUND;
	if (req_opts->auth_first)
		cipher_bits |= CIPHER_ORDER;

	/* Set the crypto parameters in the cipher.flags */
	cipher_bits |= cipher_parms->alg << CIPHER_ALG_SHIFT;
	cipher_bits |= cipher_parms->mode << CIPHER_MODE_SHIFT;
	cipher_bits |= cipher_parms->type << CIPHER_TYPE_SHIFT;

	/* Set the auth parameters in the cipher.flags */
	cipher_bits |= hash_parms->alg << HASH_ALG_SHIFT;
	cipher_bits |= hash_parms->mode << HASH_MODE_SHIFT;
	cipher_bits |= hash_parms->type << HASH_TYPE_SHIFT;

	/*
	 * Format sctx extensions if required, and update main fields if
	 * required)
	 */
	if (hash_parms->alg) {
		/* Write the authentication key material if present */
		if (hash_parms->key_len) {
			memcpy(ptr, hash_parms->key_buf, hash_parms->key_len);
			ptr += hash_parms->key_len;
			buf_len += hash_parms->key_len;
			sctx_words += hash_parms->key_len / 4;
		}

		if ((cipher_parms->mode == CIPHER_MODE_GCM) ||
		    (cipher_parms->mode == CIPHER_MODE_CCM))
			/* unpadded length */
			offset_iv = aead_parms->assoc_size;

		/* if GCM/CCM we need to write ICV into the payload */
		if (!req_opts->is_inbound) {
			if ((cipher_parms->mode == CIPHER_MODE_GCM) ||
			    (cipher_parms->mode == CIPHER_MODE_CCM))
				ecf_bits |= 1 << INSERT_ICV_SHIFT;
		} else {
			ecf_bits |= CHECK_ICV;
		}

		/* Inform the SPU of the ICV size (in words) */
		if (hash_parms->digestsize == 64)
			cipher_bits |= ICV_IS_512;
		else
			ecf_bits |=
			(hash_parms->digestsize / 4) << ICV_SIZE_SHIFT;
	}

	if (req_opts->bd_suppress)
		ecf_bits |= BD_SUPPRESS;

	/* copy the encryption keys in the SAD entry */
	if (cipher_parms->alg) {
		if (cipher_parms->key_len) {
			memcpy(ptr, cipher_parms->key_buf,
			       cipher_parms->key_len);
			ptr += cipher_parms->key_len;
			buf_len += cipher_parms->key_len;
			sctx_words += cipher_parms->key_len / 4;
		}

		/*
		 * if encrypting then set IV size, use SCTX IV unless no IV
		 * given here
		 */
		if (cipher_parms->iv_buf && cipher_parms->iv_len) {
			/* Use SCTX IV */
			ecf_bits |= SCTX_IV;

			/* cipher iv provided so put it in here */
			memcpy(ptr, cipher_parms->iv_buf, cipher_parms->iv_len);

			ptr += cipher_parms->iv_len;
			buf_len += cipher_parms->iv_len;
			sctx_words += cipher_parms->iv_len / 4;
		}
	}

	/*
	 * RFC4543 (GMAC/ESP) requires data to be sent as part of AAD
	 * so we need to override the BDESC parameters.
	 */
	if (req_opts->is_rfc4543) {
		if (req_opts->is_inbound)
			data_size -= hash_parms->digestsize;
		offset_iv = aead_parms->assoc_size + data_size;
		cipher_len = 0;
		cipher_offset = offset_iv;
		auth_len = cipher_offset + aead_parms->data_pad_len;
	}

	/* write in the total sctx length now that we know it */
	protocol_bits |= sctx_words;

	/* Endian adjust the SCTX */
	spuh->sa.proto_flags = cpu_to_be32(protocol_bits);
	spuh->sa.cipher_flags = cpu_to_be32(cipher_bits);
	spuh->sa.ecf = cpu_to_be32(ecf_bits);

	/* === create the BDESC section === */
	bdesc = (struct BDESC_HEADER *)ptr;

	bdesc->offset_mac = cpu_to_be16(auth_offset);
	bdesc->length_mac = cpu_to_be16(auth_len);
	bdesc->offset_crypto = cpu_to_be16(cipher_offset);
	bdesc->length_crypto = cpu_to_be16(cipher_len);

	/*
	 * CCM in SPU-M requires that ICV not be in same 32-bit word as data or
	 * padding.  So account for padding as necessary.
	 */
	if (cipher_parms->mode == CIPHER_MODE_CCM)
		auth_len += spum_wordalign_padlen(auth_len);

	bdesc->offset_icv = cpu_to_be16(auth_len);
	bdesc->offset_iv = cpu_to_be16(offset_iv);

	ptr += sizeof(struct BDESC_HEADER);
	buf_len += sizeof(struct BDESC_HEADER);

	/* === no MFM section === */

	/* === create the BD section === */

	/* add the BD header */
	bd = (struct BD_HEADER *)ptr;
	bd->size = cpu_to_be16(real_db_size);
	bd->prev_length = 0;

	ptr += sizeof(struct BD_HEADER);
	buf_len += sizeof(struct BD_HEADER);

	packet_dump("  SPU request header: ", spu_hdr, buf_len);

	return buf_len;
}

/**
 * spum_cipher_req_init() - Build a SPU request message header, up to and
 * including the BD header.
 * @spu_hdr:      Start of SPU request header (MH)
 * @cipher_parms: Parameters that describe the cipher request
 *
 * Construct the message starting at spu_hdr. Caller should allocate this buffer
 * in DMA-able memory at least SPU_HEADER_ALLOC_LEN bytes long.
 *
 * Return: the length of the SPU header in bytes. 0 if an error occurs.
 */
u16 spum_cipher_req_init(u8 *spu_hdr, struct spu_cipher_parms *cipher_parms)
{
	struct SPUHEADER *spuh;
	u32 protocol_bits = 0;
	u32 cipher_bits = 0;
	u32 ecf_bits = 0;
	u8 sctx_words = 0;
	u8 *ptr = spu_hdr;

	flow_log("%s()\n", __func__);
	flow_log("  cipher alg:%u mode:%u type %u\n", cipher_parms->alg,
		 cipher_parms->mode, cipher_parms->type);
	flow_log("  cipher_iv_len: %u\n", cipher_parms->iv_len);
	flow_log("    key: %d\n", cipher_parms->key_len);
	flow_dump("    key: ", cipher_parms->key_buf, cipher_parms->key_len);

	/* starting out: zero the header (plus some) */
	memset(spu_hdr, 0, sizeof(struct SPUHEADER));
	ptr += sizeof(struct SPUHEADER);

	/* format master header word */
	/* Do not set the next bit even though the datasheet says to */
	spuh = (struct SPUHEADER *)spu_hdr;

	spuh->mh.op_code = SPU_CRYPTO_OPERATION_GENERIC;
	spuh->mh.flags |= (MH_SCTX_PRES | MH_BDESC_PRES | MH_BD_PRES);

	/* Format sctx word 0 (protocol_bits) */
	sctx_words = 3;		/* size in words */

	/* copy the encryption keys in the SAD entry */
	if (cipher_parms->alg) {
		if (cipher_parms->key_len) {
			ptr += cipher_parms->key_len;
			sctx_words += cipher_parms->key_len / 4;
		}

		/*
		 * if encrypting then set IV size, use SCTX IV unless no IV
		 * given here
		 */
		if (cipher_parms->iv_len) {
			/* Use SCTX IV */
			ecf_bits |= SCTX_IV;
			ptr += cipher_parms->iv_len;
			sctx_words += cipher_parms->iv_len / 4;
		}
	}

	/* Set the crypto parameters in the cipher.flags */
	cipher_bits |= cipher_parms->alg << CIPHER_ALG_SHIFT;
	cipher_bits |= cipher_parms->mode << CIPHER_MODE_SHIFT;
	cipher_bits |= cipher_parms->type << CIPHER_TYPE_SHIFT;

	/* copy the encryption keys in the SAD entry */
	if (cipher_parms->alg && cipher_parms->key_len)
		memcpy(spuh + 1, cipher_parms->key_buf, cipher_parms->key_len);

	/* write in the total sctx length now that we know it */
	protocol_bits |= sctx_words;

	/* Endian adjust the SCTX */
	spuh->sa.proto_flags = cpu_to_be32(protocol_bits);

	/* Endian adjust the SCTX */
	spuh->sa.cipher_flags = cpu_to_be32(cipher_bits);
	spuh->sa.ecf = cpu_to_be32(ecf_bits);

	packet_dump("  SPU request header: ", spu_hdr,
		    sizeof(struct SPUHEADER));

	return sizeof(struct SPUHEADER) + cipher_parms->key_len +
		cipher_parms->iv_len + sizeof(struct BDESC_HEADER) +
		sizeof(struct BD_HEADER);
}

/**
 * spum_cipher_req_finish() - Finish building a SPU request message header for a
 * block cipher request. Assumes much of the header was already filled in at
 * setkey() time in spu_cipher_req_init().
 * @spu_hdr:         Start of the request message header (MH field)
 * @spu_req_hdr_len: Length in bytes of the SPU request header
 * @isInbound:       0 encrypt, 1 decrypt
 * @cipher_parms:    Parameters describing cipher operation to be performed
 * @data_size:       Length of the data in the BD field
 *
 * Assumes much of the header was already filled in at setkey() time in
 * spum_cipher_req_init().
 * spum_cipher_req_init() fills in the encryption key.
 */
void spum_cipher_req_finish(u8 *spu_hdr,
			    u16 spu_req_hdr_len,
			    unsigned int is_inbound,
			    struct spu_cipher_parms *cipher_parms,
			    unsigned int data_size)
{
	struct SPUHEADER *spuh;
	struct BDESC_HEADER *bdesc;
	struct BD_HEADER *bd;
	u8 *bdesc_ptr = spu_hdr + spu_req_hdr_len -
	    (sizeof(struct BD_HEADER) + sizeof(struct BDESC_HEADER));

	u32 cipher_bits;

	flow_log("%s()\n", __func__);
	flow_log(" in: %u\n", is_inbound);
	flow_log(" cipher alg: %u, cipher_type: %u\n", cipher_parms->alg,
		 cipher_parms->type);

	/*
	 * In XTS mode, API puts "i" parameter (block tweak) in IV.  For
	 * SPU-M, should be in start of the BD; tx_sg_create() copies it there.
	 * IV in SPU msg for SPU-M should be 0, since that's the "j" parameter
	 * (block ctr within larger data unit) - given we can send entire disk
	 * block (<= 4KB) in 1 SPU msg, don't need to use this parameter.
	 */
	if (cipher_parms->mode == CIPHER_MODE_XTS)
		memset(cipher_parms->iv_buf, 0, cipher_parms->iv_len);

	flow_log(" iv len: %d\n", cipher_parms->iv_len);
	flow_dump("    iv: ", cipher_parms->iv_buf, cipher_parms->iv_len);
	flow_log(" data_size: %u\n", data_size);

	/* format master header word */
	/* Do not set the next bit even though the datasheet says to */
	spuh = (struct SPUHEADER *)spu_hdr;

	/* cipher_bits was initialized at setkey time */
	cipher_bits = be32_to_cpu(spuh->sa.cipher_flags);

	/* Format sctx word 1 (cipher_bits) */
	if (is_inbound)
		cipher_bits |= CIPHER_INBOUND;
	else
		cipher_bits &= ~CIPHER_INBOUND;

	if (cipher_parms->alg && cipher_parms->iv_buf && cipher_parms->iv_len)
		/* cipher iv provided so put it in here */
		memcpy(bdesc_ptr - cipher_parms->iv_len, cipher_parms->iv_buf,
		       cipher_parms->iv_len);

	spuh->sa.cipher_flags = cpu_to_be32(cipher_bits);

	/* === create the BDESC section === */
	bdesc = (struct BDESC_HEADER *)bdesc_ptr;
	bdesc->offset_mac = 0;
	bdesc->length_mac = 0;
	bdesc->offset_crypto = 0;

	/* XTS mode, data_size needs to include tweak parameter */
	if (cipher_parms->mode == CIPHER_MODE_XTS)
		bdesc->length_crypto = cpu_to_be16(data_size +
						  SPU_XTS_TWEAK_SIZE);
	else
		bdesc->length_crypto = cpu_to_be16(data_size);

	bdesc->offset_icv = 0;
	bdesc->offset_iv = 0;

	/* === no MFM section === */

	/* === create the BD section === */
	/* add the BD header */
	bd = (struct BD_HEADER *)(bdesc_ptr + sizeof(struct BDESC_HEADER));
	bd->size = cpu_to_be16(data_size);

	/* XTS mode, data_size needs to include tweak parameter */
	if (cipher_parms->mode == CIPHER_MODE_XTS)
		bd->size = cpu_to_be16(data_size + SPU_XTS_TWEAK_SIZE);
	else
		bd->size = cpu_to_be16(data_size);

	bd->prev_length = 0;

	packet_dump("  SPU request header: ", spu_hdr, spu_req_hdr_len);
}

/**
 * spum_request_pad() - Create pad bytes at the end of the data.
 * @pad_start:		Start of buffer where pad bytes are to be written
 * @gcm_ccm_padding:	length of GCM/CCM padding, in bytes
 * @hash_pad_len:	Number of bytes of padding extend data to full block
 * @auth_alg:		authentication algorithm
 * @auth_mode:		authentication mode
 * @total_sent:		length inserted at end of hash pad
 * @status_padding:	Number of bytes of padding to align STATUS word
 *
 * There may be three forms of pad:
 *  1. GCM/CCM pad - for GCM/CCM mode ciphers, pad to 16-byte alignment
 *  2. hash pad - pad to a block length, with 0x80 data terminator and
 *                size at the end
 *  3. STAT pad - to ensure the STAT field is 4-byte aligned
 */
void spum_request_pad(u8 *pad_start,
		      u32 gcm_ccm_padding,
		      u32 hash_pad_len,
		      enum hash_alg auth_alg,
		      enum hash_mode auth_mode,
		      unsigned int total_sent, u32 status_padding)
{
	u8 *ptr = pad_start;

	/* fix data alignent for GCM/CCM */
	if (gcm_ccm_padding > 0) {
		flow_log("  GCM: padding to 16 byte alignment: %u bytes\n",
			 gcm_ccm_padding);
		memset(ptr, 0, gcm_ccm_padding);
		ptr += gcm_ccm_padding;
	}

	if (hash_pad_len > 0) {
		/* clear the padding section */
		memset(ptr, 0, hash_pad_len);

		if ((auth_alg == HASH_ALG_AES) &&
		    (auth_mode == HASH_MODE_XCBC)) {
			/* AES/XCBC just requires padding to be 0s */
			ptr += hash_pad_len;
		} else {
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
 * spum_xts_tweak_in_payload() - Indicate that SPUM DOES place the XTS tweak
 * field in the packet payload (rather than using IV)
 *
 * Return: 1
 */
u8 spum_xts_tweak_in_payload(void)
{
	return 1;
}

/**
 * spum_tx_status_len() - Return the length of the STATUS field in a SPU
 * response message.
 *
 * Return: Length of STATUS field in bytes.
 */
u8 spum_tx_status_len(void)
{
	return SPU_TX_STATUS_LEN;
}

/**
 * spum_rx_status_len() - Return the length of the STATUS field in a SPU
 * response message.
 *
 * Return: Length of STATUS field in bytes.
 */
u8 spum_rx_status_len(void)
{
	return SPU_RX_STATUS_LEN;
}

/**
 * spum_status_process() - Process the status from a SPU response message.
 * @statp:  start of STATUS word
 * Return:
 *   0 - if status is good and response should be processed
 *   !0 - status indicates an error and response is invalid
 */
int spum_status_process(u8 *statp)
{
	u32 status;

	status = __be32_to_cpu(*(__be32 *)statp);
	flow_log("SPU response STATUS %#08x\n", status);
	if (status & SPU_STATUS_ERROR_FLAG) {
		pr_err("%s() Warning: Error result from SPU: %#08x\n",
		       __func__, status);
		if (status & SPU_STATUS_INVALID_ICV)
			return SPU_INVALID_ICV;
		return -EBADMSG;
	}
	return 0;
}

/**
 * spum_ccm_update_iv() - Update the IV as per the requirements for CCM mode.
 *
 * @digestsize:		Digest size of this request
 * @cipher_parms:	(pointer to) cipher parmaeters, includes IV buf & IV len
 * @assoclen:		Length of AAD data
 * @chunksize:		length of input data to be sent in this req
 * @is_encrypt:		true if this is an output/encrypt operation
 * @is_esp:		true if this is an ESP / RFC4309 operation
 *
 */
void spum_ccm_update_iv(unsigned int digestsize,
			struct spu_cipher_parms *cipher_parms,
			unsigned int assoclen,
			unsigned int chunksize,
			bool is_encrypt,
			bool is_esp)
{
	u8 L;		/* L from CCM algorithm, length of plaintext data */
	u8 mprime;	/* M' from CCM algo, (M - 2) / 2, where M=authsize */
	u8 adata;

	if (cipher_parms->iv_len != CCM_AES_IV_SIZE) {
		pr_err("%s(): Invalid IV len %d for CCM mode, should be %d\n",
		       __func__, cipher_parms->iv_len, CCM_AES_IV_SIZE);
		return;
	}

	/*
	 * IV needs to be formatted as follows:
	 *
	 * |          Byte 0               | Bytes 1 - N | Bytes (N+1) - 15 |
	 * | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | Bits 7 - 0  |    Bits 7 - 0    |
	 * | 0 |Ad?|(M - 2) / 2|   L - 1   |    Nonce    | Plaintext Length |
	 *
	 * Ad? = 1 if AAD present, 0 if not present
	 * M = size of auth field, 8, 12, or 16 bytes (SPU-M) -or-
	 *                         4, 6, 8, 10, 12, 14, 16 bytes (SPU2)
	 * L = Size of Plaintext Length field; Nonce size = 15 - L
	 *
	 * It appears that the crypto API already expects the L-1 portion
	 * to be set in the first byte of the IV, which implicitly determines
	 * the nonce size, and also fills in the nonce.  But the other bits
	 * in byte 0 as well as the plaintext length need to be filled in.
	 *
	 * In rfc4309/esp mode, L is not already in the supplied IV and
	 * we need to fill it in, as well as move the IV data to be after
	 * the salt
	 */
	if (is_esp) {
		L = CCM_ESP_L_VALUE;	/* RFC4309 has fixed L */
	} else {
		/* L' = plaintext length - 1 so Plaintext length is L' + 1 */
		L = ((cipher_parms->iv_buf[0] & CCM_B0_L_PRIME) >>
		      CCM_B0_L_PRIME_SHIFT) + 1;
	}

	mprime = (digestsize - 2) >> 1;  /* M' = (M - 2) / 2 */
	adata = (assoclen > 0);  /* adata = 1 if any associated data */

	cipher_parms->iv_buf[0] = (adata << CCM_B0_ADATA_SHIFT) |
				  (mprime << CCM_B0_M_PRIME_SHIFT) |
				  ((L - 1) << CCM_B0_L_PRIME_SHIFT);

	/* Nonce is already filled in by crypto API, and is 15 - L bytes */

	/* Don't include digest in plaintext size when decrypting */
	if (!is_encrypt)
		chunksize -= digestsize;

	/* Fill in length of plaintext, formatted to be L bytes long */
	format_value_ccm(chunksize, &cipher_parms->iv_buf[15 - L + 1], L);
}

/**
 * spum_wordalign_padlen() - Given the length of a data field, determine the
 * padding required to align the data following this field on a 4-byte boundary.
 * @data_size: length of data field in bytes
 *
 * Return: length of status field padding, in bytes
 */
u32 spum_wordalign_padlen(u32 data_size)
{
	return ((data_size + 3) & ~3) - data_size;
}

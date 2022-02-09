/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_REQ_H
#define __NITROX_REQ_H

#include <linux/dma-mapping.h>
#include <crypto/aes.h>

#include "nitrox_dev.h"

#define PENDING_SIG	0xFFFFFFFFFFFFFFFFUL
#define PRIO 4001

typedef void (*sereq_completion_t)(void *req, int err);

/**
 * struct gphdr - General purpose Header
 * @param0: first parameter.
 * @param1: second parameter.
 * @param2: third parameter.
 * @param3: fourth parameter.
 *
 * Params tell the iv and enc/dec data offsets.
 */
struct gphdr {
	__be16 param0;
	__be16 param1;
	__be16 param2;
	__be16 param3;
};

/**
 * struct se_req_ctrl - SE request information.
 * @arg: Minor number of the opcode
 * @ctxc: Context control.
 * @unca: Uncertainity enabled.
 * @info: Additional information for SE cores.
 * @ctxl: Context length in bytes.
 * @uddl: User defined data length
 */
union se_req_ctrl {
	u64 value;
	struct {
		u64 raz	: 22;
		u64 arg	: 8;
		u64 ctxc : 2;
		u64 unca : 1;
		u64 info : 3;
		u64 unc : 8;
		u64 ctxl : 12;
		u64 uddl : 8;
	} s;
};

#define MAX_IV_LEN 16

/**
 * struct se_crypto_request - SE crypto request structure.
 * @opcode: Request opcode (enc/dec)
 * @flags: flags from crypto subsystem
 * @ctx_handle: Crypto context handle.
 * @gph: GP Header
 * @ctrl: Request Information.
 * @orh: ORH address
 * @comp: completion address
 * @src: Input sglist
 * @dst: Output sglist
 */
struct se_crypto_request {
	u8 opcode;
	gfp_t gfp;
	u32 flags;
	u64 ctx_handle;

	struct gphdr gph;
	union se_req_ctrl ctrl;
	u64 *orh;
	u64 *comp;

	struct scatterlist *src;
	struct scatterlist *dst;
};

/* Crypto opcodes */
#define FLEXI_CRYPTO_ENCRYPT_HMAC	0x33
#define ENCRYPT	0
#define DECRYPT 1

/* IV from context */
#define IV_FROM_CTX	0
/* IV from Input data */
#define IV_FROM_DPTR	1

/**
 * cipher opcodes for firmware
 */
enum flexi_cipher {
	CIPHER_NULL = 0,
	CIPHER_3DES_CBC,
	CIPHER_3DES_ECB,
	CIPHER_AES_CBC,
	CIPHER_AES_ECB,
	CIPHER_AES_CFB,
	CIPHER_AES_CTR,
	CIPHER_AES_GCM,
	CIPHER_AES_XTS,
	CIPHER_AES_CCM,
	CIPHER_AES_CBC_CTS,
	CIPHER_AES_ECB_CTS,
	CIPHER_INVALID
};

enum flexi_auth {
	AUTH_NULL = 0,
	AUTH_MD5,
	AUTH_SHA1,
	AUTH_SHA2_SHA224,
	AUTH_SHA2_SHA256,
	AUTH_SHA2_SHA384,
	AUTH_SHA2_SHA512,
	AUTH_GMAC,
	AUTH_INVALID
};

/**
 * struct crypto_keys - Crypto keys
 * @key: Encryption key or KEY1 for AES-XTS
 * @iv: Encryption IV or Tweak for AES-XTS
 */
struct crypto_keys {
	union {
		u8 key[AES_MAX_KEY_SIZE];
		u8 key1[AES_MAX_KEY_SIZE];
	} u;
	u8 iv[AES_BLOCK_SIZE];
};

/**
 * struct auth_keys - Authentication keys
 * @ipad: IPAD or KEY2 for AES-XTS
 * @opad: OPAD or AUTH KEY if auth_input_type = 1
 */
struct auth_keys {
	union {
		u8 ipad[64];
		u8 key2[64];
	} u;
	u8 opad[64];
};

union fc_ctx_flags {
	__be64 f;
	u64 fu;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 cipher_type	: 4;
		u64 reserved_59	: 1;
		u64 aes_keylen : 2;
		u64 iv_source : 1;
		u64 hash_type : 4;
		u64 reserved_49_51 : 3;
		u64 auth_input_type: 1;
		u64 mac_len : 8;
		u64 reserved_0_39 : 40;
#else
		u64 reserved_0_39 : 40;
		u64 mac_len : 8;
		u64 auth_input_type: 1;
		u64 reserved_49_51 : 3;
		u64 hash_type : 4;
		u64 iv_source : 1;
		u64 aes_keylen : 2;
		u64 reserved_59	: 1;
		u64 cipher_type	: 4;
#endif
	} w0;
};
/**
 * struct flexi_crypto_context - Crypto context
 * @cipher_type: Encryption cipher type
 * @aes_keylen: AES key length
 * @iv_source: Encryption IV source
 * @hash_type: Authentication type
 * @auth_input_type: Authentication input type
 *   1 - Authentication IV and KEY, microcode calculates OPAD/IPAD
 *   0 - Authentication OPAD/IPAD
 * @mac_len: mac length
 * @crypto: Crypto keys
 * @auth: Authentication keys
 */
struct flexi_crypto_context {
	union fc_ctx_flags flags;
	struct crypto_keys crypto;
	struct auth_keys auth;
};

struct crypto_ctx_hdr {
	struct dma_pool *pool;
	dma_addr_t dma;
	void *vaddr;
};

struct nitrox_crypto_ctx {
	struct nitrox_device *ndev;
	union {
		u64 ctx_handle;
		struct flexi_crypto_context *fctx;
	} u;
	struct crypto_ctx_hdr *chdr;
	sereq_completion_t callback;
};

struct nitrox_kcrypt_request {
	struct se_crypto_request creq;
	u8 *src;
	u8 *dst;
	u8 *iv_out;
};

/**
 * struct nitrox_aead_rctx - AEAD request context
 * @nkreq: Base request context
 * @cryptlen: Encryption/Decryption data length
 * @assoclen: AAD length
 * @srclen: Input buffer length
 * @dstlen: Output buffer length
 * @iv: IV data
 * @ivsize: IV data length
 * @flags: AEAD req flags
 * @ctx_handle: Device context handle
 * @src: Source sglist
 * @dst: Destination sglist
 * @ctrl_arg: Identifies the request type (ENCRYPT/DECRYPT)
 */
struct nitrox_aead_rctx {
	struct nitrox_kcrypt_request nkreq;
	unsigned int cryptlen;
	unsigned int assoclen;
	unsigned int srclen;
	unsigned int dstlen;
	u8 *iv;
	int ivsize;
	u32 flags;
	u64 ctx_handle;
	struct scatterlist *src;
	struct scatterlist *dst;
	u8 ctrl_arg;
};

/**
 * struct nitrox_rfc4106_rctx - rfc4106 cipher request context
 * @base: AEAD request context
 * @src: Source sglist
 * @dst: Destination sglist
 * @assoc: AAD
 */
struct nitrox_rfc4106_rctx {
	struct nitrox_aead_rctx base;
	struct scatterlist src[3];
	struct scatterlist dst[3];
	u8 assoc[20];
};

/**
 * struct pkt_instr_hdr - Packet Instruction Header
 * @g: Gather used
 *   When [G] is set and [GSZ] != 0, the instruction is
 *   indirect gather instruction.
 *   When [G] is set and [GSZ] = 0, the instruction is
 *   direct gather instruction.
 * @gsz: Number of pointers in the indirect gather list
 * @ihi: When set hardware duplicates the 1st 8 bytes of pkt_instr_hdr
 *   and adds them to the packet after the pkt_instr_hdr but before any UDD
 * @ssz: Not used by the input hardware. But can become slc_store_int[SSZ]
 *   when [IHI] is set.
 * @fsz: The number of front data bytes directly included in the
 *   PCIe instruction.
 * @tlen: The length of the input packet in bytes, include:
 *   - 16B pkt_hdr
 *   - Inline context bytes if any,
 *   - UDD if any,
 *   - packet payload bytes
 */
union pkt_instr_hdr {
	__be64 bev;
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 raz_48_63 : 16;
		u64 g : 1;
		u64 gsz	: 7;
		u64 ihi	: 1;
		u64 ssz	: 7;
		u64 raz_30_31 : 2;
		u64 fsz	: 6;
		u64 raz_16_23 : 8;
		u64 tlen : 16;
#else
		u64 tlen : 16;
		u64 raz_16_23 : 8;
		u64 fsz	: 6;
		u64 raz_30_31 : 2;
		u64 ssz	: 7;
		u64 ihi	: 1;
		u64 gsz	: 7;
		u64 g : 1;
		u64 raz_48_63 : 16;
#endif
	} s;
};

/**
 * struct pkt_hdr - Packet Input Header
 * @opcode: Request opcode (Major)
 * @arg: Request opcode (Minor)
 * @ctxc: Context control.
 * @unca: When set [UNC] is the uncertainty count for an input packet.
 *        The hardware uses uncertainty counts to predict
 *        output buffer use and avoid deadlock.
 * @info: Not used by input hardware. Available for use
 *        during SE processing.
 * @destport: The expected destination port/ring/channel for the packet.
 * @unc: Uncertainty count for an input packet.
 * @grp: SE group that will process the input packet.
 * @ctxl: Context Length in 64-bit words.
 * @uddl: User-defined data (UDD) length in bytes.
 * @ctxp: Context pointer. CTXP<63,2:0> must be zero in all cases.
 */
union pkt_hdr {
	__be64 bev[2];
	u64 value[2];
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 opcode : 8;
		u64 arg	: 8;
		u64 ctxc : 2;
		u64 unca : 1;
		u64 raz_44 : 1;
		u64 info : 3;
		u64 destport : 9;
		u64 unc	: 8;
		u64 raz_19_23 : 5;
		u64 grp	: 3;
		u64 raz_15 : 1;
		u64 ctxl : 7;
		u64 uddl : 8;
#else
		u64 uddl : 8;
		u64 ctxl : 7;
		u64 raz_15 : 1;
		u64 grp	: 3;
		u64 raz_19_23 : 5;
		u64 unc	: 8;
		u64 destport : 9;
		u64 info : 3;
		u64 raz_44 : 1;
		u64 unca : 1;
		u64 ctxc : 2;
		u64 arg	: 8;
		u64 opcode : 8;
#endif
		__be64 ctxp;
	} s;
};

/**
 * struct slc_store_info - Solicited Paceket Output Store Information.
 * @ssz: The number of scatterlist pointers for the solicited output port
 *       packet.
 * @rptr: The result pointer for the solicited output port packet.
 *        If [SSZ]=0, [RPTR] must point directly to a buffer on the remote
 *        host that is large enough to hold the entire output packet.
 *        If [SSZ]!=0, [RPTR] must point to an array of ([SSZ]+3)/4
 *        sglist components at [RPTR] on the remote host.
 */
union slc_store_info {
	__be64 bev[2];
	u64 value[2];
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 raz_39_63 : 25;
		u64 ssz	: 7;
		u64 raz_0_31 : 32;
#else
		u64 raz_0_31 : 32;
		u64 ssz	: 7;
		u64 raz_39_63 : 25;
#endif
		__be64 rptr;
	} s;
};

/**
 * struct nps_pkt_instr - NPS Packet Instruction of SE cores.
 * @dptr0 : Input pointer points to buffer in remote host.
 * @ih: Packet Instruction Header (8 bytes)
 * @irh: Packet Input Header (16 bytes)
 * @slc: Solicited Packet Output Store Information (16 bytes)
 * @fdata: Front data
 *
 * 64-Byte Instruction Format
 */
struct nps_pkt_instr {
	__be64 dptr0;
	union pkt_instr_hdr ih;
	union pkt_hdr irh;
	union slc_store_info slc;
	u64 fdata[2];
};

/**
 * struct aqmq_command_s - The 32 byte command for AE processing.
 * @opcode: Request opcode
 * @param1: Request control parameter 1
 * @param2: Request control parameter 2
 * @dlen: Input length
 * @dptr: Input pointer points to buffer in remote host
 * @rptr: Result pointer points to buffer in remote host
 * @grp: AQM Group (0..7)
 * @cptr: Context pointer
 */
struct aqmq_command_s {
	__be16 opcode;
	__be16 param1;
	__be16 param2;
	__be16 dlen;
	__be64 dptr;
	__be64 rptr;
	union {
		__be64 word3;
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 grp : 3;
		u64 cptr : 61;
#else
		u64 cptr : 61;
		u64 grp : 3;
#endif
	};
};

/**
 * struct ctx_hdr - Book keeping data about the crypto context
 * @pool: Pool used to allocate crypto context
 * @dma: Base DMA address of the crypto context
 * @ctx_dma: Actual usable crypto context for NITROX
 */
struct ctx_hdr {
	struct dma_pool *pool;
	dma_addr_t dma;
	dma_addr_t ctx_dma;
};

/*
 * struct sglist_component - SG list component format
 * @len0: The number of bytes at [PTR0] on the remote host.
 * @len1: The number of bytes at [PTR1] on the remote host.
 * @len2: The number of bytes at [PTR2] on the remote host.
 * @len3: The number of bytes at [PTR3] on the remote host.
 * @dma0: First pointer point to buffer in remote host.
 * @dma1: Second pointer point to buffer in remote host.
 * @dma2: Third pointer point to buffer in remote host.
 * @dma3: Fourth pointer point to buffer in remote host.
 */
struct nitrox_sgcomp {
	__be16 len[4];
	__be64 dma[4];
};

/*
 * strutct nitrox_sgtable - SG list information
 * @sgmap_cnt: Number of buffers mapped
 * @total_bytes: Total bytes in sglist.
 * @sgcomp_len: Total sglist components length.
 * @sgcomp_dma: DMA address of sglist component.
 * @sg: crypto request buffer.
 * @sgcomp: sglist component for NITROX.
 */
struct nitrox_sgtable {
	u8 sgmap_cnt;
	u16 total_bytes;
	u32 sgcomp_len;
	dma_addr_t sgcomp_dma;
	struct scatterlist *sg;
	struct nitrox_sgcomp *sgcomp;
};

/* Response Header Length */
#define ORH_HLEN	8
/* Completion bytes Length */
#define COMP_HLEN	8

struct resp_hdr {
	u64 *orh;
	u64 *completion;
};

typedef void (*completion_t)(void *arg, int err);

/**
 * struct nitrox_softreq - Represents the NIROX Request.
 * @response: response list entry
 * @backlog: Backlog list entry
 * @ndev: Device used to submit the request
 * @cmdq: Command queue for submission
 * @resp: Response headers
 * @instr: 64B instruction
 * @in: SG table for input
 * @out SG table for output
 * @tstamp: Request submitted time in jiffies
 * @callback: callback after request completion/timeout
 * @cb_arg: callback argument
 */
struct nitrox_softreq {
	struct list_head response;
	struct list_head backlog;

	u32 flags;
	gfp_t gfp;
	atomic_t status;

	struct nitrox_device *ndev;
	struct nitrox_cmdq *cmdq;

	struct nps_pkt_instr instr;
	struct resp_hdr resp;
	struct nitrox_sgtable in;
	struct nitrox_sgtable out;

	unsigned long tstamp;

	completion_t callback;
	void *cb_arg;
};

static inline int flexi_aes_keylen(int keylen)
{
	int aes_keylen;

	switch (keylen) {
	case AES_KEYSIZE_128:
		aes_keylen = 1;
		break;
	case AES_KEYSIZE_192:
		aes_keylen = 2;
		break;
	case AES_KEYSIZE_256:
		aes_keylen = 3;
		break;
	default:
		aes_keylen = -EINVAL;
		break;
	}
	return aes_keylen;
}

static inline void *alloc_req_buf(int nents, int extralen, gfp_t gfp)
{
	size_t size;

	size = sizeof(struct scatterlist) * nents;
	size += extralen;

	return kzalloc(size, gfp);
}

/**
 * create_single_sg - Point SG entry to the data
 * @sg:		Destination SG list
 * @buf:	Data
 * @buflen:	Data length
 *
 * Returns next free entry in the destination SG list
 **/
static inline struct scatterlist *create_single_sg(struct scatterlist *sg,
						   void *buf, int buflen)
{
	sg_set_buf(sg, buf, buflen);
	sg++;
	return sg;
}

/**
 * create_multi_sg - Create multiple sg entries with buflen data length from
 *		     source sglist
 * @to_sg:	Destination SG list
 * @from_sg:	Source SG list
 * @buflen:	Data length
 *
 * Returns next free entry in the destination SG list
 **/
static inline struct scatterlist *create_multi_sg(struct scatterlist *to_sg,
						  struct scatterlist *from_sg,
						  int buflen)
{
	struct scatterlist *sg = to_sg;
	unsigned int sglen;

	for (; buflen && from_sg; buflen -= sglen) {
		sglen = from_sg->length;
		if (sglen > buflen)
			sglen = buflen;

		sg_set_buf(sg, sg_virt(from_sg), sglen);
		from_sg = sg_next(from_sg);
		sg++;
	}

	return sg;
}

static inline void set_orh_value(u64 *orh)
{
	WRITE_ONCE(*orh, PENDING_SIG);
}

static inline void set_comp_value(u64 *comp)
{
	WRITE_ONCE(*comp, PENDING_SIG);
}

static inline int alloc_src_req_buf(struct nitrox_kcrypt_request *nkreq,
				    int nents, int ivsize)
{
	struct se_crypto_request *creq = &nkreq->creq;

	nkreq->src = alloc_req_buf(nents, ivsize, creq->gfp);
	if (!nkreq->src)
		return -ENOMEM;

	return 0;
}

static inline void nitrox_creq_copy_iv(char *dst, char *src, int size)
{
	memcpy(dst, src, size);
}

static inline struct scatterlist *nitrox_creq_src_sg(char *iv, int ivsize)
{
	return (struct scatterlist *)(iv + ivsize);
}

static inline void nitrox_creq_set_src_sg(struct nitrox_kcrypt_request *nkreq,
					  int nents, int ivsize,
					  struct scatterlist *src, int buflen)
{
	char *iv = nkreq->src;
	struct scatterlist *sg;
	struct se_crypto_request *creq = &nkreq->creq;

	creq->src = nitrox_creq_src_sg(iv, ivsize);
	sg = creq->src;
	sg_init_table(sg, nents);

	/* Input format:
	 * +----+----------------+
	 * | IV | SRC sg entries |
	 * +----+----------------+
	 */

	/* IV */
	sg = create_single_sg(sg, iv, ivsize);
	/* SRC entries */
	create_multi_sg(sg, src, buflen);
}

static inline int alloc_dst_req_buf(struct nitrox_kcrypt_request *nkreq,
				    int nents)
{
	int extralen = ORH_HLEN + COMP_HLEN;
	struct se_crypto_request *creq = &nkreq->creq;

	nkreq->dst = alloc_req_buf(nents, extralen, creq->gfp);
	if (!nkreq->dst)
		return -ENOMEM;

	return 0;
}

static inline void nitrox_creq_set_orh(struct nitrox_kcrypt_request *nkreq)
{
	struct se_crypto_request *creq = &nkreq->creq;

	creq->orh = (u64 *)(nkreq->dst);
	set_orh_value(creq->orh);
}

static inline void nitrox_creq_set_comp(struct nitrox_kcrypt_request *nkreq)
{
	struct se_crypto_request *creq = &nkreq->creq;

	creq->comp = (u64 *)(nkreq->dst + ORH_HLEN);
	set_comp_value(creq->comp);
}

static inline struct scatterlist *nitrox_creq_dst_sg(char *dst)
{
	return (struct scatterlist *)(dst + ORH_HLEN + COMP_HLEN);
}

static inline void nitrox_creq_set_dst_sg(struct nitrox_kcrypt_request *nkreq,
					  int nents, int ivsize,
					  struct scatterlist *dst, int buflen)
{
	struct se_crypto_request *creq = &nkreq->creq;
	struct scatterlist *sg;
	char *iv = nkreq->src;

	creq->dst = nitrox_creq_dst_sg(nkreq->dst);
	sg = creq->dst;
	sg_init_table(sg, nents);

	/* Output format:
	 * +-----+----+----------------+-----------------+
	 * | ORH | IV | DST sg entries | COMPLETION Bytes|
	 * +-----+----+----------------+-----------------+
	 */

	/* ORH */
	sg = create_single_sg(sg, creq->orh, ORH_HLEN);
	/* IV */
	sg = create_single_sg(sg, iv, ivsize);
	/* DST entries */
	sg = create_multi_sg(sg, dst, buflen);
	/* COMPLETION Bytes */
	create_single_sg(sg, creq->comp, COMP_HLEN);
}

#endif /* __NITROX_REQ_H */

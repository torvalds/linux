/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2016 Broadcom
 */

#ifndef _CIPHER_H
#define _CIPHER_H

#include <linux/atomic.h>
#include <linux/mailbox/brcm-message.h>
#include <linux/mailbox_client.h>
#include <crypto/aes.h>
#include <crypto/internal/hash.h>
#include <crypto/aead.h>
#include <crypto/arc4.h>
#include <crypto/gcm.h>
#include <crypto/sha.h>
#include <crypto/sha3.h>

#include "spu.h"
#include "spum.h"
#include "spu2.h"

/* Driver supports up to MAX_SPUS SPU blocks */
#define MAX_SPUS 16

#define ARC4_STATE_SIZE     4

#define CCM_AES_IV_SIZE    16
#define CCM_ESP_IV_SIZE     8
#define RFC4543_ICV_SIZE   16

#define MAX_KEY_SIZE	ARC4_MAX_KEY_SIZE
#define MAX_IV_SIZE	AES_BLOCK_SIZE
#define MAX_DIGEST_SIZE	SHA3_512_DIGEST_SIZE
#define MAX_ASSOC_SIZE	512

/* size of salt value for AES-GCM-ESP and AES-CCM-ESP */
#define GCM_ESP_SALT_SIZE   4
#define CCM_ESP_SALT_SIZE   3
#define MAX_SALT_SIZE       GCM_ESP_SALT_SIZE
#define GCM_ESP_SALT_OFFSET 0
#define CCM_ESP_SALT_OFFSET 1

#define GCM_ESP_DIGESTSIZE 16

#define MAX_HASH_BLOCK_SIZE SHA512_BLOCK_SIZE

/*
 * Maximum number of bytes from a non-final hash request that can be deferred
 * until more data is available. With new crypto API framework, this
 * can be no more than one block of data.
 */
#define HASH_CARRY_MAX  MAX_HASH_BLOCK_SIZE

/* Force at least 4-byte alignment of all SPU message fields */
#define SPU_MSG_ALIGN  4

/* Number of times to resend mailbox message if mb queue is full */
#define SPU_MB_RETRY_MAX  1000

/* op_counts[] indexes */
enum op_type {
	SPU_OP_CIPHER,
	SPU_OP_HASH,
	SPU_OP_HMAC,
	SPU_OP_AEAD,
	SPU_OP_NUM
};

enum spu_spu_type {
	SPU_TYPE_SPUM,
	SPU_TYPE_SPU2,
};

/*
 * SPUM_NS2 and SPUM_NSP are the SPU-M block on Northstar 2 and Northstar Plus,
 * respectively.
 */
enum spu_spu_subtype {
	SPU_SUBTYPE_SPUM_NS2,
	SPU_SUBTYPE_SPUM_NSP,
	SPU_SUBTYPE_SPU2_V1,
	SPU_SUBTYPE_SPU2_V2
};

struct spu_type_subtype {
	enum spu_spu_type type;
	enum spu_spu_subtype subtype;
};

struct cipher_op {
	enum spu_cipher_alg alg;
	enum spu_cipher_mode mode;
};

struct auth_op {
	enum hash_alg alg;
	enum hash_mode mode;
};

struct iproc_alg_s {
	u32 type;
	union {
		struct crypto_alg crypto;
		struct ahash_alg hash;
		struct aead_alg aead;
	} alg;
	struct cipher_op cipher_info;
	struct auth_op auth_info;
	bool auth_first;
	bool registered;
};

/*
 * Buffers for a SPU request/reply message pair. All part of one structure to
 * allow a single alloc per request.
 */
struct spu_msg_buf {
	/* Request message fragments */

	/*
	 * SPU request message header. For SPU-M, holds MH, EMH, SCTX, BDESC,
	 * and BD header. For SPU2, holds FMD, OMD.
	 */
	u8 bcm_spu_req_hdr[ALIGN(SPU2_HEADER_ALLOC_LEN, SPU_MSG_ALIGN)];

	/* IV or counter. Size to include salt. Also used for XTS tweek. */
	u8 iv_ctr[ALIGN(2 * AES_BLOCK_SIZE, SPU_MSG_ALIGN)];

	/* Hash digest. request and response. */
	u8 digest[ALIGN(MAX_DIGEST_SIZE, SPU_MSG_ALIGN)];

	/* SPU request message padding */
	u8 spu_req_pad[ALIGN(SPU_PAD_LEN_MAX, SPU_MSG_ALIGN)];

	/* SPU-M request message STATUS field */
	u8 tx_stat[ALIGN(SPU_TX_STATUS_LEN, SPU_MSG_ALIGN)];

	/* Response message fragments */

	/* SPU response message header */
	u8 spu_resp_hdr[ALIGN(SPU2_HEADER_ALLOC_LEN, SPU_MSG_ALIGN)];

	/* SPU response message STATUS field padding */
	u8 rx_stat_pad[ALIGN(SPU_STAT_PAD_MAX, SPU_MSG_ALIGN)];

	/* SPU response message STATUS field */
	u8 rx_stat[ALIGN(SPU_RX_STATUS_LEN, SPU_MSG_ALIGN)];

	union {
		/* Buffers only used for ablkcipher */
		struct {
			/*
			 * Field used for either SUPDT when RC4 is used
			 * -OR- tweak value when XTS/AES is used
			 */
			u8 supdt_tweak[ALIGN(SPU_SUPDT_LEN, SPU_MSG_ALIGN)];
		} c;

		/* Buffers only used for aead */
		struct {
			/* SPU response pad for GCM data */
			u8 gcmpad[ALIGN(AES_BLOCK_SIZE, SPU_MSG_ALIGN)];

			/* SPU request msg padding for GCM AAD */
			u8 req_aad_pad[ALIGN(SPU_PAD_LEN_MAX, SPU_MSG_ALIGN)];

			/* SPU response data to be discarded */
			u8 resp_aad[ALIGN(MAX_ASSOC_SIZE + MAX_IV_SIZE,
					  SPU_MSG_ALIGN)];
		} a;
	};
};

struct iproc_ctx_s {
	u8 enckey[MAX_KEY_SIZE + ARC4_STATE_SIZE];
	unsigned int enckeylen;

	u8 authkey[MAX_KEY_SIZE + ARC4_STATE_SIZE];
	unsigned int authkeylen;

	u8 salt[MAX_SALT_SIZE];
	unsigned int salt_len;
	unsigned int salt_offset;
	u8 iv[MAX_IV_SIZE];

	unsigned int digestsize;

	struct iproc_alg_s *alg;
	bool is_esp;

	struct cipher_op cipher;
	enum spu_cipher_type cipher_type;

	struct auth_op auth;
	bool auth_first;

	/*
	 * The maximum length in bytes of the payload in a SPU message for this
	 * context. For SPU-M, the payload is the combination of AAD and data.
	 * For SPU2, the payload is just data. A value of SPU_MAX_PAYLOAD_INF
	 * indicates that there is no limit to the length of the SPU message
	 * payload.
	 */
	unsigned int max_payload;

	struct crypto_aead *fallback_cipher;

	/* auth_type is determined during processing of request */

	u8 ipad[MAX_HASH_BLOCK_SIZE];
	u8 opad[MAX_HASH_BLOCK_SIZE];

	/*
	 * Buffer to hold SPU message header template. Template is created at
	 * setkey time for ablkcipher requests, since most of the fields in the
	 * header are known at that time. At request time, just fill in a few
	 * missing pieces related to length of data in the request and IVs, etc.
	 */
	u8 bcm_spu_req_hdr[ALIGN(SPU2_HEADER_ALLOC_LEN, SPU_MSG_ALIGN)];

	/* Length of SPU request header */
	u16 spu_req_hdr_len;

	/* Expected length of SPU response header */
	u16 spu_resp_hdr_len;

	/*
	 * shash descriptor - needed to perform incremental hashing in
	 * in software, when hw doesn't support it.
	 */
	struct shash_desc *shash;

	bool is_rfc4543;	/* RFC 4543 style of GMAC */
};

/* state from iproc_reqctx_s necessary for hash state export/import */
struct spu_hash_export_s {
	unsigned int total_todo;
	unsigned int total_sent;
	u8 hash_carry[HASH_CARRY_MAX];
	unsigned int hash_carry_len;
	u8 incr_hash[MAX_DIGEST_SIZE];
	bool is_sw_hmac;
};

struct iproc_reqctx_s {
	/* general context */
	struct crypto_async_request *parent;

	/* only valid after enqueue() */
	struct iproc_ctx_s *ctx;

	u8 chan_idx;   /* Mailbox channel to be used to submit this request */

	/* total todo, rx'd, and sent for this request */
	unsigned int total_todo;
	unsigned int total_received;	/* only valid for ablkcipher */
	unsigned int total_sent;

	/*
	 * num bytes sent to hw from the src sg in this request. This can differ
	 * from total_sent for incremental hashing. total_sent includes previous
	 * init() and update() data. src_sent does not.
	 */
	unsigned int src_sent;

	/*
	 * For AEAD requests, start of associated data. This will typically
	 * point to the beginning of the src scatterlist from the request,
	 * since assoc data is at the beginning of the src scatterlist rather
	 * than in its own sg.
	 */
	struct scatterlist *assoc;

	/*
	 * scatterlist entry and offset to start of data for next chunk. Crypto
	 * API src scatterlist for AEAD starts with AAD, if present. For first
	 * chunk, src_sg is sg entry at beginning of input data (after AAD).
	 * src_skip begins at the offset in that sg entry where data begins.
	 */
	struct scatterlist *src_sg;
	int src_nents;		/* Number of src entries with data */
	u32 src_skip;		/* bytes of current sg entry already used */

	/*
	 * Same for destination. For AEAD, if there is AAD, output data must
	 * be written at offset following AAD.
	 */
	struct scatterlist *dst_sg;
	int dst_nents;		/* Number of dst entries with data */
	u32 dst_skip;		/* bytes of current sg entry already written */

	/* Mailbox message used to send this request to PDC driver */
	struct brcm_message mb_mssg;

	bool bd_suppress;	/* suppress BD field in SPU response? */

	/* cipher context */
	bool is_encrypt;

	/*
	 * CBC mode: IV.  CTR mode: counter.  Else empty. Used as a DMA
	 * buffer for AEAD requests. So allocate as DMAable memory. If IV
	 * concatenated with salt, includes the salt.
	 */
	u8 *iv_ctr;
	/* Length of IV or counter, in bytes */
	unsigned int iv_ctr_len;

	/*
	 * Hash requests can be of any size, whether initial, update, or final.
	 * A non-final request must be submitted to the SPU as an integral
	 * number of blocks. This may leave data at the end of the request
	 * that is not a full block. Since the request is non-final, it cannot
	 * be padded. So, we write the remainder to this hash_carry buffer and
	 * hold it until the next request arrives. The carry data is then
	 * submitted at the beginning of the data in the next SPU msg.
	 * hash_carry_len is the number of bytes currently in hash_carry. These
	 * fields are only used for ahash requests.
	 */
	u8 hash_carry[HASH_CARRY_MAX];
	unsigned int hash_carry_len;
	unsigned int is_final;	/* is this the final for the hash op? */

	/*
	 * Digest from incremental hash is saved here to include in next hash
	 * operation. Cannot be stored in req->result for truncated hashes,
	 * since result may be sized for final digest. Cannot be saved in
	 * msg_buf because that gets deleted between incremental hash ops
	 * and is not saved as part of export().
	 */
	u8 incr_hash[MAX_DIGEST_SIZE];

	/* hmac context */
	bool is_sw_hmac;

	/* aead context */
	struct crypto_tfm *old_tfm;
	crypto_completion_t old_complete;
	void *old_data;

	gfp_t gfp;

	/* Buffers used to build SPU request and response messages */
	struct spu_msg_buf msg_buf;
};

/*
 * Structure encapsulates a set of function pointers specific to the type of
 * SPU hardware running. These functions handling creation and parsing of
 * SPU request messages and SPU response messages. Includes hardware-specific
 * values read from device tree.
 */
struct spu_hw {
	void (*spu_dump_msg_hdr)(u8 *buf, unsigned int buf_len);
	u32 (*spu_ctx_max_payload)(enum spu_cipher_alg cipher_alg,
				   enum spu_cipher_mode cipher_mode,
				   unsigned int blocksize);
	u32 (*spu_payload_length)(u8 *spu_hdr);
	u16 (*spu_response_hdr_len)(u16 auth_key_len, u16 enc_key_len,
				    bool is_hash);
	u16 (*spu_hash_pad_len)(enum hash_alg hash_alg,
				enum hash_mode hash_mode, u32 chunksize,
				u16 hash_block_size);
	u32 (*spu_gcm_ccm_pad_len)(enum spu_cipher_mode cipher_mode,
				   unsigned int data_size);
	u32 (*spu_assoc_resp_len)(enum spu_cipher_mode cipher_mode,
				  unsigned int assoc_len,
				  unsigned int iv_len, bool is_encrypt);
	u8 (*spu_aead_ivlen)(enum spu_cipher_mode cipher_mode,
			     u16 iv_len);
	enum hash_type (*spu_hash_type)(u32 src_sent);
	u32 (*spu_digest_size)(u32 digest_size, enum hash_alg alg,
			       enum hash_type);
	u32 (*spu_create_request)(u8 *spu_hdr,
				  struct spu_request_opts *req_opts,
				  struct spu_cipher_parms *cipher_parms,
				  struct spu_hash_parms *hash_parms,
				  struct spu_aead_parms *aead_parms,
				  unsigned int data_size);
	u16 (*spu_cipher_req_init)(u8 *spu_hdr,
				   struct spu_cipher_parms *cipher_parms);
	void (*spu_cipher_req_finish)(u8 *spu_hdr,
				      u16 spu_req_hdr_len,
				      unsigned int is_inbound,
				      struct spu_cipher_parms *cipher_parms,
				      bool update_key,
				      unsigned int data_size);
	void (*spu_request_pad)(u8 *pad_start, u32 gcm_padding,
				u32 hash_pad_len, enum hash_alg auth_alg,
				enum hash_mode auth_mode,
				unsigned int total_sent, u32 status_padding);
	u8 (*spu_xts_tweak_in_payload)(void);
	u8 (*spu_tx_status_len)(void);
	u8 (*spu_rx_status_len)(void);
	int (*spu_status_process)(u8 *statp);
	void (*spu_ccm_update_iv)(unsigned int digestsize,
				  struct spu_cipher_parms *cipher_parms,
				  unsigned int assoclen, unsigned int chunksize,
				  bool is_encrypt, bool is_esp);
	u32 (*spu_wordalign_padlen)(u32 data_size);

	/* The base virtual address of the SPU hw registers */
	void __iomem *reg_vbase[MAX_SPUS];

	/* Version of the SPU hardware */
	enum spu_spu_type spu_type;

	/* Sub-version of the SPU hardware */
	enum spu_spu_subtype spu_subtype;

	/* The number of SPUs on this platform */
	u32 num_spu;

	/* The number of SPU channels on this platform */
	u32 num_chan;
};

struct device_private {
	struct platform_device *pdev;

	struct spu_hw spu;

	atomic_t session_count;	/* number of streams active */
	atomic_t stream_count;	/* monotonic counter for streamID's */

	/* Length of BCM header. Set to 0 when hw does not expect BCM HEADER. */
	u8 bcm_hdr_len;

	/* The index of the channel to use for the next crypto request */
	atomic_t next_chan;

	struct dentry *debugfs_dir;
	struct dentry *debugfs_stats;

	/* Number of request bytes processed and result bytes returned */
	atomic64_t bytes_in;
	atomic64_t bytes_out;

	/* Number of operations of each type */
	atomic_t op_counts[SPU_OP_NUM];

	atomic_t cipher_cnt[CIPHER_ALG_LAST][CIPHER_MODE_LAST];
	atomic_t hash_cnt[HASH_ALG_LAST];
	atomic_t hmac_cnt[HASH_ALG_LAST];
	atomic_t aead_cnt[AEAD_TYPE_LAST];

	/* Number of calls to setkey() for each operation type */
	atomic_t setkey_cnt[SPU_OP_NUM];

	/* Number of times request was resubmitted because mb was full */
	atomic_t mb_no_spc;

	/* Number of mailbox send failures */
	atomic_t mb_send_fail;

	/* Number of ICV check failures for AEAD messages */
	atomic_t bad_icv;

	struct mbox_client mcl;

	/* Array of mailbox channel pointers, one for each channel */
	struct mbox_chan **mbox;
};

extern struct device_private iproc_priv;

#endif

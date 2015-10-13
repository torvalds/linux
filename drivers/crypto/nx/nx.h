
#ifndef __NX_H__
#define __NX_H__

#include <crypto/ctr.h>

#define NX_NAME		"nx-crypto"
#define NX_STRING	"IBM Power7+ Nest Accelerator Crypto Driver"
#define NX_VERSION	"1.0"

static const char nx_driver_string[] = NX_STRING;
static const char nx_driver_version[] = NX_VERSION;

/* a scatterlist in the format PHYP is expecting */
struct nx_sg {
	u64 addr;
	u32 rsvd;
	u32 len;
} __attribute((packed));

#define NX_PAGE_SIZE		(4096)
#define NX_MAX_SG_ENTRIES	(NX_PAGE_SIZE/(sizeof(struct nx_sg)))

enum nx_status {
	NX_DISABLED,
	NX_WAITING,
	NX_OKAY
};

/* msc_triplet and max_sync_cop are used only to assist in parsing the
 * openFirmware property */
struct msc_triplet {
	u32 keybitlen;
	u32 databytelen;
	u32 sglen;
} __packed;

struct max_sync_cop {
	u32 fc;
	u32 mode;
	u32 triplets;
	struct msc_triplet trip[0];
} __packed;

struct alg_props {
	u32 databytelen;
	u32 sglen;
};

#define NX_OF_FLAG_MAXSGLEN_SET		(1)
#define NX_OF_FLAG_STATUS_SET		(2)
#define NX_OF_FLAG_MAXSYNCCOP_SET	(4)
#define NX_OF_FLAG_MASK_READY		(NX_OF_FLAG_MAXSGLEN_SET | \
					 NX_OF_FLAG_STATUS_SET |   \
					 NX_OF_FLAG_MAXSYNCCOP_SET)
struct nx_of {
	u32 flags;
	u32 max_sg_len;
	enum nx_status status;
	struct alg_props ap[NX_MAX_FC][NX_MAX_MODE][3];
};

struct nx_stats {
	atomic_t aes_ops;
	atomic64_t aes_bytes;
	atomic_t sha256_ops;
	atomic64_t sha256_bytes;
	atomic_t sha512_ops;
	atomic64_t sha512_bytes;

	atomic_t sync_ops;

	atomic_t errors;
	atomic_t last_error;
	atomic_t last_error_pid;
};

struct nx_debugfs {
	struct dentry *dfs_root;
	struct dentry *dfs_aes_ops, *dfs_aes_bytes;
	struct dentry *dfs_sha256_ops, *dfs_sha256_bytes;
	struct dentry *dfs_sha512_ops, *dfs_sha512_bytes;
	struct dentry *dfs_errors, *dfs_last_error, *dfs_last_error_pid;
};

struct nx_crypto_driver {
	struct nx_stats    stats;
	struct nx_of       of;
	struct vio_dev    *viodev;
	struct vio_driver  viodriver;
	struct nx_debugfs  dfs;
};

#define NX_GCM4106_NONCE_LEN		(4)
#define NX_GCM_CTR_OFFSET		(12)
struct nx_gcm_rctx {
	u8 iv[16];
};

struct nx_gcm_priv {
	u8 iauth_tag[16];
	u8 nonce[NX_GCM4106_NONCE_LEN];
};

#define NX_CCM_AES_KEY_LEN		(16)
#define NX_CCM4309_AES_KEY_LEN		(19)
#define NX_CCM4309_NONCE_LEN		(3)
struct nx_ccm_rctx {
	u8 iv[16];
};

struct nx_ccm_priv {
	u8 b0[16];
	u8 iauth_tag[16];
	u8 oauth_tag[16];
	u8 nonce[NX_CCM4309_NONCE_LEN];
};

struct nx_xcbc_priv {
	u8 key[16];
};

struct nx_ctr_priv {
	u8 nonce[CTR_RFC3686_NONCE_SIZE];
};

struct nx_crypto_ctx {
	spinlock_t lock;	  /* synchronize access to the context */
	void *kmem;		  /* unaligned, kmalloc'd buffer */
	size_t kmem_len;	  /* length of kmem */
	struct nx_csbcpb *csbcpb; /* aligned page given to phyp @ hcall time */
	struct vio_pfo_op op;     /* operation struct with hcall parameters */
	struct nx_csbcpb *csbcpb_aead; /* secondary csbcpb used by AEAD algs */
	struct vio_pfo_op op_aead;/* operation struct for csbcpb_aead */

	struct nx_sg *in_sg;      /* aligned pointer into kmem to an sg list */
	struct nx_sg *out_sg;     /* aligned pointer into kmem to an sg list */

	struct alg_props *ap;	  /* pointer into props based on our key size */
	struct alg_props props[3];/* openFirmware properties for requests */
	struct nx_stats *stats;   /* pointer into an nx_crypto_driver for stats
				     reporting */

	union {
		struct nx_gcm_priv gcm;
		struct nx_ccm_priv ccm;
		struct nx_xcbc_priv xcbc;
		struct nx_ctr_priv ctr;
	} priv;
};

struct crypto_aead;

/* prototypes */
int nx_crypto_ctx_aes_ccm_init(struct crypto_aead *tfm);
int nx_crypto_ctx_aes_gcm_init(struct crypto_aead *tfm);
int nx_crypto_ctx_aes_xcbc_init(struct crypto_tfm *tfm);
int nx_crypto_ctx_aes_ctr_init(struct crypto_tfm *tfm);
int nx_crypto_ctx_aes_cbc_init(struct crypto_tfm *tfm);
int nx_crypto_ctx_aes_ecb_init(struct crypto_tfm *tfm);
int nx_crypto_ctx_sha_init(struct crypto_tfm *tfm);
void nx_crypto_ctx_exit(struct crypto_tfm *tfm);
void nx_crypto_ctx_aead_exit(struct crypto_aead *tfm);
void nx_ctx_init(struct nx_crypto_ctx *nx_ctx, unsigned int function);
int nx_hcall_sync(struct nx_crypto_ctx *ctx, struct vio_pfo_op *op,
		  u32 may_sleep);
struct nx_sg *nx_build_sg_list(struct nx_sg *, u8 *, unsigned int *, u32);
int nx_build_sg_lists(struct nx_crypto_ctx *, struct blkcipher_desc *,
		      struct scatterlist *, struct scatterlist *, unsigned int *,
		      unsigned int, u8 *);
struct nx_sg *nx_walk_and_build(struct nx_sg *, unsigned int,
				struct scatterlist *, unsigned int,
				unsigned int *);

#ifdef CONFIG_DEBUG_FS
#define NX_DEBUGFS_INIT(drv)	nx_debugfs_init(drv)
#define NX_DEBUGFS_FINI(drv)	nx_debugfs_fini(drv)

int nx_debugfs_init(struct nx_crypto_driver *);
void nx_debugfs_fini(struct nx_crypto_driver *);
#else
#define NX_DEBUGFS_INIT(drv)	(0)
#define NX_DEBUGFS_FINI(drv)	(0)
#endif

#define NX_PAGE_NUM(x)		((u64)(x) & 0xfffffffffffff000ULL)

extern struct crypto_alg nx_cbc_aes_alg;
extern struct crypto_alg nx_ecb_aes_alg;
extern struct aead_alg nx_gcm_aes_alg;
extern struct aead_alg nx_gcm4106_aes_alg;
extern struct crypto_alg nx_ctr3686_aes_alg;
extern struct aead_alg nx_ccm_aes_alg;
extern struct aead_alg nx_ccm4309_aes_alg;
extern struct shash_alg nx_shash_aes_xcbc_alg;
extern struct shash_alg nx_shash_sha512_alg;
extern struct shash_alg nx_shash_sha256_alg;

extern struct nx_crypto_driver nx_driver;

#define SCATTERWALK_TO_SG	1
#define SCATTERWALK_FROM_SG	0

#endif

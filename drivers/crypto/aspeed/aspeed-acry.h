#ifndef __ASPEED_ACRY_H__
#define __ASPEED_ACRY_H__

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/count_zeros.h>
#include <linux/err.h>
// #include <linux/mpi.h>
#include <linux/fips.h>
#include <linux/dma-mapping.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/kpp.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/rng.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <crypto/akcipher.h>
#include <crypto/algapi.h>
#include <crypto/ecdh.h>
// #include <crypto/ecc.h>
// #include <crypto/ecdsa.h>

/* G6 RSA/ECDH */
#define ASPEED_ACRY_TRIGGER		0x000
#define  ACRY_CMD_RSA_TRIGGER		BIT(0)
#define  ACRY_CMD_DMA_RSA_TRIGGER	BIT(1)
#define  ACRY_CMD_ECC_TRIGGER		BIT(4)
#define  ACRY_CMD_DMA_ECC_PROG		BIT(5)
#define  ACRY_CMD_DMA_ECC_DATA		BIT(6)
#define ASPEED_ACRY_PROGRAM_INDEX	0x004
#define ASPEED_ACRY_ECC_P		0x024
#define  ACRY_ECC_LEN_192		(0xc0 << 16)
#define  ACRY_ECC_LEN_224		(0xe0 << 16)
#define  ACRY_ECC_LEN_256		(0x100 << 16)
#define  ACRY_ECC_LEN_384		(0x180 << 16)
#define ASPEED_ACRY_CONTROL		0x044
#define  ACRY_ECC_P192			0
#define  ACRY_ECC_P224			(0x1 << 4)
#define  ACRY_ECC_P256			(0x2 << 4)
#define  ACRY_ECC_P384			(0x3 << 4)
#define ASPEED_ACRY_DMA_CMD		0x048
#define  ACRY_CMD_DMA_SRAM_MODE_ECC	(0x2 << 4)
#define  ACRY_CMD_DMA_SRAM_MODE_RSA	(0x3 << 4)
#define  ACRY_CMD_DMA_SRAM_AHB_CPU	BIT(8)
#define  ACRY_CMD_DMA_SRAM_AHB_ENGINE	0
#define ASPEED_ACRY_DMA_SRC_BASE	0x04C
#define ASPEED_ACRY_DMA_DEST		0x050
#define  DMA_DEST_BASE(x)		(x << 16)
#define  DMA_DEST_LEN(x)		(x)
#define ASPEED_ACRY_DRAM_BRUST		0x054
#define ASPEED_ACRY_RSA_KEY_LEN		0x058
#define  RSA_E_BITS_LEN(x)		(x << 16)
#define  RSA_M_BITS_LEN(x)		(x)
#define ASPEED_ACRY_INT_MASK		0x3F8
#define ASPEED_ACRY_STATUS		0x3FC
#define  ACRY_DMA_ISR			BIT(2)
#define  ACRY_RSA_ISR			BIT(1)
#define  ACRY_ECC_ISR			BIT(0)

#define ASPEED_ACRY_BUFF_SIZE		0x1800

#define ASPEED_ACRY_RSA_MAX_LEN		2048

#define CRYPTO_FLAGS_BUSY 		BIT(1)
#define BYTES_PER_DWORD			4

#define ASPEED_EC_X			0x30
#define ASPEED_EC_Y			0x60
#define ASPEED_EC_Z			0x90
#define ASPEED_EC_Z2			0xC0
#define ASPEED_EC_Z3			0xF0
#define ASPEED_EC_K			0x120
#define ASPEED_EC_P			0x150
#define ASPEED_EC_A			0x180

/* AHBC */
#define AHBC_REGION_PROT		0x240
#define REGION_ACRYM			BIT(23)

extern int exp_dw_mapping[512];
extern int mod_dw_mapping[512];
// static int data_dw_mapping[512];
extern int data_byte_mapping[2048];

struct aspeed_acry_dev;

typedef int (*aspeed_acry_fn_t)(struct aspeed_acry_dev *);

struct aspeed_acry_rsa_ctx {
	struct aspeed_acry_dev		*acry_dev;
	struct rsa_key			key;
	int 				enc;
	u8				*n;
	u8				*e;
	u8				*d;
	size_t				n_sz;
	size_t				e_sz;
	size_t				d_sz;
};

struct aspeed_acry_ecdsa_ctx {
	struct aspeed_acry_dev		*acry_dev;
	struct completion 		completion;
	char 				sign;
	unsigned int 			curve_id;
	unsigned int 			ndigits;
	u64 				private_key[8];
	u64 				Qx[8];
	u64 				Qy[8];
	u64 				x[8];
	u64 				y[8];
	u64 				k[8];
};

struct aspeed_acry_ctx {
	aspeed_acry_fn_t trigger;
	union {
		struct aspeed_acry_rsa_ctx 	rsa_ctx;
		struct aspeed_acry_ecdsa_ctx 	ecdsa_ctx;
	} ctx;
};


/*************************************************************************************/

// struct aspeed_ecdh_ctx {
// 	struct aspeed_acry_dev		*acry_dev;
// 	const u8 			*public_key;
// 	unsigned int 			curve_id;
// 	size_t				n_sz;
// 	u8				private_key[256];
// };

/*************************************************************************************/

struct aspeed_acry_dev {
	void __iomem			*regs;
	struct device			*dev;
	int 				irq;
	struct clk			*rsaclk;
	unsigned long			version;
	struct regmap			*ahbc;

	struct crypto_queue		queue;
	struct tasklet_struct		done_task;
	bool				is_async;
	spinlock_t			lock;
	aspeed_acry_fn_t		resume;
	unsigned long			flags;

	struct akcipher_request		*akcipher_req;
	void __iomem			*acry_sram;

	void				*buf_addr;
	dma_addr_t			buf_dma_addr;

};


struct aspeed_acry_alg {
	struct aspeed_acry_dev		*acry_dev;
	union {
		struct kpp_alg 		kpp;
		struct akcipher_alg 	akcipher;
	} alg;
};

static inline void
aspeed_acry_write(struct aspeed_acry_dev *crypto, u32 val, u32 reg)
{
	// printk("write : val: %x , reg : %x \n", val, reg);
	writel(val, crypto->regs + reg);
}

static inline u32
aspeed_acry_read(struct aspeed_acry_dev *crypto, u32 reg)
{
#if 0
	u32 val = readl(crypto->regs + reg);
	printk("R : reg %x , val: %x \n", reg, val);
	return val;
#else
	return readl(crypto->regs + reg);
#endif
}
int aspeed_acry_sts_polling(struct aspeed_acry_dev *acry_dev, u32 sts);
int aspeed_acry_complete(struct aspeed_acry_dev *acry_dev, int err);
int aspeed_acry_rsa_trigger(struct aspeed_acry_dev *acry_dev);
int aspeed_acry_ec_trigger(struct aspeed_acry_dev *acry_dev);

int aspeed_register_acry_rsa_algs(struct aspeed_acry_dev *acry_dev);
int aspeed_register_acry_ecdsa_algs(struct aspeed_acry_dev *acry_dev);

int aspeed_register_acry_kpp_algs(struct aspeed_acry_dev *acry_dev);
int aspeed_acry_handle_queue(struct aspeed_acry_dev *acry_dev,
				    struct crypto_async_request *new_areq);

extern const struct ecc_curve *ecc_get_curve(unsigned int curve_id);
extern void vli_set(u64 *dest, const u64 *src, unsigned int ndigits);
extern void vli_copy_from_buf(u64 *dst_vli, unsigned int ndigits,
			      const u8 *src_buf, unsigned int buf_len);
extern struct ecc_point *ecc_alloc_point(unsigned int ndigits);
extern void ecc_point_mult(struct ecc_point *result,
			   const struct ecc_point *point, const u64 *scalar,
			   u64 *initial_z, const struct ecc_curve *curve,
			   unsigned int ndigits);
extern void vli_mod(u64 *result, const u64 *input, const u64 *mod,
		    unsigned int ndigits);
extern void vli_mod_inv(u64 *result, const u64 *input, const u64 *mod,
			unsigned int ndigits);
extern void vli_mod_mult(u64 *result, const u64 *left, const u64 *right,
			 const u64 *mod, unsigned int ndigits);
extern void vli_mod_add(u64 *result, const u64 *left, const u64 *right,
			const u64 *mod, unsigned int ndigits);
extern void vli_mod_mult_fast(u64 *result, const u64 *left, const u64 *right,
			      const u64 *curve_prime, unsigned int ndigits);
extern void vli_copy_to_buf(u8 *dst_buf, unsigned int buf_len,
			    const u64 *src_vli, unsigned int ndigits);
extern void ecc_free_point(struct ecc_point *p);
extern void ecc_point_add(u64 *x1, u64 *y1, u64 *x2, u64 *y2, u64 *curve_prime,
			  unsigned int ndigits);
extern int vli_cmp(const u64 *left, const u64 *right, unsigned int ndigits);
extern int ecc_is_key_valid(unsigned int curve_id, unsigned int ndigits,
			    const u64 *private_key, unsigned int private_key_len);
extern int ecc_is_pub_key_valid(unsigned int curve_id, unsigned int ndigits,
				const u8 *pub_key, unsigned int pub_key_len);
#endif

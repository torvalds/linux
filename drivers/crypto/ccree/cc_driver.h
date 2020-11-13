/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

/* \file cc_driver.h
 * ARM CryptoCell Linux Crypto Driver
 */

#ifndef __CC_DRIVER_H__
#define __CC_DRIVER_H__

#ifdef COMP_IN_WQ
#include <linux/workqueue.h>
#else
#include <linux/interrupt.h>
#endif
#include <linux/dma-mapping.h>
#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aes.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include "cc_host_regs.h"
#include "cc_crypto_ctx.h"
#include "cc_hw_queue_defs.h"
#include "cc_sram_mgr.h"

extern bool cc_dump_desc;
extern bool cc_dump_bytes;

#define DRV_MODULE_VERSION "5.0"

enum cc_hw_rev {
	CC_HW_REV_630 = 630,
	CC_HW_REV_710 = 710,
	CC_HW_REV_712 = 712,
	CC_HW_REV_713 = 713
};

enum cc_std_body {
	CC_STD_NIST = 0x1,
	CC_STD_OSCCA = 0x2,
	CC_STD_ALL = 0x3
};

#define CC_COHERENT_CACHE_PARAMS 0xEEE

#define CC_PINS_FULL	0x0
#define CC_PINS_SLIM	0x9F

/* Maximum DMA mask supported by IP */
#define DMA_BIT_MASK_LEN 48

#define CC_AXI_IRQ_MASK ((1 << CC_AXIM_CFG_BRESPMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_RRESPMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_INFLTMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_COMPMASK_BIT_SHIFT))

#define CC_AXI_ERR_IRQ_MASK BIT(CC_HOST_IRR_AXI_ERR_INT_BIT_SHIFT)

#define CC_COMP_IRQ_MASK BIT(CC_HOST_IRR_AXIM_COMP_INT_BIT_SHIFT)

#define CC_SECURITY_DISABLED_MASK BIT(CC_SECURITY_DISABLED_VALUE_BIT_SHIFT)

#define CC_NVM_IS_IDLE_MASK BIT(CC_NVM_IS_IDLE_VALUE_BIT_SHIFT)

#define AXIM_MON_COMP_VALUE CC_GENMASK(CC_AXIM_MON_COMP_VALUE)

#define CC_CPP_AES_ABORT_MASK ( \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_0_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_1_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_2_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_3_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_4_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_5_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_6_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_AES_7_MASK_BIT_SHIFT))

#define CC_CPP_SM4_ABORT_MASK ( \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_0_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_1_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_2_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_3_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_4_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_5_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_6_MASK_BIT_SHIFT) | \
	BIT(CC_HOST_IMR_REE_OP_ABORTED_SM_7_MASK_BIT_SHIFT))

/* Register name mangling macro */
#define CC_REG(reg_name) CC_ ## reg_name ## _REG_OFFSET

/* TEE FIPS status interrupt */
#define CC_GPR0_IRQ_MASK BIT(CC_HOST_IRR_GPR0_BIT_SHIFT)

#define CC_CRA_PRIO 400

#define MIN_HW_QUEUE_SIZE 50 /* Minimum size required for proper function */

#define MAX_REQUEST_QUEUE_SIZE 4096
#define MAX_MLLI_BUFF_SIZE 2080

/* Definitions for HW descriptors DIN/DOUT fields */
#define NS_BIT 1
#define AXI_ID 0
/* AXI_ID is not actually the AXI ID of the transaction but the value of AXI_ID
 * field in the HW descriptor. The DMA engine +8 that value.
 */

struct cc_cpp_req {
	bool is_cpp;
	enum cc_cpp_alg alg;
	u8 slot;
};

#define CC_MAX_IVGEN_DMA_ADDRESSES	3
struct cc_crypto_req {
	void (*user_cb)(struct device *dev, void *req, int err);
	void *user_arg;
	struct completion seq_compl; /* request completion */
	struct cc_cpp_req cpp;
};

/**
 * struct cc_drvdata - driver private data context
 * @cc_base:	virt address of the CC registers
 * @irq:	bitmap indicating source of last interrupt
 */
struct cc_drvdata {
	void __iomem *cc_base;
	int irq;
	struct completion hw_queue_avail; /* wait for HW queue availability */
	struct platform_device *plat_dev;
	u32 mlli_sram_addr;
	struct dma_pool *mlli_buffs_pool;
	struct list_head alg_list;
	void *hash_handle;
	void *aead_handle;
	void *request_mgr_handle;
	void *fips_handle;
	u32 sram_free_offset;	/* offset to non-allocated area in SRAM */
	struct dentry *dir;	/* for debugfs */
	struct clk *clk;
	bool coherent;
	char *hw_rev_name;
	enum cc_hw_rev hw_rev;
	u32 axim_mon_offset;
	u32 sig_offset;
	u32 ver_offset;
	int std_bodies;
	bool sec_disabled;
	u32 comp_mask;
};

struct cc_crypto_alg {
	struct list_head entry;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	struct cc_drvdata *drvdata;
	struct skcipher_alg skcipher_alg;
	struct aead_alg aead_alg;
};

struct cc_alg_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	union {
		struct skcipher_alg skcipher;
		struct aead_alg aead;
	} template_u;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	u32 min_hw_rev;
	enum cc_std_body std_body;
	bool sec_func;
	unsigned int data_unit;
	struct cc_drvdata *drvdata;
};

struct async_gen_req_ctx {
	dma_addr_t iv_dma_addr;
	u8 *iv;
	enum drv_crypto_direction op_type;
};

static inline struct device *drvdata_to_dev(struct cc_drvdata *drvdata)
{
	return &drvdata->plat_dev->dev;
}

void __dump_byte_array(const char *name, const u8 *buf, size_t len);
static inline void dump_byte_array(const char *name, const u8 *the_array,
				   size_t size)
{
	if (cc_dump_bytes)
		__dump_byte_array(name, the_array, size);
}

bool cc_wait_for_reset_completion(struct cc_drvdata *drvdata);
int init_cc_regs(struct cc_drvdata *drvdata, bool is_probe);
void fini_cc_regs(struct cc_drvdata *drvdata);
unsigned int cc_get_default_hash_len(struct cc_drvdata *drvdata);

static inline void cc_iowrite(struct cc_drvdata *drvdata, u32 reg, u32 val)
{
	iowrite32(val, (drvdata->cc_base + reg));
}

static inline u32 cc_ioread(struct cc_drvdata *drvdata, u32 reg)
{
	return ioread32(drvdata->cc_base + reg);
}

static inline gfp_t cc_gfp_flags(struct crypto_async_request *req)
{
	return (req->flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
			GFP_KERNEL : GFP_ATOMIC;
}

static inline void set_queue_last_ind(struct cc_drvdata *drvdata,
				      struct cc_hw_desc *pdesc)
{
	if (drvdata->hw_rev >= CC_HW_REV_712)
		set_queue_last_ind_bit(pdesc);
}

#endif /*__CC_DRIVER_H__*/

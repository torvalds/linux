/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

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
#include <crypto/sha.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

/* Registers definitions from shared/hw/ree_include */
#include "cc_host_regs.h"
#define CC_DEV_SHA_MAX 512
#include "cc_crypto_ctx.h"
#include "cc_hw_queue_defs.h"
#include "cc_sram_mgr.h"

extern bool cc_dump_desc;
extern bool cc_dump_bytes;

#define DRV_MODULE_VERSION "4.0"

enum cc_hw_rev {
	CC_HW_REV_630 = 630,
	CC_HW_REV_710 = 710,
	CC_HW_REV_712 = 712
};

#define CC_COHERENT_CACHE_PARAMS 0xEEE

/* Maximum DMA mask supported by IP */
#define DMA_BIT_MASK_LEN 48

#define CC_AXI_IRQ_MASK ((1 << CC_AXIM_CFG_BRESPMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_RRESPMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_INFLTMASK_BIT_SHIFT) | \
			  (1 << CC_AXIM_CFG_COMPMASK_BIT_SHIFT))

#define CC_AXI_ERR_IRQ_MASK BIT(CC_HOST_IRR_AXI_ERR_INT_BIT_SHIFT)

#define CC_COMP_IRQ_MASK BIT(CC_HOST_IRR_AXIM_COMP_INT_BIT_SHIFT)

#define AXIM_MON_COMP_VALUE GENMASK(CC_AXIM_MON_COMP_VALUE_BIT_SIZE + \
				    CC_AXIM_MON_COMP_VALUE_BIT_SHIFT, \
				    CC_AXIM_MON_COMP_VALUE_BIT_SHIFT)

/* Register name mangling macro */
#define CC_REG(reg_name) CC_ ## reg_name ## _REG_OFFSET

/* TEE FIPS status interrupt */
#define CC_GPR0_IRQ_MASK BIT(CC_HOST_IRR_GPR0_BIT_SHIFT)

#define CC_CRA_PRIO 400

#define MIN_HW_QUEUE_SIZE 50 /* Minimum size required for proper function */

#define MAX_REQUEST_QUEUE_SIZE 4096
#define MAX_MLLI_BUFF_SIZE 2080
#define MAX_ICV_NENTS_SUPPORTED 2

/* Definitions for HW descriptors DIN/DOUT fields */
#define NS_BIT 1
#define AXI_ID 0
/* AXI_ID is not actually the AXI ID of the transaction but the value of AXI_ID
 * field in the HW descriptor. The DMA engine +8 that value.
 */

#define CC_MAX_IVGEN_DMA_ADDRESSES	3
struct cc_crypto_req {
	void (*user_cb)(struct device *dev, void *req, int err);
	void *user_arg;
	dma_addr_t ivgen_dma_addr[CC_MAX_IVGEN_DMA_ADDRESSES];
	/* For the first 'ivgen_dma_addr_len' addresses of this array,
	 * generated IV would be placed in it by send_request().
	 * Same generated IV for all addresses!
	 */
	/* Amount of 'ivgen_dma_addr' elements to be filled. */
	unsigned int ivgen_dma_addr_len;
	/* The generated IV size required, 8/16 B allowed. */
	unsigned int ivgen_size;
	struct completion seq_compl; /* request completion */
};

/**
 * struct cc_drvdata - driver private data context
 * @cc_base:	virt address of the CC registers
 * @irq:	device IRQ number
 * @irq_mask:	Interrupt mask shadow (1 for masked interrupts)
 * @fw_ver:	SeP loaded firmware version
 */
struct cc_drvdata {
	void __iomem *cc_base;
	int irq;
	u32 irq_mask;
	u32 fw_ver;
	struct completion hw_queue_avail; /* wait for HW queue availability */
	struct platform_device *plat_dev;
	cc_sram_addr_t mlli_sram_addr;
	void *buff_mgr_handle;
	void *cipher_handle;
	void *hash_handle;
	void *aead_handle;
	void *request_mgr_handle;
	void *fips_handle;
	void *ivgen_handle;
	void *sram_mgr_handle;
	void *debugfs;
	struct clk *clk;
	bool coherent;
	char *hw_rev_name;
	enum cc_hw_rev hw_rev;
	u32 hash_len_sz;
	u32 axim_mon_offset;
};

struct cc_crypto_alg {
	struct list_head entry;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	unsigned int data_unit;
	struct cc_drvdata *drvdata;
	struct skcipher_alg skcipher_alg;
	struct aead_alg aead_alg;
};

struct cc_alg_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	u32 type;
	union {
		struct skcipher_alg skcipher;
		struct aead_alg aead;
	} template_u;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	u32 min_hw_rev;
	unsigned int data_unit;
	struct cc_drvdata *drvdata;
};

struct async_gen_req_ctx {
	dma_addr_t iv_dma_addr;
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

int init_cc_regs(struct cc_drvdata *drvdata, bool is_probe);
void fini_cc_regs(struct cc_drvdata *drvdata);
int cc_clk_on(struct cc_drvdata *drvdata);
void cc_clk_off(struct cc_drvdata *drvdata);

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

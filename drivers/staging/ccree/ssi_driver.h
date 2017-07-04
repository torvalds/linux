/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file ssi_driver.h
 * ARM CryptoCell Linux Crypto Driver
 */

#ifndef __SSI_DRIVER_H__
#define __SSI_DRIVER_H__

#include "ssi_config.h"
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
#include <linux/version.h>
#include <linux/clk.h>

/* Registers definitions from shared/hw/ree_include */
#include "dx_reg_base_host.h"
#include "dx_host.h"
#include "cc_regs.h"
#include "dx_reg_common.h"
#include "cc_hal.h"
#define CC_SUPPORT_SHA DX_DEV_SHA_MAX
#include "cc_crypto_ctx.h"
#include "ssi_sysfs.h"
#include "hash_defs.h"
#include "ssi_fips_local.h"
#include "cc_hw_queue_defs.h"
#include "ssi_sram_mgr.h"

#define DRV_MODULE_VERSION "3.0"

#define SSI_DEV_NAME_STR "cc715ree"
#define CC_COHERENT_CACHE_PARAMS 0xEEE

#define SSI_CC_HAS_AES_CCM 1
#define SSI_CC_HAS_AES_GCM 1
#define SSI_CC_HAS_AES_XTS 1
#define SSI_CC_HAS_AES_ESSIV 1
#define SSI_CC_HAS_AES_BITLOCKER 1
#define SSI_CC_HAS_AES_CTS 1
#define SSI_CC_HAS_MULTI2 0
#define SSI_CC_HAS_CMAC 1

#define SSI_AXI_IRQ_MASK ((1 << DX_AXIM_CFG_BRESPMASK_BIT_SHIFT) | (1 << DX_AXIM_CFG_RRESPMASK_BIT_SHIFT) |	\
			(1 << DX_AXIM_CFG_INFLTMASK_BIT_SHIFT) | (1 << DX_AXIM_CFG_COMPMASK_BIT_SHIFT))

#define SSI_AXI_ERR_IRQ_MASK (1 << DX_HOST_IRR_AXI_ERR_INT_BIT_SHIFT)

#define SSI_COMP_IRQ_MASK (1 << DX_HOST_IRR_AXIM_COMP_INT_BIT_SHIFT)

/* TEE FIPS status interrupt */
#define SSI_GPR0_IRQ_MASK (1 << DX_HOST_IRR_GPR0_BIT_SHIFT)

#define SSI_CRA_PRIO 3000

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

/* Logging macros */
#define SSI_LOG(level, format, ...) \
	printk(level "cc715ree::%s: " format, __func__, ##__VA_ARGS__)
#define SSI_LOG_ERR(format, ...) SSI_LOG(KERN_ERR, format, ##__VA_ARGS__)
#define SSI_LOG_WARNING(format, ...) SSI_LOG(KERN_WARNING, format, ##__VA_ARGS__)
#define SSI_LOG_NOTICE(format, ...) SSI_LOG(KERN_NOTICE, format, ##__VA_ARGS__)
#define SSI_LOG_INFO(format, ...) SSI_LOG(KERN_INFO, format, ##__VA_ARGS__)
#ifdef CC_DEBUG
#define SSI_LOG_DEBUG(format, ...) SSI_LOG(KERN_DEBUG, format, ##__VA_ARGS__)
#else /* Debug log messages are removed at compile time for non-DEBUG config. */
#define SSI_LOG_DEBUG(format, ...) do {} while (0)
#endif

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define SSI_MAX_IVGEN_DMA_ADDRESSES	3
struct ssi_crypto_req {
	void (*user_cb)(struct device *dev, void *req, void __iomem *cc_base);
	void *user_arg;
	dma_addr_t ivgen_dma_addr[SSI_MAX_IVGEN_DMA_ADDRESSES];
	/* For the first 'ivgen_dma_addr_len' addresses of this array,
	 * generated IV would be placed in it by send_request().
	 * Same generated IV for all addresses!
	 */
	unsigned int ivgen_dma_addr_len; /* Amount of 'ivgen_dma_addr' elements to be filled. */
	unsigned int ivgen_size; /* The generated IV size required, 8/16 B allowed. */
	struct completion seq_compl; /* request completion */
};

/**
 * struct ssi_drvdata - driver private data context
 * @cc_base:	virt address of the CC registers
 * @irq:	device IRQ number
 * @irq_mask:	Interrupt mask shadow (1 for masked interrupts)
 * @fw_ver:	SeP loaded firmware version
 */
struct ssi_drvdata {
	struct resource *res_mem;
	struct resource *res_irq;
	void __iomem *cc_base;
	unsigned int irq;
	u32 irq_mask;
	u32 fw_ver;
	/* Calibration time of start/stop
	 * monitor descriptors
	 */
	u32 monitor_null_cycles;
	struct platform_device *plat_dev;
	ssi_sram_addr_t mlli_sram_addr;
	struct completion icache_setup_completion;
	void *buff_mgr_handle;
	void *hash_handle;
	void *aead_handle;
	void *blkcipher_handle;
	void *request_mgr_handle;
	void *fips_handle;
	void *ivgen_handle;
	void *sram_mgr_handle;
	u32 inflight_counter;
	struct clk *clk;
	bool coherent;
};

struct ssi_crypto_alg {
	struct list_head entry;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	struct ssi_drvdata *drvdata;
	struct crypto_alg crypto_alg;
	struct aead_alg aead_alg;
};

struct ssi_alg_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	u32 type;
	union {
		struct ablkcipher_alg ablkcipher;
		struct aead_alg aead;
		struct blkcipher_alg blkcipher;
		struct cipher_alg cipher;
		struct compress_alg compress;
	} template_u;
	int cipher_mode;
	int flow_mode; /* Note: currently, refers to the cipher mode only. */
	int auth_mode;
	struct ssi_drvdata *drvdata;
};

struct async_gen_req_ctx {
	dma_addr_t iv_dma_addr;
	enum drv_crypto_direction op_type;
};

#ifdef DX_DUMP_BYTES
void dump_byte_array(const char *name, const u8 *the_array, unsigned long size);
#else
#define dump_byte_array(name, array, size) do {	\
} while (0);
#endif

int init_cc_regs(struct ssi_drvdata *drvdata, bool is_probe);
void fini_cc_regs(struct ssi_drvdata *drvdata);
int cc_clk_on(struct ssi_drvdata *drvdata);
void cc_clk_off(struct ssi_drvdata *drvdata);

#endif /*__SSI_DRIVER_H__*/


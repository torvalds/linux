/* Copyright (c) 2009 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bman_priv.h"

u16 bman_ip_rev;
EXPORT_SYMBOL(bman_ip_rev);

/* Register offsets */
#define REG_FBPR_FPC		0x0800
#define REG_ECSR		0x0a00
#define REG_ECIR		0x0a04
#define REG_EADR		0x0a08
#define REG_EDATA(n)		(0x0a10 + ((n) * 0x04))
#define REG_SBEC(n)		(0x0a80 + ((n) * 0x04))
#define REG_IP_REV_1		0x0bf8
#define REG_IP_REV_2		0x0bfc
#define REG_FBPR_BARE		0x0c00
#define REG_FBPR_BAR		0x0c04
#define REG_FBPR_AR		0x0c10
#define REG_SRCIDR		0x0d04
#define REG_LIODNR		0x0d08
#define REG_ERR_ISR		0x0e00
#define REG_ERR_IER		0x0e04
#define REG_ERR_ISDR		0x0e08

/* Used by all error interrupt registers except 'inhibit' */
#define BM_EIRQ_IVCI	0x00000010	/* Invalid Command Verb */
#define BM_EIRQ_FLWI	0x00000008	/* FBPR Low Watermark */
#define BM_EIRQ_MBEI	0x00000004	/* Multi-bit ECC Error */
#define BM_EIRQ_SBEI	0x00000002	/* Single-bit ECC Error */
#define BM_EIRQ_BSCN	0x00000001	/* pool State Change Notification */

struct bman_hwerr_txt {
	u32 mask;
	const char *txt;
};

static const struct bman_hwerr_txt bman_hwerr_txts[] = {
	{ BM_EIRQ_IVCI, "Invalid Command Verb" },
	{ BM_EIRQ_FLWI, "FBPR Low Watermark" },
	{ BM_EIRQ_MBEI, "Multi-bit ECC Error" },
	{ BM_EIRQ_SBEI, "Single-bit ECC Error" },
	{ BM_EIRQ_BSCN, "Pool State Change Notification" },
};

/* Only trigger low water mark interrupt once only */
#define BMAN_ERRS_TO_DISABLE BM_EIRQ_FLWI

/* Pointer to the start of the BMan's CCSR space */
static u32 __iomem *bm_ccsr_start;

static inline u32 bm_ccsr_in(u32 offset)
{
	return ioread32be(bm_ccsr_start + offset/4);
}
static inline void bm_ccsr_out(u32 offset, u32 val)
{
	iowrite32be(val, bm_ccsr_start + offset/4);
}

static void bm_get_version(u16 *id, u8 *major, u8 *minor)
{
	u32 v = bm_ccsr_in(REG_IP_REV_1);
	*id = (v >> 16);
	*major = (v >> 8) & 0xff;
	*minor = v & 0xff;
}

/* signal transactions for FBPRs with higher priority */
#define FBPR_AR_RPRIO_HI BIT(30)

static void bm_set_memory(u64 ba, u32 size)
{
	u32 exp = ilog2(size);
	/* choke if size isn't within range */
	DPAA_ASSERT(size >= 4096 && size <= 1024*1024*1024 &&
		   is_power_of_2(size));
	/* choke if '[e]ba' has lower-alignment than 'size' */
	DPAA_ASSERT(!(ba & (size - 1)));
	bm_ccsr_out(REG_FBPR_BARE, upper_32_bits(ba));
	bm_ccsr_out(REG_FBPR_BAR, lower_32_bits(ba));
	bm_ccsr_out(REG_FBPR_AR, exp - 1);
}

/*
 * Location and size of BMan private memory
 *
 * Ideally we would use the DMA API to turn rmem->base into a DMA address
 * (especially if iommu translations ever get involved).  Unfortunately, the
 * DMA API currently does not allow mapping anything that is not backed with
 * a struct page.
 */
static dma_addr_t fbpr_a;
static size_t fbpr_sz;

static int bman_fbpr(struct reserved_mem *rmem)
{
	fbpr_a = rmem->base;
	fbpr_sz = rmem->size;

	WARN_ON(!(fbpr_a && fbpr_sz));

	return 0;
}
RESERVEDMEM_OF_DECLARE(bman_fbpr, "fsl,bman-fbpr", bman_fbpr);

static irqreturn_t bman_isr(int irq, void *ptr)
{
	u32 isr_val, ier_val, ecsr_val, isr_mask, i;
	struct device *dev = ptr;

	ier_val = bm_ccsr_in(REG_ERR_IER);
	isr_val = bm_ccsr_in(REG_ERR_ISR);
	ecsr_val = bm_ccsr_in(REG_ECSR);
	isr_mask = isr_val & ier_val;

	if (!isr_mask)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(bman_hwerr_txts); i++) {
		if (bman_hwerr_txts[i].mask & isr_mask) {
			dev_err_ratelimited(dev, "ErrInt: %s\n",
					    bman_hwerr_txts[i].txt);
			if (bman_hwerr_txts[i].mask & ecsr_val) {
				/* Re-arm error capture registers */
				bm_ccsr_out(REG_ECSR, ecsr_val);
			}
			if (bman_hwerr_txts[i].mask & BMAN_ERRS_TO_DISABLE) {
				dev_dbg(dev, "Disabling error 0x%x\n",
					bman_hwerr_txts[i].mask);
				ier_val &= ~bman_hwerr_txts[i].mask;
				bm_ccsr_out(REG_ERR_IER, ier_val);
			}
		}
	}
	bm_ccsr_out(REG_ERR_ISR, isr_val);

	return IRQ_HANDLED;
}

static int fsl_bman_probe(struct platform_device *pdev)
{
	int ret, err_irq;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource *res;
	u16 id, bm_pool_cnt;
	u8 major, minor;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Can't get %pOF property 'IORESOURCE_MEM'\n",
			node);
		return -ENXIO;
	}
	bm_ccsr_start = devm_ioremap(dev, res->start, resource_size(res));
	if (!bm_ccsr_start)
		return -ENXIO;

	bm_get_version(&id, &major, &minor);
	if (major == 1 && minor == 0) {
		bman_ip_rev = BMAN_REV10;
		bm_pool_cnt = BM_POOL_MAX;
	} else if (major == 2 && minor == 0) {
		bman_ip_rev = BMAN_REV20;
		bm_pool_cnt = 8;
	} else if (major == 2 && minor == 1) {
		bman_ip_rev = BMAN_REV21;
		bm_pool_cnt = BM_POOL_MAX;
	} else {
		dev_err(dev, "Unknown Bman version:%04x,%02x,%02x\n",
			id, major, minor);
		return -ENODEV;
	}

	/*
	 * If FBPR memory wasn't defined using the qbman compatible string
	 * try using the of_reserved_mem_device method
	 */
	if (!fbpr_a) {
		ret = qbman_init_private_mem(dev, 0, &fbpr_a, &fbpr_sz);
		if (ret) {
			dev_err(dev, "qbman_init_private_mem() failed 0x%x\n",
				ret);
			return -ENODEV;
		}
	}

	dev_dbg(dev, "Allocated FBPR 0x%llx 0x%zx\n", fbpr_a, fbpr_sz);

	bm_set_memory(fbpr_a, fbpr_sz);

	err_irq = platform_get_irq(pdev, 0);
	if (err_irq <= 0) {
		dev_info(dev, "Can't get %pOF IRQ\n", node);
		return -ENODEV;
	}
	ret = devm_request_irq(dev, err_irq, bman_isr, IRQF_SHARED, "bman-err",
			       dev);
	if (ret)  {
		dev_err(dev, "devm_request_irq() failed %d for '%pOF'\n",
			ret, node);
		return ret;
	}
	/* Disable Buffer Pool State Change */
	bm_ccsr_out(REG_ERR_ISDR, BM_EIRQ_BSCN);
	/*
	 * Write-to-clear any stale bits, (eg. starvation being asserted prior
	 * to resource allocation during driver init).
	 */
	bm_ccsr_out(REG_ERR_ISR, 0xffffffff);
	/* Enable Error Interrupts */
	bm_ccsr_out(REG_ERR_IER, 0xffffffff);

	bm_bpalloc = devm_gen_pool_create(dev, 0, -1, "bman-bpalloc");
	if (IS_ERR(bm_bpalloc)) {
		ret = PTR_ERR(bm_bpalloc);
		dev_err(dev, "bman-bpalloc pool init failed (%d)\n", ret);
		return ret;
	}

	/* seed BMan resource pool */
	ret = gen_pool_add(bm_bpalloc, DPAA_GENALLOC_OFF, bm_pool_cnt, -1);
	if (ret) {
		dev_err(dev, "Failed to seed BPID range [%d..%d] (%d)\n",
			0, bm_pool_cnt - 1, ret);
		return ret;
	}

	return 0;
};

static const struct of_device_id fsl_bman_ids[] = {
	{
		.compatible = "fsl,bman",
	},
	{}
};

static struct platform_driver fsl_bman_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = fsl_bman_ids,
		.suppress_bind_attrs = true,
	},
	.probe = fsl_bman_probe,
};

builtin_platform_driver(fsl_bman_driver);

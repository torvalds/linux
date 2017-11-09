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

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/sha.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/skcipher.h>

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/sysctl.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/pm.h>

/* cache.h required for L1_CACHE_ALIGN() and cache_line_size() */
#include <linux/cache.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_address.h>

#include "ssi_config.h"
#include "ssi_driver.h"
#include "ssi_request_mgr.h"
#include "ssi_buffer_mgr.h"
#include "ssi_sysfs.h"
#include "ssi_cipher.h"
#include "ssi_aead.h"
#include "ssi_hash.h"
#include "ssi_ivgen.h"
#include "ssi_sram_mgr.h"
#include "ssi_pm.h"
#include "ssi_fips.h"

#ifdef DX_DUMP_BYTES
void dump_byte_array(const char *name, const u8 *buf, size_t len)
{
	char prefix[NAME_LEN];

	if (!buf)
		return;

	snprintf(prefix, sizeof(prefix), "%s[%lu]: ", name, len);

	print_hex_dump(KERN_DEBUG, prefix, DUMP_PREFIX_ADDRESS, 16, 1, len,
		       false);
}
#endif

static irqreturn_t cc_isr(int irq, void *dev_id)
{
	struct ssi_drvdata *drvdata = (struct ssi_drvdata *)dev_id;
	struct device *dev = drvdata_to_dev(drvdata);
	u32 irr;
	u32 imr;

	/* STAT_OP_TYPE_GENERIC STAT_PHASE_0: Interrupt */

	/* read the interrupt status */
	irr = cc_ioread(drvdata, CC_REG(HOST_IRR));
	dev_dbg(dev, "Got IRR=0x%08X\n", irr);
	if (unlikely(irr == 0)) { /* Probably shared interrupt line */
		dev_err(dev, "Got interrupt with empty IRR\n");
		return IRQ_NONE;
	}
	imr = cc_ioread(drvdata, CC_REG(HOST_IMR));

	/* clear interrupt - must be before processing events */
	cc_iowrite(drvdata, CC_REG(HOST_ICR), irr);

	drvdata->irq = irr;
	/* Completion interrupt - most probable */
	if (likely((irr & SSI_COMP_IRQ_MASK))) {
		/* Mask AXI completion interrupt - will be unmasked in Deferred service handler */
		cc_iowrite(drvdata, CC_REG(HOST_IMR), imr | SSI_COMP_IRQ_MASK);
		irr &= ~SSI_COMP_IRQ_MASK;
		complete_request(drvdata);
	}
#ifdef CC_SUPPORT_FIPS
	/* TEE FIPS interrupt */
	if (likely((irr & SSI_GPR0_IRQ_MASK))) {
		/* Mask interrupt - will be unmasked in Deferred service handler */
		cc_iowrite(drvdata, CC_REG(HOST_IMR), imr | SSI_GPR0_IRQ_MASK);
		irr &= ~SSI_GPR0_IRQ_MASK;
		fips_handler(drvdata);
	}
#endif
	/* AXI error interrupt */
	if (unlikely((irr & SSI_AXI_ERR_IRQ_MASK))) {
		u32 axi_err;

		/* Read the AXI error ID */
		axi_err = cc_ioread(drvdata, CC_REG(AXIM_MON_ERR));
		dev_dbg(dev, "AXI completion error: axim_mon_err=0x%08X\n",
			axi_err);

		irr &= ~SSI_AXI_ERR_IRQ_MASK;
	}

	if (unlikely(irr)) {
		dev_dbg(dev, "IRR includes unknown cause bits (0x%08X)\n",
			irr);
		/* Just warning */
	}

	return IRQ_HANDLED;
}

int init_cc_regs(struct ssi_drvdata *drvdata, bool is_probe)
{
	unsigned int val, cache_params;
	struct device *dev = drvdata_to_dev(drvdata);

	/* Unmask all AXI interrupt sources AXI_CFG1 register */
	val = cc_ioread(drvdata, CC_REG(AXIM_CFG));
	cc_iowrite(drvdata, CC_REG(AXIM_CFG), val & ~SSI_AXI_IRQ_MASK);
	dev_dbg(dev, "AXIM_CFG=0x%08X\n",
		cc_ioread(drvdata, CC_REG(AXIM_CFG)));

	/* Clear all pending interrupts */
	val = cc_ioread(drvdata, CC_REG(HOST_IRR));
	dev_dbg(dev, "IRR=0x%08X\n", val);
	cc_iowrite(drvdata, CC_REG(HOST_ICR), val);

	/* Unmask relevant interrupt cause */
	val = (unsigned int)(~(SSI_COMP_IRQ_MASK | SSI_AXI_ERR_IRQ_MASK |
			       SSI_GPR0_IRQ_MASK));
	cc_iowrite(drvdata, CC_REG(HOST_IMR), val);

#ifdef DX_HOST_IRQ_TIMER_INIT_VAL_REG_OFFSET
#ifdef DX_IRQ_DELAY
	/* Set CC IRQ delay */
	cc_iowrite(drvdata, CC_REG(HOST_IRQ_TIMER_INIT_VAL), DX_IRQ_DELAY);
#endif
	if (cc_ioread(drvdata, CC_REG(HOST_IRQ_TIMER_INIT_VAL)) > 0) {
		dev_dbg(dev, "irq_delay=%d CC cycles\n",
			cc_ioread(drvdata, CC_REG(HOST_IRQ_TIMER_INIT_VAL)));
	}
#endif

	cache_params = (drvdata->coherent ? CC_COHERENT_CACHE_PARAMS : 0x0);

	val = cc_ioread(drvdata, CC_REG(AXIM_CACHE_PARAMS));

	if (is_probe)
		dev_info(dev, "Cache params previous: 0x%08X\n", val);

	cc_iowrite(drvdata, CC_REG(AXIM_CACHE_PARAMS), cache_params);
	val = cc_ioread(drvdata, CC_REG(AXIM_CACHE_PARAMS));

	if (is_probe)
		dev_info(dev, "Cache params current: 0x%08X (expect: 0x%08X)\n",
			 val, cache_params);

	return 0;
}

static int init_cc_resources(struct platform_device *plat_dev)
{
	struct resource *req_mem_cc_regs = NULL;
	struct ssi_drvdata *new_drvdata;
	struct device *dev = &plat_dev->dev;
	struct device_node *np = dev->of_node;
	u32 signature_val;
	dma_addr_t dma_mask;
	int rc = 0;

	new_drvdata = devm_kzalloc(dev, sizeof(*new_drvdata), GFP_KERNEL);
	if (!new_drvdata)
		return -ENOMEM;

	platform_set_drvdata(plat_dev, new_drvdata);
	new_drvdata->plat_dev = plat_dev;

	new_drvdata->clk = of_clk_get(np, 0);
	new_drvdata->coherent = of_dma_is_coherent(np);

	/* Get device resources */
	/* First CC registers space */
	req_mem_cc_regs = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
	/* Map registers space */
	new_drvdata->cc_base = devm_ioremap_resource(dev, req_mem_cc_regs);
	if (IS_ERR(new_drvdata->cc_base)) {
		dev_err(dev, "Failed to ioremap registers");
		return PTR_ERR(new_drvdata->cc_base);
	}

	dev_dbg(dev, "Got MEM resource (%s): %pR\n", req_mem_cc_regs->name,
		req_mem_cc_regs);
	dev_dbg(dev, "CC registers mapped from %pa to 0x%p\n",
		&req_mem_cc_regs->start, new_drvdata->cc_base);

	/* Then IRQ */
	new_drvdata->irq = platform_get_irq(plat_dev, 0);
	if (new_drvdata->irq < 0) {
		dev_err(dev, "Failed getting IRQ resource\n");
		return new_drvdata->irq;
	}

	rc = devm_request_irq(dev, new_drvdata->irq, cc_isr,
			      IRQF_SHARED, "arm_cc7x", new_drvdata);
	if (rc) {
		dev_err(dev, "Could not register to interrupt %d\n",
			new_drvdata->irq);
		return rc;
	}
	dev_dbg(dev, "Registered to IRQ: %d\n", new_drvdata->irq);

	if (!plat_dev->dev.dma_mask)
		plat_dev->dev.dma_mask = &plat_dev->dev.coherent_dma_mask;

	dma_mask = (dma_addr_t)(DMA_BIT_MASK(DMA_BIT_MASK_LEN));
	while (dma_mask > 0x7fffffffUL) {
		if (dma_supported(&plat_dev->dev, dma_mask)) {
			rc = dma_set_coherent_mask(&plat_dev->dev, dma_mask);
			if (!rc)
				break;
		}
		dma_mask >>= 1;
	}

	if (rc) {
		dev_err(dev, "Failed in dma_set_mask, mask=%par\n",
			&dma_mask);
		return rc;
	}

	rc = cc_clk_on(new_drvdata);
	if (rc) {
		dev_err(dev, "Failed to enable clock");
		return rc;
	}

	/* Verify correct mapping */
	signature_val = cc_ioread(new_drvdata, CC_REG(HOST_SIGNATURE));
	if (signature_val != DX_DEV_SIGNATURE) {
		dev_err(dev, "Invalid CC signature: SIGNATURE=0x%08X != expected=0x%08X\n",
			signature_val, (u32)DX_DEV_SIGNATURE);
		rc = -EINVAL;
		goto post_clk_err;
	}
	dev_dbg(dev, "CC SIGNATURE=0x%08X\n", signature_val);

	/* Display HW versions */
	dev_info(dev, "ARM CryptoCell %s Driver: HW version 0x%08X, Driver version %s\n",
		 SSI_DEV_NAME_STR,
		 cc_ioread(new_drvdata, CC_REG(HOST_VERSION)),
		 DRV_MODULE_VERSION);

	rc = init_cc_regs(new_drvdata, true);
	if (unlikely(rc)) {
		dev_err(dev, "init_cc_regs failed\n");
		goto post_clk_err;
	}

#ifdef ENABLE_CC_SYSFS
	rc = ssi_sysfs_init(&dev->kobj, new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "init_stat_db failed\n");
		goto post_regs_err;
	}
#endif

	rc = ssi_fips_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "SSI_FIPS_INIT failed 0x%x\n", rc);
		goto post_sysfs_err;
	}
	rc = ssi_sram_mgr_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_sram_mgr_init failed\n");
		goto post_fips_init_err;
	}

	new_drvdata->mlli_sram_addr =
		cc_sram_alloc(new_drvdata, MAX_MLLI_BUFF_SIZE);
	if (unlikely(new_drvdata->mlli_sram_addr == NULL_SRAM_ADDR)) {
		dev_err(dev, "Failed to alloc MLLI Sram buffer\n");
		rc = -ENOMEM;
		goto post_sram_mgr_err;
	}

	rc = request_mgr_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "request_mgr_init failed\n");
		goto post_sram_mgr_err;
	}

	rc = cc_buffer_mgr_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "buffer_mgr_init failed\n");
		goto post_req_mgr_err;
	}

	rc = cc_pm_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_power_mgr_init failed\n");
		goto post_buf_mgr_err;
	}

	rc = ssi_ivgen_init(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_ivgen_init failed\n");
		goto post_power_mgr_err;
	}

	/* Allocate crypto algs */
	rc = ssi_ablkcipher_alloc(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_ablkcipher_alloc failed\n");
		goto post_ivgen_err;
	}

	/* hash must be allocated before aead since hash exports APIs */
	rc = ssi_hash_alloc(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_hash_alloc failed\n");
		goto post_cipher_err;
	}

	rc = ssi_aead_alloc(new_drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "ssi_aead_alloc failed\n");
		goto post_hash_err;
	}

	/* If we got here and FIPS mode is enabled
	 * it means all FIPS test passed, so let TEE
	 * know we're good.
	 */
	cc_set_ree_fips_status(new_drvdata, true);

	return 0;

post_hash_err:
	ssi_hash_free(new_drvdata);
post_cipher_err:
	ssi_ablkcipher_free(new_drvdata);
post_ivgen_err:
	ssi_ivgen_fini(new_drvdata);
post_power_mgr_err:
	cc_pm_fini(new_drvdata);
post_buf_mgr_err:
	 cc_buffer_mgr_fini(new_drvdata);
post_req_mgr_err:
	request_mgr_fini(new_drvdata);
post_sram_mgr_err:
	ssi_sram_mgr_fini(new_drvdata);
post_fips_init_err:
	ssi_fips_fini(new_drvdata);
post_sysfs_err:
#ifdef ENABLE_CC_SYSFS
	ssi_sysfs_fini();
#endif
post_regs_err:
	fini_cc_regs(new_drvdata);
post_clk_err:
	cc_clk_off(new_drvdata);
	return rc;
}

void fini_cc_regs(struct ssi_drvdata *drvdata)
{
	/* Mask all interrupts */
	cc_iowrite(drvdata, CC_REG(HOST_IMR), 0xFFFFFFFF);
}

static void cleanup_cc_resources(struct platform_device *plat_dev)
{
	struct ssi_drvdata *drvdata =
		(struct ssi_drvdata *)platform_get_drvdata(plat_dev);

	ssi_aead_free(drvdata);
	ssi_hash_free(drvdata);
	ssi_ablkcipher_free(drvdata);
	ssi_ivgen_fini(drvdata);
	cc_pm_fini(drvdata);
	cc_buffer_mgr_fini(drvdata);
	request_mgr_fini(drvdata);
	ssi_sram_mgr_fini(drvdata);
	ssi_fips_fini(drvdata);
#ifdef ENABLE_CC_SYSFS
	ssi_sysfs_fini();
#endif
	fini_cc_regs(drvdata);
	cc_clk_off(drvdata);
}

int cc_clk_on(struct ssi_drvdata *drvdata)
{
	struct clk *clk = drvdata->clk;
	int rc;

	if (IS_ERR(clk))
		/* Not all devices have a clock associated with CCREE  */
		return 0;

	rc = clk_prepare_enable(clk);
	if (rc)
		return rc;

	return 0;
}

void cc_clk_off(struct ssi_drvdata *drvdata)
{
	struct clk *clk = drvdata->clk;

	if (IS_ERR(clk))
		/* Not all devices have a clock associated with CCREE */
		return;

	clk_disable_unprepare(clk);
}

static int cc7x_probe(struct platform_device *plat_dev)
{
	int rc;
	struct device *dev = &plat_dev->dev;
#if defined(CONFIG_ARM) && defined(CC_DEBUG)
	u32 ctr, cacheline_size;

	asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r" (ctr));
	cacheline_size =  4 << ((ctr >> 16) & 0xf);
	dev_dbg(dev, "CP15(L1_CACHE_BYTES) = %u , Kconfig(L1_CACHE_BYTES) = %u\n",
		cacheline_size, L1_CACHE_BYTES);

	asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (ctr));
	dev_dbg(dev, "Main ID register (MIDR): Implementer 0x%02X, Arch 0x%01X, Part 0x%03X, Rev r%dp%d\n",
		(ctr >> 24), (ctr >> 16) & 0xF, (ctr >> 4) & 0xFFF,
		(ctr >> 20) & 0xF, ctr & 0xF);
#endif

	/* Map registers space */
	rc = init_cc_resources(plat_dev);
	if (rc)
		return rc;

	dev_info(dev, "ARM ccree device initialized\n");

	return 0;
}

static int cc7x_remove(struct platform_device *plat_dev)
{
	struct device *dev = &plat_dev->dev;

	dev_dbg(dev, "Releasing cc7x resources...\n");

	cleanup_cc_resources(plat_dev);

	dev_info(dev, "ARM ccree device terminated\n");

	return 0;
}

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
static const struct dev_pm_ops arm_cc7x_driver_pm = {
	SET_RUNTIME_PM_OPS(cc_pm_suspend, cc_pm_resume, NULL)
};
#endif

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
#define	DX_DRIVER_RUNTIME_PM	(&arm_cc7x_driver_pm)
#else
#define	DX_DRIVER_RUNTIME_PM	NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id arm_cc7x_dev_of_match[] = {
	{.compatible = "arm,cryptocell-712-ree"},
	{}
};
MODULE_DEVICE_TABLE(of, arm_cc7x_dev_of_match);
#endif

static struct platform_driver cc7x_driver = {
	.driver = {
		   .name = "cc7xree",
#ifdef CONFIG_OF
		   .of_match_table = arm_cc7x_dev_of_match,
#endif
		   .pm = DX_DRIVER_RUNTIME_PM,
	},
	.probe = cc7x_probe,
	.remove = cc7x_remove,
};
module_platform_driver(cc7x_driver);

/* Module description */
MODULE_DESCRIPTION("ARM TrustZone CryptoCell REE Driver");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_AUTHOR("ARM");
MODULE_LICENSE("GPL v2");

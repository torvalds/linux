// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/crypto.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_address.h>

#include "cc_driver.h"
#include "cc_request_mgr.h"
#include "cc_buffer_mgr.h"
#include "cc_debugfs.h"
#include "cc_cipher.h"
#include "cc_aead.h"
#include "cc_hash.h"
#include "cc_ivgen.h"
#include "cc_sram_mgr.h"
#include "cc_pm.h"
#include "cc_fips.h"

bool cc_dump_desc;
module_param_named(dump_desc, cc_dump_desc, bool, 0600);
MODULE_PARM_DESC(cc_dump_desc, "Dump descriptors to kernel log as debugging aid");

bool cc_dump_bytes;
module_param_named(dump_bytes, cc_dump_bytes, bool, 0600);
MODULE_PARM_DESC(cc_dump_bytes, "Dump buffers to kernel log as debugging aid");

struct cc_hw_data {
	char *name;
	enum cc_hw_rev rev;
	u32 sig;
};

/* Hardware revisions defs. */

static const struct cc_hw_data cc712_hw = {
	.name = "712", .rev = CC_HW_REV_712, .sig =  0xDCC71200U
};

static const struct cc_hw_data cc710_hw = {
	.name = "710", .rev = CC_HW_REV_710, .sig =  0xDCC63200U
};

static const struct cc_hw_data cc630p_hw = {
	.name = "630P", .rev = CC_HW_REV_630, .sig = 0xDCC63000U
};

static const struct of_device_id arm_ccree_dev_of_match[] = {
	{ .compatible = "arm,cryptocell-712-ree", .data = &cc712_hw },
	{ .compatible = "arm,cryptocell-710-ree", .data = &cc710_hw },
	{ .compatible = "arm,cryptocell-630p-ree", .data = &cc630p_hw },
	{}
};
MODULE_DEVICE_TABLE(of, arm_ccree_dev_of_match);

void __dump_byte_array(const char *name, const u8 *buf, size_t len)
{
	char prefix[64];

	if (!buf)
		return;

	snprintf(prefix, sizeof(prefix), "%s[%zu]: ", name, len);

	print_hex_dump(KERN_DEBUG, prefix, DUMP_PREFIX_ADDRESS, 16, 1, buf,
		       len, false);
}

static irqreturn_t cc_isr(int irq, void *dev_id)
{
	struct cc_drvdata *drvdata = (struct cc_drvdata *)dev_id;
	struct device *dev = drvdata_to_dev(drvdata);
	u32 irr;
	u32 imr;

	/* STAT_OP_TYPE_GENERIC STAT_PHASE_0: Interrupt */

	/* read the interrupt status */
	irr = cc_ioread(drvdata, CC_REG(HOST_IRR));
	dev_dbg(dev, "Got IRR=0x%08X\n", irr);
	if (irr == 0) { /* Probably shared interrupt line */
		dev_err(dev, "Got interrupt with empty IRR\n");
		return IRQ_NONE;
	}
	imr = cc_ioread(drvdata, CC_REG(HOST_IMR));

	/* clear interrupt - must be before processing events */
	cc_iowrite(drvdata, CC_REG(HOST_ICR), irr);

	drvdata->irq = irr;
	/* Completion interrupt - most probable */
	if (irr & CC_COMP_IRQ_MASK) {
		/* Mask AXI completion interrupt - will be unmasked in
		 * Deferred service handler
		 */
		cc_iowrite(drvdata, CC_REG(HOST_IMR), imr | CC_COMP_IRQ_MASK);
		irr &= ~CC_COMP_IRQ_MASK;
		complete_request(drvdata);
	}
#ifdef CONFIG_CRYPTO_FIPS
	/* TEE FIPS interrupt */
	if (irr & CC_GPR0_IRQ_MASK) {
		/* Mask interrupt - will be unmasked in Deferred service
		 * handler
		 */
		cc_iowrite(drvdata, CC_REG(HOST_IMR), imr | CC_GPR0_IRQ_MASK);
		irr &= ~CC_GPR0_IRQ_MASK;
		fips_handler(drvdata);
	}
#endif
	/* AXI error interrupt */
	if (irr & CC_AXI_ERR_IRQ_MASK) {
		u32 axi_err;

		/* Read the AXI error ID */
		axi_err = cc_ioread(drvdata, CC_REG(AXIM_MON_ERR));
		dev_dbg(dev, "AXI completion error: axim_mon_err=0x%08X\n",
			axi_err);

		irr &= ~CC_AXI_ERR_IRQ_MASK;
	}

	if (irr) {
		dev_dbg(dev, "IRR includes unknown cause bits (0x%08X)\n",
			irr);
		/* Just warning */
	}

	return IRQ_HANDLED;
}

int init_cc_regs(struct cc_drvdata *drvdata, bool is_probe)
{
	unsigned int val, cache_params;
	struct device *dev = drvdata_to_dev(drvdata);

	/* Unmask all AXI interrupt sources AXI_CFG1 register */
	val = cc_ioread(drvdata, CC_REG(AXIM_CFG));
	cc_iowrite(drvdata, CC_REG(AXIM_CFG), val & ~CC_AXI_IRQ_MASK);
	dev_dbg(dev, "AXIM_CFG=0x%08X\n",
		cc_ioread(drvdata, CC_REG(AXIM_CFG)));

	/* Clear all pending interrupts */
	val = cc_ioread(drvdata, CC_REG(HOST_IRR));
	dev_dbg(dev, "IRR=0x%08X\n", val);
	cc_iowrite(drvdata, CC_REG(HOST_ICR), val);

	/* Unmask relevant interrupt cause */
	val = CC_COMP_IRQ_MASK | CC_AXI_ERR_IRQ_MASK;

	if (drvdata->hw_rev >= CC_HW_REV_712)
		val |= CC_GPR0_IRQ_MASK;

	cc_iowrite(drvdata, CC_REG(HOST_IMR), ~val);

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
	struct cc_drvdata *new_drvdata;
	struct device *dev = &plat_dev->dev;
	struct device_node *np = dev->of_node;
	u32 signature_val;
	u64 dma_mask;
	const struct cc_hw_data *hw_rev;
	const struct of_device_id *dev_id;
	int rc = 0;

	new_drvdata = devm_kzalloc(dev, sizeof(*new_drvdata), GFP_KERNEL);
	if (!new_drvdata)
		return -ENOMEM;

	dev_id = of_match_node(arm_ccree_dev_of_match, np);
	if (!dev_id)
		return -ENODEV;

	hw_rev = (struct cc_hw_data *)dev_id->data;
	new_drvdata->hw_rev_name = hw_rev->name;
	new_drvdata->hw_rev = hw_rev->rev;

	if (hw_rev->rev >= CC_HW_REV_712) {
		new_drvdata->hash_len_sz = HASH_LEN_SIZE_712;
		new_drvdata->axim_mon_offset = CC_REG(AXIM_MON_COMP);
	} else {
		new_drvdata->hash_len_sz = HASH_LEN_SIZE_630;
		new_drvdata->axim_mon_offset = CC_REG(AXIM_MON_COMP8);
	}

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
			      IRQF_SHARED, "ccree", new_drvdata);
	if (rc) {
		dev_err(dev, "Could not register to interrupt %d\n",
			new_drvdata->irq);
		return rc;
	}
	dev_dbg(dev, "Registered to IRQ: %d\n", new_drvdata->irq);

	init_completion(&new_drvdata->hw_queue_avail);

	if (!plat_dev->dev.dma_mask)
		plat_dev->dev.dma_mask = &plat_dev->dev.coherent_dma_mask;

	dma_mask = DMA_BIT_MASK(DMA_BIT_MASK_LEN);
	while (dma_mask > 0x7fffffffUL) {
		if (dma_supported(&plat_dev->dev, dma_mask)) {
			rc = dma_set_coherent_mask(&plat_dev->dev, dma_mask);
			if (!rc)
				break;
		}
		dma_mask >>= 1;
	}

	if (rc) {
		dev_err(dev, "Failed in dma_set_mask, mask=%pad\n", &dma_mask);
		return rc;
	}

	rc = cc_clk_on(new_drvdata);
	if (rc) {
		dev_err(dev, "Failed to enable clock");
		return rc;
	}

	/* Verify correct mapping */
	signature_val = cc_ioread(new_drvdata, CC_REG(HOST_SIGNATURE));
	if (signature_val != hw_rev->sig) {
		dev_err(dev, "Invalid CC signature: SIGNATURE=0x%08X != expected=0x%08X\n",
			signature_val, hw_rev->sig);
		rc = -EINVAL;
		goto post_clk_err;
	}
	dev_dbg(dev, "CC SIGNATURE=0x%08X\n", signature_val);

	/* Display HW versions */
	dev_info(dev, "ARM CryptoCell %s Driver: HW version 0x%08X, Driver version %s\n",
		 hw_rev->name, cc_ioread(new_drvdata, CC_REG(HOST_VERSION)),
		 DRV_MODULE_VERSION);

	rc = init_cc_regs(new_drvdata, true);
	if (rc) {
		dev_err(dev, "init_cc_regs failed\n");
		goto post_clk_err;
	}

	rc = cc_debugfs_init(new_drvdata);
	if (rc) {
		dev_err(dev, "Failed registering debugfs interface\n");
		goto post_regs_err;
	}

	rc = cc_fips_init(new_drvdata);
	if (rc) {
		dev_err(dev, "CC_FIPS_INIT failed 0x%x\n", rc);
		goto post_debugfs_err;
	}
	rc = cc_sram_mgr_init(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_sram_mgr_init failed\n");
		goto post_fips_init_err;
	}

	new_drvdata->mlli_sram_addr =
		cc_sram_alloc(new_drvdata, MAX_MLLI_BUFF_SIZE);
	if (new_drvdata->mlli_sram_addr == NULL_SRAM_ADDR) {
		dev_err(dev, "Failed to alloc MLLI Sram buffer\n");
		rc = -ENOMEM;
		goto post_sram_mgr_err;
	}

	rc = cc_req_mgr_init(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_req_mgr_init failed\n");
		goto post_sram_mgr_err;
	}

	rc = cc_buffer_mgr_init(new_drvdata);
	if (rc) {
		dev_err(dev, "buffer_mgr_init failed\n");
		goto post_req_mgr_err;
	}

	rc = cc_pm_init(new_drvdata);
	if (rc) {
		dev_err(dev, "ssi_power_mgr_init failed\n");
		goto post_buf_mgr_err;
	}

	rc = cc_ivgen_init(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_ivgen_init failed\n");
		goto post_power_mgr_err;
	}

	/* Allocate crypto algs */
	rc = cc_cipher_alloc(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_cipher_alloc failed\n");
		goto post_ivgen_err;
	}

	/* hash must be allocated before aead since hash exports APIs */
	rc = cc_hash_alloc(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_hash_alloc failed\n");
		goto post_cipher_err;
	}

	rc = cc_aead_alloc(new_drvdata);
	if (rc) {
		dev_err(dev, "cc_aead_alloc failed\n");
		goto post_hash_err;
	}

	/* If we got here and FIPS mode is enabled
	 * it means all FIPS test passed, so let TEE
	 * know we're good.
	 */
	cc_set_ree_fips_status(new_drvdata, true);

	return 0;

post_hash_err:
	cc_hash_free(new_drvdata);
post_cipher_err:
	cc_cipher_free(new_drvdata);
post_ivgen_err:
	cc_ivgen_fini(new_drvdata);
post_power_mgr_err:
	cc_pm_fini(new_drvdata);
post_buf_mgr_err:
	 cc_buffer_mgr_fini(new_drvdata);
post_req_mgr_err:
	cc_req_mgr_fini(new_drvdata);
post_sram_mgr_err:
	cc_sram_mgr_fini(new_drvdata);
post_fips_init_err:
	cc_fips_fini(new_drvdata);
post_debugfs_err:
	cc_debugfs_fini(new_drvdata);
post_regs_err:
	fini_cc_regs(new_drvdata);
post_clk_err:
	cc_clk_off(new_drvdata);
	return rc;
}

void fini_cc_regs(struct cc_drvdata *drvdata)
{
	/* Mask all interrupts */
	cc_iowrite(drvdata, CC_REG(HOST_IMR), 0xFFFFFFFF);
}

static void cleanup_cc_resources(struct platform_device *plat_dev)
{
	struct cc_drvdata *drvdata =
		(struct cc_drvdata *)platform_get_drvdata(plat_dev);

	cc_aead_free(drvdata);
	cc_hash_free(drvdata);
	cc_cipher_free(drvdata);
	cc_ivgen_fini(drvdata);
	cc_pm_fini(drvdata);
	cc_buffer_mgr_fini(drvdata);
	cc_req_mgr_fini(drvdata);
	cc_sram_mgr_fini(drvdata);
	cc_fips_fini(drvdata);
	cc_debugfs_fini(drvdata);
	fini_cc_regs(drvdata);
	cc_clk_off(drvdata);
}

int cc_clk_on(struct cc_drvdata *drvdata)
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

void cc_clk_off(struct cc_drvdata *drvdata)
{
	struct clk *clk = drvdata->clk;

	if (IS_ERR(clk))
		/* Not all devices have a clock associated with CCREE */
		return;

	clk_disable_unprepare(clk);
}

static int ccree_probe(struct platform_device *plat_dev)
{
	int rc;
	struct device *dev = &plat_dev->dev;

	/* Map registers space */
	rc = init_cc_resources(plat_dev);
	if (rc)
		return rc;

	dev_info(dev, "ARM ccree device initialized\n");

	return 0;
}

static int ccree_remove(struct platform_device *plat_dev)
{
	struct device *dev = &plat_dev->dev;

	dev_dbg(dev, "Releasing ccree resources...\n");

	cleanup_cc_resources(plat_dev);

	dev_info(dev, "ARM ccree device terminated\n");

	return 0;
}

static struct platform_driver ccree_driver = {
	.driver = {
		   .name = "ccree",
		   .of_match_table = arm_ccree_dev_of_match,
#ifdef CONFIG_PM
		   .pm = &ccree_pm,
#endif
	},
	.probe = ccree_probe,
	.remove = ccree_remove,
};

static int __init ccree_init(void)
{
	int ret;

	cc_hash_global_init();

	ret = cc_debugfs_global_init();
	if (ret)
		return ret;

	return platform_driver_register(&ccree_driver);
}
module_init(ccree_init);

static void __exit ccree_exit(void)
{
	platform_driver_unregister(&ccree_driver);
	cc_debugfs_global_fini();
}
module_exit(ccree_exit);

/* Module description */
MODULE_DESCRIPTION("ARM TrustZone CryptoCell REE Driver");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_AUTHOR("ARM");
MODULE_LICENSE("GPL v2");

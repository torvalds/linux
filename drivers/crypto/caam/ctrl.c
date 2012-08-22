/*
 * CAAM control-plane driver backend
 * Controller-level driver, kernel property detection, initialization
 *
 * Copyright 2008-2012 Freescale Semiconductor, Inc.
 */

#include "compat.h"
#include "regs.h"
#include "intern.h"
#include "jr.h"
#include "desc_constr.h"
#include "error.h"
#include "ctrl.h"

static int caam_remove(struct platform_device *pdev)
{
	struct device *ctrldev;
	struct caam_drv_private *ctrlpriv;
	struct caam_drv_private_jr *jrpriv;
	struct caam_full __iomem *topregs;
	int ring, ret = 0;

	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);
	topregs = (struct caam_full __iomem *)ctrlpriv->ctrl;

	/* shut down JobRs */
	for (ring = 0; ring < ctrlpriv->total_jobrs; ring++) {
		ret |= caam_jr_shutdown(ctrlpriv->jrdev[ring]);
		jrpriv = dev_get_drvdata(ctrlpriv->jrdev[ring]);
		irq_dispose_mapping(jrpriv->irq);
	}

	/* Shut down debug views */
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(ctrlpriv->dfs_root);
#endif

	/* Unmap controller region */
	iounmap(&topregs->ctrl);

	kfree(ctrlpriv->jrdev);
	kfree(ctrlpriv);

	return ret;
}

/*
 * Descriptor to instantiate RNG State Handle 0 in normal mode and
 * load the JDKEK, TDKEK and TDSK registers
 */
static void build_instantiation_desc(u32 *desc)
{
	u32 *jump_cmd;

	init_job_desc(desc, 0);

	/* INIT RNG in non-test mode */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 OP_ALG_AS_INIT);

	/* wait for done */
	jump_cmd = append_jump(desc, JUMP_CLASS_CLASS1);
	set_jump_tgt_here(desc, jump_cmd);

	/*
	 * load 1 to clear written reg:
	 * resets the done interrrupt and returns the RNG to idle.
	 */
	append_load_imm_u32(desc, 1, LDST_SRCDST_WORD_CLRW);

	/* generate secure keys (non-test) */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 OP_ALG_RNG4_SK);
}

struct instantiate_result {
	struct completion completion;
	int err;
};

static void rng4_init_done(struct device *dev, u32 *desc, u32 err,
			   void *context)
{
	struct instantiate_result *instantiation = context;

	if (err) {
		char tmp[CAAM_ERROR_STR_MAX];

		dev_err(dev, "%08x: %s\n", err, caam_jr_strstatus(tmp, err));
	}

	instantiation->err = err;
	complete(&instantiation->completion);
}

static int instantiate_rng(struct device *jrdev)
{
	struct instantiate_result instantiation;

	dma_addr_t desc_dma;
	u32 *desc;
	int ret;

	desc = kmalloc(CAAM_CMD_SZ * 6, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "cannot allocate RNG init descriptor memory\n");
		return -ENOMEM;
	}

	build_instantiation_desc(desc);
	desc_dma = dma_map_single(jrdev, desc, desc_bytes(desc), DMA_TO_DEVICE);
	init_completion(&instantiation.completion);
	ret = caam_jr_enqueue(jrdev, desc, rng4_init_done, &instantiation);
	if (!ret) {
		wait_for_completion_interruptible(&instantiation.completion);
		ret = instantiation.err;
		if (ret)
			dev_err(jrdev, "unable to instantiate RNG\n");
	}

	dma_unmap_single(jrdev, desc_dma, desc_bytes(desc), DMA_TO_DEVICE);

	kfree(desc);

	return ret;
}

/*
 * By default, the TRNG runs for 200 clocks per sample;
 * 800 clocks per sample generates better entropy.
 */
static void kick_trng(struct platform_device *pdev)
{
	struct device *ctrldev = &pdev->dev;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_full __iomem *topregs;
	struct rng4tst __iomem *r4tst;
	u32 val;

	topregs = (struct caam_full __iomem *)ctrlpriv->ctrl;
	r4tst = &topregs->ctrl.r4tst[0];

	/* put RNG4 into program mode */
	setbits32(&r4tst->rtmctl, RTMCTL_PRGM);
	/* 800 clocks per sample */
	val = rd_reg32(&r4tst->rtsdctl);
	val = (val & ~RTSDCTL_ENT_DLY_MASK) | (800 << RTSDCTL_ENT_DLY_SHIFT);
	wr_reg32(&r4tst->rtsdctl, val);
	/* min. freq. count */
	wr_reg32(&r4tst->rtfrqmin, 400);
	/* max. freq. count */
	wr_reg32(&r4tst->rtfrqmax, 6400);
	/* put RNG4 into run mode */
	clrbits32(&r4tst->rtmctl, RTMCTL_PRGM);
}

/**
 * caam_get_era() - Return the ERA of the SEC on SoC, based
 * on the SEC_VID register.
 * Returns the ERA number (1..4) or -ENOTSUPP if the ERA is unknown.
 * @caam_id - the value of the SEC_VID register
 **/
int caam_get_era(u64 caam_id)
{
	struct sec_vid *sec_vid = (struct sec_vid *)&caam_id;
	static const struct {
		u16 ip_id;
		u8 maj_rev;
		u8 era;
	} caam_eras[] = {
		{0x0A10, 1, 1},
		{0x0A10, 2, 2},
		{0x0A12, 1, 3},
		{0x0A14, 1, 3},
		{0x0A14, 2, 4},
		{0x0A16, 1, 4},
		{0x0A11, 1, 4}
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(caam_eras); i++)
		if (caam_eras[i].ip_id == sec_vid->ip_id &&
			caam_eras[i].maj_rev == sec_vid->maj_rev)
				return caam_eras[i].era;

	return -ENOTSUPP;
}
EXPORT_SYMBOL(caam_get_era);

/* Probe routine for CAAM top (controller) level */
static int caam_probe(struct platform_device *pdev)
{
	int ret, ring, rspec;
	u64 caam_id;
	struct device *dev;
	struct device_node *nprop, *np;
	struct caam_ctrl __iomem *ctrl;
	struct caam_full __iomem *topregs;
	struct caam_drv_private *ctrlpriv;
#ifdef CONFIG_DEBUG_FS
	struct caam_perfmon *perfmon;
#endif

	ctrlpriv = kzalloc(sizeof(struct caam_drv_private), GFP_KERNEL);
	if (!ctrlpriv)
		return -ENOMEM;

	dev = &pdev->dev;
	dev_set_drvdata(dev, ctrlpriv);
	ctrlpriv->pdev = pdev;
	nprop = pdev->dev.of_node;

	/* Get configuration properties from device tree */
	/* First, get register page */
	ctrl = of_iomap(nprop, 0);
	if (ctrl == NULL) {
		dev_err(dev, "caam: of_iomap() failed\n");
		return -ENOMEM;
	}
	ctrlpriv->ctrl = (struct caam_ctrl __force *)ctrl;

	/* topregs used to derive pointers to CAAM sub-blocks only */
	topregs = (struct caam_full __iomem *)ctrl;

	/* Get the IRQ of the controller (for security violations only) */
	ctrlpriv->secvio_irq = of_irq_to_resource(nprop, 0, NULL);

	/*
	 * Enable DECO watchdogs and, if this is a PHYS_ADDR_T_64BIT kernel,
	 * long pointers in master configuration register
	 */
	setbits32(&topregs->ctrl.mcr, MCFGR_WDENABLE |
		  (sizeof(dma_addr_t) == sizeof(u64) ? MCFGR_LONG_PTR : 0));

	if (sizeof(dma_addr_t) == sizeof(u64))
		if (of_device_is_compatible(nprop, "fsl,sec-v5.0"))
			dma_set_mask(dev, DMA_BIT_MASK(40));
		else
			dma_set_mask(dev, DMA_BIT_MASK(36));
	else
		dma_set_mask(dev, DMA_BIT_MASK(32));

	/*
	 * Detect and enable JobRs
	 * First, find out how many ring spec'ed, allocate references
	 * for all, then go probe each one.
	 */
	rspec = 0;
	for_each_compatible_node(np, NULL, "fsl,sec-v4.0-job-ring")
		rspec++;
	if (!rspec) {
		/* for backward compatible with device trees */
		for_each_compatible_node(np, NULL, "fsl,sec4.0-job-ring")
			rspec++;
	}

	ctrlpriv->jrdev = kzalloc(sizeof(struct device *) * rspec, GFP_KERNEL);
	if (ctrlpriv->jrdev == NULL) {
		iounmap(&topregs->ctrl);
		return -ENOMEM;
	}

	ring = 0;
	ctrlpriv->total_jobrs = 0;
	for_each_compatible_node(np, NULL, "fsl,sec-v4.0-job-ring") {
		caam_jr_probe(pdev, np, ring);
		ctrlpriv->total_jobrs++;
		ring++;
	}
	if (!ring) {
		for_each_compatible_node(np, NULL, "fsl,sec4.0-job-ring") {
			caam_jr_probe(pdev, np, ring);
			ctrlpriv->total_jobrs++;
			ring++;
		}
	}

	/* Check to see if QI present. If so, enable */
	ctrlpriv->qi_present = !!(rd_reg64(&topregs->ctrl.perfmon.comp_parms) &
				  CTPR_QI_MASK);
	if (ctrlpriv->qi_present) {
		ctrlpriv->qi = (struct caam_queue_if __force *)&topregs->qi;
		/* This is all that's required to physically enable QI */
		wr_reg32(&topregs->qi.qi_control_lo, QICTL_DQEN);
	}

	/* If no QI and no rings specified, quit and go home */
	if ((!ctrlpriv->qi_present) && (!ctrlpriv->total_jobrs)) {
		dev_err(dev, "no queues configured, terminating\n");
		caam_remove(pdev);
		return -ENOMEM;
	}

	/*
	 * RNG4 based SECs (v5+) need special initialization prior
	 * to executing any descriptors
	 */
	if (of_device_is_compatible(nprop, "fsl,sec-v5.0")) {
		kick_trng(pdev);
		ret = instantiate_rng(ctrlpriv->jrdev[0]);
		if (ret) {
			caam_remove(pdev);
			return ret;
		}
	}

	/* NOTE: RTIC detection ought to go here, around Si time */

	/* Initialize queue allocator lock */
	spin_lock_init(&ctrlpriv->jr_alloc_lock);

	caam_id = rd_reg64(&topregs->ctrl.perfmon.caam_id);

	/* Report "alive" for developer to see */
	dev_info(dev, "device ID = 0x%016llx (Era %d)\n", caam_id,
		 caam_get_era(caam_id));
	dev_info(dev, "job rings = %d, qi = %d\n",
		 ctrlpriv->total_jobrs, ctrlpriv->qi_present);

#ifdef CONFIG_DEBUG_FS
	/*
	 * FIXME: needs better naming distinction, as some amalgamation of
	 * "caam" and nprop->full_name. The OF name isn't distinctive,
	 * but does separate instances
	 */
	perfmon = (struct caam_perfmon __force *)&ctrl->perfmon;

	ctrlpriv->dfs_root = debugfs_create_dir("caam", NULL);
	ctrlpriv->ctl = debugfs_create_dir("ctl", ctrlpriv->dfs_root);

	/* Controller-level - performance monitor counters */
	ctrlpriv->ctl_rq_dequeued =
		debugfs_create_u64("rq_dequeued",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->req_dequeued);
	ctrlpriv->ctl_ob_enc_req =
		debugfs_create_u64("ob_rq_encrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_enc_req);
	ctrlpriv->ctl_ib_dec_req =
		debugfs_create_u64("ib_rq_decrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_dec_req);
	ctrlpriv->ctl_ob_enc_bytes =
		debugfs_create_u64("ob_bytes_encrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_enc_bytes);
	ctrlpriv->ctl_ob_prot_bytes =
		debugfs_create_u64("ob_bytes_protected",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_prot_bytes);
	ctrlpriv->ctl_ib_dec_bytes =
		debugfs_create_u64("ib_bytes_decrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_dec_bytes);
	ctrlpriv->ctl_ib_valid_bytes =
		debugfs_create_u64("ib_bytes_validated",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_valid_bytes);

	/* Controller level - global status values */
	ctrlpriv->ctl_faultaddr =
		debugfs_create_u64("fault_addr",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->faultaddr);
	ctrlpriv->ctl_faultdetail =
		debugfs_create_u32("fault_detail",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->faultdetail);
	ctrlpriv->ctl_faultstatus =
		debugfs_create_u32("fault_status",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->status);

	/* Internal covering keys (useful in non-secure mode only) */
	ctrlpriv->ctl_kek_wrap.data = &ctrlpriv->ctrl->kek[0];
	ctrlpriv->ctl_kek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_kek = debugfs_create_blob("kek",
						S_IRUSR |
						S_IRGRP | S_IROTH,
						ctrlpriv->ctl,
						&ctrlpriv->ctl_kek_wrap);

	ctrlpriv->ctl_tkek_wrap.data = &ctrlpriv->ctrl->tkek[0];
	ctrlpriv->ctl_tkek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_tkek = debugfs_create_blob("tkek",
						 S_IRUSR |
						 S_IRGRP | S_IROTH,
						 ctrlpriv->ctl,
						 &ctrlpriv->ctl_tkek_wrap);

	ctrlpriv->ctl_tdsk_wrap.data = &ctrlpriv->ctrl->tdsk[0];
	ctrlpriv->ctl_tdsk_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_tdsk = debugfs_create_blob("tdsk",
						 S_IRUSR |
						 S_IRGRP | S_IROTH,
						 ctrlpriv->ctl,
						 &ctrlpriv->ctl_tdsk_wrap);
#endif
	return 0;
}

static struct of_device_id caam_match[] = {
	{
		.compatible = "fsl,sec-v4.0",
	},
	{
		.compatible = "fsl,sec4.0",
	},
	{},
};
MODULE_DEVICE_TABLE(of, caam_match);

static struct platform_driver caam_driver = {
	.driver = {
		.name = "caam",
		.owner = THIS_MODULE,
		.of_match_table = caam_match,
	},
	.probe       = caam_probe,
	.remove      = __devexit_p(caam_remove),
};

module_platform_driver(caam_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM request backend");
MODULE_AUTHOR("Freescale Semiconductor - NMG/STC");

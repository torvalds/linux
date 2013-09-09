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
	 * resets the done interrupt and returns the RNG to idle.
	 */
	append_load_imm_u32(desc, 1, LDST_SRCDST_WORD_CLRW);

	/* generate secure keys (non-test) */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 OP_ALG_RNG4_SK);

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/* Descriptor for deinstantiation of State Handle 0 of the RNG block. */
static void build_deinstantiation_desc(u32 *desc)
{
	init_job_desc(desc, 0);

	/* Uninstantiate State Handle 0 */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 OP_ALG_AS_INITFINAL);

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/*
 * run_descriptor_deco0 - runs a descriptor on DECO0, under direct control of
 *			  the software (no JR/QI used).
 * @ctrldev - pointer to device
 * Return: - 0 if no error occurred
 *	   - -ENODEV if the DECO couldn't be acquired
 *	   - -EAGAIN if an error occurred while executing the descriptor
 */
static inline int run_descriptor_deco0(struct device *ctrldev, u32 *desc)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_full __iomem *topregs;
	unsigned int timeout = 100000;
	u32 deco_dbg_reg, flags;
	int i;

	/* Set the bit to request direct access to DECO0 */
	topregs = (struct caam_full __iomem *)ctrlpriv->ctrl;
	setbits32(&topregs->ctrl.deco_rq, DECORR_RQD0ENABLE);

	while (!(rd_reg32(&topregs->ctrl.deco_rq) & DECORR_DEN0) &&
								 --timeout)
		cpu_relax();

	if (!timeout) {
		dev_err(ctrldev, "failed to acquire DECO 0\n");
		clrbits32(&topregs->ctrl.deco_rq, DECORR_RQD0ENABLE);
		return -ENODEV;
	}

	for (i = 0; i < desc_len(desc); i++)
		wr_reg32(&topregs->deco.descbuf[i], *(desc + i));

	flags = DECO_JQCR_WHL;
	/*
	 * If the descriptor length is longer than 4 words, then the
	 * FOUR bit in JRCTRL register must be set.
	 */
	if (desc_len(desc) >= 4)
		flags |= DECO_JQCR_FOUR;

	/* Instruct the DECO to execute it */
	wr_reg32(&topregs->deco.jr_ctl_hi, flags);

	timeout = 10000000;
	do {
		deco_dbg_reg = rd_reg32(&topregs->deco.desc_dbg);
		/*
		 * If an error occured in the descriptor, then
		 * the DECO status field will be set to 0x0D
		 */
		if ((deco_dbg_reg & DESC_DBG_DECO_STAT_MASK) ==
		    DESC_DBG_DECO_STAT_HOST_ERR)
			break;
		cpu_relax();
	} while ((deco_dbg_reg & DESC_DBG_DECO_STAT_VALID) && --timeout);

	/* Mark the DECO as free */
	clrbits32(&topregs->ctrl.deco_rq, DECORR_RQD0ENABLE);

	if (!timeout)
		return -EAGAIN;

	return 0;
}

/*
 * instantiate_rng - builds and executes a descriptor on DECO0,
 *		     which initializes the RNG block.
 * @ctrldev - pointer to device
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 *	      f.i. there was a RNG hardware error due to not "good enough"
 *	      entropy being aquired.
 */
static int instantiate_rng(struct device *ctrldev)
{
	u32 *desc;
	int ret = 0;

	desc = kmalloc(CAAM_CMD_SZ * 7, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	/* Create the descriptor for instantiating RNG State Handle 0 */
	build_instantiation_desc(desc);

	/* Try to run it through DECO0 */
	ret = run_descriptor_deco0(ctrldev, desc);

	kfree(desc);

	return ret;
}

/*
 * deinstantiate_rng - builds and executes a descriptor on DECO0,
 *		       which deinitializes the RNG block.
 * @ctrldev - pointer to device
 *
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 */
static int deinstantiate_rng(struct device *ctrldev)
{
	u32 *desc;
	int i, ret = 0;

	desc = kmalloc(CAAM_CMD_SZ * 3, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* Create the descriptor for deinstantating RNG State Handle 0 */
	build_deinstantiation_desc(desc);

	/* Try to run it through DECO0 */
	ret = run_descriptor_deco0(ctrldev, desc);

	if (ret)
		dev_err(ctrldev, "failed to deinstantiate RNG\n");

	kfree(desc);

	return ret;
}

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

	/* De-initialize RNG if it was initialized by this driver. */
	if (ctrlpriv->rng4_init)
		deinstantiate_rng(ctrldev);

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
 * kick_trng - sets the various parameters for enabling the initialization
 *	       of the RNG4 block in CAAM
 * @pdev - pointer to the platform device
 * @ent_delay - Defines the length (in system clocks) of each entropy sample.
 */
static void kick_trng(struct platform_device *pdev, int ent_delay)
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

	/*
	 * Performance-wise, it does not make sense to
	 * set the delay to a value that is lower
	 * than the last one that worked (i.e. the state handles
	 * were instantiated properly. Thus, instead of wasting
	 * time trying to set the values controlling the sample
	 * frequency, the function simply returns.
	 */
	val = (rd_reg32(&r4tst->rtsdctl) & RTSDCTL_ENT_DLY_MASK)
	      >> RTSDCTL_ENT_DLY_SHIFT;
	if (ent_delay <= val) {
		/* put RNG4 into run mode */
		clrbits32(&r4tst->rtmctl, RTMCTL_PRGM);
		return;
	}

	val = rd_reg32(&r4tst->rtsdctl);
	val = (val & ~RTSDCTL_ENT_DLY_MASK) |
	      (ent_delay << RTSDCTL_ENT_DLY_SHIFT);
	wr_reg32(&r4tst->rtsdctl, val);
	/* min. freq. count, equal to 1/4 of the entropy sample length */
	wr_reg32(&r4tst->rtfrqmin, ent_delay >> 2);
	/* max. freq. count, equal to 8 times the entropy sample length */
	wr_reg32(&r4tst->rtfrqmax, ent_delay << 3);
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
	int ret, ring, rspec, ent_delay = RTSDCTL_ENT_DLY_MIN;
	u64 caam_id;
	struct device *dev;
	struct device_node *nprop, *np;
	struct caam_ctrl __iomem *ctrl;
	struct caam_full __iomem *topregs;
	struct caam_drv_private *ctrlpriv;
#ifdef CONFIG_DEBUG_FS
	struct caam_perfmon *perfmon;
#endif
	u64 cha_vid;

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

	cha_vid = rd_reg64(&topregs->ctrl.perfmon.cha_id);

	/*
	 * If SEC has RNG version >= 4 and RNG state handle has not been
	 * already instantiated, do RNG instantiation
	 */
	if ((cha_vid & CHA_ID_RNG_MASK) >> CHA_ID_RNG_SHIFT >= 4 &&
	    !(rd_reg32(&topregs->ctrl.r4tst[0].rdsta) & RDSTA_IF0)) {
		do {
			kick_trng(pdev, ent_delay);
			ret = instantiate_rng(dev);
			ent_delay += 400;
		} while ((ret == -EAGAIN) && (ent_delay < RTSDCTL_ENT_DLY_MAX));
		if (ret) {
			dev_err(dev, "failed to instantiate RNG");
			caam_remove(pdev);
			return ret;
		}

		ctrlpriv->rng4_init = 1;

		/* Enable RDB bit so that RNG works faster */
		setbits32(&topregs->ctrl.scfgr, SCFGR_RDBENABLE);
	}

	/* NOTE: RTIC detection ought to go here, around Si time */

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
	.remove      = caam_remove,
};

module_platform_driver(caam_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM request backend");
MODULE_AUTHOR("Freescale Semiconductor - NMG/STC");

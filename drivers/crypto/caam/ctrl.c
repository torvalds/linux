// SPDX-License-Identifier: GPL-2.0+
/* * CAAM control-plane driver backend
 * Controller-level driver, kernel property detection, initialization
 *
 * Copyright 2008-2012 Freescale Semiconductor, Inc.
 * Copyright 2018-2019, 2023 NXP
 */

#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sys_soc.h>
#include <linux/fsl/mc.h>

#include "compat.h"
#include "debugfs.h"
#include "regs.h"
#include "intern.h"
#include "jr.h"
#include "desc_constr.h"
#include "ctrl.h"

bool caam_dpaa2;
EXPORT_SYMBOL(caam_dpaa2);

#ifdef CONFIG_CAAM_QI
#include "qi.h"
#endif

/*
 * Descriptor to instantiate RNG State Handle 0 in normal mode and
 * load the JDKEK, TDKEK and TDSK registers
 */
static void build_instantiation_desc(u32 *desc, int handle, int do_sk)
{
	u32 *jump_cmd, op_flags;

	init_job_desc(desc, 0);

	op_flags = OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			(handle << OP_ALG_AAI_SHIFT) | OP_ALG_AS_INIT |
			OP_ALG_PR_ON;

	/* INIT RNG in non-test mode */
	append_operation(desc, op_flags);

	if (!handle && do_sk) {
		/*
		 * For SH0, Secure Keys must be generated as well
		 */

		/* wait for done */
		jump_cmd = append_jump(desc, JUMP_CLASS_CLASS1);
		set_jump_tgt_here(desc, jump_cmd);

		/*
		 * load 1 to clear written reg:
		 * resets the done interrupt and returns the RNG to idle.
		 */
		append_load_imm_u32(desc, 1, LDST_SRCDST_WORD_CLRW);

		/* Initialize State Handle  */
		append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
				 OP_ALG_AAI_RNG4_SK);
	}

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/* Descriptor for deinstantiation of State Handle 0 of the RNG block. */
static void build_deinstantiation_desc(u32 *desc, int handle)
{
	init_job_desc(desc, 0);

	/* Uninstantiate State Handle 0 */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 (handle << OP_ALG_AAI_SHIFT) | OP_ALG_AS_INITFINAL);

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/*
 * run_descriptor_deco0 - runs a descriptor on DECO0, under direct control of
 *			  the software (no JR/QI used).
 * @ctrldev - pointer to device
 * @status - descriptor status, after being run
 *
 * Return: - 0 if no error occurred
 *	   - -ENODEV if the DECO couldn't be acquired
 *	   - -EAGAIN if an error occurred while executing the descriptor
 */
static inline int run_descriptor_deco0(struct device *ctrldev, u32 *desc,
					u32 *status)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_ctrl __iomem *ctrl = ctrlpriv->ctrl;
	struct caam_deco __iomem *deco = ctrlpriv->deco;
	unsigned int timeout = 100000;
	u32 deco_dbg_reg, deco_state, flags;
	int i;


	if (ctrlpriv->virt_en == 1 ||
	    /*
	     * Apparently on i.MX8M{Q,M,N,P} it doesn't matter if virt_en == 1
	     * and the following steps should be performed regardless
	     */
	    of_machine_is_compatible("fsl,imx8mq") ||
	    of_machine_is_compatible("fsl,imx8mm") ||
	    of_machine_is_compatible("fsl,imx8mn") ||
	    of_machine_is_compatible("fsl,imx8mp")) {
		clrsetbits_32(&ctrl->deco_rsr, 0, DECORSR_JR0);

		while (!(rd_reg32(&ctrl->deco_rsr) & DECORSR_VALID) &&
		       --timeout)
			cpu_relax();

		timeout = 100000;
	}

	clrsetbits_32(&ctrl->deco_rq, 0, DECORR_RQD0ENABLE);

	while (!(rd_reg32(&ctrl->deco_rq) & DECORR_DEN0) &&
								 --timeout)
		cpu_relax();

	if (!timeout) {
		dev_err(ctrldev, "failed to acquire DECO 0\n");
		clrsetbits_32(&ctrl->deco_rq, DECORR_RQD0ENABLE, 0);
		return -ENODEV;
	}

	for (i = 0; i < desc_len(desc); i++)
		wr_reg32(&deco->descbuf[i], caam32_to_cpu(*(desc + i)));

	flags = DECO_JQCR_WHL;
	/*
	 * If the descriptor length is longer than 4 words, then the
	 * FOUR bit in JRCTRL register must be set.
	 */
	if (desc_len(desc) >= 4)
		flags |= DECO_JQCR_FOUR;

	/* Instruct the DECO to execute it */
	clrsetbits_32(&deco->jr_ctl_hi, 0, flags);

	timeout = 10000000;
	do {
		deco_dbg_reg = rd_reg32(&deco->desc_dbg);

		if (ctrlpriv->era < 10)
			deco_state = (deco_dbg_reg & DESC_DBG_DECO_STAT_MASK) >>
				     DESC_DBG_DECO_STAT_SHIFT;
		else
			deco_state = (rd_reg32(&deco->dbg_exec) &
				      DESC_DER_DECO_STAT_MASK) >>
				     DESC_DER_DECO_STAT_SHIFT;

		/*
		 * If an error occurred in the descriptor, then
		 * the DECO status field will be set to 0x0D
		 */
		if (deco_state == DECO_STAT_HOST_ERR)
			break;

		cpu_relax();
	} while ((deco_dbg_reg & DESC_DBG_DECO_STAT_VALID) && --timeout);

	*status = rd_reg32(&deco->op_status_hi) &
		  DECO_OP_STATUS_HI_ERR_MASK;

	if (ctrlpriv->virt_en == 1)
		clrsetbits_32(&ctrl->deco_rsr, DECORSR_JR0, 0);

	/* Mark the DECO as free */
	clrsetbits_32(&ctrl->deco_rq, DECORR_RQD0ENABLE, 0);

	if (!timeout)
		return -EAGAIN;

	return 0;
}

/*
 * deinstantiate_rng - builds and executes a descriptor on DECO0,
 *		       which deinitializes the RNG block.
 * @ctrldev - pointer to device
 * @state_handle_mask - bitmask containing the instantiation status
 *			for the RNG4 state handles which exist in
 *			the RNG4 block: 1 if it's been instantiated
 *
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 */
static int deinstantiate_rng(struct device *ctrldev, int state_handle_mask)
{
	u32 *desc, status;
	int sh_idx, ret = 0;

	desc = kmalloc(CAAM_CMD_SZ * 3, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (sh_idx = 0; sh_idx < RNG4_MAX_HANDLES; sh_idx++) {
		/*
		 * If the corresponding bit is set, then it means the state
		 * handle was initialized by us, and thus it needs to be
		 * deinitialized as well
		 */
		if ((1 << sh_idx) & state_handle_mask) {
			/*
			 * Create the descriptor for deinstantating this state
			 * handle
			 */
			build_deinstantiation_desc(desc, sh_idx);

			/* Try to run it through DECO0 */
			ret = run_descriptor_deco0(ctrldev, desc, &status);

			if (ret ||
			    (status && status != JRSTA_SSRC_JUMP_HALT_CC)) {
				dev_err(ctrldev,
					"Failed to deinstantiate RNG4 SH%d\n",
					sh_idx);
				break;
			}
			dev_info(ctrldev, "Deinstantiated RNG4 SH%d\n", sh_idx);
		}
	}

	kfree(desc);

	return ret;
}

static void devm_deinstantiate_rng(void *data)
{
	struct device *ctrldev = data;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);

	/*
	 * De-initialize RNG state handles initialized by this driver.
	 * In case of SoCs with Management Complex, RNG is managed by MC f/w.
	 */
	if (ctrlpriv->rng4_sh_init)
		deinstantiate_rng(ctrldev, ctrlpriv->rng4_sh_init);
}

/*
 * instantiate_rng - builds and executes a descriptor on DECO0,
 *		     which initializes the RNG block.
 * @ctrldev - pointer to device
 * @state_handle_mask - bitmask containing the instantiation status
 *			for the RNG4 state handles which exist in
 *			the RNG4 block: 1 if it's been instantiated
 *			by an external entry, 0 otherwise.
 * @gen_sk  - generate data to be loaded into the JDKEK, TDKEK and TDSK;
 *	      Caution: this can be done only once; if the keys need to be
 *	      regenerated, a POR is required
 *
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 *	      f.i. there was a RNG hardware error due to not "good enough"
 *	      entropy being acquired.
 */
static int instantiate_rng(struct device *ctrldev, int state_handle_mask,
			   int gen_sk)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_ctrl __iomem *ctrl;
	u32 *desc, status = 0, rdsta_val;
	int ret = 0, sh_idx;

	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;
	desc = kmalloc(CAAM_CMD_SZ * 7, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (sh_idx = 0; sh_idx < RNG4_MAX_HANDLES; sh_idx++) {
		const u32 rdsta_if = RDSTA_IF0 << sh_idx;
		const u32 rdsta_pr = RDSTA_PR0 << sh_idx;
		const u32 rdsta_mask = rdsta_if | rdsta_pr;

		/* Clear the contents before using the descriptor */
		memset(desc, 0x00, CAAM_CMD_SZ * 7);

		/*
		 * If the corresponding bit is set, this state handle
		 * was initialized by somebody else, so it's left alone.
		 */
		if (rdsta_if & state_handle_mask) {
			if (rdsta_pr & state_handle_mask)
				continue;

			dev_info(ctrldev,
				 "RNG4 SH%d was previously instantiated without prediction resistance. Tearing it down\n",
				 sh_idx);

			ret = deinstantiate_rng(ctrldev, rdsta_if);
			if (ret)
				break;
		}

		/* Create the descriptor for instantiating RNG State Handle */
		build_instantiation_desc(desc, sh_idx, gen_sk);

		/* Try to run it through DECO0 */
		ret = run_descriptor_deco0(ctrldev, desc, &status);

		/*
		 * If ret is not 0, or descriptor status is not 0, then
		 * something went wrong. No need to try the next state
		 * handle (if available), bail out here.
		 * Also, if for some reason, the State Handle didn't get
		 * instantiated although the descriptor has finished
		 * without any error (HW optimizations for later
		 * CAAM eras), then try again.
		 */
		if (ret)
			break;

		rdsta_val = rd_reg32(&ctrl->r4tst[0].rdsta) & RDSTA_MASK;
		if ((status && status != JRSTA_SSRC_JUMP_HALT_CC) ||
		    (rdsta_val & rdsta_mask) != rdsta_mask) {
			ret = -EAGAIN;
			break;
		}

		dev_info(ctrldev, "Instantiated RNG4 SH%d\n", sh_idx);
	}

	kfree(desc);

	if (ret)
		return ret;

	return devm_add_action_or_reset(ctrldev, devm_deinstantiate_rng, ctrldev);
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
	struct caam_ctrl __iomem *ctrl;
	struct rng4tst __iomem *r4tst;
	u32 val;

	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;
	r4tst = &ctrl->r4tst[0];

	/*
	 * Setting both RTMCTL:PRGM and RTMCTL:TRNG_ACC causes TRNG to
	 * properly invalidate the entropy in the entropy register and
	 * force re-generation.
	 */
	clrsetbits_32(&r4tst->rtmctl, 0, RTMCTL_PRGM | RTMCTL_ACC);

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
	if (ent_delay <= val)
		goto start_rng;

	val = rd_reg32(&r4tst->rtsdctl);
	val = (val & ~RTSDCTL_ENT_DLY_MASK) |
	      (ent_delay << RTSDCTL_ENT_DLY_SHIFT);
	wr_reg32(&r4tst->rtsdctl, val);
	/* min. freq. count, equal to 1/4 of the entropy sample length */
	wr_reg32(&r4tst->rtfrqmin, ent_delay >> 2);
	/* disable maximum frequency count */
	wr_reg32(&r4tst->rtfrqmax, RTFRQMAX_DISABLE);
	/* read the control register */
	val = rd_reg32(&r4tst->rtmctl);
start_rng:
	/*
	 * select raw sampling in both entropy shifter
	 * and statistical checker; ; put RNG4 into run mode
	 */
	clrsetbits_32(&r4tst->rtmctl, RTMCTL_PRGM | RTMCTL_ACC,
		      RTMCTL_SAMP_MODE_RAW_ES_SC);
}

static int caam_get_era_from_hw(struct caam_perfmon __iomem *perfmon)
{
	static const struct {
		u16 ip_id;
		u8 maj_rev;
		u8 era;
	} id[] = {
		{0x0A10, 1, 1},
		{0x0A10, 2, 2},
		{0x0A12, 1, 3},
		{0x0A14, 1, 3},
		{0x0A14, 2, 4},
		{0x0A16, 1, 4},
		{0x0A10, 3, 4},
		{0x0A11, 1, 4},
		{0x0A18, 1, 4},
		{0x0A11, 2, 5},
		{0x0A12, 2, 5},
		{0x0A13, 1, 5},
		{0x0A1C, 1, 5}
	};
	u32 ccbvid, id_ms;
	u8 maj_rev, era;
	u16 ip_id;
	int i;

	ccbvid = rd_reg32(&perfmon->ccb_id);
	era = (ccbvid & CCBVID_ERA_MASK) >> CCBVID_ERA_SHIFT;
	if (era)	/* This is '0' prior to CAAM ERA-6 */
		return era;

	id_ms = rd_reg32(&perfmon->caam_id_ms);
	ip_id = (id_ms & SECVID_MS_IPID_MASK) >> SECVID_MS_IPID_SHIFT;
	maj_rev = (id_ms & SECVID_MS_MAJ_REV_MASK) >> SECVID_MS_MAJ_REV_SHIFT;

	for (i = 0; i < ARRAY_SIZE(id); i++)
		if (id[i].ip_id == ip_id && id[i].maj_rev == maj_rev)
			return id[i].era;

	return -ENOTSUPP;
}

/**
 * caam_get_era() - Return the ERA of the SEC on SoC, based
 * on "sec-era" optional property in the DTS. This property is updated
 * by u-boot.
 * In case this property is not passed an attempt to retrieve the CAAM
 * era via register reads will be made.
 *
 * @perfmon:	Performance Monitor Registers
 */
static int caam_get_era(struct caam_perfmon __iomem *perfmon)
{
	struct device_node *caam_node;
	int ret;
	u32 prop;

	caam_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	ret = of_property_read_u32(caam_node, "fsl,sec-era", &prop);
	of_node_put(caam_node);

	if (!ret)
		return prop;
	else
		return caam_get_era_from_hw(perfmon);
}

/*
 * ERRATA: imx6 devices (imx6D, imx6Q, imx6DL, imx6S, imx6DP and imx6QP)
 * have an issue wherein AXI bus transactions may not occur in the correct
 * order. This isn't a problem running single descriptors, but can be if
 * running multiple concurrent descriptors. Reworking the driver to throttle
 * to single requests is impractical, thus the workaround is to limit the AXI
 * pipeline to a depth of 1 (from it's default of 4) to preclude this situation
 * from occurring.
 */
static void handle_imx6_err005766(u32 __iomem *mcr)
{
	if (of_machine_is_compatible("fsl,imx6q") ||
	    of_machine_is_compatible("fsl,imx6dl") ||
	    of_machine_is_compatible("fsl,imx6qp"))
		clrsetbits_32(mcr, MCFGR_AXIPIPE_MASK,
			      1 << MCFGR_AXIPIPE_SHIFT);
}

static const struct of_device_id caam_match[] = {
	{
		.compatible = "fsl,sec-v4.0",
	},
	{
		.compatible = "fsl,sec4.0",
	},
	{},
};
MODULE_DEVICE_TABLE(of, caam_match);

struct caam_imx_data {
	const struct clk_bulk_data *clks;
	int num_clks;
};

static const struct clk_bulk_data caam_imx6_clks[] = {
	{ .id = "ipg" },
	{ .id = "mem" },
	{ .id = "aclk" },
	{ .id = "emi_slow" },
};

static const struct caam_imx_data caam_imx6_data = {
	.clks = caam_imx6_clks,
	.num_clks = ARRAY_SIZE(caam_imx6_clks),
};

static const struct clk_bulk_data caam_imx7_clks[] = {
	{ .id = "ipg" },
	{ .id = "aclk" },
};

static const struct caam_imx_data caam_imx7_data = {
	.clks = caam_imx7_clks,
	.num_clks = ARRAY_SIZE(caam_imx7_clks),
};

static const struct clk_bulk_data caam_imx6ul_clks[] = {
	{ .id = "ipg" },
	{ .id = "mem" },
	{ .id = "aclk" },
};

static const struct caam_imx_data caam_imx6ul_data = {
	.clks = caam_imx6ul_clks,
	.num_clks = ARRAY_SIZE(caam_imx6ul_clks),
};

static const struct clk_bulk_data caam_vf610_clks[] = {
	{ .id = "ipg" },
};

static const struct caam_imx_data caam_vf610_data = {
	.clks = caam_vf610_clks,
	.num_clks = ARRAY_SIZE(caam_vf610_clks),
};

static const struct soc_device_attribute caam_imx_soc_table[] = {
	{ .soc_id = "i.MX6UL", .data = &caam_imx6ul_data },
	{ .soc_id = "i.MX6*",  .data = &caam_imx6_data },
	{ .soc_id = "i.MX7*",  .data = &caam_imx7_data },
	{ .soc_id = "i.MX8M*", .data = &caam_imx7_data },
	{ .soc_id = "VF*",     .data = &caam_vf610_data },
	{ .family = "Freescale i.MX" },
	{ /* sentinel */ }
};

static void disable_clocks(void *data)
{
	struct caam_drv_private *ctrlpriv = data;

	clk_bulk_disable_unprepare(ctrlpriv->num_clks, ctrlpriv->clks);
}

static int init_clocks(struct device *dev, const struct caam_imx_data *data)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(dev);
	int ret;

	ctrlpriv->num_clks = data->num_clks;
	ctrlpriv->clks = devm_kmemdup(dev, data->clks,
				      data->num_clks * sizeof(data->clks[0]),
				      GFP_KERNEL);
	if (!ctrlpriv->clks)
		return -ENOMEM;

	ret = devm_clk_bulk_get(dev, ctrlpriv->num_clks, ctrlpriv->clks);
	if (ret) {
		dev_err(dev,
			"Failed to request all necessary clocks\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(ctrlpriv->num_clks, ctrlpriv->clks);
	if (ret) {
		dev_err(dev,
			"Failed to prepare/enable all necessary clocks\n");
		return ret;
	}

	return devm_add_action_or_reset(dev, disable_clocks, ctrlpriv);
}

static void caam_remove_debugfs(void *root)
{
	debugfs_remove_recursive(root);
}

#ifdef CONFIG_FSL_MC_BUS
static bool check_version(struct fsl_mc_version *mc_version, u32 major,
			  u32 minor, u32 revision)
{
	if (mc_version->major > major)
		return true;

	if (mc_version->major == major) {
		if (mc_version->minor > minor)
			return true;

		if (mc_version->minor == minor &&
		    mc_version->revision > revision)
			return true;
	}

	return false;
}
#endif

static bool needs_entropy_delay_adjustment(void)
{
	if (of_machine_is_compatible("fsl,imx6sx"))
		return true;
	return false;
}

/* Probe routine for CAAM top (controller) level */
static int caam_probe(struct platform_device *pdev)
{
	int ret, ring, gen_sk, ent_delay = RTSDCTL_ENT_DLY_MIN;
	u64 caam_id;
	const struct soc_device_attribute *imx_soc_match;
	struct device *dev;
	struct device_node *nprop, *np;
	struct caam_ctrl __iomem *ctrl;
	struct caam_drv_private *ctrlpriv;
	struct caam_perfmon __iomem *perfmon;
	struct dentry *dfs_root;
	u32 scfgr, comp_params;
	u8 rng_vid;
	int pg_size;
	int BLOCK_OFFSET = 0;
	bool pr_support = false;
	bool reg_access = true;

	ctrlpriv = devm_kzalloc(&pdev->dev, sizeof(*ctrlpriv), GFP_KERNEL);
	if (!ctrlpriv)
		return -ENOMEM;

	dev = &pdev->dev;
	dev_set_drvdata(dev, ctrlpriv);
	nprop = pdev->dev.of_node;

	imx_soc_match = soc_device_match(caam_imx_soc_table);
	caam_imx = (bool)imx_soc_match;

	if (imx_soc_match) {
		/*
		 * Until Layerscape and i.MX OP-TEE get in sync,
		 * only i.MX OP-TEE use cases disallow access to
		 * caam page 0 (controller) registers.
		 */
		np = of_find_compatible_node(NULL, NULL, "linaro,optee-tz");
		ctrlpriv->optee_en = !!np;
		of_node_put(np);

		reg_access = !ctrlpriv->optee_en;

		if (!imx_soc_match->data) {
			dev_err(dev, "No clock data provided for i.MX SoC");
			return -EINVAL;
		}

		ret = init_clocks(dev, imx_soc_match->data);
		if (ret)
			return ret;
	}


	/* Get configuration properties from device tree */
	/* First, get register page */
	ctrl = devm_of_iomap(dev, nprop, 0, NULL);
	ret = PTR_ERR_OR_ZERO(ctrl);
	if (ret) {
		dev_err(dev, "caam: of_iomap() failed\n");
		return ret;
	}

	ring = 0;
	for_each_available_child_of_node(nprop, np)
		if (of_device_is_compatible(np, "fsl,sec-v4.0-job-ring") ||
		    of_device_is_compatible(np, "fsl,sec4.0-job-ring")) {
			u32 reg;

			if (of_property_read_u32_index(np, "reg", 0, &reg)) {
				dev_err(dev, "%s read reg property error\n",
					np->full_name);
				continue;
			}

			ctrlpriv->jr[ring] = (struct caam_job_ring __iomem __force *)
					     ((__force uint8_t *)ctrl + reg);

			ctrlpriv->total_jobrs++;
			ring++;
		}

	/*
	 * Wherever possible, instead of accessing registers from the global page,
	 * use the alias registers in the first (cf. DT nodes order)
	 * job ring's page.
	 */
	perfmon = ring ? (struct caam_perfmon __iomem *)&ctrlpriv->jr[0]->perfmon :
			 (struct caam_perfmon __iomem *)&ctrl->perfmon;

	caam_little_end = !(bool)(rd_reg32(&perfmon->status) &
				  (CSTA_PLEND | CSTA_ALT_PLEND));
	comp_params = rd_reg32(&perfmon->comp_parms_ms);
	if (reg_access && comp_params & CTPR_MS_PS &&
	    rd_reg32(&ctrl->mcr) & MCFGR_LONG_PTR)
		caam_ptr_sz = sizeof(u64);
	else
		caam_ptr_sz = sizeof(u32);
	caam_dpaa2 = !!(comp_params & CTPR_MS_DPAA2);
	ctrlpriv->qi_present = !!(comp_params & CTPR_MS_QI_MASK);

#ifdef CONFIG_CAAM_QI
	/* If (DPAA 1.x) QI present, check whether dependencies are available */
	if (ctrlpriv->qi_present && !caam_dpaa2) {
		ret = qman_is_probed();
		if (!ret) {
			return -EPROBE_DEFER;
		} else if (ret < 0) {
			dev_err(dev, "failing probe due to qman probe error\n");
			return -ENODEV;
		}

		ret = qman_portals_probed();
		if (!ret) {
			return -EPROBE_DEFER;
		} else if (ret < 0) {
			dev_err(dev, "failing probe due to qman portals probe error\n");
			return -ENODEV;
		}
	}
#endif

	/* Allocating the BLOCK_OFFSET based on the supported page size on
	 * the platform
	 */
	pg_size = (comp_params & CTPR_MS_PG_SZ_MASK) >> CTPR_MS_PG_SZ_SHIFT;
	if (pg_size == 0)
		BLOCK_OFFSET = PG_SIZE_4K;
	else
		BLOCK_OFFSET = PG_SIZE_64K;

	ctrlpriv->ctrl = (struct caam_ctrl __iomem __force *)ctrl;
	ctrlpriv->assure = (struct caam_assurance __iomem __force *)
			   ((__force uint8_t *)ctrl +
			    BLOCK_OFFSET * ASSURE_BLOCK_NUMBER
			   );
	ctrlpriv->deco = (struct caam_deco __iomem __force *)
			 ((__force uint8_t *)ctrl +
			 BLOCK_OFFSET * DECO_BLOCK_NUMBER
			 );

	/* Get the IRQ of the controller (for security violations only) */
	ctrlpriv->secvio_irq = irq_of_parse_and_map(nprop, 0);
	np = of_find_compatible_node(NULL, NULL, "fsl,qoriq-mc");
	ctrlpriv->mc_en = !!np;
	of_node_put(np);

#ifdef CONFIG_FSL_MC_BUS
	if (ctrlpriv->mc_en) {
		struct fsl_mc_version *mc_version;

		mc_version = fsl_mc_get_version();
		if (mc_version)
			pr_support = check_version(mc_version, 10, 20, 0);
		else
			return -EPROBE_DEFER;
	}
#endif

	if (!reg_access)
		goto set_dma_mask;

	/*
	 * Enable DECO watchdogs and, if this is a PHYS_ADDR_T_64BIT kernel,
	 * long pointers in master configuration register.
	 * In case of SoCs with Management Complex, MC f/w performs
	 * the configuration.
	 */
	if (!ctrlpriv->mc_en)
		clrsetbits_32(&ctrl->mcr, MCFGR_AWCACHE_MASK,
			      MCFGR_AWCACHE_CACH | MCFGR_AWCACHE_BUFF |
			      MCFGR_WDENABLE | MCFGR_LARGE_BURST);

	handle_imx6_err005766(&ctrl->mcr);

	/*
	 *  Read the Compile Time parameters and SCFGR to determine
	 * if virtualization is enabled for this platform
	 */
	scfgr = rd_reg32(&ctrl->scfgr);

	ctrlpriv->virt_en = 0;
	if (comp_params & CTPR_MS_VIRT_EN_INCL) {
		/* VIRT_EN_INCL = 1 & VIRT_EN_POR = 1 or
		 * VIRT_EN_INCL = 1 & VIRT_EN_POR = 0 & SCFGR_VIRT_EN = 1
		 */
		if ((comp_params & CTPR_MS_VIRT_EN_POR) ||
		    (!(comp_params & CTPR_MS_VIRT_EN_POR) &&
		       (scfgr & SCFGR_VIRT_EN)))
				ctrlpriv->virt_en = 1;
	} else {
		/* VIRT_EN_INCL = 0 && VIRT_EN_POR_VALUE = 1 */
		if (comp_params & CTPR_MS_VIRT_EN_POR)
				ctrlpriv->virt_en = 1;
	}

	if (ctrlpriv->virt_en == 1)
		clrsetbits_32(&ctrl->jrstart, 0, JRSTART_JR0_START |
			      JRSTART_JR1_START | JRSTART_JR2_START |
			      JRSTART_JR3_START);

set_dma_mask:
	ret = dma_set_mask_and_coherent(dev, caam_get_dma_mask(dev));
	if (ret) {
		dev_err(dev, "dma_set_mask_and_coherent failed (%d)\n", ret);
		return ret;
	}

	ctrlpriv->era = caam_get_era(perfmon);
	ctrlpriv->domain = iommu_get_domain_for_dev(dev);

	dfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		ret = devm_add_action_or_reset(dev, caam_remove_debugfs,
					       dfs_root);
		if (ret)
			return ret;
	}

	caam_debugfs_init(ctrlpriv, perfmon, dfs_root);

	/* Check to see if (DPAA 1.x) QI present. If so, enable */
	if (ctrlpriv->qi_present && !caam_dpaa2) {
		ctrlpriv->qi = (struct caam_queue_if __iomem __force *)
			       ((__force uint8_t *)ctrl +
				 BLOCK_OFFSET * QI_BLOCK_NUMBER
			       );
		/* This is all that's required to physically enable QI */
		wr_reg32(&ctrlpriv->qi->qi_control_lo, QICTL_DQEN);

		/* If QMAN driver is present, init CAAM-QI backend */
#ifdef CONFIG_CAAM_QI
		ret = caam_qi_init(pdev);
		if (ret)
			dev_err(dev, "caam qi i/f init failed: %d\n", ret);
#endif
	}

	/* If no QI and no rings specified, quit and go home */
	if ((!ctrlpriv->qi_present) && (!ctrlpriv->total_jobrs)) {
		dev_err(dev, "no queues configured, terminating\n");
		return -ENOMEM;
	}

	if (!reg_access)
		goto report_live;

	comp_params = rd_reg32(&perfmon->comp_parms_ls);
	ctrlpriv->blob_present = !!(comp_params & CTPR_LS_BLOB);

	/*
	 * Some SoCs like the LS1028A (non-E) indicate CTPR_LS_BLOB support,
	 * but fail when actually using it due to missing AES support, so
	 * check both here.
	 */
	if (ctrlpriv->era < 10) {
		rng_vid = (rd_reg32(&perfmon->cha_id_ls) &
			   CHA_ID_LS_RNG_MASK) >> CHA_ID_LS_RNG_SHIFT;
		ctrlpriv->blob_present = ctrlpriv->blob_present &&
			(rd_reg32(&perfmon->cha_num_ls) & CHA_ID_LS_AES_MASK);
	} else {
		struct version_regs __iomem *vreg;

		vreg =  ctrlpriv->total_jobrs ?
			(struct version_regs __iomem *)&ctrlpriv->jr[0]->vreg :
			(struct version_regs __iomem *)&ctrl->vreg;

		rng_vid = (rd_reg32(&vreg->rng) & CHA_VER_VID_MASK) >>
			   CHA_VER_VID_SHIFT;
		ctrlpriv->blob_present = ctrlpriv->blob_present &&
			(rd_reg32(&vreg->aesa) & CHA_VER_MISC_AES_NUM_MASK);
	}

	/*
	 * If SEC has RNG version >= 4 and RNG state handle has not been
	 * already instantiated, do RNG instantiation
	 * In case of SoCs with Management Complex, RNG is managed by MC f/w.
	 */
	if (!(ctrlpriv->mc_en && pr_support) && rng_vid >= 4) {
		ctrlpriv->rng4_sh_init =
			rd_reg32(&ctrl->r4tst[0].rdsta);
		/*
		 * If the secure keys (TDKEK, JDKEK, TDSK), were already
		 * generated, signal this to the function that is instantiating
		 * the state handles. An error would occur if RNG4 attempts
		 * to regenerate these keys before the next POR.
		 */
		gen_sk = ctrlpriv->rng4_sh_init & RDSTA_SKVN ? 0 : 1;
		ctrlpriv->rng4_sh_init &= RDSTA_MASK;
		do {
			int inst_handles =
				rd_reg32(&ctrl->r4tst[0].rdsta) &
								RDSTA_MASK;
			/*
			 * If either SH were instantiated by somebody else
			 * (e.g. u-boot) then it is assumed that the entropy
			 * parameters are properly set and thus the function
			 * setting these (kick_trng(...)) is skipped.
			 * Also, if a handle was instantiated, do not change
			 * the TRNG parameters.
			 */
			if (needs_entropy_delay_adjustment())
				ent_delay = 12000;
			if (!(ctrlpriv->rng4_sh_init || inst_handles)) {
				dev_info(dev,
					 "Entropy delay = %u\n",
					 ent_delay);
				kick_trng(pdev, ent_delay);
				ent_delay += 400;
			}
			/*
			 * if instantiate_rng(...) fails, the loop will rerun
			 * and the kick_trng(...) function will modify the
			 * upper and lower limits of the entropy sampling
			 * interval, leading to a successful initialization of
			 * the RNG.
			 */
			ret = instantiate_rng(dev, inst_handles,
					      gen_sk);
			/*
			 * Entropy delay is determined via TRNG characterization.
			 * TRNG characterization is run across different voltages
			 * and temperatures.
			 * If worst case value for ent_dly is identified,
			 * the loop can be skipped for that platform.
			 */
			if (needs_entropy_delay_adjustment())
				break;
			if (ret == -EAGAIN)
				/*
				 * if here, the loop will rerun,
				 * so don't hog the CPU
				 */
				cpu_relax();
		} while ((ret == -EAGAIN) && (ent_delay < RTSDCTL_ENT_DLY_MAX));
		if (ret) {
			dev_err(dev, "failed to instantiate RNG");
			return ret;
		}
		/*
		 * Set handles initialized by this module as the complement of
		 * the already initialized ones
		 */
		ctrlpriv->rng4_sh_init = ~ctrlpriv->rng4_sh_init & RDSTA_MASK;

		/* Enable RDB bit so that RNG works faster */
		clrsetbits_32(&ctrl->scfgr, 0, SCFGR_RDBENABLE);
	}

report_live:
	/* NOTE: RTIC detection ought to go here, around Si time */

	caam_id = (u64)rd_reg32(&perfmon->caam_id_ms) << 32 |
		  (u64)rd_reg32(&perfmon->caam_id_ls);

	/* Report "alive" for developer to see */
	dev_info(dev, "device ID = 0x%016llx (Era %d)\n", caam_id,
		 ctrlpriv->era);
	dev_info(dev, "job rings = %d, qi = %d\n",
		 ctrlpriv->total_jobrs, ctrlpriv->qi_present);

	ret = devm_of_platform_populate(dev);
	if (ret)
		dev_err(dev, "JR platform devices creation error\n");

	return ret;
}

static struct platform_driver caam_driver = {
	.driver = {
		.name = "caam",
		.of_match_table = caam_match,
	},
	.probe       = caam_probe,
};

module_platform_driver(caam_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM request backend");
MODULE_AUTHOR("Freescale Semiconductor - NMG/STC");

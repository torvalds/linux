/*
 * Cell MIC driver for ECC counting
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 */
#undef DEBUG

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/stop_machine.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/cell-regs.h>

#include "edac_core.h"

struct cell_edac_priv
{
	struct cbe_mic_tm_regs __iomem	*regs;
	int				node;
	int				chanmask;
#ifdef DEBUG
	u64				prev_fir;
#endif
};

static void cell_edac_count_ce(struct mem_ctl_info *mci, int chan, u64 ar)
{
	struct cell_edac_priv		*priv = mci->pvt_info;
	struct csrow_info		*csrow = &mci->csrows[0];
	unsigned long			address, pfn, offset, syndrome;

	dev_dbg(mci->dev, "ECC CE err on node %d, channel %d, ar = 0x%016llx\n",
		priv->node, chan, ar);

	/* Address decoding is likely a bit bogus, to dbl check */
	address = (ar & 0xffffffffe0000000ul) >> 29;
	if (priv->chanmask == 0x3)
		address = (address << 1) | chan;
	pfn = address >> PAGE_SHIFT;
	offset = address & ~PAGE_MASK;
	syndrome = (ar & 0x000000001fe00000ul) >> 21;

	/* TODO: Decoding of the error address */
	edac_mc_handle_ce(mci, csrow->first_page + pfn, offset,
			  syndrome, 0, chan, "");
}

static void cell_edac_count_ue(struct mem_ctl_info *mci, int chan, u64 ar)
{
	struct cell_edac_priv		*priv = mci->pvt_info;
	struct csrow_info		*csrow = &mci->csrows[0];
	unsigned long			address, pfn, offset;

	dev_dbg(mci->dev, "ECC UE err on node %d, channel %d, ar = 0x%016llx\n",
		priv->node, chan, ar);

	/* Address decoding is likely a bit bogus, to dbl check */
	address = (ar & 0xffffffffe0000000ul) >> 29;
	if (priv->chanmask == 0x3)
		address = (address << 1) | chan;
	pfn = address >> PAGE_SHIFT;
	offset = address & ~PAGE_MASK;

	/* TODO: Decoding of the error address */
	edac_mc_handle_ue(mci, csrow->first_page + pfn, offset, 0, "");
}

static void cell_edac_check(struct mem_ctl_info *mci)
{
	struct cell_edac_priv		*priv = mci->pvt_info;
	u64				fir, addreg, clear = 0;

	fir = in_be64(&priv->regs->mic_fir);
#ifdef DEBUG
	if (fir != priv->prev_fir) {
		dev_dbg(mci->dev, "fir change : 0x%016lx\n", fir);
		priv->prev_fir = fir;
	}
#endif
	if ((priv->chanmask & 0x1) && (fir & CBE_MIC_FIR_ECC_SINGLE_0_ERR)) {
		addreg = in_be64(&priv->regs->mic_df_ecc_address_0);
		clear |= CBE_MIC_FIR_ECC_SINGLE_0_RESET;
		cell_edac_count_ce(mci, 0, addreg);
	}
	if ((priv->chanmask & 0x2) && (fir & CBE_MIC_FIR_ECC_SINGLE_1_ERR)) {
		addreg = in_be64(&priv->regs->mic_df_ecc_address_1);
		clear |= CBE_MIC_FIR_ECC_SINGLE_1_RESET;
		cell_edac_count_ce(mci, 1, addreg);
	}
	if ((priv->chanmask & 0x1) && (fir & CBE_MIC_FIR_ECC_MULTI_0_ERR)) {
		addreg = in_be64(&priv->regs->mic_df_ecc_address_0);
		clear |= CBE_MIC_FIR_ECC_MULTI_0_RESET;
		cell_edac_count_ue(mci, 0, addreg);
	}
	if ((priv->chanmask & 0x2) && (fir & CBE_MIC_FIR_ECC_MULTI_1_ERR)) {
		addreg = in_be64(&priv->regs->mic_df_ecc_address_1);
		clear |= CBE_MIC_FIR_ECC_MULTI_1_RESET;
		cell_edac_count_ue(mci, 1, addreg);
	}

	/* The procedure for clearing FIR bits is a bit ... weird */
	if (clear) {
		fir &= ~(CBE_MIC_FIR_ECC_ERR_MASK | CBE_MIC_FIR_ECC_SET_MASK);
		fir |= CBE_MIC_FIR_ECC_RESET_MASK;
		fir &= ~clear;
		out_be64(&priv->regs->mic_fir, fir);
		(void)in_be64(&priv->regs->mic_fir);

		mb();	/* sync up */
#ifdef DEBUG
		fir = in_be64(&priv->regs->mic_fir);
		dev_dbg(mci->dev, "fir clear  : 0x%016lx\n", fir);
#endif
	}
}

static void __devinit cell_edac_init_csrows(struct mem_ctl_info *mci)
{
	struct csrow_info		*csrow = &mci->csrows[0];
	struct dimm_info		*dimm;
	struct cell_edac_priv		*priv = mci->pvt_info;
	struct device_node		*np;
	int				j;

	for (np = NULL;
	     (np = of_find_node_by_name(np, "memory")) != NULL;) {
		struct resource r;

		/* We "know" that the Cell firmware only creates one entry
		 * in the "memory" nodes. If that changes, this code will
		 * need to be adapted.
		 */
		if (of_address_to_resource(np, 0, &r))
			continue;
		if (of_node_to_nid(np) != priv->node)
			continue;
		csrow->first_page = r.start >> PAGE_SHIFT;
		csrow->nr_pages = resource_size(&r) >> PAGE_SHIFT;
		csrow->last_page = csrow->first_page + csrow->nr_pages - 1;

		for (j = 0; j < csrow->nr_channels; j++) {
			dimm = csrow->channels[j].dimm;
			dimm->mtype = MEM_XDR;
			dimm->edac_mode = EDAC_SECDED;
		}
		dev_dbg(mci->dev,
			"Initialized on node %d, chanmask=0x%x,"
			" first_page=0x%lx, nr_pages=0x%x\n",
			priv->node, priv->chanmask,
			csrow->first_page, csrow->nr_pages);
		break;
	}
}

static int __devinit cell_edac_probe(struct platform_device *pdev)
{
	struct cbe_mic_tm_regs __iomem	*regs;
	struct mem_ctl_info		*mci;
	struct cell_edac_priv		*priv;
	u64				reg;
	int				rc, chanmask;

	regs = cbe_get_cpu_mic_tm_regs(cbe_node_to_cpu(pdev->id));
	if (regs == NULL)
		return -ENODEV;

	edac_op_state = EDAC_OPSTATE_POLL;

	/* Get channel population */
	reg = in_be64(&regs->mic_mnt_cfg);
	dev_dbg(&pdev->dev, "MIC_MNT_CFG = 0x%016llx\n", reg);
	chanmask = 0;
	if (reg & CBE_MIC_MNT_CFG_CHAN_0_POP)
		chanmask |= 0x1;
	if (reg & CBE_MIC_MNT_CFG_CHAN_1_POP)
		chanmask |= 0x2;
	if (chanmask == 0) {
		dev_warn(&pdev->dev,
			 "Yuck ! No channel populated ? Aborting !\n");
		return -ENODEV;
	}
	dev_dbg(&pdev->dev, "Initial FIR = 0x%016llx\n",
		in_be64(&regs->mic_fir));

	/* Allocate & init EDAC MC data structure */
	mci = edac_mc_alloc(sizeof(struct cell_edac_priv), 1,
			    chanmask == 3 ? 2 : 1, pdev->id);
	if (mci == NULL)
		return -ENOMEM;
	priv = mci->pvt_info;
	priv->regs = regs;
	priv->node = pdev->id;
	priv->chanmask = chanmask;
	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_XDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_EC | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_EC | EDAC_FLAG_SECDED;
	mci->mod_name = "cell_edac";
	mci->ctl_name = "MIC";
	mci->dev_name = dev_name(&pdev->dev);
	mci->edac_check = cell_edac_check;
	cell_edac_init_csrows(mci);

	/* Register with EDAC core */
	rc = edac_mc_add_mc(mci);
	if (rc) {
		dev_err(&pdev->dev, "failed to register with EDAC core\n");
		edac_mc_free(mci);
		return rc;
	}

	return 0;
}

static int __devexit cell_edac_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = edac_mc_del_mc(&pdev->dev);
	if (mci)
		edac_mc_free(mci);
	return 0;
}

static struct platform_driver cell_edac_driver = {
	.driver		= {
		.name	= "cbe-mic",
		.owner	= THIS_MODULE,
	},
	.probe		= cell_edac_probe,
	.remove		= __devexit_p(cell_edac_remove),
};

static int __init cell_edac_init(void)
{
	/* Sanity check registers data structure */
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_df_ecc_address_0) != 0xf8);
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_df_ecc_address_1) != 0x1b8);
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_df_config) != 0x218);
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_fir) != 0x230);
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_mnt_cfg) != 0x210);
	BUILD_BUG_ON(offsetof(struct cbe_mic_tm_regs,
			      mic_exc) != 0x208);

	return platform_driver_register(&cell_edac_driver);
}

static void __exit cell_edac_exit(void)
{
	platform_driver_unregister(&cell_edac_driver);
}

module_init(cell_edac_init);
module_exit(cell_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("ECC counting for Cell MIC");

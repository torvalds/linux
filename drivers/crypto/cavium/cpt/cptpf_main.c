// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/version.h>

#include "cptpf.h"

#define DRV_NAME	"thunder-cpt"
#define DRV_VERSION	"1.0"

static u32 num_vfs = 4; /* Default 4 VF enabled */
module_param(num_vfs, uint, 0444);
MODULE_PARM_DESC(num_vfs, "Number of VFs to enable(1-16)");

/*
 * Disable cores specified by coremask
 */
static void cpt_disable_cores(struct cpt_device *cpt, u64 coremask,
			      u8 type, u8 grp)
{
	u64 pf_exe_ctl;
	u32 timeout = 100;
	u64 grpmask = 0;
	struct device *dev = &cpt->pdev->dev;

	if (type == AE_TYPES)
		coremask = (coremask << cpt->max_se_cores);

	/* Disengage the cores from groups */
	grpmask = cpt_read_csr64(cpt->reg_base, CPTX_PF_GX_EN(0, grp));
	cpt_write_csr64(cpt->reg_base, CPTX_PF_GX_EN(0, grp),
			(grpmask & ~coremask));
	udelay(CSR_DELAY);
	grp = cpt_read_csr64(cpt->reg_base, CPTX_PF_EXEC_BUSY(0));
	while (grp & coremask) {
		dev_err(dev, "Cores still busy %llx", coremask);
		grp = cpt_read_csr64(cpt->reg_base,
				     CPTX_PF_EXEC_BUSY(0));
		if (timeout--)
			break;

		udelay(CSR_DELAY);
	}

	/* Disable the cores */
	pf_exe_ctl = cpt_read_csr64(cpt->reg_base, CPTX_PF_EXE_CTL(0));
	cpt_write_csr64(cpt->reg_base, CPTX_PF_EXE_CTL(0),
			(pf_exe_ctl & ~coremask));
	udelay(CSR_DELAY);
}

/*
 * Enable cores specified by coremask
 */
static void cpt_enable_cores(struct cpt_device *cpt, u64 coremask,
			     u8 type)
{
	u64 pf_exe_ctl;

	if (type == AE_TYPES)
		coremask = (coremask << cpt->max_se_cores);

	pf_exe_ctl = cpt_read_csr64(cpt->reg_base, CPTX_PF_EXE_CTL(0));
	cpt_write_csr64(cpt->reg_base, CPTX_PF_EXE_CTL(0),
			(pf_exe_ctl | coremask));
	udelay(CSR_DELAY);
}

static void cpt_configure_group(struct cpt_device *cpt, u8 grp,
				u64 coremask, u8 type)
{
	u64 pf_gx_en = 0;

	if (type == AE_TYPES)
		coremask = (coremask << cpt->max_se_cores);

	pf_gx_en = cpt_read_csr64(cpt->reg_base, CPTX_PF_GX_EN(0, grp));
	cpt_write_csr64(cpt->reg_base, CPTX_PF_GX_EN(0, grp),
			(pf_gx_en | coremask));
	udelay(CSR_DELAY);
}

static void cpt_disable_mbox_interrupts(struct cpt_device *cpt)
{
	/* Clear mbox(0) interupts for all vfs */
	cpt_write_csr64(cpt->reg_base, CPTX_PF_MBOX_ENA_W1CX(0, 0), ~0ull);
}

static void cpt_disable_ecc_interrupts(struct cpt_device *cpt)
{
	/* Clear ecc(0) interupts for all vfs */
	cpt_write_csr64(cpt->reg_base, CPTX_PF_ECC0_ENA_W1C(0), ~0ull);
}

static void cpt_disable_exec_interrupts(struct cpt_device *cpt)
{
	/* Clear exec interupts for all vfs */
	cpt_write_csr64(cpt->reg_base, CPTX_PF_EXEC_ENA_W1C(0), ~0ull);
}

static void cpt_disable_all_interrupts(struct cpt_device *cpt)
{
	cpt_disable_mbox_interrupts(cpt);
	cpt_disable_ecc_interrupts(cpt);
	cpt_disable_exec_interrupts(cpt);
}

static void cpt_enable_mbox_interrupts(struct cpt_device *cpt)
{
	/* Set mbox(0) interupts for all vfs */
	cpt_write_csr64(cpt->reg_base, CPTX_PF_MBOX_ENA_W1SX(0, 0), ~0ull);
}

static int cpt_load_microcode(struct cpt_device *cpt, struct microcode *mcode)
{
	int ret = 0, core = 0, shift = 0;
	u32 total_cores = 0;
	struct device *dev = &cpt->pdev->dev;

	if (!mcode || !mcode->code) {
		dev_err(dev, "Either the mcode is null or data is NULL\n");
		return -EINVAL;
	}

	if (mcode->code_size == 0) {
		dev_err(dev, "microcode size is 0\n");
		return -EINVAL;
	}

	/* Assumes 0-9 are SE cores for UCODE_BASE registers and
	 * AE core bases follow
	 */
	if (mcode->is_ae) {
		core = CPT_MAX_SE_CORES; /* start couting from 10 */
		total_cores = CPT_MAX_TOTAL_CORES; /* upto 15 */
	} else {
		core = 0; /* start couting from 0 */
		total_cores = CPT_MAX_SE_CORES; /* upto 9 */
	}

	/* Point to microcode for each core of the group */
	for (; core < total_cores ; core++, shift++) {
		if (mcode->core_mask & (1 << shift)) {
			cpt_write_csr64(cpt->reg_base,
					CPTX_PF_ENGX_UCODE_BASE(0, core),
					(u64)mcode->phys_base);
		}
	}
	return ret;
}

static int do_cpt_init(struct cpt_device *cpt, struct microcode *mcode)
{
	int ret = 0;
	struct device *dev = &cpt->pdev->dev;

	/* Make device not ready */
	cpt->flags &= ~CPT_FLAG_DEVICE_READY;
	/* Disable All PF interrupts */
	cpt_disable_all_interrupts(cpt);
	/* Calculate mcode group and coremasks */
	if (mcode->is_ae) {
		if (mcode->num_cores > cpt->max_ae_cores) {
			dev_err(dev, "Requested for more cores than available AE cores\n");
			ret = -EINVAL;
			goto cpt_init_fail;
		}

		if (cpt->next_group >= CPT_MAX_CORE_GROUPS) {
			dev_err(dev, "Can't load, all eight microcode groups in use");
			return -ENFILE;
		}

		mcode->group = cpt->next_group;
		/* Convert requested cores to mask */
		mcode->core_mask = GENMASK(mcode->num_cores, 0);
		cpt_disable_cores(cpt, mcode->core_mask, AE_TYPES,
				  mcode->group);
		/* Load microcode for AE engines */
		ret = cpt_load_microcode(cpt, mcode);
		if (ret) {
			dev_err(dev, "Microcode load Failed for %s\n",
				mcode->version);
			goto cpt_init_fail;
		}
		cpt->next_group++;
		/* Configure group mask for the mcode */
		cpt_configure_group(cpt, mcode->group, mcode->core_mask,
				    AE_TYPES);
		/* Enable AE cores for the group mask */
		cpt_enable_cores(cpt, mcode->core_mask, AE_TYPES);
	} else {
		if (mcode->num_cores > cpt->max_se_cores) {
			dev_err(dev, "Requested for more cores than available SE cores\n");
			ret = -EINVAL;
			goto cpt_init_fail;
		}
		if (cpt->next_group >= CPT_MAX_CORE_GROUPS) {
			dev_err(dev, "Can't load, all eight microcode groups in use");
			return -ENFILE;
		}

		mcode->group = cpt->next_group;
		/* Covert requested cores to mask */
		mcode->core_mask = GENMASK(mcode->num_cores, 0);
		cpt_disable_cores(cpt, mcode->core_mask, SE_TYPES,
				  mcode->group);
		/* Load microcode for SE engines */
		ret = cpt_load_microcode(cpt, mcode);
		if (ret) {
			dev_err(dev, "Microcode load Failed for %s\n",
				mcode->version);
			goto cpt_init_fail;
		}
		cpt->next_group++;
		/* Configure group mask for the mcode */
		cpt_configure_group(cpt, mcode->group, mcode->core_mask,
				    SE_TYPES);
		/* Enable SE cores for the group mask */
		cpt_enable_cores(cpt, mcode->core_mask, SE_TYPES);
	}

	/* Enabled PF mailbox interrupts */
	cpt_enable_mbox_interrupts(cpt);
	cpt->flags |= CPT_FLAG_DEVICE_READY;

	return ret;

cpt_init_fail:
	/* Enabled PF mailbox interrupts */
	cpt_enable_mbox_interrupts(cpt);

	return ret;
}

struct ucode_header {
	u8 version[CPT_UCODE_VERSION_SZ];
	__be32 code_length;
	u32 data_length;
	u64 sram_address;
};

static int cpt_ucode_load_fw(struct cpt_device *cpt, const u8 *fw, bool is_ae)
{
	const struct firmware *fw_entry;
	struct device *dev = &cpt->pdev->dev;
	struct ucode_header *ucode;
	struct microcode *mcode;
	int j, ret = 0;

	ret = request_firmware(&fw_entry, fw, dev);
	if (ret)
		return ret;

	ucode = (struct ucode_header *)fw_entry->data;
	mcode = &cpt->mcode[cpt->next_mc_idx];
	memcpy(mcode->version, (u8 *)fw_entry->data, CPT_UCODE_VERSION_SZ);
	mcode->code_size = ntohl(ucode->code_length) * 2;
	if (!mcode->code_size) {
		ret = -EINVAL;
		goto fw_release;
	}

	mcode->is_ae = is_ae;
	mcode->core_mask = 0ULL;
	mcode->num_cores = is_ae ? 6 : 10;

	/*  Allocate DMAable space */
	mcode->code = dma_alloc_coherent(&cpt->pdev->dev, mcode->code_size,
					 &mcode->phys_base, GFP_KERNEL);
	if (!mcode->code) {
		dev_err(dev, "Unable to allocate space for microcode");
		ret = -ENOMEM;
		goto fw_release;
	}

	memcpy((void *)mcode->code, (void *)(fw_entry->data + sizeof(*ucode)),
	       mcode->code_size);

	/* Byte swap 64-bit */
	for (j = 0; j < (mcode->code_size / 8); j++)
		((__be64 *)mcode->code)[j] = cpu_to_be64(((u64 *)mcode->code)[j]);
	/*  MC needs 16-bit swap */
	for (j = 0; j < (mcode->code_size / 2); j++)
		((__be16 *)mcode->code)[j] = cpu_to_be16(((u16 *)mcode->code)[j]);

	dev_dbg(dev, "mcode->code_size = %u\n", mcode->code_size);
	dev_dbg(dev, "mcode->is_ae = %u\n", mcode->is_ae);
	dev_dbg(dev, "mcode->num_cores = %u\n", mcode->num_cores);
	dev_dbg(dev, "mcode->code = %llx\n", (u64)mcode->code);
	dev_dbg(dev, "mcode->phys_base = %llx\n", mcode->phys_base);

	ret = do_cpt_init(cpt, mcode);
	if (ret) {
		dev_err(dev, "do_cpt_init failed with ret: %d\n", ret);
		goto fw_release;
	}

	dev_info(dev, "Microcode Loaded %s\n", mcode->version);
	mcode->is_mc_valid = 1;
	cpt->next_mc_idx++;

fw_release:
	release_firmware(fw_entry);

	return ret;
}

static int cpt_ucode_load(struct cpt_device *cpt)
{
	int ret = 0;
	struct device *dev = &cpt->pdev->dev;

	ret = cpt_ucode_load_fw(cpt, "cpt8x-mc-ae.out", true);
	if (ret) {
		dev_err(dev, "ae:cpt_ucode_load failed with ret: %d\n", ret);
		return ret;
	}
	ret = cpt_ucode_load_fw(cpt, "cpt8x-mc-se.out", false);
	if (ret) {
		dev_err(dev, "se:cpt_ucode_load failed with ret: %d\n", ret);
		return ret;
	}

	return ret;
}

static irqreturn_t cpt_mbx0_intr_handler(int irq, void *cpt_irq)
{
	struct cpt_device *cpt = (struct cpt_device *)cpt_irq;

	cpt_mbox_intr_handler(cpt, 0);

	return IRQ_HANDLED;
}

static void cpt_reset(struct cpt_device *cpt)
{
	cpt_write_csr64(cpt->reg_base, CPTX_PF_RESET(0), 1);
}

static void cpt_find_max_enabled_cores(struct cpt_device *cpt)
{
	union cptx_pf_constants pf_cnsts = {0};

	pf_cnsts.u = cpt_read_csr64(cpt->reg_base, CPTX_PF_CONSTANTS(0));
	cpt->max_se_cores = pf_cnsts.s.se;
	cpt->max_ae_cores = pf_cnsts.s.ae;
}

static u32 cpt_check_bist_status(struct cpt_device *cpt)
{
	union cptx_pf_bist_status bist_sts = {0};

	bist_sts.u = cpt_read_csr64(cpt->reg_base,
				    CPTX_PF_BIST_STATUS(0));

	return bist_sts.u;
}

static u64 cpt_check_exe_bist_status(struct cpt_device *cpt)
{
	union cptx_pf_exe_bist_status bist_sts = {0};

	bist_sts.u = cpt_read_csr64(cpt->reg_base,
				    CPTX_PF_EXE_BIST_STATUS(0));

	return bist_sts.u;
}

static void cpt_disable_all_cores(struct cpt_device *cpt)
{
	u32 grp, timeout = 100;
	struct device *dev = &cpt->pdev->dev;

	/* Disengage the cores from groups */
	for (grp = 0; grp < CPT_MAX_CORE_GROUPS; grp++) {
		cpt_write_csr64(cpt->reg_base, CPTX_PF_GX_EN(0, grp), 0);
		udelay(CSR_DELAY);
	}

	grp = cpt_read_csr64(cpt->reg_base, CPTX_PF_EXEC_BUSY(0));
	while (grp) {
		dev_err(dev, "Cores still busy");
		grp = cpt_read_csr64(cpt->reg_base,
				     CPTX_PF_EXEC_BUSY(0));
		if (timeout--)
			break;

		udelay(CSR_DELAY);
	}
	/* Disable the cores */
	cpt_write_csr64(cpt->reg_base, CPTX_PF_EXE_CTL(0), 0);
}

/**
 * Ensure all cores are disengaged from all groups by
 * calling cpt_disable_all_cores() before calling this
 * function.
 */
static void cpt_unload_microcode(struct cpt_device *cpt)
{
	u32 grp = 0, core;

	/* Free microcode bases and reset group masks */
	for (grp = 0; grp < CPT_MAX_CORE_GROUPS; grp++) {
		struct microcode *mcode = &cpt->mcode[grp];

		if (cpt->mcode[grp].code)
			dma_free_coherent(&cpt->pdev->dev, mcode->code_size,
					  mcode->code, mcode->phys_base);
		mcode->code = NULL;
	}
	/* Clear UCODE_BASE registers for all engines */
	for (core = 0; core < CPT_MAX_TOTAL_CORES; core++)
		cpt_write_csr64(cpt->reg_base,
				CPTX_PF_ENGX_UCODE_BASE(0, core), 0ull);
}

static int cpt_device_init(struct cpt_device *cpt)
{
	u64 bist;
	struct device *dev = &cpt->pdev->dev;

	/* Reset the PF when probed first */
	cpt_reset(cpt);
	msleep(100);

	/*Check BIST status*/
	bist = (u64)cpt_check_bist_status(cpt);
	if (bist) {
		dev_err(dev, "RAM BIST failed with code 0x%llx", bist);
		return -ENODEV;
	}

	bist = cpt_check_exe_bist_status(cpt);
	if (bist) {
		dev_err(dev, "Engine BIST failed with code 0x%llx", bist);
		return -ENODEV;
	}

	/*Get CLK frequency*/
	/*Get max enabled cores */
	cpt_find_max_enabled_cores(cpt);
	/*Disable all cores*/
	cpt_disable_all_cores(cpt);
	/*Reset device parameters*/
	cpt->next_mc_idx   = 0;
	cpt->next_group = 0;
	/* PF is ready */
	cpt->flags |= CPT_FLAG_DEVICE_READY;

	return 0;
}

static int cpt_register_interrupts(struct cpt_device *cpt)
{
	int ret;
	struct device *dev = &cpt->pdev->dev;

	/* Enable MSI-X */
	ret = pci_alloc_irq_vectors(cpt->pdev, CPT_PF_MSIX_VECTORS,
			CPT_PF_MSIX_VECTORS, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&cpt->pdev->dev, "Request for #%d msix vectors failed\n",
			CPT_PF_MSIX_VECTORS);
		return ret;
	}

	/* Register mailbox interrupt handlers */
	ret = request_irq(pci_irq_vector(cpt->pdev, CPT_PF_INT_VEC_E_MBOXX(0)),
			  cpt_mbx0_intr_handler, 0, "CPT Mbox0", cpt);
	if (ret)
		goto fail;

	/* Enable mailbox interrupt */
	cpt_enable_mbox_interrupts(cpt);
	return 0;

fail:
	dev_err(dev, "Request irq failed\n");
	pci_disable_msix(cpt->pdev);
	return ret;
}

static void cpt_unregister_interrupts(struct cpt_device *cpt)
{
	free_irq(pci_irq_vector(cpt->pdev, CPT_PF_INT_VEC_E_MBOXX(0)), cpt);
	pci_disable_msix(cpt->pdev);
}

static int cpt_sriov_init(struct cpt_device *cpt, int num_vfs)
{
	int pos = 0;
	int err;
	u16 total_vf_cnt;
	struct pci_dev *pdev = cpt->pdev;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos) {
		dev_err(&pdev->dev, "SRIOV capability is not found in PCIe config space\n");
		return -ENODEV;
	}

	cpt->num_vf_en = num_vfs; /* User requested VFs */
	pci_read_config_word(pdev, (pos + PCI_SRIOV_TOTAL_VF), &total_vf_cnt);
	if (total_vf_cnt < cpt->num_vf_en)
		cpt->num_vf_en = total_vf_cnt;

	if (!total_vf_cnt)
		return 0;

	/*Enabled the available VFs */
	err = pci_enable_sriov(pdev, cpt->num_vf_en);
	if (err) {
		dev_err(&pdev->dev, "SRIOV enable failed, num VF is %d\n",
			cpt->num_vf_en);
		cpt->num_vf_en = 0;
		return err;
	}

	/* TODO: Optionally enable static VQ priorities feature */

	dev_info(&pdev->dev, "SRIOV enabled, number of VF available %d\n",
		 cpt->num_vf_en);

	cpt->flags |= CPT_FLAG_SRIOV_ENABLED;

	return 0;
}

static int cpt_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct cpt_device *cpt;
	int err;

	if (num_vfs > 16 || num_vfs < 4) {
		dev_warn(dev, "Invalid vf count %d, Resetting it to 4(default)\n",
			 num_vfs);
		num_vfs = 4;
	}

	cpt = devm_kzalloc(dev, sizeof(*cpt), GFP_KERNEL);
	if (!cpt)
		return -ENOMEM;

	pci_set_drvdata(pdev, cpt);
	cpt->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto cpt_err_disable_device;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable 48-bit DMA configuration\n");
		goto cpt_err_release_regions;
	}

	/* MAP PF's configuration registers */
	cpt->reg_base = pcim_iomap(pdev, 0, 0);
	if (!cpt->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		goto cpt_err_release_regions;
	}

	/* CPT device HW initialization */
	cpt_device_init(cpt);

	/* Register interrupts */
	err = cpt_register_interrupts(cpt);
	if (err)
		goto cpt_err_release_regions;

	err = cpt_ucode_load(cpt);
	if (err)
		goto cpt_err_unregister_interrupts;

	/* Configure SRIOV */
	err = cpt_sriov_init(cpt, num_vfs);
	if (err)
		goto cpt_err_unregister_interrupts;

	return 0;

cpt_err_unregister_interrupts:
	cpt_unregister_interrupts(cpt);
cpt_err_release_regions:
	pci_release_regions(pdev);
cpt_err_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void cpt_remove(struct pci_dev *pdev)
{
	struct cpt_device *cpt = pci_get_drvdata(pdev);

	/* Disengage SE and AE cores from all groups*/
	cpt_disable_all_cores(cpt);
	/* Unload microcodes */
	cpt_unload_microcode(cpt);
	cpt_unregister_interrupts(cpt);
	pci_disable_sriov(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static void cpt_shutdown(struct pci_dev *pdev)
{
	struct cpt_device *cpt = pci_get_drvdata(pdev);

	if (!cpt)
		return;

	dev_info(&pdev->dev, "Shutdown device %x:%x.\n",
		 (u32)pdev->vendor, (u32)pdev->device);

	cpt_unregister_interrupts(cpt);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id cpt_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, CPT_81XX_PCI_PF_DEVICE_ID) },
	{ 0, }  /* end of table */
};

static struct pci_driver cpt_pci_driver = {
	.name = DRV_NAME,
	.id_table = cpt_id_table,
	.probe = cpt_probe,
	.remove = cpt_remove,
	.shutdown = cpt_shutdown,
};

module_pci_driver(cpt_pci_driver);

MODULE_AUTHOR("George Cherian <george.cherian@cavium.com>");
MODULE_DESCRIPTION("Cavium Thunder CPT Physical Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, cpt_id_table);

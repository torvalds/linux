// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include <linux/firmware.h>
#include "otx2_cpt_hw_types.h"
#include "otx2_cpt_common.h"
#include "otx2_cpt_devlink.h"
#include "otx2_cptpf_ucode.h"
#include "otx2_cptpf.h"
#include "cn10k_cpt.h"
#include "rvu_reg.h"

#define OTX2_CPT_DRV_NAME    "rvu_cptpf"
#define OTX2_CPT_DRV_STRING  "Marvell RVU CPT Physical Function Driver"

static void cptpf_enable_vfpf_mbox_intr(struct otx2_cptpf_dev *cptpf,
					int num_vfs)
{
	int ena_bits;

	/* Clear any pending interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INTX(0), ~0x0ULL);
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INTX(1), ~0x0ULL);

	/* Enable VF interrupts for VFs from 0 to 63 */
	ena_bits = ((num_vfs - 1) % 64);
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INT_ENA_W1SX(0),
			 GENMASK_ULL(ena_bits, 0));

	if (num_vfs > 64) {
		/* Enable VF interrupts for VFs from 64 to 127 */
		ena_bits = num_vfs - 64 - 1;
		otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
				RVU_PF_VFPF_MBOX_INT_ENA_W1SX(1),
				GENMASK_ULL(ena_bits, 0));
	}
}

static void cptpf_disable_vfpf_mbox_intr(struct otx2_cptpf_dev *cptpf,
					 int num_vfs)
{
	int vector;

	/* Disable VF-PF interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INT_ENA_W1CX(0), ~0ULL);
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INT_ENA_W1CX(1), ~0ULL);
	/* Clear any pending interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFPF_MBOX_INTX(0), ~0ULL);

	vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFPF_MBOX0);
	free_irq(vector, cptpf);

	if (num_vfs > 64) {
		otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
				 RVU_PF_VFPF_MBOX_INTX(1), ~0ULL);
		vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFPF_MBOX1);
		free_irq(vector, cptpf);
	}
}

static void cptpf_enable_vf_flr_me_intrs(struct otx2_cptpf_dev *cptpf,
					 int num_vfs)
{
	/* Clear FLR interrupt if any */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_VFFLR_INTX(0),
			 INTR_MASK(num_vfs));

	/* Enable VF FLR interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFFLR_INT_ENA_W1SX(0), INTR_MASK(num_vfs));
	/* Clear ME interrupt if any */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_VFME_INTX(0),
			 INTR_MASK(num_vfs));
	/* Enable VF ME interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFME_INT_ENA_W1SX(0), INTR_MASK(num_vfs));

	if (num_vfs <= 64)
		return;

	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_VFFLR_INTX(1),
			 INTR_MASK(num_vfs - 64));
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFFLR_INT_ENA_W1SX(1), INTR_MASK(num_vfs - 64));

	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_VFME_INTX(1),
			 INTR_MASK(num_vfs - 64));
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFME_INT_ENA_W1SX(1), INTR_MASK(num_vfs - 64));
}

static void cptpf_disable_vf_flr_me_intrs(struct otx2_cptpf_dev *cptpf,
				       int num_vfs)
{
	int vector;

	/* Disable VF FLR interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFFLR_INT_ENA_W1CX(0), INTR_MASK(num_vfs));
	vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFFLR0);
	free_irq(vector, cptpf);

	/* Disable VF ME interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFME_INT_ENA_W1CX(0), INTR_MASK(num_vfs));
	vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFME0);
	free_irq(vector, cptpf);

	if (num_vfs <= 64)
		return;

	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFFLR_INT_ENA_W1CX(1), INTR_MASK(num_vfs - 64));
	vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFFLR1);
	free_irq(vector, cptpf);

	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
			 RVU_PF_VFME_INT_ENA_W1CX(1), INTR_MASK(num_vfs - 64));
	vector = pci_irq_vector(cptpf->pdev, RVU_PF_INT_VEC_VFME1);
	free_irq(vector, cptpf);
}

static void cptpf_flr_wq_handler(struct work_struct *work)
{
	struct cptpf_flr_work *flr_work;
	struct otx2_cptpf_dev *pf;
	struct mbox_msghdr *req;
	struct otx2_mbox *mbox;
	int vf, reg = 0;

	flr_work = container_of(work, struct cptpf_flr_work, work);
	pf = flr_work->pf;
	mbox = &pf->afpf_mbox;

	vf = flr_work - pf->flr_work;

	mutex_lock(&pf->lock);
	req = otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct msg_rsp));
	if (!req) {
		mutex_unlock(&pf->lock);
		return;
	}

	req->sig = OTX2_MBOX_REQ_SIG;
	req->id = MBOX_MSG_VF_FLR;
	req->pcifunc &= RVU_PFVF_FUNC_MASK;
	req->pcifunc |= (vf + 1) & RVU_PFVF_FUNC_MASK;

	otx2_cpt_send_mbox_msg(mbox, pf->pdev);
	if (!otx2_cpt_sync_mbox_msg(&pf->afpf_mbox)) {

		if (vf >= 64) {
			reg = 1;
			vf = vf - 64;
		}
		/* Clear transaction pending register */
		otx2_cpt_write64(pf->reg_base, BLKADDR_RVUM, 0,
				 RVU_PF_VFTRPENDX(reg), BIT_ULL(vf));
		otx2_cpt_write64(pf->reg_base, BLKADDR_RVUM, 0,
				 RVU_PF_VFFLR_INT_ENA_W1SX(reg), BIT_ULL(vf));
	}
	mutex_unlock(&pf->lock);
}

static irqreturn_t cptpf_vf_flr_intr(int __always_unused irq, void *arg)
{
	int reg, dev, vf, start_vf, num_reg = 1;
	struct otx2_cptpf_dev *cptpf = arg;
	u64 intr;

	if (cptpf->max_vfs > 64)
		num_reg = 2;

	for (reg = 0; reg < num_reg; reg++) {
		intr = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
				       RVU_PF_VFFLR_INTX(reg));
		if (!intr)
			continue;
		start_vf = 64 * reg;
		for (vf = 0; vf < 64; vf++) {
			if (!(intr & BIT_ULL(vf)))
				continue;
			dev = vf + start_vf;
			queue_work(cptpf->flr_wq, &cptpf->flr_work[dev].work);
			/* Clear interrupt */
			otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
					 RVU_PF_VFFLR_INTX(reg), BIT_ULL(vf));
			/* Disable the interrupt */
			otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
					 RVU_PF_VFFLR_INT_ENA_W1CX(reg),
					 BIT_ULL(vf));
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t cptpf_vf_me_intr(int __always_unused irq, void *arg)
{
	struct otx2_cptpf_dev *cptpf = arg;
	int reg, vf, num_reg = 1;
	u64 intr;

	if (cptpf->max_vfs > 64)
		num_reg = 2;

	for (reg = 0; reg < num_reg; reg++) {
		intr = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
				       RVU_PF_VFME_INTX(reg));
		if (!intr)
			continue;
		for (vf = 0; vf < 64; vf++) {
			if (!(intr & BIT_ULL(vf)))
				continue;
			otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
					 RVU_PF_VFTRPENDX(reg), BIT_ULL(vf));
			/* Clear interrupt */
			otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0,
					 RVU_PF_VFME_INTX(reg), BIT_ULL(vf));
		}
	}
	return IRQ_HANDLED;
}

static void cptpf_unregister_vfpf_intr(struct otx2_cptpf_dev *cptpf,
				       int num_vfs)
{
	cptpf_disable_vfpf_mbox_intr(cptpf, num_vfs);
	cptpf_disable_vf_flr_me_intrs(cptpf, num_vfs);
}

static int cptpf_register_vfpf_intr(struct otx2_cptpf_dev *cptpf, int num_vfs)
{
	struct pci_dev *pdev = cptpf->pdev;
	struct device *dev = &pdev->dev;
	int ret, vector;

	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFPF_MBOX0);
	/* Register VF-PF mailbox interrupt handler */
	ret = request_irq(vector, otx2_cptpf_vfpf_mbox_intr, 0, "CPTVFPF Mbox0",
			  cptpf);
	if (ret) {
		dev_err(dev,
			"IRQ registration failed for PFVF mbox0 irq\n");
		return ret;
	}
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFFLR0);
	/* Register VF FLR interrupt handler */
	ret = request_irq(vector, cptpf_vf_flr_intr, 0, "CPTPF FLR0", cptpf);
	if (ret) {
		dev_err(dev,
			"IRQ registration failed for VFFLR0 irq\n");
		goto free_mbox0_irq;
	}
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFME0);
	/* Register VF ME interrupt handler */
	ret = request_irq(vector, cptpf_vf_me_intr, 0, "CPTPF ME0", cptpf);
	if (ret) {
		dev_err(dev,
			"IRQ registration failed for PFVF mbox0 irq\n");
		goto free_flr0_irq;
	}

	if (num_vfs > 64) {
		vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFPF_MBOX1);
		ret = request_irq(vector, otx2_cptpf_vfpf_mbox_intr, 0,
				  "CPTVFPF Mbox1", cptpf);
		if (ret) {
			dev_err(dev,
				"IRQ registration failed for PFVF mbox1 irq\n");
			goto free_me0_irq;
		}
		vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFFLR1);
		/* Register VF FLR interrupt handler */
		ret = request_irq(vector, cptpf_vf_flr_intr, 0, "CPTPF FLR1",
				  cptpf);
		if (ret) {
			dev_err(dev,
				"IRQ registration failed for VFFLR1 irq\n");
			goto free_mbox1_irq;
		}
		vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFME1);
		/* Register VF FLR interrupt handler */
		ret = request_irq(vector, cptpf_vf_me_intr, 0, "CPTPF ME1",
				  cptpf);
		if (ret) {
			dev_err(dev,
				"IRQ registration failed for VFFLR1 irq\n");
			goto free_flr1_irq;
		}
	}
	cptpf_enable_vfpf_mbox_intr(cptpf, num_vfs);
	cptpf_enable_vf_flr_me_intrs(cptpf, num_vfs);

	return 0;

free_flr1_irq:
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFFLR1);
	free_irq(vector, cptpf);
free_mbox1_irq:
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFPF_MBOX1);
	free_irq(vector, cptpf);
free_me0_irq:
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFME0);
	free_irq(vector, cptpf);
free_flr0_irq:
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFFLR0);
	free_irq(vector, cptpf);
free_mbox0_irq:
	vector = pci_irq_vector(pdev, RVU_PF_INT_VEC_VFPF_MBOX0);
	free_irq(vector, cptpf);
	return ret;
}

static void cptpf_flr_wq_destroy(struct otx2_cptpf_dev *pf)
{
	if (!pf->flr_wq)
		return;
	destroy_workqueue(pf->flr_wq);
	pf->flr_wq = NULL;
	kfree(pf->flr_work);
}

static int cptpf_flr_wq_init(struct otx2_cptpf_dev *cptpf, int num_vfs)
{
	int vf;

	cptpf->flr_wq = alloc_ordered_workqueue("cptpf_flr_wq", 0);
	if (!cptpf->flr_wq)
		return -ENOMEM;

	cptpf->flr_work = kcalloc(num_vfs, sizeof(struct cptpf_flr_work),
				  GFP_KERNEL);
	if (!cptpf->flr_work)
		goto destroy_wq;

	for (vf = 0; vf < num_vfs; vf++) {
		cptpf->flr_work[vf].pf = cptpf;
		INIT_WORK(&cptpf->flr_work[vf].work, cptpf_flr_wq_handler);
	}
	return 0;

destroy_wq:
	destroy_workqueue(cptpf->flr_wq);
	return -ENOMEM;
}

static int cptpf_vfpf_mbox_init(struct otx2_cptpf_dev *cptpf, int num_vfs)
{
	struct device *dev = &cptpf->pdev->dev;
	u64 vfpf_mbox_base;
	int err, i;

	cptpf->vfpf_mbox_wq = alloc_workqueue("cpt_vfpf_mailbox",
					      WQ_UNBOUND | WQ_HIGHPRI |
					      WQ_MEM_RECLAIM, 1);
	if (!cptpf->vfpf_mbox_wq)
		return -ENOMEM;

	/* Map VF-PF mailbox memory */
	if (test_bit(CN10K_MBOX, &cptpf->cap_flag))
		vfpf_mbox_base = readq(cptpf->reg_base + RVU_PF_VF_MBOX_ADDR);
	else
		vfpf_mbox_base = readq(cptpf->reg_base + RVU_PF_VF_BAR4_ADDR);

	if (!vfpf_mbox_base) {
		dev_err(dev, "VF-PF mailbox address not configured\n");
		err = -ENOMEM;
		goto free_wqe;
	}
	cptpf->vfpf_mbox_base = devm_ioremap_wc(dev, vfpf_mbox_base,
						MBOX_SIZE * cptpf->max_vfs);
	if (!cptpf->vfpf_mbox_base) {
		dev_err(dev, "Mapping of VF-PF mailbox address failed\n");
		err = -ENOMEM;
		goto free_wqe;
	}
	err = otx2_mbox_init(&cptpf->vfpf_mbox, cptpf->vfpf_mbox_base,
			     cptpf->pdev, cptpf->reg_base, MBOX_DIR_PFVF,
			     num_vfs);
	if (err)
		goto free_wqe;

	for (i = 0; i < num_vfs; i++) {
		cptpf->vf[i].vf_id = i;
		cptpf->vf[i].cptpf = cptpf;
		cptpf->vf[i].intr_idx = i % 64;
		INIT_WORK(&cptpf->vf[i].vfpf_mbox_work,
			  otx2_cptpf_vfpf_mbox_handler);
	}
	return 0;

free_wqe:
	destroy_workqueue(cptpf->vfpf_mbox_wq);
	return err;
}

static void cptpf_vfpf_mbox_destroy(struct otx2_cptpf_dev *cptpf)
{
	destroy_workqueue(cptpf->vfpf_mbox_wq);
	otx2_mbox_destroy(&cptpf->vfpf_mbox);
}

static void cptpf_disable_afpf_mbox_intr(struct otx2_cptpf_dev *cptpf)
{
	/* Disable AF-PF interrupt */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT_ENA_W1C,
			 0x1ULL);
	/* Clear interrupt if any */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT, 0x1ULL);
}

static int cptpf_register_afpf_mbox_intr(struct otx2_cptpf_dev *cptpf)
{
	struct pci_dev *pdev = cptpf->pdev;
	struct device *dev = &pdev->dev;
	int ret, irq;

	irq = pci_irq_vector(pdev, RVU_PF_INT_VEC_AFPF_MBOX);
	/* Register AF-PF mailbox interrupt handler */
	ret = devm_request_irq(dev, irq, otx2_cptpf_afpf_mbox_intr, 0,
			       "CPTAFPF Mbox", cptpf);
	if (ret) {
		dev_err(dev,
			"IRQ registration failed for PFAF mbox irq\n");
		return ret;
	}
	/* Clear interrupt if any, to avoid spurious interrupts */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT, 0x1ULL);
	/* Enable AF-PF interrupt */
	otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT_ENA_W1S,
			 0x1ULL);

	ret = otx2_cpt_send_ready_msg(&cptpf->afpf_mbox, cptpf->pdev);
	if (ret) {
		dev_warn(dev,
			 "AF not responding to mailbox, deferring probe\n");
		cptpf_disable_afpf_mbox_intr(cptpf);
		return -EPROBE_DEFER;
	}
	return 0;
}

static int cptpf_afpf_mbox_init(struct otx2_cptpf_dev *cptpf)
{
	struct pci_dev *pdev = cptpf->pdev;
	resource_size_t offset;
	int err;

	cptpf->afpf_mbox_wq = alloc_workqueue("cpt_afpf_mailbox",
					      WQ_UNBOUND | WQ_HIGHPRI |
					      WQ_MEM_RECLAIM, 1);
	if (!cptpf->afpf_mbox_wq)
		return -ENOMEM;

	offset = pci_resource_start(pdev, PCI_MBOX_BAR_NUM);
	/* Map AF-PF mailbox memory */
	cptpf->afpf_mbox_base = devm_ioremap_wc(&pdev->dev, offset, MBOX_SIZE);
	if (!cptpf->afpf_mbox_base) {
		dev_err(&pdev->dev, "Unable to map BAR4\n");
		err = -ENOMEM;
		goto error;
	}

	err = otx2_mbox_init(&cptpf->afpf_mbox, cptpf->afpf_mbox_base,
			     pdev, cptpf->reg_base, MBOX_DIR_PFAF, 1);
	if (err)
		goto error;

	INIT_WORK(&cptpf->afpf_mbox_work, otx2_cptpf_afpf_mbox_handler);
	mutex_init(&cptpf->lock);
	return 0;

error:
	destroy_workqueue(cptpf->afpf_mbox_wq);
	return err;
}

static void cptpf_afpf_mbox_destroy(struct otx2_cptpf_dev *cptpf)
{
	destroy_workqueue(cptpf->afpf_mbox_wq);
	otx2_mbox_destroy(&cptpf->afpf_mbox);
}

static ssize_t kvf_limits_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct otx2_cptpf_dev *cptpf = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", cptpf->kvf_limits);
}

static ssize_t kvf_limits_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct otx2_cptpf_dev *cptpf = dev_get_drvdata(dev);
	int lfs_num;
	int ret;

	ret = kstrtoint(buf, 0, &lfs_num);
	if (ret)
		return ret;
	if (lfs_num < 1 || lfs_num > num_online_cpus()) {
		dev_err(dev, "lfs count %d must be in range [1 - %d]\n",
			lfs_num, num_online_cpus());
		return -EINVAL;
	}
	cptpf->kvf_limits = lfs_num;

	return count;
}

static DEVICE_ATTR_RW(kvf_limits);
static struct attribute *cptpf_attrs[] = {
	&dev_attr_kvf_limits.attr,
	NULL
};

static const struct attribute_group cptpf_sysfs_group = {
	.attrs = cptpf_attrs,
};

static int cpt_is_pf_usable(struct otx2_cptpf_dev *cptpf)
{
	u64 rev;

	rev = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
			      RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM));
	rev = (rev >> 12) & 0xFF;
	/*
	 * Check if AF has setup revision for RVUM block, otherwise
	 * driver probe should be deferred until AF driver comes up
	 */
	if (!rev) {
		dev_warn(&cptpf->pdev->dev,
			 "AF is not initialized, deferring probe\n");
		return -EPROBE_DEFER;
	}
	return 0;
}

static int cptx_device_reset(struct otx2_cptpf_dev *cptpf, int blkaddr)
{
	int timeout = 10, ret;
	u64 reg = 0;

	ret = otx2_cpt_write_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
				    CPT_AF_BLK_RST, 0x1, blkaddr);
	if (ret)
		return ret;

	do {
		ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
					   CPT_AF_BLK_RST, &reg, blkaddr);
		if (ret)
			return ret;

		if (!((reg >> 63) & 0x1))
			break;

		usleep_range(10000, 20000);
		if (timeout-- < 0)
			return -EBUSY;
	} while (1);

	return ret;
}

static int cptpf_device_reset(struct otx2_cptpf_dev *cptpf)
{
	int ret = 0;

	if (cptpf->has_cpt1) {
		ret = cptx_device_reset(cptpf, BLKADDR_CPT1);
		if (ret)
			return ret;
	}
	return cptx_device_reset(cptpf, BLKADDR_CPT0);
}

static void cptpf_check_block_implemented(struct otx2_cptpf_dev *cptpf)
{
	u64 cfg;

	cfg = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
			      RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_CPT1));
	if (cfg & BIT_ULL(11))
		cptpf->has_cpt1 = true;
}

static int cptpf_device_init(struct otx2_cptpf_dev *cptpf)
{
	union otx2_cptx_af_constants1 af_cnsts1 = {0};
	int ret = 0;

	/* check if 'implemented' bit is set for block BLKADDR_CPT1 */
	cptpf_check_block_implemented(cptpf);
	/* Reset the CPT PF device */
	ret = cptpf_device_reset(cptpf);
	if (ret)
		return ret;

	/* Get number of SE, IE and AE engines */
	ret = otx2_cpt_read_af_reg(&cptpf->afpf_mbox, cptpf->pdev,
				   CPT_AF_CONSTANTS1, &af_cnsts1.u,
				   BLKADDR_CPT0);
	if (ret)
		return ret;

	cptpf->eng_grps.avail.max_se_cnt = af_cnsts1.s.se;
	cptpf->eng_grps.avail.max_ie_cnt = af_cnsts1.s.ie;
	cptpf->eng_grps.avail.max_ae_cnt = af_cnsts1.s.ae;

	/* Disable all cores */
	ret = otx2_cpt_disable_all_cores(cptpf);

	return ret;
}

static int cptpf_sriov_disable(struct pci_dev *pdev)
{
	struct otx2_cptpf_dev *cptpf = pci_get_drvdata(pdev);
	int num_vfs = pci_num_vf(pdev);

	if (!num_vfs)
		return 0;

	pci_disable_sriov(pdev);
	cptpf_unregister_vfpf_intr(cptpf, num_vfs);
	cptpf_flr_wq_destroy(cptpf);
	cptpf_vfpf_mbox_destroy(cptpf);
	module_put(THIS_MODULE);
	cptpf->enabled_vfs = 0;

	return 0;
}

static int cptpf_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct otx2_cptpf_dev *cptpf = pci_get_drvdata(pdev);
	int ret;

	/* Initialize VF<=>PF mailbox */
	ret = cptpf_vfpf_mbox_init(cptpf, num_vfs);
	if (ret)
		return ret;

	ret = cptpf_flr_wq_init(cptpf, num_vfs);
	if (ret)
		goto destroy_mbox;
	/* Register VF<=>PF mailbox interrupt */
	ret = cptpf_register_vfpf_intr(cptpf, num_vfs);
	if (ret)
		goto destroy_flr;

	/* Get CPT HW capabilities using LOAD_FVC operation. */
	ret = otx2_cpt_discover_eng_capabilities(cptpf);
	if (ret)
		goto disable_intr;

	ret = otx2_cpt_create_eng_grps(cptpf, &cptpf->eng_grps);
	if (ret)
		goto disable_intr;

	cptpf->enabled_vfs = num_vfs;
	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret)
		goto disable_intr;

	dev_notice(&cptpf->pdev->dev, "VFs enabled: %d\n", num_vfs);

	try_module_get(THIS_MODULE);
	return num_vfs;

disable_intr:
	cptpf_unregister_vfpf_intr(cptpf, num_vfs);
	cptpf->enabled_vfs = 0;
destroy_flr:
	cptpf_flr_wq_destroy(cptpf);
destroy_mbox:
	cptpf_vfpf_mbox_destroy(cptpf);
	return ret;
}

static int otx2_cptpf_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs > 0) {
		return cptpf_sriov_enable(pdev, num_vfs);
	} else {
		return cptpf_sriov_disable(pdev);
	}
}

static int otx2_cptpf_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct otx2_cptpf_dev *cptpf;
	int err;

	cptpf = devm_kzalloc(dev, sizeof(*cptpf), GFP_KERNEL);
	if (!cptpf)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto clear_drvdata;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto clear_drvdata;
	}
	/* Map PF's configuration registers */
	err = pcim_iomap_regions_request_all(pdev, 1 << PCI_PF_REG_BAR_NUM,
					     OTX2_CPT_DRV_NAME);
	if (err) {
		dev_err(dev, "Couldn't get PCI resources 0x%x\n", err);
		goto clear_drvdata;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, cptpf);
	cptpf->pdev = pdev;

	cptpf->reg_base = pcim_iomap_table(pdev)[PCI_PF_REG_BAR_NUM];

	/* Check if AF driver is up, otherwise defer probe */
	err = cpt_is_pf_usable(cptpf);
	if (err)
		goto clear_drvdata;

	err = pci_alloc_irq_vectors(pdev, RVU_PF_INT_VEC_CNT,
				    RVU_PF_INT_VEC_CNT, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "Request for %d msix vectors failed\n",
			RVU_PF_INT_VEC_CNT);
		goto clear_drvdata;
	}
	otx2_cpt_set_hw_caps(pdev, &cptpf->cap_flag);
	/* Initialize AF-PF mailbox */
	err = cptpf_afpf_mbox_init(cptpf);
	if (err)
		goto clear_drvdata;
	/* Register mailbox interrupt */
	err = cptpf_register_afpf_mbox_intr(cptpf);
	if (err)
		goto destroy_afpf_mbox;

	cptpf->max_vfs = pci_sriov_get_totalvfs(pdev);

	err = cn10k_cptpf_lmtst_init(cptpf);
	if (err)
		goto unregister_intr;

	/* Initialize CPT PF device */
	err = cptpf_device_init(cptpf);
	if (err)
		goto unregister_intr;

	/* Initialize engine groups */
	err = otx2_cpt_init_eng_grps(pdev, &cptpf->eng_grps);
	if (err)
		goto unregister_intr;

	err = sysfs_create_group(&dev->kobj, &cptpf_sysfs_group);
	if (err)
		goto cleanup_eng_grps;

	err = otx2_cpt_register_dl(cptpf);
	if (err)
		goto sysfs_grp_del;

	return 0;

sysfs_grp_del:
	sysfs_remove_group(&dev->kobj, &cptpf_sysfs_group);
cleanup_eng_grps:
	otx2_cpt_cleanup_eng_grps(pdev, &cptpf->eng_grps);
unregister_intr:
	cptpf_disable_afpf_mbox_intr(cptpf);
destroy_afpf_mbox:
	cptpf_afpf_mbox_destroy(cptpf);
clear_drvdata:
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void otx2_cptpf_remove(struct pci_dev *pdev)
{
	struct otx2_cptpf_dev *cptpf = pci_get_drvdata(pdev);

	if (!cptpf)
		return;

	cptpf_sriov_disable(pdev);
	otx2_cpt_unregister_dl(cptpf);
	/* Delete sysfs entry created for kernel VF limits */
	sysfs_remove_group(&pdev->dev.kobj, &cptpf_sysfs_group);
	/* Cleanup engine groups */
	otx2_cpt_cleanup_eng_grps(pdev, &cptpf->eng_grps);
	/* Disable AF-PF mailbox interrupt */
	cptpf_disable_afpf_mbox_intr(cptpf);
	/* Destroy AF-PF mbox */
	cptpf_afpf_mbox_destroy(cptpf);
	pci_set_drvdata(pdev, NULL);
}

/* Supported devices */
static const struct pci_device_id otx2_cpt_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OTX2_CPT_PCI_PF_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, CN10K_CPT_PCI_PF_DEVICE_ID) },
	{ 0, }  /* end of table */
};

static struct pci_driver otx2_cpt_pci_driver = {
	.name = OTX2_CPT_DRV_NAME,
	.id_table = otx2_cpt_id_table,
	.probe = otx2_cptpf_probe,
	.remove = otx2_cptpf_remove,
	.sriov_configure = otx2_cptpf_sriov_configure
};

module_pci_driver(otx2_cpt_pci_driver);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION(OTX2_CPT_DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, otx2_cpt_id_table);

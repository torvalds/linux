// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_pcie.h"
#include "iosm_ipc_protocol.h"

static void ipc_write_dbell_reg(struct iosm_pcie *ipc_pcie, int irq_n, u32 data)
{
	void __iomem *write_reg;

	/* Select the first doorbell register, which is only currently needed
	 * by CP.
	 */
	write_reg = (void __iomem *)((u8 __iomem *)ipc_pcie->ipc_regs +
				     ipc_pcie->doorbell_write +
				     (irq_n * ipc_pcie->doorbell_reg_offset));

	/* Fire the doorbell irq by writing data on the doorbell write pointer
	 * register.
	 */
	iowrite32(data, write_reg);
}

void ipc_doorbell_fire(struct iosm_pcie *ipc_pcie, int irq_n, u32 data)
{
	ipc_write_dbell_reg(ipc_pcie, irq_n, data);
}

/* Threaded Interrupt handler for MSI interrupts */
static irqreturn_t ipc_msi_interrupt(int irq, void *dev_id)
{
	struct iosm_pcie *ipc_pcie = dev_id;
	int instance = irq - ipc_pcie->pci->irq;

	/* Shift the MSI irq actions to the IPC tasklet. IRQ_NONE means the
	 * irq was not from the IPC device or could not be served.
	 */
	if (instance >= ipc_pcie->nvec)
		return IRQ_NONE;

	if (!test_bit(0, &ipc_pcie->suspend))
		ipc_imem_irq_process(ipc_pcie->imem, instance);

	return IRQ_HANDLED;
}

void ipc_release_irq(struct iosm_pcie *ipc_pcie)
{
	struct pci_dev *pdev = ipc_pcie->pci;

	if (pdev->msi_enabled) {
		while (--ipc_pcie->nvec >= 0)
			free_irq(pdev->irq + ipc_pcie->nvec, ipc_pcie);
	}
	pci_free_irq_vectors(pdev);
}

int ipc_acquire_irq(struct iosm_pcie *ipc_pcie)
{
	struct pci_dev *pdev = ipc_pcie->pci;
	int i, rc = -EINVAL;

	ipc_pcie->nvec = pci_alloc_irq_vectors(pdev, IPC_MSI_VECTORS,
					       IPC_MSI_VECTORS, PCI_IRQ_MSI);

	if (ipc_pcie->nvec < 0) {
		rc = ipc_pcie->nvec;
		goto error;
	}

	if (!pdev->msi_enabled)
		goto error;

	for (i = 0; i < ipc_pcie->nvec; ++i) {
		rc = request_threaded_irq(pdev->irq + i, NULL,
					  ipc_msi_interrupt, IRQF_ONESHOT,
					  KBUILD_MODNAME, ipc_pcie);
		if (rc) {
			dev_err(ipc_pcie->dev, "unable to grab IRQ, rc=%d", rc);
			ipc_pcie->nvec = i;
			ipc_release_irq(ipc_pcie);
			goto error;
		}
	}

error:
	return rc;
}

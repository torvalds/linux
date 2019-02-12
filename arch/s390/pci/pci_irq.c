// SPDX-License-Identifier: GPL-2.0
#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/pci.h>
#include <linux/msi.h>

#include <asm/isc.h>
#include <asm/airq.h>

#define	SIC_IRQ_MODE_ALL		0
#define	SIC_IRQ_MODE_SINGLE		1

static struct airq_iv *zpci_aisb_iv;
static struct airq_iv *zpci_aibv[ZPCI_NR_DEVICES];

/* Modify PCI: Register adapter interruptions */
static int zpci_set_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib fib = {0};
	u8 status;

	fib.isc = PCI_ISC;
	fib.sum = 1;		/* enable summary notifications */
	fib.noi = airq_iv_end(zdev->aibv);
	fib.aibv = (unsigned long) zdev->aibv->vector;
	fib.aibvo = 0;		/* each zdev has its own interrupt vector */
	fib.aisb = (unsigned long) zpci_aisb_iv->vector + (zdev->aisb/64)*8;
	fib.aisbo = zdev->aisb & 63;

	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister adapter interruptions */
static int zpci_clear_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_DEREG_INT);
	struct zpci_fib fib = {0};
	u8 cc, status;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3 || (cc == 1 && status == 24))
		/* Function already gone or IRQs already deregistered. */
		cc = 0;

	return cc ? -EIO : 0;
}

static struct irq_chip zpci_irq_chip = {
	.name = "zPCI",
	.irq_unmask = pci_msi_unmask_irq,
	.irq_mask = pci_msi_mask_irq,
};

static void zpci_irq_handler(struct airq_struct *airq)
{
	unsigned long si, ai;
	struct airq_iv *aibv;
	int irqs_on = 0;

	inc_irq_stat(IRQIO_PCI);
	for (si = 0;;) {
		/* Scan adapter summary indicator bit vector */
		si = airq_iv_scan(zpci_aisb_iv, si, airq_iv_end(zpci_aisb_iv));
		if (si == -1UL) {
			if (irqs_on++)
				/* End of second scan with interrupts on. */
				break;
			/* First scan complete, reenable interrupts. */
			if (zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC))
				break;
			si = 0;
			continue;
		}

		/* Scan the adapter interrupt vector for this device. */
		aibv = zpci_aibv[si];
		for (ai = 0;;) {
			ai = airq_iv_scan(aibv, ai, airq_iv_end(aibv));
			if (ai == -1UL)
				break;
			inc_irq_stat(IRQIO_MSI);
			airq_iv_lock(aibv, ai);
			generic_handle_irq(airq_iv_get_data(aibv, ai));
			airq_iv_unlock(aibv, ai);
		}
	}
}

int arch_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	unsigned int hwirq, msi_vecs;
	unsigned long aisb;
	struct msi_desc *msi;
	struct msi_msg msg;
	int rc, irq;

	zdev->aisb = -1UL;
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;
	msi_vecs = min_t(unsigned int, nvec, zdev->max_msi);

	/* Allocate adapter summary indicator bit */
	aisb = airq_iv_alloc_bit(zpci_aisb_iv);
	if (aisb == -1UL)
		return -EIO;
	zdev->aisb = aisb;

	/* Create adapter interrupt vector */
	zdev->aibv = airq_iv_create(msi_vecs, AIRQ_IV_DATA | AIRQ_IV_BITLOCK);
	if (!zdev->aibv)
		return -ENOMEM;

	/* Wire up shortcut pointer */
	zpci_aibv[aisb] = zdev->aibv;

	/* Request MSI interrupts */
	hwirq = 0;
	for_each_pci_msi_entry(msi, pdev) {
		if (hwirq >= msi_vecs)
			break;
		irq = irq_alloc_desc(0);	/* Alloc irq on node 0 */
		if (irq < 0)
			return -ENOMEM;
		rc = irq_set_msi_desc(irq, msi);
		if (rc)
			return rc;
		irq_set_chip_and_handler(irq, &zpci_irq_chip,
					 handle_simple_irq);
		msg.data = hwirq;
		msg.address_lo = zdev->msi_addr & 0xffffffff;
		msg.address_hi = zdev->msi_addr >> 32;
		pci_write_msi_msg(irq, &msg);
		airq_iv_set_data(zdev->aibv, hwirq, irq);
		hwirq++;
	}

	/* Enable adapter interrupts */
	rc = zpci_set_airq(zdev);
	if (rc)
		return rc;

	return (msi_vecs == nvec) ? 0 : msi_vecs;
}

void arch_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	struct msi_desc *msi;
	int rc;

	/* Disable adapter interrupts */
	rc = zpci_clear_airq(zdev);
	if (rc)
		return;

	/* Release MSI interrupts */
	for_each_pci_msi_entry(msi, pdev) {
		if (!msi->irq)
			continue;
		if (msi->msi_attrib.is_msix)
			__pci_msix_desc_mask_irq(msi, 1);
		else
			__pci_msi_desc_mask_irq(msi, 1, 1);
		irq_set_msi_desc(msi->irq, NULL);
		irq_free_desc(msi->irq);
		msi->msg.address_lo = 0;
		msi->msg.address_hi = 0;
		msi->msg.data = 0;
		msi->irq = 0;
	}

	if (zdev->aisb != -1UL) {
		zpci_aibv[zdev->aisb] = NULL;
		airq_iv_free_bit(zpci_aisb_iv, zdev->aisb);
		zdev->aisb = -1UL;
	}
	if (zdev->aibv) {
		airq_iv_release(zdev->aibv);
		zdev->aibv = NULL;
	}
}

static struct airq_struct zpci_airq = {
	.handler = zpci_irq_handler,
	.isc = PCI_ISC,
};

int __init zpci_irq_init(void)
{
	int rc;

	rc = register_adapter_interrupt(&zpci_airq);
	if (rc)
		goto out;
	/* Set summary to 1 to be called every time for the ISC. */
	*zpci_airq.lsi_ptr = 1;

	rc = -ENOMEM;
	zpci_aisb_iv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC);
	if (!zpci_aisb_iv)
		goto out_airq;

	zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC);
	return 0;

out_airq:
	unregister_adapter_interrupt(&zpci_airq);
out:
	return rc;
}

void __init zpci_irq_exit(void)
{
	airq_iv_release(zpci_aisb_iv);
	unregister_adapter_interrupt(&zpci_airq);
}

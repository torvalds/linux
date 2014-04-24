/*
 * PCI Backend Operations - respond to PCI requests from Frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <xen/events.h>
#include <linux/sched.h>
#include "pciback.h"

int verbose_request;
module_param(verbose_request, int, 0644);

static irqreturn_t xen_pcibk_guest_interrupt(int irq, void *dev_id);

/* Ensure a device is has the fake IRQ handler "turned on/off" and is
 * ready to be exported. This MUST be run after xen_pcibk_reset_device
 * which does the actual PCI device enable/disable.
 */
static void xen_pcibk_control_isr(struct pci_dev *dev, int reset)
{
	struct xen_pcibk_dev_data *dev_data;
	int rc;
	int enable = 0;

	dev_data = pci_get_drvdata(dev);
	if (!dev_data)
		return;

	/* We don't deal with bridges */
	if (dev->hdr_type != PCI_HEADER_TYPE_NORMAL)
		return;

	if (reset) {
		dev_data->enable_intx = 0;
		dev_data->ack_intr = 0;
	}
	enable =  dev_data->enable_intx;

	/* Asked to disable, but ISR isn't runnig */
	if (!enable && !dev_data->isr_on)
		return;

	/* Squirrel away the IRQs in the dev_data. We need this
	 * b/c when device transitions to MSI, the dev->irq is
	 * overwritten with the MSI vector.
	 */
	if (enable)
		dev_data->irq = dev->irq;

	/*
	 * SR-IOV devices in all use MSI-X and have no legacy
	 * interrupts, so inhibit creating a fake IRQ handler for them.
	 */
	if (dev_data->irq == 0)
		goto out;

	dev_dbg(&dev->dev, "%s: #%d %s %s%s %s-> %s\n",
		dev_data->irq_name,
		dev_data->irq,
		pci_is_enabled(dev) ? "on" : "off",
		dev->msi_enabled ? "MSI" : "",
		dev->msix_enabled ? "MSI/X" : "",
		dev_data->isr_on ? "enable" : "disable",
		enable ? "enable" : "disable");

	if (enable) {
		rc = request_irq(dev_data->irq,
				xen_pcibk_guest_interrupt, IRQF_SHARED,
				dev_data->irq_name, dev);
		if (rc) {
			dev_err(&dev->dev, "%s: failed to install fake IRQ " \
				"handler for IRQ %d! (rc:%d)\n",
				dev_data->irq_name, dev_data->irq, rc);
			goto out;
		}
	} else {
		free_irq(dev_data->irq, dev);
		dev_data->irq = 0;
	}
	dev_data->isr_on = enable;
	dev_data->ack_intr = enable;
out:
	dev_dbg(&dev->dev, "%s: #%d %s %s%s %s\n",
		dev_data->irq_name,
		dev_data->irq,
		pci_is_enabled(dev) ? "on" : "off",
		dev->msi_enabled ? "MSI" : "",
		dev->msix_enabled ? "MSI/X" : "",
		enable ? (dev_data->isr_on ? "enabled" : "failed to enable") :
			(dev_data->isr_on ? "failed to disable" : "disabled"));
}

/* Ensure a device is "turned off" and ready to be exported.
 * (Also see xen_pcibk_config_reset to ensure virtual configuration space is
 * ready to be re-exported)
 */
void xen_pcibk_reset_device(struct pci_dev *dev)
{
	u16 cmd;

	xen_pcibk_control_isr(dev, 1 /* reset device */);

	/* Disable devices (but not bridges) */
	if (dev->hdr_type == PCI_HEADER_TYPE_NORMAL) {
#ifdef CONFIG_PCI_MSI
		/* The guest could have been abruptly killed without
		 * disabling MSI/MSI-X interrupts.*/
		if (dev->msix_enabled)
			pci_disable_msix(dev);
		if (dev->msi_enabled)
			pci_disable_msi(dev);
#endif
		if (pci_is_enabled(dev))
			pci_disable_device(dev);

		pci_write_config_word(dev, PCI_COMMAND, 0);

		dev->is_busmaster = 0;
	} else {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (cmd & (PCI_COMMAND_INVALIDATE)) {
			cmd &= ~(PCI_COMMAND_INVALIDATE);
			pci_write_config_word(dev, PCI_COMMAND, cmd);

			dev->is_busmaster = 0;
		}
	}
}

#ifdef CONFIG_PCI_MSI
static
int xen_pcibk_enable_msi(struct xen_pcibk_device *pdev,
			 struct pci_dev *dev, struct xen_pci_op *op)
{
	struct xen_pcibk_dev_data *dev_data;
	int status;

	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: enable MSI\n", pci_name(dev));

	status = pci_enable_msi(dev);

	if (status) {
		pr_warn_ratelimited("%s: error enabling MSI for guest %u: err %d\n",
				    pci_name(dev), pdev->xdev->otherend_id,
				    status);
		op->value = 0;
		return XEN_PCI_ERR_op_failed;
	}

	/* The value the guest needs is actually the IDT vector, not the
	 * the local domain's IRQ number. */

	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: MSI: %d\n", pci_name(dev),
			op->value);

	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 0;

	return 0;
}

static
int xen_pcibk_disable_msi(struct xen_pcibk_device *pdev,
			  struct pci_dev *dev, struct xen_pci_op *op)
{
	struct xen_pcibk_dev_data *dev_data;

	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: disable MSI\n",
		       pci_name(dev));
	pci_disable_msi(dev);

	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: MSI: %d\n", pci_name(dev),
			op->value);
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 1;
	return 0;
}

static
int xen_pcibk_enable_msix(struct xen_pcibk_device *pdev,
			  struct pci_dev *dev, struct xen_pci_op *op)
{
	struct xen_pcibk_dev_data *dev_data;
	int i, result;
	struct msix_entry *entries;

	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: enable MSI-X\n",
		       pci_name(dev));
	if (op->value > SH_INFO_MAX_VEC)
		return -EINVAL;

	entries = kmalloc(op->value * sizeof(*entries), GFP_KERNEL);
	if (entries == NULL)
		return -ENOMEM;

	for (i = 0; i < op->value; i++) {
		entries[i].entry = op->msix_entries[i].entry;
		entries[i].vector = op->msix_entries[i].vector;
	}

	result = pci_enable_msix_exact(dev, entries, op->value);
	if (result == 0) {
		for (i = 0; i < op->value; i++) {
			op->msix_entries[i].entry = entries[i].entry;
			if (entries[i].vector) {
				op->msix_entries[i].vector =
					xen_pirq_from_irq(entries[i].vector);
				if (unlikely(verbose_request))
					printk(KERN_DEBUG DRV_NAME ": %s: " \
						"MSI-X[%d]: %d\n",
						pci_name(dev), i,
						op->msix_entries[i].vector);
			}
		}
	} else
		pr_warn_ratelimited("%s: error enabling MSI-X for guest %u: err %d!\n",
				    pci_name(dev), pdev->xdev->otherend_id,
				    result);
	kfree(entries);

	op->value = result;
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 0;

	return result > 0 ? 0 : result;
}

static
int xen_pcibk_disable_msix(struct xen_pcibk_device *pdev,
			   struct pci_dev *dev, struct xen_pci_op *op)
{
	struct xen_pcibk_dev_data *dev_data;
	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: disable MSI-X\n",
			pci_name(dev));
	pci_disable_msix(dev);

	/*
	 * SR-IOV devices (which don't have any legacy IRQ) have
	 * an undefined IRQ value of zero.
	 */
	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	if (unlikely(verbose_request))
		printk(KERN_DEBUG DRV_NAME ": %s: MSI-X: %d\n", pci_name(dev),
			op->value);
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 1;
	return 0;
}
#endif
/*
* Now the same evtchn is used for both pcifront conf_read_write request
* as well as pcie aer front end ack. We use a new work_queue to schedule
* xen_pcibk conf_read_write service for avoiding confict with aer_core
* do_recovery job which also use the system default work_queue
*/
void xen_pcibk_test_and_schedule_op(struct xen_pcibk_device *pdev)
{
	/* Check that frontend is requesting an operation and that we are not
	 * already processing a request */
	if (test_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags)
	    && !test_and_set_bit(_PDEVF_op_active, &pdev->flags)) {
		queue_work(xen_pcibk_wq, &pdev->op_work);
	}
	/*_XEN_PCIB_active should have been cleared by pcifront. And also make
	sure xen_pcibk is waiting for ack by checking _PCIB_op_pending*/
	if (!test_bit(_XEN_PCIB_active, (unsigned long *)&pdev->sh_info->flags)
	    && test_bit(_PCIB_op_pending, &pdev->flags)) {
		wake_up(&xen_pcibk_aer_wait_queue);
	}
}

/* Performing the configuration space reads/writes must not be done in atomic
 * context because some of the pci_* functions can sleep (mostly due to ACPI
 * use of semaphores). This function is intended to be called from a work
 * queue in process context taking a struct xen_pcibk_device as a parameter */

void xen_pcibk_do_op(struct work_struct *data)
{
	struct xen_pcibk_device *pdev =
		container_of(data, struct xen_pcibk_device, op_work);
	struct pci_dev *dev;
	struct xen_pcibk_dev_data *dev_data = NULL;
	struct xen_pci_op *op = &pdev->sh_info->op;
	int test_intx = 0;

	dev = xen_pcibk_get_pci_dev(pdev, op->domain, op->bus, op->devfn);

	if (dev == NULL)
		op->err = XEN_PCI_ERR_dev_not_found;
	else {
		dev_data = pci_get_drvdata(dev);
		if (dev_data)
			test_intx = dev_data->enable_intx;
		switch (op->cmd) {
		case XEN_PCI_OP_conf_read:
			op->err = xen_pcibk_config_read(dev,
				  op->offset, op->size, &op->value);
			break;
		case XEN_PCI_OP_conf_write:
			op->err = xen_pcibk_config_write(dev,
				  op->offset, op->size,	op->value);
			break;
#ifdef CONFIG_PCI_MSI
		case XEN_PCI_OP_enable_msi:
			op->err = xen_pcibk_enable_msi(pdev, dev, op);
			break;
		case XEN_PCI_OP_disable_msi:
			op->err = xen_pcibk_disable_msi(pdev, dev, op);
			break;
		case XEN_PCI_OP_enable_msix:
			op->err = xen_pcibk_enable_msix(pdev, dev, op);
			break;
		case XEN_PCI_OP_disable_msix:
			op->err = xen_pcibk_disable_msix(pdev, dev, op);
			break;
#endif
		default:
			op->err = XEN_PCI_ERR_not_implemented;
			break;
		}
	}
	if (!op->err && dev && dev_data) {
		/* Transition detected */
		if ((dev_data->enable_intx != test_intx))
			xen_pcibk_control_isr(dev, 0 /* no reset */);
	}
	/* Tell the driver domain that we're done. */
	wmb();
	clear_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags);
	notify_remote_via_irq(pdev->evtchn_irq);

	/* Mark that we're done. */
	smp_mb__before_clear_bit(); /* /after/ clearing PCIF_active */
	clear_bit(_PDEVF_op_active, &pdev->flags);
	smp_mb__after_clear_bit(); /* /before/ final check for work */

	/* Check to see if the driver domain tried to start another request in
	 * between clearing _XEN_PCIF_active and clearing _PDEVF_op_active.
	*/
	xen_pcibk_test_and_schedule_op(pdev);
}

irqreturn_t xen_pcibk_handle_event(int irq, void *dev_id)
{
	struct xen_pcibk_device *pdev = dev_id;

	xen_pcibk_test_and_schedule_op(pdev);

	return IRQ_HANDLED;
}
static irqreturn_t xen_pcibk_guest_interrupt(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;
	struct xen_pcibk_dev_data *dev_data = pci_get_drvdata(dev);

	if (dev_data->isr_on && dev_data->ack_intr) {
		dev_data->handled++;
		if ((dev_data->handled % 1000) == 0) {
			if (xen_test_irq_shared(irq)) {
				pr_info("%s IRQ line is not shared "
					"with other domains. Turning ISR off\n",
					 dev_data->irq_name);
				dev_data->ack_intr = 0;
			}
		}
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/*
 * PCI Backend Operations - respond to PCI requests from Frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <xen/events.h>
#include <linux/sched.h>
#include "pciback.h"

int verbose_request;
module_param(verbose_request, int, 0644);

/* Ensure a device is "turned off" and ready to be exported.
 * (Also see pciback_config_reset to ensure virtual configuration space is
 * ready to be re-exported)
 */
void pciback_reset_device(struct pci_dev *dev)
{
	u16 cmd;

	/* Disable devices (but not bridges) */
	if (dev->hdr_type == PCI_HEADER_TYPE_NORMAL) {
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
/*
* Now the same evtchn is used for both pcifront conf_read_write request
* as well as pcie aer front end ack. We use a new work_queue to schedule
* pciback conf_read_write service for avoiding confict with aer_core
* do_recovery job which also use the system default work_queue
*/
void test_and_schedule_op(struct pciback_device *pdev)
{
	/* Check that frontend is requesting an operation and that we are not
	 * already processing a request */
	if (test_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags)
	    && !test_and_set_bit(_PDEVF_op_active, &pdev->flags)) {
		queue_work(pciback_wq, &pdev->op_work);
	}
	/*_XEN_PCIB_active should have been cleared by pcifront. And also make
	sure pciback is waiting for ack by checking _PCIB_op_pending*/
	if (!test_bit(_XEN_PCIB_active, (unsigned long *)&pdev->sh_info->flags)
	    && test_bit(_PCIB_op_pending, &pdev->flags)) {
		wake_up(&aer_wait_queue);
	}
}

/* Performing the configuration space reads/writes must not be done in atomic
 * context because some of the pci_* functions can sleep (mostly due to ACPI
 * use of semaphores). This function is intended to be called from a work
 * queue in process context taking a struct pciback_device as a parameter */

void pciback_do_op(struct work_struct *data)
{
	struct pciback_device *pdev =
		container_of(data, struct pciback_device, op_work);
	struct pci_dev *dev;
	struct xen_pci_op *op = &pdev->sh_info->op;

	dev = pciback_get_pci_dev(pdev, op->domain, op->bus, op->devfn);

	if (dev == NULL)
		op->err = XEN_PCI_ERR_dev_not_found;
	else {
		switch (op->cmd) {
		case XEN_PCI_OP_conf_read:
			op->err = pciback_config_read(dev,
				  op->offset, op->size, &op->value);
			break;
		case XEN_PCI_OP_conf_write:
			op->err = pciback_config_write(dev,
				  op->offset, op->size,	op->value);
			break;
#ifdef CONFIG_PCI_MSI
		case XEN_PCI_OP_enable_msi:
			op->err = pciback_enable_msi(pdev, dev, op);
			break;
		case XEN_PCI_OP_disable_msi:
			op->err = pciback_disable_msi(pdev, dev, op);
			break;
		case XEN_PCI_OP_enable_msix:
			op->err = pciback_enable_msix(pdev, dev, op);
			break;
		case XEN_PCI_OP_disable_msix:
			op->err = pciback_disable_msix(pdev, dev, op);
			break;
#endif
		default:
			op->err = XEN_PCI_ERR_not_implemented;
			break;
		}
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
	test_and_schedule_op(pdev);
}

irqreturn_t pciback_handle_event(int irq, void *dev_id)
{
	struct pciback_device *pdev = dev_id;

	test_and_schedule_op(pdev);

	return IRQ_HANDLED;
}

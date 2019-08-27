/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "fnic_io.h"
#include "fnic.h"

static irqreturn_t fnic_isr_legacy(int irq, void *data)
{
	struct fnic *fnic = data;
	u32 pba;
	unsigned long work_done = 0;

	pba = vnic_intr_legacy_pba(fnic->legacy_pba);
	if (!pba)
		return IRQ_NONE;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	if (pba & (1 << FNIC_INTX_NOTIFY)) {
		vnic_intr_return_all_credits(&fnic->intr[FNIC_INTX_NOTIFY]);
		fnic_handle_link_event(fnic);
	}

	if (pba & (1 << FNIC_INTX_ERR)) {
		vnic_intr_return_all_credits(&fnic->intr[FNIC_INTX_ERR]);
		fnic_log_q_error(fnic);
	}

	if (pba & (1 << FNIC_INTX_WQ_RQ_COPYWQ)) {
		work_done += fnic_wq_copy_cmpl_handler(fnic, -1);
		work_done += fnic_wq_cmpl_handler(fnic, -1);
		work_done += fnic_rq_cmpl_handler(fnic, -1);

		vnic_intr_return_credits(&fnic->intr[FNIC_INTX_WQ_RQ_COPYWQ],
					 work_done,
					 1 /* unmask intr */,
					 1 /* reset intr timer */);
	}

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msi(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	work_done += fnic_wq_copy_cmpl_handler(fnic, -1);
	work_done += fnic_wq_cmpl_handler(fnic, -1);
	work_done += fnic_rq_cmpl_handler(fnic, -1);

	vnic_intr_return_credits(&fnic->intr[0],
				 work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_rq(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long rq_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	rq_work_done = fnic_rq_cmpl_handler(fnic, -1);
	vnic_intr_return_credits(&fnic->intr[FNIC_MSIX_RQ],
				 rq_work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_wq(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long wq_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	wq_work_done = fnic_wq_cmpl_handler(fnic, -1);
	vnic_intr_return_credits(&fnic->intr[FNIC_MSIX_WQ],
				 wq_work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);
	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_wq_copy(int irq, void *data)
{
	struct fnic *fnic = data;
	unsigned long wq_copy_work_done = 0;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	wq_copy_work_done = fnic_wq_copy_cmpl_handler(fnic, -1);
	vnic_intr_return_credits(&fnic->intr[FNIC_MSIX_WQ_COPY],
				 wq_copy_work_done,
				 1 /* unmask intr */,
				 1 /* reset intr timer */);
	return IRQ_HANDLED;
}

static irqreturn_t fnic_isr_msix_err_notify(int irq, void *data)
{
	struct fnic *fnic = data;

	fnic->fnic_stats.misc_stats.last_isr_time = jiffies;
	atomic64_inc(&fnic->fnic_stats.misc_stats.isr_count);

	vnic_intr_return_all_credits(&fnic->intr[FNIC_MSIX_ERR_NOTIFY]);
	fnic_log_q_error(fnic);
	fnic_handle_link_event(fnic);

	return IRQ_HANDLED;
}

void fnic_free_intr(struct fnic *fnic)
{
	int i;

	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
	case VNIC_DEV_INTR_MODE_MSI:
		free_irq(pci_irq_vector(fnic->pdev, 0), fnic);
		break;

	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < ARRAY_SIZE(fnic->msix); i++)
			if (fnic->msix[i].requested)
				free_irq(pci_irq_vector(fnic->pdev, i),
					 fnic->msix[i].devid);
		break;

	default:
		break;
	}
}

int fnic_request_intr(struct fnic *fnic)
{
	int err = 0;
	int i;

	switch (vnic_dev_get_intr_mode(fnic->vdev)) {

	case VNIC_DEV_INTR_MODE_INTX:
		err = request_irq(pci_irq_vector(fnic->pdev, 0),
				&fnic_isr_legacy, IRQF_SHARED, DRV_NAME, fnic);
		break;

	case VNIC_DEV_INTR_MODE_MSI:
		err = request_irq(pci_irq_vector(fnic->pdev, 0), &fnic_isr_msi,
				  0, fnic->name, fnic);
		break;

	case VNIC_DEV_INTR_MODE_MSIX:

		sprintf(fnic->msix[FNIC_MSIX_RQ].devname,
			"%.11s-fcs-rq", fnic->name);
		fnic->msix[FNIC_MSIX_RQ].isr = fnic_isr_msix_rq;
		fnic->msix[FNIC_MSIX_RQ].devid = fnic;

		sprintf(fnic->msix[FNIC_MSIX_WQ].devname,
			"%.11s-fcs-wq", fnic->name);
		fnic->msix[FNIC_MSIX_WQ].isr = fnic_isr_msix_wq;
		fnic->msix[FNIC_MSIX_WQ].devid = fnic;

		sprintf(fnic->msix[FNIC_MSIX_WQ_COPY].devname,
			"%.11s-scsi-wq", fnic->name);
		fnic->msix[FNIC_MSIX_WQ_COPY].isr = fnic_isr_msix_wq_copy;
		fnic->msix[FNIC_MSIX_WQ_COPY].devid = fnic;

		sprintf(fnic->msix[FNIC_MSIX_ERR_NOTIFY].devname,
			"%.11s-err-notify", fnic->name);
		fnic->msix[FNIC_MSIX_ERR_NOTIFY].isr =
			fnic_isr_msix_err_notify;
		fnic->msix[FNIC_MSIX_ERR_NOTIFY].devid = fnic;

		for (i = 0; i < ARRAY_SIZE(fnic->msix); i++) {
			err = request_irq(pci_irq_vector(fnic->pdev, i),
					  fnic->msix[i].isr, 0,
					  fnic->msix[i].devname,
					  fnic->msix[i].devid);
			if (err) {
				shost_printk(KERN_ERR, fnic->lport->host,
					     "MSIX: request_irq"
					     " failed %d\n", err);
				fnic_free_intr(fnic);
				break;
			}
			fnic->msix[i].requested = 1;
		}
		break;

	default:
		break;
	}

	return err;
}

int fnic_set_intr_mode(struct fnic *fnic)
{
	unsigned int n = ARRAY_SIZE(fnic->rq);
	unsigned int m = ARRAY_SIZE(fnic->wq);
	unsigned int o = ARRAY_SIZE(fnic->wq_copy);

	/*
	 * Set interrupt mode (INTx, MSI, MSI-X) depending
	 * system capabilities.
	 *
	 * Try MSI-X first
	 *
	 * We need n RQs, m WQs, o Copy WQs, n+m+o CQs, and n+m+o+1 INTRs
	 * (last INTR is used for WQ/RQ errors and notification area)
	 */
	if (fnic->rq_count >= n &&
	    fnic->raw_wq_count >= m &&
	    fnic->wq_copy_count >= o &&
	    fnic->cq_count >= n + m + o) {
		int vecs = n + m + o + 1;

		if (pci_alloc_irq_vectors(fnic->pdev, vecs, vecs,
				PCI_IRQ_MSIX) == vecs) {
			fnic->rq_count = n;
			fnic->raw_wq_count = m;
			fnic->wq_copy_count = o;
			fnic->wq_count = m + o;
			fnic->cq_count = n + m + o;
			fnic->intr_count = vecs;
			fnic->err_intr_offset = FNIC_MSIX_ERR_NOTIFY;

			FNIC_ISR_DBG(KERN_DEBUG, fnic->lport->host,
				     "Using MSI-X Interrupts\n");
			vnic_dev_set_intr_mode(fnic->vdev,
					       VNIC_DEV_INTR_MODE_MSIX);
			return 0;
		}
	}

	/*
	 * Next try MSI
	 * We need 1 RQ, 1 WQ, 1 WQ_COPY, 3 CQs, and 1 INTR
	 */
	if (fnic->rq_count >= 1 &&
	    fnic->raw_wq_count >= 1 &&
	    fnic->wq_copy_count >= 1 &&
	    fnic->cq_count >= 3 &&
	    fnic->intr_count >= 1 &&
	    pci_alloc_irq_vectors(fnic->pdev, 1, 1, PCI_IRQ_MSI) == 1) {
		fnic->rq_count = 1;
		fnic->raw_wq_count = 1;
		fnic->wq_copy_count = 1;
		fnic->wq_count = 2;
		fnic->cq_count = 3;
		fnic->intr_count = 1;
		fnic->err_intr_offset = 0;

		FNIC_ISR_DBG(KERN_DEBUG, fnic->lport->host,
			     "Using MSI Interrupts\n");
		vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_MSI);

		return 0;
	}

	/*
	 * Next try INTx
	 * We need 1 RQ, 1 WQ, 1 WQ_COPY, 3 CQs, and 3 INTRs
	 * 1 INTR is used for all 3 queues, 1 INTR for queue errors
	 * 1 INTR for notification area
	 */

	if (fnic->rq_count >= 1 &&
	    fnic->raw_wq_count >= 1 &&
	    fnic->wq_copy_count >= 1 &&
	    fnic->cq_count >= 3 &&
	    fnic->intr_count >= 3) {

		fnic->rq_count = 1;
		fnic->raw_wq_count = 1;
		fnic->wq_copy_count = 1;
		fnic->cq_count = 3;
		fnic->intr_count = 3;

		FNIC_ISR_DBG(KERN_DEBUG, fnic->lport->host,
			     "Using Legacy Interrupts\n");
		vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_INTX);

		return 0;
	}

	vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);

	return -EINVAL;
}

void fnic_clear_intr_mode(struct fnic *fnic)
{
	pci_free_irq_vectors(fnic->pdev);
	vnic_dev_set_intr_mode(fnic->vdev, VNIC_DEV_INTR_MODE_INTX);
}


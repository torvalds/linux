/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
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

#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "snic_io.h"
#include "snic.h"


/*
 * snic_isr_msix_wq : MSIx ISR for work queue.
 */

static irqreturn_t
snic_isr_msix_wq(int irq, void *data)
{
	struct snic *snic = data;
	unsigned long wq_work_done = 0;

	snic->s_stats.misc.last_isr_time = jiffies;
	atomic64_inc(&snic->s_stats.misc.ack_isr_cnt);

	wq_work_done = snic_wq_cmpl_handler(snic, -1);
	svnic_intr_return_credits(&snic->intr[SNIC_MSIX_WQ],
				  wq_work_done,
				  1 /* unmask intr */,
				  1 /* reset intr timer */);

	return IRQ_HANDLED;
} /* end of snic_isr_msix_wq */

static irqreturn_t
snic_isr_msix_io_cmpl(int irq, void *data)
{
	struct snic *snic = data;
	unsigned long iocmpl_work_done = 0;

	snic->s_stats.misc.last_isr_time = jiffies;
	atomic64_inc(&snic->s_stats.misc.cmpl_isr_cnt);

	iocmpl_work_done = snic_fwcq_cmpl_handler(snic, -1);
	svnic_intr_return_credits(&snic->intr[SNIC_MSIX_IO_CMPL],
				  iocmpl_work_done,
				  1 /* unmask intr */,
				  1 /* reset intr timer */);

	return IRQ_HANDLED;
} /* end of snic_isr_msix_io_cmpl */

static irqreturn_t
snic_isr_msix_err_notify(int irq, void *data)
{
	struct snic *snic = data;

	snic->s_stats.misc.last_isr_time = jiffies;
	atomic64_inc(&snic->s_stats.misc.errnotify_isr_cnt);

	svnic_intr_return_all_credits(&snic->intr[SNIC_MSIX_ERR_NOTIFY]);
	snic_log_q_error(snic);

	/*Handling link events */
	snic_handle_link_event(snic);

	return IRQ_HANDLED;
} /* end of snic_isr_msix_err_notify */


void
snic_free_intr(struct snic *snic)
{
	int i;

	/* ONLY interrupt mode MSIX is supported */
	for (i = 0; i < ARRAY_SIZE(snic->msix); i++) {
		if (snic->msix[i].requested) {
			free_irq(pci_irq_vector(snic->pdev, i),
				 snic->msix[i].devid);
		}
	}
} /* end of snic_free_intr */

int
snic_request_intr(struct snic *snic)
{
	int ret = 0, i;
	enum vnic_dev_intr_mode intr_mode;

	intr_mode = svnic_dev_get_intr_mode(snic->vdev);
	SNIC_BUG_ON(intr_mode != VNIC_DEV_INTR_MODE_MSIX);

	/*
	 * Currently HW supports single WQ and CQ. So passing devid as snic.
	 * When hardware supports multiple WQs and CQs, one idea is
	 * to pass devid as corresponding WQ or CQ ptr and retrieve snic
	 * from queue ptr.
	 * Except for err_notify, which is always one.
	 */
	sprintf(snic->msix[SNIC_MSIX_WQ].devname,
		"%.11s-scsi-wq",
		snic->name);
	snic->msix[SNIC_MSIX_WQ].isr = snic_isr_msix_wq;
	snic->msix[SNIC_MSIX_WQ].devid = snic;

	sprintf(snic->msix[SNIC_MSIX_IO_CMPL].devname,
		"%.11s-io-cmpl",
		snic->name);
	snic->msix[SNIC_MSIX_IO_CMPL].isr = snic_isr_msix_io_cmpl;
	snic->msix[SNIC_MSIX_IO_CMPL].devid = snic;

	sprintf(snic->msix[SNIC_MSIX_ERR_NOTIFY].devname,
		"%.11s-err-notify",
		snic->name);
	snic->msix[SNIC_MSIX_ERR_NOTIFY].isr = snic_isr_msix_err_notify;
	snic->msix[SNIC_MSIX_ERR_NOTIFY].devid = snic;

	for (i = 0; i < ARRAY_SIZE(snic->msix); i++) {
		ret = request_irq(pci_irq_vector(snic->pdev, i),
				  snic->msix[i].isr,
				  0,
				  snic->msix[i].devname,
				  snic->msix[i].devid);
		if (ret) {
			SNIC_HOST_ERR(snic->shost,
				      "MSI-X: request_irq(%d) failed %d\n",
				      i,
				      ret);
			snic_free_intr(snic);
			break;
		}
		snic->msix[i].requested = 1;
	}

	return ret;
} /* end of snic_request_intr */

int
snic_set_intr_mode(struct snic *snic)
{
	unsigned int n = ARRAY_SIZE(snic->wq);
	unsigned int m = SNIC_CQ_IO_CMPL_MAX;
	unsigned int vecs = n + m + 1;

	/*
	 * We need n WQs, m CQs, and n+m+1 INTRs
	 * (last INTR is used for WQ/CQ errors and notification area
	 */
	BUILD_BUG_ON((ARRAY_SIZE(snic->wq) + SNIC_CQ_IO_CMPL_MAX) >
			ARRAY_SIZE(snic->intr));

	if (snic->wq_count < n || snic->cq_count < n + m)
		goto fail;

	if (pci_alloc_irq_vectors(snic->pdev, vecs, vecs, PCI_IRQ_MSIX) < 0)
		goto fail;

	snic->wq_count = n;
	snic->cq_count = n + m;
	snic->intr_count = vecs;
	snic->err_intr_offset = SNIC_MSIX_ERR_NOTIFY;

	SNIC_ISR_DBG(snic->shost, "Using MSI-X Interrupts\n");
	svnic_dev_set_intr_mode(snic->vdev, VNIC_DEV_INTR_MODE_MSIX);
	return 0;
fail:
	svnic_dev_set_intr_mode(snic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);
	return -EINVAL;
} /* end of snic_set_intr_mode */

void
snic_clear_intr_mode(struct snic *snic)
{
	pci_free_irq_vectors(snic->pdev);
	svnic_dev_set_intr_mode(snic->vdev, VNIC_DEV_INTR_MODE_INTX);
}

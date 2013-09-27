/*
 *  linux/drivers/scsi/esas2r/esas2r_init.c
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)mpt3sas/mpt3sas_trigger_diag.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.
 *
 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include "esas2r.h"

static bool esas2r_initmem_alloc(struct esas2r_adapter *a,
				 struct esas2r_mem_desc *mem_desc,
				 u32 align)
{
	mem_desc->esas2r_param = mem_desc->size + align;
	mem_desc->virt_addr = NULL;
	mem_desc->phys_addr = 0;
	mem_desc->esas2r_data = dma_alloc_coherent(&a->pcid->dev,
						   (size_t)mem_desc->
						   esas2r_param,
						   (dma_addr_t *)&mem_desc->
						   phys_addr,
						   GFP_KERNEL);

	if (mem_desc->esas2r_data == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to allocate %lu bytes of consistent memory!",
			   (long
			    unsigned
			    int)mem_desc->esas2r_param);
		return false;
	}

	mem_desc->virt_addr = PTR_ALIGN(mem_desc->esas2r_data, align);
	mem_desc->phys_addr = ALIGN(mem_desc->phys_addr, align);
	memset(mem_desc->virt_addr, 0, mem_desc->size);
	return true;
}

static void esas2r_initmem_free(struct esas2r_adapter *a,
				struct esas2r_mem_desc *mem_desc)
{
	if (mem_desc->virt_addr == NULL)
		return;

	/*
	 * Careful!  phys_addr and virt_addr may have been adjusted from the
	 * original allocation in order to return the desired alignment.  That
	 * means we have to use the original address (in esas2r_data) and size
	 * (esas2r_param) and calculate the original physical address based on
	 * the difference between the requested and actual allocation size.
	 */
	if (mem_desc->phys_addr) {
		int unalign = ((u8 *)mem_desc->virt_addr) -
			      ((u8 *)mem_desc->esas2r_data);

		dma_free_coherent(&a->pcid->dev,
				  (size_t)mem_desc->esas2r_param,
				  mem_desc->esas2r_data,
				  (dma_addr_t)(mem_desc->phys_addr - unalign));
	} else {
		kfree(mem_desc->esas2r_data);
	}

	mem_desc->virt_addr = NULL;
}

static bool alloc_vda_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq)
{
	struct esas2r_mem_desc *memdesc = kzalloc(
		sizeof(struct esas2r_mem_desc), GFP_KERNEL);

	if (memdesc == NULL) {
		esas2r_hdebug("could not alloc mem for vda request memdesc\n");
		return false;
	}

	memdesc->size = sizeof(union atto_vda_req) +
			ESAS2R_DATA_BUF_LEN;

	if (!esas2r_initmem_alloc(a, memdesc, 256)) {
		esas2r_hdebug("could not alloc mem for vda request\n");
		kfree(memdesc);
		return false;
	}

	a->num_vrqs++;
	list_add(&memdesc->next_desc, &a->vrq_mds_head);

	rq->vrq_md = memdesc;
	rq->vrq = (union atto_vda_req *)memdesc->virt_addr;
	rq->vrq->scsi.handle = a->num_vrqs;

	return true;
}

static void esas2r_unmap_regions(struct esas2r_adapter *a)
{
	if (a->regs)
		iounmap((void __iomem *)a->regs);

	a->regs = NULL;

	pci_release_region(a->pcid, 2);

	if (a->data_window)
		iounmap((void __iomem *)a->data_window);

	a->data_window = NULL;

	pci_release_region(a->pcid, 0);
}

static int esas2r_map_regions(struct esas2r_adapter *a)
{
	int error;

	a->regs = NULL;
	a->data_window = NULL;

	error = pci_request_region(a->pcid, 2, a->name);
	if (error != 0) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "pci_request_region(2) failed, error %d",
			   error);

		return error;
	}

	a->regs = (void __force *)ioremap(pci_resource_start(a->pcid, 2),
					  pci_resource_len(a->pcid, 2));
	if (a->regs == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "ioremap failed for regs mem region\n");
		pci_release_region(a->pcid, 2);
		return -EFAULT;
	}

	error = pci_request_region(a->pcid, 0, a->name);
	if (error != 0) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "pci_request_region(2) failed, error %d",
			   error);
		esas2r_unmap_regions(a);
		return error;
	}

	a->data_window = (void __force *)ioremap(pci_resource_start(a->pcid,
								    0),
						 pci_resource_len(a->pcid, 0));
	if (a->data_window == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "ioremap failed for data_window mem region\n");
		esas2r_unmap_regions(a);
		return -EFAULT;
	}

	return 0;
}

static void esas2r_setup_interrupts(struct esas2r_adapter *a, int intr_mode)
{
	int i;

	/* Set up interrupt mode based on the requested value */
	switch (intr_mode) {
	case INTR_MODE_LEGACY:
use_legacy_interrupts:
		a->intr_mode = INTR_MODE_LEGACY;
		break;

	case INTR_MODE_MSI:
		i = pci_enable_msi(a->pcid);
		if (i != 0) {
			esas2r_log(ESAS2R_LOG_WARN,
				   "failed to enable MSI for adapter %d, "
				   "falling back to legacy interrupts "
				   "(err=%d)", a->index,
				   i);
			goto use_legacy_interrupts;
		}
		a->intr_mode = INTR_MODE_MSI;
		esas2r_lock_set_flags(&a->flags2, AF2_MSI_ENABLED);
		break;


	default:
		esas2r_log(ESAS2R_LOG_WARN,
			   "unknown interrupt_mode %d requested, "
			   "falling back to legacy interrupt",
			   interrupt_mode);
		goto use_legacy_interrupts;
	}
}

static void esas2r_claim_interrupts(struct esas2r_adapter *a)
{
	unsigned long flags = IRQF_DISABLED;

	if (a->intr_mode == INTR_MODE_LEGACY)
		flags |= IRQF_SHARED;

	esas2r_log(ESAS2R_LOG_INFO,
		   "esas2r_claim_interrupts irq=%d (%p, %s, %x)",
		   a->pcid->irq, a, a->name, flags);

	if (request_irq(a->pcid->irq,
			(a->intr_mode ==
			 INTR_MODE_LEGACY) ? esas2r_interrupt :
			esas2r_msi_interrupt,
			flags,
			a->name,
			a)) {
		esas2r_log(ESAS2R_LOG_CRIT, "unable to request IRQ %02X",
			   a->pcid->irq);
		return;
	}

	esas2r_lock_set_flags(&a->flags2, AF2_IRQ_CLAIMED);
	esas2r_log(ESAS2R_LOG_INFO,
		   "claimed IRQ %d flags: 0x%lx",
		   a->pcid->irq, flags);
}

int esas2r_init_adapter(struct Scsi_Host *host, struct pci_dev *pcid,
			int index)
{
	struct esas2r_adapter *a;
	u64 bus_addr = 0;
	int i;
	void *next_uncached;
	struct esas2r_request *first_request, *last_request;

	if (index >= MAX_ADAPTERS) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "tried to init invalid adapter index %u!",
			   index);
		return 0;
	}

	if (esas2r_adapters[index]) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "tried to init existing adapter index %u!",
			   index);
		return 0;
	}

	a = (struct esas2r_adapter *)host->hostdata;
	memset(a, 0, sizeof(struct esas2r_adapter));
	a->pcid = pcid;
	a->host = host;

	if (sizeof(dma_addr_t) > 4) {
		const uint64_t required_mask = dma_get_required_mask
						       (&pcid->dev);
		if (required_mask > DMA_BIT_MASK(32)
		    && !pci_set_dma_mask(pcid, DMA_BIT_MASK(64))
		    && !pci_set_consistent_dma_mask(pcid,
						    DMA_BIT_MASK(64))) {
			esas2r_log_dev(ESAS2R_LOG_INFO,
				       &(a->pcid->dev),
				       "64-bit PCI addressing enabled\n");
		} else if (!pci_set_dma_mask(pcid, DMA_BIT_MASK(32))
			   && !pci_set_consistent_dma_mask(pcid,
							   DMA_BIT_MASK(32))) {
			esas2r_log_dev(ESAS2R_LOG_INFO,
				       &(a->pcid->dev),
				       "32-bit PCI addressing enabled\n");
		} else {
			esas2r_log(ESAS2R_LOG_CRIT,
				   "failed to set DMA mask");
			esas2r_kill_adapter(index);
			return 0;
		}
	} else {
		if (!pci_set_dma_mask(pcid, DMA_BIT_MASK(32))
		    && !pci_set_consistent_dma_mask(pcid,
						    DMA_BIT_MASK(32))) {
			esas2r_log_dev(ESAS2R_LOG_INFO,
				       &(a->pcid->dev),
				       "32-bit PCI addressing enabled\n");
		} else {
			esas2r_log(ESAS2R_LOG_CRIT,
				   "failed to set DMA mask");
			esas2r_kill_adapter(index);
			return 0;
		}
	}
	esas2r_adapters[index] = a;
	sprintf(a->name, ESAS2R_DRVR_NAME "_%02d", index);
	esas2r_debug("new adapter %p, name %s", a, a->name);
	spin_lock_init(&a->request_lock);
	spin_lock_init(&a->fw_event_lock);
	sema_init(&a->fm_api_semaphore, 1);
	sema_init(&a->fs_api_semaphore, 1);
	sema_init(&a->nvram_semaphore, 1);

	esas2r_fw_event_off(a);
	snprintf(a->fw_event_q_name, ESAS2R_KOBJ_NAME_LEN, "esas2r/%d",
		 a->index);
	a->fw_event_q = create_singlethread_workqueue(a->fw_event_q_name);

	init_waitqueue_head(&a->buffered_ioctl_waiter);
	init_waitqueue_head(&a->nvram_waiter);
	init_waitqueue_head(&a->fm_api_waiter);
	init_waitqueue_head(&a->fs_api_waiter);
	init_waitqueue_head(&a->vda_waiter);

	INIT_LIST_HEAD(&a->general_req.req_list);
	INIT_LIST_HEAD(&a->active_list);
	INIT_LIST_HEAD(&a->defer_list);
	INIT_LIST_HEAD(&a->free_sg_list_head);
	INIT_LIST_HEAD(&a->avail_request);
	INIT_LIST_HEAD(&a->vrq_mds_head);
	INIT_LIST_HEAD(&a->fw_event_list);

	first_request = (struct esas2r_request *)((u8 *)(a + 1));

	for (last_request = first_request, i = 1; i < num_requests;
	     last_request++, i++) {
		INIT_LIST_HEAD(&last_request->req_list);
		list_add_tail(&last_request->comp_list, &a->avail_request);
		if (!alloc_vda_req(a, last_request)) {
			esas2r_log(ESAS2R_LOG_CRIT,
				   "failed to allocate a VDA request!");
			esas2r_kill_adapter(index);
			return 0;
		}
	}

	esas2r_debug("requests: %p to %p (%d, %d)", first_request,
		     last_request,
		     sizeof(*first_request),
		     num_requests);

	if (esas2r_map_regions(a) != 0) {
		esas2r_log(ESAS2R_LOG_CRIT, "could not map PCI regions!");
		esas2r_kill_adapter(index);
		return 0;
	}

	a->index = index;

	/* interrupts will be disabled until we are done with init */
	atomic_inc(&a->dis_ints_cnt);
	atomic_inc(&a->disable_cnt);
	a->flags |= AF_CHPRST_PENDING
		    | AF_DISC_PENDING
		    | AF_FIRST_INIT
		    | AF_LEGACY_SGE_MODE;

	a->init_msg = ESAS2R_INIT_MSG_START;
	a->max_vdareq_size = 128;
	a->build_sgl = esas2r_build_sg_list_sge;

	esas2r_setup_interrupts(a, interrupt_mode);

	a->uncached_size = esas2r_get_uncached_size(a);
	a->uncached = dma_alloc_coherent(&pcid->dev,
					 (size_t)a->uncached_size,
					 (dma_addr_t *)&bus_addr,
					 GFP_KERNEL);
	if (a->uncached == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to allocate %d bytes of consistent memory!",
			   a->uncached_size);
		esas2r_kill_adapter(index);
		return 0;
	}

	a->uncached_phys = bus_addr;

	esas2r_debug("%d bytes uncached memory allocated @ %p (%x:%x)",
		     a->uncached_size,
		     a->uncached,
		     upper_32_bits(bus_addr),
		     lower_32_bits(bus_addr));
	memset(a->uncached, 0, a->uncached_size);
	next_uncached = a->uncached;

	if (!esas2r_init_adapter_struct(a,
					&next_uncached)) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to initialize adapter structure (2)!");
		esas2r_kill_adapter(index);
		return 0;
	}

	tasklet_init(&a->tasklet,
		     esas2r_adapter_tasklet,
		     (unsigned long)a);

	/*
	 * Disable chip interrupts to prevent spurious interrupts
	 * until we claim the IRQ.
	 */
	esas2r_disable_chip_interrupts(a);
	esas2r_check_adapter(a);

	if (!esas2r_init_adapter_hw(a, true))
		esas2r_log(ESAS2R_LOG_CRIT, "failed to initialize hardware!");
	else
		esas2r_debug("esas2r_init_adapter ok");

	esas2r_claim_interrupts(a);

	if (a->flags2 & AF2_IRQ_CLAIMED)
		esas2r_enable_chip_interrupts(a);

	esas2r_lock_set_flags(&a->flags2, AF2_INIT_DONE);
	if (!(a->flags & AF_DEGRADED_MODE))
		esas2r_kickoff_timer(a);
	esas2r_debug("esas2r_init_adapter done for %p (%d)",
		     a, a->disable_cnt);

	return 1;
}

static void esas2r_adapter_power_down(struct esas2r_adapter *a,
				      int power_management)
{
	struct esas2r_mem_desc *memdesc, *next;

	if ((a->flags2 & AF2_INIT_DONE)
	    &&  (!(a->flags & AF_DEGRADED_MODE))) {
		if (!power_management) {
			del_timer_sync(&a->timer);
			tasklet_kill(&a->tasklet);
		}
		esas2r_power_down(a);

		/*
		 * There are versions of firmware that do not handle the sync
		 * cache command correctly.  Stall here to ensure that the
		 * cache is lazily flushed.
		 */
		mdelay(500);
		esas2r_debug("chip halted");
	}

	/* Remove sysfs binary files */
	if (a->sysfs_fw_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj, &bin_attr_fw);
		a->sysfs_fw_created = 0;
	}

	if (a->sysfs_fs_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj, &bin_attr_fs);
		a->sysfs_fs_created = 0;
	}

	if (a->sysfs_vda_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj, &bin_attr_vda);
		a->sysfs_vda_created = 0;
	}

	if (a->sysfs_hw_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj, &bin_attr_hw);
		a->sysfs_hw_created = 0;
	}

	if (a->sysfs_live_nvram_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj,
				      &bin_attr_live_nvram);
		a->sysfs_live_nvram_created = 0;
	}

	if (a->sysfs_default_nvram_created) {
		sysfs_remove_bin_file(&a->host->shost_dev.kobj,
				      &bin_attr_default_nvram);
		a->sysfs_default_nvram_created = 0;
	}

	/* Clean up interrupts */
	if (a->flags2 & AF2_IRQ_CLAIMED) {
		esas2r_log_dev(ESAS2R_LOG_INFO,
			       &(a->pcid->dev),
			       "free_irq(%d) called", a->pcid->irq);

		free_irq(a->pcid->irq, a);
		esas2r_debug("IRQ released");
		esas2r_lock_clear_flags(&a->flags2, AF2_IRQ_CLAIMED);
	}

	if (a->flags2 & AF2_MSI_ENABLED) {
		pci_disable_msi(a->pcid);
		esas2r_lock_clear_flags(&a->flags2, AF2_MSI_ENABLED);
		esas2r_debug("MSI disabled");
	}

	if (a->inbound_list_md.virt_addr)
		esas2r_initmem_free(a, &a->inbound_list_md);

	if (a->outbound_list_md.virt_addr)
		esas2r_initmem_free(a, &a->outbound_list_md);

	list_for_each_entry_safe(memdesc, next, &a->free_sg_list_head,
				 next_desc) {
		esas2r_initmem_free(a, memdesc);
	}

	/* Following frees everything allocated via alloc_vda_req */
	list_for_each_entry_safe(memdesc, next, &a->vrq_mds_head, next_desc) {
		esas2r_initmem_free(a, memdesc);
		list_del(&memdesc->next_desc);
		kfree(memdesc);
	}

	kfree(a->first_ae_req);
	a->first_ae_req = NULL;

	kfree(a->sg_list_mds);
	a->sg_list_mds = NULL;

	kfree(a->req_table);
	a->req_table = NULL;

	if (a->regs) {
		esas2r_unmap_regions(a);
		a->regs = NULL;
		a->data_window = NULL;
		esas2r_debug("regions unmapped");
	}
}

/* Release/free allocated resources for specified adapters. */
void esas2r_kill_adapter(int i)
{
	struct esas2r_adapter *a = esas2r_adapters[i];

	if (a) {
		unsigned long flags;
		struct workqueue_struct *wq;
		esas2r_debug("killing adapter %p [%d] ", a, i);
		esas2r_fw_event_off(a);
		esas2r_adapter_power_down(a, 0);
		if (esas2r_buffered_ioctl &&
		    (a->pcid == esas2r_buffered_ioctl_pcid)) {
			dma_free_coherent(&a->pcid->dev,
					  (size_t)esas2r_buffered_ioctl_size,
					  esas2r_buffered_ioctl,
					  esas2r_buffered_ioctl_addr);
			esas2r_buffered_ioctl = NULL;
		}

		if (a->vda_buffer) {
			dma_free_coherent(&a->pcid->dev,
					  (size_t)VDA_MAX_BUFFER_SIZE,
					  a->vda_buffer,
					  (dma_addr_t)a->ppvda_buffer);
			a->vda_buffer = NULL;
		}
		if (a->fs_api_buffer) {
			dma_free_coherent(&a->pcid->dev,
					  (size_t)a->fs_api_buffer_size,
					  a->fs_api_buffer,
					  (dma_addr_t)a->ppfs_api_buffer);
			a->fs_api_buffer = NULL;
		}

		kfree(a->local_atto_ioctl);
		a->local_atto_ioctl = NULL;

		spin_lock_irqsave(&a->fw_event_lock, flags);
		wq = a->fw_event_q;
		a->fw_event_q = NULL;
		spin_unlock_irqrestore(&a->fw_event_lock, flags);
		if (wq)
			destroy_workqueue(wq);

		if (a->uncached) {
			dma_free_coherent(&a->pcid->dev,
					  (size_t)a->uncached_size,
					  a->uncached,
					  (dma_addr_t)a->uncached_phys);
			a->uncached = NULL;
			esas2r_debug("uncached area freed");
		}

		esas2r_log_dev(ESAS2R_LOG_INFO,
			       &(a->pcid->dev),
			       "pci_disable_device() called.  msix_enabled: %d "
			       "msi_enabled: %d irq: %d pin: %d",
			       a->pcid->msix_enabled,
			       a->pcid->msi_enabled,
			       a->pcid->irq,
			       a->pcid->pin);

		esas2r_log_dev(ESAS2R_LOG_INFO,
			       &(a->pcid->dev),
			       "before pci_disable_device() enable_cnt: %d",
			       a->pcid->enable_cnt.counter);

		pci_disable_device(a->pcid);
		esas2r_log_dev(ESAS2R_LOG_INFO,
			       &(a->pcid->dev),
			       "after pci_disable_device() enable_cnt: %d",
			       a->pcid->enable_cnt.counter);

		esas2r_log_dev(ESAS2R_LOG_INFO,
			       &(a->pcid->dev),
			       "pci_set_drv_data(%p, NULL) called",
			       a->pcid);

		pci_set_drvdata(a->pcid, NULL);
		esas2r_adapters[i] = NULL;

		if (a->flags2 & AF2_INIT_DONE) {
			esas2r_lock_clear_flags(&a->flags2,
						AF2_INIT_DONE);

			esas2r_lock_set_flags(&a->flags,
					      AF_DEGRADED_MODE);

			esas2r_log_dev(ESAS2R_LOG_INFO,
				       &(a->host->shost_gendev),
				       "scsi_remove_host() called");

			scsi_remove_host(a->host);

			esas2r_log_dev(ESAS2R_LOG_INFO,
				       &(a->host->shost_gendev),
				       "scsi_host_put() called");

			scsi_host_put(a->host);
		}
	}
}

int esas2r_cleanup(struct Scsi_Host *host)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)host->hostdata;
	int index;

	if (host == NULL) {
		int i;

		esas2r_debug("esas2r_cleanup everything");
		for (i = 0; i < MAX_ADAPTERS; i++)
			esas2r_kill_adapter(i);
		return -1;
	}

	esas2r_debug("esas2r_cleanup called for host %p", host);
	index = a->index;
	esas2r_kill_adapter(index);
	return index;
}

int esas2r_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	u32 device_state;
	struct esas2r_adapter *a = (struct esas2r_adapter *)host->hostdata;

	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev), "suspending adapter()");
	if (!a)
		return -ENODEV;

	esas2r_adapter_power_down(a, 1);
	device_state = pci_choose_state(pdev, state);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_save_state() called");
	pci_save_state(pdev);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_disable_device() called");
	pci_disable_device(pdev);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_set_power_state() called");
	pci_set_power_state(pdev, device_state);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev), "esas2r_suspend(): 0");
	return 0;
}

int esas2r_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct esas2r_adapter *a = (struct esas2r_adapter *)host->hostdata;
	int rez;

	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev), "resuming adapter()");
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_set_power_state(PCI_D0) "
		       "called");
	pci_set_power_state(pdev, PCI_D0);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_enable_wake(PCI_D0, 0) "
		       "called");
	pci_enable_wake(pdev, PCI_D0, 0);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_restore_state() called");
	pci_restore_state(pdev);
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "pci_enable_device() called");
	rez = pci_enable_device(pdev);
	pci_set_master(pdev);

	if (!a) {
		rez = -ENODEV;
		goto error_exit;
	}

	if (esas2r_map_regions(a) != 0) {
		esas2r_log(ESAS2R_LOG_CRIT, "could not re-map PCI regions!");
		rez = -ENOMEM;
		goto error_exit;
	}

	/* Set up interupt mode */
	esas2r_setup_interrupts(a, a->intr_mode);

	/*
	 * Disable chip interrupts to prevent spurious interrupts until we
	 * claim the IRQ.
	 */
	esas2r_disable_chip_interrupts(a);
	if (!esas2r_power_up(a, true)) {
		esas2r_debug("yikes, esas2r_power_up failed");
		rez = -ENOMEM;
		goto error_exit;
	}

	esas2r_claim_interrupts(a);

	if (a->flags2 & AF2_IRQ_CLAIMED) {
		/*
		 * Now that system interrupt(s) are claimed, we can enable
		 * chip interrupts.
		 */
		esas2r_enable_chip_interrupts(a);
		esas2r_kickoff_timer(a);
	} else {
		esas2r_debug("yikes, unable to claim IRQ");
		esas2r_log(ESAS2R_LOG_CRIT, "could not re-claim IRQ!");
		rez = -ENOMEM;
		goto error_exit;
	}

error_exit:
	esas2r_log_dev(ESAS2R_LOG_CRIT, &(pdev->dev), "esas2r_resume(): %d",
		       rez);
	return rez;
}

bool esas2r_set_degraded_mode(struct esas2r_adapter *a, char *error_str)
{
	esas2r_lock_set_flags(&a->flags, AF_DEGRADED_MODE);
	esas2r_log(ESAS2R_LOG_CRIT,
		   "setting adapter to degraded mode: %s\n", error_str);
	return false;
}

u32 esas2r_get_uncached_size(struct esas2r_adapter *a)
{
	return sizeof(struct esas2r_sas_nvram)
	       + ALIGN(ESAS2R_DISC_BUF_LEN, 8)
	       + ALIGN(sizeof(u32), 8) /* outbound list copy pointer */
	       + 8
	       + (num_sg_lists * (u16)sgl_page_size)
	       + ALIGN((num_requests + num_ae_requests + 1 +
			ESAS2R_LIST_EXTRA) *
		       sizeof(struct esas2r_inbound_list_source_entry),
		       8)
	       + ALIGN((num_requests + num_ae_requests + 1 +
			ESAS2R_LIST_EXTRA) *
		       sizeof(struct atto_vda_ob_rsp), 8)
	       + 256; /* VDA request and buffer align */
}

static void esas2r_init_pci_cfg_space(struct esas2r_adapter *a)
{
	int pcie_cap_reg;

	pcie_cap_reg = pci_find_capability(a->pcid, PCI_CAP_ID_EXP);
	if (0xffff && pcie_cap_reg) {
		u16 devcontrol;

		pci_read_config_word(a->pcid, pcie_cap_reg + PCI_EXP_DEVCTL,
				     &devcontrol);

		if ((devcontrol & PCI_EXP_DEVCTL_READRQ) > 0x2000) {
			esas2r_log(ESAS2R_LOG_INFO,
				   "max read request size > 512B");

			devcontrol &= ~PCI_EXP_DEVCTL_READRQ;
			devcontrol |= 0x2000;
			pci_write_config_word(a->pcid,
					      pcie_cap_reg + PCI_EXP_DEVCTL,
					      devcontrol);
		}
	}
}

/*
 * Determine the organization of the uncached data area and
 * finish initializing the adapter structure
 */
bool esas2r_init_adapter_struct(struct esas2r_adapter *a,
				void **uncached_area)
{
	u32 i;
	u8 *high;
	struct esas2r_inbound_list_source_entry *element;
	struct esas2r_request *rq;
	struct esas2r_mem_desc *sgl;

	spin_lock_init(&a->sg_list_lock);
	spin_lock_init(&a->mem_lock);
	spin_lock_init(&a->queue_lock);

	a->targetdb_end = &a->targetdb[ESAS2R_MAX_TARGETS];

	if (!alloc_vda_req(a, &a->general_req)) {
		esas2r_hdebug(
			"failed to allocate a VDA request for the general req!");
		return false;
	}

	/* allocate requests for asynchronous events */
	a->first_ae_req =
		kzalloc(num_ae_requests * sizeof(struct esas2r_request),
			GFP_KERNEL);

	if (a->first_ae_req == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to allocate memory for asynchronous events");
		return false;
	}

	/* allocate the S/G list memory descriptors */
	a->sg_list_mds = kzalloc(
		num_sg_lists * sizeof(struct esas2r_mem_desc), GFP_KERNEL);

	if (a->sg_list_mds == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to allocate memory for s/g list descriptors");
		return false;
	}

	/* allocate the request table */
	a->req_table =
		kzalloc((num_requests + num_ae_requests +
			 1) * sizeof(struct esas2r_request *), GFP_KERNEL);

	if (a->req_table == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "failed to allocate memory for the request table");
		return false;
	}

	/* initialize PCI configuration space */
	esas2r_init_pci_cfg_space(a);

	/*
	 * the thunder_stream boards all have a serial flash part that has a
	 * different base address on the AHB bus.
	 */
	if ((a->pcid->subsystem_vendor == ATTO_VENDOR_ID)
	    && (a->pcid->subsystem_device & ATTO_SSDID_TBT))
		a->flags2 |= AF2_THUNDERBOLT;

	if (a->flags2 & AF2_THUNDERBOLT)
		a->flags2 |= AF2_SERIAL_FLASH;

	if (a->pcid->subsystem_device == ATTO_TLSH_1068)
		a->flags2 |= AF2_THUNDERLINK;

	/* Uncached Area */
	high = (u8 *)*uncached_area;

	/* initialize the scatter/gather table pages */

	for (i = 0, sgl = a->sg_list_mds; i < num_sg_lists; i++, sgl++) {
		sgl->size = sgl_page_size;

		list_add_tail(&sgl->next_desc, &a->free_sg_list_head);

		if (!esas2r_initmem_alloc(a, sgl, ESAS2R_SGL_ALIGN)) {
			/* Allow the driver to load if the minimum count met. */
			if (i < NUM_SGL_MIN)
				return false;
			break;
		}
	}

	/* compute the size of the lists */
	a->list_size = num_requests + ESAS2R_LIST_EXTRA;

	/* allocate the inbound list */
	a->inbound_list_md.size = a->list_size *
				  sizeof(struct
					 esas2r_inbound_list_source_entry);

	if (!esas2r_initmem_alloc(a, &a->inbound_list_md, ESAS2R_LIST_ALIGN)) {
		esas2r_hdebug("failed to allocate IB list");
		return false;
	}

	/* allocate the outbound list */
	a->outbound_list_md.size = a->list_size *
				   sizeof(struct atto_vda_ob_rsp);

	if (!esas2r_initmem_alloc(a, &a->outbound_list_md,
				  ESAS2R_LIST_ALIGN)) {
		esas2r_hdebug("failed to allocate IB list");
		return false;
	}

	/* allocate the NVRAM structure */
	a->nvram = (struct esas2r_sas_nvram *)high;
	high += sizeof(struct esas2r_sas_nvram);

	/* allocate the discovery buffer */
	a->disc_buffer = high;
	high += ESAS2R_DISC_BUF_LEN;
	high = PTR_ALIGN(high, 8);

	/* allocate the outbound list copy pointer */
	a->outbound_copy = (u32 volatile *)high;
	high += sizeof(u32);

	if (!(a->flags & AF_NVR_VALID))
		esas2r_nvram_set_defaults(a);

	/* update the caller's uncached memory area pointer */
	*uncached_area = (void *)high;

	/* initialize the allocated memory */
	if (a->flags & AF_FIRST_INIT) {
		memset(a->req_table, 0,
		       (num_requests + num_ae_requests +
			1) * sizeof(struct esas2r_request *));

		esas2r_targ_db_initialize(a);

		/* prime parts of the inbound list */
		element =
			(struct esas2r_inbound_list_source_entry *)a->
			inbound_list_md.
			virt_addr;

		for (i = 0; i < a->list_size; i++) {
			element->address = 0;
			element->reserved = 0;
			element->length = cpu_to_le32(HWILSE_INTERFACE_F0
						      | (sizeof(union
								atto_vda_req)
							 /
							 sizeof(u32)));
			element++;
		}

		/* init the AE requests */
		for (rq = a->first_ae_req, i = 0; i < num_ae_requests; rq++,
		     i++) {
			INIT_LIST_HEAD(&rq->req_list);
			if (!alloc_vda_req(a, rq)) {
				esas2r_hdebug(
					"failed to allocate a VDA request!");
				return false;
			}

			esas2r_rq_init_request(rq, a);

			/* override the completion function */
			rq->comp_cb = esas2r_ae_complete;
		}
	}

	return true;
}

/* This code will verify that the chip is operational. */
bool esas2r_check_adapter(struct esas2r_adapter *a)
{
	u32 starttime;
	u32 doorbell;
	u64 ppaddr;
	u32 dw;

	/*
	 * if the chip reset detected flag is set, we can bypass a bunch of
	 * stuff.
	 */
	if (a->flags & AF_CHPRST_DETECTED)
		goto skip_chip_reset;

	/*
	 * BEFORE WE DO ANYTHING, disable the chip interrupts!  the boot driver
	 * may have left them enabled or we may be recovering from a fault.
	 */
	esas2r_write_register_dword(a, MU_INT_MASK_OUT, ESAS2R_INT_DIS_MASK);
	esas2r_flush_register_dword(a, MU_INT_MASK_OUT);

	/*
	 * wait for the firmware to become ready by forcing an interrupt and
	 * waiting for a response.
	 */
	starttime = jiffies_to_msecs(jiffies);

	while (true) {
		esas2r_force_interrupt(a);
		doorbell = esas2r_read_register_dword(a, MU_DOORBELL_OUT);
		if (doorbell == 0xFFFFFFFF) {
			/*
			 * Give the firmware up to two seconds to enable
			 * register access after a reset.
			 */
			if ((jiffies_to_msecs(jiffies) - starttime) > 2000)
				return esas2r_set_degraded_mode(a,
								"unable to access registers");
		} else if (doorbell & DRBL_FORCE_INT) {
			u32 ver = (doorbell & DRBL_FW_VER_MSK);

			/*
			 * This driver supports version 0 and version 1 of
			 * the API
			 */
			esas2r_write_register_dword(a, MU_DOORBELL_OUT,
						    doorbell);

			if (ver == DRBL_FW_VER_0) {
				esas2r_lock_set_flags(&a->flags,
						      AF_LEGACY_SGE_MODE);

				a->max_vdareq_size = 128;
				a->build_sgl = esas2r_build_sg_list_sge;
			} else if (ver == DRBL_FW_VER_1) {
				esas2r_lock_clear_flags(&a->flags,
							AF_LEGACY_SGE_MODE);

				a->max_vdareq_size = 1024;
				a->build_sgl = esas2r_build_sg_list_prd;
			} else {
				return esas2r_set_degraded_mode(a,
								"unknown firmware version");
			}
			break;
		}

		schedule_timeout_interruptible(msecs_to_jiffies(100));

		if ((jiffies_to_msecs(jiffies) - starttime) > 180000) {
			esas2r_hdebug("FW ready TMO");
			esas2r_bugon();

			return esas2r_set_degraded_mode(a,
							"firmware start has timed out");
		}
	}

	/* purge any asynchronous events since we will repost them later */
	esas2r_write_register_dword(a, MU_DOORBELL_IN, DRBL_MSG_IFC_DOWN);
	starttime = jiffies_to_msecs(jiffies);

	while (true) {
		doorbell = esas2r_read_register_dword(a, MU_DOORBELL_OUT);
		if (doorbell & DRBL_MSG_IFC_DOWN) {
			esas2r_write_register_dword(a, MU_DOORBELL_OUT,
						    doorbell);
			break;
		}

		schedule_timeout_interruptible(msecs_to_jiffies(50));

		if ((jiffies_to_msecs(jiffies) - starttime) > 3000) {
			esas2r_hdebug("timeout waiting for interface down");
			break;
		}
	}
skip_chip_reset:
	/*
	 * first things first, before we go changing any of these registers
	 * disable the communication lists.
	 */
	dw = esas2r_read_register_dword(a, MU_IN_LIST_CONFIG);
	dw &= ~MU_ILC_ENABLE;
	esas2r_write_register_dword(a, MU_IN_LIST_CONFIG, dw);
	dw = esas2r_read_register_dword(a, MU_OUT_LIST_CONFIG);
	dw &= ~MU_OLC_ENABLE;
	esas2r_write_register_dword(a, MU_OUT_LIST_CONFIG, dw);

	/* configure the communication list addresses */
	ppaddr = a->inbound_list_md.phys_addr;
	esas2r_write_register_dword(a, MU_IN_LIST_ADDR_LO,
				    lower_32_bits(ppaddr));
	esas2r_write_register_dword(a, MU_IN_LIST_ADDR_HI,
				    upper_32_bits(ppaddr));
	ppaddr = a->outbound_list_md.phys_addr;
	esas2r_write_register_dword(a, MU_OUT_LIST_ADDR_LO,
				    lower_32_bits(ppaddr));
	esas2r_write_register_dword(a, MU_OUT_LIST_ADDR_HI,
				    upper_32_bits(ppaddr));
	ppaddr = a->uncached_phys +
		 ((u8 *)a->outbound_copy - a->uncached);
	esas2r_write_register_dword(a, MU_OUT_LIST_COPY_PTR_LO,
				    lower_32_bits(ppaddr));
	esas2r_write_register_dword(a, MU_OUT_LIST_COPY_PTR_HI,
				    upper_32_bits(ppaddr));

	/* reset the read and write pointers */
	*a->outbound_copy =
		a->last_write =
			a->last_read = a->list_size - 1;
	esas2r_lock_set_flags(&a->flags, AF_COMM_LIST_TOGGLE);
	esas2r_write_register_dword(a, MU_IN_LIST_WRITE, MU_ILW_TOGGLE |
				    a->last_write);
	esas2r_write_register_dword(a, MU_OUT_LIST_COPY, MU_OLC_TOGGLE |
				    a->last_write);
	esas2r_write_register_dword(a, MU_IN_LIST_READ, MU_ILR_TOGGLE |
				    a->last_write);
	esas2r_write_register_dword(a, MU_OUT_LIST_WRITE,
				    MU_OLW_TOGGLE | a->last_write);

	/* configure the interface select fields */
	dw = esas2r_read_register_dword(a, MU_IN_LIST_IFC_CONFIG);
	dw &= ~(MU_ILIC_LIST | MU_ILIC_DEST);
	esas2r_write_register_dword(a, MU_IN_LIST_IFC_CONFIG,
				    (dw | MU_ILIC_LIST_F0 | MU_ILIC_DEST_DDR));
	dw = esas2r_read_register_dword(a, MU_OUT_LIST_IFC_CONFIG);
	dw &= ~(MU_OLIC_LIST | MU_OLIC_SOURCE);
	esas2r_write_register_dword(a, MU_OUT_LIST_IFC_CONFIG,
				    (dw | MU_OLIC_LIST_F0 |
				     MU_OLIC_SOURCE_DDR));

	/* finish configuring the communication lists */
	dw = esas2r_read_register_dword(a, MU_IN_LIST_CONFIG);
	dw &= ~(MU_ILC_ENTRY_MASK | MU_ILC_NUMBER_MASK);
	dw |= MU_ILC_ENTRY_4_DW | MU_ILC_DYNAMIC_SRC
	      | (a->list_size << MU_ILC_NUMBER_SHIFT);
	esas2r_write_register_dword(a, MU_IN_LIST_CONFIG, dw);
	dw = esas2r_read_register_dword(a, MU_OUT_LIST_CONFIG);
	dw &= ~(MU_OLC_ENTRY_MASK | MU_OLC_NUMBER_MASK);
	dw |= MU_OLC_ENTRY_4_DW | (a->list_size << MU_OLC_NUMBER_SHIFT);
	esas2r_write_register_dword(a, MU_OUT_LIST_CONFIG, dw);

	/*
	 * notify the firmware that we're done setting up the communication
	 * list registers.  wait here until the firmware is done configuring
	 * its lists.  it will signal that it is done by enabling the lists.
	 */
	esas2r_write_register_dword(a, MU_DOORBELL_IN, DRBL_MSG_IFC_INIT);
	starttime = jiffies_to_msecs(jiffies);

	while (true) {
		doorbell = esas2r_read_register_dword(a, MU_DOORBELL_OUT);
		if (doorbell & DRBL_MSG_IFC_INIT) {
			esas2r_write_register_dword(a, MU_DOORBELL_OUT,
						    doorbell);
			break;
		}

		schedule_timeout_interruptible(msecs_to_jiffies(100));

		if ((jiffies_to_msecs(jiffies) - starttime) > 3000) {
			esas2r_hdebug(
				"timeout waiting for communication list init");
			esas2r_bugon();
			return esas2r_set_degraded_mode(a,
							"timeout waiting for communication list init");
		}
	}

	/*
	 * flag whether the firmware supports the power down doorbell.  we
	 * determine this by reading the inbound doorbell enable mask.
	 */
	doorbell = esas2r_read_register_dword(a, MU_DOORBELL_IN_ENB);
	if (doorbell & DRBL_POWER_DOWN)
		esas2r_lock_set_flags(&a->flags2, AF2_VDA_POWER_DOWN);
	else
		esas2r_lock_clear_flags(&a->flags2, AF2_VDA_POWER_DOWN);

	/*
	 * enable assertion of outbound queue and doorbell interrupts in the
	 * main interrupt cause register.
	 */
	esas2r_write_register_dword(a, MU_OUT_LIST_INT_MASK, MU_OLIS_MASK);
	esas2r_write_register_dword(a, MU_DOORBELL_OUT_ENB, DRBL_ENB_MASK);
	return true;
}

/* Process the initialization message just completed and format the next one. */
static bool esas2r_format_init_msg(struct esas2r_adapter *a,
				   struct esas2r_request *rq)
{
	u32 msg = a->init_msg;
	struct atto_vda_cfg_init *ci;

	a->init_msg = 0;

	switch (msg) {
	case ESAS2R_INIT_MSG_START:
	case ESAS2R_INIT_MSG_REINIT:
	{
		struct timeval now;
		do_gettimeofday(&now);
		esas2r_hdebug("CFG init");
		esas2r_build_cfg_req(a,
				     rq,
				     VDA_CFG_INIT,
				     0,
				     NULL);
		ci = (struct atto_vda_cfg_init *)&rq->vrq->cfg.data.init;
		ci->sgl_page_size = sgl_page_size;
		ci->epoch_time = now.tv_sec;
		rq->flags |= RF_FAILURE_OK;
		a->init_msg = ESAS2R_INIT_MSG_INIT;
		break;
	}

	case ESAS2R_INIT_MSG_INIT:
		if (rq->req_stat == RS_SUCCESS) {
			u32 major;
			u32 minor;

			a->fw_version = le16_to_cpu(
				rq->func_rsp.cfg_rsp.vda_version);
			a->fw_build = rq->func_rsp.cfg_rsp.fw_build;
			major = LOBYTE(rq->func_rsp.cfg_rsp.fw_release);
			minor = HIBYTE(rq->func_rsp.cfg_rsp.fw_release);
			a->fw_version += (major << 16) + (minor << 24);
		} else {
			esas2r_hdebug("FAILED");
		}

		/*
		 * the 2.71 and earlier releases of R6xx firmware did not error
		 * unsupported config requests correctly.
		 */

		if ((a->flags2 & AF2_THUNDERBOLT)
		    || (be32_to_cpu(a->fw_version) >
			be32_to_cpu(0x47020052))) {
			esas2r_hdebug("CFG get init");
			esas2r_build_cfg_req(a,
					     rq,
					     VDA_CFG_GET_INIT2,
					     sizeof(struct atto_vda_cfg_init),
					     NULL);

			rq->vrq->cfg.sg_list_offset = offsetof(
				struct atto_vda_cfg_req,
				data.sge);
			rq->vrq->cfg.data.prde.ctl_len =
				cpu_to_le32(sizeof(struct atto_vda_cfg_init));
			rq->vrq->cfg.data.prde.address = cpu_to_le64(
				rq->vrq_md->phys_addr +
				sizeof(union atto_vda_req));
			rq->flags |= RF_FAILURE_OK;
			a->init_msg = ESAS2R_INIT_MSG_GET_INIT;
			break;
		}

	case ESAS2R_INIT_MSG_GET_INIT:
		if (msg == ESAS2R_INIT_MSG_GET_INIT) {
			ci = (struct atto_vda_cfg_init *)rq->data_buf;
			if (rq->req_stat == RS_SUCCESS) {
				a->num_targets_backend =
					le32_to_cpu(ci->num_targets_backend);
				a->ioctl_tunnel =
					le32_to_cpu(ci->ioctl_tunnel);
			} else {
				esas2r_hdebug("FAILED");
			}
		}
	/* fall through */

	default:
		rq->req_stat = RS_SUCCESS;
		return false;
	}
	return true;
}

/*
 * Perform initialization messages via the request queue.  Messages are
 * performed with interrupts disabled.
 */
bool esas2r_init_msgs(struct esas2r_adapter *a)
{
	bool success = true;
	struct esas2r_request *rq = &a->general_req;

	esas2r_rq_init_request(rq, a);
	rq->comp_cb = esas2r_dummy_complete;

	if (a->init_msg == 0)
		a->init_msg = ESAS2R_INIT_MSG_REINIT;

	while (a->init_msg) {
		if (esas2r_format_init_msg(a, rq)) {
			unsigned long flags;
			while (true) {
				spin_lock_irqsave(&a->queue_lock, flags);
				esas2r_start_vda_request(a, rq);
				spin_unlock_irqrestore(&a->queue_lock, flags);
				esas2r_wait_request(a, rq);
				if (rq->req_stat != RS_PENDING)
					break;
			}
		}

		if (rq->req_stat == RS_SUCCESS
		    || ((rq->flags & RF_FAILURE_OK)
			&& rq->req_stat != RS_TIMEOUT))
			continue;

		esas2r_log(ESAS2R_LOG_CRIT, "init message %x failed (%x, %x)",
			   a->init_msg, rq->req_stat, rq->flags);
		a->init_msg = ESAS2R_INIT_MSG_START;
		success = false;
		break;
	}

	esas2r_rq_destroy_request(rq, a);
	return success;
}

/* Initialize the adapter chip */
bool esas2r_init_adapter_hw(struct esas2r_adapter *a, bool init_poll)
{
	bool rslt = false;
	struct esas2r_request *rq;
	u32 i;

	if (a->flags & AF_DEGRADED_MODE)
		goto exit;

	if (!(a->flags & AF_NVR_VALID)) {
		if (!esas2r_nvram_read_direct(a))
			esas2r_log(ESAS2R_LOG_WARN,
				   "invalid/missing NVRAM parameters");
	}

	if (!esas2r_init_msgs(a)) {
		esas2r_set_degraded_mode(a, "init messages failed");
		goto exit;
	}

	/* The firmware is ready. */
	esas2r_lock_clear_flags(&a->flags, AF_DEGRADED_MODE);
	esas2r_lock_clear_flags(&a->flags, AF_CHPRST_PENDING);

	/* Post all the async event requests */
	for (i = 0, rq = a->first_ae_req; i < num_ae_requests; i++, rq++)
		esas2r_start_ae_request(a, rq);

	if (!a->flash_rev[0])
		esas2r_read_flash_rev(a);

	if (!a->image_type[0])
		esas2r_read_image_type(a);

	if (a->fw_version == 0)
		a->fw_rev[0] = 0;
	else
		sprintf(a->fw_rev, "%1d.%02d",
			(int)LOBYTE(HIWORD(a->fw_version)),
			(int)HIBYTE(HIWORD(a->fw_version)));

	esas2r_hdebug("firmware revision: %s", a->fw_rev);

	if ((a->flags & AF_CHPRST_DETECTED)
	    && (a->flags & AF_FIRST_INIT)) {
		esas2r_enable_chip_interrupts(a);
		return true;
	}

	/* initialize discovery */
	esas2r_disc_initialize(a);

	/*
	 * wait for the device wait time to expire here if requested.  this is
	 * usually requested during initial driver load and possibly when
	 * resuming from a low power state.  deferred device waiting will use
	 * interrupts.  chip reset recovery always defers device waiting to
	 * avoid being in a TASKLET too long.
	 */
	if (init_poll) {
		u32 currtime = a->disc_start_time;
		u32 nexttick = 100;
		u32 deltatime;

		/*
		 * Block Tasklets from getting scheduled and indicate this is
		 * polled discovery.
		 */
		esas2r_lock_set_flags(&a->flags, AF_TASKLET_SCHEDULED);
		esas2r_lock_set_flags(&a->flags, AF_DISC_POLLED);

		/*
		 * Temporarily bring the disable count to zero to enable
		 * deferred processing.  Note that the count is already zero
		 * after the first initialization.
		 */
		if (a->flags & AF_FIRST_INIT)
			atomic_dec(&a->disable_cnt);

		while (a->flags & AF_DISC_PENDING) {
			schedule_timeout_interruptible(msecs_to_jiffies(100));

			/*
			 * Determine the need for a timer tick based on the
			 * delta time between this and the last iteration of
			 * this loop.  We don't use the absolute time because
			 * then we would have to worry about when nexttick
			 * wraps and currtime hasn't yet.
			 */
			deltatime = jiffies_to_msecs(jiffies) - currtime;
			currtime += deltatime;

			/*
			 * Process any waiting discovery as long as the chip is
			 * up.  If a chip reset happens during initial polling,
			 * we have to make sure the timer tick processes the
			 * doorbell indicating the firmware is ready.
			 */
			if (!(a->flags & AF_CHPRST_PENDING))
				esas2r_disc_check_for_work(a);

			/* Simulate a timer tick. */
			if (nexttick <= deltatime) {

				/* Time for a timer tick */
				nexttick += 100;
				esas2r_timer_tick(a);
			}

			if (nexttick > deltatime)
				nexttick -= deltatime;

			/* Do any deferred processing */
			if (esas2r_is_tasklet_pending(a))
				esas2r_do_tasklet_tasks(a);

		}

		if (a->flags & AF_FIRST_INIT)
			atomic_inc(&a->disable_cnt);

		esas2r_lock_clear_flags(&a->flags, AF_DISC_POLLED);
		esas2r_lock_clear_flags(&a->flags, AF_TASKLET_SCHEDULED);
	}


	esas2r_targ_db_report_changes(a);

	/*
	 * For cases where (a) the initialization messages processing may
	 * handle an interrupt for a port event and a discovery is waiting, but
	 * we are not waiting for devices, or (b) the device wait time has been
	 * exhausted but there is still discovery pending, start any leftover
	 * discovery in interrupt driven mode.
	 */
	esas2r_disc_start_waiting(a);

	/* Enable chip interrupts */
	a->int_mask = ESAS2R_INT_STS_MASK;
	esas2r_enable_chip_interrupts(a);
	esas2r_enable_heartbeat(a);
	rslt = true;

exit:
	/*
	 * Regardless of whether initialization was successful, certain things
	 * need to get done before we exit.
	 */

	if ((a->flags & AF_CHPRST_DETECTED)
	    && (a->flags & AF_FIRST_INIT)) {
		/*
		 * Reinitialization was performed during the first
		 * initialization.  Only clear the chip reset flag so the
		 * original device polling is not cancelled.
		 */
		if (!rslt)
			esas2r_lock_clear_flags(&a->flags, AF_CHPRST_PENDING);
	} else {
		/* First initialization or a subsequent re-init is complete. */
		if (!rslt) {
			esas2r_lock_clear_flags(&a->flags, AF_CHPRST_PENDING);
			esas2r_lock_clear_flags(&a->flags, AF_DISC_PENDING);
		}


		/* Enable deferred processing after the first initialization. */
		if (a->flags & AF_FIRST_INIT) {
			esas2r_lock_clear_flags(&a->flags, AF_FIRST_INIT);

			if (atomic_dec_return(&a->disable_cnt) == 0)
				esas2r_do_deferred_processes(a);
		}
	}

	return rslt;
}

void esas2r_reset_adapter(struct esas2r_adapter *a)
{
	esas2r_lock_set_flags(&a->flags, AF_OS_RESET);
	esas2r_local_reset_adapter(a);
	esas2r_schedule_tasklet(a);
}

void esas2r_reset_chip(struct esas2r_adapter *a)
{
	if (!esas2r_is_adapter_present(a))
		return;

	/*
	 * Before we reset the chip, save off the VDA core dump.  The VDA core
	 * dump is located in the upper 512KB of the onchip SRAM.  Make sure
	 * to not overwrite a previous crash that was saved.
	 */
	if ((a->flags2 & AF2_COREDUMP_AVAIL)
	    && !(a->flags2 & AF2_COREDUMP_SAVED)
	    && a->fw_coredump_buff) {
		esas2r_read_mem_block(a,
				      a->fw_coredump_buff,
				      MW_DATA_ADDR_SRAM + 0x80000,
				      ESAS2R_FWCOREDUMP_SZ);

		esas2r_lock_set_flags(&a->flags2, AF2_COREDUMP_SAVED);
	}

	esas2r_lock_clear_flags(&a->flags2, AF2_COREDUMP_AVAIL);

	/* Reset the chip */
	if (a->pcid->revision == MVR_FREY_B2)
		esas2r_write_register_dword(a, MU_CTL_STATUS_IN_B2,
					    MU_CTL_IN_FULL_RST2);
	else
		esas2r_write_register_dword(a, MU_CTL_STATUS_IN,
					    MU_CTL_IN_FULL_RST);


	/* Stall a little while to let the reset condition clear */
	mdelay(10);
}

static void esas2r_power_down_notify_firmware(struct esas2r_adapter *a)
{
	u32 starttime;
	u32 doorbell;

	esas2r_write_register_dword(a, MU_DOORBELL_IN, DRBL_POWER_DOWN);
	starttime = jiffies_to_msecs(jiffies);

	while (true) {
		doorbell = esas2r_read_register_dword(a, MU_DOORBELL_OUT);
		if (doorbell & DRBL_POWER_DOWN) {
			esas2r_write_register_dword(a, MU_DOORBELL_OUT,
						    doorbell);
			break;
		}

		schedule_timeout_interruptible(msecs_to_jiffies(100));

		if ((jiffies_to_msecs(jiffies) - starttime) > 30000) {
			esas2r_hdebug("Timeout waiting for power down");
			break;
		}
	}
}

/*
 * Perform power management processing including managing device states, adapter
 * states, interrupts, and I/O.
 */
void esas2r_power_down(struct esas2r_adapter *a)
{
	esas2r_lock_set_flags(&a->flags, AF_POWER_MGT);
	esas2r_lock_set_flags(&a->flags, AF_POWER_DOWN);

	if (!(a->flags & AF_DEGRADED_MODE)) {
		u32 starttime;
		u32 doorbell;

		/*
		 * We are currently running OK and will be reinitializing later.
		 * increment the disable count to coordinate with
		 * esas2r_init_adapter.  We don't have to do this in degraded
		 * mode since we never enabled interrupts in the first place.
		 */
		esas2r_disable_chip_interrupts(a);
		esas2r_disable_heartbeat(a);

		/* wait for any VDA activity to clear before continuing */
		esas2r_write_register_dword(a, MU_DOORBELL_IN,
					    DRBL_MSG_IFC_DOWN);
		starttime = jiffies_to_msecs(jiffies);

		while (true) {
			doorbell =
				esas2r_read_register_dword(a, MU_DOORBELL_OUT);
			if (doorbell & DRBL_MSG_IFC_DOWN) {
				esas2r_write_register_dword(a, MU_DOORBELL_OUT,
							    doorbell);
				break;
			}

			schedule_timeout_interruptible(msecs_to_jiffies(100));

			if ((jiffies_to_msecs(jiffies) - starttime) > 3000) {
				esas2r_hdebug(
					"timeout waiting for interface down");
				break;
			}
		}

		/*
		 * For versions of firmware that support it tell them the driver
		 * is powering down.
		 */
		if (a->flags2 & AF2_VDA_POWER_DOWN)
			esas2r_power_down_notify_firmware(a);
	}

	/* Suspend I/O processing. */
	esas2r_lock_set_flags(&a->flags, AF_OS_RESET);
	esas2r_lock_set_flags(&a->flags, AF_DISC_PENDING);
	esas2r_lock_set_flags(&a->flags, AF_CHPRST_PENDING);

	esas2r_process_adapter_reset(a);

	/* Remove devices now that I/O is cleaned up. */
	a->prev_dev_cnt = esas2r_targ_db_get_tgt_cnt(a);
	esas2r_targ_db_remove_all(a, false);
}

/*
 * Perform power management processing including managing device states, adapter
 * states, interrupts, and I/O.
 */
bool esas2r_power_up(struct esas2r_adapter *a, bool init_poll)
{
	bool ret;

	esas2r_lock_clear_flags(&a->flags, AF_POWER_DOWN);
	esas2r_init_pci_cfg_space(a);
	esas2r_lock_set_flags(&a->flags, AF_FIRST_INIT);
	atomic_inc(&a->disable_cnt);

	/* reinitialize the adapter */
	ret = esas2r_check_adapter(a);
	if (!esas2r_init_adapter_hw(a, init_poll))
		ret = false;

	/* send the reset asynchronous event */
	esas2r_send_reset_ae(a, true);

	/* clear this flag after initialization. */
	esas2r_lock_clear_flags(&a->flags, AF_POWER_MGT);
	return ret;
}

bool esas2r_is_adapter_present(struct esas2r_adapter *a)
{
	if (a->flags & AF_NOT_PRESENT)
		return false;

	if (esas2r_read_register_dword(a, MU_DOORBELL_OUT) == 0xFFFFFFFF) {
		esas2r_lock_set_flags(&a->flags, AF_NOT_PRESENT);

		return false;
	}
	return true;
}

const char *esas2r_get_model_name(struct esas2r_adapter *a)
{
	switch (a->pcid->subsystem_device) {
	case ATTO_ESAS_R680:
		return "ATTO ExpressSAS R680";

	case ATTO_ESAS_R608:
		return "ATTO ExpressSAS R608";

	case ATTO_ESAS_R60F:
		return "ATTO ExpressSAS R60F";

	case ATTO_ESAS_R6F0:
		return "ATTO ExpressSAS R6F0";

	case ATTO_ESAS_R644:
		return "ATTO ExpressSAS R644";

	case ATTO_ESAS_R648:
		return "ATTO ExpressSAS R648";

	case ATTO_TSSC_3808:
		return "ATTO ThunderStream SC 3808D";

	case ATTO_TSSC_3808E:
		return "ATTO ThunderStream SC 3808E";

	case ATTO_TLSH_1068:
		return "ATTO ThunderLink SH 1068";
	}

	return "ATTO SAS Controller";
}

const char *esas2r_get_model_name_short(struct esas2r_adapter *a)
{
	switch (a->pcid->subsystem_device) {
	case ATTO_ESAS_R680:
		return "R680";

	case ATTO_ESAS_R608:
		return "R608";

	case ATTO_ESAS_R60F:
		return "R60F";

	case ATTO_ESAS_R6F0:
		return "R6F0";

	case ATTO_ESAS_R644:
		return "R644";

	case ATTO_ESAS_R648:
		return "R648";

	case ATTO_TSSC_3808:
		return "SC 3808D";

	case ATTO_TSSC_3808E:
		return "SC 3808E";

	case ATTO_TLSH_1068:
		return "SH 1068";
	}

	return "unknown";
}

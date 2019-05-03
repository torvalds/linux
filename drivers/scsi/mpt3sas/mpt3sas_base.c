/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.c
 * Copyright (C) 2012-2014  LSI Corporation
 * Copyright (C) 2013-2014 Avago Technologies
 *  (mailto: MPT-FusionLinux.pdl@avagotech.com)
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

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <asm/page.h>        /* To get host page size per arch */
#include <linux/aer.h>


#include "mpt3sas_base.h"

static MPT_CALLBACK	mpt_callbacks[MPT_MAX_CALLBACKS];


#define FAULT_POLLING_INTERVAL 1000 /* in milliseconds */

 /* maximum controller queue depth */
#define MAX_HBA_QUEUE_DEPTH	30000
#define MAX_CHAIN_DEPTH		100000
static int max_queue_depth = -1;
module_param(max_queue_depth, int, 0);
MODULE_PARM_DESC(max_queue_depth, " max controller queue depth ");

static int max_sgl_entries = -1;
module_param(max_sgl_entries, int, 0);
MODULE_PARM_DESC(max_sgl_entries, " max sg entries ");

static int msix_disable = -1;
module_param(msix_disable, int, 0);
MODULE_PARM_DESC(msix_disable, " disable msix routed interrupts (default=0)");

static int smp_affinity_enable = 1;
module_param(smp_affinity_enable, int, S_IRUGO);
MODULE_PARM_DESC(smp_affinity_enable, "SMP affinity feature enable/disable Default: enable(1)");

static int max_msix_vectors = -1;
module_param(max_msix_vectors, int, 0);
MODULE_PARM_DESC(max_msix_vectors,
	" max msix vectors");

static int mpt3sas_fwfault_debug;
MODULE_PARM_DESC(mpt3sas_fwfault_debug,
	" enable detection of firmware fault and halt firmware - (default=0)");

static int
_base_get_ioc_facts(struct MPT3SAS_ADAPTER *ioc);

/**
 * mpt3sas_base_check_cmd_timeout - Function
 *		to check timeout and command termination due
 *		to Host reset.
 *
 * @ioc:	per adapter object.
 * @status:	Status of issued command.
 * @mpi_request:mf request pointer.
 * @sz:		size of buffer.
 *
 * @Returns - 1/0 Reset to be done or Not
 */
u8
mpt3sas_base_check_cmd_timeout(struct MPT3SAS_ADAPTER *ioc,
		u8 status, void *mpi_request, int sz)
{
	u8 issue_reset = 0;

	if (!(status & MPT3_CMD_RESET))
		issue_reset = 1;

	ioc_err(ioc, "Command %s\n",
		issue_reset == 0 ? "terminated due to Host Reset" : "Timeout");
	_debug_dump_mf(mpi_request, sz);

	return issue_reset;
}

/**
 * _scsih_set_fwfault_debug - global setting of ioc->fwfault_debug.
 * @val: ?
 * @kp: ?
 *
 * Return: ?
 */
static int
_scsih_set_fwfault_debug(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT3SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	/* global ioc spinlock to protect controller list on list operations */
	pr_info("setting fwfault_debug(%d)\n", mpt3sas_fwfault_debug);
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list)
		ioc->fwfault_debug = mpt3sas_fwfault_debug;
	spin_unlock(&gioc_lock);
	return 0;
}
module_param_call(mpt3sas_fwfault_debug, _scsih_set_fwfault_debug,
	param_get_int, &mpt3sas_fwfault_debug, 0644);

/**
 * _base_readl_aero - retry readl for max three times.
 * @addr - MPT Fusion system interface register address
 *
 * Retry the readl() for max three times if it gets zero value
 * while reading the system interface register.
 */
static inline u32
_base_readl_aero(const volatile void __iomem *addr)
{
	u32 i = 0, ret_val;

	do {
		ret_val = readl(addr);
		i++;
	} while (ret_val == 0 && i < 3);

	return ret_val;
}

static inline u32
_base_readl(const volatile void __iomem *addr)
{
	return readl(addr);
}

/**
 * _base_clone_reply_to_sys_mem - copies reply to reply free iomem
 *				  in BAR0 space.
 *
 * @ioc: per adapter object
 * @reply: reply message frame(lower 32bit addr)
 * @index: System request message index.
 */
static void
_base_clone_reply_to_sys_mem(struct MPT3SAS_ADAPTER *ioc, u32 reply,
		u32 index)
{
	/*
	 * 256 is offset within sys register.
	 * 256 offset MPI frame starts. Max MPI frame supported is 32.
	 * 32 * 128 = 4K. From here, Clone of reply free for mcpu starts
	 */
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	void __iomem *reply_free_iomem = (void __iomem *)ioc->chip +
			MPI_FRAME_START_OFFSET +
			(cmd_credit * ioc->request_sz) + (index * sizeof(u32));

	writel(reply, reply_free_iomem);
}

/**
 * _base_clone_mpi_to_sys_mem - Writes/copies MPI frames
 *				to system/BAR0 region.
 *
 * @dst_iomem: Pointer to the destination location in BAR0 space.
 * @src: Pointer to the Source data.
 * @size: Size of data to be copied.
 */
static void
_base_clone_mpi_to_sys_mem(void *dst_iomem, void *src, u32 size)
{
	int i;
	u32 *src_virt_mem = (u32 *)src;

	for (i = 0; i < size/4; i++)
		writel((u32)src_virt_mem[i],
				(void __iomem *)dst_iomem + (i * 4));
}

/**
 * _base_clone_to_sys_mem - Writes/copies data to system/BAR0 region
 *
 * @dst_iomem: Pointer to the destination location in BAR0 space.
 * @src: Pointer to the Source data.
 * @size: Size of data to be copied.
 */
static void
_base_clone_to_sys_mem(void __iomem *dst_iomem, void *src, u32 size)
{
	int i;
	u32 *src_virt_mem = (u32 *)(src);

	for (i = 0; i < size/4; i++)
		writel((u32)src_virt_mem[i],
			(void __iomem *)dst_iomem + (i * 4));
}

/**
 * _base_get_chain - Calculates and Returns virtual chain address
 *			 for the provided smid in BAR0 space.
 *
 * @ioc: per adapter object
 * @smid: system request message index
 * @sge_chain_count: Scatter gather chain count.
 *
 * Return: the chain address.
 */
static inline void __iomem*
_base_get_chain(struct MPT3SAS_ADAPTER *ioc, u16 smid,
		u8 sge_chain_count)
{
	void __iomem *base_chain, *chain_virt;
	u16 cmd_credit = ioc->facts.RequestCredit + 1;

	base_chain  = (void __iomem *)ioc->chip + MPI_FRAME_START_OFFSET +
		(cmd_credit * ioc->request_sz) +
		REPLY_FREE_POOL_SIZE;
	chain_virt = base_chain + (smid * ioc->facts.MaxChainDepth *
			ioc->request_sz) + (sge_chain_count * ioc->request_sz);
	return chain_virt;
}

/**
 * _base_get_chain_phys - Calculates and Returns physical address
 *			in BAR0 for scatter gather chains, for
 *			the provided smid.
 *
 * @ioc: per adapter object
 * @smid: system request message index
 * @sge_chain_count: Scatter gather chain count.
 *
 * Return: Physical chain address.
 */
static inline phys_addr_t
_base_get_chain_phys(struct MPT3SAS_ADAPTER *ioc, u16 smid,
		u8 sge_chain_count)
{
	phys_addr_t base_chain_phys, chain_phys;
	u16 cmd_credit = ioc->facts.RequestCredit + 1;

	base_chain_phys  = ioc->chip_phys + MPI_FRAME_START_OFFSET +
		(cmd_credit * ioc->request_sz) +
		REPLY_FREE_POOL_SIZE;
	chain_phys = base_chain_phys + (smid * ioc->facts.MaxChainDepth *
			ioc->request_sz) + (sge_chain_count * ioc->request_sz);
	return chain_phys;
}

/**
 * _base_get_buffer_bar0 - Calculates and Returns BAR0 mapped Host
 *			buffer address for the provided smid.
 *			(Each smid can have 64K starts from 17024)
 *
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: Pointer to buffer location in BAR0.
 */

static void __iomem *
_base_get_buffer_bar0(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	// Added extra 1 to reach end of chain.
	void __iomem *chain_end = _base_get_chain(ioc,
			cmd_credit + 1,
			ioc->facts.MaxChainDepth);
	return chain_end + (smid * 64 * 1024);
}

/**
 * _base_get_buffer_phys_bar0 - Calculates and Returns BAR0 mapped
 *		Host buffer Physical address for the provided smid.
 *		(Each smid can have 64K starts from 17024)
 *
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: Pointer to buffer location in BAR0.
 */
static phys_addr_t
_base_get_buffer_phys_bar0(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	phys_addr_t chain_end_phys = _base_get_chain_phys(ioc,
			cmd_credit + 1,
			ioc->facts.MaxChainDepth);
	return chain_end_phys + (smid * 64 * 1024);
}

/**
 * _base_get_chain_buffer_dma_to_chain_buffer - Iterates chain
 *			lookup list and Provides chain_buffer
 *			address for the matching dma address.
 *			(Each smid can have 64K starts from 17024)
 *
 * @ioc: per adapter object
 * @chain_buffer_dma: Chain buffer dma address.
 *
 * Return: Pointer to chain buffer. Or Null on Failure.
 */
static void *
_base_get_chain_buffer_dma_to_chain_buffer(struct MPT3SAS_ADAPTER *ioc,
		dma_addr_t chain_buffer_dma)
{
	u16 index, j;
	struct chain_tracker *ct;

	for (index = 0; index < ioc->scsiio_depth; index++) {
		for (j = 0; j < ioc->chains_needed_per_io; j++) {
			ct = &ioc->chain_lookup[index].chains_per_smid[j];
			if (ct && ct->chain_buffer_dma == chain_buffer_dma)
				return ct->chain_buffer;
		}
	}
	ioc_info(ioc, "Provided chain_buffer_dma address is not in the lookup list\n");
	return NULL;
}

/**
 * _clone_sg_entries -	MPI EP's scsiio and config requests
 *			are handled here. Base function for
 *			double buffering, before submitting
 *			the requests.
 *
 * @ioc: per adapter object.
 * @mpi_request: mf request pointer.
 * @smid: system request message index.
 */
static void _clone_sg_entries(struct MPT3SAS_ADAPTER *ioc,
		void *mpi_request, u16 smid)
{
	Mpi2SGESimple32_t *sgel, *sgel_next;
	u32  sgl_flags, sge_chain_count = 0;
	bool is_write = 0;
	u16 i = 0;
	void __iomem *buffer_iomem;
	phys_addr_t buffer_iomem_phys;
	void __iomem *buff_ptr;
	phys_addr_t buff_ptr_phys;
	void __iomem *dst_chain_addr[MCPU_MAX_CHAINS_PER_IO];
	void *src_chain_addr[MCPU_MAX_CHAINS_PER_IO];
	phys_addr_t dst_addr_phys;
	MPI2RequestHeader_t *request_hdr;
	struct scsi_cmnd *scmd;
	struct scatterlist *sg_scmd = NULL;
	int is_scsiio_req = 0;

	request_hdr = (MPI2RequestHeader_t *) mpi_request;

	if (request_hdr->Function == MPI2_FUNCTION_SCSI_IO_REQUEST) {
		Mpi25SCSIIORequest_t *scsiio_request =
			(Mpi25SCSIIORequest_t *)mpi_request;
		sgel = (Mpi2SGESimple32_t *) &scsiio_request->SGL;
		is_scsiio_req = 1;
	} else if (request_hdr->Function == MPI2_FUNCTION_CONFIG) {
		Mpi2ConfigRequest_t  *config_req =
			(Mpi2ConfigRequest_t *)mpi_request;
		sgel = (Mpi2SGESimple32_t *) &config_req->PageBufferSGE;
	} else
		return;

	/* From smid we can get scsi_cmd, once we have sg_scmd,
	 * we just need to get sg_virt and sg_next to get virual
	 * address associated with sgel->Address.
	 */

	if (is_scsiio_req) {
		/* Get scsi_cmd using smid */
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (scmd == NULL) {
			ioc_err(ioc, "scmd is NULL\n");
			return;
		}

		/* Get sg_scmd from scmd provided */
		sg_scmd = scsi_sglist(scmd);
	}

	/*
	 * 0 - 255	System register
	 * 256 - 4352	MPI Frame. (This is based on maxCredit 32)
	 * 4352 - 4864	Reply_free pool (512 byte is reserved
	 *		considering maxCredit 32. Reply need extra
	 *		room, for mCPU case kept four times of
	 *		maxCredit).
	 * 4864 - 17152	SGE chain element. (32cmd * 3 chain of
	 *		128 byte size = 12288)
	 * 17152 - x	Host buffer mapped with smid.
	 *		(Each smid can have 64K Max IO.)
	 * BAR0+Last 1K MSIX Addr and Data
	 * Total size in use 2113664 bytes of 4MB BAR0
	 */

	buffer_iomem = _base_get_buffer_bar0(ioc, smid);
	buffer_iomem_phys = _base_get_buffer_phys_bar0(ioc, smid);

	buff_ptr = buffer_iomem;
	buff_ptr_phys = buffer_iomem_phys;
	WARN_ON(buff_ptr_phys > U32_MAX);

	if (le32_to_cpu(sgel->FlagsLength) &
			(MPI2_SGE_FLAGS_HOST_TO_IOC << MPI2_SGE_FLAGS_SHIFT))
		is_write = 1;

	for (i = 0; i < MPT_MIN_PHYS_SEGMENTS + ioc->facts.MaxChainDepth; i++) {

		sgl_flags =
		    (le32_to_cpu(sgel->FlagsLength) >> MPI2_SGE_FLAGS_SHIFT);

		switch (sgl_flags & MPI2_SGE_FLAGS_ELEMENT_MASK) {
		case MPI2_SGE_FLAGS_CHAIN_ELEMENT:
			/*
			 * Helper function which on passing
			 * chain_buffer_dma returns chain_buffer. Get
			 * the virtual address for sgel->Address
			 */
			sgel_next =
				_base_get_chain_buffer_dma_to_chain_buffer(ioc,
						le32_to_cpu(sgel->Address));
			if (sgel_next == NULL)
				return;
			/*
			 * This is coping 128 byte chain
			 * frame (not a host buffer)
			 */
			dst_chain_addr[sge_chain_count] =
				_base_get_chain(ioc,
					smid, sge_chain_count);
			src_chain_addr[sge_chain_count] =
						(void *) sgel_next;
			dst_addr_phys = _base_get_chain_phys(ioc,
						smid, sge_chain_count);
			WARN_ON(dst_addr_phys > U32_MAX);
			sgel->Address =
				cpu_to_le32(lower_32_bits(dst_addr_phys));
			sgel = sgel_next;
			sge_chain_count++;
			break;
		case MPI2_SGE_FLAGS_SIMPLE_ELEMENT:
			if (is_write) {
				if (is_scsiio_req) {
					_base_clone_to_sys_mem(buff_ptr,
					    sg_virt(sg_scmd),
					    (le32_to_cpu(sgel->FlagsLength) &
					    0x00ffffff));
					/*
					 * FIXME: this relies on a a zero
					 * PCI mem_offset.
					 */
					sgel->Address =
					    cpu_to_le32((u32)buff_ptr_phys);
				} else {
					_base_clone_to_sys_mem(buff_ptr,
					    ioc->config_vaddr,
					    (le32_to_cpu(sgel->FlagsLength) &
					    0x00ffffff));
					sgel->Address =
					    cpu_to_le32((u32)buff_ptr_phys);
				}
			}
			buff_ptr += (le32_to_cpu(sgel->FlagsLength) &
			    0x00ffffff);
			buff_ptr_phys += (le32_to_cpu(sgel->FlagsLength) &
			    0x00ffffff);
			if ((le32_to_cpu(sgel->FlagsLength) &
			    (MPI2_SGE_FLAGS_END_OF_BUFFER
					<< MPI2_SGE_FLAGS_SHIFT)))
				goto eob_clone_chain;
			else {
				/*
				 * Every single element in MPT will have
				 * associated sg_next. Better to sanity that
				 * sg_next is not NULL, but it will be a bug
				 * if it is null.
				 */
				if (is_scsiio_req) {
					sg_scmd = sg_next(sg_scmd);
					if (sg_scmd)
						sgel++;
					else
						goto eob_clone_chain;
				}
			}
			break;
		}
	}

eob_clone_chain:
	for (i = 0; i < sge_chain_count; i++) {
		if (is_scsiio_req)
			_base_clone_to_sys_mem(dst_chain_addr[i],
				src_chain_addr[i], ioc->request_sz);
	}
}

/**
 *  mpt3sas_remove_dead_ioc_func - kthread context to remove dead ioc
 * @arg: input argument, used to derive ioc
 *
 * Return:
 * 0 if controller is removed from pci subsystem.
 * -1 for other case.
 */
static int mpt3sas_remove_dead_ioc_func(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
	struct pci_dev *pdev;

	if (!ioc)
		return -1;

	pdev = ioc->pdev;
	if (!pdev)
		return -1;
	pci_stop_and_remove_bus_device_locked(pdev);
	return 0;
}

/**
 * _base_fault_reset_work - workq handling ioc fault conditions
 * @work: input argument, used to derive ioc
 *
 * Context: sleep.
 */
static void
_base_fault_reset_work(struct work_struct *work)
{
	struct MPT3SAS_ADAPTER *ioc =
	    container_of(work, struct MPT3SAS_ADAPTER, fault_reset_work.work);
	unsigned long	 flags;
	u32 doorbell;
	int rc;
	struct task_struct *p;


	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->shost_recovery || ioc->pci_error_recovery)
		goto rearm_timer;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	doorbell = mpt3sas_base_get_iocstate(ioc, 0);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_MASK) {
		ioc_err(ioc, "SAS host is non-operational !!!!\n");

		/* It may be possible that EEH recovery can resolve some of
		 * pci bus failure issues rather removing the dead ioc function
		 * by considering controller is in a non-operational state. So
		 * here priority is given to the EEH recovery. If it doesn't
		 * not resolve this issue, mpt3sas driver will consider this
		 * controller to non-operational state and remove the dead ioc
		 * function.
		 */
		if (ioc->non_operational_loop++ < 5) {
			spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock,
							 flags);
			goto rearm_timer;
		}

		/*
		 * Call _scsih_flush_pending_cmds callback so that we flush all
		 * pending commands back to OS. This call is required to aovid
		 * deadlock at block layer. Dead IOC will fail to do diag reset,
		 * and this call is safe since dead ioc will never return any
		 * command back from HW.
		 */
		ioc->schedule_dead_ioc_flush_running_cmds(ioc);
		/*
		 * Set remove_host flag early since kernel thread will
		 * take some time to execute.
		 */
		ioc->remove_host = 1;
		/*Remove the Dead Host */
		p = kthread_run(mpt3sas_remove_dead_ioc_func, ioc,
		    "%s_dead_ioc_%d", ioc->driver_name, ioc->id);
		if (IS_ERR(p))
			ioc_err(ioc, "%s: Running mpt3sas_dead_ioc thread failed !!!!\n",
				__func__);
		else
			ioc_err(ioc, "%s: Running mpt3sas_dead_ioc thread success !!!!\n",
				__func__);
		return; /* don't rearm timer */
	}

	ioc->non_operational_loop = 0;

	if ((doorbell & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL) {
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		ioc_warn(ioc, "%s: hard reset: %s\n",
			 __func__, rc == 0 ? "success" : "failed");
		doorbell = mpt3sas_base_get_iocstate(ioc, 0);
		if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
			mpt3sas_base_fault_info(ioc, doorbell &
			    MPI2_DOORBELL_DATA_MASK);
		if (rc && (doorbell & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_OPERATIONAL)
			return; /* don't rearm timer */
	}

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
 rearm_timer:
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

/**
 * mpt3sas_base_start_watchdog - start the fault_reset_work_q
 * @ioc: per adapter object
 *
 * Context: sleep.
 */
void
mpt3sas_base_start_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long	 flags;

	if (ioc->fault_reset_work_q)
		return;

	/* initialize fault polling */

	INIT_DELAYED_WORK(&ioc->fault_reset_work, _base_fault_reset_work);
	snprintf(ioc->fault_reset_work_q_name,
	    sizeof(ioc->fault_reset_work_q_name), "poll_%s%d_status",
	    ioc->driver_name, ioc->id);
	ioc->fault_reset_work_q =
		create_singlethread_workqueue(ioc->fault_reset_work_q_name);
	if (!ioc->fault_reset_work_q) {
		ioc_err(ioc, "%s: failed (line=%d)\n", __func__, __LINE__);
		return;
	}
	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

/**
 * mpt3sas_base_stop_watchdog - stop the fault_reset_work_q
 * @ioc: per adapter object
 *
 * Context: sleep.
 */
void
mpt3sas_base_stop_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	wq = ioc->fault_reset_work_q;
	ioc->fault_reset_work_q = NULL;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	if (wq) {
		if (!cancel_delayed_work_sync(&ioc->fault_reset_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpt3sas_base_fault_info - verbose translation of firmware FAULT code
 * @ioc: per adapter object
 * @fault_code: fault code
 */
void
mpt3sas_base_fault_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code)
{
	ioc_err(ioc, "fault_state(0x%04x)!\n", fault_code);
}

/**
 * mpt3sas_halt_firmware - halt's mpt controller firmware
 * @ioc: per adapter object
 *
 * For debugging timeout related issues.  Writing 0xCOFFEE00
 * to the doorbell register will halt controller firmware. With
 * the purpose to stop both driver and firmware, the enduser can
 * obtain a ring buffer from controller UART.
 */
void
mpt3sas_halt_firmware(struct MPT3SAS_ADAPTER *ioc)
{
	u32 doorbell;

	if (!ioc->fwfault_debug)
		return;

	dump_stack();

	doorbell = ioc->base_readl(&ioc->chip->Doorbell);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
		mpt3sas_base_fault_info(ioc , doorbell);
	else {
		writel(0xC0FFEE00, &ioc->chip->Doorbell);
		ioc_err(ioc, "Firmware is halted due to command timeout\n");
	}

	if (ioc->fwfault_debug == 2)
		for (;;)
			;
	else
		panic("panic in %s\n", __func__);
}

/**
 * _base_sas_ioc_info - verbose translation of the ioc status
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @request_hdr: request mf
 */
static void
_base_sas_ioc_info(struct MPT3SAS_ADAPTER *ioc, MPI2DefaultReply_t *mpi_reply,
	MPI2RequestHeader_t *request_hdr)
{
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	char *desc = NULL;
	u16 frame_sz;
	char *func_str = NULL;

	/* SCSI_IO, RAID_PASS are handled from _scsih_scsi_ioc_info */
	if (request_hdr->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    request_hdr->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
	    request_hdr->Function == MPI2_FUNCTION_EVENT_NOTIFICATION)
		return;

	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return;

	switch (ioc_status) {

/****************************************************************************
*  Common IOCStatus values for all replies
****************************************************************************/

	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc = "invalid function";
		break;
	case MPI2_IOCSTATUS_BUSY:
		desc = "busy";
		break;
	case MPI2_IOCSTATUS_INVALID_SGL:
		desc = "invalid sgl";
		break;
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
		desc = "internal error";
		break;
	case MPI2_IOCSTATUS_INVALID_VPID:
		desc = "invalid vpid";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		desc = "insufficient resources";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
		desc = "insufficient power";
		break;
	case MPI2_IOCSTATUS_INVALID_FIELD:
		desc = "invalid field";
		break;
	case MPI2_IOCSTATUS_INVALID_STATE:
		desc = "invalid state";
		break;
	case MPI2_IOCSTATUS_OP_STATE_NOT_SUPPORTED:
		desc = "op state not supported";
		break;

/****************************************************************************
*  Config IOCStatus values
****************************************************************************/

	case MPI2_IOCSTATUS_CONFIG_INVALID_ACTION:
		desc = "config invalid action";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_TYPE:
		desc = "config invalid type";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_PAGE:
		desc = "config invalid page";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_DATA:
		desc = "config invalid data";
		break;
	case MPI2_IOCSTATUS_CONFIG_NO_DEFAULTS:
		desc = "config no defaults";
		break;
	case MPI2_IOCSTATUS_CONFIG_CANT_COMMIT:
		desc = "config cant commit";
		break;

/****************************************************************************
*  SCSI IO Reply
****************************************************************************/

	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		break;

/****************************************************************************
*  For use by SCSI Initiator and SCSI Target end-to-end data protection
****************************************************************************/

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		desc = "eedp guard error";
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		desc = "eedp ref tag error";
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		desc = "eedp app tag error";
		break;

/****************************************************************************
*  SCSI Target values
****************************************************************************/

	case MPI2_IOCSTATUS_TARGET_INVALID_IO_INDEX:
		desc = "target invalid io index";
		break;
	case MPI2_IOCSTATUS_TARGET_ABORTED:
		desc = "target aborted";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONN_RETRYABLE:
		desc = "target no conn retryable";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONNECTION:
		desc = "target no connection";
		break;
	case MPI2_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH:
		desc = "target xfer count mismatch";
		break;
	case MPI2_IOCSTATUS_TARGET_DATA_OFFSET_ERROR:
		desc = "target data offset error";
		break;
	case MPI2_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA:
		desc = "target too much write data";
		break;
	case MPI2_IOCSTATUS_TARGET_IU_TOO_SHORT:
		desc = "target iu too short";
		break;
	case MPI2_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT:
		desc = "target ack nak timeout";
		break;
	case MPI2_IOCSTATUS_TARGET_NAK_RECEIVED:
		desc = "target nak received";
		break;

/****************************************************************************
*  Serial Attached SCSI values
****************************************************************************/

	case MPI2_IOCSTATUS_SAS_SMP_REQUEST_FAILED:
		desc = "smp request failed";
		break;
	case MPI2_IOCSTATUS_SAS_SMP_DATA_OVERRUN:
		desc = "smp data overrun";
		break;

/****************************************************************************
*  Diagnostic Buffer Post / Diagnostic Release values
****************************************************************************/

	case MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED:
		desc = "diagnostic released";
		break;
	default:
		break;
	}

	if (!desc)
		return;

	switch (request_hdr->Function) {
	case MPI2_FUNCTION_CONFIG:
		frame_sz = sizeof(Mpi2ConfigRequest_t) + ioc->sge_size;
		func_str = "config_page";
		break;
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
		frame_sz = sizeof(Mpi2SCSITaskManagementRequest_t);
		func_str = "task_mgmt";
		break;
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
		frame_sz = sizeof(Mpi2SasIoUnitControlRequest_t);
		func_str = "sas_iounit_ctl";
		break;
	case MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR:
		frame_sz = sizeof(Mpi2SepRequest_t);
		func_str = "enclosure";
		break;
	case MPI2_FUNCTION_IOC_INIT:
		frame_sz = sizeof(Mpi2IOCInitRequest_t);
		func_str = "ioc_init";
		break;
	case MPI2_FUNCTION_PORT_ENABLE:
		frame_sz = sizeof(Mpi2PortEnableRequest_t);
		func_str = "port_enable";
		break;
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
		frame_sz = sizeof(Mpi2SmpPassthroughRequest_t) + ioc->sge_size;
		func_str = "smp_passthru";
		break;
	case MPI2_FUNCTION_NVME_ENCAPSULATED:
		frame_sz = sizeof(Mpi26NVMeEncapsulatedRequest_t) +
		    ioc->sge_size;
		func_str = "nvme_encapsulated";
		break;
	default:
		frame_sz = 32;
		func_str = "unknown";
		break;
	}

	ioc_warn(ioc, "ioc_status: %s(0x%04x), request(0x%p),(%s)\n",
		 desc, ioc_status, request_hdr, func_str);

	_debug_dump_mf(request_hdr, frame_sz/4);
}

/**
 * _base_display_event_data - verbose translation of firmware asyn events
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 */
static void
_base_display_event_data(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventNotificationReply_t *mpi_reply)
{
	char *desc = NULL;
	u16 event;

	if (!(ioc->logging_level & MPT_DEBUG_EVENTS))
		return;

	event = le16_to_cpu(mpi_reply->Event);

	switch (event) {
	case MPI2_EVENT_LOG_DATA:
		desc = "Log Data";
		break;
	case MPI2_EVENT_STATE_CHANGE:
		desc = "Status Change";
		break;
	case MPI2_EVENT_HARD_RESET_RECEIVED:
		desc = "Hard Reset Received";
		break;
	case MPI2_EVENT_EVENT_CHANGE:
		desc = "Event Change";
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		desc = "Device Status Change";
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		if (!ioc->hide_ir_msg)
			desc = "IR Operation Status";
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		Mpi2EventDataSasDiscovery_t *event_data =
		    (Mpi2EventDataSasDiscovery_t *)mpi_reply->EventData;
		ioc_info(ioc, "Discovery: (%s)",
			 event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED ?
			 "start" : "stop");
		if (event_data->DiscoveryStatus)
			pr_cont(" discovery_status(0x%08x)",
			    le32_to_cpu(event_data->DiscoveryStatus));
		pr_cont("\n");
		return;
	}
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		desc = "SAS Broadcast Primitive";
		break;
	case MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
		desc = "SAS Init Device Status Change";
		break;
	case MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW:
		desc = "SAS Init Table Overflow";
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		desc = "SAS Topology Change List";
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		desc = "SAS Enclosure Device Status Change";
		break;
	case MPI2_EVENT_IR_VOLUME:
		if (!ioc->hide_ir_msg)
			desc = "IR Volume";
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		if (!ioc->hide_ir_msg)
			desc = "IR Physical Disk";
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		if (!ioc->hide_ir_msg)
			desc = "IR Configuration Change List";
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
		if (!ioc->hide_ir_msg)
			desc = "Log Entry Added";
		break;
	case MPI2_EVENT_TEMP_THRESHOLD:
		desc = "Temperature Threshold";
		break;
	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
		desc = "Cable Event";
		break;
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
		desc = "SAS Device Discovery Error";
		break;
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		desc = "PCIE Device Status Change";
		break;
	case MPI2_EVENT_PCIE_ENUMERATION:
	{
		Mpi26EventDataPCIeEnumeration_t *event_data =
			(Mpi26EventDataPCIeEnumeration_t *)mpi_reply->EventData;
		ioc_info(ioc, "PCIE Enumeration: (%s)",
			 event_data->ReasonCode == MPI26_EVENT_PCIE_ENUM_RC_STARTED ?
			 "start" : "stop");
		if (event_data->EnumerationStatus)
			pr_cont("enumeration_status(0x%08x)",
				le32_to_cpu(event_data->EnumerationStatus));
		pr_cont("\n");
		return;
	}
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
		desc = "PCIE Topology Change List";
		break;
	}

	if (!desc)
		return;

	ioc_info(ioc, "%s\n", desc);
}

/**
 * _base_sas_log_info - verbose translation of firmware log info
 * @ioc: per adapter object
 * @log_info: log info
 */
static void
_base_sas_log_info(struct MPT3SAS_ADAPTER *ioc , u32 log_info)
{
	union loginfo_type {
		u32	loginfo;
		struct {
			u32	subcode:16;
			u32	code:8;
			u32	originator:4;
			u32	bus_type:4;
		} dw;
	};
	union loginfo_type sas_loginfo;
	char *originator_str = NULL;

	sas_loginfo.loginfo = log_info;
	if (sas_loginfo.dw.bus_type != 3 /*SAS*/)
		return;

	/* each nexus loss loginfo */
	if (log_info == 0x31170000)
		return;

	/* eat the loginfos associated with task aborts */
	if (ioc->ignore_loginfos && (log_info == 0x30050000 || log_info ==
	    0x31140000 || log_info == 0x31130000))
		return;

	switch (sas_loginfo.dw.originator) {
	case 0:
		originator_str = "IOP";
		break;
	case 1:
		originator_str = "PL";
		break;
	case 2:
		if (!ioc->hide_ir_msg)
			originator_str = "IR";
		else
			originator_str = "WarpDrive";
		break;
	}

	ioc_warn(ioc, "log_info(0x%08x): originator(%s), code(0x%02x), sub_code(0x%04x)\n",
		 log_info,
		 originator_str, sas_loginfo.dw.code, sas_loginfo.dw.subcode);
}

/**
 * _base_display_reply_info -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 */
static void
_base_display_reply_info(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;
	u32 loginfo = 0;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (unlikely(!mpi_reply)) {
		ioc_err(ioc, "mpi_reply not valid at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);

	if ((ioc_status & MPI2_IOCSTATUS_MASK) &&
	    (ioc->logging_level & MPT_DEBUG_REPLY)) {
		_base_sas_ioc_info(ioc , mpi_reply,
		   mpt3sas_base_get_msg_frame(ioc, smid));
	}

	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		loginfo = le32_to_cpu(mpi_reply->IOCLogInfo);
		_base_sas_log_info(ioc, loginfo);
	}

	if (ioc_status || loginfo) {
		ioc_status &= MPI2_IOCSTATUS_MASK;
		mpt3sas_trigger_mpi(ioc, ioc_status, loginfo);
	}
}

/**
 * mpt3sas_base_done - base internal command completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return:
 * 1 meaning mf should be freed from _base_interrupt
 * 0 means the mf is freed from this function.
 */
u8
mpt3sas_base_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply && mpi_reply->Function == MPI2_FUNCTION_EVENT_ACK)
		return mpt3sas_check_for_pending_internal_cmds(ioc, smid);

	if (ioc->base_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	ioc->base_cmds.status |= MPT3_CMD_COMPLETE;
	if (mpi_reply) {
		ioc->base_cmds.status |= MPT3_CMD_REPLY_VALID;
		memcpy(ioc->base_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	}
	ioc->base_cmds.status &= ~MPT3_CMD_PENDING;

	complete(&ioc->base_cmds.done);
	return 1;
}

/**
 * _base_async_event - main callback handler for firmware asyn events
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return:
 * 1 meaning mf should be freed from _base_interrupt
 * 0 means the mf is freed from this function.
 */
static u8
_base_async_event(struct MPT3SAS_ADAPTER *ioc, u8 msix_index, u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;
	Mpi2EventAckRequest_t *ack_request;
	u16 smid;
	struct _event_ack_list *delayed_event_ack;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;
	if (mpi_reply->Function != MPI2_FUNCTION_EVENT_NOTIFICATION)
		return 1;

	_base_display_event_data(ioc, mpi_reply);

	if (!(mpi_reply->AckRequired & MPI2_EVENT_NOTIFICATION_ACK_REQUIRED))
		goto out;
	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		delayed_event_ack = kzalloc(sizeof(*delayed_event_ack),
					GFP_ATOMIC);
		if (!delayed_event_ack)
			goto out;
		INIT_LIST_HEAD(&delayed_event_ack->list);
		delayed_event_ack->Event = mpi_reply->Event;
		delayed_event_ack->EventContext = mpi_reply->EventContext;
		list_add_tail(&delayed_event_ack->list,
				&ioc->delayed_event_ack_list);
		dewtprintk(ioc,
			   ioc_info(ioc, "DELAYED: EVENT ACK: event (0x%04x)\n",
				    le16_to_cpu(mpi_reply->Event)));
		goto out;
	}

	ack_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = mpi_reply->Event;
	ack_request->EventContext = mpi_reply->EventContext;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	mpt3sas_base_put_smid_default(ioc, smid);

 out:

	/* scsih callback handler */
	mpt3sas_scsih_event_callback(ioc, msix_index, reply);

	/* ctl callback handler */
	mpt3sas_ctl_event_callback(ioc, msix_index, reply);

	return 1;
}

static struct scsiio_tracker *
_get_st_from_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct scsi_cmnd *cmd;

	if (WARN_ON(!smid) ||
	    WARN_ON(smid >= ioc->hi_priority_smid))
		return NULL;

	cmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
	if (cmd)
		return scsi_cmd_priv(cmd);

	return NULL;
}

/**
 * _base_get_cb_idx - obtain the callback index
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: callback index.
 */
static u8
_base_get_cb_idx(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	int i;
	u16 ctl_smid = ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT + 1;
	u8 cb_idx = 0xFF;

	if (smid < ioc->hi_priority_smid) {
		struct scsiio_tracker *st;

		if (smid < ctl_smid) {
			st = _get_st_from_smid(ioc, smid);
			if (st)
				cb_idx = st->cb_idx;
		} else if (smid == ctl_smid)
			cb_idx = ioc->ctl_cb_idx;
	} else if (smid < ioc->internal_smid) {
		i = smid - ioc->hi_priority_smid;
		cb_idx = ioc->hpr_lookup[i].cb_idx;
	} else if (smid <= ioc->hba_queue_depth) {
		i = smid - ioc->internal_smid;
		cb_idx = ioc->internal_lookup[i].cb_idx;
	}
	return cb_idx;
}

/**
 * _base_mask_interrupts - disable interrupts
 * @ioc: per adapter object
 *
 * Disabling ResetIRQ, Reply and Doorbell Interrupts
 */
static void
_base_mask_interrupts(struct MPT3SAS_ADAPTER *ioc)
{
	u32 him_register;

	ioc->mask_interrupts = 1;
	him_register = ioc->base_readl(&ioc->chip->HostInterruptMask);
	him_register |= MPI2_HIM_DIM + MPI2_HIM_RIM + MPI2_HIM_RESET_IRQ_MASK;
	writel(him_register, &ioc->chip->HostInterruptMask);
	ioc->base_readl(&ioc->chip->HostInterruptMask);
}

/**
 * _base_unmask_interrupts - enable interrupts
 * @ioc: per adapter object
 *
 * Enabling only Reply Interrupts
 */
static void
_base_unmask_interrupts(struct MPT3SAS_ADAPTER *ioc)
{
	u32 him_register;

	him_register = ioc->base_readl(&ioc->chip->HostInterruptMask);
	him_register &= ~MPI2_HIM_RIM;
	writel(him_register, &ioc->chip->HostInterruptMask);
	ioc->mask_interrupts = 0;
}

union reply_descriptor {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

/**
 * _base_interrupt - MPT adapter (IOC) specific interrupt handler.
 * @irq: irq number (not used)
 * @bus_id: bus identifier cookie == pointer to MPT_ADAPTER structure
 *
 * Return: IRQ_HANDLED if processed, else IRQ_NONE.
 */
static irqreturn_t
_base_interrupt(int irq, void *bus_id)
{
	struct adapter_reply_queue *reply_q = bus_id;
	union reply_descriptor rd;
	u32 completed_cmds;
	u8 request_desript_type;
	u16 smid;
	u8 cb_idx;
	u32 reply;
	u8 msix_index = reply_q->msix_index;
	struct MPT3SAS_ADAPTER *ioc = reply_q->ioc;
	Mpi2ReplyDescriptorsUnion_t *rpf;
	u8 rc;

	if (ioc->mask_interrupts)
		return IRQ_NONE;

	if (!atomic_add_unless(&reply_q->busy, 1, 1))
		return IRQ_NONE;

	rpf = &reply_q->reply_post_free[reply_q->reply_post_host_index];
	request_desript_type = rpf->Default.ReplyFlags
	     & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
	if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED) {
		atomic_dec(&reply_q->busy);
		return IRQ_NONE;
	}

	completed_cmds = 0;
	cb_idx = 0xFF;
	do {
		rd.word = le64_to_cpu(rpf->Words);
		if (rd.u.low == UINT_MAX || rd.u.high == UINT_MAX)
			goto out;
		reply = 0;
		smid = le16_to_cpu(rpf->Default.DescriptorTypeDependent1);
		if (request_desript_type ==
		    MPI25_RPY_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO_SUCCESS ||
		    request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS ||
		    request_desript_type ==
		    MPI26_RPY_DESCRIPT_FLAGS_PCIE_ENCAPSULATED_SUCCESS) {
			cb_idx = _base_get_cb_idx(ioc, smid);
			if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
			    (likely(mpt_callbacks[cb_idx] != NULL))) {
				rc = mpt_callbacks[cb_idx](ioc, smid,
				    msix_index, 0);
				if (rc)
					mpt3sas_base_free_smid(ioc, smid);
			}
		} else if (request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY) {
			reply = le32_to_cpu(
			    rpf->AddressReply.ReplyFrameAddress);
			if (reply > ioc->reply_dma_max_address ||
			    reply < ioc->reply_dma_min_address)
				reply = 0;
			if (smid) {
				cb_idx = _base_get_cb_idx(ioc, smid);
				if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
				    (likely(mpt_callbacks[cb_idx] != NULL))) {
					rc = mpt_callbacks[cb_idx](ioc, smid,
					    msix_index, reply);
					if (reply)
						_base_display_reply_info(ioc,
						    smid, msix_index, reply);
					if (rc)
						mpt3sas_base_free_smid(ioc,
						    smid);
				}
			} else {
				_base_async_event(ioc, msix_index, reply);
			}

			/* reply free queue handling */
			if (reply) {
				ioc->reply_free_host_index =
				    (ioc->reply_free_host_index ==
				    (ioc->reply_free_queue_depth - 1)) ?
				    0 : ioc->reply_free_host_index + 1;
				ioc->reply_free[ioc->reply_free_host_index] =
				    cpu_to_le32(reply);
				if (ioc->is_mcpu_endpoint)
					_base_clone_reply_to_sys_mem(ioc,
						reply,
						ioc->reply_free_host_index);
				writel(ioc->reply_free_host_index,
				    &ioc->chip->ReplyFreeHostIndex);
			}
		}

		rpf->Words = cpu_to_le64(ULLONG_MAX);
		reply_q->reply_post_host_index =
		    (reply_q->reply_post_host_index ==
		    (ioc->reply_post_queue_depth - 1)) ? 0 :
		    reply_q->reply_post_host_index + 1;
		request_desript_type =
		    reply_q->reply_post_free[reply_q->reply_post_host_index].
		    Default.ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
		completed_cmds++;
		/* Update the reply post host index after continuously
		 * processing the threshold number of Reply Descriptors.
		 * So that FW can find enough entries to post the Reply
		 * Descriptors in the reply descriptor post queue.
		 */
		if (completed_cmds > ioc->hba_queue_depth/3) {
			if (ioc->combined_reply_queue) {
				writel(reply_q->reply_post_host_index |
						((msix_index  & 7) <<
						 MPI2_RPHI_MSIX_INDEX_SHIFT),
				    ioc->replyPostRegisterIndex[msix_index/8]);
			} else {
				writel(reply_q->reply_post_host_index |
						(msix_index <<
						 MPI2_RPHI_MSIX_INDEX_SHIFT),
						&ioc->chip->ReplyPostHostIndex);
			}
			completed_cmds = 1;
		}
		if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			goto out;
		if (!reply_q->reply_post_host_index)
			rpf = reply_q->reply_post_free;
		else
			rpf++;
	} while (1);

 out:

	if (!completed_cmds) {
		atomic_dec(&reply_q->busy);
		return IRQ_NONE;
	}

	if (ioc->is_warpdrive) {
		writel(reply_q->reply_post_host_index,
		ioc->reply_post_host_index[msix_index]);
		atomic_dec(&reply_q->busy);
		return IRQ_HANDLED;
	}

	/* Update Reply Post Host Index.
	 * For those HBA's which support combined reply queue feature
	 * 1. Get the correct Supplemental Reply Post Host Index Register.
	 *    i.e. (msix_index / 8)th entry from Supplemental Reply Post Host
	 *    Index Register address bank i.e replyPostRegisterIndex[],
	 * 2. Then update this register with new reply host index value
	 *    in ReplyPostIndex field and the MSIxIndex field with
	 *    msix_index value reduced to a value between 0 and 7,
	 *    using a modulo 8 operation. Since each Supplemental Reply Post
	 *    Host Index Register supports 8 MSI-X vectors.
	 *
	 * For other HBA's just update the Reply Post Host Index register with
	 * new reply host index value in ReplyPostIndex Field and msix_index
	 * value in MSIxIndex field.
	 */
	if (ioc->combined_reply_queue)
		writel(reply_q->reply_post_host_index | ((msix_index  & 7) <<
			MPI2_RPHI_MSIX_INDEX_SHIFT),
			ioc->replyPostRegisterIndex[msix_index/8]);
	else
		writel(reply_q->reply_post_host_index | (msix_index <<
			MPI2_RPHI_MSIX_INDEX_SHIFT),
			&ioc->chip->ReplyPostHostIndex);
	atomic_dec(&reply_q->busy);
	return IRQ_HANDLED;
}

/**
 * _base_is_controller_msix_enabled - is controller support muli-reply queues
 * @ioc: per adapter object
 *
 * Return: Whether or not MSI/X is enabled.
 */
static inline int
_base_is_controller_msix_enabled(struct MPT3SAS_ADAPTER *ioc)
{
	return (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX) && ioc->msix_enable;
}

/**
 * mpt3sas_base_sync_reply_irqs - flush pending MSIX interrupts
 * @ioc: per adapter object
 * Context: non ISR conext
 *
 * Called when a Task Management request has completed.
 */
void
mpt3sas_base_sync_reply_irqs(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;

	/* If MSIX capability is turned off
	 * then multi-queues are not enabled
	 */
	if (!_base_is_controller_msix_enabled(ioc))
		return;

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->shost_recovery || ioc->remove_host ||
				ioc->pci_error_recovery)
			return;
		/* TMs are on msix_index == 0 */
		if (reply_q->msix_index == 0)
			continue;
		synchronize_irq(pci_irq_vector(ioc->pdev, reply_q->msix_index));
	}
}

/**
 * mpt3sas_base_release_callback_handler - clear interrupt callback handler
 * @cb_idx: callback index
 */
void
mpt3sas_base_release_callback_handler(u8 cb_idx)
{
	mpt_callbacks[cb_idx] = NULL;
}

/**
 * mpt3sas_base_register_callback_handler - obtain index for the interrupt callback handler
 * @cb_func: callback function
 *
 * Return: Index of @cb_func.
 */
u8
mpt3sas_base_register_callback_handler(MPT_CALLBACK cb_func)
{
	u8 cb_idx;

	for (cb_idx = MPT_MAX_CALLBACKS-1; cb_idx; cb_idx--)
		if (mpt_callbacks[cb_idx] == NULL)
			break;

	mpt_callbacks[cb_idx] = cb_func;
	return cb_idx;
}

/**
 * mpt3sas_base_initialize_callback_handler - initialize the interrupt callback handler
 */
void
mpt3sas_base_initialize_callback_handler(void)
{
	u8 cb_idx;

	for (cb_idx = 0; cb_idx < MPT_MAX_CALLBACKS; cb_idx++)
		mpt3sas_base_release_callback_handler(cb_idx);
}


/**
 * _base_build_zero_len_sge - build zero length sg entry
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 */
static void
_base_build_zero_len_sge(struct MPT3SAS_ADAPTER *ioc, void *paddr)
{
	u32 flags_length = (u32)((MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST |
	    MPI2_SGE_FLAGS_SIMPLE_ELEMENT) <<
	    MPI2_SGE_FLAGS_SHIFT);
	ioc->base_add_sg_single(paddr, flags_length, -1);
}

/**
 * _base_add_sg_single_32 - Place a simple 32 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 */
static void
_base_add_sg_single_32(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple32_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le32(dma_addr);
}


/**
 * _base_add_sg_single_64 - Place a simple 64 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 */
static void
_base_add_sg_single_64(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple64_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_64_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le64(dma_addr);
}

/**
 * _base_get_chain_buffer_tracker - obtain chain tracker
 * @ioc: per adapter object
 * @scmd: SCSI commands of the IO request
 *
 * Return: chain tracker from chain_lookup table using key as
 * smid and smid's chain_offset.
 */
static struct chain_tracker *
_base_get_chain_buffer_tracker(struct MPT3SAS_ADAPTER *ioc,
			       struct scsi_cmnd *scmd)
{
	struct chain_tracker *chain_req;
	struct scsiio_tracker *st = scsi_cmd_priv(scmd);
	u16 smid = st->smid;
	u8 chain_offset =
	   atomic_read(&ioc->chain_lookup[smid - 1].chain_offset);

	if (chain_offset == ioc->chains_needed_per_io)
		return NULL;

	chain_req = &ioc->chain_lookup[smid - 1].chains_per_smid[chain_offset];
	atomic_inc(&ioc->chain_lookup[smid - 1].chain_offset);
	return chain_req;
}


/**
 * _base_build_sg - build generic sg
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 */
static void
_base_build_sg(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u32 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size;

		/* READ sgel last */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	}
}

/* IEEE format sgls */

/**
 * _base_build_nvme_prp - This function is called for NVMe end devices to build
 * a native SGL (NVMe PRP). The native SGL is built starting in the first PRP
 * entry of the NVMe message (PRP1).  If the data buffer is small enough to be
 * described entirely using PRP1, then PRP2 is not used.  If needed, PRP2 is
 * used to describe a larger data buffer.  If the data buffer is too large to
 * describe using the two PRP entriess inside the NVMe message, then PRP1
 * describes the first data memory segment, and PRP2 contains a pointer to a PRP
 * list located elsewhere in memory to describe the remaining data memory
 * segments.  The PRP list will be contiguous.
 *
 * The native SGL for NVMe devices is a Physical Region Page (PRP).  A PRP
 * consists of a list of PRP entries to describe a number of noncontigous
 * physical memory segments as a single memory buffer, just as a SGL does.  Note
 * however, that this function is only used by the IOCTL call, so the memory
 * given will be guaranteed to be contiguous.  There is no need to translate
 * non-contiguous SGL into a PRP in this case.  All PRPs will describe
 * contiguous space that is one page size each.
 *
 * Each NVMe message contains two PRP entries.  The first (PRP1) either contains
 * a PRP list pointer or a PRP element, depending upon the command.  PRP2
 * contains the second PRP element if the memory being described fits within 2
 * PRP entries, or a PRP list pointer if the PRP spans more than two entries.
 *
 * A PRP list pointer contains the address of a PRP list, structured as a linear
 * array of PRP entries.  Each PRP entry in this list describes a segment of
 * physical memory.
 *
 * Each 64-bit PRP entry comprises an address and an offset field.  The address
 * always points at the beginning of a 4KB physical memory page, and the offset
 * describes where within that 4KB page the memory segment begins.  Only the
 * first element in a PRP list may contain a non-zero offest, implying that all
 * memory segments following the first begin at the start of a 4KB page.
 *
 * Each PRP element normally describes 4KB of physical memory, with exceptions
 * for the first and last elements in the list.  If the memory being described
 * by the list begins at a non-zero offset within the first 4KB page, then the
 * first PRP element will contain a non-zero offset indicating where the region
 * begins within the 4KB page.  The last memory segment may end before the end
 * of the 4KB segment, depending upon the overall size of the memory being
 * described by the PRP list.
 *
 * Since PRP entries lack any indication of size, the overall data buffer length
 * is used to determine where the end of the data memory buffer is located, and
 * how many PRP entries are required to describe it.
 *
 * @ioc: per adapter object
 * @smid: system request message index for getting asscociated SGL
 * @nvme_encap_request: the NVMe request msg frame pointer
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 */
static void
_base_build_nvme_prp(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	int		prp_size = NVME_PRP_SIZE;
	__le64		*prp_entry, *prp1_entry, *prp2_entry;
	__le64		*prp_page;
	dma_addr_t	prp_entry_dma, prp_page_dma, dma_addr;
	u32		offset, entry_len;
	u32		page_mask_result, page_mask;
	size_t		length;
	struct mpt3sas_nvme_cmd *nvme_cmd =
		(void *)nvme_encap_request->NVMe_Command;

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any PRP.
	 */
	if (!data_in_sz && !data_out_sz)
		return;
	prp1_entry = &nvme_cmd->prp1;
	prp2_entry = &nvme_cmd->prp2;
	prp_entry = prp1_entry;
	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory.
	 */
	prp_page = (__le64 *)mpt3sas_base_get_pcie_sgl(ioc, smid);
	prp_page_dma = mpt3sas_base_get_pcie_sgl_dma(ioc, smid);

	/*
	 * Check if we are within 1 entry of a page boundary we don't
	 * want our first entry to be a PRP List entry.
	 */
	page_mask = ioc->page_size - 1;
	page_mask_result = (uintptr_t)((u8 *)prp_page + prp_size) & page_mask;
	if (!page_mask_result) {
		/* Bump up to next page boundary. */
		prp_page = (__le64 *)((u8 *)prp_page + prp_size);
		prp_page_dma = prp_page_dma + prp_size;
	}

	/*
	 * Set PRP physical pointer, which initially points to the current PRP
	 * DMA memory page.
	 */
	prp_entry_dma = prp_page_dma;

	/* Get physical address and length of the data buffer. */
	if (data_in_sz) {
		dma_addr = data_in_dma;
		length = data_in_sz;
	} else {
		dma_addr = data_out_dma;
		length = data_out_sz;
	}

	/* Loop while the length is not zero. */
	while (length) {
		/*
		 * Check if we need to put a list pointer here if we are at
		 * page boundary - prp_size (8 bytes).
		 */
		page_mask_result = (prp_entry_dma + prp_size) & page_mask;
		if (!page_mask_result) {
			/*
			 * This is the last entry in a PRP List, so we need to
			 * put a PRP list pointer here.  What this does is:
			 *   - bump the current memory pointer to the next
			 *     address, which will be the next full page.
			 *   - set the PRP Entry to point to that page.  This
			 *     is now the PRP List pointer.
			 *   - bump the PRP Entry pointer the start of the
			 *     next page.  Since all of this PRP memory is
			 *     contiguous, no need to get a new page - it's
			 *     just the next address.
			 */
			prp_entry_dma++;
			*prp_entry = cpu_to_le64(prp_entry_dma);
			prp_entry++;
		}

		/* Need to handle if entry will be part of a page. */
		offset = dma_addr & page_mask;
		entry_len = ioc->page_size - offset;

		if (prp_entry == prp1_entry) {
			/*
			 * Must fill in the first PRP pointer (PRP1) before
			 * moving on.
			 */
			*prp1_entry = cpu_to_le64(dma_addr);

			/*
			 * Now point to the second PRP entry within the
			 * command (PRP2).
			 */
			prp_entry = prp2_entry;
		} else if (prp_entry == prp2_entry) {
			/*
			 * Should the PRP2 entry be a PRP List pointer or just
			 * a regular PRP pointer?  If there is more than one
			 * more page of data, must use a PRP List pointer.
			 */
			if (length > ioc->page_size) {
				/*
				 * PRP2 will contain a PRP List pointer because
				 * more PRP's are needed with this command. The
				 * list will start at the beginning of the
				 * contiguous buffer.
				 */
				*prp2_entry = cpu_to_le64(prp_entry_dma);

				/*
				 * The next PRP Entry will be the start of the
				 * first PRP List.
				 */
				prp_entry = prp_page;
			} else {
				/*
				 * After this, the PRP Entries are complete.
				 * This command uses 2 PRP's and no PRP list.
				 */
				*prp2_entry = cpu_to_le64(dma_addr);
			}
		} else {
			/*
			 * Put entry in list and bump the addresses.
			 *
			 * After PRP1 and PRP2 are filled in, this will fill in
			 * all remaining PRP entries in a PRP List, one per
			 * each time through the loop.
			 */
			*prp_entry = cpu_to_le64(dma_addr);
			prp_entry++;
			prp_entry_dma++;
		}

		/*
		 * Bump the phys address of the command's data buffer by the
		 * entry_len.
		 */
		dma_addr += entry_len;

		/* Decrement length accounting for last partial page. */
		if (entry_len > length)
			length = 0;
		else
			length -= entry_len;
	}
}

/**
 * base_make_prp_nvme -
 * Prepare PRPs(Physical Region Page)- SGLs specific to NVMe drives only
 *
 * @ioc:		per adapter object
 * @scmd:		SCSI command from the mid-layer
 * @mpi_request:	mpi request
 * @smid:		msg Index
 * @sge_count:		scatter gather element count.
 *
 * Return:		true: PRPs are built
 *			false: IEEE SGLs needs to be built
 */
static void
base_make_prp_nvme(struct MPT3SAS_ADAPTER *ioc,
		struct scsi_cmnd *scmd,
		Mpi25SCSIIORequest_t *mpi_request,
		u16 smid, int sge_count)
{
	int sge_len, num_prp_in_chain = 0;
	Mpi25IeeeSgeChain64_t *main_chain_element, *ptr_first_sgl;
	__le64 *curr_buff;
	dma_addr_t msg_dma, sge_addr, offset;
	u32 page_mask, page_mask_result;
	struct scatterlist *sg_scmd;
	u32 first_prp_len;
	int data_len = scsi_bufflen(scmd);
	u32 nvme_pg_size;

	nvme_pg_size = max_t(u32, ioc->page_size, NVME_PRP_PAGE_SIZE);
	/*
	 * Nvme has a very convoluted prp format.  One prp is required
	 * for each page or partial page. Driver need to split up OS sg_list
	 * entries if it is longer than one page or cross a page
	 * boundary.  Driver also have to insert a PRP list pointer entry as
	 * the last entry in each physical page of the PRP list.
	 *
	 * NOTE: The first PRP "entry" is actually placed in the first
	 * SGL entry in the main message as IEEE 64 format.  The 2nd
	 * entry in the main message is the chain element, and the rest
	 * of the PRP entries are built in the contiguous pcie buffer.
	 */
	page_mask = nvme_pg_size - 1;

	/*
	 * Native SGL is needed.
	 * Put a chain element in main message frame that points to the first
	 * chain buffer.
	 *
	 * NOTE:  The ChainOffset field must be 0 when using a chain pointer to
	 *        a native SGL.
	 */

	/* Set main message chain element pointer */
	main_chain_element = (pMpi25IeeeSgeChain64_t)&mpi_request->SGL;
	/*
	 * For NVMe the chain element needs to be the 2nd SG entry in the main
	 * message.
	 */
	main_chain_element = (Mpi25IeeeSgeChain64_t *)
		((u8 *)main_chain_element + sizeof(MPI25_IEEE_SGE_CHAIN64));

	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory.  Normal chain buffers can't be used
	 * because each chain buffer would need to be the size of an OS
	 * page (4k).
	 */
	curr_buff = mpt3sas_base_get_pcie_sgl(ioc, smid);
	msg_dma = mpt3sas_base_get_pcie_sgl_dma(ioc, smid);

	main_chain_element->Address = cpu_to_le64(msg_dma);
	main_chain_element->NextChainOffset = 0;
	main_chain_element->Flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
			MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
			MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP;

	/* Build first prp, sge need not to be page aligned*/
	ptr_first_sgl = (pMpi25IeeeSgeChain64_t)&mpi_request->SGL;
	sg_scmd = scsi_sglist(scmd);
	sge_addr = sg_dma_address(sg_scmd);
	sge_len = sg_dma_len(sg_scmd);

	offset = sge_addr & page_mask;
	first_prp_len = nvme_pg_size - offset;

	ptr_first_sgl->Address = cpu_to_le64(sge_addr);
	ptr_first_sgl->Length = cpu_to_le32(first_prp_len);

	data_len -= first_prp_len;

	if (sge_len > first_prp_len) {
		sge_addr += first_prp_len;
		sge_len -= first_prp_len;
	} else if (data_len && (sge_len == first_prp_len)) {
		sg_scmd = sg_next(sg_scmd);
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	for (;;) {
		offset = sge_addr & page_mask;

		/* Put PRP pointer due to page boundary*/
		page_mask_result = (uintptr_t)(curr_buff + 1) & page_mask;
		if (unlikely(!page_mask_result)) {
			scmd_printk(KERN_NOTICE,
				scmd, "page boundary curr_buff: 0x%p\n",
				curr_buff);
			msg_dma += 8;
			*curr_buff = cpu_to_le64(msg_dma);
			curr_buff++;
			num_prp_in_chain++;
		}

		*curr_buff = cpu_to_le64(sge_addr);
		curr_buff++;
		msg_dma += 8;
		num_prp_in_chain++;

		sge_addr += nvme_pg_size;
		sge_len -= nvme_pg_size;
		data_len -= nvme_pg_size;

		if (data_len <= 0)
			break;

		if (sge_len > 0)
			continue;

		sg_scmd = sg_next(sg_scmd);
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	main_chain_element->Length =
		cpu_to_le32(num_prp_in_chain * sizeof(u64));
	return;
}

static bool
base_is_prp_possible(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device, struct scsi_cmnd *scmd, int sge_count)
{
	u32 data_length = 0;
	bool build_prp = true;

	data_length = scsi_bufflen(scmd);

	/* If Datalenth is <= 16K and number of SGE’s entries are <= 2
	 * we built IEEE SGL
	 */
	if ((data_length <= NVME_PRP_PAGE_SIZE*4) && (sge_count <= 2))
		build_prp = false;

	return build_prp;
}

/**
 * _base_check_pcie_native_sgl - This function is called for PCIe end devices to
 * determine if the driver needs to build a native SGL.  If so, that native
 * SGL is built in the special contiguous buffers allocated especially for
 * PCIe SGL creation.  If the driver will not build a native SGL, return
 * TRUE and a normal IEEE SGL will be built.  Currently this routine
 * supports NVMe.
 * @ioc: per adapter object
 * @mpi_request: mf request pointer
 * @smid: system request message index
 * @scmd: scsi command
 * @pcie_device: points to the PCIe device's info
 *
 * Return: 0 if native SGL was built, 1 if no SGL was built
 */
static int
_base_check_pcie_native_sgl(struct MPT3SAS_ADAPTER *ioc,
	Mpi25SCSIIORequest_t *mpi_request, u16 smid, struct scsi_cmnd *scmd,
	struct _pcie_device *pcie_device)
{
	int sges_left;

	/* Get the SG list pointer and info. */
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
			"scsi_dma_map failed: request for %d bytes!\n",
			scsi_bufflen(scmd));
		return 1;
	}

	/* Check if we need to build a native SG list. */
	if (base_is_prp_possible(ioc, pcie_device,
				scmd, sges_left) == 0) {
		/* We built a native SG list, just return. */
		goto out;
	}

	/*
	 * Build native NVMe PRP.
	 */
	base_make_prp_nvme(ioc, scmd, mpi_request,
			smid, sges_left);

	return 0;
out:
	scsi_dma_unmap(scmd);
	return 1;
}

/**
 * _base_add_sg_single_ieee - add sg element for IEEE format
 * @paddr: virtual address for SGE
 * @flags: SGE flags
 * @chain_offset: number of 128 byte elements from start of segment
 * @length: data transfer length
 * @dma_addr: Physical address
 */
static void
_base_add_sg_single_ieee(void *paddr, u8 flags, u8 chain_offset, u32 length,
	dma_addr_t dma_addr)
{
	Mpi25IeeeSgeChain64_t *sgel = paddr;

	sgel->Flags = flags;
	sgel->NextChainOffset = chain_offset;
	sgel->Length = cpu_to_le32(length);
	sgel->Address = cpu_to_le64(dma_addr);
}

/**
 * _base_build_zero_len_sge_ieee - build zero length sg entry for IEEE format
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 */
static void
_base_build_zero_len_sge_ieee(struct MPT3SAS_ADAPTER *ioc, void *paddr)
{
	u8 sgl_flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
		MPI25_IEEE_SGE_FLAGS_END_OF_LIST);

	_base_add_sg_single_ieee(paddr, sgl_flags, 0, 0, -1);
}

/**
 * _base_build_sg_scmd - main sg creation routine
 *		pcie_device is unused here!
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * @unused: unused pcie_device pointer
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Return: 0 success, anything else error
 */
static int
_base_build_sg_scmd(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid, struct _pcie_device *unused)
{
	Mpi2SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	u32 chain_flags;
	int sges_left;
	u32 sges_in_segment;
	u32 sgl_flags;
	u32 sgl_flags_last_element;
	u32 sgl_flags_end_buffer;
	struct chain_tracker *chain_req;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	sgl_flags = MPI2_SGE_FLAGS_SIMPLE_ELEMENT;
	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags_last_element = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags_end_buffer = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
		 "scsi_dma_map failed: request for %d bytes!\n",
		 scsi_bufflen(scmd));
		return -ENOMEM;
	}

	sg_local = &mpi_request->SGL;
	sges_in_segment = ioc->max_sges_in_main_message;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (offsetof(Mpi2SCSIIORequest_t, SGL) +
	    (sges_in_segment * ioc->sge_size))/4;

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment) {
		if (sges_in_segment == 1)
			ioc->base_add_sg_single(sg_local,
			    sgl_flags_last_element | sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the chain flags and pointers */
	chain_flags = MPI2_SGE_FLAGS_CHAIN_ELEMENT << MPI2_SGE_FLAGS_SHIFT;
	chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : (sges_in_segment * ioc->sge_size)/4;
		chain_length = sges_in_segment * ioc->sge_size;
		if (chain_offset) {
			chain_offset = chain_offset <<
			    MPI2_SGE_CHAIN_OFFSET_SHIFT;
			chain_length += ioc->sge_size;
		}
		ioc->base_add_sg_single(sg_local, chain_flags | chain_offset |
		    chain_length, chain_dma);
		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			if (sges_in_segment == 1)
				ioc->base_add_sg_single(sg_local,
				    sgl_flags_last_element |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			else
				ioc->base_add_sg_single(sg_local, sgl_flags |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			sg_scmd = sg_next(sg_scmd);
			sg_local += ioc->sge_size;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left) {
		if (sges_left == 1)
			ioc->base_add_sg_single(sg_local, sgl_flags_end_buffer |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
	}

	return 0;
}

/**
 * _base_build_sg_scmd_ieee - main sg creation routine for IEEE format
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * @pcie_device: Pointer to pcie_device. If set, the pcie native sgl will be
 * constructed on need.
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Return: 0 success, anything else error
 */
static int
_base_build_sg_scmd_ieee(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid, struct _pcie_device *pcie_device)
{
	Mpi25SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	int sges_left;
	u32 sges_in_segment;
	u8 simple_sgl_flags;
	u8 simple_sgl_flags_last;
	u8 chain_sgl_flags;
	struct chain_tracker *chain_req;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	simple_sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
	simple_sgl_flags_last = simple_sgl_flags |
	    MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
	chain_sgl_flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;

	/* Check if we need to build a native SG list. */
	if ((pcie_device) && (_base_check_pcie_native_sgl(ioc, mpi_request,
			smid, scmd, pcie_device) == 0)) {
		/* We built a native SG list, just return. */
		return 0;
	}

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
			"scsi_dma_map failed: request for %d bytes!\n",
			scsi_bufflen(scmd));
		return -ENOMEM;
	}

	sg_local = &mpi_request->SGL;
	sges_in_segment = (ioc->request_sz -
		   offsetof(Mpi25SCSIIORequest_t, SGL))/ioc->sge_size_ieee;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (sges_in_segment - 1 /* chain element */) +
	    (offsetof(Mpi25SCSIIORequest_t, SGL)/ioc->sge_size_ieee);

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment > 1) {
		_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
		    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size_ieee;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the pointers */
	chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : sges_in_segment;
		chain_length = sges_in_segment * ioc->sge_size_ieee;
		if (chain_offset)
			chain_length += ioc->sge_size_ieee;
		_base_add_sg_single_ieee(sg_local, chain_sgl_flags,
		    chain_offset, chain_length, chain_dma);

		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
			sg_scmd = sg_next(sg_scmd);
			sg_local += ioc->sge_size_ieee;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left > 0) {
		if (sges_left == 1)
			_base_add_sg_single_ieee(sg_local,
			    simple_sgl_flags_last, 0, sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size_ieee;
		sges_left--;
	}

	return 0;
}

/**
 * _base_build_sg_ieee - build generic sg for IEEE format
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 */
static void
_base_build_sg_ieee(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u8 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge_ieee(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size_ieee;

		/* READ sgel last */
		sgl_flags |= MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	}
}

#define convert_to_kb(x) ((x) << (PAGE_SHIFT - 10))

/**
 * _base_config_dma_addressing - set dma addressing
 * @ioc: per adapter object
 * @pdev: PCI device struct
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_config_dma_addressing(struct MPT3SAS_ADAPTER *ioc, struct pci_dev *pdev)
{
	u64 required_mask, coherent_mask;
	struct sysinfo s;

	if (ioc->is_mcpu_endpoint)
		goto try_32bit;

	required_mask = dma_get_required_mask(&pdev->dev);
	if (sizeof(dma_addr_t) == 4 || required_mask == 32)
		goto try_32bit;

	if (ioc->dma_mask)
		coherent_mask = DMA_BIT_MASK(64);
	else
		coherent_mask = DMA_BIT_MASK(32);

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) ||
	    dma_set_coherent_mask(&pdev->dev, coherent_mask))
		goto try_32bit;

	ioc->base_add_sg_single = &_base_add_sg_single_64;
	ioc->sge_size = sizeof(Mpi2SGESimple64_t);
	ioc->dma_mask = 64;
	goto out;

 try_32bit:
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)))
		return -ENODEV;

	ioc->base_add_sg_single = &_base_add_sg_single_32;
	ioc->sge_size = sizeof(Mpi2SGESimple32_t);
	ioc->dma_mask = 32;
 out:
	si_meminfo(&s);
	ioc_info(ioc, "%d BIT PCI BUS DMA ADDRESSING SUPPORTED, total mem (%ld kB)\n",
		 ioc->dma_mask, convert_to_kb(s.totalram));

	return 0;
}

static int
_base_change_consistent_dma_mask(struct MPT3SAS_ADAPTER *ioc,
				      struct pci_dev *pdev)
{
	if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
			return -ENODEV;
	}
	return 0;
}

/**
 * _base_check_enable_msix - checks MSIX capabable.
 * @ioc: per adapter object
 *
 * Check to see if card is capable of MSIX, and set number
 * of available msix vectors
 */
static int
_base_check_enable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	int base;
	u16 message_control;

	/* Check whether controller SAS2008 B0 controller,
	 * if it is SAS2008 B0 controller use IO-APIC instead of MSIX
	 */
	if (ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 &&
	    ioc->pdev->revision == SAS2_PCI_DEVICE_B0_REVISION) {
		return -EINVAL;
	}

	base = pci_find_capability(ioc->pdev, PCI_CAP_ID_MSIX);
	if (!base) {
		dfailprintk(ioc, ioc_info(ioc, "msix not supported\n"));
		return -EINVAL;
	}

	/* get msix vector count */
	/* NUMA_IO not supported for older controllers */
	if (ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2004 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_1 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_2 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_3 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2116_1 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2116_2)
		ioc->msix_vector_count = 1;
	else {
		pci_read_config_word(ioc->pdev, base + 2, &message_control);
		ioc->msix_vector_count = (message_control & 0x3FF) + 1;
	}
	dinitprintk(ioc, ioc_info(ioc, "msix is supported, vector_count(%d)\n",
				  ioc->msix_vector_count));
	return 0;
}

/**
 * _base_free_irq - free irq
 * @ioc: per adapter object
 *
 * Freeing respective reply_queue from the list.
 */
static void
_base_free_irq(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q, *next;

	if (list_empty(&ioc->reply_queue_list))
		return;

	list_for_each_entry_safe(reply_q, next, &ioc->reply_queue_list, list) {
		list_del(&reply_q->list);
		free_irq(pci_irq_vector(ioc->pdev, reply_q->msix_index),
			 reply_q);
		kfree(reply_q);
	}
}

/**
 * _base_request_irq - request irq
 * @ioc: per adapter object
 * @index: msix index into vector table
 *
 * Inserting respective reply_queue into the list.
 */
static int
_base_request_irq(struct MPT3SAS_ADAPTER *ioc, u8 index)
{
	struct pci_dev *pdev = ioc->pdev;
	struct adapter_reply_queue *reply_q;
	int r;

	reply_q =  kzalloc(sizeof(struct adapter_reply_queue), GFP_KERNEL);
	if (!reply_q) {
		ioc_err(ioc, "unable to allocate memory %zu!\n",
			sizeof(struct adapter_reply_queue));
		return -ENOMEM;
	}
	reply_q->ioc = ioc;
	reply_q->msix_index = index;

	atomic_set(&reply_q->busy, 0);
	if (ioc->msix_enable)
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d-msix%d",
		    ioc->driver_name, ioc->id, index);
	else
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d",
		    ioc->driver_name, ioc->id);
	r = request_irq(pci_irq_vector(pdev, index), _base_interrupt,
			IRQF_SHARED, reply_q->name, reply_q);
	if (r) {
		pr_err("%s: unable to allocate interrupt %d!\n",
		       reply_q->name, pci_irq_vector(pdev, index));
		kfree(reply_q);
		return -EBUSY;
	}

	INIT_LIST_HEAD(&reply_q->list);
	list_add_tail(&reply_q->list, &ioc->reply_queue_list);
	return 0;
}

/**
 * _base_assign_reply_queues - assigning msix index for each cpu
 * @ioc: per adapter object
 *
 * The enduser would need to set the affinity via /proc/irq/#/smp_affinity
 *
 * It would nice if we could call irq_set_affinity, however it is not
 * an exported symbol
 */
static void
_base_assign_reply_queues(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned int cpu, nr_cpus, nr_msix, index = 0;
	struct adapter_reply_queue *reply_q;

	if (!_base_is_controller_msix_enabled(ioc))
		return;

	memset(ioc->cpu_msix_table, 0, ioc->cpu_msix_table_sz);

	nr_cpus = num_online_cpus();
	nr_msix = ioc->reply_queue_count = min(ioc->reply_queue_count,
					       ioc->facts.MaxMSIxVectors);
	if (!nr_msix)
		return;

	if (smp_affinity_enable) {
		list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
			const cpumask_t *mask = pci_irq_get_affinity(ioc->pdev,
							reply_q->msix_index);
			if (!mask) {
				ioc_warn(ioc, "no affinity for msi %x\n",
					 reply_q->msix_index);
				continue;
			}

			for_each_cpu_and(cpu, mask, cpu_online_mask) {
				if (cpu >= ioc->cpu_msix_table_sz)
					break;
				ioc->cpu_msix_table[cpu] = reply_q->msix_index;
			}
		}
		return;
	}
	cpu = cpumask_first(cpu_online_mask);

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {

		unsigned int i, group = nr_cpus / nr_msix;

		if (cpu >= nr_cpus)
			break;

		if (index < nr_cpus % nr_msix)
			group++;

		for (i = 0 ; i < group ; i++) {
			ioc->cpu_msix_table[cpu] = reply_q->msix_index;
			cpu = cpumask_next(cpu, cpu_online_mask);
		}
		index++;
	}
}

/**
 * _base_disable_msix - disables msix
 * @ioc: per adapter object
 *
 */
static void
_base_disable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	if (!ioc->msix_enable)
		return;
	pci_disable_msix(ioc->pdev);
	ioc->msix_enable = 0;
}

/**
 * _base_enable_msix - enables msix, failback to io_apic
 * @ioc: per adapter object
 *
 */
static int
_base_enable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	int r;
	int i, local_max_msix_vectors;
	u8 try_msix = 0;
	unsigned int irq_flags = PCI_IRQ_MSIX;

	if (msix_disable == -1 || msix_disable == 0)
		try_msix = 1;

	if (!try_msix)
		goto try_ioapic;

	if (_base_check_enable_msix(ioc) != 0)
		goto try_ioapic;

	ioc->reply_queue_count = min_t(int, ioc->cpu_count,
		ioc->msix_vector_count);

	ioc_info(ioc, "MSI-X vectors supported: %d, no of cores: %d, max_msix_vectors: %d\n",
		 ioc->msix_vector_count, ioc->cpu_count, max_msix_vectors);

	if (!ioc->rdpq_array_enable && max_msix_vectors == -1)
		local_max_msix_vectors = (reset_devices) ? 1 : 8;
	else
		local_max_msix_vectors = max_msix_vectors;

	if (local_max_msix_vectors > 0)
		ioc->reply_queue_count = min_t(int, local_max_msix_vectors,
			ioc->reply_queue_count);
	else if (local_max_msix_vectors == 0)
		goto try_ioapic;

	if (ioc->msix_vector_count < ioc->cpu_count)
		smp_affinity_enable = 0;

	if (smp_affinity_enable)
		irq_flags |= PCI_IRQ_AFFINITY;

	r = pci_alloc_irq_vectors(ioc->pdev, 1, ioc->reply_queue_count,
				  irq_flags);
	if (r < 0) {
		dfailprintk(ioc,
			    ioc_info(ioc, "pci_alloc_irq_vectors failed (r=%d) !!!\n",
				     r));
		goto try_ioapic;
	}

	ioc->msix_enable = 1;
	ioc->reply_queue_count = r;
	for (i = 0; i < ioc->reply_queue_count; i++) {
		r = _base_request_irq(ioc, i);
		if (r) {
			_base_free_irq(ioc);
			_base_disable_msix(ioc);
			goto try_ioapic;
		}
	}

	return 0;

/* failback to io_apic interrupt routing */
 try_ioapic:

	ioc->reply_queue_count = 1;
	r = pci_alloc_irq_vectors(ioc->pdev, 1, 1, PCI_IRQ_LEGACY);
	if (r < 0) {
		dfailprintk(ioc,
			    ioc_info(ioc, "pci_alloc_irq_vector(legacy) failed (r=%d) !!!\n",
				     r));
	} else
		r = _base_request_irq(ioc, 0);

	return r;
}

/**
 * mpt3sas_base_unmap_resources - free controller resources
 * @ioc: per adapter object
 */
static void
mpt3sas_base_unmap_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;

	dexitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	_base_free_irq(ioc);
	_base_disable_msix(ioc);

	kfree(ioc->replyPostRegisterIndex);
	ioc->replyPostRegisterIndex = NULL;


	if (ioc->chip_phys) {
		iounmap(ioc->chip);
		ioc->chip_phys = 0;
	}

	if (pci_is_enabled(pdev)) {
		pci_release_selected_regions(ioc->pdev, ioc->bars);
		pci_disable_pcie_error_reporting(pdev);
		pci_disable_device(pdev);
	}
}

/**
 * mpt3sas_base_map_resources - map in controller resources (io/irq/memap)
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_base_map_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;
	u32 memap_sz;
	u32 pio_sz;
	int i, r = 0;
	u64 pio_chip = 0;
	phys_addr_t chip_phys = 0;
	struct adapter_reply_queue *reply_q;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	ioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_enable_device_mem(pdev)) {
		ioc_warn(ioc, "pci_enable_device_mem: failed\n");
		ioc->bars = 0;
		return -ENODEV;
	}


	if (pci_request_selected_regions(pdev, ioc->bars,
	    ioc->driver_name)) {
		ioc_warn(ioc, "pci_request_selected_regions: failed\n");
		ioc->bars = 0;
		r = -ENODEV;
		goto out_fail;
	}

/* AER (Advanced Error Reporting) hooks */
	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);


	if (_base_config_dma_addressing(ioc, pdev) != 0) {
		ioc_warn(ioc, "no suitable DMA mask for %s\n", pci_name(pdev));
		r = -ENODEV;
		goto out_fail;
	}

	for (i = 0, memap_sz = 0, pio_sz = 0; (i < DEVICE_COUNT_RESOURCE) &&
	     (!memap_sz || !pio_sz); i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			if (pio_sz)
				continue;
			pio_chip = (u64)pci_resource_start(pdev, i);
			pio_sz = pci_resource_len(pdev, i);
		} else if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			if (memap_sz)
				continue;
			ioc->chip_phys = pci_resource_start(pdev, i);
			chip_phys = ioc->chip_phys;
			memap_sz = pci_resource_len(pdev, i);
			ioc->chip = ioremap(ioc->chip_phys, memap_sz);
		}
	}

	if (ioc->chip == NULL) {
		ioc_err(ioc, "unable to map adapter memory! or resource not found\n");
		r = -EINVAL;
		goto out_fail;
	}

	_base_mask_interrupts(ioc);

	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out_fail;

	if (!ioc->rdpq_array_enable_assigned) {
		ioc->rdpq_array_enable = ioc->rdpq_array_capable;
		ioc->rdpq_array_enable_assigned = 1;
	}

	r = _base_enable_msix(ioc);
	if (r)
		goto out_fail;

	/* Use the Combined reply queue feature only for SAS3 C0 & higher
	 * revision HBAs and also only when reply queue count is greater than 8
	 */
	if (ioc->combined_reply_queue) {
		/* Determine the Supplemental Reply Post Host Index Registers
		 * Addresse. Supplemental Reply Post Host Index Registers
		 * starts at offset MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET and
		 * each register is at offset bytes of
		 * MPT3_SUP_REPLY_POST_HOST_INDEX_REG_OFFSET from previous one.
		 */
		ioc->replyPostRegisterIndex = kcalloc(
		     ioc->combined_reply_index_count,
		     sizeof(resource_size_t *), GFP_KERNEL);
		if (!ioc->replyPostRegisterIndex) {
			dfailprintk(ioc,
				    ioc_warn(ioc, "allocation for reply Post Register Index failed!!!\n"));
			r = -ENOMEM;
			goto out_fail;
		}

		for (i = 0; i < ioc->combined_reply_index_count; i++) {
			ioc->replyPostRegisterIndex[i] = (resource_size_t *)
			     ((u8 __force *)&ioc->chip->Doorbell +
			     MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET +
			     (i * MPT3_SUP_REPLY_POST_HOST_INDEX_REG_OFFSET));
		}
	}

	if (ioc->is_warpdrive) {
		ioc->reply_post_host_index[0] = (resource_size_t __iomem *)
		    &ioc->chip->ReplyPostHostIndex;

		for (i = 1; i < ioc->cpu_msix_table_sz; i++)
			ioc->reply_post_host_index[i] =
			(resource_size_t __iomem *)
			((u8 __iomem *)&ioc->chip->Doorbell + (0x4000 + ((i - 1)
			* 4)));
	}

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list)
		pr_info("%s: %s enabled: IRQ %d\n",
			reply_q->name,
			ioc->msix_enable ? "PCI-MSI-X" : "IO-APIC",
			pci_irq_vector(ioc->pdev, reply_q->msix_index));

	ioc_info(ioc, "iomem(%pap), mapped(0x%p), size(%d)\n",
		 &chip_phys, ioc->chip, memap_sz);
	ioc_info(ioc, "ioport(0x%016llx), size(%d)\n",
		 (unsigned long long)pio_chip, pio_sz);

	/* Save PCI configuration state for recovery from PCI AER/EEH errors */
	pci_save_state(pdev);
	return 0;

 out_fail:
	mpt3sas_base_unmap_resources(ioc);
	return r;
}

/**
 * mpt3sas_base_get_msg_frame - obtain request mf pointer
 * @ioc: per adapter object
 * @smid: system request message index(smid zero is invalid)
 *
 * Return: virt pointer to message frame.
 */
void *
mpt3sas_base_get_msg_frame(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->request + (smid * ioc->request_sz));
}

/**
 * mpt3sas_base_get_sense_buffer - obtain a sense buffer virt addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: virt pointer to sense buffer.
 */
void *
mpt3sas_base_get_sense_buffer(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->sense + ((smid - 1) * SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_sense_buffer_dma - obtain a sense buffer dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: phys pointer to the low 32bit address of the sense buffer.
 */
__le32
mpt3sas_base_get_sense_buffer_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return cpu_to_le32(ioc->sense_dma + ((smid - 1) *
	    SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_pcie_sgl - obtain a PCIe SGL virt addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: virt pointer to a PCIe SGL.
 */
void *
mpt3sas_base_get_pcie_sgl(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->pcie_sg_lookup[smid - 1].pcie_sgl);
}

/**
 * mpt3sas_base_get_pcie_sgl_dma - obtain a PCIe SGL dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: phys pointer to the address of the PCIe buffer.
 */
dma_addr_t
mpt3sas_base_get_pcie_sgl_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->pcie_sg_lookup[smid - 1].pcie_sgl_dma;
}

/**
 * mpt3sas_base_get_reply_virt_addr - obtain reply frames virt address
 * @ioc: per adapter object
 * @phys_addr: lower 32 physical addr of the reply
 *
 * Converts 32bit lower physical addr into a virt address.
 */
void *
mpt3sas_base_get_reply_virt_addr(struct MPT3SAS_ADAPTER *ioc, u32 phys_addr)
{
	if (!phys_addr)
		return NULL;
	return ioc->reply + (phys_addr - (u32)ioc->reply_dma);
}

static inline u8
_base_get_msix_index(struct MPT3SAS_ADAPTER *ioc)
{
	return ioc->cpu_msix_table[raw_smp_processor_id()];
}

/**
 * mpt3sas_base_get_smid - obtain a free smid from internal queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Return: smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->internal_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		ioc_err(ioc, "%s: smid not available\n", __func__);
		return 0;
	}

	request = list_entry(ioc->internal_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * mpt3sas_base_get_smid_scsiio - obtain a free smid from scsiio queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 * @scmd: pointer to scsi command object
 *
 * Return: smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_scsiio(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx,
	struct scsi_cmnd *scmd)
{
	struct scsiio_tracker *request = scsi_cmd_priv(scmd);
	unsigned int tag = scmd->request->tag;
	u16 smid;

	smid = tag + 1;
	request->cb_idx = cb_idx;
	request->msix_io = _base_get_msix_index(ioc);
	request->smid = smid;
	INIT_LIST_HEAD(&request->chain_list);
	return smid;
}

/**
 * mpt3sas_base_get_smid_hpr - obtain a free smid from hi-priority queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Return: smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_hpr(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->hpr_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		return 0;
	}

	request = list_entry(ioc->hpr_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

static void
_base_recovery_check(struct MPT3SAS_ADAPTER *ioc)
{
	/*
	 * See _wait_for_commands_to_complete() call with regards to this code.
	 */
	if (ioc->shost_recovery && ioc->pending_io_count) {
		ioc->pending_io_count = scsi_host_busy(ioc->shost);
		if (ioc->pending_io_count == 0)
			wake_up(&ioc->reset_wq);
	}
}

void mpt3sas_base_clear_st(struct MPT3SAS_ADAPTER *ioc,
			   struct scsiio_tracker *st)
{
	if (WARN_ON(st->smid == 0))
		return;
	st->cb_idx = 0xFF;
	st->direct_io = 0;
	atomic_set(&ioc->chain_lookup[st->smid - 1].chain_offset, 0);
	st->smid = 0;
}

/**
 * mpt3sas_base_free_smid - put smid back on free_list
 * @ioc: per adapter object
 * @smid: system request message index
 */
void
mpt3sas_base_free_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	unsigned long flags;
	int i;

	if (smid < ioc->hi_priority_smid) {
		struct scsiio_tracker *st;
		void *request;

		st = _get_st_from_smid(ioc, smid);
		if (!st) {
			_base_recovery_check(ioc);
			return;
		}

		/* Clear MPI request frame */
		request = mpt3sas_base_get_msg_frame(ioc, smid);
		memset(request, 0, ioc->request_sz);

		mpt3sas_base_clear_st(ioc, st);
		_base_recovery_check(ioc);
		return;
	}

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (smid < ioc->internal_smid) {
		/* hi-priority */
		i = smid - ioc->hi_priority_smid;
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->hpr_lookup[i].tracker_list, &ioc->hpr_free_list);
	} else if (smid <= ioc->hba_queue_depth) {
		/* internal queue */
		i = smid - ioc->internal_smid;
		ioc->internal_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
}

/**
 * _base_mpi_ep_writeq - 32 bit write to MMIO
 * @b: data payload
 * @addr: address in MMIO space
 * @writeq_lock: spin lock
 *
 * This special handling for MPI EP to take care of 32 bit
 * environment where its not quarenteed to send the entire word
 * in one transfer.
 */
static inline void
_base_mpi_ep_writeq(__u64 b, volatile void __iomem *addr,
					spinlock_t *writeq_lock)
{
	unsigned long flags;

	spin_lock_irqsave(writeq_lock, flags);
	__raw_writel((u32)(b), addr);
	__raw_writel((u32)(b >> 32), (addr + 4));
	mmiowb();
	spin_unlock_irqrestore(writeq_lock, flags);
}

/**
 * _base_writeq - 64 bit write to MMIO
 * @b: data payload
 * @addr: address in MMIO space
 * @writeq_lock: spin lock
 *
 * Glue for handling an atomic 64 bit word to MMIO. This special handling takes
 * care of 32 bit environment where its not quarenteed to send the entire word
 * in one transfer.
 */
#if defined(writeq) && defined(CONFIG_64BIT)
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	wmb();
	__raw_writeq(b, addr);
	barrier();
}
#else
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	_base_mpi_ep_writeq(b, addr, writeq_lock);
}
#endif

/**
 * _base_put_smid_mpi_ep_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 */
static void
_base_put_smid_mpi_ep_scsi_io(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;
	void *mpi_req_iomem;
	__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);

	_clone_sg_entries(ioc, (void *) mfp, smid);
	mpi_req_iomem = (void __force *)ioc->chip +
			MPI_FRAME_START_OFFSET + (smid * ioc->request_sz);
	_base_clone_mpi_to_sys_mem(mpi_req_iomem, (void *)mfp,
					ioc->request_sz);
	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_mpi_ep_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 */
static void
_base_put_smid_scsi_io(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;


	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * mpt3sas_base_put_smid_fast_path - send fast path request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 */
void
mpt3sas_base_put_smid_fast_path(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSIIO.RequestFlags =
	    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex = _base_get_msix_index(ioc);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * mpt3sas_base_put_smid_hi_priority - send Task Management request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_task: msix_task will be same as msix of IO incase of task abort else 0.
 */
void
mpt3sas_base_put_smid_hi_priority(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 msix_task)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	void *mpi_req_iomem;
	u64 *request;

	if (ioc->is_mcpu_endpoint) {
		__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);

		/* TBD 256 is offset within sys register. */
		mpi_req_iomem = (void __force *)ioc->chip
					+ MPI_FRAME_START_OFFSET
					+ (smid * ioc->request_sz);
		_base_clone_mpi_to_sys_mem(mpi_req_iomem, (void *)mfp,
							ioc->request_sz);
	}

	request = (u64 *)&descriptor;

	descriptor.HighPriority.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.HighPriority.MSIxIndex =  msix_task;
	descriptor.HighPriority.SMID = cpu_to_le16(smid);
	descriptor.HighPriority.LMID = 0;
	descriptor.HighPriority.Reserved1 = 0;
	if (ioc->is_mcpu_endpoint)
		_base_mpi_ep_writeq(*request,
				&ioc->chip->RequestDescriptorPostLow,
				&ioc->scsi_lookup_lock);
	else
		_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
		    &ioc->scsi_lookup_lock);
}

/**
 * mpt3sas_base_put_smid_nvme_encap - send NVMe encapsulated request to
 *  firmware
 * @ioc: per adapter object
 * @smid: system request message index
 */
void
mpt3sas_base_put_smid_nvme_encap(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.Default.RequestFlags =
		MPI26_REQ_DESCRIPT_FLAGS_PCIE_ENCAPSULATED;
	descriptor.Default.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * mpt3sas_base_put_smid_default - Default, primarily used for config pages
 * @ioc: per adapter object
 * @smid: system request message index
 */
void
mpt3sas_base_put_smid_default(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	void *mpi_req_iomem;
	u64 *request;

	if (ioc->is_mcpu_endpoint) {
		__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);

		_clone_sg_entries(ioc, (void *) mfp, smid);
		/* TBD 256 is offset within sys register */
		mpi_req_iomem = (void __force *)ioc->chip +
			MPI_FRAME_START_OFFSET + (smid * ioc->request_sz);
		_base_clone_mpi_to_sys_mem(mpi_req_iomem, (void *)mfp,
							ioc->request_sz);
	}
	request = (u64 *)&descriptor;
	descriptor.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.Default.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	if (ioc->is_mcpu_endpoint)
		_base_mpi_ep_writeq(*request,
				&ioc->chip->RequestDescriptorPostLow,
				&ioc->scsi_lookup_lock);
	else
		_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
				&ioc->scsi_lookup_lock);
}

/**
 * _base_display_OEMs_branding - Display branding string
 * @ioc: per adapter object
 */
static void
_base_display_OEMs_branding(struct MPT3SAS_ADAPTER *ioc)
{
	if (ioc->pdev->subsystem_vendor != PCI_VENDOR_ID_INTEL)
		return;

	switch (ioc->pdev->subsystem_vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RMS2LL080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS2LL080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS2LL040_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS2LL040_BRANDING);
				break;
			case MPT2SAS_INTEL_SSD910_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_SSD910_BRANDING);
				break;
			default:
				ioc_info(ioc, "Intel(R) Controller: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RS25GB008_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RS25GB008_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25JB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB040_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25JB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25KB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB040_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25KB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB040_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25LB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_INTEL_RMS25LB080_BRANDING);
				break;
			default:
				ioc_info(ioc, "Intel(R) Controller: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_INTEL_RMS3JC080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_INTEL_RMS3JC080_BRANDING);
				break;

			case MPT3SAS_INTEL_RS3GC008_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_INTEL_RS3GC008_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3FC044_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_INTEL_RS3FC044_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3UC080_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_INTEL_RS3UC080_BRANDING);
				break;
			default:
				ioc_info(ioc, "Intel(R) Controller: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			ioc_info(ioc, "Intel(R) Controller: Subsystem ID: 0x%X\n",
				 ioc->pdev->subsystem_device);
			break;
		}
		break;
	case PCI_VENDOR_ID_DELL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_MODULAR_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_PERC_H200_BRANDING);
				break;
			case MPT2SAS_DELL_6GBPS_SAS_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_DELL_6GBPS_SAS_BRANDING);
				break;
			default:
				ioc_info(ioc, "Dell 6Gbps HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_DELL_12G_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_DELL_12G_HBA_BRANDING);
				break;
			default:
				ioc_info(ioc, "Dell 12Gbps HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			ioc_info(ioc, "Dell HBA: Subsystem ID: 0x%X\n",
				 ioc->pdev->subsystem_device);
			break;
		}
		break;
	case PCI_VENDOR_ID_CISCO:
		switch (ioc->pdev->device) {
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_8E_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_CISCO_12G_8E_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_8I_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_CISCO_12G_8I_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_AVILA_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING);
				break;
			default:
				ioc_info(ioc, "Cisco 12Gbps SAS HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3108_1:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_AVILA_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_BRANDING);
				break;
			default:
				ioc_info(ioc, "Cisco 12Gbps SAS HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			ioc_info(ioc, "Cisco SAS HBA: Subsystem ID: 0x%X\n",
				 ioc->pdev->subsystem_device);
			break;
		}
		break;
	case MPT2SAS_HP_3PAR_SSVID:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2004:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_BRANDING);
				break;
			default:
				ioc_info(ioc, "HP 6Gbps SAS HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_2_4_INTERNAL_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_HP_2_4_INTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_2_4_EXTERNAL_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_HP_2_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID:
				ioc_info(ioc, "%s\n",
					 MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_BRANDING);
				break;
			default:
				ioc_info(ioc, "HP 6Gbps SAS HBA: Subsystem ID: 0x%X\n",
					 ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			ioc_info(ioc, "HP SAS HBA: Subsystem ID: 0x%X\n",
				 ioc->pdev->subsystem_device);
			break;
		}
	default:
		break;
	}
}

/**
 * _base_display_fwpkg_version - sends FWUpload request to pull FWPkg
 *				version from FW Image Header.
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
	static int
_base_display_fwpkg_version(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2FWImageHeader_t *FWImgHdr;
	Mpi25FWUploadRequest_t *mpi_request;
	Mpi2FWUploadReply_t mpi_reply;
	int r = 0;
	void *fwpkg_data = NULL;
	dma_addr_t fwpkg_data_dma;
	u16 smid, ioc_status;
	size_t data_length;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		ioc_err(ioc, "%s: internal command already in use\n", __func__);
		return -EAGAIN;
	}

	data_length = sizeof(Mpi2FWImageHeader_t);
	fwpkg_data = dma_alloc_coherent(&ioc->pdev->dev, data_length,
			&fwpkg_data_dma, GFP_KERNEL);
	if (!fwpkg_data) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -ENOMEM;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		r = -EAGAIN;
		goto out;
	}

	ioc->base_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi25FWUploadRequest_t));
	mpi_request->Function = MPI2_FUNCTION_FW_UPLOAD;
	mpi_request->ImageType = MPI2_FW_UPLOAD_ITYPE_FW_FLASH;
	mpi_request->ImageSize = cpu_to_le32(data_length);
	ioc->build_sg(ioc, &mpi_request->SGL, 0, 0, fwpkg_data_dma,
			data_length);
	init_completion(&ioc->base_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	/* Wait for 15 seconds */
	wait_for_completion_timeout(&ioc->base_cmds.done,
			FW_IMG_HDR_READ_TIMEOUT*HZ);
	ioc_info(ioc, "%s: complete\n", __func__);
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		ioc_err(ioc, "%s: timeout\n", __func__);
		_debug_dump_mf(mpi_request,
				sizeof(Mpi25FWUploadRequest_t)/4);
		r = -ETIME;
	} else {
		memset(&mpi_reply, 0, sizeof(Mpi2FWUploadReply_t));
		if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID) {
			memcpy(&mpi_reply, ioc->base_cmds.reply,
					sizeof(Mpi2FWUploadReply_t));
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
						MPI2_IOCSTATUS_MASK;
			if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
				FWImgHdr = (Mpi2FWImageHeader_t *)fwpkg_data;
				if (FWImgHdr->PackageVersion.Word) {
					ioc_info(ioc, "FW Package Version (%02d.%02d.%02d.%02d)\n",
						 FWImgHdr->PackageVersion.Struct.Major,
						 FWImgHdr->PackageVersion.Struct.Minor,
						 FWImgHdr->PackageVersion.Struct.Unit,
						 FWImgHdr->PackageVersion.Struct.Dev);
				}
			} else {
				_debug_dump_mf(&mpi_reply,
						sizeof(Mpi2FWUploadReply_t)/4);
			}
		}
	}
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
out:
	if (fwpkg_data)
		dma_free_coherent(&ioc->pdev->dev, data_length, fwpkg_data,
				fwpkg_data_dma);
	return r;
}

/**
 * _base_display_ioc_capabilities - Disply IOC's capabilities.
 * @ioc: per adapter object
 */
static void
_base_display_ioc_capabilities(struct MPT3SAS_ADAPTER *ioc)
{
	int i = 0;
	char desc[16];
	u32 iounit_pg1_flags;
	u32 bios_version;

	bios_version = le32_to_cpu(ioc->bios_pg3.BiosVersion);
	strncpy(desc, ioc->manu_pg0.ChipName, 16);
	ioc_info(ioc, "%s: FWVersion(%02d.%02d.%02d.%02d), ChipRevision(0x%02x), BiosVersion(%02d.%02d.%02d.%02d)\n",
		 desc,
		 (ioc->facts.FWVersion.Word & 0xFF000000) >> 24,
		 (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16,
		 (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8,
		 ioc->facts.FWVersion.Word & 0x000000FF,
		 ioc->pdev->revision,
		 (bios_version & 0xFF000000) >> 24,
		 (bios_version & 0x00FF0000) >> 16,
		 (bios_version & 0x0000FF00) >> 8,
		 bios_version & 0x000000FF);

	_base_display_OEMs_branding(ioc);

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_NVME_DEVICES) {
		pr_info("%sNVMe", i ? "," : "");
		i++;
	}

	ioc_info(ioc, "Protocol=(");

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR) {
		pr_cont("Initiator");
		i++;
	}

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_TARGET) {
		pr_cont("%sTarget", i ? "," : "");
		i++;
	}

	i = 0;
	pr_cont("), Capabilities=(");

	if (!ioc->hide_ir_msg) {
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID) {
			pr_cont("Raid");
			i++;
		}
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR) {
		pr_cont("%sTLR", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_MULTICAST) {
		pr_cont("%sMulticast", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET) {
		pr_cont("%sBIDI Target", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_EEDP) {
		pr_cont("%sEEDP", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER) {
		pr_cont("%sSnapshot Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER) {
		pr_cont("%sDiag Trace Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER) {
		pr_cont("%sDiag Extended Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING) {
		pr_cont("%sTask Set Full", i ? "," : "");
		i++;
	}

	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if (!(iounit_pg1_flags & MPI2_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE)) {
		pr_cont("%sNCQ", i ? "," : "");
		i++;
	}

	pr_cont(")\n");
}

/**
 * mpt3sas_base_update_missing_delay - change the missing delay timers
 * @ioc: per adapter object
 * @device_missing_delay: amount of time till device is reported missing
 * @io_missing_delay: interval IO is returned when there is a missing device
 *
 * Passed on the command line, this function will modify the device missing
 * delay, as well as the io missing delay. This should be called at driver
 * load time.
 */
void
mpt3sas_base_update_missing_delay(struct MPT3SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay)
{
	u16 dmd, dmd_new, dmd_orignal;
	u8 io_missing_delay_original;
	u16 sz;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2ConfigReply_t mpi_reply;
	u8 num_phys = 0;
	u16 ioc_status;

	mpt3sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys)
		return;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt3sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}

	/* device missing delay */
	dmd = sas_iounit_pg1->ReportDeviceMissingDelay;
	if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		dmd = (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		dmd = dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
	dmd_orignal = dmd;
	if (device_missing_delay > 0x7F) {
		dmd = (device_missing_delay > 0x7F0) ? 0x7F0 :
		    device_missing_delay;
		dmd = dmd / 16;
		dmd |= MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16;
	} else
		dmd = device_missing_delay;
	sas_iounit_pg1->ReportDeviceMissingDelay = dmd;

	/* io missing delay */
	io_missing_delay_original = sas_iounit_pg1->IODeviceMissingDelay;
	sas_iounit_pg1->IODeviceMissingDelay = io_missing_delay;

	if (!mpt3sas_config_set_sas_iounit_pg1(ioc, &mpi_reply, sas_iounit_pg1,
	    sz)) {
		if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
			dmd_new = (dmd &
			    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
		else
			dmd_new =
		    dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
		ioc_info(ioc, "device_missing_delay: old(%d), new(%d)\n",
			 dmd_orignal, dmd_new);
		ioc_info(ioc, "ioc_missing_delay: old(%d), new(%d)\n",
			 io_missing_delay_original,
			 io_missing_delay);
		ioc->device_missing_delay = dmd_new;
		ioc->io_missing_delay = io_missing_delay;
	}

out:
	kfree(sas_iounit_pg1);
}

/**
 * _base_static_config_pages - static start of day config pages
 * @ioc: per adapter object
 */
static void
_base_static_config_pages(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ConfigReply_t mpi_reply;
	u32 iounit_pg1_flags;

	ioc->nvme_abort_timeout = 30;
	mpt3sas_config_get_manufacturing_pg0(ioc, &mpi_reply, &ioc->manu_pg0);
	if (ioc->ir_firmware)
		mpt3sas_config_get_manufacturing_pg10(ioc, &mpi_reply,
		    &ioc->manu_pg10);

	/*
	 * Ensure correct T10 PI operation if vendor left EEDPTagMode
	 * flag unset in NVDATA.
	 */
	mpt3sas_config_get_manufacturing_pg11(ioc, &mpi_reply, &ioc->manu_pg11);
	if (!ioc->is_gen35_ioc && ioc->manu_pg11.EEDPTagMode == 0) {
		pr_err("%s: overriding NVDATA EEDPTagMode setting\n",
		    ioc->name);
		ioc->manu_pg11.EEDPTagMode &= ~0x3;
		ioc->manu_pg11.EEDPTagMode |= 0x1;
		mpt3sas_config_set_manufacturing_pg11(ioc, &mpi_reply,
		    &ioc->manu_pg11);
	}
	if (ioc->manu_pg11.AddlFlags2 & NVME_TASK_MNGT_CUSTOM_MASK)
		ioc->tm_custom_handling = 1;
	else {
		ioc->tm_custom_handling = 0;
		if (ioc->manu_pg11.NVMeAbortTO < NVME_TASK_ABORT_MIN_TIMEOUT)
			ioc->nvme_abort_timeout = NVME_TASK_ABORT_MIN_TIMEOUT;
		else if (ioc->manu_pg11.NVMeAbortTO >
					NVME_TASK_ABORT_MAX_TIMEOUT)
			ioc->nvme_abort_timeout = NVME_TASK_ABORT_MAX_TIMEOUT;
		else
			ioc->nvme_abort_timeout = ioc->manu_pg11.NVMeAbortTO;
	}

	mpt3sas_config_get_bios_pg2(ioc, &mpi_reply, &ioc->bios_pg2);
	mpt3sas_config_get_bios_pg3(ioc, &mpi_reply, &ioc->bios_pg3);
	mpt3sas_config_get_ioc_pg8(ioc, &mpi_reply, &ioc->ioc_pg8);
	mpt3sas_config_get_iounit_pg0(ioc, &mpi_reply, &ioc->iounit_pg0);
	mpt3sas_config_get_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);
	mpt3sas_config_get_iounit_pg8(ioc, &mpi_reply, &ioc->iounit_pg8);
	_base_display_ioc_capabilities(ioc);

	/*
	 * Enable task_set_full handling in iounit_pg1 when the
	 * facts capabilities indicate that its supported.
	 */
	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if ((ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING))
		iounit_pg1_flags &=
		    ~MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	else
		iounit_pg1_flags |=
		    MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	ioc->iounit_pg1.Flags = cpu_to_le32(iounit_pg1_flags);
	mpt3sas_config_set_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);

	if (ioc->iounit_pg8.NumSensors)
		ioc->temp_sensors_count = ioc->iounit_pg8.NumSensors;
}

/**
 * mpt3sas_free_enclosure_list - release memory
 * @ioc: per adapter object
 *
 * Free memory allocated during encloure add.
 */
void
mpt3sas_free_enclosure_list(struct MPT3SAS_ADAPTER *ioc)
{
	struct _enclosure_node *enclosure_dev, *enclosure_dev_next;

	/* Free enclosure list */
	list_for_each_entry_safe(enclosure_dev,
			enclosure_dev_next, &ioc->enclosure_list, list) {
		list_del(&enclosure_dev->list);
		kfree(enclosure_dev);
	}
}

/**
 * _base_release_memory_pools - release memory
 * @ioc: per adapter object
 *
 * Free memory allocated from _base_allocate_memory_pools.
 */
static void
_base_release_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	int i = 0;
	int j = 0;
	struct chain_tracker *ct;
	struct reply_post_struct *rps;

	dexitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	if (ioc->request) {
		dma_free_coherent(&ioc->pdev->dev, ioc->request_dma_sz,
		    ioc->request,  ioc->request_dma);
		dexitprintk(ioc,
			    ioc_info(ioc, "request_pool(0x%p): free\n",
				     ioc->request));
		ioc->request = NULL;
	}

	if (ioc->sense) {
		dma_pool_free(ioc->sense_dma_pool, ioc->sense, ioc->sense_dma);
		dma_pool_destroy(ioc->sense_dma_pool);
		dexitprintk(ioc,
			    ioc_info(ioc, "sense_pool(0x%p): free\n",
				     ioc->sense));
		ioc->sense = NULL;
	}

	if (ioc->reply) {
		dma_pool_free(ioc->reply_dma_pool, ioc->reply, ioc->reply_dma);
		dma_pool_destroy(ioc->reply_dma_pool);
		dexitprintk(ioc,
			    ioc_info(ioc, "reply_pool(0x%p): free\n",
				     ioc->reply));
		ioc->reply = NULL;
	}

	if (ioc->reply_free) {
		dma_pool_free(ioc->reply_free_dma_pool, ioc->reply_free,
		    ioc->reply_free_dma);
		dma_pool_destroy(ioc->reply_free_dma_pool);
		dexitprintk(ioc,
			    ioc_info(ioc, "reply_free_pool(0x%p): free\n",
				     ioc->reply_free));
		ioc->reply_free = NULL;
	}

	if (ioc->reply_post) {
		do {
			rps = &ioc->reply_post[i];
			if (rps->reply_post_free) {
				dma_pool_free(
				    ioc->reply_post_free_dma_pool,
				    rps->reply_post_free,
				    rps->reply_post_free_dma);
				dexitprintk(ioc,
					    ioc_info(ioc, "reply_post_free_pool(0x%p): free\n",
						     rps->reply_post_free));
				rps->reply_post_free = NULL;
			}
		} while (ioc->rdpq_array_enable &&
			   (++i < ioc->reply_queue_count));
		if (ioc->reply_post_free_array &&
			ioc->rdpq_array_enable) {
			dma_pool_free(ioc->reply_post_free_array_dma_pool,
				ioc->reply_post_free_array,
				ioc->reply_post_free_array_dma);
			ioc->reply_post_free_array = NULL;
		}
		dma_pool_destroy(ioc->reply_post_free_array_dma_pool);
		dma_pool_destroy(ioc->reply_post_free_dma_pool);
		kfree(ioc->reply_post);
	}

	if (ioc->pcie_sgl_dma_pool) {
		for (i = 0; i < ioc->scsiio_depth; i++) {
			dma_pool_free(ioc->pcie_sgl_dma_pool,
					ioc->pcie_sg_lookup[i].pcie_sgl,
					ioc->pcie_sg_lookup[i].pcie_sgl_dma);
		}
		if (ioc->pcie_sgl_dma_pool)
			dma_pool_destroy(ioc->pcie_sgl_dma_pool);
	}

	if (ioc->config_page) {
		dexitprintk(ioc,
			    ioc_info(ioc, "config_page(0x%p): free\n",
				     ioc->config_page));
		dma_free_coherent(&ioc->pdev->dev, ioc->config_page_sz,
		    ioc->config_page, ioc->config_page_dma);
	}

	kfree(ioc->hpr_lookup);
	kfree(ioc->internal_lookup);
	if (ioc->chain_lookup) {
		for (i = 0; i < ioc->scsiio_depth; i++) {
			for (j = ioc->chains_per_prp_buffer;
			    j < ioc->chains_needed_per_io; j++) {
				ct = &ioc->chain_lookup[i].chains_per_smid[j];
				if (ct && ct->chain_buffer)
					dma_pool_free(ioc->chain_dma_pool,
						ct->chain_buffer,
						ct->chain_buffer_dma);
			}
			kfree(ioc->chain_lookup[i].chains_per_smid);
		}
		dma_pool_destroy(ioc->chain_dma_pool);
		kfree(ioc->chain_lookup);
		ioc->chain_lookup = NULL;
	}
}

/**
 * is_MSB_are_same - checks whether all reply queues in a set are
 *	having same upper 32bits in their base memory address.
 * @reply_pool_start_address: Base address of a reply queue set
 * @pool_sz: Size of single Reply Descriptor Post Queues pool size
 *
 * Return: 1 if reply queues in a set have a same upper 32bits in their base
 * memory address, else 0.
 */

static int
is_MSB_are_same(long reply_pool_start_address, u32 pool_sz)
{
	long reply_pool_end_address;

	reply_pool_end_address = reply_pool_start_address + pool_sz;

	if (upper_32_bits(reply_pool_start_address) ==
		upper_32_bits(reply_pool_end_address))
		return 1;
	else
		return 0;
}

/**
 * _base_allocate_memory_pools - allocate start of day memory pools
 * @ioc: per adapter object
 *
 * Return: 0 success, anything else error.
 */
static int
_base_allocate_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	struct mpt3sas_facts *facts;
	u16 max_sge_elements;
	u16 chains_needed_per_io;
	u32 sz, total_sz, reply_post_free_sz, reply_post_free_array_sz;
	u32 retry_sz;
	u16 max_request_credit, nvme_blocks_needed;
	unsigned short sg_tablesize;
	u16 sge_size;
	int i, j;
	struct chain_tracker *ct;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));


	retry_sz = 0;
	facts = &ioc->facts;

	/* command line tunables for max sgl entries */
	if (max_sgl_entries != -1)
		sg_tablesize = max_sgl_entries;
	else {
		if (ioc->hba_mpi_version_belonged == MPI2_VERSION)
			sg_tablesize = MPT2SAS_SG_DEPTH;
		else
			sg_tablesize = MPT3SAS_SG_DEPTH;
	}

	/* max sgl entries <= MPT_KDUMP_MIN_PHYS_SEGMENTS in KDUMP mode */
	if (reset_devices)
		sg_tablesize = min_t(unsigned short, sg_tablesize,
		   MPT_KDUMP_MIN_PHYS_SEGMENTS);

	if (ioc->is_mcpu_endpoint)
		ioc->shost->sg_tablesize = MPT_MIN_PHYS_SEGMENTS;
	else {
		if (sg_tablesize < MPT_MIN_PHYS_SEGMENTS)
			sg_tablesize = MPT_MIN_PHYS_SEGMENTS;
		else if (sg_tablesize > MPT_MAX_PHYS_SEGMENTS) {
			sg_tablesize = min_t(unsigned short, sg_tablesize,
					SG_MAX_SEGMENTS);
			ioc_warn(ioc, "sg_tablesize(%u) is bigger than kernel defined SG_CHUNK_SIZE(%u)\n",
				 sg_tablesize, MPT_MAX_PHYS_SEGMENTS);
		}
		ioc->shost->sg_tablesize = sg_tablesize;
	}

	ioc->internal_depth = min_t(int, (facts->HighPriorityCredit + (5)),
		(facts->RequestCredit / 4));
	if (ioc->internal_depth < INTERNAL_CMDS_COUNT) {
		if (facts->RequestCredit <= (INTERNAL_CMDS_COUNT +
				INTERNAL_SCSIIO_CMDS_COUNT)) {
			ioc_err(ioc, "IOC doesn't have enough Request Credits, it has just %d number of credits\n",
				facts->RequestCredit);
			return -ENOMEM;
		}
		ioc->internal_depth = 10;
	}

	ioc->hi_priority_depth = ioc->internal_depth - (5);
	/* command line tunables  for max controller queue depth */
	if (max_queue_depth != -1 && max_queue_depth != 0) {
		max_request_credit = min_t(u16, max_queue_depth +
			ioc->internal_depth, facts->RequestCredit);
		if (max_request_credit > MAX_HBA_QUEUE_DEPTH)
			max_request_credit =  MAX_HBA_QUEUE_DEPTH;
	} else if (reset_devices)
		max_request_credit = min_t(u16, facts->RequestCredit,
		    (MPT3SAS_KDUMP_SCSI_IO_DEPTH + ioc->internal_depth));
	else
		max_request_credit = min_t(u16, facts->RequestCredit,
		    MAX_HBA_QUEUE_DEPTH);

	/* Firmware maintains additional facts->HighPriorityCredit number of
	 * credits for HiPriprity Request messages, so hba queue depth will be
	 * sum of max_request_credit and high priority queue depth.
	 */
	ioc->hba_queue_depth = max_request_credit + ioc->hi_priority_depth;

	/* request frame size */
	ioc->request_sz = facts->IOCRequestFrameSize * 4;

	/* reply frame size */
	ioc->reply_sz = facts->ReplyFrameSize * 4;

	/* chain segment size */
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (facts->IOCMaxChainSegmentSize)
			ioc->chain_segment_sz =
					facts->IOCMaxChainSegmentSize *
					MAX_CHAIN_ELEMT_SZ;
		else
		/* set to 128 bytes size if IOCMaxChainSegmentSize is zero */
			ioc->chain_segment_sz = DEFAULT_NUM_FWCHAIN_ELEMTS *
						    MAX_CHAIN_ELEMT_SZ;
	} else
		ioc->chain_segment_sz = ioc->request_sz;

	/* calculate the max scatter element size */
	sge_size = max_t(u16, ioc->sge_size, ioc->sge_size_ieee);

 retry_allocation:
	total_sz = 0;
	/* calculate number of sg elements left over in the 1st frame */
	max_sge_elements = ioc->request_sz - ((sizeof(Mpi2SCSIIORequest_t) -
	    sizeof(Mpi2SGEIOUnion_t)) + sge_size);
	ioc->max_sges_in_main_message = max_sge_elements/sge_size;

	/* now do the same for a chain buffer */
	max_sge_elements = ioc->chain_segment_sz - sge_size;
	ioc->max_sges_in_chain_message = max_sge_elements/sge_size;

	/*
	 *  MPT3SAS_SG_DEPTH = CONFIG_FUSION_MAX_SGE
	 */
	chains_needed_per_io = ((ioc->shost->sg_tablesize -
	   ioc->max_sges_in_main_message)/ioc->max_sges_in_chain_message)
	    + 1;
	if (chains_needed_per_io > facts->MaxChainDepth) {
		chains_needed_per_io = facts->MaxChainDepth;
		ioc->shost->sg_tablesize = min_t(u16,
		ioc->max_sges_in_main_message + (ioc->max_sges_in_chain_message
		* chains_needed_per_io), ioc->shost->sg_tablesize);
	}
	ioc->chains_needed_per_io = chains_needed_per_io;

	/* reply free queue sizing - taking into account for 64 FW events */
	ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;

	/* mCPU manage single counters for simplicity */
	if (ioc->is_mcpu_endpoint)
		ioc->reply_post_queue_depth = ioc->reply_free_queue_depth;
	else {
		/* calculate reply descriptor post queue depth */
		ioc->reply_post_queue_depth = ioc->hba_queue_depth +
			ioc->reply_free_queue_depth +  1;
		/* align the reply post queue on the next 16 count boundary */
		if (ioc->reply_post_queue_depth % 16)
			ioc->reply_post_queue_depth += 16 -
				(ioc->reply_post_queue_depth % 16);
	}

	if (ioc->reply_post_queue_depth >
	    facts->MaxReplyDescriptorPostQueueDepth) {
		ioc->reply_post_queue_depth =
				facts->MaxReplyDescriptorPostQueueDepth -
		    (facts->MaxReplyDescriptorPostQueueDepth % 16);
		ioc->hba_queue_depth =
				((ioc->reply_post_queue_depth - 64) / 2) - 1;
		ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;
	}

	dinitprintk(ioc,
		    ioc_info(ioc, "scatter gather: sge_in_main_msg(%d), sge_per_chain(%d), sge_per_io(%d), chains_per_io(%d)\n",
			     ioc->max_sges_in_main_message,
			     ioc->max_sges_in_chain_message,
			     ioc->shost->sg_tablesize,
			     ioc->chains_needed_per_io));

	/* reply post queue, 16 byte align */
	reply_post_free_sz = ioc->reply_post_queue_depth *
	    sizeof(Mpi2DefaultReplyDescriptor_t);

	sz = reply_post_free_sz;
	if (_base_is_controller_msix_enabled(ioc) && !ioc->rdpq_array_enable)
		sz *= ioc->reply_queue_count;

	ioc->reply_post = kcalloc((ioc->rdpq_array_enable) ?
	    (ioc->reply_queue_count):1,
	    sizeof(struct reply_post_struct), GFP_KERNEL);

	if (!ioc->reply_post) {
		ioc_err(ioc, "reply_post_free pool: kcalloc failed\n");
		goto out;
	}
	ioc->reply_post_free_dma_pool = dma_pool_create("reply_post_free pool",
	    &ioc->pdev->dev, sz, 16, 0);
	if (!ioc->reply_post_free_dma_pool) {
		ioc_err(ioc, "reply_post_free pool: dma_pool_create failed\n");
		goto out;
	}
	i = 0;
	do {
		ioc->reply_post[i].reply_post_free =
		    dma_pool_zalloc(ioc->reply_post_free_dma_pool,
		    GFP_KERNEL,
		    &ioc->reply_post[i].reply_post_free_dma);
		if (!ioc->reply_post[i].reply_post_free) {
			ioc_err(ioc, "reply_post_free pool: dma_pool_alloc failed\n");
			goto out;
		}
		dinitprintk(ioc,
			    ioc_info(ioc, "reply post free pool (0x%p): depth(%d), element_size(%d), pool_size(%d kB)\n",
				     ioc->reply_post[i].reply_post_free,
				     ioc->reply_post_queue_depth,
				     8, sz / 1024));
		dinitprintk(ioc,
			    ioc_info(ioc, "reply_post_free_dma = (0x%llx)\n",
				     (u64)ioc->reply_post[i].reply_post_free_dma));
		total_sz += sz;
	} while (ioc->rdpq_array_enable && (++i < ioc->reply_queue_count));

	if (ioc->dma_mask == 64) {
		if (_base_change_consistent_dma_mask(ioc, ioc->pdev) != 0) {
			ioc_warn(ioc, "no suitable consistent DMA mask for %s\n",
				 pci_name(ioc->pdev));
			goto out;
		}
	}

	ioc->scsiio_depth = ioc->hba_queue_depth -
	    ioc->hi_priority_depth - ioc->internal_depth;

	/* set the scsi host can_queue depth
	 * with some internal commands that could be outstanding
	 */
	ioc->shost->can_queue = ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT;
	dinitprintk(ioc,
		    ioc_info(ioc, "scsi host: can_queue depth (%d)\n",
			     ioc->shost->can_queue));


	/* contiguous pool for request and chains, 16 byte align, one extra "
	 * "frame for smid=0
	 */
	ioc->chain_depth = ioc->chains_needed_per_io * ioc->scsiio_depth;
	sz = ((ioc->scsiio_depth + 1) * ioc->request_sz);

	/* hi-priority queue */
	sz += (ioc->hi_priority_depth * ioc->request_sz);

	/* internal queue */
	sz += (ioc->internal_depth * ioc->request_sz);

	ioc->request_dma_sz = sz;
	ioc->request = dma_alloc_coherent(&ioc->pdev->dev, sz,
			&ioc->request_dma, GFP_KERNEL);
	if (!ioc->request) {
		ioc_err(ioc, "request pool: dma_alloc_coherent failed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), total(%d kB)\n",
			ioc->hba_queue_depth, ioc->chains_needed_per_io,
			ioc->request_sz, sz / 1024);
		if (ioc->scsiio_depth < MPT3SAS_SAS_QUEUE_DEPTH)
			goto out;
		retry_sz = 64;
		ioc->hba_queue_depth -= retry_sz;
		_base_release_memory_pools(ioc);
		goto retry_allocation;
	}

	if (retry_sz)
		ioc_err(ioc, "request pool: dma_alloc_coherent succeed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), total(%d kb)\n",
			ioc->hba_queue_depth, ioc->chains_needed_per_io,
			ioc->request_sz, sz / 1024);

	/* hi-priority queue */
	ioc->hi_priority = ioc->request + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);
	ioc->hi_priority_dma = ioc->request_dma + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);

	/* internal queue */
	ioc->internal = ioc->hi_priority + (ioc->hi_priority_depth *
	    ioc->request_sz);
	ioc->internal_dma = ioc->hi_priority_dma + (ioc->hi_priority_depth *
	    ioc->request_sz);

	dinitprintk(ioc,
		    ioc_info(ioc, "request pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB)\n",
			     ioc->request, ioc->hba_queue_depth,
			     ioc->request_sz,
			     (ioc->hba_queue_depth * ioc->request_sz) / 1024));

	dinitprintk(ioc,
		    ioc_info(ioc, "request pool: dma(0x%llx)\n",
			     (unsigned long long)ioc->request_dma));
	total_sz += sz;

	dinitprintk(ioc,
		    ioc_info(ioc, "scsiio(0x%p): depth(%d)\n",
			     ioc->request, ioc->scsiio_depth));

	ioc->chain_depth = min_t(u32, ioc->chain_depth, MAX_CHAIN_DEPTH);
	sz = ioc->scsiio_depth * sizeof(struct chain_lookup);
	ioc->chain_lookup = kzalloc(sz, GFP_KERNEL);
	if (!ioc->chain_lookup) {
		ioc_err(ioc, "chain_lookup: __get_free_pages failed\n");
		goto out;
	}

	sz = ioc->chains_needed_per_io * sizeof(struct chain_tracker);
	for (i = 0; i < ioc->scsiio_depth; i++) {
		ioc->chain_lookup[i].chains_per_smid = kzalloc(sz, GFP_KERNEL);
		if (!ioc->chain_lookup[i].chains_per_smid) {
			ioc_err(ioc, "chain_lookup: kzalloc failed\n");
			goto out;
		}
	}

	/* initialize hi-priority queue smid's */
	ioc->hpr_lookup = kcalloc(ioc->hi_priority_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->hpr_lookup) {
		ioc_err(ioc, "hpr_lookup: kcalloc failed\n");
		goto out;
	}
	ioc->hi_priority_smid = ioc->scsiio_depth + 1;
	dinitprintk(ioc,
		    ioc_info(ioc, "hi_priority(0x%p): depth(%d), start smid(%d)\n",
			     ioc->hi_priority,
			     ioc->hi_priority_depth, ioc->hi_priority_smid));

	/* initialize internal queue smid's */
	ioc->internal_lookup = kcalloc(ioc->internal_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->internal_lookup) {
		ioc_err(ioc, "internal_lookup: kcalloc failed\n");
		goto out;
	}
	ioc->internal_smid = ioc->hi_priority_smid + ioc->hi_priority_depth;
	dinitprintk(ioc,
		    ioc_info(ioc, "internal(0x%p): depth(%d), start smid(%d)\n",
			     ioc->internal,
			     ioc->internal_depth, ioc->internal_smid));
	/*
	 * The number of NVMe page sized blocks needed is:
	 *     (((sg_tablesize * 8) - 1) / (page_size - 8)) + 1
	 * ((sg_tablesize * 8) - 1) is the max PRP's minus the first PRP entry
	 * that is placed in the main message frame.  8 is the size of each PRP
	 * entry or PRP list pointer entry.  8 is subtracted from page_size
	 * because of the PRP list pointer entry at the end of a page, so this
	 * is not counted as a PRP entry.  The 1 added page is a round up.
	 *
	 * To avoid allocation failures due to the amount of memory that could
	 * be required for NVMe PRP's, only each set of NVMe blocks will be
	 * contiguous, so a new set is allocated for each possible I/O.
	 */
	ioc->chains_per_prp_buffer = 0;
	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_NVME_DEVICES) {
		nvme_blocks_needed =
			(ioc->shost->sg_tablesize * NVME_PRP_SIZE) - 1;
		nvme_blocks_needed /= (ioc->page_size - NVME_PRP_SIZE);
		nvme_blocks_needed++;

		sz = sizeof(struct pcie_sg_list) * ioc->scsiio_depth;
		ioc->pcie_sg_lookup = kzalloc(sz, GFP_KERNEL);
		if (!ioc->pcie_sg_lookup) {
			ioc_info(ioc, "PCIe SGL lookup: kzalloc failed\n");
			goto out;
		}
		sz = nvme_blocks_needed * ioc->page_size;
		ioc->pcie_sgl_dma_pool =
			dma_pool_create("PCIe SGL pool", &ioc->pdev->dev, sz, 16, 0);
		if (!ioc->pcie_sgl_dma_pool) {
			ioc_info(ioc, "PCIe SGL pool: dma_pool_create failed\n");
			goto out;
		}

		ioc->chains_per_prp_buffer = sz/ioc->chain_segment_sz;
		ioc->chains_per_prp_buffer = min(ioc->chains_per_prp_buffer,
						ioc->chains_needed_per_io);

		for (i = 0; i < ioc->scsiio_depth; i++) {
			ioc->pcie_sg_lookup[i].pcie_sgl = dma_pool_alloc(
				ioc->pcie_sgl_dma_pool, GFP_KERNEL,
				&ioc->pcie_sg_lookup[i].pcie_sgl_dma);
			if (!ioc->pcie_sg_lookup[i].pcie_sgl) {
				ioc_info(ioc, "PCIe SGL pool: dma_pool_alloc failed\n");
				goto out;
			}
			for (j = 0; j < ioc->chains_per_prp_buffer; j++) {
				ct = &ioc->chain_lookup[i].chains_per_smid[j];
				ct->chain_buffer =
				    ioc->pcie_sg_lookup[i].pcie_sgl +
				    (j * ioc->chain_segment_sz);
				ct->chain_buffer_dma =
				    ioc->pcie_sg_lookup[i].pcie_sgl_dma +
				    (j * ioc->chain_segment_sz);
			}
		}

		dinitprintk(ioc,
			    ioc_info(ioc, "PCIe sgl pool depth(%d), element_size(%d), pool_size(%d kB)\n",
				     ioc->scsiio_depth, sz,
				     (sz * ioc->scsiio_depth) / 1024));
		dinitprintk(ioc,
			    ioc_info(ioc, "Number of chains can fit in a PRP page(%d)\n",
				     ioc->chains_per_prp_buffer));
		total_sz += sz * ioc->scsiio_depth;
	}

	ioc->chain_dma_pool = dma_pool_create("chain pool", &ioc->pdev->dev,
	    ioc->chain_segment_sz, 16, 0);
	if (!ioc->chain_dma_pool) {
		ioc_err(ioc, "chain_dma_pool: dma_pool_create failed\n");
		goto out;
	}
	for (i = 0; i < ioc->scsiio_depth; i++) {
		for (j = ioc->chains_per_prp_buffer;
				j < ioc->chains_needed_per_io; j++) {
			ct = &ioc->chain_lookup[i].chains_per_smid[j];
			ct->chain_buffer = dma_pool_alloc(
					ioc->chain_dma_pool, GFP_KERNEL,
					&ct->chain_buffer_dma);
			if (!ct->chain_buffer) {
				ioc_err(ioc, "chain_lookup: pci_pool_alloc failed\n");
				_base_release_memory_pools(ioc);
				goto out;
			}
		}
		total_sz += ioc->chain_segment_sz;
	}

	dinitprintk(ioc,
		    ioc_info(ioc, "chain pool depth(%d), frame_size(%d), pool_size(%d kB)\n",
			     ioc->chain_depth, ioc->chain_segment_sz,
			     (ioc->chain_depth * ioc->chain_segment_sz) / 1024));

	/* sense buffers, 4 byte align */
	sz = ioc->scsiio_depth * SCSI_SENSE_BUFFERSIZE;
	ioc->sense_dma_pool = dma_pool_create("sense pool", &ioc->pdev->dev, sz,
					      4, 0);
	if (!ioc->sense_dma_pool) {
		ioc_err(ioc, "sense pool: dma_pool_create failed\n");
		goto out;
	}
	ioc->sense = dma_pool_alloc(ioc->sense_dma_pool, GFP_KERNEL,
	    &ioc->sense_dma);
	if (!ioc->sense) {
		ioc_err(ioc, "sense pool: dma_pool_alloc failed\n");
		goto out;
	}
	/* sense buffer requires to be in same 4 gb region.
	 * Below function will check the same.
	 * In case of failure, new pci pool will be created with updated
	 * alignment. Older allocation and pool will be destroyed.
	 * Alignment will be used such a way that next allocation if
	 * success, will always meet same 4gb region requirement.
	 * Actual requirement is not alignment, but we need start and end of
	 * DMA address must have same upper 32 bit address.
	 */
	if (!is_MSB_are_same((long)ioc->sense, sz)) {
		//Release Sense pool & Reallocate
		dma_pool_free(ioc->sense_dma_pool, ioc->sense, ioc->sense_dma);
		dma_pool_destroy(ioc->sense_dma_pool);
		ioc->sense = NULL;

		ioc->sense_dma_pool =
			dma_pool_create("sense pool", &ioc->pdev->dev, sz,
						roundup_pow_of_two(sz), 0);
		if (!ioc->sense_dma_pool) {
			ioc_err(ioc, "sense pool: pci_pool_create failed\n");
			goto out;
		}
		ioc->sense = dma_pool_alloc(ioc->sense_dma_pool, GFP_KERNEL,
				&ioc->sense_dma);
		if (!ioc->sense) {
			ioc_err(ioc, "sense pool: pci_pool_alloc failed\n");
			goto out;
		}
	}
	dinitprintk(ioc,
		    ioc_info(ioc, "sense pool(0x%p): depth(%d), element_size(%d), pool_size(%d kB)\n",
			     ioc->sense, ioc->scsiio_depth,
			     SCSI_SENSE_BUFFERSIZE, sz / 1024));
	dinitprintk(ioc,
		    ioc_info(ioc, "sense_dma(0x%llx)\n",
			     (unsigned long long)ioc->sense_dma));
	total_sz += sz;

	/* reply pool, 4 byte align */
	sz = ioc->reply_free_queue_depth * ioc->reply_sz;
	ioc->reply_dma_pool = dma_pool_create("reply pool", &ioc->pdev->dev, sz,
					      4, 0);
	if (!ioc->reply_dma_pool) {
		ioc_err(ioc, "reply pool: dma_pool_create failed\n");
		goto out;
	}
	ioc->reply = dma_pool_alloc(ioc->reply_dma_pool, GFP_KERNEL,
	    &ioc->reply_dma);
	if (!ioc->reply) {
		ioc_err(ioc, "reply pool: dma_pool_alloc failed\n");
		goto out;
	}
	ioc->reply_dma_min_address = (u32)(ioc->reply_dma);
	ioc->reply_dma_max_address = (u32)(ioc->reply_dma) + sz;
	dinitprintk(ioc,
		    ioc_info(ioc, "reply pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB)\n",
			     ioc->reply, ioc->reply_free_queue_depth,
			     ioc->reply_sz, sz / 1024));
	dinitprintk(ioc,
		    ioc_info(ioc, "reply_dma(0x%llx)\n",
			     (unsigned long long)ioc->reply_dma));
	total_sz += sz;

	/* reply free queue, 16 byte align */
	sz = ioc->reply_free_queue_depth * 4;
	ioc->reply_free_dma_pool = dma_pool_create("reply_free pool",
	    &ioc->pdev->dev, sz, 16, 0);
	if (!ioc->reply_free_dma_pool) {
		ioc_err(ioc, "reply_free pool: dma_pool_create failed\n");
		goto out;
	}
	ioc->reply_free = dma_pool_zalloc(ioc->reply_free_dma_pool, GFP_KERNEL,
	    &ioc->reply_free_dma);
	if (!ioc->reply_free) {
		ioc_err(ioc, "reply_free pool: dma_pool_alloc failed\n");
		goto out;
	}
	dinitprintk(ioc,
		    ioc_info(ioc, "reply_free pool(0x%p): depth(%d), element_size(%d), pool_size(%d kB)\n",
			     ioc->reply_free, ioc->reply_free_queue_depth,
			     4, sz / 1024));
	dinitprintk(ioc,
		    ioc_info(ioc, "reply_free_dma (0x%llx)\n",
			     (unsigned long long)ioc->reply_free_dma));
	total_sz += sz;

	if (ioc->rdpq_array_enable) {
		reply_post_free_array_sz = ioc->reply_queue_count *
		    sizeof(Mpi2IOCInitRDPQArrayEntry);
		ioc->reply_post_free_array_dma_pool =
		    dma_pool_create("reply_post_free_array pool",
		    &ioc->pdev->dev, reply_post_free_array_sz, 16, 0);
		if (!ioc->reply_post_free_array_dma_pool) {
			dinitprintk(ioc,
				    ioc_info(ioc, "reply_post_free_array pool: dma_pool_create failed\n"));
			goto out;
		}
		ioc->reply_post_free_array =
		    dma_pool_alloc(ioc->reply_post_free_array_dma_pool,
		    GFP_KERNEL, &ioc->reply_post_free_array_dma);
		if (!ioc->reply_post_free_array) {
			dinitprintk(ioc,
				    ioc_info(ioc, "reply_post_free_array pool: dma_pool_alloc failed\n"));
			goto out;
		}
	}
	ioc->config_page_sz = 512;
	ioc->config_page = dma_alloc_coherent(&ioc->pdev->dev,
			ioc->config_page_sz, &ioc->config_page_dma, GFP_KERNEL);
	if (!ioc->config_page) {
		ioc_err(ioc, "config page: dma_pool_alloc failed\n");
		goto out;
	}
	dinitprintk(ioc,
		    ioc_info(ioc, "config page(0x%p): size(%d)\n",
			     ioc->config_page, ioc->config_page_sz));
	dinitprintk(ioc,
		    ioc_info(ioc, "config_page_dma(0x%llx)\n",
			     (unsigned long long)ioc->config_page_dma));
	total_sz += ioc->config_page_sz;

	ioc_info(ioc, "Allocated physical memory: size(%d kB)\n",
		 total_sz / 1024);
	ioc_info(ioc, "Current Controller Queue Depth(%d),Max Controller Queue Depth(%d)\n",
		 ioc->shost->can_queue, facts->RequestCredit);
	ioc_info(ioc, "Scatter Gather Elements per IO(%d)\n",
		 ioc->shost->sg_tablesize);
	return 0;

 out:
	return -ENOMEM;
}

/**
 * mpt3sas_base_get_iocstate - Get the current state of a MPT adapter.
 * @ioc: Pointer to MPT_ADAPTER structure
 * @cooked: Request raw or cooked IOC state
 *
 * Return: all IOC Doorbell register bits if cooked==0, else just the
 * Doorbell bits in MPI_IOC_STATE_MASK.
 */
u32
mpt3sas_base_get_iocstate(struct MPT3SAS_ADAPTER *ioc, int cooked)
{
	u32 s, sc;

	s = ioc->base_readl(&ioc->chip->Doorbell);
	sc = s & MPI2_IOC_STATE_MASK;
	return cooked ? sc : s;
}

/**
 * _base_wait_on_iocstate - waiting on a particular ioc state
 * @ioc: ?
 * @ioc_state: controller state { READY, OPERATIONAL, or RESET }
 * @timeout: timeout in second
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_wait_on_iocstate(struct MPT3SAS_ADAPTER *ioc, u32 ioc_state, int timeout)
{
	u32 count, cntdn;
	u32 current_state;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		current_state = mpt3sas_base_get_iocstate(ioc, 1);
		if (current_state == ioc_state)
			return 0;
		if (count && current_state == MPI2_IOC_STATE_FAULT)
			break;

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	return current_state;
}

/**
 * _base_wait_for_doorbell_int - waiting for controller interrupt(generated by
 * a write to the doorbell)
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_IOC2SYS_DB_STATUS - set to one when IOC writes to doorbell.
 */
static int
_base_diag_reset(struct MPT3SAS_ADAPTER *ioc);

static int
_base_wait_for_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc,
				  ioc_info(ioc, "%s: successful count(%d), timeout(%d)\n",
					   __func__, count, timeout));
			return 0;
		}

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	ioc_err(ioc, "%s: failed due to timeout count(%d), int_status(%x)!\n",
		__func__, count, int_status);
	return -EFAULT;
}

static int
_base_spin_on_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 2000 * timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc,
				  ioc_info(ioc, "%s: successful count(%d), timeout(%d)\n",
					   __func__, count, timeout));
			return 0;
		}

		udelay(500);
		count++;
	} while (--cntdn);

	ioc_err(ioc, "%s: failed due to timeout count(%d), int_status(%x)!\n",
		__func__, count, int_status);
	return -EFAULT;

}

/**
 * _base_wait_for_doorbell_ack - waiting for controller to read the doorbell.
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Return: 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_SYS2IOC_DB_STATUS - set to one when host writes to
 * doorbell.
 */
static int
_base_wait_for_doorbell_ack(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;
	u32 doorbell;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (!(int_status & MPI2_HIS_SYS2IOC_DB_STATUS)) {
			dhsprintk(ioc,
				  ioc_info(ioc, "%s: successful count(%d), timeout(%d)\n",
					   __func__, count, timeout));
			return 0;
		} else if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			doorbell = ioc->base_readl(&ioc->chip->Doorbell);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpt3sas_base_fault_info(ioc , doorbell);
				return -EFAULT;
			}
		} else if (int_status == 0xFFFFFFFF)
			goto out;

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

 out:
	ioc_err(ioc, "%s: failed due to timeout count(%d), int_status(%x)!\n",
		__func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_not_used - waiting for doorbell to not be in use
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_wait_for_doorbell_not_used(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 doorbell_reg;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		doorbell_reg = ioc->base_readl(&ioc->chip->Doorbell);
		if (!(doorbell_reg & MPI2_DOORBELL_USED)) {
			dhsprintk(ioc,
				  ioc_info(ioc, "%s: successful count(%d), timeout(%d)\n",
					   __func__, count, timeout));
			return 0;
		}

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	ioc_err(ioc, "%s: failed due to timeout count(%d), doorbell_reg(%x)!\n",
		__func__, count, doorbell_reg);
	return -EFAULT;
}

/**
 * _base_send_ioc_reset - send doorbell reset
 * @ioc: per adapter object
 * @reset_type: currently only supports: MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET
 * @timeout: timeout in second
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_reset(struct MPT3SAS_ADAPTER *ioc, u8 reset_type, int timeout)
{
	u32 ioc_state;
	int r = 0;

	if (reset_type != MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET) {
		ioc_err(ioc, "%s: unknown reset_type\n", __func__);
		return -EFAULT;
	}

	if (!(ioc->facts.IOCCapabilities &
	   MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY))
		return -EFAULT;

	ioc_info(ioc, "sending message unit reset !!\n");

	writel(reset_type << MPI2_DOORBELL_FUNCTION_SHIFT,
	    &ioc->chip->Doorbell);
	if ((_base_wait_for_doorbell_ack(ioc, 15))) {
		r = -EFAULT;
		goto out;
	}
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, timeout);
	if (ioc_state) {
		ioc_err(ioc, "%s: failed going to ready state (ioc_state=0x%x)\n",
			__func__, ioc_state);
		r = -EFAULT;
		goto out;
	}
 out:
	ioc_info(ioc, "message unit reset: %s\n",
		 r == 0 ? "SUCCESS" : "FAILED");
	return r;
}

/**
 * mpt3sas_wait_for_ioc - IOC's operational state is checked here.
 * @ioc: per adapter object
 * @wait_count: timeout in seconds
 *
 * Return: Waits up to timeout seconds for the IOC to
 * become operational. Returns 0 if IOC is present
 * and operational; otherwise returns -EFAULT.
 */

int
mpt3sas_wait_for_ioc(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	int wait_state_count = 0;
	u32 ioc_state;

	do {
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		if (ioc_state == MPI2_IOC_STATE_OPERATIONAL)
			break;
		ssleep(1);
		ioc_info(ioc, "%s: waiting for operational state(count=%d)\n",
				__func__, ++wait_state_count);
	} while (--timeout);
	if (!timeout) {
		ioc_err(ioc, "%s: failed due to ioc not operational\n", __func__);
		return -EFAULT;
	}
	if (wait_state_count)
		ioc_info(ioc, "ioc is operational\n");
	return 0;
}

/**
 * _base_handshake_req_reply_wait - send request thru doorbell interface
 * @ioc: per adapter object
 * @request_bytes: request length
 * @request: pointer having request payload
 * @reply_bytes: reply length
 * @reply: pointer to reply payload
 * @timeout: timeout in second
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_handshake_req_reply_wait(struct MPT3SAS_ADAPTER *ioc, int request_bytes,
	u32 *request, int reply_bytes, u16 *reply, int timeout)
{
	MPI2DefaultReply_t *default_reply = (MPI2DefaultReply_t *)reply;
	int i;
	u8 failed;
	__le32 *mfp;

	/* make sure doorbell is not in use */
	if ((ioc->base_readl(&ioc->chip->Doorbell) & MPI2_DOORBELL_USED)) {
		ioc_err(ioc, "doorbell is in use (line=%d)\n", __LINE__);
		return -EFAULT;
	}

	/* clear pending doorbell interrupts from previous state changes */
	if (ioc->base_readl(&ioc->chip->HostInterruptStatus) &
	    MPI2_HIS_IOC2SYS_DB_STATUS)
		writel(0, &ioc->chip->HostInterruptStatus);

	/* send message to ioc */
	writel(((MPI2_FUNCTION_HANDSHAKE<<MPI2_DOORBELL_FUNCTION_SHIFT) |
	    ((request_bytes/4)<<MPI2_DOORBELL_ADD_DWORDS_SHIFT)),
	    &ioc->chip->Doorbell);

	if ((_base_spin_on_doorbell_int(ioc, 5))) {
		ioc_err(ioc, "doorbell handshake int failed (line=%d)\n",
			__LINE__);
		return -EFAULT;
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if ((_base_wait_for_doorbell_ack(ioc, 5))) {
		ioc_err(ioc, "doorbell handshake ack failed (line=%d)\n",
			__LINE__);
		return -EFAULT;
	}

	/* send message 32-bits at a time */
	for (i = 0, failed = 0; i < request_bytes/4 && !failed; i++) {
		writel(cpu_to_le32(request[i]), &ioc->chip->Doorbell);
		if ((_base_wait_for_doorbell_ack(ioc, 5)))
			failed = 1;
	}

	if (failed) {
		ioc_err(ioc, "doorbell handshake sending request failed (line=%d)\n",
			__LINE__);
		return -EFAULT;
	}

	/* now wait for the reply */
	if ((_base_wait_for_doorbell_int(ioc, timeout))) {
		ioc_err(ioc, "doorbell handshake int failed (line=%d)\n",
			__LINE__);
		return -EFAULT;
	}

	/* read the first two 16-bits, it gives the total length of the reply */
	reply[0] = le16_to_cpu(ioc->base_readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);
	if ((_base_wait_for_doorbell_int(ioc, 5))) {
		ioc_err(ioc, "doorbell handshake int failed (line=%d)\n",
			__LINE__);
		return -EFAULT;
	}
	reply[1] = le16_to_cpu(ioc->base_readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);

	for (i = 2; i < default_reply->MsgLength * 2; i++)  {
		if ((_base_wait_for_doorbell_int(ioc, 5))) {
			ioc_err(ioc, "doorbell handshake int failed (line=%d)\n",
				__LINE__);
			return -EFAULT;
		}
		if (i >=  reply_bytes/2) /* overflow case */
			ioc->base_readl(&ioc->chip->Doorbell);
		else
			reply[i] = le16_to_cpu(
			    ioc->base_readl(&ioc->chip->Doorbell)
			    & MPI2_DOORBELL_DATA_MASK);
		writel(0, &ioc->chip->HostInterruptStatus);
	}

	_base_wait_for_doorbell_int(ioc, 5);
	if (_base_wait_for_doorbell_not_used(ioc, 5) != 0) {
		dhsprintk(ioc,
			  ioc_info(ioc, "doorbell is in use (line=%d)\n",
				   __LINE__));
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		mfp = (__le32 *)reply;
		pr_info("\toffset:data\n");
		for (i = 0; i < reply_bytes/4; i++)
			pr_info("\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}
	return 0;
}

/**
 * mpt3sas_base_sas_iounit_control - send sas iounit control to FW
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SAS IO Unit Control Request message allows the host to perform low-level
 * operations, such as resets on the PHYs of the IO Unit, also allows the host
 * to obtain the IOC assigned device handles for a device if it has other
 * identifying information about the device, in addition allows the host to
 * remove IOC resources associated with the device.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_base_sas_iounit_control(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SasIoUnitControlReply_t *mpi_reply,
	Mpi2SasIoUnitControlRequest_t *mpi_request)
{
	u16 smid;
	u8 issue_reset = 0;
	int rc;
	void *request;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: base_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = mpt3sas_wait_for_ioc(ioc, IOC_OPERATIONAL_WAIT_COUNT);
	if (rc)
		goto out;

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SasIoUnitControlRequest_t));
	if (mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET)
		ioc->ioc_link_reset_in_progress = 1;
	init_completion(&ioc->base_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if ((mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET) &&
	    ioc->ioc_link_reset_in_progress)
		ioc->ioc_link_reset_in_progress = 0;
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		issue_reset =
			mpt3sas_base_check_cmd_timeout(ioc,
				ioc->base_cmds.status, mpi_request,
				sizeof(Mpi2SasIoUnitControlRequest_t)/4);
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SasIoUnitControlReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SasIoUnitControlReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * mpt3sas_base_scsi_enclosure_processor - sending request to sep device
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SCSI Enclosure Processor request message causes the IOC to
 * communicate with SES devices to control LED status signals.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_base_scsi_enclosure_processor(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request)
{
	u16 smid;
	u8 issue_reset = 0;
	int rc;
	void *request;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: base_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = mpt3sas_wait_for_ioc(ioc, IOC_OPERATIONAL_WAIT_COUNT);
	if (rc)
		goto out;

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SepReply_t));
	init_completion(&ioc->base_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		issue_reset =
			mpt3sas_base_check_cmd_timeout(ioc,
				ioc->base_cmds.status, mpi_request,
				sizeof(Mpi2SepRequest_t)/4);
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SepReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SepReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * _base_get_port_facts - obtain port facts reply and save in ioc
 * @ioc: per adapter object
 * @port: ?
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_get_port_facts(struct MPT3SAS_ADAPTER *ioc, int port)
{
	Mpi2PortFactsRequest_t mpi_request;
	Mpi2PortFactsReply_t mpi_reply;
	struct mpt3sas_port_facts *pfacts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	mpi_reply_sz = sizeof(Mpi2PortFactsReply_t);
	mpi_request_sz = sizeof(Mpi2PortFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_PORT_FACTS;
	mpi_request.PortNumber = port;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		ioc_err(ioc, "%s: handshake failed (r=%d)\n", __func__, r);
		return r;
	}

	pfacts = &ioc->pfacts[port];
	memset(pfacts, 0, sizeof(struct mpt3sas_port_facts));
	pfacts->PortNumber = mpi_reply.PortNumber;
	pfacts->VP_ID = mpi_reply.VP_ID;
	pfacts->VF_ID = mpi_reply.VF_ID;
	pfacts->MaxPostedCmdBuffers =
	    le16_to_cpu(mpi_reply.MaxPostedCmdBuffers);

	return 0;
}

/**
 * _base_wait_for_iocstate - Wait until the card is in READY or OPERATIONAL
 * @ioc: per adapter object
 * @timeout:
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_wait_for_iocstate(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 ioc_state;
	int rc;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	if (ioc->pci_error_recovery) {
		dfailprintk(ioc,
			    ioc_info(ioc, "%s: host in pci error recovery\n",
				     __func__));
		return -EFAULT;
	}

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc,
		  ioc_info(ioc, "%s: ioc_state(0x%08x)\n",
			   __func__, ioc_state));

	if (((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY) ||
	    (ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, ioc_info(ioc, "unexpected doorbell active!\n"));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}

	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, timeout);
	if (ioc_state) {
		dfailprintk(ioc,
			    ioc_info(ioc, "%s: failed going to ready state (ioc_state=0x%x)\n",
				     __func__, ioc_state));
		return -EFAULT;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_get_ioc_facts - obtain ioc facts reply and save in ioc
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_get_ioc_facts(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCFactsRequest_t mpi_request;
	Mpi2IOCFactsReply_t mpi_reply;
	struct mpt3sas_facts *facts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	r = _base_wait_for_iocstate(ioc, 10);
	if (r) {
		dfailprintk(ioc,
			    ioc_info(ioc, "%s: failed getting to correct state\n",
				     __func__));
		return r;
	}
	mpi_reply_sz = sizeof(Mpi2IOCFactsReply_t);
	mpi_request_sz = sizeof(Mpi2IOCFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_IOC_FACTS;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		ioc_err(ioc, "%s: handshake failed (r=%d)\n", __func__, r);
		return r;
	}

	facts = &ioc->facts;
	memset(facts, 0, sizeof(struct mpt3sas_facts));
	facts->MsgVersion = le16_to_cpu(mpi_reply.MsgVersion);
	facts->HeaderVersion = le16_to_cpu(mpi_reply.HeaderVersion);
	facts->VP_ID = mpi_reply.VP_ID;
	facts->VF_ID = mpi_reply.VF_ID;
	facts->IOCExceptions = le16_to_cpu(mpi_reply.IOCExceptions);
	facts->MaxChainDepth = mpi_reply.MaxChainDepth;
	facts->WhoInit = mpi_reply.WhoInit;
	facts->NumberOfPorts = mpi_reply.NumberOfPorts;
	facts->MaxMSIxVectors = mpi_reply.MaxMSIxVectors;
	if (ioc->msix_enable && (facts->MaxMSIxVectors <=
	    MAX_COMBINED_MSIX_VECTORS(ioc->is_gen35_ioc)))
		ioc->combined_reply_queue = 0;
	facts->RequestCredit = le16_to_cpu(mpi_reply.RequestCredit);
	facts->MaxReplyDescriptorPostQueueDepth =
	    le16_to_cpu(mpi_reply.MaxReplyDescriptorPostQueueDepth);
	facts->ProductID = le16_to_cpu(mpi_reply.ProductID);
	facts->IOCCapabilities = le32_to_cpu(mpi_reply.IOCCapabilities);
	if ((facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID))
		ioc->ir_firmware = 1;
	if ((facts->IOCCapabilities &
	      MPI2_IOCFACTS_CAPABILITY_RDPQ_ARRAY_CAPABLE) && (!reset_devices))
		ioc->rdpq_array_capable = 1;
	facts->FWVersion.Word = le32_to_cpu(mpi_reply.FWVersion.Word);
	facts->IOCRequestFrameSize =
	    le16_to_cpu(mpi_reply.IOCRequestFrameSize);
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		facts->IOCMaxChainSegmentSize =
			le16_to_cpu(mpi_reply.IOCMaxChainSegmentSize);
	}
	facts->MaxInitiators = le16_to_cpu(mpi_reply.MaxInitiators);
	facts->MaxTargets = le16_to_cpu(mpi_reply.MaxTargets);
	ioc->shost->max_id = -1;
	facts->MaxSasExpanders = le16_to_cpu(mpi_reply.MaxSasExpanders);
	facts->MaxEnclosures = le16_to_cpu(mpi_reply.MaxEnclosures);
	facts->ProtocolFlags = le16_to_cpu(mpi_reply.ProtocolFlags);
	facts->HighPriorityCredit =
	    le16_to_cpu(mpi_reply.HighPriorityCredit);
	facts->ReplyFrameSize = mpi_reply.ReplyFrameSize;
	facts->MaxDevHandle = le16_to_cpu(mpi_reply.MaxDevHandle);
	facts->CurrentHostPageSize = mpi_reply.CurrentHostPageSize;

	/*
	 * Get the Page Size from IOC Facts. If it's 0, default to 4k.
	 */
	ioc->page_size = 1 << facts->CurrentHostPageSize;
	if (ioc->page_size == 1) {
		ioc_info(ioc, "CurrentHostPageSize is 0: Setting default host page size to 4k\n");
		ioc->page_size = 1 << MPT3SAS_HOST_PAGE_SIZE_4K;
	}
	dinitprintk(ioc,
		    ioc_info(ioc, "CurrentHostPageSize(%d)\n",
			     facts->CurrentHostPageSize));

	dinitprintk(ioc,
		    ioc_info(ioc, "hba queue depth(%d), max chains per io(%d)\n",
			     facts->RequestCredit, facts->MaxChainDepth));
	dinitprintk(ioc,
		    ioc_info(ioc, "request frame size(%d), reply frame size(%d)\n",
			     facts->IOCRequestFrameSize * 4,
			     facts->ReplyFrameSize * 4));
	return 0;
}

/**
 * _base_send_ioc_init - send ioc_init to firmware
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_init(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCInitRequest_t mpi_request;
	Mpi2IOCInitReply_t mpi_reply;
	int i, r = 0;
	ktime_t current_time;
	u16 ioc_status;
	u32 reply_post_free_array_sz = 0;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	memset(&mpi_request, 0, sizeof(Mpi2IOCInitRequest_t));
	mpi_request.Function = MPI2_FUNCTION_IOC_INIT;
	mpi_request.WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	mpi_request.VF_ID = 0; /* TODO */
	mpi_request.VP_ID = 0;
	mpi_request.MsgVersion = cpu_to_le16(ioc->hba_mpi_version_belonged);
	mpi_request.HeaderVersion = cpu_to_le16(MPI2_HEADER_VERSION);
	mpi_request.HostPageSize = MPT3SAS_HOST_PAGE_SIZE_4K;

	if (_base_is_controller_msix_enabled(ioc))
		mpi_request.HostMSIxVectors = ioc->reply_queue_count;
	mpi_request.SystemRequestFrameSize = cpu_to_le16(ioc->request_sz/4);
	mpi_request.ReplyDescriptorPostQueueDepth =
	    cpu_to_le16(ioc->reply_post_queue_depth);
	mpi_request.ReplyFreeQueueDepth =
	    cpu_to_le16(ioc->reply_free_queue_depth);

	mpi_request.SenseBufferAddressHigh =
	    cpu_to_le32((u64)ioc->sense_dma >> 32);
	mpi_request.SystemReplyAddressHigh =
	    cpu_to_le32((u64)ioc->reply_dma >> 32);
	mpi_request.SystemRequestFrameBaseAddress =
	    cpu_to_le64((u64)ioc->request_dma);
	mpi_request.ReplyFreeQueueAddress =
	    cpu_to_le64((u64)ioc->reply_free_dma);

	if (ioc->rdpq_array_enable) {
		reply_post_free_array_sz = ioc->reply_queue_count *
		    sizeof(Mpi2IOCInitRDPQArrayEntry);
		memset(ioc->reply_post_free_array, 0, reply_post_free_array_sz);
		for (i = 0; i < ioc->reply_queue_count; i++)
			ioc->reply_post_free_array[i].RDPQBaseAddress =
			    cpu_to_le64(
				(u64)ioc->reply_post[i].reply_post_free_dma);
		mpi_request.MsgFlags = MPI2_IOCINIT_MSGFLAG_RDPQ_ARRAY_MODE;
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)ioc->reply_post_free_array_dma);
	} else {
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)ioc->reply_post[0].reply_post_free_dma);
	}

	/* This time stamp specifies number of milliseconds
	 * since epoch ~ midnight January 1, 1970.
	 */
	current_time = ktime_get_real();
	mpi_request.TimeStamp = cpu_to_le64(ktime_to_ms(current_time));

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		__le32 *mfp;
		int i;

		mfp = (__le32 *)&mpi_request;
		pr_info("\toffset:data\n");
		for (i = 0; i < sizeof(Mpi2IOCInitRequest_t)/4; i++)
			pr_info("\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}

	r = _base_handshake_req_reply_wait(ioc,
	    sizeof(Mpi2IOCInitRequest_t), (u32 *)&mpi_request,
	    sizeof(Mpi2IOCInitReply_t), (u16 *)&mpi_reply, 10);

	if (r != 0) {
		ioc_err(ioc, "%s: handshake failed (r=%d)\n", __func__, r);
		return r;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS ||
	    mpi_reply.IOCLogInfo) {
		ioc_err(ioc, "%s: failed\n", __func__);
		r = -EIO;
	}

	return r;
}

/**
 * mpt3sas_port_enable_done - command completion routine for port enable
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *          0 means the mf is freed from this function.
 */
u8
mpt3sas_port_enable_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;

	if (ioc->port_enable_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;

	if (mpi_reply->Function != MPI2_FUNCTION_PORT_ENABLE)
		return 1;

	ioc->port_enable_cmds.status &= ~MPT3_CMD_PENDING;
	ioc->port_enable_cmds.status |= MPT3_CMD_COMPLETE;
	ioc->port_enable_cmds.status |= MPT3_CMD_REPLY_VALID;
	memcpy(ioc->port_enable_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		ioc->port_enable_failed = 1;

	if (ioc->is_driver_loading) {
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
			mpt3sas_port_enable_complete(ioc);
			return 1;
		} else {
			ioc->start_scan_failed = ioc_status;
			ioc->start_scan = 0;
			return 1;
		}
	}
	complete(&ioc->port_enable_cmds.done);
	return 1;
}

/**
 * _base_send_port_enable - send port_enable(discovery stuff) to firmware
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_send_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	Mpi2PortEnableReply_t *mpi_reply;
	int r = 0;
	u16 smid;
	u16 ioc_status;

	ioc_info(ioc, "sending port enable !!\n");

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		ioc_err(ioc, "%s: internal command already in use\n", __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	init_completion(&ioc->port_enable_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->port_enable_cmds.done, 300*HZ);
	if (!(ioc->port_enable_cmds.status & MPT3_CMD_COMPLETE)) {
		ioc_err(ioc, "%s: timeout\n", __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2PortEnableRequest_t)/4);
		if (ioc->port_enable_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
		goto out;
	}

	mpi_reply = ioc->port_enable_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "%s: failed with (ioc_status=0x%08x)\n",
			__func__, ioc_status);
		r = -EFAULT;
		goto out;
	}

 out:
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;
	ioc_info(ioc, "port enable: %s\n", r == 0 ? "SUCCESS" : "FAILED");
	return r;
}

/**
 * mpt3sas_port_enable - initiate firmware discovery (don't wait for reply)
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	u16 smid;

	ioc_info(ioc, "sending port enable !!\n");

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		ioc_err(ioc, "%s: internal command already in use\n", __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	mpt3sas_base_put_smid_default(ioc, smid);
	return 0;
}

/**
 * _base_determine_wait_on_discovery - desposition
 * @ioc: per adapter object
 *
 * Decide whether to wait on discovery to complete. Used to either
 * locate boot device, or report volumes ahead of physical devices.
 *
 * Return: 1 for wait, 0 for don't wait.
 */
static int
_base_determine_wait_on_discovery(struct MPT3SAS_ADAPTER *ioc)
{
	/* We wait for discovery to complete if IR firmware is loaded.
	 * The sas topology events arrive before PD events, so we need time to
	 * turn on the bit in ioc->pd_handles to indicate PD
	 * Also, it maybe required to report Volumes ahead of physical
	 * devices when MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING is set.
	 */
	if (ioc->ir_firmware)
		return 1;

	/* if no Bios, then we don't need to wait */
	if (!ioc->bios_pg3.BiosVersion)
		return 0;

	/* Bios is present, then we drop down here.
	 *
	 * If there any entries in the Bios Page 2, then we wait
	 * for discovery to complete.
	 */

	/* Current Boot Device */
	if ((ioc->bios_pg2.CurrentBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Request Boot Device */
	   (ioc->bios_pg2.ReqBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Alternate Request Boot Device */
	   (ioc->bios_pg2.ReqAltBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED)
		return 0;

	return 1;
}

/**
 * _base_unmask_events - turn on notification for this event
 * @ioc: per adapter object
 * @event: firmware event
 *
 * The mask is stored in ioc->event_masks.
 */
static void
_base_unmask_events(struct MPT3SAS_ADAPTER *ioc, u16 event)
{
	u32 desired_event;

	if (event >= 128)
		return;

	desired_event = (1 << (event % 32));

	if (event < 32)
		ioc->event_masks[0] &= ~desired_event;
	else if (event < 64)
		ioc->event_masks[1] &= ~desired_event;
	else if (event < 96)
		ioc->event_masks[2] &= ~desired_event;
	else if (event < 128)
		ioc->event_masks[3] &= ~desired_event;
}

/**
 * _base_event_notification - send event notification
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_event_notification(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2EventNotificationRequest_t *mpi_request;
	u16 smid;
	int r = 0;
	int i;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		ioc_err(ioc, "%s: internal command already in use\n", __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		return -EAGAIN;
	}
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2EventNotificationRequest_t));
	mpi_request->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		mpi_request->EventMasks[i] =
		    cpu_to_le32(ioc->event_masks[i]);
	init_completion(&ioc->base_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done, 30*HZ);
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		ioc_err(ioc, "%s: timeout\n", __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2EventNotificationRequest_t)/4);
		if (ioc->base_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
	} else
		dinitprintk(ioc, ioc_info(ioc, "%s: complete\n", __func__));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	return r;
}

/**
 * mpt3sas_base_validate_event_type - validating event types
 * @ioc: per adapter object
 * @event_type: firmware event
 *
 * This will turn on firmware event notification when application
 * ask for that event. We don't mask events that are already enabled.
 */
void
mpt3sas_base_validate_event_type(struct MPT3SAS_ADAPTER *ioc, u32 *event_type)
{
	int i, j;
	u32 event_mask, desired_event;
	u8 send_update_to_fw;

	for (i = 0, send_update_to_fw = 0; i <
	    MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++) {
		event_mask = ~event_type[i];
		desired_event = 1;
		for (j = 0; j < 32; j++) {
			if (!(event_mask & desired_event) &&
			    (ioc->event_masks[i] & desired_event)) {
				ioc->event_masks[i] &= ~desired_event;
				send_update_to_fw = 1;
			}
			desired_event = (desired_event << 1);
		}
	}

	if (!send_update_to_fw)
		return;

	mutex_lock(&ioc->base_cmds.mutex);
	_base_event_notification(ioc);
	mutex_unlock(&ioc->base_cmds.mutex);
}

/**
 * _base_diag_reset - the "big hammer" start of day reset
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_diag_reset(struct MPT3SAS_ADAPTER *ioc)
{
	u32 host_diagnostic;
	u32 ioc_state;
	u32 count;
	u32 hcb_size;

	ioc_info(ioc, "sending diag reset !!\n");

	drsprintk(ioc, ioc_info(ioc, "clear interrupts\n"));

	count = 0;
	do {
		/* Write magic sequence to WriteSequence register
		 * Loop until in diagnostic mode
		 */
		drsprintk(ioc, ioc_info(ioc, "write magic sequence\n"));
		writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_1ST_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_2ND_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_3RD_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_4TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_5TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_6TH_KEY_VALUE, &ioc->chip->WriteSequence);

		/* wait 100 msec */
		msleep(100);

		if (count++ > 20)
			goto out;

		host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);
		drsprintk(ioc,
			  ioc_info(ioc, "wrote magic sequence: count(%d), host_diagnostic(0x%08x)\n",
				   count, host_diagnostic));

	} while ((host_diagnostic & MPI2_DIAG_DIAG_WRITE_ENABLE) == 0);

	hcb_size = ioc->base_readl(&ioc->chip->HCBSize);

	drsprintk(ioc, ioc_info(ioc, "diag reset: issued\n"));
	writel(host_diagnostic | MPI2_DIAG_RESET_ADAPTER,
	     &ioc->chip->HostDiagnostic);

	/*This delay allows the chip PCIe hardware time to finish reset tasks*/
	msleep(MPI2_HARD_RESET_PCIE_FIRST_READ_DELAY_MICRO_SEC/1000);

	/* Approximately 300 second max wait */
	for (count = 0; count < (300000000 /
		MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC); count++) {

		host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);

		if (host_diagnostic == 0xFFFFFFFF)
			goto out;
		if (!(host_diagnostic & MPI2_DIAG_RESET_ADAPTER))
			break;

		msleep(MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC / 1000);
	}

	if (host_diagnostic & MPI2_DIAG_HCB_MODE) {

		drsprintk(ioc,
			  ioc_info(ioc, "restart the adapter assuming the HCB Address points to good F/W\n"));
		host_diagnostic &= ~MPI2_DIAG_BOOT_DEVICE_SELECT_MASK;
		host_diagnostic |= MPI2_DIAG_BOOT_DEVICE_SELECT_HCDW;
		writel(host_diagnostic, &ioc->chip->HostDiagnostic);

		drsprintk(ioc, ioc_info(ioc, "re-enable the HCDW\n"));
		writel(hcb_size | MPI2_HCB_SIZE_HCB_ENABLE,
		    &ioc->chip->HCBSize);
	}

	drsprintk(ioc, ioc_info(ioc, "restart the adapter\n"));
	writel(host_diagnostic & ~MPI2_DIAG_HOLD_IOC_RESET,
	    &ioc->chip->HostDiagnostic);

	drsprintk(ioc,
		  ioc_info(ioc, "disable writes to the diagnostic register\n"));
	writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);

	drsprintk(ioc, ioc_info(ioc, "Wait for FW to go to the READY state\n"));
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, 20);
	if (ioc_state) {
		ioc_err(ioc, "%s: failed going to ready state (ioc_state=0x%x)\n",
			__func__, ioc_state);
		goto out;
	}

	ioc_info(ioc, "diag reset: SUCCESS\n");
	return 0;

 out:
	ioc_err(ioc, "diag reset: FAILED\n");
	return -EFAULT;
}

/**
 * _base_make_ioc_ready - put controller in READY state
 * @ioc: per adapter object
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_ready(struct MPT3SAS_ADAPTER *ioc, enum reset_type type)
{
	u32 ioc_state;
	int rc;
	int count;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	if (ioc->pci_error_recovery)
		return 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc,
		  ioc_info(ioc, "%s: ioc_state(0x%08x)\n",
			   __func__, ioc_state));

	/* if in RESET state, it should move to READY state shortly */
	count = 0;
	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_RESET) {
		while ((ioc_state & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_READY) {
			if (count++ == 10) {
				ioc_err(ioc, "%s: failed going to ready state (ioc_state=0x%x)\n",
					__func__, ioc_state);
				return -EFAULT;
			}
			ssleep(1);
			ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
		}
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, ioc_info(ioc, "unexpected doorbell active!\n"));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}

	if (type == FORCE_BIG_HAMMER)
		goto issue_diag_reset;

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		if (!(_base_send_ioc_reset(ioc,
		    MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET, 15))) {
			return 0;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_make_ioc_operational - put controller in OPERATIONAL state
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_operational(struct MPT3SAS_ADAPTER *ioc)
{
	int r, i, index;
	unsigned long	flags;
	u32 reply_address;
	u16 smid;
	struct _tr_list *delayed_tr, *delayed_tr_next;
	struct _sc_list *delayed_sc, *delayed_sc_next;
	struct _event_ack_list *delayed_event_ack, *delayed_event_ack_next;
	u8 hide_flag;
	struct adapter_reply_queue *reply_q;
	Mpi2ReplyDescriptorsUnion_t *reply_post_free_contig;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	/* clean the delayed target reset list */
	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}


	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_volume_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	list_for_each_entry_safe(delayed_sc, delayed_sc_next,
	    &ioc->delayed_sc_list, list) {
		list_del(&delayed_sc->list);
		kfree(delayed_sc);
	}

	list_for_each_entry_safe(delayed_event_ack, delayed_event_ack_next,
	    &ioc->delayed_event_ack_list, list) {
		list_del(&delayed_event_ack->list);
		kfree(delayed_event_ack);
	}

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);

	/* hi-priority queue */
	INIT_LIST_HEAD(&ioc->hpr_free_list);
	smid = ioc->hi_priority_smid;
	for (i = 0; i < ioc->hi_priority_depth; i++, smid++) {
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		ioc->hpr_lookup[i].smid = smid;
		list_add_tail(&ioc->hpr_lookup[i].tracker_list,
		    &ioc->hpr_free_list);
	}

	/* internal queue */
	INIT_LIST_HEAD(&ioc->internal_free_list);
	smid = ioc->internal_smid;
	for (i = 0; i < ioc->internal_depth; i++, smid++) {
		ioc->internal_lookup[i].cb_idx = 0xFF;
		ioc->internal_lookup[i].smid = smid;
		list_add_tail(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}

	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	/* initialize Reply Free Queue */
	for (i = 0, reply_address = (u32)ioc->reply_dma ;
	    i < ioc->reply_free_queue_depth ; i++, reply_address +=
	    ioc->reply_sz) {
		ioc->reply_free[i] = cpu_to_le32(reply_address);
		if (ioc->is_mcpu_endpoint)
			_base_clone_reply_to_sys_mem(ioc,
					reply_address, i);
	}

	/* initialize reply queues */
	if (ioc->is_driver_loading)
		_base_assign_reply_queues(ioc);

	/* initialize Reply Post Free Queue */
	index = 0;
	reply_post_free_contig = ioc->reply_post[0].reply_post_free;
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		/*
		 * If RDPQ is enabled, switch to the next allocation.
		 * Otherwise advance within the contiguous region.
		 */
		if (ioc->rdpq_array_enable) {
			reply_q->reply_post_free =
				ioc->reply_post[index++].reply_post_free;
		} else {
			reply_q->reply_post_free = reply_post_free_contig;
			reply_post_free_contig += ioc->reply_post_queue_depth;
		}

		reply_q->reply_post_host_index = 0;
		for (i = 0; i < ioc->reply_post_queue_depth; i++)
			reply_q->reply_post_free[i].Words =
			    cpu_to_le64(ULLONG_MAX);
		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_free_queue;
	}
 skip_init_reply_post_free_queue:

	r = _base_send_ioc_init(ioc);
	if (r)
		return r;

	/* initialize reply free host index */
	ioc->reply_free_host_index = ioc->reply_free_queue_depth - 1;
	writel(ioc->reply_free_host_index, &ioc->chip->ReplyFreeHostIndex);

	/* initialize reply post host index */
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->combined_reply_queue)
			writel((reply_q->msix_index & 7)<<
			   MPI2_RPHI_MSIX_INDEX_SHIFT,
			   ioc->replyPostRegisterIndex[reply_q->msix_index/8]);
		else
			writel(reply_q->msix_index <<
				MPI2_RPHI_MSIX_INDEX_SHIFT,
				&ioc->chip->ReplyPostHostIndex);

		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_host_index;
	}

 skip_init_reply_post_host_index:

	_base_unmask_interrupts(ioc);

	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		r = _base_display_fwpkg_version(ioc);
		if (r)
			return r;
	}

	_base_static_config_pages(ioc);
	r = _base_event_notification(ioc);
	if (r)
		return r;

	if (ioc->is_driver_loading) {

		if (ioc->is_warpdrive && ioc->manu_pg10.OEMIdentifier
		    == 0x80) {
			hide_flag = (u8) (
			    le32_to_cpu(ioc->manu_pg10.OEMSpecificFlags0) &
			    MFG_PAGE10_HIDE_SSDS_MASK);
			if (hide_flag != MFG_PAGE10_HIDE_SSDS_MASK)
				ioc->mfg_pg10_hide_flag = hide_flag;
		}

		ioc->wait_for_discovery_to_complete =
		    _base_determine_wait_on_discovery(ioc);

		return r; /* scan_start and scan_finished support */
	}

	r = _base_send_port_enable(ioc);
	if (r)
		return r;

	return r;
}

/**
 * mpt3sas_base_free_resources - free resources controller resources
 * @ioc: per adapter object
 */
void
mpt3sas_base_free_resources(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	/* synchronizing freeing resource with pci_access_mutex lock */
	mutex_lock(&ioc->pci_access_mutex);
	if (ioc->chip_phys && ioc->chip) {
		_base_mask_interrupts(ioc);
		ioc->shost_recovery = 1;
		_base_make_ioc_ready(ioc, SOFT_RESET);
		ioc->shost_recovery = 0;
	}

	mpt3sas_base_unmap_resources(ioc);
	mutex_unlock(&ioc->pci_access_mutex);
	return;
}

/**
 * mpt3sas_base_attach - attach controller instance
 * @ioc: per adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_base_attach(struct MPT3SAS_ADAPTER *ioc)
{
	int r, i;
	int cpu_id, last_cpu_id = 0;

	dinitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	/* setup cpu_msix_table */
	ioc->cpu_count = num_online_cpus();
	for_each_online_cpu(cpu_id)
		last_cpu_id = cpu_id;
	ioc->cpu_msix_table_sz = last_cpu_id + 1;
	ioc->cpu_msix_table = kzalloc(ioc->cpu_msix_table_sz, GFP_KERNEL);
	ioc->reply_queue_count = 1;
	if (!ioc->cpu_msix_table) {
		dfailprintk(ioc,
			    ioc_info(ioc, "allocation for cpu_msix_table failed!!!\n"));
		r = -ENOMEM;
		goto out_free_resources;
	}

	if (ioc->is_warpdrive) {
		ioc->reply_post_host_index = kcalloc(ioc->cpu_msix_table_sz,
		    sizeof(resource_size_t *), GFP_KERNEL);
		if (!ioc->reply_post_host_index) {
			dfailprintk(ioc,
				    ioc_info(ioc, "allocation for reply_post_host_index failed!!!\n"));
			r = -ENOMEM;
			goto out_free_resources;
		}
	}

	ioc->rdpq_array_enable_assigned = 0;
	ioc->dma_mask = 0;
	if (ioc->is_aero_ioc)
		ioc->base_readl = &_base_readl_aero;
	else
		ioc->base_readl = &_base_readl;
	r = mpt3sas_base_map_resources(ioc);
	if (r)
		goto out_free_resources;

	pci_set_drvdata(ioc->pdev, ioc->shost);
	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out_free_resources;

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		ioc->build_sg_scmd = &_base_build_sg_scmd;
		ioc->build_sg = &_base_build_sg;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge;
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		/*
		 * In SAS3.0,
		 * SCSI_IO, SMP_PASSTHRU, SATA_PASSTHRU, Target Assist, and
		 * Target Status - all require the IEEE formated scatter gather
		 * elements.
		 */
		ioc->build_sg_scmd = &_base_build_sg_scmd_ieee;
		ioc->build_sg = &_base_build_sg_ieee;
		ioc->build_nvme_prp = &_base_build_nvme_prp;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge_ieee;
		ioc->sge_size_ieee = sizeof(Mpi2IeeeSgeSimple64_t);

		break;
	}

	if (ioc->is_mcpu_endpoint)
		ioc->put_smid_scsi_io = &_base_put_smid_mpi_ep_scsi_io;
	else
		ioc->put_smid_scsi_io = &_base_put_smid_scsi_io;

	/*
	 * These function pointers for other requests that don't
	 * the require IEEE scatter gather elements.
	 *
	 * For example Configuration Pages and SAS IOUNIT Control don't.
	 */
	ioc->build_sg_mpi = &_base_build_sg;
	ioc->build_zero_len_sge_mpi = &_base_build_zero_len_sge;

	r = _base_make_ioc_ready(ioc, SOFT_RESET);
	if (r)
		goto out_free_resources;

	ioc->pfacts = kcalloc(ioc->facts.NumberOfPorts,
	    sizeof(struct mpt3sas_port_facts), GFP_KERNEL);
	if (!ioc->pfacts) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0 ; i < ioc->facts.NumberOfPorts; i++) {
		r = _base_get_port_facts(ioc, i);
		if (r)
			goto out_free_resources;
	}

	r = _base_allocate_memory_pools(ioc);
	if (r)
		goto out_free_resources;

	init_waitqueue_head(&ioc->reset_wq);

	/* allocate memory pd handle bitmask list */
	ioc->pd_handles_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pd_handles_sz++;
	ioc->pd_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->pd_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}
	ioc->blocking_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->blocking_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	/* allocate memory for pending OS device add list */
	ioc->pend_os_device_add_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pend_os_device_add_sz++;
	ioc->pend_os_device_add = kzalloc(ioc->pend_os_device_add_sz,
	    GFP_KERNEL);
	if (!ioc->pend_os_device_add)
		goto out_free_resources;

	ioc->device_remove_in_progress_sz = ioc->pend_os_device_add_sz;
	ioc->device_remove_in_progress =
		kzalloc(ioc->device_remove_in_progress_sz, GFP_KERNEL);
	if (!ioc->device_remove_in_progress)
		goto out_free_resources;

	ioc->fwfault_debug = mpt3sas_fwfault_debug;

	/* base internal command bits */
	mutex_init(&ioc->base_cmds.mutex);
	ioc->base_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;

	/* port_enable command bits */
	ioc->port_enable_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;

	/* transport internal command bits */
	ioc->transport_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->transport_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->transport_cmds.mutex);

	/* scsih internal command bits */
	ioc->scsih_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->scsih_cmds.mutex);

	/* task management internal command bits */
	ioc->tm_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->tm_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->tm_cmds.mutex);

	/* config page internal command bits */
	ioc->config_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->config_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->config_cmds.mutex);

	/* ctl module internal command bits */
	ioc->ctl_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->ctl_cmds.sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_KERNEL);
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->ctl_cmds.mutex);

	if (!ioc->base_cmds.reply || !ioc->port_enable_cmds.reply ||
	    !ioc->transport_cmds.reply || !ioc->scsih_cmds.reply ||
	    !ioc->tm_cmds.reply || !ioc->config_cmds.reply ||
	    !ioc->ctl_cmds.reply || !ioc->ctl_cmds.sense) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		ioc->event_masks[i] = -1;

	/* here we enable the events we care about */
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DISCOVERY);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_BROADCAST_PRIMITIVE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_IR_VOLUME);
	_base_unmask_events(ioc, MPI2_EVENT_IR_PHYSICAL_DISK);
	_base_unmask_events(ioc, MPI2_EVENT_IR_OPERATION_STATUS);
	_base_unmask_events(ioc, MPI2_EVENT_LOG_ENTRY_ADDED);
	_base_unmask_events(ioc, MPI2_EVENT_TEMP_THRESHOLD);
	_base_unmask_events(ioc, MPI2_EVENT_ACTIVE_CABLE_EXCEPTION);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR);
	if (ioc->hba_mpi_version_belonged == MPI26_VERSION) {
		if (ioc->is_gen35_ioc) {
			_base_unmask_events(ioc,
				MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE);
			_base_unmask_events(ioc, MPI2_EVENT_PCIE_ENUMERATION);
			_base_unmask_events(ioc,
				MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST);
		}
	}
	r = _base_make_ioc_operational(ioc);
	if (r)
		goto out_free_resources;

	ioc->non_operational_loop = 0;
	ioc->got_task_abort_from_ioctl = 0;
	return 0;

 out_free_resources:

	ioc->remove_host = 1;

	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->pfacts);
	ioc->ctl_cmds.reply = NULL;
	ioc->base_cmds.reply = NULL;
	ioc->tm_cmds.reply = NULL;
	ioc->scsih_cmds.reply = NULL;
	ioc->transport_cmds.reply = NULL;
	ioc->config_cmds.reply = NULL;
	ioc->pfacts = NULL;
	return r;
}


/**
 * mpt3sas_base_detach - remove controller instance
 * @ioc: per adapter object
 */
void
mpt3sas_base_detach(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, ioc_info(ioc, "%s\n", __func__));

	mpt3sas_base_stop_watchdog(ioc);
	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	mpt3sas_free_enclosure_list(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->pfacts);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
}

/**
 * _base_pre_reset_handler - pre reset handler
 * @ioc: per adapter object
 */
static void _base_pre_reset_handler(struct MPT3SAS_ADAPTER *ioc)
{
	mpt3sas_scsih_pre_reset_handler(ioc);
	mpt3sas_ctl_pre_reset_handler(ioc);
	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_PRE_RESET\n", __func__));
}

/**
 * _base_after_reset_handler - after reset handler
 * @ioc: per adapter object
 */
static void _base_after_reset_handler(struct MPT3SAS_ADAPTER *ioc)
{
	mpt3sas_scsih_after_reset_handler(ioc);
	mpt3sas_ctl_after_reset_handler(ioc);
	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_AFTER_RESET\n", __func__));
	if (ioc->transport_cmds.status & MPT3_CMD_PENDING) {
		ioc->transport_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->transport_cmds.smid);
		complete(&ioc->transport_cmds.done);
	}
	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		ioc->base_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->base_cmds.smid);
		complete(&ioc->base_cmds.done);
	}
	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		ioc->port_enable_failed = 1;
		ioc->port_enable_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->port_enable_cmds.smid);
		if (ioc->is_driver_loading) {
			ioc->start_scan_failed =
				MPI2_IOCSTATUS_INTERNAL_ERROR;
			ioc->start_scan = 0;
			ioc->port_enable_cmds.status =
				MPT3_CMD_NOT_USED;
		} else {
			complete(&ioc->port_enable_cmds.done);
		}
	}
	if (ioc->config_cmds.status & MPT3_CMD_PENDING) {
		ioc->config_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->config_cmds.smid);
		ioc->config_cmds.smid = USHRT_MAX;
		complete(&ioc->config_cmds.done);
	}
}

/**
 * _base_reset_done_handler - reset done handler
 * @ioc: per adapter object
 */
static void _base_reset_done_handler(struct MPT3SAS_ADAPTER *ioc)
{
	mpt3sas_scsih_reset_done_handler(ioc);
	mpt3sas_ctl_reset_done_handler(ioc);
	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_DONE_RESET\n", __func__));
}

/**
 * mpt3sas_wait_for_commands_to_complete - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 *
 * This function is waiting 10s for all pending commands to complete
 * prior to putting controller in reset.
 */
void
mpt3sas_wait_for_commands_to_complete(struct MPT3SAS_ADAPTER *ioc)
{
	u32 ioc_state;

	ioc->pending_io_count = 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	if ((ioc_state & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL)
		return;

	/* pending command count */
	ioc->pending_io_count = scsi_host_busy(ioc->shost);

	if (!ioc->pending_io_count)
		return;

	/* wait for pending commands to complete */
	wait_event_timeout(ioc->reset_wq, ioc->pending_io_count == 0, 10 * HZ);
}

/**
 * mpt3sas_base_hard_reset_handler - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_base_hard_reset_handler(struct MPT3SAS_ADAPTER *ioc,
	enum reset_type type)
{
	int r;
	unsigned long flags;
	u32 ioc_state;
	u8 is_fault = 0, is_trigger = 0;

	dtmprintk(ioc, ioc_info(ioc, "%s: enter\n", __func__));

	if (ioc->pci_error_recovery) {
		ioc_err(ioc, "%s: pci error recovery reset\n", __func__);
		r = 0;
		goto out_unlocked;
	}

	if (mpt3sas_fwfault_debug)
		mpt3sas_halt_firmware(ioc);

	/* wait for an active reset in progress to complete */
	mutex_lock(&ioc->reset_in_progress_mutex);

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->shost_recovery = 1;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) &&
	    (!(ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED))) {
		is_trigger = 1;
		ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
		if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
			is_fault = 1;
	}
	_base_pre_reset_handler(ioc);
	mpt3sas_wait_for_commands_to_complete(ioc);
	_base_mask_interrupts(ioc);
	r = _base_make_ioc_ready(ioc, type);
	if (r)
		goto out;
	_base_after_reset_handler(ioc);

	/* If this hard reset is called while port enable is active, then
	 * there is no reason to call make_ioc_operational
	 */
	if (ioc->is_driver_loading && ioc->port_enable_failed) {
		ioc->remove_host = 1;
		r = -EFAULT;
		goto out;
	}
	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out;

	if (ioc->rdpq_array_enable && !ioc->rdpq_array_capable)
		panic("%s: Issue occurred with flashing controller firmware."
		      "Please reboot the system and ensure that the correct"
		      " firmware version is running\n", ioc->name);

	r = _base_make_ioc_operational(ioc);
	if (!r)
		_base_reset_done_handler(ioc);

 out:
	dtmprintk(ioc,
		  ioc_info(ioc, "%s: %s\n",
			   __func__, r == 0 ? "SUCCESS" : "FAILED"));

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->shost_recovery = 0;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_count++;
	mutex_unlock(&ioc->reset_in_progress_mutex);

 out_unlocked:
	if ((r == 0) && is_trigger) {
		if (is_fault)
			mpt3sas_trigger_master(ioc, MASTER_TRIGGER_FW_FAULT);
		else
			mpt3sas_trigger_master(ioc,
			    MASTER_TRIGGER_ADAPTER_RESET);
	}
	dtmprintk(ioc, ioc_info(ioc, "%s: exit\n", __func__));
	return r;
}

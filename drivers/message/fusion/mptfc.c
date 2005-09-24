/*
 *  linux/drivers/message/fusion/mptfc.c
 *      For use with LSI Logic PCI chip/adapter(s)
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2005 LSI Logic Corporation
 *  (mailto:mpt_linux_developer@lsil.com)
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#include "linux_compat.h"	/* linux-2.6 tweaks */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>	/* for mdelay */
#include <linux/interrupt.h>	/* needed for in_interrupt() proto */
#include <linux/reboot.h>	/* notifier code */
#include <linux/sched.h>
#include <linux/workqueue.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "mptbase.h"
#include "mptscsih.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT FC Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptfc"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");

/* Command line args */
static int mpt_pq_filter = 0;
module_param(mpt_pq_filter, int, 0);
MODULE_PARM_DESC(mpt_pq_filter, " Enable peripheral qualifier filter: enable=1  (default=0)");

static int	mptfcDoneCtx = -1;
static int	mptfcTaskCtx = -1;
static int	mptfcInternalCtx = -1; /* Used only for internal commands */

static struct scsi_host_template mptfc_driver_template = {
	.proc_name			= "mptfc",
	.proc_info			= mptscsih_proc_info,
	.name				= "MPT FC Host",
	.info				= mptscsih_info,
	.queuecommand			= mptscsih_qcmd,
	.slave_alloc			= mptscsih_slave_alloc,
	.slave_configure		= mptscsih_slave_configure,
	.slave_destroy			= mptscsih_slave_destroy,
	.change_queue_depth 		= mptscsih_change_queue_depth,
	.eh_abort_handler		= mptscsih_abort,
	.eh_device_reset_handler	= mptscsih_dev_reset,
	.eh_bus_reset_handler		= mptscsih_bus_reset,
	.eh_host_reset_handler		= mptscsih_host_reset,
	.bios_param			= mptscsih_bios_param,
	.can_queue			= MPT_FC_CAN_QUEUE,
	.this_id			= -1,
	.sg_tablesize			= MPT_SCSI_SG_DEPTH,
	.max_sectors			= 8192,
	.cmd_per_lun			= 7,
	.use_clustering			= ENABLE_CLUSTERING,
};

/****************************************************************************
 * Supported hardware
 */

static struct pci_device_id mptfc_pci_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC909,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC919,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC929,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC919X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC929X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC939X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC949X,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptfc_pci_table);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptfc_probe - Installs scsi devices per bus.
 *	@pdev: Pointer to pci_dev structure
 *
 *	Returns 0 for success, non-zero for failure.
 *
 */
static int
mptfc_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host	*sh;
	MPT_SCSI_HOST		*hd;
	MPT_ADAPTER 		*ioc;
	unsigned long		 flags;
	int			 sz, ii;
	int			 numSGE = 0;
	int			 scale;
	int			 ioc_cap;
	u8			*mem;
	int			error=0;
	int			r;
		
	if ((r = mpt_attach(pdev,id)) != 0)
		return r;
	
	ioc = pci_get_drvdata(pdev);
	ioc->DoneCtx = mptfcDoneCtx;
	ioc->TaskCtx = mptfcTaskCtx;
	ioc->InternalCtx = mptfcInternalCtx;

	/*  Added sanity check on readiness of the MPT adapter.
	 */
	if (ioc->last_state != MPI_IOC_STATE_OPERATIONAL) {
		printk(MYIOC_s_WARN_FMT
		  "Skipping because it's not operational!\n",
		  ioc->name);
		return -ENODEV;
	}

	if (!ioc->active) {
		printk(MYIOC_s_WARN_FMT "Skipping because it's disabled!\n",
		  ioc->name);
		return -ENODEV;
	}

	/*  Sanity check - ensure at least 1 port is INITIATOR capable
	 */
	ioc_cap = 0;
	for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
		if (ioc->pfacts[ii].ProtocolFlags &
		    MPI_PORTFACTS_PROTOCOL_INITIATOR)
			ioc_cap ++;
	}

	if (!ioc_cap) {
		printk(MYIOC_s_WARN_FMT
			"Skipping ioc=%p because SCSI Initiator mode is NOT enabled!\n",
			ioc->name, ioc);
		return 0;
	}

	sh = scsi_host_alloc(&mptfc_driver_template, sizeof(MPT_SCSI_HOST));

	if (!sh) {
		printk(MYIOC_s_WARN_FMT
			"Unable to register controller with SCSI subsystem\n",
			ioc->name);
                return -1;
        }

	spin_lock_irqsave(&ioc->FreeQlock, flags);

	/* Attach the SCSI Host to the IOC structure
	 */
	ioc->sh = sh;

	sh->io_port = 0;
	sh->n_io_port = 0;
	sh->irq = 0;

	/* set 16 byte cdb's */
	sh->max_cmd_len = 16;

	sh->max_id = MPT_MAX_FC_DEVICES<256 ? MPT_MAX_FC_DEVICES : 255;

	sh->max_lun = MPT_LAST_LUN + 1;
	sh->max_channel = 0;
	sh->this_id = ioc->pfacts[0].PortSCSIID;

	/* Required entry.
	 */
	sh->unique_id = ioc->id;

	/* Verify that we won't exceed the maximum
	 * number of chain buffers
	 * We can optimize:  ZZ = req_sz/sizeof(SGE)
	 * For 32bit SGE's:
	 *  numSGE = 1 + (ZZ-1)*(maxChain -1) + ZZ
	 *               + (req_sz - 64)/sizeof(SGE)
	 * A slightly different algorithm is required for
	 * 64bit SGEs.
	 */
	scale = ioc->req_sz/(sizeof(dma_addr_t) + sizeof(u32));
	if (sizeof(dma_addr_t) == sizeof(u64)) {
		numSGE = (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 60) / (sizeof(dma_addr_t) +
		  sizeof(u32));
	} else {
		numSGE = 1 + (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 64) / (sizeof(dma_addr_t) +
		  sizeof(u32));
	}

	if (numSGE < sh->sg_tablesize) {
		/* Reset this value */
		dprintk((MYIOC_s_INFO_FMT
		  "Resetting sg_tablesize to %d from %d\n",
		  ioc->name, numSGE, sh->sg_tablesize));
		sh->sg_tablesize = numSGE;
	}

	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	hd = (MPT_SCSI_HOST *) sh->hostdata;
	hd->ioc = ioc;

	/* SCSI needs scsi_cmnd lookup table!
	 * (with size equal to req_depth*PtrSz!)
	 */
	sz = ioc->req_depth * sizeof(void *);
	mem = kmalloc(sz, GFP_ATOMIC);
	if (mem == NULL) {
		error = -ENOMEM;
		goto mptfc_probe_failed;
	}

	memset(mem, 0, sz);
	hd->ScsiLookup = (struct scsi_cmnd **) mem;

	dprintk((MYIOC_s_INFO_FMT "ScsiLookup @ %p, sz=%d\n",
		 ioc->name, hd->ScsiLookup, sz));

	/* Allocate memory for the device structures.
	 * A non-Null pointer at an offset
	 * indicates a device exists.
	 * max_id = 1 + maximum id (hosts.h)
	 */
	sz = sh->max_id * sizeof(void *);
	mem = kmalloc(sz, GFP_ATOMIC);
	if (mem == NULL) {
		error = -ENOMEM;
		goto mptfc_probe_failed;
	}

	memset(mem, 0, sz);
	hd->Targets = (VirtDevice **) mem;

	dprintk((KERN_INFO
	  "  Targets @ %p, sz=%d\n", hd->Targets, sz));

	/* Clear the TM flags
	 */
	hd->tmPending = 0;
	hd->tmState = TM_STATE_NONE;
	hd->resetPending = 0;
	hd->abortSCpnt = NULL;

	/* Clear the pointer used to store
	 * single-threaded commands, i.e., those
	 * issued during a bus scan, dv and
	 * configuration pages.
	 */
	hd->cmdPtr = NULL;

	/* Initialize this SCSI Hosts' timers
	 * To use, set the timer expires field
	 * and add_timer
	 */
	init_timer(&hd->timer);
	hd->timer.data = (unsigned long) hd;
	hd->timer.function = mptscsih_timer_expired;

	hd->mpt_pq_filter = mpt_pq_filter;

	ddvprintk((MYIOC_s_INFO_FMT
		"mpt_pq_filter %x\n",
		ioc->name, 
		mpt_pq_filter));

	init_waitqueue_head(&hd->scandv_waitq);
	hd->scandv_wait_done = 0;
	hd->last_queue_full = 0;

	error = scsi_add_host (sh, &ioc->pcidev->dev);
	if(error) {
		dprintk((KERN_ERR MYNAM
		  "scsi_add_host failed\n"));
		goto mptfc_probe_failed;
	}

	scsi_scan_host(sh);
	return 0;

mptfc_probe_failed:

	mptscsih_remove(pdev);
	return error;
}

static struct pci_driver mptfc_driver = {
	.name		= "mptfc",
	.id_table	= mptfc_pci_table,
	.probe		= mptfc_probe,
	.remove		= __devexit_p(mptscsih_remove),
	.shutdown	= mptscsih_shutdown,
#ifdef CONFIG_PM
	.suspend	= mptscsih_suspend,
	.resume		= mptscsih_resume,
#endif
};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptfc_init - Register MPT adapter(s) as SCSI host(s) with
 *	linux scsi mid-layer.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int __init
mptfc_init(void)
{

	show_mptmod_ver(my_NAME, my_VERSION);

	mptfcDoneCtx = mpt_register(mptscsih_io_done, MPTFC_DRIVER);
	mptfcTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTFC_DRIVER);
	mptfcInternalCtx = mpt_register(mptscsih_scandv_complete, MPTFC_DRIVER);

	if (mpt_event_register(mptfcDoneCtx, mptscsih_event_process) == 0) {
		devtprintk((KERN_INFO MYNAM
		  ": Registered for IOC event notifications\n"));
	}

	if (mpt_reset_register(mptfcDoneCtx, mptscsih_ioc_reset) == 0) {
		dprintk((KERN_INFO MYNAM
		  ": Registered for IOC reset notifications\n"));
	}

	return pci_register_driver(&mptfc_driver);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptfc_exit - Unregisters MPT adapter(s)
 *
 */
static void __exit
mptfc_exit(void)
{
	pci_unregister_driver(&mptfc_driver);
	
	mpt_reset_deregister(mptfcDoneCtx);
	dprintk((KERN_INFO MYNAM
	  ": Deregistered for IOC reset notifications\n"));

	mpt_event_deregister(mptfcDoneCtx);
	dprintk((KERN_INFO MYNAM
	  ": Deregistered for IOC event notifications\n"));

	mpt_deregister(mptfcInternalCtx);
	mpt_deregister(mptfcTaskCtx);
	mpt_deregister(mptfcDoneCtx);
}

module_init(mptfc_init);
module_exit(mptfc_exit);

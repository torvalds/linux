/*
 *  linux/drivers/message/fusion/mptbase.c
 *      This is the Fusion MPT base driver which supports multiple
 *      (SCSI + LAN) specialized protocol drivers.
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>		/* needed for in_interrupt() proto */
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <scsi/scsi_host.h>

#include "mptbase.h"
#include "lsi/mpi_log_fc.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT base driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptbase"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(my_VERSION);

/*
 *  cmd line parameters
 */

static int mpt_msi_enable_spi;
module_param(mpt_msi_enable_spi, int, 0);
MODULE_PARM_DESC(mpt_msi_enable_spi,
		 " Enable MSI Support for SPI controllers (default=0)");

static int mpt_msi_enable_fc;
module_param(mpt_msi_enable_fc, int, 0);
MODULE_PARM_DESC(mpt_msi_enable_fc,
		 " Enable MSI Support for FC controllers (default=0)");

static int mpt_msi_enable_sas;
module_param(mpt_msi_enable_sas, int, 0);
MODULE_PARM_DESC(mpt_msi_enable_sas,
		 " Enable MSI Support for SAS controllers (default=0)");

static int mpt_channel_mapping;
module_param(mpt_channel_mapping, int, 0);
MODULE_PARM_DESC(mpt_channel_mapping, " Mapping id's to channels (default=0)");

static int mpt_debug_level;
static int mpt_set_debug_level(const char *val, const struct kernel_param *kp);
module_param_call(mpt_debug_level, mpt_set_debug_level, param_get_int,
		  &mpt_debug_level, 0600);
MODULE_PARM_DESC(mpt_debug_level,
		 " debug level - refer to mptdebug.h - (default=0)");

int mpt_fwfault_debug;
EXPORT_SYMBOL(mpt_fwfault_debug);
module_param(mpt_fwfault_debug, int, 0600);
MODULE_PARM_DESC(mpt_fwfault_debug,
		 "Enable detection of Firmware fault and halt Firmware on fault - (default=0)");

static char	MptCallbacksName[MPT_MAX_PROTOCOL_DRIVERS]
				[MPT_MAX_CALLBACKNAME_LEN+1];

#ifdef MFCNT
static int mfcounter = 0;
#define PRINT_MF_COUNT 20000
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Public data...
 */

#define WHOINIT_UNKNOWN		0xAA

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Private data...
 */
					/* Adapter link list */
LIST_HEAD(ioc_list);
					/* Callback lookup table */
static MPT_CALLBACK		 MptCallbacks[MPT_MAX_PROTOCOL_DRIVERS];
					/* Protocol driver class lookup table */
static int			 MptDriverClass[MPT_MAX_PROTOCOL_DRIVERS];
					/* Event handler lookup table */
static MPT_EVHANDLER		 MptEvHandlers[MPT_MAX_PROTOCOL_DRIVERS];
					/* Reset handler lookup table */
static MPT_RESETHANDLER		 MptResetHandlers[MPT_MAX_PROTOCOL_DRIVERS];
static struct mpt_pci_driver 	*MptDeviceDriverHandlers[MPT_MAX_PROTOCOL_DRIVERS];

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry 	*mpt_proc_root_dir;
#endif

/*
 *  Driver Callback Index's
 */
static u8 mpt_base_index = MPT_MAX_PROTOCOL_DRIVERS;
static u8 last_drv_idx;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Forward protos...
 */
static irqreturn_t mpt_interrupt(int irq, void *bus_id);
static int	mptbase_reply(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req,
		MPT_FRAME_HDR *reply);
static int	mpt_handshake_req_reply_wait(MPT_ADAPTER *ioc, int reqBytes,
			u32 *req, int replyBytes, u16 *u16reply, int maxwait,
			int sleepFlag);
static int	mpt_do_ioc_recovery(MPT_ADAPTER *ioc, u32 reason, int sleepFlag);
static void	mpt_detect_bound_ports(MPT_ADAPTER *ioc, struct pci_dev *pdev);
static void	mpt_adapter_disable(MPT_ADAPTER *ioc);
static void	mpt_adapter_dispose(MPT_ADAPTER *ioc);

static void	MptDisplayIocCapabilities(MPT_ADAPTER *ioc);
static int	MakeIocReady(MPT_ADAPTER *ioc, int force, int sleepFlag);
static int	GetIocFacts(MPT_ADAPTER *ioc, int sleepFlag, int reason);
static int	GetPortFacts(MPT_ADAPTER *ioc, int portnum, int sleepFlag);
static int	SendIocInit(MPT_ADAPTER *ioc, int sleepFlag);
static int	SendPortEnable(MPT_ADAPTER *ioc, int portnum, int sleepFlag);
static int	mpt_do_upload(MPT_ADAPTER *ioc, int sleepFlag);
static int	mpt_downloadboot(MPT_ADAPTER *ioc, MpiFwHeader_t *pFwHeader, int sleepFlag);
static int	mpt_diag_reset(MPT_ADAPTER *ioc, int ignore, int sleepFlag);
static int	KickStart(MPT_ADAPTER *ioc, int ignore, int sleepFlag);
static int	SendIocReset(MPT_ADAPTER *ioc, u8 reset_type, int sleepFlag);
static int	PrimeIocFifos(MPT_ADAPTER *ioc);
static int	WaitForDoorbellAck(MPT_ADAPTER *ioc, int howlong, int sleepFlag);
static int	WaitForDoorbellInt(MPT_ADAPTER *ioc, int howlong, int sleepFlag);
static int	WaitForDoorbellReply(MPT_ADAPTER *ioc, int howlong, int sleepFlag);
static int	GetLanConfigPages(MPT_ADAPTER *ioc);
static int	GetIoUnitPage2(MPT_ADAPTER *ioc);
int		mptbase_sas_persist_operation(MPT_ADAPTER *ioc, u8 persist_opcode);
static int	mpt_GetScsiPortSettings(MPT_ADAPTER *ioc, int portnum);
static int	mpt_readScsiDevicePageHeaders(MPT_ADAPTER *ioc, int portnum);
static void 	mpt_read_ioc_pg_1(MPT_ADAPTER *ioc);
static void 	mpt_read_ioc_pg_4(MPT_ADAPTER *ioc);
static void	mpt_get_manufacturing_pg_0(MPT_ADAPTER *ioc);
static int	SendEventNotification(MPT_ADAPTER *ioc, u8 EvSwitch,
	int sleepFlag);
static int	SendEventAck(MPT_ADAPTER *ioc, EventNotificationReply_t *evnp);
static int	mpt_host_page_access_control(MPT_ADAPTER *ioc, u8 access_control_value, int sleepFlag);
static int	mpt_host_page_alloc(MPT_ADAPTER *ioc, pIOCInit_t ioc_init);

#ifdef CONFIG_PROC_FS
static int mpt_summary_proc_show(struct seq_file *m, void *v);
static int mpt_version_proc_show(struct seq_file *m, void *v);
static int mpt_iocinfo_proc_show(struct seq_file *m, void *v);
#endif
static void	mpt_get_fw_exp_ver(char *buf, MPT_ADAPTER *ioc);

static int	ProcessEventNotification(MPT_ADAPTER *ioc,
		EventNotificationReply_t *evReply, int *evHandlers);
static void	mpt_iocstatus_info(MPT_ADAPTER *ioc, u32 ioc_status, MPT_FRAME_HDR *mf);
static void	mpt_fc_log_info(MPT_ADAPTER *ioc, u32 log_info);
static void	mpt_spi_log_info(MPT_ADAPTER *ioc, u32 log_info);
static void	mpt_sas_log_info(MPT_ADAPTER *ioc, u32 log_info , u8 cb_idx);
static int	mpt_read_ioc_pg_3(MPT_ADAPTER *ioc);
static void	mpt_inactive_raid_list_free(MPT_ADAPTER *ioc);

/* module entry point */
static int  __init    fusion_init  (void);
static void __exit    fusion_exit  (void);

#define CHIPREG_READ32(addr) 		readl_relaxed(addr)
#define CHIPREG_READ32_dmasync(addr)	readl(addr)
#define CHIPREG_WRITE32(addr,val) 	writel(val, addr)
#define CHIPREG_PIO_WRITE32(addr,val)	outl(val, (unsigned long)addr)
#define CHIPREG_PIO_READ32(addr) 	inl((unsigned long)addr)

static void
pci_disable_io_access(struct pci_dev *pdev)
{
	u16 command_reg;

	pci_read_config_word(pdev, PCI_COMMAND, &command_reg);
	command_reg &= ~1;
	pci_write_config_word(pdev, PCI_COMMAND, command_reg);
}

static void
pci_enable_io_access(struct pci_dev *pdev)
{
	u16 command_reg;

	pci_read_config_word(pdev, PCI_COMMAND, &command_reg);
	command_reg |= 1;
	pci_write_config_word(pdev, PCI_COMMAND, command_reg);
}

static int mpt_set_debug_level(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	MPT_ADAPTER *ioc;

	if (ret)
		return ret;

	list_for_each_entry(ioc, &ioc_list, list)
		ioc->debug_level = mpt_debug_level;
	return 0;
}

/**
 *	mpt_get_cb_idx - obtain cb_idx for registered driver
 *	@dclass: class driver enum
 *
 *	Returns cb_idx, or zero means it wasn't found
 **/
static u8
mpt_get_cb_idx(MPT_DRIVER_CLASS dclass)
{
	u8 cb_idx;

	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--)
		if (MptDriverClass[cb_idx] == dclass)
			return cb_idx;
	return 0;
}

/**
 * mpt_is_discovery_complete - determine if discovery has completed
 * @ioc: per adatper instance
 *
 * Returns 1 when discovery completed, else zero.
 */
static int
mpt_is_discovery_complete(MPT_ADAPTER *ioc)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasIOUnitPage0_t *buffer;
	dma_addr_t dma_handle;
	int rc = 0;

	memset(&hdr, 0, sizeof(ConfigExtendedPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));
	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	cfg.cfghdr.ehdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	if ((mpt_config(ioc, &cfg)))
		goto out;
	if (!hdr.ExtPageLength)
		goto out;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
	    &dma_handle);
	if (!buffer)
		goto out;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((mpt_config(ioc, &cfg)))
		goto out_free_consistent;

	if (!(buffer->PhyData[0].PortFlags &
	    MPI_SAS_IOUNIT0_PORT_FLAGS_DISCOVERY_IN_PROGRESS))
		rc = 1;

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
	    buffer, dma_handle);
 out:
	return rc;
}


/**
 *  mpt_remove_dead_ioc_func - kthread context to remove dead ioc
 * @arg: input argument, used to derive ioc
 *
 * Return 0 if controller is removed from pci subsystem.
 * Return -1 for other case.
 */
static int mpt_remove_dead_ioc_func(void *arg)
{
	MPT_ADAPTER *ioc = (MPT_ADAPTER *)arg;
	struct pci_dev *pdev;

	if ((ioc == NULL))
		return -1;

	pdev = ioc->pcidev;
	if ((pdev == NULL))
		return -1;

	pci_stop_and_remove_bus_device_locked(pdev);
	return 0;
}



/**
 *	mpt_fault_reset_work - work performed on workq after ioc fault
 *	@work: input argument, used to derive ioc
 *
**/
static void
mpt_fault_reset_work(struct work_struct *work)
{
	MPT_ADAPTER	*ioc =
	    container_of(work, MPT_ADAPTER, fault_reset_work.work);
	u32		 ioc_raw_state;
	int		 rc;
	unsigned long	 flags;
	MPT_SCSI_HOST	*hd;
	struct task_struct *p;

	if (ioc->ioc_reset_in_progress || !ioc->active)
		goto out;


	ioc_raw_state = mpt_GetIocState(ioc, 0);
	if ((ioc_raw_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_MASK) {
		printk(MYIOC_s_INFO_FMT "%s: IOC is non-operational !!!!\n",
		    ioc->name, __func__);

		/*
		 * Call mptscsih_flush_pending_cmds callback so that we
		 * flush all pending commands back to OS.
		 * This call is required to aovid deadlock at block layer.
		 * Dead IOC will fail to do diag reset,and this call is safe
		 * since dead ioc will never return any command back from HW.
		 */
		hd = shost_priv(ioc->sh);
		ioc->schedule_dead_ioc_flush_running_cmds(hd);

		/*Remove the Dead Host */
		p = kthread_run(mpt_remove_dead_ioc_func, ioc,
				"mpt_dead_ioc_%d", ioc->id);
		if (IS_ERR(p))	{
			printk(MYIOC_s_ERR_FMT
				"%s: Running mpt_dead_ioc thread failed !\n",
				ioc->name, __func__);
		} else {
			printk(MYIOC_s_WARN_FMT
				"%s: Running mpt_dead_ioc thread success !\n",
				ioc->name, __func__);
		}
		return; /* don't rearm timer */
	}

	if ((ioc_raw_state & MPI_IOC_STATE_MASK)
			== MPI_IOC_STATE_FAULT) {
		printk(MYIOC_s_WARN_FMT "IOC is in FAULT state (%04xh)!!!\n",
		       ioc->name, ioc_raw_state & MPI_DOORBELL_DATA_MASK);
		printk(MYIOC_s_WARN_FMT "Issuing HardReset from %s!!\n",
		       ioc->name, __func__);
		rc = mpt_HardResetHandler(ioc, CAN_SLEEP);
		printk(MYIOC_s_WARN_FMT "%s: HardReset: %s\n", ioc->name,
		       __func__, (rc == 0) ? "success" : "failed");
		ioc_raw_state = mpt_GetIocState(ioc, 0);
		if ((ioc_raw_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_FAULT)
			printk(MYIOC_s_WARN_FMT "IOC is in FAULT state after "
			    "reset (%04xh)\n", ioc->name, ioc_raw_state &
			    MPI_DOORBELL_DATA_MASK);
	} else if (ioc->bus_type == SAS && ioc->sas_discovery_quiesce_io) {
		if ((mpt_is_discovery_complete(ioc))) {
			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "clearing "
			    "discovery_quiesce_io flag\n", ioc->name));
			ioc->sas_discovery_quiesce_io = 0;
		}
	}

 out:
	/*
	 * Take turns polling alternate controller
	 */
	if (ioc->alt_ioc)
		ioc = ioc->alt_ioc;

	/* rearm the timer */
	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->reset_work_q)
		queue_delayed_work(ioc->reset_work_q, &ioc->fault_reset_work,
			msecs_to_jiffies(MPT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
}


/*
 *  Process turbo (context) reply...
 */
static void
mpt_turbo_reply(MPT_ADAPTER *ioc, u32 pa)
{
	MPT_FRAME_HDR *mf = NULL;
	MPT_FRAME_HDR *mr = NULL;
	u16 req_idx = 0;
	u8 cb_idx;

	dmfprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Got TURBO reply req_idx=%08x\n",
				ioc->name, pa));

	switch (pa >> MPI_CONTEXT_REPLY_TYPE_SHIFT) {
	case MPI_CONTEXT_REPLY_TYPE_SCSI_INIT:
		req_idx = pa & 0x0000FFFF;
		cb_idx = (pa & 0x00FF0000) >> 16;
		mf = MPT_INDEX_2_MFPTR(ioc, req_idx);
		break;
	case MPI_CONTEXT_REPLY_TYPE_LAN:
		cb_idx = mpt_get_cb_idx(MPTLAN_DRIVER);
		/*
		 *  Blind set of mf to NULL here was fatal
		 *  after lan_reply says "freeme"
		 *  Fix sort of combined with an optimization here;
		 *  added explicit check for case where lan_reply
		 *  was just returning 1 and doing nothing else.
		 *  For this case skip the callback, but set up
		 *  proper mf value first here:-)
		 */
		if ((pa & 0x58000000) == 0x58000000) {
			req_idx = pa & 0x0000FFFF;
			mf = MPT_INDEX_2_MFPTR(ioc, req_idx);
			mpt_free_msg_frame(ioc, mf);
			mb();
			return;
			break;
		}
		mr = (MPT_FRAME_HDR *) CAST_U32_TO_PTR(pa);
		break;
	case MPI_CONTEXT_REPLY_TYPE_SCSI_TARGET:
		cb_idx = mpt_get_cb_idx(MPTSTM_DRIVER);
		mr = (MPT_FRAME_HDR *) CAST_U32_TO_PTR(pa);
		break;
	default:
		cb_idx = 0;
		BUG();
	}

	/*  Check for (valid) IO callback!  */
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS ||
		MptCallbacks[cb_idx] == NULL) {
		printk(MYIOC_s_WARN_FMT "%s: Invalid cb_idx (%d)!\n",
				__func__, ioc->name, cb_idx);
		goto out;
	}

	if (MptCallbacks[cb_idx](ioc, mf, mr))
		mpt_free_msg_frame(ioc, mf);
 out:
	mb();
}

static void
mpt_reply(MPT_ADAPTER *ioc, u32 pa)
{
	MPT_FRAME_HDR	*mf;
	MPT_FRAME_HDR	*mr;
	u16		 req_idx;
	u8		 cb_idx;
	int		 freeme;

	u32 reply_dma_low;
	u16 ioc_stat;

	/* non-TURBO reply!  Hmmm, something may be up...
	 *  Newest turbo reply mechanism; get address
	 *  via left shift 1 (get rid of MPI_ADDRESS_REPLY_A_BIT)!
	 */

	/* Map DMA address of reply header to cpu address.
	 * pa is 32 bits - but the dma address may be 32 or 64 bits
	 * get offset based only only the low addresses
	 */

	reply_dma_low = (pa <<= 1);
	mr = (MPT_FRAME_HDR *)((u8 *)ioc->reply_frames +
			 (reply_dma_low - ioc->reply_frames_low_dma));

	req_idx = le16_to_cpu(mr->u.frame.hwhdr.msgctxu.fld.req_idx);
	cb_idx = mr->u.frame.hwhdr.msgctxu.fld.cb_idx;
	mf = MPT_INDEX_2_MFPTR(ioc, req_idx);

	dmfprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Got non-TURBO reply=%p req_idx=%x cb_idx=%x Function=%x\n",
			ioc->name, mr, req_idx, cb_idx, mr->u.hdr.Function));
	DBG_DUMP_REPLY_FRAME(ioc, (u32 *)mr);

	 /*  Check/log IOC log info
	 */
	ioc_stat = le16_to_cpu(mr->u.reply.IOCStatus);
	if (ioc_stat & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		u32	 log_info = le32_to_cpu(mr->u.reply.IOCLogInfo);
		if (ioc->bus_type == FC)
			mpt_fc_log_info(ioc, log_info);
		else if (ioc->bus_type == SPI)
			mpt_spi_log_info(ioc, log_info);
		else if (ioc->bus_type == SAS)
			mpt_sas_log_info(ioc, log_info, cb_idx);
	}

	if (ioc_stat & MPI_IOCSTATUS_MASK)
		mpt_iocstatus_info(ioc, (u32)ioc_stat, mf);

	/*  Check for (valid) IO callback!  */
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS ||
		MptCallbacks[cb_idx] == NULL) {
		printk(MYIOC_s_WARN_FMT "%s: Invalid cb_idx (%d)!\n",
				__func__, ioc->name, cb_idx);
		freeme = 0;
		goto out;
	}

	freeme = MptCallbacks[cb_idx](ioc, mf, mr);

 out:
	/*  Flush (non-TURBO) reply with a WRITE!  */
	CHIPREG_WRITE32(&ioc->chip->ReplyFifo, pa);

	if (freeme)
		mpt_free_msg_frame(ioc, mf);
	mb();
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_interrupt - MPT adapter (IOC) specific interrupt handler.
 *	@irq: irq number (not used)
 *	@bus_id: bus identifier cookie == pointer to MPT_ADAPTER structure
 *
 *	This routine is registered via the request_irq() kernel API call,
 *	and handles all interrupts generated from a specific MPT adapter
 *	(also referred to as a IO Controller or IOC).
 *	This routine must clear the interrupt from the adapter and does
 *	so by reading the reply FIFO.  Multiple replies may be processed
 *	per single call to this routine.
 *
 *	This routine handles register-level access of the adapter but
 *	dispatches (calls) a protocol-specific callback routine to handle
 *	the protocol-specific details of the MPT request completion.
 */
static irqreturn_t
mpt_interrupt(int irq, void *bus_id)
{
	MPT_ADAPTER *ioc = bus_id;
	u32 pa = CHIPREG_READ32_dmasync(&ioc->chip->ReplyFifo);

	if (pa == 0xFFFFFFFF)
		return IRQ_NONE;

	/*
	 *  Drain the reply FIFO!
	 */
	do {
		if (pa & MPI_ADDRESS_REPLY_A_BIT)
			mpt_reply(ioc, pa);
		else
			mpt_turbo_reply(ioc, pa);
		pa = CHIPREG_READ32_dmasync(&ioc->chip->ReplyFifo);
	} while (pa != 0xFFFFFFFF);

	return IRQ_HANDLED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptbase_reply - MPT base driver's callback routine
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@req: Pointer to original MPT request frame
 *	@reply: Pointer to MPT reply frame (NULL if TurboReply)
 *
 *	MPT base driver's callback routine; all base driver
 *	"internal" request/reply processing is routed here.
 *	Currently used for EventNotification and EventAck handling.
 *
 *	Returns 1 indicating original alloc'd request frame ptr
 *	should be freed, or 0 if it shouldn't.
 */
static int
mptbase_reply(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req, MPT_FRAME_HDR *reply)
{
	EventNotificationReply_t *pEventReply;
	u8 event;
	int evHandlers;
	int freereq = 1;

	switch (reply->u.hdr.Function) {
	case MPI_FUNCTION_EVENT_NOTIFICATION:
		pEventReply = (EventNotificationReply_t *)reply;
		evHandlers = 0;
		ProcessEventNotification(ioc, pEventReply, &evHandlers);
		event = le32_to_cpu(pEventReply->Event) & 0xFF;
		if (pEventReply->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY)
			freereq = 0;
		if (event != MPI_EVENT_EVENT_CHANGE)
			break;
		/* else: fall through */
	case MPI_FUNCTION_CONFIG:
	case MPI_FUNCTION_SAS_IO_UNIT_CONTROL:
		ioc->mptbase_cmds.status |= MPT_MGMT_STATUS_COMMAND_GOOD;
		ioc->mptbase_cmds.status |= MPT_MGMT_STATUS_RF_VALID;
		memcpy(ioc->mptbase_cmds.reply, reply,
		    min(MPT_DEFAULT_FRAME_SIZE,
			4 * reply->u.reply.MsgLength));
		if (ioc->mptbase_cmds.status & MPT_MGMT_STATUS_PENDING) {
			ioc->mptbase_cmds.status &= ~MPT_MGMT_STATUS_PENDING;
			complete(&ioc->mptbase_cmds.done);
		} else
			freereq = 0;
		if (ioc->mptbase_cmds.status & MPT_MGMT_STATUS_FREE_MF)
			freereq = 1;
		break;
	case MPI_FUNCTION_EVENT_ACK:
		devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "EventAck reply received\n", ioc->name));
		break;
	default:
		printk(MYIOC_s_ERR_FMT
		    "Unexpected msg function (=%02Xh) reply received!\n",
		    ioc->name, reply->u.hdr.Function);
		break;
	}

	/*
	 *	Conditionally tell caller to free the original
	 *	EventNotification/EventAck/unexpected request frame!
	 */
	return freereq;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_register - Register protocol-specific main callback handler.
 *	@cbfunc: callback function pointer
 *	@dclass: Protocol driver's class (%MPT_DRIVER_CLASS enum value)
 *	@func_name: call function's name
 *
 *	This routine is called by a protocol-specific driver (SCSI host,
 *	LAN, SCSI target) to register its reply callback routine.  Each
 *	protocol-specific driver must do this before it will be able to
 *	use any IOC resources, such as obtaining request frames.
 *
 *	NOTES: The SCSI protocol driver currently calls this routine thrice
 *	in order to register separate callbacks; one for "normal" SCSI IO;
 *	one for MptScsiTaskMgmt requests; one for Scan/DV requests.
 *
 *	Returns u8 valued "handle" in the range (and S.O.D. order)
 *	{N,...,7,6,5,...,1} if successful.
 *	A return value of MPT_MAX_PROTOCOL_DRIVERS (including zero!) should be
 *	considered an error by the caller.
 */
u8
mpt_register(MPT_CALLBACK cbfunc, MPT_DRIVER_CLASS dclass, char *func_name)
{
	u8 cb_idx;
	last_drv_idx = MPT_MAX_PROTOCOL_DRIVERS;

	/*
	 *  Search for empty callback slot in this order: {N,...,7,6,5,...,1}
	 *  (slot/handle 0 is reserved!)
	 */
	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptCallbacks[cb_idx] == NULL) {
			MptCallbacks[cb_idx] = cbfunc;
			MptDriverClass[cb_idx] = dclass;
			MptEvHandlers[cb_idx] = NULL;
			last_drv_idx = cb_idx;
			strlcpy(MptCallbacksName[cb_idx], func_name,
				MPT_MAX_CALLBACKNAME_LEN+1);
			break;
		}
	}

	return last_drv_idx;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_deregister - Deregister a protocol drivers resources.
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine when its
 *	module is unloaded.
 */
void
mpt_deregister(u8 cb_idx)
{
	if (cb_idx && (cb_idx < MPT_MAX_PROTOCOL_DRIVERS)) {
		MptCallbacks[cb_idx] = NULL;
		MptDriverClass[cb_idx] = MPTUNKNOWN_DRIVER;
		MptEvHandlers[cb_idx] = NULL;

		last_drv_idx++;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_event_register - Register protocol-specific event callback handler.
 *	@cb_idx: previously registered (via mpt_register) callback handle
 *	@ev_cbfunc: callback function
 *
 *	This routine can be called by one or more protocol-specific drivers
 *	if/when they choose to be notified of MPT events.
 *
 *	Returns 0 for success.
 */
int
mpt_event_register(u8 cb_idx, MPT_EVHANDLER ev_cbfunc)
{
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return -1;

	MptEvHandlers[cb_idx] = ev_cbfunc;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_event_deregister - Deregister protocol-specific event callback handler
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine
 *	when it does not (or can no longer) handle events,
 *	or when its module is unloaded.
 */
void
mpt_event_deregister(u8 cb_idx)
{
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return;

	MptEvHandlers[cb_idx] = NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_reset_register - Register protocol-specific IOC reset handler.
 *	@cb_idx: previously registered (via mpt_register) callback handle
 *	@reset_func: reset function
 *
 *	This routine can be called by one or more protocol-specific drivers
 *	if/when they choose to be notified of IOC resets.
 *
 *	Returns 0 for success.
 */
int
mpt_reset_register(u8 cb_idx, MPT_RESETHANDLER reset_func)
{
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return -1;

	MptResetHandlers[cb_idx] = reset_func;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_reset_deregister - Deregister protocol-specific IOC reset handler.
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine
 *	when it does not (or can no longer) handle IOC reset handling,
 *	or when its module is unloaded.
 */
void
mpt_reset_deregister(u8 cb_idx)
{
	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return;

	MptResetHandlers[cb_idx] = NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_device_driver_register - Register device driver hooks
 *	@dd_cbfunc: driver callbacks struct
 *	@cb_idx: MPT protocol driver index
 */
int
mpt_device_driver_register(struct mpt_pci_driver * dd_cbfunc, u8 cb_idx)
{
	MPT_ADAPTER	*ioc;
	const struct pci_device_id *id;

	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return -EINVAL;

	MptDeviceDriverHandlers[cb_idx] = dd_cbfunc;

	/* call per pci device probe entry point */
	list_for_each_entry(ioc, &ioc_list, list) {
		id = ioc->pcidev->driver ?
		    ioc->pcidev->driver->id_table : NULL;
		if (dd_cbfunc->probe)
			dd_cbfunc->probe(ioc->pcidev, id);
	 }

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_device_driver_deregister - DeRegister device driver hooks
 *	@cb_idx: MPT protocol driver index
 */
void
mpt_device_driver_deregister(u8 cb_idx)
{
	struct mpt_pci_driver *dd_cbfunc;
	MPT_ADAPTER	*ioc;

	if (!cb_idx || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return;

	dd_cbfunc = MptDeviceDriverHandlers[cb_idx];

	list_for_each_entry(ioc, &ioc_list, list) {
		if (dd_cbfunc->remove)
			dd_cbfunc->remove(ioc->pcidev);
	}

	MptDeviceDriverHandlers[cb_idx] = NULL;
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_get_msg_frame - Obtain an MPT request frame from the pool
 *	@cb_idx: Handle of registered MPT protocol driver
 *	@ioc: Pointer to MPT adapter structure
 *
 *	Obtain an MPT request frame from the pool (of 1024) that are
 *	allocated per MPT adapter.
 *
 *	Returns pointer to a MPT request frame or %NULL if none are available
 *	or IOC is not active.
 */
MPT_FRAME_HDR*
mpt_get_msg_frame(u8 cb_idx, MPT_ADAPTER *ioc)
{
	MPT_FRAME_HDR *mf;
	unsigned long flags;
	u16	 req_idx;	/* Request index */

	/* validate handle and ioc identifier */

#ifdef MFCNT
	if (!ioc->active)
		printk(MYIOC_s_WARN_FMT "IOC Not Active! mpt_get_msg_frame "
		    "returning NULL!\n", ioc->name);
#endif

	/* If interrupts are not attached, do not return a request frame */
	if (!ioc->active)
		return NULL;

	spin_lock_irqsave(&ioc->FreeQlock, flags);
	if (!list_empty(&ioc->FreeQ)) {
		int req_offset;

		mf = list_entry(ioc->FreeQ.next, MPT_FRAME_HDR,
				u.frame.linkage.list);
		list_del(&mf->u.frame.linkage.list);
		mf->u.frame.linkage.arg1 = 0;
		mf->u.frame.hwhdr.msgctxu.fld.cb_idx = cb_idx;	/* byte */
		req_offset = (u8 *)mf - (u8 *)ioc->req_frames;
								/* u16! */
		req_idx = req_offset / ioc->req_sz;
		mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(req_idx);
		mf->u.frame.hwhdr.msgctxu.fld.rsvd = 0;
		/* Default, will be changed if necessary in SG generation */
		ioc->RequestNB[req_idx] = ioc->NB_for_64_byte_frame;
#ifdef MFCNT
		ioc->mfcnt++;
#endif
	}
	else
		mf = NULL;
	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

#ifdef MFCNT
	if (mf == NULL)
		printk(MYIOC_s_WARN_FMT "IOC Active. No free Msg Frames! "
		    "Count 0x%x Max 0x%x\n", ioc->name, ioc->mfcnt,
		    ioc->req_depth);
	mfcounter++;
	if (mfcounter == PRINT_MF_COUNT)
		printk(MYIOC_s_INFO_FMT "MF Count 0x%x Max 0x%x \n", ioc->name,
		    ioc->mfcnt, ioc->req_depth);
#endif

	dmfprintk(ioc, printk(MYIOC_s_DEBUG_FMT "mpt_get_msg_frame(%d,%d), got mf=%p\n",
	    ioc->name, cb_idx, ioc->id, mf));
	return mf;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_put_msg_frame - Send a protocol-specific MPT request frame to an IOC
 *	@cb_idx: Handle of registered MPT protocol driver
 *	@ioc: Pointer to MPT adapter structure
 *	@mf: Pointer to MPT request frame
 *
 *	This routine posts an MPT request frame to the request post FIFO of a
 *	specific MPT adapter.
 */
void
mpt_put_msg_frame(u8 cb_idx, MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf)
{
	u32 mf_dma_addr;
	int req_offset;
	u16 req_idx;	/* Request index */

	/* ensure values are reset properly! */
	mf->u.frame.hwhdr.msgctxu.fld.cb_idx = cb_idx;		/* byte */
	req_offset = (u8 *)mf - (u8 *)ioc->req_frames;
								/* u16! */
	req_idx = req_offset / ioc->req_sz;
	mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(req_idx);
	mf->u.frame.hwhdr.msgctxu.fld.rsvd = 0;

	DBG_DUMP_PUT_MSG_FRAME(ioc, (u32 *)mf);

	mf_dma_addr = (ioc->req_frames_low_dma + req_offset) | ioc->RequestNB[req_idx];
	dsgprintk(ioc, printk(MYIOC_s_DEBUG_FMT "mf_dma_addr=%x req_idx=%d "
	    "RequestNB=%x\n", ioc->name, mf_dma_addr, req_idx,
	    ioc->RequestNB[req_idx]));
	CHIPREG_WRITE32(&ioc->chip->RequestFifo, mf_dma_addr);
}

/**
 *	mpt_put_msg_frame_hi_pri - Send a hi-pri protocol-specific MPT request frame
 *	@cb_idx: Handle of registered MPT protocol driver
 *	@ioc: Pointer to MPT adapter structure
 *	@mf: Pointer to MPT request frame
 *
 *	Send a protocol-specific MPT request frame to an IOC using
 *	hi-priority request queue.
 *
 *	This routine posts an MPT request frame to the request post FIFO of a
 *	specific MPT adapter.
 **/
void
mpt_put_msg_frame_hi_pri(u8 cb_idx, MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf)
{
	u32 mf_dma_addr;
	int req_offset;
	u16 req_idx;	/* Request index */

	/* ensure values are reset properly! */
	mf->u.frame.hwhdr.msgctxu.fld.cb_idx = cb_idx;
	req_offset = (u8 *)mf - (u8 *)ioc->req_frames;
	req_idx = req_offset / ioc->req_sz;
	mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(req_idx);
	mf->u.frame.hwhdr.msgctxu.fld.rsvd = 0;

	DBG_DUMP_PUT_MSG_FRAME(ioc, (u32 *)mf);

	mf_dma_addr = (ioc->req_frames_low_dma + req_offset);
	dsgprintk(ioc, printk(MYIOC_s_DEBUG_FMT "mf_dma_addr=%x req_idx=%d\n",
		ioc->name, mf_dma_addr, req_idx));
	CHIPREG_WRITE32(&ioc->chip->RequestHiPriFifo, mf_dma_addr);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_free_msg_frame - Place MPT request frame back on FreeQ.
 *	@ioc: Pointer to MPT adapter structure
 *	@mf: Pointer to MPT request frame
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 */
void
mpt_free_msg_frame(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf)
{
	unsigned long flags;

	/*  Put Request back on FreeQ!  */
	spin_lock_irqsave(&ioc->FreeQlock, flags);
	if (cpu_to_le32(mf->u.frame.linkage.arg1) == 0xdeadbeaf)
		goto out;
	/* signature to know if this mf is freed */
	mf->u.frame.linkage.arg1 = cpu_to_le32(0xdeadbeaf);
	list_add(&mf->u.frame.linkage.list, &ioc->FreeQ);
#ifdef MFCNT
	ioc->mfcnt--;
#endif
 out:
	spin_unlock_irqrestore(&ioc->FreeQlock, flags);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_add_sge - Place a simple 32 bit SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@flagslength: SGE flags and data transfer length
 *	@dma_addr: Physical address
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 */
static void
mpt_add_sge(void *pAddr, u32 flagslength, dma_addr_t dma_addr)
{
	SGESimple32_t *pSge = (SGESimple32_t *) pAddr;
	pSge->FlagsLength = cpu_to_le32(flagslength);
	pSge->Address = cpu_to_le32(dma_addr);
}

/**
 *	mpt_add_sge_64bit - Place a simple 64 bit SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@flagslength: SGE flags and data transfer length
 *	@dma_addr: Physical address
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 **/
static void
mpt_add_sge_64bit(void *pAddr, u32 flagslength, dma_addr_t dma_addr)
{
	SGESimple64_t *pSge = (SGESimple64_t *) pAddr;
	pSge->Address.Low = cpu_to_le32
			(lower_32_bits(dma_addr));
	pSge->Address.High = cpu_to_le32
			(upper_32_bits(dma_addr));
	pSge->FlagsLength = cpu_to_le32
			((flagslength | MPT_SGE_FLAGS_64_BIT_ADDRESSING));
}

/**
 *	mpt_add_sge_64bit_1078 - Place a simple 64 bit SGE at address pAddr (1078 workaround).
 *	@pAddr: virtual address for SGE
 *	@flagslength: SGE flags and data transfer length
 *	@dma_addr: Physical address
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 **/
static void
mpt_add_sge_64bit_1078(void *pAddr, u32 flagslength, dma_addr_t dma_addr)
{
	SGESimple64_t *pSge = (SGESimple64_t *) pAddr;
	u32 tmp;

	pSge->Address.Low = cpu_to_le32
			(lower_32_bits(dma_addr));
	tmp = (u32)(upper_32_bits(dma_addr));

	/*
	 * 1078 errata workaround for the 36GB limitation
	 */
	if ((((u64)dma_addr + MPI_SGE_LENGTH(flagslength)) >> 32)  == 9) {
		flagslength |=
		    MPI_SGE_SET_FLAGS(MPI_SGE_FLAGS_LOCAL_ADDRESS);
		tmp |= (1<<31);
		if (mpt_debug_level & MPT_DEBUG_36GB_MEM)
			printk(KERN_DEBUG "1078 P0M2 addressing for "
			    "addr = 0x%llx len = %d\n",
			    (unsigned long long)dma_addr,
			    MPI_SGE_LENGTH(flagslength));
	}

	pSge->Address.High = cpu_to_le32(tmp);
	pSge->FlagsLength = cpu_to_le32(
		(flagslength | MPT_SGE_FLAGS_64_BIT_ADDRESSING));
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_add_chain - Place a 32 bit chain SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@next: nextChainOffset value (u32's)
 *	@length: length of next SGL segment
 *	@dma_addr: Physical address
 *
 */
static void
mpt_add_chain(void *pAddr, u8 next, u16 length, dma_addr_t dma_addr)
{
	SGEChain32_t *pChain = (SGEChain32_t *) pAddr;

	pChain->Length = cpu_to_le16(length);
	pChain->Flags = MPI_SGE_FLAGS_CHAIN_ELEMENT;
	pChain->NextChainOffset = next;
	pChain->Address = cpu_to_le32(dma_addr);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_add_chain_64bit - Place a 64 bit chain SGE at address pAddr.
 *	@pAddr: virtual address for SGE
 *	@next: nextChainOffset value (u32's)
 *	@length: length of next SGL segment
 *	@dma_addr: Physical address
 *
 */
static void
mpt_add_chain_64bit(void *pAddr, u8 next, u16 length, dma_addr_t dma_addr)
{
	SGEChain64_t *pChain = (SGEChain64_t *) pAddr;
	u32 tmp = dma_addr & 0xFFFFFFFF;

	pChain->Length = cpu_to_le16(length);
	pChain->Flags = (MPI_SGE_FLAGS_CHAIN_ELEMENT |
			 MPI_SGE_FLAGS_64_BIT_ADDRESSING);

	pChain->NextChainOffset = next;

	pChain->Address.Low = cpu_to_le32(tmp);
	tmp = (u32)(upper_32_bits(dma_addr));
	pChain->Address.High = cpu_to_le32(tmp);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_send_handshake_request - Send MPT request via doorbell handshake method.
 *	@cb_idx: Handle of registered MPT protocol driver
 *	@ioc: Pointer to MPT adapter structure
 *	@reqBytes: Size of the request in bytes
 *	@req: Pointer to MPT request frame
 *	@sleepFlag: Use schedule if CAN_SLEEP else use udelay.
 *
 *	This routine is used exclusively to send MptScsiTaskMgmt
 *	requests since they are required to be sent via doorbell handshake.
 *
 *	NOTE: It is the callers responsibility to byte-swap fields in the
 *	request which are greater than 1 byte in size.
 *
 *	Returns 0 for success, non-zero for failure.
 */
int
mpt_send_handshake_request(u8 cb_idx, MPT_ADAPTER *ioc, int reqBytes, u32 *req, int sleepFlag)
{
	int	r = 0;
	u8	*req_as_bytes;
	int	 ii;

	/* State is known to be good upon entering
	 * this function so issue the bus reset
	 * request.
	 */

	/*
	 * Emulate what mpt_put_msg_frame() does /wrt to sanity
	 * setting cb_idx/req_idx.  But ONLY if this request
	 * is in proper (pre-alloc'd) request buffer range...
	 */
	ii = MFPTR_2_MPT_INDEX(ioc,(MPT_FRAME_HDR*)req);
	if (reqBytes >= 12 && ii >= 0 && ii < ioc->req_depth) {
		MPT_FRAME_HDR *mf = (MPT_FRAME_HDR*)req;
		mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(ii);
		mf->u.frame.hwhdr.msgctxu.fld.cb_idx = cb_idx;
	}

	/* Make sure there are no doorbells */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	CHIPREG_WRITE32(&ioc->chip->Doorbell,
			((MPI_FUNCTION_HANDSHAKE<<MPI_DOORBELL_FUNCTION_SHIFT) |
			 ((reqBytes/4)<<MPI_DOORBELL_ADD_DWORDS_SHIFT)));

	/* Wait for IOC doorbell int */
	if ((ii = WaitForDoorbellInt(ioc, 5, sleepFlag)) < 0) {
		return ii;
	}

	/* Read doorbell and check for active bit */
	if (!(CHIPREG_READ32(&ioc->chip->Doorbell) & MPI_DOORBELL_ACTIVE))
		return -5;

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "mpt_send_handshake_request start, WaitCnt=%d\n",
		ioc->name, ii));

	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	if ((r = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0) {
		return -2;
	}

	/* Send request via doorbell handshake */
	req_as_bytes = (u8 *) req;
	for (ii = 0; ii < reqBytes/4; ii++) {
		u32 word;

		word = ((req_as_bytes[(ii*4) + 0] <<  0) |
			(req_as_bytes[(ii*4) + 1] <<  8) |
			(req_as_bytes[(ii*4) + 2] << 16) |
			(req_as_bytes[(ii*4) + 3] << 24));
		CHIPREG_WRITE32(&ioc->chip->Doorbell, word);
		if ((r = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0) {
			r = -3;
			break;
		}
	}

	if (r >= 0 && WaitForDoorbellInt(ioc, 10, sleepFlag) >= 0)
		r = 0;
	else
		r = -4;

	/* Make sure there are no doorbells */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 * mpt_host_page_access_control - control the IOC's Host Page Buffer access
 * @ioc: Pointer to MPT adapter structure
 * @access_control_value: define bits below
 * @sleepFlag: Specifies whether the process can sleep
 *
 * Provides mechanism for the host driver to control the IOC's
 * Host Page Buffer access.
 *
 * Access Control Value - bits[15:12]
 * 0h Reserved
 * 1h Enable Access { MPI_DB_HPBAC_ENABLE_ACCESS }
 * 2h Disable Access { MPI_DB_HPBAC_DISABLE_ACCESS }
 * 3h Free Buffer { MPI_DB_HPBAC_FREE_BUFFER }
 *
 * Returns 0 for success, non-zero for failure.
 */

static int
mpt_host_page_access_control(MPT_ADAPTER *ioc, u8 access_control_value, int sleepFlag)
{
	int	 r = 0;

	/* return if in use */
	if (CHIPREG_READ32(&ioc->chip->Doorbell)
	    & MPI_DOORBELL_ACTIVE)
	    return -1;

	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	CHIPREG_WRITE32(&ioc->chip->Doorbell,
		((MPI_FUNCTION_HOST_PAGEBUF_ACCESS_CONTROL
		 <<MPI_DOORBELL_FUNCTION_SHIFT) |
		 (access_control_value<<12)));

	/* Wait for IOC to clear Doorbell Status bit */
	if ((r = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0) {
		return -2;
	}else
		return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_host_page_alloc - allocate system memory for the fw
 *	@ioc: Pointer to pointer to IOC adapter
 *	@ioc_init: Pointer to ioc init config page
 *
 *	If we already allocated memory in past, then resend the same pointer.
 *	Returns 0 for success, non-zero for failure.
 */
static int
mpt_host_page_alloc(MPT_ADAPTER *ioc, pIOCInit_t ioc_init)
{
	char	*psge;
	int	flags_length;
	u32	host_page_buffer_sz=0;

	if(!ioc->HostPageBuffer) {

		host_page_buffer_sz =
		    le32_to_cpu(ioc->facts.HostPageBufferSGE.FlagsLength) & 0xFFFFFF;

		if(!host_page_buffer_sz)
			return 0; /* fw doesn't need any host buffers */

		/* spin till we get enough memory */
		while(host_page_buffer_sz > 0) {

			if((ioc->HostPageBuffer = pci_alloc_consistent(
			    ioc->pcidev,
			    host_page_buffer_sz,
			    &ioc->HostPageBuffer_dma)) != NULL) {

				dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				    "host_page_buffer @ %p, dma @ %x, sz=%d bytes\n",
				    ioc->name, ioc->HostPageBuffer,
				    (u32)ioc->HostPageBuffer_dma,
				    host_page_buffer_sz));
				ioc->alloc_total += host_page_buffer_sz;
				ioc->HostPageBuffer_sz = host_page_buffer_sz;
				break;
			}

			host_page_buffer_sz -= (4*1024);
		}
	}

	if(!ioc->HostPageBuffer) {
		printk(MYIOC_s_ERR_FMT
		    "Failed to alloc memory for host_page_buffer!\n",
		    ioc->name);
		return -999;
	}

	psge = (char *)&ioc_init->HostPageBufferSGE;
	flags_length = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_HOST_TO_IOC |
	    MPI_SGE_FLAGS_END_OF_BUFFER;
	flags_length = flags_length << MPI_SGE_FLAGS_SHIFT;
	flags_length |= ioc->HostPageBuffer_sz;
	ioc->add_sge(psge, flags_length, ioc->HostPageBuffer_dma);
	ioc->facts.HostPageBufferSGE = ioc_init->HostPageBufferSGE;

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_verify_adapter - Given IOC identifier, set pointer to its adapter structure.
 *	@iocid: IOC unique identifier (integer)
 *	@iocpp: Pointer to pointer to IOC adapter
 *
 *	Given a unique IOC identifier, set pointer to the associated MPT
 *	adapter structure.
 *
 *	Returns iocid and sets iocpp if iocid is found.
 *	Returns -1 if iocid is not found.
 */
int
mpt_verify_adapter(int iocid, MPT_ADAPTER **iocpp)
{
	MPT_ADAPTER *ioc;

	list_for_each_entry(ioc,&ioc_list,list) {
		if (ioc->id == iocid) {
			*iocpp =ioc;
			return iocid;
		}
	}

	*iocpp = NULL;
	return -1;
}

/**
 *	mpt_get_product_name - returns product string
 *	@vendor: pci vendor id
 *	@device: pci device id
 *	@revision: pci revision id
 *
 *	Returns product string displayed when driver loads,
 *	in /proc/mpt/summary and /sysfs/class/scsi_host/host<X>/version_product
 *
 **/
static const char*
mpt_get_product_name(u16 vendor, u16 device, u8 revision)
{
	char *product_str = NULL;

	if (vendor == PCI_VENDOR_ID_BROCADE) {
		switch (device)
		{
		case MPI_MANUFACTPAGE_DEVICEID_FC949E:
			switch (revision)
			{
			case 0x00:
				product_str = "BRE040 A0";
				break;
			case 0x01:
				product_str = "BRE040 A1";
				break;
			default:
				product_str = "BRE040";
				break;
			}
			break;
		}
		goto out;
	}

	switch (device)
	{
	case MPI_MANUFACTPAGE_DEVICEID_FC909:
		product_str = "LSIFC909 B1";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919:
		product_str = "LSIFC919 B0";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929:
		product_str = "LSIFC929 B0";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919X:
		if (revision < 0x80)
			product_str = "LSIFC919X A0";
		else
			product_str = "LSIFC919XL A1";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929X:
		if (revision < 0x80)
			product_str = "LSIFC929X A0";
		else
			product_str = "LSIFC929XL A1";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC939X:
		product_str = "LSIFC939X A1";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC949X:
		product_str = "LSIFC949X A1";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC949E:
		switch (revision)
		{
		case 0x00:
			product_str = "LSIFC949E A0";
			break;
		case 0x01:
			product_str = "LSIFC949E A1";
			break;
		default:
			product_str = "LSIFC949E";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_53C1030:
		switch (revision)
		{
		case 0x00:
			product_str = "LSI53C1030 A0";
			break;
		case 0x01:
			product_str = "LSI53C1030 B0";
			break;
		case 0x03:
			product_str = "LSI53C1030 B1";
			break;
		case 0x07:
			product_str = "LSI53C1030 B2";
			break;
		case 0x08:
			product_str = "LSI53C1030 C0";
			break;
		case 0x80:
			product_str = "LSI53C1030T A0";
			break;
		case 0x83:
			product_str = "LSI53C1030T A2";
			break;
		case 0x87:
			product_str = "LSI53C1030T A3";
			break;
		case 0xc1:
			product_str = "LSI53C1020A A1";
			break;
		default:
			product_str = "LSI53C1030";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_1030_53C1035:
		switch (revision)
		{
		case 0x03:
			product_str = "LSI53C1035 A2";
			break;
		case 0x04:
			product_str = "LSI53C1035 B0";
			break;
		default:
			product_str = "LSI53C1035";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1064:
		switch (revision)
		{
		case 0x00:
			product_str = "LSISAS1064 A1";
			break;
		case 0x01:
			product_str = "LSISAS1064 A2";
			break;
		case 0x02:
			product_str = "LSISAS1064 A3";
			break;
		case 0x03:
			product_str = "LSISAS1064 A4";
			break;
		default:
			product_str = "LSISAS1064";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1064E:
		switch (revision)
		{
		case 0x00:
			product_str = "LSISAS1064E A0";
			break;
		case 0x01:
			product_str = "LSISAS1064E B0";
			break;
		case 0x02:
			product_str = "LSISAS1064E B1";
			break;
		case 0x04:
			product_str = "LSISAS1064E B2";
			break;
		case 0x08:
			product_str = "LSISAS1064E B3";
			break;
		default:
			product_str = "LSISAS1064E";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1068:
		switch (revision)
		{
		case 0x00:
			product_str = "LSISAS1068 A0";
			break;
		case 0x01:
			product_str = "LSISAS1068 B0";
			break;
		case 0x02:
			product_str = "LSISAS1068 B1";
			break;
		default:
			product_str = "LSISAS1068";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1068E:
		switch (revision)
		{
		case 0x00:
			product_str = "LSISAS1068E A0";
			break;
		case 0x01:
			product_str = "LSISAS1068E B0";
			break;
		case 0x02:
			product_str = "LSISAS1068E B1";
			break;
		case 0x04:
			product_str = "LSISAS1068E B2";
			break;
		case 0x08:
			product_str = "LSISAS1068E B3";
			break;
		default:
			product_str = "LSISAS1068E";
			break;
		}
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1078:
		switch (revision)
		{
		case 0x00:
			product_str = "LSISAS1078 A0";
			break;
		case 0x01:
			product_str = "LSISAS1078 B0";
			break;
		case 0x02:
			product_str = "LSISAS1078 C0";
			break;
		case 0x03:
			product_str = "LSISAS1078 C1";
			break;
		case 0x04:
			product_str = "LSISAS1078 C2";
			break;
		default:
			product_str = "LSISAS1078";
			break;
		}
		break;
	}

 out:
	return product_str;
}

/**
 *	mpt_mapresources - map in memory mapped io
 *	@ioc: Pointer to pointer to IOC adapter
 *
 **/
static int
mpt_mapresources(MPT_ADAPTER *ioc)
{
	u8		__iomem *mem;
	int		 ii;
	resource_size_t	 mem_phys;
	unsigned long	 port;
	u32		 msize;
	u32		 psize;
	int		 r = -ENODEV;
	struct pci_dev *pdev;

	pdev = ioc->pcidev;
	ioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_enable_device_mem(pdev)) {
		printk(MYIOC_s_ERR_FMT "pci_enable_device_mem() "
		    "failed\n", ioc->name);
		return r;
	}
	if (pci_request_selected_regions(pdev, ioc->bars, "mpt")) {
		printk(MYIOC_s_ERR_FMT "pci_request_selected_regions() with "
		    "MEM failed\n", ioc->name);
		goto out_pci_disable_device;
	}

	if (sizeof(dma_addr_t) > 4) {
		const uint64_t required_mask = dma_get_required_mask
		    (&pdev->dev);
		if (required_mask > DMA_BIT_MASK(32)
			&& !pci_set_dma_mask(pdev, DMA_BIT_MASK(64))
			&& !pci_set_consistent_dma_mask(pdev,
						 DMA_BIT_MASK(64))) {
			ioc->dma_mask = DMA_BIT_MASK(64);
			dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
				": 64 BIT PCI BUS DMA ADDRESSING SUPPORTED\n",
				ioc->name));
		} else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))
			&& !pci_set_consistent_dma_mask(pdev,
						DMA_BIT_MASK(32))) {
			ioc->dma_mask = DMA_BIT_MASK(32);
			dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
				": 32 BIT PCI BUS DMA ADDRESSING SUPPORTED\n",
				ioc->name));
		} else {
			printk(MYIOC_s_WARN_FMT "no suitable DMA mask for %s\n",
			    ioc->name, pci_name(pdev));
			goto out_pci_release_region;
		}
	} else {
		if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))
			&& !pci_set_consistent_dma_mask(pdev,
						DMA_BIT_MASK(32))) {
			ioc->dma_mask = DMA_BIT_MASK(32);
			dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
				": 32 BIT PCI BUS DMA ADDRESSING SUPPORTED\n",
				ioc->name));
		} else {
			printk(MYIOC_s_WARN_FMT "no suitable DMA mask for %s\n",
			    ioc->name, pci_name(pdev));
			goto out_pci_release_region;
		}
	}

	mem_phys = msize = 0;
	port = psize = 0;
	for (ii = 0; ii < DEVICE_COUNT_RESOURCE; ii++) {
		if (pci_resource_flags(pdev, ii) & PCI_BASE_ADDRESS_SPACE_IO) {
			if (psize)
				continue;
			/* Get I/O space! */
			port = pci_resource_start(pdev, ii);
			psize = pci_resource_len(pdev, ii);
		} else {
			if (msize)
				continue;
			/* Get memmap */
			mem_phys = pci_resource_start(pdev, ii);
			msize = pci_resource_len(pdev, ii);
		}
	}
	ioc->mem_size = msize;

	mem = NULL;
	/* Get logical ptr for PciMem0 space */
	/*mem = ioremap(mem_phys, msize);*/
	mem = ioremap(mem_phys, msize);
	if (mem == NULL) {
		printk(MYIOC_s_ERR_FMT ": ERROR - Unable to map adapter"
			" memory!\n", ioc->name);
		r = -EINVAL;
		goto out_pci_release_region;
	}
	ioc->memmap = mem;
	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT "mem = %p, mem_phys = %llx\n",
	    ioc->name, mem, (unsigned long long)mem_phys));

	ioc->mem_phys = mem_phys;
	ioc->chip = (SYSIF_REGS __iomem *)mem;

	/* Save Port IO values in case we need to do downloadboot */
	ioc->pio_mem_phys = port;
	ioc->pio_chip = (SYSIF_REGS __iomem *)port;

	return 0;

out_pci_release_region:
	pci_release_selected_regions(pdev, ioc->bars);
out_pci_disable_device:
	pci_disable_device(pdev);
	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_attach - Install a PCI intelligent MPT adapter.
 *	@pdev: Pointer to pci_dev structure
 *	@id: PCI device ID information
 *
 *	This routine performs all the steps necessary to bring the IOC of
 *	a MPT adapter to a OPERATIONAL state.  This includes registering
 *	memory regions, registering the interrupt, and allocating request
 *	and reply memory pools.
 *
 *	This routine also pre-fetches the LAN MAC address of a Fibre Channel
 *	MPT adapter.
 *
 *	Returns 0 for success, non-zero for failure.
 *
 *	TODO: Add support for polled controllers
 */
int
mpt_attach(struct pci_dev *pdev, const struct pci_device_id *id)
{
	MPT_ADAPTER	*ioc;
	u8		 cb_idx;
	int		 r = -ENODEV;
	u8		 pcixcmd;
	static int	 mpt_ids = 0;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dent;
#endif

	ioc = kzalloc(sizeof(MPT_ADAPTER), GFP_KERNEL);
	if (ioc == NULL) {
		printk(KERN_ERR MYNAM ": ERROR - Insufficient memory to add adapter!\n");
		return -ENOMEM;
	}

	ioc->id = mpt_ids++;
	sprintf(ioc->name, "ioc%d", ioc->id);
	dinitprintk(ioc, printk(KERN_WARNING MYNAM ": mpt_adapter_install\n"));

	/*
	 * set initial debug level
	 * (refer to mptdebug.h)
	 *
	 */
	ioc->debug_level = mpt_debug_level;
	if (mpt_debug_level)
		printk(KERN_INFO "mpt_debug_level=%xh\n", mpt_debug_level);

	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT ": mpt_adapter_install\n", ioc->name));

	ioc->pcidev = pdev;
	if (mpt_mapresources(ioc)) {
		goto out_free_ioc;
	}

	/*
	 * Setting up proper handlers for scatter gather handling
	 */
	if (ioc->dma_mask == DMA_BIT_MASK(64)) {
		if (pdev->device == MPI_MANUFACTPAGE_DEVID_SAS1078)
			ioc->add_sge = &mpt_add_sge_64bit_1078;
		else
			ioc->add_sge = &mpt_add_sge_64bit;
		ioc->add_chain = &mpt_add_chain_64bit;
		ioc->sg_addr_size = 8;
	} else {
		ioc->add_sge = &mpt_add_sge;
		ioc->add_chain = &mpt_add_chain;
		ioc->sg_addr_size = 4;
	}
	ioc->SGE_size = sizeof(u32) + ioc->sg_addr_size;

	ioc->alloc_total = sizeof(MPT_ADAPTER);
	ioc->req_sz = MPT_DEFAULT_FRAME_SIZE;		/* avoid div by zero! */
	ioc->reply_sz = MPT_REPLY_FRAME_SIZE;


	spin_lock_init(&ioc->taskmgmt_lock);
	mutex_init(&ioc->internal_cmds.mutex);
	init_completion(&ioc->internal_cmds.done);
	mutex_init(&ioc->mptbase_cmds.mutex);
	init_completion(&ioc->mptbase_cmds.done);
	mutex_init(&ioc->taskmgmt_cmds.mutex);
	init_completion(&ioc->taskmgmt_cmds.done);

	/* Initialize the event logging.
	 */
	ioc->eventTypes = 0;	/* None */
	ioc->eventContext = 0;
	ioc->eventLogSize = 0;
	ioc->events = NULL;

#ifdef MFCNT
	ioc->mfcnt = 0;
#endif

	ioc->sh = NULL;
	ioc->cached_fw = NULL;

	/* Initialize SCSI Config Data structure
	 */
	memset(&ioc->spi_data, 0, sizeof(SpiCfgData));

	/* Initialize the fc rport list head.
	 */
	INIT_LIST_HEAD(&ioc->fc_rports);

	/* Find lookup slot. */
	INIT_LIST_HEAD(&ioc->list);


	/* Initialize workqueue */
	INIT_DELAYED_WORK(&ioc->fault_reset_work, mpt_fault_reset_work);

	snprintf(ioc->reset_work_q_name, MPT_KOBJ_NAME_LEN,
		 "mpt_poll_%d", ioc->id);
	ioc->reset_work_q = alloc_workqueue(ioc->reset_work_q_name,
					    WQ_MEM_RECLAIM, 0);
	if (!ioc->reset_work_q) {
		printk(MYIOC_s_ERR_FMT "Insufficient memory to add adapter!\n",
		    ioc->name);
		r = -ENOMEM;
		goto out_unmap_resources;
	}

	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT "facts @ %p, pfacts[0] @ %p\n",
	    ioc->name, &ioc->facts, &ioc->pfacts[0]));

	ioc->prod_name = mpt_get_product_name(pdev->vendor, pdev->device,
					      pdev->revision);

	switch (pdev->device)
	{
	case MPI_MANUFACTPAGE_DEVICEID_FC939X:
	case MPI_MANUFACTPAGE_DEVICEID_FC949X:
		ioc->errata_flag_1064 = 1;
		/* fall through */
	case MPI_MANUFACTPAGE_DEVICEID_FC909:
	case MPI_MANUFACTPAGE_DEVICEID_FC929:
	case MPI_MANUFACTPAGE_DEVICEID_FC919:
	case MPI_MANUFACTPAGE_DEVICEID_FC949E:
		ioc->bus_type = FC;
		break;

	case MPI_MANUFACTPAGE_DEVICEID_FC929X:
		if (pdev->revision < XL_929) {
			/* 929X Chip Fix. Set Split transactions level
		 	* for PCIX. Set MOST bits to zero.
		 	*/
			pci_read_config_byte(pdev, 0x6a, &pcixcmd);
			pcixcmd &= 0x8F;
			pci_write_config_byte(pdev, 0x6a, pcixcmd);
		} else {
			/* 929XL Chip Fix. Set MMRBC to 0x08.
		 	*/
			pci_read_config_byte(pdev, 0x6a, &pcixcmd);
			pcixcmd |= 0x08;
			pci_write_config_byte(pdev, 0x6a, pcixcmd);
		}
		ioc->bus_type = FC;
		break;

	case MPI_MANUFACTPAGE_DEVICEID_FC919X:
		/* 919X Chip Fix. Set Split transactions level
		 * for PCIX. Set MOST bits to zero.
		 */
		pci_read_config_byte(pdev, 0x6a, &pcixcmd);
		pcixcmd &= 0x8F;
		pci_write_config_byte(pdev, 0x6a, pcixcmd);
		ioc->bus_type = FC;
		break;

	case MPI_MANUFACTPAGE_DEVID_53C1030:
		/* 1030 Chip Fix. Disable Split transactions
		 * for PCIX. Set MOST bits to zero if Rev < C0( = 8).
		 */
		if (pdev->revision < C0_1030) {
			pci_read_config_byte(pdev, 0x6a, &pcixcmd);
			pcixcmd &= 0x8F;
			pci_write_config_byte(pdev, 0x6a, pcixcmd);
		}
		/* fall through */

	case MPI_MANUFACTPAGE_DEVID_1030_53C1035:
		ioc->bus_type = SPI;
		break;

	case MPI_MANUFACTPAGE_DEVID_SAS1064:
	case MPI_MANUFACTPAGE_DEVID_SAS1068:
		ioc->errata_flag_1064 = 1;
		ioc->bus_type = SAS;
		break;

	case MPI_MANUFACTPAGE_DEVID_SAS1064E:
	case MPI_MANUFACTPAGE_DEVID_SAS1068E:
	case MPI_MANUFACTPAGE_DEVID_SAS1078:
		ioc->bus_type = SAS;
		break;
	}


	switch (ioc->bus_type) {

	case SAS:
		ioc->msi_enable = mpt_msi_enable_sas;
		break;

	case SPI:
		ioc->msi_enable = mpt_msi_enable_spi;
		break;

	case FC:
		ioc->msi_enable = mpt_msi_enable_fc;
		break;

	default:
		ioc->msi_enable = 0;
		break;
	}

	ioc->fw_events_off = 1;

	if (ioc->errata_flag_1064)
		pci_disable_io_access(pdev);

	spin_lock_init(&ioc->FreeQlock);

	/* Disable all! */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	/* Set IOC ptr in the pcidev's driver data. */
	pci_set_drvdata(ioc->pcidev, ioc);

	/* Set lookup ptr. */
	list_add_tail(&ioc->list, &ioc_list);

	/* Check for "bound ports" (929, 929X, 1030, 1035) to reduce redundant resets.
	 */
	mpt_detect_bound_ports(ioc, pdev);

	INIT_LIST_HEAD(&ioc->fw_event_list);
	spin_lock_init(&ioc->fw_event_lock);
	snprintf(ioc->fw_event_q_name, MPT_KOBJ_NAME_LEN, "mpt/%d", ioc->id);
	ioc->fw_event_q = alloc_workqueue(ioc->fw_event_q_name,
					  WQ_MEM_RECLAIM, 0);
	if (!ioc->fw_event_q) {
		printk(MYIOC_s_ERR_FMT "Insufficient memory to add adapter!\n",
		    ioc->name);
		r = -ENOMEM;
		goto out_remove_ioc;
	}

	if ((r = mpt_do_ioc_recovery(ioc, MPT_HOSTEVENT_IOC_BRINGUP,
	    CAN_SLEEP)) != 0){
		printk(MYIOC_s_ERR_FMT "didn't initialize properly! (%d)\n",
		    ioc->name, r);

		destroy_workqueue(ioc->fw_event_q);
		ioc->fw_event_q = NULL;

		list_del(&ioc->list);
		if (ioc->alt_ioc)
			ioc->alt_ioc->alt_ioc = NULL;
		iounmap(ioc->memmap);
		if (pci_is_enabled(pdev))
			pci_disable_device(pdev);
		if (r != -5)
			pci_release_selected_regions(pdev, ioc->bars);

		destroy_workqueue(ioc->reset_work_q);
		ioc->reset_work_q = NULL;

		kfree(ioc);
		return r;
	}

	/* call per device driver probe entry point */
	for(cb_idx = 0; cb_idx < MPT_MAX_PROTOCOL_DRIVERS; cb_idx++) {
		if(MptDeviceDriverHandlers[cb_idx] &&
		  MptDeviceDriverHandlers[cb_idx]->probe) {
			MptDeviceDriverHandlers[cb_idx]->probe(pdev,id);
		}
	}

#ifdef CONFIG_PROC_FS
	/*
	 *  Create "/proc/mpt/iocN" subdirectory entry for each MPT adapter.
	 */
	dent = proc_mkdir(ioc->name, mpt_proc_root_dir);
	if (dent) {
		proc_create_single_data("info", S_IRUGO, dent,
				mpt_iocinfo_proc_show, ioc);
		proc_create_single_data("summary", S_IRUGO, dent,
				mpt_summary_proc_show, ioc);
	}
#endif

	if (!ioc->alt_ioc)
		queue_delayed_work(ioc->reset_work_q, &ioc->fault_reset_work,
			msecs_to_jiffies(MPT_POLLING_INTERVAL));

	return 0;

out_remove_ioc:
	list_del(&ioc->list);
	if (ioc->alt_ioc)
		ioc->alt_ioc->alt_ioc = NULL;

	destroy_workqueue(ioc->reset_work_q);
	ioc->reset_work_q = NULL;

out_unmap_resources:
	iounmap(ioc->memmap);
	pci_disable_device(pdev);
	pci_release_selected_regions(pdev, ioc->bars);

out_free_ioc:
	kfree(ioc);

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_detach - Remove a PCI intelligent MPT adapter.
 *	@pdev: Pointer to pci_dev structure
 */

void
mpt_detach(struct pci_dev *pdev)
{
	MPT_ADAPTER 	*ioc = pci_get_drvdata(pdev);
	char pname[64];
	u8 cb_idx;
	unsigned long flags;
	struct workqueue_struct *wq;

	/*
	 * Stop polling ioc for fault condition
	 */
	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	wq = ioc->reset_work_q;
	ioc->reset_work_q = NULL;
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
	cancel_delayed_work(&ioc->fault_reset_work);
	destroy_workqueue(wq);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->fw_event_q;
	ioc->fw_event_q = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	destroy_workqueue(wq);

	snprintf(pname, sizeof(pname), MPT_PROCFS_MPTBASEDIR "/%s/summary", ioc->name);
	remove_proc_entry(pname, NULL);
	snprintf(pname, sizeof(pname), MPT_PROCFS_MPTBASEDIR "/%s/info", ioc->name);
	remove_proc_entry(pname, NULL);
	snprintf(pname, sizeof(pname), MPT_PROCFS_MPTBASEDIR "/%s", ioc->name);
	remove_proc_entry(pname, NULL);

	/* call per device driver remove entry point */
	for(cb_idx = 0; cb_idx < MPT_MAX_PROTOCOL_DRIVERS; cb_idx++) {
		if(MptDeviceDriverHandlers[cb_idx] &&
		  MptDeviceDriverHandlers[cb_idx]->remove) {
			MptDeviceDriverHandlers[cb_idx]->remove(pdev);
		}
	}

	/* Disable interrupts! */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);

	ioc->active = 0;
	synchronize_irq(pdev->irq);

	/* Clear any lingering interrupt */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	CHIPREG_READ32(&ioc->chip->IntStatus);

	mpt_adapter_dispose(ioc);

}

/**************************************************************************
 * Power Management
 */
#ifdef CONFIG_PM
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_suspend - Fusion MPT base driver suspend routine.
 *	@pdev: Pointer to pci_dev structure
 *	@state: new state to enter
 */
int
mpt_suspend(struct pci_dev *pdev, pm_message_t state)
{
	u32 device_state;
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);

	device_state = pci_choose_state(pdev, state);
	printk(MYIOC_s_INFO_FMT "pci-suspend: pdev=0x%p, slot=%s, Entering "
	    "operating state [D%d]\n", ioc->name, pdev, pci_name(pdev),
	    device_state);

	/* put ioc into READY_STATE */
	if (SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET, CAN_SLEEP)) {
		printk(MYIOC_s_ERR_FMT
		"pci-suspend:  IOC msg unit reset failed!\n", ioc->name);
	}

	/* disable interrupts */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;

	/* Clear any lingering interrupt */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	free_irq(ioc->pci_irq, ioc);
	if (ioc->msi_enable)
		pci_disable_msi(ioc->pcidev);
	ioc->pci_irq = -1;
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_release_selected_regions(pdev, ioc->bars);
	pci_set_power_state(pdev, device_state);
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_resume - Fusion MPT base driver resume routine.
 *	@pdev: Pointer to pci_dev structure
 */
int
mpt_resume(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);
	u32 device_state = pdev->current_state;
	int recovery_state;
	int err;

	printk(MYIOC_s_INFO_FMT "pci-resume: pdev=0x%p, slot=%s, Previous "
	    "operating state [D%d]\n", ioc->name, pdev, pci_name(pdev),
	    device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	ioc->pcidev = pdev;
	err = mpt_mapresources(ioc);
	if (err)
		return err;

	if (ioc->dma_mask == DMA_BIT_MASK(64)) {
		if (pdev->device == MPI_MANUFACTPAGE_DEVID_SAS1078)
			ioc->add_sge = &mpt_add_sge_64bit_1078;
		else
			ioc->add_sge = &mpt_add_sge_64bit;
		ioc->add_chain = &mpt_add_chain_64bit;
		ioc->sg_addr_size = 8;
	} else {

		ioc->add_sge = &mpt_add_sge;
		ioc->add_chain = &mpt_add_chain;
		ioc->sg_addr_size = 4;
	}
	ioc->SGE_size = sizeof(u32) + ioc->sg_addr_size;

	printk(MYIOC_s_INFO_FMT "pci-resume: ioc-state=0x%x,doorbell=0x%x\n",
	    ioc->name, (mpt_GetIocState(ioc, 1) >> MPI_IOC_STATE_SHIFT),
	    CHIPREG_READ32(&ioc->chip->Doorbell));

	/*
	 * Errata workaround for SAS pci express:
	 * Upon returning to the D0 state, the contents of the doorbell will be
	 * stale data, and this will incorrectly signal to the host driver that
	 * the firmware is ready to process mpt commands.   The workaround is
	 * to issue a diagnostic reset.
	 */
	if (ioc->bus_type == SAS && (pdev->device ==
	    MPI_MANUFACTPAGE_DEVID_SAS1068E || pdev->device ==
	    MPI_MANUFACTPAGE_DEVID_SAS1064E)) {
		if (KickStart(ioc, 1, CAN_SLEEP) < 0) {
			printk(MYIOC_s_WARN_FMT "pci-resume: Cannot recover\n",
			    ioc->name);
			goto out;
		}
	}

	/* bring ioc to operational state */
	printk(MYIOC_s_INFO_FMT "Sending mpt_do_ioc_recovery\n", ioc->name);
	recovery_state = mpt_do_ioc_recovery(ioc, MPT_HOSTEVENT_IOC_BRINGUP,
						 CAN_SLEEP);
	if (recovery_state != 0)
		printk(MYIOC_s_WARN_FMT "pci-resume: Cannot recover, "
		    "error:[%x]\n", ioc->name, recovery_state);
	else
		printk(MYIOC_s_INFO_FMT
		    "pci-resume: success\n", ioc->name);
 out:
	return 0;

}
#endif

static int
mpt_signal_reset(u8 index, MPT_ADAPTER *ioc, int reset_phase)
{
	if ((MptDriverClass[index] == MPTSPI_DRIVER &&
	     ioc->bus_type != SPI) ||
	    (MptDriverClass[index] == MPTFC_DRIVER &&
	     ioc->bus_type != FC) ||
	    (MptDriverClass[index] == MPTSAS_DRIVER &&
	     ioc->bus_type != SAS))
		/* make sure we only call the relevant reset handler
		 * for the bus */
		return 0;
	return (MptResetHandlers[index])(ioc, reset_phase);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_do_ioc_recovery - Initialize or recover MPT adapter.
 *	@ioc: Pointer to MPT adapter structure
 *	@reason: Event word / reason
 *	@sleepFlag: Use schedule if CAN_SLEEP else use udelay.
 *
 *	This routine performs all the steps necessary to bring the IOC
 *	to a OPERATIONAL state.
 *
 *	This routine also pre-fetches the LAN MAC address of a Fibre Channel
 *	MPT adapter.
 *
 *	Returns:
 *		 0 for success
 *		-1 if failed to get board READY
 *		-2 if READY but IOCFacts Failed
 *		-3 if READY but PrimeIOCFifos Failed
 *		-4 if READY but IOCInit Failed
 *		-5 if failed to enable_device and/or request_selected_regions
 *		-6 if failed to upload firmware
 */
static int
mpt_do_ioc_recovery(MPT_ADAPTER *ioc, u32 reason, int sleepFlag)
{
	int	 hard_reset_done = 0;
	int	 alt_ioc_ready = 0;
	int	 hard;
	int	 rc=0;
	int	 ii;
	int	 ret = 0;
	int	 reset_alt_ioc_active = 0;
	int	 irq_allocated = 0;
	u8	*a;

	printk(MYIOC_s_INFO_FMT "Initiating %s\n", ioc->name,
	    reason == MPT_HOSTEVENT_IOC_BRINGUP ? "bringup" : "recovery");

	/* Disable reply interrupts (also blocks FreeQ) */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;

	if (ioc->alt_ioc) {
		if (ioc->alt_ioc->active ||
		    reason == MPT_HOSTEVENT_IOC_RECOVER) {
			reset_alt_ioc_active = 1;
			/* Disable alt-IOC's reply interrupts
			 *  (and FreeQ) for a bit
			 **/
			CHIPREG_WRITE32(&ioc->alt_ioc->chip->IntMask,
				0xFFFFFFFF);
			ioc->alt_ioc->active = 0;
		}
	}

	hard = 1;
	if (reason == MPT_HOSTEVENT_IOC_BRINGUP)
		hard = 0;

	if ((hard_reset_done = MakeIocReady(ioc, hard, sleepFlag)) < 0) {
		if (hard_reset_done == -4) {
			printk(MYIOC_s_WARN_FMT "Owned by PEER..skipping!\n",
			    ioc->name);

			if (reset_alt_ioc_active && ioc->alt_ioc) {
				/* (re)Enable alt-IOC! (reply interrupt, FreeQ) */
				dprintk(ioc, printk(MYIOC_s_INFO_FMT
				    "alt_ioc reply irq re-enabled\n", ioc->alt_ioc->name));
				CHIPREG_WRITE32(&ioc->alt_ioc->chip->IntMask, MPI_HIM_DIM);
				ioc->alt_ioc->active = 1;
			}

		} else {
			printk(MYIOC_s_WARN_FMT
			    "NOT READY WARNING!\n", ioc->name);
		}
		ret = -1;
		goto out;
	}

	/* hard_reset_done = 0 if a soft reset was performed
	 * and 1 if a hard reset was performed.
	 */
	if (hard_reset_done && reset_alt_ioc_active && ioc->alt_ioc) {
		if ((rc = MakeIocReady(ioc->alt_ioc, 0, sleepFlag)) == 0)
			alt_ioc_ready = 1;
		else
			printk(MYIOC_s_WARN_FMT
			    ": alt-ioc Not ready WARNING!\n",
			    ioc->alt_ioc->name);
	}

	for (ii=0; ii<5; ii++) {
		/* Get IOC facts! Allow 5 retries */
		if ((rc = GetIocFacts(ioc, sleepFlag, reason)) == 0)
			break;
	}


	if (ii == 5) {
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "Retry IocFacts failed rc=%x\n", ioc->name, rc));
		ret = -2;
	} else if (reason == MPT_HOSTEVENT_IOC_BRINGUP) {
		MptDisplayIocCapabilities(ioc);
	}

	if (alt_ioc_ready) {
		if ((rc = GetIocFacts(ioc->alt_ioc, sleepFlag, reason)) != 0) {
			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "Initial Alt IocFacts failed rc=%x\n",
			    ioc->name, rc));
			/* Retry - alt IOC was initialized once
			 */
			rc = GetIocFacts(ioc->alt_ioc, sleepFlag, reason);
		}
		if (rc) {
			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "Retry Alt IocFacts failed rc=%x\n", ioc->name, rc));
			alt_ioc_ready = 0;
			reset_alt_ioc_active = 0;
		} else if (reason == MPT_HOSTEVENT_IOC_BRINGUP) {
			MptDisplayIocCapabilities(ioc->alt_ioc);
		}
	}

	if ((ret == 0) && (reason == MPT_HOSTEVENT_IOC_BRINGUP) &&
	    (ioc->facts.Flags & MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT)) {
		pci_release_selected_regions(ioc->pcidev, ioc->bars);
		ioc->bars = pci_select_bars(ioc->pcidev, IORESOURCE_MEM |
		    IORESOURCE_IO);
		if (pci_enable_device(ioc->pcidev))
			return -5;
		if (pci_request_selected_regions(ioc->pcidev, ioc->bars,
			"mpt"))
			return -5;
	}

	/*
	 * Device is reset now. It must have de-asserted the interrupt line
	 * (if it was asserted) and it should be safe to register for the
	 * interrupt now.
	 */
	if ((ret == 0) && (reason == MPT_HOSTEVENT_IOC_BRINGUP)) {
		ioc->pci_irq = -1;
		if (ioc->pcidev->irq) {
			if (ioc->msi_enable && !pci_enable_msi(ioc->pcidev))
				printk(MYIOC_s_INFO_FMT "PCI-MSI enabled\n",
				    ioc->name);
			else
				ioc->msi_enable = 0;
			rc = request_irq(ioc->pcidev->irq, mpt_interrupt,
			    IRQF_SHARED, ioc->name, ioc);
			if (rc < 0) {
				printk(MYIOC_s_ERR_FMT "Unable to allocate "
				    "interrupt %d!\n",
				    ioc->name, ioc->pcidev->irq);
				if (ioc->msi_enable)
					pci_disable_msi(ioc->pcidev);
				ret = -EBUSY;
				goto out;
			}
			irq_allocated = 1;
			ioc->pci_irq = ioc->pcidev->irq;
			pci_set_master(ioc->pcidev);		/* ?? */
			pci_set_drvdata(ioc->pcidev, ioc);
			dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
			    "installed at interrupt %d\n", ioc->name,
			    ioc->pcidev->irq));
		}
	}

	/* Prime reply & request queues!
	 * (mucho alloc's) Must be done prior to
	 * init as upper addresses are needed for init.
	 * If fails, continue with alt-ioc processing
	 */
	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT "PrimeIocFifos\n",
	    ioc->name));
	if ((ret == 0) && ((rc = PrimeIocFifos(ioc)) != 0))
		ret = -3;

	/* May need to check/upload firmware & data here!
	 * If fails, continue with alt-ioc processing
	 */
	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT "SendIocInit\n",
	    ioc->name));
	if ((ret == 0) && ((rc = SendIocInit(ioc, sleepFlag)) != 0))
		ret = -4;
// NEW!
	if (alt_ioc_ready && ((rc = PrimeIocFifos(ioc->alt_ioc)) != 0)) {
		printk(MYIOC_s_WARN_FMT
		    ": alt-ioc (%d) FIFO mgmt alloc WARNING!\n",
		    ioc->alt_ioc->name, rc);
		alt_ioc_ready = 0;
		reset_alt_ioc_active = 0;
	}

	if (alt_ioc_ready) {
		if ((rc = SendIocInit(ioc->alt_ioc, sleepFlag)) != 0) {
			alt_ioc_ready = 0;
			reset_alt_ioc_active = 0;
			printk(MYIOC_s_WARN_FMT
				": alt-ioc: (%d) init failure WARNING!\n",
					ioc->alt_ioc->name, rc);
		}
	}

	if (reason == MPT_HOSTEVENT_IOC_BRINGUP){
		if (ioc->upload_fw) {
			ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "firmware upload required!\n", ioc->name));

			/* Controller is not operational, cannot do upload
			 */
			if (ret == 0) {
				rc = mpt_do_upload(ioc, sleepFlag);
				if (rc == 0) {
					if (ioc->alt_ioc && ioc->alt_ioc->cached_fw) {
						/*
						 * Maintain only one pointer to FW memory
						 * so there will not be two attempt to
						 * downloadboot onboard dual function
						 * chips (mpt_adapter_disable,
						 * mpt_diag_reset)
						 */
						ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
						    "mpt_upload:  alt_%s has cached_fw=%p \n",
						    ioc->name, ioc->alt_ioc->name, ioc->alt_ioc->cached_fw));
						ioc->cached_fw = NULL;
					}
				} else {
					printk(MYIOC_s_WARN_FMT
					    "firmware upload failure!\n", ioc->name);
					ret = -6;
				}
			}
		}
	}

	/*  Enable MPT base driver management of EventNotification
	 *  and EventAck handling.
	 */
	if ((ret == 0) && (!ioc->facts.EventState)) {
		dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
			"SendEventNotification\n",
		    ioc->name));
		ret = SendEventNotification(ioc, 1, sleepFlag);	/* 1=Enable */
	}

	if (ioc->alt_ioc && alt_ioc_ready && !ioc->alt_ioc->facts.EventState)
		rc = SendEventNotification(ioc->alt_ioc, 1, sleepFlag);

	if (ret == 0) {
		/* Enable! (reply interrupt) */
		CHIPREG_WRITE32(&ioc->chip->IntMask, MPI_HIM_DIM);
		ioc->active = 1;
	}
	if (rc == 0) {	/* alt ioc */
		if (reset_alt_ioc_active && ioc->alt_ioc) {
			/* (re)Enable alt-IOC! (reply interrupt) */
			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "alt-ioc"
				"reply irq re-enabled\n",
				ioc->alt_ioc->name));
			CHIPREG_WRITE32(&ioc->alt_ioc->chip->IntMask,
				MPI_HIM_DIM);
			ioc->alt_ioc->active = 1;
		}
	}


	/*	Add additional "reason" check before call to GetLanConfigPages
	 *	(combined with GetIoUnitPage2 call).  This prevents a somewhat
	 *	recursive scenario; GetLanConfigPages times out, timer expired
	 *	routine calls HardResetHandler, which calls into here again,
	 *	and we try GetLanConfigPages again...
	 */
	if ((ret == 0) && (reason == MPT_HOSTEVENT_IOC_BRINGUP)) {

		/*
		 * Initialize link list for inactive raid volumes.
		 */
		mutex_init(&ioc->raid_data.inactive_list_mutex);
		INIT_LIST_HEAD(&ioc->raid_data.inactive_list);

		switch (ioc->bus_type) {

		case SAS:
			/* clear persistency table */
			if(ioc->facts.IOCExceptions &
			    MPI_IOCFACTS_EXCEPT_PERSISTENT_TABLE_FULL) {
				ret = mptbase_sas_persist_operation(ioc,
				    MPI_SAS_OP_CLEAR_NOT_PRESENT);
				if(ret != 0)
					goto out;
			}

			/* Find IM volumes
			 */
			mpt_findImVolumes(ioc);

			/* Check, and possibly reset, the coalescing value
			 */
			mpt_read_ioc_pg_1(ioc);

			break;

		case FC:
			if ((ioc->pfacts[0].ProtocolFlags &
				MPI_PORTFACTS_PROTOCOL_LAN) &&
			    (ioc->lan_cnfg_page0.Header.PageLength == 0)) {
				/*
				 *  Pre-fetch the ports LAN MAC address!
				 *  (LANPage1_t stuff)
				 */
				(void) GetLanConfigPages(ioc);
				a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
				dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"LanAddr = %pMR\n", ioc->name, a));
			}
			break;

		case SPI:
			/* Get NVRAM and adapter maximums from SPP 0 and 2
			 */
			mpt_GetScsiPortSettings(ioc, 0);

			/* Get version and length of SDP 1
			 */
			mpt_readScsiDevicePageHeaders(ioc, 0);

			/* Find IM volumes
			 */
			if (ioc->facts.MsgVersion >= MPI_VERSION_01_02)
				mpt_findImVolumes(ioc);

			/* Check, and possibly reset, the coalescing value
			 */
			mpt_read_ioc_pg_1(ioc);

			mpt_read_ioc_pg_4(ioc);

			break;
		}

		GetIoUnitPage2(ioc);
		mpt_get_manufacturing_pg_0(ioc);
	}

 out:
	if ((ret != 0) && irq_allocated) {
		free_irq(ioc->pci_irq, ioc);
		if (ioc->msi_enable)
			pci_disable_msi(ioc->pcidev);
	}
	return ret;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_detect_bound_ports - Search for matching PCI bus/dev_function
 *	@ioc: Pointer to MPT adapter structure
 *	@pdev: Pointer to (struct pci_dev) structure
 *
 *	Search for PCI bus/dev_function which matches
 *	PCI bus/dev_function (+/-1) for newly discovered 929,
 *	929X, 1030 or 1035.
 *
 *	If match on PCI dev_function +/-1 is found, bind the two MPT adapters
 *	using alt_ioc pointer fields in their %MPT_ADAPTER structures.
 */
static void
mpt_detect_bound_ports(MPT_ADAPTER *ioc, struct pci_dev *pdev)
{
	struct pci_dev *peer=NULL;
	unsigned int slot = PCI_SLOT(pdev->devfn);
	unsigned int func = PCI_FUNC(pdev->devfn);
	MPT_ADAPTER *ioc_srch;

	dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PCI device %s devfn=%x/%x,"
	    " searching for devfn match on %x or %x\n",
	    ioc->name, pci_name(pdev), pdev->bus->number,
	    pdev->devfn, func-1, func+1));

	peer = pci_get_slot(pdev->bus, PCI_DEVFN(slot,func-1));
	if (!peer) {
		peer = pci_get_slot(pdev->bus, PCI_DEVFN(slot,func+1));
		if (!peer)
			return;
	}

	list_for_each_entry(ioc_srch, &ioc_list, list) {
		struct pci_dev *_pcidev = ioc_srch->pcidev;
		if (_pcidev == peer) {
			/* Paranoia checks */
			if (ioc->alt_ioc != NULL) {
				printk(MYIOC_s_WARN_FMT
				    "Oops, already bound (%s <==> %s)!\n",
				    ioc->name, ioc->name, ioc->alt_ioc->name);
				break;
			} else if (ioc_srch->alt_ioc != NULL) {
				printk(MYIOC_s_WARN_FMT
				    "Oops, already bound (%s <==> %s)!\n",
				    ioc_srch->name, ioc_srch->name,
				    ioc_srch->alt_ioc->name);
				break;
			}
			dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"FOUND! binding %s <==> %s\n",
				ioc->name, ioc->name, ioc_srch->name));
			ioc_srch->alt_ioc = ioc;
			ioc->alt_ioc = ioc_srch;
		}
	}
	pci_dev_put(peer);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_adapter_disable - Disable misbehaving MPT adapter.
 *	@ioc: Pointer to MPT adapter structure
 */
static void
mpt_adapter_disable(MPT_ADAPTER *ioc)
{
	int sz;
	int ret;

	if (ioc->cached_fw != NULL) {
		ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"%s: Pushing FW onto adapter\n", __func__, ioc->name));
		if ((ret = mpt_downloadboot(ioc, (MpiFwHeader_t *)
		    ioc->cached_fw, CAN_SLEEP)) < 0) {
			printk(MYIOC_s_WARN_FMT
			    ": firmware downloadboot failure (%d)!\n",
			    ioc->name, ret);
		}
	}

	/*
	 * Put the controller into ready state (if its not already)
	 */
	if (mpt_GetIocState(ioc, 1) != MPI_IOC_STATE_READY) {
		if (!SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET,
		    CAN_SLEEP)) {
			if (mpt_GetIocState(ioc, 1) != MPI_IOC_STATE_READY)
				printk(MYIOC_s_ERR_FMT "%s:  IOC msg unit "
				    "reset failed to put ioc in ready state!\n",
				    ioc->name, __func__);
		} else
			printk(MYIOC_s_ERR_FMT "%s:  IOC msg unit reset "
			    "failed!\n", ioc->name, __func__);
	}


	/* Disable adapter interrupts! */
	synchronize_irq(ioc->pcidev->irq);
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;

	/* Clear any lingering interrupt */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	CHIPREG_READ32(&ioc->chip->IntStatus);

	if (ioc->alloc != NULL) {
		sz = ioc->alloc_sz;
		dexitprintk(ioc, printk(MYIOC_s_INFO_FMT "free  @ %p, sz=%d bytes\n",
		    ioc->name, ioc->alloc, ioc->alloc_sz));
		pci_free_consistent(ioc->pcidev, sz,
				ioc->alloc, ioc->alloc_dma);
		ioc->reply_frames = NULL;
		ioc->req_frames = NULL;
		ioc->alloc = NULL;
		ioc->alloc_total -= sz;
	}

	if (ioc->sense_buf_pool != NULL) {
		sz = (ioc->req_depth * MPT_SENSE_BUFFER_ALLOC);
		pci_free_consistent(ioc->pcidev, sz,
				ioc->sense_buf_pool, ioc->sense_buf_pool_dma);
		ioc->sense_buf_pool = NULL;
		ioc->alloc_total -= sz;
	}

	if (ioc->events != NULL){
		sz = MPTCTL_EVENT_LOG_SIZE * sizeof(MPT_IOCTL_EVENTS);
		kfree(ioc->events);
		ioc->events = NULL;
		ioc->alloc_total -= sz;
	}

	mpt_free_fw_memory(ioc);

	kfree(ioc->spi_data.nvram);
	mpt_inactive_raid_list_free(ioc);
	kfree(ioc->raid_data.pIocPg2);
	kfree(ioc->raid_data.pIocPg3);
	ioc->spi_data.nvram = NULL;
	ioc->raid_data.pIocPg3 = NULL;

	if (ioc->spi_data.pIocPg4 != NULL) {
		sz = ioc->spi_data.IocPg4Sz;
		pci_free_consistent(ioc->pcidev, sz,
			ioc->spi_data.pIocPg4,
			ioc->spi_data.IocPg4_dma);
		ioc->spi_data.pIocPg4 = NULL;
		ioc->alloc_total -= sz;
	}

	if (ioc->ReqToChain != NULL) {
		kfree(ioc->ReqToChain);
		kfree(ioc->RequestNB);
		ioc->ReqToChain = NULL;
	}

	kfree(ioc->ChainToChain);
	ioc->ChainToChain = NULL;

	if (ioc->HostPageBuffer != NULL) {
		if((ret = mpt_host_page_access_control(ioc,
		    MPI_DB_HPBAC_FREE_BUFFER, NO_SLEEP)) != 0) {
			printk(MYIOC_s_ERR_FMT
			   ": %s: host page buffers free failed (%d)!\n",
			    ioc->name, __func__, ret);
		}
		dexitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"HostPageBuffer free  @ %p, sz=%d bytes\n",
			ioc->name, ioc->HostPageBuffer,
			ioc->HostPageBuffer_sz));
		pci_free_consistent(ioc->pcidev, ioc->HostPageBuffer_sz,
		    ioc->HostPageBuffer, ioc->HostPageBuffer_dma);
		ioc->HostPageBuffer = NULL;
		ioc->HostPageBuffer_sz = 0;
		ioc->alloc_total -= ioc->HostPageBuffer_sz;
	}

	pci_set_drvdata(ioc->pcidev, NULL);
}
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_adapter_dispose - Free all resources associated with an MPT adapter
 *	@ioc: Pointer to MPT adapter structure
 *
 *	This routine unregisters h/w resources and frees all alloc'd memory
 *	associated with a MPT adapter structure.
 */
static void
mpt_adapter_dispose(MPT_ADAPTER *ioc)
{
	int sz_first, sz_last;

	if (ioc == NULL)
		return;

	sz_first = ioc->alloc_total;

	mpt_adapter_disable(ioc);

	if (ioc->pci_irq != -1) {
		free_irq(ioc->pci_irq, ioc);
		if (ioc->msi_enable)
			pci_disable_msi(ioc->pcidev);
		ioc->pci_irq = -1;
	}

	if (ioc->memmap != NULL) {
		iounmap(ioc->memmap);
		ioc->memmap = NULL;
	}

	pci_disable_device(ioc->pcidev);
	pci_release_selected_regions(ioc->pcidev, ioc->bars);

	/*  Zap the adapter lookup ptr!  */
	list_del(&ioc->list);

	sz_last = ioc->alloc_total;
	dprintk(ioc, printk(MYIOC_s_INFO_FMT "free'd %d of %d bytes\n",
	    ioc->name, sz_first-sz_last+(int)sizeof(*ioc), sz_first));

	if (ioc->alt_ioc)
		ioc->alt_ioc->alt_ioc = NULL;

	kfree(ioc);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	MptDisplayIocCapabilities - Disply IOC's capabilities.
 *	@ioc: Pointer to MPT adapter structure
 */
static void
MptDisplayIocCapabilities(MPT_ADAPTER *ioc)
{
	int i = 0;

	printk(KERN_INFO "%s: ", ioc->name);
	if (ioc->prod_name)
		pr_cont("%s: ", ioc->prod_name);
	pr_cont("Capabilities={");

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_INITIATOR) {
		pr_cont("Initiator");
		i++;
	}

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_TARGET) {
		pr_cont("%sTarget", i ? "," : "");
		i++;
	}

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN) {
		pr_cont("%sLAN", i ? "," : "");
		i++;
	}

#if 0
	/*
	 *  This would probably evoke more questions than it's worth
	 */
	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_TARGET) {
		pr_cont("%sLogBusAddr", i ? "," : "");
		i++;
	}
#endif

	pr_cont("}\n");
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	MakeIocReady - Get IOC to a READY state, using KickStart if needed.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@force: Force hard KickStart of IOC
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Returns:
 *		 1 - DIAG reset and READY
 *		 0 - READY initially OR soft reset and READY
 *		-1 - Any failure on KickStart
 *		-2 - Msg Unit Reset Failed
 *		-3 - IO Unit Reset Failed
 *		-4 - IOC owned by a PEER
 */
static int
MakeIocReady(MPT_ADAPTER *ioc, int force, int sleepFlag)
{
	u32	 ioc_state;
	int	 statefault = 0;
	int	 cntdn;
	int	 hard_reset_done = 0;
	int	 r;
	int	 ii;
	int	 whoinit;

	/* Get current [raw] IOC state  */
	ioc_state = mpt_GetIocState(ioc, 0);
	dhsprintk(ioc, printk(MYIOC_s_INFO_FMT "MakeIocReady [raw] state=%08x\n", ioc->name, ioc_state));

	/*
	 *	Check to see if IOC got left/stuck in doorbell handshake
	 *	grip of death.  If so, hard reset the IOC.
	 */
	if (ioc_state & MPI_DOORBELL_ACTIVE) {
		statefault = 1;
		printk(MYIOC_s_WARN_FMT "Unexpected doorbell active!\n",
				ioc->name);
	}

	/* Is it already READY? */
	if (!statefault &&
	    ((ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_READY)) {
		dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
		    "IOC is in READY state\n", ioc->name));
		return 0;
	}

	/*
	 *	Check to see if IOC is in FAULT state.
	 */
	if ((ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_FAULT) {
		statefault = 2;
		printk(MYIOC_s_WARN_FMT "IOC is in FAULT state!!!\n",
		    ioc->name);
		printk(MYIOC_s_WARN_FMT "           FAULT code = %04xh\n",
		    ioc->name, ioc_state & MPI_DOORBELL_DATA_MASK);
	}

	/*
	 *	Hmmm...  Did it get left operational?
	 */
	if ((ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_OPERATIONAL) {
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "IOC operational unexpected\n",
				ioc->name));

		/* Check WhoInit.
		 * If PCI Peer, exit.
		 * Else, if no fault conditions are present, issue a MessageUnitReset
		 * Else, fall through to KickStart case
		 */
		whoinit = (ioc_state & MPI_DOORBELL_WHO_INIT_MASK) >> MPI_DOORBELL_WHO_INIT_SHIFT;
		dinitprintk(ioc, printk(MYIOC_s_INFO_FMT
			"whoinit 0x%x statefault %d force %d\n",
			ioc->name, whoinit, statefault, force));
		if (whoinit == MPI_WHOINIT_PCI_PEER)
			return -4;
		else {
			if ((statefault == 0 ) && (force == 0)) {
				if ((r = SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET, sleepFlag)) == 0)
					return 0;
			}
			statefault = 3;
		}
	}

	hard_reset_done = KickStart(ioc, statefault||force, sleepFlag);
	if (hard_reset_done < 0)
		return -1;

	/*
	 *  Loop here waiting for IOC to come READY.
	 */
	ii = 0;
	cntdn = ((sleepFlag == CAN_SLEEP) ? HZ : 1000) * 5;	/* 5 seconds */

	while ((ioc_state = mpt_GetIocState(ioc, 1)) != MPI_IOC_STATE_READY) {
		if (ioc_state == MPI_IOC_STATE_OPERATIONAL) {
			/*
			 *  BIOS or previous driver load left IOC in OP state.
			 *  Reset messaging FIFOs.
			 */
			if ((r = SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET, sleepFlag)) != 0) {
				printk(MYIOC_s_ERR_FMT "IOC msg unit reset failed!\n", ioc->name);
				return -2;
			}
		} else if (ioc_state == MPI_IOC_STATE_RESET) {
			/*
			 *  Something is wrong.  Try to get IOC back
			 *  to a known state.
			 */
			if ((r = SendIocReset(ioc, MPI_FUNCTION_IO_UNIT_RESET, sleepFlag)) != 0) {
				printk(MYIOC_s_ERR_FMT "IO unit reset failed!\n", ioc->name);
				return -3;
			}
		}

		ii++; cntdn--;
		if (!cntdn) {
			printk(MYIOC_s_ERR_FMT
				"Wait IOC_READY state (0x%x) timeout(%d)!\n",
				ioc->name, ioc_state, (int)((ii+5)/HZ));
			return -ETIME;
		}

		if (sleepFlag == CAN_SLEEP) {
			msleep(1);
		} else {
			mdelay (1);	/* 1 msec delay */
		}

	}

	if (statefault < 3) {
		printk(MYIOC_s_INFO_FMT "Recovered from %s\n", ioc->name,
			statefault == 1 ? "stuck handshake" : "IOC FAULT");
	}

	return hard_reset_done;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_GetIocState - Get the current state of a MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@cooked: Request raw or cooked IOC state
 *
 *	Returns all IOC Doorbell register bits if cooked==0, else just the
 *	Doorbell bits in MPI_IOC_STATE_MASK.
 */
u32
mpt_GetIocState(MPT_ADAPTER *ioc, int cooked)
{
	u32 s, sc;

	/*  Get!  */
	s = CHIPREG_READ32(&ioc->chip->Doorbell);
	sc = s & MPI_IOC_STATE_MASK;

	/*  Save!  */
	ioc->last_state = sc;

	return cooked ? sc : s;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	GetIocFacts - Send IOCFacts request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Specifies whether the process can sleep
 *	@reason: If recovery, only update facts.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
GetIocFacts(MPT_ADAPTER *ioc, int sleepFlag, int reason)
{
	IOCFacts_t		 get_facts;
	IOCFactsReply_t		*facts;
	int			 r;
	int			 req_sz;
	int			 reply_sz;
	int			 sz;
	u32			 status, vv;
	u8			 shiftFactor=1;

	/* IOC *must* NOT be in RESET state! */
	if (ioc->last_state == MPI_IOC_STATE_RESET) {
		printk(KERN_ERR MYNAM
		    ": ERROR - Can't get IOCFacts, %s NOT READY! (%08x)\n",
		    ioc->name, ioc->last_state);
		return -44;
	}

	facts = &ioc->facts;

	/* Destination (reply area)... */
	reply_sz = sizeof(*facts);
	memset(facts, 0, reply_sz);

	/* Request area (get_facts on the stack right now!) */
	req_sz = sizeof(get_facts);
	memset(&get_facts, 0, req_sz);

	get_facts.Function = MPI_FUNCTION_IOC_FACTS;
	/* Assert: All other get_facts fields are zero! */

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Sending get IocFacts request req_sz=%d reply_sz=%d\n",
	    ioc->name, req_sz, reply_sz));

	/* No non-zero fields in the get_facts request are greater than
	 * 1 byte in size, so we can just fire it off as is.
	 */
	r = mpt_handshake_req_reply_wait(ioc, req_sz, (u32*)&get_facts,
			reply_sz, (u16*)facts, 5 /*seconds*/, sleepFlag);
	if (r != 0)
		return r;

	/*
	 * Now byte swap (GRRR) the necessary fields before any further
	 * inspection of reply contents.
	 *
	 * But need to do some sanity checks on MsgLength (byte) field
	 * to make sure we don't zero IOC's req_sz!
	 */
	/* Did we get a valid reply? */
	if (facts->MsgLength > offsetof(IOCFactsReply_t, RequestFrameSize)/sizeof(u32)) {
		if (reason == MPT_HOSTEVENT_IOC_BRINGUP) {
			/*
			 * If not been here, done that, save off first WhoInit value
			 */
			if (ioc->FirstWhoInit == WHOINIT_UNKNOWN)
				ioc->FirstWhoInit = facts->WhoInit;
		}

		facts->MsgVersion = le16_to_cpu(facts->MsgVersion);
		facts->MsgContext = le32_to_cpu(facts->MsgContext);
		facts->IOCExceptions = le16_to_cpu(facts->IOCExceptions);
		facts->IOCStatus = le16_to_cpu(facts->IOCStatus);
		facts->IOCLogInfo = le32_to_cpu(facts->IOCLogInfo);
		status = le16_to_cpu(facts->IOCStatus) & MPI_IOCSTATUS_MASK;
		/* CHECKME! IOCStatus, IOCLogInfo */

		facts->ReplyQueueDepth = le16_to_cpu(facts->ReplyQueueDepth);
		facts->RequestFrameSize = le16_to_cpu(facts->RequestFrameSize);

		/*
		 * FC f/w version changed between 1.1 and 1.2
		 *	Old: u16{Major(4),Minor(4),SubMinor(8)}
		 *	New: u32{Major(8),Minor(8),Unit(8),Dev(8)}
		 */
		if (facts->MsgVersion < MPI_VERSION_01_02) {
			/*
			 *	Handle old FC f/w style, convert to new...
			 */
			u16	 oldv = le16_to_cpu(facts->Reserved_0101_FWVersion);
			facts->FWVersion.Word =
					((oldv<<12) & 0xFF000000) |
					((oldv<<8)  & 0x000FFF00);
		} else
			facts->FWVersion.Word = le32_to_cpu(facts->FWVersion.Word);

		facts->ProductID = le16_to_cpu(facts->ProductID);

		if ((ioc->facts.ProductID & MPI_FW_HEADER_PID_PROD_MASK)
		    > MPI_FW_HEADER_PID_PROD_TARGET_SCSI)
			ioc->ir_firmware = 1;

		facts->CurrentHostMfaHighAddr =
				le32_to_cpu(facts->CurrentHostMfaHighAddr);
		facts->GlobalCredits = le16_to_cpu(facts->GlobalCredits);
		facts->CurrentSenseBufferHighAddr =
				le32_to_cpu(facts->CurrentSenseBufferHighAddr);
		facts->CurReplyFrameSize =
				le16_to_cpu(facts->CurReplyFrameSize);
		facts->IOCCapabilities = le32_to_cpu(facts->IOCCapabilities);

		/*
		 * Handle NEW (!) IOCFactsReply fields in MPI-1.01.xx
		 * Older MPI-1.00.xx struct had 13 dwords, and enlarged
		 * to 14 in MPI-1.01.0x.
		 */
		if (facts->MsgLength >= (offsetof(IOCFactsReply_t,FWImageSize) + 7)/4 &&
		    facts->MsgVersion > MPI_VERSION_01_00) {
			facts->FWImageSize = le32_to_cpu(facts->FWImageSize);
		}

		facts->FWImageSize = ALIGN(facts->FWImageSize, 4);

		if (!facts->RequestFrameSize) {
			/*  Something is wrong!  */
			printk(MYIOC_s_ERR_FMT "IOC reported invalid 0 request size!\n",
					ioc->name);
			return -55;
		}

		r = sz = facts->BlockSize;
		vv = ((63 / (sz * 4)) + 1) & 0x03;
		ioc->NB_for_64_byte_frame = vv;
		while ( sz )
		{
			shiftFactor++;
			sz = sz >> 1;
		}
		ioc->NBShiftFactor  = shiftFactor;
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "NB_for_64_byte_frame=%x NBShiftFactor=%x BlockSize=%x\n",
		    ioc->name, vv, shiftFactor, r));

		if (reason == MPT_HOSTEVENT_IOC_BRINGUP) {
			/*
			 * Set values for this IOC's request & reply frame sizes,
			 * and request & reply queue depths...
			 */
			ioc->req_sz = min(MPT_DEFAULT_FRAME_SIZE, facts->RequestFrameSize * 4);
			ioc->req_depth = min_t(int, MPT_MAX_REQ_DEPTH, facts->GlobalCredits);
			ioc->reply_sz = MPT_REPLY_FRAME_SIZE;
			ioc->reply_depth = min_t(int, MPT_DEFAULT_REPLY_DEPTH, facts->ReplyQueueDepth);

			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "reply_sz=%3d, reply_depth=%4d\n",
				ioc->name, ioc->reply_sz, ioc->reply_depth));
			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "req_sz  =%3d, req_depth  =%4d\n",
				ioc->name, ioc->req_sz, ioc->req_depth));

			/* Get port facts! */
			if ( (r = GetPortFacts(ioc, 0, sleepFlag)) != 0 )
				return r;
		}
	} else {
		printk(MYIOC_s_ERR_FMT
		     "Invalid IOC facts reply, msgLength=%d offsetof=%zd!\n",
		     ioc->name, facts->MsgLength, (offsetof(IOCFactsReply_t,
		     RequestFrameSize)/sizeof(u32)));
		return -66;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	GetPortFacts - Send PortFacts request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@portnum: Port number
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
GetPortFacts(MPT_ADAPTER *ioc, int portnum, int sleepFlag)
{
	PortFacts_t		 get_pfacts;
	PortFactsReply_t	*pfacts;
	int			 ii;
	int			 req_sz;
	int			 reply_sz;
	int			 max_id;

	/* IOC *must* NOT be in RESET state! */
	if (ioc->last_state == MPI_IOC_STATE_RESET) {
		printk(MYIOC_s_ERR_FMT "Can't get PortFacts NOT READY! (%08x)\n",
		    ioc->name, ioc->last_state );
		return -4;
	}

	pfacts = &ioc->pfacts[portnum];

	/* Destination (reply area)...  */
	reply_sz = sizeof(*pfacts);
	memset(pfacts, 0, reply_sz);

	/* Request area (get_pfacts on the stack right now!) */
	req_sz = sizeof(get_pfacts);
	memset(&get_pfacts, 0, req_sz);

	get_pfacts.Function = MPI_FUNCTION_PORT_FACTS;
	get_pfacts.PortNumber = portnum;
	/* Assert: All other get_pfacts fields are zero! */

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending get PortFacts(%d) request\n",
			ioc->name, portnum));

	/* No non-zero fields in the get_pfacts request are greater than
	 * 1 byte in size, so we can just fire it off as is.
	 */
	ii = mpt_handshake_req_reply_wait(ioc, req_sz, (u32*)&get_pfacts,
				reply_sz, (u16*)pfacts, 5 /*seconds*/, sleepFlag);
	if (ii != 0)
		return ii;

	/* Did we get a valid reply? */

	/* Now byte swap the necessary fields in the response. */
	pfacts->MsgContext = le32_to_cpu(pfacts->MsgContext);
	pfacts->IOCStatus = le16_to_cpu(pfacts->IOCStatus);
	pfacts->IOCLogInfo = le32_to_cpu(pfacts->IOCLogInfo);
	pfacts->MaxDevices = le16_to_cpu(pfacts->MaxDevices);
	pfacts->PortSCSIID = le16_to_cpu(pfacts->PortSCSIID);
	pfacts->ProtocolFlags = le16_to_cpu(pfacts->ProtocolFlags);
	pfacts->MaxPostedCmdBuffers = le16_to_cpu(pfacts->MaxPostedCmdBuffers);
	pfacts->MaxPersistentIDs = le16_to_cpu(pfacts->MaxPersistentIDs);
	pfacts->MaxLanBuckets = le16_to_cpu(pfacts->MaxLanBuckets);

	max_id = (ioc->bus_type == SAS) ? pfacts->PortSCSIID :
	    pfacts->MaxDevices;
	ioc->devices_per_bus = (max_id > 255) ? 256 : max_id;
	ioc->number_of_buses = (ioc->devices_per_bus < 256) ? 1 : max_id/256;

	/*
	 * Place all the devices on channels
	 *
	 * (for debuging)
	 */
	if (mpt_channel_mapping) {
		ioc->devices_per_bus = 1;
		ioc->number_of_buses = (max_id > 255) ? 255 : max_id;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendIocInit - Send IOCInit request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Send IOCInit followed by PortEnable to bring IOC to OPERATIONAL state.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendIocInit(MPT_ADAPTER *ioc, int sleepFlag)
{
	IOCInit_t		 ioc_init;
	MPIDefaultReply_t	 init_reply;
	u32			 state;
	int			 r;
	int			 count;
	int			 cntdn;

	memset(&ioc_init, 0, sizeof(ioc_init));
	memset(&init_reply, 0, sizeof(init_reply));

	ioc_init.WhoInit = MPI_WHOINIT_HOST_DRIVER;
	ioc_init.Function = MPI_FUNCTION_IOC_INIT;

	/* If we are in a recovery mode and we uploaded the FW image,
	 * then this pointer is not NULL. Skip the upload a second time.
	 * Set this flag if cached_fw set for either IOC.
	 */
	if (ioc->facts.Flags & MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT)
		ioc->upload_fw = 1;
	else
		ioc->upload_fw = 0;
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "upload_fw %d facts.Flags=%x\n",
		   ioc->name, ioc->upload_fw, ioc->facts.Flags));

	ioc_init.MaxDevices = (U8)ioc->devices_per_bus;
	ioc_init.MaxBuses = (U8)ioc->number_of_buses;

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "facts.MsgVersion=%x\n",
		   ioc->name, ioc->facts.MsgVersion));
	if (ioc->facts.MsgVersion >= MPI_VERSION_01_05) {
		// set MsgVersion and HeaderVersion host driver was built with
		ioc_init.MsgVersion = cpu_to_le16(MPI_VERSION);
	        ioc_init.HeaderVersion = cpu_to_le16(MPI_HEADER_VERSION);

		if (ioc->facts.Flags & MPI_IOCFACTS_FLAGS_HOST_PAGE_BUFFER_PERSISTENT) {
			ioc_init.HostPageBufferSGE = ioc->facts.HostPageBufferSGE;
		} else if(mpt_host_page_alloc(ioc, &ioc_init))
			return -99;
	}
	ioc_init.ReplyFrameSize = cpu_to_le16(ioc->reply_sz);	/* in BYTES */

	if (ioc->sg_addr_size == sizeof(u64)) {
		/* Save the upper 32-bits of the request
		 * (reply) and sense buffers.
		 */
		ioc_init.HostMfaHighAddr = cpu_to_le32((u32)((u64)ioc->alloc_dma >> 32));
		ioc_init.SenseBufferHighAddr = cpu_to_le32((u32)((u64)ioc->sense_buf_pool_dma >> 32));
	} else {
		/* Force 32-bit addressing */
		ioc_init.HostMfaHighAddr = cpu_to_le32(0);
		ioc_init.SenseBufferHighAddr = cpu_to_le32(0);
	}

	ioc->facts.CurrentHostMfaHighAddr = ioc_init.HostMfaHighAddr;
	ioc->facts.CurrentSenseBufferHighAddr = ioc_init.SenseBufferHighAddr;
	ioc->facts.MaxDevices = ioc_init.MaxDevices;
	ioc->facts.MaxBuses = ioc_init.MaxBuses;

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending IOCInit (req @ %p)\n",
			ioc->name, &ioc_init));

	r = mpt_handshake_req_reply_wait(ioc, sizeof(IOCInit_t), (u32*)&ioc_init,
				sizeof(MPIDefaultReply_t), (u16*)&init_reply, 10 /*seconds*/, sleepFlag);
	if (r != 0) {
		printk(MYIOC_s_ERR_FMT "Sending IOCInit failed(%d)!\n",ioc->name, r);
		return r;
	}

	/* No need to byte swap the multibyte fields in the reply
	 * since we don't even look at its contents.
	 */

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending PortEnable (req @ %p)\n",
			ioc->name, &ioc_init));

	if ((r = SendPortEnable(ioc, 0, sleepFlag)) != 0) {
		printk(MYIOC_s_ERR_FMT "Sending PortEnable failed(%d)!\n",ioc->name, r);
		return r;
	}

	/* YIKES!  SUPER IMPORTANT!!!
	 *  Poll IocState until _OPERATIONAL while IOC is doing
	 *  LoopInit and TargetDiscovery!
	 */
	count = 0;
	cntdn = ((sleepFlag == CAN_SLEEP) ? HZ : 1000) * 60;	/* 60 seconds */
	state = mpt_GetIocState(ioc, 1);
	while (state != MPI_IOC_STATE_OPERATIONAL && --cntdn) {
		if (sleepFlag == CAN_SLEEP) {
			msleep(1);
		} else {
			mdelay(1);
		}

		if (!cntdn) {
			printk(MYIOC_s_ERR_FMT "Wait IOC_OP state timeout(%d)!\n",
					ioc->name, (int)((count+5)/HZ));
			return -9;
		}

		state = mpt_GetIocState(ioc, 1);
		count++;
	}
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Wait IOC_OPERATIONAL state (cnt=%d)\n",
			ioc->name, count));

	ioc->aen_event_read_flag=0;
	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendPortEnable - Send PortEnable request to MPT adapter port.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@portnum: Port number to enable
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Send PortEnable to bring IOC to OPERATIONAL state.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendPortEnable(MPT_ADAPTER *ioc, int portnum, int sleepFlag)
{
	PortEnable_t		 port_enable;
	MPIDefaultReply_t	 reply_buf;
	int	 rc;
	int	 req_sz;
	int	 reply_sz;

	/*  Destination...  */
	reply_sz = sizeof(MPIDefaultReply_t);
	memset(&reply_buf, 0, reply_sz);

	req_sz = sizeof(PortEnable_t);
	memset(&port_enable, 0, req_sz);

	port_enable.Function = MPI_FUNCTION_PORT_ENABLE;
	port_enable.PortNumber = portnum;
/*	port_enable.ChainOffset = 0;		*/
/*	port_enable.MsgFlags = 0;		*/
/*	port_enable.MsgContext = 0;		*/

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending Port(%d)Enable (req @ %p)\n",
			ioc->name, portnum, &port_enable));

	/* RAID FW may take a long time to enable
	 */
	if (ioc->ir_firmware || ioc->bus_type == SAS) {
		rc = mpt_handshake_req_reply_wait(ioc, req_sz,
		(u32*)&port_enable, reply_sz, (u16*)&reply_buf,
		300 /*seconds*/, sleepFlag);
	} else {
		rc = mpt_handshake_req_reply_wait(ioc, req_sz,
		(u32*)&port_enable, reply_sz, (u16*)&reply_buf,
		30 /*seconds*/, sleepFlag);
	}
	return rc;
}

/**
 *	mpt_alloc_fw_memory - allocate firmware memory
 *	@ioc: Pointer to MPT_ADAPTER structure
 *      @size: total FW bytes
 *
 *	If memory has already been allocated, the same (cached) value
 *	is returned.
 *
 *	Return 0 if successful, or non-zero for failure
 **/
int
mpt_alloc_fw_memory(MPT_ADAPTER *ioc, int size)
{
	int rc;

	if (ioc->cached_fw) {
		rc = 0;  /* use already allocated memory */
		goto out;
	}
	else if (ioc->alt_ioc && ioc->alt_ioc->cached_fw) {
		ioc->cached_fw = ioc->alt_ioc->cached_fw;  /* use alt_ioc's memory */
		ioc->cached_fw_dma = ioc->alt_ioc->cached_fw_dma;
		rc = 0;
		goto out;
	}
	ioc->cached_fw = pci_alloc_consistent(ioc->pcidev, size, &ioc->cached_fw_dma);
	if (!ioc->cached_fw) {
		printk(MYIOC_s_ERR_FMT "Unable to allocate memory for the cached firmware image!\n",
		    ioc->name);
		rc = -1;
	} else {
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "FW Image  @ %p[%p], sz=%d[%x] bytes\n",
		    ioc->name, ioc->cached_fw, (void *)(ulong)ioc->cached_fw_dma, size, size));
		ioc->alloc_total += size;
		rc = 0;
	}
 out:
	return rc;
}

/**
 *	mpt_free_fw_memory - free firmware memory
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	If alt_img is NULL, delete from ioc structure.
 *	Else, delete a secondary image in same format.
 **/
void
mpt_free_fw_memory(MPT_ADAPTER *ioc)
{
	int sz;

	if (!ioc->cached_fw)
		return;

	sz = ioc->facts.FWImageSize;
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "free_fw_memory: FW Image  @ %p[%p], sz=%d[%x] bytes\n",
		 ioc->name, ioc->cached_fw, (void *)(ulong)ioc->cached_fw_dma, sz, sz));
	pci_free_consistent(ioc->pcidev, sz, ioc->cached_fw, ioc->cached_fw_dma);
	ioc->alloc_total -= sz;
	ioc->cached_fw = NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_do_upload - Construct and Send FWUpload request to MPT adapter port.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Returns 0 for success, >0 for handshake failure
 *		<0 for fw upload failure.
 *
 *	Remark: If bound IOC and a successful FWUpload was performed
 *	on the bound IOC, the second image is discarded
 *	and memory is free'd. Both channels must upload to prevent
 *	IOC from running in degraded mode.
 */
static int
mpt_do_upload(MPT_ADAPTER *ioc, int sleepFlag)
{
	u8			 reply[sizeof(FWUploadReply_t)];
	FWUpload_t		*prequest;
	FWUploadReply_t		*preply;
	FWUploadTCSGE_t		*ptcsge;
	u32			 flagsLength;
	int			 ii, sz, reply_sz;
	int			 cmdStatus;
	int			request_size;
	/* If the image size is 0, we are done.
	 */
	if ((sz = ioc->facts.FWImageSize) == 0)
		return 0;

	if (mpt_alloc_fw_memory(ioc, ioc->facts.FWImageSize) != 0)
		return -ENOMEM;

	dinitprintk(ioc, printk(MYIOC_s_INFO_FMT ": FW Image  @ %p[%p], sz=%d[%x] bytes\n",
	    ioc->name, ioc->cached_fw, (void *)(ulong)ioc->cached_fw_dma, sz, sz));

	prequest = (sleepFlag == NO_SLEEP) ? kzalloc(ioc->req_sz, GFP_ATOMIC) :
	    kzalloc(ioc->req_sz, GFP_KERNEL);
	if (!prequest) {
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "fw upload failed "
		    "while allocating memory \n", ioc->name));
		mpt_free_fw_memory(ioc);
		return -ENOMEM;
	}

	preply = (FWUploadReply_t *)&reply;

	reply_sz = sizeof(reply);
	memset(preply, 0, reply_sz);

	prequest->ImageType = MPI_FW_UPLOAD_ITYPE_FW_IOC_MEM;
	prequest->Function = MPI_FUNCTION_FW_UPLOAD;

	ptcsge = (FWUploadTCSGE_t *) &prequest->SGL;
	ptcsge->DetailsLength = 12;
	ptcsge->Flags = MPI_SGE_FLAGS_TRANSACTION_ELEMENT;
	ptcsge->ImageSize = cpu_to_le32(sz);
	ptcsge++;

	flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ | sz;
	ioc->add_sge((char *)ptcsge, flagsLength, ioc->cached_fw_dma);
	request_size = offsetof(FWUpload_t, SGL) + sizeof(FWUploadTCSGE_t) +
	    ioc->SGE_size;
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending FW Upload "
	    " (req @ %p) fw_size=%d mf_request_size=%d\n", ioc->name, prequest,
	    ioc->facts.FWImageSize, request_size));
	DBG_DUMP_FW_REQUEST_FRAME(ioc, (u32 *)prequest);

	ii = mpt_handshake_req_reply_wait(ioc, request_size, (u32 *)prequest,
	    reply_sz, (u16 *)preply, 65 /*seconds*/, sleepFlag);

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "FW Upload completed "
	    "rc=%x \n", ioc->name, ii));

	cmdStatus = -EFAULT;
	if (ii == 0) {
		/* Handshake transfer was complete and successful.
		 * Check the Reply Frame.
		 */
		int status;
		status = le16_to_cpu(preply->IOCStatus) &
				MPI_IOCSTATUS_MASK;
		if (status == MPI_IOCSTATUS_SUCCESS &&
		    ioc->facts.FWImageSize ==
		    le32_to_cpu(preply->ActualImageSize))
				cmdStatus = 0;
	}
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT ": do_upload cmdStatus=%d \n",
			ioc->name, cmdStatus));


	if (cmdStatus) {
		ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "fw upload failed, "
		    "freeing image \n", ioc->name));
		mpt_free_fw_memory(ioc);
	}
	kfree(prequest);

	return cmdStatus;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_downloadboot - DownloadBoot code
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@pFwHeader: Pointer to firmware header info
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	FwDownloadBoot requires Programmed IO access.
 *
 *	Returns 0 for success
 *		-1 FW Image size is 0
 *		-2 No valid cached_fw Pointer
 *		<0 for fw upload failure.
 */
static int
mpt_downloadboot(MPT_ADAPTER *ioc, MpiFwHeader_t *pFwHeader, int sleepFlag)
{
	MpiExtImageHeader_t	*pExtImage;
	u32			 fwSize;
	u32			 diag0val;
	int			 count;
	u32			*ptrFw;
	u32			 diagRwData;
	u32			 nextImage;
	u32			 load_addr;
	u32 			 ioc_state=0;

	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "downloadboot: fw size 0x%x (%d), FW Ptr %p\n",
				ioc->name, pFwHeader->ImageSize, pFwHeader->ImageSize, pFwHeader));

	CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFF);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);

	CHIPREG_WRITE32(&ioc->chip->Diagnostic, (MPI_DIAG_PREVENT_IOC_BOOT | MPI_DIAG_DISABLE_ARM));

	/* wait 1 msec */
	if (sleepFlag == CAN_SLEEP) {
		msleep(1);
	} else {
		mdelay (1);
	}

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val | MPI_DIAG_RESET_ADAPTER);

	for (count = 0; count < 30; count ++) {
		diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
		if (!(diag0val & MPI_DIAG_RESET_ADAPTER)) {
			ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RESET_ADAPTER cleared, count=%d\n",
				ioc->name, count));
			break;
		}
		/* wait .1 sec */
		if (sleepFlag == CAN_SLEEP) {
			msleep (100);
		} else {
			mdelay (100);
		}
	}

	if ( count == 30 ) {
		ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "downloadboot failed! "
		"Unable to get MPI_DIAG_DRWE mode, diag0val=%x\n",
		ioc->name, diag0val));
		return -3;
	}

	CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFF);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);

	/* Set the DiagRwEn and Disable ARM bits */
	CHIPREG_WRITE32(&ioc->chip->Diagnostic, (MPI_DIAG_RW_ENABLE | MPI_DIAG_DISABLE_ARM));

	fwSize = (pFwHeader->ImageSize + 3)/4;
	ptrFw = (u32 *) pFwHeader;

	/* Write the LoadStartAddress to the DiagRw Address Register
	 * using Programmed IO
	 */
	if (ioc->errata_flag_1064)
		pci_enable_io_access(ioc->pcidev);

	CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwAddress, pFwHeader->LoadStartAddress);
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "LoadStart addr written 0x%x \n",
		ioc->name, pFwHeader->LoadStartAddress));

	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Write FW Image: 0x%x bytes @ %p\n",
				ioc->name, fwSize*4, ptrFw));
	while (fwSize--) {
		CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwData, *ptrFw++);
	}

	nextImage = pFwHeader->NextImageHeaderOffset;
	while (nextImage) {
		pExtImage = (MpiExtImageHeader_t *) ((char *)pFwHeader + nextImage);

		load_addr = pExtImage->LoadStartAddress;

		fwSize = (pExtImage->ImageSize + 3) >> 2;
		ptrFw = (u32 *)pExtImage;

		ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Write Ext Image: 0x%x (%d) bytes @ %p load_addr=%x\n",
						ioc->name, fwSize*4, fwSize*4, ptrFw, load_addr));
		CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwAddress, load_addr);

		while (fwSize--) {
			CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwData, *ptrFw++);
		}
		nextImage = pExtImage->NextImageHeaderOffset;
	}

	/* Write the IopResetVectorRegAddr */
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Write IopResetVector Addr=%x! \n", ioc->name, 	pFwHeader->IopResetRegAddr));
	CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwAddress, pFwHeader->IopResetRegAddr);

	/* Write the IopResetVectorValue */
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Write IopResetVector Value=%x! \n", ioc->name, pFwHeader->IopResetVectorValue));
	CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwData, pFwHeader->IopResetVectorValue);

	/* Clear the internal flash bad bit - autoincrementing register,
	 * so must do two writes.
	 */
	if (ioc->bus_type == SPI) {
		/*
		 * 1030 and 1035 H/W errata, workaround to access
		 * the ClearFlashBadSignatureBit
		 */
		CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwAddress, 0x3F000000);
		diagRwData = CHIPREG_PIO_READ32(&ioc->pio_chip->DiagRwData);
		diagRwData |= 0x40000000;
		CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwAddress, 0x3F000000);
		CHIPREG_PIO_WRITE32(&ioc->pio_chip->DiagRwData, diagRwData);

	} else /* if((ioc->bus_type == SAS) || (ioc->bus_type == FC)) */ {
		diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
		CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val |
		    MPI_DIAG_CLEAR_FLASH_BAD_SIG);

		/* wait 1 msec */
		if (sleepFlag == CAN_SLEEP) {
			msleep (1);
		} else {
			mdelay (1);
		}
	}

	if (ioc->errata_flag_1064)
		pci_disable_io_access(ioc->pcidev);

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "downloadboot diag0val=%x, "
		"turning off PREVENT_IOC_BOOT, DISABLE_ARM, RW_ENABLE\n",
		ioc->name, diag0val));
	diag0val &= ~(MPI_DIAG_PREVENT_IOC_BOOT | MPI_DIAG_DISABLE_ARM | MPI_DIAG_RW_ENABLE);
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "downloadboot now diag0val=%x\n",
		ioc->name, diag0val));
	CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val);

	/* Write 0xFF to reset the sequencer */
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFF);

	if (ioc->bus_type == SAS) {
		ioc_state = mpt_GetIocState(ioc, 0);
		if ( (GetIocFacts(ioc, sleepFlag,
				MPT_HOSTEVENT_IOC_BRINGUP)) != 0 ) {
			ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT "GetIocFacts failed: IocState=%x\n",
					ioc->name, ioc_state));
			return -EFAULT;
		}
	}

	for (count=0; count<HZ*20; count++) {
		if ((ioc_state = mpt_GetIocState(ioc, 0)) & MPI_IOC_STATE_READY) {
			ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"downloadboot successful! (count=%d) IocState=%x\n",
				ioc->name, count, ioc_state));
			if (ioc->bus_type == SAS) {
				return 0;
			}
			if ((SendIocInit(ioc, sleepFlag)) != 0) {
				ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"downloadboot: SendIocInit failed\n",
					ioc->name));
				return -EFAULT;
			}
			ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"downloadboot: SendIocInit successful\n",
					ioc->name));
			return 0;
		}
		if (sleepFlag == CAN_SLEEP) {
			msleep (10);
		} else {
			mdelay (10);
		}
	}
	ddlprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		"downloadboot failed! IocState=%x\n",ioc->name, ioc_state));
	return -EFAULT;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	KickStart - Perform hard reset of MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@force: Force hard reset
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	This routine places MPT adapter in diagnostic mode via the
 *	WriteSequence register, and then performs a hard reset of adapter
 *	via the Diagnostic register.
 *
 *	Inputs:   sleepflag - CAN_SLEEP (non-interrupt thread)
 *			or NO_SLEEP (interrupt thread, use mdelay)
 *		  force - 1 if doorbell active, board fault state
 *				board operational, IOC_RECOVERY or
 *				IOC_BRINGUP and there is an alt_ioc.
 *			  0 else
 *
 *	Returns:
 *		 1 - hard reset, READY
 *		 0 - no reset due to History bit, READY
 *		-1 - no reset due to History bit but not READY
 *		     OR reset but failed to come READY
 *		-2 - no reset, could not enter DIAG mode
 *		-3 - reset but bad FW bit
 */
static int
KickStart(MPT_ADAPTER *ioc, int force, int sleepFlag)
{
	int hard_reset_done = 0;
	u32 ioc_state=0;
	int cnt,cntdn;

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "KickStarting!\n", ioc->name));
	if (ioc->bus_type == SPI) {
		/* Always issue a Msg Unit Reset first. This will clear some
		 * SCSI bus hang conditions.
		 */
		SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET, sleepFlag);

		if (sleepFlag == CAN_SLEEP) {
			msleep (1000);
		} else {
			mdelay (1000);
		}
	}

	hard_reset_done = mpt_diag_reset(ioc, force, sleepFlag);
	if (hard_reset_done < 0)
		return hard_reset_done;

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Diagnostic reset successful!\n",
		ioc->name));

	cntdn = ((sleepFlag == CAN_SLEEP) ? HZ : 1000) * 2;	/* 2 seconds */
	for (cnt=0; cnt<cntdn; cnt++) {
		ioc_state = mpt_GetIocState(ioc, 1);
		if ((ioc_state == MPI_IOC_STATE_READY) || (ioc_state == MPI_IOC_STATE_OPERATIONAL)) {
			dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "KickStart successful! (cnt=%d)\n",
 					ioc->name, cnt));
			return hard_reset_done;
		}
		if (sleepFlag == CAN_SLEEP) {
			msleep (10);
		} else {
			mdelay (10);
		}
	}

	dinitprintk(ioc, printk(MYIOC_s_ERR_FMT "Failed to come READY after reset! IocState=%x\n",
		ioc->name, mpt_GetIocState(ioc, 0)));
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_diag_reset - Perform hard reset of the adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@ignore: Set if to honor and clear to ignore
 *		the reset history bit
 *	@sleepFlag: CAN_SLEEP if called in a non-interrupt thread,
 *		else set to NO_SLEEP (use mdelay instead)
 *
 *	This routine places the adapter in diagnostic mode via the
 *	WriteSequence register and then performs a hard reset of adapter
 *	via the Diagnostic register. Adapter should be in ready state
 *	upon successful completion.
 *
 *	Returns:  1  hard reset successful
 *		  0  no reset performed because reset history bit set
 *		 -2  enabling diagnostic mode failed
 *		 -3  diagnostic reset failed
 */
static int
mpt_diag_reset(MPT_ADAPTER *ioc, int ignore, int sleepFlag)
{
	u32 diag0val;
	u32 doorbell;
	int hard_reset_done = 0;
	int count = 0;
	u32 diag1val = 0;
	MpiFwHeader_t *cached_fw;	/* Pointer to FW */
	u8	 cb_idx;

	/* Clear any existing interrupts */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	if (ioc->pcidev->device == MPI_MANUFACTPAGE_DEVID_SAS1078) {

		if (!ignore)
			return 0;

		drsprintk(ioc, printk(MYIOC_s_WARN_FMT "%s: Doorbell=%p; 1078 reset "
			"address=%p\n",  ioc->name, __func__,
			&ioc->chip->Doorbell, &ioc->chip->Reset_1078));
		CHIPREG_WRITE32(&ioc->chip->Reset_1078, 0x07);
		if (sleepFlag == CAN_SLEEP)
			msleep(1);
		else
			mdelay(1);

		/*
		 * Call each currently registered protocol IOC reset handler
		 * with pre-reset indication.
		 * NOTE: If we're doing _IOC_BRINGUP, there can be no
		 * MptResetHandlers[] registered yet.
		 */
		for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
			if (MptResetHandlers[cb_idx])
				(*(MptResetHandlers[cb_idx]))(ioc,
						MPT_IOC_PRE_RESET);
		}

		for (count = 0; count < 60; count ++) {
			doorbell = CHIPREG_READ32(&ioc->chip->Doorbell);
			doorbell &= MPI_IOC_STATE_MASK;

			drsprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"looking for READY STATE: doorbell=%x"
			        " count=%d\n",
				ioc->name, doorbell, count));

			if (doorbell == MPI_IOC_STATE_READY) {
				return 1;
			}

			/* wait 1 sec */
			if (sleepFlag == CAN_SLEEP)
				msleep(1000);
			else
				mdelay(1000);
		}
		return -1;
	}

	/* Use "Diagnostic reset" method! (only thing available!) */
	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);

	if (ioc->debug_level & MPT_DEBUG) {
		if (ioc->alt_ioc)
			diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DbG1: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
	}

	/* Do the reset if we are told to ignore the reset history
	 * or if the reset history is 0
	 */
	if (ignore || !(diag0val & MPI_DIAG_RESET_HISTORY)) {
		while ((diag0val & MPI_DIAG_DRWE) == 0) {
			/* Write magic sequence to WriteSequence register
			 * Loop until in diagnostic mode
			 */
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFF);
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
			CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);

			/* wait 100 msec */
			if (sleepFlag == CAN_SLEEP) {
				msleep (100);
			} else {
				mdelay (100);
			}

			count++;
			if (count > 20) {
				printk(MYIOC_s_ERR_FMT "Enable Diagnostic mode FAILED! (%02xh)\n",
						ioc->name, diag0val);
				return -2;

			}

			diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);

			dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Wrote magic DiagWriteEn sequence (%x)\n",
					ioc->name, diag0val));
		}

		if (ioc->debug_level & MPT_DEBUG) {
			if (ioc->alt_ioc)
				diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
			dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DbG2: diag0=%08x, diag1=%08x\n",
				ioc->name, diag0val, diag1val));
		}
		/*
		 * Disable the ARM (Bug fix)
		 *
		 */
		CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val | MPI_DIAG_DISABLE_ARM);
		mdelay(1);

		/*
		 * Now hit the reset bit in the Diagnostic register
		 * (THE BIG HAMMER!) (Clears DRWE bit).
		 */
		CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val | MPI_DIAG_RESET_ADAPTER);
		hard_reset_done = 1;
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Diagnostic reset performed\n",
				ioc->name));

		/*
		 * Call each currently registered protocol IOC reset handler
		 * with pre-reset indication.
		 * NOTE: If we're doing _IOC_BRINGUP, there can be no
		 * MptResetHandlers[] registered yet.
		 */
		for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
			if (MptResetHandlers[cb_idx]) {
				mpt_signal_reset(cb_idx,
					ioc, MPT_IOC_PRE_RESET);
				if (ioc->alt_ioc) {
					mpt_signal_reset(cb_idx,
					ioc->alt_ioc, MPT_IOC_PRE_RESET);
				}
			}
		}

		if (ioc->cached_fw)
			cached_fw = (MpiFwHeader_t *)ioc->cached_fw;
		else if (ioc->alt_ioc && ioc->alt_ioc->cached_fw)
			cached_fw = (MpiFwHeader_t *)ioc->alt_ioc->cached_fw;
		else
			cached_fw = NULL;
		if (cached_fw) {
			/* If the DownloadBoot operation fails, the
			 * IOC will be left unusable. This is a fatal error
			 * case.  _diag_reset will return < 0
			 */
			for (count = 0; count < 30; count ++) {
				diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
				if (!(diag0val & MPI_DIAG_RESET_ADAPTER)) {
					break;
				}

				dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "cached_fw: diag0val=%x count=%d\n",
					ioc->name, diag0val, count));
				/* wait 1 sec */
				if (sleepFlag == CAN_SLEEP) {
					msleep (1000);
				} else {
					mdelay (1000);
				}
			}
			if ((count = mpt_downloadboot(ioc, cached_fw, sleepFlag)) < 0) {
				printk(MYIOC_s_WARN_FMT
					"firmware downloadboot failure (%d)!\n", ioc->name, count);
			}

		} else {
			/* Wait for FW to reload and for board
			 * to go to the READY state.
			 * Maximum wait is 60 seconds.
			 * If fail, no error will check again
			 * with calling program.
			 */
			for (count = 0; count < 60; count ++) {
				doorbell = CHIPREG_READ32(&ioc->chip->Doorbell);
				doorbell &= MPI_IOC_STATE_MASK;

				drsprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				    "looking for READY STATE: doorbell=%x"
				    " count=%d\n", ioc->name, doorbell, count));

				if (doorbell == MPI_IOC_STATE_READY) {
					break;
				}

				/* wait 1 sec */
				if (sleepFlag == CAN_SLEEP) {
					msleep (1000);
				} else {
					mdelay (1000);
				}
			}

			if (doorbell != MPI_IOC_STATE_READY)
				printk(MYIOC_s_ERR_FMT "Failed to come READY "
				    "after reset! IocState=%x", ioc->name,
				    doorbell);
		}
	}

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	if (ioc->debug_level & MPT_DEBUG) {
		if (ioc->alt_ioc)
			diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DbG3: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
	}

	/* Clear RESET_HISTORY bit!  Place board in the
	 * diagnostic mode to update the diag register.
	 */
	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	count = 0;
	while ((diag0val & MPI_DIAG_DRWE) == 0) {
		/* Write magic sequence to WriteSequence register
		 * Loop until in diagnostic mode
		 */
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFF);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);

		/* wait 100 msec */
		if (sleepFlag == CAN_SLEEP) {
			msleep (100);
		} else {
			mdelay (100);
		}

		count++;
		if (count > 20) {
			printk(MYIOC_s_ERR_FMT "Enable Diagnostic mode FAILED! (%02xh)\n",
					ioc->name, diag0val);
			break;
		}
		diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	}
	diag0val &= ~MPI_DIAG_RESET_HISTORY;
	CHIPREG_WRITE32(&ioc->chip->Diagnostic, diag0val);
	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	if (diag0val & MPI_DIAG_RESET_HISTORY) {
		printk(MYIOC_s_WARN_FMT "ResetHistory bit failed to clear!\n",
				ioc->name);
	}

	/* Disable Diagnostic Mode
	 */
	CHIPREG_WRITE32(&ioc->chip->WriteSequence, 0xFFFFFFFF);

	/* Check FW reload status flags.
	 */
	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
	if (diag0val & (MPI_DIAG_FLASH_BAD_SIG | MPI_DIAG_RESET_ADAPTER | MPI_DIAG_DISABLE_ARM)) {
		printk(MYIOC_s_ERR_FMT "Diagnostic reset FAILED! (%02xh)\n",
				ioc->name, diag0val);
		return -3;
	}

	if (ioc->debug_level & MPT_DEBUG) {
		if (ioc->alt_ioc)
			diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DbG4: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
	}

	/*
	 * Reset flag that says we've enabled event notification
	 */
	ioc->facts.EventState = 0;

	if (ioc->alt_ioc)
		ioc->alt_ioc->facts.EventState = 0;

	return hard_reset_done;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendIocReset - Send IOCReset request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reset_type: reset type, expected values are
 *	%MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET or %MPI_FUNCTION_IO_UNIT_RESET
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	Send IOCReset request to the MPT adapter.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendIocReset(MPT_ADAPTER *ioc, u8 reset_type, int sleepFlag)
{
	int r;
	u32 state;
	int cntdn, count;

	drsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending IOC reset(0x%02x)!\n",
			ioc->name, reset_type));
	CHIPREG_WRITE32(&ioc->chip->Doorbell, reset_type<<MPI_DOORBELL_FUNCTION_SHIFT);
	if ((r = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0)
		return r;

	/* FW ACK'd request, wait for READY state
	 */
	count = 0;
	cntdn = ((sleepFlag == CAN_SLEEP) ? HZ : 1000) * 15;	/* 15 seconds */

	while ((state = mpt_GetIocState(ioc, 1)) != MPI_IOC_STATE_READY) {
		cntdn--;
		count++;
		if (!cntdn) {
			if (sleepFlag != CAN_SLEEP)
				count *= 10;

			printk(MYIOC_s_ERR_FMT
			    "Wait IOC_READY state (0x%x) timeout(%d)!\n",
			    ioc->name, state, (int)((count+5)/HZ));
			return -ETIME;
		}

		if (sleepFlag == CAN_SLEEP) {
			msleep(1);
		} else {
			mdelay (1);	/* 1 msec delay */
		}
	}

	/* TODO!
	 *  Cleanup all event stuff for this IOC; re-issue EventNotification
	 *  request if needed.
	 */
	if (ioc->facts.Function)
		ioc->facts.EventState = 0;

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	initChainBuffers - Allocate memory for and initialize chain buffers
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Allocates memory for and initializes chain buffers,
 *	chain buffer control arrays and spinlock.
 */
static int
initChainBuffers(MPT_ADAPTER *ioc)
{
	u8		*mem;
	int		sz, ii, num_chain;
	int 		scale, num_sge, numSGE;

	/* ReqToChain size must equal the req_depth
	 * index = req_idx
	 */
	if (ioc->ReqToChain == NULL) {
		sz = ioc->req_depth * sizeof(int);
		mem = kmalloc(sz, GFP_ATOMIC);
		if (mem == NULL)
			return -1;

		ioc->ReqToChain = (int *) mem;
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ReqToChain alloc  @ %p, sz=%d bytes\n",
			 	ioc->name, mem, sz));
		mem = kmalloc(sz, GFP_ATOMIC);
		if (mem == NULL)
			return -1;

		ioc->RequestNB = (int *) mem;
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RequestNB alloc  @ %p, sz=%d bytes\n",
			 	ioc->name, mem, sz));
	}
	for (ii = 0; ii < ioc->req_depth; ii++) {
		ioc->ReqToChain[ii] = MPT_HOST_NO_CHAIN;
	}

	/* ChainToChain size must equal the total number
	 * of chain buffers to be allocated.
	 * index = chain_idx
	 *
	 * Calculate the number of chain buffers needed(plus 1) per I/O
	 * then multiply the maximum number of simultaneous cmds
	 *
	 * num_sge = num sge in request frame + last chain buffer
	 * scale = num sge per chain buffer if no chain element
	 */
	scale = ioc->req_sz / ioc->SGE_size;
	if (ioc->sg_addr_size == sizeof(u64))
		num_sge =  scale + (ioc->req_sz - 60) / ioc->SGE_size;
	else
		num_sge =  1 + scale + (ioc->req_sz - 64) / ioc->SGE_size;

	if (ioc->sg_addr_size == sizeof(u64)) {
		numSGE = (scale - 1) * (ioc->facts.MaxChainDepth-1) + scale +
			(ioc->req_sz - 60) / ioc->SGE_size;
	} else {
		numSGE = 1 + (scale - 1) * (ioc->facts.MaxChainDepth-1) +
		    scale + (ioc->req_sz - 64) / ioc->SGE_size;
	}
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "num_sge=%d numSGE=%d\n",
		ioc->name, num_sge, numSGE));

	if (ioc->bus_type == FC) {
		if (numSGE > MPT_SCSI_FC_SG_DEPTH)
			numSGE = MPT_SCSI_FC_SG_DEPTH;
	} else {
		if (numSGE > MPT_SCSI_SG_DEPTH)
			numSGE = MPT_SCSI_SG_DEPTH;
	}

	num_chain = 1;
	while (numSGE - num_sge > 0) {
		num_chain++;
		num_sge += (scale - 1);
	}
	num_chain++;

	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Now numSGE=%d num_sge=%d num_chain=%d\n",
		ioc->name, numSGE, num_sge, num_chain));

	if (ioc->bus_type == SPI)
		num_chain *= MPT_SCSI_CAN_QUEUE;
	else if (ioc->bus_type == SAS)
		num_chain *= MPT_SAS_CAN_QUEUE;
	else
		num_chain *= MPT_FC_CAN_QUEUE;

	ioc->num_chain = num_chain;

	sz = num_chain * sizeof(int);
	if (ioc->ChainToChain == NULL) {
		mem = kmalloc(sz, GFP_ATOMIC);
		if (mem == NULL)
			return -1;

		ioc->ChainToChain = (int *) mem;
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ChainToChain alloc @ %p, sz=%d bytes\n",
			 	ioc->name, mem, sz));
	} else {
		mem = (u8 *) ioc->ChainToChain;
	}
	memset(mem, 0xFF, sz);
	return num_chain;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	PrimeIocFifos - Initialize IOC request and reply FIFOs.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	This routine allocates memory for the MPT reply and request frame
 *	pools (if necessary), and primes the IOC reply FIFO with
 *	reply frames.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
PrimeIocFifos(MPT_ADAPTER *ioc)
{
	MPT_FRAME_HDR *mf;
	unsigned long flags;
	dma_addr_t alloc_dma;
	u8 *mem;
	int i, reply_sz, sz, total_size, num_chain;
	u64	dma_mask;

	dma_mask = 0;

	/*  Prime reply FIFO...  */

	if (ioc->reply_frames == NULL) {
		if ( (num_chain = initChainBuffers(ioc)) < 0)
			return -1;
		/*
		 * 1078 errata workaround for the 36GB limitation
		 */
		if (ioc->pcidev->device == MPI_MANUFACTPAGE_DEVID_SAS1078 &&
		    ioc->dma_mask > DMA_BIT_MASK(35)) {
			if (!pci_set_dma_mask(ioc->pcidev, DMA_BIT_MASK(32))
			    && !pci_set_consistent_dma_mask(ioc->pcidev,
			    DMA_BIT_MASK(32))) {
				dma_mask = DMA_BIT_MASK(35);
				d36memprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				    "setting 35 bit addressing for "
				    "Request/Reply/Chain and Sense Buffers\n",
				    ioc->name));
			} else {
				/*Reseting DMA mask to 64 bit*/
				pci_set_dma_mask(ioc->pcidev,
					DMA_BIT_MASK(64));
				pci_set_consistent_dma_mask(ioc->pcidev,
					DMA_BIT_MASK(64));

				printk(MYIOC_s_ERR_FMT
				    "failed setting 35 bit addressing for "
				    "Request/Reply/Chain and Sense Buffers\n",
				    ioc->name);
				return -1;
			}
		}

		total_size = reply_sz = (ioc->reply_sz * ioc->reply_depth);
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ReplyBuffer sz=%d bytes, ReplyDepth=%d\n",
			 	ioc->name, ioc->reply_sz, ioc->reply_depth));
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ReplyBuffer sz=%d[%x] bytes\n",
			 	ioc->name, reply_sz, reply_sz));

		sz = (ioc->req_sz * ioc->req_depth);
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RequestBuffer sz=%d bytes, RequestDepth=%d\n",
			 	ioc->name, ioc->req_sz, ioc->req_depth));
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RequestBuffer sz=%d[%x] bytes\n",
			 	ioc->name, sz, sz));
		total_size += sz;

		sz = num_chain * ioc->req_sz; /* chain buffer pool size */
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ChainBuffer sz=%d bytes, ChainDepth=%d\n",
			 	ioc->name, ioc->req_sz, num_chain));
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ChainBuffer sz=%d[%x] bytes num_chain=%d\n",
			 	ioc->name, sz, sz, num_chain));

		total_size += sz;
		mem = pci_alloc_consistent(ioc->pcidev, total_size, &alloc_dma);
		if (mem == NULL) {
			printk(MYIOC_s_ERR_FMT "Unable to allocate Reply, Request, Chain Buffers!\n",
				ioc->name);
			goto out_fail;
		}

		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Total alloc @ %p[%p], sz=%d[%x] bytes\n",
			 	ioc->name, mem, (void *)(ulong)alloc_dma, total_size, total_size));

		memset(mem, 0, total_size);
		ioc->alloc_total += total_size;
		ioc->alloc = mem;
		ioc->alloc_dma = alloc_dma;
		ioc->alloc_sz = total_size;
		ioc->reply_frames = (MPT_FRAME_HDR *) mem;
		ioc->reply_frames_low_dma = (u32) (alloc_dma & 0xFFFFFFFF);

		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ReplyBuffers @ %p[%p]\n",
	 		ioc->name, ioc->reply_frames, (void *)(ulong)alloc_dma));

		alloc_dma += reply_sz;
		mem += reply_sz;

		/*  Request FIFO - WE manage this!  */

		ioc->req_frames = (MPT_FRAME_HDR *) mem;
		ioc->req_frames_dma = alloc_dma;

		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "RequestBuffers @ %p[%p]\n",
			 	ioc->name, mem, (void *)(ulong)alloc_dma));

		ioc->req_frames_low_dma = (u32) (alloc_dma & 0xFFFFFFFF);

		for (i = 0; i < ioc->req_depth; i++) {
			alloc_dma += ioc->req_sz;
			mem += ioc->req_sz;
		}

		ioc->ChainBuffer = mem;
		ioc->ChainBufferDMA = alloc_dma;

		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ChainBuffers @ %p(%p)\n",
			ioc->name, ioc->ChainBuffer, (void *)(ulong)ioc->ChainBufferDMA));

		/* Initialize the free chain Q.
	 	*/

		INIT_LIST_HEAD(&ioc->FreeChainQ);

		/* Post the chain buffers to the FreeChainQ.
	 	*/
		mem = (u8 *)ioc->ChainBuffer;
		for (i=0; i < num_chain; i++) {
			mf = (MPT_FRAME_HDR *) mem;
			list_add_tail(&mf->u.frame.linkage.list, &ioc->FreeChainQ);
			mem += ioc->req_sz;
		}

		/* Initialize Request frames linked list
		 */
		alloc_dma = ioc->req_frames_dma;
		mem = (u8 *) ioc->req_frames;

		spin_lock_irqsave(&ioc->FreeQlock, flags);
		INIT_LIST_HEAD(&ioc->FreeQ);
		for (i = 0; i < ioc->req_depth; i++) {
			mf = (MPT_FRAME_HDR *) mem;

			/*  Queue REQUESTs *internally*!  */
			list_add_tail(&mf->u.frame.linkage.list, &ioc->FreeQ);

			mem += ioc->req_sz;
		}
		spin_unlock_irqrestore(&ioc->FreeQlock, flags);

		sz = (ioc->req_depth * MPT_SENSE_BUFFER_ALLOC);
		ioc->sense_buf_pool =
			pci_alloc_consistent(ioc->pcidev, sz, &ioc->sense_buf_pool_dma);
		if (ioc->sense_buf_pool == NULL) {
			printk(MYIOC_s_ERR_FMT "Unable to allocate Sense Buffers!\n",
				ioc->name);
			goto out_fail;
		}

		ioc->sense_buf_low_dma = (u32) (ioc->sense_buf_pool_dma & 0xFFFFFFFF);
		ioc->alloc_total += sz;
		dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SenseBuffers @ %p[%p]\n",
 			ioc->name, ioc->sense_buf_pool, (void *)(ulong)ioc->sense_buf_pool_dma));

	}

	/* Post Reply frames to FIFO
	 */
	alloc_dma = ioc->alloc_dma;
	dinitprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ReplyBuffers @ %p[%p]\n",
	 	ioc->name, ioc->reply_frames, (void *)(ulong)alloc_dma));

	for (i = 0; i < ioc->reply_depth; i++) {
		/*  Write each address to the IOC!  */
		CHIPREG_WRITE32(&ioc->chip->ReplyFifo, alloc_dma);
		alloc_dma += ioc->reply_sz;
	}

	if (dma_mask == DMA_BIT_MASK(35) && !pci_set_dma_mask(ioc->pcidev,
	    ioc->dma_mask) && !pci_set_consistent_dma_mask(ioc->pcidev,
	    ioc->dma_mask))
		d36memprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "restoring 64 bit addressing\n", ioc->name));

	return 0;

out_fail:

	if (ioc->alloc != NULL) {
		sz = ioc->alloc_sz;
		pci_free_consistent(ioc->pcidev,
				sz,
				ioc->alloc, ioc->alloc_dma);
		ioc->reply_frames = NULL;
		ioc->req_frames = NULL;
		ioc->alloc_total -= sz;
	}
	if (ioc->sense_buf_pool != NULL) {
		sz = (ioc->req_depth * MPT_SENSE_BUFFER_ALLOC);
		pci_free_consistent(ioc->pcidev,
				sz,
				ioc->sense_buf_pool, ioc->sense_buf_pool_dma);
		ioc->sense_buf_pool = NULL;
	}

	if (dma_mask == DMA_BIT_MASK(35) && !pci_set_dma_mask(ioc->pcidev,
	    DMA_BIT_MASK(64)) && !pci_set_consistent_dma_mask(ioc->pcidev,
	    DMA_BIT_MASK(64)))
		d36memprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "restoring 64 bit addressing\n", ioc->name));

	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_handshake_req_reply_wait - Send MPT request to and receive reply
 *	from IOC via doorbell handshake method.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reqBytes: Size of the request in bytes
 *	@req: Pointer to MPT request frame
 *	@replyBytes: Expected size of the reply in bytes
 *	@u16reply: Pointer to area where reply should be written
 *	@maxwait: Max wait time for a reply (in seconds)
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	NOTES: It is the callers responsibility to byte-swap fields in the
 *	request which are greater than 1 byte in size.  It is also the
 *	callers responsibility to byte-swap response fields which are
 *	greater than 1 byte in size.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
mpt_handshake_req_reply_wait(MPT_ADAPTER *ioc, int reqBytes, u32 *req,
		int replyBytes, u16 *u16reply, int maxwait, int sleepFlag)
{
	MPIDefaultReply_t *mptReply;
	int failcnt = 0;
	int t;

	/*
	 * Get ready to cache a handshake reply
	 */
	ioc->hs_reply_idx = 0;
	mptReply = (MPIDefaultReply_t *) ioc->hs_reply;
	mptReply->MsgLength = 0;

	/*
	 * Make sure there are no doorbells (WRITE 0 to IntStatus reg),
	 * then tell IOC that we want to handshake a request of N words.
	 * (WRITE u32val to Doorbell reg).
	 */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	CHIPREG_WRITE32(&ioc->chip->Doorbell,
			((MPI_FUNCTION_HANDSHAKE<<MPI_DOORBELL_FUNCTION_SHIFT) |
			 ((reqBytes/4)<<MPI_DOORBELL_ADD_DWORDS_SHIFT)));

	/*
	 * Wait for IOC's doorbell handshake int
	 */
	if ((t = WaitForDoorbellInt(ioc, 5, sleepFlag)) < 0)
		failcnt++;

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "HandShake request start reqBytes=%d, WaitCnt=%d%s\n",
			ioc->name, reqBytes, t, failcnt ? " - MISSING DOORBELL HANDSHAKE!" : ""));

	/* Read doorbell and check for active bit */
	if (!(CHIPREG_READ32(&ioc->chip->Doorbell) & MPI_DOORBELL_ACTIVE))
			return -1;

	/*
	 * Clear doorbell int (WRITE 0 to IntStatus reg),
	 * then wait for IOC to ACKnowledge that it's ready for
	 * our handshake request.
	 */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	if (!failcnt && (t = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0)
		failcnt++;

	if (!failcnt) {
		int	 ii;
		u8	*req_as_bytes = (u8 *) req;

		/*
		 * Stuff request words via doorbell handshake,
		 * with ACK from IOC for each.
		 */
		for (ii = 0; !failcnt && ii < reqBytes/4; ii++) {
			u32 word = ((req_as_bytes[(ii*4) + 0] <<  0) |
				    (req_as_bytes[(ii*4) + 1] <<  8) |
				    (req_as_bytes[(ii*4) + 2] << 16) |
				    (req_as_bytes[(ii*4) + 3] << 24));

			CHIPREG_WRITE32(&ioc->chip->Doorbell, word);
			if ((t = WaitForDoorbellAck(ioc, 5, sleepFlag)) < 0)
				failcnt++;
		}

		dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Handshake request frame (@%p) header\n", ioc->name, req));
		DBG_DUMP_REQUEST_FRAME_HDR(ioc, (u32 *)req);

		dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "HandShake request post done, WaitCnt=%d%s\n",
				ioc->name, t, failcnt ? " - MISSING DOORBELL ACK!" : ""));

		/*
		 * Wait for completion of doorbell handshake reply from the IOC
		 */
		if (!failcnt && (t = WaitForDoorbellReply(ioc, maxwait, sleepFlag)) < 0)
			failcnt++;

		dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "HandShake reply count=%d%s\n",
				ioc->name, t, failcnt ? " - MISSING DOORBELL REPLY!" : ""));

		/*
		 * Copy out the cached reply...
		 */
		for (ii=0; ii < min(replyBytes/2,mptReply->MsgLength*2); ii++)
			u16reply[ii] = ioc->hs_reply[ii];
	} else {
		return -99;
	}

	return -failcnt;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	WaitForDoorbellAck - Wait for IOC doorbell handshake acknowledge
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	This routine waits (up to ~2 seconds max) for IOC doorbell
 *	handshake ACKnowledge, indicated by the IOP_DOORBELL_STATUS
 *	bit in its IntStatus register being clear.
 *
 *	Returns a negative value on failure, else wait loop count.
 */
static int
WaitForDoorbellAck(MPT_ADAPTER *ioc, int howlong, int sleepFlag)
{
	int cntdn;
	int count = 0;
	u32 intstat=0;

	cntdn = 1000 * howlong;

	if (sleepFlag == CAN_SLEEP) {
		while (--cntdn) {
			msleep (1);
			intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
			if (! (intstat & MPI_HIS_IOP_DOORBELL_STATUS))
				break;
			count++;
		}
	} else {
		while (--cntdn) {
			udelay (1000);
			intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
			if (! (intstat & MPI_HIS_IOP_DOORBELL_STATUS))
				break;
			count++;
		}
	}

	if (cntdn) {
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "WaitForDoorbell ACK (count=%d)\n",
				ioc->name, count));
		return count;
	}

	printk(MYIOC_s_ERR_FMT "Doorbell ACK timeout (count=%d), IntStatus=%x!\n",
			ioc->name, count, intstat);
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	WaitForDoorbellInt - Wait for IOC to set its doorbell interrupt bit
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	This routine waits (up to ~2 seconds max) for IOC doorbell interrupt
 *	(MPI_HIS_DOORBELL_INTERRUPT) to be set in the IntStatus register.
 *
 *	Returns a negative value on failure, else wait loop count.
 */
static int
WaitForDoorbellInt(MPT_ADAPTER *ioc, int howlong, int sleepFlag)
{
	int cntdn;
	int count = 0;
	u32 intstat=0;

	cntdn = 1000 * howlong;
	if (sleepFlag == CAN_SLEEP) {
		while (--cntdn) {
			intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
			if (intstat & MPI_HIS_DOORBELL_INTERRUPT)
				break;
			msleep(1);
			count++;
		}
	} else {
		while (--cntdn) {
			intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
			if (intstat & MPI_HIS_DOORBELL_INTERRUPT)
				break;
			udelay (1000);
			count++;
		}
	}

	if (cntdn) {
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "WaitForDoorbell INT (cnt=%d) howlong=%d\n",
				ioc->name, count, howlong));
		return count;
	}

	printk(MYIOC_s_ERR_FMT "Doorbell INT timeout (count=%d), IntStatus=%x!\n",
			ioc->name, count, intstat);
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	WaitForDoorbellReply - Wait for and capture an IOC handshake reply.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *	@sleepFlag: Specifies whether the process can sleep
 *
 *	This routine polls the IOC for a handshake reply, 16 bits at a time.
 *	Reply is cached to IOC private area large enough to hold a maximum
 *	of 128 bytes of reply data.
 *
 *	Returns a negative value on failure, else size of reply in WORDS.
 */
static int
WaitForDoorbellReply(MPT_ADAPTER *ioc, int howlong, int sleepFlag)
{
	int u16cnt = 0;
	int failcnt = 0;
	int t;
	u16 *hs_reply = ioc->hs_reply;
	volatile MPIDefaultReply_t *mptReply = (MPIDefaultReply_t *) ioc->hs_reply;
	u16 hword;

	hs_reply[0] = hs_reply[1] = hs_reply[7] = 0;

	/*
	 * Get first two u16's so we can look at IOC's intended reply MsgLength
	 */
	u16cnt=0;
	if ((t = WaitForDoorbellInt(ioc, howlong, sleepFlag)) < 0) {
		failcnt++;
	} else {
		hs_reply[u16cnt++] = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
		CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
		if ((t = WaitForDoorbellInt(ioc, 5, sleepFlag)) < 0)
			failcnt++;
		else {
			hs_reply[u16cnt++] = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
			CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
		}
	}

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "WaitCnt=%d First handshake reply word=%08x%s\n",
			ioc->name, t, le32_to_cpu(*(u32 *)hs_reply),
			failcnt ? " - MISSING DOORBELL HANDSHAKE!" : ""));

	/*
	 * If no error (and IOC said MsgLength is > 0), piece together
	 * reply 16 bits at a time.
	 */
	for (u16cnt=2; !failcnt && u16cnt < (2 * mptReply->MsgLength); u16cnt++) {
		if ((t = WaitForDoorbellInt(ioc, 5, sleepFlag)) < 0)
			failcnt++;
		hword = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
		/* don't overflow our IOC hs_reply[] buffer! */
		if (u16cnt < ARRAY_SIZE(ioc->hs_reply))
			hs_reply[u16cnt] = hword;
		CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	}

	if (!failcnt && (t = WaitForDoorbellInt(ioc, 5, sleepFlag)) < 0)
		failcnt++;
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	if (failcnt) {
		printk(MYIOC_s_ERR_FMT "Handshake reply failure!\n",
				ioc->name);
		return -failcnt;
	}
#if 0
	else if (u16cnt != (2 * mptReply->MsgLength)) {
		return -101;
	}
	else if ((mptReply->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		return -102;
	}
#endif

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Got Handshake reply:\n", ioc->name));
	DBG_DUMP_REPLY_FRAME(ioc, (u32 *)mptReply);

	dhsprintk(ioc, printk(MYIOC_s_DEBUG_FMT "WaitForDoorbell REPLY WaitCnt=%d (sz=%d)\n",
			ioc->name, t, u16cnt/2));
	return u16cnt/2;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	GetLanConfigPages - Fetch LANConfig pages.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Return: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 */
static int
GetLanConfigPages(MPT_ADAPTER *ioc)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	LANPage0_t		*ppage0_alloc;
	dma_addr_t		 page0_dma;
	LANPage1_t		*ppage1_alloc;
	dma_addr_t		 page1_dma;
	int			 rc = 0;
	int			 data_sz;
	int			 copy_sz;

	/* Get LAN Page 0 header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_LAN;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = 0;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength > 0) {
		data_sz = hdr.PageLength * 4;
		ppage0_alloc = (LANPage0_t *) pci_alloc_consistent(ioc->pcidev, data_sz, &page0_dma);
		rc = -ENOMEM;
		if (ppage0_alloc) {
			memset((u8 *)ppage0_alloc, 0, data_sz);
			cfg.physAddr = page0_dma;
			cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

			if ((rc = mpt_config(ioc, &cfg)) == 0) {
				/* save the data */
				copy_sz = min_t(int, sizeof(LANPage0_t), data_sz);
				memcpy(&ioc->lan_cnfg_page0, ppage0_alloc, copy_sz);

			}

			pci_free_consistent(ioc->pcidev, data_sz, (u8 *) ppage0_alloc, page0_dma);

			/* FIXME!
			 *	Normalize endianness of structure data,
			 *	by byte-swapping all > 1 byte fields!
			 */

		}

		if (rc)
			return rc;
	}

	/* Get LAN Page 1 header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 1;
	hdr.PageType = MPI_CONFIG_PAGETYPE_LAN;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength == 0)
		return 0;

	data_sz = hdr.PageLength * 4;
	rc = -ENOMEM;
	ppage1_alloc = (LANPage1_t *) pci_alloc_consistent(ioc->pcidev, data_sz, &page1_dma);
	if (ppage1_alloc) {
		memset((u8 *)ppage1_alloc, 0, data_sz);
		cfg.physAddr = page1_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		if ((rc = mpt_config(ioc, &cfg)) == 0) {
			/* save the data */
			copy_sz = min_t(int, sizeof(LANPage1_t), data_sz);
			memcpy(&ioc->lan_cnfg_page1, ppage1_alloc, copy_sz);
		}

		pci_free_consistent(ioc->pcidev, data_sz, (u8 *) ppage1_alloc, page1_dma);

		/* FIXME!
		 *	Normalize endianness of structure data,
		 *	by byte-swapping all > 1 byte fields!
		 */

	}

	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptbase_sas_persist_operation - Perform operation on SAS Persistent Table
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@persist_opcode: see below
 *
 *	MPI_SAS_OP_CLEAR_NOT_PRESENT - Free all persist TargetID mappings for
 *		devices not currently present.
 *	MPI_SAS_OP_CLEAR_ALL_PERSISTENT - Clear al persist TargetID mappings
 *
 *	NOTE: Don't use not this function during interrupt time.
 *
 *	Returns 0 for success, non-zero error
 */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
int
mptbase_sas_persist_operation(MPT_ADAPTER *ioc, u8 persist_opcode)
{
	SasIoUnitControlRequest_t	*sasIoUnitCntrReq;
	SasIoUnitControlReply_t		*sasIoUnitCntrReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	int				ret = 0;
	unsigned long 	 		timeleft;

	mutex_lock(&ioc->mptbase_cmds.mutex);

	/* init the internal cmd struct */
	memset(ioc->mptbase_cmds.reply, 0 , MPT_DEFAULT_FRAME_SIZE);
	INITIALIZE_MGMT_STATUS(ioc->mptbase_cmds.status)

	/* insure garbage is not sent to fw */
	switch(persist_opcode) {

	case MPI_SAS_OP_CLEAR_NOT_PRESENT:
	case MPI_SAS_OP_CLEAR_ALL_PERSISTENT:
		break;

	default:
		ret = -1;
		goto out;
	}

	printk(KERN_DEBUG  "%s: persist_opcode=%x\n",
		__func__, persist_opcode);

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(mpt_base_index, ioc)) == NULL) {
		printk(KERN_DEBUG "%s: no msg frames!\n", __func__);
		ret = -1;
		goto out;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	sasIoUnitCntrReq = (SasIoUnitControlRequest_t *)mf;
	memset(sasIoUnitCntrReq,0,sizeof(SasIoUnitControlRequest_t));
	sasIoUnitCntrReq->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	sasIoUnitCntrReq->MsgContext = mpi_hdr->MsgContext;
	sasIoUnitCntrReq->Operation = persist_opcode;

	mpt_put_msg_frame(mpt_base_index, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->mptbase_cmds.done, 10*HZ);
	if (!(ioc->mptbase_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		ret = -ETIME;
		printk(KERN_DEBUG "%s: failed\n", __func__);
		if (ioc->mptbase_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		if (!timeleft) {
			printk(MYIOC_s_WARN_FMT
			       "Issuing Reset from %s!!, doorbell=0x%08x\n",
			       ioc->name, __func__, mpt_GetIocState(ioc, 0));
			mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP);
			mpt_free_msg_frame(ioc, mf);
		}
		goto out;
	}

	if (!(ioc->mptbase_cmds.status & MPT_MGMT_STATUS_RF_VALID)) {
		ret = -1;
		goto out;
	}

	sasIoUnitCntrReply =
	    (SasIoUnitControlReply_t *)ioc->mptbase_cmds.reply;
	if (le16_to_cpu(sasIoUnitCntrReply->IOCStatus) != MPI_IOCSTATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: IOCStatus=0x%X IOCLogInfo=0x%X\n",
		    __func__, sasIoUnitCntrReply->IOCStatus,
		    sasIoUnitCntrReply->IOCLogInfo);
		printk(KERN_DEBUG "%s: failed\n", __func__);
		ret = -1;
	} else
		printk(KERN_DEBUG "%s: success\n", __func__);
 out:

	CLEAR_MGMT_STATUS(ioc->mptbase_cmds.status)
	mutex_unlock(&ioc->mptbase_cmds.mutex);
	return ret;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

static void
mptbase_raid_process_event_data(MPT_ADAPTER *ioc,
    MpiEventDataRaid_t * pRaidEventData)
{
	int 	volume;
	int 	reason;
	int 	disk;
	int 	status;
	int 	flags;
	int 	state;

	volume	= pRaidEventData->VolumeID;
	reason	= pRaidEventData->ReasonCode;
	disk	= pRaidEventData->PhysDiskNum;
	status	= le32_to_cpu(pRaidEventData->SettingsStatus);
	flags	= (status >> 0) & 0xff;
	state	= (status >> 8) & 0xff;

	if (reason == MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED) {
		return;
	}

	if ((reason >= MPI_EVENT_RAID_RC_PHYSDISK_CREATED &&
	     reason <= MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED) ||
	    (reason == MPI_EVENT_RAID_RC_SMART_DATA)) {
		printk(MYIOC_s_INFO_FMT "RAID STATUS CHANGE for PhysDisk %d id=%d\n",
			ioc->name, disk, volume);
	} else {
		printk(MYIOC_s_INFO_FMT "RAID STATUS CHANGE for VolumeID %d\n",
			ioc->name, volume);
	}

	switch(reason) {
	case MPI_EVENT_RAID_RC_VOLUME_CREATED:
		printk(MYIOC_s_INFO_FMT "  volume has been created\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_VOLUME_DELETED:

		printk(MYIOC_s_INFO_FMT "  volume has been deleted\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_VOLUME_SETTINGS_CHANGED:
		printk(MYIOC_s_INFO_FMT "  volume settings have been changed\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
		printk(MYIOC_s_INFO_FMT "  volume is now %s%s%s%s\n",
			ioc->name,
			state == MPI_RAIDVOL0_STATUS_STATE_OPTIMAL
			 ? "optimal"
			 : state == MPI_RAIDVOL0_STATUS_STATE_DEGRADED
			  ? "degraded"
			  : state == MPI_RAIDVOL0_STATUS_STATE_FAILED
			   ? "failed"
			   : "state unknown",
			flags & MPI_RAIDVOL0_STATUS_FLAG_ENABLED
			 ? ", enabled" : "",
			flags & MPI_RAIDVOL0_STATUS_FLAG_QUIESCED
			 ? ", quiesced" : "",
			flags & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS
			 ? ", resync in progress" : "" );
		break;

	case MPI_EVENT_RAID_RC_VOLUME_PHYSDISK_CHANGED:
		printk(MYIOC_s_INFO_FMT "  volume membership of PhysDisk %d has changed\n",
			ioc->name, disk);
		break;

	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
		printk(MYIOC_s_INFO_FMT "  PhysDisk has been created\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		printk(MYIOC_s_INFO_FMT "  PhysDisk has been deleted\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_PHYSDISK_SETTINGS_CHANGED:
		printk(MYIOC_s_INFO_FMT "  PhysDisk settings have been changed\n",
			ioc->name);
		break;

	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		printk(MYIOC_s_INFO_FMT "  PhysDisk is now %s%s%s\n",
			ioc->name,
			state == MPI_PHYSDISK0_STATUS_ONLINE
			 ? "online"
			 : state == MPI_PHYSDISK0_STATUS_MISSING
			  ? "missing"
			  : state == MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE
			   ? "not compatible"
			   : state == MPI_PHYSDISK0_STATUS_FAILED
			    ? "failed"
			    : state == MPI_PHYSDISK0_STATUS_INITIALIZING
			     ? "initializing"
			     : state == MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED
			      ? "offline requested"
			      : state == MPI_PHYSDISK0_STATUS_FAILED_REQUESTED
			       ? "failed requested"
			       : state == MPI_PHYSDISK0_STATUS_OTHER_OFFLINE
			        ? "offline"
			        : "state unknown",
			flags & MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC
			 ? ", out of sync" : "",
			flags & MPI_PHYSDISK0_STATUS_FLAG_QUIESCED
			 ? ", quiesced" : "" );
		break;

	case MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED:
		printk(MYIOC_s_INFO_FMT "  Domain Validation needed for PhysDisk %d\n",
			ioc->name, disk);
		break;

	case MPI_EVENT_RAID_RC_SMART_DATA:
		printk(MYIOC_s_INFO_FMT "  SMART data received, ASC/ASCQ = %02xh/%02xh\n",
			ioc->name, pRaidEventData->ASC, pRaidEventData->ASCQ);
		break;

	case MPI_EVENT_RAID_RC_REPLACE_ACTION_STARTED:
		printk(MYIOC_s_INFO_FMT "  replacement of PhysDisk %d has started\n",
			ioc->name, disk);
		break;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	GetIoUnitPage2 - Retrieve BIOS version and boot order information.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Returns: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 */
static int
GetIoUnitPage2(MPT_ADAPTER *ioc)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	IOUnitPage2_t		*ppage_alloc;
	dma_addr_t		 page_dma;
	int			 data_sz;
	int			 rc;

	/* Get the page header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 2;
	hdr.PageType = MPI_CONFIG_PAGETYPE_IO_UNIT;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = 0;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength == 0)
		return 0;

	/* Read the config page */
	data_sz = hdr.PageLength * 4;
	rc = -ENOMEM;
	ppage_alloc = (IOUnitPage2_t *) pci_alloc_consistent(ioc->pcidev, data_sz, &page_dma);
	if (ppage_alloc) {
		memset((u8 *)ppage_alloc, 0, data_sz);
		cfg.physAddr = page_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		/* If Good, save data */
		if ((rc = mpt_config(ioc, &cfg)) == 0)
			ioc->biosVersion = le32_to_cpu(ppage_alloc->BiosVersion);

		pci_free_consistent(ioc->pcidev, data_sz, (u8 *) ppage_alloc, page_dma);
	}

	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_GetScsiPortSettings - read SCSI Port Page 0 and 2
 *	@ioc: Pointer to a Adapter Strucutre
 *	@portnum: IOC port number
 *
 *	Return: -EFAULT if read of config page header fails
 *			or if no nvram
 *	If read of SCSI Port Page 0 fails,
 *		NVRAM = MPT_HOST_NVRAM_INVALID  (0xFFFFFFFF)
 *		Adapter settings: async, narrow
 *		Return 1
 *	If read of SCSI Port Page 2 fails,
 *		Adapter settings valid
 *		NVRAM = MPT_HOST_NVRAM_INVALID  (0xFFFFFFFF)
 *		Return 1
 *	Else
 *		Both valid
 *		Return 0
 *	CHECK - what type of locking mechanisms should be used????
 */
static int
mpt_GetScsiPortSettings(MPT_ADAPTER *ioc, int portnum)
{
	u8			*pbuf;
	dma_addr_t		 buf_dma;
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;
	int			 ii;
	int			 data, rc = 0;

	/* Allocate memory
	 */
	if (!ioc->spi_data.nvram) {
		int	 sz;
		u8	*mem;
		sz = MPT_MAX_SCSI_DEVICES * sizeof(int);
		mem = kmalloc(sz, GFP_ATOMIC);
		if (mem == NULL)
			return -EFAULT;

		ioc->spi_data.nvram = (int *) mem;

		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SCSI device NVRAM settings @ %p, sz=%d\n",
			ioc->name, ioc->spi_data.nvram, sz));
	}

	/* Invalidate NVRAM information
	 */
	for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
		ioc->spi_data.nvram[ii] = MPT_HOST_NVRAM_INVALID;
	}

	/* Read SPP0 header, allocate memory, then read page.
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_SCSI_PORT;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = portnum;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;	/* use default */
	if (mpt_config(ioc, &cfg) != 0)
		 return -EFAULT;

	if (header.PageLength > 0) {
		pbuf = pci_alloc_consistent(ioc->pcidev, header.PageLength * 4, &buf_dma);
		if (pbuf) {
			cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
			cfg.physAddr = buf_dma;
			if (mpt_config(ioc, &cfg) != 0) {
				ioc->spi_data.maxBusWidth = MPT_NARROW;
				ioc->spi_data.maxSyncOffset = 0;
				ioc->spi_data.minSyncFactor = MPT_ASYNC;
				ioc->spi_data.busType = MPT_HOST_BUS_UNKNOWN;
				rc = 1;
				ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"Unable to read PortPage0 minSyncFactor=%x\n",
					ioc->name, ioc->spi_data.minSyncFactor));
			} else {
				/* Save the Port Page 0 data
				 */
				SCSIPortPage0_t  *pPP0 = (SCSIPortPage0_t  *) pbuf;
				pPP0->Capabilities = le32_to_cpu(pPP0->Capabilities);
				pPP0->PhysicalInterface = le32_to_cpu(pPP0->PhysicalInterface);

				if ( (pPP0->Capabilities & MPI_SCSIPORTPAGE0_CAP_QAS) == 0 ) {
					ioc->spi_data.noQas |= MPT_TARGET_NO_NEGO_QAS;
					ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
						"noQas due to Capabilities=%x\n",
						ioc->name, pPP0->Capabilities));
				}
				ioc->spi_data.maxBusWidth = pPP0->Capabilities & MPI_SCSIPORTPAGE0_CAP_WIDE ? 1 : 0;
				data = pPP0->Capabilities & MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_MASK;
				if (data) {
					ioc->spi_data.maxSyncOffset = (u8) (data >> 16);
					data = pPP0->Capabilities & MPI_SCSIPORTPAGE0_CAP_MIN_SYNC_PERIOD_MASK;
					ioc->spi_data.minSyncFactor = (u8) (data >> 8);
					ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
						"PortPage0 minSyncFactor=%x\n",
						ioc->name, ioc->spi_data.minSyncFactor));
				} else {
					ioc->spi_data.maxSyncOffset = 0;
					ioc->spi_data.minSyncFactor = MPT_ASYNC;
				}

				ioc->spi_data.busType = pPP0->PhysicalInterface & MPI_SCSIPORTPAGE0_PHY_SIGNAL_TYPE_MASK;

				/* Update the minSyncFactor based on bus type.
				 */
				if ((ioc->spi_data.busType == MPI_SCSIPORTPAGE0_PHY_SIGNAL_HVD) ||
					(ioc->spi_data.busType == MPI_SCSIPORTPAGE0_PHY_SIGNAL_SE))  {

					if (ioc->spi_data.minSyncFactor < MPT_ULTRA) {
						ioc->spi_data.minSyncFactor = MPT_ULTRA;
						ddvprintk(ioc, printk(MYIOC_s_DEBUG_FMT
							"HVD or SE detected, minSyncFactor=%x\n",
							ioc->name, ioc->spi_data.minSyncFactor));
					}
				}
			}
			if (pbuf) {
				pci_free_consistent(ioc->pcidev, header.PageLength * 4, pbuf, buf_dma);
			}
		}
	}

	/* SCSI Port Page 2 - Read the header then the page.
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 2;
	header.PageType = MPI_CONFIG_PAGETYPE_SCSI_PORT;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = portnum;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	if (mpt_config(ioc, &cfg) != 0)
		return -EFAULT;

	if (header.PageLength > 0) {
		/* Allocate memory and read SCSI Port Page 2
		 */
		pbuf = pci_alloc_consistent(ioc->pcidev, header.PageLength * 4, &buf_dma);
		if (pbuf) {
			cfg.action = MPI_CONFIG_ACTION_PAGE_READ_NVRAM;
			cfg.physAddr = buf_dma;
			if (mpt_config(ioc, &cfg) != 0) {
				/* Nvram data is left with INVALID mark
				 */
				rc = 1;
			} else if (ioc->pcidev->vendor == PCI_VENDOR_ID_ATTO) {

				/* This is an ATTO adapter, read Page2 accordingly
				*/
				ATTO_SCSIPortPage2_t *pPP2 = (ATTO_SCSIPortPage2_t  *) pbuf;
				ATTODeviceInfo_t *pdevice = NULL;
				u16 ATTOFlags;

				/* Save the Port Page 2 data
				 * (reformat into a 32bit quantity)
				 */
				for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
				  pdevice = &pPP2->DeviceSettings[ii];
				  ATTOFlags = le16_to_cpu(pdevice->ATTOFlags);
				  data = 0;

				  /* Translate ATTO device flags to LSI format
				   */
				  if (ATTOFlags & ATTOFLAG_DISC)
				    data |= (MPI_SCSIPORTPAGE2_DEVICE_DISCONNECT_ENABLE);
				  if (ATTOFlags & ATTOFLAG_ID_ENB)
				    data |= (MPI_SCSIPORTPAGE2_DEVICE_ID_SCAN_ENABLE);
				  if (ATTOFlags & ATTOFLAG_LUN_ENB)
				    data |= (MPI_SCSIPORTPAGE2_DEVICE_LUN_SCAN_ENABLE);
				  if (ATTOFlags & ATTOFLAG_TAGGED)
				    data |= (MPI_SCSIPORTPAGE2_DEVICE_TAG_QUEUE_ENABLE);
				  if (!(ATTOFlags & ATTOFLAG_WIDE_ENB))
				    data |= (MPI_SCSIPORTPAGE2_DEVICE_WIDE_DISABLE);

				  data = (data << 16) | (pdevice->Period << 8) | 10;
				  ioc->spi_data.nvram[ii] = data;
				}
			} else {
				SCSIPortPage2_t *pPP2 = (SCSIPortPage2_t  *) pbuf;
				MpiDeviceInfo_t	*pdevice = NULL;

				/*
				 * Save "Set to Avoid SCSI Bus Resets" flag
				 */
				ioc->spi_data.bus_reset =
				    (le32_to_cpu(pPP2->PortFlags) &
			        MPI_SCSIPORTPAGE2_PORT_FLAGS_AVOID_SCSI_RESET) ?
				    0 : 1 ;

				/* Save the Port Page 2 data
				 * (reformat into a 32bit quantity)
				 */
				data = le32_to_cpu(pPP2->PortFlags) & MPI_SCSIPORTPAGE2_PORT_FLAGS_DV_MASK;
				ioc->spi_data.PortFlags = data;
				for (ii=0; ii < MPT_MAX_SCSI_DEVICES; ii++) {
					pdevice = &pPP2->DeviceSettings[ii];
					data = (le16_to_cpu(pdevice->DeviceFlags) << 16) |
						(pdevice->SyncFactor << 8) | pdevice->Timeout;
					ioc->spi_data.nvram[ii] = data;
				}
			}

			pci_free_consistent(ioc->pcidev, header.PageLength * 4, pbuf, buf_dma);
		}
	}

	/* Update Adapter limits with those from NVRAM
	 * Comment: Don't need to do this. Target performance
	 * parameters will never exceed the adapters limits.
	 */

	return rc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_readScsiDevicePageHeaders - save version and length of SDP1
 *	@ioc: Pointer to a Adapter Strucutre
 *	@portnum: IOC port number
 *
 *	Return: -EFAULT if read of config page header fails
 *		or 0 if success.
 */
static int
mpt_readScsiDevicePageHeaders(MPT_ADAPTER *ioc, int portnum)
{
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;

	/* Read the SCSI Device Page 1 header
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 1;
	header.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = portnum;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0)
		 return -EFAULT;

	ioc->spi_data.sdp1version = cfg.cfghdr.hdr->PageVersion;
	ioc->spi_data.sdp1length = cfg.cfghdr.hdr->PageLength;

	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
	if (mpt_config(ioc, &cfg) != 0)
		 return -EFAULT;

	ioc->spi_data.sdp0version = cfg.cfghdr.hdr->PageVersion;
	ioc->spi_data.sdp0length = cfg.cfghdr.hdr->PageLength;

	dcprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Headers: 0: version %d length %d\n",
			ioc->name, ioc->spi_data.sdp0version, ioc->spi_data.sdp0length));

	dcprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Headers: 1: version %d length %d\n",
			ioc->name, ioc->spi_data.sdp1version, ioc->spi_data.sdp1length));
	return 0;
}

/**
 * mpt_inactive_raid_list_free - This clears this link list.
 * @ioc : pointer to per adapter structure
 **/
static void
mpt_inactive_raid_list_free(MPT_ADAPTER *ioc)
{
	struct inactive_raid_component_info *component_info, *pNext;

	if (list_empty(&ioc->raid_data.inactive_list))
		return;

	mutex_lock(&ioc->raid_data.inactive_list_mutex);
	list_for_each_entry_safe(component_info, pNext,
	    &ioc->raid_data.inactive_list, list) {
		list_del(&component_info->list);
		kfree(component_info);
	}
	mutex_unlock(&ioc->raid_data.inactive_list_mutex);
}

/**
 * mpt_inactive_raid_volumes - sets up link list of phy_disk_nums for devices belonging in an inactive volume
 *
 * @ioc : pointer to per adapter structure
 * @channel : volume channel
 * @id : volume target id
 **/
static void
mpt_inactive_raid_volumes(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidVolumePage0_t		buffer = NULL;
	int				i;
	RaidPhysDiskPage0_t 		phys_disk;
	struct inactive_raid_component_info *component_info;
	int				handle_inactive_volumes;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.pageAddr = (channel << 8) + id;
	cfg.cfghdr.hdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!hdr.PageLength)
		goto out;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer)
		goto out;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!buffer->NumPhysDisks)
		goto out;

	handle_inactive_volumes =
	   (buffer->VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE ||
	   (buffer->VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_ENABLED) == 0 ||
	    buffer->VolumeStatus.State == MPI_RAIDVOL0_STATUS_STATE_FAILED ||
	    buffer->VolumeStatus.State == MPI_RAIDVOL0_STATUS_STATE_MISSING) ? 1 : 0;

	if (!handle_inactive_volumes)
		goto out;

	mutex_lock(&ioc->raid_data.inactive_list_mutex);
	for (i = 0; i < buffer->NumPhysDisks; i++) {
		if(mpt_raid_phys_disk_pg0(ioc,
		    buffer->PhysDisk[i].PhysDiskNum, &phys_disk) != 0)
			continue;

		if ((component_info = kmalloc(sizeof (*component_info),
		 GFP_KERNEL)) == NULL)
			continue;

		component_info->volumeID = id;
		component_info->volumeBus = channel;
		component_info->d.PhysDiskNum = phys_disk.PhysDiskNum;
		component_info->d.PhysDiskBus = phys_disk.PhysDiskBus;
		component_info->d.PhysDiskID = phys_disk.PhysDiskID;
		component_info->d.PhysDiskIOC = phys_disk.PhysDiskIOC;

		list_add_tail(&component_info->list,
		    &ioc->raid_data.inactive_list);
	}
	mutex_unlock(&ioc->raid_data.inactive_list_mutex);

 out:
	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);
}

/**
 *	mpt_raid_phys_disk_pg0 - returns phys disk page zero
 *	@ioc: Pointer to a Adapter Structure
 *	@phys_disk_num: io unit unique phys disk num generated by the ioc
 *	@phys_disk: requested payload data returned
 *
 *	Return:
 *	0 on success
 *	-EFAULT if read of config page header fails or data pointer not NULL
 *	-ENOMEM if pci_alloc failed
 **/
int
mpt_raid_phys_disk_pg0(MPT_ADAPTER *ioc, u8 phys_disk_num,
			RaidPhysDiskPage0_t *phys_disk)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidPhysDiskPage0_t		buffer = NULL;
	int				rc;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	memset(phys_disk, 0, sizeof(RaidPhysDiskPage0_t));

	hdr.PageVersion = MPI_RAIDPHYSDISKPAGE0_PAGEVERSION;
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = -EFAULT;
		goto out;
	}

	if (!hdr.PageLength) {
		rc = -EFAULT;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer) {
		rc = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.pageAddr = phys_disk_num;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = -EFAULT;
		goto out;
	}

	rc = 0;
	memcpy(phys_disk, buffer, sizeof(*buffer));
	phys_disk->MaxLBA = le32_to_cpu(buffer->MaxLBA);

 out:

	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);

	return rc;
}

/**
 *	mpt_raid_phys_disk_get_num_paths - returns number paths associated to this phys_num
 *	@ioc: Pointer to a Adapter Structure
 *	@phys_disk_num: io unit unique phys disk num generated by the ioc
 *
 *	Return:
 *	returns number paths
 **/
int
mpt_raid_phys_disk_get_num_paths(MPT_ADAPTER *ioc, u8 phys_disk_num)
{
	CONFIGPARMS		 	cfg;
	ConfigPageHeader_t	 	hdr;
	dma_addr_t			dma_handle;
	pRaidPhysDiskPage1_t		buffer = NULL;
	int				rc;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));

	hdr.PageVersion = MPI_RAIDPHYSDISKPAGE1_PAGEVERSION;
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	hdr.PageNumber = 1;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = 0;
		goto out;
	}

	if (!hdr.PageLength) {
		rc = 0;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer) {
		rc = 0;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.pageAddr = phys_disk_num;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = 0;
		goto out;
	}

	rc = buffer->NumPhysDiskPaths;
 out:

	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);

	return rc;
}
EXPORT_SYMBOL(mpt_raid_phys_disk_get_num_paths);

/**
 *	mpt_raid_phys_disk_pg1 - returns phys disk page 1
 *	@ioc: Pointer to a Adapter Structure
 *	@phys_disk_num: io unit unique phys disk num generated by the ioc
 *	@phys_disk: requested payload data returned
 *
 *	Return:
 *	0 on success
 *	-EFAULT if read of config page header fails or data pointer not NULL
 *	-ENOMEM if pci_alloc failed
 **/
int
mpt_raid_phys_disk_pg1(MPT_ADAPTER *ioc, u8 phys_disk_num,
		RaidPhysDiskPage1_t *phys_disk)
{
	CONFIGPARMS		 	cfg;
	ConfigPageHeader_t	 	hdr;
	dma_addr_t			dma_handle;
	pRaidPhysDiskPage1_t		buffer = NULL;
	int				rc;
	int				i;
	__le64				sas_address;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	rc = 0;

	hdr.PageVersion = MPI_RAIDPHYSDISKPAGE1_PAGEVERSION;
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	hdr.PageNumber = 1;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = -EFAULT;
		goto out;
	}

	if (!hdr.PageLength) {
		rc = -EFAULT;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer) {
		rc = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.pageAddr = phys_disk_num;

	if (mpt_config(ioc, &cfg) != 0) {
		rc = -EFAULT;
		goto out;
	}

	phys_disk->NumPhysDiskPaths = buffer->NumPhysDiskPaths;
	phys_disk->PhysDiskNum = phys_disk_num;
	for (i = 0; i < phys_disk->NumPhysDiskPaths; i++) {
		phys_disk->Path[i].PhysDiskID = buffer->Path[i].PhysDiskID;
		phys_disk->Path[i].PhysDiskBus = buffer->Path[i].PhysDiskBus;
		phys_disk->Path[i].OwnerIdentifier =
				buffer->Path[i].OwnerIdentifier;
		phys_disk->Path[i].Flags = le16_to_cpu(buffer->Path[i].Flags);
		memcpy(&sas_address, &buffer->Path[i].WWID, sizeof(__le64));
		sas_address = le64_to_cpu(sas_address);
		memcpy(&phys_disk->Path[i].WWID, &sas_address, sizeof(__le64));
		memcpy(&sas_address,
				&buffer->Path[i].OwnerWWID, sizeof(__le64));
		sas_address = le64_to_cpu(sas_address);
		memcpy(&phys_disk->Path[i].OwnerWWID,
				&sas_address, sizeof(__le64));
	}

 out:

	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);

	return rc;
}
EXPORT_SYMBOL(mpt_raid_phys_disk_pg1);


/**
 *	mpt_findImVolumes - Identify IDs of hidden disks and RAID Volumes
 *	@ioc: Pointer to a Adapter Strucutre
 *
 *	Return:
 *	0 on success
 *	-EFAULT if read of config page header fails or data pointer not NULL
 *	-ENOMEM if pci_alloc failed
 **/
int
mpt_findImVolumes(MPT_ADAPTER *ioc)
{
	IOCPage2_t		*pIoc2;
	u8			*mem;
	dma_addr_t		 ioc2_dma;
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;
	int			 rc = 0;
	int			 iocpage2sz;
	int			 i;

	if (!ioc->ir_firmware)
		return 0;

	/* Free the old page
	 */
	kfree(ioc->raid_data.pIocPg2);
	ioc->raid_data.pIocPg2 = NULL;
	mpt_inactive_raid_list_free(ioc);

	/* Read IOCP2 header then the page.
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 2;
	header.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0)
		 return -EFAULT;

	if (header.PageLength == 0)
		return -EFAULT;

	iocpage2sz = header.PageLength * 4;
	pIoc2 = pci_alloc_consistent(ioc->pcidev, iocpage2sz, &ioc2_dma);
	if (!pIoc2)
		return -ENOMEM;

	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.physAddr = ioc2_dma;
	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	mem = kmalloc(iocpage2sz, GFP_KERNEL);
	if (!mem) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(mem, (u8 *)pIoc2, iocpage2sz);
	ioc->raid_data.pIocPg2 = (IOCPage2_t *) mem;

	mpt_read_ioc_pg_3(ioc);

	for (i = 0; i < pIoc2->NumActiveVolumes ; i++)
		mpt_inactive_raid_volumes(ioc,
		    pIoc2->RaidVolume[i].VolumeBus,
		    pIoc2->RaidVolume[i].VolumeID);

 out:
	pci_free_consistent(ioc->pcidev, iocpage2sz, pIoc2, ioc2_dma);

	return rc;
}

static int
mpt_read_ioc_pg_3(MPT_ADAPTER *ioc)
{
	IOCPage3_t		*pIoc3;
	u8			*mem;
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;
	dma_addr_t		 ioc3_dma;
	int			 iocpage3sz = 0;

	/* Free the old page
	 */
	kfree(ioc->raid_data.pIocPg3);
	ioc->raid_data.pIocPg3 = NULL;

	/* There is at least one physical disk.
	 * Read and save IOC Page 3
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 3;
	header.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0)
		return 0;

	if (header.PageLength == 0)
		return 0;

	/* Read Header good, alloc memory
	 */
	iocpage3sz = header.PageLength * 4;
	pIoc3 = pci_alloc_consistent(ioc->pcidev, iocpage3sz, &ioc3_dma);
	if (!pIoc3)
		return 0;

	/* Read the Page and save the data
	 * into malloc'd memory.
	 */
	cfg.physAddr = ioc3_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	if (mpt_config(ioc, &cfg) == 0) {
		mem = kmalloc(iocpage3sz, GFP_KERNEL);
		if (mem) {
			memcpy(mem, (u8 *)pIoc3, iocpage3sz);
			ioc->raid_data.pIocPg3 = (IOCPage3_t *) mem;
		}
	}

	pci_free_consistent(ioc->pcidev, iocpage3sz, pIoc3, ioc3_dma);

	return 0;
}

static void
mpt_read_ioc_pg_4(MPT_ADAPTER *ioc)
{
	IOCPage4_t		*pIoc4;
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;
	dma_addr_t		 ioc4_dma;
	int			 iocpage4sz;

	/* Read and save IOC Page 4
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 4;
	header.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0)
		return;

	if (header.PageLength == 0)
		return;

	if ( (pIoc4 = ioc->spi_data.pIocPg4) == NULL ) {
		iocpage4sz = (header.PageLength + 4) * 4; /* Allow 4 additional SEP's */
		pIoc4 = pci_alloc_consistent(ioc->pcidev, iocpage4sz, &ioc4_dma);
		if (!pIoc4)
			return;
		ioc->alloc_total += iocpage4sz;
	} else {
		ioc4_dma = ioc->spi_data.IocPg4_dma;
		iocpage4sz = ioc->spi_data.IocPg4Sz;
	}

	/* Read the Page into dma memory.
	 */
	cfg.physAddr = ioc4_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	if (mpt_config(ioc, &cfg) == 0) {
		ioc->spi_data.pIocPg4 = (IOCPage4_t *) pIoc4;
		ioc->spi_data.IocPg4_dma = ioc4_dma;
		ioc->spi_data.IocPg4Sz = iocpage4sz;
	} else {
		pci_free_consistent(ioc->pcidev, iocpage4sz, pIoc4, ioc4_dma);
		ioc->spi_data.pIocPg4 = NULL;
		ioc->alloc_total -= iocpage4sz;
	}
}

static void
mpt_read_ioc_pg_1(MPT_ADAPTER *ioc)
{
	IOCPage1_t		*pIoc1;
	CONFIGPARMS		 cfg;
	ConfigPageHeader_t	 header;
	dma_addr_t		 ioc1_dma;
	int			 iocpage1sz = 0;
	u32			 tmp;

	/* Check the Coalescing Timeout in IOC Page 1
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 1;
	header.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = 0;
	if (mpt_config(ioc, &cfg) != 0)
		return;

	if (header.PageLength == 0)
		return;

	/* Read Header good, alloc memory
	 */
	iocpage1sz = header.PageLength * 4;
	pIoc1 = pci_alloc_consistent(ioc->pcidev, iocpage1sz, &ioc1_dma);
	if (!pIoc1)
		return;

	/* Read the Page and check coalescing timeout
	 */
	cfg.physAddr = ioc1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	if (mpt_config(ioc, &cfg) == 0) {

		tmp = le32_to_cpu(pIoc1->Flags) & MPI_IOCPAGE1_REPLY_COALESCING;
		if (tmp == MPI_IOCPAGE1_REPLY_COALESCING) {
			tmp = le32_to_cpu(pIoc1->CoalescingTimeout);

			dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Coalescing Enabled Timeout = %d\n",
					ioc->name, tmp));

			if (tmp > MPT_COALESCING_TIMEOUT) {
				pIoc1->CoalescingTimeout = cpu_to_le32(MPT_COALESCING_TIMEOUT);

				/* Write NVRAM and current
				 */
				cfg.dir = 1;
				cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
				if (mpt_config(ioc, &cfg) == 0) {
					dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Reset Current Coalescing Timeout to = %d\n",
							ioc->name, MPT_COALESCING_TIMEOUT));

					cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM;
					if (mpt_config(ioc, &cfg) == 0) {
						dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
								"Reset NVRAM Coalescing Timeout to = %d\n",
								ioc->name, MPT_COALESCING_TIMEOUT));
					} else {
						dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
								"Reset NVRAM Coalescing Timeout Failed\n",
								ioc->name));
					}

				} else {
					dprintk(ioc, printk(MYIOC_s_WARN_FMT
						"Reset of Current Coalescing Timeout Failed!\n",
						ioc->name));
				}
			}

		} else {
			dprintk(ioc, printk(MYIOC_s_WARN_FMT "Coalescing Disabled\n", ioc->name));
		}
	}

	pci_free_consistent(ioc->pcidev, iocpage1sz, pIoc1, ioc1_dma);

	return;
}

static void
mpt_get_manufacturing_pg_0(MPT_ADAPTER *ioc)
{
	CONFIGPARMS		cfg;
	ConfigPageHeader_t	hdr;
	dma_addr_t		buf_dma;
	ManufacturingPage0_t	*pbuf = NULL;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));

	hdr.PageType = MPI_CONFIG_PAGETYPE_MANUFACTURING;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = 10;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!cfg.cfghdr.hdr->PageLength)
		goto out;

	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	pbuf = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4, &buf_dma);
	if (!pbuf)
		goto out;

	cfg.physAddr = buf_dma;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	memcpy(ioc->board_name, pbuf->BoardName, sizeof(ioc->board_name));
	memcpy(ioc->board_assembly, pbuf->BoardAssembly, sizeof(ioc->board_assembly));
	memcpy(ioc->board_tracer, pbuf->BoardTracerNumber, sizeof(ioc->board_tracer));

out:

	if (pbuf)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, pbuf, buf_dma);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendEventNotification - Send EventNotification (on or off) request to adapter
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@EvSwitch: Event switch flags
 *	@sleepFlag: Specifies whether the process can sleep
 */
static int
SendEventNotification(MPT_ADAPTER *ioc, u8 EvSwitch, int sleepFlag)
{
	EventNotification_t	evn;
	MPIDefaultReply_t	reply_buf;

	memset(&evn, 0, sizeof(EventNotification_t));
	memset(&reply_buf, 0, sizeof(MPIDefaultReply_t));

	evn.Function = MPI_FUNCTION_EVENT_NOTIFICATION;
	evn.Switch = EvSwitch;
	evn.MsgContext = cpu_to_le32(mpt_base_index << 16);

	devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Sending EventNotification (%d) request %p\n",
	    ioc->name, EvSwitch, &evn));

	return mpt_handshake_req_reply_wait(ioc, sizeof(EventNotification_t),
	    (u32 *)&evn, sizeof(MPIDefaultReply_t), (u16 *)&reply_buf, 30,
	    sleepFlag);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendEventAck - Send EventAck request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@evnp: Pointer to original EventNotification request
 */
static int
SendEventAck(MPT_ADAPTER *ioc, EventNotificationReply_t *evnp)
{
	EventAck_t	*pAck;

	if ((pAck = (EventAck_t *) mpt_get_msg_frame(mpt_base_index, ioc)) == NULL) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT "%s, no msg frames!!\n",
		    ioc->name, __func__));
		return -1;
	}

	devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Sending EventAck\n", ioc->name));

	pAck->Function     = MPI_FUNCTION_EVENT_ACK;
	pAck->ChainOffset  = 0;
	pAck->Reserved[0]  = pAck->Reserved[1] = 0;
	pAck->MsgFlags     = 0;
	pAck->Reserved1[0] = pAck->Reserved1[1] = pAck->Reserved1[2] = 0;
	pAck->Event        = evnp->Event;
	pAck->EventContext = evnp->EventContext;

	mpt_put_msg_frame(mpt_base_index, ioc, (MPT_FRAME_HDR *)pAck);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_config - Generic function to issue config message
 *	@ioc:   Pointer to an adapter structure
 *	@pCfg:  Pointer to a configuration structure. Struct contains
 *		action, page address, direction, physical address
 *		and pointer to a configuration page header
 *		Page header is updated.
 *
 *	Returns 0 for success
 *	-EPERM if not allowed due to ISR context
 *	-EAGAIN if no msg frames currently available
 *	-EFAULT for non-successful reply or no reply (timeout)
 */
int
mpt_config(MPT_ADAPTER *ioc, CONFIGPARMS *pCfg)
{
	Config_t	*pReq;
	ConfigReply_t	*pReply;
	ConfigExtendedPageHeader_t  *pExtHdr = NULL;
	MPT_FRAME_HDR	*mf;
	int		 ii;
	int		 flagsLength;
	long		 timeout;
	int		 ret;
	u8		 page_type = 0, extend_page;
	unsigned long 	 timeleft;
	unsigned long	 flags;
	int		 in_isr;
	u8		 issue_hard_reset = 0;
	u8		 retry_count = 0;

	/*	Prevent calling wait_event() (below), if caller happens
	 *	to be in ISR context, because that is fatal!
	 */
	in_isr = in_interrupt();
	if (in_isr) {
		dcprintk(ioc, printk(MYIOC_s_WARN_FMT "Config request not allowed in ISR context!\n",
				ioc->name));
		return -EPERM;
    }

	/* don't send a config page during diag reset */
	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress) {
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: busy with host reset\n", ioc->name, __func__));
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	/* don't send if no chance of success */
	if (!ioc->active ||
	    mpt_GetIocState(ioc, 1) != MPI_IOC_STATE_OPERATIONAL) {
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: ioc not operational, %d, %xh\n",
		    ioc->name, __func__, ioc->active,
		    mpt_GetIocState(ioc, 0)));
		return -EFAULT;
	}

 retry_config:
	mutex_lock(&ioc->mptbase_cmds.mutex);
	/* init the internal cmd struct */
	memset(ioc->mptbase_cmds.reply, 0 , MPT_DEFAULT_FRAME_SIZE);
	INITIALIZE_MGMT_STATUS(ioc->mptbase_cmds.status)

	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(mpt_base_index, ioc)) == NULL) {
		dcprintk(ioc, printk(MYIOC_s_WARN_FMT
		"mpt_config: no msg frames!\n", ioc->name));
		ret = -EAGAIN;
		goto out;
	}

	pReq = (Config_t *)mf;
	pReq->Action = pCfg->action;
	pReq->Reserved = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_CONFIG;

	/* Assume page type is not extended and clear "reserved" fields. */
	pReq->ExtPageLength = 0;
	pReq->ExtPageType = 0;
	pReq->MsgFlags = 0;

	for (ii=0; ii < 8; ii++)
		pReq->Reserved2[ii] = 0;

	pReq->Header.PageVersion = pCfg->cfghdr.hdr->PageVersion;
	pReq->Header.PageLength = pCfg->cfghdr.hdr->PageLength;
	pReq->Header.PageNumber = pCfg->cfghdr.hdr->PageNumber;
	pReq->Header.PageType = (pCfg->cfghdr.hdr->PageType & MPI_CONFIG_PAGETYPE_MASK);

	if ((pCfg->cfghdr.hdr->PageType & MPI_CONFIG_PAGETYPE_MASK) == MPI_CONFIG_PAGETYPE_EXTENDED) {
		pExtHdr = (ConfigExtendedPageHeader_t *)pCfg->cfghdr.ehdr;
		pReq->ExtPageLength = cpu_to_le16(pExtHdr->ExtPageLength);
		pReq->ExtPageType = pExtHdr->ExtPageType;
		pReq->Header.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;

		/* Page Length must be treated as a reserved field for the
		 * extended header.
		 */
		pReq->Header.PageLength = 0;
	}

	pReq->PageAddress = cpu_to_le32(pCfg->pageAddr);

	/* Add a SGE to the config request.
	 */
	if (pCfg->dir)
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE;
	else
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ;

	if ((pCfg->cfghdr.hdr->PageType & MPI_CONFIG_PAGETYPE_MASK) ==
	    MPI_CONFIG_PAGETYPE_EXTENDED) {
		flagsLength |= pExtHdr->ExtPageLength * 4;
		page_type = pReq->ExtPageType;
		extend_page = 1;
	} else {
		flagsLength |= pCfg->cfghdr.hdr->PageLength * 4;
		page_type = pReq->Header.PageType;
		extend_page = 0;
	}

	dcprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Sending Config request type 0x%x, page 0x%x and action %d\n",
	    ioc->name, page_type, pReq->Header.PageNumber, pReq->Action));

	ioc->add_sge((char *)&pReq->PageBufferSGE, flagsLength, pCfg->physAddr);
	timeout = (pCfg->timeout < 15) ? HZ*15 : HZ*pCfg->timeout;
	mpt_put_msg_frame(mpt_base_index, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->mptbase_cmds.done,
		timeout);
	if (!(ioc->mptbase_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		ret = -ETIME;
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "Failed Sending Config request type 0x%x, page 0x%x,"
		    " action %d, status %xh, time left %ld\n\n",
			ioc->name, page_type, pReq->Header.PageNumber,
			pReq->Action, ioc->mptbase_cmds.status, timeleft));
		if (ioc->mptbase_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		if (!timeleft) {
			spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
			if (ioc->ioc_reset_in_progress) {
				spin_unlock_irqrestore(&ioc->taskmgmt_lock,
					flags);
				printk(MYIOC_s_INFO_FMT "%s: host reset in"
					" progress mpt_config timed out.!!\n",
					__func__, ioc->name);
				mutex_unlock(&ioc->mptbase_cmds.mutex);
				return -EFAULT;
			}
			spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
			issue_hard_reset = 1;
		}
		goto out;
	}

	if (!(ioc->mptbase_cmds.status & MPT_MGMT_STATUS_RF_VALID)) {
		ret = -1;
		goto out;
	}
	pReply = (ConfigReply_t	*)ioc->mptbase_cmds.reply;
	ret = le16_to_cpu(pReply->IOCStatus) & MPI_IOCSTATUS_MASK;
	if (ret == MPI_IOCSTATUS_SUCCESS) {
		if (extend_page) {
			pCfg->cfghdr.ehdr->ExtPageLength =
			    le16_to_cpu(pReply->ExtPageLength);
			pCfg->cfghdr.ehdr->ExtPageType =
			    pReply->ExtPageType;
		}
		pCfg->cfghdr.hdr->PageVersion = pReply->Header.PageVersion;
		pCfg->cfghdr.hdr->PageLength = pReply->Header.PageLength;
		pCfg->cfghdr.hdr->PageNumber = pReply->Header.PageNumber;
		pCfg->cfghdr.hdr->PageType = pReply->Header.PageType;

	}

	if (retry_count)
		printk(MYIOC_s_INFO_FMT "Retry completed "
		    "ret=0x%x timeleft=%ld\n",
		    ioc->name, ret, timeleft);

	dcprintk(ioc, printk(KERN_DEBUG "IOCStatus=%04xh, IOCLogInfo=%08xh\n",
	     ret, le32_to_cpu(pReply->IOCLogInfo)));

out:

	CLEAR_MGMT_STATUS(ioc->mptbase_cmds.status)
	mutex_unlock(&ioc->mptbase_cmds.mutex);
	if (issue_hard_reset) {
		issue_hard_reset = 0;
		printk(MYIOC_s_WARN_FMT
		       "Issuing Reset from %s!!, doorbell=0x%08x\n",
		       ioc->name, __func__, mpt_GetIocState(ioc, 0));
		if (retry_count == 0) {
			if (mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP) != 0)
				retry_count++;
		} else
			mpt_HardResetHandler(ioc, CAN_SLEEP);

		mpt_free_msg_frame(ioc, mf);
		/* attempt one retry for a timed out command */
		if (retry_count < 2) {
			printk(MYIOC_s_INFO_FMT
			    "Attempting Retry Config request"
			    " type 0x%x, page 0x%x,"
			    " action %d\n", ioc->name, page_type,
			    pCfg->cfghdr.hdr->PageNumber, pCfg->action);
			retry_count++;
			goto retry_config;
		}
	}
	return ret;

}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_ioc_reset - Base cleanup for hard reset
 *	@ioc: Pointer to the adapter structure
 *	@reset_phase: Indicates pre- or post-reset functionality
 *
 *	Remark: Frees resources with internally generated commands.
 */
static int
mpt_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	switch (reset_phase) {
	case MPT_IOC_SETUP_RESET:
		ioc->taskmgmt_quiesce_io = 1;
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_SETUP_RESET\n", ioc->name, __func__));
		break;
	case MPT_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT_IOC_POST_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_POST_RESET\n",  ioc->name, __func__));
/* wake up mptbase_cmds */
		if (ioc->mptbase_cmds.status & MPT_MGMT_STATUS_PENDING) {
			ioc->mptbase_cmds.status |=
			    MPT_MGMT_STATUS_DID_IOCRESET;
			complete(&ioc->mptbase_cmds.done);
		}
/* wake up taskmgmt_cmds */
		if (ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_PENDING) {
			ioc->taskmgmt_cmds.status |=
				MPT_MGMT_STATUS_DID_IOCRESET;
			complete(&ioc->taskmgmt_cmds.done);
		}
		break;
	default:
		break;
	}

	return 1;		/* currently means nothing really */
}


#ifdef CONFIG_PROC_FS		/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	procfs (%MPT_PROCFS_MPTBASEDIR/...) support stuff...
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	procmpt_create - Create %MPT_PROCFS_MPTBASEDIR entries.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
procmpt_create(void)
{
	mpt_proc_root_dir = proc_mkdir(MPT_PROCFS_MPTBASEDIR, NULL);
	if (mpt_proc_root_dir == NULL)
		return -ENOTDIR;

	proc_create_single("summary", S_IRUGO, mpt_proc_root_dir,
			mpt_summary_proc_show);
	proc_create_single("version", S_IRUGO, mpt_proc_root_dir,
			mpt_version_proc_show);
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	procmpt_destroy - Tear down %MPT_PROCFS_MPTBASEDIR entries.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static void
procmpt_destroy(void)
{
	remove_proc_entry("version", mpt_proc_root_dir);
	remove_proc_entry("summary", mpt_proc_root_dir);
	remove_proc_entry(MPT_PROCFS_MPTBASEDIR, NULL);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	Handles read request from /proc/mpt/summary or /proc/mpt/iocN/summary.
 */
static void seq_mpt_print_ioc_summary(MPT_ADAPTER *ioc, struct seq_file *m, int showlan);

static int mpt_summary_proc_show(struct seq_file *m, void *v)
{
	MPT_ADAPTER *ioc = m->private;

	if (ioc) {
		seq_mpt_print_ioc_summary(ioc, m, 1);
	} else {
		list_for_each_entry(ioc, &ioc_list, list) {
			seq_mpt_print_ioc_summary(ioc, m, 1);
		}
	}

	return 0;
}

static int mpt_version_proc_show(struct seq_file *m, void *v)
{
	u8	 cb_idx;
	int	 scsi, fc, sas, lan, ctl, targ, dmp;
	char	*drvname;

	seq_printf(m, "%s-%s\n", "mptlinux", MPT_LINUX_VERSION_COMMON);
	seq_printf(m, "  Fusion MPT base driver\n");

	scsi = fc = sas = lan = ctl = targ = dmp = 0;
	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		drvname = NULL;
		if (MptCallbacks[cb_idx]) {
			switch (MptDriverClass[cb_idx]) {
			case MPTSPI_DRIVER:
				if (!scsi++) drvname = "SPI host";
				break;
			case MPTFC_DRIVER:
				if (!fc++) drvname = "FC host";
				break;
			case MPTSAS_DRIVER:
				if (!sas++) drvname = "SAS host";
				break;
			case MPTLAN_DRIVER:
				if (!lan++) drvname = "LAN";
				break;
			case MPTSTM_DRIVER:
				if (!targ++) drvname = "SCSI target";
				break;
			case MPTCTL_DRIVER:
				if (!ctl++) drvname = "ioctl";
				break;
			}

			if (drvname)
				seq_printf(m, "  Fusion MPT %s driver\n", drvname);
		}
	}

	return 0;
}

static int mpt_iocinfo_proc_show(struct seq_file *m, void *v)
{
	MPT_ADAPTER	*ioc = m->private;
	char		 expVer[32];
	int		 sz;
	int		 p;

	mpt_get_fw_exp_ver(expVer, ioc);

	seq_printf(m, "%s:", ioc->name);
	if (ioc->facts.Flags & MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT)
		seq_printf(m, "  (f/w download boot flag set)");
//	if (ioc->facts.IOCExceptions & MPI_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL)
//		seq_printf(m, "  CONFIG_CHECKSUM_FAIL!");

	seq_printf(m, "\n  ProductID = 0x%04x (%s)\n",
			ioc->facts.ProductID,
			ioc->prod_name);
	seq_printf(m, "  FWVersion = 0x%08x%s", ioc->facts.FWVersion.Word, expVer);
	if (ioc->facts.FWImageSize)
		seq_printf(m, " (fw_size=%d)", ioc->facts.FWImageSize);
	seq_printf(m, "\n  MsgVersion = 0x%04x\n", ioc->facts.MsgVersion);
	seq_printf(m, "  FirstWhoInit = 0x%02x\n", ioc->FirstWhoInit);
	seq_printf(m, "  EventState = 0x%02x\n", ioc->facts.EventState);

	seq_printf(m, "  CurrentHostMfaHighAddr = 0x%08x\n",
			ioc->facts.CurrentHostMfaHighAddr);
	seq_printf(m, "  CurrentSenseBufferHighAddr = 0x%08x\n",
			ioc->facts.CurrentSenseBufferHighAddr);

	seq_printf(m, "  MaxChainDepth = 0x%02x frames\n", ioc->facts.MaxChainDepth);
	seq_printf(m, "  MinBlockSize = 0x%02x bytes\n", 4*ioc->facts.BlockSize);

	seq_printf(m, "  RequestFrames @ 0x%p (Dma @ 0x%p)\n",
					(void *)ioc->req_frames, (void *)(ulong)ioc->req_frames_dma);
	/*
	 *  Rounding UP to nearest 4-kB boundary here...
	 */
	sz = (ioc->req_sz * ioc->req_depth) + 128;
	sz = ((sz + 0x1000UL - 1UL) / 0x1000) * 0x1000;
	seq_printf(m, "    {CurReqSz=%d} x {CurReqDepth=%d} = %d bytes ^= 0x%x\n",
					ioc->req_sz, ioc->req_depth, ioc->req_sz*ioc->req_depth, sz);
	seq_printf(m, "    {MaxReqSz=%d}   {MaxReqDepth=%d}\n",
					4*ioc->facts.RequestFrameSize,
					ioc->facts.GlobalCredits);

	seq_printf(m, "  Frames   @ 0x%p (Dma @ 0x%p)\n",
					(void *)ioc->alloc, (void *)(ulong)ioc->alloc_dma);
	sz = (ioc->reply_sz * ioc->reply_depth) + 128;
	seq_printf(m, "    {CurRepSz=%d} x {CurRepDepth=%d} = %d bytes ^= 0x%x\n",
					ioc->reply_sz, ioc->reply_depth, ioc->reply_sz*ioc->reply_depth, sz);
	seq_printf(m, "    {MaxRepSz=%d}   {MaxRepDepth=%d}\n",
					ioc->facts.CurReplyFrameSize,
					ioc->facts.ReplyQueueDepth);

	seq_printf(m, "  MaxDevices = %d\n",
			(ioc->facts.MaxDevices==0) ? 255 : ioc->facts.MaxDevices);
	seq_printf(m, "  MaxBuses = %d\n", ioc->facts.MaxBuses);

	/* per-port info */
	for (p=0; p < ioc->facts.NumberOfPorts; p++) {
		seq_printf(m, "  PortNumber = %d (of %d)\n",
				p+1,
				ioc->facts.NumberOfPorts);
		if (ioc->bus_type == FC) {
			if (ioc->pfacts[p].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN) {
				u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
				seq_printf(m, "    LanAddr = %pMR\n", a);
			}
			seq_printf(m, "    WWN = %08X%08X:%08X%08X\n",
					ioc->fc_port_page0[p].WWNN.High,
					ioc->fc_port_page0[p].WWNN.Low,
					ioc->fc_port_page0[p].WWPN.High,
					ioc->fc_port_page0[p].WWPN.Low);
		}
	}

	return 0;
}
#endif		/* CONFIG_PROC_FS } */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static void
mpt_get_fw_exp_ver(char *buf, MPT_ADAPTER *ioc)
{
	buf[0] ='\0';
	if ((ioc->facts.FWVersion.Word >> 24) == 0x0E) {
		sprintf(buf, " (Exp %02d%02d)",
			(ioc->facts.FWVersion.Word >> 16) & 0x00FF,	/* Month */
			(ioc->facts.FWVersion.Word >> 8) & 0x1F);	/* Day */

		/* insider hack! */
		if ((ioc->facts.FWVersion.Word >> 8) & 0x80)
			strcat(buf, " [MDBG]");
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_print_ioc_summary - Write ASCII summary of IOC to a buffer.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@buffer: Pointer to buffer where IOC summary info should be written
 *	@size: Pointer to number of bytes we wrote (set by this routine)
 *	@len: Offset at which to start writing in buffer
 *	@showlan: Display LAN stuff?
 *
 *	This routine writes (english readable) ASCII text, which represents
 *	a summary of IOC information, to a buffer.
 */
void
mpt_print_ioc_summary(MPT_ADAPTER *ioc, char *buffer, int *size, int len, int showlan)
{
	char expVer[32];
	int y;

	mpt_get_fw_exp_ver(expVer, ioc);

	/*
	 *  Shorter summary of attached ioc's...
	 */
	y = sprintf(buffer+len, "%s: %s, %s%08xh%s, Ports=%d, MaxQ=%d",
			ioc->name,
			ioc->prod_name,
			MPT_FW_REV_MAGIC_ID_STRING,	/* "FwRev=" or somesuch */
			ioc->facts.FWVersion.Word,
			expVer,
			ioc->facts.NumberOfPorts,
			ioc->req_depth);

	if (showlan && (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN)) {
		u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
		y += sprintf(buffer+len+y, ", LanAddr=%pMR", a);
	}

	y += sprintf(buffer+len+y, ", IRQ=%d", ioc->pci_irq);

	if (!ioc->active)
		y += sprintf(buffer+len+y, " (disabled)");

	y += sprintf(buffer+len+y, "\n");

	*size = y;
}

#ifdef CONFIG_PROC_FS
static void seq_mpt_print_ioc_summary(MPT_ADAPTER *ioc, struct seq_file *m, int showlan)
{
	char expVer[32];

	mpt_get_fw_exp_ver(expVer, ioc);

	/*
	 *  Shorter summary of attached ioc's...
	 */
	seq_printf(m, "%s: %s, %s%08xh%s, Ports=%d, MaxQ=%d",
			ioc->name,
			ioc->prod_name,
			MPT_FW_REV_MAGIC_ID_STRING,	/* "FwRev=" or somesuch */
			ioc->facts.FWVersion.Word,
			expVer,
			ioc->facts.NumberOfPorts,
			ioc->req_depth);

	if (showlan && (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN)) {
		u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
		seq_printf(m, ", LanAddr=%pMR", a);
	}

	seq_printf(m, ", IRQ=%d", ioc->pci_irq);

	if (!ioc->active)
		seq_printf(m, " (disabled)");

	seq_putc(m, '\n');
}
#endif

/**
 *	mpt_set_taskmgmt_in_progress_flag - set flags associated with task management
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Returns 0 for SUCCESS or -1 if FAILED.
 *
 *	If -1 is return, then it was not possible to set the flags
 **/
int
mpt_set_taskmgmt_in_progress_flag(MPT_ADAPTER *ioc)
{
	unsigned long	 flags;
	int		 retval;

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress || ioc->taskmgmt_in_progress ||
	    (ioc->alt_ioc && ioc->alt_ioc->taskmgmt_in_progress)) {
		retval = -1;
		goto out;
	}
	retval = 0;
	ioc->taskmgmt_in_progress = 1;
	ioc->taskmgmt_quiesce_io = 1;
	if (ioc->alt_ioc) {
		ioc->alt_ioc->taskmgmt_in_progress = 1;
		ioc->alt_ioc->taskmgmt_quiesce_io = 1;
	}
 out:
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
	return retval;
}
EXPORT_SYMBOL(mpt_set_taskmgmt_in_progress_flag);

/**
 *	mpt_clear_taskmgmt_in_progress_flag - clear flags associated with task management
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
void
mpt_clear_taskmgmt_in_progress_flag(MPT_ADAPTER *ioc)
{
	unsigned long	 flags;

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	ioc->taskmgmt_in_progress = 0;
	ioc->taskmgmt_quiesce_io = 0;
	if (ioc->alt_ioc) {
		ioc->alt_ioc->taskmgmt_in_progress = 0;
		ioc->alt_ioc->taskmgmt_quiesce_io = 0;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
}
EXPORT_SYMBOL(mpt_clear_taskmgmt_in_progress_flag);


/**
 *	mpt_halt_firmware - Halts the firmware if it is operational and panic
 *	the kernel
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
void
mpt_halt_firmware(MPT_ADAPTER *ioc)
{
	u32	 ioc_raw_state;

	ioc_raw_state = mpt_GetIocState(ioc, 0);

	if ((ioc_raw_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_FAULT) {
		printk(MYIOC_s_ERR_FMT "IOC is in FAULT state (%04xh)!!!\n",
			ioc->name, ioc_raw_state & MPI_DOORBELL_DATA_MASK);
		panic("%s: IOC Fault (%04xh)!!!\n", ioc->name,
			ioc_raw_state & MPI_DOORBELL_DATA_MASK);
	} else {
		CHIPREG_WRITE32(&ioc->chip->Doorbell, 0xC0FFEE00);
		panic("%s: Firmware is halted due to command timeout\n",
			ioc->name);
	}
}
EXPORT_SYMBOL(mpt_halt_firmware);

/**
 *	mpt_SoftResetHandler - Issues a less expensive reset
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Indicates if sleep or schedule must be called.
 *
 *	Returns 0 for SUCCESS or -1 if FAILED.
 *
 *	Message Unit Reset - instructs the IOC to reset the Reply Post and
 *	Free FIFO's. All the Message Frames on Reply Free FIFO are discarded.
 *	All posted buffers are freed, and event notification is turned off.
 *	IOC doesn't reply to any outstanding request. This will transfer IOC
 *	to READY state.
 **/
static int
mpt_SoftResetHandler(MPT_ADAPTER *ioc, int sleepFlag)
{
	int		 rc;
	int		 ii;
	u8		 cb_idx;
	unsigned long	 flags;
	u32		 ioc_state;
	unsigned long	 time_count;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SoftResetHandler Entered!\n",
		ioc->name));

	ioc_state = mpt_GetIocState(ioc, 0) & MPI_IOC_STATE_MASK;

	if (mpt_fwfault_debug)
		mpt_halt_firmware(ioc);

	if (ioc_state == MPI_IOC_STATE_FAULT ||
	    ioc_state == MPI_IOC_STATE_RESET) {
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "skipping, either in FAULT or RESET state!\n", ioc->name));
		return -1;
	}

	if (ioc->bus_type == FC) {
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "skipping, because the bus type is FC!\n", ioc->name));
		return -1;
	}

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress) {
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		return -1;
	}
	ioc->ioc_reset_in_progress = 1;
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	rc = -1;

	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptResetHandlers[cb_idx])
			mpt_signal_reset(cb_idx, ioc, MPT_IOC_SETUP_RESET);
	}

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->taskmgmt_in_progress) {
		ioc->ioc_reset_in_progress = 0;
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		return -1;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
	/* Disable reply interrupts (also blocks FreeQ) */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;
	time_count = jiffies;

	rc = SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET, sleepFlag);

	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptResetHandlers[cb_idx])
			mpt_signal_reset(cb_idx, ioc, MPT_IOC_PRE_RESET);
	}

	if (rc)
		goto out;

	ioc_state = mpt_GetIocState(ioc, 0) & MPI_IOC_STATE_MASK;
	if (ioc_state != MPI_IOC_STATE_READY)
		goto out;

	for (ii = 0; ii < 5; ii++) {
		/* Get IOC facts! Allow 5 retries */
		rc = GetIocFacts(ioc, sleepFlag,
			MPT_HOSTEVENT_IOC_RECOVER);
		if (rc == 0)
			break;
		if (sleepFlag == CAN_SLEEP)
			msleep(100);
		else
			mdelay(100);
	}
	if (ii == 5)
		goto out;

	rc = PrimeIocFifos(ioc);
	if (rc != 0)
		goto out;

	rc = SendIocInit(ioc, sleepFlag);
	if (rc != 0)
		goto out;

	rc = SendEventNotification(ioc, 1, sleepFlag);
	if (rc != 0)
		goto out;

	if (ioc->hard_resets < -1)
		ioc->hard_resets++;

	/*
	 * At this point, we know soft reset succeeded.
	 */

	ioc->active = 1;
	CHIPREG_WRITE32(&ioc->chip->IntMask, MPI_HIM_DIM);

 out:
	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	ioc->ioc_reset_in_progress = 0;
	ioc->taskmgmt_quiesce_io = 0;
	ioc->taskmgmt_in_progress = 0;
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	if (ioc->active) {	/* otherwise, hard reset coming */
		for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
			if (MptResetHandlers[cb_idx])
				mpt_signal_reset(cb_idx, ioc,
					MPT_IOC_POST_RESET);
		}
	}

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		"SoftResetHandler: completed (%d seconds): %s\n",
		ioc->name, jiffies_to_msecs(jiffies - time_count)/1000,
		((rc == 0) ? "SUCCESS" : "FAILED")));

	return rc;
}

/**
 *	mpt_Soft_Hard_ResetHandler - Try less expensive reset
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Indicates if sleep or schedule must be called.
 *
 *	Returns 0 for SUCCESS or -1 if FAILED.
 *	Try for softreset first, only if it fails go for expensive
 *	HardReset.
 **/
int
mpt_Soft_Hard_ResetHandler(MPT_ADAPTER *ioc, int sleepFlag) {
	int ret = -1;

	ret = mpt_SoftResetHandler(ioc, sleepFlag);
	if (ret == 0)
		return ret;
	ret = mpt_HardResetHandler(ioc, sleepFlag);
	return ret;
}
EXPORT_SYMBOL(mpt_Soft_Hard_ResetHandler);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	Reset Handling
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_HardResetHandler - Generic reset handler
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sleepFlag: Indicates if sleep or schedule must be called.
 *
 *	Issues SCSI Task Management call based on input arg values.
 *	If TaskMgmt fails, returns associated SCSI request.
 *
 *	Remark: _HardResetHandler can be invoked from an interrupt thread (timer)
 *	or a non-interrupt thread.  In the former, must not call schedule().
 *
 *	Note: A return of -1 is a FATAL error case, as it means a
 *	FW reload/initialization failed.
 *
 *	Returns 0 for SUCCESS or -1 if FAILED.
 */
int
mpt_HardResetHandler(MPT_ADAPTER *ioc, int sleepFlag)
{
	int	 rc;
	u8	 cb_idx;
	unsigned long	 flags;
	unsigned long	 time_count;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "HardResetHandler Entered!\n", ioc->name));
#ifdef MFCNT
	printk(MYIOC_s_INFO_FMT "HardResetHandler Entered!\n", ioc->name);
	printk("MF count 0x%x !\n", ioc->mfcnt);
#endif
	if (mpt_fwfault_debug)
		mpt_halt_firmware(ioc);

	/* Reset the adapter. Prevent more than 1 call to
	 * mpt_do_ioc_recovery at any instant in time.
	 */
	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress) {
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		ioc->wait_on_reset_completion = 1;
		do {
			ssleep(1);
		} while (ioc->ioc_reset_in_progress == 1);
		ioc->wait_on_reset_completion = 0;
		return ioc->reset_status;
	}
	if (ioc->wait_on_reset_completion) {
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		rc = 0;
		time_count = jiffies;
		goto exit;
	}
	ioc->ioc_reset_in_progress = 1;
	if (ioc->alt_ioc)
		ioc->alt_ioc->ioc_reset_in_progress = 1;
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);


	/* The SCSI driver needs to adjust timeouts on all current
	 * commands prior to the diagnostic reset being issued.
	 * Prevents timeouts occurring during a diagnostic reset...very bad.
	 * For all other protocol drivers, this is a no-op.
	 */
	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptResetHandlers[cb_idx]) {
			mpt_signal_reset(cb_idx, ioc, MPT_IOC_SETUP_RESET);
			if (ioc->alt_ioc)
				mpt_signal_reset(cb_idx, ioc->alt_ioc,
					MPT_IOC_SETUP_RESET);
		}
	}

	time_count = jiffies;
	rc = mpt_do_ioc_recovery(ioc, MPT_HOSTEVENT_IOC_RECOVER, sleepFlag);
	if (rc != 0) {
		printk(KERN_WARNING MYNAM
		       ": WARNING - (%d) Cannot recover %s, doorbell=0x%08x\n",
		       rc, ioc->name, mpt_GetIocState(ioc, 0));
	} else {
		if (ioc->hard_resets < -1)
			ioc->hard_resets++;
	}

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	ioc->ioc_reset_in_progress = 0;
	ioc->taskmgmt_quiesce_io = 0;
	ioc->taskmgmt_in_progress = 0;
	ioc->reset_status = rc;
	if (ioc->alt_ioc) {
		ioc->alt_ioc->ioc_reset_in_progress = 0;
		ioc->alt_ioc->taskmgmt_quiesce_io = 0;
		ioc->alt_ioc->taskmgmt_in_progress = 0;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptResetHandlers[cb_idx]) {
			mpt_signal_reset(cb_idx, ioc, MPT_IOC_POST_RESET);
			if (ioc->alt_ioc)
				mpt_signal_reset(cb_idx,
					ioc->alt_ioc, MPT_IOC_POST_RESET);
		}
	}
exit:
	dtmprintk(ioc,
	    printk(MYIOC_s_DEBUG_FMT
		"HardResetHandler: completed (%d seconds): %s\n", ioc->name,
		jiffies_to_msecs(jiffies - time_count)/1000, ((rc == 0) ?
		"SUCCESS" : "FAILED")));

	return rc;
}

#ifdef CONFIG_FUSION_LOGGING
static void
mpt_display_event_info(MPT_ADAPTER *ioc, EventNotificationReply_t *pEventReply)
{
	char *ds = NULL;
	u32 evData0;
	int ii;
	u8 event;
	char *evStr = ioc->evStr;

	event = le32_to_cpu(pEventReply->Event) & 0xFF;
	evData0 = le32_to_cpu(pEventReply->Data[0]);

	switch(event) {
	case MPI_EVENT_NONE:
		ds = "None";
		break;
	case MPI_EVENT_LOG_DATA:
		ds = "Log Data";
		break;
	case MPI_EVENT_STATE_CHANGE:
		ds = "State Change";
		break;
	case MPI_EVENT_UNIT_ATTENTION:
		ds = "Unit Attention";
		break;
	case MPI_EVENT_IOC_BUS_RESET:
		ds = "IOC Bus Reset";
		break;
	case MPI_EVENT_EXT_BUS_RESET:
		ds = "External Bus Reset";
		break;
	case MPI_EVENT_RESCAN:
		ds = "Bus Rescan Event";
		break;
	case MPI_EVENT_LINK_STATUS_CHANGE:
		if (evData0 == MPI_EVENT_LINK_STATUS_FAILURE)
			ds = "Link Status(FAILURE) Change";
		else
			ds = "Link Status(ACTIVE) Change";
		break;
	case MPI_EVENT_LOOP_STATE_CHANGE:
		if (evData0 == MPI_EVENT_LOOP_STATE_CHANGE_LIP)
			ds = "Loop State(LIP) Change";
		else if (evData0 == MPI_EVENT_LOOP_STATE_CHANGE_LPE)
			ds = "Loop State(LPE) Change";
		else
			ds = "Loop State(LPB) Change";
		break;
	case MPI_EVENT_LOGOUT:
		ds = "Logout";
		break;
	case MPI_EVENT_EVENT_CHANGE:
		if (evData0)
			ds = "Events ON";
		else
			ds = "Events OFF";
		break;
	case MPI_EVENT_INTEGRATED_RAID:
	{
		u8 ReasonCode = (u8)(evData0 >> 16);
		switch (ReasonCode) {
		case MPI_EVENT_RAID_RC_VOLUME_CREATED :
			ds = "Integrated Raid: Volume Created";
			break;
		case MPI_EVENT_RAID_RC_VOLUME_DELETED :
			ds = "Integrated Raid: Volume Deleted";
			break;
		case MPI_EVENT_RAID_RC_VOLUME_SETTINGS_CHANGED :
			ds = "Integrated Raid: Volume Settings Changed";
			break;
		case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED :
			ds = "Integrated Raid: Volume Status Changed";
			break;
		case MPI_EVENT_RAID_RC_VOLUME_PHYSDISK_CHANGED :
			ds = "Integrated Raid: Volume Physdisk Changed";
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_CREATED :
			ds = "Integrated Raid: Physdisk Created";
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_DELETED :
			ds = "Integrated Raid: Physdisk Deleted";
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_SETTINGS_CHANGED :
			ds = "Integrated Raid: Physdisk Settings Changed";
			break;
		case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED :
			ds = "Integrated Raid: Physdisk Status Changed";
			break;
		case MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED :
			ds = "Integrated Raid: Domain Validation Needed";
			break;
		case MPI_EVENT_RAID_RC_SMART_DATA :
			ds = "Integrated Raid; Smart Data";
			break;
		case MPI_EVENT_RAID_RC_REPLACE_ACTION_STARTED :
			ds = "Integrated Raid: Replace Action Started";
			break;
		default:
			ds = "Integrated Raid";
		break;
		}
		break;
	}
	case MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE:
		ds = "SCSI Device Status Change";
		break;
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
	{
		u8 id = (u8)(evData0);
		u8 channel = (u8)(evData0 >> 8);
		u8 ReasonCode = (u8)(evData0 >> 16);
		switch (ReasonCode) {
		case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Added: "
			    "id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Deleted: "
			    "id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: SMART Data: "
			    "id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: No Persistency: "
			    "id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Unsupported Device "
			    "Discovered : id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Internal Device "
			    "Reset : id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Internal Task "
			    "Abort : id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Internal Abort "
			    "Task Set : id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Internal Clear "
			    "Task Set : id=%d channel=%d", id, channel);
			break;
		case MPI_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Internal Query "
			    "Task : id=%d channel=%d", id, channel);
			break;
		default:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS Device Status Change: Unknown: "
			    "id=%d channel=%d", id, channel);
			break;
		}
		break;
	}
	case MPI_EVENT_ON_BUS_TIMER_EXPIRED:
		ds = "Bus Timer Expired";
		break;
	case MPI_EVENT_QUEUE_FULL:
	{
		u16 curr_depth = (u16)(evData0 >> 16);
		u8 channel = (u8)(evData0 >> 8);
		u8 id = (u8)(evData0);

		snprintf(evStr, EVENT_DESCR_STR_SZ,
		   "Queue Full: channel=%d id=%d depth=%d",
		   channel, id, curr_depth);
		break;
	}
	case MPI_EVENT_SAS_SES:
		ds = "SAS SES Event";
		break;
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
		ds = "Persistent Table Full";
		break;
	case MPI_EVENT_SAS_PHY_LINK_STATUS:
	{
		u8 LinkRates = (u8)(evData0 >> 8);
		u8 PhyNumber = (u8)(evData0);
		LinkRates = (LinkRates & MPI_EVENT_SAS_PLS_LR_CURRENT_MASK) >>
			MPI_EVENT_SAS_PLS_LR_CURRENT_SHIFT;
		switch (LinkRates) {
		case MPI_EVENT_SAS_PLS_LR_RATE_UNKNOWN:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Rate Unknown",PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_PHY_DISABLED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Phy Disabled",PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_FAILED_SPEED_NEGOTIATION:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Failed Speed Nego",PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_SATA_OOB_COMPLETE:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Sata OOB Completed",PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_1_5:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Rate 1.5 Gbps",PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_3_0:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Rate 3.0 Gbps", PhyNumber);
			break;
		case MPI_EVENT_SAS_PLS_LR_RATE_6_0:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d:"
			   " Rate 6.0 Gbps", PhyNumber);
			break;
		default:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			   "SAS PHY Link Status: Phy=%d", PhyNumber);
			break;
		}
		break;
	}
	case MPI_EVENT_SAS_DISCOVERY_ERROR:
		ds = "SAS Discovery Error";
		break;
	case MPI_EVENT_IR_RESYNC_UPDATE:
	{
		u8 resync_complete = (u8)(evData0 >> 16);
		snprintf(evStr, EVENT_DESCR_STR_SZ,
		    "IR Resync Update: Complete = %d:",resync_complete);
		break;
	}
	case MPI_EVENT_IR2:
	{
		u8 id = (u8)(evData0);
		u8 channel = (u8)(evData0 >> 8);
		u8 phys_num = (u8)(evData0 >> 24);
		u8 ReasonCode = (u8)(evData0 >> 16);

		switch (ReasonCode) {
		case MPI_EVENT_IR2_RC_LD_STATE_CHANGED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: LD State Changed: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_PD_STATE_CHANGED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: PD State Changed "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_BAD_BLOCK_TABLE_FULL:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: Bad Block Table Full: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_PD_INSERTED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: PD Inserted: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_PD_REMOVED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: PD Removed: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_FOREIGN_CFG_DETECTED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: Foreign CFG Detected: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_REBUILD_MEDIUM_ERROR:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: Rebuild Medium Error: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_DUAL_PORT_ADDED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: Dual Port Added: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		case MPI_EVENT_IR2_RC_DUAL_PORT_REMOVED:
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "IR2: Dual Port Removed: "
			    "id=%d channel=%d phys_num=%d",
			    id, channel, phys_num);
			break;
		default:
			ds = "IR2";
		break;
		}
		break;
	}
	case MPI_EVENT_SAS_DISCOVERY:
	{
		if (evData0)
			ds = "SAS Discovery: Start";
		else
			ds = "SAS Discovery: Stop";
		break;
	}
	case MPI_EVENT_LOG_ENTRY_ADDED:
		ds = "SAS Log Entry Added";
		break;

	case MPI_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		u8 phy_num = (u8)(evData0);
		u8 port_num = (u8)(evData0 >> 8);
		u8 port_width = (u8)(evData0 >> 16);
		u8 primative = (u8)(evData0 >> 24);
		snprintf(evStr, EVENT_DESCR_STR_SZ,
		    "SAS Broadcase Primative: phy=%d port=%d "
		    "width=%d primative=0x%02x",
		    phy_num, port_num, port_width, primative);
		break;
	}

	case MPI_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
	{
		u8 reason = (u8)(evData0);

		switch (reason) {
		case MPI_EVENT_SAS_INIT_RC_ADDED:
			ds = "SAS Initiator Status Change: Added";
			break;
		case MPI_EVENT_SAS_INIT_RC_REMOVED:
			ds = "SAS Initiator Status Change: Deleted";
			break;
		default:
			ds = "SAS Initiator Status Change";
			break;
		}
		break;
	}

	case MPI_EVENT_SAS_INIT_TABLE_OVERFLOW:
	{
		u8 max_init = (u8)(evData0);
		u8 current_init = (u8)(evData0 >> 8);

		snprintf(evStr, EVENT_DESCR_STR_SZ,
		    "SAS Initiator Device Table Overflow: max initiators=%02d "
		    "current initiators=%02d",
		    max_init, current_init);
		break;
	}
	case MPI_EVENT_SAS_SMP_ERROR:
	{
		u8 status = (u8)(evData0);
		u8 port_num = (u8)(evData0 >> 8);
		u8 result = (u8)(evData0 >> 16);

		if (status == MPI_EVENT_SAS_SMP_FUNCTION_RESULT_VALID)
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d result=0x%02x",
			    port_num, result);
		else if (status == MPI_EVENT_SAS_SMP_CRC_ERROR)
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d : CRC Error",
			    port_num);
		else if (status == MPI_EVENT_SAS_SMP_TIMEOUT)
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d : Timeout",
			    port_num);
		else if (status == MPI_EVENT_SAS_SMP_NO_DESTINATION)
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d : No Destination",
			    port_num);
		else if (status == MPI_EVENT_SAS_SMP_BAD_DESTINATION)
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d : Bad Destination",
			    port_num);
		else
			snprintf(evStr, EVENT_DESCR_STR_SZ,
			    "SAS SMP Error: port=%d : status=0x%02x",
			    port_num, status);
		break;
	}

	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
	{
		u8 reason = (u8)(evData0);

		switch (reason) {
		case MPI_EVENT_SAS_EXP_RC_ADDED:
			ds = "Expander Status Change: Added";
			break;
		case MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING:
			ds = "Expander Status Change: Deleted";
			break;
		default:
			ds = "Expander Status Change";
			break;
		}
		break;
	}

	/*
	 *  MPT base "custom" events may be added here...
	 */
	default:
		ds = "Unknown";
		break;
	}
	if (ds)
		strlcpy(evStr, ds, EVENT_DESCR_STR_SZ);


	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "MPT event:(%02Xh) : %s\n",
	    ioc->name, event, evStr));

	devtverboseprintk(ioc, printk(KERN_DEBUG MYNAM
	    ": Event data:\n"));
	for (ii = 0; ii < le16_to_cpu(pEventReply->EventDataLength); ii++)
		devtverboseprintk(ioc, printk(" %08x",
		    le32_to_cpu(pEventReply->Data[ii])));
	devtverboseprintk(ioc, printk(KERN_DEBUG "\n"));
}
#endif
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	ProcessEventNotification - Route EventNotificationReply to all event handlers
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@pEventReply: Pointer to EventNotification reply frame
 *	@evHandlers: Pointer to integer, number of event handlers
 *
 *	Routes a received EventNotificationReply to all currently registered
 *	event handlers.
 *	Returns sum of event handlers return values.
 */
static int
ProcessEventNotification(MPT_ADAPTER *ioc, EventNotificationReply_t *pEventReply, int *evHandlers)
{
	u16 evDataLen;
	u32 evData0 = 0;
	int ii;
	u8 cb_idx;
	int r = 0;
	int handlers = 0;
	u8 event;

	/*
	 *  Do platform normalization of values
	 */
	event = le32_to_cpu(pEventReply->Event) & 0xFF;
	evDataLen = le16_to_cpu(pEventReply->EventDataLength);
	if (evDataLen) {
		evData0 = le32_to_cpu(pEventReply->Data[0]);
	}

#ifdef CONFIG_FUSION_LOGGING
	if (evDataLen)
		mpt_display_event_info(ioc, pEventReply);
#endif

	/*
	 *  Do general / base driver event processing
	 */
	switch(event) {
	case MPI_EVENT_EVENT_CHANGE:		/* 0A */
		if (evDataLen) {
			u8 evState = evData0 & 0xFF;

			/* CHECKME! What if evState unexpectedly says OFF (0)? */

			/* Update EventState field in cached IocFacts */
			if (ioc->facts.Function) {
				ioc->facts.EventState = evState;
			}
		}
		break;
	case MPI_EVENT_INTEGRATED_RAID:
		mptbase_raid_process_event_data(ioc,
		    (MpiEventDataRaid_t *)pEventReply->Data);
		break;
	default:
		break;
	}

	/*
	 * Should this event be logged? Events are written sequentially.
	 * When buffer is full, start again at the top.
	 */
	if (ioc->events && (ioc->eventTypes & ( 1 << event))) {
		int idx;

		idx = ioc->eventContext % MPTCTL_EVENT_LOG_SIZE;

		ioc->events[idx].event = event;
		ioc->events[idx].eventContext = ioc->eventContext;

		for (ii = 0; ii < 2; ii++) {
			if (ii < evDataLen)
				ioc->events[idx].data[ii] = le32_to_cpu(pEventReply->Data[ii]);
			else
				ioc->events[idx].data[ii] =  0;
		}

		ioc->eventContext++;
	}


	/*
	 *  Call each currently registered protocol event handler.
	 */
	for (cb_idx = MPT_MAX_PROTOCOL_DRIVERS-1; cb_idx; cb_idx--) {
		if (MptEvHandlers[cb_idx]) {
			devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "Routing Event to event handler #%d\n",
			    ioc->name, cb_idx));
			r += (*(MptEvHandlers[cb_idx]))(ioc, pEventReply);
			handlers++;
		}
	}
	/* FIXME?  Examine results here? */

	/*
	 *  If needed, send (a single) EventAck.
	 */
	if (pEventReply->AckRequired == MPI_EVENT_NOTIFICATION_ACK_REQUIRED) {
		devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"EventAck required\n",ioc->name));
		if ((ii = SendEventAck(ioc, pEventReply)) != 0) {
			devtverboseprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SendEventAck returned %d\n",
					ioc->name, ii));
		}
	}

	*evHandlers = handlers;
	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_fc_log_info - Log information returned from Fibre Channel IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@log_info: U32 LogInfo reply word from the IOC
 *
 *	Refer to lsi/mpi_log_fc.h.
 */
static void
mpt_fc_log_info(MPT_ADAPTER *ioc, u32 log_info)
{
	char *desc = "unknown";

	switch (log_info & 0xFF000000) {
	case MPI_IOCLOGINFO_FC_INIT_BASE:
		desc = "FCP Initiator";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_BASE:
		desc = "FCP Target";
		break;
	case MPI_IOCLOGINFO_FC_LAN_BASE:
		desc = "LAN";
		break;
	case MPI_IOCLOGINFO_FC_MSG_BASE:
		desc = "MPI Message Layer";
		break;
	case MPI_IOCLOGINFO_FC_LINK_BASE:
		desc = "FC Link";
		break;
	case MPI_IOCLOGINFO_FC_CTX_BASE:
		desc = "Context Manager";
		break;
	case MPI_IOCLOGINFO_FC_INVALID_FIELD_BYTE_OFFSET:
		desc = "Invalid Field Offset";
		break;
	case MPI_IOCLOGINFO_FC_STATE_CHANGE:
		desc = "State Change Info";
		break;
	}

	printk(MYIOC_s_INFO_FMT "LogInfo(0x%08x): SubClass={%s}, Value=(0x%06x)\n",
			ioc->name, log_info, desc, (log_info & 0xFFFFFF));
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_spi_log_info - Log information returned from SCSI Parallel IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@log_info: U32 LogInfo word from the IOC
 *
 *	Refer to lsi/sp_log.h.
 */
static void
mpt_spi_log_info(MPT_ADAPTER *ioc, u32 log_info)
{
	u32 info = log_info & 0x00FF0000;
	char *desc = "unknown";

	switch (info) {
	case 0x00010000:
		desc = "bug! MID not found";
		break;

	case 0x00020000:
		desc = "Parity Error";
		break;

	case 0x00030000:
		desc = "ASYNC Outbound Overrun";
		break;

	case 0x00040000:
		desc = "SYNC Offset Error";
		break;

	case 0x00050000:
		desc = "BM Change";
		break;

	case 0x00060000:
		desc = "Msg In Overflow";
		break;

	case 0x00070000:
		desc = "DMA Error";
		break;

	case 0x00080000:
		desc = "Outbound DMA Overrun";
		break;

	case 0x00090000:
		desc = "Task Management";
		break;

	case 0x000A0000:
		desc = "Device Problem";
		break;

	case 0x000B0000:
		desc = "Invalid Phase Change";
		break;

	case 0x000C0000:
		desc = "Untagged Table Size";
		break;

	}

	printk(MYIOC_s_INFO_FMT "LogInfo(0x%08x): F/W: %s\n", ioc->name, log_info, desc);
}

/* strings for sas loginfo */
	static char *originator_str[] = {
		"IOP",						/* 00h */
		"PL",						/* 01h */
		"IR"						/* 02h */
	};
	static char *iop_code_str[] = {
		NULL,						/* 00h */
		"Invalid SAS Address",				/* 01h */
		NULL,						/* 02h */
		"Invalid Page",					/* 03h */
		"Diag Message Error",				/* 04h */
		"Task Terminated",				/* 05h */
		"Enclosure Management",				/* 06h */
		"Target Mode"					/* 07h */
	};
	static char *pl_code_str[] = {
		NULL,						/* 00h */
		"Open Failure",					/* 01h */
		"Invalid Scatter Gather List",			/* 02h */
		"Wrong Relative Offset or Frame Length",	/* 03h */
		"Frame Transfer Error",				/* 04h */
		"Transmit Frame Connected Low",			/* 05h */
		"SATA Non-NCQ RW Error Bit Set",		/* 06h */
		"SATA Read Log Receive Data Error",		/* 07h */
		"SATA NCQ Fail All Commands After Error",	/* 08h */
		"SATA Error in Receive Set Device Bit FIS",	/* 09h */
		"Receive Frame Invalid Message",		/* 0Ah */
		"Receive Context Message Valid Error",		/* 0Bh */
		"Receive Frame Current Frame Error",		/* 0Ch */
		"SATA Link Down",				/* 0Dh */
		"Discovery SATA Init W IOS",			/* 0Eh */
		"Config Invalid Page",				/* 0Fh */
		"Discovery SATA Init Timeout",			/* 10h */
		"Reset",					/* 11h */
		"Abort",					/* 12h */
		"IO Not Yet Executed",				/* 13h */
		"IO Executed",					/* 14h */
		"Persistent Reservation Out Not Affiliation "
		    "Owner", 					/* 15h */
		"Open Transmit DMA Abort",			/* 16h */
		"IO Device Missing Delay Retry",		/* 17h */
		"IO Cancelled Due to Receive Error",		/* 18h */
		NULL,						/* 19h */
		NULL,						/* 1Ah */
		NULL,						/* 1Bh */
		NULL,						/* 1Ch */
		NULL,						/* 1Dh */
		NULL,						/* 1Eh */
		NULL,						/* 1Fh */
		"Enclosure Management"				/* 20h */
	};
	static char *ir_code_str[] = {
		"Raid Action Error",				/* 00h */
		NULL,						/* 00h */
		NULL,						/* 01h */
		NULL,						/* 02h */
		NULL,						/* 03h */
		NULL,						/* 04h */
		NULL,						/* 05h */
		NULL,						/* 06h */
		NULL						/* 07h */
	};
	static char *raid_sub_code_str[] = {
		NULL, 						/* 00h */
		"Volume Creation Failed: Data Passed too "
		    "Large", 					/* 01h */
		"Volume Creation Failed: Duplicate Volumes "
		    "Attempted", 				/* 02h */
		"Volume Creation Failed: Max Number "
		    "Supported Volumes Exceeded",		/* 03h */
		"Volume Creation Failed: DMA Error",		/* 04h */
		"Volume Creation Failed: Invalid Volume Type",	/* 05h */
		"Volume Creation Failed: Error Reading "
		    "MFG Page 4", 				/* 06h */
		"Volume Creation Failed: Creating Internal "
		    "Structures", 				/* 07h */
		NULL,						/* 08h */
		NULL,						/* 09h */
		NULL,						/* 0Ah */
		NULL,						/* 0Bh */
		NULL,						/* 0Ch */
		NULL,						/* 0Dh */
		NULL,						/* 0Eh */
		NULL,						/* 0Fh */
		"Activation failed: Already Active Volume", 	/* 10h */
		"Activation failed: Unsupported Volume Type", 	/* 11h */
		"Activation failed: Too Many Active Volumes", 	/* 12h */
		"Activation failed: Volume ID in Use", 		/* 13h */
		"Activation failed: Reported Failure", 		/* 14h */
		"Activation failed: Importing a Volume", 	/* 15h */
		NULL,						/* 16h */
		NULL,						/* 17h */
		NULL,						/* 18h */
		NULL,						/* 19h */
		NULL,						/* 1Ah */
		NULL,						/* 1Bh */
		NULL,						/* 1Ch */
		NULL,						/* 1Dh */
		NULL,						/* 1Eh */
		NULL,						/* 1Fh */
		"Phys Disk failed: Too Many Phys Disks", 	/* 20h */
		"Phys Disk failed: Data Passed too Large",	/* 21h */
		"Phys Disk failed: DMA Error", 			/* 22h */
		"Phys Disk failed: Invalid <channel:id>", 	/* 23h */
		"Phys Disk failed: Creating Phys Disk Config "
		    "Page", 					/* 24h */
		NULL,						/* 25h */
		NULL,						/* 26h */
		NULL,						/* 27h */
		NULL,						/* 28h */
		NULL,						/* 29h */
		NULL,						/* 2Ah */
		NULL,						/* 2Bh */
		NULL,						/* 2Ch */
		NULL,						/* 2Dh */
		NULL,						/* 2Eh */
		NULL,						/* 2Fh */
		"Compatibility Error: IR Disabled",		/* 30h */
		"Compatibility Error: Inquiry Command Failed",	/* 31h */
		"Compatibility Error: Device not Direct Access "
		    "Device ",					/* 32h */
		"Compatibility Error: Removable Device Found",	/* 33h */
		"Compatibility Error: Device SCSI Version not "
		    "2 or Higher", 				/* 34h */
		"Compatibility Error: SATA Device, 48 BIT LBA "
		    "not Supported", 				/* 35h */
		"Compatibility Error: Device doesn't have "
		    "512 Byte Block Sizes", 			/* 36h */
		"Compatibility Error: Volume Type Check Failed", /* 37h */
		"Compatibility Error: Volume Type is "
		    "Unsupported by FW", 			/* 38h */
		"Compatibility Error: Disk Drive too Small for "
		    "use in Volume", 				/* 39h */
		"Compatibility Error: Phys Disk for Create "
		    "Volume not Found", 			/* 3Ah */
		"Compatibility Error: Too Many or too Few "
		    "Disks for Volume Type", 			/* 3Bh */
		"Compatibility Error: Disk stripe Sizes "
		    "Must be 64KB", 				/* 3Ch */
		"Compatibility Error: IME Size Limited to < 2TB", /* 3Dh */
	};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_sas_log_info - Log information returned from SAS IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@log_info: U32 LogInfo reply word from the IOC
 *	@cb_idx: callback function's handle
 *
 *	Refer to lsi/mpi_log_sas.h.
 **/
static void
mpt_sas_log_info(MPT_ADAPTER *ioc, u32 log_info, u8 cb_idx)
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
	char *originator_desc = NULL;
	char *code_desc = NULL;
	char *sub_code_desc = NULL;

	sas_loginfo.loginfo = log_info;
	if ((sas_loginfo.dw.bus_type != 3 /*SAS*/) &&
	    (sas_loginfo.dw.originator < ARRAY_SIZE(originator_str)))
		return;

	originator_desc = originator_str[sas_loginfo.dw.originator];

	switch (sas_loginfo.dw.originator) {

		case 0:  /* IOP */
			if (sas_loginfo.dw.code <
			    ARRAY_SIZE(iop_code_str))
				code_desc = iop_code_str[sas_loginfo.dw.code];
			break;
		case 1:  /* PL */
			if (sas_loginfo.dw.code <
			    ARRAY_SIZE(pl_code_str))
				code_desc = pl_code_str[sas_loginfo.dw.code];
			break;
		case 2:  /* IR */
			if (sas_loginfo.dw.code >=
			    ARRAY_SIZE(ir_code_str))
				break;
			code_desc = ir_code_str[sas_loginfo.dw.code];
			if (sas_loginfo.dw.subcode >=
			    ARRAY_SIZE(raid_sub_code_str))
				break;
			if (sas_loginfo.dw.code == 0)
				sub_code_desc =
				    raid_sub_code_str[sas_loginfo.dw.subcode];
			break;
		default:
			return;
	}

	if (sub_code_desc != NULL)
		printk(MYIOC_s_INFO_FMT
			"LogInfo(0x%08x): Originator={%s}, Code={%s},"
			" SubCode={%s} cb_idx %s\n",
			ioc->name, log_info, originator_desc, code_desc,
			sub_code_desc, MptCallbacksName[cb_idx]);
	else if (code_desc != NULL)
		printk(MYIOC_s_INFO_FMT
			"LogInfo(0x%08x): Originator={%s}, Code={%s},"
			" SubCode(0x%04x) cb_idx %s\n",
			ioc->name, log_info, originator_desc, code_desc,
			sas_loginfo.dw.subcode, MptCallbacksName[cb_idx]);
	else
		printk(MYIOC_s_INFO_FMT
			"LogInfo(0x%08x): Originator={%s}, Code=(0x%02x),"
			" SubCode(0x%04x) cb_idx %s\n",
			ioc->name, log_info, originator_desc,
			sas_loginfo.dw.code, sas_loginfo.dw.subcode,
			MptCallbacksName[cb_idx]);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_iocstatus_info_config - IOCSTATUS information for config pages
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@ioc_status: U32 IOCStatus word from IOC
 *	@mf: Pointer to MPT request frame
 *
 *	Refer to lsi/mpi.h.
 **/
static void
mpt_iocstatus_info_config(MPT_ADAPTER *ioc, u32 ioc_status, MPT_FRAME_HDR *mf)
{
	Config_t *pReq = (Config_t *)mf;
	char extend_desc[EVENT_DESCR_STR_SZ];
	char *desc = NULL;
	u32 form;
	u8 page_type;

	if (pReq->Header.PageType == MPI_CONFIG_PAGETYPE_EXTENDED)
		page_type = pReq->ExtPageType;
	else
		page_type = pReq->Header.PageType;

	/*
	 * ignore invalid page messages for GET_NEXT_HANDLE
	 */
	form = le32_to_cpu(pReq->PageAddress);
	if (ioc_status == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		if (page_type == MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE ||
		    page_type == MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER ||
		    page_type == MPI_CONFIG_EXTPAGETYPE_ENCLOSURE) {
			if ((form >> MPI_SAS_DEVICE_PGAD_FORM_SHIFT) ==
				MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE)
				return;
		}
		if (page_type == MPI_CONFIG_PAGETYPE_FC_DEVICE)
			if ((form & MPI_FC_DEVICE_PGAD_FORM_MASK) ==
				MPI_FC_DEVICE_PGAD_FORM_NEXT_DID)
				return;
	}

	snprintf(extend_desc, EVENT_DESCR_STR_SZ,
	    "type=%02Xh, page=%02Xh, action=%02Xh, form=%08Xh",
	    page_type, pReq->Header.PageNumber, pReq->Action, form);

	switch (ioc_status) {

	case MPI_IOCSTATUS_CONFIG_INVALID_ACTION: /* 0x0020 */
		desc = "Config Page Invalid Action";
		break;

	case MPI_IOCSTATUS_CONFIG_INVALID_TYPE:   /* 0x0021 */
		desc = "Config Page Invalid Type";
		break;

	case MPI_IOCSTATUS_CONFIG_INVALID_PAGE:   /* 0x0022 */
		desc = "Config Page Invalid Page";
		break;

	case MPI_IOCSTATUS_CONFIG_INVALID_DATA:   /* 0x0023 */
		desc = "Config Page Invalid Data";
		break;

	case MPI_IOCSTATUS_CONFIG_NO_DEFAULTS:    /* 0x0024 */
		desc = "Config Page No Defaults";
		break;

	case MPI_IOCSTATUS_CONFIG_CANT_COMMIT:    /* 0x0025 */
		desc = "Config Page Can't Commit";
		break;
	}

	if (!desc)
		return;

	dreplyprintk(ioc, printk(MYIOC_s_DEBUG_FMT "IOCStatus(0x%04X): %s: %s\n",
	    ioc->name, ioc_status, desc, extend_desc));
}

/**
 *	mpt_iocstatus_info - IOCSTATUS information returned from IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@ioc_status: U32 IOCStatus word from IOC
 *	@mf: Pointer to MPT request frame
 *
 *	Refer to lsi/mpi.h.
 **/
static void
mpt_iocstatus_info(MPT_ADAPTER *ioc, u32 ioc_status, MPT_FRAME_HDR *mf)
{
	u32 status = ioc_status & MPI_IOCSTATUS_MASK;
	char *desc = NULL;

	switch (status) {

/****************************************************************************/
/*  Common IOCStatus values for all replies                                 */
/****************************************************************************/

	case MPI_IOCSTATUS_INVALID_FUNCTION: /* 0x0001 */
		desc = "Invalid Function";
		break;

	case MPI_IOCSTATUS_BUSY: /* 0x0002 */
		desc = "Busy";
		break;

	case MPI_IOCSTATUS_INVALID_SGL: /* 0x0003 */
		desc = "Invalid SGL";
		break;

	case MPI_IOCSTATUS_INTERNAL_ERROR: /* 0x0004 */
		desc = "Internal Error";
		break;

	case MPI_IOCSTATUS_RESERVED: /* 0x0005 */
		desc = "Reserved";
		break;

	case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES: /* 0x0006 */
		desc = "Insufficient Resources";
		break;

	case MPI_IOCSTATUS_INVALID_FIELD: /* 0x0007 */
		desc = "Invalid Field";
		break;

	case MPI_IOCSTATUS_INVALID_STATE: /* 0x0008 */
		desc = "Invalid State";
		break;

/****************************************************************************/
/*  Config IOCStatus values                                                 */
/****************************************************************************/

	case MPI_IOCSTATUS_CONFIG_INVALID_ACTION: /* 0x0020 */
	case MPI_IOCSTATUS_CONFIG_INVALID_TYPE:   /* 0x0021 */
	case MPI_IOCSTATUS_CONFIG_INVALID_PAGE:   /* 0x0022 */
	case MPI_IOCSTATUS_CONFIG_INVALID_DATA:   /* 0x0023 */
	case MPI_IOCSTATUS_CONFIG_NO_DEFAULTS:    /* 0x0024 */
	case MPI_IOCSTATUS_CONFIG_CANT_COMMIT:    /* 0x0025 */
		mpt_iocstatus_info_config(ioc, status, mf);
		break;

/****************************************************************************/
/*  SCSIIO Reply (SPI, FCP, SAS) initiator values                           */
/*                                                                          */
/*  Look at mptscsih_iocstatus_info_scsiio in mptscsih.c */
/*                                                                          */
/****************************************************************************/

	case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR: /* 0x0040 */
	case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN: /* 0x0045 */
	case MPI_IOCSTATUS_SCSI_INVALID_BUS: /* 0x0041 */
	case MPI_IOCSTATUS_SCSI_INVALID_TARGETID: /* 0x0042 */
	case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE: /* 0x0043 */
	case MPI_IOCSTATUS_SCSI_DATA_OVERRUN: /* 0x0044 */
	case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR: /* 0x0046 */
	case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR: /* 0x0047 */
	case MPI_IOCSTATUS_SCSI_TASK_TERMINATED: /* 0x0048 */
	case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH: /* 0x0049 */
	case MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED: /* 0x004A */
	case MPI_IOCSTATUS_SCSI_IOC_TERMINATED: /* 0x004B */
	case MPI_IOCSTATUS_SCSI_EXT_TERMINATED: /* 0x004C */
		break;

/****************************************************************************/
/*  SCSI Target values                                                      */
/****************************************************************************/

	case MPI_IOCSTATUS_TARGET_PRIORITY_IO: /* 0x0060 */
		desc = "Target: Priority IO";
		break;

	case MPI_IOCSTATUS_TARGET_INVALID_PORT: /* 0x0061 */
		desc = "Target: Invalid Port";
		break;

	case MPI_IOCSTATUS_TARGET_INVALID_IO_INDEX: /* 0x0062 */
		desc = "Target Invalid IO Index:";
		break;

	case MPI_IOCSTATUS_TARGET_ABORTED: /* 0x0063 */
		desc = "Target: Aborted";
		break;

	case MPI_IOCSTATUS_TARGET_NO_CONN_RETRYABLE: /* 0x0064 */
		desc = "Target: No Conn Retryable";
		break;

	case MPI_IOCSTATUS_TARGET_NO_CONNECTION: /* 0x0065 */
		desc = "Target: No Connection";
		break;

	case MPI_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH: /* 0x006A */
		desc = "Target: Transfer Count Mismatch";
		break;

	case MPI_IOCSTATUS_TARGET_STS_DATA_NOT_SENT: /* 0x006B */
		desc = "Target: STS Data not Sent";
		break;

	case MPI_IOCSTATUS_TARGET_DATA_OFFSET_ERROR: /* 0x006D */
		desc = "Target: Data Offset Error";
		break;

	case MPI_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA: /* 0x006E */
		desc = "Target: Too Much Write Data";
		break;

	case MPI_IOCSTATUS_TARGET_IU_TOO_SHORT: /* 0x006F */
		desc = "Target: IU Too Short";
		break;

	case MPI_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT: /* 0x0070 */
		desc = "Target: ACK NAK Timeout";
		break;

	case MPI_IOCSTATUS_TARGET_NAK_RECEIVED: /* 0x0071 */
		desc = "Target: Nak Received";
		break;

/****************************************************************************/
/*  Fibre Channel Direct Access values                                      */
/****************************************************************************/

	case MPI_IOCSTATUS_FC_ABORTED: /* 0x0066 */
		desc = "FC: Aborted";
		break;

	case MPI_IOCSTATUS_FC_RX_ID_INVALID: /* 0x0067 */
		desc = "FC: RX ID Invalid";
		break;

	case MPI_IOCSTATUS_FC_DID_INVALID: /* 0x0068 */
		desc = "FC: DID Invalid";
		break;

	case MPI_IOCSTATUS_FC_NODE_LOGGED_OUT: /* 0x0069 */
		desc = "FC: Node Logged Out";
		break;

	case MPI_IOCSTATUS_FC_EXCHANGE_CANCELED: /* 0x006C */
		desc = "FC: Exchange Canceled";
		break;

/****************************************************************************/
/*  LAN values                                                              */
/****************************************************************************/

	case MPI_IOCSTATUS_LAN_DEVICE_NOT_FOUND: /* 0x0080 */
		desc = "LAN: Device not Found";
		break;

	case MPI_IOCSTATUS_LAN_DEVICE_FAILURE: /* 0x0081 */
		desc = "LAN: Device Failure";
		break;

	case MPI_IOCSTATUS_LAN_TRANSMIT_ERROR: /* 0x0082 */
		desc = "LAN: Transmit Error";
		break;

	case MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED: /* 0x0083 */
		desc = "LAN: Transmit Aborted";
		break;

	case MPI_IOCSTATUS_LAN_RECEIVE_ERROR: /* 0x0084 */
		desc = "LAN: Receive Error";
		break;

	case MPI_IOCSTATUS_LAN_RECEIVE_ABORTED: /* 0x0085 */
		desc = "LAN: Receive Aborted";
		break;

	case MPI_IOCSTATUS_LAN_PARTIAL_PACKET: /* 0x0086 */
		desc = "LAN: Partial Packet";
		break;

	case MPI_IOCSTATUS_LAN_CANCELED: /* 0x0087 */
		desc = "LAN: Canceled";
		break;

/****************************************************************************/
/*  Serial Attached SCSI values                                             */
/****************************************************************************/

	case MPI_IOCSTATUS_SAS_SMP_REQUEST_FAILED: /* 0x0090 */
		desc = "SAS: SMP Request Failed";
		break;

	case MPI_IOCSTATUS_SAS_SMP_DATA_OVERRUN: /* 0x0090 */
		desc = "SAS: SMP Data Overrun";
		break;

	default:
		desc = "Others";
		break;
	}

	if (!desc)
		return;

	dreplyprintk(ioc, printk(MYIOC_s_DEBUG_FMT "IOCStatus(0x%04X): %s\n",
	    ioc->name, status, desc));
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
EXPORT_SYMBOL(mpt_attach);
EXPORT_SYMBOL(mpt_detach);
#ifdef CONFIG_PM
EXPORT_SYMBOL(mpt_resume);
EXPORT_SYMBOL(mpt_suspend);
#endif
EXPORT_SYMBOL(ioc_list);
EXPORT_SYMBOL(mpt_register);
EXPORT_SYMBOL(mpt_deregister);
EXPORT_SYMBOL(mpt_event_register);
EXPORT_SYMBOL(mpt_event_deregister);
EXPORT_SYMBOL(mpt_reset_register);
EXPORT_SYMBOL(mpt_reset_deregister);
EXPORT_SYMBOL(mpt_device_driver_register);
EXPORT_SYMBOL(mpt_device_driver_deregister);
EXPORT_SYMBOL(mpt_get_msg_frame);
EXPORT_SYMBOL(mpt_put_msg_frame);
EXPORT_SYMBOL(mpt_put_msg_frame_hi_pri);
EXPORT_SYMBOL(mpt_free_msg_frame);
EXPORT_SYMBOL(mpt_send_handshake_request);
EXPORT_SYMBOL(mpt_verify_adapter);
EXPORT_SYMBOL(mpt_GetIocState);
EXPORT_SYMBOL(mpt_print_ioc_summary);
EXPORT_SYMBOL(mpt_HardResetHandler);
EXPORT_SYMBOL(mpt_config);
EXPORT_SYMBOL(mpt_findImVolumes);
EXPORT_SYMBOL(mpt_alloc_fw_memory);
EXPORT_SYMBOL(mpt_free_fw_memory);
EXPORT_SYMBOL(mptbase_sas_persist_operation);
EXPORT_SYMBOL(mpt_raid_phys_disk_pg0);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	fusion_init - Fusion MPT base driver initialization routine.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int __init
fusion_init(void)
{
	u8 cb_idx;

	show_mptmod_ver(my_NAME, my_VERSION);
	printk(KERN_INFO COPYRIGHT "\n");

	for (cb_idx = 0; cb_idx < MPT_MAX_PROTOCOL_DRIVERS; cb_idx++) {
		MptCallbacks[cb_idx] = NULL;
		MptDriverClass[cb_idx] = MPTUNKNOWN_DRIVER;
		MptEvHandlers[cb_idx] = NULL;
		MptResetHandlers[cb_idx] = NULL;
	}

	/*  Register ourselves (mptbase) in order to facilitate
	 *  EventNotification handling.
	 */
	mpt_base_index = mpt_register(mptbase_reply, MPTBASE_DRIVER,
	    "mptbase_reply");

	/* Register for hard reset handling callbacks.
	 */
	mpt_reset_register(mpt_base_index, mpt_ioc_reset);

#ifdef CONFIG_PROC_FS
	(void) procmpt_create();
#endif
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	fusion_exit - Perform driver unload cleanup.
 *
 *	This routine frees all resources associated with each MPT adapter
 *	and removes all %MPT_PROCFS_MPTBASEDIR entries.
 */
static void __exit
fusion_exit(void)
{

	mpt_reset_deregister(mpt_base_index);

#ifdef CONFIG_PROC_FS
	procmpt_destroy();
#endif
}

module_init(fusion_init);
module_exit(fusion_exit);

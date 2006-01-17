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
#include <linux/sort.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

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

#define MPTFC_DEV_LOSS_TMO (60)
static int mptfc_dev_loss_tmo = MPTFC_DEV_LOSS_TMO;	/* reasonable default */
module_param(mptfc_dev_loss_tmo, int, 0);
MODULE_PARM_DESC(mptfc_dev_loss_tmo, " Initial time the driver programs the "
    				     " transport to wait for an rport to "
				     " return following a device loss event."
				     "  Default=60.");

static int	mptfcDoneCtx = -1;
static int	mptfcTaskCtx = -1;
static int	mptfcInternalCtx = -1; /* Used only for internal commands */

int mptfc_slave_alloc(struct scsi_device *device);
static int mptfc_qcmd(struct scsi_cmnd *SCpnt,
    void (*done)(struct scsi_cmnd *));

static void mptfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout);
static void __devexit mptfc_remove(struct pci_dev *pdev);

static struct scsi_host_template mptfc_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptfc",
	.proc_info			= mptscsih_proc_info,
	.name				= "MPT FC Host",
	.info				= mptscsih_info,
	.queuecommand			= mptfc_qcmd,
	.target_alloc			= mptscsih_target_alloc,
	.slave_alloc			= mptfc_slave_alloc,
	.slave_configure		= mptscsih_slave_configure,
	.target_destroy			= mptscsih_target_destroy,
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
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_FC949ES,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptfc_pci_table);

static struct scsi_transport_template *mptfc_transport_template = NULL;

struct fc_function_template mptfc_transport_functions = {
	.dd_fcrport_size = 8,
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_port_id = 1,
	.show_rport_supported_classes = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,
	.set_rport_dev_loss_tmo = mptfc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

};

/* FIXME! values controlling firmware RESCAN event
 * need to be set low to allow dev_loss_tmo to
 * work as expected.  Currently, firmware doesn't
 * notify driver of RESCAN event until some number
 * of seconds elapse.  This value can be set via
 * lsiutil.
 */
static void
mptfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	if (timeout > 0)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = mptfc_dev_loss_tmo;
}

static int
mptfc_FcDevPage0_cmp_func(const void *a, const void *b)
{
	FCDevicePage0_t **aa = (FCDevicePage0_t **)a;
	FCDevicePage0_t **bb = (FCDevicePage0_t **)b;

	if ((*aa)->CurrentBus == (*bb)->CurrentBus) {
		if ((*aa)->CurrentTargetID == (*bb)->CurrentTargetID)
			return 0;
		if ((*aa)->CurrentTargetID < (*bb)->CurrentTargetID)
			return -1;
		return 1;
	}
	if ((*aa)->CurrentBus < (*bb)->CurrentBus)
		return -1;
	return 1;
}

static int
mptfc_GetFcDevPage0(MPT_ADAPTER *ioc, int ioc_port,
	void(*func)(MPT_ADAPTER *ioc,int channel, FCDevicePage0_t *arg))
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	FCDevicePage0_t		*ppage0_alloc, *fc;
	dma_addr_t		 page0_dma;
	int			 data_sz;
	int			 ii;

	FCDevicePage0_t		*p0_array=NULL, *p_p0;
	FCDevicePage0_t		**pp0_array=NULL, **p_pp0;

	int			 rc = -ENOMEM;
	U32			 port_id = 0xffffff;
	int			 num_targ = 0;
	int			 max_bus = ioc->facts.MaxBuses;
	int			 max_targ = ioc->facts.MaxDevices;

	if (max_bus == 0 || max_targ == 0)
		goto out;

	data_sz = sizeof(FCDevicePage0_t) * max_bus * max_targ;
	p_p0 = p0_array =  kzalloc(data_sz, GFP_KERNEL);
	if (!p0_array)
		goto out;

	data_sz = sizeof(FCDevicePage0_t *) * max_bus * max_targ;
	p_pp0 = pp0_array = kzalloc(data_sz, GFP_KERNEL);
	if (!pp0_array)
		goto out;

	do {
		/* Get FC Device Page 0 header */
		hdr.PageVersion = 0;
		hdr.PageLength = 0;
		hdr.PageNumber = 0;
		hdr.PageType = MPI_CONFIG_PAGETYPE_FC_DEVICE;
		cfg.cfghdr.hdr = &hdr;
		cfg.physAddr = -1;
		cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
		cfg.dir = 0;
		cfg.pageAddr = port_id;
		cfg.timeout = 0;

		if ((rc = mpt_config(ioc, &cfg)) != 0)
			break;

		if (hdr.PageLength <= 0)
			break;

		data_sz = hdr.PageLength * 4;
		ppage0_alloc = pci_alloc_consistent(ioc->pcidev, data_sz,
		    					&page0_dma);
		rc = -ENOMEM;
		if (!ppage0_alloc)
			break;

		cfg.physAddr = page0_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		if ((rc = mpt_config(ioc, &cfg)) == 0) {
			ppage0_alloc->PortIdentifier =
				le32_to_cpu(ppage0_alloc->PortIdentifier);

			ppage0_alloc->WWNN.Low =
				le32_to_cpu(ppage0_alloc->WWNN.Low);

			ppage0_alloc->WWNN.High =
				le32_to_cpu(ppage0_alloc->WWNN.High);

			ppage0_alloc->WWPN.Low =
				le32_to_cpu(ppage0_alloc->WWPN.Low);

			ppage0_alloc->WWPN.High =
				le32_to_cpu(ppage0_alloc->WWPN.High);

			ppage0_alloc->BBCredit =
				le16_to_cpu(ppage0_alloc->BBCredit);

			ppage0_alloc->MaxRxFrameSize =
				le16_to_cpu(ppage0_alloc->MaxRxFrameSize);

			port_id = ppage0_alloc->PortIdentifier;
			num_targ++;
			*p_p0 = *ppage0_alloc;	/* save data */
			*p_pp0++ = p_p0++;	/* save addr */
		}
		pci_free_consistent(ioc->pcidev, data_sz,
		    			(u8 *) ppage0_alloc, page0_dma);
		if (rc != 0)
			break;

	} while (port_id <= 0xff0000);

	if (num_targ) {
		/* sort array */
		if (num_targ > 1)
			sort (pp0_array, num_targ, sizeof(FCDevicePage0_t *),
				mptfc_FcDevPage0_cmp_func, NULL);
		/* call caller's func for each targ */
		for (ii = 0; ii < num_targ;  ii++) {
			fc = *(pp0_array+ii);
			func(ioc, ioc_port, fc);
		}
	}

 out:
	if (pp0_array)
		kfree(pp0_array);
	if (p0_array)
		kfree(p0_array);
	return rc;
}

static int
mptfc_generate_rport_ids(FCDevicePage0_t *pg0, struct fc_rport_identifiers *rid)
{
	/* not currently usable */
	if (pg0->Flags & (MPI_FC_DEVICE_PAGE0_FLAGS_PLOGI_INVALID |
			  MPI_FC_DEVICE_PAGE0_FLAGS_PRLI_INVALID))
		return -1;

	if (!(pg0->Flags & MPI_FC_DEVICE_PAGE0_FLAGS_TARGETID_BUS_VALID))
		return -1;

	if (!(pg0->Protocol & MPI_FC_DEVICE_PAGE0_PROT_FCP_TARGET))
		return -1;

	/*
	 * board data structure already normalized to platform endianness
	 * shifted to avoid unaligned access on 64 bit architecture
	 */
	rid->node_name = ((u64)pg0->WWNN.High) << 32 | (u64)pg0->WWNN.Low;
	rid->port_name = ((u64)pg0->WWPN.High) << 32 | (u64)pg0->WWPN.Low;
	rid->port_id =   pg0->PortIdentifier;
	rid->roles = FC_RPORT_ROLE_UNKNOWN;
	rid->roles |= FC_RPORT_ROLE_FCP_TARGET;
	if (pg0->Protocol & MPI_FC_DEVICE_PAGE0_PROT_FCP_INITIATOR)
		rid->roles |= FC_RPORT_ROLE_FCP_INITIATOR;

	return 0;
}

static void
mptfc_register_dev(MPT_ADAPTER *ioc, int channel, FCDevicePage0_t *pg0)
{
	struct fc_rport_identifiers rport_ids;
	struct fc_rport		*rport;
	struct mptfc_rport_info	*ri;
	int			match = 0;
	u64			port_name;
	unsigned long		flags;

	if (mptfc_generate_rport_ids(pg0, &rport_ids) < 0)
		return;

	/* scan list looking for a match */
	spin_lock_irqsave(&ioc->fc_rport_lock, flags);
	list_for_each_entry(ri, &ioc->fc_rports, list) {
		port_name = (u64)ri->pg0.WWPN.High << 32 | (u64)ri->pg0.WWPN.Low;
		if (port_name == rport_ids.port_name) {	/* match */
			list_move_tail(&ri->list, &ioc->fc_rports);
			match = 1;
			break;
		}
	}
	if (!match) {	/* allocate one */
		spin_unlock_irqrestore(&ioc->fc_rport_lock, flags);
		ri = kzalloc(sizeof(struct mptfc_rport_info), GFP_KERNEL);
		if (!ri)
			return;
		spin_lock_irqsave(&ioc->fc_rport_lock, flags);
		list_add_tail(&ri->list, &ioc->fc_rports);
	}

	ri->pg0 = *pg0;	/* add/update pg0 data */
	ri->flags &= ~MPT_RPORT_INFO_FLAGS_MISSING;

	if (!(ri->flags & MPT_RPORT_INFO_FLAGS_REGISTERED)) {
		ri->flags |= MPT_RPORT_INFO_FLAGS_REGISTERED;
		spin_unlock_irqrestore(&ioc->fc_rport_lock, flags);
		rport = fc_remote_port_add(ioc->sh,channel, &rport_ids);
		spin_lock_irqsave(&ioc->fc_rport_lock, flags);
		if (rport) {
			if (*((struct mptfc_rport_info **)rport->dd_data) != ri) {
				ri->flags &= ~MPT_RPORT_INFO_FLAGS_MAPPED_VDEV;
				ri->vdev = NULL;
				ri->rport = rport;
				*((struct mptfc_rport_info **)rport->dd_data) = ri;
			}
			rport->dev_loss_tmo = mptfc_dev_loss_tmo;
			/*
			 * if already mapped, remap here.  If not mapped,
			 * slave_alloc will allocate vdev and map
			 */
			if (ri->flags & MPT_RPORT_INFO_FLAGS_MAPPED_VDEV) {
				ri->vdev->target_id = ri->pg0.CurrentTargetID;
				ri->vdev->bus_id = ri->pg0.CurrentBus;
				ri->vdev->vtarget->target_id = ri->vdev->target_id;
				ri->vdev->vtarget->bus_id = ri->vdev->bus_id;
			}
			#ifdef MPT_DEBUG
			printk ("mptfc_reg_dev.%d: %x, %llx / %llx, tid %d, "
				"rport tid %d, tmo %d\n",
					ioc->sh->host_no,
					pg0->PortIdentifier,
					pg0->WWNN,
					pg0->WWPN,
					pg0->CurrentTargetID,
					ri->rport->scsi_target_id,
					ri->rport->dev_loss_tmo);
			#endif
		} else {
			list_del(&ri->list);
			kfree(ri);
			ri = NULL;
		}
	}
	spin_unlock_irqrestore(&ioc->fc_rport_lock,flags);

}

/*
 *	OS entry point to allow host driver to alloc memory
 *	for each scsi device. Called once per device the bus scan.
 *	Return non-zero if allocation fails.
 *	Init memory once per LUN.
 */
int
mptfc_slave_alloc(struct scsi_device *sdev)
{
	MPT_SCSI_HOST		*hd;
	VirtTarget		*vtarget;
	VirtDevice		*vdev;
	struct scsi_target	*starget;
	struct fc_rport		*rport;
	struct mptfc_rport_info *ri;
	unsigned long		flags;


	rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	hd = (MPT_SCSI_HOST *)sdev->host->hostdata;

	vdev = kmalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdev) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kmalloc(%zd) FAILED!\n",
				hd->ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}
	memset(vdev, 0, sizeof(VirtDevice));

	spin_lock_irqsave(&hd->ioc->fc_rport_lock,flags);

	if (!(ri = *((struct mptfc_rport_info **)rport->dd_data))) {
		spin_unlock_irqrestore(&hd->ioc->fc_rport_lock,flags);
		kfree(vdev);
		return -ENODEV;
	}

	sdev->hostdata = vdev;
	starget = scsi_target(sdev);
	vtarget = starget->hostdata;
	if (vtarget->num_luns == 0) {
		vtarget->tflags = MPT_TARGET_FLAGS_Q_YES |
		    		  MPT_TARGET_FLAGS_VALID_INQUIRY;
		hd->Targets[sdev->id] = vtarget;
	}

	vtarget->target_id = vdev->target_id;
	vtarget->bus_id = vdev->bus_id;

	vdev->vtarget = vtarget;
	vdev->ioc_id = hd->ioc->id;
	vdev->lun = sdev->lun;
	vdev->target_id = ri->pg0.CurrentTargetID;
	vdev->bus_id = ri->pg0.CurrentBus;

	ri->flags |= MPT_RPORT_INFO_FLAGS_MAPPED_VDEV;
	ri->vdev = vdev;

	spin_unlock_irqrestore(&hd->ioc->fc_rport_lock,flags);

	vtarget->num_luns++;

#ifdef MPT_DEBUG
	printk ("mptfc_slv_alloc.%d: num_luns %d, sdev.id %d, "
	        "CurrentTargetID %d, %x %llx %llx\n",
			sdev->host->host_no,
			vtarget->num_luns,
			sdev->id, ri->pg0.CurrentTargetID,
			ri->pg0.PortIdentifier, ri->pg0.WWPN, ri->pg0.WWNN);
#endif

	return 0;
}

static int
mptfc_qcmd(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	struct fc_rport	*rport = starget_to_rport(scsi_target(SCpnt->device));
	int		err;

	err = fc_remote_port_chkready(rport);
	if (unlikely(err)) {
		SCpnt->result = err;
		done(SCpnt);
		return 0;
	}
	return mptscsih_qcmd(SCpnt,done);
}

static void
mptfc_init_host_attr(MPT_ADAPTER *ioc,int portnum)
{
	unsigned class = 0, cos = 0;

	/* don't know what to do as only one scsi (fc) host was allocated */
	if (portnum != 0)
		return;

	class = ioc->fc_port_page0[portnum].SupportedServiceClass;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_1)
		cos |= FC_COS_CLASS1;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_2)
		cos |= FC_COS_CLASS2;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_3)
		cos |= FC_COS_CLASS3;

	fc_host_node_name(ioc->sh) =
	    	(u64)ioc->fc_port_page0[portnum].WWNN.High << 32
		    | (u64)ioc->fc_port_page0[portnum].WWNN.Low;

	fc_host_port_name(ioc->sh) =
	    	(u64)ioc->fc_port_page0[portnum].WWPN.High << 32
		    | (u64)ioc->fc_port_page0[portnum].WWPN.Low;

	fc_host_port_id(ioc->sh) = ioc->fc_port_page0[portnum].PortIdentifier;

	fc_host_supported_classes(ioc->sh) = cos;

	fc_host_tgtid_bind_type(ioc->sh) = FC_TGTID_BIND_BY_WWPN;
}

static void
mptfc_rescan_devices(void *arg)
{
	MPT_ADAPTER		*ioc = (MPT_ADAPTER *)arg;
	int			ii;
	int			work_to_do;
	unsigned long		flags;
	struct mptfc_rport_info *ri;

	do {
		/* start by tagging all ports as missing */
		spin_lock_irqsave(&ioc->fc_rport_lock,flags);
		list_for_each_entry(ri, &ioc->fc_rports, list) {
			if (ri->flags & MPT_RPORT_INFO_FLAGS_REGISTERED) {
				ri->flags |= MPT_RPORT_INFO_FLAGS_MISSING;
			}
		}
		spin_unlock_irqrestore(&ioc->fc_rport_lock,flags);

		/*
		 * now rescan devices known to adapter,
		 * will reregister existing rports
		 */
		for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
			(void) mptbase_GetFcPortPage0(ioc, ii);
			mptfc_init_host_attr(ioc,ii);	/* refresh */
			mptfc_GetFcDevPage0(ioc,ii,mptfc_register_dev);
		}

		/* delete devices still missing */
		spin_lock_irqsave(&ioc->fc_rport_lock, flags);
		list_for_each_entry(ri, &ioc->fc_rports, list) {
			/* if newly missing, delete it */
			if ((ri->flags & (MPT_RPORT_INFO_FLAGS_REGISTERED |
					  MPT_RPORT_INFO_FLAGS_MISSING))
			  == (MPT_RPORT_INFO_FLAGS_REGISTERED |
			      MPT_RPORT_INFO_FLAGS_MISSING)) {

				ri->flags &= ~(MPT_RPORT_INFO_FLAGS_REGISTERED|
					       MPT_RPORT_INFO_FLAGS_MISSING);
				fc_remote_port_delete(ri->rport);
				/*
				 * remote port not really deleted 'cause
				 * binding is by WWPN and driver only
				 * registers FCP_TARGETs
				 */
				#ifdef MPT_DEBUG
				printk ("mptfc_rescan.%d: %llx deleted\n",
					ioc->sh->host_no, ri->pg0.WWPN);
				#endif
			}
		}
		spin_unlock_irqrestore(&ioc->fc_rport_lock,flags);

		/*
		 * allow multiple passes as target state
		 * might have changed during scan
		 */
		spin_lock_irqsave(&ioc->fc_rescan_work_lock, flags);
		if (ioc->fc_rescan_work_count > 2) 	/* only need one more */
			ioc->fc_rescan_work_count = 2;
		work_to_do = --ioc->fc_rescan_work_count;
		spin_unlock_irqrestore(&ioc->fc_rescan_work_lock, flags);
	} while (work_to_do);
}

static int
mptfc_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host	*sh;
	MPT_SCSI_HOST		*hd;
	MPT_ADAPTER 		*ioc;
	unsigned long		 flags;
	int			 ii;
	int			 numSGE = 0;
	int			 scale;
	int			 ioc_cap;
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
		error = -ENODEV;
		goto out_mptfc_probe;
	}

	if (!ioc->active) {
		printk(MYIOC_s_WARN_FMT "Skipping because it's disabled!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptfc_probe;
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
		return -ENODEV;
	}

	sh = scsi_host_alloc(&mptfc_driver_template, sizeof(MPT_SCSI_HOST));

	if (!sh) {
		printk(MYIOC_s_WARN_FMT
			"Unable to register controller with SCSI subsystem\n",
			ioc->name);
		error = -1;
		goto out_mptfc_probe;
        }

	INIT_WORK(&ioc->fc_rescan_work, mptfc_rescan_devices,(void *)ioc);

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
	hd->ScsiLookup = kcalloc(ioc->req_depth, sizeof(void *), GFP_ATOMIC);
	if (!hd->ScsiLookup) {
		error = -ENOMEM;
		goto out_mptfc_probe;
	}

	dprintk((MYIOC_s_INFO_FMT "ScsiLookup @ %p\n",
		 ioc->name, hd->ScsiLookup));

	/* Allocate memory for the device structures.
	 * A non-Null pointer at an offset
	 * indicates a device exists.
	 * max_id = 1 + maximum id (hosts.h)
	 */
	hd->Targets = kcalloc(sh->max_id, sizeof(void *), GFP_ATOMIC);
	if (!hd->Targets) {
		error = -ENOMEM;
		goto out_mptfc_probe;
	}

	dprintk((KERN_INFO "  vdev @ %p\n", hd->Targets));

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

	sh->transportt = mptfc_transport_template;
	error = scsi_add_host (sh, &ioc->pcidev->dev);
	if(error) {
		dprintk((KERN_ERR MYNAM
		  "scsi_add_host failed\n"));
		goto out_mptfc_probe;
	}

	for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
		mptfc_init_host_attr(ioc,ii);
		mptfc_GetFcDevPage0(ioc,ii,mptfc_register_dev);
	}

	return 0;

out_mptfc_probe:

	mptscsih_remove(pdev);
	return error;
}

static struct pci_driver mptfc_driver = {
	.name		= "mptfc",
	.id_table	= mptfc_pci_table,
	.probe		= mptfc_probe,
	.remove		= __devexit_p(mptfc_remove),
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
	int error;

	show_mptmod_ver(my_NAME, my_VERSION);

	/* sanity check module parameter */
	if (mptfc_dev_loss_tmo == 0)
		mptfc_dev_loss_tmo = MPTFC_DEV_LOSS_TMO;

	mptfc_transport_template =
		fc_attach_transport(&mptfc_transport_functions);

	if (!mptfc_transport_template)
		return -ENODEV;

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

	error = pci_register_driver(&mptfc_driver);
	if (error) {
		fc_release_transport(mptfc_transport_template);
	}

	return error;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptfc_remove - Removed fc infrastructure for devices
 *	@pdev: Pointer to pci_dev structure
 *
 */
static void __devexit mptfc_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);
	struct mptfc_rport_info *p, *n;

	fc_remove_host(ioc->sh);

	list_for_each_entry_safe(p, n, &ioc->fc_rports, list) {
		list_del(&p->list);
		kfree(p);
	}

	mptscsih_remove(pdev);
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
	fc_release_transport(mptfc_transport_template);

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

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

static int mptfc_target_alloc(struct scsi_target *starget);
static int mptfc_slave_alloc(struct scsi_device *sdev);
static int mptfc_qcmd(struct scsi_cmnd *SCpnt,
		      void (*done)(struct scsi_cmnd *));
static void mptfc_target_destroy(struct scsi_target *starget);
static void mptfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout);
static void __devexit mptfc_remove(struct pci_dev *pdev);
static int mptfc_abort(struct scsi_cmnd *SCpnt);
static int mptfc_dev_reset(struct scsi_cmnd *SCpnt);
static int mptfc_bus_reset(struct scsi_cmnd *SCpnt);
static int mptfc_host_reset(struct scsi_cmnd *SCpnt);

static struct scsi_host_template mptfc_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptfc",
	.proc_info			= mptscsih_proc_info,
	.name				= "MPT FC Host",
	.info				= mptscsih_info,
	.queuecommand			= mptfc_qcmd,
	.target_alloc			= mptfc_target_alloc,
	.slave_alloc			= mptfc_slave_alloc,
	.slave_configure		= mptscsih_slave_configure,
	.target_destroy			= mptfc_target_destroy,
	.slave_destroy			= mptscsih_slave_destroy,
	.change_queue_depth 		= mptscsih_change_queue_depth,
	.eh_abort_handler		= mptfc_abort,
	.eh_device_reset_handler	= mptfc_dev_reset,
	.eh_bus_reset_handler		= mptfc_bus_reset,
	.eh_host_reset_handler		= mptfc_host_reset,
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
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC909,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC919,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC929,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC919X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC929X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC939X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC949X,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVICEID_FC949E,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptfc_pci_table);

static struct scsi_transport_template *mptfc_transport_template = NULL;

static struct fc_function_template mptfc_transport_functions = {
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
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,
	.show_host_speed = 1,
	.show_host_fabric_name = 1,
	.show_host_port_type = 1,
	.show_host_port_state = 1,
	.show_host_symbolic_name = 1,
};

static int
mptfc_block_error_handler(struct scsi_cmnd *SCpnt,
			  int (*func)(struct scsi_cmnd *SCpnt),
			  const char *caller)
{
	struct scsi_device	*sdev = SCpnt->device;
	struct Scsi_Host	*shost = sdev->host;
	struct fc_rport		*rport = starget_to_rport(scsi_target(sdev));
	unsigned long		flags;
	int			ready;

	spin_lock_irqsave(shost->host_lock, flags);
	while ((ready = fc_remote_port_chkready(rport) >> 16) == DID_IMM_RETRY) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		dfcprintk ((MYIOC_s_INFO_FMT
			"mptfc_block_error_handler.%d: %d:%d, port status is "
			"DID_IMM_RETRY, deferring %s recovery.\n",
			((MPT_SCSI_HOST *) shost->hostdata)->ioc->name,
			((MPT_SCSI_HOST *) shost->hostdata)->ioc->sh->host_no,
			SCpnt->device->id,SCpnt->device->lun,caller));
		msleep(1000);
		spin_lock_irqsave(shost->host_lock, flags);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	if (ready == DID_NO_CONNECT || !SCpnt->device->hostdata) {
		dfcprintk ((MYIOC_s_INFO_FMT
			"%s.%d: %d:%d, failing recovery, "
			"port state %d, vdev %p.\n", caller,
			((MPT_SCSI_HOST *) shost->hostdata)->ioc->name,
			((MPT_SCSI_HOST *) shost->hostdata)->ioc->sh->host_no,
			SCpnt->device->id,SCpnt->device->lun,ready,
			SCpnt->device->hostdata));
		return FAILED;
	}
	dfcprintk ((MYIOC_s_INFO_FMT
		"%s.%d: %d:%d, executing recovery.\n", caller,
		((MPT_SCSI_HOST *) shost->hostdata)->ioc->name,
		((MPT_SCSI_HOST *) shost->hostdata)->ioc->sh->host_no,
		SCpnt->device->id,SCpnt->device->lun));
	return (*func)(SCpnt);
}

static int
mptfc_abort(struct scsi_cmnd *SCpnt)
{
	return
	    mptfc_block_error_handler(SCpnt, mptscsih_abort, __FUNCTION__);
}

static int
mptfc_dev_reset(struct scsi_cmnd *SCpnt)
{
	return
	    mptfc_block_error_handler(SCpnt, mptscsih_dev_reset, __FUNCTION__);
}

static int
mptfc_bus_reset(struct scsi_cmnd *SCpnt)
{
	return
	    mptfc_block_error_handler(SCpnt, mptscsih_bus_reset, __FUNCTION__);
}

static int
mptfc_host_reset(struct scsi_cmnd *SCpnt)
{
	return
	    mptfc_block_error_handler(SCpnt, mptscsih_host_reset, __FUNCTION__);
}

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
	kfree(pp0_array);
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

	return 0;
}

static void
mptfc_register_dev(MPT_ADAPTER *ioc, int channel, FCDevicePage0_t *pg0)
{
	struct fc_rport_identifiers rport_ids;
	struct fc_rport		*rport;
	struct mptfc_rport_info	*ri;
	int			new_ri = 1;
	u64			pn, nn;
	VirtTarget		*vtarget;
	u32			roles = FC_RPORT_ROLE_UNKNOWN;

	if (mptfc_generate_rport_ids(pg0, &rport_ids) < 0)
		return;

	roles |= FC_RPORT_ROLE_FCP_TARGET;
	if (pg0->Protocol & MPI_FC_DEVICE_PAGE0_PROT_FCP_INITIATOR)
		roles |= FC_RPORT_ROLE_FCP_INITIATOR;

	/* scan list looking for a match */
	list_for_each_entry(ri, &ioc->fc_rports, list) {
		pn = (u64)ri->pg0.WWPN.High << 32 | (u64)ri->pg0.WWPN.Low;
		if (pn == rport_ids.port_name) {	/* match */
			list_move_tail(&ri->list, &ioc->fc_rports);
			new_ri = 0;
			break;
		}
	}
	if (new_ri) {	/* allocate one */
		ri = kzalloc(sizeof(struct mptfc_rport_info), GFP_KERNEL);
		if (!ri)
			return;
		list_add_tail(&ri->list, &ioc->fc_rports);
	}

	ri->pg0 = *pg0;	/* add/update pg0 data */
	ri->flags &= ~MPT_RPORT_INFO_FLAGS_MISSING;

	/* MPT_RPORT_INFO_FLAGS_REGISTERED - rport not previously deleted */
	if (!(ri->flags & MPT_RPORT_INFO_FLAGS_REGISTERED)) {
		ri->flags |= MPT_RPORT_INFO_FLAGS_REGISTERED;
		rport = fc_remote_port_add(ioc->sh, channel, &rport_ids);
		if (rport) {
			ri->rport = rport;
			if (new_ri) /* may have been reset by user */
				rport->dev_loss_tmo = mptfc_dev_loss_tmo;
			/*
			 * if already mapped, remap here.  If not mapped,
			 * target_alloc will allocate vtarget and map,
			 * slave_alloc will fill in vdev from vtarget.
			 */
			if (ri->starget) {
				vtarget = ri->starget->hostdata;
				if (vtarget) {
					vtarget->target_id = pg0->CurrentTargetID;
					vtarget->bus_id = pg0->CurrentBus;
				}
			}
			*((struct mptfc_rport_info **)rport->dd_data) = ri;
			/* scan will be scheduled once rport becomes a target */
			fc_remote_port_rolechg(rport,roles);

			pn = (u64)ri->pg0.WWPN.High << 32 | (u64)ri->pg0.WWPN.Low;
			nn = (u64)ri->pg0.WWNN.High << 32 | (u64)ri->pg0.WWNN.Low;
			dfcprintk ((MYIOC_s_INFO_FMT
				"mptfc_reg_dev.%d: %x, %llx / %llx, tid %d, "
				"rport tid %d, tmo %d\n",
					ioc->name,
					ioc->sh->host_no,
					pg0->PortIdentifier,
					(unsigned long long)nn,
					(unsigned long long)pn,
					pg0->CurrentTargetID,
					ri->rport->scsi_target_id,
					ri->rport->dev_loss_tmo));
		} else {
			list_del(&ri->list);
			kfree(ri);
			ri = NULL;
		}
	}
}

/*
 *	OS entry point to allow for host driver to free allocated memory
 *	Called if no device present or device being unloaded
 */
static void
mptfc_target_destroy(struct scsi_target *starget)
{
	struct fc_rport		*rport;
	struct mptfc_rport_info *ri;

	rport = starget_to_rport(starget);
	if (rport) {
		ri = *((struct mptfc_rport_info **)rport->dd_data);
		if (ri)	/* better be! */
			ri->starget = NULL;
	}
	if (starget->hostdata)
		kfree(starget->hostdata);
	starget->hostdata = NULL;
}

/*
 *	OS entry point to allow host driver to alloc memory
 *	for each scsi target. Called once per device the bus scan.
 *	Return non-zero if allocation fails.
 */
static int
mptfc_target_alloc(struct scsi_target *starget)
{
	VirtTarget		*vtarget;
	struct fc_rport		*rport;
	struct mptfc_rport_info *ri;
	int			rc;

	vtarget = kzalloc(sizeof(VirtTarget), GFP_KERNEL);
	if (!vtarget)
		return -ENOMEM;
	starget->hostdata = vtarget;

	rc = -ENODEV;
	rport = starget_to_rport(starget);
	if (rport) {
		ri = *((struct mptfc_rport_info **)rport->dd_data);
		if (ri) {	/* better be! */
			vtarget->target_id = ri->pg0.CurrentTargetID;
			vtarget->bus_id = ri->pg0.CurrentBus;
			ri->starget = starget;
			rc = 0;
		}
	}
	if (rc != 0) {
		kfree(vtarget);
		starget->hostdata = NULL;
	}

	return rc;
}

/*
 *	OS entry point to allow host driver to alloc memory
 *	for each scsi device. Called once per device the bus scan.
 *	Return non-zero if allocation fails.
 *	Init memory once per LUN.
 */
static int
mptfc_slave_alloc(struct scsi_device *sdev)
{
	MPT_SCSI_HOST		*hd;
	VirtTarget		*vtarget;
	VirtDevice		*vdev;
	struct scsi_target	*starget;
	struct fc_rport		*rport;


	starget = scsi_target(sdev);
	rport = starget_to_rport(starget);

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	hd = (MPT_SCSI_HOST *)sdev->host->hostdata;

	vdev = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdev) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kmalloc(%zd) FAILED!\n",
				hd->ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}


	sdev->hostdata = vdev;
	vtarget = starget->hostdata;

	if (vtarget->num_luns == 0) {
		vtarget->ioc_id = hd->ioc->id;
		vtarget->tflags = MPT_TARGET_FLAGS_Q_YES;
		hd->Targets[sdev->id] = vtarget;
	}

	vdev->vtarget = vtarget;
	vdev->lun = sdev->lun;

	vtarget->num_luns++;


#ifdef DMPT_DEBUG_FC
	{
	u64 nn, pn;
	struct mptfc_rport_info *ri;
	ri = *((struct mptfc_rport_info **)rport->dd_data);
	pn = (u64)ri->pg0.WWPN.High << 32 | (u64)ri->pg0.WWPN.Low;
	nn = (u64)ri->pg0.WWNN.High << 32 | (u64)ri->pg0.WWNN.Low;
	dfcprintk ((MYIOC_s_INFO_FMT
		"mptfc_slv_alloc.%d: num_luns %d, sdev.id %d, "
	        "CurrentTargetID %d, %x %llx %llx\n",
		hd->ioc->name,
		sdev->host->host_no,
		vtarget->num_luns,
		sdev->id, ri->pg0.CurrentTargetID,
		ri->pg0.PortIdentifier,
		(unsigned long long)pn,
		(unsigned long long)nn));
	}
#endif

	return 0;
}

static int
mptfc_qcmd(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	struct mptfc_rport_info	*ri;
	struct fc_rport	*rport = starget_to_rport(scsi_target(SCpnt->device));
	int		err;

	err = fc_remote_port_chkready(rport);
	if (unlikely(err)) {
		SCpnt->result = err;
		done(SCpnt);
		return 0;
	}

	if (!SCpnt->device->hostdata) {	/* vdev */
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}

	/* dd_data is null until finished adding target */
	ri = *((struct mptfc_rport_info **)rport->dd_data);
	if (unlikely(!ri)) {
		dfcprintk ((MYIOC_s_INFO_FMT
			"mptfc_qcmd.%d: %d:%d, dd_data is null.\n",
			((MPT_SCSI_HOST *) SCpnt->device->host->hostdata)->ioc->name,
			((MPT_SCSI_HOST *) SCpnt->device->host->hostdata)->ioc->sh->host_no,
			SCpnt->device->id,SCpnt->device->lun));
		SCpnt->result = DID_IMM_RETRY << 16;
		done(SCpnt);
		return 0;
	}

	err = mptscsih_qcmd(SCpnt,done);
#ifdef DMPT_DEBUG_FC
	if (unlikely(err)) {
		dfcprintk ((MYIOC_s_INFO_FMT
			"mptfc_qcmd.%d: %d:%d, mptscsih_qcmd returns non-zero, (%x).\n",
			((MPT_SCSI_HOST *) SCpnt->device->host->hostdata)->ioc->name,
			((MPT_SCSI_HOST *) SCpnt->device->host->hostdata)->ioc->sh->host_no,
			SCpnt->device->id,SCpnt->device->lun,err));
	}
#endif
	return err;
}

/*
 *	mptfc_GetFcPortPage0 - Fetch FCPort config Page0.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@portnum: IOC Port number
 *
 *	Return: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 *		-EINVAL portnum arg out of range (hardwired to two elements)
 */
static int
mptfc_GetFcPortPage0(MPT_ADAPTER *ioc, int portnum)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	FCPortPage0_t		*ppage0_alloc;
	FCPortPage0_t		*pp0dest;
	dma_addr_t		 page0_dma;
	int			 data_sz;
	int			 copy_sz;
	int			 rc;
	int			 count = 400;

	if (portnum > 1)
		return -EINVAL;

	/* Get FCPort Page 0 header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_FC_PORT;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = portnum;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength == 0)
		return 0;

	data_sz = hdr.PageLength * 4;
	rc = -ENOMEM;
	ppage0_alloc = (FCPortPage0_t *) pci_alloc_consistent(ioc->pcidev, data_sz, &page0_dma);
	if (ppage0_alloc) {

 try_again:
		memset((u8 *)ppage0_alloc, 0, data_sz);
		cfg.physAddr = page0_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		if ((rc = mpt_config(ioc, &cfg)) == 0) {
			/* save the data */
			pp0dest = &ioc->fc_port_page0[portnum];
			copy_sz = min_t(int, sizeof(FCPortPage0_t), data_sz);
			memcpy(pp0dest, ppage0_alloc, copy_sz);

			/*
			 *	Normalize endianness of structure data,
			 *	by byte-swapping all > 1 byte fields!
			 */
			pp0dest->Flags = le32_to_cpu(pp0dest->Flags);
			pp0dest->PortIdentifier = le32_to_cpu(pp0dest->PortIdentifier);
			pp0dest->WWNN.Low = le32_to_cpu(pp0dest->WWNN.Low);
			pp0dest->WWNN.High = le32_to_cpu(pp0dest->WWNN.High);
			pp0dest->WWPN.Low = le32_to_cpu(pp0dest->WWPN.Low);
			pp0dest->WWPN.High = le32_to_cpu(pp0dest->WWPN.High);
			pp0dest->SupportedServiceClass = le32_to_cpu(pp0dest->SupportedServiceClass);
			pp0dest->SupportedSpeeds = le32_to_cpu(pp0dest->SupportedSpeeds);
			pp0dest->CurrentSpeed = le32_to_cpu(pp0dest->CurrentSpeed);
			pp0dest->MaxFrameSize = le32_to_cpu(pp0dest->MaxFrameSize);
			pp0dest->FabricWWNN.Low = le32_to_cpu(pp0dest->FabricWWNN.Low);
			pp0dest->FabricWWNN.High = le32_to_cpu(pp0dest->FabricWWNN.High);
			pp0dest->FabricWWPN.Low = le32_to_cpu(pp0dest->FabricWWPN.Low);
			pp0dest->FabricWWPN.High = le32_to_cpu(pp0dest->FabricWWPN.High);
			pp0dest->DiscoveredPortsCount = le32_to_cpu(pp0dest->DiscoveredPortsCount);
			pp0dest->MaxInitiators = le32_to_cpu(pp0dest->MaxInitiators);

			/*
			 * if still doing discovery,
			 * hang loose a while until finished
			 */
			if ((pp0dest->PortState == MPI_FCPORTPAGE0_PORTSTATE_UNKNOWN) ||
			    (pp0dest->PortState == MPI_FCPORTPAGE0_PORTSTATE_ONLINE &&
			     (pp0dest->Flags & MPI_FCPORTPAGE0_FLAGS_ATTACH_TYPE_MASK)
			      == MPI_FCPORTPAGE0_FLAGS_ATTACH_NO_INIT)) {
				if (count-- > 0) {
					msleep(100);
					goto try_again;
				}
				printk(MYIOC_s_INFO_FMT "Firmware discovery not"
							" complete.\n",
						ioc->name);
			}
		}

		pci_free_consistent(ioc->pcidev, data_sz, (u8 *) ppage0_alloc, page0_dma);
	}

	return rc;
}

static int
mptfc_WriteFcPortPage1(MPT_ADAPTER *ioc, int portnum)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	int			 rc;

	if (portnum > 1)
		return -EINVAL;

	if (!(ioc->fc_data.fc_port_page1[portnum].data))
		return -EINVAL;

	/* get fcport page 1 header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 1;
	hdr.PageType = MPI_CONFIG_PAGETYPE_FC_PORT;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = portnum;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength == 0)
		return -ENODEV;

	if (hdr.PageLength*4 != ioc->fc_data.fc_port_page1[portnum].pg_sz)
		return -EINVAL;

	cfg.physAddr = ioc->fc_data.fc_port_page1[portnum].dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	cfg.dir = 1;

	rc = mpt_config(ioc, &cfg);

	return rc;
}

static int
mptfc_GetFcPortPage1(MPT_ADAPTER *ioc, int portnum)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	FCPortPage1_t		*page1_alloc;
	dma_addr_t		 page1_dma;
	int			 data_sz;
	int			 rc;

	if (portnum > 1)
		return -EINVAL;

	/* get fcport page 1 header */
	hdr.PageVersion = 0;
	hdr.PageLength = 0;
	hdr.PageNumber = 1;
	hdr.PageType = MPI_CONFIG_PAGETYPE_FC_PORT;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = portnum;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		return rc;

	if (hdr.PageLength == 0)
		return -ENODEV;

start_over:

	if (ioc->fc_data.fc_port_page1[portnum].data == NULL) {
		data_sz = hdr.PageLength * 4;
		if (data_sz < sizeof(FCPortPage1_t))
			data_sz = sizeof(FCPortPage1_t);

		page1_alloc = (FCPortPage1_t *) pci_alloc_consistent(ioc->pcidev,
						data_sz,
						&page1_dma);
		if (!page1_alloc)
			return -ENOMEM;
	}
	else {
		page1_alloc = ioc->fc_data.fc_port_page1[portnum].data;
		page1_dma = ioc->fc_data.fc_port_page1[portnum].dma;
		data_sz = ioc->fc_data.fc_port_page1[portnum].pg_sz;
		if (hdr.PageLength * 4 > data_sz) {
			ioc->fc_data.fc_port_page1[portnum].data = NULL;
			pci_free_consistent(ioc->pcidev, data_sz, (u8 *)
				page1_alloc, page1_dma);
			goto start_over;
		}
	}

	memset(page1_alloc,0,data_sz);

	cfg.physAddr = page1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) == 0) {
		ioc->fc_data.fc_port_page1[portnum].data = page1_alloc;
		ioc->fc_data.fc_port_page1[portnum].pg_sz = data_sz;
		ioc->fc_data.fc_port_page1[portnum].dma = page1_dma;
	}
	else {
		ioc->fc_data.fc_port_page1[portnum].data = NULL;
		pci_free_consistent(ioc->pcidev, data_sz, (u8 *)
			page1_alloc, page1_dma);
	}

	return rc;
}

static void
mptfc_SetFcPortPage1_defaults(MPT_ADAPTER *ioc)
{
	int		ii;
	FCPortPage1_t	*pp1;

	#define MPTFC_FW_DEVICE_TIMEOUT	(1)
	#define MPTFC_FW_IO_PEND_TIMEOUT (1)
	#define ON_FLAGS  (MPI_FCPORTPAGE1_FLAGS_IMMEDIATE_ERROR_REPLY)
	#define OFF_FLAGS (MPI_FCPORTPAGE1_FLAGS_VERBOSE_RESCAN_EVENTS)

	for (ii=0; ii<ioc->facts.NumberOfPorts; ii++) {
		if (mptfc_GetFcPortPage1(ioc, ii) != 0)
			continue;
		pp1 = ioc->fc_data.fc_port_page1[ii].data;
		if ((pp1->InitiatorDeviceTimeout == MPTFC_FW_DEVICE_TIMEOUT)
		 && (pp1->InitiatorIoPendTimeout == MPTFC_FW_IO_PEND_TIMEOUT)
		 && ((pp1->Flags & ON_FLAGS) == ON_FLAGS)
		 && ((pp1->Flags & OFF_FLAGS) == 0))
			continue;
		pp1->InitiatorDeviceTimeout = MPTFC_FW_DEVICE_TIMEOUT;
		pp1->InitiatorIoPendTimeout = MPTFC_FW_IO_PEND_TIMEOUT;
		pp1->Flags &= ~OFF_FLAGS;
		pp1->Flags |= ON_FLAGS;
		mptfc_WriteFcPortPage1(ioc, ii);
	}
}


static void
mptfc_init_host_attr(MPT_ADAPTER *ioc,int portnum)
{
	unsigned	class = 0;
	unsigned	cos = 0;
	unsigned	speed;
	unsigned	port_type;
	unsigned	port_state;
	FCPortPage0_t	*pp0;
	struct Scsi_Host *sh;
	char		*sn;

	/* don't know what to do as only one scsi (fc) host was allocated */
	if (portnum != 0)
		return;

	pp0 = &ioc->fc_port_page0[portnum];
	sh = ioc->sh;

	sn = fc_host_symbolic_name(sh);
	snprintf(sn, FC_SYMBOLIC_NAME_SIZE, "%s %s%08xh",
	    ioc->prod_name,
	    MPT_FW_REV_MAGIC_ID_STRING,
	    ioc->facts.FWVersion.Word);

	fc_host_tgtid_bind_type(sh) = FC_TGTID_BIND_BY_WWPN;

	fc_host_maxframe_size(sh) = pp0->MaxFrameSize;

	fc_host_node_name(sh) =
	    	(u64)pp0->WWNN.High << 32 | (u64)pp0->WWNN.Low;

	fc_host_port_name(sh) =
	    	(u64)pp0->WWPN.High << 32 | (u64)pp0->WWPN.Low;

	fc_host_port_id(sh) = pp0->PortIdentifier;

	class = pp0->SupportedServiceClass;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_1)
		cos |= FC_COS_CLASS1;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_2)
		cos |= FC_COS_CLASS2;
	if (class & MPI_FCPORTPAGE0_SUPPORT_CLASS_3)
		cos |= FC_COS_CLASS3;
	fc_host_supported_classes(sh) = cos;

	if (pp0->CurrentSpeed == MPI_FCPORTPAGE0_CURRENT_SPEED_1GBIT)
		speed = FC_PORTSPEED_1GBIT;
	else if (pp0->CurrentSpeed == MPI_FCPORTPAGE0_CURRENT_SPEED_2GBIT)
		speed = FC_PORTSPEED_2GBIT;
	else if (pp0->CurrentSpeed == MPI_FCPORTPAGE0_CURRENT_SPEED_4GBIT)
		speed = FC_PORTSPEED_4GBIT;
	else if (pp0->CurrentSpeed == MPI_FCPORTPAGE0_CURRENT_SPEED_10GBIT)
		speed = FC_PORTSPEED_10GBIT;
	else
		speed = FC_PORTSPEED_UNKNOWN;
	fc_host_speed(sh) = speed;

	speed = 0;
	if (pp0->SupportedSpeeds & MPI_FCPORTPAGE0_SUPPORT_1GBIT_SPEED)
		speed |= FC_PORTSPEED_1GBIT;
	if (pp0->SupportedSpeeds & MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED)
		speed |= FC_PORTSPEED_2GBIT;
	if (pp0->SupportedSpeeds & MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED)
		speed |= FC_PORTSPEED_4GBIT;
	if (pp0->SupportedSpeeds & MPI_FCPORTPAGE0_SUPPORT_10GBIT_SPEED)
		speed |= FC_PORTSPEED_10GBIT;
	fc_host_supported_speeds(sh) = speed;

	port_state = FC_PORTSTATE_UNKNOWN;
	if (pp0->PortState == MPI_FCPORTPAGE0_PORTSTATE_ONLINE)
		port_state = FC_PORTSTATE_ONLINE;
	else if (pp0->PortState == MPI_FCPORTPAGE0_PORTSTATE_OFFLINE)
		port_state = FC_PORTSTATE_LINKDOWN;
	fc_host_port_state(sh) = port_state;

	port_type = FC_PORTTYPE_UNKNOWN;
	if (pp0->Flags & MPI_FCPORTPAGE0_FLAGS_ATTACH_POINT_TO_POINT)
		port_type = FC_PORTTYPE_PTP;
	else if (pp0->Flags & MPI_FCPORTPAGE0_FLAGS_ATTACH_PRIVATE_LOOP)
		port_type = FC_PORTTYPE_LPORT;
	else if (pp0->Flags & MPI_FCPORTPAGE0_FLAGS_ATTACH_PUBLIC_LOOP)
		port_type = FC_PORTTYPE_NLPORT;
	else if (pp0->Flags & MPI_FCPORTPAGE0_FLAGS_ATTACH_FABRIC_DIRECT)
		port_type = FC_PORTTYPE_NPORT;
	fc_host_port_type(sh) = port_type;

	fc_host_fabric_name(sh) =
	    (pp0->Flags & MPI_FCPORTPAGE0_FLAGS_FABRIC_WWN_VALID) ?
		(u64) pp0->FabricWWNN.High << 32 | (u64) pp0->FabricWWPN.Low :
		(u64)pp0->WWNN.High << 32 | (u64)pp0->WWNN.Low;

}

static void
mptfc_setup_reset(void *arg)
{
	MPT_ADAPTER		*ioc = (MPT_ADAPTER *)arg;
	u64			pn;
	struct mptfc_rport_info *ri;

	/* reset about to happen, delete (block) all rports */
	list_for_each_entry(ri, &ioc->fc_rports, list) {
		if (ri->flags & MPT_RPORT_INFO_FLAGS_REGISTERED) {
			ri->flags &= ~MPT_RPORT_INFO_FLAGS_REGISTERED;
			fc_remote_port_delete(ri->rport);	/* won't sleep */
			ri->rport = NULL;

			pn = (u64)ri->pg0.WWPN.High << 32 |
			     (u64)ri->pg0.WWPN.Low;
			dfcprintk ((MYIOC_s_INFO_FMT
				"mptfc_setup_reset.%d: %llx deleted\n",
				ioc->name,
				ioc->sh->host_no,
				(unsigned long long)pn));
		}
	}
}

static void
mptfc_rescan_devices(void *arg)
{
	MPT_ADAPTER		*ioc = (MPT_ADAPTER *)arg;
	int			ii;
	u64			pn;
	struct mptfc_rport_info *ri;

	/* start by tagging all ports as missing */
	list_for_each_entry(ri, &ioc->fc_rports, list) {
		if (ri->flags & MPT_RPORT_INFO_FLAGS_REGISTERED) {
			ri->flags |= MPT_RPORT_INFO_FLAGS_MISSING;
		}
	}

	/*
	 * now rescan devices known to adapter,
	 * will reregister existing rports
	 */
	for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
		(void) mptfc_GetFcPortPage0(ioc, ii);
		mptfc_init_host_attr(ioc, ii);	/* refresh */
		mptfc_GetFcDevPage0(ioc, ii, mptfc_register_dev);
	}

	/* delete devices still missing */
	list_for_each_entry(ri, &ioc->fc_rports, list) {
		/* if newly missing, delete it */
		if (ri->flags & MPT_RPORT_INFO_FLAGS_MISSING) {

			ri->flags &= ~(MPT_RPORT_INFO_FLAGS_REGISTERED|
				       MPT_RPORT_INFO_FLAGS_MISSING);
			fc_remote_port_delete(ri->rport);	/* won't sleep */
			ri->rport = NULL;

			pn = (u64)ri->pg0.WWPN.High << 32 |
			     (u64)ri->pg0.WWPN.Low;
			dfcprintk ((MYIOC_s_INFO_FMT
				"mptfc_rescan.%d: %llx deleted\n",
				ioc->name,
				ioc->sh->host_no,
				(unsigned long long)pn));
		}
	}
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

	spin_lock_init(&ioc->fc_rescan_work_lock);
	INIT_WORK(&ioc->fc_rescan_work, mptfc_rescan_devices,(void *)ioc);
	INIT_WORK(&ioc->fc_setup_reset_work, mptfc_setup_reset, (void *)ioc);

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

	/* initialize workqueue */

	snprintf(ioc->fc_rescan_work_q_name, KOBJ_NAME_LEN, "mptfc_wq_%d",
		sh->host_no);
	ioc->fc_rescan_work_q =
		create_singlethread_workqueue(ioc->fc_rescan_work_q_name);
	if (!ioc->fc_rescan_work_q)
		goto out_mptfc_probe;

	/*
	 *  Pre-fetch FC port WWN and stuff...
	 *  (FCPortPage0_t stuff)
	 */
	for (ii=0; ii < ioc->facts.NumberOfPorts; ii++) {
		(void) mptfc_GetFcPortPage0(ioc, ii);
	}
	mptfc_SetFcPortPage1_defaults(ioc);

	/*
	 * scan for rports -
	 *	by doing it via the workqueue, some locking is eliminated
	 */

	queue_work(ioc->fc_rescan_work_q, &ioc->fc_rescan_work);
	flush_workqueue(ioc->fc_rescan_work_q);

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

static int
mptfc_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply)
{
	MPT_SCSI_HOST *hd;
	u8 event = le32_to_cpu(pEvReply->Event) & 0xFF;
	unsigned long flags;
	int rc=1;

	devtverboseprintk((MYIOC_s_INFO_FMT "MPT event (=%02Xh) routed to SCSI host driver!\n",
			ioc->name, event));

	if (ioc->sh == NULL ||
		((hd = (MPT_SCSI_HOST *)ioc->sh->hostdata) == NULL))
		return 1;

	switch (event) {
	case MPI_EVENT_RESCAN:
		spin_lock_irqsave(&ioc->fc_rescan_work_lock, flags);
		if (ioc->fc_rescan_work_q) {
			queue_work(ioc->fc_rescan_work_q,
				   &ioc->fc_rescan_work);
		}
		spin_unlock_irqrestore(&ioc->fc_rescan_work_lock, flags);
		break;
	default:
		rc = mptscsih_event_process(ioc,pEvReply);
		break;
	}
	return rc;
}

static int
mptfc_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	int		rc;
	unsigned long	flags;

	rc = mptscsih_ioc_reset(ioc,reset_phase);
	if (rc == 0)
		return rc;


	dtmprintk((KERN_WARNING MYNAM
		": IOC %s_reset routed to FC host driver!\n",
		reset_phase==MPT_IOC_SETUP_RESET ? "setup" : (
		reset_phase==MPT_IOC_PRE_RESET ? "pre" : "post")));

	if (reset_phase == MPT_IOC_SETUP_RESET) {
		spin_lock_irqsave(&ioc->fc_rescan_work_lock, flags);
		if (ioc->fc_rescan_work_q) {
			queue_work(ioc->fc_rescan_work_q,
				   &ioc->fc_setup_reset_work);
		}
		spin_unlock_irqrestore(&ioc->fc_rescan_work_lock, flags);
	}

	else if (reset_phase == MPT_IOC_PRE_RESET) {
	}

	else {	/* MPT_IOC_POST_RESET */
		mptfc_SetFcPortPage1_defaults(ioc);
		spin_lock_irqsave(&ioc->fc_rescan_work_lock, flags);
		if (ioc->fc_rescan_work_q) {
			queue_work(ioc->fc_rescan_work_q,
				   &ioc->fc_rescan_work);
		}
		spin_unlock_irqrestore(&ioc->fc_rescan_work_lock, flags);
	}
	return 1;
}

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

	/* sanity check module parameters */
	if (mptfc_dev_loss_tmo <= 0)
		mptfc_dev_loss_tmo = MPTFC_DEV_LOSS_TMO;

	mptfc_transport_template =
		fc_attach_transport(&mptfc_transport_functions);

	if (!mptfc_transport_template)
		return -ENODEV;

	mptfcDoneCtx = mpt_register(mptscsih_io_done, MPTFC_DRIVER);
	mptfcTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTFC_DRIVER);
	mptfcInternalCtx = mpt_register(mptscsih_scandv_complete, MPTFC_DRIVER);

	if (mpt_event_register(mptfcDoneCtx, mptfc_event_process) == 0) {
		devtverboseprintk((KERN_INFO MYNAM
		  ": Registered for IOC event notifications\n"));
	}

	if (mpt_reset_register(mptfcDoneCtx, mptfc_ioc_reset) == 0) {
		dprintk((KERN_INFO MYNAM
		  ": Registered for IOC reset notifications\n"));
	}

	error = pci_register_driver(&mptfc_driver);
	if (error)
		fc_release_transport(mptfc_transport_template);

	return error;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptfc_remove - Removed fc infrastructure for devices
 *	@pdev: Pointer to pci_dev structure
 *
 */
static void __devexit
mptfc_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER		*ioc = pci_get_drvdata(pdev);
	struct mptfc_rport_info	*p, *n;
	struct workqueue_struct *work_q;
	unsigned long		flags;
	int			ii;

	/* destroy workqueue */
	if ((work_q=ioc->fc_rescan_work_q)) {
		spin_lock_irqsave(&ioc->fc_rescan_work_lock, flags);
		ioc->fc_rescan_work_q = NULL;
		spin_unlock_irqrestore(&ioc->fc_rescan_work_lock, flags);
		destroy_workqueue(work_q);
	}

	fc_remove_host(ioc->sh);

	list_for_each_entry_safe(p, n, &ioc->fc_rports, list) {
		list_del(&p->list);
		kfree(p);
	}

	for (ii=0; ii<ioc->facts.NumberOfPorts; ii++) {
		if (ioc->fc_data.fc_port_page1[ii].data) {
			pci_free_consistent(ioc->pcidev,
				ioc->fc_data.fc_port_page1[ii].pg_sz,
				(u8 *) ioc->fc_data.fc_port_page1[ii].data,
				ioc->fc_data.fc_port_page1[ii].dma);
			ioc->fc_data.fc_port_page1[ii].data = NULL;
		}
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

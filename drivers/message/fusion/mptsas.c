/*
 *  linux/drivers/message/fusion/mptsas.c
 *      For use with LSI Logic PCI chip/adapter(s)
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2005 LSI Logic Corporation
 *  (mailto:mpt_linux_developer@lsil.com)
 *  Copyright (c) 2005-2006 Dell
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_sas.h>

#include "mptbase.h"
#include "mptscsih.h"


#define my_NAME		"Fusion MPT SAS Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptsas"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");

static int mpt_pq_filter;
module_param(mpt_pq_filter, int, 0);
MODULE_PARM_DESC(mpt_pq_filter,
		"Enable peripheral qualifier filter: enable=1  "
		"(default=0)");

static int mpt_pt_clear;
module_param(mpt_pt_clear, int, 0);
MODULE_PARM_DESC(mpt_pt_clear,
		"Clear persistency table: enable=1  "
		"(default=MPTSCSIH_PT_CLEAR=0)");

static int	mptsasDoneCtx = -1;
static int	mptsasTaskCtx = -1;
static int	mptsasInternalCtx = -1; /* Used only for internal commands */
static int	mptsasMgmtCtx = -1;


enum mptsas_hotplug_action {
	MPTSAS_ADD_DEVICE,
	MPTSAS_DEL_DEVICE,
	MPTSAS_ADD_RAID,
	MPTSAS_DEL_RAID,
	MPTSAS_IGNORE_EVENT,
};

struct mptsas_hotplug_event {
	struct work_struct	work;
	MPT_ADAPTER		*ioc;
	enum mptsas_hotplug_action event_type;
	u64			sas_address;
	u32			channel;
	u32			id;
	u32			device_info;
	u16			handle;
	u16			parent_handle;
	u8			phy_id;
	u8			phys_disk_num;
	u8			phys_disk_num_valid;
};

struct mptsas_discovery_event {
	struct work_struct	work;
	MPT_ADAPTER		*ioc;
};

/*
 * SAS topology structures
 *
 * The MPT Fusion firmware interface spreads information about the
 * SAS topology over many manufacture pages, thus we need some data
 * structure to collect it and process it for the SAS transport class.
 */

struct mptsas_devinfo {
	u16	handle;		/* unique id to address this device */
	u16	handle_parent;	/* unique id to address parent device */
	u16	handle_enclosure; /* enclosure identifier of the enclosure */
	u16	slot;		/* physical slot in enclosure */
	u8	phy_id;		/* phy number of parent device */
	u8	port_id;	/* sas physical port this device
				   is assoc'd with */
	u8	id;		/* logical target id of this device */
	u8	channel;	/* logical bus number of this device */
	u64	sas_address;    /* WWN of this device,
				   SATA is assigned by HBA,expander */
	u32	device_info;	/* bitfield detailed info about this device */
};

struct mptsas_phyinfo {
	u8	phy_id; 		/* phy index */
	u8	port_id; 		/* port number this phy is part of */
	u8	negotiated_link_rate;	/* nego'd link rate for this phy */
	u8	hw_link_rate; 		/* hardware max/min phys link rate */
	u8	programmed_link_rate;	/* programmed max/min phy link rate */
	struct mptsas_devinfo identify;	/* point to phy device info */
	struct mptsas_devinfo attached;	/* point to attached device info */
	struct sas_phy *phy;
	struct sas_rphy *rphy;
	struct scsi_target *starget;
};

struct mptsas_portinfo {
	struct list_head list;
	u16		handle;		/* unique id to address this */
	u8		num_phys;	/* number of phys */
	struct mptsas_phyinfo *phy_info;
};

struct mptsas_enclosure {
	u64	enclosure_logical_id;	/* The WWN for the enclosure */
	u16	enclosure_handle;	/* unique id to address this */
	u16	flags;			/* details enclosure management */
	u16	num_slot;		/* num slots */
	u16	start_slot;		/* first slot */
	u8	start_id;		/* starting logical target id */
	u8	start_channel;		/* starting logical channel id */
	u8	sep_id;			/* SEP device logical target id */
	u8	sep_channel;		/* SEP channel logical channel id */
};

#ifdef SASDEBUG
static void mptsas_print_phy_data(MPI_SAS_IO_UNIT0_PHY_DATA *phy_data)
{
	printk("---- IO UNIT PAGE 0 ------------\n");
	printk("Handle=0x%X\n",
		le16_to_cpu(phy_data->AttachedDeviceHandle));
	printk("Controller Handle=0x%X\n",
		le16_to_cpu(phy_data->ControllerDevHandle));
	printk("Port=0x%X\n", phy_data->Port);
	printk("Port Flags=0x%X\n", phy_data->PortFlags);
	printk("PHY Flags=0x%X\n", phy_data->PhyFlags);
	printk("Negotiated Link Rate=0x%X\n", phy_data->NegotiatedLinkRate);
	printk("Controller PHY Device Info=0x%X\n",
		le32_to_cpu(phy_data->ControllerPhyDeviceInfo));
	printk("DiscoveryStatus=0x%X\n",
		le32_to_cpu(phy_data->DiscoveryStatus));
	printk("\n");
}

static void mptsas_print_phy_pg0(SasPhyPage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	printk("---- SAS PHY PAGE 0 ------------\n");
	printk("Attached Device Handle=0x%X\n",
			le16_to_cpu(pg0->AttachedDevHandle));
	printk("SAS Address=0x%llX\n",
			(unsigned long long)le64_to_cpu(sas_address));
	printk("Attached PHY Identifier=0x%X\n", pg0->AttachedPhyIdentifier);
	printk("Attached Device Info=0x%X\n",
			le32_to_cpu(pg0->AttachedDeviceInfo));
	printk("Programmed Link Rate=0x%X\n", pg0->ProgrammedLinkRate);
	printk("Change Count=0x%X\n", pg0->ChangeCount);
	printk("PHY Info=0x%X\n", le32_to_cpu(pg0->PhyInfo));
	printk("\n");
}

static void mptsas_print_phy_pg1(SasPhyPage1_t *pg1)
{
	printk("---- SAS PHY PAGE 1 ------------\n");
	printk("Invalid Dword Count=0x%x\n", pg1->InvalidDwordCount);
	printk("Running Disparity Error Count=0x%x\n",
			pg1->RunningDisparityErrorCount);
	printk("Loss Dword Synch Count=0x%x\n", pg1->LossDwordSynchCount);
	printk("PHY Reset Problem Count=0x%x\n", pg1->PhyResetProblemCount);
	printk("\n");
}

static void mptsas_print_device_pg0(SasDevicePage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	printk("---- SAS DEVICE PAGE 0 ---------\n");
	printk("Handle=0x%X\n" ,le16_to_cpu(pg0->DevHandle));
	printk("Parent Handle=0x%X\n" ,le16_to_cpu(pg0->ParentDevHandle));
	printk("Enclosure Handle=0x%X\n", le16_to_cpu(pg0->EnclosureHandle));
	printk("Slot=0x%X\n", le16_to_cpu(pg0->Slot));
	printk("SAS Address=0x%llX\n", le64_to_cpu(sas_address));
	printk("Target ID=0x%X\n", pg0->TargetID);
	printk("Bus=0x%X\n", pg0->Bus);
	/* The PhyNum field specifies the PHY number of the parent
	 * device this device is linked to
	 */
	printk("Parent Phy Num=0x%X\n", pg0->PhyNum);
	printk("Access Status=0x%X\n", le16_to_cpu(pg0->AccessStatus));
	printk("Device Info=0x%X\n", le32_to_cpu(pg0->DeviceInfo));
	printk("Flags=0x%X\n", le16_to_cpu(pg0->Flags));
	printk("Physical Port=0x%X\n", pg0->PhysicalPort);
	printk("\n");
}

static void mptsas_print_expander_pg1(SasExpanderPage1_t *pg1)
{
	printk("---- SAS EXPANDER PAGE 1 ------------\n");

	printk("Physical Port=0x%X\n", pg1->PhysicalPort);
	printk("PHY Identifier=0x%X\n", pg1->PhyIdentifier);
	printk("Negotiated Link Rate=0x%X\n", pg1->NegotiatedLinkRate);
	printk("Programmed Link Rate=0x%X\n", pg1->ProgrammedLinkRate);
	printk("Hardware Link Rate=0x%X\n", pg1->HwLinkRate);
	printk("Owner Device Handle=0x%X\n",
			le16_to_cpu(pg1->OwnerDevHandle));
	printk("Attached Device Handle=0x%X\n",
			le16_to_cpu(pg1->AttachedDevHandle));
}
#else
#define mptsas_print_phy_data(phy_data)		do { } while (0)
#define mptsas_print_phy_pg0(pg0)		do { } while (0)
#define mptsas_print_phy_pg1(pg1)		do { } while (0)
#define mptsas_print_device_pg0(pg0)		do { } while (0)
#define mptsas_print_expander_pg1(pg1)		do { } while (0)
#endif

static inline MPT_ADAPTER *phy_to_ioc(struct sas_phy *phy)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	return ((MPT_SCSI_HOST *)shost->hostdata)->ioc;
}

static inline MPT_ADAPTER *rphy_to_ioc(struct sas_rphy *rphy)
{
	struct Scsi_Host *shost = dev_to_shost(rphy->dev.parent->parent);
	return ((MPT_SCSI_HOST *)shost->hostdata)->ioc;
}

/*
 * mptsas_find_portinfo_by_handle
 *
 * This function should be called with the sas_topology_mutex already held
 */
static struct mptsas_portinfo *
mptsas_find_portinfo_by_handle(MPT_ADAPTER *ioc, u16 handle)
{
	struct mptsas_portinfo *port_info, *rc=NULL;
	int i;

	list_for_each_entry(port_info, &ioc->sas_topology, list)
		for (i = 0; i < port_info->num_phys; i++)
			if (port_info->phy_info[i].identify.handle == handle) {
				rc = port_info;
				goto out;
			}
 out:
	return rc;
}

/*
 * Returns true if there is a scsi end device
 */
static inline int
mptsas_is_end_device(struct mptsas_devinfo * attached)
{
	if ((attached->handle) &&
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_END_DEVICE) &&
	    ((attached->device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

static int
mptsas_sas_enclosure_pg0(MPT_ADAPTER *ioc, struct mptsas_enclosure *enclosure,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasEnclosurePage0_t *buffer;
	dma_addr_t dma_handle;
	int error;
	__le64 le_identifier;

	memset(&hdr, 0, sizeof(hdr));
	hdr.PageVersion = MPI_SASENCLOSURE0_PAGEVERSION;
	hdr.PageNumber = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_ENCLOSURE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			&dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	/* save config data */
	memcpy(&le_identifier, &buffer->EnclosureLogicalID, sizeof(__le64));
	enclosure->enclosure_logical_id = le64_to_cpu(le_identifier);
	enclosure->enclosure_handle = le16_to_cpu(buffer->EnclosureHandle);
	enclosure->flags = le16_to_cpu(buffer->Flags);
	enclosure->num_slot = le16_to_cpu(buffer->NumSlots);
	enclosure->start_slot = le16_to_cpu(buffer->StartSlot);
	enclosure->start_id = buffer->StartTargetID;
	enclosure->start_channel = buffer->StartBus;
	enclosure->sep_id = buffer->SEPTargetID;
	enclosure->sep_channel = buffer->SEPBus;

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;

	/*
	 * RAID volumes placed beyond the last expected port.
	 * Ignore sending sas mode pages in that case..
	 */
	if (sdev->channel < hd->ioc->num_ports)
		sas_read_port_mode_page(sdev);

	return mptscsih_slave_configure(sdev);
}

/*
 * This is pretty ugly.  We will be able to seriously clean it up
 * once the DV code in mptscsih goes away and we can properly
 * implement ->target_alloc.
 */
static int
mptsas_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	VirtTarget		*vtarget;
	VirtDevice		*vdev;
	struct scsi_target 	*starget;
	u32			target_id;
	int i;

	vdev = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdev) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kmalloc(%zd) FAILED!\n",
				hd->ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}
	sdev->hostdata = vdev;
	starget = scsi_target(sdev);
	vtarget = starget->hostdata;
	vtarget->ioc_id = hd->ioc->id;
	vdev->vtarget = vtarget;
	if (vtarget->num_luns == 0) {
		vtarget->tflags = MPT_TARGET_FLAGS_Q_YES|MPT_TARGET_FLAGS_VALID_INQUIRY;
		hd->Targets[sdev->id] = vtarget;
	}

	/*
	  RAID volumes placed beyond the last expected port.
	*/
	if (sdev->channel == hd->ioc->num_ports) {
		target_id = sdev->id;
		vtarget->bus_id = 0;
		vdev->lun = 0;
		goto out;
	}

	rphy = dev_to_rphy(sdev->sdev_target->dev.parent);
	mutex_lock(&hd->ioc->sas_topology_mutex);
	list_for_each_entry(p, &hd->ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address ==
					rphy->identify.sas_address) {
				target_id = p->phy_info[i].attached.id;
				vtarget->bus_id = p->phy_info[i].attached.channel;
				vdev->lun = sdev->lun;
				p->phy_info[i].starget = sdev->sdev_target;
				/*
				 * Exposing hidden disk (RAID)
				 */
				if (mptscsih_is_phys_disk(hd->ioc, target_id)) {
					target_id = mptscsih_raid_id_to_num(hd,
							target_id);
					vdev->vtarget->tflags |=
					    MPT_TARGET_FLAGS_RAID_COMPONENT;
					sdev->no_uld_attach = 1;
				}
				mutex_unlock(&hd->ioc->sas_topology_mutex);
				goto out;
			}
		}
	}
	mutex_unlock(&hd->ioc->sas_topology_mutex);

	kfree(vdev);
	return -ENXIO;

 out:
	vtarget->target_id = target_id;
	vtarget->num_luns++;
	return 0;
}

static void
mptsas_slave_destroy(struct scsi_device *sdev)
{
	struct Scsi_Host *host = sdev->host;
	MPT_SCSI_HOST *hd = (MPT_SCSI_HOST *)host->hostdata;
	VirtDevice *vdev;

	/*
	 * Issue target reset to flush firmware outstanding commands.
	 */
	vdev = sdev->hostdata;
	if (vdev->configured_lun){
		if (mptscsih_TMHandler(hd,
		     MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
		     vdev->vtarget->bus_id,
		     vdev->vtarget->target_id,
		     0, 0, 5 /* 5 second timeout */)
		     < 0){

			/* The TM request failed!
			 * Fatal error case.
			 */
			printk(MYIOC_s_WARN_FMT
		       "Error processing TaskMgmt id=%d TARGET_RESET\n",
				hd->ioc->name,
				vdev->vtarget->target_id);

			hd->tmPending = 0;
			hd->tmState = TM_STATE_NONE;
		}
	}
	mptscsih_slave_destroy(sdev);
}

static struct scsi_host_template mptsas_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptsas",
	.proc_info			= mptscsih_proc_info,
	.name				= "MPT SPI Host",
	.info				= mptscsih_info,
	.queuecommand			= mptscsih_qcmd,
	.target_alloc			= mptscsih_target_alloc,
	.slave_alloc			= mptsas_slave_alloc,
	.slave_configure		= mptsas_slave_configure,
	.target_destroy			= mptscsih_target_destroy,
	.slave_destroy			= mptsas_slave_destroy,
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

static int mptsas_get_linkerrors(struct sas_phy *phy)
{
	MPT_ADAPTER *ioc = phy_to_ioc(phy);
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasPhyPage1_t *buffer;
	dma_addr_t dma_handle;
	int error;

	hdr.PageVersion = MPI_SASPHY1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1*/;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phy->identify.phy_identifier;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;    /* read */
	cfg.timeout = 10;

	error = mpt_config(ioc, &cfg);
	if (error)
		return error;
	if (!hdr.ExtPageLength)
		return -ENXIO;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer)
		return -ENOMEM;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg1(buffer);

	phy->invalid_dword_count = le32_to_cpu(buffer->InvalidDwordCount);
	phy->running_disparity_error_count =
		le32_to_cpu(buffer->RunningDisparityErrorCount);
	phy->loss_of_dword_sync_count =
		le32_to_cpu(buffer->LossDwordSynchCount);
	phy->phy_reset_problem_count =
		le32_to_cpu(buffer->PhyResetProblemCount);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
	return error;
}

static int mptsas_mgmt_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req,
		MPT_FRAME_HDR *reply)
{
	ioc->sas_mgmt.status |= MPT_SAS_MGMT_STATUS_COMMAND_GOOD;
	if (reply != NULL) {
		ioc->sas_mgmt.status |= MPT_SAS_MGMT_STATUS_RF_VALID;
		memcpy(ioc->sas_mgmt.reply, reply,
		    min(ioc->reply_sz, 4 * reply->u.reply.MsgLength));
	}
	complete(&ioc->sas_mgmt.done);
	return 1;
}

static int mptsas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	MPT_ADAPTER *ioc = phy_to_ioc(phy);
	SasIoUnitControlRequest_t *req;
	SasIoUnitControlReply_t *reply;
	MPT_FRAME_HDR *mf;
	MPIHeader_t *hdr;
	unsigned long timeleft;
	int error = -ERESTARTSYS;

	/* not implemented for expanders */
	if (phy->identify.target_port_protocols & SAS_PROTOCOL_SMP)
		return -ENXIO;

	if (mutex_lock_interruptible(&ioc->sas_mgmt.mutex))
		goto out;

	mf = mpt_get_msg_frame(mptsasMgmtCtx, ioc);
	if (!mf) {
		error = -ENOMEM;
		goto out_unlock;
	}

	hdr = (MPIHeader_t *) mf;
	req = (SasIoUnitControlRequest_t *)mf;
	memset(req, 0, sizeof(SasIoUnitControlRequest_t));
	req->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	req->MsgContext = hdr->MsgContext;
	req->Operation = hard_reset ?
		MPI_SAS_OP_PHY_HARD_RESET : MPI_SAS_OP_PHY_LINK_RESET;
	req->PhyNum = phy->identify.phy_identifier;

	mpt_put_msg_frame(mptsasMgmtCtx, ioc, mf);

	timeleft = wait_for_completion_timeout(&ioc->sas_mgmt.done,
			10 * HZ);
	if (!timeleft) {
		/* On timeout reset the board */
		mpt_free_msg_frame(ioc, mf);
		mpt_HardResetHandler(ioc, CAN_SLEEP);
		error = -ETIMEDOUT;
		goto out_unlock;
	}

	/* a reply frame is expected */
	if ((ioc->sas_mgmt.status &
	    MPT_IOCTL_STATUS_RF_VALID) == 0) {
		error = -ENXIO;
		goto out_unlock;
	}

	/* process the completed Reply Message Frame */
	reply = (SasIoUnitControlReply_t *)ioc->sas_mgmt.reply;
	if (reply->IOCStatus != MPI_IOCSTATUS_SUCCESS) {
		printk("%s: IOCStatus=0x%X IOCLogInfo=0x%X\n",
		    __FUNCTION__,
		    reply->IOCStatus,
		    reply->IOCLogInfo);
		error = -ENXIO;
		goto out_unlock;
	}

	error = 0;

 out_unlock:
	mutex_unlock(&ioc->sas_mgmt.mutex);
 out:
	return error;
}

static int
mptsas_get_enclosure_identifier(struct sas_rphy *rphy, u64 *identifier)
{
	MPT_ADAPTER *ioc = rphy_to_ioc(rphy);
	int i, error;
	struct mptsas_portinfo *p;
	struct mptsas_enclosure enclosure_info;
	u64 enclosure_handle;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address ==
			    rphy->identify.sas_address) {
				enclosure_handle = p->phy_info[i].
					attached.handle_enclosure;
				goto found_info;
			}
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return -ENXIO;

 found_info:
	mutex_unlock(&ioc->sas_topology_mutex);
	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	error = mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
			(MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
			 MPI_SAS_ENCLOS_PGAD_FORM_SHIFT), enclosure_handle);
	if (!error)
		*identifier = enclosure_info.enclosure_logical_id;
	return error;
}

static int
mptsas_get_bay_identifier(struct sas_rphy *rphy)
{
	MPT_ADAPTER *ioc = rphy_to_ioc(rphy);
	struct mptsas_portinfo *p;
	int i, rc;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address ==
			    rphy->identify.sas_address) {
				rc = p->phy_info[i].attached.slot;
				goto out;
			}
		}
	}
	rc = -ENXIO;
 out:
	mutex_unlock(&ioc->sas_topology_mutex);
	return rc;
}

static struct sas_function_template mptsas_transport_functions = {
	.get_linkerrors		= mptsas_get_linkerrors,
	.get_enclosure_identifier = mptsas_get_enclosure_identifier,
	.get_bay_identifier	= mptsas_get_bay_identifier,
	.phy_reset		= mptsas_phy_reset,
};

static struct scsi_transport_template *mptsas_transport_template;

static int
mptsas_sas_io_unit_pg0(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasIOUnitPage0_t *buffer;
	dma_addr_t dma_handle;
	int error, i;

	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
					    &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	port_info->num_phys = buffer->NumPhys;
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(struct mptsas_phyinfo),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	if (port_info->num_phys)
		port_info->handle =
		    le16_to_cpu(buffer->PhyData[0].ControllerDevHandle);
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_print_phy_data(&buffer->PhyData[i]);
		port_info->phy_info[i].phy_id = i;
		port_info->phy_info[i].port_id =
		    buffer->PhyData[i].Port;
		port_info->phy_info[i].negotiated_link_rate =
		    buffer->PhyData[i].NegotiatedLinkRate;
	}

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_sas_phy_pg0(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasPhyPage0_t *buffer;
	dma_addr_t dma_handle;
	int error;

	hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	/* Get Phy Pg 0 for each Phy. */
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg0(buffer);

	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_sas_device_pg0(MPT_ADAPTER *ioc, struct mptsas_devinfo *device_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasDevicePage0_t *buffer;
	dma_addr_t dma_handle;
	__le64 sas_address;
	int error=0;

	if (ioc->sas_discovery_runtime &&
		mptsas_is_end_device(device_info))
			goto out;

	hdr.PageVersion = MPI_SASDEVICE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.pageAddr = form + form_specific;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	memset(device_info, 0, sizeof(struct mptsas_devinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_device_pg0(buffer);

	device_info->handle = le16_to_cpu(buffer->DevHandle);
	device_info->handle_parent = le16_to_cpu(buffer->ParentDevHandle);
	device_info->handle_enclosure =
	    le16_to_cpu(buffer->EnclosureHandle);
	device_info->slot = le16_to_cpu(buffer->Slot);
	device_info->phy_id = buffer->PhyNum;
	device_info->port_id = buffer->PhysicalPort;
	device_info->id = buffer->TargetID;
	device_info->channel = buffer->Bus;
	memcpy(&sas_address, &buffer->SASAddress, sizeof(__le64));
	device_info->sas_address = le64_to_cpu(sas_address);
	device_info->device_info =
	    le32_to_cpu(buffer->DeviceInfo);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_sas_expander_pg0(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasExpanderPage0_t *buffer;
	dma_addr_t dma_handle;
	int error;

	hdr.PageVersion = MPI_SASEXPANDER0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	memset(port_info, 0, sizeof(struct mptsas_portinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	/* save config data */
	port_info->num_phys = buffer->NumPhys;
	port_info->handle = le16_to_cpu(buffer->DevHandle);
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(struct mptsas_phyinfo),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_sas_expander_pg1(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasExpanderPage1_t *buffer;
	dma_addr_t dma_handle;
	int error=0;

	if (ioc->sas_discovery_runtime &&
		mptsas_is_end_device(&phy_info->attached))
			goto out;

	hdr.PageVersion = MPI_SASEXPANDER0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;


	mptsas_print_expander_pg1(buffer);

	/* save config data */
	phy_info->phy_id = buffer->PhyIdentifier;
	phy_info->port_id = buffer->PhysicalPort;
	phy_info->negotiated_link_rate = buffer->NegotiatedLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static void
mptsas_parse_device_info(struct sas_identify *identify,
		struct mptsas_devinfo *device_info)
{
	u16 protocols;

	identify->sas_address = device_info->sas_address;
	identify->phy_identifier = device_info->phy_id;

	/*
	 * Fill in Phy Initiator Port Protocol.
	 * Bits 6:3, more than one bit can be set, fall through cases.
	 */
	protocols = device_info->device_info & 0x78;
	identify->initiator_port_protocols = 0;
	if (protocols & MPI_SAS_DEVICE_INFO_SSP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SSP;
	if (protocols & MPI_SAS_DEVICE_INFO_STP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_STP;
	if (protocols & MPI_SAS_DEVICE_INFO_SMP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SMP;
	if (protocols & MPI_SAS_DEVICE_INFO_SATA_HOST)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SATA;

	/*
	 * Fill in Phy Target Port Protocol.
	 * Bits 10:7, more than one bit can be set, fall through cases.
	 */
	protocols = device_info->device_info & 0x780;
	identify->target_port_protocols = 0;
	if (protocols & MPI_SAS_DEVICE_INFO_SSP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SSP;
	if (protocols & MPI_SAS_DEVICE_INFO_STP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_STP;
	if (protocols & MPI_SAS_DEVICE_INFO_SMP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SMP;
	if (protocols & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
		identify->target_port_protocols |= SAS_PROTOCOL_SATA;

	/*
	 * Fill in Attached device type.
	 */
	switch (device_info->device_info &
			MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {
	case MPI_SAS_DEVICE_INFO_NO_DEVICE:
		identify->device_type = SAS_PHY_UNUSED;
		break;
	case MPI_SAS_DEVICE_INFO_END_DEVICE:
		identify->device_type = SAS_END_DEVICE;
		break;
	case MPI_SAS_DEVICE_INFO_EDGE_EXPANDER:
		identify->device_type = SAS_EDGE_EXPANDER_DEVICE;
		break;
	case MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER:
		identify->device_type = SAS_FANOUT_EXPANDER_DEVICE;
		break;
	}
}

static int mptsas_probe_one_phy(struct device *dev,
		struct mptsas_phyinfo *phy_info, int index, int local)
{
	MPT_ADAPTER *ioc;
	struct sas_phy *phy;
	int error;

	if (!dev)
		return -ENODEV;

	if (!phy_info->phy) {
		phy = sas_phy_alloc(dev, index);
		if (!phy)
			return -ENOMEM;
	} else
		phy = phy_info->phy;

	phy->port_identifier = phy_info->port_id;
	mptsas_parse_device_info(&phy->identify, &phy_info->identify);

	/*
	 * Set Negotiated link rate.
	 */
	switch (phy_info->negotiated_link_rate) {
	case MPI_SAS_IOUNIT0_RATE_PHY_DISABLED:
		phy->negotiated_linkrate = SAS_PHY_DISABLED;
		break;
	case MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION:
		phy->negotiated_linkrate = SAS_LINK_RATE_FAILED;
		break;
	case MPI_SAS_IOUNIT0_RATE_1_5:
		phy->negotiated_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_IOUNIT0_RATE_3_0:
		phy->negotiated_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	case MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE:
	case MPI_SAS_IOUNIT0_RATE_UNKNOWN:
	default:
		phy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;
		break;
	}

	/*
	 * Set Max hardware link rate.
	 */
	switch (phy_info->hw_link_rate & MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {
	case MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5:
		phy->maximum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
		phy->maximum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Max programmed link rate.
	 */
	switch (phy_info->programmed_link_rate &
			MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {
	case MPI_SAS_PHY0_PRATE_MAX_RATE_1_5:
		phy->maximum_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
		phy->maximum_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Min hardware link rate.
	 */
	switch (phy_info->hw_link_rate & MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK) {
	case MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5:
		phy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
		phy->minimum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Min programmed link rate.
	 */
	switch (phy_info->programmed_link_rate &
			MPI_SAS_PHY0_PRATE_MIN_RATE_MASK) {
	case MPI_SAS_PHY0_PRATE_MIN_RATE_1_5:
		phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
		phy->minimum_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	if (!phy_info->phy) {

		if (local)
			phy->local_attached = 1;

		error = sas_phy_add(phy);
		if (error) {
			sas_phy_free(phy);
			return error;
		}
		phy_info->phy = phy;
	}

	if ((phy_info->attached.handle) &&
	    (!phy_info->rphy)) {

		struct sas_rphy *rphy;
		struct sas_identify identify;

		ioc = phy_to_ioc(phy_info->phy);

		/*
		 * Let the hotplug_work thread handle processing
		 * the adding/removing of devices that occur
		 * after start of day.
		 */
		if (ioc->sas_discovery_runtime &&
			mptsas_is_end_device(&phy_info->attached))
			return 0;

		mptsas_parse_device_info(&identify, &phy_info->attached);
		switch (identify.device_type) {
		case SAS_END_DEVICE:
			rphy = sas_end_device_alloc(phy);
			break;
		case SAS_EDGE_EXPANDER_DEVICE:
		case SAS_FANOUT_EXPANDER_DEVICE:
			rphy = sas_expander_alloc(phy, identify.device_type);
			break;
		default:
			rphy = NULL;
			break;
		}
		if (!rphy)
			return 0; /* non-fatal: an rphy can be added later */

		rphy->identify = identify;

		error = sas_rphy_add(rphy);
		if (error) {
			sas_rphy_free(rphy);
			return error;
		}

		phy_info->rphy = rphy;
	}

	return 0;
}

static int
mptsas_probe_hba_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo *port_info, *hba;
	u32 handle = 0xFFFF;
	int error = -ENOMEM, i;

	hba = kzalloc(sizeof(*port_info), GFP_KERNEL);
	if (! hba)
		goto out;

	error = mptsas_sas_io_unit_pg0(ioc, hba);
	if (error)
		goto out_free_port_info;

	mutex_lock(&ioc->sas_topology_mutex);
	port_info = mptsas_find_portinfo_by_handle(ioc, hba->handle);
	if (!port_info) {
		port_info = hba;
		list_add_tail(&port_info->list, &ioc->sas_topology);
	} else {
		port_info->handle = hba->handle;
		for (i = 0; i < hba->num_phys; i++)
			port_info->phy_info[i].negotiated_link_rate =
				hba->phy_info[i].negotiated_link_rate;
		if (hba->phy_info)
			kfree(hba->phy_info);
		kfree(hba);
		hba = NULL;
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	ioc->num_ports = port_info->num_phys;

	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_phy_pg0(ioc, &port_info->phy_info[i],
			(MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER <<
			 MPI_SAS_PHY_PGAD_FORM_SHIFT), i);

		mptsas_sas_device_pg0(ioc, &port_info->phy_info[i].identify,
			(MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE <<
			 MPI_SAS_DEVICE_PGAD_FORM_SHIFT), handle);
		port_info->phy_info[i].identify.phy_id =
		    port_info->phy_info[i].phy_id;
		handle = port_info->phy_info[i].identify.handle;

		if (port_info->phy_info[i].attached.handle) {
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].attached,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].attached.handle);
		}

		mptsas_probe_one_phy(&ioc->sh->shost_gendev,
		    &port_info->phy_info[i], ioc->sas_index, 1);
		ioc->sas_index++;
	}

	return 0;

 out_free_port_info:
	if (hba)
		kfree(hba);
 out:
	return error;
}

static int
mptsas_probe_expander_phys(MPT_ADAPTER *ioc, u32 *handle)
{
	struct mptsas_portinfo *port_info, *p, *ex;
	int error = -ENOMEM, i, j;

	ex = kzalloc(sizeof(*port_info), GFP_KERNEL);
	if (!ex)
		goto out;

	error = mptsas_sas_expander_pg0(ioc, ex,
		(MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
		 MPI_SAS_EXPAND_PGAD_FORM_SHIFT), *handle);
	if (error)
		goto out_free_port_info;

	*handle = ex->handle;

	mutex_lock(&ioc->sas_topology_mutex);
	port_info = mptsas_find_portinfo_by_handle(ioc, *handle);
	if (!port_info) {
		port_info = ex;
		list_add_tail(&port_info->list, &ioc->sas_topology);
	} else {
		port_info->handle = ex->handle;
		if (ex->phy_info)
			kfree(ex->phy_info);
		kfree(ex);
		ex = NULL;
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	for (i = 0; i < port_info->num_phys; i++) {
		struct device *parent;

		mptsas_sas_expander_pg1(ioc, &port_info->phy_info[i],
			(MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM <<
			 MPI_SAS_EXPAND_PGAD_FORM_SHIFT), (i << 16) + *handle);

		if (port_info->phy_info[i].identify.handle) {
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].identify,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].identify.handle);
			port_info->phy_info[i].identify.phy_id =
			    port_info->phy_info[i].phy_id;
		}

		if (port_info->phy_info[i].attached.handle) {
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].attached,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].attached.handle);
			port_info->phy_info[i].attached.phy_id =
			    port_info->phy_info[i].phy_id;
		}

		/*
		 * If we find a parent port handle this expander is
		 * attached to another expander, else it hangs of the
		 * HBA phys.
		 */
		parent = &ioc->sh->shost_gendev;
		mutex_lock(&ioc->sas_topology_mutex);
		list_for_each_entry(p, &ioc->sas_topology, list) {
			for (j = 0; j < p->num_phys; j++) {
				if (port_info->phy_info[i].identify.handle ==
						p->phy_info[j].attached.handle)
					parent = &p->phy_info[j].rphy->dev;
			}
		}
		mutex_unlock(&ioc->sas_topology_mutex);

		mptsas_probe_one_phy(parent, &port_info->phy_info[i],
		    ioc->sas_index, 0);
		ioc->sas_index++;
	}

	return 0;

 out_free_port_info:
	if (ex) {
		if (ex->phy_info)
			kfree(ex->phy_info);
		kfree(ex);
	}
 out:
	return error;
}

/*
 * mptsas_delete_expander_phys
 *
 *
 * This will traverse topology, and remove expanders
 * that are no longer present
 */
static void
mptsas_delete_expander_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo buffer;
	struct mptsas_portinfo *port_info, *n, *parent;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(port_info, n, &ioc->sas_topology, list) {

		if (port_info->phy_info &&
		    (!(port_info->phy_info[0].identify.device_info &
		    MPI_SAS_DEVICE_INFO_SMP_TARGET)))
			continue;

		if (mptsas_sas_expander_pg0(ioc, &buffer,
		     (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
		     MPI_SAS_EXPAND_PGAD_FORM_SHIFT), port_info->handle)) {

			/*
			 * Obtain the port_info instance to the parent port
			 */
			parent = mptsas_find_portinfo_by_handle(ioc,
			    port_info->phy_info[0].identify.handle_parent);

			if (!parent)
				goto next_port;

			/*
			 * Delete rphys in the parent that point
			 * to this expander.  The transport layer will
			 * cleanup all the children.
			 */
			for (i = 0; i < parent->num_phys; i++) {
				if ((!parent->phy_info[i].rphy) ||
				    (parent->phy_info[i].attached.sas_address !=
				   port_info->phy_info[i].identify.sas_address))
					continue;
				sas_rphy_delete(parent->phy_info[i].rphy);
				memset(&parent->phy_info[i].attached, 0,
				    sizeof(struct mptsas_devinfo));
				parent->phy_info[i].rphy = NULL;
				parent->phy_info[i].starget = NULL;
			}
 next_port:
			list_del(&port_info->list);
			if (port_info->phy_info)
				kfree(port_info->phy_info);
			kfree(port_info);
		}
		/*
		* Free this memory allocated from inside
		* mptsas_sas_expander_pg0
		*/
		if (buffer.phy_info)
			kfree(buffer.phy_info);
	}
	mutex_unlock(&ioc->sas_topology_mutex);
}

/*
 * Start of day discovery
 */
static void
mptsas_scan_sas_topology(MPT_ADAPTER *ioc)
{
	u32 handle = 0xFFFF;
	int i;

	mutex_lock(&ioc->sas_discovery_mutex);
	mptsas_probe_hba_phys(ioc);
	while (!mptsas_probe_expander_phys(ioc, &handle))
		;
	/*
	  Reporting RAID volumes.
	*/
	if (!ioc->raid_data.pIocPg2)
		goto out;
	if (!ioc->raid_data.pIocPg2->NumActiveVolumes)
		goto out;
	for (i=0; i<ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
		scsi_add_device(ioc->sh, ioc->num_ports,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID, 0);
	}
 out:
	mutex_unlock(&ioc->sas_discovery_mutex);
}

/*
 * Work queue thread to handle Runtime discovery
 * Mere purpose is the hot add/delete of expanders
 */
static void
mptscsih_discovery_work(void * arg)
{
	struct mptsas_discovery_event *ev = arg;
	MPT_ADAPTER *ioc = ev->ioc;
	u32 handle = 0xFFFF;

	mutex_lock(&ioc->sas_discovery_mutex);
	ioc->sas_discovery_runtime=1;
	mptsas_delete_expander_phys(ioc);
	mptsas_probe_hba_phys(ioc);
	while (!mptsas_probe_expander_phys(ioc, &handle))
		;
	kfree(ev);
	ioc->sas_discovery_runtime=0;
	mutex_unlock(&ioc->sas_discovery_mutex);
}

static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_parent(MPT_ADAPTER *ioc, u16 parent_handle, u8 phy_id)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_devinfo device_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i, error;

	/*
	 * Retrieve the parent sas_address
	 */
	error = mptsas_sas_device_pg0(ioc, &device_info,
		(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
		 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		parent_handle);
	if (error)
		return NULL;

	/*
	 * The phy_info structures are never deallocated during lifetime of
	 * a host, so the code below is safe without additional refcounting.
	 */
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (port_info->phy_info[i].identify.sas_address ==
			    device_info.sas_address &&
			    port_info->phy_info[i].phy_id == phy_id) {
				phy_info = &port_info->phy_info[i];
				break;
			}
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	return phy_info;
}

static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_target(MPT_ADAPTER *ioc, u32 id)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	/*
	 * The phy_info structures are never deallocated during lifetime of
	 * a host, so the code below is safe without additional refcounting.
	 */
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++)
			if (mptsas_is_end_device(&port_info->phy_info[i].attached))
				if (port_info->phy_info[i].attached.id == id) {
					phy_info = &port_info->phy_info[i];
					break;
				}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	return phy_info;
}

/*
 * Work queue thread to clear the persitency table
 */
static void
mptscsih_sas_persist_clear_table(void * arg)
{
	MPT_ADAPTER *ioc = (MPT_ADAPTER *)arg;

	mptbase_sas_persist_operation(ioc, MPI_SAS_OP_CLEAR_NOT_PRESENT);
}

static void
mptsas_reprobe_lun(struct scsi_device *sdev, void *data)
{
	sdev->no_uld_attach = data ? 1 : 0;
	scsi_device_reprobe(sdev);
}

static void
mptsas_reprobe_target(struct scsi_target *starget, int uld_attach)
{
	starget_for_each_device(starget, uld_attach ? (void *)1 : NULL,
			mptsas_reprobe_lun);
}


/*
 * Work queue thread to handle SAS hotplug events
 */
static void
mptsas_hotplug_work(void *arg)
{
	struct mptsas_hotplug_event *ev = arg;
	MPT_ADAPTER *ioc = ev->ioc;
	struct mptsas_phyinfo *phy_info;
	struct sas_rphy *rphy;
	struct scsi_device *sdev;
	struct sas_identify identify;
	char *ds = NULL;
	struct mptsas_devinfo sas_device;
	VirtTarget *vtarget;

	mutex_lock(&ioc->sas_discovery_mutex);

	switch (ev->event_type) {
	case MPTSAS_DEL_DEVICE:

		phy_info = mptsas_find_phyinfo_by_target(ioc, ev->id);

		/*
		 * Sanity checks, for non-existing phys and remote rphys.
		 */
		if (!phy_info)
			break;
		if (!phy_info->rphy)
			break;
		if (phy_info->starget) {
			vtarget = phy_info->starget->hostdata;

			if (!vtarget)
				break;
			/*
			 * Handling  RAID components
			 */
			if (ev->phys_disk_num_valid) {
				vtarget->target_id = ev->phys_disk_num;
				vtarget->tflags |= MPT_TARGET_FLAGS_RAID_COMPONENT;
				mptsas_reprobe_target(vtarget->starget, 1);
				break;
			}
		}

		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_SSP_TARGET)
			ds = "ssp";
		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_STP_TARGET)
			ds = "stp";
		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "sata";

		printk(MYIOC_s_INFO_FMT
		       "removing %s device, channel %d, id %d, phy %d\n",
		       ioc->name, ds, ev->channel, ev->id, phy_info->phy_id);

		sas_rphy_delete(phy_info->rphy);
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		phy_info->rphy = NULL;
		phy_info->starget = NULL;
		break;
	case MPTSAS_ADD_DEVICE:

		if (ev->phys_disk_num_valid)
			mpt_findImVolumes(ioc);

		/*
		 * Refresh sas device pg0 data
		 */
		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT), ev->id))
			break;

		phy_info = mptsas_find_phyinfo_by_parent(ioc,
				sas_device.handle_parent, sas_device.phy_id);

		if (!phy_info) {
			u32 handle = 0xFFFF;

			/*
			* Its possible when an expander has been hot added
			* containing attached devices, the sas firmware
			* may send a RC_ADDED event prior to the
			* DISCOVERY STOP event. If that occurs, our
			* view of the topology in the driver in respect to this
			* expander might of not been setup, and we hit this
			* condition.
			* Therefore, this code kicks off discovery to
			* refresh the data.
			* Then again, we check whether the parent phy has
			* been created.
			*/
			ioc->sas_discovery_runtime=1;
			mptsas_delete_expander_phys(ioc);
			mptsas_probe_hba_phys(ioc);
			while (!mptsas_probe_expander_phys(ioc, &handle))
				;
			ioc->sas_discovery_runtime=0;

			phy_info = mptsas_find_phyinfo_by_parent(ioc,
				sas_device.handle_parent, sas_device.phy_id);
			if (!phy_info)
				break;
		}

		if (phy_info->starget) {
			vtarget = phy_info->starget->hostdata;

			if (!vtarget)
				break;
			/*
			 * Handling  RAID components
			 */
			if (vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT) {
				vtarget->tflags &= ~MPT_TARGET_FLAGS_RAID_COMPONENT;
				vtarget->target_id = ev->id;
				mptsas_reprobe_target(phy_info->starget, 0);
			}
			break;
		}

		if (phy_info->rphy)
			break;

		memcpy(&phy_info->attached, &sas_device,
		    sizeof(struct mptsas_devinfo));

		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_SSP_TARGET)
			ds = "ssp";
		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_STP_TARGET)
			ds = "stp";
		if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "sata";

		printk(MYIOC_s_INFO_FMT
		       "attaching %s device, channel %d, id %d, phy %d\n",
		       ioc->name, ds, ev->channel, ev->id, ev->phy_id);

		mptsas_parse_device_info(&identify, &phy_info->attached);
		switch (identify.device_type) {
		case SAS_END_DEVICE:
			rphy = sas_end_device_alloc(phy_info->phy);
			break;
		case SAS_EDGE_EXPANDER_DEVICE:
		case SAS_FANOUT_EXPANDER_DEVICE:
			rphy = sas_expander_alloc(phy_info->phy, identify.device_type);
			break;
		default:
			rphy = NULL;
			break;
		}
		if (!rphy)
			break; /* non-fatal: an rphy can be added later */

		rphy->identify = identify;
		if (sas_rphy_add(rphy)) {
			sas_rphy_free(rphy);
			break;
		}

		phy_info->rphy = rphy;
		break;
	case MPTSAS_ADD_RAID:
		sdev = scsi_device_lookup(
			ioc->sh,
			ioc->num_ports,
			ev->id,
			0);
		if (sdev) {
			scsi_device_put(sdev);
			break;
		}
		printk(MYIOC_s_INFO_FMT
		       "attaching raid volume, channel %d, id %d\n",
		       ioc->name, ioc->num_ports, ev->id);
		scsi_add_device(ioc->sh,
			ioc->num_ports,
			ev->id,
			0);
		mpt_findImVolumes(ioc);
		break;
	case MPTSAS_DEL_RAID:
		sdev = scsi_device_lookup(
			ioc->sh,
			ioc->num_ports,
			ev->id,
			0);
		if (!sdev)
			break;
		printk(MYIOC_s_INFO_FMT
		       "removing raid volume, channel %d, id %d\n",
		       ioc->name, ioc->num_ports, ev->id);
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
		mpt_findImVolumes(ioc);
		break;
	case MPTSAS_IGNORE_EVENT:
	default:
		break;
	}

	kfree(ev);
	mutex_unlock(&ioc->sas_discovery_mutex);
}

static void
mptscsih_send_sas_event(MPT_ADAPTER *ioc,
		EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data)
{
	struct mptsas_hotplug_event *ev;
	u32 device_info = le32_to_cpu(sas_event_data->DeviceInfo);
	__le64 sas_address;

	if ((device_info &
	     (MPI_SAS_DEVICE_INFO_SSP_TARGET |
	      MPI_SAS_DEVICE_INFO_STP_TARGET |
	      MPI_SAS_DEVICE_INFO_SATA_DEVICE )) == 0)
		return;

	switch (sas_event_data->ReasonCode) {
	case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
	case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:
		ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
		if (!ev) {
			printk(KERN_WARNING "mptsas: lost hotplug event\n");
			break;
		}

		INIT_WORK(&ev->work, mptsas_hotplug_work, ev);
		ev->ioc = ioc;
		ev->handle = le16_to_cpu(sas_event_data->DevHandle);
		ev->parent_handle =
		    le16_to_cpu(sas_event_data->ParentDevHandle);
		ev->channel = sas_event_data->Bus;
		ev->id = sas_event_data->TargetID;
		ev->phy_id = sas_event_data->PhyNum;
		memcpy(&sas_address, &sas_event_data->SASAddress,
		    sizeof(__le64));
		ev->sas_address = le64_to_cpu(sas_address);
		ev->device_info = device_info;

		if (sas_event_data->ReasonCode &
		    MPI_EVENT_SAS_DEV_STAT_RC_ADDED)
			ev->event_type = MPTSAS_ADD_DEVICE;
		else
			ev->event_type = MPTSAS_DEL_DEVICE;
		schedule_work(&ev->work);
		break;
	case MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED:
	/*
	 * Persistent table is full.
	 */
		INIT_WORK(&ioc->mptscsih_persistTask,
		    mptscsih_sas_persist_clear_table,
		    (void *)ioc);
		schedule_work(&ioc->mptscsih_persistTask);
		break;
	case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
	/* TODO */
	case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
	/* TODO */
	default:
		break;
	}
}

static void
mptscsih_send_raid_event(MPT_ADAPTER *ioc,
		EVENT_DATA_RAID *raid_event_data)
{
	struct mptsas_hotplug_event *ev;
	int status = le32_to_cpu(raid_event_data->SettingsStatus);
	int state = (status >> 8) & 0xff;

	if (ioc->bus_type != SAS)
		return;

	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		printk(KERN_WARNING "mptsas: lost hotplug event\n");
		return;
	}

	memset(ev,0,sizeof(struct mptsas_hotplug_event));
	INIT_WORK(&ev->work, mptsas_hotplug_work, ev);
	ev->ioc = ioc;
	ev->id = raid_event_data->VolumeID;
	ev->event_type = MPTSAS_IGNORE_EVENT;

	switch (raid_event_data->ReasonCode) {
	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		ev->event_type = MPTSAS_ADD_DEVICE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
		ioc->raid_data.isRaid = 1;
		ev->phys_disk_num_valid = 1;
		ev->phys_disk_num = raid_event_data->PhysDiskNum;
		ev->event_type = MPTSAS_DEL_DEVICE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		switch (state) {
		case MPI_PD_STATE_ONLINE:
			ioc->raid_data.isRaid = 1;
			ev->phys_disk_num_valid = 1;
			ev->phys_disk_num = raid_event_data->PhysDiskNum;
			ev->event_type = MPTSAS_ADD_DEVICE;
			break;
		case MPI_PD_STATE_MISSING:
		case MPI_PD_STATE_NOT_COMPATIBLE:
		case MPI_PD_STATE_OFFLINE_AT_HOST_REQUEST:
		case MPI_PD_STATE_FAILED_AT_HOST_REQUEST:
		case MPI_PD_STATE_OFFLINE_FOR_ANOTHER_REASON:
			ev->event_type = MPTSAS_DEL_DEVICE;
			break;
		default:
			break;
		}
		break;
	case MPI_EVENT_RAID_RC_VOLUME_DELETED:
		ev->event_type = MPTSAS_DEL_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_CREATED:
		ev->event_type = MPTSAS_ADD_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
		switch (state) {
		case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		case MPI_RAIDVOL0_STATUS_STATE_MISSING:
			ev->event_type = MPTSAS_DEL_RAID;
			break;
		case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
			ev->event_type = MPTSAS_ADD_RAID;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	schedule_work(&ev->work);
}

static void
mptscsih_send_discovery(MPT_ADAPTER *ioc,
	EVENT_DATA_SAS_DISCOVERY *discovery_data)
{
	struct mptsas_discovery_event *ev;

	/*
	 * DiscoveryStatus
	 *
	 * This flag will be non-zero when firmware
	 * kicks off discovery, and return to zero
	 * once its completed.
	 */
	if (discovery_data->DiscoveryStatus)
		return;

	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
	memset(ev,0,sizeof(struct mptsas_discovery_event));
	INIT_WORK(&ev->work, mptscsih_discovery_work, ev);
	ev->ioc = ioc;
	schedule_work(&ev->work);
};


static int
mptsas_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *reply)
{
	int rc=1;
	u8 event = le32_to_cpu(reply->Event) & 0xFF;

	if (!ioc->sh)
		goto out;

	/*
	 * sas_discovery_ignore_events
	 *
	 * This flag is to prevent anymore processing of
	 * sas events once mptsas_remove function is called.
	 */
	if (ioc->sas_discovery_ignore_events) {
		rc = mptscsih_event_process(ioc, reply);
		goto out;
	}

	switch (event) {
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
		mptscsih_send_sas_event(ioc,
			(EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)reply->Data);
		break;
	case MPI_EVENT_INTEGRATED_RAID:
		mptscsih_send_raid_event(ioc,
			(EVENT_DATA_RAID *)reply->Data);
		break;
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
		INIT_WORK(&ioc->mptscsih_persistTask,
		    mptscsih_sas_persist_clear_table,
		    (void *)ioc);
		schedule_work(&ioc->mptscsih_persistTask);
		break;
	 case MPI_EVENT_SAS_DISCOVERY:
		mptscsih_send_discovery(ioc,
			(EVENT_DATA_SAS_DISCOVERY *)reply->Data);
		break;
	default:
		rc = mptscsih_event_process(ioc, reply);
		break;
	}
 out:

	return rc;
}

static int
mptsas_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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

	r = mpt_attach(pdev,id);
	if (r)
		return r;

	ioc = pci_get_drvdata(pdev);
	ioc->DoneCtx = mptsasDoneCtx;
	ioc->TaskCtx = mptsasTaskCtx;
	ioc->InternalCtx = mptsasInternalCtx;

	/*  Added sanity check on readiness of the MPT adapter.
	 */
	if (ioc->last_state != MPI_IOC_STATE_OPERATIONAL) {
		printk(MYIOC_s_WARN_FMT
		  "Skipping because it's not operational!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptsas_probe;
	}

	if (!ioc->active) {
		printk(MYIOC_s_WARN_FMT "Skipping because it's disabled!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptsas_probe;
	}

	/*  Sanity check - ensure at least 1 port is INITIATOR capable
	 */
	ioc_cap = 0;
	for (ii = 0; ii < ioc->facts.NumberOfPorts; ii++) {
		if (ioc->pfacts[ii].ProtocolFlags &
				MPI_PORTFACTS_PROTOCOL_INITIATOR)
			ioc_cap++;
	}

	if (!ioc_cap) {
		printk(MYIOC_s_WARN_FMT
			"Skipping ioc=%p because SCSI Initiator mode "
			"is NOT enabled!\n", ioc->name, ioc);
		return 0;
	}

	sh = scsi_host_alloc(&mptsas_driver_template, sizeof(MPT_SCSI_HOST));
	if (!sh) {
		printk(MYIOC_s_WARN_FMT
			"Unable to register controller with SCSI subsystem\n",
			ioc->name);
		error = -1;
		goto out_mptsas_probe;
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

	sh->max_id = ioc->pfacts->MaxDevices + 1;

	sh->transportt = mptsas_transport_template;

	sh->max_lun = MPT_LAST_LUN + 1;
	sh->max_channel = 0;
	sh->this_id = ioc->pfacts[0].PortSCSIID;

	/* Required entry.
	 */
	sh->unique_id = ioc->id;

	INIT_LIST_HEAD(&ioc->sas_topology);
	mutex_init(&ioc->sas_topology_mutex);
	mutex_init(&ioc->sas_discovery_mutex);
	mutex_init(&ioc->sas_mgmt.mutex);
	init_completion(&ioc->sas_mgmt.done);

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
		goto out_mptsas_probe;
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
		goto out_mptsas_probe;
	}

	dprintk((KERN_INFO "  vtarget @ %p\n", hd->Targets));

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
	ioc->sas_data.ptClear = mpt_pt_clear;

	if (ioc->sas_data.ptClear==1) {
		mptbase_sas_persist_operation(
		    ioc, MPI_SAS_OP_CLEAR_ALL_PERSISTENT);
	}

	ddvprintk((MYIOC_s_INFO_FMT
		"mpt_pq_filter %x mpt_pq_filter %x\n",
		ioc->name,
		mpt_pq_filter,
		mpt_pq_filter));

	init_waitqueue_head(&hd->scandv_waitq);
	hd->scandv_wait_done = 0;
	hd->last_queue_full = 0;

	error = scsi_add_host(sh, &ioc->pcidev->dev);
	if (error) {
		dprintk((KERN_ERR MYNAM
		  "scsi_add_host failed\n"));
		goto out_mptsas_probe;
	}

	mptsas_scan_sas_topology(ioc);

	return 0;

out_mptsas_probe:

	mptscsih_remove(pdev);
	return error;
}

static void __devexit mptsas_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);
	struct mptsas_portinfo *p, *n;

	ioc->sas_discovery_ignore_events=1;
	sas_remove_host(ioc->sh);

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(p, n, &ioc->sas_topology, list) {
		list_del(&p->list);
		if (p->phy_info)
			kfree(p->phy_info);
		kfree(p);
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	mptscsih_remove(pdev);
}

static struct pci_device_id mptsas_pci_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1064,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1066,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1068,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1064E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1066E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_SAS1068E,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptsas_pci_table);


static struct pci_driver mptsas_driver = {
	.name		= "mptsas",
	.id_table	= mptsas_pci_table,
	.probe		= mptsas_probe,
	.remove		= __devexit_p(mptsas_remove),
	.shutdown	= mptscsih_shutdown,
#ifdef CONFIG_PM
	.suspend	= mptscsih_suspend,
	.resume		= mptscsih_resume,
#endif
};

static int __init
mptsas_init(void)
{
	show_mptmod_ver(my_NAME, my_VERSION);

	mptsas_transport_template =
	    sas_attach_transport(&mptsas_transport_functions);
	if (!mptsas_transport_template)
		return -ENODEV;

	mptsasDoneCtx = mpt_register(mptscsih_io_done, MPTSAS_DRIVER);
	mptsasTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTSAS_DRIVER);
	mptsasInternalCtx =
		mpt_register(mptscsih_scandv_complete, MPTSAS_DRIVER);
	mptsasMgmtCtx = mpt_register(mptsas_mgmt_done, MPTSAS_DRIVER);

	if (mpt_event_register(mptsasDoneCtx, mptsas_event_process) == 0) {
		devtverboseprintk((KERN_INFO MYNAM
		  ": Registered for IOC event notifications\n"));
	}

	if (mpt_reset_register(mptsasDoneCtx, mptscsih_ioc_reset) == 0) {
		dprintk((KERN_INFO MYNAM
		  ": Registered for IOC reset notifications\n"));
	}

	return pci_register_driver(&mptsas_driver);
}

static void __exit
mptsas_exit(void)
{
	pci_unregister_driver(&mptsas_driver);
	sas_release_transport(mptsas_transport_template);

	mpt_reset_deregister(mptsasDoneCtx);
	mpt_event_deregister(mptsasDoneCtx);

	mpt_deregister(mptsasMgmtCtx);
	mpt_deregister(mptsasInternalCtx);
	mpt_deregister(mptsasTaskCtx);
	mpt_deregister(mptsasDoneCtx);
}

module_init(mptsas_init);
module_exit(mptsas_exit);

/*
 *  linux/drivers/message/fusion/mptsas.c
 *      For use with LSI Logic PCI chip/adapter(s)
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2007 LSI Logic Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *  Copyright (c) 2005-2007 Dell
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
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>	/* for mdelay */

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/scsi_dbg.h>

#include "mptbase.h"
#include "mptscsih.h"


#define my_NAME		"Fusion MPT SAS Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptsas"

/*
 * Reserved channel for integrated raid
 */
#define MPTSAS_RAID_CHANNEL	1

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(my_VERSION);

static int mpt_pt_clear;
module_param(mpt_pt_clear, int, 0);
MODULE_PARM_DESC(mpt_pt_clear,
		" Clear persistency table: enable=1  "
		"(default=MPTSCSIH_PT_CLEAR=0)");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPTSAS_MAX_LUN (16895)
static int max_lun = MPTSAS_MAX_LUN;
module_param(max_lun, int, 0);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

static int	mptsasDoneCtx = -1;
static int	mptsasTaskCtx = -1;
static int	mptsasInternalCtx = -1; /* Used only for internal commands */
static int	mptsasMgmtCtx = -1;

static void mptsas_hotplug_work(struct work_struct *work);

struct mptsas_target_reset_event {
	struct list_head 	list;
	EVENT_DATA_SAS_DEVICE_STATUS_CHANGE sas_event_data;
	u8	target_reset_issued;
};

enum mptsas_hotplug_action {
	MPTSAS_ADD_DEVICE,
	MPTSAS_DEL_DEVICE,
	MPTSAS_ADD_RAID,
	MPTSAS_DEL_RAID,
	MPTSAS_ADD_INACTIVE_VOLUME,
	MPTSAS_IGNORE_EVENT,
};

struct mptsas_hotplug_event {
	struct work_struct	work;
	MPT_ADAPTER		*ioc;
	enum mptsas_hotplug_action event_type;
	u64			sas_address;
	u8			channel;
	u8			id;
	u32			device_info;
	u16			handle;
	u16			parent_handle;
	u8			phy_id;
	u8			phys_disk_num_valid;	/* hrc (hidden raid component) */
	u8			phys_disk_num;		/* hrc - unique index*/
	u8			hidden_raid_component;	/* hrc - don't expose*/
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
	u32	phys_disk_num;	/* phys disk id, for csmi-ioctls */
	u8	channel;	/* logical bus number of this device */
	u64	sas_address;    /* WWN of this device,
				   SATA is assigned by HBA,expander */
	u32	device_info;	/* bitfield detailed info about this device */
};

/*
 * Specific details on ports, wide/narrow
 */
struct mptsas_portinfo_details{
	u16	num_phys;	/* number of phys belong to this port */
	u64	phy_bitmask; 	/* TODO, extend support for 255 phys */
	struct sas_rphy *rphy;	/* transport layer rphy object */
	struct sas_port *port;	/* transport layer port object */
	struct scsi_target *starget;
	struct mptsas_portinfo *port_info;
};

struct mptsas_phyinfo {
	u16	handle;			/* unique id to address this */
	u8	phy_id; 		/* phy index */
	u8	port_id; 		/* firmware port identifier */
	u8	negotiated_link_rate;	/* nego'd link rate for this phy */
	u8	hw_link_rate; 		/* hardware max/min phys link rate */
	u8	programmed_link_rate;	/* programmed max/min phy link rate */
	u8	sas_port_add_phy;	/* flag to request sas_port_add_phy*/
	struct mptsas_devinfo identify;	/* point to phy device info */
	struct mptsas_devinfo attached;	/* point to attached device info */
	struct sas_phy *phy;		/* transport layer phy object */
	struct mptsas_portinfo *portinfo;
	struct mptsas_portinfo_details * port_details;
};

struct mptsas_portinfo {
	struct list_head list;
	u16		num_phys;	/* number of phys */
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

#ifdef MPT_DEBUG_SAS
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
	printk("SAS Address=0x%llX\n", (unsigned long long)
	    le64_to_cpu(sas_address));
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
	if ((attached->sas_address) &&
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

/* no mutex */
static void
mptsas_port_delete(struct mptsas_portinfo_details * port_details)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info;
	u8	i;

	if (!port_details)
		return;

	port_info = port_details->port_info;
	phy_info = port_info->phy_info;

	dsaswideprintk((KERN_DEBUG "%s: [%p]: num_phys=%02d "
	    "bitmask=0x%016llX\n", __FUNCTION__, port_details,
	    port_details->num_phys, (unsigned long long)
	    port_details->phy_bitmask));

	for (i = 0; i < port_info->num_phys; i++, phy_info++) {
		if(phy_info->port_details != port_details)
			continue;
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		phy_info->port_details = NULL;
	}
	kfree(port_details);
}

static inline struct sas_rphy *
mptsas_get_rphy(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->rphy;
	else
		return NULL;
}

static inline void
mptsas_set_rphy(struct mptsas_phyinfo *phy_info, struct sas_rphy *rphy)
{
	if (phy_info->port_details) {
		phy_info->port_details->rphy = rphy;
		dsaswideprintk((KERN_DEBUG "sas_rphy_add: rphy=%p\n", rphy));
	}

#ifdef MPT_DEBUG_SAS_WIDE
	if (rphy) {
		dev_printk(KERN_DEBUG, &rphy->dev, "add:");
		printk("rphy=%p release=%p\n",
			rphy, rphy->dev.release);
	}
#endif
}

static inline struct sas_port *
mptsas_get_port(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->port;
	else
		return NULL;
}

static inline void
mptsas_set_port(struct mptsas_phyinfo *phy_info, struct sas_port *port)
{
	if (phy_info->port_details)
		phy_info->port_details->port = port;

#ifdef MPT_DEBUG_SAS_WIDE
	if (port) {
		dev_printk(KERN_DEBUG, &port->dev, "add: ");
		printk("port=%p release=%p\n",
			port, port->dev.release);
	}
#endif
}

static inline struct scsi_target *
mptsas_get_starget(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->starget;
	else
		return NULL;
}

static inline void
mptsas_set_starget(struct mptsas_phyinfo *phy_info, struct scsi_target *
starget)
{
	if (phy_info->port_details)
		phy_info->port_details->starget = starget;
}


/*
 * mptsas_setup_wide_ports
 *
 * Updates for new and existing narrow/wide port configuration
 * in the sas_topology
 */
static void
mptsas_setup_wide_ports(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	struct mptsas_portinfo_details * port_details;
	struct mptsas_phyinfo *phy_info, *phy_info_cmp;
	u64	sas_address;
	int	i, j;

	mutex_lock(&ioc->sas_topology_mutex);

	phy_info = port_info->phy_info;
	for (i = 0 ; i < port_info->num_phys ; i++, phy_info++) {
		if (phy_info->attached.handle)
			continue;
		port_details = phy_info->port_details;
		if (!port_details)
			continue;
		if (port_details->num_phys < 2)
			continue;
		/*
		 * Removing a phy from a port, letting the last
		 * phy be removed by firmware events.
		 */
		dsaswideprintk((KERN_DEBUG
			"%s: [%p]: deleting phy = %d\n",
			__FUNCTION__, port_details, i));
		port_details->num_phys--;
		port_details->phy_bitmask &= ~ (1 << phy_info->phy_id);
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		sas_port_delete_phy(port_details->port, phy_info->phy);
		phy_info->port_details = NULL;
	}

	/*
	 * Populate and refresh the tree
	 */
	phy_info = port_info->phy_info;
	for (i = 0 ; i < port_info->num_phys ; i++, phy_info++) {
		sas_address = phy_info->attached.sas_address;
		dsaswideprintk((KERN_DEBUG "phy_id=%d sas_address=0x%018llX\n",
		    i, (unsigned long long)sas_address));
		if (!sas_address)
			continue;
		port_details = phy_info->port_details;
		/*
		 * Forming a port
		 */
		if (!port_details) {
			port_details = kzalloc(sizeof(*port_details),
				GFP_KERNEL);
			if (!port_details)
				goto out;
			port_details->num_phys = 1;
			port_details->port_info = port_info;
			if (phy_info->phy_id < 64 )
				port_details->phy_bitmask |=
				    (1 << phy_info->phy_id);
			phy_info->sas_port_add_phy=1;
			dsaswideprintk((KERN_DEBUG "\t\tForming port\n\t\t"
			    "phy_id=%d sas_address=0x%018llX\n",
			    i, (unsigned long long)sas_address));
			phy_info->port_details = port_details;
		}

		if (i == port_info->num_phys - 1)
			continue;
		phy_info_cmp = &port_info->phy_info[i + 1];
		for (j = i + 1 ; j < port_info->num_phys ; j++,
		    phy_info_cmp++) {
			if (!phy_info_cmp->attached.sas_address)
				continue;
			if (sas_address != phy_info_cmp->attached.sas_address)
				continue;
			if (phy_info_cmp->port_details == port_details )
				continue;
			dsaswideprintk((KERN_DEBUG
			    "\t\tphy_id=%d sas_address=0x%018llX\n",
			    j, (unsigned long long)
			    phy_info_cmp->attached.sas_address));
			if (phy_info_cmp->port_details) {
				port_details->rphy =
				    mptsas_get_rphy(phy_info_cmp);
				port_details->port =
				    mptsas_get_port(phy_info_cmp);
				port_details->starget =
				    mptsas_get_starget(phy_info_cmp);
				port_details->num_phys =
					phy_info_cmp->port_details->num_phys;
				if (!phy_info_cmp->port_details->num_phys)
					kfree(phy_info_cmp->port_details);
			} else
				phy_info_cmp->sas_port_add_phy=1;
			/*
			 * Adding a phy to a port
			 */
			phy_info_cmp->port_details = port_details;
			if (phy_info_cmp->phy_id < 64 )
				port_details->phy_bitmask |=
				(1 << phy_info_cmp->phy_id);
			port_details->num_phys++;
		}
	}

 out:

#ifdef MPT_DEBUG_SAS_WIDE
	for (i = 0; i < port_info->num_phys; i++) {
		port_details = port_info->phy_info[i].port_details;
		if (!port_details)
			continue;
		dsaswideprintk((KERN_DEBUG
		    "%s: [%p]: phy_id=%02d num_phys=%02d "
		    "bitmask=0x%016llX\n", __FUNCTION__,
		    port_details, i, port_details->num_phys,
		    (unsigned long long)port_details->phy_bitmask));
		dsaswideprintk((KERN_DEBUG"\t\tport = %p rphy=%p\n",
			port_details->port, port_details->rphy));
	}
	dsaswideprintk((KERN_DEBUG"\n"));
#endif
	mutex_unlock(&ioc->sas_topology_mutex);
}

/**
 * csmisas_find_vtarget
 *
 * @ioc
 * @volume_id
 * @volume_bus
 *
 **/
static VirtTarget *
mptsas_find_vtarget(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct scsi_device 		*sdev;
	VirtDevice			*vdev;
	VirtTarget 			*vtarget = NULL;

	shost_for_each_device(sdev, ioc->sh) {
		if ((vdev = sdev->hostdata) == NULL)
			continue;
		if (vdev->vtarget->id == id &&
		    vdev->vtarget->channel == channel)
			vtarget = vdev->vtarget;
	}
	return vtarget;
}

/**
 * mptsas_target_reset
 *
 * Issues TARGET_RESET to end device using handshaking method
 *
 * @ioc
 * @channel
 * @id
 *
 * Returns (1) success
 *         (0) failure
 *
 **/
static int
mptsas_target_reset(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;

	if ((mf = mpt_get_msg_frame(ioc->TaskCtx, ioc)) == NULL) {
		dfailprintk((MYIOC_s_WARN_FMT "%s, no msg frames @%d!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		return 0;
	}

	/* Format the Request
	 */
	pScsiTm = (SCSITaskMgmt_t *) mf;
	memset (pScsiTm, 0, sizeof(SCSITaskMgmt_t));
	pScsiTm->TargetID = id;
	pScsiTm->Bus = channel;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	pScsiTm->MsgFlags = MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION;

	DBG_DUMP_TM_REQUEST_FRAME(mf);

	if (mpt_send_handshake_request(ioc->TaskCtx, ioc,
	    sizeof(SCSITaskMgmt_t), (u32 *)mf, NO_SLEEP)) {
		mpt_free_msg_frame(ioc, mf);
		dfailprintk((MYIOC_s_WARN_FMT "%s, tm handshake failed @%d!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		return 0;
	}

	return 1;
}

/**
 * mptsas_target_reset_queue
 *
 * Receive request for TARGET_RESET after recieving an firmware
 * event NOT_RESPONDING_EVENT, then put command in link list
 * and queue if task_queue already in use.
 *
 * @ioc
 * @sas_event_data
 *
 **/
static void
mptsas_target_reset_queue(MPT_ADAPTER *ioc,
    EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data)
{
	MPT_SCSI_HOST	*hd = (MPT_SCSI_HOST *)ioc->sh->hostdata;
	VirtTarget *vtarget = NULL;
	struct mptsas_target_reset_event *target_reset_list;
	u8		id, channel;

	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	if (!(vtarget = mptsas_find_vtarget(ioc, channel, id)))
		return;

	vtarget->deleted = 1; /* block IO */

	target_reset_list = kzalloc(sizeof(*target_reset_list),
	    GFP_ATOMIC);
	if (!target_reset_list) {
		dfailprintk((MYIOC_s_WARN_FMT "%s, failed to allocate mem @%d..!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		return;
	}

	memcpy(&target_reset_list->sas_event_data, sas_event_data,
		sizeof(*sas_event_data));
	list_add_tail(&target_reset_list->list, &hd->target_reset_list);

	if (hd->resetPending)
		return;

	if (mptsas_target_reset(ioc, channel, id)) {
		target_reset_list->target_reset_issued = 1;
		hd->resetPending = 1;
	}
}

/**
 * mptsas_dev_reset_complete
 *
 * Completion for TARGET_RESET after NOT_RESPONDING_EVENT,
 * enable work queue to finish off removing device from upper layers.
 * then send next TARGET_RESET in the queue.
 *
 * @ioc
 *
 **/
static void
mptsas_dev_reset_complete(MPT_ADAPTER *ioc)
{
	MPT_SCSI_HOST	*hd = (MPT_SCSI_HOST *)ioc->sh->hostdata;
        struct list_head *head = &hd->target_reset_list;
	struct mptsas_target_reset_event *target_reset_list;
	struct mptsas_hotplug_event *ev;
	EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data;
	u8		id, channel;
	__le64		sas_address;

	if (list_empty(head))
		return;

	target_reset_list = list_entry(head->next, struct mptsas_target_reset_event, list);

	sas_event_data = &target_reset_list->sas_event_data;
	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;
	hd->resetPending = 0;

	/*
	 * retry target reset
	 */
	if (!target_reset_list->target_reset_issued) {
		if (mptsas_target_reset(ioc, channel, id)) {
			target_reset_list->target_reset_issued = 1;
			hd->resetPending = 1;
		}
		return;
	}

	/*
	 * enable work queue to remove device from upper layers
	 */
	list_del(&target_reset_list->list);

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		dfailprintk((MYIOC_s_WARN_FMT "%s, failed to allocate mem @%d..!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		return;
	}

	INIT_WORK(&ev->work, mptsas_hotplug_work);
	ev->ioc = ioc;
	ev->handle = le16_to_cpu(sas_event_data->DevHandle);
	ev->parent_handle =
	    le16_to_cpu(sas_event_data->ParentDevHandle);
	ev->channel = channel;
	ev->id =id;
	ev->phy_id = sas_event_data->PhyNum;
	memcpy(&sas_address, &sas_event_data->SASAddress,
	    sizeof(__le64));
	ev->sas_address = le64_to_cpu(sas_address);
	ev->device_info = le32_to_cpu(sas_event_data->DeviceInfo);
	ev->event_type = MPTSAS_DEL_DEVICE;
	schedule_work(&ev->work);
	kfree(target_reset_list);

	/*
	 * issue target reset to next device in the queue
	 */

	head = &hd->target_reset_list;
	if (list_empty(head))
		return;

	target_reset_list = list_entry(head->next, struct mptsas_target_reset_event,
	    list);

	sas_event_data = &target_reset_list->sas_event_data;
	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	if (mptsas_target_reset(ioc, channel, id)) {
		target_reset_list->target_reset_issued = 1;
		hd->resetPending = 1;
	}
}

/**
 * mptsas_taskmgmt_complete
 *
 * @ioc
 * @mf
 * @mr
 *
 **/
static int
mptsas_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	mptsas_dev_reset_complete(ioc);
	return mptscsih_taskmgmt_complete(ioc, mf, mr);
}

/**
 * mptscsih_ioc_reset
 *
 * @ioc
 * @reset_phase
 *
 **/
static int
mptsas_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	MPT_SCSI_HOST	*hd;
	struct mptsas_target_reset_event *target_reset_list, *n;
	int rc;

	rc = mptscsih_ioc_reset(ioc, reset_phase);

	if (ioc->bus_type != SAS)
		goto out;

	if (reset_phase != MPT_IOC_POST_RESET)
		goto out;

	if (!ioc->sh || !ioc->sh->hostdata)
		goto out;
	hd = (MPT_SCSI_HOST *)ioc->sh->hostdata;
	if (!hd->ioc)
		goto out;

	if (list_empty(&hd->target_reset_list))
		goto out;

	/* flush the target_reset_list */
	list_for_each_entry_safe(target_reset_list, n,
	    &hd->target_reset_list, list) {
		list_del(&target_reset_list->list);
		kfree(target_reset_list);
	}

 out:
	return rc;
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

	if (sdev->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	sas_read_port_mode_page(sdev);

 out:
	return mptscsih_slave_configure(sdev);
}

static int
mptsas_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(&starget->dev);
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	VirtTarget		*vtarget;
	u8			id, channel;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;

	vtarget = kzalloc(sizeof(VirtTarget), GFP_KERNEL);
	if (!vtarget)
		return -ENOMEM;

	vtarget->starget = starget;
	vtarget->ioc_id = hd->ioc->id;
	vtarget->tflags = MPT_TARGET_FLAGS_Q_YES;
	id = starget->id;
	channel = 0;

	/*
	 * RAID volumes placed beyond the last expected port.
	 */
	if (starget->channel == MPTSAS_RAID_CHANNEL) {
		for (i=0; i < hd->ioc->raid_data.pIocPg2->NumActiveVolumes; i++)
			if (id == hd->ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID)
				channel = hd->ioc->raid_data.pIocPg2->RaidVolume[i].VolumeBus;
		goto out;
	}

	rphy = dev_to_rphy(starget->dev.parent);
	mutex_lock(&hd->ioc->sas_topology_mutex);
	list_for_each_entry(p, &hd->ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;
			id = p->phy_info[i].attached.id;
			channel = p->phy_info[i].attached.channel;
			mptsas_set_starget(&p->phy_info[i], starget);

			/*
			 * Exposing hidden raid components
			 */
			if (mptscsih_is_phys_disk(hd->ioc, channel, id)) {
				id = mptscsih_raid_id_to_num(hd->ioc,
						channel, id);
				vtarget->tflags |=
				    MPT_TARGET_FLAGS_RAID_COMPONENT;
				p->phy_info[i].attached.phys_disk_num = id;
			}
			mutex_unlock(&hd->ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&hd->ioc->sas_topology_mutex);

	kfree(vtarget);
	return -ENXIO;

 out:
	vtarget->id = id;
	vtarget->channel = channel;
	starget->hostdata = vtarget;
	return 0;
}

static void
mptsas_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(&starget->dev);
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;

	if (!starget->hostdata)
		return;

	if (starget->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	rphy = dev_to_rphy(starget->dev.parent);
	list_for_each_entry(p, &hd->ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;
			mptsas_set_starget(&p->phy_info[i], NULL);
			goto out;
		}
	}

 out:
	kfree(starget->hostdata);
	starget->hostdata = NULL;
}


static int
mptsas_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = (MPT_SCSI_HOST *)host->hostdata;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	VirtDevice		*vdev;
	struct scsi_target 	*starget;
	int 			i;

	vdev = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdev) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kzalloc(%zd) FAILED!\n",
				hd->ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}
	starget = scsi_target(sdev);
	vdev->vtarget = starget->hostdata;

	if (sdev->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	rphy = dev_to_rphy(sdev->sdev_target->dev.parent);
	mutex_lock(&hd->ioc->sas_topology_mutex);
	list_for_each_entry(p, &hd->ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;
			vdev->lun = sdev->lun;
			/*
			 * Exposing hidden raid components
			 */
			if (mptscsih_is_phys_disk(hd->ioc,
			    p->phy_info[i].attached.channel,
			    p->phy_info[i].attached.id))
				sdev->no_uld_attach = 1;
			mutex_unlock(&hd->ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&hd->ioc->sas_topology_mutex);

	kfree(vdev);
	return -ENXIO;

 out:
	vdev->vtarget->num_luns++;
	sdev->hostdata = vdev;
	return 0;
}

static int
mptsas_qcmd(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	VirtDevice	*vdev = SCpnt->device->hostdata;

	if (!vdev || !vdev->vtarget || vdev->vtarget->deleted) {
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}

//	scsi_print_command(SCpnt);

	return mptscsih_qcmd(SCpnt,done);
}


static struct scsi_host_template mptsas_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptsas",
	.proc_info			= mptscsih_proc_info,
	.name				= "MPT SPI Host",
	.info				= mptscsih_info,
	.queuecommand			= mptsas_qcmd,
	.target_alloc			= mptsas_target_alloc,
	.slave_alloc			= mptsas_slave_alloc,
	.slave_configure		= mptsas_slave_configure,
	.target_destroy			= mptsas_target_destroy,
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
	.shost_attrs			= mptscsih_host_attrs,
};

static int mptsas_get_linkerrors(struct sas_phy *phy)
{
	MPT_ADAPTER *ioc = phy_to_ioc(phy);
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasPhyPage1_t *buffer;
	dma_addr_t dma_handle;
	int error;

	/* FIXME: only have link errors on local phys */
	if (!scsi_is_sas_phy_local(phy))
		return -EINVAL;

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

	/* FIXME: fusion doesn't allow non-local phy reset */
	if (!scsi_is_sas_phy_local(phy))
		return -EINVAL;

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
		sizeof(*port_info->phy_info),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	ioc->nvdata_version_persistent =
	    le16_to_cpu(buffer->NvdataVersionPersistent);
	ioc->nvdata_version_default =
	    le16_to_cpu(buffer->NvdataVersionDefault);

	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_print_phy_data(&buffer->PhyData[i]);
		port_info->phy_info[i].phy_id = i;
		port_info->phy_info[i].port_id =
		    buffer->PhyData[i].Port;
		port_info->phy_info[i].negotiated_link_rate =
		    buffer->PhyData[i].NegotiatedLinkRate;
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(buffer->PhyData[i].ControllerDevHandle);
	}

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

static int
mptsas_sas_io_unit_pg1(MPT_ADAPTER *ioc)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasIOUnitPage1_t *buffer;
	dma_addr_t dma_handle;
	int error;
	u16 device_missing_delay;

	memset(&hdr, 0, sizeof(ConfigExtendedPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));

	cfg.cfghdr.ehdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = 10;
	cfg.cfghdr.ehdr->PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	cfg.cfghdr.ehdr->ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	cfg.cfghdr.ehdr->PageVersion = MPI_SASIOUNITPAGE1_PAGEVERSION;
	cfg.cfghdr.ehdr->PageNumber = 1;

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

	ioc->io_missing_delay  =
	    le16_to_cpu(buffer->IODeviceMissingDelay);
	device_missing_delay = le16_to_cpu(buffer->ReportDeviceMissingDelay);
	ioc->device_missing_delay = (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_UNIT_16) ?
	    (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16 :
	    device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

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
	device_info->phys_disk_num = ~0;
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
	int i, error;

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
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(*port_info->phy_info),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	for (i = 0; i < port_info->num_phys; i++) {
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(buffer->DevHandle);
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
	struct sas_port *port;
	int error = 0;

	if (!dev) {
		error = -ENODEV;
		goto out;
	}

	if (!phy_info->phy) {
		phy = sas_phy_alloc(dev, index);
		if (!phy) {
			error = -ENOMEM;
			goto out;
		}
	} else
		phy = phy_info->phy;

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

		error = sas_phy_add(phy);
		if (error) {
			sas_phy_free(phy);
			goto out;
		}
		phy_info->phy = phy;
	}

	if (!phy_info->attached.handle ||
			!phy_info->port_details)
		goto out;

	port = mptsas_get_port(phy_info);
	ioc = phy_to_ioc(phy_info->phy);

	if (phy_info->sas_port_add_phy) {

		if (!port) {
			port = sas_port_alloc_num(dev);
			if (!port) {
				error = -ENOMEM;
				goto out;
			}
			error = sas_port_add(port);
			if (error) {
				dfailprintk((MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
					__FUNCTION__, __LINE__));
				goto out;
			}
			mptsas_set_port(phy_info, port);
			dsaswideprintk((KERN_DEBUG
			    "sas_port_alloc: port=%p dev=%p port_id=%d\n",
			    port, dev, port->port_identifier));
		}
		dsaswideprintk((KERN_DEBUG "sas_port_add_phy: phy_id=%d\n",
		    phy_info->phy_id));
		sas_port_add_phy(port, phy_info->phy);
		phy_info->sas_port_add_phy = 0;
	}

	if (!mptsas_get_rphy(phy_info) && port && !port->rphy) {

		struct sas_rphy *rphy;
		struct device *parent;
		struct sas_identify identify;

		parent = dev->parent->parent;
		/*
		 * Let the hotplug_work thread handle processing
		 * the adding/removing of devices that occur
		 * after start of day.
		 */
		if (ioc->sas_discovery_runtime &&
			mptsas_is_end_device(&phy_info->attached))
				goto out;

		mptsas_parse_device_info(&identify, &phy_info->attached);
		if (scsi_is_host_device(parent)) {
			struct mptsas_portinfo *port_info;
			int i;

			mutex_lock(&ioc->sas_topology_mutex);
			port_info = mptsas_find_portinfo_by_handle(ioc,
								   ioc->handle);
			mutex_unlock(&ioc->sas_topology_mutex);

			for (i = 0; i < port_info->num_phys; i++)
				if (port_info->phy_info[i].identify.sas_address ==
				    identify.sas_address) {
					sas_port_mark_backlink(port);
					goto out;
				}

		} else if (scsi_is_sas_rphy(parent)) {
			struct sas_rphy *parent_rphy = dev_to_rphy(parent);
			if (identify.sas_address ==
			    parent_rphy->identify.sas_address) {
				sas_port_mark_backlink(port);
				goto out;
			}
		}

		switch (identify.device_type) {
		case SAS_END_DEVICE:
			rphy = sas_end_device_alloc(port);
			break;
		case SAS_EDGE_EXPANDER_DEVICE:
		case SAS_FANOUT_EXPANDER_DEVICE:
			rphy = sas_expander_alloc(port, identify.device_type);
			break;
		default:
			rphy = NULL;
			break;
		}
		if (!rphy) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
			goto out;
		}

		rphy->identify = identify;
		error = sas_rphy_add(rphy);
		if (error) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
			sas_rphy_free(rphy);
			goto out;
		}
		mptsas_set_rphy(phy_info, rphy);
	}

 out:
	return error;
}

static int
mptsas_probe_hba_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo *port_info, *hba;
	int error = -ENOMEM, i;

	hba = kzalloc(sizeof(*port_info), GFP_KERNEL);
	if (! hba)
		goto out;

	error = mptsas_sas_io_unit_pg0(ioc, hba);
	if (error)
		goto out_free_port_info;

	mptsas_sas_io_unit_pg1(ioc);
	mutex_lock(&ioc->sas_topology_mutex);
	ioc->handle = hba->phy_info[0].handle;
	port_info = mptsas_find_portinfo_by_handle(ioc, ioc->handle);
	if (!port_info) {
		port_info = hba;
		list_add_tail(&port_info->list, &ioc->sas_topology);
	} else {
		for (i = 0; i < hba->num_phys; i++) {
			port_info->phy_info[i].negotiated_link_rate =
				hba->phy_info[i].negotiated_link_rate;
			port_info->phy_info[i].handle =
				hba->phy_info[i].handle;
			port_info->phy_info[i].port_id =
				hba->phy_info[i].port_id;
		}
		kfree(hba->phy_info);
		kfree(hba);
		hba = NULL;
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_phy_pg0(ioc, &port_info->phy_info[i],
			(MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER <<
			 MPI_SAS_PHY_PGAD_FORM_SHIFT), i);

		mptsas_sas_device_pg0(ioc, &port_info->phy_info[i].identify,
			(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
			 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			 port_info->phy_info[i].handle);
		port_info->phy_info[i].identify.phy_id =
		    port_info->phy_info[i].phy_id = i;
		if (port_info->phy_info[i].attached.handle)
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].attached,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].attached.handle);
	}

	mptsas_setup_wide_ports(ioc, port_info);

	for (i = 0; i < port_info->num_phys; i++, ioc->sas_index++)
		mptsas_probe_one_phy(&ioc->sh->shost_gendev,
		    &port_info->phy_info[i], ioc->sas_index, 1);

	return 0;

 out_free_port_info:
	kfree(hba);
 out:
	return error;
}

static int
mptsas_probe_expander_phys(MPT_ADAPTER *ioc, u32 *handle)
{
	struct mptsas_portinfo *port_info, *p, *ex;
	struct device *parent;
	struct sas_rphy *rphy;
	int error = -ENOMEM, i, j;

	ex = kzalloc(sizeof(*port_info), GFP_KERNEL);
	if (!ex)
		goto out;

	error = mptsas_sas_expander_pg0(ioc, ex,
	    (MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
	     MPI_SAS_EXPAND_PGAD_FORM_SHIFT), *handle);
	if (error)
		goto out_free_port_info;

	*handle = ex->phy_info[0].handle;

	mutex_lock(&ioc->sas_topology_mutex);
	port_info = mptsas_find_portinfo_by_handle(ioc, *handle);
	if (!port_info) {
		port_info = ex;
		list_add_tail(&port_info->list, &ioc->sas_topology);
	} else {
		for (i = 0; i < ex->num_phys; i++) {
			port_info->phy_info[i].handle =
				ex->phy_info[i].handle;
			port_info->phy_info[i].port_id =
				ex->phy_info[i].port_id;
		}
		kfree(ex->phy_info);
		kfree(ex);
		ex = NULL;
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	for (i = 0; i < port_info->num_phys; i++) {
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
	}

	parent = &ioc->sh->shost_gendev;
	for (i = 0; i < port_info->num_phys; i++) {
		mutex_lock(&ioc->sas_topology_mutex);
		list_for_each_entry(p, &ioc->sas_topology, list) {
			for (j = 0; j < p->num_phys; j++) {
				if (port_info->phy_info[i].identify.handle !=
						p->phy_info[j].attached.handle)
					continue;
				rphy = mptsas_get_rphy(&p->phy_info[j]);
				parent = &rphy->dev;
			}
		}
		mutex_unlock(&ioc->sas_topology_mutex);
	}

	mptsas_setup_wide_ports(ioc, port_info);

	for (i = 0; i < port_info->num_phys; i++, ioc->sas_index++)
		mptsas_probe_one_phy(parent, &port_info->phy_info[i],
		    ioc->sas_index, 0);

	return 0;

 out_free_port_info:
	if (ex) {
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
	struct mptsas_phyinfo *phy_info;
	struct sas_port * port;
	int i;
	u64	expander_sas_address;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(port_info, n, &ioc->sas_topology, list) {

		if (port_info->phy_info &&
		    (!(port_info->phy_info[0].identify.device_info &
		    MPI_SAS_DEVICE_INFO_SMP_TARGET)))
			continue;

		if (mptsas_sas_expander_pg0(ioc, &buffer,
		     (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
		     MPI_SAS_EXPAND_PGAD_FORM_SHIFT),
		     port_info->phy_info[0].handle)) {

			/*
			 * Obtain the port_info instance to the parent port
			 */
			parent = mptsas_find_portinfo_by_handle(ioc,
			    port_info->phy_info[0].identify.handle_parent);

			if (!parent)
				goto next_port;

			expander_sas_address =
				port_info->phy_info[0].identify.sas_address;

			/*
			 * Delete rphys in the parent that point
			 * to this expander.  The transport layer will
			 * cleanup all the children.
			 */
			phy_info = parent->phy_info;
			for (i = 0; i < parent->num_phys; i++, phy_info++) {
				port = mptsas_get_port(phy_info);
				if (!port)
					continue;
				if (phy_info->attached.sas_address !=
					expander_sas_address)
					continue;
#ifdef MPT_DEBUG_SAS_WIDE
				dev_printk(KERN_DEBUG, &port->dev,
				    "delete port (%d)\n", port->port_identifier);
#endif
				sas_port_delete(port);
				mptsas_port_delete(phy_info->port_details);
			}
 next_port:

			phy_info = port_info->phy_info;
			for (i = 0; i < port_info->num_phys; i++, phy_info++)
				mptsas_port_delete(phy_info->port_details);

			list_del(&port_info->list);
			kfree(port_info->phy_info);
			kfree(port_info);
		}
		/*
		* Free this memory allocated from inside
		* mptsas_sas_expander_pg0
		*/
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
	if (!ioc->ir_firmware)
		goto out;
	if (!ioc->raid_data.pIocPg2)
		goto out;
	if (!ioc->raid_data.pIocPg2->NumActiveVolumes)
		goto out;
	for (i = 0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID, 0);
	}
 out:
	mutex_unlock(&ioc->sas_discovery_mutex);
}

/*
 * Work queue thread to handle Runtime discovery
 * Mere purpose is the hot add/delete of expanders
 *(Mutex UNLOCKED)
 */
static void
__mptsas_discovery_work(MPT_ADAPTER *ioc)
{
	u32 handle = 0xFFFF;

	ioc->sas_discovery_runtime=1;
	mptsas_delete_expander_phys(ioc);
	mptsas_probe_hba_phys(ioc);
	while (!mptsas_probe_expander_phys(ioc, &handle))
		;
	ioc->sas_discovery_runtime=0;
}

/*
 * Work queue thread to handle Runtime discovery
 * Mere purpose is the hot add/delete of expanders
 *(Mutex LOCKED)
 */
static void
mptsas_discovery_work(struct work_struct *work)
{
	struct mptsas_discovery_event *ev =
		container_of(work, struct mptsas_discovery_event, work);
	MPT_ADAPTER *ioc = ev->ioc;

	mutex_lock(&ioc->sas_discovery_mutex);
	__mptsas_discovery_work(ioc);
	mutex_unlock(&ioc->sas_discovery_mutex);
	kfree(ev);
}

static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_sas_address(MPT_ADAPTER *ioc, u64 sas_address)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.sas_address
			    != sas_address)
				continue;
			phy_info = &port_info->phy_info[i];
			break;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return phy_info;
}

static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_target(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.id != id)
				continue;
			if (port_info->phy_info[i].attached.channel != channel)
				continue;
			phy_info = &port_info->phy_info[i];
			break;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return phy_info;
}

static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_phys_disk_num(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.phys_disk_num == ~0)
				continue;
			if (port_info->phy_info[i].attached.phys_disk_num != id)
				continue;
			if (port_info->phy_info[i].attached.channel != channel)
				continue;
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
mptsas_persist_clear_table(struct work_struct *work)
{
	MPT_ADAPTER *ioc = container_of(work, MPT_ADAPTER, sas_persist_task);

	mptbase_sas_persist_operation(ioc, MPI_SAS_OP_CLEAR_NOT_PRESENT);
}

static void
mptsas_reprobe_lun(struct scsi_device *sdev, void *data)
{
	int rc;

	sdev->no_uld_attach = data ? 1 : 0;
	rc = scsi_device_reprobe(sdev);
}

static void
mptsas_reprobe_target(struct scsi_target *starget, int uld_attach)
{
	starget_for_each_device(starget, uld_attach ? (void *)1 : NULL,
			mptsas_reprobe_lun);
}

static void
mptsas_adding_inactive_raid_components(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidVolumePage0_t		buffer = NULL;
	RaidPhysDiskPage0_t 		phys_disk;
	int				i;
	struct mptsas_hotplug_event 	*ev;

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

	if (!(buffer->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE))
		goto out;

	if (!buffer->NumPhysDisks)
		goto out;

	for (i = 0; i < buffer->NumPhysDisks; i++) {

		if (mpt_raid_phys_disk_pg0(ioc,
		    buffer->PhysDisk[i].PhysDiskNum, &phys_disk) != 0)
			continue;

		ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
		if (!ev) {
			printk(KERN_WARNING "mptsas: lost hotplug event\n");
			goto out;
		}

		INIT_WORK(&ev->work, mptsas_hotplug_work);
		ev->ioc = ioc;
		ev->id = phys_disk.PhysDiskID;
		ev->channel = phys_disk.PhysDiskBus;
		ev->phys_disk_num_valid = 1;
		ev->phys_disk_num = phys_disk.PhysDiskNum;
		ev->event_type = MPTSAS_ADD_DEVICE;
		schedule_work(&ev->work);
	}

 out:
	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);
}
/*
 * Work queue thread to handle SAS hotplug events
 */
static void
mptsas_hotplug_work(struct work_struct *work)
{
	struct mptsas_hotplug_event *ev =
		container_of(work, struct mptsas_hotplug_event, work);

	MPT_ADAPTER *ioc = ev->ioc;
	struct mptsas_phyinfo *phy_info;
	struct sas_rphy *rphy;
	struct sas_port *port;
	struct scsi_device *sdev;
	struct scsi_target * starget;
	struct sas_identify identify;
	char *ds = NULL;
	struct mptsas_devinfo sas_device;
	VirtTarget *vtarget;
	VirtDevice *vdevice;

	mutex_lock(&ioc->sas_discovery_mutex);
	switch (ev->event_type) {
	case MPTSAS_DEL_DEVICE:

		phy_info = NULL;
		if (ev->phys_disk_num_valid) {
			if (ev->hidden_raid_component){
				if (mptsas_sas_device_pg0(ioc, &sas_device,
				    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
				     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				    (ev->channel << 8) + ev->id)) {
					dfailprintk((MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
						__FUNCTION__, __LINE__));
					break;
				}
				phy_info = mptsas_find_phyinfo_by_sas_address(
				    ioc, sas_device.sas_address);
			}else
				phy_info = mptsas_find_phyinfo_by_phys_disk_num(
				    ioc, ev->channel, ev->phys_disk_num);
		}

		if (!phy_info)
			phy_info = mptsas_find_phyinfo_by_target(ioc,
			    ev->channel, ev->id);

		/*
		 * Sanity checks, for non-existing phys and remote rphys.
		 */
		if (!phy_info){
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
			break;
		}
		if (!phy_info->port_details) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break;
		}
		rphy = mptsas_get_rphy(phy_info);
		if (!rphy) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break;
		}

		port = mptsas_get_port(phy_info);
		if (!port) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (starget) {
			vtarget = starget->hostdata;

			if (!vtarget) {
				dfailprintk((MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
					__FUNCTION__, __LINE__));
				break;
			}

			/*
			 * Handling  RAID components
			 */
			if (ev->phys_disk_num_valid &&
			    ev->hidden_raid_component) {
				printk(MYIOC_s_INFO_FMT
				    "RAID Hidding: channel=%d, id=%d, "
				    "physdsk %d \n", ioc->name, ev->channel,
				    ev->id, ev->phys_disk_num);
				vtarget->id = ev->phys_disk_num;
				vtarget->tflags |=
				    MPT_TARGET_FLAGS_RAID_COMPONENT;
				mptsas_reprobe_target(starget, 1);
				phy_info->attached.phys_disk_num =
				    ev->phys_disk_num;
			break;
			}
		}

		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_SSP_TARGET)
			ds = "ssp";
		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_STP_TARGET)
			ds = "stp";
		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "sata";

		printk(MYIOC_s_INFO_FMT
		       "removing %s device, channel %d, id %d, phy %d\n",
		       ioc->name, ds, ev->channel, ev->id, phy_info->phy_id);
#ifdef MPT_DEBUG_SAS_WIDE
		dev_printk(KERN_DEBUG, &port->dev,
		    "delete port (%d)\n", port->port_identifier);
#endif
		sas_port_delete(port);
		mptsas_port_delete(phy_info->port_details);
		break;
	case MPTSAS_ADD_DEVICE:

		if (ev->phys_disk_num_valid)
			mpt_findImVolumes(ioc);

		/*
		 * Refresh sas device pg0 data
		 */
		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(ev->channel << 8) + ev->id)) {
				dfailprintk((MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
					__FUNCTION__, __LINE__));
			break;
		}

		__mptsas_discovery_work(ioc);

		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
				sas_device.sas_address);

		if (!phy_info || !phy_info->port_details) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (starget && (!ev->hidden_raid_component)){

			vtarget = starget->hostdata;

			if (!vtarget) {
				dfailprintk((MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
				       	__FUNCTION__, __LINE__));
				break;
			}
			/*
			 * Handling  RAID components
			 */
			if (vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT) {
				printk(MYIOC_s_INFO_FMT
				    "RAID Exposing: channel=%d, id=%d, "
				    "physdsk %d \n", ioc->name, ev->channel,
				    ev->id, ev->phys_disk_num);
				vtarget->tflags &=
				    ~MPT_TARGET_FLAGS_RAID_COMPONENT;
				vtarget->id = ev->id;
				mptsas_reprobe_target(starget, 0);
				phy_info->attached.phys_disk_num = ~0;
			}
			break;
		}

		if (mptsas_get_rphy(phy_info)) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			if (ev->channel) printk("%d\n", __LINE__);
			break;
		}

		port = mptsas_get_port(phy_info);
		if (!port) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break;
		}
		memcpy(&phy_info->attached, &sas_device,
		    sizeof(struct mptsas_devinfo));

		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_SSP_TARGET)
			ds = "ssp";
		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_STP_TARGET)
			ds = "stp";
		if (phy_info->attached.device_info &
		    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "sata";

		printk(MYIOC_s_INFO_FMT
		       "attaching %s device, channel %d, id %d, phy %d\n",
		       ioc->name, ds, ev->channel, ev->id, ev->phy_id);

		mptsas_parse_device_info(&identify, &phy_info->attached);
		rphy = sas_end_device_alloc(port);
		if (!rphy) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			break; /* non-fatal: an rphy can be added later */
		}

		rphy->identify = identify;
		if (sas_rphy_add(rphy)) {
			dfailprintk((MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
			       	__FUNCTION__, __LINE__));
			sas_rphy_free(rphy);
			break;
		}
		mptsas_set_rphy(phy_info, rphy);
		break;
	case MPTSAS_ADD_RAID:
		sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ev->id, 0);
		if (sdev) {
			scsi_device_put(sdev);
			break;
		}
		printk(MYIOC_s_INFO_FMT
		       "attaching raid volume, channel %d, id %d\n",
		       ioc->name, MPTSAS_RAID_CHANNEL, ev->id);
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL, ev->id, 0);
		mpt_findImVolumes(ioc);
		break;
	case MPTSAS_DEL_RAID:
		sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ev->id, 0);
		if (!sdev)
			break;
		printk(MYIOC_s_INFO_FMT
		       "removing raid volume, channel %d, id %d\n",
		       ioc->name, MPTSAS_RAID_CHANNEL, ev->id);
		vdevice = sdev->hostdata;
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
		mpt_findImVolumes(ioc);
		break;
	case MPTSAS_ADD_INACTIVE_VOLUME:
		mptsas_adding_inactive_raid_components(ioc,
		    ev->channel, ev->id);
		break;
	case MPTSAS_IGNORE_EVENT:
	default:
		break;
	}

	mutex_unlock(&ioc->sas_discovery_mutex);
	kfree(ev);
}

static void
mptsas_send_sas_event(MPT_ADAPTER *ioc,
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
	case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:

		mptsas_target_reset_queue(ioc, sas_event_data);
		break;

	case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
		ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
		if (!ev) {
			printk(KERN_WARNING "mptsas: lost hotplug event\n");
			break;
		}

		INIT_WORK(&ev->work, mptsas_hotplug_work);
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
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table);
		schedule_work(&ioc->sas_persist_task);
		break;
	/*
	 * TODO, handle other events
	 */
	case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
	case MPI_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
	case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
	case MPI_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
	default:
		break;
	}
}
static void
mptsas_send_raid_event(MPT_ADAPTER *ioc,
		EVENT_DATA_RAID *raid_event_data)
{
	struct mptsas_hotplug_event *ev;
	int status = le32_to_cpu(raid_event_data->SettingsStatus);
	int state = (status >> 8) & 0xff;

	if (ioc->bus_type != SAS)
		return;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		printk(KERN_WARNING "mptsas: lost hotplug event\n");
		return;
	}

	INIT_WORK(&ev->work, mptsas_hotplug_work);
	ev->ioc = ioc;
	ev->id = raid_event_data->VolumeID;
	ev->channel = raid_event_data->VolumeBus;
	ev->event_type = MPTSAS_IGNORE_EVENT;

	switch (raid_event_data->ReasonCode) {
	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		ev->phys_disk_num_valid = 1;
		ev->phys_disk_num = raid_event_data->PhysDiskNum;
		ev->event_type = MPTSAS_ADD_DEVICE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
		ev->phys_disk_num_valid = 1;
		ev->phys_disk_num = raid_event_data->PhysDiskNum;
		ev->hidden_raid_component = 1;
		ev->event_type = MPTSAS_DEL_DEVICE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		switch (state) {
		case MPI_PD_STATE_ONLINE:
		case MPI_PD_STATE_NOT_COMPATIBLE:
			ev->phys_disk_num_valid = 1;
			ev->phys_disk_num = raid_event_data->PhysDiskNum;
			ev->hidden_raid_component = 1;
			ev->event_type = MPTSAS_ADD_DEVICE;
			break;
		case MPI_PD_STATE_MISSING:
		case MPI_PD_STATE_OFFLINE_AT_HOST_REQUEST:
		case MPI_PD_STATE_FAILED_AT_HOST_REQUEST:
		case MPI_PD_STATE_OFFLINE_FOR_ANOTHER_REASON:
			ev->phys_disk_num_valid = 1;
			ev->phys_disk_num = raid_event_data->PhysDiskNum;
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
mptsas_send_discovery_event(MPT_ADAPTER *ioc,
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

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
	INIT_WORK(&ev->work, mptsas_discovery_work);
	ev->ioc = ioc;
	schedule_work(&ev->work);
};

/*
 * mptsas_send_ir2_event - handle exposing hidden disk when
 * an inactive raid volume is added
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @ir2_data
 *
 */
static void
mptsas_send_ir2_event(MPT_ADAPTER *ioc, PTR_MPI_EVENT_DATA_IR2 ir2_data)
{
	struct mptsas_hotplug_event *ev;

	if (ir2_data->ReasonCode !=
	    MPI_EVENT_IR2_RC_FOREIGN_CFG_DETECTED)
		return;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;

	INIT_WORK(&ev->work, mptsas_hotplug_work);
	ev->ioc = ioc;
	ev->id = ir2_data->TargetID;
	ev->channel = ir2_data->Bus;
	ev->event_type = MPTSAS_ADD_INACTIVE_VOLUME;

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
		mptsas_send_sas_event(ioc,
			(EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)reply->Data);
		break;
	case MPI_EVENT_INTEGRATED_RAID:
		mptsas_send_raid_event(ioc,
			(EVENT_DATA_RAID *)reply->Data);
		break;
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table);
		schedule_work(&ioc->sas_persist_task);
		break;
	 case MPI_EVENT_SAS_DISCOVERY:
		mptsas_send_discovery_event(ioc,
			(EVENT_DATA_SAS_DISCOVERY *)reply->Data);
		break;
	case MPI_EVENT_IR2:
		mptsas_send_ir2_event(ioc,
		    (PTR_MPI_EVENT_DATA_IR2)reply->Data);
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

	sh->max_id = ioc->pfacts[0].PortSCSIID;
	sh->max_lun = max_lun;

	sh->transportt = mptsas_transport_template;

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

	ioc->sas_data.ptClear = mpt_pt_clear;

	init_waitqueue_head(&hd->scandv_waitq);
	hd->scandv_wait_done = 0;
	hd->last_queue_full = 0;
	INIT_LIST_HEAD(&hd->target_reset_list);
	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	if (ioc->sas_data.ptClear==1) {
		mptbase_sas_persist_operation(
		    ioc, MPI_SAS_OP_CLEAR_ALL_PERSISTENT);
	}

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
	int i;

	ioc->sas_discovery_ignore_events = 1;
	sas_remove_host(ioc->sh);

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(p, n, &ioc->sas_topology, list) {
		list_del(&p->list);
		for (i = 0 ; i < p->num_phys ; i++)
			mptsas_port_delete(p->phy_info[i].port_details);
		kfree(p->phy_info);
		kfree(p);
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	mptscsih_remove(pdev);
}

static struct pci_device_id mptsas_pci_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1064,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1068,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1064E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1068E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1078,
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
	int error;

	show_mptmod_ver(my_NAME, my_VERSION);

	mptsas_transport_template =
	    sas_attach_transport(&mptsas_transport_functions);
	if (!mptsas_transport_template)
		return -ENODEV;

	mptsasDoneCtx = mpt_register(mptscsih_io_done, MPTSAS_DRIVER);
	mptsasTaskCtx = mpt_register(mptsas_taskmgmt_complete, MPTSAS_DRIVER);
	mptsasInternalCtx =
		mpt_register(mptscsih_scandv_complete, MPTSAS_DRIVER);
	mptsasMgmtCtx = mpt_register(mptsas_mgmt_done, MPTSAS_DRIVER);

	if (mpt_event_register(mptsasDoneCtx, mptsas_event_process) == 0) {
		devtverboseprintk((KERN_INFO MYNAM
		  ": Registered for IOC event notifications\n"));
	}

	if (mpt_reset_register(mptsasDoneCtx, mptsas_ioc_reset) == 0) {
		dprintk((KERN_INFO MYNAM
		  ": Registered for IOC reset notifications\n"));
	}

	error = pci_register_driver(&mptsas_driver);
	if (error)
		sas_release_transport(mptsas_transport_template);

	return error;
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

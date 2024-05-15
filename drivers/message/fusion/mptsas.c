/*
 *  linux/drivers/message/fusion/mptsas.c
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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
#include <linux/slab.h>
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
#include <scsi/scsi_transport.h>
#include <scsi/scsi_dbg.h>

#include "mptbase.h"
#include "mptscsih.h"
#include "mptsas.h"


#define my_NAME		"Fusion MPT SAS Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptsas"

/*
 * Reserved channel for integrated raid
 */
#define MPTSAS_RAID_CHANNEL	1

#define SAS_CONFIG_PAGE_TIMEOUT		30
MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(my_VERSION);

static int mpt_pt_clear;
module_param(mpt_pt_clear, int, 0);
MODULE_PARM_DESC(mpt_pt_clear,
		" Clear persistency table: enable=1  "
		"(default=MPTSCSIH_PT_CLEAR=0)");

/* scsi-mid layer global parameter is max_report_luns, which is 511 */
#define MPTSAS_MAX_LUN (16895)
static int max_lun = MPTSAS_MAX_LUN;
module_param(max_lun, int, 0);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

static int mpt_loadtime_max_sectors = 8192;
module_param(mpt_loadtime_max_sectors, int, 0);
MODULE_PARM_DESC(mpt_loadtime_max_sectors,
		" Maximum sector define for Host Bus Adaptor.Range 64 to 8192 default=8192");

static u8	mptsasDoneCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasTaskCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasInternalCtx = MPT_MAX_PROTOCOL_DRIVERS; /* Used only for internal commands */
static u8	mptsasMgmtCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasDeviceResetCtx = MPT_MAX_PROTOCOL_DRIVERS;

static void mptsas_firmware_event_work(struct work_struct *work);
static void mptsas_send_sas_event(struct fw_event_work *fw_event);
static void mptsas_send_raid_event(struct fw_event_work *fw_event);
static void mptsas_send_ir2_event(struct fw_event_work *fw_event);
static void mptsas_parse_device_info(struct sas_identify *identify,
		struct mptsas_devinfo *device_info);
static inline void mptsas_set_rphy(MPT_ADAPTER *ioc,
		struct mptsas_phyinfo *phy_info, struct sas_rphy *rphy);
static struct mptsas_phyinfo	*mptsas_find_phyinfo_by_sas_address
		(MPT_ADAPTER *ioc, u64 sas_address);
static int mptsas_sas_device_pg0(MPT_ADAPTER *ioc,
	struct mptsas_devinfo *device_info, u32 form, u32 form_specific);
static int mptsas_sas_enclosure_pg0(MPT_ADAPTER *ioc,
	struct mptsas_enclosure *enclosure, u32 form, u32 form_specific);
static int mptsas_add_end_device(MPT_ADAPTER *ioc,
	struct mptsas_phyinfo *phy_info);
static void mptsas_del_end_device(MPT_ADAPTER *ioc,
	struct mptsas_phyinfo *phy_info);
static void mptsas_send_link_status_event(struct fw_event_work *fw_event);
static struct mptsas_portinfo	*mptsas_find_portinfo_by_sas_address
		(MPT_ADAPTER *ioc, u64 sas_address);
static void mptsas_expander_delete(MPT_ADAPTER *ioc,
		struct mptsas_portinfo *port_info, u8 force);
static void mptsas_send_expander_event(struct fw_event_work *fw_event);
static void mptsas_not_responding_devices(MPT_ADAPTER *ioc);
static void mptsas_scan_sas_topology(MPT_ADAPTER *ioc);
static void mptsas_broadcast_primitive_work(struct fw_event_work *fw_event);
static void mptsas_handle_queue_full_event(struct fw_event_work *fw_event);
static void mptsas_volume_delete(MPT_ADAPTER *ioc, u8 id);
void	mptsas_schedule_target_reset(void *ioc);

static void mptsas_print_phy_data(MPT_ADAPTER *ioc,
					MPI_SAS_IO_UNIT0_PHY_DATA *phy_data)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- IO UNIT PAGE 0 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Handle=0x%X\n",
	    ioc->name, le16_to_cpu(phy_data->AttachedDeviceHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Controller Handle=0x%X\n",
	    ioc->name, le16_to_cpu(phy_data->ControllerDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Port=0x%X\n",
	    ioc->name, phy_data->Port));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Port Flags=0x%X\n",
	    ioc->name, phy_data->PortFlags));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Flags=0x%X\n",
	    ioc->name, phy_data->PhyFlags));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Negotiated Link Rate=0x%X\n",
	    ioc->name, phy_data->NegotiatedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Controller PHY Device Info=0x%X\n", ioc->name,
	    le32_to_cpu(phy_data->ControllerPhyDeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DiscoveryStatus=0x%X\n\n",
	    ioc->name, le32_to_cpu(phy_data->DiscoveryStatus)));
}

static void mptsas_print_phy_pg0(MPT_ADAPTER *ioc, SasPhyPage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS PHY PAGE 0 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached Device Handle=0x%X\n", ioc->name,
	    le16_to_cpu(pg0->AttachedDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SAS Address=0x%llX\n",
	    ioc->name, (unsigned long long)le64_to_cpu(sas_address)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached PHY Identifier=0x%X\n", ioc->name,
	    pg0->AttachedPhyIdentifier));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Attached Device Info=0x%X\n",
	    ioc->name, le32_to_cpu(pg0->AttachedDeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Programmed Link Rate=0x%X\n",
	    ioc->name,  pg0->ProgrammedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Change Count=0x%X\n",
	    ioc->name, pg0->ChangeCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Info=0x%X\n\n",
	    ioc->name, le32_to_cpu(pg0->PhyInfo)));
}

static void mptsas_print_phy_pg1(MPT_ADAPTER *ioc, SasPhyPage1_t *pg1)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS PHY PAGE 1 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Invalid Dword Count=0x%x\n",
	    ioc->name,  pg1->InvalidDwordCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Running Disparity Error Count=0x%x\n", ioc->name,
	    pg1->RunningDisparityErrorCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Loss Dword Synch Count=0x%x\n", ioc->name,
	    pg1->LossDwordSynchCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "PHY Reset Problem Count=0x%x\n\n", ioc->name,
	    pg1->PhyResetProblemCount));
}

static void mptsas_print_device_pg0(MPT_ADAPTER *ioc, SasDevicePage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS DEVICE PAGE 0 ---------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->DevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Parent Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->ParentDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Enclosure Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->EnclosureHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Slot=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->Slot)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SAS Address=0x%llX\n",
	    ioc->name, (unsigned long long)le64_to_cpu(sas_address)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Target ID=0x%X\n",
	    ioc->name, pg0->TargetID));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Bus=0x%X\n",
	    ioc->name, pg0->Bus));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Parent Phy Num=0x%X\n",
	    ioc->name, pg0->PhyNum));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Access Status=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->AccessStatus)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Device Info=0x%X\n",
	    ioc->name, le32_to_cpu(pg0->DeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Flags=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->Flags)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Physical Port=0x%X\n\n",
	    ioc->name, pg0->PhysicalPort));
}

static void mptsas_print_expander_pg1(MPT_ADAPTER *ioc, SasExpanderPage1_t *pg1)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS EXPANDER PAGE 1 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Physical Port=0x%X\n",
	    ioc->name, pg1->PhysicalPort));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Identifier=0x%X\n",
	    ioc->name, pg1->PhyIdentifier));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Negotiated Link Rate=0x%X\n",
	    ioc->name, pg1->NegotiatedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Programmed Link Rate=0x%X\n",
	    ioc->name, pg1->ProgrammedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Hardware Link Rate=0x%X\n",
	    ioc->name, pg1->HwLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Owner Device Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg1->OwnerDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached Device Handle=0x%X\n\n", ioc->name,
	    le16_to_cpu(pg1->AttachedDevHandle)));
}

/* inhibit sas firmware event handling */
static void
mptsas_fw_event_off(MPT_ADAPTER *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	ioc->fw_events_off = 1;
	ioc->sas_discovery_quiesce_io = 0;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

}

/* enable sas firmware event handling */
static void
mptsas_fw_event_on(MPT_ADAPTER *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	ioc->fw_events_off = 0;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/* queue a sas firmware event */
static void
mptsas_add_fw_event(MPT_ADAPTER *ioc, struct fw_event_work *fw_event,
    unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
	fw_event->users = 1;
	INIT_DELAYED_WORK(&fw_event->work, mptsas_firmware_event_work);
	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: add (fw_event=0x%p)"
		"on cpuid %d\n", ioc->name, __func__,
		fw_event, smp_processor_id()));
	queue_delayed_work_on(smp_processor_id(), ioc->fw_event_q,
	    &fw_event->work, delay);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/* requeue a sas firmware event */
static void
mptsas_requeue_fw_event(MPT_ADAPTER *ioc, struct fw_event_work *fw_event,
    unsigned long delay)
{
	unsigned long flags;
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: reschedule task "
	    "(fw_event=0x%p)on cpuid %d\n", ioc->name, __func__,
		fw_event, smp_processor_id()));
	fw_event->retries++;
	queue_delayed_work_on(smp_processor_id(), ioc->fw_event_q,
	    &fw_event->work, msecs_to_jiffies(delay));
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

static void __mptsas_free_fw_event(MPT_ADAPTER *ioc,
				   struct fw_event_work *fw_event)
{
	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: kfree (fw_event=0x%p)\n",
	    ioc->name, __func__, fw_event));
	list_del(&fw_event->list);
	kfree(fw_event);
}

/* free memory associated to a sas firmware event */
static void
mptsas_free_fw_event(MPT_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	fw_event->users--;
	if (!fw_event->users)
		__mptsas_free_fw_event(ioc, fw_event);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/* walk the firmware event queue, and either stop or wait for
 * outstanding events to complete */
static void
mptsas_cleanup_fw_event_q(MPT_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;
	struct mptsas_target_reset_event *target_reset_list, *n;
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
	unsigned long flags;

	/* flush the target_reset_list */
	if (!list_empty(&hd->target_reset_list)) {
		list_for_each_entry_safe(target_reset_list, n,
		    &hd->target_reset_list, list) {
			dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "%s: removing target reset for id=%d\n",
			    ioc->name, __func__,
			   target_reset_list->sas_event_data.TargetID));
			list_del(&target_reset_list->list);
			kfree(target_reset_list);
		}
	}

	if (list_empty(&ioc->fw_event_list) || !ioc->fw_event_q)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);

	while (!list_empty(&ioc->fw_event_list)) {
		bool canceled = false;

		fw_event = list_first_entry(&ioc->fw_event_list,
					    struct fw_event_work, list);
		fw_event->users++;
		spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
		if (cancel_delayed_work_sync(&fw_event->work))
			canceled = true;

		spin_lock_irqsave(&ioc->fw_event_lock, flags);
		if (canceled)
			fw_event->users--;
		fw_event->users--;
		WARN_ON_ONCE(fw_event->users);
		__mptsas_free_fw_event(ioc, fw_event);
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}


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

/**
 *	mptsas_find_portinfo_by_sas_address - find and return portinfo for
 *		this sas_address
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sas_address: expander sas address
 *
 *	This function should be called with the sas_topology_mutex already held.
 *
 *	Return: %NULL if not found.
 **/
static struct mptsas_portinfo *
mptsas_find_portinfo_by_sas_address(MPT_ADAPTER *ioc, u64 sas_address)
{
	struct mptsas_portinfo *port_info, *rc = NULL;
	int i;

	if (sas_address >= ioc->hba_port_sas_addr &&
	    sas_address < (ioc->hba_port_sas_addr +
	    ioc->hba_port_num_phy))
		return ioc->hba_port_info;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list)
		for (i = 0; i < port_info->num_phys; i++)
			if (port_info->phy_info[i].identify.sas_address ==
			    sas_address) {
				rc = port_info;
				goto out;
			}
 out:
	mutex_unlock(&ioc->sas_topology_mutex);
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
mptsas_port_delete(MPT_ADAPTER *ioc, struct mptsas_portinfo_details * port_details)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info;
	u8	i;

	if (!port_details)
		return;

	port_info = port_details->port_info;
	phy_info = port_info->phy_info;

	dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: [%p]: num_phys=%02d "
	    "bitmask=0x%016llX\n", ioc->name, __func__, port_details,
	    port_details->num_phys, (unsigned long long)
	    port_details->phy_bitmask));

	for (i = 0; i < port_info->num_phys; i++, phy_info++) {
		if(phy_info->port_details != port_details)
			continue;
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		mptsas_set_rphy(ioc, phy_info, NULL);
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
mptsas_set_rphy(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info, struct sas_rphy *rphy)
{
	if (phy_info->port_details) {
		phy_info->port_details->rphy = rphy;
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "sas_rphy_add: rphy=%p\n",
		    ioc->name, rphy));
	}

	if (rphy) {
		dsaswideprintk(ioc, dev_printk(KERN_DEBUG,
		    &rphy->dev, MYIOC_s_FMT "add:", ioc->name));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "rphy=%p release=%p\n",
		    ioc->name, rphy, rphy->dev.release));
	}
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
mptsas_set_port(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info, struct sas_port *port)
{
	if (phy_info->port_details)
		phy_info->port_details->port = port;

	if (port) {
		dsaswideprintk(ioc, dev_printk(KERN_DEBUG,
		    &port->dev, MYIOC_s_FMT "add:", ioc->name));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "port=%p release=%p\n",
		    ioc->name, port, port->dev.release));
	}
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

/**
 *	mptsas_add_device_component - adds a new device component to our lists
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: channel number
 *	@id: Logical Target ID for reset (if appropriate)
 *	@sas_address: expander sas address
 *	@device_info: specific bits (flags) for devices
 *	@slot: enclosure slot ID
 *	@enclosure_logical_id: enclosure WWN
 *
 **/
static void
mptsas_add_device_component(MPT_ADAPTER *ioc, u8 channel, u8 id,
	u64 sas_address, u32 device_info, u16 slot, u64 enclosure_logical_id)
{
	struct mptsas_device_info	*sas_info, *next;
	struct scsi_device	*sdev;
	struct scsi_target	*starget;
	struct sas_rphy	*rphy;

	/*
	 * Delete all matching devices out of the list
	 */
	mutex_lock(&ioc->sas_device_info_mutex);
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list,
	    list) {
		if (!sas_info->is_logical_volume &&
		    (sas_info->sas_address == sas_address ||
		    (sas_info->fw.channel == channel &&
		     sas_info->fw.id == id))) {
			list_del(&sas_info->list);
			kfree(sas_info);
		}
	}

	sas_info = kzalloc(sizeof(struct mptsas_device_info), GFP_KERNEL);
	if (!sas_info)
		goto out;

	/*
	 * Set Firmware mapping
	 */
	sas_info->fw.id = id;
	sas_info->fw.channel = channel;

	sas_info->sas_address = sas_address;
	sas_info->device_info = device_info;
	sas_info->slot = slot;
	sas_info->enclosure_logical_id = enclosure_logical_id;
	INIT_LIST_HEAD(&sas_info->list);
	list_add_tail(&sas_info->list, &ioc->sas_device_info_list);

	/*
	 * Set OS mapping
	 */
	shost_for_each_device(sdev, ioc->sh) {
		starget = scsi_target(sdev);
		rphy = dev_to_rphy(starget->dev.parent);
		if (rphy->identify.sas_address == sas_address) {
			sas_info->os.id = starget->id;
			sas_info->os.channel = starget->channel;
		}
	}

 out:
	mutex_unlock(&ioc->sas_device_info_mutex);
	return;
}

/**
 *	mptsas_add_device_component_by_fw - adds a new device component by FW ID
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: channel number
 *	@id: Logical Target ID
 *
 **/
static void
mptsas_add_device_component_by_fw(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct mptsas_devinfo sas_device;
	struct mptsas_enclosure enclosure_info;
	int rc;

	rc = mptsas_sas_device_pg0(ioc, &sas_device,
	    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
	     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
	    (channel << 8) + id);
	if (rc)
		return;

	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
	    (MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
	     MPI_SAS_ENCLOS_PGAD_FORM_SHIFT),
	     sas_device.handle_enclosure);

	mptsas_add_device_component(ioc, sas_device.channel,
	    sas_device.id, sas_device.sas_address, sas_device.device_info,
	    sas_device.slot, enclosure_info.enclosure_logical_id);
}

/**
 *	mptsas_add_device_component_starget_ir - Handle Integrated RAID, adding each individual device to list
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@starget: SCSI target for this SCSI device
 *
 **/
static void
mptsas_add_device_component_starget_ir(MPT_ADAPTER *ioc,
		struct scsi_target *starget)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidVolumePage0_t		buffer = NULL;
	int				i;
	RaidPhysDiskPage0_t 		phys_disk;
	struct mptsas_device_info	*sas_info, *next;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	/* assumption that all volumes on channel = 0 */
	cfg.pageAddr = starget->id;
	cfg.cfghdr.hdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!hdr.PageLength)
		goto out;

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.PageLength * 4,
				    &dma_handle, GFP_KERNEL);

	if (!buffer)
		goto out;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!buffer->NumPhysDisks)
		goto out;

	/*
	 * Adding entry for hidden components
	 */
	for (i = 0; i < buffer->NumPhysDisks; i++) {

		if (mpt_raid_phys_disk_pg0(ioc,
		    buffer->PhysDisk[i].PhysDiskNum, &phys_disk) != 0)
			continue;

		mptsas_add_device_component_by_fw(ioc, phys_disk.PhysDiskBus,
		    phys_disk.PhysDiskID);

		mutex_lock(&ioc->sas_device_info_mutex);
		list_for_each_entry(sas_info, &ioc->sas_device_info_list,
		    list) {
			if (!sas_info->is_logical_volume &&
			    (sas_info->fw.channel == phys_disk.PhysDiskBus &&
			    sas_info->fw.id == phys_disk.PhysDiskID)) {
				sas_info->is_hidden_raid_component = 1;
				sas_info->volume_id = starget->id;
			}
		}
		mutex_unlock(&ioc->sas_device_info_mutex);

	}

	/*
	 * Delete all matching devices out of the list
	 */
	mutex_lock(&ioc->sas_device_info_mutex);
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list,
	    list) {
		if (sas_info->is_logical_volume && sas_info->fw.id ==
		    starget->id) {
			list_del(&sas_info->list);
			kfree(sas_info);
		}
	}

	sas_info = kzalloc(sizeof(struct mptsas_device_info), GFP_KERNEL);
	if (sas_info) {
		sas_info->fw.id = starget->id;
		sas_info->os.id = starget->id;
		sas_info->os.channel = starget->channel;
		sas_info->is_logical_volume = 1;
		INIT_LIST_HEAD(&sas_info->list);
		list_add_tail(&sas_info->list, &ioc->sas_device_info_list);
	}
	mutex_unlock(&ioc->sas_device_info_mutex);

 out:
	if (buffer)
		dma_free_coherent(&ioc->pcidev->dev, hdr.PageLength * 4,
				  buffer, dma_handle);
}

/**
 *	mptsas_add_device_component_starget - adds a SCSI target device component
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@starget: SCSI target for this SCSI device
 *
 **/
static void
mptsas_add_device_component_starget(MPT_ADAPTER *ioc,
	struct scsi_target *starget)
{
	struct sas_rphy	*rphy;
	struct mptsas_phyinfo	*phy_info = NULL;
	struct mptsas_enclosure	enclosure_info;

	rphy = dev_to_rphy(starget->dev.parent);
	phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
			rphy->identify.sas_address);
	if (!phy_info)
		return;

	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
		(MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
		MPI_SAS_ENCLOS_PGAD_FORM_SHIFT),
		phy_info->attached.handle_enclosure);

	mptsas_add_device_component(ioc, phy_info->attached.channel,
		phy_info->attached.id, phy_info->attached.sas_address,
		phy_info->attached.device_info,
		phy_info->attached.slot, enclosure_info.enclosure_logical_id);
}

/**
 *	mptsas_del_device_component_by_os - Once a device has been removed, we mark the entry in the list as being cached
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: os mapped id's
 *	@id: Logical Target ID
 *
 **/
static void
mptsas_del_device_component_by_os(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct mptsas_device_info	*sas_info, *next;

	/*
	 * Set is_cached flag
	 */
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list,
		list) {
		if (sas_info->os.channel == channel && sas_info->os.id == id)
			sas_info->is_cached = 1;
	}
}

/**
 *	mptsas_del_device_components - Cleaning the list
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static void
mptsas_del_device_components(MPT_ADAPTER *ioc)
{
	struct mptsas_device_info	*sas_info, *next;

	mutex_lock(&ioc->sas_device_info_mutex);
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list,
		list) {
		list_del(&sas_info->list);
		kfree(sas_info);
	}
	mutex_unlock(&ioc->sas_device_info_mutex);
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
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: [%p]: deleting phy = %d\n",
		    ioc->name, __func__, port_details, i));
		port_details->num_phys--;
		port_details->phy_bitmask &= ~ (1 << phy_info->phy_id);
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		if (phy_info->phy) {
			devtprintk(ioc, dev_printk(KERN_DEBUG,
				&phy_info->phy->dev, MYIOC_s_FMT
				"delete phy %d, phy-obj (0x%p)\n", ioc->name,
				phy_info->phy_id, phy_info->phy));
			sas_port_delete_phy(port_details->port, phy_info->phy);
		}
		phy_info->port_details = NULL;
	}

	/*
	 * Populate and refresh the tree
	 */
	phy_info = port_info->phy_info;
	for (i = 0 ; i < port_info->num_phys ; i++, phy_info++) {
		sas_address = phy_info->attached.sas_address;
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "phy_id=%d sas_address=0x%018llX\n",
		    ioc->name, i, (unsigned long long)sas_address));
		if (!sas_address)
			continue;
		port_details = phy_info->port_details;
		/*
		 * Forming a port
		 */
		if (!port_details) {
			port_details = kzalloc(sizeof(struct
				mptsas_portinfo_details), GFP_KERNEL);
			if (!port_details)
				goto out;
			port_details->num_phys = 1;
			port_details->port_info = port_info;
			if (phy_info->phy_id < 64 )
				port_details->phy_bitmask |=
				    (1 << phy_info->phy_id);
			phy_info->sas_port_add_phy=1;
			dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "\t\tForming port\n\t\t"
			    "phy_id=%d sas_address=0x%018llX\n",
			    ioc->name, i, (unsigned long long)sas_address));
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
			dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "\t\tphy_id=%d sas_address=0x%018llX\n",
			    ioc->name, j, (unsigned long long)
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

	for (i = 0; i < port_info->num_phys; i++) {
		port_details = port_info->phy_info[i].port_details;
		if (!port_details)
			continue;
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: [%p]: phy_id=%02d num_phys=%02d "
		    "bitmask=0x%016llX\n", ioc->name, __func__,
		    port_details, i, port_details->num_phys,
		    (unsigned long long)port_details->phy_bitmask));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "\t\tport = %p rphy=%p\n",
		    ioc->name, port_details->port, port_details->rphy));
	}
	dsaswideprintk(ioc, printk("\n"));
	mutex_unlock(&ioc->sas_topology_mutex);
}

/**
 * mptsas_find_vtarget - find a virtual target device (FC LUN device or
 *				SCSI target device)
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @channel: channel number
 * @id: Logical Target ID
 *
 **/
static VirtTarget *
mptsas_find_vtarget(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct scsi_device 		*sdev;
	VirtDevice			*vdevice;
	VirtTarget 			*vtarget = NULL;

	shost_for_each_device(sdev, ioc->sh) {
		vdevice = sdev->hostdata;
		if ((vdevice == NULL) ||
			(vdevice->vtarget == NULL))
			continue;
		if ((vdevice->vtarget->tflags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT ||
		    vdevice->vtarget->raidVolume))
			continue;
		if (vdevice->vtarget->id == id &&
			vdevice->vtarget->channel == channel)
			vtarget = vdevice->vtarget;
	}
	return vtarget;
}

static void
mptsas_queue_device_delete(MPT_ADAPTER *ioc,
	MpiEventDataSasDeviceStatusChange_t *sas_event_data)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(*fw_event) +
			   sizeof(MpiEventDataSasDeviceStatusChange_t),
			   GFP_ATOMIC);
	if (!fw_event) {
		printk(MYIOC_s_WARN_FMT "%s: failed at (line=%d)\n",
		    ioc->name, __func__, __LINE__);
		return;
	}
	memcpy(fw_event->event_data, sas_event_data,
	    sizeof(MpiEventDataSasDeviceStatusChange_t));
	fw_event->event = MPI_EVENT_SAS_DEVICE_STATUS_CHANGE;
	fw_event->ioc = ioc;
	mptsas_add_fw_event(ioc, fw_event, msecs_to_jiffies(1));
}

static void
mptsas_queue_rescan(MPT_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(*fw_event), GFP_ATOMIC);
	if (!fw_event) {
		printk(MYIOC_s_WARN_FMT "%s: failed at (line=%d)\n",
		    ioc->name, __func__, __LINE__);
		return;
	}
	fw_event->event = -1;
	fw_event->ioc = ioc;
	mptsas_add_fw_event(ioc, fw_event, msecs_to_jiffies(1));
}


/**
 * mptsas_target_reset - Issues TARGET_RESET to end device using
 *			 handshaking method
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @channel: channel number
 * @id: Logical Target ID for reset
 *
 * Return: (1) success
 *         (0) failure
 *
 **/
static int
mptsas_target_reset(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	if (mpt_set_taskmgmt_in_progress_flag(ioc) != 0)
		return 0;


	mf = mpt_get_msg_frame(mptsasDeviceResetCtx, ioc);
	if (mf == NULL) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT
			"%s, no msg frames @%d!!\n", ioc->name,
			__func__, __LINE__));
		goto out_fail;
	}

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt request (mf=%p)\n",
		ioc->name, mf));

	/* Format the Request
	 */
	pScsiTm = (SCSITaskMgmt_t *) mf;
	memset (pScsiTm, 0, sizeof(SCSITaskMgmt_t));
	pScsiTm->TargetID = id;
	pScsiTm->Bus = channel;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	pScsiTm->MsgFlags = MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION;

	DBG_DUMP_TM_REQUEST_FRAME(ioc, (u32 *)mf);

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	   "TaskMgmt type=%d (sas device delete) fw_channel = %d fw_id = %d)\n",
	   ioc->name, MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET, channel, id));

	mpt_put_msg_frame_hi_pri(mptsasDeviceResetCtx, ioc, mf);

	return 1;

 out_fail:

	mpt_clear_taskmgmt_in_progress_flag(ioc);
	return 0;
}

static void
mptsas_block_io_sdev(struct scsi_device *sdev, void *data)
{
	scsi_device_set_state(sdev, SDEV_BLOCK);
}

static void
mptsas_block_io_starget(struct scsi_target *starget)
{
	if (starget)
		starget_for_each_device(starget, NULL, mptsas_block_io_sdev);
}

/**
 * mptsas_target_reset_queue - queue a target reset
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @sas_event_data: SAS Device Status Change Event data
 *
 * Receive request for TARGET_RESET after receiving a firmware
 * event NOT_RESPONDING_EVENT, then put command in link list
 * and queue if task_queue already in use.
 *
 **/
static void
mptsas_target_reset_queue(MPT_ADAPTER *ioc,
    EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data)
{
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
	VirtTarget *vtarget = NULL;
	struct mptsas_target_reset_event *target_reset_list;
	u8		id, channel;

	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	vtarget = mptsas_find_vtarget(ioc, channel, id);
	if (vtarget) {
		mptsas_block_io_starget(vtarget->starget);
		vtarget->deleted = 1; /* block IO */
	}

	target_reset_list = kzalloc(sizeof(struct mptsas_target_reset_event),
	    GFP_ATOMIC);
	if (!target_reset_list) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT
			"%s, failed to allocate mem @%d..!!\n",
			ioc->name, __func__, __LINE__));
		return;
	}

	memcpy(&target_reset_list->sas_event_data, sas_event_data,
		sizeof(*sas_event_data));
	list_add_tail(&target_reset_list->list, &hd->target_reset_list);

	target_reset_list->time_count = jiffies;

	if (mptsas_target_reset(ioc, channel, id)) {
		target_reset_list->target_reset_issued = 1;
	}
}

/**
 * mptsas_schedule_target_reset- send pending target reset
 * @iocp: per adapter object
 *
 * This function will delete scheduled target reset from the list and
 * try to send next target reset. This will be called from completion
 * context of any Task management command.
 */

void
mptsas_schedule_target_reset(void *iocp)
{
	MPT_ADAPTER *ioc = (MPT_ADAPTER *)(iocp);
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
	struct list_head *head = &hd->target_reset_list;
	struct mptsas_target_reset_event	*target_reset_list;
	u8		id, channel;
	/*
	 * issue target reset to next device in the queue
	 */

	if (list_empty(head))
		return;

	target_reset_list = list_entry(head->next,
		struct mptsas_target_reset_event, list);

	id = target_reset_list->sas_event_data.TargetID;
	channel = target_reset_list->sas_event_data.Bus;
	target_reset_list->time_count = jiffies;

	if (mptsas_target_reset(ioc, channel, id))
		target_reset_list->target_reset_issued = 1;
	return;
}


/**
 *	mptsas_taskmgmt_complete - complete SAS task management function
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: MPT message frame
 *	@mr: SCSI Task Management Reply structure ptr (may be %NULL)
 *
 *	Completion for TARGET_RESET after NOT_RESPONDING_EVENT, enable work
 *	queue to finish off removing device from upper layers, then send next
 *	TARGET_RESET in the queue.
 **/
static int
mptsas_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
        struct list_head *head = &hd->target_reset_list;
	u8		id, channel;
	struct mptsas_target_reset_event	*target_reset_list;
	SCSITaskMgmtReply_t *pScsiTmReply;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt completed: "
	    "(mf = %p, mr = %p)\n", ioc->name, mf, mr));

	pScsiTmReply = (SCSITaskMgmtReply_t *)mr;
	if (!pScsiTmReply)
		return 0;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "\tTaskMgmt completed: fw_channel = %d, fw_id = %d,\n"
	    "\ttask_type = 0x%02X, iocstatus = 0x%04X "
	    "loginfo = 0x%08X,\n\tresponse_code = 0x%02X, "
	    "term_cmnds = %d\n", ioc->name,
	    pScsiTmReply->Bus, pScsiTmReply->TargetID,
	    pScsiTmReply->TaskType,
	    le16_to_cpu(pScsiTmReply->IOCStatus),
	    le32_to_cpu(pScsiTmReply->IOCLogInfo),
	    pScsiTmReply->ResponseCode,
	    le32_to_cpu(pScsiTmReply->TerminationCount)));

	if (pScsiTmReply->ResponseCode)
		mptscsih_taskmgmt_response_code(ioc,
		pScsiTmReply->ResponseCode);

	if (pScsiTmReply->TaskType ==
	    MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK || pScsiTmReply->TaskType ==
	     MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET) {
		ioc->taskmgmt_cmds.status |= MPT_MGMT_STATUS_COMMAND_GOOD;
		ioc->taskmgmt_cmds.status |= MPT_MGMT_STATUS_RF_VALID;
		memcpy(ioc->taskmgmt_cmds.reply, mr,
		    min(MPT_DEFAULT_FRAME_SIZE, 4 * mr->u.reply.MsgLength));
		if (ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_PENDING) {
			ioc->taskmgmt_cmds.status &= ~MPT_MGMT_STATUS_PENDING;
			complete(&ioc->taskmgmt_cmds.done);
			return 1;
		}
		return 0;
	}

	mpt_clear_taskmgmt_in_progress_flag(ioc);

	if (list_empty(head))
		return 1;

	target_reset_list = list_entry(head->next,
	    struct mptsas_target_reset_event, list);

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "TaskMgmt: completed (%d seconds)\n",
	    ioc->name, jiffies_to_msecs(jiffies -
	    target_reset_list->time_count)/1000));

	id = pScsiTmReply->TargetID;
	channel = pScsiTmReply->Bus;
	target_reset_list->time_count = jiffies;

	/*
	 * retry target reset
	 */
	if (!target_reset_list->target_reset_issued) {
		if (mptsas_target_reset(ioc, channel, id))
			target_reset_list->target_reset_issued = 1;
		return 1;
	}

	/*
	 * enable work queue to remove device from upper layers
	 */
	list_del(&target_reset_list->list);
	if (!ioc->fw_events_off)
		mptsas_queue_device_delete(ioc,
			&target_reset_list->sas_event_data);


	ioc->schedule_target_reset(ioc);

	return 1;
}

/**
 * mptsas_ioc_reset - issue an IOC reset for this reset phase
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @reset_phase: id of phase of reset
 *
 **/
static int
mptsas_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	MPT_SCSI_HOST	*hd;
	int rc;

	rc = mptscsih_ioc_reset(ioc, reset_phase);
	if ((ioc->bus_type != SAS) || (!rc))
		return rc;

	hd = shost_priv(ioc->sh);
	if (!hd->ioc)
		goto out;

	switch (reset_phase) {
	case MPT_IOC_SETUP_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_SETUP_RESET\n", ioc->name, __func__));
		mptsas_fw_event_off(ioc);
		break;
	case MPT_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT_IOC_POST_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_POST_RESET\n", ioc->name, __func__));
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_PENDING) {
			ioc->sas_mgmt.status |= MPT_MGMT_STATUS_DID_IOCRESET;
			complete(&ioc->sas_mgmt.done);
		}
		mptsas_cleanup_fw_event_q(ioc);
		mptsas_queue_rescan(ioc);
		break;
	default:
		break;
	}

 out:
	return rc;
}


/**
 * enum device_state - TUR device state
 * @DEVICE_RETRY: need to retry the TUR
 * @DEVICE_ERROR: TUR return error, don't add device
 * @DEVICE_READY: device can be added
 *
 */
enum device_state{
	DEVICE_RETRY,
	DEVICE_ERROR,
	DEVICE_READY,
};

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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
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
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
 out:
	return error;
}

/**
 *	mptsas_add_end_device - report a new end device to sas transport layer
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info: describes attached device
 *
 *	return (0) success (1) failure
 *
 **/
static int
mptsas_add_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info)
{
	struct sas_rphy *rphy;
	struct sas_port *port;
	struct sas_identify identify;
	char *ds = NULL;
	u8 fw_id;

	if (!phy_info) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: exit at line=%d\n", ioc->name,
			 __func__, __LINE__));
		return 1;
	}

	fw_id = phy_info->attached.id;

	if (mptsas_get_rphy(phy_info)) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return 2;
	}

	port = mptsas_get_port(phy_info);
	if (!port) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return 3;
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

	printk(MYIOC_s_INFO_FMT "attaching %s device: fw_channel %d, fw_id %d,"
	    " phy %d, sas_addr 0x%llx\n", ioc->name, ds,
	    phy_info->attached.channel, phy_info->attached.id,
	    phy_info->attached.phy_id, (unsigned long long)
	    phy_info->attached.sas_address);

	mptsas_parse_device_info(&identify, &phy_info->attached);
	rphy = sas_end_device_alloc(port);
	if (!rphy) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return 5; /* non-fatal: an rphy can be added later */
	}

	rphy->identify = identify;
	if (sas_rphy_add(rphy)) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		sas_rphy_free(rphy);
		return 6;
	}
	mptsas_set_rphy(ioc, phy_info, rphy);
	return 0;
}

/**
 *	mptsas_del_end_device - report a deleted end device to sas transport layer
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info: describes attached device
 *
 **/
static void
mptsas_del_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info)
{
	struct sas_rphy *rphy;
	struct sas_port *port;
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info_parent;
	int i;
	char *ds = NULL;
	u8 fw_id;
	u64 sas_address;

	if (!phy_info)
		return;

	fw_id = phy_info->attached.id;
	sas_address = phy_info->attached.sas_address;

	if (!phy_info->port_details) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return;
	}
	rphy = mptsas_get_rphy(phy_info);
	if (!rphy) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return;
	}

	if (phy_info->attached.device_info & MPI_SAS_DEVICE_INFO_SSP_INITIATOR
		|| phy_info->attached.device_info
			& MPI_SAS_DEVICE_INFO_SMP_INITIATOR
		|| phy_info->attached.device_info
			& MPI_SAS_DEVICE_INFO_STP_INITIATOR)
		ds = "initiator";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET)
		ds = "ssp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET)
		ds = "stp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
		ds = "sata";

	dev_printk(KERN_DEBUG, &rphy->dev, MYIOC_s_FMT
	    "removing %s device: fw_channel %d, fw_id %d, phy %d,"
	    "sas_addr 0x%llx\n", ioc->name, ds, phy_info->attached.channel,
	    phy_info->attached.id, phy_info->attached.phy_id,
	    (unsigned long long) sas_address);

	port = mptsas_get_port(phy_info);
	if (!port) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, fw_id, __LINE__));
		return;
	}
	port_info = phy_info->portinfo;
	phy_info_parent = port_info->phy_info;
	for (i = 0; i < port_info->num_phys; i++, phy_info_parent++) {
		if (!phy_info_parent->phy)
			continue;
		if (phy_info_parent->attached.sas_address !=
		    sas_address)
			continue;
		dev_printk(KERN_DEBUG, &phy_info_parent->phy->dev,
		    MYIOC_s_FMT "delete phy %d, phy-obj (0x%p)\n",
		    ioc->name, phy_info_parent->phy_id,
		    phy_info_parent->phy);
		sas_port_delete_phy(port, phy_info_parent->phy);
	}

	dev_printk(KERN_DEBUG, &port->dev, MYIOC_s_FMT
	    "delete port %d, sas_addr (0x%llx)\n", ioc->name,
	     port->port_identifier, (unsigned long long)sas_address);
	sas_port_delete(port);
	mptsas_set_port(ioc, phy_info, NULL);
	mptsas_port_delete(ioc, phy_info->port_details);
}

static struct mptsas_phyinfo *
mptsas_refreshing_device_handles(MPT_ADAPTER *ioc,
	struct mptsas_devinfo *sas_device)
{
	struct mptsas_phyinfo *phy_info;
	struct mptsas_portinfo *port_info;
	int i;

	phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
	    sas_device->sas_address);
	if (!phy_info)
		goto out;
	port_info = phy_info->portinfo;
	if (!port_info)
		goto out;
	mutex_lock(&ioc->sas_topology_mutex);
	for (i = 0; i < port_info->num_phys; i++) {
		if (port_info->phy_info[i].attached.sas_address !=
			sas_device->sas_address)
			continue;
		port_info->phy_info[i].attached.channel = sas_device->channel;
		port_info->phy_info[i].attached.id = sas_device->id;
		port_info->phy_info[i].attached.sas_address =
		    sas_device->sas_address;
		port_info->phy_info[i].attached.handle = sas_device->handle;
		port_info->phy_info[i].attached.handle_parent =
		    sas_device->handle_parent;
		port_info->phy_info[i].attached.handle_enclosure =
		    sas_device->handle_enclosure;
	}
	mutex_unlock(&ioc->sas_topology_mutex);
 out:
	return phy_info;
}

/**
 * mptsas_firmware_event_work - work thread for processing fw events
 * @work: work queue payload containing info describing the event
 * Context: user
 *
 */
static void
mptsas_firmware_event_work(struct work_struct *work)
{
	struct fw_event_work *fw_event =
		container_of(work, struct fw_event_work, work.work);
	MPT_ADAPTER *ioc = fw_event->ioc;

	/* special rescan topology handling */
	if (fw_event->event == -1) {
		if (ioc->in_rescan) {
			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"%s: rescan ignored as it is in progress\n",
				ioc->name, __func__));
			return;
		}
		devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: rescan after "
		    "reset\n", ioc->name, __func__));
		ioc->in_rescan = 1;
		mptsas_not_responding_devices(ioc);
		mptsas_scan_sas_topology(ioc);
		ioc->in_rescan = 0;
		mptsas_free_fw_event(ioc, fw_event);
		mptsas_fw_event_on(ioc);
		return;
	}

	/* events handling turned off during host reset */
	if (ioc->fw_events_off) {
		mptsas_free_fw_event(ioc, fw_event);
		return;
	}

	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: fw_event=(0x%p), "
	    "event = (0x%02x)\n", ioc->name, __func__, fw_event,
	    (fw_event->event & 0xFF)));

	switch (fw_event->event) {
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
		mptsas_send_sas_event(fw_event);
		break;
	case MPI_EVENT_INTEGRATED_RAID:
		mptsas_send_raid_event(fw_event);
		break;
	case MPI_EVENT_IR2:
		mptsas_send_ir2_event(fw_event);
		break;
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
		mptbase_sas_persist_operation(ioc,
		    MPI_SAS_OP_CLEAR_NOT_PRESENT);
		mptsas_free_fw_event(ioc, fw_event);
		break;
	case MPI_EVENT_SAS_BROADCAST_PRIMITIVE:
		mptsas_broadcast_primitive_work(fw_event);
		break;
	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
		mptsas_send_expander_event(fw_event);
		break;
	case MPI_EVENT_SAS_PHY_LINK_STATUS:
		mptsas_send_link_status_event(fw_event);
		break;
	case MPI_EVENT_QUEUE_FULL:
		mptsas_handle_queue_full_event(fw_event);
		break;
	}
}



static int
mptsas_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST	*hd = shost_priv(host);
	MPT_ADAPTER	*ioc = hd->ioc;
	VirtDevice	*vdevice = sdev->hostdata;

	if (vdevice->vtarget->deleted) {
		sdev_printk(KERN_INFO, sdev, "clearing deleted flag\n");
		vdevice->vtarget->deleted = 0;
	}

	/*
	 * RAID volumes placed beyond the last expected port.
	 * Ignore sending sas mode pages in that case..
	 */
	if (sdev->channel == MPTSAS_RAID_CHANNEL) {
		mptsas_add_device_component_starget_ir(ioc, scsi_target(sdev));
		goto out;
	}

	sas_read_port_mode_page(sdev);

	mptsas_add_device_component_starget(ioc, scsi_target(sdev));

 out:
	return mptscsih_slave_configure(sdev);
}

static int
mptsas_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(&starget->dev);
	MPT_SCSI_HOST		*hd = shost_priv(host);
	VirtTarget		*vtarget;
	u8			id, channel;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;
	MPT_ADAPTER		*ioc = hd->ioc;

	vtarget = kzalloc(sizeof(VirtTarget), GFP_KERNEL);
	if (!vtarget)
		return -ENOMEM;

	vtarget->starget = starget;
	vtarget->ioc_id = ioc->id;
	vtarget->tflags = MPT_TARGET_FLAGS_Q_YES;
	id = starget->id;
	channel = 0;

	/*
	 * RAID volumes placed beyond the last expected port.
	 */
	if (starget->channel == MPTSAS_RAID_CHANNEL) {
		if (!ioc->raid_data.pIocPg2) {
			kfree(vtarget);
			return -ENXIO;
		}
		for (i = 0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
			if (id == ioc->raid_data.pIocPg2->
					RaidVolume[i].VolumeID) {
				channel = ioc->raid_data.pIocPg2->
					RaidVolume[i].VolumeBus;
			}
		}
		vtarget->raidVolume = 1;
		goto out;
	}

	rphy = dev_to_rphy(starget->dev.parent);
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
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
			if (mptscsih_is_phys_disk(ioc, channel, id)) {
				id = mptscsih_raid_id_to_num(ioc,
						channel, id);
				vtarget->tflags |=
				    MPT_TARGET_FLAGS_RAID_COMPONENT;
				p->phy_info[i].attached.phys_disk_num = id;
			}
			mutex_unlock(&ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

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
	MPT_SCSI_HOST		*hd = shost_priv(host);
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;
	MPT_ADAPTER	*ioc = hd->ioc;
	VirtTarget	*vtarget;

	if (!starget->hostdata)
		return;

	vtarget = starget->hostdata;

	mptsas_del_device_component_by_os(ioc, starget->channel,
	    starget->id);


	if (starget->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	rphy = dev_to_rphy(starget->dev.parent);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;

			starget_printk(KERN_INFO, starget, MYIOC_s_FMT
			"delete device: fw_channel %d, fw_id %d, phy %d, "
			"sas_addr 0x%llx\n", ioc->name,
			p->phy_info[i].attached.channel,
			p->phy_info[i].attached.id,
			p->phy_info[i].attached.phy_id, (unsigned long long)
			p->phy_info[i].attached.sas_address);

			mptsas_set_starget(&p->phy_info[i], NULL);
		}
	}

 out:
	vtarget->starget = NULL;
	kfree(starget->hostdata);
	starget->hostdata = NULL;
}


static int
mptsas_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = shost_priv(host);
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	VirtDevice		*vdevice;
	struct scsi_target 	*starget;
	int 			i;
	MPT_ADAPTER *ioc = hd->ioc;

	vdevice = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdevice) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kzalloc(%zd) FAILED!\n",
				ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}
	starget = scsi_target(sdev);
	vdevice->vtarget = starget->hostdata;

	if (sdev->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	rphy = dev_to_rphy(sdev->sdev_target->dev.parent);
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;
			vdevice->lun = sdev->lun;
			/*
			 * Exposing hidden raid components
			 */
			if (mptscsih_is_phys_disk(ioc,
			    p->phy_info[i].attached.channel,
			    p->phy_info[i].attached.id))
				sdev->no_uld_attach = 1;
			mutex_unlock(&ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	kfree(vdevice);
	return -ENXIO;

 out:
	vdevice->vtarget->num_luns++;
	sdev->hostdata = vdevice;
	return 0;
}

static int
mptsas_qcmd(struct Scsi_Host *shost, struct scsi_cmnd *SCpnt)
{
	MPT_SCSI_HOST	*hd;
	MPT_ADAPTER	*ioc;
	VirtDevice	*vdevice = SCpnt->device->hostdata;

	if (!vdevice || !vdevice->vtarget || vdevice->vtarget->deleted) {
		SCpnt->result = DID_NO_CONNECT << 16;
		scsi_done(SCpnt);
		return 0;
	}

	hd = shost_priv(shost);
	ioc = hd->ioc;

	if (ioc->sas_discovery_quiesce_io)
		return SCSI_MLQUEUE_HOST_BUSY;

	if (ioc->debug_level & MPT_DEBUG_SCSI)
		scsi_print_command(SCpnt);

	return mptscsih_qcmd(SCpnt);
}

/**
 *	mptsas_eh_timed_out - resets the scsi_cmnd timeout
 *		if the device under question is currently in the
 *		device removal delay.
 *	@sc: scsi command that the midlayer is about to time out
 *
 **/
static enum scsi_timeout_action mptsas_eh_timed_out(struct scsi_cmnd *sc)
{
	MPT_SCSI_HOST *hd;
	MPT_ADAPTER   *ioc;
	VirtDevice    *vdevice;
	enum scsi_timeout_action rc = SCSI_EH_NOT_HANDLED;

	hd = shost_priv(sc->device->host);
	if (hd == NULL) {
		printk(KERN_ERR MYNAM ": %s: Can't locate host! (sc=%p)\n",
		    __func__, sc);
		goto done;
	}

	ioc = hd->ioc;
	if (ioc->bus_type != SAS) {
		printk(KERN_ERR MYNAM ": %s: Wrong bus type (sc=%p)\n",
		    __func__, sc);
		goto done;
	}

	/* In case if IOC is in reset from internal context.
	*  Do not execute EEH for the same IOC. SML should to reset timer.
	*/
	if (ioc->ioc_reset_in_progress) {
		dtmprintk(ioc, printk(MYIOC_s_WARN_FMT ": %s: ioc is in reset,"
		    "SML need to reset the timer (sc=%p)\n",
		    ioc->name, __func__, sc));
		rc = SCSI_EH_RESET_TIMER;
	}
	vdevice = sc->device->hostdata;
	if (vdevice && vdevice->vtarget && (vdevice->vtarget->inDMD
		|| vdevice->vtarget->deleted)) {
		dtmprintk(ioc, printk(MYIOC_s_WARN_FMT ": %s: target removed "
		    "or in device removal delay (sc=%p)\n",
		    ioc->name, __func__, sc));
		rc = SCSI_EH_RESET_TIMER;
		goto done;
	}

done:
	return rc;
}


static const struct scsi_host_template mptsas_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptsas",
	.show_info			= mptscsih_show_info,
	.name				= "MPT SAS Host",
	.info				= mptscsih_info,
	.queuecommand			= mptsas_qcmd,
	.target_alloc			= mptsas_target_alloc,
	.slave_alloc			= mptsas_slave_alloc,
	.slave_configure		= mptsas_slave_configure,
	.target_destroy			= mptsas_target_destroy,
	.slave_destroy			= mptscsih_slave_destroy,
	.change_queue_depth 		= mptscsih_change_queue_depth,
	.eh_timed_out			= mptsas_eh_timed_out,
	.eh_abort_handler		= mptscsih_abort,
	.eh_device_reset_handler	= mptscsih_dev_reset,
	.eh_host_reset_handler		= mptscsih_host_reset,
	.bios_param			= mptscsih_bios_param,
	.can_queue			= MPT_SAS_CAN_QUEUE,
	.this_id			= -1,
	.sg_tablesize			= MPT_SCSI_SG_DEPTH,
	.max_sectors			= 8192,
	.cmd_per_lun			= 7,
	.dma_alignment			= 511,
	.shost_groups			= mptscsih_host_attr_groups,
	.no_write_same			= 1,
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		return error;
	if (!hdr.ExtPageLength)
		return -ENXIO;

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg1(ioc, buffer);

	phy->invalid_dword_count = le32_to_cpu(buffer->InvalidDwordCount);
	phy->running_disparity_error_count =
		le32_to_cpu(buffer->RunningDisparityErrorCount);
	phy->loss_of_dword_sync_count =
		le32_to_cpu(buffer->LossDwordSynchCount);
	phy->phy_reset_problem_count =
		le32_to_cpu(buffer->PhyResetProblemCount);

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
	return error;
}

static int mptsas_mgmt_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req,
		MPT_FRAME_HDR *reply)
{
	ioc->sas_mgmt.status |= MPT_MGMT_STATUS_COMMAND_GOOD;
	if (reply != NULL) {
		ioc->sas_mgmt.status |= MPT_MGMT_STATUS_RF_VALID;
		memcpy(ioc->sas_mgmt.reply, reply,
		    min(ioc->reply_sz, 4 * reply->u.reply.MsgLength));
	}

	if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_PENDING) {
		ioc->sas_mgmt.status &= ~MPT_MGMT_STATUS_PENDING;
		complete(&ioc->sas_mgmt.done);
		return 1;
	}
	return 0;
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

	INITIALIZE_MGMT_STATUS(ioc->sas_mgmt.status)
	mpt_put_msg_frame(mptsasMgmtCtx, ioc, mf);

	timeleft = wait_for_completion_timeout(&ioc->sas_mgmt.done,
			10 * HZ);
	if (!(ioc->sas_mgmt.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		error = -ETIME;
		mpt_free_msg_frame(ioc, mf);
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out_unlock;
		if (!timeleft)
			mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP);
		goto out_unlock;
	}

	/* a reply frame is expected */
	if ((ioc->sas_mgmt.status &
	    MPT_MGMT_STATUS_RF_VALID) == 0) {
		error = -ENXIO;
		goto out_unlock;
	}

	/* process the completed Reply Message Frame */
	reply = (SasIoUnitControlReply_t *)ioc->sas_mgmt.reply;
	if (reply->IOCStatus != MPI_IOCSTATUS_SUCCESS) {
		printk(MYIOC_s_INFO_FMT "%s: IOCStatus=0x%X IOCLogInfo=0x%X\n",
		    ioc->name, __func__, reply->IOCStatus, reply->IOCLogInfo);
		error = -ENXIO;
		goto out_unlock;
	}

	error = 0;

 out_unlock:
	CLEAR_MGMT_STATUS(ioc->sas_mgmt.status)
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

static void mptsas_smp_handler(struct bsg_job *job, struct Scsi_Host *shost,
		struct sas_rphy *rphy)
{
	MPT_ADAPTER *ioc = ((MPT_SCSI_HOST *) shost->hostdata)->ioc;
	MPT_FRAME_HDR *mf;
	SmpPassthroughRequest_t *smpreq;
	int flagsLength;
	unsigned long timeleft;
	char *psge;
	u64 sas_address = 0;
	unsigned int reslen = 0;
	int ret = -EINVAL;

	/* do we need to support multiple segments? */
	if (job->request_payload.sg_cnt > 1 ||
	    job->reply_payload.sg_cnt > 1) {
		printk(MYIOC_s_ERR_FMT "%s: multiple segments req %u, rsp %u\n",
		    ioc->name, __func__, job->request_payload.payload_len,
		    job->reply_payload.payload_len);
		goto out;
	}

	ret = mutex_lock_interruptible(&ioc->sas_mgmt.mutex);
	if (ret)
		goto out;

	mf = mpt_get_msg_frame(mptsasMgmtCtx, ioc);
	if (!mf) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	smpreq = (SmpPassthroughRequest_t *)mf;
	memset(smpreq, 0, sizeof(*smpreq));

	smpreq->RequestDataLength =
		cpu_to_le16(job->request_payload.payload_len - 4);
	smpreq->Function = MPI_FUNCTION_SMP_PASSTHROUGH;

	if (rphy)
		sas_address = rphy->identify.sas_address;
	else {
		struct mptsas_portinfo *port_info;

		mutex_lock(&ioc->sas_topology_mutex);
		port_info = ioc->hba_port_info;
		if (port_info && port_info->phy_info)
			sas_address =
				port_info->phy_info[0].phy->identify.sas_address;
		mutex_unlock(&ioc->sas_topology_mutex);
	}

	*((u64 *)&smpreq->SASAddress) = cpu_to_le64(sas_address);

	psge = (char *)
		(((int *) mf) + (offsetof(SmpPassthroughRequest_t, SGL) / 4));

	/* request */
	flagsLength = (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		       MPI_SGE_FLAGS_END_OF_BUFFER |
		       MPI_SGE_FLAGS_DIRECTION)
		       << MPI_SGE_FLAGS_SHIFT;

	if (!dma_map_sg(&ioc->pcidev->dev, job->request_payload.sg_list,
			1, DMA_BIDIRECTIONAL))
		goto put_mf;

	flagsLength |= (sg_dma_len(job->request_payload.sg_list) - 4);
	ioc->add_sge(psge, flagsLength,
			sg_dma_address(job->request_payload.sg_list));
	psge += ioc->SGE_size;

	/* response */
	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_IOC_TO_HOST |
		MPI_SGE_FLAGS_END_OF_BUFFER;

	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;

	if (!dma_map_sg(&ioc->pcidev->dev, job->reply_payload.sg_list,
			1, DMA_BIDIRECTIONAL))
		goto unmap_out;
	flagsLength |= sg_dma_len(job->reply_payload.sg_list) + 4;
	ioc->add_sge(psge, flagsLength,
			sg_dma_address(job->reply_payload.sg_list));

	INITIALIZE_MGMT_STATUS(ioc->sas_mgmt.status)
	mpt_put_msg_frame(mptsasMgmtCtx, ioc, mf);

	timeleft = wait_for_completion_timeout(&ioc->sas_mgmt.done, 10 * HZ);
	if (!(ioc->sas_mgmt.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		ret = -ETIME;
		mpt_free_msg_frame(ioc, mf);
		mf = NULL;
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto unmap_in;
		if (!timeleft)
			mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP);
		goto unmap_in;
	}
	mf = NULL;

	if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_RF_VALID) {
		SmpPassthroughReply_t *smprep;

		smprep = (SmpPassthroughReply_t *)ioc->sas_mgmt.reply;
		memcpy(job->reply, smprep, sizeof(*smprep));
		job->reply_len = sizeof(*smprep);
		reslen = smprep->ResponseDataLength;
	} else {
		printk(MYIOC_s_ERR_FMT
		    "%s: smp passthru reply failed to be returned\n",
		    ioc->name, __func__);
		ret = -ENXIO;
	}

unmap_in:
	dma_unmap_sg(&ioc->pcidev->dev, job->reply_payload.sg_list, 1,
			DMA_BIDIRECTIONAL);
unmap_out:
	dma_unmap_sg(&ioc->pcidev->dev, job->request_payload.sg_list, 1,
			DMA_BIDIRECTIONAL);
put_mf:
	if (mf)
		mpt_free_msg_frame(ioc, mf);
out_unlock:
	CLEAR_MGMT_STATUS(ioc->sas_mgmt.status)
	mutex_unlock(&ioc->sas_mgmt.mutex);
out:
	bsg_job_done(job, ret, reslen);
}

static struct sas_function_template mptsas_transport_functions = {
	.get_linkerrors		= mptsas_get_linkerrors,
	.get_enclosure_identifier = mptsas_get_enclosure_identifier,
	.get_bay_identifier	= mptsas_get_bay_identifier,
	.phy_reset		= mptsas_phy_reset,
	.smp_handler		= mptsas_smp_handler,
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
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
		sizeof(struct mptsas_phyinfo), GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	ioc->nvdata_version_persistent =
	    le16_to_cpu(buffer->NvdataVersionPersistent);
	ioc->nvdata_version_default =
	    le16_to_cpu(buffer->NvdataVersionDefault);

	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_print_phy_data(ioc, &buffer->PhyData[i]);
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
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
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
	u8 device_missing_delay;

	memset(&hdr, 0, sizeof(ConfigExtendedPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));

	cfg.cfghdr.ehdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;
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

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
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
	device_missing_delay = buffer->ReportDeviceMissingDelay;
	ioc->device_missing_delay = (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_UNIT_16) ?
	    (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16 :
	    device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

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

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg0(ioc, buffer);

	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	memset(device_info, 0, sizeof(struct mptsas_devinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);

	if (error == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		error = -ENODEV;
		goto out_free_consistent;
	}

	if (error)
		goto out_free_consistent;

	mptsas_print_device_pg0(ioc, buffer);

	memset(device_info, 0, sizeof(struct mptsas_devinfo));
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
	device_info->flags = le16_to_cpu(buffer->Flags);

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
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
	__le64 sas_address;

	memset(port_info, 0, sizeof(struct mptsas_portinfo));
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	memset(port_info, 0, sizeof(struct mptsas_portinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		error = -ENODEV;
		goto out_free_consistent;
	}

	if (error)
		goto out_free_consistent;

	/* save config data */
	port_info->num_phys = (buffer->NumPhys) ? buffer->NumPhys : 1;
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(struct mptsas_phyinfo), GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	memcpy(&sas_address, &buffer->SASAddress, sizeof(__le64));
	for (i = 0; i < port_info->num_phys; i++) {
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(buffer->DevHandle);
		port_info->phy_info[i].identify.sas_address =
		    le64_to_cpu(sas_address);
		port_info->phy_info[i].identify.handle_parent =
		    le16_to_cpu(buffer->ParentDevHandle);
	}

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
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

	hdr.PageVersion = MPI_SASEXPANDER1_PAGEVERSION;
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
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4,
				    &dma_handle, GFP_KERNEL);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);

	if (error == MPI_IOCSTATUS_CONFIG_INVALID_PAGE) {
		error = -ENODEV;
		goto out_free_consistent;
	}

	if (error)
		goto out_free_consistent;


	mptsas_print_expander_pg1(ioc, buffer);

	/* save config data */
	phy_info->phy_id = buffer->PhyIdentifier;
	phy_info->port_id = buffer->PhysicalPort;
	phy_info->negotiated_link_rate = buffer->NegotiatedLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);

 out_free_consistent:
	dma_free_coherent(&ioc->pcidev->dev, hdr.ExtPageLength * 4, buffer,
			  dma_handle);
 out:
	return error;
}

struct rep_manu_request{
	u8 smp_frame_type;
	u8 function;
	u8 reserved;
	u8 request_length;
};

struct rep_manu_reply{
	u8 smp_frame_type; /* 0x41 */
	u8 function; /* 0x01 */
	u8 function_result;
	u8 response_length;
	u16 expander_change_count;
	u8 reserved0[2];
	u8 sas_format:1;
	u8 reserved1:7;
	u8 reserved2[3];
	u8 vendor_id[SAS_EXPANDER_VENDOR_ID_LEN];
	u8 product_id[SAS_EXPANDER_PRODUCT_ID_LEN];
	u8 product_rev[SAS_EXPANDER_PRODUCT_REV_LEN];
	u8 component_vendor_id[SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN];
	u16 component_id;
	u8 component_revision_id;
	u8 reserved3;
	u8 vendor_specific[8];
};

/**
  * mptsas_exp_repmanufacture_info - sets expander manufacturer info
  * @ioc: per adapter object
  * @sas_address: expander sas address
  * @edev: the sas_expander_device object
  *
  * For an edge expander or a fanout expander:
  * fills in the sas_expander_device object when SMP port is created.
  *
  * Return: 0 for success, non-zero for failure.
  */
static int
mptsas_exp_repmanufacture_info(MPT_ADAPTER *ioc,
	u64 sas_address, struct sas_expander_device *edev)
{
	MPT_FRAME_HDR *mf;
	SmpPassthroughRequest_t *smpreq;
	SmpPassthroughReply_t *smprep;
	struct rep_manu_reply *manufacture_reply;
	struct rep_manu_request *manufacture_request;
	int ret;
	int flagsLength;
	unsigned long timeleft;
	char *psge;
	unsigned long flags;
	void *data_out = NULL;
	dma_addr_t data_out_dma = 0;
	u32 sz;

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress) {
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		printk(MYIOC_s_INFO_FMT "%s: host reset in progress!\n",
			__func__, ioc->name);
		return -EFAULT;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	ret = mutex_lock_interruptible(&ioc->sas_mgmt.mutex);
	if (ret)
		goto out;

	mf = mpt_get_msg_frame(mptsasMgmtCtx, ioc);
	if (!mf) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	smpreq = (SmpPassthroughRequest_t *)mf;
	memset(smpreq, 0, sizeof(*smpreq));

	sz = sizeof(struct rep_manu_request) + sizeof(struct rep_manu_reply);

	data_out = dma_alloc_coherent(&ioc->pcidev->dev, sz, &data_out_dma,
				      GFP_KERNEL);
	if (!data_out) {
		printk(KERN_ERR "Memory allocation failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		ret = -ENOMEM;
		goto put_mf;
	}

	manufacture_request = data_out;
	manufacture_request->smp_frame_type = 0x40;
	manufacture_request->function = 1;
	manufacture_request->reserved = 0;
	manufacture_request->request_length = 0;

	smpreq->Function = MPI_FUNCTION_SMP_PASSTHROUGH;
	smpreq->PhysicalPort = 0xFF;
	*((u64 *)&smpreq->SASAddress) = cpu_to_le64(sas_address);
	smpreq->RequestDataLength = sizeof(struct rep_manu_request);

	psge = (char *)
		(((int *) mf) + (offsetof(SmpPassthroughRequest_t, SGL) / 4));

	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_HOST_TO_IOC |
		MPI_SGE_FLAGS_END_OF_BUFFER;
	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= sizeof(struct rep_manu_request);

	ioc->add_sge(psge, flagsLength, data_out_dma);
	psge += ioc->SGE_size;

	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_IOC_TO_HOST |
		MPI_SGE_FLAGS_END_OF_BUFFER;
	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= sizeof(struct rep_manu_reply);
	ioc->add_sge(psge, flagsLength, data_out_dma +
	sizeof(struct rep_manu_request));

	INITIALIZE_MGMT_STATUS(ioc->sas_mgmt.status)
	mpt_put_msg_frame(mptsasMgmtCtx, ioc, mf);

	timeleft = wait_for_completion_timeout(&ioc->sas_mgmt.done, 10 * HZ);
	if (!(ioc->sas_mgmt.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		ret = -ETIME;
		mpt_free_msg_frame(ioc, mf);
		mf = NULL;
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out_free;
		if (!timeleft)
			mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP);
		goto out_free;
	}

	mf = NULL;

	if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_RF_VALID) {
		u8 *tmp;

		smprep = (SmpPassthroughReply_t *)ioc->sas_mgmt.reply;
		if (le16_to_cpu(smprep->ResponseDataLength) !=
		    sizeof(struct rep_manu_reply))
			goto out_free;

		manufacture_reply = data_out + sizeof(struct rep_manu_request);
		memtostr(edev->vendor_id, manufacture_reply->vendor_id);
		memtostr(edev->product_id, manufacture_reply->product_id);
		memtostr(edev->product_rev, manufacture_reply->product_rev);
		edev->level = manufacture_reply->sas_format;
		if (manufacture_reply->sas_format) {
			memtostr(edev->component_vendor_id,
				 manufacture_reply->component_vendor_id);
			tmp = (u8 *)&manufacture_reply->component_id;
			edev->component_id = tmp[0] << 8 | tmp[1];
			edev->component_revision_id =
				manufacture_reply->component_revision_id;
		}
	} else {
		printk(MYIOC_s_ERR_FMT
			"%s: smp passthru reply failed to be returned\n",
			ioc->name, __func__);
		ret = -ENXIO;
	}
out_free:
	if (data_out_dma)
		dma_free_coherent(&ioc->pcidev->dev, sz, data_out,
				  data_out_dma);
put_mf:
	if (mf)
		mpt_free_msg_frame(ioc, mf);
out_unlock:
	CLEAR_MGMT_STATUS(ioc->sas_mgmt.status)
	mutex_unlock(&ioc->sas_mgmt.mutex);
out:
	return ret;
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
	VirtTarget *vtarget;

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
	case MPI_SAS_IOUNIT0_RATE_6_0:
		phy->negotiated_linkrate = SAS_LINK_RATE_6_0_GBPS;
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
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
					__func__, __LINE__));
				goto out;
			}
			mptsas_set_port(ioc, phy_info, port);
			devtprintk(ioc, dev_printk(KERN_DEBUG, &port->dev,
			    MYIOC_s_FMT "add port %d, sas_addr (0x%llx)\n",
			    ioc->name, port->port_identifier,
			    (unsigned long long)phy_info->
			    attached.sas_address));
		}
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"sas_port_add_phy: phy_id=%d\n",
			ioc->name, phy_info->phy_id));
		sas_port_add_phy(port, phy_info->phy);
		phy_info->sas_port_add_phy = 0;
		devtprintk(ioc, dev_printk(KERN_DEBUG, &phy_info->phy->dev,
		    MYIOC_s_FMT "add phy %d, phy-obj (0x%p)\n", ioc->name,
		     phy_info->phy_id, phy_info->phy));
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
		if (mptsas_is_end_device(&phy_info->attached) &&
		    phy_info->attached.handle_parent) {
			goto out;
		}

		mptsas_parse_device_info(&identify, &phy_info->attached);
		if (scsi_is_host_device(parent)) {
			struct mptsas_portinfo *port_info;
			int i;

			port_info = ioc->hba_port_info;

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
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__func__, __LINE__));
			goto out;
		}

		rphy->identify = identify;
		error = sas_rphy_add(rphy);
		if (error) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__func__, __LINE__));
			sas_rphy_free(rphy);
			goto out;
		}
		mptsas_set_rphy(ioc, phy_info, rphy);
		if (identify.device_type == SAS_EDGE_EXPANDER_DEVICE ||
			identify.device_type == SAS_FANOUT_EXPANDER_DEVICE)
				mptsas_exp_repmanufacture_info(ioc,
					identify.sas_address,
					rphy_to_expander_device(rphy));
	}

	/* If the device exists, verify it wasn't previously flagged
	as a missing device.  If so, clear it */
	vtarget = mptsas_find_vtarget(ioc,
	    phy_info->attached.channel,
	    phy_info->attached.id);
	if (vtarget && vtarget->inDMD) {
		printk(KERN_INFO "Device returned, unsetting inDMD\n");
		vtarget->inDMD = 0;
	}

 out:
	return error;
}

static int
mptsas_probe_hba_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo *port_info, *hba;
	int error = -ENOMEM, i;

	hba = kzalloc(sizeof(struct mptsas_portinfo), GFP_KERNEL);
	if (! hba)
		goto out;

	error = mptsas_sas_io_unit_pg0(ioc, hba);
	if (error)
		goto out_free_port_info;

	mptsas_sas_io_unit_pg1(ioc);
	mutex_lock(&ioc->sas_topology_mutex);
	port_info = ioc->hba_port_info;
	if (!port_info) {
		ioc->hba_port_info = port_info = hba;
		ioc->hba_port_num_phy = port_info->num_phys;
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
#if defined(CPQ_CIM)
	ioc->num_ports = port_info->num_phys;
#endif
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_phy_pg0(ioc, &port_info->phy_info[i],
			(MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER <<
			 MPI_SAS_PHY_PGAD_FORM_SHIFT), i);
		port_info->phy_info[i].identify.handle =
		    port_info->phy_info[i].handle;
		mptsas_sas_device_pg0(ioc, &port_info->phy_info[i].identify,
			(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
			 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			 port_info->phy_info[i].identify.handle);
		if (!ioc->hba_port_sas_addr)
			ioc->hba_port_sas_addr =
			    port_info->phy_info[i].identify.sas_address;
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

static void
mptsas_expander_refresh(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	struct mptsas_portinfo *parent;
	struct device *parent_dev;
	struct sas_rphy	*rphy;
	int		i;
	u64		sas_address; /* expander sas address */
	u32		handle;

	handle = port_info->phy_info[0].handle;
	sas_address = port_info->phy_info[0].identify.sas_address;
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_expander_pg1(ioc, &port_info->phy_info[i],
		    (MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM <<
		    MPI_SAS_EXPAND_PGAD_FORM_SHIFT), (i << 16) + handle);

		mptsas_sas_device_pg0(ioc,
		    &port_info->phy_info[i].identify,
		    (MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    port_info->phy_info[i].identify.handle);
		port_info->phy_info[i].identify.phy_id =
		    port_info->phy_info[i].phy_id;

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

	mutex_lock(&ioc->sas_topology_mutex);
	parent = mptsas_find_portinfo_by_handle(ioc,
	    port_info->phy_info[0].identify.handle_parent);
	if (!parent) {
		mutex_unlock(&ioc->sas_topology_mutex);
		return;
	}
	for (i = 0, parent_dev = NULL; i < parent->num_phys && !parent_dev;
	    i++) {
		if (parent->phy_info[i].attached.sas_address == sas_address) {
			rphy = mptsas_get_rphy(&parent->phy_info[i]);
			parent_dev = &rphy->dev;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	mptsas_setup_wide_ports(ioc, port_info);
	for (i = 0; i < port_info->num_phys; i++, ioc->sas_index++)
		mptsas_probe_one_phy(parent_dev, &port_info->phy_info[i],
		    ioc->sas_index, 0);
}

static void
mptsas_expander_event_add(MPT_ADAPTER *ioc,
    MpiEventDataSasExpanderStatusChange_t *expander_data)
{
	struct mptsas_portinfo *port_info;
	int i;
	__le64 sas_address;

	port_info = kzalloc(sizeof(struct mptsas_portinfo), GFP_KERNEL);
	BUG_ON(!port_info);
	port_info->num_phys = (expander_data->NumPhys) ?
	    expander_data->NumPhys : 1;
	port_info->phy_info = kcalloc(port_info->num_phys,
	    sizeof(struct mptsas_phyinfo), GFP_KERNEL);
	BUG_ON(!port_info->phy_info);
	memcpy(&sas_address, &expander_data->SASAddress, sizeof(__le64));
	for (i = 0; i < port_info->num_phys; i++) {
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(expander_data->DevHandle);
		port_info->phy_info[i].identify.sas_address =
		    le64_to_cpu(sas_address);
		port_info->phy_info[i].identify.handle_parent =
		    le16_to_cpu(expander_data->ParentDevHandle);
	}

	mutex_lock(&ioc->sas_topology_mutex);
	list_add_tail(&port_info->list, &ioc->sas_topology);
	mutex_unlock(&ioc->sas_topology_mutex);

	printk(MYIOC_s_INFO_FMT "add expander: num_phys %d, "
	    "sas_addr (0x%llx)\n", ioc->name, port_info->num_phys,
	    (unsigned long long)sas_address);

	mptsas_expander_refresh(ioc, port_info);
}

/**
 * mptsas_delete_expander_siblings - remove siblings attached to expander
 * @ioc: Pointer to MPT_ADAPTER structure
 * @parent: the parent port_info object
 * @expander: the expander port_info object
 **/
static void
mptsas_delete_expander_siblings(MPT_ADAPTER *ioc, struct mptsas_portinfo
    *parent, struct mptsas_portinfo *expander)
{
	struct mptsas_phyinfo *phy_info;
	struct mptsas_portinfo *port_info;
	struct sas_rphy *rphy;
	int i;

	phy_info = expander->phy_info;
	for (i = 0; i < expander->num_phys; i++, phy_info++) {
		rphy = mptsas_get_rphy(phy_info);
		if (!rphy)
			continue;
		if (rphy->identify.device_type == SAS_END_DEVICE)
			mptsas_del_end_device(ioc, phy_info);
	}

	phy_info = expander->phy_info;
	for (i = 0; i < expander->num_phys; i++, phy_info++) {
		rphy = mptsas_get_rphy(phy_info);
		if (!rphy)
			continue;
		if (rphy->identify.device_type ==
		    MPI_SAS_DEVICE_INFO_EDGE_EXPANDER ||
		    rphy->identify.device_type ==
		    MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER) {
			port_info = mptsas_find_portinfo_by_sas_address(ioc,
			    rphy->identify.sas_address);
			if (!port_info)
				continue;
			if (port_info == parent) /* backlink rphy */
				continue;
			/*
			Delete this expander even if the expdevpage is exists
			because the parent expander is already deleted
			*/
			mptsas_expander_delete(ioc, port_info, 1);
		}
	}
}


/**
 *	mptsas_expander_delete - remove this expander
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info: expander port_info struct
 *	@force: Flag to forcefully delete the expander
 *
 **/

static void mptsas_expander_delete(MPT_ADAPTER *ioc,
		struct mptsas_portinfo *port_info, u8 force)
{

	struct mptsas_portinfo *parent;
	int		i;
	u64		expander_sas_address;
	struct mptsas_phyinfo *phy_info;
	struct mptsas_portinfo buffer;
	struct mptsas_portinfo_details *port_details;
	struct sas_port *port;

	if (!port_info)
		return;

	/* see if expander is still there before deleting */
	mptsas_sas_expander_pg0(ioc, &buffer,
	    (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
	    MPI_SAS_EXPAND_PGAD_FORM_SHIFT),
	    port_info->phy_info[0].identify.handle);

	if (buffer.num_phys) {
		kfree(buffer.phy_info);
		if (!force)
			return;
	}


	/*
	 * Obtain the port_info instance to the parent port
	 */
	port_details = NULL;
	expander_sas_address =
	    port_info->phy_info[0].identify.sas_address;
	parent = mptsas_find_portinfo_by_handle(ioc,
	    port_info->phy_info[0].identify.handle_parent);
	mptsas_delete_expander_siblings(ioc, parent, port_info);
	if (!parent)
		goto out;

	/*
	 * Delete rphys in the parent that point
	 * to this expander.
	 */
	phy_info = parent->phy_info;
	port = NULL;
	for (i = 0; i < parent->num_phys; i++, phy_info++) {
		if (!phy_info->phy)
			continue;
		if (phy_info->attached.sas_address !=
		    expander_sas_address)
			continue;
		if (!port) {
			port = mptsas_get_port(phy_info);
			port_details = phy_info->port_details;
		}
		dev_printk(KERN_DEBUG, &phy_info->phy->dev,
		    MYIOC_s_FMT "delete phy %d, phy-obj (0x%p)\n", ioc->name,
		    phy_info->phy_id, phy_info->phy);
		sas_port_delete_phy(port, phy_info->phy);
	}
	if (port) {
		dev_printk(KERN_DEBUG, &port->dev,
		    MYIOC_s_FMT "delete port %d, sas_addr (0x%llx)\n",
		    ioc->name, port->port_identifier,
		    (unsigned long long)expander_sas_address);
		sas_port_delete(port);
		mptsas_port_delete(ioc, port_details);
	}
 out:

	printk(MYIOC_s_INFO_FMT "delete expander: num_phys %d, "
	    "sas_addr (0x%llx)\n",  ioc->name, port_info->num_phys,
	    (unsigned long long)expander_sas_address);

	/*
	 * free link
	 */
	list_del(&port_info->list);
	kfree(port_info->phy_info);
	kfree(port_info);
}


/**
 * mptsas_send_expander_event - expanders events
 * @fw_event: event data
 *
 *
 * This function handles adding, removing, and refreshing
 * device handles within the expander objects.
 */
static void
mptsas_send_expander_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc;
	MpiEventDataSasExpanderStatusChange_t *expander_data;
	struct mptsas_portinfo *port_info;
	__le64 sas_address;
	int i;

	ioc = fw_event->ioc;
	expander_data = (MpiEventDataSasExpanderStatusChange_t *)
	    fw_event->event_data;
	memcpy(&sas_address, &expander_data->SASAddress, sizeof(__le64));
	sas_address = le64_to_cpu(sas_address);
	port_info = mptsas_find_portinfo_by_sas_address(ioc, sas_address);

	if (expander_data->ReasonCode == MPI_EVENT_SAS_EXP_RC_ADDED) {
		if (port_info) {
			for (i = 0; i < port_info->num_phys; i++) {
				port_info->phy_info[i].portinfo = port_info;
				port_info->phy_info[i].handle =
				    le16_to_cpu(expander_data->DevHandle);
				port_info->phy_info[i].identify.sas_address =
				    le64_to_cpu(sas_address);
				port_info->phy_info[i].identify.handle_parent =
				    le16_to_cpu(expander_data->ParentDevHandle);
			}
			mptsas_expander_refresh(ioc, port_info);
		} else if (!port_info && expander_data->NumPhys)
			mptsas_expander_event_add(ioc, expander_data);
	} else if (expander_data->ReasonCode ==
	    MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING)
		mptsas_expander_delete(ioc, port_info, 0);

	mptsas_free_fw_event(ioc, fw_event);
}


/**
 * mptsas_expander_add - adds a newly discovered expander
 * @ioc: Pointer to MPT_ADAPTER structure
 * @handle: device handle
 *
 */
static struct mptsas_portinfo *
mptsas_expander_add(MPT_ADAPTER *ioc, u16 handle)
{
	struct mptsas_portinfo buffer, *port_info;
	int i;

	if ((mptsas_sas_expander_pg0(ioc, &buffer,
	    (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
	    MPI_SAS_EXPAND_PGAD_FORM_SHIFT), handle)))
		return NULL;

	port_info = kzalloc(sizeof(struct mptsas_portinfo), GFP_KERNEL);
	if (!port_info) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
		"%s: exit at line=%d\n", ioc->name,
		__func__, __LINE__));
		return NULL;
	}
	port_info->num_phys = buffer.num_phys;
	port_info->phy_info = buffer.phy_info;
	for (i = 0; i < port_info->num_phys; i++)
		port_info->phy_info[i].portinfo = port_info;
	mutex_lock(&ioc->sas_topology_mutex);
	list_add_tail(&port_info->list, &ioc->sas_topology);
	mutex_unlock(&ioc->sas_topology_mutex);
	printk(MYIOC_s_INFO_FMT "add expander: num_phys %d, "
	    "sas_addr (0x%llx)\n", ioc->name, port_info->num_phys,
	    (unsigned long long)buffer.phy_info[0].identify.sas_address);
	mptsas_expander_refresh(ioc, port_info);
	return port_info;
}

static void
mptsas_send_link_status_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc;
	MpiEventDataSasPhyLinkStatus_t *link_data;
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	__le64 sas_address;
	u8 phy_num;
	u8 link_rate;

	ioc = fw_event->ioc;
	link_data = (MpiEventDataSasPhyLinkStatus_t *)fw_event->event_data;

	memcpy(&sas_address, &link_data->SASAddress, sizeof(__le64));
	sas_address = le64_to_cpu(sas_address);
	link_rate = link_data->LinkRates >> 4;
	phy_num = link_data->PhyNum;

	port_info = mptsas_find_portinfo_by_sas_address(ioc, sas_address);
	if (port_info) {
		phy_info = &port_info->phy_info[phy_num];
		if (phy_info)
			phy_info->negotiated_link_rate = link_rate;
	}

	if (link_rate == MPI_SAS_IOUNIT0_RATE_1_5 ||
	    link_rate == MPI_SAS_IOUNIT0_RATE_3_0 ||
	    link_rate == MPI_SAS_IOUNIT0_RATE_6_0) {

		if (!port_info) {
			if (ioc->old_sas_discovery_protocal) {
				port_info = mptsas_expander_add(ioc,
					le16_to_cpu(link_data->DevHandle));
				if (port_info)
					goto out;
			}
			goto out;
		}

		if (port_info == ioc->hba_port_info)
			mptsas_probe_hba_phys(ioc);
		else
			mptsas_expander_refresh(ioc, port_info);
	} else if (phy_info && phy_info->phy) {
		if (link_rate ==  MPI_SAS_IOUNIT0_RATE_PHY_DISABLED)
			phy_info->phy->negotiated_linkrate =
			    SAS_PHY_DISABLED;
		else if (link_rate ==
		    MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION)
			phy_info->phy->negotiated_linkrate =
			    SAS_LINK_RATE_FAILED;
		else {
			phy_info->phy->negotiated_linkrate =
			    SAS_LINK_RATE_UNKNOWN;
			if (ioc->device_missing_delay &&
			    mptsas_is_end_device(&phy_info->attached)) {
				struct scsi_device		*sdev;
				VirtDevice			*vdevice;
				u8	channel, id;
				id = phy_info->attached.id;
				channel = phy_info->attached.channel;
				devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"Link down for fw_id %d:fw_channel %d\n",
				    ioc->name, phy_info->attached.id,
				    phy_info->attached.channel));

				shost_for_each_device(sdev, ioc->sh) {
					vdevice = sdev->hostdata;
					if ((vdevice == NULL) ||
						(vdevice->vtarget == NULL))
						continue;
					if ((vdevice->vtarget->tflags &
					    MPT_TARGET_FLAGS_RAID_COMPONENT ||
					    vdevice->vtarget->raidVolume))
						continue;
					if (vdevice->vtarget->id == id &&
						vdevice->vtarget->channel ==
						channel)
						devtprintk(ioc,
						printk(MYIOC_s_DEBUG_FMT
						"SDEV OUTSTANDING CMDS"
						"%d\n", ioc->name,
						scsi_device_busy(sdev)));
				}

			}
		}
	}
 out:
	mptsas_free_fw_event(ioc, fw_event);
}

static void
mptsas_not_responding_devices(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo buffer, *port_info;
	struct mptsas_device_info	*sas_info;
	struct mptsas_devinfo sas_device;
	u32	handle;
	VirtTarget *vtarget = NULL;
	struct mptsas_phyinfo *phy_info;
	u8 found_expander;
	int retval, retry_count;
	unsigned long flags;

	mpt_findImVolumes(ioc);

	spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
	if (ioc->ioc_reset_in_progress) {
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		   "%s: exiting due to a parallel reset \n", ioc->name,
		    __func__));
		spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ioc->taskmgmt_lock, flags);

	/* devices, logical volumes */
	mutex_lock(&ioc->sas_device_info_mutex);
 redo_device_scan:
	list_for_each_entry(sas_info, &ioc->sas_device_info_list, list) {
		if (sas_info->is_cached)
			continue;
		if (!sas_info->is_logical_volume) {
			sas_device.handle = 0;
			retry_count = 0;
retry_page:
			retval = mptsas_sas_device_pg0(ioc, &sas_device,
				(MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID
				<< MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				(sas_info->fw.channel << 8) +
				sas_info->fw.id);

			if (sas_device.handle)
				continue;
			if (retval == -EBUSY) {
				spin_lock_irqsave(&ioc->taskmgmt_lock, flags);
				if (ioc->ioc_reset_in_progress) {
					dfailprintk(ioc,
					printk(MYIOC_s_DEBUG_FMT
					"%s: exiting due to reset\n",
					ioc->name, __func__));
					spin_unlock_irqrestore
					(&ioc->taskmgmt_lock, flags);
					mutex_unlock(&ioc->
					sas_device_info_mutex);
					return;
				}
				spin_unlock_irqrestore(&ioc->taskmgmt_lock,
				flags);
			}

			if (retval && (retval != -ENODEV)) {
				if (retry_count < 10) {
					retry_count++;
					goto retry_page;
				} else {
					devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"%s: Config page retry exceeded retry "
					"count deleting device 0x%llx\n",
					ioc->name, __func__,
					sas_info->sas_address));
				}
			}

			/* delete device */
			vtarget = mptsas_find_vtarget(ioc,
				sas_info->fw.channel, sas_info->fw.id);

			if (vtarget)
				vtarget->deleted = 1;

			phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
					sas_info->sas_address);

			mptsas_del_end_device(ioc, phy_info);
			goto redo_device_scan;
		} else
			mptsas_volume_delete(ioc, sas_info->fw.id);
	}
	mutex_unlock(&ioc->sas_device_info_mutex);

	/* expanders */
	mutex_lock(&ioc->sas_topology_mutex);
 redo_expander_scan:
	list_for_each_entry(port_info, &ioc->sas_topology, list) {

		if (!(port_info->phy_info[0].identify.device_info &
		    MPI_SAS_DEVICE_INFO_SMP_TARGET))
			continue;
		found_expander = 0;
		handle = 0xFFFF;
		while (!mptsas_sas_expander_pg0(ioc, &buffer,
		    (MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
		     MPI_SAS_EXPAND_PGAD_FORM_SHIFT), handle) &&
		    !found_expander) {

			handle = buffer.phy_info[0].handle;
			if (buffer.phy_info[0].identify.sas_address ==
			    port_info->phy_info[0].identify.sas_address) {
				found_expander = 1;
			}
			kfree(buffer.phy_info);
		}

		if (!found_expander) {
			mptsas_expander_delete(ioc, port_info, 0);
			goto redo_expander_scan;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
}

/**
 *	mptsas_probe_expanders - adding expanders
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static void
mptsas_probe_expanders(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo buffer, *port_info;
	u32 			handle;
	int i;

	handle = 0xFFFF;
	while (!mptsas_sas_expander_pg0(ioc, &buffer,
	    (MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
	     MPI_SAS_EXPAND_PGAD_FORM_SHIFT), handle)) {

		handle = buffer.phy_info[0].handle;
		port_info = mptsas_find_portinfo_by_sas_address(ioc,
		    buffer.phy_info[0].identify.sas_address);

		if (port_info) {
			/* refreshing handles */
			for (i = 0; i < buffer.num_phys; i++) {
				port_info->phy_info[i].handle = handle;
				port_info->phy_info[i].identify.handle_parent =
				    buffer.phy_info[0].identify.handle_parent;
			}
			mptsas_expander_refresh(ioc, port_info);
			kfree(buffer.phy_info);
			continue;
		}

		port_info = kzalloc(sizeof(struct mptsas_portinfo), GFP_KERNEL);
		if (!port_info) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: exit at line=%d\n", ioc->name,
			__func__, __LINE__));
			return;
		}
		port_info->num_phys = buffer.num_phys;
		port_info->phy_info = buffer.phy_info;
		for (i = 0; i < port_info->num_phys; i++)
			port_info->phy_info[i].portinfo = port_info;
		mutex_lock(&ioc->sas_topology_mutex);
		list_add_tail(&port_info->list, &ioc->sas_topology);
		mutex_unlock(&ioc->sas_topology_mutex);
		printk(MYIOC_s_INFO_FMT "add expander: num_phys %d, "
		    "sas_addr (0x%llx)\n", ioc->name, port_info->num_phys,
	    (unsigned long long)buffer.phy_info[0].identify.sas_address);
		mptsas_expander_refresh(ioc, port_info);
	}
}

static void
mptsas_probe_devices(MPT_ADAPTER *ioc)
{
	u16 handle;
	struct mptsas_devinfo sas_device;
	struct mptsas_phyinfo *phy_info;

	handle = 0xFFFF;
	while (!(mptsas_sas_device_pg0(ioc, &sas_device,
	    MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE, handle))) {

		handle = sas_device.handle;

		if ((sas_device.device_info &
		     (MPI_SAS_DEVICE_INFO_SSP_TARGET |
		      MPI_SAS_DEVICE_INFO_STP_TARGET |
		      MPI_SAS_DEVICE_INFO_SATA_DEVICE)) == 0)
			continue;

		/* If there is no FW B_T mapping for this device then continue
		 * */
		if (!(sas_device.flags & MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)
			|| !(sas_device.flags &
			MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED))
			continue;

		phy_info = mptsas_refreshing_device_handles(ioc, &sas_device);
		if (!phy_info)
			continue;

		if (mptsas_get_rphy(phy_info))
			continue;

		mptsas_add_end_device(ioc, phy_info);
	}
}

/**
 *	mptsas_scan_sas_topology - scans new SAS topology
 *	  (part of probe or rescan)
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static void
mptsas_scan_sas_topology(MPT_ADAPTER *ioc)
{
	struct scsi_device *sdev;
	int i;

	mptsas_probe_hba_phys(ioc);
	mptsas_probe_expanders(ioc);
	mptsas_probe_devices(ioc);

	/*
	  Reporting RAID volumes.
	*/
	if (!ioc->ir_firmware || !ioc->raid_data.pIocPg2 ||
	    !ioc->raid_data.pIocPg2->NumActiveVolumes)
		return;
	for (i = 0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
		sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID, 0);
		if (sdev) {
			scsi_device_put(sdev);
			continue;
		}
		printk(MYIOC_s_INFO_FMT "attaching raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID);
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID, 0);
	}
}


static void
mptsas_handle_queue_full_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc;
	EventDataQueueFull_t *qfull_data;
	struct mptsas_device_info *sas_info;
	struct scsi_device	*sdev;
	int depth;
	int id = -1;
	int channel = -1;
	int fw_id, fw_channel;
	u16 current_depth;


	ioc = fw_event->ioc;
	qfull_data = (EventDataQueueFull_t *)fw_event->event_data;
	fw_id = qfull_data->TargetID;
	fw_channel = qfull_data->Bus;
	current_depth = le16_to_cpu(qfull_data->CurrentDepth);

	/* if hidden raid component, look for the volume id */
	mutex_lock(&ioc->sas_device_info_mutex);
	if (mptscsih_is_phys_disk(ioc, fw_channel, fw_id)) {
		list_for_each_entry(sas_info, &ioc->sas_device_info_list,
		    list) {
			if (sas_info->is_cached ||
			    sas_info->is_logical_volume)
				continue;
			if (sas_info->is_hidden_raid_component &&
			    (sas_info->fw.channel == fw_channel &&
			    sas_info->fw.id == fw_id)) {
				id = sas_info->volume_id;
				channel = MPTSAS_RAID_CHANNEL;
				goto out;
			}
		}
	} else {
		list_for_each_entry(sas_info, &ioc->sas_device_info_list,
		    list) {
			if (sas_info->is_cached ||
			    sas_info->is_hidden_raid_component ||
			    sas_info->is_logical_volume)
				continue;
			if (sas_info->fw.channel == fw_channel &&
			    sas_info->fw.id == fw_id) {
				id = sas_info->os.id;
				channel = sas_info->os.channel;
				goto out;
			}
		}

	}

 out:
	mutex_unlock(&ioc->sas_device_info_mutex);

	if (id != -1) {
		shost_for_each_device(sdev, ioc->sh) {
			if (sdev->id == id && sdev->channel == channel) {
				if (current_depth > sdev->queue_depth) {
					sdev_printk(KERN_INFO, sdev,
					    "strange observation, the queue "
					    "depth is (%d) meanwhile fw queue "
					    "depth (%d)\n", sdev->queue_depth,
					    current_depth);
					continue;
				}
				depth = scsi_track_queue_full(sdev,
					sdev->queue_depth - 1);
				if (depth > 0)
					sdev_printk(KERN_INFO, sdev,
					"Queue depth reduced to (%d)\n",
					   depth);
				else if (depth < 0)
					sdev_printk(KERN_INFO, sdev,
					"Tagged Command Queueing is being "
					"disabled\n");
				else if (depth == 0)
					sdev_printk(KERN_DEBUG, sdev,
					"Queue depth not changed yet\n");
			}
		}
	}

	mptsas_free_fw_event(ioc, fw_event);
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

/**
 *	mptsas_find_phyinfo_by_phys_disk_num - find phyinfo for the
 *	  specified @phys_disk_num
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phys_disk_num: (hot plug) physical disk number (for RAID support)
 *	@channel: channel number
 *	@id: Logical Target ID
 *
 **/
static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_phys_disk_num(MPT_ADAPTER *ioc, u8 phys_disk_num,
	u8 channel, u8 id)
{
	struct mptsas_phyinfo *phy_info = NULL;
	struct mptsas_portinfo *port_info;
	RaidPhysDiskPage1_t *phys_disk = NULL;
	int num_paths;
	u64 sas_address = 0;
	int i;

	phy_info = NULL;
	if (!ioc->raid_data.pIocPg3)
		return NULL;
	/* dual port support */
	num_paths = mpt_raid_phys_disk_get_num_paths(ioc, phys_disk_num);
	if (!num_paths)
		goto out;
	phys_disk = kzalloc(offsetof(RaidPhysDiskPage1_t, Path) +
	   (num_paths * sizeof(RAID_PHYS_DISK1_PATH)), GFP_KERNEL);
	if (!phys_disk)
		goto out;
	mpt_raid_phys_disk_pg1(ioc, phys_disk_num, phys_disk);
	for (i = 0; i < num_paths; i++) {
		if ((phys_disk->Path[i].Flags & 1) != 0)
			/* entry no longer valid */
			continue;
		if ((id == phys_disk->Path[i].PhysDiskID) &&
		    (channel == phys_disk->Path[i].PhysDiskBus)) {
			memcpy(&sas_address, &phys_disk->Path[i].WWID,
				sizeof(u64));
			phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
					sas_address);
			goto out;
		}
	}

 out:
	kfree(phys_disk);
	if (phy_info)
		return phy_info;

	/*
	 * Extra code to handle RAID0 case, where the sas_address is not updated
	 * in phys_disk_page_1 when hotswapped
	 */
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys && !phy_info; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.phys_disk_num == ~0)
				continue;
			if ((port_info->phy_info[i].attached.phys_disk_num ==
			    phys_disk_num) &&
			    (port_info->phy_info[i].attached.id == id) &&
			    (port_info->phy_info[i].attached.channel ==
			     channel))
				phy_info = &port_info->phy_info[i];
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return phy_info;
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
	struct mptsas_phyinfo	*phy_info;
	struct mptsas_devinfo		sas_device;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.pageAddr = (channel << 8) + id;
	cfg.cfghdr.hdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!hdr.PageLength)
		goto out;

	buffer = dma_alloc_coherent(&ioc->pcidev->dev, hdr.PageLength * 4,
				    &dma_handle, GFP_KERNEL);

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

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(phys_disk.PhysDiskBus << 8) +
			phys_disk.PhysDiskID))
			continue;

		/* If there is no FW B_T mapping for this device then continue
		 * */
		if (!(sas_device.flags & MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)
			|| !(sas_device.flags &
			MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED))
			continue;


		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
		    sas_device.sas_address);
		mptsas_add_end_device(ioc, phy_info);
	}

 out:
	if (buffer)
		dma_free_coherent(&ioc->pcidev->dev, hdr.PageLength * 4,
				  buffer, dma_handle);
}
/*
 * Work queue thread to handle SAS hotplug events
 */
static void
mptsas_hotplug_work(MPT_ADAPTER *ioc, struct fw_event_work *fw_event,
    struct mptsas_hotplug_event *hot_plug_info)
{
	struct mptsas_phyinfo *phy_info;
	struct scsi_target * starget;
	struct mptsas_devinfo sas_device;
	VirtTarget *vtarget;
	int i;
	struct mptsas_portinfo *port_info;

	switch (hot_plug_info->event_type) {

	case MPTSAS_ADD_PHYSDISK:

		if (!ioc->raid_data.pIocPg2)
			break;

		for (i = 0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
			if (ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID ==
			    hot_plug_info->id) {
				printk(MYIOC_s_WARN_FMT "firmware bug: unable "
				    "to add hidden disk - target_id matches "
				    "volume_id\n", ioc->name);
				mptsas_free_fw_event(ioc, fw_event);
				return;
			}
		}
		mpt_findImVolumes(ioc);
		fallthrough;

	case MPTSAS_ADD_DEVICE:
		memset(&sas_device, 0, sizeof(struct mptsas_devinfo));
		mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    (hot_plug_info->channel << 8) +
		    hot_plug_info->id);

		/* If there is no FW B_T mapping for this device then break
		 * */
		if (!(sas_device.flags & MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)
			|| !(sas_device.flags &
			MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED))
			break;

		if (!sas_device.handle)
			return;

		phy_info = mptsas_refreshing_device_handles(ioc, &sas_device);
		/* Device hot plug */
		if (!phy_info) {
			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				"%s %d HOT PLUG: "
				"parent handle of device %x\n", ioc->name,
				__func__, __LINE__, sas_device.handle_parent));
			port_info = mptsas_find_portinfo_by_handle(ioc,
				sas_device.handle_parent);

			if (port_info == ioc->hba_port_info)
				mptsas_probe_hba_phys(ioc);
			else if (port_info)
				mptsas_expander_refresh(ioc, port_info);
			else {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
					"%s %d port info is NULL\n",
					ioc->name, __func__, __LINE__));
				break;
			}
			phy_info = mptsas_refreshing_device_handles
				(ioc, &sas_device);
		}

		if (!phy_info) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s %d phy info is NULL\n",
				ioc->name, __func__, __LINE__));
			break;
		}

		if (mptsas_get_rphy(phy_info))
			break;

		mptsas_add_end_device(ioc, phy_info);
		break;

	case MPTSAS_DEL_DEVICE:
		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
		    hot_plug_info->sas_address);
		mptsas_del_end_device(ioc, phy_info);
		break;

	case MPTSAS_DEL_PHYSDISK:

		mpt_findImVolumes(ioc);

		phy_info = mptsas_find_phyinfo_by_phys_disk_num(
				ioc, hot_plug_info->phys_disk_num,
				hot_plug_info->channel,
				hot_plug_info->id);
		mptsas_del_end_device(ioc, phy_info);
		break;

	case MPTSAS_ADD_PHYSDISK_REPROBE:

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    (hot_plug_info->channel << 8) + hot_plug_info->id)) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
				 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		/* If there is no FW B_T mapping for this device then break
		 * */
		if (!(sas_device.flags & MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)
			|| !(sas_device.flags &
			MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED))
			break;

		phy_info = mptsas_find_phyinfo_by_sas_address(
		    ioc, sas_device.sas_address);

		if (!phy_info) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
				 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (!starget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
				 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		vtarget = starget->hostdata;
		if (!vtarget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
				 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		mpt_findImVolumes(ioc);

		starget_printk(KERN_INFO, starget, MYIOC_s_FMT "RAID Hidding: "
		    "fw_channel=%d, fw_id=%d, physdsk %d, sas_addr 0x%llx\n",
		    ioc->name, hot_plug_info->channel, hot_plug_info->id,
		    hot_plug_info->phys_disk_num, (unsigned long long)
		    sas_device.sas_address);

		vtarget->id = hot_plug_info->phys_disk_num;
		vtarget->tflags |= MPT_TARGET_FLAGS_RAID_COMPONENT;
		phy_info->attached.phys_disk_num = hot_plug_info->phys_disk_num;
		mptsas_reprobe_target(starget, 1);
		break;

	case MPTSAS_DEL_PHYSDISK_REPROBE:

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(hot_plug_info->channel << 8) + hot_plug_info->id)) {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				    "%s: fw_id=%d exit at line=%d\n",
				    ioc->name, __func__,
				    hot_plug_info->id, __LINE__));
			break;
		}

		/* If there is no FW B_T mapping for this device then break
		 * */
		if (!(sas_device.flags & MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)
			|| !(sas_device.flags &
			MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED))
			break;

		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
				sas_device.sas_address);
		if (!phy_info) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (!starget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		vtarget = starget->hostdata;
		if (!vtarget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		if (!(vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT)) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			 __func__, hot_plug_info->id, __LINE__));
			break;
		}

		mpt_findImVolumes(ioc);

		starget_printk(KERN_INFO, starget, MYIOC_s_FMT "RAID Exposing:"
		    " fw_channel=%d, fw_id=%d, physdsk %d, sas_addr 0x%llx\n",
		    ioc->name, hot_plug_info->channel, hot_plug_info->id,
		    hot_plug_info->phys_disk_num, (unsigned long long)
		    sas_device.sas_address);

		vtarget->tflags &= ~MPT_TARGET_FLAGS_RAID_COMPONENT;
		vtarget->id = hot_plug_info->id;
		phy_info->attached.phys_disk_num = ~0;
		mptsas_reprobe_target(starget, 0);
		mptsas_add_device_component_by_fw(ioc,
		    hot_plug_info->channel, hot_plug_info->id);
		break;

	case MPTSAS_ADD_RAID:

		mpt_findImVolumes(ioc);
		printk(MYIOC_s_INFO_FMT "attaching raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL,
		    hot_plug_info->id);
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL,
		    hot_plug_info->id, 0);
		break;

	case MPTSAS_DEL_RAID:

		mpt_findImVolumes(ioc);
		printk(MYIOC_s_INFO_FMT "removing raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL,
		    hot_plug_info->id);
		scsi_remove_device(hot_plug_info->sdev);
		scsi_device_put(hot_plug_info->sdev);
		break;

	case MPTSAS_ADD_INACTIVE_VOLUME:

		mpt_findImVolumes(ioc);
		mptsas_adding_inactive_raid_components(ioc,
		    hot_plug_info->channel, hot_plug_info->id);
		break;

	default:
		break;
	}

	mptsas_free_fw_event(ioc, fw_event);
}

static void
mptsas_send_sas_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc;
	struct mptsas_hotplug_event hot_plug_info;
	EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data;
	u32 device_info;
	u64 sas_address;

	ioc = fw_event->ioc;
	sas_event_data = (EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)
	    fw_event->event_data;
	device_info = le32_to_cpu(sas_event_data->DeviceInfo);

	if ((device_info &
		(MPI_SAS_DEVICE_INFO_SSP_TARGET |
		MPI_SAS_DEVICE_INFO_STP_TARGET |
		MPI_SAS_DEVICE_INFO_SATA_DEVICE)) == 0) {
		mptsas_free_fw_event(ioc, fw_event);
		return;
	}

	if (sas_event_data->ReasonCode ==
		MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED) {
		mptbase_sas_persist_operation(ioc,
		MPI_SAS_OP_CLEAR_NOT_PRESENT);
		mptsas_free_fw_event(ioc, fw_event);
		return;
	}

	switch (sas_event_data->ReasonCode) {
	case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:
	case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
		memset(&hot_plug_info, 0, sizeof(struct mptsas_hotplug_event));
		hot_plug_info.handle = le16_to_cpu(sas_event_data->DevHandle);
		hot_plug_info.channel = sas_event_data->Bus;
		hot_plug_info.id = sas_event_data->TargetID;
		hot_plug_info.phy_id = sas_event_data->PhyNum;
		memcpy(&sas_address, &sas_event_data->SASAddress,
		    sizeof(u64));
		hot_plug_info.sas_address = le64_to_cpu(sas_address);
		hot_plug_info.device_info = device_info;
		if (sas_event_data->ReasonCode &
		    MPI_EVENT_SAS_DEV_STAT_RC_ADDED)
			hot_plug_info.event_type = MPTSAS_ADD_DEVICE;
		else
			hot_plug_info.event_type = MPTSAS_DEL_DEVICE;
		mptsas_hotplug_work(ioc, fw_event, &hot_plug_info);
		break;

	case MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED:
		mptbase_sas_persist_operation(ioc,
		    MPI_SAS_OP_CLEAR_NOT_PRESENT);
		mptsas_free_fw_event(ioc, fw_event);
		break;

	case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
	/* TODO */
	case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
	/* TODO */
	default:
		mptsas_free_fw_event(ioc, fw_event);
		break;
	}
}

static void
mptsas_send_raid_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc;
	EVENT_DATA_RAID *raid_event_data;
	struct mptsas_hotplug_event hot_plug_info;
	int status;
	int state;
	struct scsi_device *sdev = NULL;
	VirtDevice *vdevice = NULL;
	RaidPhysDiskPage0_t phys_disk;

	ioc = fw_event->ioc;
	raid_event_data = (EVENT_DATA_RAID *)fw_event->event_data;
	status = le32_to_cpu(raid_event_data->SettingsStatus);
	state = (status >> 8) & 0xff;

	memset(&hot_plug_info, 0, sizeof(struct mptsas_hotplug_event));
	hot_plug_info.id = raid_event_data->VolumeID;
	hot_plug_info.channel = raid_event_data->VolumeBus;
	hot_plug_info.phys_disk_num = raid_event_data->PhysDiskNum;

	if (raid_event_data->ReasonCode == MPI_EVENT_RAID_RC_VOLUME_DELETED ||
	    raid_event_data->ReasonCode == MPI_EVENT_RAID_RC_VOLUME_CREATED ||
	    raid_event_data->ReasonCode ==
	    MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED) {
		sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL,
		    hot_plug_info.id, 0);
		hot_plug_info.sdev = sdev;
		if (sdev)
			vdevice = sdev->hostdata;
	}

	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Entering %s: "
	    "ReasonCode=%02x\n", ioc->name, __func__,
	    raid_event_data->ReasonCode));

	switch (raid_event_data->ReasonCode) {
	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		hot_plug_info.event_type = MPTSAS_DEL_PHYSDISK_REPROBE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
		hot_plug_info.event_type = MPTSAS_ADD_PHYSDISK_REPROBE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		switch (state) {
		case MPI_PD_STATE_ONLINE:
		case MPI_PD_STATE_NOT_COMPATIBLE:
			mpt_raid_phys_disk_pg0(ioc,
			    raid_event_data->PhysDiskNum, &phys_disk);
			hot_plug_info.id = phys_disk.PhysDiskID;
			hot_plug_info.channel = phys_disk.PhysDiskBus;
			hot_plug_info.event_type = MPTSAS_ADD_PHYSDISK;
			break;
		case MPI_PD_STATE_FAILED:
		case MPI_PD_STATE_MISSING:
		case MPI_PD_STATE_OFFLINE_AT_HOST_REQUEST:
		case MPI_PD_STATE_FAILED_AT_HOST_REQUEST:
		case MPI_PD_STATE_OFFLINE_FOR_ANOTHER_REASON:
			hot_plug_info.event_type = MPTSAS_DEL_PHYSDISK;
			break;
		default:
			break;
		}
		break;
	case MPI_EVENT_RAID_RC_VOLUME_DELETED:
		if (!sdev)
			break;
		vdevice->vtarget->deleted = 1; /* block IO */
		hot_plug_info.event_type = MPTSAS_DEL_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_CREATED:
		if (sdev) {
			scsi_device_put(sdev);
			break;
		}
		hot_plug_info.event_type = MPTSAS_ADD_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
		if (!(status & MPI_RAIDVOL0_STATUS_FLAG_ENABLED)) {
			if (!sdev)
				break;
			vdevice->vtarget->deleted = 1; /* block IO */
			hot_plug_info.event_type = MPTSAS_DEL_RAID;
			break;
		}
		switch (state) {
		case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		case MPI_RAIDVOL0_STATUS_STATE_MISSING:
			if (!sdev)
				break;
			vdevice->vtarget->deleted = 1; /* block IO */
			hot_plug_info.event_type = MPTSAS_DEL_RAID;
			break;
		case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
			if (sdev) {
				scsi_device_put(sdev);
				break;
			}
			hot_plug_info.event_type = MPTSAS_ADD_RAID;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (hot_plug_info.event_type != MPTSAS_IGNORE_EVENT)
		mptsas_hotplug_work(ioc, fw_event, &hot_plug_info);
	else
		mptsas_free_fw_event(ioc, fw_event);
}

/**
 *	mptsas_issue_tm - send mptsas internal tm request
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@type: Task Management type
 *	@channel: channel number for task management
 *	@id: Logical Target ID for reset (if appropriate)
 *	@lun: Logical unit for reset (if appropriate)
 *	@task_context: Context for the task to be aborted
 *	@timeout: timeout for task management control
 *	@issue_reset: set to 1 on return if reset is needed, else 0
 *
 *	Return: 0 on success or -1 on failure.
 *
 */
static int
mptsas_issue_tm(MPT_ADAPTER *ioc, u8 type, u8 channel, u8 id, u64 lun,
	int task_context, ulong timeout, u8 *issue_reset)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	int		 retval;
	unsigned long	 timeleft;

	*issue_reset = 0;
	mf = mpt_get_msg_frame(mptsasDeviceResetCtx, ioc);
	if (mf == NULL) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_WARN_FMT "TaskMgmt request: no "
		    "msg frames!!\n", ioc->name));
		goto out;
	}

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt request: mr = %p, "
	    "task_type = 0x%02X,\n\t timeout = %ld, fw_channel = %d, "
	    "fw_id = %d, lun = %lld,\n\t task_context = 0x%x\n", ioc->name, mf,
	     type, timeout, channel, id, (unsigned long long)lun,
	     task_context));

	pScsiTm = (SCSITaskMgmt_t *) mf;
	memset(pScsiTm, 0, sizeof(SCSITaskMgmt_t));
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	pScsiTm->TaskType = type;
	pScsiTm->MsgFlags = 0;
	pScsiTm->TargetID = id;
	pScsiTm->Bus = channel;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Reserved = 0;
	pScsiTm->Reserved1 = 0;
	pScsiTm->TaskMsgContext = task_context;
	int_to_scsilun(lun, (struct scsi_lun *)pScsiTm->LUN);

	INITIALIZE_MGMT_STATUS(ioc->taskmgmt_cmds.status)
	CLEAR_MGMT_STATUS(ioc->internal_cmds.status)
	retval = 0;
	mpt_put_msg_frame_hi_pri(mptsasDeviceResetCtx, ioc, mf);

	/* Now wait for the command to complete */
	timeleft = wait_for_completion_timeout(&ioc->taskmgmt_cmds.done,
	    timeout*HZ);
	if (!(ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_ERR_FMT
		    "TaskMgmt request: TIMED OUT!(mr=%p)\n", ioc->name, mf));
		mpt_free_msg_frame(ioc, mf);
		if (ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		*issue_reset = 1;
		goto out;
	}

	if (!(ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_RF_VALID)) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "TaskMgmt request: failed with no reply\n", ioc->name));
		goto out;
	}

 out:
	CLEAR_MGMT_STATUS(ioc->taskmgmt_cmds.status)
	return retval;
}

/**
 *	mptsas_broadcast_primitive_work - Handle broadcast primitives
 *	@fw_event: work queue payload containing info describing the event
 *
 *	This will be handled in workqueue context.
 */
static void
mptsas_broadcast_primitive_work(struct fw_event_work *fw_event)
{
	MPT_ADAPTER *ioc = fw_event->ioc;
	MPT_FRAME_HDR	*mf;
	VirtDevice	*vdevice;
	int			ii;
	struct scsi_cmnd	*sc;
	SCSITaskMgmtReply_t	*pScsiTmReply;
	u8			issue_reset;
	int			task_context;
	u8			channel, id;
	int			 lun;
	u32			 termination_count;
	u32			 query_count;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "%s - enter\n", ioc->name, __func__));

	mutex_lock(&ioc->taskmgmt_cmds.mutex);
	if (mpt_set_taskmgmt_in_progress_flag(ioc) != 0) {
		mutex_unlock(&ioc->taskmgmt_cmds.mutex);
		mptsas_requeue_fw_event(ioc, fw_event, 1000);
		return;
	}

	issue_reset = 0;
	termination_count = 0;
	query_count = 0;
	mpt_findImVolumes(ioc);
	pScsiTmReply = (SCSITaskMgmtReply_t *) ioc->taskmgmt_cmds.reply;

	for (ii = 0; ii < ioc->req_depth; ii++) {
		if (ioc->fw_events_off)
			goto out;
		sc = mptscsih_get_scsi_lookup(ioc, ii);
		if (!sc)
			continue;
		mf = MPT_INDEX_2_MFPTR(ioc, ii);
		if (!mf)
			continue;
		task_context = mf->u.frame.hwhdr.msgctxu.MsgContext;
		vdevice = sc->device->hostdata;
		if (!vdevice || !vdevice->vtarget)
			continue;
		if (vdevice->vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT)
			continue; /* skip hidden raid components */
		if (vdevice->vtarget->raidVolume)
			continue; /* skip hidden raid components */
		channel = vdevice->vtarget->channel;
		id = vdevice->vtarget->id;
		lun = vdevice->lun;
		if (mptsas_issue_tm(ioc, MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK,
		    channel, id, (u64)lun, task_context, 30, &issue_reset))
			goto out;
		query_count++;
		termination_count +=
		    le32_to_cpu(pScsiTmReply->TerminationCount);
		if ((pScsiTmReply->IOCStatus == MPI_IOCSTATUS_SUCCESS) &&
		    (pScsiTmReply->ResponseCode ==
		    MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED ||
		    pScsiTmReply->ResponseCode ==
		    MPI_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC))
			continue;
		if (mptsas_issue_tm(ioc,
		    MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET,
		    channel, id, (u64)lun, 0, 30, &issue_reset))
			goto out;
		termination_count +=
		    le32_to_cpu(pScsiTmReply->TerminationCount);
	}

 out:
	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "%s - exit, query_count = %d termination_count = %d\n",
	    ioc->name, __func__, query_count, termination_count));

	ioc->broadcast_aen_busy = 0;
	mpt_clear_taskmgmt_in_progress_flag(ioc);
	mutex_unlock(&ioc->taskmgmt_cmds.mutex);

	if (issue_reset) {
		printk(MYIOC_s_WARN_FMT
		       "Issuing Reset from %s!! doorbell=0x%08x\n",
		       ioc->name, __func__, mpt_GetIocState(ioc, 0));
		mpt_Soft_Hard_ResetHandler(ioc, CAN_SLEEP);
	}
	mptsas_free_fw_event(ioc, fw_event);
}

/*
 * mptsas_send_ir2_event - handle exposing hidden disk when
 * an inactive raid volume is added
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @ir2_data
 *
 */
static void
mptsas_send_ir2_event(struct fw_event_work *fw_event)
{
	MPT_ADAPTER	*ioc;
	struct mptsas_hotplug_event hot_plug_info;
	MPI_EVENT_DATA_IR2	*ir2_data;
	u8 reasonCode;
	RaidPhysDiskPage0_t phys_disk;

	ioc = fw_event->ioc;
	ir2_data = (MPI_EVENT_DATA_IR2 *)fw_event->event_data;
	reasonCode = ir2_data->ReasonCode;

	devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Entering %s: "
	    "ReasonCode=%02x\n", ioc->name, __func__, reasonCode));

	memset(&hot_plug_info, 0, sizeof(struct mptsas_hotplug_event));
	hot_plug_info.id = ir2_data->TargetID;
	hot_plug_info.channel = ir2_data->Bus;
	switch (reasonCode) {
	case MPI_EVENT_IR2_RC_FOREIGN_CFG_DETECTED:
		hot_plug_info.event_type = MPTSAS_ADD_INACTIVE_VOLUME;
		break;
	case MPI_EVENT_IR2_RC_DUAL_PORT_REMOVED:
		hot_plug_info.phys_disk_num = ir2_data->PhysDiskNum;
		hot_plug_info.event_type = MPTSAS_DEL_PHYSDISK;
		break;
	case MPI_EVENT_IR2_RC_DUAL_PORT_ADDED:
		hot_plug_info.phys_disk_num = ir2_data->PhysDiskNum;
		mpt_raid_phys_disk_pg0(ioc,
		    ir2_data->PhysDiskNum, &phys_disk);
		hot_plug_info.id = phys_disk.PhysDiskID;
		hot_plug_info.event_type = MPTSAS_ADD_PHYSDISK;
		break;
	default:
		mptsas_free_fw_event(ioc, fw_event);
		return;
	}
	mptsas_hotplug_work(ioc, fw_event, &hot_plug_info);
}

static int
mptsas_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *reply)
{
	u32 event = le32_to_cpu(reply->Event);
	int event_data_sz;
	struct fw_event_work *fw_event;
	unsigned long delay;

	if (ioc->bus_type != SAS)
		return 0;

	/* events turned off due to host reset or driver unloading */
	if (ioc->fw_events_off)
		return 0;

	delay = msecs_to_jiffies(1);
	switch (event) {
	case MPI_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		EVENT_DATA_SAS_BROADCAST_PRIMITIVE *broadcast_event_data =
		    (EVENT_DATA_SAS_BROADCAST_PRIMITIVE *)reply->Data;
		if (broadcast_event_data->Primitive !=
		    MPI_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT)
			return 0;
		if (ioc->broadcast_aen_busy)
			return 0;
		ioc->broadcast_aen_busy = 1;
		break;
	}
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
	{
		EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data =
		    (EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)reply->Data;
		u16	ioc_stat;
		ioc_stat = le16_to_cpu(reply->IOCStatus);

		if (sas_event_data->ReasonCode ==
		    MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING) {
			mptsas_target_reset_queue(ioc, sas_event_data);
			return 0;
		}
		if (sas_event_data->ReasonCode ==
			MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET &&
			ioc->device_missing_delay &&
			(ioc_stat & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)) {
			VirtTarget *vtarget = NULL;
			u8		id, channel;

			id = sas_event_data->TargetID;
			channel = sas_event_data->Bus;

			vtarget = mptsas_find_vtarget(ioc, channel, id);
			if (vtarget) {
				devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
				    "LogInfo (0x%x) available for "
				   "INTERNAL_DEVICE_RESET"
				   "fw_id %d fw_channel %d\n", ioc->name,
				   le32_to_cpu(reply->IOCLogInfo),
				   id, channel));
				if (vtarget->raidVolume) {
					devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"Skipping Raid Volume for inDMD\n",
					ioc->name));
				} else {
					devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT
					"Setting device flag inDMD\n",
					ioc->name));
					vtarget->inDMD = 1;
				}

			}

		}

		break;
	}
	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
	{
		MpiEventDataSasExpanderStatusChange_t *expander_data =
		    (MpiEventDataSasExpanderStatusChange_t *)reply->Data;

		if (ioc->old_sas_discovery_protocal)
			return 0;

		if (expander_data->ReasonCode ==
		    MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING &&
		    ioc->device_missing_delay)
			delay = HZ * ioc->device_missing_delay;
		break;
	}
	case MPI_EVENT_SAS_DISCOVERY:
	{
		u32 discovery_status;
		EventDataSasDiscovery_t *discovery_data =
		    (EventDataSasDiscovery_t *)reply->Data;

		discovery_status = le32_to_cpu(discovery_data->DiscoveryStatus);
		ioc->sas_discovery_quiesce_io = discovery_status ? 1 : 0;
		if (ioc->old_sas_discovery_protocal && !discovery_status)
			mptsas_queue_rescan(ioc);
		return 0;
	}
	case MPI_EVENT_INTEGRATED_RAID:
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
	case MPI_EVENT_IR2:
	case MPI_EVENT_SAS_PHY_LINK_STATUS:
	case MPI_EVENT_QUEUE_FULL:
		break;
	default:
		return 0;
	}

	event_data_sz = ((reply->MsgLength * 4) -
	    offsetof(EventNotificationReply_t, Data));
	fw_event = kzalloc(sizeof(*fw_event) + event_data_sz, GFP_ATOMIC);
	if (!fw_event) {
		printk(MYIOC_s_WARN_FMT "%s: failed at (line=%d)\n", ioc->name,
		 __func__, __LINE__);
		return 0;
	}
	memcpy(fw_event->event_data, reply->Data, event_data_sz);
	fw_event->event = event;
	fw_event->ioc = ioc;
	mptsas_add_fw_event(ioc, fw_event, delay);
	return 0;
}

/* Delete a volume when no longer listed in ioc pg2
 */
static void mptsas_volume_delete(MPT_ADAPTER *ioc, u8 id)
{
	struct scsi_device *sdev;
	int i;

	sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL, id, 0);
	if (!sdev)
		return;
	if (!ioc->raid_data.pIocPg2)
		goto out;
	if (!ioc->raid_data.pIocPg2->NumActiveVolumes)
		goto out;
	for (i = 0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++)
		if (ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID == id)
			goto release_sdev;
 out:
	printk(MYIOC_s_INFO_FMT "removing raid volume, channel %d, "
	    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL, id);
	scsi_remove_device(sdev);
 release_sdev:
	scsi_device_put(sdev);
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
	mptsas_fw_event_off(ioc);
	ioc->DoneCtx = mptsasDoneCtx;
	ioc->TaskCtx = mptsasTaskCtx;
	ioc->InternalCtx = mptsasInternalCtx;
	ioc->schedule_target_reset = &mptsas_schedule_target_reset;
	ioc->schedule_dead_ioc_flush_running_cmds =
				&mptscsih_flush_running_cmds;
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
	sh->can_queue = min_t(int, ioc->req_depth - 10, sh->can_queue);
	sh->max_id = -1;
	sh->max_lun = max_lun;
	sh->transportt = mptsas_transport_template;

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
	scale = ioc->req_sz/ioc->SGE_size;
	if (ioc->sg_addr_size == sizeof(u64)) {
		numSGE = (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 60) / ioc->SGE_size;
	} else {
		numSGE = 1 + (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 64) / ioc->SGE_size;
	}

	if (numSGE < sh->sg_tablesize) {
		/* Reset this value */
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		  "Resetting sg_tablesize to %d from %d\n",
		  ioc->name, numSGE, sh->sg_tablesize));
		sh->sg_tablesize = numSGE;
	}

	if (mpt_loadtime_max_sectors) {
		if (mpt_loadtime_max_sectors < 64 ||
			mpt_loadtime_max_sectors > 8192) {
			printk(MYIOC_s_INFO_FMT "Invalid value passed for"
				"mpt_loadtime_max_sectors %d."
				"Range from 64 to 8192\n", ioc->name,
				mpt_loadtime_max_sectors);
		}
		mpt_loadtime_max_sectors &=  0xFFFFFFFE;
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"Resetting max sector to %d from %d\n",
		  ioc->name, mpt_loadtime_max_sectors, sh->max_sectors));
		sh->max_sectors = mpt_loadtime_max_sectors;
	}

	hd = shost_priv(sh);
	hd->ioc = ioc;

	/* SCSI needs scsi_cmnd lookup table!
	 * (with size equal to req_depth*PtrSz!)
	 */
	ioc->ScsiLookup = kcalloc(ioc->req_depth, sizeof(void *), GFP_ATOMIC);
	if (!ioc->ScsiLookup) {
		error = -ENOMEM;
		spin_unlock_irqrestore(&ioc->FreeQlock, flags);
		goto out_mptsas_probe;
	}
	spin_lock_init(&ioc->scsi_lookup_lock);

	dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ScsiLookup @ %p\n",
		 ioc->name, ioc->ScsiLookup));

	ioc->sas_data.ptClear = mpt_pt_clear;

	hd->last_queue_full = 0;
	INIT_LIST_HEAD(&hd->target_reset_list);
	INIT_LIST_HEAD(&ioc->sas_device_info_list);
	mutex_init(&ioc->sas_device_info_mutex);

	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	if (ioc->sas_data.ptClear==1) {
		mptbase_sas_persist_operation(
		    ioc, MPI_SAS_OP_CLEAR_ALL_PERSISTENT);
	}

	error = scsi_add_host(sh, &ioc->pcidev->dev);
	if (error) {
		dprintk(ioc, printk(MYIOC_s_ERR_FMT
		  "scsi_add_host failed\n", ioc->name));
		goto out_mptsas_probe;
	}

	/* older firmware doesn't support expander events */
	if ((ioc->facts.HeaderVersion >> 8) < 0xE)
		ioc->old_sas_discovery_protocal = 1;
	mptsas_scan_sas_topology(ioc);
	mptsas_fw_event_on(ioc);
	return 0;

 out_mptsas_probe:

	mptscsih_remove(pdev);
	return error;
}

static void
mptsas_shutdown(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);

	mptsas_fw_event_off(ioc);
	mptsas_cleanup_fw_event_q(ioc);
}

static void mptsas_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);
	struct mptsas_portinfo *p, *n;
	int i;

	if (!ioc->sh) {
		printk(MYIOC_s_INFO_FMT "IOC is in Target mode\n", ioc->name);
		mpt_detach(pdev);
		return;
	}

	mptsas_shutdown(pdev);

	mptsas_del_device_components(ioc);

	ioc->sas_discovery_ignore_events = 1;
	sas_remove_host(ioc->sh);

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(p, n, &ioc->sas_topology, list) {
		list_del(&p->list);
		for (i = 0 ; i < p->num_phys ; i++)
			mptsas_port_delete(ioc, p->phy_info[i].port_details);

		kfree(p->phy_info);
		kfree(p);
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	ioc->hba_port_info = NULL;
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
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1068_820XELP,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptsas_pci_table);


static struct pci_driver mptsas_driver = {
	.name		= "mptsas",
	.id_table	= mptsas_pci_table,
	.probe		= mptsas_probe,
	.remove		= mptsas_remove,
	.shutdown	= mptsas_shutdown,
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

	mptsasDoneCtx = mpt_register(mptscsih_io_done, MPTSAS_DRIVER,
	    "mptscsih_io_done");
	mptsasTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTSAS_DRIVER,
	    "mptscsih_taskmgmt_complete");
	mptsasInternalCtx =
		mpt_register(mptscsih_scandv_complete, MPTSAS_DRIVER,
		    "mptscsih_scandv_complete");
	mptsasMgmtCtx = mpt_register(mptsas_mgmt_done, MPTSAS_DRIVER,
	    "mptsas_mgmt_done");
	mptsasDeviceResetCtx =
		mpt_register(mptsas_taskmgmt_complete, MPTSAS_DRIVER,
		    "mptsas_taskmgmt_complete");

	mpt_event_register(mptsasDoneCtx, mptsas_event_process);
	mpt_reset_register(mptsasDoneCtx, mptsas_ioc_reset);

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
	mpt_deregister(mptsasDeviceResetCtx);
}

module_init(mptsas_init);
module_exit(mptsas_exit);

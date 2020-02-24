/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_scsih.c
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/aer.h>
#include <linux/raid_class.h>
#include <asm/unaligned.h>

#include "mpt3sas_base.h"

#define RAID_CHANNEL 1

#define PCIE_CHANNEL 2

/* forward proto's */
static void _scsih_expander_node_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander);
static void _firmware_event_work(struct work_struct *work);

static void _scsih_remove_device(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device);
static int _scsih_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd);
static int _scsih_pcie_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle);
static void _scsih_pcie_device_remove_from_sml(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device);
static void
_scsih_pcie_check_device(struct MPT3SAS_ADAPTER *ioc, u16 handle);
static u8 _scsih_check_for_pending_tm(struct MPT3SAS_ADAPTER *ioc, u16 smid);

/* global parameters */
LIST_HEAD(mpt3sas_ioc_list);
/* global ioc lock for list operations */
DEFINE_SPINLOCK(gioc_lock);

MODULE_AUTHOR(MPT3SAS_AUTHOR);
MODULE_DESCRIPTION(MPT3SAS_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(MPT3SAS_DRIVER_VERSION);
MODULE_ALIAS("mpt2sas");

/* local parameters */
static u8 scsi_io_cb_idx = -1;
static u8 tm_cb_idx = -1;
static u8 ctl_cb_idx = -1;
static u8 base_cb_idx = -1;
static u8 port_enable_cb_idx = -1;
static u8 transport_cb_idx = -1;
static u8 scsih_cb_idx = -1;
static u8 config_cb_idx = -1;
static int mpt2_ids;
static int mpt3_ids;

static u8 tm_tr_cb_idx = -1 ;
static u8 tm_tr_volume_cb_idx = -1 ;
static u8 tm_sas_control_cb_idx = -1;

/* command line options */
static u32 logging_level;
MODULE_PARM_DESC(logging_level,
	" bits for enabling additional logging info (default=0)");


static ushort max_sectors = 0xFFFF;
module_param(max_sectors, ushort, 0444);
MODULE_PARM_DESC(max_sectors, "max sectors, range 64 to 32767  default=32767");


static int missing_delay[2] = {-1, -1};
module_param_array(missing_delay, int, NULL, 0444);
MODULE_PARM_DESC(missing_delay, " device missing delay , io missing delay");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPT3SAS_MAX_LUN (16895)
static u64 max_lun = MPT3SAS_MAX_LUN;
module_param(max_lun, ullong, 0444);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

static ushort hbas_to_enumerate;
module_param(hbas_to_enumerate, ushort, 0444);
MODULE_PARM_DESC(hbas_to_enumerate,
		" 0 - enumerates both SAS 2.0 & SAS 3.0 generation HBAs\n \
		  1 - enumerates only SAS 2.0 generation HBAs\n \
		  2 - enumerates only SAS 3.0 generation HBAs (default=0)");

/* diag_buffer_enable is bitwise
 * bit 0 set = TRACE
 * bit 1 set = SNAPSHOT
 * bit 2 set = EXTENDED
 *
 * Either bit can be set, or both
 */
static int diag_buffer_enable = -1;
module_param(diag_buffer_enable, int, 0444);
MODULE_PARM_DESC(diag_buffer_enable,
	" post diag buffers (TRACE=1/SNAPSHOT=2/EXTENDED=4/default=0)");
static int disable_discovery = -1;
module_param(disable_discovery, int, 0444);
MODULE_PARM_DESC(disable_discovery, " disable discovery ");


/* permit overriding the host protection capabilities mask (EEDP/T10 PI) */
static int prot_mask = -1;
module_param(prot_mask, int, 0444);
MODULE_PARM_DESC(prot_mask, " host protection capabilities mask, def=7 ");

static bool enable_sdev_max_qd;
module_param(enable_sdev_max_qd, bool, 0444);
MODULE_PARM_DESC(enable_sdev_max_qd,
	"Enable sdev max qd as can_queue, def=disabled(0)");

/* raid transport support */
static struct raid_template *mpt3sas_raid_template;
static struct raid_template *mpt2sas_raid_template;


/**
 * struct sense_info - common structure for obtaining sense keys
 * @skey: sense key
 * @asc: additional sense code
 * @ascq: additional sense code qualifier
 */
struct sense_info {
	u8 skey;
	u8 asc;
	u8 ascq;
};

#define MPT3SAS_PROCESS_TRIGGER_DIAG (0xFFFB)
#define MPT3SAS_TURN_ON_PFA_LED (0xFFFC)
#define MPT3SAS_PORT_ENABLE_COMPLETE (0xFFFD)
#define MPT3SAS_ABRT_TASK_SET (0xFFFE)
#define MPT3SAS_REMOVE_UNRESPONDING_DEVICES (0xFFFF)
/**
 * struct fw_event_work - firmware event struct
 * @list: link list framework
 * @work: work object (ioc->fault_reset_work_q)
 * @ioc: per adapter object
 * @device_handle: device handle
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @ignore: flag meaning this event has been marked to ignore
 * @event: firmware event MPI2_EVENT_XXX defined in mpi2_ioc.h
 * @refcount: kref for this event
 * @event_data: reply event data payload follows
 *
 * This object stored on ioc->fw_event_list.
 */
struct fw_event_work {
	struct list_head	list;
	struct work_struct	work;

	struct MPT3SAS_ADAPTER *ioc;
	u16			device_handle;
	u8			VF_ID;
	u8			VP_ID;
	u8			ignore;
	u16			event;
	struct kref		refcount;
	char			event_data[] __aligned(4);
};

static void fw_event_work_free(struct kref *r)
{
	kfree(container_of(r, struct fw_event_work, refcount));
}

static void fw_event_work_get(struct fw_event_work *fw_work)
{
	kref_get(&fw_work->refcount);
}

static void fw_event_work_put(struct fw_event_work *fw_work)
{
	kref_put(&fw_work->refcount, fw_event_work_free);
}

static struct fw_event_work *alloc_fw_event_work(int len)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(*fw_event) + len, GFP_ATOMIC);
	if (!fw_event)
		return NULL;

	kref_init(&fw_event->refcount);
	return fw_event;
}

/**
 * struct _scsi_io_transfer - scsi io transfer
 * @handle: sas device handle (assigned by firmware)
 * @is_raid: flag set for hidden raid components
 * @dir: DMA_TO_DEVICE, DMA_FROM_DEVICE,
 * @data_length: data transfer length
 * @data_dma: dma pointer to data
 * @sense: sense data
 * @lun: lun number
 * @cdb_length: cdb length
 * @cdb: cdb contents
 * @timeout: timeout for this command
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @valid_reply: flag set for reply message
 * @sense_length: sense length
 * @ioc_status: ioc status
 * @scsi_state: scsi state
 * @scsi_status: scsi staus
 * @log_info: log information
 * @transfer_length: data length transfer when there is a reply message
 *
 * Used for sending internal scsi commands to devices within this module.
 * Refer to _scsi_send_scsi_io().
 */
struct _scsi_io_transfer {
	u16	handle;
	u8	is_raid;
	enum dma_data_direction dir;
	u32	data_length;
	dma_addr_t data_dma;
	u8	sense[SCSI_SENSE_BUFFERSIZE];
	u32	lun;
	u8	cdb_length;
	u8	cdb[32];
	u8	timeout;
	u8	VF_ID;
	u8	VP_ID;
	u8	valid_reply;
  /* the following bits are only valid when 'valid_reply = 1' */
	u32	sense_length;
	u16	ioc_status;
	u8	scsi_state;
	u8	scsi_status;
	u32	log_info;
	u32	transfer_length;
};

/**
 * _scsih_set_debug_level - global setting of ioc->logging_level.
 * @val: ?
 * @kp: ?
 *
 * Note: The logging levels are defined in mpt3sas_debug.h.
 */
static int
_scsih_set_debug_level(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT3SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	pr_info("setting logging_level(0x%08x)\n", logging_level);
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list)
		ioc->logging_level = logging_level;
	spin_unlock(&gioc_lock);
	return 0;
}
module_param_call(logging_level, _scsih_set_debug_level, param_get_int,
	&logging_level, 0644);

/**
 * _scsih_srch_boot_sas_address - search based on sas_address
 * @sas_address: sas address
 * @boot_device: boot device object from bios page 2
 *
 * Return: 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_sas_address(u64 sas_address,
	Mpi2BootDeviceSasWwid_t *boot_device)
{
	return (sas_address == le64_to_cpu(boot_device->SASAddress)) ?  1 : 0;
}

/**
 * _scsih_srch_boot_device_name - search based on device name
 * @device_name: device name specified in INDENTIFY fram
 * @boot_device: boot device object from bios page 2
 *
 * Return: 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_device_name(u64 device_name,
	Mpi2BootDeviceDeviceName_t *boot_device)
{
	return (device_name == le64_to_cpu(boot_device->DeviceName)) ? 1 : 0;
}

/**
 * _scsih_srch_boot_encl_slot - search based on enclosure_logical_id/slot
 * @enclosure_logical_id: enclosure logical id
 * @slot_number: slot number
 * @boot_device: boot device object from bios page 2
 *
 * Return: 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_encl_slot(u64 enclosure_logical_id, u16 slot_number,
	Mpi2BootDeviceEnclosureSlot_t *boot_device)
{
	return (enclosure_logical_id == le64_to_cpu(boot_device->
	    EnclosureLogicalID) && slot_number == le16_to_cpu(boot_device->
	    SlotNumber)) ? 1 : 0;
}

/**
 * _scsih_is_boot_device - search for matching boot device.
 * @sas_address: sas address
 * @device_name: device name specified in INDENTIFY fram
 * @enclosure_logical_id: enclosure logical id
 * @slot: slot number
 * @form: specifies boot device form
 * @boot_device: boot device object from bios page 2
 *
 * Return: 1 when there's a match, 0 means no match.
 */
static int
_scsih_is_boot_device(u64 sas_address, u64 device_name,
	u64 enclosure_logical_id, u16 slot, u8 form,
	Mpi2BiosPage2BootDevice_t *boot_device)
{
	int rc = 0;

	switch (form) {
	case MPI2_BIOSPAGE2_FORM_SAS_WWID:
		if (!sas_address)
			break;
		rc = _scsih_srch_boot_sas_address(
		    sas_address, &boot_device->SasWwid);
		break;
	case MPI2_BIOSPAGE2_FORM_ENCLOSURE_SLOT:
		if (!enclosure_logical_id)
			break;
		rc = _scsih_srch_boot_encl_slot(
		    enclosure_logical_id,
		    slot, &boot_device->EnclosureSlot);
		break;
	case MPI2_BIOSPAGE2_FORM_DEVICE_NAME:
		if (!device_name)
			break;
		rc = _scsih_srch_boot_device_name(
		    device_name, &boot_device->DeviceName);
		break;
	case MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED:
		break;
	}

	return rc;
}

/**
 * _scsih_get_sas_address - set the sas_address for given device handle
 * @ioc: ?
 * @handle: device handle
 * @sas_address: sas address
 *
 * Return: 0 success, non-zero when failure
 */
static int
_scsih_get_sas_address(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u64 *sas_address)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 ioc_status;

	*sas_address = 0;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -ENXIO;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		/* For HBA, vSES doesn't return HBA SAS address. Instead return
		 * vSES's sas address.
		 */
		if ((handle <= ioc->sas_hba.num_phys) &&
		   (!(le32_to_cpu(sas_device_pg0.DeviceInfo) &
		   MPI2_SAS_DEVICE_INFO_SEP)))
			*sas_address = ioc->sas_hba.sas_address;
		else
			*sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
		return 0;
	}

	/* we hit this because the given parent handle doesn't exist */
	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return -ENXIO;

	/* else error case */
	ioc_err(ioc, "handle(0x%04x), ioc_status(0x%04x), failure at %s:%d/%s()!\n",
		handle, ioc_status, __FILE__, __LINE__, __func__);
	return -EIO;
}

/**
 * _scsih_determine_boot_device - determine boot device.
 * @ioc: per adapter object
 * @device: sas_device or pcie_device object
 * @channel: SAS or PCIe channel
 *
 * Determines whether this device should be first reported device to
 * to scsi-ml or sas transport, this purpose is for persistent boot device.
 * There are primary, alternate, and current entries in bios page 2. The order
 * priority is primary, alternate, then current.  This routine saves
 * the corresponding device object.
 * The saved data to be used later in _scsih_probe_boot_devices().
 */
static void
_scsih_determine_boot_device(struct MPT3SAS_ADAPTER *ioc, void *device,
	u32 channel)
{
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _raid_device *raid_device;
	u64 sas_address;
	u64 device_name;
	u64 enclosure_logical_id;
	u16 slot;

	 /* only process this function when driver loads */
	if (!ioc->is_driver_loading)
		return;

	 /* no Bios, return immediately */
	if (!ioc->bios_pg3.BiosVersion)
		return;

	if (channel == RAID_CHANNEL) {
		raid_device = device;
		sas_address = raid_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	} else if (channel == PCIE_CHANNEL) {
		pcie_device = device;
		sas_address = pcie_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	} else {
		sas_device = device;
		sas_address = sas_device->sas_address;
		device_name = sas_device->device_name;
		enclosure_logical_id = sas_device->enclosure_logical_id;
		slot = sas_device->slot;
	}

	if (!ioc->req_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedBootDevice)) {
			dinitprintk(ioc,
				    ioc_info(ioc, "%s: req_boot_device(0x%016llx)\n",
					     __func__, (u64)sas_address));
			ioc->req_boot_device.device = device;
			ioc->req_boot_device.channel = channel;
		}
	}

	if (!ioc->req_alt_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqAltBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedAltBootDevice)) {
			dinitprintk(ioc,
				    ioc_info(ioc, "%s: req_alt_boot_device(0x%016llx)\n",
					     __func__, (u64)sas_address));
			ioc->req_alt_boot_device.device = device;
			ioc->req_alt_boot_device.channel = channel;
		}
	}

	if (!ioc->current_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.CurrentBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.CurrentBootDevice)) {
			dinitprintk(ioc,
				    ioc_info(ioc, "%s: current_boot_device(0x%016llx)\n",
					     __func__, (u64)sas_address));
			ioc->current_boot_device.device = device;
			ioc->current_boot_device.channel = channel;
		}
	}
}

static struct _sas_device *
__mpt3sas_get_sdev_from_target(struct MPT3SAS_ADAPTER *ioc,
		struct MPT3SAS_TARGET *tgt_priv)
{
	struct _sas_device *ret;

	assert_spin_locked(&ioc->sas_device_lock);

	ret = tgt_priv->sas_dev;
	if (ret)
		sas_device_get(ret);

	return ret;
}

static struct _sas_device *
mpt3sas_get_sdev_from_target(struct MPT3SAS_ADAPTER *ioc,
		struct MPT3SAS_TARGET *tgt_priv)
{
	struct _sas_device *ret;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	ret = __mpt3sas_get_sdev_from_target(ioc, tgt_priv);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return ret;
}

static struct _pcie_device *
__mpt3sas_get_pdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _pcie_device *ret;

	assert_spin_locked(&ioc->pcie_device_lock);

	ret = tgt_priv->pcie_dev;
	if (ret)
		pcie_device_get(ret);

	return ret;
}

/**
 * mpt3sas_get_pdev_from_target - pcie device search
 * @ioc: per adapter object
 * @tgt_priv: starget private object
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device from target, then return pcie_device object.
 */
static struct _pcie_device *
mpt3sas_get_pdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _pcie_device *ret;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	ret = __mpt3sas_get_pdev_from_target(ioc, tgt_priv);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return ret;
}

struct _sas_device *
__mpt3sas_get_sdev_by_addr(struct MPT3SAS_ADAPTER *ioc,
					u64 sas_address)
{
	struct _sas_device *sas_device;

	assert_spin_locked(&ioc->sas_device_lock);

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->sas_address == sas_address)
			goto found_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->sas_address == sas_address)
			goto found_device;

	return NULL;

found_device:
	sas_device_get(sas_device);
	return sas_device;
}

/**
 * mpt3sas_get_sdev_by_addr - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
struct _sas_device *
mpt3sas_get_sdev_by_addr(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
			sas_address);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

static struct _sas_device *
__mpt3sas_get_sdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;

	assert_spin_locked(&ioc->sas_device_lock);

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->handle == handle)
			goto found_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->handle == handle)
			goto found_device;

	return NULL;

found_device:
	sas_device_get(sas_device);
	return sas_device;
}

/**
 * mpt3sas_get_sdev_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
struct _sas_device *
mpt3sas_get_sdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

/**
 * _scsih_display_enclosure_chassis_info - display device location info
 * @ioc: per adapter object
 * @sas_device: per sas device object
 * @sdev: scsi device struct
 * @starget: scsi target struct
 */
static void
_scsih_display_enclosure_chassis_info(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device, struct scsi_device *sdev,
	struct scsi_target *starget)
{
	if (sdev) {
		if (sas_device->enclosure_handle != 0)
			sdev_printk(KERN_INFO, sdev,
			    "enclosure logical id (0x%016llx), slot(%d) \n",
			    (unsigned long long)
			    sas_device->enclosure_logical_id,
			    sas_device->slot);
		if (sas_device->connector_name[0] != '\0')
			sdev_printk(KERN_INFO, sdev,
			    "enclosure level(0x%04x), connector name( %s)\n",
			    sas_device->enclosure_level,
			    sas_device->connector_name);
		if (sas_device->is_chassis_slot_valid)
			sdev_printk(KERN_INFO, sdev, "chassis slot(0x%04x)\n",
			    sas_device->chassis_slot);
	} else if (starget) {
		if (sas_device->enclosure_handle != 0)
			starget_printk(KERN_INFO, starget,
			    "enclosure logical id(0x%016llx), slot(%d) \n",
			    (unsigned long long)
			    sas_device->enclosure_logical_id,
			    sas_device->slot);
		if (sas_device->connector_name[0] != '\0')
			starget_printk(KERN_INFO, starget,
			    "enclosure level(0x%04x), connector name( %s)\n",
			    sas_device->enclosure_level,
			    sas_device->connector_name);
		if (sas_device->is_chassis_slot_valid)
			starget_printk(KERN_INFO, starget,
			    "chassis slot(0x%04x)\n",
			    sas_device->chassis_slot);
	} else {
		if (sas_device->enclosure_handle != 0)
			ioc_info(ioc, "enclosure logical id(0x%016llx), slot(%d)\n",
				 (u64)sas_device->enclosure_logical_id,
				 sas_device->slot);
		if (sas_device->connector_name[0] != '\0')
			ioc_info(ioc, "enclosure level(0x%04x), connector name( %s)\n",
				 sas_device->enclosure_level,
				 sas_device->connector_name);
		if (sas_device->is_chassis_slot_valid)
			ioc_info(ioc, "chassis slot(0x%04x)\n",
				 sas_device->chassis_slot);
	}
}

/**
 * _scsih_sas_device_remove - remove sas_device from list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * If sas_device is on the list, remove it and decrement its reference count.
 */
static void
_scsih_sas_device_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;

	if (!sas_device)
		return;
	ioc_info(ioc, "removing handle(0x%04x), sas_addr(0x%016llx)\n",
		 sas_device->handle, (u64)sas_device->sas_address);

	_scsih_display_enclosure_chassis_info(ioc, sas_device, NULL, NULL);

	/*
	 * The lock serializes access to the list, but we still need to verify
	 * that nobody removed the entry while we were waiting on the lock.
	 */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	if (!list_empty(&sas_device->list)) {
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_device_remove_by_handle - removing device object by handle
 * @ioc: per adapter object
 * @handle: device handle
 */
static void
_scsih_device_remove_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device) {
		_scsih_remove_device(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * mpt3sas_device_remove_by_sas_address - removing device object by sas address
 * @ioc: per adapter object
 * @sas_address: device sas_address
 */
void
mpt3sas_device_remove_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc, sas_address);
	if (sas_device) {
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device) {
		_scsih_remove_device(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * _scsih_sas_device_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object to the ioc->sas_device_list.
 */
static void
_scsih_sas_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: handle(0x%04x), sas_addr(0x%016llx)\n",
			    __func__, sas_device->handle,
			    (u64)sas_device->sas_address));

	dewtprintk(ioc, _scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (ioc->hide_drives) {
		clear_bit(sas_device->handle, ioc->pend_os_device_add);
		return;
	}

	if (!mpt3sas_transport_port_add(ioc, sas_device->handle,
	     sas_device->sas_address_parent)) {
		_scsih_sas_device_remove(ioc, sas_device);
	} else if (!sas_device->starget) {
		/*
		 * When asyn scanning is enabled, its not possible to remove
		 * devices while scanning is turned on due to an oops in
		 * scsi_sysfs_add_sdev()->add_device()->sysfs_addrm_start()
		 */
		if (!ioc->is_driver_loading) {
			mpt3sas_transport_port_remove(ioc,
			    sas_device->sas_address,
			    sas_device->sas_address_parent);
			_scsih_sas_device_remove(ioc, sas_device);
		}
	} else
		clear_bit(sas_device->handle, ioc->pend_os_device_add);
}

/**
 * _scsih_sas_device_init_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object at driver load time to the ioc->sas_device_init_list.
 */
static void
_scsih_sas_device_init_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: handle(0x%04x), sas_addr(0x%016llx)\n",
			    __func__, sas_device->handle,
			    (u64)sas_device->sas_address));

	dewtprintk(ioc, _scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_init_list);
	_scsih_determine_boot_device(ioc, sas_device, 0);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}


static struct _pcie_device *
__mpt3sas_get_pdev_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->wwid == wwid)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->wwid == wwid)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}


/**
 * mpt3sas_get_pdev_by_wwid - pcie device search
 * @ioc: per adapter object
 * @wwid: wwid
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device based on wwid, then return pcie_device object.
 */
static struct _pcie_device *
mpt3sas_get_pdev_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}


static struct _pcie_device *
__mpt3sas_get_pdev_by_idchannel(struct MPT3SAS_ADAPTER *ioc, int id,
	int channel)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->id == id && pcie_device->channel == channel)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->id == id && pcie_device->channel == channel)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}

static struct _pcie_device *
__mpt3sas_get_pdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->handle == handle)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->handle == handle)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}


/**
 * mpt3sas_get_pdev_by_handle - pcie device search
 * @ioc: per adapter object
 * @handle: Firmware device handle
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device based on handle, then return pcie_device
 * object.
 */
struct _pcie_device *
mpt3sas_get_pdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}

/**
 * _scsih_set_nvme_max_shutdown_latency - Update max_shutdown_latency.
 * @ioc: per adapter object
 * Context: This function will acquire ioc->pcie_device_lock
 *
 * Update ioc->max_shutdown_latency to that NVMe drives RTD3 Entry Latency
 * which has reported maximum among all available NVMe drives.
 * Minimum max_shutdown_latency will be six seconds.
 */
static void
_scsih_set_nvme_max_shutdown_latency(struct MPT3SAS_ADAPTER *ioc)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	u16 shutdown_latency = IO_UNIT_CONTROL_SHUTDOWN_TIMEOUT;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list) {
		if (pcie_device->shutdown_latency) {
			if (shutdown_latency < pcie_device->shutdown_latency)
				shutdown_latency =
					pcie_device->shutdown_latency;
		}
	}
	ioc->max_shutdown_latency = shutdown_latency;
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_pcie_device_remove - remove pcie_device from list.
 * @ioc: per adapter object
 * @pcie_device: the pcie_device object
 * Context: This function will acquire ioc->pcie_device_lock.
 *
 * If pcie_device is on the list, remove it and decrement its reference count.
 */
static void
_scsih_pcie_device_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device)
{
	unsigned long flags;
	int was_on_pcie_device_list = 0;
	u8 update_latency = 0;

	if (!pcie_device)
		return;
	ioc_info(ioc, "removing handle(0x%04x), wwid(0x%016llx)\n",
		 pcie_device->handle, (u64)pcie_device->wwid);
	if (pcie_device->enclosure_handle != 0)
		ioc_info(ioc, "removing enclosure logical id(0x%016llx), slot(%d)\n",
			 (u64)pcie_device->enclosure_logical_id,
			 pcie_device->slot);
	if (pcie_device->connector_name[0] != '\0')
		ioc_info(ioc, "removing enclosure level(0x%04x), connector name( %s)\n",
			 pcie_device->enclosure_level,
			 pcie_device->connector_name);

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	if (!list_empty(&pcie_device->list)) {
		list_del_init(&pcie_device->list);
		was_on_pcie_device_list = 1;
	}
	if (pcie_device->shutdown_latency == ioc->max_shutdown_latency)
		update_latency = 1;
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	if (was_on_pcie_device_list) {
		kfree(pcie_device->serial_number);
		pcie_device_put(pcie_device);
	}

	/*
	 * This device's RTD3 Entry Latency matches IOC's
	 * max_shutdown_latency. Recalculate IOC's max_shutdown_latency
	 * from the available drives as current drive is getting removed.
	 */
	if (update_latency)
		_scsih_set_nvme_max_shutdown_latency(ioc);
}


/**
 * _scsih_pcie_device_remove_by_handle - removing pcie device object by handle
 * @ioc: per adapter object
 * @handle: device handle
 */
static void
_scsih_pcie_device_remove_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	int was_on_pcie_device_list = 0;
	u8 update_latency = 0;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
	if (pcie_device) {
		if (!list_empty(&pcie_device->list)) {
			list_del_init(&pcie_device->list);
			was_on_pcie_device_list = 1;
			pcie_device_put(pcie_device);
		}
		if (pcie_device->shutdown_latency == ioc->max_shutdown_latency)
			update_latency = 1;
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	if (was_on_pcie_device_list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		pcie_device_put(pcie_device);
	}

	/*
	 * This device's RTD3 Entry Latency matches IOC's
	 * max_shutdown_latency. Recalculate IOC's max_shutdown_latency
	 * from the available drives as current drive is getting removed.
	 */
	if (update_latency)
		_scsih_set_nvme_max_shutdown_latency(ioc);
}

/**
 * _scsih_pcie_device_add - add pcie_device object
 * @ioc: per adapter object
 * @pcie_device: pcie_device object
 *
 * This is added to the pcie_device_list link list.
 */
static void
_scsih_pcie_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device)
{
	unsigned long flags;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: handle (0x%04x), wwid(0x%016llx)\n",
			    __func__,
			    pcie_device->handle, (u64)pcie_device->wwid));
	if (pcie_device->enclosure_handle != 0)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enclosure logical id(0x%016llx), slot( %d)\n",
				    __func__,
				    (u64)pcie_device->enclosure_logical_id,
				    pcie_device->slot));
	if (pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enclosure level(0x%04x), connector name( %s)\n",
				    __func__, pcie_device->enclosure_level,
				    pcie_device->connector_name));

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_list);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	if (pcie_device->access_status ==
	    MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED) {
		clear_bit(pcie_device->handle, ioc->pend_os_device_add);
		return;
	}
	if (scsi_add_device(ioc->shost, PCIE_CHANNEL, pcie_device->id, 0)) {
		_scsih_pcie_device_remove(ioc, pcie_device);
	} else if (!pcie_device->starget) {
		if (!ioc->is_driver_loading) {
/*TODO-- Need to find out whether this condition will occur or not*/
			clear_bit(pcie_device->handle, ioc->pend_os_device_add);
		}
	} else
		clear_bit(pcie_device->handle, ioc->pend_os_device_add);
}

/*
 * _scsih_pcie_device_init_add - insert pcie_device to the init list.
 * @ioc: per adapter object
 * @pcie_device: the pcie_device object
 * Context: This function will acquire ioc->pcie_device_lock.
 *
 * Adding new object at driver load time to the ioc->pcie_device_init_list.
 */
static void
_scsih_pcie_device_init_add(struct MPT3SAS_ADAPTER *ioc,
				struct _pcie_device *pcie_device)
{
	unsigned long flags;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: handle (0x%04x), wwid(0x%016llx)\n",
			    __func__,
			    pcie_device->handle, (u64)pcie_device->wwid));
	if (pcie_device->enclosure_handle != 0)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enclosure logical id(0x%016llx), slot( %d)\n",
				    __func__,
				    (u64)pcie_device->enclosure_logical_id,
				    pcie_device->slot));
	if (pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enclosure level(0x%04x), connector name( %s)\n",
				    __func__, pcie_device->enclosure_level,
				    pcie_device->connector_name));

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_init_list);
	if (pcie_device->access_status !=
	    MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED)
		_scsih_determine_boot_device(ioc, pcie_device, PCIE_CHANNEL);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}
/**
 * _scsih_raid_device_find_by_id - raid device search
 * @ioc: per adapter object
 * @id: sas device target id
 * @channel: sas device channel
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on target id, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_id(struct MPT3SAS_ADAPTER *ioc, int id, int channel)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->id == id && raid_device->channel == channel) {
			r = raid_device;
			goto out;
		}
	}

 out:
	return r;
}

/**
 * mpt3sas_raid_device_find_by_handle - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on handle, then return raid_device
 * object.
 */
struct _raid_device *
mpt3sas_raid_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->handle != handle)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_find_by_wwid - raid device search
 * @ioc: per adapter object
 * @wwid: ?
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on wwid, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid != wwid)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_add - add raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 * This is added to the raid_device_list link list.
 */
static void
_scsih_raid_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	unsigned long flags;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: handle(0x%04x), wwid(0x%016llx)\n",
			    __func__,
			    raid_device->handle, (u64)raid_device->wwid));

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_add_tail(&raid_device->list, &ioc->raid_device_list);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_raid_device_remove - delete raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 */
static void
_scsih_raid_device_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_del(&raid_device->list);
	kfree(raid_device);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * mpt3sas_scsih_expander_find_by_handle - expander device search
 * @ioc: per adapter object
 * @handle: expander handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for expander device based on handle, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt3sas_scsih_expander_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->handle != handle)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * mpt3sas_scsih_enclosure_find_by_handle - exclosure device search
 * @ioc: per adapter object
 * @handle: enclosure handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for enclosure device based on handle, then returns the
 * enclosure object.
 */
static struct _enclosure_node *
mpt3sas_scsih_enclosure_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _enclosure_node *enclosure_dev, *r;

	r = NULL;
	list_for_each_entry(enclosure_dev, &ioc->enclosure_list, list) {
		if (le16_to_cpu(enclosure_dev->pg0.EnclosureHandle) != handle)
			continue;
		r = enclosure_dev;
		goto out;
	}
out:
	return r;
}
/**
 * mpt3sas_scsih_expander_find_by_sas_address - expander device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * This searches for expander device based on sas_address, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt3sas_scsih_expander_find_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address)
{
	struct _sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * _scsih_expander_node_add - insert expander device to the list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 * Context: This function will acquire ioc->sas_node_lock.
 *
 * Adding new object to the ioc->sas_expander_list.
 */
static void
_scsih_expander_node_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_add_tail(&sas_expander->list, &ioc->sas_expander_list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_is_end_device - determines if device is an end device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Return: 1 if end device.
 */
static int
_scsih_is_end_device(u32 device_info)
{
	if (device_info & MPI2_SAS_DEVICE_INFO_END_DEVICE &&
		((device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

/**
 * _scsih_is_nvme_pciescsi_device - determines if
 *			device is an pcie nvme/scsi device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Returns 1 if device is pcie device type nvme/scsi.
 */
static int
_scsih_is_nvme_pciescsi_device(u32 device_info)
{
	if (((device_info & MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE)
	    == MPI26_PCIE_DEVINFO_NVME) ||
	    ((device_info & MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE)
	    == MPI26_PCIE_DEVINFO_SCSI))
		return 1;
	else
		return 0;
}

/**
 * mpt3sas_scsih_scsi_lookup_get - returns scmd entry
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return: the smid stored scmd pointer.
 * Then will dereference the stored scmd pointer.
 */
struct scsi_cmnd *
mpt3sas_scsih_scsi_lookup_get(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct scsi_cmnd *scmd = NULL;
	struct scsiio_tracker *st;
	Mpi25SCSIIORequest_t *mpi_request;

	if (smid > 0  &&
	    smid <= ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT) {
		u32 unique_tag = smid - 1;

		mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

		/*
		 * If SCSI IO request is outstanding at driver level then
		 * DevHandle filed must be non-zero. If DevHandle is zero
		 * then it means that this smid is free at driver level,
		 * so return NULL.
		 */
		if (!mpi_request->DevHandle)
			return scmd;

		scmd = scsi_host_find_tag(ioc->shost, unique_tag);
		if (scmd) {
			st = scsi_cmd_priv(scmd);
			if (st->cb_idx == 0xFF || st->smid == 0)
				scmd = NULL;
		}
	}
	return scmd;
}

/**
 * scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 * Return: queue depth.
 */
static int
scsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost = sdev->host;
	int max_depth;
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;

	max_depth = shost->can_queue;

	/*
	 * limit max device queue for SATA to 32 if enable_sdev_max_qd
	 * is disabled.
	 */
	if (ioc->enable_sdev_max_qd)
		goto not_sata;

	sas_device_priv_data = sdev->hostdata;
	if (!sas_device_priv_data)
		goto not_sata;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data)
		goto not_sata;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))
		goto not_sata;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_from_target(ioc, sas_target_priv_data);
	if (sas_device) {
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			max_depth = MPT3SAS_SATA_QUEUE_DEPTH;

		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 not_sata:

	if (!sdev->tagged_supported)
		max_depth = 1;
	if (qdepth > max_depth)
		qdepth = max_depth;
	scsi_change_queue_depth(sdev, qdepth);
	sdev_printk(KERN_INFO, sdev,
	    "qdepth(%d), tagged(%d), scsi_level(%d), cmd_que(%d)\n",
	    sdev->queue_depth, sdev->tagged_supported,
	    sdev->scsi_level, ((sdev->inquiry[7] & 2) >> 1));
	return sdev->queue_depth;
}

/**
 * mpt3sas_scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 * Returns nothing.
 */
void
mpt3sas_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	if (ioc->enable_sdev_max_qd)
		qdepth = shost->can_queue;

	scsih_change_queue_depth(sdev, qdepth);
}

/**
 * scsih_target_alloc - target add routine
 * @starget: scsi target struct
 *
 * Return: 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
scsih_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;
	struct sas_rphy *rphy;

	sas_target_priv_data = kzalloc(sizeof(*sas_target_priv_data),
				       GFP_KERNEL);
	if (!sas_target_priv_data)
		return -ENOMEM;

	starget->hostdata = sas_target_priv_data;
	sas_target_priv_data->starget = starget;
	sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;

	/* RAID volumes */
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			sas_target_priv_data->handle = raid_device->handle;
			sas_target_priv_data->sas_address = raid_device->wwid;
			sas_target_priv_data->flags |= MPT_TARGET_FLAGS_VOLUME;
			if (ioc->is_warpdrive)
				sas_target_priv_data->raid_device = raid_device;
			raid_device->starget = starget;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		return 0;
	}

	/* PCIe devices */
	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_idchannel(ioc, starget->id,
			starget->channel);
		if (pcie_device) {
			sas_target_priv_data->handle = pcie_device->handle;
			sas_target_priv_data->sas_address = pcie_device->wwid;
			sas_target_priv_data->pcie_dev = pcie_device;
			pcie_device->starget = starget;
			pcie_device->id = starget->id;
			pcie_device->channel = starget->channel;
			sas_target_priv_data->flags |=
				MPT_TARGET_FLAGS_PCIE_DEVICE;
			if (pcie_device->fast_path)
				sas_target_priv_data->flags |=
					MPT_TARGET_FASTPATH_IO;
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		return 0;
	}

	/* sas/sata devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
	   rphy->identify.sas_address);

	if (sas_device) {
		sas_target_priv_data->handle = sas_device->handle;
		sas_target_priv_data->sas_address = sas_device->sas_address;
		sas_target_priv_data->sas_dev = sas_device;
		sas_device->starget = starget;
		sas_device->id = starget->id;
		sas_device->channel = starget->channel;
		if (test_bit(sas_device->handle, ioc->pd_handles))
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
		if (sas_device->fast_path)
			sas_target_priv_data->flags |=
					MPT_TARGET_FASTPATH_IO;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return 0;
}

/**
 * scsih_target_destroy - target destroy routine
 * @starget: scsi target struct
 */
static void
scsih_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	sas_target_priv_data = starget->hostdata;
	if (!sas_target_priv_data)
		return;

	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			raid_device->starget = NULL;
			raid_device->sdev = NULL;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		goto out;
	}

	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc,
							sas_target_priv_data);
		if (pcie_device && (pcie_device->starget == starget) &&
			(pcie_device->id == starget->id) &&
			(pcie_device->channel == starget->channel))
			pcie_device->starget = NULL;

		if (pcie_device) {
			/*
			 * Corresponding get() is in _scsih_target_alloc()
			 */
			sas_target_priv_data->pcie_dev = NULL;
			pcie_device_put(pcie_device);
			pcie_device_put(pcie_device);
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		goto out;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_from_target(ioc, sas_target_priv_data);
	if (sas_device && (sas_device->starget == starget) &&
	    (sas_device->id == starget->id) &&
	    (sas_device->channel == starget->channel))
		sas_device->starget = NULL;

	if (sas_device) {
		/*
		 * Corresponding get() is in _scsih_target_alloc()
		 */
		sas_target_priv_data->sas_dev = NULL;
		sas_device_put(sas_device);

		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 out:
	kfree(sas_target_priv_data);
	starget->hostdata = NULL;
}

/**
 * scsih_slave_alloc - device add routine
 * @sdev: scsi device struct
 *
 * Return: 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
scsih_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct MPT3SAS_ADAPTER *ioc;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	sas_device_priv_data = kzalloc(sizeof(*sas_device_priv_data),
				       GFP_KERNEL);
	if (!sas_device_priv_data)
		return -ENOMEM;

	sas_device_priv_data->lun = sdev->lun;
	sas_device_priv_data->flags = MPT_DEVICE_FLAGS_INIT;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns++;
	sas_device_priv_data->sas_target = sas_target_priv_data;
	sdev->hostdata = sas_device_priv_data;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT))
		sdev->no_uld_attach = 1;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_priv(shost);
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc,
		    starget->id, starget->channel);
		if (raid_device)
			raid_device->sdev = sdev; /* raid is single lun */
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_wwid(ioc,
				sas_target_priv_data->sas_address);
		if (pcie_device && (pcie_device->starget == NULL)) {
			sdev_printk(KERN_INFO, sdev,
			    "%s : pcie_device->starget set to starget @ %d\n",
			    __func__, __LINE__);
			pcie_device->starget = starget;
		}

		if (pcie_device)
			pcie_device_put(pcie_device);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else  if (!(sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_by_addr(ioc,
					sas_target_priv_data->sas_address);
		if (sas_device && (sas_device->starget == NULL)) {
			sdev_printk(KERN_INFO, sdev,
			"%s : sas_device->starget set to starget @ %d\n",
			     __func__, __LINE__);
			sas_device->starget = starget;
		}

		if (sas_device)
			sas_device_put(sas_device);

		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}

	return 0;
}

/**
 * scsih_slave_destroy - device destroy routine
 * @sdev: scsi device struct
 */
static void
scsih_slave_destroy(struct scsi_device *sdev)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct Scsi_Host *shost;
	struct MPT3SAS_ADAPTER *ioc;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns--;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_priv(shost);

	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc,
				sas_target_priv_data);
		if (pcie_device && !sas_target_priv_data->num_luns)
			pcie_device->starget = NULL;

		if (pcie_device)
			pcie_device_put(pcie_device);

		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else if (!(sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_from_target(ioc,
				sas_target_priv_data);
		if (sas_device && !sas_target_priv_data->num_luns)
			sas_device->starget = NULL;

		if (sas_device)
			sas_device_put(sas_device);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}

	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

/**
 * _scsih_display_sata_capabilities - sata capabilities
 * @ioc: per adapter object
 * @handle: device handle
 * @sdev: scsi device struct
 */
static void
_scsih_display_sata_capabilities(struct MPT3SAS_ADAPTER *ioc,
	u16 handle, struct scsi_device *sdev)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u16 flags;
	u32 device_info;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	flags = le16_to_cpu(sas_device_pg0.Flags);
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);

	sdev_printk(KERN_INFO, sdev,
	    "atapi(%s), ncq(%s), asyn_notify(%s), smart(%s), fua(%s), "
	    "sw_preserve(%s)\n",
	    (device_info & MPI2_SAS_DEVICE_INFO_ATAPI_DEVICE) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_NCQ_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_ASYNCHRONOUS_NOTIFY) ? "y" :
	    "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_FUA_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SW_PRESERVE) ? "y" : "n");
}

/*
 * raid transport support -
 * Enabled for SLES11 and newer, in older kernels the driver will panic when
 * unloading the driver followed by a load - I believe that the subroutine
 * raid_class_release() is not cleaning up properly.
 */

/**
 * scsih_is_raid - return boolean indicating device is raid volume
 * @dev: the device struct object
 */
static int
scsih_is_raid(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(sdev->host);

	if (ioc->is_warpdrive)
		return 0;
	return (sdev->channel == RAID_CHANNEL) ? 1 : 0;
}

static int
scsih_is_nvme(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return (sdev->channel == PCIE_CHANNEL) ? 1 : 0;
}

/**
 * scsih_get_resync - get raid volume resync percent complete
 * @dev: the device struct object
 */
static void
scsih_get_resync(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(sdev->host);
	static struct _raid_device *raid_device;
	unsigned long flags;
	Mpi2RaidVolPage0_t vol_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 volume_status_flags;
	u8 percent_complete;
	u16 handle;

	percent_complete = 0;
	handle = 0;
	if (ioc->is_warpdrive)
		goto out;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_id(ioc, sdev->id,
	    sdev->channel);
	if (raid_device) {
		handle = raid_device->handle;
		percent_complete = raid_device->percent_complete;
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (!handle)
		goto out;

	if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		percent_complete = 0;
		goto out;
	}

	volume_status_flags = le32_to_cpu(vol_pg0.VolumeStatusFlags);
	if (!(volume_status_flags &
	    MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS))
		percent_complete = 0;

 out:

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_resync(mpt2sas_raid_template, dev, percent_complete);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_resync(mpt3sas_raid_template, dev, percent_complete);
		break;
	}
}

/**
 * scsih_get_state - get raid volume level
 * @dev: the device struct object
 */
static void
scsih_get_state(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(sdev->host);
	static struct _raid_device *raid_device;
	unsigned long flags;
	Mpi2RaidVolPage0_t vol_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 volstate;
	enum raid_state state = RAID_STATE_UNKNOWN;
	u16 handle = 0;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_id(ioc, sdev->id,
	    sdev->channel);
	if (raid_device)
		handle = raid_device->handle;
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (!raid_device)
		goto out;

	if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}

	volstate = le32_to_cpu(vol_pg0.VolumeStatusFlags);
	if (volstate & MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) {
		state = RAID_STATE_RESYNCING;
		goto out;
	}

	switch (vol_pg0.VolumeState) {
	case MPI2_RAID_VOL_STATE_OPTIMAL:
	case MPI2_RAID_VOL_STATE_ONLINE:
		state = RAID_STATE_ACTIVE;
		break;
	case  MPI2_RAID_VOL_STATE_DEGRADED:
		state = RAID_STATE_DEGRADED;
		break;
	case MPI2_RAID_VOL_STATE_FAILED:
	case MPI2_RAID_VOL_STATE_MISSING:
		state = RAID_STATE_OFFLINE;
		break;
	}
 out:
	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_state(mpt2sas_raid_template, dev, state);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_state(mpt3sas_raid_template, dev, state);
		break;
	}
}

/**
 * _scsih_set_level - set raid level
 * @ioc: ?
 * @sdev: scsi device struct
 * @volume_type: volume type
 */
static void
_scsih_set_level(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_device *sdev, u8 volume_type)
{
	enum raid_level level = RAID_LEVEL_UNKNOWN;

	switch (volume_type) {
	case MPI2_RAID_VOL_TYPE_RAID0:
		level = RAID_LEVEL_0;
		break;
	case MPI2_RAID_VOL_TYPE_RAID10:
		level = RAID_LEVEL_10;
		break;
	case MPI2_RAID_VOL_TYPE_RAID1E:
		level = RAID_LEVEL_1E;
		break;
	case MPI2_RAID_VOL_TYPE_RAID1:
		level = RAID_LEVEL_1;
		break;
	}

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_level(mpt2sas_raid_template,
			&sdev->sdev_gendev, level);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_level(mpt3sas_raid_template,
			&sdev->sdev_gendev, level);
		break;
	}
}


/**
 * _scsih_get_volume_capabilities - volume capabilities
 * @ioc: per adapter object
 * @raid_device: the raid_device object
 *
 * Return: 0 for success, else 1
 */
static int
_scsih_get_volume_capabilities(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	Mpi2RaidVolPage0_t *vol_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 sz;
	u8 num_pds;

	if ((mpt3sas_config_get_number_pds(ioc, raid_device->handle,
	    &num_pds)) || !num_pds) {
		dfailprintk(ioc,
			    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
				     __FILE__, __LINE__, __func__));
		return 1;
	}

	raid_device->num_pds = num_pds;
	sz = offsetof(Mpi2RaidVolPage0_t, PhysDisk) + (num_pds *
	    sizeof(Mpi2RaidVol0PhysDisk_t));
	vol_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!vol_pg0) {
		dfailprintk(ioc,
			    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
				     __FILE__, __LINE__, __func__));
		return 1;
	}

	if ((mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, raid_device->handle, sz))) {
		dfailprintk(ioc,
			    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
				     __FILE__, __LINE__, __func__));
		kfree(vol_pg0);
		return 1;
	}

	raid_device->volume_type = vol_pg0->VolumeType;

	/* figure out what the underlying devices are by
	 * obtaining the device_info bits for the 1st device
	 */
	if (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM,
	    vol_pg0->PhysDisk[0].PhysDiskNum))) {
		if (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    le16_to_cpu(pd_pg0.DevHandle)))) {
			raid_device->device_info =
			    le32_to_cpu(sas_device_pg0.DeviceInfo);
		}
	}

	kfree(vol_pg0);
	return 0;
}

/**
 * _scsih_enable_tlr - setting TLR flags
 * @ioc: per adapter object
 * @sdev: scsi device struct
 *
 * Enabling Transaction Layer Retries for tape devices when
 * vpd page 0x90 is present
 *
 */
static void
_scsih_enable_tlr(struct MPT3SAS_ADAPTER *ioc, struct scsi_device *sdev)
{

	/* only for TAPE */
	if (sdev->type != TYPE_TAPE)
		return;

	if (!(ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR))
		return;

	sas_enable_tlr(sdev);
	sdev_printk(KERN_INFO, sdev, "TLR %s\n",
	    sas_is_tlr_enabled(sdev) ? "Enabled" : "Disabled");
	return;

}

/**
 * scsih_slave_configure - device configure routine.
 * @sdev: scsi device struct
 *
 * Return: 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
scsih_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	int qdepth;
	u8 ssp_target = 0;
	char *ds = "";
	char *r_level = "";
	u16 handle, volume_handle = 0;
	u64 volume_wwid = 0;

	qdepth = 1;
	sas_device_priv_data = sdev->hostdata;
	sas_device_priv_data->configured_lun = 1;
	sas_device_priv_data->flags &= ~MPT_DEVICE_FLAGS_INIT;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	handle = sas_target_priv_data->handle;

	/* raid volume handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (!raid_device) {
			dfailprintk(ioc,
				    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
					     __FILE__, __LINE__, __func__));
			return 1;
		}

		if (_scsih_get_volume_capabilities(ioc, raid_device)) {
			dfailprintk(ioc,
				    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
					     __FILE__, __LINE__, __func__));
			return 1;
		}

		/*
		 * WARPDRIVE: Initialize the required data for Direct IO
		 */
		mpt3sas_init_warpdrive_properties(ioc, raid_device);

		/* RAID Queue Depth Support
		 * IS volume = underlying qdepth of drive type, either
		 *    MPT3SAS_SAS_QUEUE_DEPTH or MPT3SAS_SATA_QUEUE_DEPTH
		 * IM/IME/R10 = 128 (MPT3SAS_RAID_QUEUE_DEPTH)
		 */
		if (raid_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
			qdepth = MPT3SAS_SAS_QUEUE_DEPTH;
			ds = "SSP";
		} else {
			qdepth = MPT3SAS_SATA_QUEUE_DEPTH;
			if (raid_device->device_info &
			    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
				ds = "SATA";
			else
				ds = "STP";
		}

		switch (raid_device->volume_type) {
		case MPI2_RAID_VOL_TYPE_RAID0:
			r_level = "RAID0";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1E:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			if (ioc->manu_pg10.OEMIdentifier &&
			    (le32_to_cpu(ioc->manu_pg10.GenericFlags0) &
			    MFG10_GF0_R10_DISPLAY) &&
			    !(raid_device->num_pds % 2))
				r_level = "RAID10";
			else
				r_level = "RAID1E";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID1";
			break;
		case MPI2_RAID_VOL_TYPE_RAID10:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID10";
			break;
		case MPI2_RAID_VOL_TYPE_UNKNOWN:
		default:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAIDX";
			break;
		}

		if (!ioc->hide_ir_msg)
			sdev_printk(KERN_INFO, sdev,
			   "%s: handle(0x%04x), wwid(0x%016llx),"
			    " pd_count(%d), type(%s)\n",
			    r_level, raid_device->handle,
			    (unsigned long long)raid_device->wwid,
			    raid_device->num_pds, ds);

		if (shost->max_sectors > MPT3SAS_RAID_MAX_SECTORS) {
			blk_queue_max_hw_sectors(sdev->request_queue,
						MPT3SAS_RAID_MAX_SECTORS);
			sdev_printk(KERN_INFO, sdev,
					"Set queue's max_sector to: %u\n",
						MPT3SAS_RAID_MAX_SECTORS);
		}

		mpt3sas_scsih_change_queue_depth(sdev, qdepth);

		/* raid transport support */
		if (!ioc->is_warpdrive)
			_scsih_set_level(ioc, sdev, raid_device->volume_type);
		return 0;
	}

	/* non-raid handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) {
		if (mpt3sas_config_get_volume_handle(ioc, handle,
		    &volume_handle)) {
			dfailprintk(ioc,
				    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
					     __FILE__, __LINE__, __func__));
			return 1;
		}
		if (volume_handle && mpt3sas_config_get_volume_wwid(ioc,
		    volume_handle, &volume_wwid)) {
			dfailprintk(ioc,
				    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
					     __FILE__, __LINE__, __func__));
			return 1;
		}
	}

	/* PCIe handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_wwid(ioc,
				sas_device_priv_data->sas_target->sas_address);
		if (!pcie_device) {
			spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
			dfailprintk(ioc,
				    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
					     __FILE__, __LINE__, __func__));
			return 1;
		}

		qdepth = MPT3SAS_NVME_QUEUE_DEPTH;
		ds = "NVMe";
		sdev_printk(KERN_INFO, sdev,
			"%s: handle(0x%04x), wwid(0x%016llx), port(%d)\n",
			ds, handle, (unsigned long long)pcie_device->wwid,
			pcie_device->port_num);
		if (pcie_device->enclosure_handle != 0)
			sdev_printk(KERN_INFO, sdev,
			"%s: enclosure logical id(0x%016llx), slot(%d)\n",
			ds,
			(unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot);
		if (pcie_device->connector_name[0] != '\0')
			sdev_printk(KERN_INFO, sdev,
				"%s: enclosure level(0x%04x),"
				"connector name( %s)\n", ds,
				pcie_device->enclosure_level,
				pcie_device->connector_name);

		if (pcie_device->nvme_mdts)
			blk_queue_max_hw_sectors(sdev->request_queue,
					pcie_device->nvme_mdts/512);

		pcie_device_put(pcie_device);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		mpt3sas_scsih_change_queue_depth(sdev, qdepth);
		/* Enable QUEUE_FLAG_NOMERGES flag, so that IOs won't be
		 ** merged and can eliminate holes created during merging
		 ** operation.
		 **/
		blk_queue_flag_set(QUEUE_FLAG_NOMERGES,
				sdev->request_queue);
		blk_queue_virt_boundary(sdev->request_queue,
				ioc->page_size - 1);
		return 0;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
	   sas_device_priv_data->sas_target->sas_address);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		dfailprintk(ioc,
			    ioc_warn(ioc, "failure at %s:%d/%s()!\n",
				     __FILE__, __LINE__, __func__));
		return 1;
	}

	sas_device->volume_handle = volume_handle;
	sas_device->volume_wwid = volume_wwid;
	if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
		qdepth = MPT3SAS_SAS_QUEUE_DEPTH;
		ssp_target = 1;
		if (sas_device->device_info &
				MPI2_SAS_DEVICE_INFO_SEP) {
			sdev_printk(KERN_WARNING, sdev,
			"set ignore_delay_remove for handle(0x%04x)\n",
			sas_device_priv_data->sas_target->handle);
			sas_device_priv_data->ignore_delay_remove = 1;
			ds = "SES";
		} else
			ds = "SSP";
	} else {
		qdepth = MPT3SAS_SATA_QUEUE_DEPTH;
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET)
			ds = "STP";
		else if (sas_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "SATA";
	}

	sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), " \
	    "sas_addr(0x%016llx), phy(%d), device_name(0x%016llx)\n",
	    ds, handle, (unsigned long long)sas_device->sas_address,
	    sas_device->phy, (unsigned long long)sas_device->device_name);

	_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);

	sas_device_put(sas_device);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (!ssp_target)
		_scsih_display_sata_capabilities(ioc, handle, sdev);


	mpt3sas_scsih_change_queue_depth(sdev, qdepth);

	if (ssp_target) {
		sas_read_port_mode_page(sdev);
		_scsih_enable_tlr(ioc, sdev);
	}

	return 0;
}

/**
 * scsih_bios_param - fetch head, sector, cylinder info for a disk
 * @sdev: scsi device struct
 * @bdev: pointer to block device context
 * @capacity: device size (in 512 byte sectors)
 * @params: three element array to place output:
 *              params[0] number of heads (max 255)
 *              params[1] number of sectors (max 63)
 *              params[2] number of cylinders
 */
static int
scsih_bios_param(struct scsi_device *sdev, struct block_device *bdev,
	sector_t capacity, int params[])
{
	int		heads;
	int		sectors;
	sector_t	cylinders;
	ulong		dummy;

	heads = 64;
	sectors = 32;

	dummy = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders, dummy);

	/*
	 * Handle extended translation size for logical drives
	 * > 1Gb
	 */
	if ((ulong)capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		dummy = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders, dummy);
	}

	/* return result */
	params[0] = heads;
	params[1] = sectors;
	params[2] = cylinders;

	return 0;
}

/**
 * _scsih_response_code - translation of device response code
 * @ioc: per adapter object
 * @response_code: response code returned by the device
 */
static void
_scsih_response_code(struct MPT3SAS_ADAPTER *ioc, u8 response_code)
{
	char *desc;

	switch (response_code) {
	case MPI2_SCSITASKMGMT_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI2_SCSITASKMGMT_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case 0xA:
		desc = "overlapped tag attempted";
		break;
	case MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	ioc_warn(ioc, "response_code(0x%01x): %s\n", response_code, desc);
}

/**
 * _scsih_tm_done - tm completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using scsih_issue_tm.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_tm_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->tm_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->tm_cmds.smid != smid)
		return 1;
	ioc->tm_cmds.status |= MPT3_CMD_COMPLETE;
	mpi_reply =  mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		memcpy(ioc->tm_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
		ioc->tm_cmds.status |= MPT3_CMD_REPLY_VALID;
	}
	ioc->tm_cmds.status &= ~MPT3_CMD_PENDING;
	complete(&ioc->tm_cmds.done);
	return 1;
}

/**
 * mpt3sas_scsih_set_tm_flag - set per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt3sas_scsih_set_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 1;
			skip = 1;
			ioc->ignore_loginfos = 1;
		}
	}
}

/**
 * mpt3sas_scsih_clear_tm_flag - clear per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt3sas_scsih_clear_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 0;
			skip = 1;
			ioc->ignore_loginfos = 0;
		}
	}
}

/**
 * mpt3sas_scsih_issue_tm - main routine for sending tm requests
 * @ioc: per adapter struct
 * @handle: device handle
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 * @msix_task: MSIX table index supplied by the OS
 * @timeout: timeout in seconds
 * @tr_method: Target Reset Method
 * Context: user
 *
 * A generic API for sending task management requests to firmware.
 *
 * The callback index is set inside `ioc->tm_cb_idx`.
 * The caller is responsible to check for outstanding commands.
 *
 * Return: SUCCESS or FAILED.
 */
int
mpt3sas_scsih_issue_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle, u64 lun,
	u8 type, u16 smid_task, u16 msix_task, u8 timeout, u8 tr_method)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	u16 smid = 0;
	u32 ioc_state;
	int rc;
	u8 issue_reset = 0;

	lockdep_assert_held(&ioc->tm_cmds.mutex);

	if (ioc->tm_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_info(ioc, "%s: tm_cmd busy!!!\n", __func__);
		return FAILED;
	}

	if (ioc->shost_recovery || ioc->remove_host ||
	    ioc->pci_error_recovery) {
		ioc_info(ioc, "%s: host reset in progress!\n", __func__);
		return FAILED;
	}

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, ioc_info(ioc, "unexpected doorbell active!\n"));
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	} else if ((ioc_state & MPI2_IOC_STATE_MASK) ==
	    MPI2_IOC_STATE_COREDUMP) {
		mpt3sas_print_coredump_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		return FAILED;
	}

	dtmprintk(ioc,
		  ioc_info(ioc, "sending tm: handle(0x%04x), task_type(0x%02x), smid(%d), timeout(%d), tr_method(0x%x)\n",
			   handle, type, smid_task, timeout, tr_method));
	ioc->tm_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->tm_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	memset(ioc->tm_cmds.reply, 0, sizeof(Mpi2SCSITaskManagementReply_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = type;
	mpi_request->MsgFlags = tr_method;
	mpi_request->TaskMID = cpu_to_le16(smid_task);
	int_to_scsilun(lun, (struct scsi_lun *)mpi_request->LUN);
	mpt3sas_scsih_set_tm_flag(ioc, handle);
	init_completion(&ioc->tm_cmds.done);
	ioc->put_smid_hi_priority(ioc, smid, msix_task);
	wait_for_completion_timeout(&ioc->tm_cmds.done, timeout*HZ);
	if (!(ioc->tm_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->tm_cmds.status, mpi_request,
		    sizeof(Mpi2SCSITaskManagementRequest_t)/4, issue_reset);
		if (issue_reset) {
			rc = mpt3sas_base_hard_reset_handler(ioc,
					FORCE_BIG_HAMMER);
			rc = (!rc) ? SUCCESS : FAILED;
			goto out;
		}
	}

	/* sync IRQs in case those were busy during flush. */
	mpt3sas_base_sync_reply_irqs(ioc);

	if (ioc->tm_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
		mpi_reply = ioc->tm_cmds.reply;
		dtmprintk(ioc,
			  ioc_info(ioc, "complete tm: ioc_status(0x%04x), loginfo(0x%08x), term_count(0x%08x)\n",
				   le16_to_cpu(mpi_reply->IOCStatus),
				   le32_to_cpu(mpi_reply->IOCLogInfo),
				   le32_to_cpu(mpi_reply->TerminationCount)));
		if (ioc->logging_level & MPT_DEBUG_TM) {
			_scsih_response_code(ioc, mpi_reply->ResponseCode);
			if (mpi_reply->IOCStatus)
				_debug_dump_mf(mpi_request,
				    sizeof(Mpi2SCSITaskManagementRequest_t)/4);
		}
	}
	rc = SUCCESS;

out:
	mpt3sas_scsih_clear_tm_flag(ioc, handle);
	ioc->tm_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}

int mpt3sas_scsih_issue_locked_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle,
		u64 lun, u8 type, u16 smid_task, u16 msix_task,
		u8 timeout, u8 tr_method)
{
	int ret;

	mutex_lock(&ioc->tm_cmds.mutex);
	ret = mpt3sas_scsih_issue_tm(ioc, handle, lun, type, smid_task,
			msix_task, timeout, tr_method);
	mutex_unlock(&ioc->tm_cmds.mutex);

	return ret;
}

/**
 * _scsih_tm_display_info - displays info about the device
 * @ioc: per adapter struct
 * @scmd: pointer to scsi command object
 *
 * Called by task management callback handlers.
 */
static void
_scsih_tm_display_info(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd)
{
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *priv_target = starget->hostdata;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	unsigned long flags;
	char *device_str = NULL;

	if (!priv_target)
		return;
	if (ioc->hide_ir_msg)
		device_str = "WarpDrive";
	else
		device_str = "volume";

	scsi_print_command(scmd);
	if (priv_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		starget_printk(KERN_INFO, starget,
			"%s handle(0x%04x), %s wwid(0x%016llx)\n",
			device_str, priv_target->handle,
		    device_str, (unsigned long long)priv_target->sas_address);

	} else if (priv_target->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc, priv_target);
		if (pcie_device) {
			starget_printk(KERN_INFO, starget,
				"handle(0x%04x), wwid(0x%016llx), port(%d)\n",
				pcie_device->handle,
				(unsigned long long)pcie_device->wwid,
				pcie_device->port_num);
			if (pcie_device->enclosure_handle != 0)
				starget_printk(KERN_INFO, starget,
					"enclosure logical id(0x%016llx), slot(%d)\n",
					(unsigned long long)
					pcie_device->enclosure_logical_id,
					pcie_device->slot);
			if (pcie_device->connector_name[0] != '\0')
				starget_printk(KERN_INFO, starget,
					"enclosure level(0x%04x), connector name( %s)\n",
					pcie_device->enclosure_level,
					pcie_device->connector_name);
			pcie_device_put(pcie_device);
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_from_target(ioc, priv_target);
		if (sas_device) {
			if (priv_target->flags &
			    MPT_TARGET_FLAGS_RAID_COMPONENT) {
				starget_printk(KERN_INFO, starget,
				    "volume handle(0x%04x), "
				    "volume wwid(0x%016llx)\n",
				    sas_device->volume_handle,
				   (unsigned long long)sas_device->volume_wwid);
			}
			starget_printk(KERN_INFO, starget,
			    "handle(0x%04x), sas_address(0x%016llx), phy(%d)\n",
			    sas_device->handle,
			    (unsigned long long)sas_device->sas_address,
			    sas_device->phy);

			_scsih_display_enclosure_chassis_info(NULL, sas_device,
			    NULL, starget);

			sas_device_put(sas_device);
		}
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}
}

/**
 * scsih_abort - eh threads main abort routine
 * @scmd: pointer to scsi command object
 *
 * Return: SUCCESS if command aborted else FAILED
 */
static int
scsih_abort(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsiio_tracker *st = scsi_cmd_priv(scmd);
	u16 handle;
	int r;

	u8 timeout = 30;
	struct _pcie_device *pcie_device = NULL;
	sdev_printk(KERN_INFO, scmd->device, "attempting task abort!"
	    "scmd(0x%p), outstanding for %u ms & timeout %u ms\n",
	    scmd, jiffies_to_msecs(jiffies - scmd->jiffies_at_alloc),
	    (scmd->request->timeout / HZ) * 1000);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	    ioc->remove_host) {
		sdev_printk(KERN_INFO, scmd->device,
		    "device been deleted! scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* check for completed command */
	if (st == NULL || st->cb_idx == 0xFF) {
		sdev_printk(KERN_INFO, scmd->device, "No reference found at "
		    "driver, assuming scmd(0x%p) might have completed\n", scmd);
		scmd->result = DID_RESET << 16;
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components and volumes this is not supported */
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT ||
	    sas_device_priv_data->sas_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	mpt3sas_halt_firmware(ioc);

	handle = sas_device_priv_data->sas_target->handle;
	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);
	if (pcie_device && (!ioc->tm_custom_handling) &&
	    (!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info))))
		timeout = ioc->nvme_abort_timeout;
	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->lun,
		MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK,
		st->smid, st->msix_io, timeout, 0);
	/* Command must be cleared after abort */
	if (r == SUCCESS && st->cb_idx != 0xFF)
		r = FAILED;
 out:
	sdev_printk(KERN_INFO, scmd->device, "task abort: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	if (pcie_device)
		pcie_device_put(pcie_device);
	return r;
}

/**
 * scsih_dev_reset - eh threads main device reset routine
 * @scmd: pointer to scsi command object
 *
 * Return: SUCCESS if command aborted else FAILED
 */
static int
scsih_dev_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	u16	handle;
	u8	tr_method = 0;
	u8	tr_timeout = 30;
	int r;

	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *target_priv_data = starget->hostdata;

	sdev_printk(KERN_INFO, scmd->device,
	    "attempting device reset! scmd(0x%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	    ioc->remove_host) {
		sdev_printk(KERN_INFO, scmd->device,
		    "device been deleted! scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		sas_device = mpt3sas_get_sdev_from_target(ioc,
				target_priv_data);
		if (sas_device)
			handle = sas_device->volume_handle;
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
	    (!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	} else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->lun,
		MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, 0, 0,
		tr_timeout, tr_method);
	/* Check for busy commands after reset */
	if (r == SUCCESS && atomic_read(&scmd->device->device_busy))
		r = FAILED;
 out:
	sdev_printk(KERN_INFO, scmd->device, "device reset: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);

	return r;
}

/**
 * scsih_target_reset - eh threads main target reset routine
 * @scmd: pointer to scsi command object
 *
 * Return: SUCCESS if command aborted else FAILED
 */
static int
scsih_target_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	u16	handle;
	u8	tr_method = 0;
	u8	tr_timeout = 30;
	int r;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *target_priv_data = starget->hostdata;

	starget_printk(KERN_INFO, starget,
	    "attempting target reset! scmd(0x%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	    ioc->remove_host) {
		starget_printk(KERN_INFO, starget,
		    "target been deleted! scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		sas_device = mpt3sas_get_sdev_from_target(ioc,
				target_priv_data);
		if (sas_device)
			handle = sas_device->volume_handle;
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
	    (!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	} else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;
	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, 0,
		MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 0,
	    tr_timeout, tr_method);
	/* Check for busy commands after reset */
	if (r == SUCCESS && atomic_read(&starget->target_busy))
		r = FAILED;
 out:
	starget_printk(KERN_INFO, starget, "target reset: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);
	return r;
}


/**
 * scsih_host_reset - eh threads main host reset routine
 * @scmd: pointer to scsi command object
 *
 * Return: SUCCESS if command aborted else FAILED
 */
static int
scsih_host_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	int r, retval;

	ioc_info(ioc, "attempting host reset! scmd(0x%p)\n", scmd);
	scsi_print_command(scmd);

	if (ioc->is_driver_loading || ioc->remove_host) {
		ioc_info(ioc, "Blocking the host reset\n");
		r = FAILED;
		goto out;
	}

	retval = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	r = (retval < 0) ? FAILED : SUCCESS;
out:
	ioc_info(ioc, "host reset: %s scmd(0x%p)\n",
		 r == SUCCESS ? "SUCCESS" : "FAILED", scmd);

	return r;
}

/**
 * _scsih_fw_event_add - insert and queue up fw_event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * This adds the firmware event object into link list, then queues it up to
 * be processed from user context.
 */
static void
_scsih_fw_event_add(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	unsigned long flags;

	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	fw_event_work_get(fw_event);
	INIT_LIST_HEAD(&fw_event->list);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
	INIT_WORK(&fw_event->work, _firmware_event_work);
	fw_event_work_get(fw_event);
	queue_work(ioc->firmware_event_thread, &fw_event->work);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_del_from_list - delete fw_event from the list
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * If the fw_event is on the fw_event_list, remove it and do a put.
 */
static void
_scsih_fw_event_del_from_list(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work
	*fw_event)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (!list_empty(&fw_event->list)) {
		list_del_init(&fw_event->list);
		fw_event_work_put(fw_event);
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}


 /**
 * mpt3sas_send_trigger_data_event - send event for processing trigger data
 * @ioc: per adapter object
 * @event_data: trigger event data
 */
void
mpt3sas_send_trigger_data_event(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data)
{
	struct fw_event_work *fw_event;
	u16 sz;

	if (ioc->is_driver_loading)
		return;
	sz = sizeof(*event_data);
	fw_event = alloc_fw_event_work(sz);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_PROCESS_TRIGGER_DIAG;
	fw_event->ioc = ioc;
	memcpy(fw_event->event_data, event_data, sizeof(*event_data));
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * _scsih_error_recovery_delete_devices - remove devices not responding
 * @ioc: per adapter object
 */
static void
_scsih_error_recovery_delete_devices(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	if (ioc->is_driver_loading)
		return;
	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_REMOVE_UNRESPONDING_DEVICES;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * mpt3sas_port_enable_complete - port enable completed (fake event)
 * @ioc: per adapter object
 */
void
mpt3sas_port_enable_complete(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_PORT_ENABLE_COMPLETE;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

static struct fw_event_work *dequeue_next_fw_event(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct fw_event_work *fw_event = NULL;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (!list_empty(&ioc->fw_event_list)) {
		fw_event = list_first_entry(&ioc->fw_event_list,
				struct fw_event_work, list);
		list_del_init(&fw_event->list);
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

	return fw_event;
}

/**
 * _scsih_fw_event_cleanup_queue - cleanup event queue
 * @ioc: per adapter object
 *
 * Walk the firmware event queue, either killing timers, or waiting
 * for outstanding events to complete
 */
static void
_scsih_fw_event_cleanup_queue(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	if (list_empty(&ioc->fw_event_list) ||
	     !ioc->firmware_event_thread || in_interrupt())
		return;

	while ((fw_event = dequeue_next_fw_event(ioc))) {
		/*
		 * Wait on the fw_event to complete. If this returns 1, then
		 * the event was never executed, and we need a put for the
		 * reference the work had on the fw_event.
		 *
		 * If it did execute, we wait for it to finish, and the put will
		 * happen from _firmware_event_work()
		 */
		if (cancel_work_sync(&fw_event->work))
			fw_event_work_put(fw_event);

		fw_event_work_put(fw_event);
	}
}

/**
 * _scsih_internal_device_block - block the sdev device
 * @sdev: per device object
 * @sas_device_priv_data : per device driver private data
 *
 * make sure device is blocked without error, if not
 * print an error
 */
static void
_scsih_internal_device_block(struct scsi_device *sdev,
			struct MPT3SAS_DEVICE *sas_device_priv_data)
{
	int r = 0;

	sdev_printk(KERN_INFO, sdev, "device_block, handle(0x%04x)\n",
	    sas_device_priv_data->sas_target->handle);
	sas_device_priv_data->block = 1;

	r = scsi_internal_device_block_nowait(sdev);
	if (r == -EINVAL)
		sdev_printk(KERN_WARNING, sdev,
		    "device_block failed with return(%d) for handle(0x%04x)\n",
		    r, sas_device_priv_data->sas_target->handle);
}

/**
 * _scsih_internal_device_unblock - unblock the sdev device
 * @sdev: per device object
 * @sas_device_priv_data : per device driver private data
 * make sure device is unblocked without error, if not retry
 * by blocking and then unblocking
 */

static void
_scsih_internal_device_unblock(struct scsi_device *sdev,
			struct MPT3SAS_DEVICE *sas_device_priv_data)
{
	int r = 0;

	sdev_printk(KERN_WARNING, sdev, "device_unblock and setting to running, "
	    "handle(0x%04x)\n", sas_device_priv_data->sas_target->handle);
	sas_device_priv_data->block = 0;
	r = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
	if (r == -EINVAL) {
		/* The device has been set to SDEV_RUNNING by SD layer during
		 * device addition but the request queue is still stopped by
		 * our earlier block call. We need to perform a block again
		 * to get the device to SDEV_BLOCK and then to SDEV_RUNNING */

		sdev_printk(KERN_WARNING, sdev,
		    "device_unblock failed with return(%d) for handle(0x%04x) "
		    "performing a block followed by an unblock\n",
		    r, sas_device_priv_data->sas_target->handle);
		sas_device_priv_data->block = 1;
		r = scsi_internal_device_block_nowait(sdev);
		if (r)
			sdev_printk(KERN_WARNING, sdev, "retried device_block "
			    "failed with return(%d) for handle(0x%04x)\n",
			    r, sas_device_priv_data->sas_target->handle);

		sas_device_priv_data->block = 0;
		r = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
		if (r)
			sdev_printk(KERN_WARNING, sdev, "retried device_unblock"
			    " failed with return(%d) for handle(0x%04x)\n",
			    r, sas_device_priv_data->sas_target->handle);
	}
}

/**
 * _scsih_ublock_io_all_device - unblock every device
 * @ioc: per adapter object
 *
 * change the device state from block to running
 */
static void
_scsih_ublock_io_all_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (!sas_device_priv_data->block)
			continue;

		dewtprintk(ioc, sdev_printk(KERN_INFO, sdev,
			"device_running, handle(0x%04x)\n",
		    sas_device_priv_data->sas_target->handle));
		_scsih_internal_device_unblock(sdev, sas_device_priv_data);
	}
}


/**
 * _scsih_ublock_io_device - prepare device to be deleted
 * @ioc: per adapter object
 * @sas_address: sas address
 *
 * unblock then put device in offline state
 */
static void
_scsih_ublock_io_device(struct MPT3SAS_ADAPTER *ioc, u64 sas_address)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->sas_address
		    != sas_address)
			continue;
		if (sas_device_priv_data->block)
			_scsih_internal_device_unblock(sdev,
				sas_device_priv_data);
	}
}

/**
 * _scsih_block_io_all_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 *
 * During device pull we need to appropriately set the sdev state.
 */
static void
_scsih_block_io_all_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->ignore_delay_remove) {
			sdev_printk(KERN_INFO, sdev,
			"%s skip device_block for SES handle(0x%04x)\n",
			__func__, sas_device_priv_data->sas_target->handle);
			continue;
		}
		_scsih_internal_device_block(sdev, sas_device_priv_data);
	}
}

/**
 * _scsih_block_io_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropriately set the sdev state.
 */
static void
_scsih_block_io_device(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	struct _sas_device *sas_device;

	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle != handle)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device && sas_device->pend_sas_rphy_add)
			continue;
		if (sas_device_priv_data->ignore_delay_remove) {
			sdev_printk(KERN_INFO, sdev,
			"%s skip device_block for SES handle(0x%04x)\n",
			__func__, sas_device_priv_data->sas_target->handle);
			continue;
		}
		_scsih_internal_device_block(sdev, sas_device_priv_data);
	}

	if (sas_device)
		sas_device_put(sas_device);
}

/**
 * _scsih_block_io_to_children_attached_to_ex
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * attached to this expander. This function called when expander is
 * pulled.
 */
static void
_scsih_block_io_to_children_attached_to_ex(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	struct _sas_port *mpt3sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	unsigned long flags;

	if (!sas_expander)
		return;

	list_for_each_entry(mpt3sas_port,
	   &sas_expander->sas_port_list, port_list) {
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device = __mpt3sas_get_sdev_by_addr(ioc,
			    mpt3sas_port->remote_identify.sas_address);
			if (sas_device) {
				set_bit(sas_device->handle,
						ioc->blocking_handles);
				sas_device_put(sas_device);
			}
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		}
	}

	list_for_each_entry(mpt3sas_port,
	   &sas_expander->sas_port_list, port_list) {

		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE) {
			expander_sibling =
			    mpt3sas_scsih_expander_find_by_sas_address(
			    ioc, mpt3sas_port->remote_identify.sas_address);
			_scsih_block_io_to_children_attached_to_ex(ioc,
			    expander_sibling);
		}
	}
}

/**
 * _scsih_block_io_to_children_attached_directly
 * @ioc: per adapter object
 * @event_data: topology change event data
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * direct attached during device pull.
 */
static void
_scsih_block_io_to_children_attached_directly(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING)
			_scsih_block_io_device(ioc, handle);
	}
}

/**
 * _scsih_block_io_to_pcie_children_attached_directly
 * @ioc: per adapter object
 * @event_data: topology change event data
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * direct attached during device pull/reconnect.
 */
static void
_scsih_block_io_to_pcie_children_attached_directly(struct MPT3SAS_ADAPTER *ioc,
		Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle =
			le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PortEntry[i].PortStatus;
		if (reason_code ==
				MPI26_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING)
			_scsih_block_io_device(ioc, handle);
	}
}
/**
 * _scsih_tm_tr_send - send task management request
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This code is to initiate the device removal handshake protocol
 * with controller firmware.  This function will issue target reset
 * using high priority request queue.  It will send a sas iounit
 * control request (MPI2_SAS_OP_REMOVE_DEVICE) from this completion.
 *
 * This is designed to send muliple task management request at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_tr_send(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	u64 sas_address = 0;
	unsigned long flags;
	struct _tr_list *delayed_tr;
	u32 ioc_state;
	u8 tr_method = 0;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host in pci error recovery: handle(0x%04x)\n",
				    __func__, handle));
		return;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host is not operational: handle(0x%04x)\n",
				    __func__, handle));
		return;
	}

	/* if PD, then return */
	if (test_bit(handle, ioc->pd_handles))
		return;

	clear_bit(handle, ioc->pend_os_device_add);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device && sas_device->starget &&
	    sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		sas_address = sas_device->sas_address;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
		if (pcie_device && pcie_device->starget &&
			pcie_device->starget->hostdata) {
			sas_target_priv_data = pcie_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			sas_address = pcie_device->wwid;
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		if (pcie_device && (!ioc->tm_custom_handling) &&
		    (!(mpt3sas_scsih_is_pcie_scsi_device(
		    pcie_device->device_info))))
			tr_method =
			    MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
		else
			tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;
	}
	if (sas_target_priv_data) {
		dewtprintk(ioc,
			   ioc_info(ioc, "setting delete flag: handle(0x%04x), sas_addr(0x%016llx)\n",
				    handle, (u64)sas_address));
		if (sas_device) {
			if (sas_device->enclosure_handle != 0)
				dewtprintk(ioc,
					   ioc_info(ioc, "setting delete flag:enclosure logical id(0x%016llx), slot(%d)\n",
						    (u64)sas_device->enclosure_logical_id,
						    sas_device->slot));
			if (sas_device->connector_name[0] != '\0')
				dewtprintk(ioc,
					   ioc_info(ioc, "setting delete flag: enclosure level(0x%04x), connector name( %s)\n",
						    sas_device->enclosure_level,
						    sas_device->connector_name));
		} else if (pcie_device) {
			if (pcie_device->enclosure_handle != 0)
				dewtprintk(ioc,
					   ioc_info(ioc, "setting delete flag: logical id(0x%016llx), slot(%d)\n",
						    (u64)pcie_device->enclosure_logical_id,
						    pcie_device->slot));
			if (pcie_device->connector_name[0] != '\0')
				dewtprintk(ioc,
					   ioc_info(ioc, "setting delete flag:, enclosure level(0x%04x), connector name( %s)\n",
						    pcie_device->enclosure_level,
						    pcie_device->connector_name));
		}
		_scsih_ublock_io_device(ioc, sas_address);
		sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_tr_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			goto out;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_list);
		dewtprintk(ioc,
			   ioc_info(ioc, "DELAYED:tr:handle(0x%04x), (open)\n",
				    handle));
		goto out;
	}

	dewtprintk(ioc,
		   ioc_info(ioc, "tr_send:handle(0x%04x), (open), smid(%d), cb(%d)\n",
			    handle, smid, ioc->tm_tr_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpi_request->MsgFlags = tr_method;
	set_bit(handle, ioc->device_remove_in_progress);
	ioc->put_smid_hi_priority(ioc, smid, 0);
	mpt3sas_trigger_master(ioc, MASTER_TRIGGER_DEVICE_REMOVAL);

out:
	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);
}

/**
 * _scsih_tm_tr_complete -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the target reset completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocol with controller firmware.
 * It will send a sas iounit control request (MPI2_SAS_OP_REMOVE_DEVICE)
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_tm_tr_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt3sas_base_get_reply_virt_addr(ioc, reply);
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u16 smid_sas_ctrl;
	u32 ioc_state;
	struct _sc_list *delayed_sc;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host in pci error recovery\n",
				    __func__));
		return 1;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host is not operational\n",
				    __func__));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		ioc_err(ioc, "mpi_reply not valid at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 1;
	}
	mpi_request_tm = mpt3sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc,
			   ioc_err(ioc, "spurious interrupt: handle(0x%04x:0x%04x), smid(%d)!!!\n",
				   handle,
				   le16_to_cpu(mpi_reply->DevHandle), smid));
		return 0;
	}

	mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
	dewtprintk(ioc,
		   ioc_info(ioc, "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), loginfo(0x%08x), completed(%d)\n",
			    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo),
			    le32_to_cpu(mpi_reply->TerminationCount)));

	smid_sas_ctrl = mpt3sas_base_get_smid(ioc, ioc->tm_sas_control_cb_idx);
	if (!smid_sas_ctrl) {
		delayed_sc = kzalloc(sizeof(*delayed_sc), GFP_ATOMIC);
		if (!delayed_sc)
			return _scsih_check_for_pending_tm(ioc, smid);
		INIT_LIST_HEAD(&delayed_sc->list);
		delayed_sc->handle = le16_to_cpu(mpi_request_tm->DevHandle);
		list_add_tail(&delayed_sc->list, &ioc->delayed_sc_list);
		dewtprintk(ioc,
			   ioc_info(ioc, "DELAYED:sc:handle(0x%04x), (open)\n",
				    handle));
		return _scsih_check_for_pending_tm(ioc, smid);
	}

	dewtprintk(ioc,
		   ioc_info(ioc, "sc_send:handle(0x%04x), (open), smid(%d), cb(%d)\n",
			    handle, smid_sas_ctrl, ioc->tm_sas_control_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid_sas_ctrl);
	memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	mpi_request->DevHandle = mpi_request_tm->DevHandle;
	ioc->put_smid_default(ioc, smid_sas_ctrl);

	return _scsih_check_for_pending_tm(ioc, smid);
}

/** _scsih_allow_scmd_to_device - check whether scmd needs to
 *				 issue to IOC or not.
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 *
 * Returns true if scmd can be issued to IOC otherwise returns false.
 */
inline bool _scsih_allow_scmd_to_device(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd)
{

	if (ioc->pci_error_recovery)
		return false;

	if (ioc->hba_mpi_version_belonged == MPI2_VERSION) {
		if (ioc->remove_host)
			return false;

		return true;
	}

	if (ioc->remove_host) {

		switch (scmd->cmnd[0]) {
		case SYNCHRONIZE_CACHE:
		case START_STOP:
			return true;
		default:
			return false;
		}
	}

	return true;
}

/**
 * _scsih_sas_control_complete - completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the sas iounit control completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocol with controller firmware.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_sas_control_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply)
{
	Mpi2SasIoUnitControlReply_t *mpi_reply =
	    mpt3sas_base_get_reply_virt_addr(ioc, reply);

	if (likely(mpi_reply)) {
		dewtprintk(ioc,
			   ioc_info(ioc, "sc_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), loginfo(0x%08x)\n",
				    le16_to_cpu(mpi_reply->DevHandle), smid,
				    le16_to_cpu(mpi_reply->IOCStatus),
				    le32_to_cpu(mpi_reply->IOCLogInfo)));
		if (le16_to_cpu(mpi_reply->IOCStatus) ==
		     MPI2_IOCSTATUS_SUCCESS) {
			clear_bit(le16_to_cpu(mpi_reply->DevHandle),
			    ioc->device_remove_in_progress);
		}
	} else {
		ioc_err(ioc, "mpi_reply not valid at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
	}
	return mpt3sas_check_for_pending_internal_cmds(ioc, smid);
}

/**
 * _scsih_tm_tr_volume_send - send target reset request for volumes
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This is designed to send muliple task management request at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_tr_volume_send(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _tr_list *delayed_tr;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host reset in progress!\n",
				    __func__));
		return;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_tr_volume_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_volume_list);
		dewtprintk(ioc,
			   ioc_info(ioc, "DELAYED:tr:handle(0x%04x), (open)\n",
				    handle));
		return;
	}

	dewtprintk(ioc,
		   ioc_info(ioc, "tr_send:handle(0x%04x), (open), smid(%d), cb(%d)\n",
			    handle, smid, ioc->tm_tr_volume_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	ioc->put_smid_hi_priority(ioc, smid, 0);
}

/**
 * _scsih_tm_volume_tr_complete - target reset completion
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_tm_volume_tr_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt3sas_base_get_reply_virt_addr(ioc, reply);

	if (ioc->shost_recovery || ioc->pci_error_recovery) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host reset in progress!\n",
				    __func__));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		ioc_err(ioc, "mpi_reply not valid at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 1;
	}

	mpi_request_tm = mpt3sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc,
			   ioc_err(ioc, "spurious interrupt: handle(0x%04x:0x%04x), smid(%d)!!!\n",
				   handle, le16_to_cpu(mpi_reply->DevHandle),
				   smid));
		return 0;
	}

	dewtprintk(ioc,
		   ioc_info(ioc, "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), loginfo(0x%08x), completed(%d)\n",
			    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo),
			    le32_to_cpu(mpi_reply->TerminationCount)));

	return _scsih_check_for_pending_tm(ioc, smid);
}

/**
 * _scsih_issue_delayed_event_ack - issue delayed Event ACK messages
 * @ioc: per adapter object
 * @smid: system request message index
 * @event: Event ID
 * @event_context: used to track events uniquely
 *
 * Context - processed in interrupt context.
 */
static void
_scsih_issue_delayed_event_ack(struct MPT3SAS_ADAPTER *ioc, u16 smid, U16 event,
				U32 event_context)
{
	Mpi2EventAckRequest_t *ack_request;
	int i = smid - ioc->internal_smid;
	unsigned long flags;

	/* Without releasing the smid just update the
	 * call back index and reuse the same smid for
	 * processing this delayed request
	 */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	ioc->internal_lookup[i].cb_idx = ioc->base_cb_idx;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	dewtprintk(ioc,
		   ioc_info(ioc, "EVENT ACK: event(0x%04x), smid(%d), cb(%d)\n",
			    le16_to_cpu(event), smid, ioc->base_cb_idx));
	ack_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = event;
	ack_request->EventContext = event_context;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	ioc->put_smid_default(ioc, smid);
}

/**
 * _scsih_issue_delayed_sas_io_unit_ctrl - issue delayed
 *				sas_io_unit_ctrl messages
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Context - processed in interrupt context.
 */
static void
_scsih_issue_delayed_sas_io_unit_ctrl(struct MPT3SAS_ADAPTER *ioc,
					u16 smid, u16 handle)
{
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u32 ioc_state;
	int i = smid - ioc->internal_smid;
	unsigned long flags;

	if (ioc->remove_host) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host has been removed\n",
				    __func__));
		return;
	} else if (ioc->pci_error_recovery) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host in pci error recovery\n",
				    __func__));
		return;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: host is not operational\n",
				    __func__));
		return;
	}

	/* Without releasing the smid just update the
	 * call back index and reuse the same smid for
	 * processing this delayed request
	 */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	ioc->internal_lookup[i].cb_idx = ioc->tm_sas_control_cb_idx;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	dewtprintk(ioc,
		   ioc_info(ioc, "sc_send:handle(0x%04x), (open), smid(%d), cb(%d)\n",
			    handle, smid, ioc->tm_sas_control_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	mpi_request->DevHandle = cpu_to_le16(handle);
	ioc->put_smid_default(ioc, smid);
}

/**
 * _scsih_check_for_pending_internal_cmds - check for pending internal messages
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Context: Executed in interrupt context
 *
 * This will check delayed internal messages list, and process the
 * next request.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
u8
mpt3sas_check_for_pending_internal_cmds(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct _sc_list *delayed_sc;
	struct _event_ack_list *delayed_event_ack;

	if (!list_empty(&ioc->delayed_event_ack_list)) {
		delayed_event_ack = list_entry(ioc->delayed_event_ack_list.next,
						struct _event_ack_list, list);
		_scsih_issue_delayed_event_ack(ioc, smid,
		  delayed_event_ack->Event, delayed_event_ack->EventContext);
		list_del(&delayed_event_ack->list);
		kfree(delayed_event_ack);
		return 0;
	}

	if (!list_empty(&ioc->delayed_sc_list)) {
		delayed_sc = list_entry(ioc->delayed_sc_list.next,
						struct _sc_list, list);
		_scsih_issue_delayed_sas_io_unit_ctrl(ioc, smid,
						 delayed_sc->handle);
		list_del(&delayed_sc->list);
		kfree(delayed_sc);
		return 0;
	}
	return 1;
}

/**
 * _scsih_check_for_pending_tm - check for pending task management
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * This will check delayed target reset list, and feed the
 * next reqeust.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_check_for_pending_tm(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct _tr_list *delayed_tr;

	if (!list_empty(&ioc->delayed_tr_volume_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_volume_list.next,
		    struct _tr_list, list);
		mpt3sas_base_free_smid(ioc, smid);
		_scsih_tm_tr_volume_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	if (!list_empty(&ioc->delayed_tr_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_list.next,
		    struct _tr_list, list);
		mpt3sas_base_free_smid(ioc, smid);
		_scsih_tm_tr_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	return 1;
}

/**
 * _scsih_check_topo_delete_events - sanity check on topo events
 * @ioc: per adapter object
 * @event_data: the event data payload
 *
 * This routine added to better handle cable breaker.
 *
 * This handles the case where driver receives multiple expander
 * add and delete events in a single shot.  When there is a delete event
 * the routine will void any pending add events waiting in the event queue.
 */
static void
_scsih_check_topo_delete_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	struct fw_event_work *fw_event;
	Mpi2EventDataSasTopologyChangeList_t *local_event_data;
	u16 expander_handle;
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i, reason_code;
	u16 handle;

	for (i = 0 ; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING)
			_scsih_tm_tr_send(ioc, handle);
	}

	expander_handle = le16_to_cpu(event_data->ExpanderDevHandle);
	if (expander_handle < ioc->sas_hba.num_phys) {
		_scsih_block_io_to_children_attached_directly(ioc, event_data);
		return;
	}
	if (event_data->ExpStatus ==
	    MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING) {
		/* put expander attached devices into blocking state */
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt3sas_scsih_expander_find_by_handle(ioc,
		    expander_handle);
		_scsih_block_io_to_children_attached_to_ex(ioc, sas_expander);
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		do {
			handle = find_first_bit(ioc->blocking_handles,
			    ioc->facts.MaxDevHandle);
			if (handle < ioc->facts.MaxDevHandle)
				_scsih_block_io_device(ioc, handle);
		} while (test_and_clear_bit(handle, ioc->blocking_handles));
	} else if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_RESPONDING)
		_scsih_block_io_to_children_attached_directly(ioc, event_data);

	if (event_data->ExpStatus != MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING)
		return;

	/* mark ignore flag for pending events */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_for_each_entry(fw_event, &ioc->fw_event_list, list) {
		if (fw_event->event != MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST ||
		    fw_event->ignore)
			continue;
		local_event_data = (Mpi2EventDataSasTopologyChangeList_t *)
				   fw_event->event_data;
		if (local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->ExpanderDevHandle) ==
			    expander_handle) {
				dewtprintk(ioc,
					   ioc_info(ioc, "setting ignoring flag\n"));
				fw_event->ignore = 1;
			}
		}
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_check_pcie_topo_remove_events - sanity check on topo
 * events
 * @ioc: per adapter object
 * @event_data: the event data payload
 *
 * This handles the case where driver receives multiple switch
 * or device add and delete events in a single shot.  When there
 * is a delete event the routine will void any pending add
 * events waiting in the event queue.
 */
static void
_scsih_check_pcie_topo_remove_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	struct fw_event_work *fw_event;
	Mpi26EventDataPCIeTopologyChangeList_t *local_event_data;
	unsigned long flags;
	int i, reason_code;
	u16 handle, switch_handle;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle =
			le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PortEntry[i].PortStatus;
		if (reason_code == MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING)
			_scsih_tm_tr_send(ioc, handle);
	}

	switch_handle = le16_to_cpu(event_data->SwitchDevHandle);
	if (!switch_handle) {
		_scsih_block_io_to_pcie_children_attached_directly(
							ioc, event_data);
		return;
	}
    /* TODO We are not supporting cascaded PCIe Switch removal yet*/
	if ((event_data->SwitchStatus
		== MPI26_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING) ||
		(event_data->SwitchStatus ==
					MPI26_EVENT_PCIE_TOPO_SS_RESPONDING))
		_scsih_block_io_to_pcie_children_attached_directly(
							ioc, event_data);

	if (event_data->SwitchStatus != MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING)
		return;

	/* mark ignore flag for pending events */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_for_each_entry(fw_event, &ioc->fw_event_list, list) {
		if (fw_event->event != MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST ||
			fw_event->ignore)
			continue;
		local_event_data =
			(Mpi26EventDataPCIeTopologyChangeList_t *)
			fw_event->event_data;
		if (local_event_data->SwitchStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->SwitchStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->SwitchDevHandle) ==
				switch_handle) {
				dewtprintk(ioc,
					   ioc_info(ioc, "setting ignoring flag for switch event\n"));
				fw_event->ignore = 1;
			}
		}
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_set_volume_delete_flag - setting volume delete flag
 * @ioc: per adapter object
 * @handle: device handle
 *
 * This returns nothing.
 */
static void
_scsih_set_volume_delete_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
	if (raid_device && raid_device->starget &&
	    raid_device->starget->hostdata) {
		sas_target_priv_data =
		    raid_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		dewtprintk(ioc,
			   ioc_info(ioc, "setting delete flag: handle(0x%04x), wwid(0x%016llx)\n",
				    handle, (u64)raid_device->wwid));
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_set_volume_handle_for_tr - set handle for target reset to volume
 * @handle: input handle
 * @a: handle for volume a
 * @b: handle for volume b
 *
 * IR firmware only supports two raid volumes.  The purpose of this
 * routine is to set the volume handle in either a or b. When the given
 * input handle is non-zero, or when a and b have not been set before.
 */
static void
_scsih_set_volume_handle_for_tr(u16 handle, u16 *a, u16 *b)
{
	if (!handle || handle == *a || handle == *b)
		return;
	if (!*a)
		*a = handle;
	else if (!*b)
		*b = handle;
}

/**
 * _scsih_check_ir_config_unhide_events - check for UNHIDE events
 * @ioc: per adapter object
 * @event_data: the event data payload
 * Context: interrupt time.
 *
 * This routine will send target reset to volume, followed by target
 * resets to the PDs. This is called when a PD has been removed, or
 * volume has been deleted or removed. When the target reset is sent
 * to volume, the PD target resets need to be queued to start upon
 * completion of the volume target reset.
 */
static void
_scsih_check_ir_config_unhide_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u16 handle, volume_handle, a, b;
	struct _tr_list *delayed_tr;

	a = 0;
	b = 0;

	if (ioc->is_warpdrive)
		return;

	/* Volume Resets for Deleted or Removed */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (le32_to_cpu(event_data->Flags) &
		    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG)
			continue;
		if (element->ReasonCode ==
		    MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED ||
		    element->ReasonCode ==
		    MPI2_EVENT_IR_CHANGE_RC_REMOVED) {
			volume_handle = le16_to_cpu(element->VolDevHandle);
			_scsih_set_volume_delete_flag(ioc, volume_handle);
			_scsih_set_volume_handle_for_tr(volume_handle, &a, &b);
		}
	}

	/* Volume Resets for UNHIDE events */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (le32_to_cpu(event_data->Flags) &
		    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG)
			continue;
		if (element->ReasonCode == MPI2_EVENT_IR_CHANGE_RC_UNHIDE) {
			volume_handle = le16_to_cpu(element->VolDevHandle);
			_scsih_set_volume_handle_for_tr(volume_handle, &a, &b);
		}
	}

	if (a)
		_scsih_tm_tr_volume_send(ioc, a);
	if (b)
		_scsih_tm_tr_volume_send(ioc, b);

	/* PD target resets */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (element->ReasonCode != MPI2_EVENT_IR_CHANGE_RC_UNHIDE)
			continue;
		handle = le16_to_cpu(element->PhysDiskDevHandle);
		volume_handle = le16_to_cpu(element->VolDevHandle);
		clear_bit(handle, ioc->pd_handles);
		if (!volume_handle)
			_scsih_tm_tr_send(ioc, handle);
		else if (volume_handle == a || volume_handle == b) {
			delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
			BUG_ON(!delayed_tr);
			INIT_LIST_HEAD(&delayed_tr->list);
			delayed_tr->handle = handle;
			list_add_tail(&delayed_tr->list, &ioc->delayed_tr_list);
			dewtprintk(ioc,
				   ioc_info(ioc, "DELAYED:tr:handle(0x%04x), (open)\n",
					    handle));
		} else
			_scsih_tm_tr_send(ioc, handle);
	}
}


/**
 * _scsih_check_volume_delete_events - set delete flag for volumes
 * @ioc: per adapter object
 * @event_data: the event data payload
 * Context: interrupt time.
 *
 * This will handle the case when the cable connected to entire volume is
 * pulled. We will take care of setting the deleted flag so normal IO will
 * not be sent.
 */
static void
_scsih_check_volume_delete_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrVolume_t *event_data)
{
	u32 state;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;
	state = le32_to_cpu(event_data->NewValue);
	if (state == MPI2_RAID_VOL_STATE_MISSING || state ==
	    MPI2_RAID_VOL_STATE_FAILED)
		_scsih_set_volume_delete_flag(ioc,
		    le16_to_cpu(event_data->VolDevHandle));
}

/**
 * _scsih_temp_threshold_events - display temperature threshold exceeded events
 * @ioc: per adapter object
 * @event_data: the temp threshold event data
 * Context: interrupt time.
 */
static void
_scsih_temp_threshold_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataTemperature_t *event_data)
{
	u32 doorbell;
	if (ioc->temp_sensors_count >= event_data->SensorNum) {
		ioc_err(ioc, "Temperature Threshold flags %s%s%s%s exceeded for Sensor: %d !!!\n",
			le16_to_cpu(event_data->Status) & 0x1 ? "0 " : " ",
			le16_to_cpu(event_data->Status) & 0x2 ? "1 " : " ",
			le16_to_cpu(event_data->Status) & 0x4 ? "2 " : " ",
			le16_to_cpu(event_data->Status) & 0x8 ? "3 " : " ",
			event_data->SensorNum);
		ioc_err(ioc, "Current Temp In Celsius: %d\n",
			event_data->CurrentTemperature);
		if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
			doorbell = mpt3sas_base_get_iocstate(ioc, 0);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpt3sas_print_fault_code(ioc,
				    doorbell & MPI2_DOORBELL_DATA_MASK);
			} else if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_COREDUMP) {
				mpt3sas_print_coredump_info(ioc,
				    doorbell & MPI2_DOORBELL_DATA_MASK);
			}
		}
	}
}

static int _scsih_set_satl_pending(struct scsi_cmnd *scmd, bool pending)
{
	struct MPT3SAS_DEVICE *priv = scmd->device->hostdata;

	if (scmd->cmnd[0] != ATA_12 && scmd->cmnd[0] != ATA_16)
		return 0;

	if (pending)
		return test_and_set_bit(0, &priv->ata_command_pending);

	clear_bit(0, &priv->ata_command_pending);
	return 0;
}

/**
 * _scsih_flush_running_cmds - completing outstanding commands.
 * @ioc: per adapter object
 *
 * The flushing out of all pending scmd commands following host reset,
 * where all IO is dropped to the floor.
 */
static void
_scsih_flush_running_cmds(struct MPT3SAS_ADAPTER *ioc)
{
	struct scsi_cmnd *scmd;
	struct scsiio_tracker *st;
	u16 smid;
	int count = 0;

	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		count++;
		_scsih_set_satl_pending(scmd, false);
		st = scsi_cmd_priv(scmd);
		mpt3sas_base_clear_st(ioc, st);
		scsi_dma_unmap(scmd);
		if (ioc->pci_error_recovery || ioc->remove_host)
			scmd->result = DID_NO_CONNECT << 16;
		else
			scmd->result = DID_RESET << 16;
		scmd->scsi_done(scmd);
	}
	dtmprintk(ioc, ioc_info(ioc, "completing %d cmds\n", count));
}

/**
 * _scsih_setup_eedp - setup MPI request for EEDP transfer
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_request: pointer to the SCSI_IO request message frame
 *
 * Supporting protection 1 and 3.
 */
static void
_scsih_setup_eedp(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	Mpi25SCSIIORequest_t *mpi_request)
{
	u16 eedp_flags;
	unsigned char prot_op = scsi_get_prot_op(scmd);
	unsigned char prot_type = scsi_get_prot_type(scmd);
	Mpi25SCSIIORequest_t *mpi_request_3v =
	   (Mpi25SCSIIORequest_t *)mpi_request;

	if (prot_type == SCSI_PROT_DIF_TYPE0 || prot_op == SCSI_PROT_NORMAL)
		return;

	if (prot_op ==  SCSI_PROT_READ_STRIP)
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
	else if (prot_op ==  SCSI_PROT_WRITE_INSERT)
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
	else
		return;

	switch (prot_type) {
	case SCSI_PROT_DIF_TYPE1:
	case SCSI_PROT_DIF_TYPE2:

		/*
		* enable ref/guard checking
		* auto increment ref tag
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		mpi_request->CDB.EEDP32.PrimaryReferenceTag =
		    cpu_to_be32(t10_pi_ref_tag(scmd->request));
		break;

	case SCSI_PROT_DIF_TYPE3:

		/*
		* enable guard checking
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;

		break;
	}

	mpi_request_3v->EEDPBlockSize =
	    cpu_to_le16(scmd->device->sector_size);

	if (ioc->is_gen35_ioc)
		eedp_flags |= MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE;
	mpi_request->EEDPFlags = cpu_to_le16(eedp_flags);
}

/**
 * _scsih_eedp_error_handling - return sense code for EEDP errors
 * @scmd: pointer to scsi command object
 * @ioc_status: ioc status
 */
static void
_scsih_eedp_error_handling(struct scsi_cmnd *scmd, u16 ioc_status)
{
	u8 ascq;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		ascq = 0x01;
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		ascq = 0x02;
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		ascq = 0x03;
		break;
	default:
		ascq = 0x00;
		break;
	}
	scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST, 0x10,
	    ascq);
	scmd->result = DRIVER_SENSE << 24 | (DID_ABORT << 16) |
	    SAM_STAT_CHECK_CONDITION;
}

/**
 * scsih_qcmd - main scsi request entry point
 * @shost: SCSI host pointer
 * @scmd: pointer to scsi command object
 *
 * The callback index is set inside `ioc->scsi_io_cb_idx`.
 *
 * Return: 0 on success.  If there's a failure, return either:
 * SCSI_MLQUEUE_DEVICE_BUSY if the device queue is full, or
 * SCSI_MLQUEUE_HOST_BUSY if the entire host queue is full
 */
static int
scsih_qcmd(struct Scsi_Host *shost, struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _raid_device *raid_device;
	struct request *rq = scmd->request;
	int class;
	Mpi25SCSIIORequest_t *mpi_request;
	struct _pcie_device *pcie_device = NULL;
	u32 mpi_control;
	u16 smid;
	u16 handle;

	if (ioc->logging_level & MPT_DEBUG_SCSI)
		scsi_print_command(scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	if (!(_scsih_allow_scmd_to_device(ioc, scmd))) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	sas_target_priv_data = sas_device_priv_data->sas_target;

	/* invalid device handle */
	handle = sas_target_priv_data->handle;
	if (handle == MPT3SAS_INVALID_DEVICE_HANDLE) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}


	if (ioc->shost_recovery || ioc->ioc_link_reset_in_progress) {
		/* host recovery or link resets sent via IOCTLs */
		return SCSI_MLQUEUE_HOST_BUSY;
	} else if (sas_target_priv_data->deleted) {
		/* device has been deleted */
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	} else if (sas_target_priv_data->tm_busy ||
		   sas_device_priv_data->block) {
		/* device busy with task management */
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	/*
	 * Bug work around for firmware SATL handling.  The loop
	 * is based on atomic operations and ensures consistency
	 * since we're lockless at this point
	 */
	do {
		if (test_bit(0, &sas_device_priv_data->ata_command_pending))
			return SCSI_MLQUEUE_DEVICE_BUSY;
	} while (_scsih_set_satl_pending(scmd, true));

	if (scmd->sc_data_direction == DMA_FROM_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
	else if (scmd->sc_data_direction == DMA_TO_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
	else
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;

	/* set tags */
	mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
	/* NCQ Prio supported, make sure control indicated high priority */
	if (sas_device_priv_data->ncq_prio_enable) {
		class = IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
		if (class == IOPRIO_CLASS_RT)
			mpi_control |= 1 << MPI2_SCSIIO_CONTROL_CMDPRI_SHIFT;
	}
	/* Make sure Device is not raid volume.
	 * We do not expose raid functionality to upper layer for warpdrive.
	 */
	if (((!ioc->is_warpdrive && !scsih_is_raid(&scmd->device->sdev_gendev))
		&& !scsih_is_nvme(&scmd->device->sdev_gendev))
		&& sas_is_tlr_enabled(scmd->device) && scmd->cmd_len != 32)
		mpi_control |= MPI2_SCSIIO_CONTROL_TLR_ON;

	smid = mpt3sas_base_get_smid_scsiio(ioc, ioc->scsi_io_cb_idx, scmd);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		_scsih_set_satl_pending(scmd, false);
		goto out;
	}
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, ioc->request_sz);
	_scsih_setup_eedp(ioc, scmd, mpi_request);

	if (scmd->cmd_len == 32)
		mpi_control |= 4 << MPI2_SCSIIO_CONTROL_ADDCDBLEN_SHIFT;
	mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT)
		mpi_request->Function = MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	else
		mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	mpi_request->Control = cpu_to_le32(mpi_control);
	mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi25SCSIIORequest_t, SGL) / 4;
	int_to_scsilun(sas_device_priv_data->lun, (struct scsi_lun *)
	    mpi_request->LUN);
	memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);

	if (mpi_request->DataLength) {
		pcie_device = sas_target_priv_data->pcie_dev;
		if (ioc->build_sg_scmd(ioc, scmd, smid, pcie_device)) {
			mpt3sas_base_free_smid(ioc, smid);
			_scsih_set_satl_pending(scmd, false);
			goto out;
		}
	} else
		ioc->build_zero_len_sge(ioc, &mpi_request->SGL);

	raid_device = sas_target_priv_data->raid_device;
	if (raid_device && raid_device->direct_io_enabled)
		mpt3sas_setup_direct_io(ioc, scmd,
			raid_device, mpi_request);

	if (likely(mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST)) {
		if (sas_target_priv_data->flags & MPT_TARGET_FASTPATH_IO) {
			mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len |
			    MPI25_SCSIIO_IOFLAGS_FAST_PATH);
			ioc->put_smid_fast_path(ioc, smid, handle);
		} else
			ioc->put_smid_scsi_io(ioc, smid,
			    le16_to_cpu(mpi_request->DevHandle));
	} else
		ioc->put_smid_default(ioc, smid);
	return 0;

 out:
	return SCSI_MLQUEUE_HOST_BUSY;
}

/**
 * _scsih_normalize_sense - normalize descriptor and fixed format sense data
 * @sense_buffer: sense data returned by target
 * @data: normalized skey/asc/ascq
 */
static void
_scsih_normalize_sense(char *sense_buffer, struct sense_info *data)
{
	if ((sense_buffer[0] & 0x7F) >= 0x72) {
		/* descriptor format */
		data->skey = sense_buffer[1] & 0x0F;
		data->asc = sense_buffer[2];
		data->ascq = sense_buffer[3];
	} else {
		/* fixed format */
		data->skey = sense_buffer[2] & 0x0F;
		data->asc = sense_buffer[12];
		data->ascq = sense_buffer[13];
	}
}

/**
 * _scsih_scsi_ioc_info - translated non-succesfull SCSI_IO request
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_reply: reply mf payload returned from firmware
 * @smid: ?
 *
 * scsi_status - SCSI Status code returned from target device
 * scsi_state - state info associated with SCSI_IO determined by ioc
 * ioc_status - ioc supplied status info
 */
static void
_scsih_scsi_ioc_info(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	Mpi2SCSIIOReply_t *mpi_reply, u16 smid)
{
	u32 response_info;
	u8 *response_bytes;
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	u8 scsi_state = mpi_reply->SCSIState;
	u8 scsi_status = mpi_reply->SCSIStatus;
	char *desc_ioc_state = NULL;
	char *desc_scsi_status = NULL;
	char *desc_scsi_state = ioc->tmp_string;
	u32 log_info = le32_to_cpu(mpi_reply->IOCLogInfo);
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *priv_target = starget->hostdata;
	char *device_str = NULL;

	if (!priv_target)
		return;
	if (ioc->hide_ir_msg)
		device_str = "WarpDrive";
	else
		device_str = "volume";

	if (log_info == 0x31170000)
		return;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_SUCCESS:
		desc_ioc_state = "success";
		break;
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc_ioc_state = "invalid function";
		break;
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
		desc_ioc_state = "scsi recovered error";
		break;
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
		desc_ioc_state = "scsi invalid dev handle";
		break;
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		desc_ioc_state = "scsi device not there";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		desc_ioc_state = "scsi data overrun";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		desc_ioc_state = "scsi data underrun";
		break;
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
		desc_ioc_state = "scsi io data error";
		break;
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
		desc_ioc_state = "scsi protocol error";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
		desc_ioc_state = "scsi task terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		desc_ioc_state = "scsi residual mismatch";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
		desc_ioc_state = "scsi task mgmt failed";
		break;
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		desc_ioc_state = "scsi ioc terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		desc_ioc_state = "scsi ext terminated";
		break;
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		desc_ioc_state = "eedp guard error";
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		desc_ioc_state = "eedp ref tag error";
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		desc_ioc_state = "eedp app tag error";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
		desc_ioc_state = "insufficient power";
		break;
	default:
		desc_ioc_state = "unknown";
		break;
	}

	switch (scsi_status) {
	case MPI2_SCSI_STATUS_GOOD:
		desc_scsi_status = "good";
		break;
	case MPI2_SCSI_STATUS_CHECK_CONDITION:
		desc_scsi_status = "check condition";
		break;
	case MPI2_SCSI_STATUS_CONDITION_MET:
		desc_scsi_status = "condition met";
		break;
	case MPI2_SCSI_STATUS_BUSY:
		desc_scsi_status = "busy";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE:
		desc_scsi_status = "intermediate";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE_CONDMET:
		desc_scsi_status = "intermediate condmet";
		break;
	case MPI2_SCSI_STATUS_RESERVATION_CONFLICT:
		desc_scsi_status = "reservation conflict";
		break;
	case MPI2_SCSI_STATUS_COMMAND_TERMINATED:
		desc_scsi_status = "command terminated";
		break;
	case MPI2_SCSI_STATUS_TASK_SET_FULL:
		desc_scsi_status = "task set full";
		break;
	case MPI2_SCSI_STATUS_ACA_ACTIVE:
		desc_scsi_status = "aca active";
		break;
	case MPI2_SCSI_STATUS_TASK_ABORTED:
		desc_scsi_status = "task aborted";
		break;
	default:
		desc_scsi_status = "unknown";
		break;
	}

	desc_scsi_state[0] = '\0';
	if (!scsi_state)
		desc_scsi_state = " ";
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		strcat(desc_scsi_state, "response info ");
	if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
		strcat(desc_scsi_state, "state terminated ");
	if (scsi_state & MPI2_SCSI_STATE_NO_SCSI_STATUS)
		strcat(desc_scsi_state, "no status ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_FAILED)
		strcat(desc_scsi_state, "autosense failed ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID)
		strcat(desc_scsi_state, "autosense valid ");

	scsi_print_command(scmd);

	if (priv_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		ioc_warn(ioc, "\t%s wwid(0x%016llx)\n",
			 device_str, (u64)priv_target->sas_address);
	} else if (priv_target->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		pcie_device = mpt3sas_get_pdev_from_target(ioc, priv_target);
		if (pcie_device) {
			ioc_info(ioc, "\twwid(0x%016llx), port(%d)\n",
				 (u64)pcie_device->wwid, pcie_device->port_num);
			if (pcie_device->enclosure_handle != 0)
				ioc_info(ioc, "\tenclosure logical id(0x%016llx), slot(%d)\n",
					 (u64)pcie_device->enclosure_logical_id,
					 pcie_device->slot);
			if (pcie_device->connector_name[0])
				ioc_info(ioc, "\tenclosure level(0x%04x), connector name( %s)\n",
					 pcie_device->enclosure_level,
					 pcie_device->connector_name);
			pcie_device_put(pcie_device);
		}
	} else {
		sas_device = mpt3sas_get_sdev_from_target(ioc, priv_target);
		if (sas_device) {
			ioc_warn(ioc, "\tsas_address(0x%016llx), phy(%d)\n",
				 (u64)sas_device->sas_address, sas_device->phy);

			_scsih_display_enclosure_chassis_info(ioc, sas_device,
			    NULL, NULL);

			sas_device_put(sas_device);
		}
	}

	ioc_warn(ioc, "\thandle(0x%04x), ioc_status(%s)(0x%04x), smid(%d)\n",
		 le16_to_cpu(mpi_reply->DevHandle),
		 desc_ioc_state, ioc_status, smid);
	ioc_warn(ioc, "\trequest_len(%d), underflow(%d), resid(%d)\n",
		 scsi_bufflen(scmd), scmd->underflow, scsi_get_resid(scmd));
	ioc_warn(ioc, "\ttag(%d), transfer_count(%d), sc->result(0x%08x)\n",
		 le16_to_cpu(mpi_reply->TaskTag),
		 le32_to_cpu(mpi_reply->TransferCount), scmd->result);
	ioc_warn(ioc, "\tscsi_status(%s)(0x%02x), scsi_state(%s)(0x%02x)\n",
		 desc_scsi_status, scsi_status, desc_scsi_state, scsi_state);

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		ioc_warn(ioc, "\t[sense_key,asc,ascq]: [0x%02x,0x%02x,0x%02x], count(%d)\n",
			 data.skey, data.asc, data.ascq,
			 le32_to_cpu(mpi_reply->SenseCount));
	}
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
		response_info = le32_to_cpu(mpi_reply->ResponseInfo);
		response_bytes = (u8 *)&response_info;
		_scsih_response_code(ioc, response_bytes[0]);
	}
}

/**
 * _scsih_turn_on_pfa_led - illuminate PFA LED
 * @ioc: per adapter object
 * @handle: device handle
 * Context: process
 */
static void
_scsih_turn_on_pfa_led(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;
	struct _sas_device *sas_device;

	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
	if (!sas_device)
		return;

	memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
	mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
	mpi_request.SlotStatus =
	    cpu_to_le32(MPI2_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT);
	mpi_request.DevHandle = cpu_to_le16(handle);
	mpi_request.Flags = MPI2_SEP_REQ_FLAGS_DEVHANDLE_ADDRESS;
	if ((mpt3sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
	    &mpi_request)) != 0) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}
	sas_device->pfa_led_on = 1;

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
		dewtprintk(ioc,
			   ioc_info(ioc, "enclosure_processor: ioc_status (0x%04x), loginfo(0x%08x)\n",
				    le16_to_cpu(mpi_reply.IOCStatus),
				    le32_to_cpu(mpi_reply.IOCLogInfo)));
		goto out;
	}
out:
	sas_device_put(sas_device);
}

/**
 * _scsih_turn_off_pfa_led - turn off Fault LED
 * @ioc: per adapter object
 * @sas_device: sas device whose PFA LED has to turned off
 * Context: process
 */
static void
_scsih_turn_off_pfa_led(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;

	memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
	mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
	mpi_request.SlotStatus = 0;
	mpi_request.Slot = cpu_to_le16(sas_device->slot);
	mpi_request.DevHandle = 0;
	mpi_request.EnclosureHandle = cpu_to_le16(sas_device->enclosure_handle);
	mpi_request.Flags = MPI2_SEP_REQ_FLAGS_ENCLOSURE_SLOT_ADDRESS;
	if ((mpt3sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
		&mpi_request)) != 0) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
		dewtprintk(ioc,
			   ioc_info(ioc, "enclosure_processor: ioc_status (0x%04x), loginfo(0x%08x)\n",
				    le16_to_cpu(mpi_reply.IOCStatus),
				    le32_to_cpu(mpi_reply.IOCLogInfo)));
		return;
	}
}

/**
 * _scsih_send_event_to_turn_on_pfa_led - fire delayed event
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 */
static void
_scsih_send_event_to_turn_on_pfa_led(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct fw_event_work *fw_event;

	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_TURN_ON_PFA_LED;
	fw_event->device_handle = handle;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * _scsih_smart_predicted_fault - process smart errors
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 */
static void
_scsih_smart_predicted_fault(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	Mpi2EventNotificationReply_t *event_reply;
	Mpi2EventDataSasDeviceStatusChange_t *event_data;
	struct _sas_device *sas_device;
	ssize_t sz;
	unsigned long flags;

	/* only handle non-raid devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (!sas_device)
		goto out_unlock;

	starget = sas_device->starget;
	sas_target_priv_data = starget->hostdata;

	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) ||
	   ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)))
		goto out_unlock;

	_scsih_display_enclosure_chassis_info(NULL, sas_device, NULL, starget);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM)
		_scsih_send_event_to_turn_on_pfa_led(ioc, handle);

	/* insert into event log */
	sz = offsetof(Mpi2EventNotificationReply_t, EventData) +
	     sizeof(Mpi2EventDataSasDeviceStatusChange_t);
	event_reply = kzalloc(sz, GFP_ATOMIC);
	if (!event_reply) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}

	event_reply->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	event_reply->Event =
	    cpu_to_le16(MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	event_reply->MsgLength = sz/4;
	event_reply->EventDataLength =
	    cpu_to_le16(sizeof(Mpi2EventDataSasDeviceStatusChange_t)/4);
	event_data = (Mpi2EventDataSasDeviceStatusChange_t *)
	    event_reply->EventData;
	event_data->ReasonCode = MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA;
	event_data->ASC = 0x5D;
	event_data->DevHandle = cpu_to_le16(handle);
	event_data->SASAddress = cpu_to_le64(sas_target_priv_data->sas_address);
	mpt3sas_ctl_add_to_event_log(ioc, event_reply);
	kfree(event_reply);
out:
	if (sas_device)
		sas_device_put(sas_device);
	return;

out_unlock:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	goto out;
}

/**
 * _scsih_io_done - scsi request callback
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when using _scsih_qcmd.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_io_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	Mpi25SCSIIORequest_t *mpi_request;
	Mpi2SCSIIOReply_t *mpi_reply;
	struct scsi_cmnd *scmd;
	struct scsiio_tracker *st;
	u16 ioc_status;
	u32 xfer_cnt;
	u8 scsi_state;
	u8 scsi_status;
	u32 log_info;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u32 response_code = 0;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);

	scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
	if (scmd == NULL)
		return 1;

	_scsih_set_satl_pending(scmd, false);

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	if (mpi_reply == NULL) {
		scmd->result = DID_OK << 16;
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	     sas_device_priv_data->sas_target->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);

	/*
	 * WARPDRIVE: If direct_io is set then it is directIO,
	 * the failed direct I/O should be redirected to volume
	 */
	st = scsi_cmd_priv(scmd);
	if (st->direct_io &&
	     ((ioc_status & MPI2_IOCSTATUS_MASK)
	      != MPI2_IOCSTATUS_SCSI_TASK_TERMINATED)) {
		st->direct_io = 0;
		st->scmd = scmd;
		memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);
		mpi_request->DevHandle =
		    cpu_to_le16(sas_device_priv_data->sas_target->handle);
		ioc->put_smid_scsi_io(ioc, smid,
		    sas_device_priv_data->sas_target->handle);
		return 0;
	}
	/* turning off TLR */
	scsi_state = mpi_reply->SCSIState;
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		response_code =
		    le32_to_cpu(mpi_reply->ResponseInfo) & 0xFF;
	if (!sas_device_priv_data->tlr_snoop_check) {
		sas_device_priv_data->tlr_snoop_check++;
		if ((!ioc->is_warpdrive &&
		    !scsih_is_raid(&scmd->device->sdev_gendev) &&
		    !scsih_is_nvme(&scmd->device->sdev_gendev))
		    && sas_is_tlr_enabled(scmd->device) &&
		    response_code == MPI2_SCSITASKMGMT_RSP_INVALID_FRAME) {
			sas_disable_tlr(scmd->device);
			sdev_printk(KERN_INFO, scmd->device, "TLR disabled\n");
		}
	}

	xfer_cnt = le32_to_cpu(mpi_reply->TransferCount);
	scsi_set_resid(scmd, scsi_bufflen(scmd) - xfer_cnt);
	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
		log_info =  le32_to_cpu(mpi_reply->IOCLogInfo);
	else
		log_info = 0;
	ioc_status &= MPI2_IOCSTATUS_MASK;
	scsi_status = mpi_reply->SCSIStatus;

	if (ioc_status == MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN && xfer_cnt == 0 &&
	    (scsi_status == MPI2_SCSI_STATUS_BUSY ||
	     scsi_status == MPI2_SCSI_STATUS_RESERVATION_CONFLICT ||
	     scsi_status == MPI2_SCSI_STATUS_TASK_SET_FULL)) {
		ioc_status = MPI2_IOCSTATUS_SUCCESS;
	}

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		const void *sense_data = mpt3sas_base_get_sense_buffer(ioc,
		    smid);
		u32 sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
		    le32_to_cpu(mpi_reply->SenseCount));
		memcpy(scmd->sense_buffer, sense_data, sz);
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		/* failure prediction threshold exceeded */
		if (data.asc == 0x5D)
			_scsih_smart_predicted_fault(ioc,
			    le16_to_cpu(mpi_reply->DevHandle));
		mpt3sas_trigger_scsi(ioc, data.skey, data.asc, data.ascq);

		if ((ioc->logging_level & MPT_DEBUG_REPLY) &&
		     ((scmd->sense_buffer[2] == UNIT_ATTENTION) ||
		     (scmd->sense_buffer[2] == MEDIUM_ERROR) ||
		     (scmd->sense_buffer[2] == HARDWARE_ERROR)))
			_scsih_scsi_ioc_info(ioc, scmd, mpi_reply, smid);
	}
	switch (ioc_status) {
	case MPI2_IOCSTATUS_BUSY:
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		scmd->result = SAM_STAT_BUSY;
		break;

	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		scmd->result = DID_NO_CONNECT << 16;
		break;

	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		if (sas_device_priv_data->block) {
			scmd->result = DID_TRANSPORT_DISRUPTED << 16;
			goto out;
		}
		if (log_info == 0x31110630) {
			if (scmd->retries > 2) {
				scmd->result = DID_NO_CONNECT << 16;
				scsi_device_set_state(scmd->device,
				    SDEV_OFFLINE);
			} else {
				scmd->result = DID_SOFT_ERROR << 16;
				scmd->device->expecting_cc_ua = 1;
			}
			break;
		} else if (log_info == VIRTUAL_IO_FAILED_RETRY) {
			scmd->result = DID_RESET << 16;
			break;
		} else if ((scmd->device->channel == RAID_CHANNEL) &&
		   (scsi_state == (MPI2_SCSI_STATE_TERMINATED |
		   MPI2_SCSI_STATE_NO_SCSI_STATUS))) {
			scmd->result = DID_RESET << 16;
			break;
		}
		scmd->result = DID_SOFT_ERROR << 16;
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		scmd->result = DID_RESET << 16;
		break;

	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		if ((xfer_cnt == 0) || (scmd->underflow > xfer_cnt))
			scmd->result = DID_SOFT_ERROR << 16;
		else
			scmd->result = (DID_OK << 16) | scsi_status;
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		scmd->result = (DID_OK << 16) | scsi_status;

		if ((scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID))
			break;

		if (xfer_cnt < scmd->underflow) {
			if (scsi_status == SAM_STAT_BUSY)
				scmd->result = SAM_STAT_BUSY;
			else
				scmd->result = DID_SOFT_ERROR << 16;
		} else if (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		else if (!xfer_cnt && scmd->cmnd[0] == REPORT_LUNS) {
			mpi_reply->SCSIState = MPI2_SCSI_STATE_AUTOSENSE_VALID;
			mpi_reply->SCSIStatus = SAM_STAT_CHECK_CONDITION;
			scmd->result = (DRIVER_SENSE << 24) |
			    SAM_STAT_CHECK_CONDITION;
			scmd->sense_buffer[0] = 0x70;
			scmd->sense_buffer[2] = ILLEGAL_REQUEST;
			scmd->sense_buffer[12] = 0x20;
			scmd->sense_buffer[13] = 0;
		}
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		scsi_set_resid(scmd, 0);
		/* fall through */
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SUCCESS:
		scmd->result = (DID_OK << 16) | scsi_status;
		if (response_code ==
		    MPI2_SCSITASKMGMT_RSP_INVALID_FRAME ||
		    (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS)))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		break;

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		_scsih_eedp_error_handling(scmd, ioc_status);
		break;

	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
	case MPI2_IOCSTATUS_INVALID_SGL:
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FIELD:
	case MPI2_IOCSTATUS_INVALID_STATE:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
	default:
		scmd->result = DID_SOFT_ERROR << 16;
		break;

	}

	if (scmd->result && (ioc->logging_level & MPT_DEBUG_REPLY))
		_scsih_scsi_ioc_info(ioc , scmd, mpi_reply, smid);

 out:

	scsi_dma_unmap(scmd);
	mpt3sas_base_free_smid(ioc, smid);
	scmd->scsi_done(scmd);
	return 0;
}

/**
 * _scsih_sas_host_refresh - refreshing sas host object contents
 * @ioc: per adapter object
 * Context: user
 *
 * During port enable, fw will send topology events for every device. Its
 * possible that the handles may change from the previous setting, so this
 * code keeping handles updating if changed.
 */
static void
_scsih_sas_host_refresh(struct MPT3SAS_ADAPTER *ioc)
{
	u16 sz;
	u16 ioc_status;
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;
	u8 link_rate;

	dtmprintk(ioc,
		  ioc_info(ioc, "updating handles for sas_host(0x%016llx)\n",
			   (u64)ioc->sas_hba.sas_address));

	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz)) != 0)
		goto out;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto out;
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		link_rate = sas_iounit_pg0->PhyData[i].NegotiatedLinkRate >> 4;
		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		attached_handle = le16_to_cpu(sas_iounit_pg0->PhyData[i].
		    AttachedDevHandle);
		if (attached_handle && link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
			link_rate = MPI2_SAS_NEG_LINK_RATE_1_5;
		mpt3sas_transport_update_links(ioc, ioc->sas_hba.sas_address,
		    attached_handle, i, link_rate);
	}
 out:
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_sas_host_add - create sas host object
 * @ioc: per adapter object
 *
 * Creating host side data object, stored in ioc->sas_hba
 */
static void
_scsih_sas_host_add(struct MPT3SAS_ADAPTER *ioc)
{
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2SasPhyPage0_t phy_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	u16 ioc_status;
	u16 sz;
	u8 device_missing_delay;
	u8 num_phys;

	mpt3sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}
	ioc->sas_hba.phy = kcalloc(num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!ioc->sas_hba.phy) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}
	ioc->sas_hba.num_phys = num_phys;

	/* sas_iounit page 0 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}
	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz))) {
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

	/* sas_iounit page 1 */
	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
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

	ioc->io_missing_delay =
	    sas_iounit_pg1->IODeviceMissingDelay;
	device_missing_delay =
	    sas_iounit_pg1->ReportDeviceMissingDelay;
	if (device_missing_delay & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		ioc->device_missing_delay = (device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		ioc->device_missing_delay = device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

	ioc->sas_hba.parent_dev = &ioc->shost->shost_gendev;
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		if ((mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
		    i))) {
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

		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		ioc->sas_hba.phy[i].phy_id = i;
		mpt3sas_transport_add_host_phy(ioc, &ioc->sas_hba.phy[i],
		    phy_pg0, ioc->sas_hba.parent_dev);
	}
	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, ioc->sas_hba.handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out;
	}
	ioc->sas_hba.enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	ioc->sas_hba.sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	ioc_info(ioc, "host_add: handle(0x%04x), sas_addr(0x%016llx), phys(%d)\n",
		 ioc->sas_hba.handle,
		 (u64)ioc->sas_hba.sas_address,
		 ioc->sas_hba.num_phys);

	if (ioc->sas_hba.enclosure_handle) {
		if (!(mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
		    &enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
		   ioc->sas_hba.enclosure_handle)))
			ioc->sas_hba.enclosure_logical_id =
			    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
	}

 out:
	kfree(sas_iounit_pg1);
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_expander_add -  creating expander object
 * @ioc: per adapter object
 * @handle: expander handle
 *
 * Creating expander object, stored in ioc->sas_expander_list.
 *
 * Return: 0 for success, else error.
 */
static int
_scsih_expander_add(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander;
	struct _enclosure_node *enclosure_dev;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ExpanderPage1_t expander_pg1;
	u32 ioc_status;
	u16 parent_handle;
	u64 sas_address, sas_address_parent = 0;
	int i;
	unsigned long flags;
	struct _sas_port *mpt3sas_port = NULL;

	int rc = 0;

	if (!handle)
		return -1;

	if (ioc->shost_recovery || ioc->pci_error_recovery)
		return -1;

	if ((mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_HNDL, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}

	/* handle out of order topology events */
	parent_handle = le16_to_cpu(expander_pg0.ParentDevHandle);
	if (_scsih_get_sas_address(ioc, parent_handle, &sas_address_parent)
	    != 0) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}
	if (sas_address_parent != ioc->sas_hba.sas_address) {
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt3sas_scsih_expander_find_by_sas_address(ioc,
		    sas_address_parent);
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (!sas_expander) {
			rc = _scsih_expander_add(ioc, parent_handle);
			if (rc != 0)
				return rc;
		}
	}

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_address = le64_to_cpu(expander_pg0.SASAddress);
	sas_expander = mpt3sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	if (sas_expander)
		return 0;

	sas_expander = kzalloc(sizeof(struct _sas_node),
	    GFP_KERNEL);
	if (!sas_expander) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}

	sas_expander->handle = handle;
	sas_expander->num_phys = expander_pg0.NumPhys;
	sas_expander->sas_address_parent = sas_address_parent;
	sas_expander->sas_address = sas_address;

	ioc_info(ioc, "expander_add: handle(0x%04x), parent(0x%04x), sas_addr(0x%016llx), phys(%d)\n",
		 handle, parent_handle,
		 (u64)sas_expander->sas_address, sas_expander->num_phys);

	if (!sas_expander->num_phys)
		goto out_fail;
	sas_expander->phy = kcalloc(sas_expander->num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!sas_expander->phy) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}

	INIT_LIST_HEAD(&sas_expander->sas_port_list);
	mpt3sas_port = mpt3sas_transport_port_add(ioc, handle,
	    sas_address_parent);
	if (!mpt3sas_port) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}
	sas_expander->parent_dev = &mpt3sas_port->rphy->dev;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt3sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
		sas_expander->phy[i].handle = handle;
		sas_expander->phy[i].phy_id = i;

		if ((mpt3sas_transport_add_expander_phy(ioc,
		    &sas_expander->phy[i], expander_pg1,
		    sas_expander->parent_dev))) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
	}

	if (sas_expander->enclosure_handle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						sas_expander->enclosure_handle);
		if (enclosure_dev)
			sas_expander->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
	}

	_scsih_expander_node_add(ioc, sas_expander);
	return 0;

 out_fail:

	if (mpt3sas_port)
		mpt3sas_transport_port_remove(ioc, sas_expander->sas_address,
		    sas_address_parent);
	kfree(sas_expander);
	return rc;
}

/**
 * mpt3sas_expander_remove - removing expander object
 * @ioc: per adapter object
 * @sas_address: expander sas_address
 */
void
mpt3sas_expander_remove(struct MPT3SAS_ADAPTER *ioc, u64 sas_address)
{
	struct _sas_node *sas_expander;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt3sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	if (sas_expander)
		_scsih_expander_node_remove(ioc, sas_expander);
}

/**
 * _scsih_done -  internal SCSI_IO callback handler.
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when sending internal generated SCSI_IO.
 * The callback index passed is `ioc->scsih_cb_idx`
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
static u8
_scsih_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply =  mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (ioc->scsih_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->scsih_cmds.smid != smid)
		return 1;
	ioc->scsih_cmds.status |= MPT3_CMD_COMPLETE;
	if (mpi_reply) {
		memcpy(ioc->scsih_cmds.reply, mpi_reply,
		    mpi_reply->MsgLength*4);
		ioc->scsih_cmds.status |= MPT3_CMD_REPLY_VALID;
	}
	ioc->scsih_cmds.status &= ~MPT3_CMD_PENDING;
	complete(&ioc->scsih_cmds.done);
	return 1;
}




#define MPT3_MAX_LUNS (255)


/**
 * _scsih_check_access_status - check access flags
 * @ioc: per adapter object
 * @sas_address: sas address
 * @handle: sas device handle
 * @access_status: errors returned during discovery of the device
 *
 * Return: 0 for success, else failure
 */
static u8
_scsih_check_access_status(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	u16 handle, u8 access_status)
{
	u8 rc = 1;
	char *desc = NULL;

	switch (access_status) {
	case MPI2_SAS_DEVICE0_ASTATUS_NO_ERRORS:
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_NEEDS_INITIALIZATION:
		rc = 0;
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_CAPABILITY_FAILED:
		desc = "sata capability failed";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_AFFILIATION_CONFLICT:
		desc = "sata affiliation conflict";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_ROUTE_NOT_ADDRESSABLE:
		desc = "route not addressable";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SMP_ERROR_NOT_ADDRESSABLE:
		desc = "smp error not addressable";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_DEVICE_BLOCKED:
		desc = "device blocked";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_INIT_FAILED:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_UNKNOWN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_AFFILIATION_CONFLICT:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_DIAG:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_IDENTIFICATION:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_CHECK_POWER:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_PIO_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_MDMA_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_UDMA_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_ZONING_VIOLATION:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_NOT_ADDRESSABLE:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_MAX:
		desc = "sata initialization failed";
		break;
	default:
		desc = "unknown";
		break;
	}

	if (!rc)
		return 0;

	ioc_err(ioc, "discovery errors(%s): sas_address(0x%016llx), handle(0x%04x)\n",
		desc, (u64)sas_address, handle);
	return rc;
}

/**
 * _scsih_check_device - checking device responsiveness
 * @ioc: per adapter object
 * @parent_sas_address: sas address of parent expander or sas host
 * @handle: attached device handle
 * @phy_number: phy number
 * @link_rate: new link rate
 */
static void
_scsih_check_device(struct MPT3SAS_ADAPTER *ioc,
	u64 parent_sas_address, u16 handle, u8 phy_number, u8 link_rate)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	struct _sas_device *sas_device;
	struct _enclosure_node *enclosure_dev = NULL;
	u32 ioc_status;
	unsigned long flags;
	u64 sas_address;
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	u32 device_info;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle)))
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return;

	/* wide port handling ~ we need only handle device once for the phy that
	 * is matched in sas device page zero
	 */
	if (phy_number != sas_device_pg0.PhyNum)
		return;

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_end_device(device_info)))
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
	    sas_address);

	if (!sas_device)
		goto out_unlock;

	if (unlikely(sas_device->handle != handle)) {
		starget = sas_device->starget;
		sas_target_priv_data = starget->hostdata;
		starget_printk(KERN_INFO, starget,
			"handle changed from(0x%04x) to (0x%04x)!!!\n",
			sas_device->handle, handle);
		sas_target_priv_data->handle = handle;
		sas_device->handle = handle;
		if (le16_to_cpu(sas_device_pg0.Flags) &
		     MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID) {
			sas_device->enclosure_level =
				sas_device_pg0.EnclosureLevel;
			memcpy(sas_device->connector_name,
				sas_device_pg0.ConnectorName, 4);
			sas_device->connector_name[4] = '\0';
		} else {
			sas_device->enclosure_level = 0;
			sas_device->connector_name[0] = '\0';
		}

		sas_device->enclosure_handle =
				le16_to_cpu(sas_device_pg0.EnclosureHandle);
		sas_device->is_chassis_slot_valid = 0;
		enclosure_dev = mpt3sas_scsih_enclosure_find_by_handle(ioc,
						sas_device->enclosure_handle);
		if (enclosure_dev) {
			sas_device->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
			if (le16_to_cpu(enclosure_dev->pg0.Flags) &
			    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
				sas_device->is_chassis_slot_valid = 1;
				sas_device->chassis_slot =
					enclosure_dev->pg0.ChassisSlot;
			}
		}
	}

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		ioc_err(ioc, "device is not present handle(0x%04x), flags!!!\n",
			handle);
		goto out_unlock;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus))
		goto out_unlock;

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	_scsih_ublock_io_device(ioc, sas_address);

	if (sas_device)
		sas_device_put(sas_device);
	return;

out_unlock:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device)
		sas_device_put(sas_device);
}

/**
 * _scsih_add_device -  creating sas device object
 * @ioc: per adapter object
 * @handle: sas device handle
 * @phy_num: phy number end device attached to
 * @is_pd: is this hidden raid component
 *
 * Creating end device object, stored in ioc->sas_device_list.
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_scsih_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 phy_num,
	u8 is_pd)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	struct _sas_device *sas_device;
	struct _enclosure_node *enclosure_dev = NULL;
	u32 ioc_status;
	u64 sas_address;
	u32 device_info;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return -1;
	}

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_end_device(device_info)))
		return -1;
	set_bit(handle, ioc->pend_os_device_add);
	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		ioc_err(ioc, "device is not present handle(0x04%x)!!!\n",
			handle);
		return -1;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus))
		return -1;

	sas_device = mpt3sas_get_sdev_by_addr(ioc,
					sas_address);
	if (sas_device) {
		clear_bit(handle, ioc->pend_os_device_add);
		sas_device_put(sas_device);
		return -1;
	}

	if (sas_device_pg0.EnclosureHandle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
			    le16_to_cpu(sas_device_pg0.EnclosureHandle));
		if (enclosure_dev == NULL)
			ioc_info(ioc, "Enclosure handle(0x%04x) doesn't match with enclosure device!\n",
				 sas_device_pg0.EnclosureHandle);
	}

	sas_device = kzalloc(sizeof(struct _sas_device),
	    GFP_KERNEL);
	if (!sas_device) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 0;
	}

	kref_init(&sas_device->refcount);
	sas_device->handle = handle;
	if (_scsih_get_sas_address(ioc,
	    le16_to_cpu(sas_device_pg0.ParentDevHandle),
	    &sas_device->sas_address_parent) != 0)
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
	sas_device->enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	if (sas_device->enclosure_handle != 0)
		sas_device->slot =
		    le16_to_cpu(sas_device_pg0.Slot);
	sas_device->device_info = device_info;
	sas_device->sas_address = sas_address;
	sas_device->phy = sas_device_pg0.PhyNum;
	sas_device->fast_path = (le16_to_cpu(sas_device_pg0.Flags) &
	    MPI25_SAS_DEVICE0_FLAGS_FAST_PATH_CAPABLE) ? 1 : 0;

	if (le16_to_cpu(sas_device_pg0.Flags)
		& MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID) {
		sas_device->enclosure_level =
			sas_device_pg0.EnclosureLevel;
		memcpy(sas_device->connector_name,
			sas_device_pg0.ConnectorName, 4);
		sas_device->connector_name[4] = '\0';
	} else {
		sas_device->enclosure_level = 0;
		sas_device->connector_name[0] = '\0';
	}
	/* get enclosure_logical_id & chassis_slot*/
	sas_device->is_chassis_slot_valid = 0;
	if (enclosure_dev) {
		sas_device->enclosure_logical_id =
		    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
		if (le16_to_cpu(enclosure_dev->pg0.Flags) &
		    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
			sas_device->is_chassis_slot_valid = 1;
			sas_device->chassis_slot =
					enclosure_dev->pg0.ChassisSlot;
		}
	}

	/* get device name */
	sas_device->device_name = le64_to_cpu(sas_device_pg0.DeviceName);

	if (ioc->wait_for_discovery_to_complete)
		_scsih_sas_device_init_add(ioc, sas_device);
	else
		_scsih_sas_device_add(ioc, sas_device);

	sas_device_put(sas_device);
	return 0;
}

/**
 * _scsih_remove_device -  removing sas device object
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 */
static void
_scsih_remove_device(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;

	if ((ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM) &&
	     (sas_device->pfa_led_on)) {
		_scsih_turn_off_pfa_led(ioc, sas_device);
		sas_device->pfa_led_on = 0;
	}

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: enter: handle(0x%04x), sas_addr(0x%016llx)\n",
			    __func__,
			    sas_device->handle, (u64)sas_device->sas_address));

	dewtprintk(ioc, _scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	if (sas_device->starget && sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		_scsih_ublock_io_device(ioc, sas_device->sas_address);
		sas_target_priv_data->handle =
		     MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	if (!ioc->hide_drives)
		mpt3sas_transport_port_remove(ioc,
		    sas_device->sas_address,
		    sas_device->sas_address_parent);

	ioc_info(ioc, "removing handle(0x%04x), sas_addr(0x%016llx)\n",
		 sas_device->handle, (u64)sas_device->sas_address);

	_scsih_display_enclosure_chassis_info(ioc, sas_device, NULL, NULL);

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: exit: handle(0x%04x), sas_addr(0x%016llx)\n",
			    __func__,
			    sas_device->handle, (u64)sas_device->sas_address));
	dewtprintk(ioc, _scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));
}

/**
 * _scsih_sas_topology_change_event_debug - debug for topology event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_topology_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 phy_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->ExpStatus) {
	case MPI2_EVENT_SAS_TOPO_ES_ADDED:
		status_str = "add";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_RESPONDING:
	case 0:
		status_str =  "responding";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	ioc_info(ioc, "sas topology change: (%s)\n", status_str);
	pr_info("\thandle(0x%04x), enclosure_handle(0x%04x) " \
	    "start_phy(%02d), count(%d)\n",
	    le16_to_cpu(event_data->ExpanderDevHandle),
	    le16_to_cpu(event_data->EnclosureHandle),
	    event_data->StartPhyNum, event_data->NumEntries);
	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:
			status_str = "target add";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:
			status_str = "link rate change";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->PHY[i].LinkRate >> 4;
		prev_link_rate = event_data->PHY[i].LinkRate & 0xF;
		pr_info("\tphy(%02d), attached_handle(0x%04x): %s:" \
		    " link rate: new(0x%02x), old(0x%02x)\n", phy_number,
		    handle, status_str, link_rate, prev_link_rate);

	}
}

/**
 * _scsih_sas_topology_change_event - handle topology changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static int
_scsih_sas_topology_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	int i;
	u16 parent_handle, handle;
	u16 reason_code;
	u8 phy_number, max_phys;
	struct _sas_node *sas_expander;
	u64 sas_address;
	unsigned long flags;
	u8 link_rate, prev_link_rate;
	Mpi2EventDataSasTopologyChangeList_t *event_data =
		(Mpi2EventDataSasTopologyChangeList_t *)
		fw_event->event_data;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_topology_change_event_debug(ioc, event_data);

	if (ioc->shost_recovery || ioc->remove_host || ioc->pci_error_recovery)
		return 0;

	if (!ioc->sas_hba.num_phys)
		_scsih_sas_host_add(ioc);
	else
		_scsih_sas_host_refresh(ioc);

	if (fw_event->ignore) {
		dewtprintk(ioc, ioc_info(ioc, "ignoring expander event\n"));
		return 0;
	}

	parent_handle = le16_to_cpu(event_data->ExpanderDevHandle);

	/* handle expander add */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_ADDED)
		if (_scsih_expander_add(ioc, parent_handle) != 0)
			return 0;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt3sas_scsih_expander_find_by_handle(ioc,
	    parent_handle);
	if (sas_expander) {
		sas_address = sas_expander->sas_address;
		max_phys = sas_expander->num_phys;
	} else if (parent_handle < ioc->sas_hba.num_phys) {
		sas_address = ioc->sas_hba.sas_address;
		max_phys = ioc->sas_hba.num_phys;
	} else {
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	/* handle siblings events */
	for (i = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc,
				   ioc_info(ioc, "ignoring expander event\n"));
			return 0;
		}
		if (ioc->remove_host || ioc->pci_error_recovery)
			return 0;
		phy_number = event_data->StartPhyNum + i;
		if (phy_number >= max_phys)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if ((event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT) && (reason_code !=
		    MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING))
				continue;
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		link_rate = event_data->PHY[i].LinkRate >> 4;
		prev_link_rate = event_data->PHY[i].LinkRate & 0xF;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:

			if (ioc->shost_recovery)
				break;

			if (link_rate == prev_link_rate)
				break;

			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate);

			if (link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
				break;

			_scsih_check_device(ioc, sas_address, handle,
			    phy_number, link_rate);

			if (!test_bit(handle, ioc->pend_os_device_add))
				break;

			/* fall through */

		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:

			if (ioc->shost_recovery)
				break;

			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate);

			_scsih_add_device(ioc, handle, phy_number, 0);

			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:

			_scsih_device_remove_by_handle(ioc, handle);
			break;
		}
	}

	/* handle expander removal */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING &&
	    sas_expander)
		mpt3sas_expander_remove(ioc, sas_address);

	return 0;
}

/**
 * _scsih_sas_device_status_change_event_debug - debug for device event
 * @ioc: ?
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_device_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasDeviceStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
		reason_str = "smart data";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
		reason_str = "unsupported device discovered";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
		reason_str = "internal device reset";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
		reason_str = "internal task abort set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
		reason_str = "internal clear task set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
		reason_str = "internal query task";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE:
		reason_str = "sata init failure";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		reason_str = "internal device reset complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION:
		reason_str = "internal async notification";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality complete";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}
	ioc_info(ioc, "device status change: (%s)\thandle(0x%04x), sas address(0x%016llx), tag(%d)",
		 reason_str, le16_to_cpu(event_data->DevHandle),
		 (u64)le64_to_cpu(event_data->SASAddress),
		 le16_to_cpu(event_data->TaskTag));
	if (event_data->ReasonCode == MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA)
		pr_cont(", ASC(0x%x), ASCQ(0x%x)\n",
			event_data->ASC, event_data->ASCQ);
	pr_cont("\n");
}

/**
 * _scsih_sas_device_status_change_event - handle device status change
 * @ioc: per adapter object
 * @event_data: The fw event
 * Context: user.
 */
static void
_scsih_sas_device_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasDeviceStatusChange_t *event_data)
{
	struct MPT3SAS_TARGET *target_priv_data;
	struct _sas_device *sas_device;
	u64 sas_address;
	unsigned long flags;

	/* In MPI Revision K (0xC), the internal device reset complete was
	 * implemented, so avoid setting tm_busy flag for older firmware.
	 */
	if ((ioc->facts.HeaderVersion >> 8) < 0xC)
		return;

	if (event_data->ReasonCode !=
	    MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET &&
	   event_data->ReasonCode !=
	    MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_address = le64_to_cpu(event_data->SASAddress);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
	    sas_address);

	if (!sas_device || !sas_device->starget)
		goto out;

	target_priv_data = sas_device->starget->hostdata;
	if (!target_priv_data)
		goto out;

	if (event_data->ReasonCode ==
	    MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET)
		target_priv_data->tm_busy = 1;
	else
		target_priv_data->tm_busy = 0;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		ioc_info(ioc,
		    "%s tm_busy flag for handle(0x%04x)\n",
		    (target_priv_data->tm_busy == 1) ? "Enable" : "Disable",
		    target_priv_data->handle);

out:
	if (sas_device)
		sas_device_put(sas_device);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}


/**
 * _scsih_check_pcie_access_status - check access flags
 * @ioc: per adapter object
 * @wwid: wwid
 * @handle: sas device handle
 * @access_status: errors returned during discovery of the device
 *
 * Return: 0 for success, else failure
 */
static u8
_scsih_check_pcie_access_status(struct MPT3SAS_ADAPTER *ioc, u64 wwid,
	u16 handle, u8 access_status)
{
	u8 rc = 1;
	char *desc = NULL;

	switch (access_status) {
	case MPI26_PCIEDEV0_ASTATUS_NO_ERRORS:
	case MPI26_PCIEDEV0_ASTATUS_NEEDS_INITIALIZATION:
		rc = 0;
		break;
	case MPI26_PCIEDEV0_ASTATUS_CAPABILITY_FAILED:
		desc = "PCIe device capability failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED:
		desc = "PCIe device blocked";
		ioc_info(ioc,
		    "Device with Access Status (%s): wwid(0x%016llx), "
		    "handle(0x%04x)\n ll only be added to the internal list",
		    desc, (u64)wwid, handle);
		rc = 0;
		break;
	case MPI26_PCIEDEV0_ASTATUS_MEMORY_SPACE_ACCESS_FAILED:
		desc = "PCIe device mem space access failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_UNSUPPORTED_DEVICE:
		desc = "PCIe device unsupported";
		break;
	case MPI26_PCIEDEV0_ASTATUS_MSIX_REQUIRED:
		desc = "PCIe device MSIx Required";
		break;
	case MPI26_PCIEDEV0_ASTATUS_INIT_FAIL_MAX:
		desc = "PCIe device init fail max";
		break;
	case MPI26_PCIEDEV0_ASTATUS_UNKNOWN:
		desc = "PCIe device status unknown";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_READY_TIMEOUT:
		desc = "nvme ready timeout";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_DEVCFG_UNSUPPORTED:
		desc = "nvme device configuration unsupported";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_IDENTIFY_FAILED:
		desc = "nvme identify failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_QCONFIG_FAILED:
		desc = "nvme qconfig failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_QCREATION_FAILED:
		desc = "nvme qcreation failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_EVENTCFG_FAILED:
		desc = "nvme eventcfg failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_GET_FEATURE_STAT_FAILED:
		desc = "nvme get feature stat failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_IDLE_TIMEOUT:
		desc = "nvme idle timeout";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_FAILURE_STATUS:
		desc = "nvme failure status";
		break;
	default:
		ioc_err(ioc, "NVMe discovery error(0x%02x): wwid(0x%016llx), handle(0x%04x)\n",
			access_status, (u64)wwid, handle);
		return rc;
	}

	if (!rc)
		return rc;

	ioc_info(ioc, "NVMe discovery error(%s): wwid(0x%016llx), handle(0x%04x)\n",
		 desc, (u64)wwid, handle);
	return rc;
}

/**
 * _scsih_pcie_device_remove_from_sml -  removing pcie device
 * from SML and free up associated memory
 * @ioc: per adapter object
 * @pcie_device: the pcie_device object
 */
static void
_scsih_pcie_device_remove_from_sml(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;

	dewtprintk(ioc,
		   ioc_info(ioc, "%s: enter: handle(0x%04x), wwid(0x%016llx)\n",
			    __func__,
			    pcie_device->handle, (u64)pcie_device->wwid));
	if (pcie_device->enclosure_handle != 0)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enter: enclosure logical id(0x%016llx), slot(%d)\n",
				    __func__,
				    (u64)pcie_device->enclosure_logical_id,
				    pcie_device->slot));
	if (pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: enter: enclosure level(0x%04x), connector name(%s)\n",
				    __func__,
				    pcie_device->enclosure_level,
				    pcie_device->connector_name));

	if (pcie_device->starget && pcie_device->starget->hostdata) {
		sas_target_priv_data = pcie_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		_scsih_ublock_io_device(ioc, pcie_device->wwid);
		sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	ioc_info(ioc, "removing handle(0x%04x), wwid(0x%016llx)\n",
		 pcie_device->handle, (u64)pcie_device->wwid);
	if (pcie_device->enclosure_handle != 0)
		ioc_info(ioc, "removing : enclosure logical id(0x%016llx), slot(%d)\n",
			 (u64)pcie_device->enclosure_logical_id,
			 pcie_device->slot);
	if (pcie_device->connector_name[0] != '\0')
		ioc_info(ioc, "removing: enclosure level(0x%04x), connector name( %s)\n",
			 pcie_device->enclosure_level,
			 pcie_device->connector_name);

	if (pcie_device->starget && (pcie_device->access_status !=
				MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED))
		scsi_remove_target(&pcie_device->starget->dev);
	dewtprintk(ioc,
		   ioc_info(ioc, "%s: exit: handle(0x%04x), wwid(0x%016llx)\n",
			    __func__,
			    pcie_device->handle, (u64)pcie_device->wwid));
	if (pcie_device->enclosure_handle != 0)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: exit: enclosure logical id(0x%016llx), slot(%d)\n",
				    __func__,
				    (u64)pcie_device->enclosure_logical_id,
				    pcie_device->slot));
	if (pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: exit: enclosure level(0x%04x), connector name( %s)\n",
				    __func__,
				    pcie_device->enclosure_level,
				    pcie_device->connector_name));

	kfree(pcie_device->serial_number);
}


/**
 * _scsih_pcie_check_device - checking device responsiveness
 * @ioc: per adapter object
 * @handle: attached device handle
 */
static void
_scsih_pcie_check_device(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	u32 ioc_status;
	struct _pcie_device *pcie_device;
	u64 wwid;
	unsigned long flags;
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	u32 device_info;

	if ((mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
		&pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle)))
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return;

	/* check if this is end device */
	device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
	if (!(_scsih_is_nvme_pciescsi_device(device_info)))
		return;

	wwid = le64_to_cpu(pcie_device_pg0.WWID);
	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);

	if (!pcie_device) {
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		return;
	}

	if (unlikely(pcie_device->handle != handle)) {
		starget = pcie_device->starget;
		sas_target_priv_data = starget->hostdata;
		pcie_device->access_status = pcie_device_pg0.AccessStatus;
		starget_printk(KERN_INFO, starget,
		    "handle changed from(0x%04x) to (0x%04x)!!!\n",
		    pcie_device->handle, handle);
		sas_target_priv_data->handle = handle;
		pcie_device->handle = handle;

		if (le32_to_cpu(pcie_device_pg0.Flags) &
		    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) {
			pcie_device->enclosure_level =
			    pcie_device_pg0.EnclosureLevel;
			memcpy(&pcie_device->connector_name[0],
			    &pcie_device_pg0.ConnectorName[0], 4);
		} else {
			pcie_device->enclosure_level = 0;
			pcie_device->connector_name[0] = '\0';
		}
	}

	/* check if device is present */
	if (!(le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_DEVICE_PRESENT)) {
		ioc_info(ioc, "device is not present handle(0x%04x), flags!!!\n",
			 handle);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		pcie_device_put(pcie_device);
		return;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_pcie_access_status(ioc, wwid, handle,
	    pcie_device_pg0.AccessStatus)) {
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		pcie_device_put(pcie_device);
		return;
	}

	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	pcie_device_put(pcie_device);

	_scsih_ublock_io_device(ioc, wwid);

	return;
}

/**
 * _scsih_pcie_add_device -  creating pcie device object
 * @ioc: per adapter object
 * @handle: pcie device handle
 *
 * Creating end device object, stored in ioc->pcie_device_list.
 *
 * Return: 1 means queue the event later, 0 means complete the event
 */
static int
_scsih_pcie_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi26PCIeDevicePage2_t pcie_device_pg2;
	Mpi2ConfigReply_t mpi_reply;
	struct _pcie_device *pcie_device;
	struct _enclosure_node *enclosure_dev;
	u32 ioc_status;
	u64 wwid;

	if ((mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
	    &pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 0;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 0;
	}

	set_bit(handle, ioc->pend_os_device_add);
	wwid = le64_to_cpu(pcie_device_pg0.WWID);

	/* check if device is present */
	if (!(le32_to_cpu(pcie_device_pg0.Flags) &
		MPI26_PCIEDEV0_FLAGS_DEVICE_PRESENT)) {
		ioc_err(ioc, "device is not present handle(0x04%x)!!!\n",
			handle);
		return 0;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_pcie_access_status(ioc, wwid, handle,
	    pcie_device_pg0.AccessStatus))
		return 0;

	if (!(_scsih_is_nvme_pciescsi_device(le32_to_cpu
	    (pcie_device_pg0.DeviceInfo))))
		return 0;

	pcie_device = mpt3sas_get_pdev_by_wwid(ioc, wwid);
	if (pcie_device) {
		clear_bit(handle, ioc->pend_os_device_add);
		pcie_device_put(pcie_device);
		return 0;
	}

	/* PCIe Device Page 2 contains read-only information about a
	 * specific NVMe device; therefore, this page is only
	 * valid for NVMe devices and skip for pcie devices of type scsi.
	 */
	if (!(mpt3sas_scsih_is_pcie_scsi_device(
		le32_to_cpu(pcie_device_pg0.DeviceInfo)))) {
		if (mpt3sas_config_get_pcie_device_pg2(ioc, &mpi_reply,
		    &pcie_device_pg2, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle)) {
			ioc_err(ioc,
			    "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			return 0;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
					MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_err(ioc,
			    "failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			return 0;
		}
	}

	pcie_device = kzalloc(sizeof(struct _pcie_device), GFP_KERNEL);
	if (!pcie_device) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 0;
	}

	kref_init(&pcie_device->refcount);
	pcie_device->id = ioc->pcie_target_id++;
	pcie_device->channel = PCIE_CHANNEL;
	pcie_device->handle = handle;
	pcie_device->access_status = pcie_device_pg0.AccessStatus;
	pcie_device->device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
	pcie_device->wwid = wwid;
	pcie_device->port_num = pcie_device_pg0.PortNum;
	pcie_device->fast_path = (le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_FAST_PATH_CAPABLE) ? 1 : 0;

	pcie_device->enclosure_handle =
	    le16_to_cpu(pcie_device_pg0.EnclosureHandle);
	if (pcie_device->enclosure_handle != 0)
		pcie_device->slot = le16_to_cpu(pcie_device_pg0.Slot);

	if (le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) {
		pcie_device->enclosure_level = pcie_device_pg0.EnclosureLevel;
		memcpy(&pcie_device->connector_name[0],
		    &pcie_device_pg0.ConnectorName[0], 4);
	} else {
		pcie_device->enclosure_level = 0;
		pcie_device->connector_name[0] = '\0';
	}

	/* get enclosure_logical_id */
	if (pcie_device->enclosure_handle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						pcie_device->enclosure_handle);
		if (enclosure_dev)
			pcie_device->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
	}
	/* TODO -- Add device name once FW supports it */
	if (!(mpt3sas_scsih_is_pcie_scsi_device(
	    le32_to_cpu(pcie_device_pg0.DeviceInfo)))) {
		pcie_device->nvme_mdts =
		    le32_to_cpu(pcie_device_pg2.MaximumDataTransferSize);
		pcie_device->shutdown_latency =
			le16_to_cpu(pcie_device_pg2.ShutdownLatency);
		/*
		 * Set IOC's max_shutdown_latency to drive's RTD3 Entry Latency
		 * if drive's RTD3 Entry Latency is greater then IOC's
		 * max_shutdown_latency.
		 */
		if (pcie_device->shutdown_latency > ioc->max_shutdown_latency)
			ioc->max_shutdown_latency =
				pcie_device->shutdown_latency;
		if (pcie_device_pg2.ControllerResetTO)
			pcie_device->reset_timeout =
			    pcie_device_pg2.ControllerResetTO;
		else
			pcie_device->reset_timeout = 30;
	} else
		pcie_device->reset_timeout = 30;

	if (ioc->wait_for_discovery_to_complete)
		_scsih_pcie_device_init_add(ioc, pcie_device);
	else
		_scsih_pcie_device_add(ioc, pcie_device);

	pcie_device_put(pcie_device);
	return 0;
}

/**
 * _scsih_pcie_topology_change_event_debug - debug for topology
 * event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_pcie_topology_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 port_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->SwitchStatus) {
	case MPI26_EVENT_PCIE_TOPO_SS_ADDED:
		status_str = "add";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_RESPONDING:
	case 0:
		status_str =  "responding";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	ioc_info(ioc, "pcie topology change: (%s)\n", status_str);
	pr_info("\tswitch_handle(0x%04x), enclosure_handle(0x%04x)"
		"start_port(%02d), count(%d)\n",
		le16_to_cpu(event_data->SwitchDevHandle),
		le16_to_cpu(event_data->EnclosureHandle),
		event_data->StartPortNum, event_data->NumEntries);
	for (i = 0; i < event_data->NumEntries; i++) {
		handle =
			le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		port_number = event_data->StartPortNum + i;
		reason_code = event_data->PortEntry[i].PortStatus;
		switch (reason_code) {
		case MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED:
			status_str = "target add";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			status_str = "link rate change";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_NO_CHANGE:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->PortEntry[i].CurrentPortInfo &
			MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		prev_link_rate = event_data->PortEntry[i].PreviousPortInfo &
			MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		pr_info("\tport(%02d), attached_handle(0x%04x): %s:"
			" link rate: new(0x%02x), old(0x%02x)\n", port_number,
			handle, status_str, link_rate, prev_link_rate);
	}
}

/**
 * _scsih_pcie_topology_change_event - handle PCIe topology
 *  changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static void
_scsih_pcie_topology_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 link_rate, prev_link_rate;
	unsigned long flags;
	int rc;
	Mpi26EventDataPCIeTopologyChangeList_t *event_data =
		(Mpi26EventDataPCIeTopologyChangeList_t *) fw_event->event_data;
	struct _pcie_device *pcie_device;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_pcie_topology_change_event_debug(ioc, event_data);

	if (ioc->shost_recovery || ioc->remove_host ||
		ioc->pci_error_recovery)
		return;

	if (fw_event->ignore) {
		dewtprintk(ioc, ioc_info(ioc, "ignoring switch event\n"));
		return;
	}

	/* handle siblings events */
	for (i = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc,
				   ioc_info(ioc, "ignoring switch event\n"));
			return;
		}
		if (ioc->remove_host || ioc->pci_error_recovery)
			return;
		reason_code = event_data->PortEntry[i].PortStatus;
		handle =
			le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;

		link_rate = event_data->PortEntry[i].CurrentPortInfo
			& MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		prev_link_rate = event_data->PortEntry[i].PreviousPortInfo
			& MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;

		switch (reason_code) {
		case MPI26_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			if (ioc->shost_recovery)
				break;
			if (link_rate == prev_link_rate)
				break;
			if (link_rate < MPI26_EVENT_PCIE_TOPO_PI_RATE_2_5)
				break;

			_scsih_pcie_check_device(ioc, handle);

			/* This code after this point handles the test case
			 * where a device has been added, however its returning
			 * BUSY for sometime.  Then before the Device Missing
			 * Delay expires and the device becomes READY, the
			 * device is removed and added back.
			 */
			spin_lock_irqsave(&ioc->pcie_device_lock, flags);
			pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
			spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

			if (pcie_device) {
				pcie_device_put(pcie_device);
				break;
			}

			if (!test_bit(handle, ioc->pend_os_device_add))
				break;

			dewtprintk(ioc,
				   ioc_info(ioc, "handle(0x%04x) device not found: convert event to a device add\n",
					    handle));
			event_data->PortEntry[i].PortStatus &= 0xF0;
			event_data->PortEntry[i].PortStatus |=
				MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED;
			/* fall through */
		case MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED:
			if (ioc->shost_recovery)
				break;
			if (link_rate < MPI26_EVENT_PCIE_TOPO_PI_RATE_2_5)
				break;

			rc = _scsih_pcie_add_device(ioc, handle);
			if (!rc) {
				/* mark entry vacant */
				/* TODO This needs to be reviewed and fixed,
				 * we dont have an entry
				 * to make an event void like vacant
				 */
				event_data->PortEntry[i].PortStatus |=
					MPI26_EVENT_PCIE_TOPO_PS_NO_CHANGE;
			}
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			_scsih_pcie_device_remove_by_handle(ioc, handle);
			break;
		}
	}
}

/**
 * _scsih_pcie_device_status_change_event_debug - debug for device event
 * @ioc: ?
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_pcie_device_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeDeviceStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI26_EVENT_PCIDEV_STAT_RC_SMART_DATA:
		reason_str = "smart data";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_UNSUPPORTED:
		reason_str = "unsupported device discovered";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET:
		reason_str = "internal device reset";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
		reason_str = "internal task abort set";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
		reason_str = "internal clear task set";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_QUERY_TASK_INTERNAL:
		reason_str = "internal query task";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_DEV_INIT_FAILURE:
		reason_str = "device init failure";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		reason_str = "internal device reset complete";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort complete";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_ASYNC_NOTIFICATION:
		reason_str = "internal async notification";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_PCIE_HOT_RESET_FAILED:
		reason_str = "pcie hot reset failed";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}

	ioc_info(ioc, "PCIE device status change: (%s)\n"
		 "\thandle(0x%04x), WWID(0x%016llx), tag(%d)",
		 reason_str, le16_to_cpu(event_data->DevHandle),
		 (u64)le64_to_cpu(event_data->WWID),
		 le16_to_cpu(event_data->TaskTag));
	if (event_data->ReasonCode == MPI26_EVENT_PCIDEV_STAT_RC_SMART_DATA)
		pr_cont(", ASC(0x%x), ASCQ(0x%x)\n",
			event_data->ASC, event_data->ASCQ);
	pr_cont("\n");
}

/**
 * _scsih_pcie_device_status_change_event - handle device status
 * change
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_pcie_device_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	struct MPT3SAS_TARGET *target_priv_data;
	struct _pcie_device *pcie_device;
	u64 wwid;
	unsigned long flags;
	Mpi26EventDataPCIeDeviceStatusChange_t *event_data =
		(Mpi26EventDataPCIeDeviceStatusChange_t *)fw_event->event_data;
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_pcie_device_status_change_event_debug(ioc,
			event_data);

	if (event_data->ReasonCode !=
		MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET &&
		event_data->ReasonCode !=
		MPI26_EVENT_PCIDEV_STAT_RC_CMP_INTERNAL_DEV_RESET)
		return;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	wwid = le64_to_cpu(event_data->WWID);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);

	if (!pcie_device || !pcie_device->starget)
		goto out;

	target_priv_data = pcie_device->starget->hostdata;
	if (!target_priv_data)
		goto out;

	if (event_data->ReasonCode ==
		MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET)
		target_priv_data->tm_busy = 1;
	else
		target_priv_data->tm_busy = 0;
out:
	if (pcie_device)
		pcie_device_put(pcie_device);

	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_sas_enclosure_dev_status_change_event_debug - debug for enclosure
 * event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_enclosure_dev_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasEnclDevStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_ENCL_RC_ADDED:
		reason_str = "enclosure add";
		break;
	case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING:
		reason_str = "enclosure remove";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}

	ioc_info(ioc, "enclosure status change: (%s)\n"
		 "\thandle(0x%04x), enclosure logical id(0x%016llx) number slots(%d)\n",
		 reason_str,
		 le16_to_cpu(event_data->EnclosureHandle),
		 (u64)le64_to_cpu(event_data->EnclosureLogicalID),
		 le16_to_cpu(event_data->StartSlot));
}

/**
 * _scsih_sas_enclosure_dev_status_change_event - handle enclosure events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_enclosure_dev_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2ConfigReply_t mpi_reply;
	struct _enclosure_node *enclosure_dev = NULL;
	Mpi2EventDataSasEnclDevStatusChange_t *event_data =
		(Mpi2EventDataSasEnclDevStatusChange_t *)fw_event->event_data;
	int rc;
	u16 enclosure_handle = le16_to_cpu(event_data->EnclosureHandle);

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_enclosure_dev_status_change_event_debug(ioc,
		     (Mpi2EventDataSasEnclDevStatusChange_t *)
		     fw_event->event_data);
	if (ioc->shost_recovery)
		return;

	if (enclosure_handle)
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						enclosure_handle);
	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_ENCL_RC_ADDED:
		if (!enclosure_dev) {
			enclosure_dev =
				kzalloc(sizeof(struct _enclosure_node),
					GFP_KERNEL);
			if (!enclosure_dev) {
				ioc_info(ioc, "failure at %s:%d/%s()!\n",
					 __FILE__, __LINE__, __func__);
				return;
			}
			rc = mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
				&enclosure_dev->pg0,
				MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
				enclosure_handle);

			if (rc || (le16_to_cpu(mpi_reply.IOCStatus) &
						MPI2_IOCSTATUS_MASK)) {
				kfree(enclosure_dev);
				return;
			}

			list_add_tail(&enclosure_dev->list,
							&ioc->enclosure_list);
		}
		break;
	case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING:
		if (enclosure_dev) {
			list_del(&enclosure_dev->list);
			kfree(enclosure_dev);
		}
		break;
	default:
		break;
	}
}

/**
 * _scsih_sas_broadcast_primitive_event - handle broadcast events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_broadcast_primitive_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	struct scsi_cmnd *scmd;
	struct scsi_device *sdev;
	struct scsiio_tracker *st;
	u16 smid, handle;
	u32 lun;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u32 termination_count;
	u32 query_count;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	Mpi2EventDataSasBroadcastPrimitive_t *event_data =
		(Mpi2EventDataSasBroadcastPrimitive_t *)
		fw_event->event_data;
	u16 ioc_status;
	unsigned long flags;
	int r;
	u8 max_retries = 0;
	u8 task_abort_retries;

	mutex_lock(&ioc->tm_cmds.mutex);
	ioc_info(ioc, "%s: enter: phy number(%d), width(%d)\n",
		 __func__, event_data->PhyNum, event_data->PortWidth);

	_scsih_block_io_all_device(ioc);

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	mpi_reply = ioc->tm_cmds.reply;
 broadcast_aen_retry:

	/* sanity checks for retrying this loop */
	if (max_retries++ == 5) {
		dewtprintk(ioc, ioc_info(ioc, "%s: giving up\n", __func__));
		goto out;
	} else if (max_retries > 1)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: %d retry\n",
				    __func__, max_retries - 1));

	termination_count = 0;
	query_count = 0;
	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		if (ioc->shost_recovery)
			goto out;
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		st = scsi_cmd_priv(scmd);
		sdev = scmd->device;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data || !sas_device_priv_data->sas_target)
			continue;
		 /* skip hidden raid components */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT)
			continue;
		 /* skip volumes */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_VOLUME)
			continue;
		 /* skip PCIe devices */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_PCIE_DEVICE)
			continue;

		handle = sas_device_priv_data->sas_target->handle;
		lun = sas_device_priv_data->lun;
		query_count++;

		if (ioc->shost_recovery)
			goto out;

		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		r = mpt3sas_scsih_issue_tm(ioc, handle, lun,
			MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK, st->smid,
			st->msix_io, 30, 0);
		if (r == FAILED) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: FAILED when sending "
			    "QUERY_TASK: scmd(%p)\n", scmd);
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus)
		    & MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			sdev_printk(KERN_WARNING, sdev,
				"query task: FAILED with IOCSTATUS(0x%04x), scmd(%p)\n",
				ioc_status, scmd);
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}

		/* see if IO is still owned by IOC and target */
		if (mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED ||
		     mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC) {
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			continue;
		}
		task_abort_retries = 0;
 tm_retry:
		if (task_abort_retries++ == 60) {
			dewtprintk(ioc,
				   ioc_info(ioc, "%s: ABORT_TASK: giving up\n",
					    __func__));
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}

		if (ioc->shost_recovery)
			goto out_no_lock;

		r = mpt3sas_scsih_issue_tm(ioc, handle, sdev->lun,
			MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, st->smid,
			st->msix_io, 30, 0);
		if (r == FAILED || st->cb_idx != 0xFF) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: ABORT_TASK: FAILED : "
			    "scmd(%p)\n", scmd);
			goto tm_retry;
		}

		if (task_abort_retries > 1)
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: ABORT_TASK: RETRIES (%d):"
			    " scmd(%p)\n",
			    task_abort_retries - 1, scmd);

		termination_count += le32_to_cpu(mpi_reply->TerminationCount);
		spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	}

	if (ioc->broadcast_aen_pending) {
		dewtprintk(ioc,
			   ioc_info(ioc,
				    "%s: loop back due to pending AEN\n",
				    __func__));
		 ioc->broadcast_aen_pending = 0;
		 goto broadcast_aen_retry;
	}

 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
 out_no_lock:

	dewtprintk(ioc,
		   ioc_info(ioc, "%s - exit, query_count = %d termination_count = %d\n",
			    __func__, query_count, termination_count));

	ioc->broadcast_aen_busy = 0;
	if (!ioc->shost_recovery)
		_scsih_ublock_io_all_device(ioc);
	mutex_unlock(&ioc->tm_cmds.mutex);
}

/**
 * _scsih_sas_discovery_event - handle discovery events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_discovery_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventDataSasDiscovery_t *event_data =
		(Mpi2EventDataSasDiscovery_t *) fw_event->event_data;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		ioc_info(ioc, "discovery event: (%s)",
			 event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED ?
			 "start" : "stop");
		if (event_data->DiscoveryStatus)
			pr_cont("discovery_status(0x%08x)",
				le32_to_cpu(event_data->DiscoveryStatus));
		pr_cont("\n");
	}

	if (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED &&
	    !ioc->sas_hba.num_phys) {
		if (disable_discovery > 0 && ioc->shost_recovery) {
			/* Wait for the reset to complete */
			while (ioc->shost_recovery)
				ssleep(1);
		}
		_scsih_sas_host_add(ioc);
	}
}

/**
 * _scsih_sas_device_discovery_error_event - display SAS device discovery error
 *						events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_device_discovery_error_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi25EventDataSasDeviceDiscoveryError_t *event_data =
		(Mpi25EventDataSasDeviceDiscoveryError_t *)fw_event->event_data;

	switch (event_data->ReasonCode) {
	case MPI25_EVENT_SAS_DISC_ERR_SMP_FAILED:
		ioc_warn(ioc, "SMP command sent to the expander (handle:0x%04x, sas_address:0x%016llx, physical_port:0x%02x) has failed\n",
			 le16_to_cpu(event_data->DevHandle),
			 (u64)le64_to_cpu(event_data->SASAddress),
			 event_data->PhysicalPort);
		break;
	case MPI25_EVENT_SAS_DISC_ERR_SMP_TIMEOUT:
		ioc_warn(ioc, "SMP command sent to the expander (handle:0x%04x, sas_address:0x%016llx, physical_port:0x%02x) has timed out\n",
			 le16_to_cpu(event_data->DevHandle),
			 (u64)le64_to_cpu(event_data->SASAddress),
			 event_data->PhysicalPort);
		break;
	default:
		break;
	}
}

/**
 * _scsih_pcie_enumeration_event - handle enumeration events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_pcie_enumeration_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi26EventDataPCIeEnumeration_t *event_data =
		(Mpi26EventDataPCIeEnumeration_t *)fw_event->event_data;

	if (!(ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK))
		return;

	ioc_info(ioc, "pcie enumeration event: (%s) Flag 0x%02x",
		 (event_data->ReasonCode == MPI26_EVENT_PCIE_ENUM_RC_STARTED) ?
		 "started" : "completed",
		 event_data->Flags);
	if (event_data->EnumerationStatus)
		pr_cont("enumeration_status(0x%08x)",
			le32_to_cpu(event_data->EnumerationStatus));
	pr_cont("\n");
}

/**
 * _scsih_ir_fastpath - turn on fastpath for IR physdisk
 * @ioc: per adapter object
 * @handle: device handle for physical disk
 * @phys_disk_num: physical disk number
 *
 * Return: 0 for success, else failure.
 */
static int
_scsih_ir_fastpath(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 phys_disk_num)
{
	Mpi2RaidActionRequest_t *mpi_request;
	Mpi2RaidActionReply_t *mpi_reply;
	u16 smid;
	u8 issue_reset = 0;
	int rc = 0;
	u16 ioc_status;
	u32 log_info;

	if (ioc->hba_mpi_version_belonged == MPI2_VERSION)
		return rc;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: scsih_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		rc = -EAGAIN;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2RaidActionRequest_t));

	mpi_request->Function = MPI2_FUNCTION_RAID_ACTION;
	mpi_request->Action = MPI2_RAID_ACTION_PHYSDISK_HIDDEN;
	mpi_request->PhysDiskNum = phys_disk_num;

	dewtprintk(ioc,
		   ioc_info(ioc, "IR RAID_ACTION: turning fast path on for handle(0x%04x), phys_disk_num (0x%02x)\n",
			    handle, phys_disk_num));

	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->scsih_cmds.done, 10*HZ);

	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->scsih_cmds.status, mpi_request,
		    sizeof(Mpi2RaidActionRequest_t)/4, issue_reset);
		rc = -EFAULT;
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {

		mpi_reply = ioc->scsih_cmds.reply;
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus);
		if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
			log_info =  le32_to_cpu(mpi_reply->IOCLogInfo);
		else
			log_info = 0;
		ioc_status &= MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			dewtprintk(ioc,
				   ioc_info(ioc, "IR RAID_ACTION: failed: ioc_status(0x%04x), loginfo(0x%08x)!!!\n",
					    ioc_status, log_info));
			rc = -EFAULT;
		} else
			dewtprintk(ioc,
				   ioc_info(ioc, "IR RAID_ACTION: completed successfully\n"));
	}

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);

	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return rc;
}

/**
 * _scsih_reprobe_lun - reprobing lun
 * @sdev: scsi device struct
 * @no_uld_attach: sdev->no_uld_attach flag setting
 *
 **/
static void
_scsih_reprobe_lun(struct scsi_device *sdev, void *no_uld_attach)
{
	sdev->no_uld_attach = no_uld_attach ? 1 : 0;
	sdev_printk(KERN_INFO, sdev, "%s raid component\n",
	    sdev->no_uld_attach ? "hiding" : "exposing");
	WARN_ON(scsi_device_reprobe(sdev));
}

/**
 * _scsih_sas_volume_add - add new volume
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 */
static void
_scsih_sas_volume_add(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	u64 wwid;
	u16 handle = le16_to_cpu(element->VolDevHandle);
	int rc;

	mpt3sas_config_get_volume_wwid(ioc, handle, &wwid);
	if (!wwid) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_wwid(ioc, wwid);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (raid_device)
		return;

	raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
	if (!raid_device) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	raid_device->id = ioc->sas_id++;
	raid_device->channel = RAID_CHANNEL;
	raid_device->handle = handle;
	raid_device->wwid = wwid;
	_scsih_raid_device_add(ioc, raid_device);
	if (!ioc->wait_for_discovery_to_complete) {
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		_scsih_determine_boot_device(ioc, raid_device, 1);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
}

/**
 * _scsih_sas_volume_delete - delete volume
 * @ioc: per adapter object
 * @handle: volume device handle
 * Context: user.
 */
static void
_scsih_sas_volume_delete(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget = NULL;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
	if (raid_device) {
		if (raid_device->starget) {
			starget = raid_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->deleted = 1;
		}
		ioc_info(ioc, "removing handle(0x%04x), wwid(0x%016llx)\n",
			 raid_device->handle, (u64)raid_device->wwid);
		list_del(&raid_device->list);
		kfree(raid_device);
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	if (starget)
		scsi_remove_target(&starget->dev);
}

/**
 * _scsih_sas_pd_expose - expose pd component to /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 */
static void
_scsih_sas_pd_expose(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		sas_device->volume_handle = 0;
		sas_device->volume_wwid = 0;
		clear_bit(handle, ioc->pd_handles);
		if (sas_device->starget && sas_device->starget->hostdata) {
			starget = sas_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->flags &=
			    ~MPT_TARGET_FLAGS_RAID_COMPONENT;
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;

	/* exposing raid component */
	if (starget)
		starget_for_each_device(starget, NULL, _scsih_reprobe_lun);

	sas_device_put(sas_device);
}

/**
 * _scsih_sas_pd_hide - hide pd component from /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 */
static void
_scsih_sas_pd_hide(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	u16 volume_handle = 0;
	u64 volume_wwid = 0;

	mpt3sas_config_get_volume_handle(ioc, handle, &volume_handle);
	if (volume_handle)
		mpt3sas_config_get_volume_wwid(ioc, volume_handle,
		    &volume_wwid);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		set_bit(handle, ioc->pd_handles);
		if (sas_device->starget && sas_device->starget->hostdata) {
			starget = sas_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
			sas_device->volume_handle = volume_handle;
			sas_device->volume_wwid = volume_wwid;
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;

	/* hiding raid component */
	_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);

	if (starget)
		starget_for_each_device(starget, (void *)1, _scsih_reprobe_lun);

	sas_device_put(sas_device);
}

/**
 * _scsih_sas_pd_delete - delete pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 */
static void
_scsih_sas_pd_delete(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	_scsih_device_remove_by_handle(ioc, handle);
}

/**
 * _scsih_sas_pd_add - remove pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 */
static void
_scsih_sas_pd_add(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u64 sas_address;
	u16 parent_handle;

	set_bit(handle, ioc->pd_handles);

	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);
		sas_device_put(sas_device);
		return;
	}

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return;
	}

	parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
	if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
		mpt3sas_transport_update_links(ioc, sas_address, handle,
		    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);

	_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);
	_scsih_add_device(ioc, handle, 0, 1);
}

/**
 * _scsih_sas_ir_config_change_event_debug - debug for IR Config Change events
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_ir_config_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	u8 element_type;
	int i;
	char *reason_str = NULL, *element_str = NULL;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];

	ioc_info(ioc, "raid config change: (%s), elements(%d)\n",
		 le32_to_cpu(event_data->Flags) & MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG ?
		 "foreign" : "native",
		 event_data->NumElements);
	for (i = 0; i < event_data->NumElements; i++, element++) {
		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			reason_str = "add";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			reason_str = "remove";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE:
			reason_str = "no change";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			reason_str = "hide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			reason_str = "unhide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
			reason_str = "volume_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
			reason_str = "volume_deleted";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			reason_str = "pd_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			reason_str = "pd_deleted";
			break;
		default:
			reason_str = "unknown reason";
			break;
		}
		element_type = le16_to_cpu(element->ElementFlags) &
		    MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK;
		switch (element_type) {
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT:
			element_str = "volume";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT:
			element_str = "phys disk";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT:
			element_str = "hot spare";
			break;
		default:
			element_str = "unknown element";
			break;
		}
		pr_info("\t(%s:%s), vol handle(0x%04x), " \
		    "pd handle(0x%04x), pd num(0x%02x)\n", element_str,
		    reason_str, le16_to_cpu(element->VolDevHandle),
		    le16_to_cpu(element->PhysDiskDevHandle),
		    element->PhysDiskNum);
	}
}

/**
 * _scsih_sas_ir_config_change_event - handle ir configuration change events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_ir_config_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u8 foreign_config;
	Mpi2EventDataIrConfigChangeList_t *event_data =
		(Mpi2EventDataIrConfigChangeList_t *)
		fw_event->event_data;

	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) &&
	     (!ioc->hide_ir_msg))
		_scsih_sas_ir_config_change_event_debug(ioc, event_data);

	foreign_config = (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ? 1 : 0;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	if (ioc->shost_recovery &&
	    ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		for (i = 0; i < event_data->NumElements; i++, element++) {
			if (element->ReasonCode == MPI2_EVENT_IR_CHANGE_RC_HIDE)
				_scsih_ir_fastpath(ioc,
					le16_to_cpu(element->PhysDiskDevHandle),
					element->PhysDiskNum);
		}
		return;
	}

	for (i = 0; i < event_data->NumElements; i++, element++) {

		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			if (!foreign_config)
				_scsih_sas_volume_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			if (!foreign_config)
				_scsih_sas_volume_delete(ioc,
				    le16_to_cpu(element->VolDevHandle));
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_hide(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_expose(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_delete(ioc, element);
			break;
		}
	}
}

/**
 * _scsih_sas_ir_volume_event - IR volume event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_ir_volume_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	u64 wwid;
	unsigned long flags;
	struct _raid_device *raid_device;
	u16 handle;
	u32 state;
	int rc;
	Mpi2EventDataIrVolume_t *event_data =
		(Mpi2EventDataIrVolume_t *) fw_event->event_data;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->VolDevHandle);
	state = le32_to_cpu(event_data->NewValue);
	if (!ioc->hide_ir_msg)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: handle(0x%04x), old(0x%08x), new(0x%08x)\n",
				    __func__, handle,
				    le32_to_cpu(event_data->PreviousValue),
				    state));
	switch (state) {
	case MPI2_RAID_VOL_STATE_MISSING:
	case MPI2_RAID_VOL_STATE_FAILED:
		_scsih_sas_volume_delete(ioc, handle);
		break;

	case MPI2_RAID_VOL_STATE_ONLINE:
	case MPI2_RAID_VOL_STATE_DEGRADED:
	case MPI2_RAID_VOL_STATE_OPTIMAL:

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

		if (raid_device)
			break;

		mpt3sas_config_get_volume_wwid(ioc, handle, &wwid);
		if (!wwid) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			break;
		}

		raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
		if (!raid_device) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			break;
		}

		raid_device->id = ioc->sas_id++;
		raid_device->channel = RAID_CHANNEL;
		raid_device->handle = handle;
		raid_device->wwid = wwid;
		_scsih_raid_device_add(ioc, raid_device);
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
		break;

	case MPI2_RAID_VOL_STATE_INITIALIZING:
	default:
		break;
	}
}

/**
 * _scsih_sas_ir_physical_disk_event - PD event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_ir_physical_disk_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	u16 handle, parent_handle;
	u32 state;
	struct _sas_device *sas_device;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	Mpi2EventDataIrPhysicalDisk_t *event_data =
		(Mpi2EventDataIrPhysicalDisk_t *) fw_event->event_data;
	u64 sas_address;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->PhysDiskDevHandle);
	state = le32_to_cpu(event_data->NewValue);

	if (!ioc->hide_ir_msg)
		dewtprintk(ioc,
			   ioc_info(ioc, "%s: handle(0x%04x), old(0x%08x), new(0x%08x)\n",
				    __func__, handle,
				    le32_to_cpu(event_data->PreviousValue),
				    state));

	switch (state) {
	case MPI2_RAID_PD_STATE_ONLINE:
	case MPI2_RAID_PD_STATE_DEGRADED:
	case MPI2_RAID_PD_STATE_REBUILDING:
	case MPI2_RAID_PD_STATE_OPTIMAL:
	case MPI2_RAID_PD_STATE_HOT_SPARE:

		if (!ioc->is_warpdrive)
			set_bit(handle, ioc->pd_handles);

		sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
		if (sas_device) {
			sas_device_put(sas_device);
			return;
		}

		if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle))) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			return;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			return;
		}

		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
			mpt3sas_transport_update_links(ioc, sas_address, handle,
			    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);

		_scsih_add_device(ioc, handle, 0, 1);

		break;

	case MPI2_RAID_PD_STATE_OFFLINE:
	case MPI2_RAID_PD_STATE_NOT_CONFIGURED:
	case MPI2_RAID_PD_STATE_NOT_COMPATIBLE:
	default:
		break;
	}
}

/**
 * _scsih_sas_ir_operation_status_event_debug - debug for IR op event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_ir_operation_status_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrOperationStatus_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->RAIDOperation) {
	case MPI2_EVENT_IR_RAIDOP_RESYNC:
		reason_str = "resync";
		break;
	case MPI2_EVENT_IR_RAIDOP_ONLINE_CAP_EXPANSION:
		reason_str = "online capacity expansion";
		break;
	case MPI2_EVENT_IR_RAIDOP_CONSISTENCY_CHECK:
		reason_str = "consistency check";
		break;
	case MPI2_EVENT_IR_RAIDOP_BACKGROUND_INIT:
		reason_str = "background init";
		break;
	case MPI2_EVENT_IR_RAIDOP_MAKE_DATA_CONSISTENT:
		reason_str = "make data consistent";
		break;
	}

	if (!reason_str)
		return;

	ioc_info(ioc, "raid operational status: (%s)\thandle(0x%04x), percent complete(%d)\n",
		 reason_str,
		 le16_to_cpu(event_data->VolDevHandle),
		 event_data->PercentComplete);
}

/**
 * _scsih_sas_ir_operation_status_event - handle RAID operation events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_scsih_sas_ir_operation_status_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventDataIrOperationStatus_t *event_data =
		(Mpi2EventDataIrOperationStatus_t *)
		fw_event->event_data;
	static struct _raid_device *raid_device;
	unsigned long flags;
	u16 handle;

	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) &&
	    (!ioc->hide_ir_msg))
		_scsih_sas_ir_operation_status_event_debug(ioc,
		     event_data);

	/* code added for raid transport support */
	if (event_data->RAIDOperation == MPI2_EVENT_IR_RAIDOP_RESYNC) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		handle = le16_to_cpu(event_data->VolDevHandle);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		if (raid_device)
			raid_device->percent_complete =
			    event_data->PercentComplete;
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
}

/**
 * _scsih_prep_device_scan - initialize parameters prior to device scan
 * @ioc: per adapter object
 *
 * Set the deleted flag prior to device scan.  If the device is found during
 * the scan, then we clear the deleted flag.
 */
static void
_scsih_prep_device_scan(struct MPT3SAS_ADAPTER *ioc)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (sas_device_priv_data && sas_device_priv_data->sas_target)
			sas_device_priv_data->sas_target->deleted = 1;
	}
}

/**
 * _scsih_mark_responding_sas_device - mark a sas_devices as responding
 * @ioc: per adapter object
 * @sas_device_pg0: SAS Device page 0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponsive_sas_devices.
 */
static void
_scsih_mark_responding_sas_device(struct MPT3SAS_ADAPTER *ioc,
Mpi2SasDevicePage0_t *sas_device_pg0)
{
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _sas_device *sas_device = NULL;
	struct _enclosure_node *enclosure_dev = NULL;
	unsigned long flags;

	if (sas_device_pg0->EnclosureHandle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
				le16_to_cpu(sas_device_pg0->EnclosureHandle));
		if (enclosure_dev == NULL)
			ioc_info(ioc, "Enclosure handle(0x%04x) doesn't match with enclosure device!\n",
				 sas_device_pg0->EnclosureHandle);
	}
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if ((sas_device->sas_address == le64_to_cpu(
		    sas_device_pg0->SASAddress)) && (sas_device->slot ==
		    le16_to_cpu(sas_device_pg0->Slot))) {
			sas_device->responding = 1;
			starget = sas_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			if (starget) {
				starget_printk(KERN_INFO, starget,
				    "handle(0x%04x), sas_addr(0x%016llx)\n",
				    le16_to_cpu(sas_device_pg0->DevHandle),
				    (unsigned long long)
				    sas_device->sas_address);

				if (sas_device->enclosure_handle != 0)
					starget_printk(KERN_INFO, starget,
					 "enclosure logical id(0x%016llx),"
					 " slot(%d)\n",
					 (unsigned long long)
					 sas_device->enclosure_logical_id,
					 sas_device->slot);
			}
			if (le16_to_cpu(sas_device_pg0->Flags) &
			      MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID) {
				sas_device->enclosure_level =
				   sas_device_pg0->EnclosureLevel;
				memcpy(&sas_device->connector_name[0],
					&sas_device_pg0->ConnectorName[0], 4);
			} else {
				sas_device->enclosure_level = 0;
				sas_device->connector_name[0] = '\0';
			}

			sas_device->enclosure_handle =
				le16_to_cpu(sas_device_pg0->EnclosureHandle);
			sas_device->is_chassis_slot_valid = 0;
			if (enclosure_dev) {
				sas_device->enclosure_logical_id = le64_to_cpu(
					enclosure_dev->pg0.EnclosureLogicalID);
				if (le16_to_cpu(enclosure_dev->pg0.Flags) &
				    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
					sas_device->is_chassis_slot_valid = 1;
					sas_device->chassis_slot =
						enclosure_dev->pg0.ChassisSlot;
				}
			}

			if (sas_device->handle == le16_to_cpu(
			    sas_device_pg0->DevHandle))
				goto out;
			pr_info("\thandle changed from(0x%04x)!!!\n",
			    sas_device->handle);
			sas_device->handle = le16_to_cpu(
			    sas_device_pg0->DevHandle);
			if (sas_target_priv_data)
				sas_target_priv_data->handle =
				    le16_to_cpu(sas_device_pg0->DevHandle);
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_create_enclosure_list_after_reset - Free Existing list,
 *	And create enclosure list by scanning all Enclosure Page(0)s
 * @ioc: per adapter object
 */
static void
_scsih_create_enclosure_list_after_reset(struct MPT3SAS_ADAPTER *ioc)
{
	struct _enclosure_node *enclosure_dev;
	Mpi2ConfigReply_t mpi_reply;
	u16 enclosure_handle;
	int rc;

	/* Free existing enclosure list */
	mpt3sas_free_enclosure_list(ioc);

	/* Re constructing enclosure list after reset*/
	enclosure_handle = 0xFFFF;
	do {
		enclosure_dev =
			kzalloc(sizeof(struct _enclosure_node), GFP_KERNEL);
		if (!enclosure_dev) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			return;
		}
		rc = mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
				&enclosure_dev->pg0,
				MPI2_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE,
				enclosure_handle);

		if (rc || (le16_to_cpu(mpi_reply.IOCStatus) &
						MPI2_IOCSTATUS_MASK)) {
			kfree(enclosure_dev);
			return;
		}
		list_add_tail(&enclosure_dev->list,
						&ioc->enclosure_list);
		enclosure_handle =
			le16_to_cpu(enclosure_dev->pg0.EnclosureHandle);
	} while (1);
}

/**
 * _scsih_search_responding_sas_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 */
static void
_scsih_search_responding_sas_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u32 device_info;

	ioc_info(ioc, "search for end-devices: start\n");

	if (list_empty(&ioc->sas_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
			break;
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
		if (!(_scsih_is_end_device(device_info)))
			continue;
		_scsih_mark_responding_sas_device(ioc, &sas_device_pg0);
	}

 out:
	ioc_info(ioc, "search for end-devices: complete\n");
}

/**
 * _scsih_mark_responding_pcie_device - mark a pcie_device as responding
 * @ioc: per adapter object
 * @pcie_device_pg0: PCIe Device page 0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponding_devices.
 */
static void
_scsih_mark_responding_pcie_device(struct MPT3SAS_ADAPTER *ioc,
	Mpi26PCIeDevicePage0_t *pcie_device_pg0)
{
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list) {
		if ((pcie_device->wwid == le64_to_cpu(pcie_device_pg0->WWID))
		    && (pcie_device->slot == le16_to_cpu(
		    pcie_device_pg0->Slot))) {
			pcie_device->access_status =
					pcie_device_pg0->AccessStatus;
			pcie_device->responding = 1;
			starget = pcie_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			if (starget) {
				starget_printk(KERN_INFO, starget,
				    "handle(0x%04x), wwid(0x%016llx) ",
				    pcie_device->handle,
				    (unsigned long long)pcie_device->wwid);
				if (pcie_device->enclosure_handle != 0)
					starget_printk(KERN_INFO, starget,
					    "enclosure logical id(0x%016llx), "
					    "slot(%d)\n",
					    (unsigned long long)
					    pcie_device->enclosure_logical_id,
					    pcie_device->slot);
			}

			if (((le32_to_cpu(pcie_device_pg0->Flags)) &
			    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) &&
			    (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
				pcie_device->enclosure_level =
				    pcie_device_pg0->EnclosureLevel;
				memcpy(&pcie_device->connector_name[0],
				    &pcie_device_pg0->ConnectorName[0], 4);
			} else {
				pcie_device->enclosure_level = 0;
				pcie_device->connector_name[0] = '\0';
			}

			if (pcie_device->handle == le16_to_cpu(
			    pcie_device_pg0->DevHandle))
				goto out;
			pr_info("\thandle changed from(0x%04x)!!!\n",
			    pcie_device->handle);
			pcie_device->handle = le16_to_cpu(
			    pcie_device_pg0->DevHandle);
			if (sas_target_priv_data)
				sas_target_priv_data->handle =
				    le16_to_cpu(pcie_device_pg0->DevHandle);
			goto out;
		}
	}

 out:
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_search_responding_pcie_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 */
static void
_scsih_search_responding_pcie_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u32 device_info;

	ioc_info(ioc, "search for end-devices: start\n");

	if (list_empty(&ioc->pcie_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
		&pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
		handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from %s: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 __func__, ioc_status,
				 le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(pcie_device_pg0.DevHandle);
		device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
		if (!(_scsih_is_nvme_pciescsi_device(device_info)))
			continue;
		_scsih_mark_responding_pcie_device(ioc, &pcie_device_pg0);
	}
out:
	ioc_info(ioc, "search for PCIe end-devices: complete\n");
}

/**
 * _scsih_mark_responding_raid_device - mark a raid_device as responding
 * @ioc: per adapter object
 * @wwid: world wide identifier for raid volume
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponsive_raid_devices.
 */
static void
_scsih_mark_responding_raid_device(struct MPT3SAS_ADAPTER *ioc, u64 wwid,
	u16 handle)
{
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid == wwid && raid_device->starget) {
			starget = raid_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			raid_device->responding = 1;
			spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
			starget_printk(KERN_INFO, raid_device->starget,
			    "handle(0x%04x), wwid(0x%016llx)\n", handle,
			    (unsigned long long)raid_device->wwid);

			/*
			 * WARPDRIVE: The handles of the PDs might have changed
			 * across the host reset so re-initialize the
			 * required data for Direct IO
			 */
			mpt3sas_init_warpdrive_properties(ioc, raid_device);
			spin_lock_irqsave(&ioc->raid_device_lock, flags);
			if (raid_device->handle == handle) {
				spin_unlock_irqrestore(&ioc->raid_device_lock,
				    flags);
				return;
			}
			pr_info("\thandle changed from(0x%04x)!!!\n",
			    raid_device->handle);
			raid_device->handle = handle;
			if (sas_target_priv_data)
				sas_target_priv_data->handle = handle;
			spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_search_responding_raid_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 */
static void
_scsih_search_responding_raid_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2RaidVolPage1_t volume_pg1;
	Mpi2RaidVolPage0_t volume_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u8 phys_disk_num;

	if (!ioc->ir_firmware)
		return;

	ioc_info(ioc, "search for raid volumes: start\n");

	if (list_empty(&ioc->raid_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
			break;
		handle = le16_to_cpu(volume_pg1.DevHandle);

		if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
		    &volume_pg0, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
		     sizeof(Mpi2RaidVolPage0_t)))
			continue;

		if (volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_OPTIMAL ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_ONLINE ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_DEGRADED)
			_scsih_mark_responding_raid_device(ioc,
			    le64_to_cpu(volume_pg1.WWID), handle);
	}

	/* refresh the pd_handles */
	if (!ioc->is_warpdrive) {
		phys_disk_num = 0xFF;
		memset(ioc->pd_handles, 0, ioc->pd_handles_sz);
		while (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
		    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
		    phys_disk_num))) {
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
				break;
			phys_disk_num = pd_pg0.PhysDiskNum;
			handle = le16_to_cpu(pd_pg0.DevHandle);
			set_bit(handle, ioc->pd_handles);
		}
	}
 out:
	ioc_info(ioc, "search for responding raid volumes: complete\n");
}

/**
 * _scsih_mark_responding_expander - mark a expander as responding
 * @ioc: per adapter object
 * @expander_pg0:SAS Expander Config Page0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponsive_expanders.
 */
static void
_scsih_mark_responding_expander(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ExpanderPage0_t *expander_pg0)
{
	struct _sas_node *sas_expander = NULL;
	unsigned long flags;
	int i;
	struct _enclosure_node *enclosure_dev = NULL;
	u16 handle = le16_to_cpu(expander_pg0->DevHandle);
	u16 enclosure_handle = le16_to_cpu(expander_pg0->EnclosureHandle);
	u64 sas_address = le64_to_cpu(expander_pg0->SASAddress);

	if (enclosure_handle)
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
							enclosure_handle);

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address)
			continue;
		sas_expander->responding = 1;

		if (enclosure_dev) {
			sas_expander->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
			sas_expander->enclosure_handle =
			    le16_to_cpu(expander_pg0->EnclosureHandle);
		}

		if (sas_expander->handle == handle)
			goto out;
		pr_info("\texpander(0x%016llx): handle changed" \
		    " from(0x%04x) to (0x%04x)!!!\n",
		    (unsigned long long)sas_expander->sas_address,
		    sas_expander->handle, handle);
		sas_expander->handle = handle;
		for (i = 0 ; i < sas_expander->num_phys ; i++)
			sas_expander->phy[i].handle = handle;
		goto out;
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_search_responding_expanders -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 */
static void
_scsih_search_responding_expanders(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u64 sas_address;
	u16 handle;

	ioc_info(ioc, "search for expanders: start\n");

	if (list_empty(&ioc->sas_expander_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
			break;

		handle = le16_to_cpu(expander_pg0.DevHandle);
		sas_address = le64_to_cpu(expander_pg0.SASAddress);
		pr_info("\texpander present: handle(0x%04x), sas_addr(0x%016llx)\n",
			handle,
		    (unsigned long long)sas_address);
		_scsih_mark_responding_expander(ioc, &expander_pg0);
	}

 out:
	ioc_info(ioc, "search for expanders: complete\n");
}

/**
 * _scsih_remove_unresponding_devices - removing unresponding devices
 * @ioc: per adapter object
 */
static void
_scsih_remove_unresponding_devices(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *sas_device_next;
	struct _sas_node *sas_expander, *sas_expander_next;
	struct _raid_device *raid_device, *raid_device_next;
	struct _pcie_device *pcie_device, *pcie_device_next;
	struct list_head tmp_list;
	unsigned long flags;
	LIST_HEAD(head);

	ioc_info(ioc, "removing unresponding devices: start\n");

	/* removing unresponding end devices */
	ioc_info(ioc, "removing unresponding devices: end-devices\n");
	/*
	 * Iterate, pulling off devices marked as non-responding. We become the
	 * owner for the reference the list had on any object we prune.
	 */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry_safe(sas_device, sas_device_next,
	    &ioc->sas_device_list, list) {
		if (!sas_device->responding)
			list_move_tail(&sas_device->list, &head);
		else
			sas_device->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	/*
	 * Now, uninitialize and remove the unresponding devices we pruned.
	 */
	list_for_each_entry_safe(sas_device, sas_device_next, &head, list) {
		_scsih_remove_device(ioc, sas_device);
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}

	ioc_info(ioc, "Removing unresponding devices: pcie end-devices\n");
	INIT_LIST_HEAD(&head);
	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry_safe(pcie_device, pcie_device_next,
	    &ioc->pcie_device_list, list) {
		if (!pcie_device->responding)
			list_move_tail(&pcie_device->list, &head);
		else
			pcie_device->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	list_for_each_entry_safe(pcie_device, pcie_device_next, &head, list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}

	/* removing unresponding volumes */
	if (ioc->ir_firmware) {
		ioc_info(ioc, "removing unresponding devices: volumes\n");
		list_for_each_entry_safe(raid_device, raid_device_next,
		    &ioc->raid_device_list, list) {
			if (!raid_device->responding)
				_scsih_sas_volume_delete(ioc,
				    raid_device->handle);
			else
				raid_device->responding = 0;
		}
	}

	/* removing unresponding expanders */
	ioc_info(ioc, "removing unresponding devices: expanders\n");
	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	INIT_LIST_HEAD(&tmp_list);
	list_for_each_entry_safe(sas_expander, sas_expander_next,
	    &ioc->sas_expander_list, list) {
		if (!sas_expander->responding)
			list_move_tail(&sas_expander->list, &tmp_list);
		else
			sas_expander->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	list_for_each_entry_safe(sas_expander, sas_expander_next, &tmp_list,
	    list) {
		_scsih_expander_node_remove(ioc, sas_expander);
	}

	ioc_info(ioc, "removing unresponding devices: complete\n");

	/* unblock devices */
	_scsih_ublock_io_all_device(ioc);
}

static void
_scsih_refresh_expander_links(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander, u16 handle)
{
	Mpi2ExpanderPage1_t expander_pg1;
	Mpi2ConfigReply_t mpi_reply;
	int i;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt3sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			ioc_err(ioc, "failure at %s:%d/%s()!\n",
				__FILE__, __LINE__, __func__);
			return;
		}

		mpt3sas_transport_update_links(ioc, sas_expander->sas_address,
		    le16_to_cpu(expander_pg1.AttachedDevHandle), i,
		    expander_pg1.NegotiatedLinkRate >> 4);
	}
}

/**
 * _scsih_scan_for_devices_after_reset - scan for devices after host reset
 * @ioc: per adapter object
 */
static void
_scsih_scan_for_devices_after_reset(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi2RaidVolPage1_t volume_pg1;
	Mpi2RaidVolPage0_t volume_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2EventIrConfigElement_t element;
	Mpi2ConfigReply_t mpi_reply;
	u8 phys_disk_num;
	u16 ioc_status;
	u16 handle, parent_handle;
	u64 sas_address;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _sas_node *expander_device;
	static struct _raid_device *raid_device;
	u8 retry_count;
	unsigned long flags;

	ioc_info(ioc, "scan devices: start\n");

	_scsih_sas_host_refresh(ioc);

	ioc_info(ioc, "\tscan devices: expanders start\n");

	/* expanders */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from expander scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(expander_pg0.DevHandle);
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		expander_device = mpt3sas_scsih_expander_find_by_sas_address(
		    ioc, le64_to_cpu(expander_pg0.SASAddress));
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (expander_device)
			_scsih_refresh_expander_links(ioc, expander_device,
			    handle);
		else {
			ioc_info(ioc, "\tBEFORE adding expander: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(expander_pg0.SASAddress));
			_scsih_expander_add(ioc, handle);
			ioc_info(ioc, "\tAFTER adding expander: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(expander_pg0.SASAddress));
		}
	}

	ioc_info(ioc, "\tscan devices: expanders complete\n");

	if (!ioc->ir_firmware)
		goto skip_to_sas;

	ioc_info(ioc, "\tscan devices: phys disk start\n");

	/* phys disk */
	phys_disk_num = 0xFF;
	while (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
	    phys_disk_num))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from phys disk scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		phys_disk_num = pd_pg0.PhysDiskNum;
		handle = le16_to_cpu(pd_pg0.DevHandle);
		sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
		if (sas_device) {
			sas_device_put(sas_device);
			continue;
		}
		if (mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle) != 0)
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from phys disk scan ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle,
		    &sas_address)) {
			ioc_info(ioc, "\tBEFORE adding phys disk: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(sas_device_pg0.SASAddress));
			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, sas_device_pg0.PhyNum,
			    MPI2_SAS_NEG_LINK_RATE_1_5);
			set_bit(handle, ioc->pd_handles);
			retry_count = 0;
			/* This will retry adding the end device.
			 * _scsih_add_device() will decide on retries and
			 * return "1" when it should be retried
			 */
			while (_scsih_add_device(ioc, handle, retry_count++,
			    1)) {
				ssleep(1);
			}
			ioc_info(ioc, "\tAFTER adding phys disk: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}

	ioc_info(ioc, "\tscan devices: phys disk complete\n");

	ioc_info(ioc, "\tscan devices: volumes start\n");

	/* volumes */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from volume scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(volume_pg1.DevHandle);
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_wwid(ioc,
		    le64_to_cpu(volume_pg1.WWID));
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (raid_device)
			continue;
		if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
		    &volume_pg0, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
		     sizeof(Mpi2RaidVolPage0_t)))
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from volume scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		if (volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_OPTIMAL ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_ONLINE ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_DEGRADED) {
			memset(&element, 0, sizeof(Mpi2EventIrConfigElement_t));
			element.ReasonCode = MPI2_EVENT_IR_CHANGE_RC_ADDED;
			element.VolDevHandle = volume_pg1.DevHandle;
			ioc_info(ioc, "\tBEFORE adding volume: handle (0x%04x)\n",
				 volume_pg1.DevHandle);
			_scsih_sas_volume_add(ioc, &element);
			ioc_info(ioc, "\tAFTER adding volume: handle (0x%04x)\n",
				 volume_pg1.DevHandle);
		}
	}

	ioc_info(ioc, "\tscan devices: volumes complete\n");

 skip_to_sas:

	ioc_info(ioc, "\tscan devices: end devices start\n");

	/* sas devices */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from end device scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		if (!(_scsih_is_end_device(
		    le32_to_cpu(sas_device_pg0.DeviceInfo))))
			continue;
		sas_device = mpt3sas_get_sdev_by_addr(ioc,
		    le64_to_cpu(sas_device_pg0.SASAddress));
		if (sas_device) {
			sas_device_put(sas_device);
			continue;
		}
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address)) {
			ioc_info(ioc, "\tBEFORE adding end device: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(sas_device_pg0.SASAddress));
			mpt3sas_transport_update_links(ioc, sas_address, handle,
			    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);
			retry_count = 0;
			/* This will retry adding the end device.
			 * _scsih_add_device() will decide on retries and
			 * return "1" when it should be retried
			 */
			while (_scsih_add_device(ioc, handle, retry_count++,
			    0)) {
				ssleep(1);
			}
			ioc_info(ioc, "\tAFTER adding end device: handle (0x%04x), sas_addr(0x%016llx)\n",
				 handle,
				 (u64)le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}
	ioc_info(ioc, "\tscan devices: end devices complete\n");
	ioc_info(ioc, "\tscan devices: pcie end devices start\n");

	/* pcie devices */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
		&pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
		handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus)
				& MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			ioc_info(ioc, "\tbreak from pcie end device scan: ioc_status(0x%04x), loginfo(0x%08x)\n",
				 ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(pcie_device_pg0.DevHandle);
		if (!(_scsih_is_nvme_pciescsi_device(
			le32_to_cpu(pcie_device_pg0.DeviceInfo))))
			continue;
		pcie_device = mpt3sas_get_pdev_by_wwid(ioc,
				le64_to_cpu(pcie_device_pg0.WWID));
		if (pcie_device) {
			pcie_device_put(pcie_device);
			continue;
		}
		retry_count = 0;
		parent_handle = le16_to_cpu(pcie_device_pg0.ParentDevHandle);
		_scsih_pcie_add_device(ioc, handle);

		ioc_info(ioc, "\tAFTER adding pcie end device: handle (0x%04x), wwid(0x%016llx)\n",
			 handle, (u64)le64_to_cpu(pcie_device_pg0.WWID));
	}
	ioc_info(ioc, "\tpcie devices: pcie end devices complete\n");
	ioc_info(ioc, "scan devices: complete\n");
}

/**
 * mpt3sas_scsih_reset_handler - reset callback handler (for scsih)
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void mpt3sas_scsih_pre_reset_handler(struct MPT3SAS_ADAPTER *ioc)
{
	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_PRE_RESET\n", __func__));
}

/**
 * mpt3sas_scsih_clear_outstanding_scsi_tm_commands - clears outstanding
 *							scsi & tm cmds.
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void
mpt3sas_scsih_clear_outstanding_scsi_tm_commands(struct MPT3SAS_ADAPTER *ioc)
{
	dtmprintk(ioc,
	    ioc_info(ioc, "%s: clear outstanding scsi & tm cmds\n", __func__));
	if (ioc->scsih_cmds.status & MPT3_CMD_PENDING) {
		ioc->scsih_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->scsih_cmds.smid);
		complete(&ioc->scsih_cmds.done);
	}
	if (ioc->tm_cmds.status & MPT3_CMD_PENDING) {
		ioc->tm_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->tm_cmds.smid);
		complete(&ioc->tm_cmds.done);
	}

	memset(ioc->pend_os_device_add, 0, ioc->pend_os_device_add_sz);
	memset(ioc->device_remove_in_progress, 0,
	       ioc->device_remove_in_progress_sz);
	_scsih_fw_event_cleanup_queue(ioc);
	_scsih_flush_running_cmds(ioc);
}

/**
 * mpt3sas_scsih_reset_handler - reset callback handler (for scsih)
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void
mpt3sas_scsih_reset_done_handler(struct MPT3SAS_ADAPTER *ioc)
{
	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_DONE_RESET\n", __func__));
	if ((!ioc->is_driver_loading) && !(disable_discovery > 0 &&
					   !ioc->sas_hba.num_phys)) {
		_scsih_prep_device_scan(ioc);
		_scsih_create_enclosure_list_after_reset(ioc);
		_scsih_search_responding_sas_devices(ioc);
		_scsih_search_responding_pcie_devices(ioc);
		_scsih_search_responding_raid_devices(ioc);
		_scsih_search_responding_expanders(ioc);
		_scsih_error_recovery_delete_devices(ioc);
	}
}

/**
 * _mpt3sas_fw_work - delayed task for processing firmware events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 */
static void
_mpt3sas_fw_work(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	_scsih_fw_event_del_from_list(ioc, fw_event);

	/* the queue is being flushed so ignore this event */
	if (ioc->remove_host || ioc->pci_error_recovery) {
		fw_event_work_put(fw_event);
		return;
	}

	switch (fw_event->event) {
	case MPT3SAS_PROCESS_TRIGGER_DIAG:
		mpt3sas_process_trigger_data(ioc,
			(struct SL_WH_TRIGGERS_EVENT_DATA_T *)
			fw_event->event_data);
		break;
	case MPT3SAS_REMOVE_UNRESPONDING_DEVICES:
		while (scsi_host_in_recovery(ioc->shost) ||
					 ioc->shost_recovery) {
			/*
			 * If we're unloading, bail. Otherwise, this can become
			 * an infinite loop.
			 */
			if (ioc->remove_host)
				goto out;
			ssleep(1);
		}
		_scsih_remove_unresponding_devices(ioc);
		_scsih_scan_for_devices_after_reset(ioc);
		_scsih_set_nvme_max_shutdown_latency(ioc);
		break;
	case MPT3SAS_PORT_ENABLE_COMPLETE:
		ioc->start_scan = 0;
		if (missing_delay[0] != -1 && missing_delay[1] != -1)
			mpt3sas_base_update_missing_delay(ioc, missing_delay[0],
			    missing_delay[1]);
		dewtprintk(ioc,
			   ioc_info(ioc, "port enable: complete from worker thread\n"));
		break;
	case MPT3SAS_TURN_ON_PFA_LED:
		_scsih_turn_on_pfa_led(ioc, fw_event->device_handle);
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_sas_topology_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
			_scsih_sas_device_status_change_event_debug(ioc,
			    (Mpi2EventDataSasDeviceStatusChange_t *)
			    fw_event->event_data);
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
		_scsih_sas_discovery_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
		_scsih_sas_device_discovery_error_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		_scsih_sas_broadcast_primitive_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		_scsih_sas_enclosure_dev_status_change_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		_scsih_sas_ir_config_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_VOLUME:
		_scsih_sas_ir_volume_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		_scsih_sas_ir_physical_disk_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		_scsih_sas_ir_operation_status_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		_scsih_pcie_device_status_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_ENUMERATION:
		_scsih_pcie_enumeration_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
		_scsih_pcie_topology_change_event(ioc, fw_event);
			return;
	break;
	}
out:
	fw_event_work_put(fw_event);
}

/**
 * _firmware_event_work
 * @work: The fw_event_work object
 * Context: user.
 *
 * wrappers for the work thread handling firmware events
 */

static void
_firmware_event_work(struct work_struct *work)
{
	struct fw_event_work *fw_event = container_of(work,
	    struct fw_event_work, work);

	_mpt3sas_fw_work(fw_event->ioc, fw_event);
}

/**
 * mpt3sas_scsih_event_callback - firmware event handler (called at ISR time)
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt.
 *
 * This function merely adds a new work task into ioc->firmware_event_thread.
 * The tasks are worked from _firmware_event_work in user context.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
u8
mpt3sas_scsih_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	struct fw_event_work *fw_event;
	Mpi2EventNotificationReply_t *mpi_reply;
	u16 event;
	u16 sz;
	Mpi26EventDataActiveCableExcept_t *ActiveCableEventData;

	/* events turned off due to host reset */
	if (ioc->pci_error_recovery)
		return 1;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);

	if (unlikely(!mpi_reply)) {
		ioc_err(ioc, "mpi_reply not valid at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 1;
	}

	event = le16_to_cpu(mpi_reply->Event);

	if (event != MPI2_EVENT_LOG_ENTRY_ADDED)
		mpt3sas_trigger_event(ioc, event, 0);

	switch (event) {
	/* handle these */
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		Mpi2EventDataSasBroadcastPrimitive_t *baen_data =
		    (Mpi2EventDataSasBroadcastPrimitive_t *)
		    mpi_reply->EventData;

		if (baen_data->Primitive !=
		    MPI2_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT)
			return 1;

		if (ioc->broadcast_aen_busy) {
			ioc->broadcast_aen_pending++;
			return 1;
		} else
			ioc->broadcast_aen_busy = 1;
		break;
	}

	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_check_topo_delete_events(ioc,
		    (Mpi2EventDataSasTopologyChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
	_scsih_check_pcie_topo_remove_events(ioc,
		    (Mpi26EventDataPCIeTopologyChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		_scsih_check_ir_config_unhide_events(ioc,
		    (Mpi2EventDataIrConfigChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_VOLUME:
		_scsih_check_volume_delete_events(ioc,
		    (Mpi2EventDataIrVolume_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
	{
		Mpi2EventDataLogEntryAdded_t *log_entry;
		u32 *log_code;

		if (!ioc->is_warpdrive)
			break;

		log_entry = (Mpi2EventDataLogEntryAdded_t *)
		    mpi_reply->EventData;
		log_code = (u32 *)log_entry->LogData;

		if (le16_to_cpu(log_entry->LogEntryQualifier)
		    != MPT2_WARPDRIVE_LOGENTRY)
			break;

		switch (le32_to_cpu(*log_code)) {
		case MPT2_WARPDRIVE_LC_SSDT:
			ioc_warn(ioc, "WarpDrive Warning: IO Throttling has occurred in the WarpDrive subsystem. Check WarpDrive documentation for additional details.\n");
			break;
		case MPT2_WARPDRIVE_LC_SSDLW:
			ioc_warn(ioc, "WarpDrive Warning: Program/Erase Cycles for the WarpDrive subsystem in degraded range. Check WarpDrive documentation for additional details.\n");
			break;
		case MPT2_WARPDRIVE_LC_SSDLF:
			ioc_err(ioc, "WarpDrive Fatal Error: There are no Program/Erase Cycles for the WarpDrive subsystem. The storage device will be in read-only mode. Check WarpDrive documentation for additional details.\n");
			break;
		case MPT2_WARPDRIVE_LC_BRMF:
			ioc_err(ioc, "WarpDrive Fatal Error: The Backup Rail Monitor has failed on the WarpDrive subsystem. Check WarpDrive documentation for additional details.\n");
			break;
		}

		break;
	}
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		_scsih_sas_device_status_change_event(ioc,
		    (Mpi2EventDataSasDeviceStatusChange_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
	case MPI2_EVENT_SAS_DISCOVERY:
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_PHYSICAL_DISK:
	case MPI2_EVENT_PCIE_ENUMERATION:
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		break;

	case MPI2_EVENT_TEMP_THRESHOLD:
		_scsih_temp_threshold_events(ioc,
			(Mpi2EventDataTemperature_t *)
			mpi_reply->EventData);
		break;
	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
		ActiveCableEventData =
		    (Mpi26EventDataActiveCableExcept_t *) mpi_reply->EventData;
		switch (ActiveCableEventData->ReasonCode) {
		case MPI26_EVENT_ACTIVE_CABLE_INSUFFICIENT_POWER:
			ioc_notice(ioc, "Currently an active cable with ReceptacleID %d\n",
				   ActiveCableEventData->ReceptacleID);
			pr_notice("cannot be powered and devices connected\n");
			pr_notice("to this active cable will not be seen\n");
			pr_notice("This active cable requires %d mW of power\n",
			     ActiveCableEventData->ActiveCablePowerRequirement);
			break;

		case MPI26_EVENT_ACTIVE_CABLE_DEGRADED:
			ioc_notice(ioc, "Currently a cable with ReceptacleID %d\n",
				   ActiveCableEventData->ReceptacleID);
			pr_notice(
			    "is not running at optimal speed(12 Gb/s rate)\n");
			break;
		}

		break;

	default: /* ignore the rest */
		return 1;
	}

	sz = le16_to_cpu(mpi_reply->EventDataLength) * 4;
	fw_event = alloc_fw_event_work(sz);
	if (!fw_event) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		return 1;
	}

	memcpy(fw_event->event_data, mpi_reply->EventData, sz);
	fw_event->ioc = ioc;
	fw_event->VF_ID = mpi_reply->VF_ID;
	fw_event->VP_ID = mpi_reply->VP_ID;
	fw_event->event = event;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
	return 1;
}

/**
 * _scsih_expander_node_remove - removing expander device from list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 *
 * Removing object and freeing associated memory from the
 * ioc->sas_expander_list.
 */
static void
_scsih_expander_node_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	struct _sas_port *mpt3sas_port, *next;
	unsigned long flags;

	/* remove sibling ports attached to this expander */
	list_for_each_entry_safe(mpt3sas_port, next,
	   &sas_expander->sas_port_list, port_list) {
		if (ioc->shost_recovery)
			return;
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt3sas_device_remove_by_sas_address(ioc,
			    mpt3sas_port->remote_identify.sas_address);
		else if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt3sas_expander_remove(ioc,
			    mpt3sas_port->remote_identify.sas_address);
	}

	mpt3sas_transport_port_remove(ioc, sas_expander->sas_address,
	    sas_expander->sas_address_parent);

	ioc_info(ioc, "expander_remove: handle(0x%04x), sas_addr(0x%016llx)\n",
		 sas_expander->handle, (unsigned long long)
		 sas_expander->sas_address);

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_del(&sas_expander->list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	kfree(sas_expander->phy);
	kfree(sas_expander);
}

/**
 * _scsih_nvme_shutdown - NVMe shutdown notification
 * @ioc: per adapter object
 *
 * Sending IoUnitControl request with shutdown operation code to alert IOC that
 * the host system is shutting down so that IOC can issue NVMe shutdown to
 * NVMe drives attached to it.
 */
static void
_scsih_nvme_shutdown(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26IoUnitControlRequest_t *mpi_request;
	Mpi26IoUnitControlReply_t *mpi_reply;
	u16 smid;

	/* are there any NVMe devices ? */
	if (list_empty(&ioc->pcie_device_list))
		return;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: scsih_cmd in use\n", __func__);
		goto out;
	}

	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		ioc_err(ioc,
		    "%s: failed obtaining a smid\n", __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi26IoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI26_CTRL_OP_SHUTDOWN;

	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	/* Wait for max_shutdown_latency seconds */
	ioc_info(ioc,
		"Io Unit Control shutdown (sending), Shutdown latency %d sec\n",
		ioc->max_shutdown_latency);
	wait_for_completion_timeout(&ioc->scsih_cmds.done,
			ioc->max_shutdown_latency*HZ);

	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		ioc_err(ioc, "%s: timeout\n", __func__);
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		ioc_info(ioc, "Io Unit Control shutdown (complete):"
			"ioc_status(0x%04x), loginfo(0x%08x)\n",
			le16_to_cpu(mpi_reply->IOCStatus),
			le32_to_cpu(mpi_reply->IOCLogInfo));
	}
 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
}


/**
 * _scsih_ir_shutdown - IR shutdown notification
 * @ioc: per adapter object
 *
 * Sending RAID Action to alert the Integrated RAID subsystem of the IOC that
 * the host system is shutting down.
 */
static void
_scsih_ir_shutdown(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2RaidActionRequest_t *mpi_request;
	Mpi2RaidActionReply_t *mpi_reply;
	u16 smid;

	/* is IR firmware build loaded ? */
	if (!ioc->ir_firmware)
		return;

	/* are there any volumes ? */
	if (list_empty(&ioc->raid_device_list))
		return;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: scsih_cmd in use\n", __func__);
		goto out;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2RaidActionRequest_t));

	mpi_request->Function = MPI2_FUNCTION_RAID_ACTION;
	mpi_request->Action = MPI2_RAID_ACTION_SYSTEM_SHUTDOWN_INITIATED;

	if (!ioc->hide_ir_msg)
		ioc_info(ioc, "IR shutdown (sending)\n");
	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->scsih_cmds.done, 10*HZ);

	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		ioc_err(ioc, "%s: timeout\n", __func__);
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		if (!ioc->hide_ir_msg)
			ioc_info(ioc, "IR shutdown (complete): ioc_status(0x%04x), loginfo(0x%08x)\n",
				 le16_to_cpu(mpi_reply->IOCStatus),
				 le32_to_cpu(mpi_reply->IOCLogInfo));
	}

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
}

/**
 * scsih_remove - detach and remove add host
 * @pdev: PCI device struct
 *
 * Routine called when unloading the driver.
 */
static void scsih_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct _sas_port *mpt3sas_port, *next_port;
	struct _raid_device *raid_device, *next;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _pcie_device *pcie_device, *pcienext;
	struct workqueue_struct	*wq;
	unsigned long flags;
	Mpi2ConfigReply_t mpi_reply;

	ioc->remove_host = 1;

	mpt3sas_wait_for_commands_to_complete(ioc);
	_scsih_flush_running_cmds(ioc);

	_scsih_fw_event_cleanup_queue(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);
	/*
	 * Copy back the unmodified ioc page1. so that on next driver load,
	 * current modified changes on ioc page1 won't take effect.
	 */
	if (ioc->is_aero_ioc)
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply,
				&ioc->ioc_pg1_copy);
	/* release all the volumes */
	_scsih_ir_shutdown(ioc);
	sas_remove_host(shost);
	list_for_each_entry_safe(raid_device, next, &ioc->raid_device_list,
	    list) {
		if (raid_device->starget) {
			sas_target_priv_data =
			    raid_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			scsi_remove_target(&raid_device->starget->dev);
		}
		ioc_info(ioc, "removing handle(0x%04x), wwid(0x%016llx)\n",
			 raid_device->handle, (u64)raid_device->wwid);
		_scsih_raid_device_remove(ioc, raid_device);
	}
	list_for_each_entry_safe(pcie_device, pcienext, &ioc->pcie_device_list,
		list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}

	/* free ports attached to the sas_host */
	list_for_each_entry_safe(mpt3sas_port, next_port,
	   &ioc->sas_hba.sas_port_list, port_list) {
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt3sas_device_remove_by_sas_address(ioc,
			    mpt3sas_port->remote_identify.sas_address);
		else if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt3sas_expander_remove(ioc,
			    mpt3sas_port->remote_identify.sas_address);
	}

	/* free phys attached to the sas_host */
	if (ioc->sas_hba.num_phys) {
		kfree(ioc->sas_hba.phy);
		ioc->sas_hba.phy = NULL;
		ioc->sas_hba.num_phys = 0;
	}

	mpt3sas_base_detach(ioc);
	spin_lock(&gioc_lock);
	list_del(&ioc->list);
	spin_unlock(&gioc_lock);
	scsi_host_put(shost);
}

/**
 * scsih_shutdown - routine call during system shutdown
 * @pdev: PCI device struct
 */
static void
scsih_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct workqueue_struct	*wq;
	unsigned long flags;
	Mpi2ConfigReply_t mpi_reply;

	ioc->remove_host = 1;

	mpt3sas_wait_for_commands_to_complete(ioc);
	_scsih_flush_running_cmds(ioc);

	_scsih_fw_event_cleanup_queue(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);
	/*
	 * Copy back the unmodified ioc page1 so that on next driver load,
	 * current modified changes on ioc page1 won't take effect.
	 */
	if (ioc->is_aero_ioc)
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply,
				&ioc->ioc_pg1_copy);

	_scsih_ir_shutdown(ioc);
	_scsih_nvme_shutdown(ioc);
	mpt3sas_base_detach(ioc);
}


/**
 * _scsih_probe_boot_devices - reports 1st device
 * @ioc: per adapter object
 *
 * If specified in bios page 2, this routine reports the 1st
 * device scsi-ml or sas transport for persistent boot device
 * purposes.  Please refer to function _scsih_determine_boot_device()
 */
static void
_scsih_probe_boot_devices(struct MPT3SAS_ADAPTER *ioc)
{
	u32 channel;
	void *device;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	u16 handle;
	u64 sas_address_parent;
	u64 sas_address;
	unsigned long flags;
	int rc;
	int tid;

	 /* no Bios, return immediately */
	if (!ioc->bios_pg3.BiosVersion)
		return;

	device = NULL;
	if (ioc->req_boot_device.device) {
		device =  ioc->req_boot_device.device;
		channel = ioc->req_boot_device.channel;
	} else if (ioc->req_alt_boot_device.device) {
		device =  ioc->req_alt_boot_device.device;
		channel = ioc->req_alt_boot_device.channel;
	} else if (ioc->current_boot_device.device) {
		device =  ioc->current_boot_device.device;
		channel = ioc->current_boot_device.channel;
	}

	if (!device)
		return;

	if (channel == RAID_CHANNEL) {
		raid_device = device;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else if (channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = device;
		tid = pcie_device->id;
		list_move_tail(&pcie_device->list, &ioc->pcie_device_list);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		rc = scsi_add_device(ioc->shost, PCIE_CHANNEL, tid, 0);
		if (rc)
			_scsih_pcie_device_remove(ioc, pcie_device);
	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = device;
		handle = sas_device->handle;
		sas_address_parent = sas_device->sas_address_parent;
		sas_address = sas_device->sas_address;
		list_move_tail(&sas_device->list, &ioc->sas_device_list);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

		if (ioc->hide_drives)
			return;
		if (!mpt3sas_transport_port_add(ioc, handle,
		    sas_address_parent)) {
			_scsih_sas_device_remove(ioc, sas_device);
		} else if (!sas_device->starget) {
			if (!ioc->is_driver_loading) {
				mpt3sas_transport_port_remove(ioc,
				    sas_address,
				    sas_address_parent);
				_scsih_sas_device_remove(ioc, sas_device);
			}
		}
	}
}

/**
 * _scsih_probe_raid - reporting raid volumes to scsi-ml
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_raid(struct MPT3SAS_ADAPTER *ioc)
{
	struct _raid_device *raid_device, *raid_next;
	int rc;

	list_for_each_entry_safe(raid_device, raid_next,
	    &ioc->raid_device_list, list) {
		if (raid_device->starget)
			continue;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	}
}

static struct _sas_device *get_next_sas_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	if (!list_empty(&ioc->sas_device_init_list)) {
		sas_device = list_first_entry(&ioc->sas_device_init_list,
				struct _sas_device, list);
		sas_device_get(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

static void sas_device_make_active(struct MPT3SAS_ADAPTER *ioc,
		struct _sas_device *sas_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);

	/*
	 * Since we dropped the lock during the call to port_add(), we need to
	 * be careful here that somebody else didn't move or delete this item
	 * while we were busy with other things.
	 *
	 * If it was on the list, we need a put() for the reference the list
	 * had. Either way, we need a get() for the destination list.
	 */
	if (!list_empty(&sas_device->list)) {
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}

	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_probe_sas - reporting sas devices to sas transport
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_sas(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device;

	if (ioc->hide_drives)
		return;

	while ((sas_device = get_next_sas_device(ioc))) {
		if (!mpt3sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent)) {
			_scsih_sas_device_remove(ioc, sas_device);
			sas_device_put(sas_device);
			continue;
		} else if (!sas_device->starget) {
			/*
			 * When asyn scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()->
			 * sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
				mpt3sas_transport_port_remove(ioc,
				    sas_device->sas_address,
				    sas_device->sas_address_parent);
				_scsih_sas_device_remove(ioc, sas_device);
				sas_device_put(sas_device);
				continue;
			}
		}
		sas_device_make_active(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * get_next_pcie_device - Get the next pcie device
 * @ioc: per adapter object
 *
 * Get the next pcie device from pcie_device_init_list list.
 *
 * Return: pcie device structure if pcie_device_init_list list is not empty
 * otherwise returns NULL
 */
static struct _pcie_device *get_next_pcie_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct _pcie_device *pcie_device = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	if (!list_empty(&ioc->pcie_device_init_list)) {
		pcie_device = list_first_entry(&ioc->pcie_device_init_list,
				struct _pcie_device, list);
		pcie_device_get(pcie_device);
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}

/**
 * pcie_device_make_active - Add pcie device to pcie_device_list list
 * @ioc: per adapter object
 * @pcie_device: pcie device object
 *
 * Add the pcie device which has registered with SCSI Transport Later to
 * pcie_device_list list
 */
static void pcie_device_make_active(struct MPT3SAS_ADAPTER *ioc,
		struct _pcie_device *pcie_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);

	if (!list_empty(&pcie_device->list)) {
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_list);

	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_probe_pcie - reporting PCIe devices to scsi-ml
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_pcie(struct MPT3SAS_ADAPTER *ioc)
{
	struct _pcie_device *pcie_device;
	int rc;

	/* PCIe Device List */
	while ((pcie_device = get_next_pcie_device(ioc))) {
		if (pcie_device->starget) {
			pcie_device_put(pcie_device);
			continue;
		}
		if (pcie_device->access_status ==
		    MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED) {
			pcie_device_make_active(ioc, pcie_device);
			pcie_device_put(pcie_device);
			continue;
		}
		rc = scsi_add_device(ioc->shost, PCIE_CHANNEL,
			pcie_device->id, 0);
		if (rc) {
			_scsih_pcie_device_remove(ioc, pcie_device);
			pcie_device_put(pcie_device);
			continue;
		} else if (!pcie_device->starget) {
			/*
			 * When async scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()->
			 * sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
			/* TODO-- Need to find out whether this condition will
			 * occur or not
			 */
				_scsih_pcie_device_remove(ioc, pcie_device);
				pcie_device_put(pcie_device);
				continue;
			}
		}
		pcie_device_make_active(ioc, pcie_device);
		pcie_device_put(pcie_device);
	}
}

/**
 * _scsih_probe_devices - probing for devices
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_devices(struct MPT3SAS_ADAPTER *ioc)
{
	u16 volume_mapping_flags;

	if (!(ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR))
		return;  /* return when IOC doesn't support initiator mode */

	_scsih_probe_boot_devices(ioc);

	if (ioc->ir_firmware) {
		volume_mapping_flags =
		    le16_to_cpu(ioc->ioc_pg8.IRVolumeMappingFlags) &
		    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
		if (volume_mapping_flags ==
		    MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING) {
			_scsih_probe_raid(ioc);
			_scsih_probe_sas(ioc);
		} else {
			_scsih_probe_sas(ioc);
			_scsih_probe_raid(ioc);
		}
	} else {
		_scsih_probe_sas(ioc);
		_scsih_probe_pcie(ioc);
	}
}

/**
 * scsih_scan_start - scsi lld callback for .scan_start
 * @shost: SCSI host pointer
 *
 * The shost has the ability to discover targets on its own instead
 * of scanning the entire bus.  In our implemention, we will kick off
 * firmware discovery.
 */
static void
scsih_scan_start(struct Scsi_Host *shost)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int rc;
	if (diag_buffer_enable != -1 && diag_buffer_enable != 0)
		mpt3sas_enable_diag_buffer(ioc, diag_buffer_enable);
	else if (ioc->manu_pg11.HostTraceBufferMaxSizeKB != 0)
		mpt3sas_enable_diag_buffer(ioc, 1);

	if (disable_discovery > 0)
		return;

	ioc->start_scan = 1;
	rc = mpt3sas_port_enable(ioc);

	if (rc != 0)
		ioc_info(ioc, "port enable: FAILED\n");
}

/**
 * scsih_scan_finished - scsi lld callback for .scan_finished
 * @shost: SCSI host pointer
 * @time: elapsed time of the scan in jiffies
 *
 * This function will be called periodicallyn until it returns 1 with the
 * scsi_host and the elapsed time of the scan in jiffies. In our implemention,
 * we wait for firmware discovery to complete, then return 1.
 */
static int
scsih_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	if (disable_discovery > 0) {
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		return 1;
	}

	if (time >= (300 * HZ)) {
		ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;
		ioc_info(ioc, "port enable: FAILED with timeout (timeout=300s)\n");
		ioc->is_driver_loading = 0;
		return 1;
	}

	if (ioc->start_scan)
		return 0;

	if (ioc->start_scan_failed) {
		ioc_info(ioc, "port enable: FAILED with (ioc_status=0x%08x)\n",
			 ioc->start_scan_failed);
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		ioc->remove_host = 1;
		return 1;
	}

	ioc_info(ioc, "port enable: SUCCESS\n");
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;

	if (ioc->wait_for_discovery_to_complete) {
		ioc->wait_for_discovery_to_complete = 0;
		_scsih_probe_devices(ioc);
	}
	mpt3sas_base_start_watchdog(ioc);
	ioc->is_driver_loading = 0;
	return 1;
}

/* shost template for SAS 2.0 HBA devices */
static struct scsi_host_template mpt2sas_driver_template = {
	.module				= THIS_MODULE,
	.name				= "Fusion MPT SAS Host",
	.proc_name			= MPT2SAS_DRIVER_NAME,
	.queuecommand			= scsih_qcmd,
	.target_alloc			= scsih_target_alloc,
	.slave_alloc			= scsih_slave_alloc,
	.slave_configure		= scsih_slave_configure,
	.target_destroy			= scsih_target_destroy,
	.slave_destroy			= scsih_slave_destroy,
	.scan_finished			= scsih_scan_finished,
	.scan_start			= scsih_scan_start,
	.change_queue_depth		= scsih_change_queue_depth,
	.eh_abort_handler		= scsih_abort,
	.eh_device_reset_handler	= scsih_dev_reset,
	.eh_target_reset_handler	= scsih_target_reset,
	.eh_host_reset_handler		= scsih_host_reset,
	.bios_param			= scsih_bios_param,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPT2SAS_SG_DEPTH,
	.max_sectors			= 32767,
	.cmd_per_lun			= 7,
	.shost_attrs			= mpt3sas_host_attrs,
	.sdev_attrs			= mpt3sas_dev_attrs,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct scsiio_tracker),
};

/* raid transport support for SAS 2.0 HBA devices */
static struct raid_function_template mpt2sas_raid_functions = {
	.cookie		= &mpt2sas_driver_template,
	.is_raid	= scsih_is_raid,
	.get_resync	= scsih_get_resync,
	.get_state	= scsih_get_state,
};

/* shost template for SAS 3.0 HBA devices */
static struct scsi_host_template mpt3sas_driver_template = {
	.module				= THIS_MODULE,
	.name				= "Fusion MPT SAS Host",
	.proc_name			= MPT3SAS_DRIVER_NAME,
	.queuecommand			= scsih_qcmd,
	.target_alloc			= scsih_target_alloc,
	.slave_alloc			= scsih_slave_alloc,
	.slave_configure		= scsih_slave_configure,
	.target_destroy			= scsih_target_destroy,
	.slave_destroy			= scsih_slave_destroy,
	.scan_finished			= scsih_scan_finished,
	.scan_start			= scsih_scan_start,
	.change_queue_depth		= scsih_change_queue_depth,
	.eh_abort_handler		= scsih_abort,
	.eh_device_reset_handler	= scsih_dev_reset,
	.eh_target_reset_handler	= scsih_target_reset,
	.eh_host_reset_handler		= scsih_host_reset,
	.bios_param			= scsih_bios_param,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPT3SAS_SG_DEPTH,
	.max_sectors			= 32767,
	.max_segment_size		= 0xffffffff,
	.cmd_per_lun			= 7,
	.shost_attrs			= mpt3sas_host_attrs,
	.sdev_attrs			= mpt3sas_dev_attrs,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct scsiio_tracker),
};

/* raid transport support for SAS 3.0 HBA devices */
static struct raid_function_template mpt3sas_raid_functions = {
	.cookie		= &mpt3sas_driver_template,
	.is_raid	= scsih_is_raid,
	.get_resync	= scsih_get_resync,
	.get_state	= scsih_get_state,
};

/**
 * _scsih_determine_hba_mpi_version - determine in which MPI version class
 *					this device belongs to.
 * @pdev: PCI device struct
 *
 * return MPI2_VERSION for SAS 2.0 HBA devices,
 *	MPI25_VERSION for SAS 3.0 HBA devices, and
 *	MPI26 VERSION for Cutlass & Invader SAS 3.0 HBA devices
 */
static u16
_scsih_determine_hba_mpi_version(struct pci_dev *pdev)
{

	switch (pdev->device) {
	case MPI2_MFGPAGE_DEVID_SSS6200:
	case MPI2_MFGPAGE_DEVID_SAS2004:
	case MPI2_MFGPAGE_DEVID_SAS2008:
	case MPI2_MFGPAGE_DEVID_SAS2108_1:
	case MPI2_MFGPAGE_DEVID_SAS2108_2:
	case MPI2_MFGPAGE_DEVID_SAS2108_3:
	case MPI2_MFGPAGE_DEVID_SAS2116_1:
	case MPI2_MFGPAGE_DEVID_SAS2116_2:
	case MPI2_MFGPAGE_DEVID_SAS2208_1:
	case MPI2_MFGPAGE_DEVID_SAS2208_2:
	case MPI2_MFGPAGE_DEVID_SAS2208_3:
	case MPI2_MFGPAGE_DEVID_SAS2208_4:
	case MPI2_MFGPAGE_DEVID_SAS2208_5:
	case MPI2_MFGPAGE_DEVID_SAS2208_6:
	case MPI2_MFGPAGE_DEVID_SAS2308_1:
	case MPI2_MFGPAGE_DEVID_SAS2308_2:
	case MPI2_MFGPAGE_DEVID_SAS2308_3:
	case MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP:
	case MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP_1:
		return MPI2_VERSION;
	case MPI25_MFGPAGE_DEVID_SAS3004:
	case MPI25_MFGPAGE_DEVID_SAS3008:
	case MPI25_MFGPAGE_DEVID_SAS3108_1:
	case MPI25_MFGPAGE_DEVID_SAS3108_2:
	case MPI25_MFGPAGE_DEVID_SAS3108_5:
	case MPI25_MFGPAGE_DEVID_SAS3108_6:
		return MPI25_VERSION;
	case MPI26_MFGPAGE_DEVID_SAS3216:
	case MPI26_MFGPAGE_DEVID_SAS3224:
	case MPI26_MFGPAGE_DEVID_SAS3316_1:
	case MPI26_MFGPAGE_DEVID_SAS3316_2:
	case MPI26_MFGPAGE_DEVID_SAS3316_3:
	case MPI26_MFGPAGE_DEVID_SAS3316_4:
	case MPI26_MFGPAGE_DEVID_SAS3324_1:
	case MPI26_MFGPAGE_DEVID_SAS3324_2:
	case MPI26_MFGPAGE_DEVID_SAS3324_3:
	case MPI26_MFGPAGE_DEVID_SAS3324_4:
	case MPI26_MFGPAGE_DEVID_SAS3508:
	case MPI26_MFGPAGE_DEVID_SAS3508_1:
	case MPI26_MFGPAGE_DEVID_SAS3408:
	case MPI26_MFGPAGE_DEVID_SAS3516:
	case MPI26_MFGPAGE_DEVID_SAS3516_1:
	case MPI26_MFGPAGE_DEVID_SAS3416:
	case MPI26_MFGPAGE_DEVID_SAS3616:
	case MPI26_ATLAS_PCIe_SWITCH_DEVID:
	case MPI26_MFGPAGE_DEVID_CFG_SEC_3916:
	case MPI26_MFGPAGE_DEVID_HARD_SEC_3916:
	case MPI26_MFGPAGE_DEVID_CFG_SEC_3816:
	case MPI26_MFGPAGE_DEVID_HARD_SEC_3816:
		return MPI26_VERSION;
	}
	return 0;
}

/**
 * _scsih_probe - attach and add scsi host
 * @pdev: PCI device struct
 * @id: pci device id
 *
 * Return: 0 success, anything else error.
 */
static int
_scsih_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct MPT3SAS_ADAPTER *ioc;
	struct Scsi_Host *shost = NULL;
	int rv;
	u16 hba_mpi_version;

	/* Determine in which MPI version class this pci device belongs */
	hba_mpi_version = _scsih_determine_hba_mpi_version(pdev);
	if (hba_mpi_version == 0)
		return -ENODEV;

	/* Enumerate only SAS 2.0 HBA's if hbas_to_enumerate is one,
	 * for other generation HBA's return with -ENODEV
	 */
	if ((hbas_to_enumerate == 1) && (hba_mpi_version !=  MPI2_VERSION))
		return -ENODEV;

	/* Enumerate only SAS 3.0 HBA's if hbas_to_enumerate is two,
	 * for other generation HBA's return with -ENODEV
	 */
	if ((hbas_to_enumerate == 2) && (!(hba_mpi_version ==  MPI25_VERSION
		|| hba_mpi_version ==  MPI26_VERSION)))
		return -ENODEV;

	switch (hba_mpi_version) {
	case MPI2_VERSION:
		pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S |
			PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM);
		/* Use mpt2sas driver host template for SAS 2.0 HBA's */
		shost = scsi_host_alloc(&mpt2sas_driver_template,
		  sizeof(struct MPT3SAS_ADAPTER));
		if (!shost)
			return -ENODEV;
		ioc = shost_priv(shost);
		memset(ioc, 0, sizeof(struct MPT3SAS_ADAPTER));
		ioc->hba_mpi_version_belonged = hba_mpi_version;
		ioc->id = mpt2_ids++;
		sprintf(ioc->driver_name, "%s", MPT2SAS_DRIVER_NAME);
		switch (pdev->device) {
		case MPI2_MFGPAGE_DEVID_SSS6200:
			ioc->is_warpdrive = 1;
			ioc->hide_ir_msg = 1;
			break;
		case MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP:
		case MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP_1:
			ioc->is_mcpu_endpoint = 1;
			break;
		default:
			ioc->mfg_pg10_hide_flag = MFG_PAGE10_EXPOSE_ALL_DISKS;
			break;
		}
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		/* Use mpt3sas driver host template for SAS 3.0 HBA's */
		shost = scsi_host_alloc(&mpt3sas_driver_template,
		  sizeof(struct MPT3SAS_ADAPTER));
		if (!shost)
			return -ENODEV;
		ioc = shost_priv(shost);
		memset(ioc, 0, sizeof(struct MPT3SAS_ADAPTER));
		ioc->hba_mpi_version_belonged = hba_mpi_version;
		ioc->id = mpt3_ids++;
		sprintf(ioc->driver_name, "%s", MPT3SAS_DRIVER_NAME);
		switch (pdev->device) {
		case MPI26_MFGPAGE_DEVID_SAS3508:
		case MPI26_MFGPAGE_DEVID_SAS3508_1:
		case MPI26_MFGPAGE_DEVID_SAS3408:
		case MPI26_MFGPAGE_DEVID_SAS3516:
		case MPI26_MFGPAGE_DEVID_SAS3516_1:
		case MPI26_MFGPAGE_DEVID_SAS3416:
		case MPI26_MFGPAGE_DEVID_SAS3616:
		case MPI26_ATLAS_PCIe_SWITCH_DEVID:
			ioc->is_gen35_ioc = 1;
			break;
		case MPI26_MFGPAGE_DEVID_CFG_SEC_3816:
		case MPI26_MFGPAGE_DEVID_CFG_SEC_3916:
			dev_info(&pdev->dev,
			    "HBA is in Configurable Secure mode\n");
			/* fall through */
		case MPI26_MFGPAGE_DEVID_HARD_SEC_3816:
		case MPI26_MFGPAGE_DEVID_HARD_SEC_3916:
			ioc->is_aero_ioc = ioc->is_gen35_ioc = 1;
			break;
		default:
			ioc->is_gen35_ioc = ioc->is_aero_ioc = 0;
		}
		if ((ioc->hba_mpi_version_belonged == MPI25_VERSION &&
			pdev->revision >= SAS3_PCI_DEVICE_C0_REVISION) ||
			(ioc->hba_mpi_version_belonged == MPI26_VERSION)) {
			ioc->combined_reply_queue = 1;
			if (ioc->is_gen35_ioc)
				ioc->combined_reply_index_count =
				 MPT3_SUP_REPLY_POST_HOST_INDEX_REG_COUNT_G35;
			else
				ioc->combined_reply_index_count =
				 MPT3_SUP_REPLY_POST_HOST_INDEX_REG_COUNT_G3;
		}
		break;
	default:
		return -ENODEV;
	}

	INIT_LIST_HEAD(&ioc->list);
	spin_lock(&gioc_lock);
	list_add_tail(&ioc->list, &mpt3sas_ioc_list);
	spin_unlock(&gioc_lock);
	ioc->shost = shost;
	ioc->pdev = pdev;
	ioc->scsi_io_cb_idx = scsi_io_cb_idx;
	ioc->tm_cb_idx = tm_cb_idx;
	ioc->ctl_cb_idx = ctl_cb_idx;
	ioc->base_cb_idx = base_cb_idx;
	ioc->port_enable_cb_idx = port_enable_cb_idx;
	ioc->transport_cb_idx = transport_cb_idx;
	ioc->scsih_cb_idx = scsih_cb_idx;
	ioc->config_cb_idx = config_cb_idx;
	ioc->tm_tr_cb_idx = tm_tr_cb_idx;
	ioc->tm_tr_volume_cb_idx = tm_tr_volume_cb_idx;
	ioc->tm_sas_control_cb_idx = tm_sas_control_cb_idx;
	ioc->logging_level = logging_level;
	ioc->schedule_dead_ioc_flush_running_cmds = &_scsih_flush_running_cmds;
	/* Host waits for minimum of six seconds */
	ioc->max_shutdown_latency = IO_UNIT_CONTROL_SHUTDOWN_TIMEOUT;
	/*
	 * Enable MEMORY MOVE support flag.
	 */
	ioc->drv_support_bitmap |= MPT_DRV_SUPPORT_BITMAP_MEMMOVE;

	ioc->enable_sdev_max_qd = enable_sdev_max_qd;

	/* misc semaphores and spin locks */
	mutex_init(&ioc->reset_in_progress_mutex);
	/* initializing pci_access_mutex lock */
	mutex_init(&ioc->pci_access_mutex);
	spin_lock_init(&ioc->ioc_reset_in_progress_lock);
	spin_lock_init(&ioc->scsi_lookup_lock);
	spin_lock_init(&ioc->sas_device_lock);
	spin_lock_init(&ioc->sas_node_lock);
	spin_lock_init(&ioc->fw_event_lock);
	spin_lock_init(&ioc->raid_device_lock);
	spin_lock_init(&ioc->pcie_device_lock);
	spin_lock_init(&ioc->diag_trigger_lock);

	INIT_LIST_HEAD(&ioc->sas_device_list);
	INIT_LIST_HEAD(&ioc->sas_device_init_list);
	INIT_LIST_HEAD(&ioc->sas_expander_list);
	INIT_LIST_HEAD(&ioc->enclosure_list);
	INIT_LIST_HEAD(&ioc->pcie_device_list);
	INIT_LIST_HEAD(&ioc->pcie_device_init_list);
	INIT_LIST_HEAD(&ioc->fw_event_list);
	INIT_LIST_HEAD(&ioc->raid_device_list);
	INIT_LIST_HEAD(&ioc->sas_hba.sas_port_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_list);
	INIT_LIST_HEAD(&ioc->delayed_sc_list);
	INIT_LIST_HEAD(&ioc->delayed_event_ack_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_volume_list);
	INIT_LIST_HEAD(&ioc->reply_queue_list);

	sprintf(ioc->name, "%s_cm%d", ioc->driver_name, ioc->id);

	/* init shost parameters */
	shost->max_cmd_len = 32;
	shost->max_lun = max_lun;
	shost->transportt = mpt3sas_transport_template;
	shost->unique_id = ioc->id;

	if (ioc->is_mcpu_endpoint) {
		/* mCPU MPI support 64K max IO */
		shost->max_sectors = 128;
		ioc_info(ioc, "The max_sectors value is set to %d\n",
			 shost->max_sectors);
	} else {
		if (max_sectors != 0xFFFF) {
			if (max_sectors < 64) {
				shost->max_sectors = 64;
				ioc_warn(ioc, "Invalid value %d passed for max_sectors, range is 64 to 32767. Assigning value of 64.\n",
					 max_sectors);
			} else if (max_sectors > 32767) {
				shost->max_sectors = 32767;
				ioc_warn(ioc, "Invalid value %d passed for max_sectors, range is 64 to 32767.Assigning default value of 32767.\n",
					 max_sectors);
			} else {
				shost->max_sectors = max_sectors & 0xFFFE;
				ioc_info(ioc, "The max_sectors value is set to %d\n",
					 shost->max_sectors);
			}
		}
	}
	/* register EEDP capabilities with SCSI layer */
	if (prot_mask > 0)
		scsi_host_set_prot(shost, prot_mask);
	else
		scsi_host_set_prot(shost, SHOST_DIF_TYPE1_PROTECTION
				   | SHOST_DIF_TYPE2_PROTECTION
				   | SHOST_DIF_TYPE3_PROTECTION);

	scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);

	/* event thread */
	snprintf(ioc->firmware_event_name, sizeof(ioc->firmware_event_name),
	    "fw_event_%s%d", ioc->driver_name, ioc->id);
	ioc->firmware_event_thread = alloc_ordered_workqueue(
	    ioc->firmware_event_name, 0);
	if (!ioc->firmware_event_thread) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		rv = -ENODEV;
		goto out_thread_fail;
	}

	ioc->is_driver_loading = 1;
	if ((mpt3sas_base_attach(ioc))) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		rv = -ENODEV;
		goto out_attach_fail;
	}

	if (ioc->is_warpdrive) {
		if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_EXPOSE_ALL_DISKS)
			ioc->hide_drives = 0;
		else if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_HIDE_ALL_DISKS)
			ioc->hide_drives = 1;
		else {
			if (mpt3sas_get_num_volumes(ioc))
				ioc->hide_drives = 1;
			else
				ioc->hide_drives = 0;
		}
	} else
		ioc->hide_drives = 0;

	rv = scsi_add_host(shost, &pdev->dev);
	if (rv) {
		ioc_err(ioc, "failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);
		goto out_add_shost_fail;
	}

	scsi_scan_host(shost);
	return 0;
out_add_shost_fail:
	mpt3sas_base_detach(ioc);
 out_attach_fail:
	destroy_workqueue(ioc->firmware_event_thread);
 out_thread_fail:
	spin_lock(&gioc_lock);
	list_del(&ioc->list);
	spin_unlock(&gioc_lock);
	scsi_host_put(shost);
	return rv;
}

#ifdef CONFIG_PM
/**
 * scsih_suspend - power management suspend main entry point
 * @pdev: PCI device struct
 * @state: PM state change to (usually PCI_D3)
 *
 * Return: 0 success, anything else error.
 */
static int
scsih_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	pci_power_t device_state;

	mpt3sas_base_stop_watchdog(ioc);
	flush_scheduled_work();
	scsi_block_requests(shost);
	_scsih_nvme_shutdown(ioc);
	device_state = pci_choose_state(pdev, state);
	ioc_info(ioc, "pdev=0x%p, slot=%s, entering operating state [D%d]\n",
		 pdev, pci_name(pdev), device_state);

	pci_save_state(pdev);
	mpt3sas_base_free_resources(ioc);
	pci_set_power_state(pdev, device_state);
	return 0;
}

/**
 * scsih_resume - power management resume main entry point
 * @pdev: PCI device struct
 *
 * Return: 0 success, anything else error.
 */
static int
scsih_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	pci_power_t device_state = pdev->current_state;
	int r;

	ioc_info(ioc, "pdev=0x%p, slot=%s, previous operating state [D%d]\n",
		 pdev, pci_name(pdev), device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	ioc->pdev = pdev;
	r = mpt3sas_base_map_resources(ioc);
	if (r)
		return r;
	ioc_info(ioc, "Issuing Hard Reset as part of OS Resume\n");
	mpt3sas_base_hard_reset_handler(ioc, SOFT_RESET);
	scsi_unblock_requests(shost);
	mpt3sas_base_start_watchdog(ioc);
	return 0;
}
#endif /* CONFIG_PM */

/**
 * scsih_pci_error_detected - Called when a PCI error is detected.
 * @pdev: PCI device struct
 * @state: PCI channel state
 *
 * Description: Called when a PCI error is detected.
 *
 * Return: PCI_ERS_RESULT_NEED_RESET or PCI_ERS_RESULT_DISCONNECT.
 */
static pci_ers_result_t
scsih_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	ioc_info(ioc, "PCI error: detected callback, state(%d)!!\n", state);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		/* Fatal error, prepare for slot reset */
		ioc->pci_error_recovery = 1;
		scsi_block_requests(ioc->shost);
		mpt3sas_base_stop_watchdog(ioc);
		mpt3sas_base_free_resources(ioc);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		/* Permanent error, prepare for device removal */
		ioc->pci_error_recovery = 1;
		mpt3sas_base_stop_watchdog(ioc);
		_scsih_flush_running_cmds(ioc);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * scsih_pci_slot_reset - Called when PCI slot has been reset.
 * @pdev: PCI device struct
 *
 * Description: This routine is called by the pci error recovery
 * code after the PCI slot has been reset, just before we
 * should resume normal operations.
 */
static pci_ers_result_t
scsih_pci_slot_reset(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int rc;

	ioc_info(ioc, "PCI error: slot reset callback!!\n");

	ioc->pci_error_recovery = 0;
	ioc->pdev = pdev;
	pci_restore_state(pdev);
	rc = mpt3sas_base_map_resources(ioc);
	if (rc)
		return PCI_ERS_RESULT_DISCONNECT;

	ioc_info(ioc, "Issuing Hard Reset as part of PCI Slot Reset\n");
	rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);

	ioc_warn(ioc, "hard reset: %s\n",
		 (rc == 0) ? "success" : "failed");

	if (!rc)
		return PCI_ERS_RESULT_RECOVERED;
	else
		return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * scsih_pci_resume() - resume normal ops after PCI reset
 * @pdev: pointer to PCI device
 *
 * Called when the error recovery driver tells us that its
 * OK to resume normal operation. Use completion to allow
 * halted scsi ops to resume.
 */
static void
scsih_pci_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	ioc_info(ioc, "PCI error: resume callback!!\n");

	mpt3sas_base_start_watchdog(ioc);
	scsi_unblock_requests(ioc->shost);
}

/**
 * scsih_pci_mmio_enabled - Enable MMIO and dump debug registers
 * @pdev: pointer to PCI device
 */
static pci_ers_result_t
scsih_pci_mmio_enabled(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	ioc_info(ioc, "PCI error: mmio enabled callback!!\n");

	/* TODO - dump whatever for debugging purposes */

	/* This called only if scsih_pci_error_detected returns
	 * PCI_ERS_RESULT_CAN_RECOVER. Read/write to the device still
	 * works, no need to reset slot.
	 */
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * scsih__ncq_prio_supp - Check for NCQ command priority support
 * @sdev: scsi device struct
 *
 * This is called when a user indicates they would like to enable
 * ncq command priorities. This works only on SATA devices.
 */
bool scsih_ncq_prio_supp(struct scsi_device *sdev)
{
	unsigned char *buf;
	bool ncq_prio_supp = false;

	if (!scsi_device_supports_vpd(sdev))
		return ncq_prio_supp;

	buf = kmalloc(SCSI_VPD_PG_LEN, GFP_KERNEL);
	if (!buf)
		return ncq_prio_supp;

	if (!scsi_get_vpd_page(sdev, 0x89, buf, SCSI_VPD_PG_LEN))
		ncq_prio_supp = (buf[213] >> 4) & 1;

	kfree(buf);
	return ncq_prio_supp;
}
/*
 * The pci device ids are defined in mpi/mpi2_cnfg.h.
 */
static const struct pci_device_id mpt3sas_pci_table[] = {
	/* Spitfire ~ 2004 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Falcon ~ 2008 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2008,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Liberator ~ 2108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_3,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Meteor ~ 2116 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_2,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Thunderbolt ~ 2208 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_6,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Mustang ~ 2308 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SWITCH_MPI_EP_1,
		PCI_ANY_ID, PCI_ANY_ID },
	/* SSS6200 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SSS6200,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Fury ~ 3004 and 3008 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3004,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3008,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Invader ~ 3108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_6,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Cutlass ~ 3216 and 3224 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3216,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3224,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Intruder ~ 3316 and 3324 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_4,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Ventura, Crusader, Harpoon & Tomcat ~ 3516, 3416, 3508 & 3408*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3508,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3508_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3408,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3516,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3516_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3416,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Mercator ~ 3616*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3616,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Aero SI 0x00E1 Configurable Secure
	 * 0x00E2 Hard Secure
	 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_CFG_SEC_3916,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_HARD_SEC_3916,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Atlas PCIe Switch Management Port */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_ATLAS_PCIe_SWITCH_DEVID,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Sea SI 0x00E5 Configurable Secure
	 * 0x00E6 Hard Secure
	 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_CFG_SEC_3816,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_HARD_SEC_3816,
		PCI_ANY_ID, PCI_ANY_ID },

	{0}     /* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mpt3sas_pci_table);

static struct pci_error_handlers _mpt3sas_err_handler = {
	.error_detected	= scsih_pci_error_detected,
	.mmio_enabled	= scsih_pci_mmio_enabled,
	.slot_reset	= scsih_pci_slot_reset,
	.resume		= scsih_pci_resume,
};

static struct pci_driver mpt3sas_driver = {
	.name		= MPT3SAS_DRIVER_NAME,
	.id_table	= mpt3sas_pci_table,
	.probe		= _scsih_probe,
	.remove		= scsih_remove,
	.shutdown	= scsih_shutdown,
	.err_handler	= &_mpt3sas_err_handler,
#ifdef CONFIG_PM
	.suspend	= scsih_suspend,
	.resume		= scsih_resume,
#endif
};

/**
 * scsih_init - main entry point for this driver.
 *
 * Return: 0 success, anything else error.
 */
static int
scsih_init(void)
{
	mpt2_ids = 0;
	mpt3_ids = 0;

	mpt3sas_base_initialize_callback_handler();

	 /* queuecommand callback hander */
	scsi_io_cb_idx = mpt3sas_base_register_callback_handler(_scsih_io_done);

	/* task management callback handler */
	tm_cb_idx = mpt3sas_base_register_callback_handler(_scsih_tm_done);

	/* base internal commands callback handler */
	base_cb_idx = mpt3sas_base_register_callback_handler(mpt3sas_base_done);
	port_enable_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_port_enable_done);

	/* transport internal commands callback handler */
	transport_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_transport_done);

	/* scsih internal commands callback handler */
	scsih_cb_idx = mpt3sas_base_register_callback_handler(_scsih_done);

	/* configuration page API internal commands callback handler */
	config_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_config_done);

	/* ctl module callback handler */
	ctl_cb_idx = mpt3sas_base_register_callback_handler(mpt3sas_ctl_done);

	tm_tr_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_tm_tr_complete);

	tm_tr_volume_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_tm_volume_tr_complete);

	tm_sas_control_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_sas_control_complete);

	return 0;
}

/**
 * scsih_exit - exit point for this driver (when it is a module).
 *
 * Return: 0 success, anything else error.
 */
static void
scsih_exit(void)
{

	mpt3sas_base_release_callback_handler(scsi_io_cb_idx);
	mpt3sas_base_release_callback_handler(tm_cb_idx);
	mpt3sas_base_release_callback_handler(base_cb_idx);
	mpt3sas_base_release_callback_handler(port_enable_cb_idx);
	mpt3sas_base_release_callback_handler(transport_cb_idx);
	mpt3sas_base_release_callback_handler(scsih_cb_idx);
	mpt3sas_base_release_callback_handler(config_cb_idx);
	mpt3sas_base_release_callback_handler(ctl_cb_idx);

	mpt3sas_base_release_callback_handler(tm_tr_cb_idx);
	mpt3sas_base_release_callback_handler(tm_tr_volume_cb_idx);
	mpt3sas_base_release_callback_handler(tm_sas_control_cb_idx);

/* raid transport support */
	if (hbas_to_enumerate != 1)
		raid_class_release(mpt3sas_raid_template);
	if (hbas_to_enumerate != 2)
		raid_class_release(mpt2sas_raid_template);
	sas_release_transport(mpt3sas_transport_template);
}

/**
 * _mpt3sas_init - main entry point for this driver.
 *
 * Return: 0 success, anything else error.
 */
static int __init
_mpt3sas_init(void)
{
	int error;

	pr_info("%s version %s loaded\n", MPT3SAS_DRIVER_NAME,
					MPT3SAS_DRIVER_VERSION);

	mpt3sas_transport_template =
	    sas_attach_transport(&mpt3sas_transport_functions);
	if (!mpt3sas_transport_template)
		return -ENODEV;

	/* No need attach mpt3sas raid functions template
	 * if hbas_to_enumarate value is one.
	 */
	if (hbas_to_enumerate != 1) {
		mpt3sas_raid_template =
				raid_class_attach(&mpt3sas_raid_functions);
		if (!mpt3sas_raid_template) {
			sas_release_transport(mpt3sas_transport_template);
			return -ENODEV;
		}
	}

	/* No need to attach mpt2sas raid functions template
	 * if hbas_to_enumarate value is two
	 */
	if (hbas_to_enumerate != 2) {
		mpt2sas_raid_template =
				raid_class_attach(&mpt2sas_raid_functions);
		if (!mpt2sas_raid_template) {
			sas_release_transport(mpt3sas_transport_template);
			return -ENODEV;
		}
	}

	error = scsih_init();
	if (error) {
		scsih_exit();
		return error;
	}

	mpt3sas_ctl_init(hbas_to_enumerate);

	error = pci_register_driver(&mpt3sas_driver);
	if (error)
		scsih_exit();

	return error;
}

/**
 * _mpt3sas_exit - exit point for this driver (when it is a module).
 *
 */
static void __exit
_mpt3sas_exit(void)
{
	pr_info("mpt3sas version %s unloading\n",
				MPT3SAS_DRIVER_VERSION);

	mpt3sas_ctl_exit(hbas_to_enumerate);

	pci_unregister_driver(&mpt3sas_driver);

	scsih_exit();
}

module_init(_mpt3sas_init);
module_exit(_mpt3sas_exit);

/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_scsih.c
 * Copyright (C) 2007-2012  LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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
#include <linux/slab.h>

#include "mpt2sas_base.h"

MODULE_AUTHOR(MPT2SAS_AUTHOR);
MODULE_DESCRIPTION(MPT2SAS_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(MPT2SAS_DRIVER_VERSION);

#define RAID_CHANNEL 1

/* forward proto's */
static void _scsih_expander_node_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander);
static void _firmware_event_work(struct work_struct *work);

static u8 _scsih_check_for_pending_tm(struct MPT2SAS_ADAPTER *ioc, u16 smid);

static void _scsih_scan_start(struct Scsi_Host *shost);
static int _scsih_scan_finished(struct Scsi_Host *shost, unsigned long time);

/* global parameters */
LIST_HEAD(mpt2sas_ioc_list);

/* local parameters */
static u8 scsi_io_cb_idx = -1;
static u8 tm_cb_idx = -1;
static u8 ctl_cb_idx = -1;
static u8 base_cb_idx = -1;
static u8 port_enable_cb_idx = -1;
static u8 transport_cb_idx = -1;
static u8 scsih_cb_idx = -1;
static u8 config_cb_idx = -1;
static int mpt_ids;

static u8 tm_tr_cb_idx = -1 ;
static u8 tm_tr_volume_cb_idx = -1 ;
static u8 tm_sas_control_cb_idx = -1;

/* command line options */
static u32 logging_level;
MODULE_PARM_DESC(logging_level, " bits for enabling additional logging info "
    "(default=0)");

static ushort max_sectors = 0xFFFF;
module_param(max_sectors, ushort, 0);
MODULE_PARM_DESC(max_sectors, "max sectors, range 64 to 32767  default=32767");

static int missing_delay[2] = {-1, -1};
module_param_array(missing_delay, int, NULL, 0);
MODULE_PARM_DESC(missing_delay, " device missing delay , io missing delay");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPT2SAS_MAX_LUN (16895)
static int max_lun = MPT2SAS_MAX_LUN;
module_param(max_lun, int, 0);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

/* diag_buffer_enable is bitwise
 * bit 0 set = TRACE
 * bit 1 set = SNAPSHOT
 * bit 2 set = EXTENDED
 *
 * Either bit can be set, or both
 */
static int diag_buffer_enable = -1;
module_param(diag_buffer_enable, int, 0);
MODULE_PARM_DESC(diag_buffer_enable, " post diag buffers "
	"(TRACE=1/SNAPSHOT=2/EXTENDED=4/default=0)");

static int disable_discovery = -1;
module_param(disable_discovery, int, 0);
MODULE_PARM_DESC(disable_discovery, " disable discovery ");

/* permit overriding the host protection capabilities mask (EEDP/T10 PI) */
static int prot_mask = 0;
module_param(prot_mask, int, 0);
MODULE_PARM_DESC(prot_mask, " host protection capabilities mask, def=7 ");

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


#define MPT2SAS_TURN_ON_FAULT_LED (0xFFFC)
#define MPT2SAS_PORT_ENABLE_COMPLETE (0xFFFD)
#define MPT2SAS_REMOVE_UNRESPONDING_DEVICES (0xFFFF)
/**
 * struct fw_event_work - firmware event struct
 * @list: link list framework
 * @work: work object (ioc->fault_reset_work_q)
 * @cancel_pending_work: flag set during reset handling
 * @ioc: per adapter object
 * @device_handle: device handle
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @ignore: flag meaning this event has been marked to ignore
 * @event: firmware event MPI2_EVENT_XXX defined in mpt2_ioc.h
 * @event_data: reply event data payload follows
 *
 * This object stored on ioc->fw_event_list.
 */
struct fw_event_work {
	struct list_head 	list;
	u8			cancel_pending_work;
	struct delayed_work	delayed_work;
	struct MPT2SAS_ADAPTER *ioc;
	u16			device_handle;
	u8			VF_ID;
	u8			VP_ID;
	u8			ignore;
	u16			event;
	void			*event_data;
};

/* raid transport support */
static struct raid_template *mpt2sas_raid_template;

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
	u8 	sense[SCSI_SENSE_BUFFERSIZE];
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

/*
 * The pci device ids are defined in mpi/mpi2_cnfg.h.
 */
static struct pci_device_id scsih_pci_table[] = {
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Falcon ~ 2008*/
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
	/* SSS6200 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SSS6200,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, scsih_pci_table);

/**
 * _scsih_set_debug_level - global setting of ioc->logging_level.
 *
 * Note: The logging levels are defined in mpt2sas_debug.h.
 */
static int
_scsih_set_debug_level(const char *val, struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT2SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	printk(KERN_INFO "setting logging_level(0x%08x)\n", logging_level);
	list_for_each_entry(ioc, &mpt2sas_ioc_list, list)
		ioc->logging_level = logging_level;
	return 0;
}
module_param_call(logging_level, _scsih_set_debug_level, param_get_int,
    &logging_level, 0644);

/**
 * _scsih_srch_boot_sas_address - search based on sas_address
 * @sas_address: sas address
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
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
 * Returns 1 when there's a match, 0 means no match.
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
 * Returns 1 when there's a match, 0 means no match.
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
 * @slot_number: slot number
 * @form: specifies boot device form
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
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
 * @handle: device handle
 * @sas_address: sas address
 *
 * Returns 0 success, non-zero when failure
 */
static int
_scsih_get_sas_address(struct MPT2SAS_ADAPTER *ioc, u16 handle,
    u64 *sas_address)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 ioc_status;
	*sas_address = 0;

	if (handle <= ioc->sas_hba.num_phys) {
		*sas_address = ioc->sas_hba.sas_address;
		return 0;
	}

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		__FILE__, __LINE__, __func__);
		return -ENXIO;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		*sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
		return 0;
	}

	/* we hit this becuase the given parent handle doesn't exist */
	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return -ENXIO;
	/* else error case */
	printk(MPT2SAS_ERR_FMT "handle(0x%04x), ioc_status(0x%04x), "
	    "failure at %s:%d/%s()!\n", ioc->name, handle, ioc_status,
	     __FILE__, __LINE__, __func__);
	return -EIO;
}

/**
 * _scsih_determine_boot_device - determine boot device.
 * @ioc: per adapter object
 * @device: either sas_device or raid_device object
 * @is_raid: [flag] 1 = raid object, 0 = sas object
 *
 * Determines whether this device should be first reported device to
 * to scsi-ml or sas transport, this purpose is for persistent boot device.
 * There are primary, alternate, and current entries in bios page 2. The order
 * priority is primary, alternate, then current.  This routine saves
 * the corresponding device object and is_raid flag in the ioc object.
 * The saved data to be used later in _scsih_probe_boot_devices().
 */
static void
_scsih_determine_boot_device(struct MPT2SAS_ADAPTER *ioc,
    void *device, u8 is_raid)
{
	struct _sas_device *sas_device;
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

	if (!is_raid) {
		sas_device = device;
		sas_address = sas_device->sas_address;
		device_name = sas_device->device_name;
		enclosure_logical_id = sas_device->enclosure_logical_id;
		slot = sas_device->slot;
	} else {
		raid_device = device;
		sas_address = raid_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	}

	if (!ioc->req_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_INFO_FMT
			   "%s: req_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_boot_device.device = device;
			ioc->req_boot_device.is_raid = is_raid;
		}
	}

	if (!ioc->req_alt_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqAltBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedAltBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_INFO_FMT
			   "%s: req_alt_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_alt_boot_device.device = device;
			ioc->req_alt_boot_device.is_raid = is_raid;
		}
	}

	if (!ioc->current_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.CurrentBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.CurrentBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_INFO_FMT
			   "%s: current_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->current_boot_device.device = device;
			ioc->current_boot_device.is_raid = is_raid;
		}
	}
}

/**
 * mpt2sas_scsih_sas_device_find_by_sas_address - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
struct _sas_device *
mpt2sas_scsih_sas_device_find_by_sas_address(struct MPT2SAS_ADAPTER *ioc,
    u64 sas_address)
{
	struct _sas_device *sas_device;

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->sas_address == sas_address)
			return sas_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->sas_address == sas_address)
			return sas_device;

	return NULL;
}

/**
 * _scsih_sas_device_find_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
static struct _sas_device *
_scsih_sas_device_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->handle == handle)
			return sas_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->handle == handle)
			return sas_device;

	return NULL;
}

/**
 * _scsih_sas_device_remove - remove sas_device from list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Removing object and freeing associated memory from the ioc->sas_device_list.
 */
static void
_scsih_sas_device_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	if (!sas_device)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_del(&sas_device->list);
	kfree(sas_device);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
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
_scsih_sas_device_add(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
	     sas_device->sas_address_parent)) {
		_scsih_sas_device_remove(ioc, sas_device);
	} else if (!sas_device->starget) {
		/* When asyn scanning is enabled, its not possible to remove
		 * devices while scanning is turned on due to an oops in
		 * scsi_sysfs_add_sdev()->add_device()->sysfs_addrm_start()
		 */
		if (!ioc->is_driver_loading)
			mpt2sas_transport_port_remove(ioc,
			sas_device->sas_address,
			sas_device->sas_address_parent);
		_scsih_sas_device_remove(ioc, sas_device);
	}
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
_scsih_sas_device_init_add(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_add_tail(&sas_device->list, &ioc->sas_device_init_list);
	_scsih_determine_boot_device(ioc, sas_device, 0);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
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
_scsih_raid_device_find_by_id(struct MPT2SAS_ADAPTER *ioc, int id, int channel)
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
 * _scsih_raid_device_find_by_handle - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on handle, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
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
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on wwid, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_wwid(struct MPT2SAS_ADAPTER *ioc, u64 wwid)
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
_scsih_raid_device_add(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: handle"
	    "(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
	    raid_device->handle, (unsigned long long)raid_device->wwid));

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
_scsih_raid_device_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_del(&raid_device->list);
	kfree(raid_device);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * mpt2sas_scsih_expander_find_by_handle - expander device search
 * @ioc: per adapter object
 * @handle: expander handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for expander device based on handle, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt2sas_scsih_expander_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
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
 * mpt2sas_scsih_expander_find_by_sas_address - expander device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * This searches for expander device based on sas_address, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt2sas_scsih_expander_find_by_sas_address(struct MPT2SAS_ADAPTER *ioc,
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
 *
 * Return nothing.
 */
static void
_scsih_expander_node_add(struct MPT2SAS_ADAPTER *ioc,
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
 * Returns 1 if end device.
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
 * _scsih_scsi_lookup_get - returns scmd entry
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns the smid stored scmd pointer.
 */
static struct scsi_cmnd *
_scsih_scsi_lookup_get(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->scsi_lookup[smid - 1].scmd;
}

/**
 * _scsih_scsi_lookup_get_clear - returns scmd entry
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns the smid stored scmd pointer.
 * Then will derefrence the stored scmd pointer.
 */
static inline struct scsi_cmnd *
_scsih_scsi_lookup_get_clear(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	unsigned long flags;
	struct scsi_cmnd *scmd;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	scmd = ioc->scsi_lookup[smid - 1].scmd;
	ioc->scsi_lookup[smid - 1].scmd = NULL;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	return scmd;
}

/**
 * _scsih_scsi_lookup_find_by_scmd - scmd lookup
 * @ioc: per adapter object
 * @smid: system request message index
 * @scmd: pointer to scsi command object
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a scmd pointer in the scsi_lookup array,
 * returning the revelent smid.  A returned value of zero means invalid.
 */
static u16
_scsih_scsi_lookup_find_by_scmd(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd
    *scmd)
{
	u16 smid;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	smid = 0;
	for (i = 0; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd == scmd) {
			smid = ioc->scsi_lookup[i].smid;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * _scsih_scsi_lookup_find_by_target - search for matching channel:id
 * @ioc: per adapter object
 * @id: target id
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_target(struct MPT2SAS_ADAPTER *ioc, int id,
    int channel)
{
	u8 found;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	found = 0;
	for (i = 0 ; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd &&
		    (ioc->scsi_lookup[i].scmd->device->id == id &&
		    ioc->scsi_lookup[i].scmd->device->channel == channel)) {
			found = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return found;
}

/**
 * _scsih_scsi_lookup_find_by_lun - search for matching channel:id:lun
 * @ioc: per adapter object
 * @id: target id
 * @lun: lun number
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id:lun in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_lun(struct MPT2SAS_ADAPTER *ioc, int id,
    unsigned int lun, int channel)
{
	u8 found;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	found = 0;
	for (i = 0 ; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd &&
		    (ioc->scsi_lookup[i].scmd->device->id == id &&
		    ioc->scsi_lookup[i].scmd->device->channel == channel &&
		    ioc->scsi_lookup[i].scmd->device->lun == lun)) {
			found = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return found;
}

/**
 * _scsih_get_chain_buffer_tracker - obtain chain tracker
 * @ioc: per adapter object
 * @smid: smid associated to an IO request
 *
 * Returns chain tracker(from ioc->free_chain_list)
 */
static struct chain_tracker *
_scsih_get_chain_buffer_tracker(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	struct chain_tracker *chain_req;
	unsigned long flags;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->free_chain_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		dfailprintk(ioc, printk(MPT2SAS_WARN_FMT "chain buffers not "
			"available\n", ioc->name));
		return NULL;
	}
	chain_req = list_entry(ioc->free_chain_list.next,
	    struct chain_tracker, tracker_list);
	list_del_init(&chain_req->tracker_list);
	list_add_tail(&chain_req->tracker_list,
	    &ioc->scsi_lookup[smid - 1].chain_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return chain_req;
}

/**
 * _scsih_build_scatter_gather - main sg creation routine
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_scsih_build_scatter_gather(struct MPT2SAS_ADAPTER *ioc,
    struct scsi_cmnd *scmd, u16 smid)
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

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);

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
		sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
		" failed: request for %d bytes!\n", scsi_bufflen(scmd));
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
	chain_req = _scsih_get_chain_buffer_tracker(ioc, smid);
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

		chain_req = _scsih_get_chain_buffer_tracker(ioc, smid);
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
 * _scsih_adjust_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 *
 * Returns nothing
 */
static void
_scsih_adjust_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost = sdev->host;
	int max_depth;
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;

	max_depth = shost->can_queue;

	/* limit max device queue for SATA to 32 */
	sas_device_priv_data = sdev->hostdata;
	if (!sas_device_priv_data)
		goto not_sata;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data)
		goto not_sata;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))
		goto not_sata;
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   sas_device_priv_data->sas_target->sas_address);
	if (sas_device && sas_device->device_info &
	    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
		max_depth = MPT2SAS_SATA_QUEUE_DEPTH;
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 not_sata:

	if (!sdev->tagged_supported)
		max_depth = 1;
	if (qdepth > max_depth)
		qdepth = max_depth;
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
}

/**
 * _scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 * @reason: SCSI_QDEPTH_DEFAULT/SCSI_QDEPTH_QFULL/SCSI_QDEPTH_RAMP_UP
 * (see include/scsi/scsi_host.h for definition)
 *
 * Returns queue depth.
 */
static int
_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth, int reason)
{
	if (reason == SCSI_QDEPTH_DEFAULT || reason == SCSI_QDEPTH_RAMP_UP)
		_scsih_adjust_queue_depth(sdev, qdepth);
	else if (reason == SCSI_QDEPTH_QFULL)
		scsi_track_queue_full(sdev, qdepth);
	else
		return -EOPNOTSUPP;

	if (sdev->inquiry_len > 7)
		sdev_printk(KERN_INFO, sdev, "qdepth(%d), tagged(%d), "
		"simple(%d), ordered(%d), scsi_level(%d), cmd_que(%d)\n",
		sdev->queue_depth, sdev->tagged_supported, sdev->simple_tags,
		sdev->ordered_tags, sdev->scsi_level,
		(sdev->inquiry[7] & 2) >> 1);

	return sdev->queue_depth;
}

/**
 * _scsih_change_queue_type - changing device queue tag type
 * @sdev: scsi device struct
 * @tag_type: requested tag type
 *
 * Returns queue tag type.
 */
static int
_scsih_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}

/**
 * _scsih_target_alloc - target add routine
 * @starget: scsi target struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	struct sas_rphy *rphy;

	sas_target_priv_data = kzalloc(sizeof(struct scsi_target), GFP_KERNEL);
	if (!sas_target_priv_data)
		return -ENOMEM;

	starget->hostdata = sas_target_priv_data;
	sas_target_priv_data->starget = starget;
	sas_target_priv_data->handle = MPT2SAS_INVALID_DEVICE_HANDLE;

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

	/* sas/sata devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   rphy->identify.sas_address);

	if (sas_device) {
		sas_target_priv_data->handle = sas_device->handle;
		sas_target_priv_data->sas_address = sas_device->sas_address;
		sas_device->starget = starget;
		sas_device->id = starget->id;
		sas_device->channel = starget->channel;
		if (test_bit(sas_device->handle, ioc->pd_handles))
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return 0;
}

/**
 * _scsih_target_destroy - target destroy routine
 * @starget: scsi target struct
 *
 * Returns nothing.
 */
static void
_scsih_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	struct sas_rphy *rphy;

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

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   rphy->identify.sas_address);
	if (sas_device && (sas_device->starget == starget) &&
	    (sas_device->id == starget->id) &&
	    (sas_device->channel == starget->channel))
		sas_device->starget = NULL;

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 out:
	kfree(sas_target_priv_data);
	starget->hostdata = NULL;
}

/**
 * _scsih_slave_alloc - device add routine
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct MPT2SAS_ADAPTER *ioc;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	unsigned long flags;

	sas_device_priv_data = kzalloc(sizeof(struct scsi_device), GFP_KERNEL);
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

	return 0;
}

/**
 * _scsih_slave_destroy - device destroy routine
 * @sdev: scsi device struct
 *
 * Returns nothing.
 */
static void
_scsih_slave_destroy(struct scsi_device *sdev)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct Scsi_Host *shost;
	struct MPT2SAS_ADAPTER *ioc;
	struct _sas_device *sas_device;
	unsigned long flags;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns--;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_priv(shost);

	if (!(sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
		   sas_target_priv_data->sas_address);
		if (sas_device && !sas_target_priv_data->num_luns)
			sas_device->starget = NULL;
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
_scsih_display_sata_capabilities(struct MPT2SAS_ADAPTER *ioc,
	u16 handle, struct scsi_device *sdev)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u16 flags;
	u32 device_info;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
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

/**
 * _scsih_is_raid - return boolean indicating device is raid volume
 * @dev the device struct object
 */
static int
_scsih_is_raid(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(sdev->host);

	if (ioc->is_warpdrive)
		return 0;
	return (sdev->channel == RAID_CHANNEL) ? 1 : 0;
}

/**
 * _scsih_get_resync - get raid volume resync percent complete
 * @dev the device struct object
 */
static void
_scsih_get_resync(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(sdev->host);
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

	if (mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		percent_complete = 0;
		goto out;
	}

	volume_status_flags = le32_to_cpu(vol_pg0.VolumeStatusFlags);
	if (!(volume_status_flags &
	    MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS))
		percent_complete = 0;

 out:
	raid_set_resync(mpt2sas_raid_template, dev, percent_complete);
}

/**
 * _scsih_get_state - get raid volume level
 * @dev the device struct object
 */
static void
_scsih_get_state(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(sdev->host);
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

	if (mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
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
	raid_set_state(mpt2sas_raid_template, dev, state);
}

/**
 * _scsih_set_level - set raid level
 * @sdev: scsi device struct
 * @volume_type: volume type
 */
static void
_scsih_set_level(struct scsi_device *sdev, u8 volume_type)
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

	raid_set_level(mpt2sas_raid_template, &sdev->sdev_gendev, level);
}

/**
 * _scsih_get_volume_capabilities - volume capabilities
 * @ioc: per adapter object
 * @sas_device: the raid_device object
 *
 * Returns 0 for success, else 1
 */
static int
_scsih_get_volume_capabilities(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	Mpi2RaidVolPage0_t *vol_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 sz;
	u8 num_pds;

	if ((mpt2sas_config_get_number_pds(ioc, raid_device->handle,
	    &num_pds)) || !num_pds) {
		dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		return 1;
	}

	raid_device->num_pds = num_pds;
	sz = offsetof(Mpi2RaidVolPage0_t, PhysDisk) + (num_pds *
	    sizeof(Mpi2RaidVol0PhysDisk_t));
	vol_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!vol_pg0) {
		dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		return 1;
	}

	if ((mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply, vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, raid_device->handle, sz))) {
		dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		kfree(vol_pg0);
		return 1;
	}

	raid_device->volume_type = vol_pg0->VolumeType;

	/* figure out what the underlying devices are by
	 * obtaining the device_info bits for the 1st device
	 */
	if (!(mpt2sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM,
	    vol_pg0->PhysDisk[0].PhysDiskNum))) {
		if (!(mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
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
 * _scsih_disable_ddio - Disable direct I/O for all the volumes
 * @ioc: per adapter object
 */
static void
_scsih_disable_ddio(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2RaidVolPage1_t vol_pg1;
	Mpi2ConfigReply_t mpi_reply;
	struct _raid_device *raid_device;
	u16 handle;
	u16 ioc_status;
	unsigned long flags;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &vol_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		handle = le16_to_cpu(vol_pg1.DevHandle);
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
		if (raid_device)
			raid_device->direct_io_enabled = 0;
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
	return;
}


/**
 * _scsih_get_num_volumes - Get number of volumes in the ioc
 * @ioc: per adapter object
 */
static u8
_scsih_get_num_volumes(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2RaidVolPage1_t vol_pg1;
	Mpi2ConfigReply_t mpi_reply;
	u16 handle;
	u8 vol_cnt = 0;
	u16 ioc_status;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &vol_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		vol_cnt++;
		handle = le16_to_cpu(vol_pg1.DevHandle);
	}
	return vol_cnt;
}


/**
 * _scsih_init_warpdrive_properties - Set properties for warpdrive direct I/O.
 * @ioc: per adapter object
 * @raid_device: the raid_device object
 */
static void
_scsih_init_warpdrive_properties(struct MPT2SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	Mpi2RaidVolPage0_t *vol_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 sz;
	u8 num_pds, count;
	unsigned long stripe_sz, block_sz;
	u8 stripe_exp, block_exp;
	u64 dev_max_lba;

	if (!ioc->is_warpdrive)
		return;

	if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_EXPOSE_ALL_DISKS) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "globally as drives are exposed\n", ioc->name);
		return;
	}
	if (_scsih_get_num_volumes(ioc) > 1) {
		_scsih_disable_ddio(ioc);
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "globally as number of drives > 1\n", ioc->name);
		return;
	}
	if ((mpt2sas_config_get_number_pds(ioc, raid_device->handle,
	    &num_pds)) || !num_pds) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "Failure in computing number of drives\n", ioc->name);
		return;
	}

	sz = offsetof(Mpi2RaidVolPage0_t, PhysDisk) + (num_pds *
	    sizeof(Mpi2RaidVol0PhysDisk_t));
	vol_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!vol_pg0) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "Memory allocation failure for RVPG0\n", ioc->name);
		return;
	}

	if ((mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply, vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, raid_device->handle, sz))) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "Failure in retrieving RVPG0\n", ioc->name);
		kfree(vol_pg0);
		return;
	}

	/*
	 * WARPDRIVE:If number of physical disks in a volume exceeds the max pds
	 * assumed for WARPDRIVE, disable direct I/O
	 */
	if (num_pds > MPT_MAX_WARPDRIVE_PDS) {
		printk(MPT2SAS_WARN_FMT "WarpDrive : Direct IO is disabled "
		    "for the drive with handle(0x%04x): num_mem=%d, "
		    "max_mem_allowed=%d\n", ioc->name, raid_device->handle,
		    num_pds, MPT_MAX_WARPDRIVE_PDS);
		kfree(vol_pg0);
		return;
	}
	for (count = 0; count < num_pds; count++) {
		if (mpt2sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
		    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM,
		    vol_pg0->PhysDisk[count].PhysDiskNum) ||
		     le16_to_cpu(pd_pg0.DevHandle) ==
		    MPT2SAS_INVALID_DEVICE_HANDLE) {
			printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is "
			    "disabled for the drive with handle(0x%04x) member"
			    "handle retrieval failed for member number=%d\n",
			    ioc->name, raid_device->handle,
			    vol_pg0->PhysDisk[count].PhysDiskNum);
			goto out_error;
		}
		/* Disable direct I/O if member drive lba exceeds 4 bytes */
		dev_max_lba = le64_to_cpu(pd_pg0.DeviceMaxLBA);
		if (dev_max_lba >> 32) {
			printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is "
			    "disabled for the drive with handle(0x%04x) member"
			    "handle (0x%04x) unsupported max lba 0x%016llx\n",
			    ioc->name, raid_device->handle,
			    le16_to_cpu(pd_pg0.DevHandle),
			    (unsigned long long)dev_max_lba);
			goto out_error;
		}

		raid_device->pd_handle[count] = le16_to_cpu(pd_pg0.DevHandle);
	}

	/*
	 * Assumption for WD: Direct I/O is not supported if the volume is
	 * not RAID0
	 */
	if (raid_device->volume_type != MPI2_RAID_VOL_TYPE_RAID0) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "for the drive with handle(0x%04x): type=%d, "
		    "s_sz=%uK, blk_size=%u\n", ioc->name,
		    raid_device->handle, raid_device->volume_type,
		    (le32_to_cpu(vol_pg0->StripeSize) *
		    le16_to_cpu(vol_pg0->BlockSize)) / 1024,
		    le16_to_cpu(vol_pg0->BlockSize));
		goto out_error;
	}

	stripe_sz = le32_to_cpu(vol_pg0->StripeSize);
	stripe_exp = find_first_bit(&stripe_sz, 32);
	if (stripe_exp == 32) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		"for the drive with handle(0x%04x) invalid stripe sz %uK\n",
		    ioc->name, raid_device->handle,
		    (le32_to_cpu(vol_pg0->StripeSize) *
		    le16_to_cpu(vol_pg0->BlockSize)) / 1024);
		goto out_error;
	}
	raid_device->stripe_exponent = stripe_exp;
	block_sz = le16_to_cpu(vol_pg0->BlockSize);
	block_exp = find_first_bit(&block_sz, 16);
	if (block_exp == 16) {
		printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is disabled "
		    "for the drive with handle(0x%04x) invalid block sz %u\n",
		    ioc->name, raid_device->handle,
		    le16_to_cpu(vol_pg0->BlockSize));
		goto out_error;
	}
	raid_device->block_exponent = block_exp;
	raid_device->direct_io_enabled = 1;

	printk(MPT2SAS_INFO_FMT "WarpDrive : Direct IO is Enabled for the drive"
	    " with handle(0x%04x)\n", ioc->name, raid_device->handle);
	/*
	 * WARPDRIVE: Though the following fields are not used for direct IO,
	 * stored for future purpose:
	 */
	raid_device->max_lba = le64_to_cpu(vol_pg0->MaxLBA);
	raid_device->stripe_sz = le32_to_cpu(vol_pg0->StripeSize);
	raid_device->block_sz = le16_to_cpu(vol_pg0->BlockSize);


	kfree(vol_pg0);
	return;

out_error:
	raid_device->direct_io_enabled = 0;
	for (count = 0; count < num_pds; count++)
		raid_device->pd_handle[count] = 0;
	kfree(vol_pg0);
	return;
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
_scsih_enable_tlr(struct MPT2SAS_ADAPTER *ioc, struct scsi_device *sdev)
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
 * _scsih_slave_configure - device configure routine.
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
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
		raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (!raid_device) {
			dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name, __FILE__,
			    __LINE__, __func__));
			return 1;
		}

		if (_scsih_get_volume_capabilities(ioc, raid_device)) {
			dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name, __FILE__,
			    __LINE__, __func__));
			return 1;
		}
		/*
		 * WARPDRIVE: Initialize the required data for Direct IO
		 */
		_scsih_init_warpdrive_properties(ioc, raid_device);

		/* RAID Queue Depth Support
		 * IS volume = underlying qdepth of drive type, either
		 *    MPT2SAS_SAS_QUEUE_DEPTH or MPT2SAS_SATA_QUEUE_DEPTH
		 * IM/IME/R10 = 128 (MPT2SAS_RAID_QUEUE_DEPTH)
		 */
		if (raid_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
			qdepth = MPT2SAS_SAS_QUEUE_DEPTH;
			ds = "SSP";
		} else {
			qdepth = MPT2SAS_SATA_QUEUE_DEPTH;
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
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			if (ioc->manu_pg10.OEMIdentifier &&
			    (le32_to_cpu(ioc->manu_pg10.GenericFlags0) &
			    MFG10_GF0_R10_DISPLAY) &&
			    !(raid_device->num_pds % 2))
				r_level = "RAID10";
			else
				r_level = "RAID1E";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID1";
			break;
		case MPI2_RAID_VOL_TYPE_RAID10:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID10";
			break;
		case MPI2_RAID_VOL_TYPE_UNKNOWN:
		default:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAIDX";
			break;
		}

		if (!ioc->hide_ir_msg)
			sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), "
			    "wwid(0x%016llx), pd_count(%d), type(%s)\n",
			    r_level, raid_device->handle,
			    (unsigned long long)raid_device->wwid,
			    raid_device->num_pds, ds);
		_scsih_change_queue_depth(sdev, qdepth, SCSI_QDEPTH_DEFAULT);
		/* raid transport support */
		if (!ioc->is_warpdrive)
			_scsih_set_level(sdev, raid_device->volume_type);
		return 0;
	}

	/* non-raid handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) {
		if (mpt2sas_config_get_volume_handle(ioc, handle,
		    &volume_handle)) {
			dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__));
			return 1;
		}
		if (volume_handle && mpt2sas_config_get_volume_wwid(ioc,
		    volume_handle, &volume_wwid)) {
			dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__));
			return 1;
		}
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   sas_device_priv_data->sas_target->sas_address);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		dfailprintk(ioc, printk(MPT2SAS_WARN_FMT
			"failure at %s:%d/%s()!\n", ioc->name, __FILE__,
			__LINE__, __func__));
		return 1;
	}
	sas_device->volume_handle = volume_handle;
	sas_device->volume_wwid = volume_wwid;
	if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
		qdepth = MPT2SAS_SAS_QUEUE_DEPTH;
		ssp_target = 1;
		ds = "SSP";
	} else {
		qdepth = MPT2SAS_SATA_QUEUE_DEPTH;
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET)
			ds = "STP";
		else if (sas_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "SATA";
	}
	sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), "
	    "sas_addr(0x%016llx), phy(%d), device_name(0x%016llx)\n",
	    ds, sas_device->handle,
	    (unsigned long long)sas_device->sas_address,
	    sas_device->phy,
	    (unsigned long long)sas_device->device_name);
	sdev_printk(KERN_INFO, sdev, "%s: "
	    "enclosure_logical_id(0x%016llx), slot(%d)\n", ds,
	    (unsigned long long) sas_device->enclosure_logical_id,
	    sas_device->slot);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!ssp_target)
		_scsih_display_sata_capabilities(ioc, handle, sdev);


	_scsih_change_queue_depth(sdev, qdepth, SCSI_QDEPTH_DEFAULT);

	if (ssp_target) {
		sas_read_port_mode_page(sdev);
		_scsih_enable_tlr(ioc, sdev);
	}
	return 0;
}

/**
 * _scsih_bios_param - fetch head, sector, cylinder info for a disk
 * @sdev: scsi device struct
 * @bdev: pointer to block device context
 * @capacity: device size (in 512 byte sectors)
 * @params: three element array to place output:
 *              params[0] number of heads (max 255)
 *              params[1] number of sectors (max 63)
 *              params[2] number of cylinders
 *
 * Return nothing.
 */
static int
_scsih_bios_param(struct scsi_device *sdev, struct block_device *bdev,
    sector_t capacity, int params[])
{
	int		heads;
	int		sectors;
	sector_t	cylinders;
	ulong 		dummy;

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
 *
 * Return nothing.
 */
static void
_scsih_response_code(struct MPT2SAS_ADAPTER *ioc, u8 response_code)
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
	printk(MPT2SAS_WARN_FMT "response_code(0x%01x): %s\n",
		ioc->name, response_code, desc);
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
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->tm_cmds.status == MPT2_CMD_NOT_USED)
		return 1;
	if (ioc->tm_cmds.smid != smid)
		return 1;
	mpt2sas_base_flush_reply_queues(ioc);
	ioc->tm_cmds.status |= MPT2_CMD_COMPLETE;
	mpi_reply =  mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		memcpy(ioc->tm_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
		ioc->tm_cmds.status |= MPT2_CMD_REPLY_VALID;
	}
	ioc->tm_cmds.status &= ~MPT2_CMD_PENDING;
	complete(&ioc->tm_cmds.done);
	return 1;
}

/**
 * mpt2sas_scsih_set_tm_flag - set per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt2sas_scsih_set_tm_flag(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
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
 * mpt2sas_scsih_clear_tm_flag - clear per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt2sas_scsih_clear_tm_flag(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
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
 * mpt2sas_scsih_issue_tm - main routine for sending tm requests
 * @ioc: per adapter struct
 * @device_handle: device handle
 * @channel: the channel assigned by the OS
 * @id: the id assigned by the OS
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 * @timeout: timeout in seconds
 * @serial_number: the serial_number from scmd
 * @m_type: TM_MUTEX_ON or TM_MUTEX_OFF
 * Context: user
 *
 * A generic API for sending task management requests to firmware.
 *
 * The callback index is set inside `ioc->tm_cb_idx`.
 *
 * Return SUCCESS or FAILED.
 */
int
mpt2sas_scsih_issue_tm(struct MPT2SAS_ADAPTER *ioc, u16 handle, uint channel,
    uint id, uint lun, u8 type, u16 smid_task, ulong timeout,
	unsigned long serial_number, enum mutex_type m_type)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	u16 smid = 0;
	u32 ioc_state;
	unsigned long timeleft;
	struct scsiio_tracker *scsi_lookup = NULL;
	int rc;

	if (m_type == TM_MUTEX_ON)
		mutex_lock(&ioc->tm_cmds.mutex);
	if (ioc->tm_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_INFO_FMT "%s: tm_cmd busy!!!\n",
		    __func__, ioc->name);
		rc = FAILED;
		goto err_out;
	}

	if (ioc->shost_recovery || ioc->remove_host ||
	    ioc->pci_error_recovery) {
		printk(MPT2SAS_INFO_FMT "%s: host reset in progress!\n",
		    __func__, ioc->name);
		rc = FAILED;
		goto err_out;
	}

	ioc_state = mpt2sas_base_get_iocstate(ioc, 0);
	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "unexpected doorbell "
		    "active!\n", ioc->name));
		rc = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
		rc = (!rc) ? SUCCESS : FAILED;
		goto err_out;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt2sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
		rc = (!rc) ? SUCCESS : FAILED;
		goto err_out;
	}

	smid = mpt2sas_base_get_smid_hpr(ioc, ioc->tm_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = FAILED;
		goto err_out;
	}

	if (type == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
		scsi_lookup = &ioc->scsi_lookup[smid_task - 1];

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "sending tm: handle(0x%04x),"
	    " task_type(0x%02x), smid(%d)\n", ioc->name, handle, type,
	    smid_task));
	ioc->tm_cmds.status = MPT2_CMD_PENDING;
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->tm_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	memset(ioc->tm_cmds.reply, 0, sizeof(Mpi2SCSITaskManagementReply_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = type;
	mpi_request->TaskMID = cpu_to_le16(smid_task);
	int_to_scsilun(lun, (struct scsi_lun *)mpi_request->LUN);
	mpt2sas_scsih_set_tm_flag(ioc, handle);
	init_completion(&ioc->tm_cmds.done);
	mpt2sas_base_put_smid_hi_priority(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->tm_cmds.done, timeout*HZ);
	if (!(ioc->tm_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SCSITaskManagementRequest_t)/4);
		if (!(ioc->tm_cmds.status & MPT2_CMD_RESET)) {
			rc = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
			    FORCE_BIG_HAMMER);
			rc = (!rc) ? SUCCESS : FAILED;
			ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
			mpt2sas_scsih_clear_tm_flag(ioc, handle);
			goto err_out;
		}
	}

	if (ioc->tm_cmds.status & MPT2_CMD_REPLY_VALID) {
		mpi_reply = ioc->tm_cmds.reply;
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "complete tm: "
		    "ioc_status(0x%04x), loginfo(0x%08x), term_count(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo),
		    le32_to_cpu(mpi_reply->TerminationCount)));
		if (ioc->logging_level & MPT_DEBUG_TM) {
			_scsih_response_code(ioc, mpi_reply->ResponseCode);
			if (mpi_reply->IOCStatus)
				_debug_dump_mf(mpi_request,
				    sizeof(Mpi2SCSITaskManagementRequest_t)/4);
		}
	}

	switch (type) {
	case MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		rc = SUCCESS;
		if (scsi_lookup->scmd == NULL)
			break;
		rc = FAILED;
		break;

	case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		if (_scsih_scsi_lookup_find_by_target(ioc, id, channel))
			rc = FAILED;
		else
			rc = SUCCESS;
		break;

	case MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		if (_scsih_scsi_lookup_find_by_lun(ioc, id, lun, channel))
			rc = FAILED;
		else
			rc = SUCCESS;
		break;
	case MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK:
		rc = SUCCESS;
		break;
	default:
		rc = FAILED;
		break;
	}

	mpt2sas_scsih_clear_tm_flag(ioc, handle);
	ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
	if (m_type == TM_MUTEX_ON)
		mutex_unlock(&ioc->tm_cmds.mutex);

	return rc;

 err_out:
	if (m_type == TM_MUTEX_ON)
		mutex_unlock(&ioc->tm_cmds.mutex);
	return rc;
}

/**
 * _scsih_tm_display_info - displays info about the device
 * @ioc: per adapter struct
 * @scmd: pointer to scsi command object
 *
 * Called by task management callback handlers.
 */
static void
_scsih_tm_display_info(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd *scmd)
{
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT2SAS_TARGET *priv_target = starget->hostdata;
	struct _sas_device *sas_device = NULL;
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
		starget_printk(KERN_INFO, starget, "%s handle(0x%04x), "
		    "%s wwid(0x%016llx)\n", device_str, priv_target->handle,
		    device_str, (unsigned long long)priv_target->sas_address);
	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
		    priv_target->sas_address);
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
			starget_printk(KERN_INFO, starget,
			    "enclosure_logical_id(0x%016llx), slot(%d)\n",
			   (unsigned long long)sas_device->enclosure_logical_id,
			    sas_device->slot);
		}
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}
}

/**
 * _scsih_abort - eh threads main abort routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_abort(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u16 smid;
	u16 handle;
	int r;

	sdev_printk(KERN_INFO, scmd->device, "attempting task abort! "
	    "scmd(%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		sdev_printk(KERN_INFO, scmd->device, "device been deleted! "
		    "scmd(%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* search for the command */
	smid = _scsih_scsi_lookup_find_by_scmd(ioc, scmd);
	if (!smid) {
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

	mpt2sas_halt_firmware(ioc);

	handle = sas_device_priv_data->sas_target->handle;
	r = mpt2sas_scsih_issue_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, scmd->device->lun,
	    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, smid, 30,
	    scmd->serial_number, TM_MUTEX_ON);

 out:
	sdev_printk(KERN_INFO, scmd->device, "task abort: %s scmd(%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_dev_reset - eh threads main device reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_dev_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;
	u16	handle;
	int r;

	struct scsi_target *starget = scmd->device->sdev_target;

	starget_printk(KERN_INFO, starget, "attempting device reset! "
	    "scmd(%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		starget_printk(KERN_INFO, starget, "device been deleted! "
		    "scmd(%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc,
		   sas_device_priv_data->sas_target->handle);
		if (sas_device)
			handle = sas_device->volume_handle;
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	r = mpt2sas_scsih_issue_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, scmd->device->lun,
	    MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, 0, 30, 0,
	    TM_MUTEX_ON);

 out:
	sdev_printk(KERN_INFO, scmd->device, "device reset: %s scmd(%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_target_reset - eh threads main target reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_target_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;
	u16	handle;
	int r;
	struct scsi_target *starget = scmd->device->sdev_target;

	starget_printk(KERN_INFO, starget, "attempting target reset! "
	    "scmd(%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		starget_printk(KERN_INFO, starget, "target been deleted! "
		    "scmd(%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc,
		   sas_device_priv_data->sas_target->handle);
		if (sas_device)
			handle = sas_device->volume_handle;
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	r = mpt2sas_scsih_issue_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, 0, MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0,
	    30, 0, TM_MUTEX_ON);

 out:
	starget_printk(KERN_INFO, starget, "target reset: %s scmd(%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_host_reset - eh threads main host reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_host_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	int r, retval;

	printk(MPT2SAS_INFO_FMT "attempting host reset! scmd(%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	retval = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
	    FORCE_BIG_HAMMER);
	r = (retval < 0) ? FAILED : SUCCESS;
	printk(MPT2SAS_INFO_FMT "host reset: %s scmd(%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

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
 *
 * Return nothing.
 */
static void
_scsih_fw_event_add(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	unsigned long flags;

	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
	INIT_DELAYED_WORK(&fw_event->delayed_work, _firmware_event_work);
	queue_delayed_work(ioc->firmware_event_thread,
	    &fw_event->delayed_work, 0);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_free - delete fw_event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * This removes firmware event object from link list, frees associated memory.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_free(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work
    *fw_event)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_del(&fw_event->list);
	kfree(fw_event->event_data);
	kfree(fw_event);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}


/**
 * _scsih_error_recovery_delete_devices - remove devices not responding
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_error_recovery_delete_devices(struct MPT2SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	if (ioc->is_driver_loading)
		return;

	fw_event = kzalloc(sizeof(struct fw_event_work), GFP_ATOMIC);
	if (!fw_event)
		return;

	fw_event->event = MPT2SAS_REMOVE_UNRESPONDING_DEVICES;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
}

/**
 * mpt2sas_port_enable_complete - port enable completed (fake event)
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt2sas_port_enable_complete(struct MPT2SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(struct fw_event_work), GFP_ATOMIC);
	if (!fw_event)
		return;
	fw_event->event = MPT2SAS_PORT_ENABLE_COMPLETE;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
}

/**
 * _scsih_fw_event_cleanup_queue - cleanup event queue
 * @ioc: per adapter object
 *
 * Walk the firmware event queue, either killing timers, or waiting
 * for outstanding events to complete
 *
 * Return nothing.
 */
static void
_scsih_fw_event_cleanup_queue(struct MPT2SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event, *next;

	if (list_empty(&ioc->fw_event_list) ||
	     !ioc->firmware_event_thread || in_interrupt())
		return;

	list_for_each_entry_safe(fw_event, next, &ioc->fw_event_list, list) {
		if (cancel_delayed_work(&fw_event->delayed_work)) {
			_scsih_fw_event_free(ioc, fw_event);
			continue;
		}
		fw_event->cancel_pending_work = 1;
	}
}

/**
 * _scsih_ublock_io_all_device - unblock every device
 * @ioc: per adapter object
 *
 * change the device state from block to running
 */
static void
_scsih_ublock_io_all_device(struct MPT2SAS_ADAPTER *ioc)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (!sas_device_priv_data->block)
			continue;
		sas_device_priv_data->block = 0;
		dewtprintk(ioc, sdev_printk(KERN_INFO, sdev, "device_running, "
		    "handle(0x%04x)\n",
		    sas_device_priv_data->sas_target->handle));
		scsi_internal_device_unblock(sdev, SDEV_RUNNING);
	}
}
/**
 * _scsih_ublock_io_device - set the device state to SDEV_RUNNING
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_ublock_io_device(struct MPT2SAS_ADAPTER *ioc, u64 sas_address)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (!sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->sas_target->sas_address ==
								sas_address) {
			dewtprintk(ioc, sdev_printk(KERN_INFO, sdev,
			    MPT2SAS_INFO_FMT "SDEV_RUNNING: "
			    "sas address(0x%016llx)\n", ioc->name,
				(unsigned long long)sas_address));
			sas_device_priv_data->block = 0;
			scsi_internal_device_unblock(sdev, SDEV_RUNNING);
		}
	}
}

/**
 * _scsih_block_io_all_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_block_io_all_device(struct MPT2SAS_ADAPTER *ioc)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->block)
			continue;
		sas_device_priv_data->block = 1;
		dewtprintk(ioc, sdev_printk(KERN_INFO, sdev, "device_blocked, "
		    "handle(0x%04x)\n",
		    sas_device_priv_data->sas_target->handle));
		scsi_internal_device_block(sdev);
	}
}


/**
 * _scsih_block_io_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_block_io_device(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			dewtprintk(ioc, sdev_printk(KERN_INFO, sdev,
			    MPT2SAS_INFO_FMT "SDEV_BLOCK: "
			    "handle(0x%04x)\n", ioc->name, handle));
			sas_device_priv_data->block = 1;
			scsi_internal_device_block(sdev);
		}
	}
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
_scsih_block_io_to_children_attached_to_ex(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander)
{
	struct _sas_port *mpt2sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	unsigned long flags;

	if (!sas_expander)
		return;

	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device =
			    mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
			   mpt2sas_port->remote_identify.sas_address);
			if (sas_device)
				set_bit(sas_device->handle,
				    ioc->blocking_handles);
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		}
	}

	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {

		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt2sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE) {
			expander_sibling =
			    mpt2sas_scsih_expander_find_by_sas_address(
			    ioc, mpt2sas_port->remote_identify.sas_address);
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
_scsih_block_io_to_children_attached_directly(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 phy_number;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING)
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
_scsih_tm_tr_send(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _sas_device *sas_device;
	struct MPT2SAS_TARGET *sas_target_priv_data = NULL;
	u64 sas_address = 0;
	unsigned long flags;
	struct _tr_list *delayed_tr;
	u32 ioc_state;

	if (ioc->remove_host) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host has been "
		    "removed: handle(0x%04x)\n", __func__, ioc->name, handle));
		return;
	} else if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host in pci "
		    "error recovery: handle(0x%04x)\n", __func__, ioc->name,
		    handle));
		return;
	}
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host is not "
		   "operational: handle(0x%04x)\n", __func__, ioc->name,
		   handle));
		return;
	}

	/* if PD, then return */
	if (test_bit(handle, ioc->pd_handles))
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	if (sas_device && sas_device->starget &&
	     sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		sas_address = sas_device->sas_address;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (sas_target_priv_data) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "setting delete flag: "
		"handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, handle,
			(unsigned long long)sas_address));
		_scsih_ublock_io_device(ioc, sas_address);
		sas_target_priv_data->handle = MPT2SAS_INVALID_DEVICE_HANDLE;
	}

	smid = mpt2sas_base_get_smid_hpr(ioc, ioc->tm_tr_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_list);
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "DELAYED:tr:handle(0x%04x), (open)\n",
		    ioc->name, handle));
		return;
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "tr_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_tr_cb_idx));
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpt2sas_base_put_smid_hi_priority(ioc, smid);
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
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_sas_control_complete(struct MPT2SAS_ADAPTER *ioc, u16 smid,
    u8 msix_index, u32 reply)
{
	Mpi2SasIoUnitControlReply_t *mpi_reply =
	    mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (likely(mpi_reply)) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		"sc_complete:handle(0x%04x), (open) "
		"smid(%d), ioc_status(0x%04x), loginfo(0x%08x)\n",
		ioc->name, le16_to_cpu(mpi_reply->DevHandle), smid,
		le16_to_cpu(mpi_reply->IOCStatus),
		le32_to_cpu(mpi_reply->IOCLogInfo)));
	} else {
		printk(MPT2SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	}
	return 1;
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
_scsih_tm_tr_volume_send(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _tr_list *delayed_tr;

	if (ioc->shost_recovery || ioc->remove_host ||
	    ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host reset in "
		   "progress!\n", __func__, ioc->name));
		return;
	}

	smid = mpt2sas_base_get_smid_hpr(ioc, ioc->tm_tr_volume_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_volume_list);
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "DELAYED:tr:handle(0x%04x), (open)\n",
		    ioc->name, handle));
		return;
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "tr_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_tr_volume_cb_idx));
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpt2sas_base_put_smid_hi_priority(ioc, smid);
}

/**
 * _scsih_tm_volume_tr_complete - target reset completion
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_volume_tr_complete(struct MPT2SAS_ADAPTER *ioc, u16 smid,
    u8 msix_index, u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt2sas_base_get_reply_virt_addr(ioc, reply);

	if (ioc->shost_recovery || ioc->remove_host ||
	    ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host reset in "
		   "progress!\n", __func__, ioc->name));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		printk(MPT2SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	mpi_request_tm = mpt2sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc, printk("spurious interrupt: "
		    "handle(0x%04x:0x%04x), smid(%d)!!!\n", handle,
		    le16_to_cpu(mpi_reply->DevHandle), smid));
		return 0;
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), "
	    "loginfo(0x%08x), completed(%d)\n", ioc->name,
	    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
	    le32_to_cpu(mpi_reply->IOCLogInfo),
	    le32_to_cpu(mpi_reply->TerminationCount)));

	return _scsih_check_for_pending_tm(ioc, smid);
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
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_tr_complete(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
    u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt2sas_base_get_reply_virt_addr(ioc, reply);
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u16 smid_sas_ctrl;
	u32 ioc_state;

	if (ioc->remove_host) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host has been "
		   "removed\n", __func__, ioc->name));
		return 1;
	} else if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host in pci "
		    "error recovery\n", __func__, ioc->name));
		return 1;
	}
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: host is not "
		    "operational\n", __func__, ioc->name));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		printk(MPT2SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	mpi_request_tm = mpt2sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "spurious interrupt: "
		    "handle(0x%04x:0x%04x), smid(%d)!!!\n", ioc->name, handle,
		    le16_to_cpu(mpi_reply->DevHandle), smid));
		return 0;
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), "
	    "loginfo(0x%08x), completed(%d)\n", ioc->name,
	    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
	    le32_to_cpu(mpi_reply->IOCLogInfo),
	    le32_to_cpu(mpi_reply->TerminationCount)));

	smid_sas_ctrl = mpt2sas_base_get_smid(ioc, ioc->tm_sas_control_cb_idx);
	if (!smid_sas_ctrl) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return 1;
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "sc_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid_sas_ctrl,
	    ioc->tm_sas_control_cb_idx));
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid_sas_ctrl);
	memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	mpi_request->DevHandle = mpi_request_tm->DevHandle;
	mpt2sas_base_put_smid_default(ioc, smid_sas_ctrl);

	return _scsih_check_for_pending_tm(ioc, smid);
}

/**
 * _scsih_check_for_pending_tm - check for pending task management
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * This will check delayed target reset list, and feed the
 * next reqeust.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_check_for_pending_tm(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	struct _tr_list *delayed_tr;

	if (!list_empty(&ioc->delayed_tr_volume_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_volume_list.next,
		    struct _tr_list, list);
		mpt2sas_base_free_smid(ioc, smid);
		_scsih_tm_tr_volume_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	if (!list_empty(&ioc->delayed_tr_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_list.next,
		    struct _tr_list, list);
		mpt2sas_base_free_smid(ioc, smid);
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
 *
 * Return nothing.
 */
static void
_scsih_check_topo_delete_events(struct MPT2SAS_ADAPTER *ioc,
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
		sas_expander = mpt2sas_scsih_expander_find_by_handle(ioc,
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
		local_event_data = fw_event->event_data;
		if (local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->ExpanderDevHandle) ==
			    expander_handle) {
				dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
				    "setting ignoring flag\n", ioc->name));
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
 * This
 * Return nothing.
 */
static void
_scsih_set_volume_delete_flag(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
	if (raid_device && raid_device->starget &&
	    raid_device->starget->hostdata) {
		sas_target_priv_data =
		    raid_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "setting delete flag: handle(0x%04x), "
		    "wwid(0x%016llx)\n", ioc->name, handle,
		    (unsigned long long) raid_device->wwid));
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
 *
 * Return nothing.
 */
static void
_scsih_check_ir_config_unhide_events(struct MPT2SAS_ADAPTER *ioc,
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
			dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
			    "DELAYED:tr:handle(0x%04x), (open)\n", ioc->name,
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
 *
 * Return nothing.
 */
static void
_scsih_check_volume_delete_events(struct MPT2SAS_ADAPTER *ioc,
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
 * _scsih_flush_running_cmds - completing outstanding commands.
 * @ioc: per adapter object
 *
 * The flushing out of all pending scmd commands following host reset,
 * where all IO is dropped to the floor.
 *
 * Return nothing.
 */
static void
_scsih_flush_running_cmds(struct MPT2SAS_ADAPTER *ioc)
{
	struct scsi_cmnd *scmd;
	u16 smid;
	u16 count = 0;

	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		scmd = _scsih_scsi_lookup_get_clear(ioc, smid);
		if (!scmd)
			continue;
		count++;
		mpt2sas_base_free_smid(ioc, smid);
		scsi_dma_unmap(scmd);
		if (ioc->pci_error_recovery)
			scmd->result = DID_NO_CONNECT << 16;
		else
			scmd->result = DID_RESET << 16;
		scmd->scsi_done(scmd);
	}
	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "completing %d cmds\n",
	    ioc->name, count));
}

/**
 * _scsih_setup_eedp - setup MPI request for EEDP transfer
 * @scmd: pointer to scsi command object
 * @mpi_request: pointer to the SCSI_IO reqest message frame
 *
 * Supporting protection 1 and 3.
 *
 * Returns nothing
 */
static void
_scsih_setup_eedp(struct scsi_cmnd *scmd, Mpi2SCSIIORequest_t *mpi_request)
{
	u16 eedp_flags;
	unsigned char prot_op = scsi_get_prot_op(scmd);
	unsigned char prot_type = scsi_get_prot_type(scmd);

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
		    cpu_to_be32(scsi_get_lba(scmd));
		break;

	case SCSI_PROT_DIF_TYPE3:

		/*
		* enable guard checking
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		break;
	}
	mpi_request->EEDPBlockSize = cpu_to_le32(scmd->device->sector_size);
	mpi_request->EEDPFlags = cpu_to_le16(eedp_flags);
}

/**
 * _scsih_eedp_error_handling - return sense code for EEDP errors
 * @scmd: pointer to scsi command object
 * @ioc_status: ioc status
 *
 * Returns nothing
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

	scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST, 0x10, ascq);
	scmd->result = DRIVER_SENSE << 24 | (DID_ABORT << 16) |
	    SAM_STAT_CHECK_CONDITION;
}

/**
 * _scsih_scsi_direct_io_get - returns direct io flag
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns the smid stored scmd pointer.
 */
static inline u8
_scsih_scsi_direct_io_get(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->scsi_lookup[smid - 1].direct_io;
}

/**
 * _scsih_scsi_direct_io_set - sets direct io flag
 * @ioc: per adapter object
 * @smid: system request message index
 * @direct_io: Zero or non-zero value to set in the direct_io flag
 *
 * Returns Nothing.
 */
static inline void
_scsih_scsi_direct_io_set(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 direct_io)
{
	ioc->scsi_lookup[smid - 1].direct_io = direct_io;
}


/**
 * _scsih_setup_direct_io - setup MPI request for WARPDRIVE Direct I/O
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @raid_device: pointer to raid device data structure
 * @mpi_request: pointer to the SCSI_IO reqest message frame
 * @smid: system request message index
 *
 * Returns nothing
 */
static void
_scsih_setup_direct_io(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	struct _raid_device *raid_device, Mpi2SCSIIORequest_t *mpi_request,
	u16 smid)
{
	u32 v_lba, p_lba, stripe_off, stripe_unit, column, io_size;
	u32 stripe_sz, stripe_exp;
	u8 num_pds, *cdb_ptr, i;
	u8 cdb0 = scmd->cmnd[0];
	u64 v_llba;

	/*
	 * Try Direct I/O to RAID memeber disks
	 */
	if (cdb0 == READ_16 || cdb0 == READ_10 ||
	    cdb0 == WRITE_16 || cdb0 == WRITE_10) {
		cdb_ptr = mpi_request->CDB.CDB32;

		if ((cdb0 < READ_16) || !(cdb_ptr[2] | cdb_ptr[3] | cdb_ptr[4]
			| cdb_ptr[5])) {
			io_size = scsi_bufflen(scmd) >>
			    raid_device->block_exponent;
			i = (cdb0 < READ_16) ? 2 : 6;
			/* get virtual lba */
			v_lba = be32_to_cpu(*(__be32 *)(&cdb_ptr[i]));

			if (((u64)v_lba + (u64)io_size - 1) <=
			    (u32)raid_device->max_lba) {
				stripe_sz = raid_device->stripe_sz;
				stripe_exp = raid_device->stripe_exponent;
				stripe_off = v_lba & (stripe_sz - 1);

				/* Check whether IO falls within a stripe */
				if ((stripe_off + io_size) <= stripe_sz) {
					num_pds = raid_device->num_pds;
					p_lba = v_lba >> stripe_exp;
					stripe_unit = p_lba / num_pds;
					column = p_lba % num_pds;
					p_lba = (stripe_unit << stripe_exp) +
					    stripe_off;
					mpi_request->DevHandle =
						cpu_to_le16(raid_device->
						    pd_handle[column]);
					(*(__be32 *)(&cdb_ptr[i])) =
						cpu_to_be32(p_lba);
					/*
					* WD: To indicate this I/O is directI/O
					*/
					_scsih_scsi_direct_io_set(ioc, smid, 1);
				}
			}
		} else {
			io_size = scsi_bufflen(scmd) >>
			    raid_device->block_exponent;
			/* get virtual lba */
			v_llba = be64_to_cpu(*(__be64 *)(&cdb_ptr[2]));

			if ((v_llba + (u64)io_size - 1) <=
			    raid_device->max_lba) {
				stripe_sz = raid_device->stripe_sz;
				stripe_exp = raid_device->stripe_exponent;
				stripe_off = (u32) (v_llba & (stripe_sz - 1));

				/* Check whether IO falls within a stripe */
				if ((stripe_off + io_size) <= stripe_sz) {
					num_pds = raid_device->num_pds;
					p_lba = (u32)(v_llba >> stripe_exp);
					stripe_unit = p_lba / num_pds;
					column = p_lba % num_pds;
					p_lba = (stripe_unit << stripe_exp) +
					    stripe_off;
					mpi_request->DevHandle =
						cpu_to_le16(raid_device->
						    pd_handle[column]);
					(*(__be64 *)(&cdb_ptr[2])) =
					    cpu_to_be64((u64)p_lba);
					/*
					* WD: To indicate this I/O is directI/O
					*/
					_scsih_scsi_direct_io_set(ioc, smid, 1);
				}
			}
		}
	}
}

/**
 * _scsih_qcmd - main scsi request entry point
 * @scmd: pointer to scsi command object
 * @done: function pointer to be invoked on completion
 *
 * The callback index is set inside `ioc->scsi_io_cb_idx`.
 *
 * Returns 0 on success.  If there's a failure, return either:
 * SCSI_MLQUEUE_DEVICE_BUSY if the device queue is full, or
 * SCSI_MLQUEUE_HOST_BUSY if the entire host queue is full
 */
static int
_scsih_qcmd_lck(struct scsi_cmnd *scmd, void (*done)(struct scsi_cmnd *))
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _raid_device *raid_device;
	Mpi2SCSIIORequest_t *mpi_request;
	u32 mpi_control;
	u16 smid;

	scmd->scsi_done = done;
	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	if (ioc->pci_error_recovery || ioc->remove_host) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	sas_target_priv_data = sas_device_priv_data->sas_target;
	/* invalid device handle */
	if (sas_target_priv_data->handle == MPT2SAS_INVALID_DEVICE_HANDLE) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	/* host recovery or link resets sent via IOCTLs */
	if (ioc->shost_recovery || ioc->ioc_link_reset_in_progress)
		return SCSI_MLQUEUE_HOST_BUSY;
	/* device busy with task management */
	else if (sas_device_priv_data->block || sas_target_priv_data->tm_busy)
		return SCSI_MLQUEUE_DEVICE_BUSY;
	/* device has been deleted */
	else if (sas_target_priv_data->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	if (scmd->sc_data_direction == DMA_FROM_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
	else if (scmd->sc_data_direction == DMA_TO_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
	else
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;

	/* set tags */
	if (!(sas_device_priv_data->flags & MPT_DEVICE_FLAGS_INIT)) {
		if (scmd->device->tagged_supported) {
			if (scmd->device->ordered_tags)
				mpi_control |= MPI2_SCSIIO_CONTROL_ORDEREDQ;
			else
				mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
		} else
/* MPI Revision I (UNIT = 0xA) - removed MPI2_SCSIIO_CONTROL_UNTAGGED */
/*			mpi_control |= MPI2_SCSIIO_CONTROL_UNTAGGED;
 */
			mpi_control |= (0x500);

	} else
		mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
	/* Make sure Device is not raid volume.
	 * We do not expose raid functionality to upper layer for warpdrive.
	 */
	if (!ioc->is_warpdrive && !_scsih_is_raid(&scmd->device->sdev_gendev) &&
	    sas_is_tlr_enabled(scmd->device) && scmd->cmd_len != 32)
		mpi_control |= MPI2_SCSIIO_CONTROL_TLR_ON;

	smid = mpt2sas_base_get_smid_scsiio(ioc, ioc->scsi_io_cb_idx, scmd);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		goto out;
	}
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSIIORequest_t));
	_scsih_setup_eedp(scmd, mpi_request);
	if (scmd->cmd_len == 32)
		mpi_control |= 4 << MPI2_SCSIIO_CONTROL_ADDCDBLEN_SHIFT;
	mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT)
		mpi_request->Function = MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	else
		mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle =
	    cpu_to_le16(sas_device_priv_data->sas_target->handle);
	mpi_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	mpi_request->Control = cpu_to_le32(mpi_control);
	mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    mpt2sas_base_get_sense_buffer_dma(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi2SCSIIORequest_t, SGL) / 4;
	mpi_request->SGLFlags = cpu_to_le16(MPI2_SCSIIO_SGLFLAGS_TYPE_MPI +
	    MPI2_SCSIIO_SGLFLAGS_SYSTEM_ADDR);
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	int_to_scsilun(sas_device_priv_data->lun, (struct scsi_lun *)
	    mpi_request->LUN);
	memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);

	if (!mpi_request->DataLength) {
		mpt2sas_base_build_zero_len_sge(ioc, &mpi_request->SGL);
	} else {
		if (_scsih_build_scatter_gather(ioc, scmd, smid)) {
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	raid_device = sas_target_priv_data->raid_device;
	if (raid_device && raid_device->direct_io_enabled)
		_scsih_setup_direct_io(ioc, scmd, raid_device, mpi_request,
		    smid);

	if (likely(mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST))
		mpt2sas_base_put_smid_scsi_io(ioc, smid,
		    le16_to_cpu(mpi_request->DevHandle));
	else
		mpt2sas_base_put_smid_default(ioc, smid);
	return 0;

 out:
	return SCSI_MLQUEUE_HOST_BUSY;
}

static DEF_SCSI_QCMD(_scsih_qcmd)

/**
 * _scsih_normalize_sense - normalize descriptor and fixed format sense data
 * @sense_buffer: sense data returned by target
 * @data: normalized skey/asc/ascq
 *
 * Return nothing.
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

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_scsi_ioc_info - translated non-successful SCSI_IO request
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_reply: reply mf payload returned from firmware
 *
 * scsi_status - SCSI Status code returned from target device
 * scsi_state - state info associated with SCSI_IO determined by ioc
 * ioc_status - ioc supplied status info
 *
 * Return nothing.
 */
static void
_scsih_scsi_ioc_info(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
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
	unsigned long flags;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT2SAS_TARGET *priv_target = starget->hostdata;
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
		printk(MPT2SAS_WARN_FMT "\t%s wwid(0x%016llx)\n", ioc->name,
		    device_str, (unsigned long long)priv_target->sas_address);
	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
		    priv_target->sas_address);
		if (sas_device) {
			printk(MPT2SAS_WARN_FMT "\tsas_address(0x%016llx), "
			    "phy(%d)\n", ioc->name, sas_device->sas_address,
			    sas_device->phy);
			printk(MPT2SAS_WARN_FMT
			    "\tenclosure_logical_id(0x%016llx), slot(%d)\n",
			    ioc->name, sas_device->enclosure_logical_id,
			    sas_device->slot);
		}
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}

	printk(MPT2SAS_WARN_FMT "\thandle(0x%04x), ioc_status(%s)(0x%04x), "
	    "smid(%d)\n", ioc->name, le16_to_cpu(mpi_reply->DevHandle),
	    desc_ioc_state, ioc_status, smid);
	printk(MPT2SAS_WARN_FMT "\trequest_len(%d), underflow(%d), "
	    "resid(%d)\n", ioc->name, scsi_bufflen(scmd), scmd->underflow,
	    scsi_get_resid(scmd));
	printk(MPT2SAS_WARN_FMT "\ttag(%d), transfer_count(%d), "
	    "sc->result(0x%08x)\n", ioc->name, le16_to_cpu(mpi_reply->TaskTag),
	    le32_to_cpu(mpi_reply->TransferCount), scmd->result);
	printk(MPT2SAS_WARN_FMT "\tscsi_status(%s)(0x%02x), "
	    "scsi_state(%s)(0x%02x)\n", ioc->name, desc_scsi_status,
	    scsi_status, desc_scsi_state, scsi_state);

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		printk(MPT2SAS_WARN_FMT "\t[sense_key,asc,ascq]: "
		    "[0x%02x,0x%02x,0x%02x], count(%d)\n", ioc->name, data.skey,
		    data.asc, data.ascq, le32_to_cpu(mpi_reply->SenseCount));
	}

	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
		response_info = le32_to_cpu(mpi_reply->ResponseInfo);
		response_bytes = (u8 *)&response_info;
		_scsih_response_code(ioc, response_bytes[0]);
	}
}
#endif

/**
 * _scsih_turn_on_fault_led - illuminate Fault LED
 * @ioc: per adapter object
 * @handle: device handle
 * Context: process
 *
 * Return nothing.
 */
static void
_scsih_turn_on_fault_led(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;

	memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
	mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
	mpi_request.SlotStatus =
	    cpu_to_le32(MPI2_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT);
	mpi_request.DevHandle = cpu_to_le16(handle);
	mpi_request.Flags = MPI2_SEP_REQ_FLAGS_DEVHANDLE_ADDRESS;
	if ((mpt2sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
	    &mpi_request)) != 0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		__FILE__, __LINE__, __func__);
		return;
	}

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "enclosure_processor: "
		    "ioc_status (0x%04x), loginfo(0x%08x)\n", ioc->name,
		    le16_to_cpu(mpi_reply.IOCStatus),
		    le32_to_cpu(mpi_reply.IOCLogInfo)));
		return;
	}
}

/**
 * _scsih_send_event_to_turn_on_fault_led - fire delayed event
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 *
 * Return nothing.
 */
static void
_scsih_send_event_to_turn_on_fault_led(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(struct fw_event_work), GFP_ATOMIC);
	if (!fw_event)
		return;
	fw_event->event = MPT2SAS_TURN_ON_FAULT_LED;
	fw_event->device_handle = handle;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
}

/**
 * _scsih_smart_predicted_fault - process smart errors
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 *
 * Return nothing.
 */
static void
_scsih_smart_predicted_fault(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct scsi_target *starget;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	Mpi2EventNotificationReply_t *event_reply;
	Mpi2EventDataSasDeviceStatusChange_t *event_data;
	struct _sas_device *sas_device;
	ssize_t sz;
	unsigned long flags;

	/* only handle non-raid devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	starget = sas_device->starget;
	sas_target_priv_data = starget->hostdata;

	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) ||
	   ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	starget_printk(KERN_WARNING, starget, "predicted fault\n");
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM)
		_scsih_send_event_to_turn_on_fault_led(ioc, handle);

	/* insert into event log */
	sz = offsetof(Mpi2EventNotificationReply_t, EventData) +
	     sizeof(Mpi2EventDataSasDeviceStatusChange_t);
	event_reply = kzalloc(sz, GFP_ATOMIC);
	if (!event_reply) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
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
	mpt2sas_ctl_add_to_event_log(ioc, event_reply);
	kfree(event_reply);
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
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_io_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	Mpi2SCSIIORequest_t *mpi_request;
	Mpi2SCSIIOReply_t *mpi_reply;
	struct scsi_cmnd *scmd;
	u16 ioc_status;
	u32 xfer_cnt;
	u8 scsi_state;
	u8 scsi_status;
	u32 log_info;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u32 response_code = 0;
	unsigned long flags;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	scmd = _scsih_scsi_lookup_get_clear(ioc, smid);
	if (scmd == NULL)
		return 1;

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);

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
	if (_scsih_scsi_direct_io_get(ioc, smid) &&
	    ((ioc_status & MPI2_IOCSTATUS_MASK)
	    != MPI2_IOCSTATUS_SCSI_TASK_TERMINATED)) {
		spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
		ioc->scsi_lookup[smid - 1].scmd = scmd;
		_scsih_scsi_direct_io_set(ioc, smid, 0);
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);
		mpi_request->DevHandle =
		    cpu_to_le16(sas_device_priv_data->sas_target->handle);
		mpt2sas_base_put_smid_scsi_io(ioc, smid,
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
	/* Make sure Device is not raid volume.
	 * We do not expose raid functionality to upper layer for warpdrive.
	 */
	if (!ioc->is_warpdrive && !_scsih_is_raid(&scmd->device->sdev_gendev) &&
		sas_is_tlr_enabled(scmd->device) &&
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
		const void *sense_data = mpt2sas_base_get_sense_buffer(ioc,
		    smid);
		u32 sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
		    le32_to_cpu(mpi_reply->SenseCount));
		memcpy(scmd->sense_buffer, sense_data, sz);
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		/* failure prediction threshold exceeded */
		if (data.asc == 0x5D)
			_scsih_smart_predicted_fault(ioc,
			    le16_to_cpu(mpi_reply->DevHandle));
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
	default:
		scmd->result = DID_SOFT_ERROR << 16;
		break;

	}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (scmd->result && (ioc->logging_level & MPT_DEBUG_REPLY))
		_scsih_scsi_ioc_info(ioc , scmd, mpi_reply, smid);
#endif

 out:
	scsi_dma_unmap(scmd);
	scmd->scsi_done(scmd);
	return 1;
}

/**
 * _scsih_sas_host_refresh - refreshing sas host object contents
 * @ioc: per adapter object
 * Context: user
 *
 * During port enable, fw will send topology events for every device. Its
 * possible that the handles may change from the previous setting, so this
 * code keeping handles updating if changed.
 *
 * Return nothing.
 */
static void
_scsih_sas_host_refresh(struct MPT2SAS_ADAPTER *ioc)
{
	u16 sz;
	u16 ioc_status;
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;
	u8 link_rate;

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "updating handles for sas_host(0x%016llx)\n",
	    ioc->name, (unsigned long long)ioc->sas_hba.sas_address));

	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	if ((mpt2sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
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
		mpt2sas_transport_update_links(ioc, ioc->sas_hba.sas_address,
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
 *
 * Return nothing.
 */
static void
_scsih_sas_host_add(struct MPT2SAS_ADAPTER *ioc)
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
	u16 device_missing_delay;

	mpt2sas_config_get_number_hba_phys(ioc, &ioc->sas_hba.num_phys);
	if (!ioc->sas_hba.num_phys) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	/* sas_iounit page 0 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	if ((mpt2sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	/* sas_iounit page 1 */
	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt2sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	ioc->io_missing_delay =
	    le16_to_cpu(sas_iounit_pg1->IODeviceMissingDelay);
	device_missing_delay =
	    le16_to_cpu(sas_iounit_pg1->ReportDeviceMissingDelay);
	if (device_missing_delay & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		ioc->device_missing_delay = (device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		ioc->device_missing_delay = device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

	ioc->sas_hba.parent_dev = &ioc->shost->shost_gendev;
	ioc->sas_hba.phy = kcalloc(ioc->sas_hba.num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!ioc->sas_hba.phy) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		if ((mpt2sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
		    i))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}

		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		ioc->sas_hba.phy[i].phy_id = i;
		mpt2sas_transport_add_host_phy(ioc, &ioc->sas_hba.phy[i],
		    phy_pg0, ioc->sas_hba.parent_dev);
	}
	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, ioc->sas_hba.handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc->sas_hba.enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	ioc->sas_hba.sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	printk(MPT2SAS_INFO_FMT "host_add: handle(0x%04x), "
	    "sas_addr(0x%016llx), phys(%d)\n", ioc->name, ioc->sas_hba.handle,
	    (unsigned long long) ioc->sas_hba.sas_address,
	    ioc->sas_hba.num_phys) ;

	if (ioc->sas_hba.enclosure_handle) {
		if (!(mpt2sas_config_get_enclosure_pg0(ioc, &mpi_reply,
		    &enclosure_pg0,
		   MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
		   ioc->sas_hba.enclosure_handle))) {
			ioc->sas_hba.enclosure_logical_id =
			    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
		}
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
 * Return 0 for success, else error.
 */
static int
_scsih_expander_add(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ExpanderPage1_t expander_pg1;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	u32 ioc_status;
	u16 parent_handle;
	u64 sas_address, sas_address_parent = 0;
	int i;
	unsigned long flags;
	struct _sas_port *mpt2sas_port = NULL;
	int rc = 0;

	if (!handle)
		return -1;

	if (ioc->shost_recovery || ioc->pci_error_recovery)
		return -1;

	if ((mpt2sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_HNDL, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	/* handle out of order topology events */
	parent_handle = le16_to_cpu(expander_pg0.ParentDevHandle);
	if (_scsih_get_sas_address(ioc, parent_handle, &sas_address_parent)
	    != 0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}
	if (sas_address_parent != ioc->sas_hba.sas_address) {
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
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
	sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	if (sas_expander)
		return 0;

	sas_expander = kzalloc(sizeof(struct _sas_node),
	    GFP_KERNEL);
	if (!sas_expander) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_expander->handle = handle;
	sas_expander->num_phys = expander_pg0.NumPhys;
	sas_expander->sas_address_parent = sas_address_parent;
	sas_expander->sas_address = sas_address;

	printk(MPT2SAS_INFO_FMT "expander_add: handle(0x%04x),"
	    " parent(0x%04x), sas_addr(0x%016llx), phys(%d)\n", ioc->name,
	    handle, parent_handle, (unsigned long long)
	    sas_expander->sas_address, sas_expander->num_phys);

	if (!sas_expander->num_phys)
		goto out_fail;
	sas_expander->phy = kcalloc(sas_expander->num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!sas_expander->phy) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}

	INIT_LIST_HEAD(&sas_expander->sas_port_list);
	mpt2sas_port = mpt2sas_transport_port_add(ioc, handle,
	    sas_address_parent);
	if (!mpt2sas_port) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}
	sas_expander->parent_dev = &mpt2sas_port->rphy->dev;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt2sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
		sas_expander->phy[i].handle = handle;
		sas_expander->phy[i].phy_id = i;

		if ((mpt2sas_transport_add_expander_phy(ioc,
		    &sas_expander->phy[i], expander_pg1,
		    sas_expander->parent_dev))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
	}

	if (sas_expander->enclosure_handle) {
		if (!(mpt2sas_config_get_enclosure_pg0(ioc, &mpi_reply,
		    &enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
		   sas_expander->enclosure_handle))) {
			sas_expander->enclosure_logical_id =
			    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
		}
	}

	_scsih_expander_node_add(ioc, sas_expander);
	 return 0;

 out_fail:

	if (mpt2sas_port)
		mpt2sas_transport_port_remove(ioc, sas_expander->sas_address,
		    sas_address_parent);
	kfree(sas_expander);
	return rc;
}

/**
 * _scsih_done -  scsih callback handler.
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when sending internal generated message frames.
 * The callback index passed is `ioc->scsih_cb_idx`
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply =  mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (ioc->scsih_cmds.status == MPT2_CMD_NOT_USED)
		return 1;
	if (ioc->scsih_cmds.smid != smid)
		return 1;
	ioc->scsih_cmds.status |= MPT2_CMD_COMPLETE;
	if (mpi_reply) {
		memcpy(ioc->scsih_cmds.reply, mpi_reply,
		    mpi_reply->MsgLength*4);
		ioc->scsih_cmds.status |= MPT2_CMD_REPLY_VALID;
	}
	ioc->scsih_cmds.status &= ~MPT2_CMD_PENDING;
	complete(&ioc->scsih_cmds.done);
	return 1;
}

/**
 * mpt2sas_expander_remove - removing expander object
 * @ioc: per adapter object
 * @sas_address: expander sas_address
 *
 * Return nothing.
 */
void
mpt2sas_expander_remove(struct MPT2SAS_ADAPTER *ioc, u64 sas_address)
{
	struct _sas_node *sas_expander;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	if (sas_expander)
		list_del(&sas_expander->list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	if (sas_expander)
		_scsih_expander_node_remove(ioc, sas_expander);
}

/**
 * _scsih_check_access_status - check access flags
 * @ioc: per adapter object
 * @sas_address: sas address
 * @handle: sas device handle
 * @access_flags: errors returned during discovery of the device
 *
 * Return 0 for success, else failure
 */
static u8
_scsih_check_access_status(struct MPT2SAS_ADAPTER *ioc, u64 sas_address,
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

	printk(MPT2SAS_ERR_FMT "discovery errors(%s): sas_address(0x%016llx), "
	    "handle(0x%04x)\n", ioc->name, desc,
	    (unsigned long long)sas_address, handle);
	return rc;
}

static void
_scsih_check_device(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	struct _sas_device *sas_device;
	u32 ioc_status;
	unsigned long flags;
	u64 sas_address;
	struct scsi_target *starget;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	u32 device_info;


	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle)))
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return;

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_end_device(device_info)))
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address);

	if (!sas_device) {
		printk(MPT2SAS_ERR_FMT "device is not present "
		    "handle(0x%04x), no sas_device!!!\n", ioc->name, handle);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}

	if (unlikely(sas_device->handle != handle)) {
		starget = sas_device->starget;
		sas_target_priv_data = starget->hostdata;
		starget_printk(KERN_INFO, starget, "handle changed from(0x%04x)"
		   " to (0x%04x)!!!\n", sas_device->handle, handle);
		sas_target_priv_data->handle = handle;
		sas_device->handle = handle;
	}

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT2SAS_ERR_FMT "device is not present "
		    "handle(0x%04x), flags!!!\n", ioc->name, handle);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus)) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	_scsih_ublock_io_device(ioc, sas_address);

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
 * Returns 0 for success, non-zero for failure.
 */
static int
_scsih_add_device(struct MPT2SAS_ADAPTER *ioc, u16 handle, u8 phy_num, u8 is_pd)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	struct _sas_device *sas_device;
	u32 ioc_status;
	__le64 sas_address;
	u32 device_info;
	unsigned long flags;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		printk(MPT2SAS_ERR_FMT "Flags = 0x%04x\n",
		    ioc->name, le16_to_cpu(sas_device_pg0.Flags));
		return -1;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus))
		return -1;

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_end_device(device_info))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}


	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (sas_device)
		return 0;

	sas_device = kzalloc(sizeof(struct _sas_device),
	    GFP_KERNEL);
	if (!sas_device) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_device->handle = handle;
	if (_scsih_get_sas_address(ioc, le16_to_cpu
		(sas_device_pg0.ParentDevHandle),
		&sas_device->sas_address_parent) != 0)
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	sas_device->enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	sas_device->slot =
	    le16_to_cpu(sas_device_pg0.Slot);
	sas_device->device_info = device_info;
	sas_device->sas_address = sas_address;
	sas_device->phy = sas_device_pg0.PhyNum;

	/* get enclosure_logical_id */
	if (sas_device->enclosure_handle && !(mpt2sas_config_get_enclosure_pg0(
	   ioc, &mpi_reply, &enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
	   sas_device->enclosure_handle)))
		sas_device->enclosure_logical_id =
		    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);

	/* get device name */
	sas_device->device_name = le64_to_cpu(sas_device_pg0.DeviceName);

	if (ioc->wait_for_discovery_to_complete)
		_scsih_sas_device_init_add(ioc, sas_device);
	else
		_scsih_sas_device_add(ioc, sas_device);

	return 0;
}

/**
 * _scsih_remove_device -  removing sas device object
 * @ioc: per adapter object
 * @sas_device_delete: the sas_device object
 *
 * Return nothing.
 */
static void
_scsih_remove_device(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: enter: "
	    "handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
		sas_device->handle, (unsigned long long)
	    sas_device->sas_address));

	if (sas_device->starget && sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		_scsih_ublock_io_device(ioc, sas_device->sas_address);
		sas_target_priv_data->handle =
		     MPT2SAS_INVALID_DEVICE_HANDLE;
	}

	if (!ioc->hide_drives)
		mpt2sas_transport_port_remove(ioc,
		    sas_device->sas_address,
		    sas_device->sas_address_parent);

	printk(MPT2SAS_INFO_FMT "removing handle(0x%04x), sas_addr"
	    "(0x%016llx)\n", ioc->name, sas_device->handle,
	    (unsigned long long) sas_device->sas_address);

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: exit: "
	    "handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)
	    sas_device->sas_address));
	kfree(sas_device);
}
/**
 * _scsih_device_remove_by_handle - removing device object by handle
 * @ioc: per adapter object
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_scsih_device_remove_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	if (sas_device)
		list_del(&sas_device->list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device)
		_scsih_remove_device(ioc, sas_device);
}

/**
 * mpt2sas_device_remove_by_sas_address - removing device object by sas address
 * @ioc: per adapter object
 * @sas_address: device sas_address
 *
 * Return nothing.
 */
void
mpt2sas_device_remove_by_sas_address(struct MPT2SAS_ADAPTER *ioc,
	u64 sas_address)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address);
	if (sas_device)
		list_del(&sas_device->list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device)
		_scsih_remove_device(ioc, sas_device);
}
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_topology_change_event_debug - debug for topology event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_topology_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
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
	printk(MPT2SAS_INFO_FMT "sas topology change: (%s)\n",
	    ioc->name, status_str);
	printk(KERN_INFO "\thandle(0x%04x), enclosure_handle(0x%04x) "
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
		printk(KERN_INFO "\tphy(%02d), attached_handle(0x%04x): %s:"
		    " link rate: new(0x%02x), old(0x%02x)\n", phy_number,
		    handle, status_str, link_rate, prev_link_rate);

	}
}
#endif

/**
 * _scsih_sas_topology_change_event - handle topology changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static void
_scsih_sas_topology_change_event(struct MPT2SAS_ADAPTER *ioc,
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
	Mpi2EventDataSasTopologyChangeList_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_topology_change_event_debug(ioc, event_data);
#endif

	if (ioc->remove_host || ioc->pci_error_recovery)
		return;

	if (!ioc->sas_hba.num_phys)
		_scsih_sas_host_add(ioc);
	else
		_scsih_sas_host_refresh(ioc);

	if (fw_event->ignore) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "ignoring expander "
		    "event\n", ioc->name));
		return;
	}

	parent_handle = le16_to_cpu(event_data->ExpanderDevHandle);

	/* handle expander add */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_ADDED)
		if (_scsih_expander_add(ioc, parent_handle) != 0)
			return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt2sas_scsih_expander_find_by_handle(ioc,
	    parent_handle);
	if (sas_expander) {
		sas_address = sas_expander->sas_address;
		max_phys = sas_expander->num_phys;
	} else if (parent_handle < ioc->sas_hba.num_phys) {
		sas_address = ioc->sas_hba.sas_address;
		max_phys = ioc->sas_hba.num_phys;
	} else {
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	/* handle siblings events */
	for (i = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "ignoring "
			    "expander event\n", ioc->name));
			return;
		}
		if (ioc->shost_recovery || ioc->remove_host ||
		    ioc->pci_error_recovery)
			return;
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

			mpt2sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate);

			if (link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
				break;

			_scsih_check_device(ioc, handle);
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:

			if (ioc->shost_recovery)
				break;

			mpt2sas_transport_update_links(ioc, sas_address,
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
		mpt2sas_expander_remove(ioc, sas_address);

}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_device_status_change_event_debug - debug for device event
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
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
	printk(MPT2SAS_INFO_FMT "device status change: (%s)\n"
	    "\thandle(0x%04x), sas address(0x%016llx), tag(%d)",
	    ioc->name, reason_str, le16_to_cpu(event_data->DevHandle),
	    (unsigned long long)le64_to_cpu(event_data->SASAddress),
	    le16_to_cpu(event_data->TaskTag));
	if (event_data->ReasonCode == MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA)
		printk(MPT2SAS_INFO_FMT ", ASC(0x%x), ASCQ(0x%x)\n", ioc->name,
		    event_data->ASC, event_data->ASCQ);
	printk(KERN_INFO "\n");
}
#endif

/**
 * _scsih_sas_device_status_change_event - handle device status change
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	struct MPT2SAS_TARGET *target_priv_data;
	struct _sas_device *sas_device;
	u64 sas_address;
	unsigned long flags;
	Mpi2EventDataSasDeviceStatusChange_t *event_data =
	    fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_device_status_change_event_debug(ioc,
		     event_data);
#endif

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
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address);

	if (!sas_device || !sas_device->starget) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}

	target_priv_data = sas_device->starget->hostdata;
	if (!target_priv_data) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}

	if (event_data->ReasonCode ==
	    MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET)
		target_priv_data->tm_busy = 1;
	else
		target_priv_data->tm_busy = 0;
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_enclosure_dev_status_change_event_debug - debug for enclosure event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
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

	printk(MPT2SAS_INFO_FMT "enclosure status change: (%s)\n"
	    "\thandle(0x%04x), enclosure logical id(0x%016llx)"
	    " number slots(%d)\n", ioc->name, reason_str,
	    le16_to_cpu(event_data->EnclosureHandle),
	    (unsigned long long)le64_to_cpu(event_data->EnclosureLogicalID),
	    le16_to_cpu(event_data->StartSlot));
}
#endif

/**
 * _scsih_sas_enclosure_dev_status_change_event - handle enclosure events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_enclosure_dev_status_change_event_debug(ioc,
		     fw_event->event_data);
#endif
}

/**
 * _scsih_sas_broadcast_primitive_event - handle broadcast events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_broadcast_primitive_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	struct scsi_cmnd *scmd;
	struct scsi_device *sdev;
	u16 smid, handle;
	u32 lun;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u32 termination_count;
	u32 query_count;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	Mpi2EventDataSasBroadcastPrimitive_t *event_data = fw_event->event_data;
	u16 ioc_status;
	unsigned long flags;
	int r;
	u8 max_retries = 0;
	u8 task_abort_retries;

	mutex_lock(&ioc->tm_cmds.mutex);
	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: enter: phy number(%d), "
	    "width(%d)\n", ioc->name, __func__, event_data->PhyNum,
	     event_data->PortWidth));

	_scsih_block_io_all_device(ioc);

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	mpi_reply = ioc->tm_cmds.reply;
broadcast_aen_retry:

	/* sanity checks for retrying this loop */
	if (max_retries++ == 5) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: giving up\n",
		    ioc->name, __func__));
		goto out;
	} else if (max_retries > 1)
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: %d retry\n",
		    ioc->name, __func__, max_retries - 1));

	termination_count = 0;
	query_count = 0;
	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		if (ioc->shost_recovery)
			goto out;
		scmd = _scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
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

		handle = sas_device_priv_data->sas_target->handle;
		lun = sas_device_priv_data->lun;
		query_count++;

		if (ioc->shost_recovery)
			goto out;

		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		r = mpt2sas_scsih_issue_tm(ioc, handle, 0, 0, lun,
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK, smid, 30, 0,
		    TM_MUTEX_OFF);
		if (r == FAILED) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt2sas_scsih_issue_tm: FAILED when sending "
			    "QUERY_TASK: scmd(%p)\n", scmd);
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus)
		    & MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			sdev_printk(KERN_WARNING, sdev, "query task: FAILED "
			    "with IOCSTATUS(0x%04x), scmd(%p)\n", ioc_status,
			    scmd);
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
			dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
			    "%s: ABORT_TASK: giving up\n", ioc->name,
			    __func__));
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}

		if (ioc->shost_recovery)
			goto out_no_lock;

		r = mpt2sas_scsih_issue_tm(ioc, handle, sdev->channel, sdev->id,
		    sdev->lun, MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, smid, 30,
		    scmd->serial_number, TM_MUTEX_OFF);
		if (r == FAILED) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt2sas_scsih_issue_tm: ABORT_TASK: FAILED : "
			    "scmd(%p)\n", scmd);
			goto tm_retry;
		}

		if (task_abort_retries > 1)
			sdev_printk(KERN_WARNING, sdev,
			    "mpt2sas_scsih_issue_tm: ABORT_TASK: RETRIES (%d):"
			    " scmd(%p)\n",
			    task_abort_retries - 1, scmd);

		termination_count += le32_to_cpu(mpi_reply->TerminationCount);
		spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	}

	if (ioc->broadcast_aen_pending) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: loop back due to"
		     " pending AEN\n", ioc->name, __func__));
		 ioc->broadcast_aen_pending = 0;
		 goto broadcast_aen_retry;
	}

 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
 out_no_lock:

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "%s - exit, query_count = %d termination_count = %d\n",
	    ioc->name, __func__, query_count, termination_count));

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
 *
 * Return nothing.
 */
static void
_scsih_sas_discovery_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	Mpi2EventDataSasDiscovery_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		printk(MPT2SAS_INFO_FMT "discovery event: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
	if (event_data->DiscoveryStatus)
		printk("discovery_status(0x%08x)",
		    le32_to_cpu(event_data->DiscoveryStatus));
	printk("\n");
	}
#endif

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
 * _scsih_reprobe_lun - reprobing lun
 * @sdev: scsi device struct
 * @no_uld_attach: sdev->no_uld_attach flag setting
 *
 **/
static void
_scsih_reprobe_lun(struct scsi_device *sdev, void *no_uld_attach)
{
	int rc;

	sdev->no_uld_attach = no_uld_attach ? 1 : 0;
	sdev_printk(KERN_INFO, sdev, "%s raid component\n",
	    sdev->no_uld_attach ? "hidding" : "exposing");
	rc = scsi_device_reprobe(sdev);
}

/**
 * _scsih_sas_volume_add - add new volume
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_add(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	u64 wwid;
	u16 handle = le16_to_cpu(element->VolDevHandle);
	int rc;

	mpt2sas_config_get_volume_wwid(ioc, handle, &wwid);
	if (!wwid) {
		printk(MPT2SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
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
		printk(MPT2SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
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
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_delete(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget = NULL;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
	if (raid_device) {
		if (raid_device->starget) {
			starget = raid_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->deleted = 1;
		}
		printk(MPT2SAS_INFO_FMT "removing handle(0x%04x), wwid"
		    "(0x%016llx)\n", ioc->name,  raid_device->handle,
		    (unsigned long long) raid_device->wwid);
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
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_expose(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
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
}

/**
 * _scsih_sas_pd_hide - hide pd component from /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_hide(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	u16 volume_handle = 0;
	u64 volume_wwid = 0;

	mpt2sas_config_get_volume_handle(ioc, handle, &volume_handle);
	if (volume_handle)
		mpt2sas_config_get_volume_wwid(ioc, volume_handle,
		    &volume_wwid);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
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
	if (starget)
		starget_for_each_device(starget, (void *)1, _scsih_reprobe_lun);
}

/**
 * _scsih_sas_pd_delete - delete pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_delete(struct MPT2SAS_ADAPTER *ioc,
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
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_add(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u64 sas_address;
	u16 parent_handle;

	set_bit(handle, ioc->pd_handles);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device)
		return;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
	if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
		mpt2sas_transport_update_links(ioc, sas_address, handle,
		    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);

	_scsih_add_device(ioc, handle, 0, 1);
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_ir_config_change_event_debug - debug for IR Config Change events
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	u8 element_type;
	int i;
	char *reason_str = NULL, *element_str = NULL;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];

	printk(MPT2SAS_INFO_FMT "raid config change: (%s), elements(%d)\n",
	    ioc->name, (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ?
	    "foreign" : "native", event_data->NumElements);
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
		printk(KERN_INFO "\t(%s:%s), vol handle(0x%04x), "
		    "pd handle(0x%04x), pd num(0x%02x)\n", element_str,
		    reason_str, le16_to_cpu(element->VolDevHandle),
		    le16_to_cpu(element->PhysDiskDevHandle),
		    element->PhysDiskNum);
	}
}
#endif

/**
 * _scsih_sas_ir_config_change_event - handle ir configuration change events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u8 foreign_config;
	Mpi2EventDataIrConfigChangeList_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
	    && !ioc->hide_ir_msg)
		_scsih_sas_ir_config_change_event_debug(ioc, event_data);

#endif

	if (ioc->shost_recovery)
		return;

	foreign_config = (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ? 1 : 0;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
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
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_volume_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	u64 wwid;
	unsigned long flags;
	struct _raid_device *raid_device;
	u16 handle;
	u32 state;
	int rc;
	Mpi2EventDataIrVolume_t *event_data = fw_event->event_data;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->VolDevHandle);
	state = le32_to_cpu(event_data->NewValue);
	if (!ioc->hide_ir_msg)
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: handle(0x%04x), "
		    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
		    le32_to_cpu(event_data->PreviousValue), state));

	switch (state) {
	case MPI2_RAID_VOL_STATE_MISSING:
	case MPI2_RAID_VOL_STATE_FAILED:
		_scsih_sas_volume_delete(ioc, handle);
		break;

	case MPI2_RAID_VOL_STATE_ONLINE:
	case MPI2_RAID_VOL_STATE_DEGRADED:
	case MPI2_RAID_VOL_STATE_OPTIMAL:

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

		if (raid_device)
			break;

		mpt2sas_config_get_volume_wwid(ioc, handle, &wwid);
		if (!wwid) {
			printk(MPT2SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			break;
		}

		raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
		if (!raid_device) {
			printk(MPT2SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
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
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_physical_disk_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	u16 handle, parent_handle;
	u32 state;
	struct _sas_device *sas_device;
	unsigned long flags;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	Mpi2EventDataIrPhysicalDisk_t *event_data = fw_event->event_data;
	u64 sas_address;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->PhysDiskDevHandle);
	state = le32_to_cpu(event_data->NewValue);

	if (!ioc->hide_ir_msg)
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: handle(0x%04x), "
		    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
		    le32_to_cpu(event_data->PreviousValue), state));

	switch (state) {
	case MPI2_RAID_PD_STATE_ONLINE:
	case MPI2_RAID_PD_STATE_DEGRADED:
	case MPI2_RAID_PD_STATE_REBUILDING:
	case MPI2_RAID_PD_STATE_OPTIMAL:
	case MPI2_RAID_PD_STATE_HOT_SPARE:

		if (!ioc->is_warpdrive)
			set_bit(handle, ioc->pd_handles);

		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

		if (sas_device)
			return;

		if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
			mpt2sas_transport_update_links(ioc, sas_address, handle,
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

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_ir_operation_status_event_debug - debug for IR op event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event_debug(struct MPT2SAS_ADAPTER *ioc,
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

	printk(MPT2SAS_INFO_FMT "raid operational status: (%s)"
	    "\thandle(0x%04x), percent complete(%d)\n",
	    ioc->name, reason_str,
	    le16_to_cpu(event_data->VolDevHandle),
	    event_data->PercentComplete);
}
#endif

/**
 * _scsih_sas_ir_operation_status_event - handle RAID operation events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	Mpi2EventDataIrOperationStatus_t *event_data = fw_event->event_data;
	static struct _raid_device *raid_device;
	unsigned long flags;
	u16 handle;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
	    && !ioc->hide_ir_msg)
		_scsih_sas_ir_operation_status_event_debug(ioc,
		     event_data);
#endif

	/* code added for raid transport support */
	if (event_data->RAIDOperation == MPI2_EVENT_IR_RAIDOP_RESYNC) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		handle = le16_to_cpu(event_data->VolDevHandle);
		raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
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
_scsih_prep_device_scan(struct MPT2SAS_ADAPTER *ioc)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
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
 * @sas_address: sas address
 * @slot: enclosure slot id
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_sas_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_sas_device(struct MPT2SAS_ADAPTER *ioc, u64 sas_address,
    u16 slot, u16 handle)
{
	struct MPT2SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _sas_device *sas_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (sas_device->sas_address == sas_address &&
		    sas_device->slot == slot) {
			sas_device->responding = 1;
			starget = sas_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			if (starget)
				starget_printk(KERN_INFO, starget,
				    "handle(0x%04x), sas_addr(0x%016llx), "
				    "enclosure logical id(0x%016llx), "
				    "slot(%d)\n", handle,
				    (unsigned long long)sas_device->sas_address,
				    (unsigned long long)
				    sas_device->enclosure_logical_id,
				    sas_device->slot);
			if (sas_device->handle == handle)
				goto out;
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    sas_device->handle);
			sas_device->handle = handle;
			if (sas_target_priv_data)
				sas_target_priv_data->handle = handle;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_search_responding_sas_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_sas_devices(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	__le64 sas_address;
	u16 handle;
	u32 device_info;
	u16 slot;

	printk(MPT2SAS_INFO_FMT "search for end-devices: start\n", ioc->name);

	if (list_empty(&ioc->sas_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
		if (!(_scsih_is_end_device(device_info)))
			continue;
		sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
		slot = le16_to_cpu(sas_device_pg0.Slot);
		_scsih_mark_responding_sas_device(ioc, sas_address, slot,
		    handle);
	}
out:
	printk(MPT2SAS_INFO_FMT "search for end-devices: complete\n",
	    ioc->name);
}

/**
 * _scsih_mark_responding_raid_device - mark a raid_device as responding
 * @ioc: per adapter object
 * @wwid: world wide identifier for raid volume
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_raid_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_raid_device(struct MPT2SAS_ADAPTER *ioc, u64 wwid,
    u16 handle)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
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
			_scsih_init_warpdrive_properties(ioc, raid_device);
			spin_lock_irqsave(&ioc->raid_device_lock, flags);
			if (raid_device->handle == handle) {
				spin_unlock_irqrestore(&ioc->raid_device_lock,
				    flags);
				return;
			}
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
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
 *
 * Return nothing.
 */
static void
_scsih_search_responding_raid_devices(struct MPT2SAS_ADAPTER *ioc)
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

	printk(MPT2SAS_INFO_FMT "search for raid volumes: start\n",
	    ioc->name);

	if (list_empty(&ioc->raid_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		handle = le16_to_cpu(volume_pg1.DevHandle);

		if (mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
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
		while (!(mpt2sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
		    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
		    phys_disk_num))) {
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
				break;
			phys_disk_num = pd_pg0.PhysDiskNum;
			handle = le16_to_cpu(pd_pg0.DevHandle);
			set_bit(handle, ioc->pd_handles);
		}
	}
out:
	printk(MPT2SAS_INFO_FMT "search for responding raid volumes: "
	    "complete\n", ioc->name);
}

/**
 * _scsih_mark_responding_expander - mark a expander as responding
 * @ioc: per adapter object
 * @sas_address: sas address
 * @handle:
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_expanders.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_expander(struct MPT2SAS_ADAPTER *ioc, u64 sas_address,
     u16 handle)
{
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address)
			continue;
		sas_expander->responding = 1;
		if (sas_expander->handle == handle)
			goto out;
		printk(KERN_INFO "\texpander(0x%016llx): handle changed"
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
 *
 * Return nothing.
 */
static void
_scsih_search_responding_expanders(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u64 sas_address;
	u16 handle;

	printk(MPT2SAS_INFO_FMT "search for expanders: start\n", ioc->name);

	if (list_empty(&ioc->sas_expander_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;

		handle = le16_to_cpu(expander_pg0.DevHandle);
		sas_address = le64_to_cpu(expander_pg0.SASAddress);
		printk(KERN_INFO "\texpander present: handle(0x%04x), "
		    "sas_addr(0x%016llx)\n", handle,
		    (unsigned long long)sas_address);
		_scsih_mark_responding_expander(ioc, sas_address, handle);
	}

 out:
	printk(MPT2SAS_INFO_FMT "search for expanders: complete\n", ioc->name);
}

/**
 * _scsih_remove_unresponding_sas_devices - removing unresponding devices
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_remove_unresponding_sas_devices(struct MPT2SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *sas_device_next;
	struct _sas_node *sas_expander, *sas_expander_next;
	struct _raid_device *raid_device, *raid_device_next;
	struct list_head tmp_list;
	unsigned long flags;

	printk(MPT2SAS_INFO_FMT "removing unresponding devices: start\n",
	    ioc->name);

	/* removing unresponding end devices */
	printk(MPT2SAS_INFO_FMT "removing unresponding devices: end-devices\n",
	    ioc->name);
	list_for_each_entry_safe(sas_device, sas_device_next,
	    &ioc->sas_device_list, list) {
		if (!sas_device->responding)
			mpt2sas_device_remove_by_sas_address(ioc,
				sas_device->sas_address);
		else
			sas_device->responding = 0;
	}

	/* removing unresponding volumes */
	if (ioc->ir_firmware) {
		printk(MPT2SAS_INFO_FMT "removing unresponding devices: "
		    "volumes\n", ioc->name);
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
	printk(MPT2SAS_INFO_FMT "removing unresponding devices: expanders\n",
	    ioc->name);
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
		list_del(&sas_expander->list);
		_scsih_expander_node_remove(ioc, sas_expander);
	}
	printk(MPT2SAS_INFO_FMT "removing unresponding devices: complete\n",
	    ioc->name);
	/* unblock devices */
	_scsih_ublock_io_all_device(ioc);
}

static void
_scsih_refresh_expander_links(struct MPT2SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander, u16 handle)
{
	Mpi2ExpanderPage1_t expander_pg1;
	Mpi2ConfigReply_t mpi_reply;
	int i;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt2sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		mpt2sas_transport_update_links(ioc, sas_expander->sas_address,
		    le16_to_cpu(expander_pg1.AttachedDevHandle), i,
		    expander_pg1.NegotiatedLinkRate >> 4);
	}
}

/**
 * _scsih_scan_for_devices_after_reset - scan for devices after host reset
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_scan_for_devices_after_reset(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
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
	struct _sas_node *expander_device;
	static struct _raid_device *raid_device;
	u8 retry_count;
	unsigned long flags;

	printk(MPT2SAS_INFO_FMT "scan devices: start\n", ioc->name);

	_scsih_sas_host_refresh(ioc);

	printk(MPT2SAS_INFO_FMT "\tscan devices: expanders start\n",
		ioc->name);
	/* expanders */
	handle = 0xFFFF;
	while (!(mpt2sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from expander scan: "
				"ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(expander_pg0.DevHandle);
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		expander_device = mpt2sas_scsih_expander_find_by_sas_address(
		    ioc, le64_to_cpu(expander_pg0.SASAddress));
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (expander_device)
			_scsih_refresh_expander_links(ioc, expander_device,
			    handle);
		else {
			printk(MPT2SAS_INFO_FMT "\tBEFORE adding expander: "
				"handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(expander_pg0.SASAddress));
			_scsih_expander_add(ioc, handle);
			printk(MPT2SAS_INFO_FMT "\tAFTER adding expander: "
				"handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(expander_pg0.SASAddress));
		}
	}

	printk(MPT2SAS_INFO_FMT "\tscan devices: expanders complete\n",
		ioc->name);

	if (!ioc->ir_firmware)
		goto skip_to_sas;

	printk(MPT2SAS_INFO_FMT "\tscan devices phys disk start\n", ioc->name);
	/* phys disk */
	phys_disk_num = 0xFF;
	while (!(mpt2sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
	    phys_disk_num))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from phys disk scan:"
				"ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		phys_disk_num = pd_pg0.PhysDiskNum;
		handle = le16_to_cpu(pd_pg0.DevHandle);
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		if (sas_device)
			continue;
		if (mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle) != 0)
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from phys disk scan "
				"ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle,
		    &sas_address)) {
			printk(MPT2SAS_INFO_FMT "\tBEFORE adding phys disk: "
				" handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(sas_device_pg0.SASAddress));
			mpt2sas_transport_update_links(ioc, sas_address,
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
			printk(MPT2SAS_INFO_FMT "\tAFTER adding phys disk: "
				" handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}

	printk(MPT2SAS_INFO_FMT "\tscan devices: phys disk complete\n",
		ioc->name);

	printk(MPT2SAS_INFO_FMT "\tscan devices: volumes start\n", ioc->name);
	/* volumes */
	handle = 0xFFFF;
	while (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from volume scan: "
				"ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(volume_pg1.DevHandle);
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_wwid(ioc,
		    le64_to_cpu(volume_pg1.WWID));
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (raid_device)
			continue;
		if (mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
		    &volume_pg0, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
		     sizeof(Mpi2RaidVolPage0_t)))
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from volume scan: "
				"ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		if (volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_OPTIMAL ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_ONLINE ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_DEGRADED) {
			memset(&element, 0, sizeof(Mpi2EventIrConfigElement_t));
			element.ReasonCode = MPI2_EVENT_IR_CHANGE_RC_ADDED;
			element.VolDevHandle = volume_pg1.DevHandle;
			printk(MPT2SAS_INFO_FMT "\tBEFORE adding volume: "
				" handle (0x%04x)\n", ioc->name,
				volume_pg1.DevHandle);
			_scsih_sas_volume_add(ioc, &element);
			printk(MPT2SAS_INFO_FMT "\tAFTER adding volume: "
				" handle (0x%04x)\n", ioc->name,
				volume_pg1.DevHandle);
		}
	}

	printk(MPT2SAS_INFO_FMT "\tscan devices: volumes complete\n",
		ioc->name);

 skip_to_sas:

	printk(MPT2SAS_INFO_FMT "\tscan devices: end devices start\n",
		ioc->name);
	/* sas devices */
	handle = 0xFFFF;
	while (!(mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_INFO_FMT "\tbreak from end device scan:"
				" ioc_status(0x%04x), loginfo(0x%08x)\n",
				ioc->name, ioc_status,
				le32_to_cpu(mpi_reply.IOCLogInfo));
				break;
		}
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		if (!(_scsih_is_end_device(
		    le32_to_cpu(sas_device_pg0.DeviceInfo))))
			continue;
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
		    le64_to_cpu(sas_device_pg0.SASAddress));
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		if (sas_device)
			continue;
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address)) {
			printk(MPT2SAS_INFO_FMT "\tBEFORE adding end device: "
				"handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(sas_device_pg0.SASAddress));
			mpt2sas_transport_update_links(ioc, sas_address, handle,
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
			printk(MPT2SAS_INFO_FMT "\tAFTER adding end device: "
				"handle (0x%04x), sas_addr(0x%016llx)\n",
				ioc->name, handle, (unsigned long long)
				le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}

	printk(MPT2SAS_INFO_FMT "\tscan devices: end devices complete\n",
		ioc->name);

	printk(MPT2SAS_INFO_FMT "scan devices: complete\n", ioc->name);
}


/**
 * mpt2sas_scsih_reset_handler - reset callback handler (for scsih)
 * @ioc: per adapter object
 * @reset_phase: phase
 *
 * The handler for doing any required cleanup or initialization.
 *
 * The reset phase can be MPT2_IOC_PRE_RESET, MPT2_IOC_AFTER_RESET,
 * MPT2_IOC_DONE_RESET
 *
 * Return nothing.
 */
void
mpt2sas_scsih_reset_handler(struct MPT2SAS_ADAPTER *ioc, int reset_phase)
{
	switch (reset_phase) {
	case MPT2_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT2_IOC_AFTER_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_AFTER_RESET\n", ioc->name, __func__));
		if (ioc->scsih_cmds.status & MPT2_CMD_PENDING) {
			ioc->scsih_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->scsih_cmds.smid);
			complete(&ioc->scsih_cmds.done);
		}
		if (ioc->tm_cmds.status & MPT2_CMD_PENDING) {
			ioc->tm_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->tm_cmds.smid);
			complete(&ioc->tm_cmds.done);
		}
		_scsih_fw_event_cleanup_queue(ioc);
		_scsih_flush_running_cmds(ioc);
		break;
	case MPT2_IOC_DONE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_DONE_RESET\n", ioc->name, __func__));
		_scsih_sas_host_refresh(ioc);
		_scsih_prep_device_scan(ioc);
		_scsih_search_responding_sas_devices(ioc);
		_scsih_search_responding_raid_devices(ioc);
		_scsih_search_responding_expanders(ioc);
		if ((!ioc->is_driver_loading) && !(disable_discovery > 0 &&
		    !ioc->sas_hba.num_phys)) {
			_scsih_prep_device_scan(ioc);
			_scsih_search_responding_sas_devices(ioc);
			_scsih_search_responding_raid_devices(ioc);
			_scsih_search_responding_expanders(ioc);
			_scsih_error_recovery_delete_devices(ioc);
		}
		break;
	}
}

/**
 * _firmware_event_work - delayed task for processing firmware events
 * @ioc: per adapter object
 * @work: equal to the fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_firmware_event_work(struct work_struct *work)
{
	struct fw_event_work *fw_event = container_of(work,
	    struct fw_event_work, delayed_work.work);
	struct MPT2SAS_ADAPTER *ioc = fw_event->ioc;

	/* the queue is being flushed so ignore this event */
	if (ioc->remove_host || fw_event->cancel_pending_work ||
	    ioc->pci_error_recovery) {
		_scsih_fw_event_free(ioc, fw_event);
		return;
	}

	switch (fw_event->event) {
	case MPT2SAS_REMOVE_UNRESPONDING_DEVICES:
		while (scsi_host_in_recovery(ioc->shost) || ioc->shost_recovery)
			ssleep(1);
		_scsih_remove_unresponding_sas_devices(ioc);
		_scsih_scan_for_devices_after_reset(ioc);
		break;
	case MPT2SAS_PORT_ENABLE_COMPLETE:
		ioc->start_scan = 0;

		if (missing_delay[0] != -1 && missing_delay[1] != -1)
			mpt2sas_base_update_missing_delay(ioc, missing_delay[0],
				missing_delay[1]);

		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "port enable: complete "
		    "from worker thread\n", ioc->name));
		break;
	case MPT2SAS_TURN_ON_FAULT_LED:
		_scsih_turn_on_fault_led(ioc, fw_event->device_handle);
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_sas_topology_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		_scsih_sas_device_status_change_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
		_scsih_sas_discovery_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		_scsih_sas_broadcast_primitive_event(ioc,
		    fw_event);
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
	}
	_scsih_fw_event_free(ioc, fw_event);
}

/**
 * mpt2sas_scsih_event_callback - firmware event handler (called at ISR time)
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt.
 *
 * This function merely adds a new work task into ioc->firmware_event_thread.
 * The tasks are worked from _firmware_event_work in user context.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt2sas_scsih_event_callback(struct MPT2SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	struct fw_event_work *fw_event;
	Mpi2EventNotificationReply_t *mpi_reply;
	u16 event;
	u16 sz;

	/* events turned off due to host reset or driver unloading */
	if (ioc->remove_host || ioc->pci_error_recovery)
		return 1;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);

	if (unlikely(!mpi_reply)) {
		printk(MPT2SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}

	event = le16_to_cpu(mpi_reply->Event);

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
		__le32 *log_code;

		if (!ioc->is_warpdrive)
			break;

		log_entry = (Mpi2EventDataLogEntryAdded_t *)
		    mpi_reply->EventData;
		log_code = (__le32 *)log_entry->LogData;

		if (le16_to_cpu(log_entry->LogEntryQualifier)
		    != MPT2_WARPDRIVE_LOGENTRY)
			break;

		switch (le32_to_cpu(*log_code)) {
		case MPT2_WARPDRIVE_LC_SSDT:
			printk(MPT2SAS_WARN_FMT "WarpDrive Warning: "
			    "IO Throttling has occurred in the WarpDrive "
			    "subsystem. Check WarpDrive documentation for "
			    "additional details.\n", ioc->name);
			break;
		case MPT2_WARPDRIVE_LC_SSDLW:
			printk(MPT2SAS_WARN_FMT "WarpDrive Warning: "
			    "Program/Erase Cycles for the WarpDrive subsystem "
			    "in degraded range. Check WarpDrive documentation "
			    "for additional details.\n", ioc->name);
			break;
		case MPT2_WARPDRIVE_LC_SSDLF:
			printk(MPT2SAS_ERR_FMT "WarpDrive Fatal Error: "
			    "There are no Program/Erase Cycles for the "
			    "WarpDrive subsystem. The storage device will be "
			    "in read-only mode. Check WarpDrive documentation "
			    "for additional details.\n", ioc->name);
			break;
		case MPT2_WARPDRIVE_LC_BRMF:
			printk(MPT2SAS_ERR_FMT "WarpDrive Fatal Error: "
			    "The Backup Rail Monitor has failed on the "
			    "WarpDrive subsystem. Check WarpDrive "
			    "documentation for additional details.\n",
			    ioc->name);
			break;
		}

		break;
	}
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_OPERATION_STATUS:
	case MPI2_EVENT_SAS_DISCOVERY:
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		break;

	default: /* ignore the rest */
		return 1;
	}

	fw_event = kzalloc(sizeof(struct fw_event_work), GFP_ATOMIC);
	if (!fw_event) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	sz = le16_to_cpu(mpi_reply->EventDataLength) * 4;
	fw_event->event_data = kzalloc(sz, GFP_ATOMIC);
	if (!fw_event->event_data) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		kfree(fw_event);
		return 1;
	}

	memcpy(fw_event->event_data, mpi_reply->EventData,
	    sz);
	fw_event->ioc = ioc;
	fw_event->VF_ID = mpi_reply->VF_ID;
	fw_event->VP_ID = mpi_reply->VP_ID;
	fw_event->event = event;
	_scsih_fw_event_add(ioc, fw_event);
	return 1;
}

/* shost template */
static struct scsi_host_template scsih_driver_template = {
	.module				= THIS_MODULE,
	.name				= "Fusion MPT SAS Host",
	.proc_name			= MPT2SAS_DRIVER_NAME,
	.queuecommand			= _scsih_qcmd,
	.target_alloc			= _scsih_target_alloc,
	.slave_alloc			= _scsih_slave_alloc,
	.slave_configure		= _scsih_slave_configure,
	.target_destroy			= _scsih_target_destroy,
	.slave_destroy			= _scsih_slave_destroy,
	.scan_finished			= _scsih_scan_finished,
	.scan_start			= _scsih_scan_start,
	.change_queue_depth 		= _scsih_change_queue_depth,
	.change_queue_type		= _scsih_change_queue_type,
	.eh_abort_handler		= _scsih_abort,
	.eh_device_reset_handler	= _scsih_dev_reset,
	.eh_target_reset_handler	= _scsih_target_reset,
	.eh_host_reset_handler		= _scsih_host_reset,
	.bios_param			= _scsih_bios_param,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPT2SAS_SG_DEPTH,
	.max_sectors			= 32767,
	.cmd_per_lun			= 7,
	.use_clustering			= ENABLE_CLUSTERING,
	.shost_attrs			= mpt2sas_host_attrs,
	.sdev_attrs			= mpt2sas_dev_attrs,
};

/**
 * _scsih_expander_node_remove - removing expander device from list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * Removing object and freeing associated memory from the
 * ioc->sas_expander_list.
 *
 * Return nothing.
 */
static void
_scsih_expander_node_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander)
{
	struct _sas_port *mpt2sas_port, *next;

	/* remove sibling ports attached to this expander */
	list_for_each_entry_safe(mpt2sas_port, next,
	   &sas_expander->sas_port_list, port_list) {
		if (ioc->shost_recovery)
			return;
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt2sas_device_remove_by_sas_address(ioc,
			    mpt2sas_port->remote_identify.sas_address);
		else if (mpt2sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt2sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt2sas_expander_remove(ioc,
			    mpt2sas_port->remote_identify.sas_address);
	}

	mpt2sas_transport_port_remove(ioc, sas_expander->sas_address,
	    sas_expander->sas_address_parent);

	printk(MPT2SAS_INFO_FMT "expander_remove: handle"
	   "(0x%04x), sas_addr(0x%016llx)\n", ioc->name,
	    sas_expander->handle, (unsigned long long)
	    sas_expander->sas_address);

	kfree(sas_expander->phy);
	kfree(sas_expander);
}

/**
 * _scsih_ir_shutdown - IR shutdown notification
 * @ioc: per adapter object
 *
 * Sending RAID Action to alert the Integrated RAID subsystem of the IOC that
 * the host system is shutting down.
 *
 * Return nothing.
 */
static void
_scsih_ir_shutdown(struct MPT2SAS_ADAPTER *ioc)
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

	if (ioc->scsih_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: scsih_cmd in use\n",
		    ioc->name, __func__);
		goto out;
	}
	ioc->scsih_cmds.status = MPT2_CMD_PENDING;

	smid = mpt2sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ioc->scsih_cmds.status = MPT2_CMD_NOT_USED;
		goto out;
	}

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2RaidActionRequest_t));

	mpi_request->Function = MPI2_FUNCTION_RAID_ACTION;
	mpi_request->Action = MPI2_RAID_ACTION_SYSTEM_SHUTDOWN_INITIATED;

	if (!ioc->hide_ir_msg)
		printk(MPT2SAS_INFO_FMT "IR shutdown (sending)\n", ioc->name);
	init_completion(&ioc->scsih_cmds.done);
	mpt2sas_base_put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->scsih_cmds.done, 10*HZ);

	if (!(ioc->scsih_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT2_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;

		if (!ioc->hide_ir_msg)
			printk(MPT2SAS_INFO_FMT "IR shutdown (complete): "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo));
	}

 out:
	ioc->scsih_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
}

/**
 * _scsih_shutdown - routine call during system shutdown
 * @pdev: PCI device struct
 *
 * Return nothing.
 */
static void
_scsih_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct workqueue_struct	*wq;
	unsigned long flags;

	ioc->remove_host = 1;
	_scsih_fw_event_cleanup_queue(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	_scsih_ir_shutdown(ioc);
	mpt2sas_base_detach(ioc);
}

/**
 * _scsih_remove - detach and remove add host
 * @pdev: PCI device struct
 *
 * Routine called when unloading the driver.
 * Return nothing.
 */
static void
_scsih_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct _sas_port *mpt2sas_port, *next_port;
	struct _raid_device *raid_device, *next;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct workqueue_struct	*wq;
	unsigned long flags;

	ioc->remove_host = 1;
	_scsih_fw_event_cleanup_queue(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	/* release all the volumes */
	_scsih_ir_shutdown(ioc);
	list_for_each_entry_safe(raid_device, next, &ioc->raid_device_list,
	    list) {
		if (raid_device->starget) {
			sas_target_priv_data =
			    raid_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			scsi_remove_target(&raid_device->starget->dev);
		}
		printk(MPT2SAS_INFO_FMT "removing handle(0x%04x), wwid"
		    "(0x%016llx)\n", ioc->name,  raid_device->handle,
		    (unsigned long long) raid_device->wwid);
		_scsih_raid_device_remove(ioc, raid_device);
	}

	/* free ports attached to the sas_host */
	list_for_each_entry_safe(mpt2sas_port, next_port,
	   &ioc->sas_hba.sas_port_list, port_list) {
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt2sas_device_remove_by_sas_address(ioc,
			    mpt2sas_port->remote_identify.sas_address);
		else if (mpt2sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt2sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt2sas_expander_remove(ioc,
			    mpt2sas_port->remote_identify.sas_address);
	}

	/* free phys attached to the sas_host */
	if (ioc->sas_hba.num_phys) {
		kfree(ioc->sas_hba.phy);
		ioc->sas_hba.phy = NULL;
		ioc->sas_hba.num_phys = 0;
	}

	sas_remove_host(shost);
	mpt2sas_base_detach(ioc);
	list_del(&ioc->list);
	scsi_remove_host(shost);
	scsi_host_put(shost);
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
_scsih_probe_boot_devices(struct MPT2SAS_ADAPTER *ioc)
{
	u8 is_raid;
	void *device;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	u16 handle;
	u64 sas_address_parent;
	u64 sas_address;
	unsigned long flags;
	int rc;

	 /* no Bios, return immediately */
	if (!ioc->bios_pg3.BiosVersion)
		return;

	device = NULL;
	is_raid = 0;
	if (ioc->req_boot_device.device) {
		device =  ioc->req_boot_device.device;
		is_raid = ioc->req_boot_device.is_raid;
	} else if (ioc->req_alt_boot_device.device) {
		device =  ioc->req_alt_boot_device.device;
		is_raid = ioc->req_alt_boot_device.is_raid;
	} else if (ioc->current_boot_device.device) {
		device =  ioc->current_boot_device.device;
		is_raid = ioc->current_boot_device.is_raid;
	}

	if (!device)
		return;

	if (is_raid) {
		raid_device = device;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
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
		if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent)) {
			_scsih_sas_device_remove(ioc, sas_device);
		} else if (!sas_device->starget) {
			if (!ioc->is_driver_loading)
				mpt2sas_transport_port_remove(ioc, sas_address,
					sas_address_parent);
			_scsih_sas_device_remove(ioc, sas_device);
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
_scsih_probe_raid(struct MPT2SAS_ADAPTER *ioc)
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

/**
 * _scsih_probe_sas - reporting sas devices to sas transport
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_sas(struct MPT2SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *next;
	unsigned long flags;

	/* SAS Device List */
	list_for_each_entry_safe(sas_device, next, &ioc->sas_device_init_list,
	    list) {

		if (ioc->hide_drives)
			continue;

		if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent)) {
			list_del(&sas_device->list);
			kfree(sas_device);
			continue;
		} else if (!sas_device->starget) {
			if (!ioc->is_driver_loading)
				mpt2sas_transport_port_remove(ioc,
					sas_device->sas_address,
					sas_device->sas_address_parent);
			list_del(&sas_device->list);
			kfree(sas_device);
			continue;

		}
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		list_move_tail(&sas_device->list, &ioc->sas_device_list);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}
}

/**
 * _scsih_probe_devices - probing for devices
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_devices(struct MPT2SAS_ADAPTER *ioc)
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
	} else
		_scsih_probe_sas(ioc);
}


/**
 * _scsih_scan_start - scsi lld callback for .scan_start
 * @shost: SCSI host pointer
 *
 * The shost has the ability to discover targets on its own instead
 * of scanning the entire bus.  In our implemention, we will kick off
 * firmware discovery.
 */
static void
_scsih_scan_start(struct Scsi_Host *shost)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	int rc;

	if (diag_buffer_enable != -1 && diag_buffer_enable != 0)
		mpt2sas_enable_diag_buffer(ioc, diag_buffer_enable);

	if (disable_discovery > 0)
		return;

	ioc->start_scan = 1;
	rc = mpt2sas_port_enable(ioc);

	if (rc != 0)
		printk(MPT2SAS_INFO_FMT "port enable: FAILED\n", ioc->name);
}

/**
 * _scsih_scan_finished - scsi lld callback for .scan_finished
 * @shost: SCSI host pointer
 * @time: elapsed time of the scan in jiffies
 *
 * This function will be called periodically until it returns 1 with the
 * scsi_host and the elapsed time of the scan in jiffies. In our implemention,
 * we wait for firmware discovery to complete, then return 1.
 */
static int
_scsih_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);

	if (disable_discovery > 0) {
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		return 1;
	}

	if (time >= (300 * HZ)) {
		ioc->base_cmds.status = MPT2_CMD_NOT_USED;
		printk(MPT2SAS_INFO_FMT "port enable: FAILED with timeout "
		    "(timeout=300s)\n", ioc->name);
		ioc->is_driver_loading = 0;
		return 1;
	}

	if (ioc->start_scan)
		return 0;

	if (ioc->start_scan_failed) {
		printk(MPT2SAS_INFO_FMT "port enable: FAILED with "
		    "(ioc_status=0x%08x)\n", ioc->name, ioc->start_scan_failed);
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		ioc->remove_host = 1;
		return 1;
	}

	printk(MPT2SAS_INFO_FMT "port enable: SUCCESS\n", ioc->name);
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;

	if (ioc->wait_for_discovery_to_complete) {
		ioc->wait_for_discovery_to_complete = 0;
		_scsih_probe_devices(ioc);
	}
	mpt2sas_base_start_watchdog(ioc);
	ioc->is_driver_loading = 0;
	return 1;
}


/**
 * _scsih_probe - attach and add scsi host
 * @pdev: PCI device struct
 * @id: pci device id
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct MPT2SAS_ADAPTER *ioc;
	struct Scsi_Host *shost;

	shost = scsi_host_alloc(&scsih_driver_template,
	    sizeof(struct MPT2SAS_ADAPTER));
	if (!shost)
		return -ENODEV;

	/* init local params */
	ioc = shost_priv(shost);
	memset(ioc, 0, sizeof(struct MPT2SAS_ADAPTER));
	INIT_LIST_HEAD(&ioc->list);
	list_add_tail(&ioc->list, &mpt2sas_ioc_list);
	ioc->shost = shost;
	ioc->id = mpt_ids++;
	sprintf(ioc->name, "%s%d", MPT2SAS_DRIVER_NAME, ioc->id);
	ioc->pdev = pdev;
	if (id->device == MPI2_MFGPAGE_DEVID_SSS6200) {
		ioc->is_warpdrive = 1;
		ioc->hide_ir_msg = 1;
	} else
		ioc->mfg_pg10_hide_flag = MFG_PAGE10_EXPOSE_ALL_DISKS;
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
	/* misc semaphores and spin locks */
	mutex_init(&ioc->reset_in_progress_mutex);
	spin_lock_init(&ioc->ioc_reset_in_progress_lock);
	spin_lock_init(&ioc->scsi_lookup_lock);
	spin_lock_init(&ioc->sas_device_lock);
	spin_lock_init(&ioc->sas_node_lock);
	spin_lock_init(&ioc->fw_event_lock);
	spin_lock_init(&ioc->raid_device_lock);

	INIT_LIST_HEAD(&ioc->sas_device_list);
	INIT_LIST_HEAD(&ioc->sas_device_init_list);
	INIT_LIST_HEAD(&ioc->sas_expander_list);
	INIT_LIST_HEAD(&ioc->fw_event_list);
	INIT_LIST_HEAD(&ioc->raid_device_list);
	INIT_LIST_HEAD(&ioc->sas_hba.sas_port_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_volume_list);

	/* init shost parameters */
	shost->max_cmd_len = 32;
	shost->max_lun = max_lun;
	shost->transportt = mpt2sas_transport_template;
	shost->unique_id = ioc->id;

	if (max_sectors != 0xFFFF) {
		if (max_sectors < 64) {
			shost->max_sectors = 64;
			printk(MPT2SAS_WARN_FMT "Invalid value %d passed "
			    "for max_sectors, range is 64 to 32767. Assigning "
			    "value of 64.\n", ioc->name, max_sectors);
		} else if (max_sectors > 32767) {
			shost->max_sectors = 32767;
			printk(MPT2SAS_WARN_FMT "Invalid value %d passed "
			    "for max_sectors, range is 64 to 8192. Assigning "
			    "default value of 32767.\n", ioc->name,
			    max_sectors);
		} else {
			shost->max_sectors = max_sectors & 0xFFFE;
			printk(MPT2SAS_INFO_FMT "The max_sectors value is "
			    "set to %d\n", ioc->name, shost->max_sectors);
		}
	}

	if ((scsi_add_host(shost, &pdev->dev))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		list_del(&ioc->list);
		goto out_add_shost_fail;
	}

	/* register EEDP capabilities with SCSI layer */
	if (prot_mask)
		scsi_host_set_prot(shost, prot_mask);
	else
		scsi_host_set_prot(shost, SHOST_DIF_TYPE1_PROTECTION
				   | SHOST_DIF_TYPE2_PROTECTION
				   | SHOST_DIF_TYPE3_PROTECTION);

	scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);

	/* event thread */
	snprintf(ioc->firmware_event_name, sizeof(ioc->firmware_event_name),
	    "fw_event%d", ioc->id);
	ioc->firmware_event_thread = create_singlethread_workqueue(
	    ioc->firmware_event_name);
	if (!ioc->firmware_event_thread) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out_thread_fail;
	}

	ioc->is_driver_loading = 1;
	if ((mpt2sas_base_attach(ioc))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out_attach_fail;
	}

	if (ioc->is_warpdrive) {
		if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_EXPOSE_ALL_DISKS)
			ioc->hide_drives = 0;
		else if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_HIDE_ALL_DISKS)
			ioc->hide_drives = 1;
		else {
			if (_scsih_get_num_volumes(ioc))
				ioc->hide_drives = 1;
			else
				ioc->hide_drives = 0;
		}
	} else
		ioc->hide_drives = 0;
	scsi_scan_host(shost);

	return 0;

 out_attach_fail:
	destroy_workqueue(ioc->firmware_event_thread);
 out_thread_fail:
	list_del(&ioc->list);
	scsi_remove_host(shost);
 out_add_shost_fail:
	scsi_host_put(shost);
	return -ENODEV;
}

#ifdef CONFIG_PM
/**
 * _scsih_suspend - power management suspend main entry point
 * @pdev: PCI device struct
 * @state: PM state change to (usually PCI_D3)
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	pci_power_t device_state;

	mpt2sas_base_stop_watchdog(ioc);
	scsi_block_requests(shost);
	device_state = pci_choose_state(pdev, state);
	printk(MPT2SAS_INFO_FMT "pdev=0x%p, slot=%s, entering "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	mpt2sas_base_free_resources(ioc);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, device_state);
	return 0;
}

/**
 * _scsih_resume - power management resume main entry point
 * @pdev: PCI device struct
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	pci_power_t device_state = pdev->current_state;
	int r;

	printk(MPT2SAS_INFO_FMT "pdev=0x%p, slot=%s, previous "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	ioc->pdev = pdev;
	r = mpt2sas_base_map_resources(ioc);
	if (r)
		return r;

	mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP, SOFT_RESET);
	scsi_unblock_requests(shost);
	mpt2sas_base_start_watchdog(ioc);
	return 0;
}
#endif /* CONFIG_PM */

/**
 * _scsih_pci_error_detected - Called when a PCI error is detected.
 * @pdev: PCI device struct
 * @state: PCI channel state
 *
 * Description: Called when a PCI error is detected.
 *
 * Return value:
 *      PCI_ERS_RESULT_NEED_RESET or PCI_ERS_RESULT_DISCONNECT
 */
static pci_ers_result_t
_scsih_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);

	printk(MPT2SAS_INFO_FMT "PCI error: detected callback, state(%d)!!\n",
	    ioc->name, state);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		/* Fatal error, prepare for slot reset */
		ioc->pci_error_recovery = 1;
		scsi_block_requests(ioc->shost);
		mpt2sas_base_stop_watchdog(ioc);
		mpt2sas_base_free_resources(ioc);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		/* Permanent error, prepare for device removal */
		ioc->pci_error_recovery = 1;
		mpt2sas_base_stop_watchdog(ioc);
		_scsih_flush_running_cmds(ioc);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * _scsih_pci_slot_reset - Called when PCI slot has been reset.
 * @pdev: PCI device struct
 *
 * Description: This routine is called by the pci error recovery
 * code after the PCI slot has been reset, just before we
 * should resume normal operations.
 */
static pci_ers_result_t
_scsih_pci_slot_reset(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	int rc;

	printk(MPT2SAS_INFO_FMT "PCI error: slot reset callback!!\n",
		ioc->name);

	ioc->pci_error_recovery = 0;
	ioc->pdev = pdev;
	pci_restore_state(pdev);
	rc = mpt2sas_base_map_resources(ioc);
	if (rc)
		return PCI_ERS_RESULT_DISCONNECT;


	rc = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
	    FORCE_BIG_HAMMER);

	printk(MPT2SAS_WARN_FMT "hard reset: %s\n", ioc->name,
	    (rc == 0) ? "success" : "failed");

	if (!rc)
		return PCI_ERS_RESULT_RECOVERED;
	else
		return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * _scsih_pci_resume() - resume normal ops after PCI reset
 * @pdev: pointer to PCI device
 *
 * Called when the error recovery driver tells us that its
 * OK to resume normal operation. Use completion to allow
 * halted scsi ops to resume.
 */
static void
_scsih_pci_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);

	printk(MPT2SAS_INFO_FMT "PCI error: resume callback!!\n", ioc->name);

	pci_cleanup_aer_uncorrect_error_status(pdev);
	mpt2sas_base_start_watchdog(ioc);
	scsi_unblock_requests(ioc->shost);
}

/**
 * _scsih_pci_mmio_enabled - Enable MMIO and dump debug registers
 * @pdev: pointer to PCI device
 */
static pci_ers_result_t
_scsih_pci_mmio_enabled(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);

	printk(MPT2SAS_INFO_FMT "PCI error: mmio enabled callback!!\n",
	    ioc->name);

	/* TODO - dump whatever for debugging purposes */

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

static const struct pci_error_handlers _scsih_err_handler = {
	.error_detected = _scsih_pci_error_detected,
	.mmio_enabled = _scsih_pci_mmio_enabled,
	.slot_reset =	_scsih_pci_slot_reset,
	.resume =	_scsih_pci_resume,
};

static struct pci_driver scsih_driver = {
	.name		= MPT2SAS_DRIVER_NAME,
	.id_table	= scsih_pci_table,
	.probe		= _scsih_probe,
	.remove		= _scsih_remove,
	.shutdown	= _scsih_shutdown,
	.err_handler	= &_scsih_err_handler,
#ifdef CONFIG_PM
	.suspend	= _scsih_suspend,
	.resume		= _scsih_resume,
#endif
};

/* raid transport support */
static struct raid_function_template mpt2sas_raid_functions = {
	.cookie		= &scsih_driver_template,
	.is_raid	= _scsih_is_raid,
	.get_resync	= _scsih_get_resync,
	.get_state	= _scsih_get_state,
};

/**
 * _scsih_init - main entry point for this driver.
 *
 * Returns 0 success, anything else error.
 */
static int __init
_scsih_init(void)
{
	int error;

	mpt_ids = 0;
	printk(KERN_INFO "%s version %s loaded\n", MPT2SAS_DRIVER_NAME,
	    MPT2SAS_DRIVER_VERSION);

	mpt2sas_transport_template =
	    sas_attach_transport(&mpt2sas_transport_functions);
	if (!mpt2sas_transport_template)
		return -ENODEV;
	/* raid transport support */
	mpt2sas_raid_template = raid_class_attach(&mpt2sas_raid_functions);
	if (!mpt2sas_raid_template) {
		sas_release_transport(mpt2sas_transport_template);
		return -ENODEV;
	}

	mpt2sas_base_initialize_callback_handler();

	 /* queuecommand callback hander */
	scsi_io_cb_idx = mpt2sas_base_register_callback_handler(_scsih_io_done);

	/* task management callback handler */
	tm_cb_idx = mpt2sas_base_register_callback_handler(_scsih_tm_done);

	/* base internal commands callback handler */
	base_cb_idx = mpt2sas_base_register_callback_handler(mpt2sas_base_done);
	port_enable_cb_idx = mpt2sas_base_register_callback_handler(
		mpt2sas_port_enable_done);

	/* transport internal commands callback handler */
	transport_cb_idx = mpt2sas_base_register_callback_handler(
	    mpt2sas_transport_done);

	/* scsih internal commands callback handler */
	scsih_cb_idx = mpt2sas_base_register_callback_handler(_scsih_done);

	/* configuration page API internal commands callback handler */
	config_cb_idx = mpt2sas_base_register_callback_handler(
	    mpt2sas_config_done);

	/* ctl module callback handler */
	ctl_cb_idx = mpt2sas_base_register_callback_handler(mpt2sas_ctl_done);

	tm_tr_cb_idx = mpt2sas_base_register_callback_handler(
	    _scsih_tm_tr_complete);

	tm_tr_volume_cb_idx = mpt2sas_base_register_callback_handler(
	    _scsih_tm_volume_tr_complete);

	tm_sas_control_cb_idx = mpt2sas_base_register_callback_handler(
	    _scsih_sas_control_complete);

	mpt2sas_ctl_init();

	error = pci_register_driver(&scsih_driver);
	if (error) {
		/* raid transport support */
		raid_class_release(mpt2sas_raid_template);
		sas_release_transport(mpt2sas_transport_template);
	}

	return error;
}

/**
 * _scsih_exit - exit point for this driver (when it is a module).
 *
 * Returns 0 success, anything else error.
 */
static void __exit
_scsih_exit(void)
{
	printk(KERN_INFO "mpt2sas version %s unloading\n",
	    MPT2SAS_DRIVER_VERSION);

	pci_unregister_driver(&scsih_driver);

	mpt2sas_ctl_exit();

	mpt2sas_base_release_callback_handler(scsi_io_cb_idx);
	mpt2sas_base_release_callback_handler(tm_cb_idx);
	mpt2sas_base_release_callback_handler(base_cb_idx);
	mpt2sas_base_release_callback_handler(port_enable_cb_idx);
	mpt2sas_base_release_callback_handler(transport_cb_idx);
	mpt2sas_base_release_callback_handler(scsih_cb_idx);
	mpt2sas_base_release_callback_handler(config_cb_idx);
	mpt2sas_base_release_callback_handler(ctl_cb_idx);

	mpt2sas_base_release_callback_handler(tm_tr_cb_idx);
	mpt2sas_base_release_callback_handler(tm_tr_volume_cb_idx);
	mpt2sas_base_release_callback_handler(tm_sas_control_cb_idx);

	/* raid transport support */
	raid_class_release(mpt2sas_raid_template);
	sas_release_transport(mpt2sas_transport_template);

}

module_init(_scsih_init);
module_exit(_scsih_exit);

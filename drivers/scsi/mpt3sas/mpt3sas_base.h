/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.h
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

#ifndef MPT3SAS_BASE_H_INCLUDED
#define MPT3SAS_BASE_H_INCLUDED

#include "mpi/mpi2_type.h"
#include "mpi/mpi2.h"
#include "mpi/mpi2_ioc.h"
#include "mpi/mpi2_cnfg.h"
#include "mpi/mpi2_init.h"
#include "mpi/mpi2_raid.h"
#include "mpi/mpi2_tool.h"
#include "mpi/mpi2_sas.h"

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <linux/pci.h>
#include <linux/poll.h>

#include "mpt3sas_debug.h"
#include "mpt3sas_trigger_diag.h"

/* driver versioning info */
#define MPT3SAS_DRIVER_NAME		"mpt3sas"
#define MPT3SAS_AUTHOR "Avago Technologies <MPT-FusionLinux.pdl@avagotech.com>"
#define MPT3SAS_DESCRIPTION	"LSI MPT Fusion SAS 3.0 Device Driver"
#define MPT3SAS_DRIVER_VERSION		"15.100.00.00"
#define MPT3SAS_MAJOR_VERSION		15
#define MPT3SAS_MINOR_VERSION		100
#define MPT3SAS_BUILD_VERSION		0
#define MPT3SAS_RELEASE_VERSION	00

#define MPT2SAS_DRIVER_NAME		"mpt2sas"
#define MPT2SAS_DESCRIPTION	"LSI MPT Fusion SAS 2.0 Device Driver"
#define MPT2SAS_DRIVER_VERSION		"20.102.00.00"
#define MPT2SAS_MAJOR_VERSION		20
#define MPT2SAS_MINOR_VERSION		102
#define MPT2SAS_BUILD_VERSION		0
#define MPT2SAS_RELEASE_VERSION	00

/*
 * Set MPT3SAS_SG_DEPTH value based on user input.
 */
#define MPT_MAX_PHYS_SEGMENTS	SG_CHUNK_SIZE
#define MPT_MIN_PHYS_SEGMENTS	16

#ifdef CONFIG_SCSI_MPT3SAS_MAX_SGE
#define MPT3SAS_SG_DEPTH		CONFIG_SCSI_MPT3SAS_MAX_SGE
#else
#define MPT3SAS_SG_DEPTH		MPT_MAX_PHYS_SEGMENTS
#endif

#ifdef CONFIG_SCSI_MPT2SAS_MAX_SGE
#define MPT2SAS_SG_DEPTH		CONFIG_SCSI_MPT2SAS_MAX_SGE
#else
#define MPT2SAS_SG_DEPTH		MPT_MAX_PHYS_SEGMENTS
#endif

/*
 * Generic Defines
 */
#define MPT3SAS_SATA_QUEUE_DEPTH	32
#define MPT3SAS_SAS_QUEUE_DEPTH		254
#define MPT3SAS_RAID_QUEUE_DEPTH	128

#define MPT3SAS_RAID_MAX_SECTORS	8192

#define MPT_NAME_LENGTH			32	/* generic length of strings */
#define MPT_STRING_LENGTH		64

#define MPT_MAX_CALLBACKS		32

#define INTERNAL_CMDS_COUNT		10	/* reserved cmds */
/* reserved for issuing internally framed scsi io cmds */
#define INTERNAL_SCSIIO_CMDS_COUNT	3

#define MPI3_HIM_MASK			0xFFFFFFFF /* mask every bit*/

#define MPT3SAS_INVALID_DEVICE_HANDLE	0xFFFF

#define MAX_CHAIN_ELEMT_SZ		16
#define DEFAULT_NUM_FWCHAIN_ELEMTS	8

/*
 * reset phases
 */
#define MPT3_IOC_PRE_RESET		1 /* prior to host reset */
#define MPT3_IOC_AFTER_RESET		2 /* just after host reset */
#define MPT3_IOC_DONE_RESET		3 /* links re-initialized */

/*
 * logging format
 */
#define MPT3SAS_FMT			"%s: "

/*
 *  WarpDrive Specific Log codes
 */

#define MPT2_WARPDRIVE_LOGENTRY		(0x8002)
#define MPT2_WARPDRIVE_LC_SSDT			(0x41)
#define MPT2_WARPDRIVE_LC_SSDLW		(0x43)
#define MPT2_WARPDRIVE_LC_SSDLF		(0x44)
#define MPT2_WARPDRIVE_LC_BRMF			(0x4D)

/*
 * per target private data
 */
#define MPT_TARGET_FLAGS_RAID_COMPONENT	0x01
#define MPT_TARGET_FLAGS_VOLUME		0x02
#define MPT_TARGET_FLAGS_DELETED	0x04
#define MPT_TARGET_FASTPATH_IO		0x08

#define SAS2_PCI_DEVICE_B0_REVISION	(0x01)
#define SAS3_PCI_DEVICE_C0_REVISION	(0x02)

/*
 * Intel HBA branding
 */
#define MPT2SAS_INTEL_RMS25JB080_BRANDING    \
	"Intel(R) Integrated RAID Module RMS25JB080"
#define MPT2SAS_INTEL_RMS25JB040_BRANDING    \
	"Intel(R) Integrated RAID Module RMS25JB040"
#define MPT2SAS_INTEL_RMS25KB080_BRANDING    \
	"Intel(R) Integrated RAID Module RMS25KB080"
#define MPT2SAS_INTEL_RMS25KB040_BRANDING    \
	"Intel(R) Integrated RAID Module RMS25KB040"
#define MPT2SAS_INTEL_RMS25LB040_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25LB040"
#define MPT2SAS_INTEL_RMS25LB080_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25LB080"
#define MPT2SAS_INTEL_RMS2LL080_BRANDING	\
	"Intel Integrated RAID Module RMS2LL080"
#define MPT2SAS_INTEL_RMS2LL040_BRANDING	\
	"Intel Integrated RAID Module RMS2LL040"
#define MPT2SAS_INTEL_RS25GB008_BRANDING       \
	"Intel(R) RAID Controller RS25GB008"
#define MPT2SAS_INTEL_SSD910_BRANDING          \
	"Intel(R) SSD 910 Series"

#define MPT3SAS_INTEL_RMS3JC080_BRANDING       \
	"Intel(R) Integrated RAID Module RMS3JC080"
#define MPT3SAS_INTEL_RS3GC008_BRANDING       \
	"Intel(R) RAID Controller RS3GC008"
#define MPT3SAS_INTEL_RS3FC044_BRANDING       \
	"Intel(R) RAID Controller RS3FC044"
#define MPT3SAS_INTEL_RS3UC080_BRANDING       \
	"Intel(R) RAID Controller RS3UC080"

/*
 * Intel HBA SSDIDs
 */
#define MPT2SAS_INTEL_RMS25JB080_SSDID		0x3516
#define MPT2SAS_INTEL_RMS25JB040_SSDID		0x3517
#define MPT2SAS_INTEL_RMS25KB080_SSDID		0x3518
#define MPT2SAS_INTEL_RMS25KB040_SSDID		0x3519
#define MPT2SAS_INTEL_RMS25LB040_SSDID		0x351A
#define MPT2SAS_INTEL_RMS25LB080_SSDID		0x351B
#define MPT2SAS_INTEL_RMS2LL080_SSDID		0x350E
#define MPT2SAS_INTEL_RMS2LL040_SSDID		0x350F
#define MPT2SAS_INTEL_RS25GB008_SSDID		0x3000
#define MPT2SAS_INTEL_SSD910_SSDID		0x3700

#define MPT3SAS_INTEL_RMS3JC080_SSDID		0x3521
#define MPT3SAS_INTEL_RS3GC008_SSDID		0x3522
#define MPT3SAS_INTEL_RS3FC044_SSDID		0x3523
#define MPT3SAS_INTEL_RS3UC080_SSDID		0x3524

/*
 * Dell HBA branding
 */
#define MPT2SAS_DELL_BRANDING_SIZE                 32

#define MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING        "Dell 6Gbps SAS HBA"
#define MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING    "Dell PERC H200 Adapter"
#define MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING "Dell PERC H200 Integrated"
#define MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING    "Dell PERC H200 Modular"
#define MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING   "Dell PERC H200 Embedded"
#define MPT2SAS_DELL_PERC_H200_BRANDING            "Dell PERC H200"
#define MPT2SAS_DELL_6GBPS_SAS_BRANDING            "Dell 6Gbps SAS"

#define MPT3SAS_DELL_12G_HBA_BRANDING       \
	"Dell 12Gbps HBA"

/*
 * Dell HBA SSDIDs
 */
#define MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID	0x1F1C
#define MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID	0x1F1D
#define MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID	0x1F1E
#define MPT2SAS_DELL_PERC_H200_MODULAR_SSDID	0x1F1F
#define MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID	0x1F20
#define MPT2SAS_DELL_PERC_H200_SSDID		0x1F21
#define MPT2SAS_DELL_6GBPS_SAS_SSDID		0x1F22

#define MPT3SAS_DELL_12G_HBA_SSDID		0x1F46

/*
 * Cisco HBA branding
 */
#define MPT3SAS_CISCO_12G_8E_HBA_BRANDING		\
	"Cisco 9300-8E 12G SAS HBA"
#define MPT3SAS_CISCO_12G_8I_HBA_BRANDING		\
	"Cisco 9300-8i 12G SAS HBA"
#define MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING	\
	"Cisco 12G Modular SAS Pass through Controller"
#define MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_BRANDING		\
	"UCS C3X60 12G SAS Pass through Controller"
/*
 * Cisco HBA SSSDIDs
 */
#define MPT3SAS_CISCO_12G_8E_HBA_SSDID  0x14C
#define MPT3SAS_CISCO_12G_8I_HBA_SSDID  0x154
#define MPT3SAS_CISCO_12G_AVILA_HBA_SSDID  0x155
#define MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_SSDID  0x156

/*
 * status bits for ioc->diag_buffer_status
 */
#define MPT3_DIAG_BUFFER_IS_REGISTERED	(0x01)
#define MPT3_DIAG_BUFFER_IS_RELEASED	(0x02)
#define MPT3_DIAG_BUFFER_IS_DIAG_RESET	(0x04)

/*
 * HP HBA branding
 */
#define MPT2SAS_HP_3PAR_SSVID                0x1590

#define MPT2SAS_HP_2_4_INTERNAL_BRANDING	\
	"HP H220 Host Bus Adapter"
#define MPT2SAS_HP_2_4_EXTERNAL_BRANDING	\
	"HP H221 Host Bus Adapter"
#define MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_BRANDING	\
	"HP H222 Host Bus Adapter"
#define MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_BRANDING	\
	"HP H220i Host Bus Adapter"
#define MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_BRANDING	\
	"HP H210i Host Bus Adapter"

/*
 * HO HBA SSDIDs
 */
#define MPT2SAS_HP_2_4_INTERNAL_SSDID			0x0041
#define MPT2SAS_HP_2_4_EXTERNAL_SSDID			0x0042
#define MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID	0x0043
#define MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID		0x0044
#define MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID		0x0046

/*
 * Combined Reply Queue constants,
 * There are twelve Supplemental Reply Post Host Index Registers
 * and each register is at offset 0x10 bytes from the previous one.
 */
#define MPT3_SUP_REPLY_POST_HOST_INDEX_REG_COUNT_G3	12
#define MPT3_SUP_REPLY_POST_HOST_INDEX_REG_COUNT_G35	16
#define MPT3_SUP_REPLY_POST_HOST_INDEX_REG_OFFSET	(0x10)

/* OEM Identifiers */
#define MFG10_OEM_ID_INVALID                   (0x00000000)
#define MFG10_OEM_ID_DELL                      (0x00000001)
#define MFG10_OEM_ID_FSC                       (0x00000002)
#define MFG10_OEM_ID_SUN                       (0x00000003)
#define MFG10_OEM_ID_IBM                       (0x00000004)

/* GENERIC Flags 0*/
#define MFG10_GF0_OCE_DISABLED                 (0x00000001)
#define MFG10_GF0_R1E_DRIVE_COUNT              (0x00000002)
#define MFG10_GF0_R10_DISPLAY                  (0x00000004)
#define MFG10_GF0_SSD_DATA_SCRUB_DISABLE       (0x00000008)
#define MFG10_GF0_SINGLE_DRIVE_R0              (0x00000010)

#define VIRTUAL_IO_FAILED_RETRY			(0x32010081)

/* OEM Specific Flags will come from OEM specific header files */
struct Mpi2ManufacturingPage10_t {
	MPI2_CONFIG_PAGE_HEADER	Header;		/* 00h */
	U8	OEMIdentifier;			/* 04h */
	U8	Reserved1;			/* 05h */
	U16	Reserved2;			/* 08h */
	U32	Reserved3;			/* 0Ch */
	U32	GenericFlags0;			/* 10h */
	U32	GenericFlags1;			/* 14h */
	U32	Reserved4;			/* 18h */
	U32	OEMSpecificFlags0;		/* 1Ch */
	U32	OEMSpecificFlags1;		/* 20h */
	U32	Reserved5[18];			/* 24h - 60h*/
};


/* Miscellaneous options */
struct Mpi2ManufacturingPage11_t {
	MPI2_CONFIG_PAGE_HEADER Header;		/* 00h */
	__le32	Reserved1;			/* 04h */
	u8	Reserved2;			/* 08h */
	u8	EEDPTagMode;			/* 09h */
	u8	Reserved3;			/* 0Ah */
	u8	Reserved4;			/* 0Bh */
	__le32	Reserved5[23];			/* 0Ch-60h*/
};

/**
 * struct MPT3SAS_TARGET - starget private hostdata
 * @starget: starget object
 * @sas_address: target sas address
 * @raid_device: raid_device pointer to access volume data
 * @handle: device handle
 * @num_luns: number luns
 * @flags: MPT_TARGET_FLAGS_XXX flags
 * @deleted: target flaged for deletion
 * @tm_busy: target is busy with TM request.
 * @sdev: The sas_device associated with this target
 */
struct MPT3SAS_TARGET {
	struct scsi_target *starget;
	u64	sas_address;
	struct _raid_device *raid_device;
	u16	handle;
	int	num_luns;
	u32	flags;
	u8	deleted;
	u8	tm_busy;
	struct _sas_device *sdev;
};


/*
 * per device private data
 */
#define MPT_DEVICE_FLAGS_INIT		0x01

#define MFG_PAGE10_HIDE_SSDS_MASK	(0x00000003)
#define MFG_PAGE10_HIDE_ALL_DISKS	(0x00)
#define MFG_PAGE10_EXPOSE_ALL_DISKS	(0x01)
#define MFG_PAGE10_HIDE_IF_VOL_PRESENT	(0x02)

/**
 * struct MPT3SAS_DEVICE - sdev private hostdata
 * @sas_target: starget private hostdata
 * @lun: lun number
 * @flags: MPT_DEVICE_XXX flags
 * @configured_lun: lun is configured
 * @block: device is in SDEV_BLOCK state
 * @tlr_snoop_check: flag used in determining whether to disable TLR
 * @eedp_enable: eedp support enable bit
 * @eedp_type: 0(type_1), 1(type_2), 2(type_3)
 * @eedp_block_length: block size
 * @ata_command_pending: SATL passthrough outstanding for device
 */
struct MPT3SAS_DEVICE {
	struct MPT3SAS_TARGET *sas_target;
	unsigned int	lun;
	u32	flags;
	u8	configured_lun;
	u8	block;
	u8	tlr_snoop_check;
	u8	ignore_delay_remove;
	/* Iopriority Command Handling */
	u8	ncq_prio_enable;
	/*
	 * Bug workaround for SATL handling: the mpt2/3sas firmware
	 * doesn't return BUSY or TASK_SET_FULL for subsequent
	 * commands while a SATL pass through is in operation as the
	 * spec requires, it simply does nothing with them until the
	 * pass through completes, causing them possibly to timeout if
	 * the passthrough is a long executing command (like format or
	 * secure erase).  This variable allows us to do the right
	 * thing while a SATL command is pending.
	 */
	unsigned long ata_command_pending;

};

#define MPT3_CMD_NOT_USED	0x8000	/* free */
#define MPT3_CMD_COMPLETE	0x0001	/* completed */
#define MPT3_CMD_PENDING	0x0002	/* pending */
#define MPT3_CMD_REPLY_VALID	0x0004	/* reply is valid */
#define MPT3_CMD_RESET		0x0008	/* host reset dropped the command */

/**
 * struct _internal_cmd - internal commands struct
 * @mutex: mutex
 * @done: completion
 * @reply: reply message pointer
 * @sense: sense data
 * @status: MPT3_CMD_XXX status
 * @smid: system message id
 */
struct _internal_cmd {
	struct mutex mutex;
	struct completion done;
	void	*reply;
	void	*sense;
	u16	status;
	u16	smid;
};



/**
 * struct _sas_device - attached device information
 * @list: sas device list
 * @starget: starget object
 * @sas_address: device sas address
 * @device_name: retrieved from the SAS IDENTIFY frame.
 * @handle: device handle
 * @sas_address_parent: sas address of parent expander or sas host
 * @enclosure_handle: enclosure handle
 * @enclosure_logical_id: enclosure logical identifier
 * @volume_handle: volume handle (valid when hidden raid member)
 * @volume_wwid: volume unique identifier
 * @device_info: bitfield provides detailed info about the device
 * @id: target id
 * @channel: target channel
 * @slot: number number
 * @phy: phy identifier provided in sas device page 0
 * @responding: used in _scsih_sas_device_mark_responding
 * @fast_path: fast path feature enable bit
 * @pfa_led_on: flag for PFA LED status
 * @pend_sas_rphy_add: flag to check if device is in sas_rphy_add()
 *	addition routine.
 */
struct _sas_device {
	struct list_head list;
	struct scsi_target *starget;
	u64	sas_address;
	u64	device_name;
	u16	handle;
	u64	sas_address_parent;
	u16	enclosure_handle;
	u64	enclosure_logical_id;
	u16	volume_handle;
	u64	volume_wwid;
	u32	device_info;
	int	id;
	int	channel;
	u16	slot;
	u8	phy;
	u8	responding;
	u8	fast_path;
	u8	pfa_led_on;
	u8	pend_sas_rphy_add;
	u8	enclosure_level;
	u8	connector_name[5];
	struct kref refcount;
};

static inline void sas_device_get(struct _sas_device *s)
{
	kref_get(&s->refcount);
}

static inline void sas_device_free(struct kref *r)
{
	kfree(container_of(r, struct _sas_device, refcount));
}

static inline void sas_device_put(struct _sas_device *s)
{
	kref_put(&s->refcount, sas_device_free);
}

/**
 * struct _raid_device - raid volume link list
 * @list: sas device list
 * @starget: starget object
 * @sdev: scsi device struct (volumes are single lun)
 * @wwid: unique identifier for the volume
 * @handle: device handle
 * @block_size: Block size of the volume
 * @id: target id
 * @channel: target channel
 * @volume_type: the raid level
 * @device_info: bitfield provides detailed info about the hidden components
 * @num_pds: number of hidden raid components
 * @responding: used in _scsih_raid_device_mark_responding
 * @percent_complete: resync percent complete
 * @direct_io_enabled: Whether direct io to PDs are allowed or not
 * @stripe_exponent: X where 2powX is the stripe sz in blocks
 * @block_exponent: X where 2powX is the block sz in bytes
 * @max_lba: Maximum number of LBA in the volume
 * @stripe_sz: Stripe Size of the volume
 * @device_info: Device info of the volume member disk
 * @pd_handle: Array of handles of the physical drives for direct I/O in le16
 */
#define MPT_MAX_WARPDRIVE_PDS		8
struct _raid_device {
	struct list_head list;
	struct scsi_target *starget;
	struct scsi_device *sdev;
	u64	wwid;
	u16	handle;
	u16	block_sz;
	int	id;
	int	channel;
	u8	volume_type;
	u8	num_pds;
	u8	responding;
	u8	percent_complete;
	u8	direct_io_enabled;
	u8	stripe_exponent;
	u8	block_exponent;
	u64	max_lba;
	u32	stripe_sz;
	u32	device_info;
	u16	pd_handle[MPT_MAX_WARPDRIVE_PDS];
};

/**
 * struct _boot_device - boot device info
 * @is_raid: flag to indicate whether this is volume
 * @device: holds pointer for either struct _sas_device or
 *     struct _raid_device
 */
struct _boot_device {
	u8 is_raid;
	void *device;
};

/**
 * struct _sas_port - wide/narrow sas port information
 * @port_list: list of ports belonging to expander
 * @num_phys: number of phys belonging to this port
 * @remote_identify: attached device identification
 * @rphy: sas transport rphy object
 * @port: sas transport wide/narrow port object
 * @phy_list: _sas_phy list objects belonging to this port
 */
struct _sas_port {
	struct list_head port_list;
	u8	num_phys;
	struct sas_identify remote_identify;
	struct sas_rphy *rphy;
	struct sas_port *port;
	struct list_head phy_list;
};

/**
 * struct _sas_phy - phy information
 * @port_siblings: list of phys belonging to a port
 * @identify: phy identification
 * @remote_identify: attached device identification
 * @phy: sas transport phy object
 * @phy_id: unique phy id
 * @handle: device handle for this phy
 * @attached_handle: device handle for attached device
 * @phy_belongs_to_port: port has been created for this phy
 */
struct _sas_phy {
	struct list_head port_siblings;
	struct sas_identify identify;
	struct sas_identify remote_identify;
	struct sas_phy *phy;
	u8	phy_id;
	u16	handle;
	u16	attached_handle;
	u8	phy_belongs_to_port;
};

/**
 * struct _sas_node - sas_host/expander information
 * @list: list of expanders
 * @parent_dev: parent device class
 * @num_phys: number phys belonging to this sas_host/expander
 * @sas_address: sas address of this sas_host/expander
 * @handle: handle for this sas_host/expander
 * @sas_address_parent: sas address of parent expander or sas host
 * @enclosure_handle: handle for this a member of an enclosure
 * @device_info: bitwise defining capabilities of this sas_host/expander
 * @responding: used in _scsih_expander_device_mark_responding
 * @phy: a list of phys that make up this sas_host/expander
 * @sas_port_list: list of ports attached to this sas_host/expander
 */
struct _sas_node {
	struct list_head list;
	struct device *parent_dev;
	u8	num_phys;
	u64	sas_address;
	u16	handle;
	u64	sas_address_parent;
	u16	enclosure_handle;
	u64	enclosure_logical_id;
	u8	responding;
	struct	_sas_phy *phy;
	struct list_head sas_port_list;
};

/**
 * enum reset_type - reset state
 * @FORCE_BIG_HAMMER: issue diagnostic reset
 * @SOFT_RESET: issue message_unit_reset, if fails to to big hammer
 */
enum reset_type {
	FORCE_BIG_HAMMER,
	SOFT_RESET,
};

/**
 * struct chain_tracker - firmware chain tracker
 * @chain_buffer: chain buffer
 * @chain_buffer_dma: physical address
 * @tracker_list: list of free request (ioc->free_chain_list)
 */
struct chain_tracker {
	void *chain_buffer;
	dma_addr_t chain_buffer_dma;
	struct list_head tracker_list;
};

/**
 * struct scsiio_tracker - scsi mf request tracker
 * @smid: system message id
 * @scmd: scsi request pointer
 * @cb_idx: callback index
 * @direct_io: To indicate whether I/O is direct (WARPDRIVE)
 * @tracker_list: list of free request (ioc->free_list)
 * @msix_io: IO's msix
 */
struct scsiio_tracker {
	u16	smid;
	struct scsi_cmnd *scmd;
	u8	cb_idx;
	u8	direct_io;
	struct list_head chain_list;
	struct list_head tracker_list;
	u16     msix_io;
};

/**
 * struct request_tracker - firmware request tracker
 * @smid: system message id
 * @cb_idx: callback index
 * @tracker_list: list of free request (ioc->free_list)
 */
struct request_tracker {
	u16	smid;
	u8	cb_idx;
	struct list_head tracker_list;
};

/**
 * struct _tr_list - target reset list
 * @handle: device handle
 * @state: state machine
 */
struct _tr_list {
	struct list_head list;
	u16	handle;
	u16	state;
};

/**
 * struct _sc_list - delayed SAS_IO_UNIT_CONTROL message list
 * @handle: device handle
 */
struct _sc_list {
	struct list_head list;
	u16     handle;
};

/**
 * struct _event_ack_list - delayed event acknowledgment list
 * @Event: Event ID
 * @EventContext: used to track the event uniquely
 */
struct _event_ack_list {
	struct list_head list;
	u16     Event;
	u32     EventContext;
};

/**
 * struct adapter_reply_queue - the reply queue struct
 * @ioc: per adapter object
 * @msix_index: msix index into vector table
 * @vector: irq vector
 * @reply_post_host_index: head index in the pool where FW completes IO
 * @reply_post_free: reply post base virt address
 * @name: the name registered to request_irq()
 * @busy: isr is actively processing replies on another cpu
 * @list: this list
*/
struct adapter_reply_queue {
	struct MPT3SAS_ADAPTER	*ioc;
	u8			msix_index;
	u32			reply_post_host_index;
	Mpi2ReplyDescriptorsUnion_t *reply_post_free;
	char			name[MPT_NAME_LENGTH];
	atomic_t		busy;
	struct list_head	list;
};

typedef void (*MPT_ADD_SGE)(void *paddr, u32 flags_length, dma_addr_t dma_addr);

/* SAS3.0 support */
typedef int (*MPT_BUILD_SG_SCMD)(struct MPT3SAS_ADAPTER *ioc,
		struct scsi_cmnd *scmd, u16 smid);
typedef void (*MPT_BUILD_SG)(struct MPT3SAS_ADAPTER *ioc, void *psge,
		dma_addr_t data_out_dma, size_t data_out_sz,
		dma_addr_t data_in_dma, size_t data_in_sz);
typedef void (*MPT_BUILD_ZERO_LEN_SGE)(struct MPT3SAS_ADAPTER *ioc,
		void *paddr);

/* To support atomic and non atomic descriptors*/
typedef void (*PUT_SMID_IO_FP_HIP) (struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 funcdep);
typedef void (*PUT_SMID_DEFAULT) (struct MPT3SAS_ADAPTER *ioc, u16 smid);

/* IOC Facts and Port Facts converted from little endian to cpu */
union mpi3_version_union {
	MPI2_VERSION_STRUCT		Struct;
	u32				Word;
};

struct mpt3sas_facts {
	u16			MsgVersion;
	u16			HeaderVersion;
	u8			IOCNumber;
	u8			VP_ID;
	u8			VF_ID;
	u16			IOCExceptions;
	u16			IOCStatus;
	u32			IOCLogInfo;
	u8			MaxChainDepth;
	u8			WhoInit;
	u8			NumberOfPorts;
	u8			MaxMSIxVectors;
	u16			RequestCredit;
	u16			ProductID;
	u32			IOCCapabilities;
	union mpi3_version_union	FWVersion;
	u16			IOCRequestFrameSize;
	u16			IOCMaxChainSegmentSize;
	u16			MaxInitiators;
	u16			MaxTargets;
	u16			MaxSasExpanders;
	u16			MaxEnclosures;
	u16			ProtocolFlags;
	u16			HighPriorityCredit;
	u16			MaxReplyDescriptorPostQueueDepth;
	u8			ReplyFrameSize;
	u8			MaxVolumes;
	u16			MaxDevHandle;
	u16			MaxPersistentEntries;
	u16			MinDevHandle;
};

struct mpt3sas_port_facts {
	u8			PortNumber;
	u8			VP_ID;
	u8			VF_ID;
	u8			PortType;
	u16			MaxPostedCmdBuffers;
};

struct reply_post_struct {
	Mpi2ReplyDescriptorsUnion_t	*reply_post_free;
	dma_addr_t			reply_post_free_dma;
};

typedef void (*MPT3SAS_FLUSH_RUNNING_CMDS)(struct MPT3SAS_ADAPTER *ioc);
/**
 * struct MPT3SAS_ADAPTER - per adapter struct
 * @list: ioc_list
 * @shost: shost object
 * @id: unique adapter id
 * @cpu_count: number online cpus
 * @name: generic ioc string
 * @tmp_string: tmp string used for logging
 * @pdev: pci pdev object
 * @pio_chip: physical io register space
 * @chip: memory mapped register space
 * @chip_phys: physical addrss prior to mapping
 * @logging_level: see mpt3sas_debug.h
 * @fwfault_debug: debuging FW timeouts
 * @ir_firmware: IR firmware present
 * @bars: bitmask of BAR's that must be configured
 * @mask_interrupts: ignore interrupt
 * @dma_mask: used to set the consistent dma mask
 * @fault_reset_work_q_name: fw fault work queue
 * @fault_reset_work_q: ""
 * @fault_reset_work: ""
 * @firmware_event_name: fw event work queue
 * @firmware_event_thread: ""
 * @fw_event_lock:
 * @fw_event_list: list of fw events
 * @aen_event_read_flag: event log was read
 * @broadcast_aen_busy: broadcast aen waiting to be serviced
 * @shost_recovery: host reset in progress
 * @ioc_reset_in_progress_lock:
 * @ioc_link_reset_in_progress: phy/hard reset in progress
 * @ignore_loginfos: ignore loginfos during task management
 * @remove_host: flag for when driver unloads, to avoid sending dev resets
 * @pci_error_recovery: flag to prevent ioc access until slot reset completes
 * @wait_for_discovery_to_complete: flag set at driver load time when
 *                                               waiting on reporting devices
 * @is_driver_loading: flag set at driver load time
 * @port_enable_failed: flag set when port enable has failed
 * @start_scan: flag set from scan_start callback, cleared from _mpt3sas_fw_work
 * @start_scan_failed: means port enable failed, return's the ioc_status
 * @msix_enable: flag indicating msix is enabled
 * @msix_vector_count: number msix vectors
 * @cpu_msix_table: table for mapping cpus to msix index
 * @cpu_msix_table_sz: table size
 * @schedule_dead_ioc_flush_running_cmds: callback to flush pending commands
 * @scsi_io_cb_idx: shost generated commands
 * @tm_cb_idx: task management commands
 * @scsih_cb_idx: scsih internal commands
 * @transport_cb_idx: transport internal commands
 * @ctl_cb_idx: clt internal commands
 * @base_cb_idx: base internal commands
 * @config_cb_idx: base internal commands
 * @tm_tr_cb_idx : device removal target reset handshake
 * @tm_tr_volume_cb_idx : volume removal target reset
 * @base_cmds:
 * @transport_cmds:
 * @scsih_cmds:
 * @tm_cmds:
 * @ctl_cmds:
 * @config_cmds:
 * @base_add_sg_single: handler for either 32/64 bit sgl's
 * @event_type: bits indicating which events to log
 * @event_context: unique id for each logged event
 * @event_log: event log pointer
 * @event_masks: events that are masked
 * @facts: static facts data
 * @pfacts: static port facts data
 * @manu_pg0: static manufacturing page 0
 * @manu_pg10: static manufacturing page 10
 * @manu_pg11: static manufacturing page 11
 * @bios_pg2: static bios page 2
 * @bios_pg3: static bios page 3
 * @ioc_pg8: static ioc page 8
 * @iounit_pg0: static iounit page 0
 * @iounit_pg1: static iounit page 1
 * @iounit_pg8: static iounit page 8
 * @sas_hba: sas host object
 * @sas_expander_list: expander object list
 * @sas_node_lock:
 * @sas_device_list: sas device object list
 * @sas_device_init_list: sas device object list (used only at init time)
 * @sas_device_lock:
 * @io_missing_delay: time for IO completed by fw when PDR enabled
 * @device_missing_delay: time for device missing by fw when PDR enabled
 * @sas_id : used for setting volume target IDs
 * @blocking_handles: bitmask used to identify which devices need blocking
 * @pd_handles : bitmask for PD handles
 * @pd_handles_sz : size of pd_handle bitmask
 * @config_page_sz: config page size
 * @config_page: reserve memory for config page payload
 * @config_page_dma:
 * @hba_queue_depth: hba request queue depth
 * @sge_size: sg element size for either 32/64 bit
 * @scsiio_depth: SCSI_IO queue depth
 * @request_sz: per request frame size
 * @request: pool of request frames
 * @request_dma:
 * @request_dma_sz:
 * @scsi_lookup: firmware request tracker list
 * @scsi_lookup_lock:
 * @free_list: free list of request
 * @pending_io_count:
 * @reset_wq:
 * @chain: pool of chains
 * @chain_dma:
 * @max_sges_in_main_message: number sg elements in main message
 * @max_sges_in_chain_message: number sg elements per chain
 * @chains_needed_per_io: max chains per io
 * @chain_depth: total chains allocated
 * @chain_segment_sz: gives the max number of
 *			SGEs accommodate on single chain buffer
 * @hi_priority_smid:
 * @hi_priority:
 * @hi_priority_dma:
 * @hi_priority_depth:
 * @hpr_lookup:
 * @hpr_free_list:
 * @internal_smid:
 * @internal:
 * @internal_dma:
 * @internal_depth:
 * @internal_lookup:
 * @internal_free_list:
 * @sense: pool of sense
 * @sense_dma:
 * @sense_dma_pool:
 * @reply_depth: hba reply queue depth:
 * @reply_sz: per reply frame size:
 * @reply: pool of replys:
 * @reply_dma:
 * @reply_dma_pool:
 * @reply_free_queue_depth: reply free depth
 * @reply_free: pool for reply free queue (32 bit addr)
 * @reply_free_dma:
 * @reply_free_dma_pool:
 * @reply_free_host_index: tail index in pool to insert free replys
 * @reply_post_queue_depth: reply post queue depth
 * @reply_post_struct: struct for reply_post_free physical & virt address
 * @rdpq_array_capable: FW supports multiple reply queue addresses in ioc_init
 * @rdpq_array_enable: rdpq_array support is enabled in the driver
 * @rdpq_array_enable_assigned: this ensures that rdpq_array_enable flag
 *				is assigned only ones
 * @reply_queue_count: number of reply queue's
 * @reply_queue_list: link list contaning the reply queue info
 * @msix96_vector: 96 MSI-X vector support
 * @replyPostRegisterIndex: index of next position in Reply Desc Post Queue
 * @delayed_tr_list: target reset link list
 * @delayed_tr_volume_list: volume target reset link list
 * @delayed_sc_list:
 * @delayed_event_ack_list:
 * @temp_sensors_count: flag to carry the number of temperature sensors
 * @pci_access_mutex: Mutex to synchronize ioctl,sysfs show path and
 *	pci resource handling. PCI resource freeing will lead to free
 *	vital hardware/memory resource, which might be in use by cli/sysfs
 *	path functions resulting in Null pointer reference followed by kernel
 *	crash. To avoid the above race condition we use mutex syncrhonization
 *	which ensures the syncrhonization between cli/sysfs_show path.
 */
struct MPT3SAS_ADAPTER {
	struct list_head list;
	struct Scsi_Host *shost;
	u8		id;
	int		cpu_count;
	char		name[MPT_NAME_LENGTH];
	char		driver_name[MPT_NAME_LENGTH];
	char		tmp_string[MPT_STRING_LENGTH];
	struct pci_dev	*pdev;
	Mpi2SystemInterfaceRegs_t __iomem *chip;
	resource_size_t	chip_phys;
	int		logging_level;
	int		fwfault_debug;
	u8		ir_firmware;
	int		bars;
	u8		mask_interrupts;
	int		dma_mask;

	/* fw fault handler */
	char		fault_reset_work_q_name[20];
	struct workqueue_struct *fault_reset_work_q;
	struct delayed_work fault_reset_work;

	/* fw event handler */
	char		firmware_event_name[20];
	struct workqueue_struct	*firmware_event_thread;
	spinlock_t	fw_event_lock;
	struct list_head fw_event_list;

	 /* misc flags */
	int		aen_event_read_flag;
	u8		broadcast_aen_busy;
	u16		broadcast_aen_pending;
	u8		shost_recovery;
	u8		got_task_abort_from_ioctl;

	struct mutex	reset_in_progress_mutex;
	spinlock_t	ioc_reset_in_progress_lock;
	u8		ioc_link_reset_in_progress;
	u8		ioc_reset_in_progress_status;

	u8		ignore_loginfos;
	u8		remove_host;
	u8		pci_error_recovery;
	u8		wait_for_discovery_to_complete;
	u8		is_driver_loading;
	u8		port_enable_failed;
	u8		start_scan;
	u16		start_scan_failed;

	u8		msix_enable;
	u16		msix_vector_count;
	u8		*cpu_msix_table;
	u16		cpu_msix_table_sz;
	resource_size_t __iomem **reply_post_host_index;
	u32		ioc_reset_count;
	MPT3SAS_FLUSH_RUNNING_CMDS schedule_dead_ioc_flush_running_cmds;
	u32             non_operational_loop;

	/* internal commands, callback index */
	u8		scsi_io_cb_idx;
	u8		tm_cb_idx;
	u8		transport_cb_idx;
	u8		scsih_cb_idx;
	u8		ctl_cb_idx;
	u8		base_cb_idx;
	u8		port_enable_cb_idx;
	u8		config_cb_idx;
	u8		tm_tr_cb_idx;
	u8		tm_tr_volume_cb_idx;
	u8		tm_sas_control_cb_idx;
	struct _internal_cmd base_cmds;
	struct _internal_cmd port_enable_cmds;
	struct _internal_cmd transport_cmds;
	struct _internal_cmd scsih_cmds;
	struct _internal_cmd tm_cmds;
	struct _internal_cmd ctl_cmds;
	struct _internal_cmd config_cmds;

	MPT_ADD_SGE	base_add_sg_single;

	/* function ptr for either IEEE or MPI sg elements */
	MPT_BUILD_SG_SCMD build_sg_scmd;
	MPT_BUILD_SG    build_sg;
	MPT_BUILD_ZERO_LEN_SGE build_zero_len_sge;
	u16             sge_size_ieee;
	u16		hba_mpi_version_belonged;

	/* function ptr for MPI sg elements only */
	MPT_BUILD_SG    build_sg_mpi;
	MPT_BUILD_ZERO_LEN_SGE build_zero_len_sge_mpi;

	/* event log */
	u32		event_type[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];
	u32		event_context;
	void		*event_log;
	u32		event_masks[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];

	/* static config pages */
	struct mpt3sas_facts facts;
	struct mpt3sas_port_facts *pfacts;
	Mpi2ManufacturingPage0_t manu_pg0;
	struct Mpi2ManufacturingPage10_t manu_pg10;
	struct Mpi2ManufacturingPage11_t manu_pg11;
	Mpi2BiosPage2_t	bios_pg2;
	Mpi2BiosPage3_t	bios_pg3;
	Mpi2IOCPage8_t ioc_pg8;
	Mpi2IOUnitPage0_t iounit_pg0;
	Mpi2IOUnitPage1_t iounit_pg1;
	Mpi2IOUnitPage8_t iounit_pg8;

	struct _boot_device req_boot_device;
	struct _boot_device req_alt_boot_device;
	struct _boot_device current_boot_device;

	/* sas hba, expander, and device list */
	struct _sas_node sas_hba;
	struct list_head sas_expander_list;
	spinlock_t	sas_node_lock;
	struct list_head sas_device_list;
	struct list_head sas_device_init_list;
	spinlock_t	sas_device_lock;
	struct list_head raid_device_list;
	spinlock_t	raid_device_lock;
	u8		io_missing_delay;
	u16		device_missing_delay;
	int		sas_id;

	void		*blocking_handles;
	void		*pd_handles;
	u16		pd_handles_sz;

	void		*pend_os_device_add;
	u16		pend_os_device_add_sz;

	/* config page */
	u16		config_page_sz;
	void		*config_page;
	dma_addr_t	config_page_dma;

	/* scsiio request */
	u16		hba_queue_depth;
	u16		sge_size;
	u16		scsiio_depth;
	u16		request_sz;
	u8		*request;
	dma_addr_t	request_dma;
	u32		request_dma_sz;
	struct scsiio_tracker *scsi_lookup;
	ulong		scsi_lookup_pages;
	spinlock_t	scsi_lookup_lock;
	struct list_head free_list;
	int		pending_io_count;
	wait_queue_head_t reset_wq;

	/* chain */
	struct chain_tracker *chain_lookup;
	struct list_head free_chain_list;
	struct dma_pool *chain_dma_pool;
	ulong		chain_pages;
	u16		max_sges_in_main_message;
	u16		max_sges_in_chain_message;
	u16		chains_needed_per_io;
	u32		chain_depth;
	u16		chain_segment_sz;

	/* hi-priority queue */
	u16		hi_priority_smid;
	u8		*hi_priority;
	dma_addr_t	hi_priority_dma;
	u16		hi_priority_depth;
	struct request_tracker *hpr_lookup;
	struct list_head hpr_free_list;

	/* internal queue */
	u16		internal_smid;
	u8		*internal;
	dma_addr_t	internal_dma;
	u16		internal_depth;
	struct request_tracker *internal_lookup;
	struct list_head internal_free_list;

	/* sense */
	u8		*sense;
	dma_addr_t	sense_dma;
	struct dma_pool *sense_dma_pool;

	/* reply */
	u16		reply_sz;
	u8		*reply;
	dma_addr_t	reply_dma;
	u32		reply_dma_max_address;
	u32		reply_dma_min_address;
	struct dma_pool *reply_dma_pool;

	/* reply free queue */
	u16		reply_free_queue_depth;
	__le32		*reply_free;
	dma_addr_t	reply_free_dma;
	struct dma_pool *reply_free_dma_pool;
	u32		reply_free_host_index;

	/* reply post queue */
	u16		reply_post_queue_depth;
	struct reply_post_struct *reply_post;
	u8		rdpq_array_capable;
	u8		rdpq_array_enable;
	u8		rdpq_array_enable_assigned;
	struct dma_pool *reply_post_free_dma_pool;
	u8		reply_queue_count;
	struct list_head reply_queue_list;

	u8		combined_reply_queue;
	u8		combined_reply_index_count;
	/* reply post register index */
	resource_size_t	**replyPostRegisterIndex;

	struct list_head delayed_tr_list;
	struct list_head delayed_tr_volume_list;
	struct list_head delayed_sc_list;
	struct list_head delayed_event_ack_list;
	u8		temp_sensors_count;
	struct mutex pci_access_mutex;

	/* diag buffer support */
	u8		*diag_buffer[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		diag_buffer_sz[MPI2_DIAG_BUF_TYPE_COUNT];
	dma_addr_t	diag_buffer_dma[MPI2_DIAG_BUF_TYPE_COUNT];
	u8		diag_buffer_status[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		unique_id[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		product_specific[MPI2_DIAG_BUF_TYPE_COUNT][23];
	u32		diagnostic_flags[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		ring_buffer_offset;
	u32		ring_buffer_sz;
	u8		is_warpdrive;
	u8		hide_ir_msg;
	u8		mfg_pg10_hide_flag;
	u8		hide_drives;
	spinlock_t	diag_trigger_lock;
	u8		diag_trigger_active;
	struct SL_WH_MASTER_TRIGGER_T diag_trigger_master;
	struct SL_WH_EVENT_TRIGGERS_T diag_trigger_event;
	struct SL_WH_SCSI_TRIGGERS_T diag_trigger_scsi;
	struct SL_WH_MPI_TRIGGERS_T diag_trigger_mpi;
	void		*device_remove_in_progress;
	u16		device_remove_in_progress_sz;
	u8		is_gen35_ioc;
	u8		atomic_desc_capable;
	PUT_SMID_IO_FP_HIP put_smid_scsi_io;
	PUT_SMID_IO_FP_HIP put_smid_fast_path;
	PUT_SMID_IO_FP_HIP put_smid_hi_priority;
	PUT_SMID_DEFAULT put_smid_default;

};

typedef u8 (*MPT_CALLBACK)(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);


/* base shared API */
extern struct list_head mpt3sas_ioc_list;
extern char    driver_name[MPT_NAME_LENGTH];
/* spinlock on list operations over IOCs
 * Case: when multiple warpdrive cards(IOCs) are in use
 * Each IOC will added to the ioc list structure on initialization.
 * Watchdog threads run at regular intervals to check IOC for any
 * fault conditions which will trigger the dead_ioc thread to
 * deallocate pci resource, resulting deleting the IOC netry from list,
 * this deletion need to protected by spinlock to enusre that
 * ioc removal is syncrhonized, if not synchronized it might lead to
 * list_del corruption as the ioc list is traversed in cli path.
 */
extern spinlock_t gioc_lock;

void mpt3sas_base_start_watchdog(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_stop_watchdog(struct MPT3SAS_ADAPTER *ioc);

int mpt3sas_base_attach(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_detach(struct MPT3SAS_ADAPTER *ioc);
int mpt3sas_base_map_resources(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_free_resources(struct MPT3SAS_ADAPTER *ioc);
int mpt3sas_base_hard_reset_handler(struct MPT3SAS_ADAPTER *ioc,
	enum reset_type type);

void *mpt3sas_base_get_msg_frame(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void *mpt3sas_base_get_sense_buffer(struct MPT3SAS_ADAPTER *ioc, u16 smid);
__le32 mpt3sas_base_get_sense_buffer_dma(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);

void mpt3sas_base_sync_reply_irqs(struct MPT3SAS_ADAPTER *ioc);

/* hi-priority queue */
u16 mpt3sas_base_get_smid_hpr(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx);
u16 mpt3sas_base_get_smid_scsiio(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx,
	struct scsi_cmnd *scmd);

u16 mpt3sas_base_get_smid(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx);
void mpt3sas_base_free_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void mpt3sas_base_initialize_callback_handler(void);
u8 mpt3sas_base_register_callback_handler(MPT_CALLBACK cb_func);
void mpt3sas_base_release_callback_handler(u8 cb_idx);

u8 mpt3sas_base_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
u8 mpt3sas_port_enable_done(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply);
void *mpt3sas_base_get_reply_virt_addr(struct MPT3SAS_ADAPTER *ioc,
	u32 phys_addr);

u32 mpt3sas_base_get_iocstate(struct MPT3SAS_ADAPTER *ioc, int cooked);

void mpt3sas_base_fault_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code);
int mpt3sas_base_sas_iounit_control(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SasIoUnitControlReply_t *mpi_reply,
	Mpi2SasIoUnitControlRequest_t *mpi_request);
int mpt3sas_base_scsi_enclosure_processor(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request);

void mpt3sas_base_validate_event_type(struct MPT3SAS_ADAPTER *ioc,
	u32 *event_type);

void mpt3sas_halt_firmware(struct MPT3SAS_ADAPTER *ioc);

void mpt3sas_base_update_missing_delay(struct MPT3SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay);

int mpt3sas_port_enable(struct MPT3SAS_ADAPTER *ioc);


/* scsih shared API */
u8 mpt3sas_scsih_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply);
void mpt3sas_scsih_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase);

int mpt3sas_scsih_issue_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	uint channel, uint id, uint lun, u8 type, u16 smid_task,
	ulong timeout);
int mpt3sas_scsih_issue_locked_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	uint channel, uint id, uint lun, u8 type, u16 smid_task,
	ulong timeout);

void mpt3sas_scsih_set_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle);
void mpt3sas_scsih_clear_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle);
void mpt3sas_expander_remove(struct MPT3SAS_ADAPTER *ioc, u64 sas_address);
void mpt3sas_device_remove_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address);
u8 mpt3sas_check_for_pending_internal_cmds(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);

struct _sas_node *mpt3sas_scsih_expander_find_by_handle(
	struct MPT3SAS_ADAPTER *ioc, u16 handle);
struct _sas_node *mpt3sas_scsih_expander_find_by_sas_address(
	struct MPT3SAS_ADAPTER *ioc, u64 sas_address);
struct _sas_device *mpt3sas_get_sdev_by_addr(
	 struct MPT3SAS_ADAPTER *ioc, u64 sas_address);
struct _sas_device *__mpt3sas_get_sdev_by_addr(
	 struct MPT3SAS_ADAPTER *ioc, u64 sas_address);

void mpt3sas_port_enable_complete(struct MPT3SAS_ADAPTER *ioc);
struct _raid_device *
mpt3sas_raid_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle);

/* config shared API */
u8 mpt3sas_config_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
int mpt3sas_config_get_number_hba_phys(struct MPT3SAS_ADAPTER *ioc,
	u8 *num_phys);
int mpt3sas_config_get_manufacturing_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage0_t *config_page);
int mpt3sas_config_get_manufacturing_pg7(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage7_t *config_page,
	u16 sz);
int mpt3sas_config_get_manufacturing_pg10(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage10_t *config_page);

int mpt3sas_config_get_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t  *config_page);
int mpt3sas_config_set_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t *config_page);

int mpt3sas_config_get_bios_pg2(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2BiosPage2_t *config_page);
int mpt3sas_config_get_bios_pg3(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2BiosPage3_t *config_page);
int mpt3sas_config_get_iounit_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage0_t *config_page);
int mpt3sas_config_get_sas_device_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_sas_device_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage1_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_sas_iounit_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage0_t *config_page,
	u16 sz);
int mpt3sas_config_get_iounit_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage1_t *config_page);
int mpt3sas_config_get_iounit_pg3(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage3_t *config_page, u16 sz);
int mpt3sas_config_set_iounit_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage1_t *config_page);
int mpt3sas_config_get_iounit_pg8(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage8_t *config_page);
int mpt3sas_config_get_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz);
int mpt3sas_config_set_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz);
int mpt3sas_config_get_ioc_pg8(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOCPage8_t *config_page);
int mpt3sas_config_get_expander_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ExpanderPage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_expander_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ExpanderPage1_t *config_page,
	u32 phy_number, u16 handle);
int mpt3sas_config_get_enclosure_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasEnclosurePage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_phy_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage0_t *config_page, u32 phy_number);
int mpt3sas_config_get_phy_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage1_t *config_page, u32 phy_number);
int mpt3sas_config_get_raid_volume_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form,
	u32 handle);
int mpt3sas_config_get_number_pds(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 *num_pds);
int mpt3sas_config_get_raid_volume_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 form,
	u32 handle, u16 sz);
int mpt3sas_config_get_phys_disk_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidPhysDiskPage0_t *config_page,
	u32 form, u32 form_specific);
int mpt3sas_config_get_volume_handle(struct MPT3SAS_ADAPTER *ioc, u16 pd_handle,
	u16 *volume_handle);
int mpt3sas_config_get_volume_wwid(struct MPT3SAS_ADAPTER *ioc,
	u16 volume_handle, u64 *wwid);

/* ctl shared API */
extern struct device_attribute *mpt3sas_host_attrs[];
extern struct device_attribute *mpt3sas_dev_attrs[];
void mpt3sas_ctl_init(ushort hbas_to_enumerate);
void mpt3sas_ctl_exit(ushort hbas_to_enumerate);
u8 mpt3sas_ctl_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
void mpt3sas_ctl_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase);
u8 mpt3sas_ctl_event_callback(struct MPT3SAS_ADAPTER *ioc,
	u8 msix_index, u32 reply);
void mpt3sas_ctl_add_to_event_log(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventNotificationReply_t *mpi_reply);

void mpt3sas_enable_diag_buffer(struct MPT3SAS_ADAPTER *ioc,
	u8 bits_to_regsiter);
int mpt3sas_send_diag_release(struct MPT3SAS_ADAPTER *ioc, u8 buffer_type,
	u8 *issue_reset);

/* transport shared API */
extern struct scsi_transport_template *mpt3sas_transport_template;
u8 mpt3sas_transport_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
struct _sas_port *mpt3sas_transport_port_add(struct MPT3SAS_ADAPTER *ioc,
	u16 handle, u64 sas_address);
void mpt3sas_transport_port_remove(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	u64 sas_address_parent);
int mpt3sas_transport_add_host_phy(struct MPT3SAS_ADAPTER *ioc, struct _sas_phy
	*mpt3sas_phy, Mpi2SasPhyPage0_t phy_pg0, struct device *parent_dev);
int mpt3sas_transport_add_expander_phy(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_phy *mpt3sas_phy, Mpi2ExpanderPage1_t expander_pg1,
	struct device *parent_dev);
void mpt3sas_transport_update_links(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, u16 handle, u8 phy_number, u8 link_rate);
extern struct sas_function_template mpt3sas_transport_functions;
extern struct scsi_transport_template *mpt3sas_transport_template;
extern int scsi_internal_device_block(struct scsi_device *sdev);
extern int scsi_internal_device_unblock(struct scsi_device *sdev,
				enum scsi_device_state new_state);
/* trigger data externs */
void mpt3sas_send_trigger_data_event(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data);
void mpt3sas_process_trigger_data(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data);
void mpt3sas_trigger_master(struct MPT3SAS_ADAPTER *ioc,
	u32 tigger_bitmask);
void mpt3sas_trigger_event(struct MPT3SAS_ADAPTER *ioc, u16 event,
	u16 log_entry_qualifier);
void mpt3sas_trigger_scsi(struct MPT3SAS_ADAPTER *ioc, u8 sense_key,
	u8 asc, u8 ascq);
void mpt3sas_trigger_mpi(struct MPT3SAS_ADAPTER *ioc, u16 ioc_status,
	u32 loginfo);

/* warpdrive APIs */
u8 mpt3sas_get_num_volumes(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_init_warpdrive_properties(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device);
u8
mpt3sas_scsi_direct_io_get(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void
mpt3sas_scsi_direct_io_set(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 direct_io);
void
mpt3sas_setup_direct_io(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	struct _raid_device *raid_device, Mpi2SCSIIORequest_t *mpi_request,
	u16 smid);

/* NCQ Prio Handling Check */
bool scsih_ncq_prio_supp(struct scsi_device *sdev);

#endif /* MPT3SAS_BASE_H_INCLUDED */

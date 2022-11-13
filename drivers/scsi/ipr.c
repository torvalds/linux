// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ipr.c -- driver for IBM Power Linux RAID adapters
 *
 * Written By: Brian King <brking@us.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2003, 2004 IBM Corporation
 */

/*
 * Notes:
 *
 * This driver is used to control the following SCSI adapters:
 *
 * IBM iSeries: 5702, 5703, 2780, 5709, 570A, 570B
 *
 * IBM pSeries: PCI-X Dual Channel Ultra 320 SCSI RAID Adapter
 *              PCI-X Dual Channel Ultra 320 SCSI Adapter
 *              PCI-X Dual Channel Ultra 320 SCSI RAID Enablement Card
 *              Embedded SCSI adapter on p615 and p655 systems
 *
 * Supported Hardware Features:
 *	- Ultra 320 SCSI controller
 *	- PCI-X host interface
 *	- Embedded PowerPC RISC Processor and Hardware XOR DMA Engine
 *	- Non-Volatile Write Cache
 *	- Supports attachment of non-RAID disks, tape, and optical devices
 *	- RAID Levels 0, 5, 10
 *	- Hot spare
 *	- Background Parity Checking
 *	- Background Data Scrubbing
 *	- Ability to increase the capacity of an existing RAID 5 disk array
 *		by adding disks
 *
 * Driver Features:
 *	- Tagged command queuing
 *	- Adapter microcode download
 *	- PCI hot plug
 *	- SCSI device hot plug
 *
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/libata.h>
#include <linux/hdreg.h>
#include <linux/reboot.h>
#include <linux/stringify.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_cmnd.h>
#include "ipr.h"

/*
 *   Global Data
 */
static LIST_HEAD(ipr_ioa_head);
static unsigned int ipr_log_level = IPR_DEFAULT_LOG_LEVEL;
static unsigned int ipr_max_speed = 1;
static int ipr_testmode = 0;
static unsigned int ipr_fastfail = 0;
static unsigned int ipr_transop_timeout = 0;
static unsigned int ipr_debug = 0;
static unsigned int ipr_max_devs = IPR_DEFAULT_SIS64_DEVS;
static unsigned int ipr_dual_ioa_raid = 1;
static unsigned int ipr_number_of_msix = 16;
static unsigned int ipr_fast_reboot;
static DEFINE_SPINLOCK(ipr_driver_lock);

/* This table describes the differences between DMA controller chips */
static const struct ipr_chip_cfg_t ipr_chip_cfg[] = {
	{ /* Gemstone, Citrine, Obsidian, and Obsidian-E */
		.mailbox = 0x0042C,
		.max_cmds = 100,
		.cache_line_size = 0x20,
		.clear_isr = 1,
		.iopoll_weight = 0,
		{
			.set_interrupt_mask_reg = 0x0022C,
			.clr_interrupt_mask_reg = 0x00230,
			.clr_interrupt_mask_reg32 = 0x00230,
			.sense_interrupt_mask_reg = 0x0022C,
			.sense_interrupt_mask_reg32 = 0x0022C,
			.clr_interrupt_reg = 0x00228,
			.clr_interrupt_reg32 = 0x00228,
			.sense_interrupt_reg = 0x00224,
			.sense_interrupt_reg32 = 0x00224,
			.ioarrin_reg = 0x00404,
			.sense_uproc_interrupt_reg = 0x00214,
			.sense_uproc_interrupt_reg32 = 0x00214,
			.set_uproc_interrupt_reg = 0x00214,
			.set_uproc_interrupt_reg32 = 0x00214,
			.clr_uproc_interrupt_reg = 0x00218,
			.clr_uproc_interrupt_reg32 = 0x00218
		}
	},
	{ /* Snipe and Scamp */
		.mailbox = 0x0052C,
		.max_cmds = 100,
		.cache_line_size = 0x20,
		.clear_isr = 1,
		.iopoll_weight = 0,
		{
			.set_interrupt_mask_reg = 0x00288,
			.clr_interrupt_mask_reg = 0x0028C,
			.clr_interrupt_mask_reg32 = 0x0028C,
			.sense_interrupt_mask_reg = 0x00288,
			.sense_interrupt_mask_reg32 = 0x00288,
			.clr_interrupt_reg = 0x00284,
			.clr_interrupt_reg32 = 0x00284,
			.sense_interrupt_reg = 0x00280,
			.sense_interrupt_reg32 = 0x00280,
			.ioarrin_reg = 0x00504,
			.sense_uproc_interrupt_reg = 0x00290,
			.sense_uproc_interrupt_reg32 = 0x00290,
			.set_uproc_interrupt_reg = 0x00290,
			.set_uproc_interrupt_reg32 = 0x00290,
			.clr_uproc_interrupt_reg = 0x00294,
			.clr_uproc_interrupt_reg32 = 0x00294
		}
	},
	{ /* CRoC */
		.mailbox = 0x00044,
		.max_cmds = 1000,
		.cache_line_size = 0x20,
		.clear_isr = 0,
		.iopoll_weight = 64,
		{
			.set_interrupt_mask_reg = 0x00010,
			.clr_interrupt_mask_reg = 0x00018,
			.clr_interrupt_mask_reg32 = 0x0001C,
			.sense_interrupt_mask_reg = 0x00010,
			.sense_interrupt_mask_reg32 = 0x00014,
			.clr_interrupt_reg = 0x00008,
			.clr_interrupt_reg32 = 0x0000C,
			.sense_interrupt_reg = 0x00000,
			.sense_interrupt_reg32 = 0x00004,
			.ioarrin_reg = 0x00070,
			.sense_uproc_interrupt_reg = 0x00020,
			.sense_uproc_interrupt_reg32 = 0x00024,
			.set_uproc_interrupt_reg = 0x00020,
			.set_uproc_interrupt_reg32 = 0x00024,
			.clr_uproc_interrupt_reg = 0x00028,
			.clr_uproc_interrupt_reg32 = 0x0002C,
			.init_feedback_reg = 0x0005C,
			.dump_addr_reg = 0x00064,
			.dump_data_reg = 0x00068,
			.endian_swap_reg = 0x00084
		}
	},
};

static const struct ipr_chip_t ipr_chip[] = {
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CITRINE, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_OBSIDIAN, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN_E, true, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_SNIPE, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[1] },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_SCAMP, false, IPR_SIS32, IPR_PCI_CFG, &ipr_chip_cfg[1] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2, true, IPR_SIS64, IPR_MMIO, &ipr_chip_cfg[2] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE, true, IPR_SIS64, IPR_MMIO, &ipr_chip_cfg[2] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_RATTLESNAKE, true, IPR_SIS64, IPR_MMIO, &ipr_chip_cfg[2] }
};

static int ipr_max_bus_speeds[] = {
	IPR_80MBs_SCSI_RATE, IPR_U160_SCSI_RATE, IPR_U320_SCSI_RATE
};

MODULE_AUTHOR("Brian King <brking@us.ibm.com>");
MODULE_DESCRIPTION("IBM Power RAID SCSI Adapter Driver");
module_param_named(max_speed, ipr_max_speed, uint, 0);
MODULE_PARM_DESC(max_speed, "Maximum bus speed (0-2). Default: 1=U160. Speeds: 0=80 MB/s, 1=U160, 2=U320");
module_param_named(log_level, ipr_log_level, uint, 0);
MODULE_PARM_DESC(log_level, "Set to 0 - 4 for increasing verbosity of device driver");
module_param_named(testmode, ipr_testmode, int, 0);
MODULE_PARM_DESC(testmode, "DANGEROUS!!! Allows unsupported configurations");
module_param_named(fastfail, ipr_fastfail, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fastfail, "Reduce timeouts and retries");
module_param_named(transop_timeout, ipr_transop_timeout, int, 0);
MODULE_PARM_DESC(transop_timeout, "Time in seconds to wait for adapter to come operational (default: 300)");
module_param_named(debug, ipr_debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable device driver debugging logging. Set to 1 to enable. (default: 0)");
module_param_named(dual_ioa_raid, ipr_dual_ioa_raid, int, 0);
MODULE_PARM_DESC(dual_ioa_raid, "Enable dual adapter RAID support. Set to 1 to enable. (default: 1)");
module_param_named(max_devs, ipr_max_devs, int, 0);
MODULE_PARM_DESC(max_devs, "Specify the maximum number of physical devices. "
		 "[Default=" __stringify(IPR_DEFAULT_SIS64_DEVS) "]");
module_param_named(number_of_msix, ipr_number_of_msix, int, 0);
MODULE_PARM_DESC(number_of_msix, "Specify the number of MSIX interrupts to use on capable adapters (1 - 16).  (default:16)");
module_param_named(fast_reboot, ipr_fast_reboot, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fast_reboot, "Skip adapter shutdown during reboot. Set to 1 to enable. (default: 0)");
MODULE_LICENSE("GPL");
MODULE_VERSION(IPR_DRIVER_VERSION);

/*  A constant array of IOASCs/URCs/Error Messages */
static const
struct ipr_error_table_t ipr_error_table[] = {
	{0x00000000, 1, IPR_DEFAULT_LOG_LEVEL,
	"8155: An unknown error was received"},
	{0x00330000, 0, 0,
	"Soft underlength error"},
	{0x005A0000, 0, 0,
	"Command to be cancelled not found"},
	{0x00808000, 0, 0,
	"Qualified success"},
	{0x01080000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFFE: Soft device bus error recovered by the IOA"},
	{0x01088100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4101: Soft device bus fabric error"},
	{0x01100100, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFC: Logical block guard error recovered by the device"},
	{0x01100300, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFC: Logical block reference tag error recovered by the device"},
	{0x01108300, 0, IPR_DEFAULT_LOG_LEVEL,
	"4171: Recovered scatter list tag / sequence number error"},
	{0x01109000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FF3D: Recovered logical block CRC error on IOA to Host transfer"},
	{0x01109200, 0, IPR_DEFAULT_LOG_LEVEL,
	"4171: Recovered logical block sequence number error on IOA to Host transfer"},
	{0x0110A000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFD: Recovered logical block reference tag error detected by the IOA"},
	{0x0110A100, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFD: Logical block guard error recovered by the IOA"},
	{0x01170600, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF9: Device sector reassign successful"},
	{0x01170900, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF7: Media error recovered by device rewrite procedures"},
	{0x01180200, 0, IPR_DEFAULT_LOG_LEVEL,
	"7001: IOA sector reassignment successful"},
	{0x01180500, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF9: Soft media error. Sector reassignment recommended"},
	{0x01180600, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF7: Media error recovered by IOA rewrite procedures"},
	{0x01418000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FF3D: Soft PCI bus error recovered by the IOA"},
	{0x01440000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFF6: Device hardware error recovered by the IOA"},
	{0x01448100, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF6: Device hardware error recovered by the device"},
	{0x01448200, 1, IPR_DEFAULT_LOG_LEVEL,
	"FF3D: Soft IOA error recovered by the IOA"},
	{0x01448300, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFA: Undefined device response recovered by the IOA"},
	{0x014A0000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFF6: Device bus error, message or command phase"},
	{0x014A8000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFE: Task Management Function failed"},
	{0x015D0000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF6: Failure prediction threshold exceeded"},
	{0x015D9200, 0, IPR_DEFAULT_LOG_LEVEL,
	"8009: Impending cache battery pack failure"},
	{0x02040100, 0, 0,
	"Logical Unit in process of becoming ready"},
	{0x02040200, 0, 0,
	"Initializing command required"},
	{0x02040400, 0, 0,
	"34FF: Disk device format in progress"},
	{0x02040C00, 0, 0,
	"Logical unit not accessible, target port in unavailable state"},
	{0x02048000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9070: IOA requested reset"},
	{0x023F0000, 0, 0,
	"Synchronization required"},
	{0x02408500, 0, 0,
	"IOA microcode download required"},
	{0x02408600, 0, 0,
	"Device bus connection is prohibited by host"},
	{0x024E0000, 0, 0,
	"No ready, IOA shutdown"},
	{0x025A0000, 0, 0,
	"Not ready, IOA has been shutdown"},
	{0x02670100, 0, IPR_DEFAULT_LOG_LEVEL,
	"3020: Storage subsystem configuration error"},
	{0x03110B00, 0, 0,
	"FFF5: Medium error, data unreadable, recommend reassign"},
	{0x03110C00, 0, 0,
	"7000: Medium error, data unreadable, do not reassign"},
	{0x03310000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF3: Disk media format bad"},
	{0x04050000, 0, IPR_DEFAULT_LOG_LEVEL,
	"3002: Addressed device failed to respond to selection"},
	{0x04080000, 1, IPR_DEFAULT_LOG_LEVEL,
	"3100: Device bus error"},
	{0x04080100, 0, IPR_DEFAULT_LOG_LEVEL,
	"3109: IOA timed out a device command"},
	{0x04088000, 0, 0,
	"3120: SCSI bus is not operational"},
	{0x04088100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4100: Hard device bus fabric error"},
	{0x04100100, 0, IPR_DEFAULT_LOG_LEVEL,
	"310C: Logical block guard error detected by the device"},
	{0x04100300, 0, IPR_DEFAULT_LOG_LEVEL,
	"310C: Logical block reference tag error detected by the device"},
	{0x04108300, 1, IPR_DEFAULT_LOG_LEVEL,
	"4170: Scatter list tag / sequence number error"},
	{0x04109000, 1, IPR_DEFAULT_LOG_LEVEL,
	"8150: Logical block CRC error on IOA to Host transfer"},
	{0x04109200, 1, IPR_DEFAULT_LOG_LEVEL,
	"4170: Logical block sequence number error on IOA to Host transfer"},
	{0x0410A000, 0, IPR_DEFAULT_LOG_LEVEL,
	"310D: Logical block reference tag error detected by the IOA"},
	{0x0410A100, 0, IPR_DEFAULT_LOG_LEVEL,
	"310D: Logical block guard error detected by the IOA"},
	{0x04118000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9000: IOA reserved area data check"},
	{0x04118100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9001: IOA reserved area invalid data pattern"},
	{0x04118200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9002: IOA reserved area LRC error"},
	{0x04118300, 1, IPR_DEFAULT_LOG_LEVEL,
	"Hardware Error, IOA metadata access error"},
	{0x04320000, 0, IPR_DEFAULT_LOG_LEVEL,
	"102E: Out of alternate sectors for disk storage"},
	{0x04330000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFF4: Data transfer underlength error"},
	{0x04338000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFF4: Data transfer overlength error"},
	{0x043E0100, 0, IPR_DEFAULT_LOG_LEVEL,
	"3400: Logical unit failure"},
	{0x04408500, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF4: Device microcode is corrupt"},
	{0x04418000, 1, IPR_DEFAULT_LOG_LEVEL,
	"8150: PCI bus error"},
	{0x04430000, 1, 0,
	"Unsupported device bus message received"},
	{0x04440000, 1, IPR_DEFAULT_LOG_LEVEL,
	"FFF4: Disk device problem"},
	{0x04448200, 1, IPR_DEFAULT_LOG_LEVEL,
	"8150: Permanent IOA failure"},
	{0x04448300, 0, IPR_DEFAULT_LOG_LEVEL,
	"3010: Disk device returned wrong response to IOA"},
	{0x04448400, 0, IPR_DEFAULT_LOG_LEVEL,
	"8151: IOA microcode error"},
	{0x04448500, 0, 0,
	"Device bus status error"},
	{0x04448600, 0, IPR_DEFAULT_LOG_LEVEL,
	"8157: IOA error requiring IOA reset to recover"},
	{0x04448700, 0, 0,
	"ATA device status error"},
	{0x04490000, 0, 0,
	"Message reject received from the device"},
	{0x04449200, 0, IPR_DEFAULT_LOG_LEVEL,
	"8008: A permanent cache battery pack failure occurred"},
	{0x0444A000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9090: Disk unit has been modified after the last known status"},
	{0x0444A200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9081: IOA detected device error"},
	{0x0444A300, 0, IPR_DEFAULT_LOG_LEVEL,
	"9082: IOA detected device error"},
	{0x044A0000, 1, IPR_DEFAULT_LOG_LEVEL,
	"3110: Device bus error, message or command phase"},
	{0x044A8000, 1, IPR_DEFAULT_LOG_LEVEL,
	"3110: SAS Command / Task Management Function failed"},
	{0x04670400, 0, IPR_DEFAULT_LOG_LEVEL,
	"9091: Incorrect hardware configuration change has been detected"},
	{0x04678000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9073: Invalid multi-adapter configuration"},
	{0x04678100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4010: Incorrect connection between cascaded expanders"},
	{0x04678200, 0, IPR_DEFAULT_LOG_LEVEL,
	"4020: Connections exceed IOA design limits"},
	{0x04678300, 0, IPR_DEFAULT_LOG_LEVEL,
	"4030: Incorrect multipath connection"},
	{0x04679000, 0, IPR_DEFAULT_LOG_LEVEL,
	"4110: Unsupported enclosure function"},
	{0x04679800, 0, IPR_DEFAULT_LOG_LEVEL,
	"4120: SAS cable VPD cannot be read"},
	{0x046E0000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFF4: Command to logical unit failed"},
	{0x05240000, 1, 0,
	"Illegal request, invalid request type or request packet"},
	{0x05250000, 0, 0,
	"Illegal request, invalid resource handle"},
	{0x05258000, 0, 0,
	"Illegal request, commands not allowed to this device"},
	{0x05258100, 0, 0,
	"Illegal request, command not allowed to a secondary adapter"},
	{0x05258200, 0, 0,
	"Illegal request, command not allowed to a non-optimized resource"},
	{0x05260000, 0, 0,
	"Illegal request, invalid field in parameter list"},
	{0x05260100, 0, 0,
	"Illegal request, parameter not supported"},
	{0x05260200, 0, 0,
	"Illegal request, parameter value invalid"},
	{0x052C0000, 0, 0,
	"Illegal request, command sequence error"},
	{0x052C8000, 1, 0,
	"Illegal request, dual adapter support not enabled"},
	{0x052C8100, 1, 0,
	"Illegal request, another cable connector was physically disabled"},
	{0x054E8000, 1, 0,
	"Illegal request, inconsistent group id/group count"},
	{0x06040500, 0, IPR_DEFAULT_LOG_LEVEL,
	"9031: Array protection temporarily suspended, protection resuming"},
	{0x06040600, 0, IPR_DEFAULT_LOG_LEVEL,
	"9040: Array protection temporarily suspended, protection resuming"},
	{0x060B0100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4080: IOA exceeded maximum operating temperature"},
	{0x060B8000, 0, IPR_DEFAULT_LOG_LEVEL,
	"4085: Service required"},
	{0x060B8100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4086: SAS Adapter Hardware Configuration Error"},
	{0x06288000, 0, IPR_DEFAULT_LOG_LEVEL,
	"3140: Device bus not ready to ready transition"},
	{0x06290000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFB: SCSI bus was reset"},
	{0x06290500, 0, 0,
	"FFFE: SCSI bus transition to single ended"},
	{0x06290600, 0, 0,
	"FFFE: SCSI bus transition to LVD"},
	{0x06298000, 0, IPR_DEFAULT_LOG_LEVEL,
	"FFFB: SCSI bus was reset by another initiator"},
	{0x063F0300, 0, IPR_DEFAULT_LOG_LEVEL,
	"3029: A device replacement has occurred"},
	{0x063F8300, 0, IPR_DEFAULT_LOG_LEVEL,
	"4102: Device bus fabric performance degradation"},
	{0x064C8000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9051: IOA cache data exists for a missing or failed device"},
	{0x064C8100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9055: Auxiliary cache IOA contains cache data needed by the primary IOA"},
	{0x06670100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9025: Disk unit is not supported at its physical location"},
	{0x06670600, 0, IPR_DEFAULT_LOG_LEVEL,
	"3020: IOA detected a SCSI bus configuration error"},
	{0x06678000, 0, IPR_DEFAULT_LOG_LEVEL,
	"3150: SCSI bus configuration error"},
	{0x06678100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9074: Asymmetric advanced function disk configuration"},
	{0x06678300, 0, IPR_DEFAULT_LOG_LEVEL,
	"4040: Incomplete multipath connection between IOA and enclosure"},
	{0x06678400, 0, IPR_DEFAULT_LOG_LEVEL,
	"4041: Incomplete multipath connection between enclosure and device"},
	{0x06678500, 0, IPR_DEFAULT_LOG_LEVEL,
	"9075: Incomplete multipath connection between IOA and remote IOA"},
	{0x06678600, 0, IPR_DEFAULT_LOG_LEVEL,
	"9076: Configuration error, missing remote IOA"},
	{0x06679100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4050: Enclosure does not support a required multipath function"},
	{0x06679800, 0, IPR_DEFAULT_LOG_LEVEL,
	"4121: Configuration error, required cable is missing"},
	{0x06679900, 0, IPR_DEFAULT_LOG_LEVEL,
	"4122: Cable is not plugged into the correct location on remote IOA"},
	{0x06679A00, 0, IPR_DEFAULT_LOG_LEVEL,
	"4123: Configuration error, invalid cable vital product data"},
	{0x06679B00, 0, IPR_DEFAULT_LOG_LEVEL,
	"4124: Configuration error, both cable ends are plugged into the same IOA"},
	{0x06690000, 0, IPR_DEFAULT_LOG_LEVEL,
	"4070: Logically bad block written on device"},
	{0x06690200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9041: Array protection temporarily suspended"},
	{0x06698200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9042: Corrupt array parity detected on specified device"},
	{0x066B0200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9030: Array no longer protected due to missing or failed disk unit"},
	{0x066B8000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9071: Link operational transition"},
	{0x066B8100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9072: Link not operational transition"},
	{0x066B8200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9032: Array exposed but still protected"},
	{0x066B8300, 0, IPR_DEBUG_LOG_LEVEL,
	"70DD: Device forced failed by disrupt device command"},
	{0x066B9100, 0, IPR_DEFAULT_LOG_LEVEL,
	"4061: Multipath redundancy level got better"},
	{0x066B9200, 0, IPR_DEFAULT_LOG_LEVEL,
	"4060: Multipath redundancy level got worse"},
	{0x06808100, 0, IPR_DEBUG_LOG_LEVEL,
	"9083: Device raw mode enabled"},
	{0x06808200, 0, IPR_DEBUG_LOG_LEVEL,
	"9084: Device raw mode disabled"},
	{0x07270000, 0, 0,
	"Failure due to other device"},
	{0x07278000, 0, IPR_DEFAULT_LOG_LEVEL,
	"9008: IOA does not support functions expected by devices"},
	{0x07278100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9010: Cache data associated with attached devices cannot be found"},
	{0x07278200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9011: Cache data belongs to devices other than those attached"},
	{0x07278400, 0, IPR_DEFAULT_LOG_LEVEL,
	"9020: Array missing 2 or more devices with only 1 device present"},
	{0x07278500, 0, IPR_DEFAULT_LOG_LEVEL,
	"9021: Array missing 2 or more devices with 2 or more devices present"},
	{0x07278600, 0, IPR_DEFAULT_LOG_LEVEL,
	"9022: Exposed array is missing a required device"},
	{0x07278700, 0, IPR_DEFAULT_LOG_LEVEL,
	"9023: Array member(s) not at required physical locations"},
	{0x07278800, 0, IPR_DEFAULT_LOG_LEVEL,
	"9024: Array not functional due to present hardware configuration"},
	{0x07278900, 0, IPR_DEFAULT_LOG_LEVEL,
	"9026: Array not functional due to present hardware configuration"},
	{0x07278A00, 0, IPR_DEFAULT_LOG_LEVEL,
	"9027: Array is missing a device and parity is out of sync"},
	{0x07278B00, 0, IPR_DEFAULT_LOG_LEVEL,
	"9028: Maximum number of arrays already exist"},
	{0x07278C00, 0, IPR_DEFAULT_LOG_LEVEL,
	"9050: Required cache data cannot be located for a disk unit"},
	{0x07278D00, 0, IPR_DEFAULT_LOG_LEVEL,
	"9052: Cache data exists for a device that has been modified"},
	{0x07278F00, 0, IPR_DEFAULT_LOG_LEVEL,
	"9054: IOA resources not available due to previous problems"},
	{0x07279100, 0, IPR_DEFAULT_LOG_LEVEL,
	"9092: Disk unit requires initialization before use"},
	{0x07279200, 0, IPR_DEFAULT_LOG_LEVEL,
	"9029: Incorrect hardware configuration change has been detected"},
	{0x07279600, 0, IPR_DEFAULT_LOG_LEVEL,
	"9060: One or more disk pairs are missing from an array"},
	{0x07279700, 0, IPR_DEFAULT_LOG_LEVEL,
	"9061: One or more disks are missing from an array"},
	{0x07279800, 0, IPR_DEFAULT_LOG_LEVEL,
	"9062: One or more disks are missing from an array"},
	{0x07279900, 0, IPR_DEFAULT_LOG_LEVEL,
	"9063: Maximum number of functional arrays has been exceeded"},
	{0x07279A00, 0, 0,
	"Data protect, other volume set problem"},
	{0x0B260000, 0, 0,
	"Aborted command, invalid descriptor"},
	{0x0B3F9000, 0, 0,
	"Target operating conditions have changed, dual adapter takeover"},
	{0x0B530200, 0, 0,
	"Aborted command, medium removal prevented"},
	{0x0B5A0000, 0, 0,
	"Command terminated by host"},
	{0x0B5B8000, 0, 0,
	"Aborted command, command terminated by host"}
};

static const struct ipr_ses_table_entry ipr_ses_table[] = {
	{ "2104-DL1        ", "XXXXXXXXXXXXXXXX", 80 },
	{ "2104-TL1        ", "XXXXXXXXXXXXXXXX", 80 },
	{ "HSBP07M P U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Hidive 7 slot */
	{ "HSBP05M P U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Hidive 5 slot */
	{ "HSBP05M S U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Bowtie */
	{ "HSBP06E ASU2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* MartinFenning */
	{ "2104-DU3        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "2104-TU3        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "HSBP04C RSU2SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "HSBP06E RSU2SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "St  V1S2        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "HSBPD4M  PU3SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "VSBPD1H   U3SCSI", "XXXXXXX*XXXXXXXX", 160 }
};

/*
 *  Function Prototypes
 */
static int ipr_reset_alert(struct ipr_cmnd *);
static void ipr_process_ccn(struct ipr_cmnd *);
static void ipr_process_error(struct ipr_cmnd *);
static void ipr_reset_ioa_job(struct ipr_cmnd *);
static void ipr_initiate_ioa_reset(struct ipr_ioa_cfg *,
				   enum ipr_shutdown_type);

#ifdef CONFIG_SCSI_IPR_TRACE
/**
 * ipr_trc_hook - Add a trace entry to the driver trace
 * @ipr_cmd:	ipr command struct
 * @type:		trace type
 * @add_data:	additional data
 *
 * Return value:
 * 	none
 **/
static void ipr_trc_hook(struct ipr_cmnd *ipr_cmd,
			 u8 type, u32 add_data)
{
	struct ipr_trace_entry *trace_entry;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	unsigned int trace_index;

	trace_index = atomic_add_return(1, &ioa_cfg->trace_index) & IPR_TRACE_INDEX_MASK;
	trace_entry = &ioa_cfg->trace[trace_index];
	trace_entry->time = jiffies;
	trace_entry->op_code = ipr_cmd->ioarcb.cmd_pkt.cdb[0];
	trace_entry->type = type;
	if (ipr_cmd->ioa_cfg->sis64)
		trace_entry->ata_op_code = ipr_cmd->i.ata_ioadl.regs.command;
	else
		trace_entry->ata_op_code = ipr_cmd->ioarcb.u.add_data.u.regs.command;
	trace_entry->cmd_index = ipr_cmd->cmd_index & 0xff;
	trace_entry->res_handle = ipr_cmd->ioarcb.res_handle;
	trace_entry->u.add_data = add_data;
	wmb();
}
#else
#define ipr_trc_hook(ipr_cmd, type, add_data) do { } while (0)
#endif

/**
 * ipr_lock_and_done - Acquire lock and complete command
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 *	none
 **/
static void ipr_lock_and_done(struct ipr_cmnd *ipr_cmd)
{
	unsigned long lock_flags;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ipr_cmd->done(ipr_cmd);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
}

/**
 * ipr_reinit_ipr_cmnd - Re-initialize an IPR Cmnd block for reuse
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_reinit_ipr_cmnd(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioasa *ioasa = &ipr_cmd->s.ioasa;
	struct ipr_ioasa64 *ioasa64 = &ipr_cmd->s.ioasa64;
	dma_addr_t dma_addr = ipr_cmd->dma_addr;
	int hrrq_id;

	hrrq_id = ioarcb->cmd_pkt.hrrq_id;
	memset(&ioarcb->cmd_pkt, 0, sizeof(struct ipr_cmd_pkt));
	ioarcb->cmd_pkt.hrrq_id = hrrq_id;
	ioarcb->data_transfer_length = 0;
	ioarcb->read_data_transfer_length = 0;
	ioarcb->ioadl_len = 0;
	ioarcb->read_ioadl_len = 0;

	if (ipr_cmd->ioa_cfg->sis64) {
		ioarcb->u.sis64_addr_data.data_ioadl_addr =
			cpu_to_be64(dma_addr + offsetof(struct ipr_cmnd, i.ioadl64));
		ioasa64->u.gata.status = 0;
	} else {
		ioarcb->write_ioadl_addr =
			cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, i.ioadl));
		ioarcb->read_ioadl_addr = ioarcb->write_ioadl_addr;
		ioasa->u.gata.status = 0;
	}

	ioasa->hdr.ioasc = 0;
	ioasa->hdr.residual_data_len = 0;
	ipr_cmd->scsi_cmd = NULL;
	ipr_cmd->qc = NULL;
	ipr_cmd->sense_buffer[0] = 0;
	ipr_cmd->dma_use_sg = 0;
}

/**
 * ipr_init_ipr_cmnd - Initialize an IPR Cmnd block
 * @ipr_cmd:	ipr command struct
 * @fast_done:	fast done function call-back
 *
 * Return value:
 * 	none
 **/
static void ipr_init_ipr_cmnd(struct ipr_cmnd *ipr_cmd,
			      void (*fast_done) (struct ipr_cmnd *))
{
	ipr_reinit_ipr_cmnd(ipr_cmd);
	ipr_cmd->u.scratch = 0;
	ipr_cmd->sibling = NULL;
	ipr_cmd->eh_comp = NULL;
	ipr_cmd->fast_done = fast_done;
	timer_setup(&ipr_cmd->timer, NULL, 0);
}

/**
 * __ipr_get_free_ipr_cmnd - Get a free IPR Cmnd block
 * @hrrq:	hrr queue
 *
 * Return value:
 * 	pointer to ipr command struct
 **/
static
struct ipr_cmnd *__ipr_get_free_ipr_cmnd(struct ipr_hrr_queue *hrrq)
{
	struct ipr_cmnd *ipr_cmd = NULL;

	if (likely(!list_empty(&hrrq->hrrq_free_q))) {
		ipr_cmd = list_entry(hrrq->hrrq_free_q.next,
			struct ipr_cmnd, queue);
		list_del(&ipr_cmd->queue);
	}


	return ipr_cmd;
}

/**
 * ipr_get_free_ipr_cmnd - Get a free IPR Cmnd block and initialize it
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 *	pointer to ipr command struct
 **/
static
struct ipr_cmnd *ipr_get_free_ipr_cmnd(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd =
		__ipr_get_free_ipr_cmnd(&ioa_cfg->hrrq[IPR_INIT_HRRQ]);
	ipr_init_ipr_cmnd(ipr_cmd, ipr_lock_and_done);
	return ipr_cmd;
}

/**
 * ipr_mask_and_clear_interrupts - Mask all and clear specified interrupts
 * @ioa_cfg:	ioa config struct
 * @clr_ints:     interrupts to clear
 *
 * This function masks all interrupts on the adapter, then clears the
 * interrupts specified in the mask
 *
 * Return value:
 * 	none
 **/
static void ipr_mask_and_clear_interrupts(struct ipr_ioa_cfg *ioa_cfg,
					  u32 clr_ints)
{
	int i;

	/* Stop new interrupts */
	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].allow_interrupts = 0;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}

	/* Set interrupt mask to stop all new interrupts */
	if (ioa_cfg->sis64)
		writeq(~0, ioa_cfg->regs.set_interrupt_mask_reg);
	else
		writel(~0, ioa_cfg->regs.set_interrupt_mask_reg);

	/* Clear any pending interrupts */
	if (ioa_cfg->sis64)
		writel(~0, ioa_cfg->regs.clr_interrupt_reg);
	writel(clr_ints, ioa_cfg->regs.clr_interrupt_reg32);
	readl(ioa_cfg->regs.sense_interrupt_reg);
}

/**
 * ipr_save_pcix_cmd_reg - Save PCI-X command register
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_save_pcix_cmd_reg(struct ipr_ioa_cfg *ioa_cfg)
{
	int pcix_cmd_reg = pci_find_capability(ioa_cfg->pdev, PCI_CAP_ID_PCIX);

	if (pcix_cmd_reg == 0)
		return 0;

	if (pci_read_config_word(ioa_cfg->pdev, pcix_cmd_reg + PCI_X_CMD,
				 &ioa_cfg->saved_pcix_cmd_reg) != PCIBIOS_SUCCESSFUL) {
		dev_err(&ioa_cfg->pdev->dev, "Failed to save PCI-X command register\n");
		return -EIO;
	}

	ioa_cfg->saved_pcix_cmd_reg |= PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO;
	return 0;
}

/**
 * ipr_set_pcix_cmd_reg - Setup PCI-X command register
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_set_pcix_cmd_reg(struct ipr_ioa_cfg *ioa_cfg)
{
	int pcix_cmd_reg = pci_find_capability(ioa_cfg->pdev, PCI_CAP_ID_PCIX);

	if (pcix_cmd_reg) {
		if (pci_write_config_word(ioa_cfg->pdev, pcix_cmd_reg + PCI_X_CMD,
					  ioa_cfg->saved_pcix_cmd_reg) != PCIBIOS_SUCCESSFUL) {
			dev_err(&ioa_cfg->pdev->dev, "Failed to setup PCI-X command register\n");
			return -EIO;
		}
	}

	return 0;
}

/**
 * __ipr_sata_eh_done - done function for aborted SATA commands
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked for ops generated to SATA
 * devices which are being aborted.
 *
 * Return value:
 * 	none
 **/
static void __ipr_sata_eh_done(struct ipr_cmnd *ipr_cmd)
{
	struct ata_queued_cmd *qc = ipr_cmd->qc;
	struct ipr_sata_port *sata_port = qc->ap->private_data;

	qc->err_mask |= AC_ERR_OTHER;
	sata_port->ioasa.status |= ATA_BUSY;
	ata_qc_complete(qc);
	if (ipr_cmd->eh_comp)
		complete(ipr_cmd->eh_comp);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
}

/**
 * ipr_sata_eh_done - done function for aborted SATA commands
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked for ops generated to SATA
 * devices which are being aborted.
 *
 * Return value:
 * 	none
 **/
static void ipr_sata_eh_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hrr_queue *hrrq = ipr_cmd->hrrq;
	unsigned long hrrq_flags;

	spin_lock_irqsave(&hrrq->_lock, hrrq_flags);
	__ipr_sata_eh_done(ipr_cmd);
	spin_unlock_irqrestore(&hrrq->_lock, hrrq_flags);
}

/**
 * __ipr_scsi_eh_done - mid-layer done function for aborted ops
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer which are being aborted.
 *
 * Return value:
 * 	none
 **/
static void __ipr_scsi_eh_done(struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;

	scsi_cmd->result |= (DID_ERROR << 16);

	scsi_dma_unmap(ipr_cmd->scsi_cmd);
	scsi_cmd->scsi_done(scsi_cmd);
	if (ipr_cmd->eh_comp)
		complete(ipr_cmd->eh_comp);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
}

/**
 * ipr_scsi_eh_done - mid-layer done function for aborted ops
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer which are being aborted.
 *
 * Return value:
 * 	none
 **/
static void ipr_scsi_eh_done(struct ipr_cmnd *ipr_cmd)
{
	unsigned long hrrq_flags;
	struct ipr_hrr_queue *hrrq = ipr_cmd->hrrq;

	spin_lock_irqsave(&hrrq->_lock, hrrq_flags);
	__ipr_scsi_eh_done(ipr_cmd);
	spin_unlock_irqrestore(&hrrq->_lock, hrrq_flags);
}

/**
 * ipr_fail_all_ops - Fails all outstanding ops.
 * @ioa_cfg:	ioa config struct
 *
 * This function fails all outstanding ops.
 *
 * Return value:
 * 	none
 **/
static void ipr_fail_all_ops(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd, *temp;
	struct ipr_hrr_queue *hrrq;

	ENTER;
	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		list_for_each_entry_safe(ipr_cmd,
					temp, &hrrq->hrrq_pending_q, queue) {
			list_del(&ipr_cmd->queue);

			ipr_cmd->s.ioasa.hdr.ioasc =
				cpu_to_be32(IPR_IOASC_IOA_WAS_RESET);
			ipr_cmd->s.ioasa.hdr.ilid =
				cpu_to_be32(IPR_DRIVER_ILID);

			if (ipr_cmd->scsi_cmd)
				ipr_cmd->done = __ipr_scsi_eh_done;
			else if (ipr_cmd->qc)
				ipr_cmd->done = __ipr_sata_eh_done;

			ipr_trc_hook(ipr_cmd, IPR_TRACE_FINISH,
				     IPR_IOASC_IOA_WAS_RESET);
			del_timer(&ipr_cmd->timer);
			ipr_cmd->done(ipr_cmd);
		}
		spin_unlock(&hrrq->_lock);
	}
	LEAVE;
}

/**
 * ipr_send_command -  Send driver initiated requests.
 * @ipr_cmd:		ipr command struct
 *
 * This function sends a command to the adapter using the correct write call.
 * In the case of sis64, calculate the ioarcb size required. Then or in the
 * appropriate bits.
 *
 * Return value:
 * 	none
 **/
static void ipr_send_command(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	dma_addr_t send_dma_addr = ipr_cmd->dma_addr;

	if (ioa_cfg->sis64) {
		/* The default size is 256 bytes */
		send_dma_addr |= 0x1;

		/* If the number of ioadls * size of ioadl > 128 bytes,
		   then use a 512 byte ioarcb */
		if (ipr_cmd->dma_use_sg * sizeof(struct ipr_ioadl64_desc) > 128 )
			send_dma_addr |= 0x4;
		writeq(send_dma_addr, ioa_cfg->regs.ioarrin_reg);
	} else
		writel(send_dma_addr, ioa_cfg->regs.ioarrin_reg);
}

/**
 * ipr_do_req -  Send driver initiated requests.
 * @ipr_cmd:		ipr command struct
 * @done:			done function
 * @timeout_func:	timeout function
 * @timeout:		timeout value
 *
 * This function sends the specified command to the adapter with the
 * timeout given. The done function is invoked on command completion.
 *
 * Return value:
 * 	none
 **/
static void ipr_do_req(struct ipr_cmnd *ipr_cmd,
		       void (*done) (struct ipr_cmnd *),
		       void (*timeout_func) (struct timer_list *), u32 timeout)
{
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);

	ipr_cmd->done = done;

	ipr_cmd->timer.expires = jiffies + timeout;
	ipr_cmd->timer.function = timeout_func;

	add_timer(&ipr_cmd->timer);

	ipr_trc_hook(ipr_cmd, IPR_TRACE_START, 0);

	ipr_send_command(ipr_cmd);
}

/**
 * ipr_internal_cmd_done - Op done function for an internally generated op.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for an internally generated,
 * blocking op. It simply wakes the sleeping thread.
 *
 * Return value:
 * 	none
 **/
static void ipr_internal_cmd_done(struct ipr_cmnd *ipr_cmd)
{
	if (ipr_cmd->sibling)
		ipr_cmd->sibling = NULL;
	else
		complete(&ipr_cmd->completion);
}

/**
 * ipr_init_ioadl - initialize the ioadl for the correct SIS type
 * @ipr_cmd:	ipr command struct
 * @dma_addr:	dma address
 * @len:	transfer length
 * @flags:	ioadl flag value
 *
 * This function initializes an ioadl in the case where there is only a single
 * descriptor.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_init_ioadl(struct ipr_cmnd *ipr_cmd, dma_addr_t dma_addr,
			   u32 len, int flags)
{
	struct ipr_ioadl_desc *ioadl = ipr_cmd->i.ioadl;
	struct ipr_ioadl64_desc *ioadl64 = ipr_cmd->i.ioadl64;

	ipr_cmd->dma_use_sg = 1;

	if (ipr_cmd->ioa_cfg->sis64) {
		ioadl64->flags = cpu_to_be32(flags);
		ioadl64->data_len = cpu_to_be32(len);
		ioadl64->address = cpu_to_be64(dma_addr);

		ipr_cmd->ioarcb.ioadl_len =
		       	cpu_to_be32(sizeof(struct ipr_ioadl64_desc));
		ipr_cmd->ioarcb.data_transfer_length = cpu_to_be32(len);
	} else {
		ioadl->flags_and_data_len = cpu_to_be32(flags | len);
		ioadl->address = cpu_to_be32(dma_addr);

		if (flags == IPR_IOADL_FLAGS_READ_LAST) {
			ipr_cmd->ioarcb.read_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc));
			ipr_cmd->ioarcb.read_data_transfer_length = cpu_to_be32(len);
		} else {
			ipr_cmd->ioarcb.ioadl_len =
			       	cpu_to_be32(sizeof(struct ipr_ioadl_desc));
			ipr_cmd->ioarcb.data_transfer_length = cpu_to_be32(len);
		}
	}
}

/**
 * ipr_send_blocking_cmd - Send command and sleep on its completion.
 * @ipr_cmd:	ipr command struct
 * @timeout_func:	function to invoke if command times out
 * @timeout:	timeout
 *
 * Return value:
 * 	none
 **/
static void ipr_send_blocking_cmd(struct ipr_cmnd *ipr_cmd,
				  void (*timeout_func) (struct timer_list *),
				  u32 timeout)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	init_completion(&ipr_cmd->completion);
	ipr_do_req(ipr_cmd, ipr_internal_cmd_done, timeout_func, timeout);

	spin_unlock_irq(ioa_cfg->host->host_lock);
	wait_for_completion(&ipr_cmd->completion);
	spin_lock_irq(ioa_cfg->host->host_lock);
}

static int ipr_get_hrrq_index(struct ipr_ioa_cfg *ioa_cfg)
{
	unsigned int hrrq;

	if (ioa_cfg->hrrq_num == 1)
		hrrq = 0;
	else {
		hrrq = atomic_add_return(1, &ioa_cfg->hrrq_index);
		hrrq = (hrrq % (ioa_cfg->hrrq_num - 1)) + 1;
	}
	return hrrq;
}

/**
 * ipr_send_hcam - Send an HCAM to the adapter.
 * @ioa_cfg:	ioa config struct
 * @type:		HCAM type
 * @hostrcb:	hostrcb struct
 *
 * This function will send a Host Controlled Async command to the adapter.
 * If HCAMs are currently not allowed to be issued to the adapter, it will
 * place the hostrcb on the free queue.
 *
 * Return value:
 * 	none
 **/
static void ipr_send_hcam(struct ipr_ioa_cfg *ioa_cfg, u8 type,
			  struct ipr_hostrcb *hostrcb)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;

	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].allow_cmds) {
		ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
		list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);
		list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_pending_q);

		ipr_cmd->u.hostrcb = hostrcb;
		ioarcb = &ipr_cmd->ioarcb;

		ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_HCAM;
		ioarcb->cmd_pkt.cdb[0] = IPR_HOST_CONTROLLED_ASYNC;
		ioarcb->cmd_pkt.cdb[1] = type;
		ioarcb->cmd_pkt.cdb[7] = (sizeof(hostrcb->hcam) >> 8) & 0xff;
		ioarcb->cmd_pkt.cdb[8] = sizeof(hostrcb->hcam) & 0xff;

		ipr_init_ioadl(ipr_cmd, hostrcb->hostrcb_dma,
			       sizeof(hostrcb->hcam), IPR_IOADL_FLAGS_READ_LAST);

		if (type == IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE)
			ipr_cmd->done = ipr_process_ccn;
		else
			ipr_cmd->done = ipr_process_error;

		ipr_trc_hook(ipr_cmd, IPR_TRACE_START, IPR_IOA_RES_ADDR);

		ipr_send_command(ipr_cmd);
	} else {
		list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
	}
}

/**
 * ipr_update_ata_class - Update the ata class in the resource entry
 * @res:	resource entry struct
 * @proto:	cfgte device bus protocol value
 *
 * Return value:
 * 	none
 **/
static void ipr_update_ata_class(struct ipr_resource_entry *res, unsigned int proto)
{
	switch (proto) {
	case IPR_PROTO_SATA:
	case IPR_PROTO_SAS_STP:
		res->ata_class = ATA_DEV_ATA;
		break;
	case IPR_PROTO_SATA_ATAPI:
	case IPR_PROTO_SAS_STP_ATAPI:
		res->ata_class = ATA_DEV_ATAPI;
		break;
	default:
		res->ata_class = ATA_DEV_UNKNOWN;
		break;
	}
}

/**
 * ipr_init_res_entry - Initialize a resource entry struct.
 * @res:	resource entry struct
 * @cfgtew:	config table entry wrapper struct
 *
 * Return value:
 * 	none
 **/
static void ipr_init_res_entry(struct ipr_resource_entry *res,
			       struct ipr_config_table_entry_wrapper *cfgtew)
{
	int found = 0;
	unsigned int proto;
	struct ipr_ioa_cfg *ioa_cfg = res->ioa_cfg;
	struct ipr_resource_entry *gscsi_res = NULL;

	res->needs_sync_complete = 0;
	res->in_erp = 0;
	res->add_to_ml = 0;
	res->del_from_ml = 0;
	res->resetting_device = 0;
	res->reset_occurred = 0;
	res->sdev = NULL;
	res->sata_port = NULL;

	if (ioa_cfg->sis64) {
		proto = cfgtew->u.cfgte64->proto;
		res->flags = be16_to_cpu(cfgtew->u.cfgte64->flags);
		res->res_flags = be16_to_cpu(cfgtew->u.cfgte64->res_flags);
		res->qmodel = IPR_QUEUEING_MODEL64(res);
		res->type = cfgtew->u.cfgte64->res_type;

		memcpy(res->res_path, &cfgtew->u.cfgte64->res_path,
			sizeof(res->res_path));

		res->bus = 0;
		memcpy(&res->dev_lun.scsi_lun, &cfgtew->u.cfgte64->lun,
			sizeof(res->dev_lun.scsi_lun));
		res->lun = scsilun_to_int(&res->dev_lun);

		if (res->type == IPR_RES_TYPE_GENERIC_SCSI) {
			list_for_each_entry(gscsi_res, &ioa_cfg->used_res_q, queue) {
				if (gscsi_res->dev_id == cfgtew->u.cfgte64->dev_id) {
					found = 1;
					res->target = gscsi_res->target;
					break;
				}
			}
			if (!found) {
				res->target = find_first_zero_bit(ioa_cfg->target_ids,
								  ioa_cfg->max_devs_supported);
				set_bit(res->target, ioa_cfg->target_ids);
			}
		} else if (res->type == IPR_RES_TYPE_IOAFP) {
			res->bus = IPR_IOAFP_VIRTUAL_BUS;
			res->target = 0;
		} else if (res->type == IPR_RES_TYPE_ARRAY) {
			res->bus = IPR_ARRAY_VIRTUAL_BUS;
			res->target = find_first_zero_bit(ioa_cfg->array_ids,
							  ioa_cfg->max_devs_supported);
			set_bit(res->target, ioa_cfg->array_ids);
		} else if (res->type == IPR_RES_TYPE_VOLUME_SET) {
			res->bus = IPR_VSET_VIRTUAL_BUS;
			res->target = find_first_zero_bit(ioa_cfg->vset_ids,
							  ioa_cfg->max_devs_supported);
			set_bit(res->target, ioa_cfg->vset_ids);
		} else {
			res->target = find_first_zero_bit(ioa_cfg->target_ids,
							  ioa_cfg->max_devs_supported);
			set_bit(res->target, ioa_cfg->target_ids);
		}
	} else {
		proto = cfgtew->u.cfgte->proto;
		res->qmodel = IPR_QUEUEING_MODEL(res);
		res->flags = cfgtew->u.cfgte->flags;
		if (res->flags & IPR_IS_IOA_RESOURCE)
			res->type = IPR_RES_TYPE_IOAFP;
		else
			res->type = cfgtew->u.cfgte->rsvd_subtype & 0x0f;

		res->bus = cfgtew->u.cfgte->res_addr.bus;
		res->target = cfgtew->u.cfgte->res_addr.target;
		res->lun = cfgtew->u.cfgte->res_addr.lun;
		res->lun_wwn = get_unaligned_be64(cfgtew->u.cfgte->lun_wwn);
	}

	ipr_update_ata_class(res, proto);
}

/**
 * ipr_is_same_device - Determine if two devices are the same.
 * @res:	resource entry struct
 * @cfgtew:	config table entry wrapper struct
 *
 * Return value:
 * 	1 if the devices are the same / 0 otherwise
 **/
static int ipr_is_same_device(struct ipr_resource_entry *res,
			      struct ipr_config_table_entry_wrapper *cfgtew)
{
	if (res->ioa_cfg->sis64) {
		if (!memcmp(&res->dev_id, &cfgtew->u.cfgte64->dev_id,
					sizeof(cfgtew->u.cfgte64->dev_id)) &&
			!memcmp(&res->dev_lun.scsi_lun, &cfgtew->u.cfgte64->lun,
					sizeof(cfgtew->u.cfgte64->lun))) {
			return 1;
		}
	} else {
		if (res->bus == cfgtew->u.cfgte->res_addr.bus &&
		    res->target == cfgtew->u.cfgte->res_addr.target &&
		    res->lun == cfgtew->u.cfgte->res_addr.lun)
			return 1;
	}

	return 0;
}

/**
 * __ipr_format_res_path - Format the resource path for printing.
 * @res_path:	resource path
 * @buffer:	buffer
 * @len:	length of buffer provided
 *
 * Return value:
 * 	pointer to buffer
 **/
static char *__ipr_format_res_path(u8 *res_path, char *buffer, int len)
{
	int i;
	char *p = buffer;

	*p = '\0';
	p += scnprintf(p, buffer + len - p, "%02X", res_path[0]);
	for (i = 1; res_path[i] != 0xff && ((i * 3) < len); i++)
		p += scnprintf(p, buffer + len - p, "-%02X", res_path[i]);

	return buffer;
}

/**
 * ipr_format_res_path - Format the resource path for printing.
 * @ioa_cfg:	ioa config struct
 * @res_path:	resource path
 * @buffer:	buffer
 * @len:	length of buffer provided
 *
 * Return value:
 *	pointer to buffer
 **/
static char *ipr_format_res_path(struct ipr_ioa_cfg *ioa_cfg,
				 u8 *res_path, char *buffer, int len)
{
	char *p = buffer;

	*p = '\0';
	p += scnprintf(p, buffer + len - p, "%d/", ioa_cfg->host->host_no);
	__ipr_format_res_path(res_path, p, len - (buffer - p));
	return buffer;
}

/**
 * ipr_update_res_entry - Update the resource entry.
 * @res:	resource entry struct
 * @cfgtew:	config table entry wrapper struct
 *
 * Return value:
 *      none
 **/
static void ipr_update_res_entry(struct ipr_resource_entry *res,
				 struct ipr_config_table_entry_wrapper *cfgtew)
{
	char buffer[IPR_MAX_RES_PATH_LENGTH];
	unsigned int proto;
	int new_path = 0;

	if (res->ioa_cfg->sis64) {
		res->flags = be16_to_cpu(cfgtew->u.cfgte64->flags);
		res->res_flags = be16_to_cpu(cfgtew->u.cfgte64->res_flags);
		res->type = cfgtew->u.cfgte64->res_type;

		memcpy(&res->std_inq_data, &cfgtew->u.cfgte64->std_inq_data,
			sizeof(struct ipr_std_inq_data));

		res->qmodel = IPR_QUEUEING_MODEL64(res);
		proto = cfgtew->u.cfgte64->proto;
		res->res_handle = cfgtew->u.cfgte64->res_handle;
		res->dev_id = cfgtew->u.cfgte64->dev_id;

		memcpy(&res->dev_lun.scsi_lun, &cfgtew->u.cfgte64->lun,
			sizeof(res->dev_lun.scsi_lun));

		if (memcmp(res->res_path, &cfgtew->u.cfgte64->res_path,
					sizeof(res->res_path))) {
			memcpy(res->res_path, &cfgtew->u.cfgte64->res_path,
				sizeof(res->res_path));
			new_path = 1;
		}

		if (res->sdev && new_path)
			sdev_printk(KERN_INFO, res->sdev, "Resource path: %s\n",
				    ipr_format_res_path(res->ioa_cfg,
					res->res_path, buffer, sizeof(buffer)));
	} else {
		res->flags = cfgtew->u.cfgte->flags;
		if (res->flags & IPR_IS_IOA_RESOURCE)
			res->type = IPR_RES_TYPE_IOAFP;
		else
			res->type = cfgtew->u.cfgte->rsvd_subtype & 0x0f;

		memcpy(&res->std_inq_data, &cfgtew->u.cfgte->std_inq_data,
			sizeof(struct ipr_std_inq_data));

		res->qmodel = IPR_QUEUEING_MODEL(res);
		proto = cfgtew->u.cfgte->proto;
		res->res_handle = cfgtew->u.cfgte->res_handle;
	}

	ipr_update_ata_class(res, proto);
}

/**
 * ipr_clear_res_target - Clear the bit in the bit map representing the target
 * 			  for the resource.
 * @res:	resource entry struct
 *
 * Return value:
 *      none
 **/
static void ipr_clear_res_target(struct ipr_resource_entry *res)
{
	struct ipr_resource_entry *gscsi_res = NULL;
	struct ipr_ioa_cfg *ioa_cfg = res->ioa_cfg;

	if (!ioa_cfg->sis64)
		return;

	if (res->bus == IPR_ARRAY_VIRTUAL_BUS)
		clear_bit(res->target, ioa_cfg->array_ids);
	else if (res->bus == IPR_VSET_VIRTUAL_BUS)
		clear_bit(res->target, ioa_cfg->vset_ids);
	else if (res->bus == 0 && res->type == IPR_RES_TYPE_GENERIC_SCSI) {
		list_for_each_entry(gscsi_res, &ioa_cfg->used_res_q, queue)
			if (gscsi_res->dev_id == res->dev_id && gscsi_res != res)
				return;
		clear_bit(res->target, ioa_cfg->target_ids);

	} else if (res->bus == 0)
		clear_bit(res->target, ioa_cfg->target_ids);
}

/**
 * ipr_handle_config_change - Handle a config change from the adapter
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb
 *
 * Return value:
 * 	none
 **/
static void ipr_handle_config_change(struct ipr_ioa_cfg *ioa_cfg,
				     struct ipr_hostrcb *hostrcb)
{
	struct ipr_resource_entry *res = NULL;
	struct ipr_config_table_entry_wrapper cfgtew;
	__be32 cc_res_handle;

	u32 is_ndn = 1;

	if (ioa_cfg->sis64) {
		cfgtew.u.cfgte64 = &hostrcb->hcam.u.ccn.u.cfgte64;
		cc_res_handle = cfgtew.u.cfgte64->res_handle;
	} else {
		cfgtew.u.cfgte = &hostrcb->hcam.u.ccn.u.cfgte;
		cc_res_handle = cfgtew.u.cfgte->res_handle;
	}

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (res->res_handle == cc_res_handle) {
			is_ndn = 0;
			break;
		}
	}

	if (is_ndn) {
		if (list_empty(&ioa_cfg->free_res_q)) {
			ipr_send_hcam(ioa_cfg,
				      IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
				      hostrcb);
			return;
		}

		res = list_entry(ioa_cfg->free_res_q.next,
				 struct ipr_resource_entry, queue);

		list_del(&res->queue);
		ipr_init_res_entry(res, &cfgtew);
		list_add_tail(&res->queue, &ioa_cfg->used_res_q);
	}

	ipr_update_res_entry(res, &cfgtew);

	if (hostrcb->hcam.notify_type == IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY) {
		if (res->sdev) {
			res->del_from_ml = 1;
			res->res_handle = IPR_INVALID_RES_HANDLE;
			schedule_work(&ioa_cfg->work_q);
		} else {
			ipr_clear_res_target(res);
			list_move_tail(&res->queue, &ioa_cfg->free_res_q);
		}
	} else if (!res->sdev || res->del_from_ml) {
		res->add_to_ml = 1;
		schedule_work(&ioa_cfg->work_q);
	}

	ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
}

/**
 * ipr_process_ccn - Op done function for a CCN.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for a configuration
 * change notification host controlled async from the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_process_ccn(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb = ipr_cmd->u.hostrcb;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	list_del_init(&hostrcb->queue);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);

	if (ioasc) {
		if (ioasc != IPR_IOASC_IOA_WAS_RESET &&
		    ioasc != IPR_IOASC_ABORTED_CMD_TERM_BY_HOST)
			dev_err(&ioa_cfg->pdev->dev,
				"Host RCB failed with IOASC: 0x%08X\n", ioasc);

		ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
	} else {
		ipr_handle_config_change(ioa_cfg, hostrcb);
	}
}

/**
 * strip_and_pad_whitespace - Strip and pad trailing whitespace.
 * @i:		index into buffer
 * @buf:		string to modify
 *
 * This function will strip all trailing whitespace, pad the end
 * of the string with a single space, and NULL terminate the string.
 *
 * Return value:
 * 	new length of string
 **/
static int strip_and_pad_whitespace(int i, char *buf)
{
	while (i && buf[i] == ' ')
		i--;
	buf[i+1] = ' ';
	buf[i+2] = '\0';
	return i + 2;
}

/**
 * ipr_log_vpd_compact - Log the passed extended VPD compactly.
 * @prefix:		string to print at start of printk
 * @hostrcb:	hostrcb pointer
 * @vpd:		vendor/product id/sn struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_vpd_compact(char *prefix, struct ipr_hostrcb *hostrcb,
				struct ipr_vpd *vpd)
{
	char buffer[IPR_VENDOR_ID_LEN + IPR_PROD_ID_LEN + IPR_SERIAL_NUM_LEN + 3];
	int i = 0;

	memcpy(buffer, vpd->vpids.vendor_id, IPR_VENDOR_ID_LEN);
	i = strip_and_pad_whitespace(IPR_VENDOR_ID_LEN - 1, buffer);

	memcpy(&buffer[i], vpd->vpids.product_id, IPR_PROD_ID_LEN);
	i = strip_and_pad_whitespace(i + IPR_PROD_ID_LEN - 1, buffer);

	memcpy(&buffer[i], vpd->sn, IPR_SERIAL_NUM_LEN);
	buffer[IPR_SERIAL_NUM_LEN + i] = '\0';

	ipr_hcam_err(hostrcb, "%s VPID/SN: %s\n", prefix, buffer);
}

/**
 * ipr_log_vpd - Log the passed VPD to the error log.
 * @vpd:		vendor/product id/sn struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_vpd(struct ipr_vpd *vpd)
{
	char buffer[IPR_VENDOR_ID_LEN + IPR_PROD_ID_LEN
		    + IPR_SERIAL_NUM_LEN];

	memcpy(buffer, vpd->vpids.vendor_id, IPR_VENDOR_ID_LEN);
	memcpy(buffer + IPR_VENDOR_ID_LEN, vpd->vpids.product_id,
	       IPR_PROD_ID_LEN);
	buffer[IPR_VENDOR_ID_LEN + IPR_PROD_ID_LEN] = '\0';
	ipr_err("Vendor/Product ID: %s\n", buffer);

	memcpy(buffer, vpd->sn, IPR_SERIAL_NUM_LEN);
	buffer[IPR_SERIAL_NUM_LEN] = '\0';
	ipr_err("    Serial Number: %s\n", buffer);
}

/**
 * ipr_log_ext_vpd_compact - Log the passed extended VPD compactly.
 * @prefix:		string to print at start of printk
 * @hostrcb:	hostrcb pointer
 * @vpd:		vendor/product id/sn/wwn struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_ext_vpd_compact(char *prefix, struct ipr_hostrcb *hostrcb,
				    struct ipr_ext_vpd *vpd)
{
	ipr_log_vpd_compact(prefix, hostrcb, &vpd->vpd);
	ipr_hcam_err(hostrcb, "%s WWN: %08X%08X\n", prefix,
		     be32_to_cpu(vpd->wwid[0]), be32_to_cpu(vpd->wwid[1]));
}

/**
 * ipr_log_ext_vpd - Log the passed extended VPD to the error log.
 * @vpd:		vendor/product id/sn/wwn struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_ext_vpd(struct ipr_ext_vpd *vpd)
{
	ipr_log_vpd(&vpd->vpd);
	ipr_err("    WWN: %08X%08X\n", be32_to_cpu(vpd->wwid[0]),
		be32_to_cpu(vpd->wwid[1]));
}

/**
 * ipr_log_enhanced_cache_error - Log a cache error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_enhanced_cache_error(struct ipr_ioa_cfg *ioa_cfg,
					 struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_12_error *error;

	if (ioa_cfg->sis64)
		error = &hostrcb->hcam.u.error64.u.type_12_error;
	else
		error = &hostrcb->hcam.u.error.u.type_12_error;

	ipr_err("-----Current Configuration-----\n");
	ipr_err("Cache Directory Card Information:\n");
	ipr_log_ext_vpd(&error->ioa_vpd);
	ipr_err("Adapter Card Information:\n");
	ipr_log_ext_vpd(&error->cfc_vpd);

	ipr_err("-----Expected Configuration-----\n");
	ipr_err("Cache Directory Card Information:\n");
	ipr_log_ext_vpd(&error->ioa_last_attached_to_cfc_vpd);
	ipr_err("Adapter Card Information:\n");
	ipr_log_ext_vpd(&error->cfc_last_attached_to_ioa_vpd);

	ipr_err("Additional IOA Data: %08X %08X %08X\n",
		     be32_to_cpu(error->ioa_data[0]),
		     be32_to_cpu(error->ioa_data[1]),
		     be32_to_cpu(error->ioa_data[2]));
}

/**
 * ipr_log_cache_error - Log a cache error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_cache_error(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_02_error *error =
		&hostrcb->hcam.u.error.u.type_02_error;

	ipr_err("-----Current Configuration-----\n");
	ipr_err("Cache Directory Card Information:\n");
	ipr_log_vpd(&error->ioa_vpd);
	ipr_err("Adapter Card Information:\n");
	ipr_log_vpd(&error->cfc_vpd);

	ipr_err("-----Expected Configuration-----\n");
	ipr_err("Cache Directory Card Information:\n");
	ipr_log_vpd(&error->ioa_last_attached_to_cfc_vpd);
	ipr_err("Adapter Card Information:\n");
	ipr_log_vpd(&error->cfc_last_attached_to_ioa_vpd);

	ipr_err("Additional IOA Data: %08X %08X %08X\n",
		     be32_to_cpu(error->ioa_data[0]),
		     be32_to_cpu(error->ioa_data[1]),
		     be32_to_cpu(error->ioa_data[2]));
}

/**
 * ipr_log_enhanced_config_error - Log a configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_enhanced_config_error(struct ipr_ioa_cfg *ioa_cfg,
					  struct ipr_hostrcb *hostrcb)
{
	int errors_logged, i;
	struct ipr_hostrcb_device_data_entry_enhanced *dev_entry;
	struct ipr_hostrcb_type_13_error *error;

	error = &hostrcb->hcam.u.error.u.type_13_error;
	errors_logged = be32_to_cpu(error->errors_logged);

	ipr_err("Device Errors Detected/Logged: %d/%d\n",
		be32_to_cpu(error->errors_detected), errors_logged);

	dev_entry = error->dev;

	for (i = 0; i < errors_logged; i++, dev_entry++) {
		ipr_err_separator;

		ipr_phys_res_err(ioa_cfg, dev_entry->dev_res_addr, "Device %d", i + 1);
		ipr_log_ext_vpd(&dev_entry->vpd);

		ipr_err("-----New Device Information-----\n");
		ipr_log_ext_vpd(&dev_entry->new_vpd);

		ipr_err("Cache Directory Card Information:\n");
		ipr_log_ext_vpd(&dev_entry->ioa_last_with_dev_vpd);

		ipr_err("Adapter Card Information:\n");
		ipr_log_ext_vpd(&dev_entry->cfc_last_with_dev_vpd);
	}
}

/**
 * ipr_log_sis64_config_error - Log a device error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_sis64_config_error(struct ipr_ioa_cfg *ioa_cfg,
				       struct ipr_hostrcb *hostrcb)
{
	int errors_logged, i;
	struct ipr_hostrcb64_device_data_entry_enhanced *dev_entry;
	struct ipr_hostrcb_type_23_error *error;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	error = &hostrcb->hcam.u.error64.u.type_23_error;
	errors_logged = be32_to_cpu(error->errors_logged);

	ipr_err("Device Errors Detected/Logged: %d/%d\n",
		be32_to_cpu(error->errors_detected), errors_logged);

	dev_entry = error->dev;

	for (i = 0; i < errors_logged; i++, dev_entry++) {
		ipr_err_separator;

		ipr_err("Device %d : %s", i + 1,
			__ipr_format_res_path(dev_entry->res_path,
					      buffer, sizeof(buffer)));
		ipr_log_ext_vpd(&dev_entry->vpd);

		ipr_err("-----New Device Information-----\n");
		ipr_log_ext_vpd(&dev_entry->new_vpd);

		ipr_err("Cache Directory Card Information:\n");
		ipr_log_ext_vpd(&dev_entry->ioa_last_with_dev_vpd);

		ipr_err("Adapter Card Information:\n");
		ipr_log_ext_vpd(&dev_entry->cfc_last_with_dev_vpd);
	}
}

/**
 * ipr_log_config_error - Log a configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_config_error(struct ipr_ioa_cfg *ioa_cfg,
				 struct ipr_hostrcb *hostrcb)
{
	int errors_logged, i;
	struct ipr_hostrcb_device_data_entry *dev_entry;
	struct ipr_hostrcb_type_03_error *error;

	error = &hostrcb->hcam.u.error.u.type_03_error;
	errors_logged = be32_to_cpu(error->errors_logged);

	ipr_err("Device Errors Detected/Logged: %d/%d\n",
		be32_to_cpu(error->errors_detected), errors_logged);

	dev_entry = error->dev;

	for (i = 0; i < errors_logged; i++, dev_entry++) {
		ipr_err_separator;

		ipr_phys_res_err(ioa_cfg, dev_entry->dev_res_addr, "Device %d", i + 1);
		ipr_log_vpd(&dev_entry->vpd);

		ipr_err("-----New Device Information-----\n");
		ipr_log_vpd(&dev_entry->new_vpd);

		ipr_err("Cache Directory Card Information:\n");
		ipr_log_vpd(&dev_entry->ioa_last_with_dev_vpd);

		ipr_err("Adapter Card Information:\n");
		ipr_log_vpd(&dev_entry->cfc_last_with_dev_vpd);

		ipr_err("Additional IOA Data: %08X %08X %08X %08X %08X\n",
			be32_to_cpu(dev_entry->ioa_data[0]),
			be32_to_cpu(dev_entry->ioa_data[1]),
			be32_to_cpu(dev_entry->ioa_data[2]),
			be32_to_cpu(dev_entry->ioa_data[3]),
			be32_to_cpu(dev_entry->ioa_data[4]));
	}
}

/**
 * ipr_log_enhanced_array_error - Log an array configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_enhanced_array_error(struct ipr_ioa_cfg *ioa_cfg,
					 struct ipr_hostrcb *hostrcb)
{
	int i, num_entries;
	struct ipr_hostrcb_type_14_error *error;
	struct ipr_hostrcb_array_data_entry_enhanced *array_entry;
	const u8 zero_sn[IPR_SERIAL_NUM_LEN] = { [0 ... IPR_SERIAL_NUM_LEN-1] = '0' };

	error = &hostrcb->hcam.u.error.u.type_14_error;

	ipr_err_separator;

	ipr_err("RAID %s Array Configuration: %d:%d:%d:%d\n",
		error->protection_level,
		ioa_cfg->host->host_no,
		error->last_func_vset_res_addr.bus,
		error->last_func_vset_res_addr.target,
		error->last_func_vset_res_addr.lun);

	ipr_err_separator;

	array_entry = error->array_member;
	num_entries = min_t(u32, be32_to_cpu(error->num_entries),
			    ARRAY_SIZE(error->array_member));

	for (i = 0; i < num_entries; i++, array_entry++) {
		if (!memcmp(array_entry->vpd.vpd.sn, zero_sn, IPR_SERIAL_NUM_LEN))
			continue;

		if (be32_to_cpu(error->exposed_mode_adn) == i)
			ipr_err("Exposed Array Member %d:\n", i);
		else
			ipr_err("Array Member %d:\n", i);

		ipr_log_ext_vpd(&array_entry->vpd);
		ipr_phys_res_err(ioa_cfg, array_entry->dev_res_addr, "Current Location");
		ipr_phys_res_err(ioa_cfg, array_entry->expected_dev_res_addr,
				 "Expected Location");

		ipr_err_separator;
	}
}

/**
 * ipr_log_array_error - Log an array configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_array_error(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	int i;
	struct ipr_hostrcb_type_04_error *error;
	struct ipr_hostrcb_array_data_entry *array_entry;
	const u8 zero_sn[IPR_SERIAL_NUM_LEN] = { [0 ... IPR_SERIAL_NUM_LEN-1] = '0' };

	error = &hostrcb->hcam.u.error.u.type_04_error;

	ipr_err_separator;

	ipr_err("RAID %s Array Configuration: %d:%d:%d:%d\n",
		error->protection_level,
		ioa_cfg->host->host_no,
		error->last_func_vset_res_addr.bus,
		error->last_func_vset_res_addr.target,
		error->last_func_vset_res_addr.lun);

	ipr_err_separator;

	array_entry = error->array_member;

	for (i = 0; i < 18; i++) {
		if (!memcmp(array_entry->vpd.sn, zero_sn, IPR_SERIAL_NUM_LEN))
			continue;

		if (be32_to_cpu(error->exposed_mode_adn) == i)
			ipr_err("Exposed Array Member %d:\n", i);
		else
			ipr_err("Array Member %d:\n", i);

		ipr_log_vpd(&array_entry->vpd);

		ipr_phys_res_err(ioa_cfg, array_entry->dev_res_addr, "Current Location");
		ipr_phys_res_err(ioa_cfg, array_entry->expected_dev_res_addr,
				 "Expected Location");

		ipr_err_separator;

		if (i == 9)
			array_entry = error->array_member2;
		else
			array_entry++;
	}
}

/**
 * ipr_log_hex_data - Log additional hex IOA error data.
 * @ioa_cfg:	ioa config struct
 * @data:		IOA error data
 * @len:		data length
 *
 * Return value:
 * 	none
 **/
static void ipr_log_hex_data(struct ipr_ioa_cfg *ioa_cfg, __be32 *data, int len)
{
	int i;

	if (len == 0)
		return;

	if (ioa_cfg->log_level <= IPR_DEFAULT_LOG_LEVEL)
		len = min_t(int, len, IPR_DEFAULT_MAX_ERROR_DUMP);

	for (i = 0; i < len / 4; i += 4) {
		ipr_err("%08X: %08X %08X %08X %08X\n", i*4,
			be32_to_cpu(data[i]),
			be32_to_cpu(data[i+1]),
			be32_to_cpu(data[i+2]),
			be32_to_cpu(data[i+3]));
	}
}

/**
 * ipr_log_enhanced_dual_ioa_error - Log an enhanced dual adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_enhanced_dual_ioa_error(struct ipr_ioa_cfg *ioa_cfg,
					    struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_17_error *error;

	if (ioa_cfg->sis64)
		error = &hostrcb->hcam.u.error64.u.type_17_error;
	else
		error = &hostrcb->hcam.u.error.u.type_17_error;

	error->failure_reason[sizeof(error->failure_reason) - 1] = '\0';
	strim(error->failure_reason);

	ipr_hcam_err(hostrcb, "%s [PRC: %08X]\n", error->failure_reason,
		     be32_to_cpu(hostrcb->hcam.u.error.prc));
	ipr_log_ext_vpd_compact("Remote IOA", hostrcb, &error->vpd);
	ipr_log_hex_data(ioa_cfg, error->data,
			 be32_to_cpu(hostrcb->hcam.length) -
			 (offsetof(struct ipr_hostrcb_error, u) +
			  offsetof(struct ipr_hostrcb_type_17_error, data)));
}

/**
 * ipr_log_dual_ioa_error - Log a dual adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_dual_ioa_error(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_07_error *error;

	error = &hostrcb->hcam.u.error.u.type_07_error;
	error->failure_reason[sizeof(error->failure_reason) - 1] = '\0';
	strim(error->failure_reason);

	ipr_hcam_err(hostrcb, "%s [PRC: %08X]\n", error->failure_reason,
		     be32_to_cpu(hostrcb->hcam.u.error.prc));
	ipr_log_vpd_compact("Remote IOA", hostrcb, &error->vpd);
	ipr_log_hex_data(ioa_cfg, error->data,
			 be32_to_cpu(hostrcb->hcam.length) -
			 (offsetof(struct ipr_hostrcb_error, u) +
			  offsetof(struct ipr_hostrcb_type_07_error, data)));
}

static const struct {
	u8 active;
	char *desc;
} path_active_desc[] = {
	{ IPR_PATH_NO_INFO, "Path" },
	{ IPR_PATH_ACTIVE, "Active path" },
	{ IPR_PATH_NOT_ACTIVE, "Inactive path" }
};

static const struct {
	u8 state;
	char *desc;
} path_state_desc[] = {
	{ IPR_PATH_STATE_NO_INFO, "has no path state information available" },
	{ IPR_PATH_HEALTHY, "is healthy" },
	{ IPR_PATH_DEGRADED, "is degraded" },
	{ IPR_PATH_FAILED, "is failed" }
};

/**
 * ipr_log_fabric_path - Log a fabric path error
 * @hostrcb:	hostrcb struct
 * @fabric:		fabric descriptor
 *
 * Return value:
 * 	none
 **/
static void ipr_log_fabric_path(struct ipr_hostrcb *hostrcb,
				struct ipr_hostrcb_fabric_desc *fabric)
{
	int i, j;
	u8 path_state = fabric->path_state;
	u8 active = path_state & IPR_PATH_ACTIVE_MASK;
	u8 state = path_state & IPR_PATH_STATE_MASK;

	for (i = 0; i < ARRAY_SIZE(path_active_desc); i++) {
		if (path_active_desc[i].active != active)
			continue;

		for (j = 0; j < ARRAY_SIZE(path_state_desc); j++) {
			if (path_state_desc[j].state != state)
				continue;

			if (fabric->cascaded_expander == 0xff && fabric->phy == 0xff) {
				ipr_hcam_err(hostrcb, "%s %s: IOA Port=%d\n",
					     path_active_desc[i].desc, path_state_desc[j].desc,
					     fabric->ioa_port);
			} else if (fabric->cascaded_expander == 0xff) {
				ipr_hcam_err(hostrcb, "%s %s: IOA Port=%d, Phy=%d\n",
					     path_active_desc[i].desc, path_state_desc[j].desc,
					     fabric->ioa_port, fabric->phy);
			} else if (fabric->phy == 0xff) {
				ipr_hcam_err(hostrcb, "%s %s: IOA Port=%d, Cascade=%d\n",
					     path_active_desc[i].desc, path_state_desc[j].desc,
					     fabric->ioa_port, fabric->cascaded_expander);
			} else {
				ipr_hcam_err(hostrcb, "%s %s: IOA Port=%d, Cascade=%d, Phy=%d\n",
					     path_active_desc[i].desc, path_state_desc[j].desc,
					     fabric->ioa_port, fabric->cascaded_expander, fabric->phy);
			}
			return;
		}
	}

	ipr_err("Path state=%02X IOA Port=%d Cascade=%d Phy=%d\n", path_state,
		fabric->ioa_port, fabric->cascaded_expander, fabric->phy);
}

/**
 * ipr_log64_fabric_path - Log a fabric path error
 * @hostrcb:	hostrcb struct
 * @fabric:		fabric descriptor
 *
 * Return value:
 * 	none
 **/
static void ipr_log64_fabric_path(struct ipr_hostrcb *hostrcb,
				  struct ipr_hostrcb64_fabric_desc *fabric)
{
	int i, j;
	u8 path_state = fabric->path_state;
	u8 active = path_state & IPR_PATH_ACTIVE_MASK;
	u8 state = path_state & IPR_PATH_STATE_MASK;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	for (i = 0; i < ARRAY_SIZE(path_active_desc); i++) {
		if (path_active_desc[i].active != active)
			continue;

		for (j = 0; j < ARRAY_SIZE(path_state_desc); j++) {
			if (path_state_desc[j].state != state)
				continue;

			ipr_hcam_err(hostrcb, "%s %s: Resource Path=%s\n",
				     path_active_desc[i].desc, path_state_desc[j].desc,
				     ipr_format_res_path(hostrcb->ioa_cfg,
						fabric->res_path,
						buffer, sizeof(buffer)));
			return;
		}
	}

	ipr_err("Path state=%02X Resource Path=%s\n", path_state,
		ipr_format_res_path(hostrcb->ioa_cfg, fabric->res_path,
				    buffer, sizeof(buffer)));
}

static const struct {
	u8 type;
	char *desc;
} path_type_desc[] = {
	{ IPR_PATH_CFG_IOA_PORT, "IOA port" },
	{ IPR_PATH_CFG_EXP_PORT, "Expander port" },
	{ IPR_PATH_CFG_DEVICE_PORT, "Device port" },
	{ IPR_PATH_CFG_DEVICE_LUN, "Device LUN" }
};

static const struct {
	u8 status;
	char *desc;
} path_status_desc[] = {
	{ IPR_PATH_CFG_NO_PROB, "Functional" },
	{ IPR_PATH_CFG_DEGRADED, "Degraded" },
	{ IPR_PATH_CFG_FAILED, "Failed" },
	{ IPR_PATH_CFG_SUSPECT, "Suspect" },
	{ IPR_PATH_NOT_DETECTED, "Missing" },
	{ IPR_PATH_INCORRECT_CONN, "Incorrectly connected" }
};

static const char *link_rate[] = {
	"unknown",
	"disabled",
	"phy reset problem",
	"spinup hold",
	"port selector",
	"unknown",
	"unknown",
	"unknown",
	"1.5Gbps",
	"3.0Gbps",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown"
};

/**
 * ipr_log_path_elem - Log a fabric path element.
 * @hostrcb:	hostrcb struct
 * @cfg:		fabric path element struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_path_elem(struct ipr_hostrcb *hostrcb,
			      struct ipr_hostrcb_config_element *cfg)
{
	int i, j;
	u8 type = cfg->type_status & IPR_PATH_CFG_TYPE_MASK;
	u8 status = cfg->type_status & IPR_PATH_CFG_STATUS_MASK;

	if (type == IPR_PATH_CFG_NOT_EXIST)
		return;

	for (i = 0; i < ARRAY_SIZE(path_type_desc); i++) {
		if (path_type_desc[i].type != type)
			continue;

		for (j = 0; j < ARRAY_SIZE(path_status_desc); j++) {
			if (path_status_desc[j].status != status)
				continue;

			if (type == IPR_PATH_CFG_IOA_PORT) {
				ipr_hcam_err(hostrcb, "%s %s: Phy=%d, Link rate=%s, WWN=%08X%08X\n",
					     path_status_desc[j].desc, path_type_desc[i].desc,
					     cfg->phy, link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
					     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
			} else {
				if (cfg->cascaded_expander == 0xff && cfg->phy == 0xff) {
					ipr_hcam_err(hostrcb, "%s %s: Link rate=%s, WWN=%08X%08X\n",
						     path_status_desc[j].desc, path_type_desc[i].desc,
						     link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
						     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
				} else if (cfg->cascaded_expander == 0xff) {
					ipr_hcam_err(hostrcb, "%s %s: Phy=%d, Link rate=%s, "
						     "WWN=%08X%08X\n", path_status_desc[j].desc,
						     path_type_desc[i].desc, cfg->phy,
						     link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
						     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
				} else if (cfg->phy == 0xff) {
					ipr_hcam_err(hostrcb, "%s %s: Cascade=%d, Link rate=%s, "
						     "WWN=%08X%08X\n", path_status_desc[j].desc,
						     path_type_desc[i].desc, cfg->cascaded_expander,
						     link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
						     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
				} else {
					ipr_hcam_err(hostrcb, "%s %s: Cascade=%d, Phy=%d, Link rate=%s "
						     "WWN=%08X%08X\n", path_status_desc[j].desc,
						     path_type_desc[i].desc, cfg->cascaded_expander, cfg->phy,
						     link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
						     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
				}
			}
			return;
		}
	}

	ipr_hcam_err(hostrcb, "Path element=%02X: Cascade=%d Phy=%d Link rate=%s "
		     "WWN=%08X%08X\n", cfg->type_status, cfg->cascaded_expander, cfg->phy,
		     link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
		     be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
}

/**
 * ipr_log64_path_elem - Log a fabric path element.
 * @hostrcb:	hostrcb struct
 * @cfg:		fabric path element struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log64_path_elem(struct ipr_hostrcb *hostrcb,
				struct ipr_hostrcb64_config_element *cfg)
{
	int i, j;
	u8 desc_id = cfg->descriptor_id & IPR_DESCRIPTOR_MASK;
	u8 type = cfg->type_status & IPR_PATH_CFG_TYPE_MASK;
	u8 status = cfg->type_status & IPR_PATH_CFG_STATUS_MASK;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	if (type == IPR_PATH_CFG_NOT_EXIST || desc_id != IPR_DESCRIPTOR_SIS64)
		return;

	for (i = 0; i < ARRAY_SIZE(path_type_desc); i++) {
		if (path_type_desc[i].type != type)
			continue;

		for (j = 0; j < ARRAY_SIZE(path_status_desc); j++) {
			if (path_status_desc[j].status != status)
				continue;

			ipr_hcam_err(hostrcb, "%s %s: Resource Path=%s, Link rate=%s, WWN=%08X%08X\n",
				     path_status_desc[j].desc, path_type_desc[i].desc,
				     ipr_format_res_path(hostrcb->ioa_cfg,
					cfg->res_path, buffer, sizeof(buffer)),
					link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
					be32_to_cpu(cfg->wwid[0]),
					be32_to_cpu(cfg->wwid[1]));
			return;
		}
	}
	ipr_hcam_err(hostrcb, "Path element=%02X: Resource Path=%s, Link rate=%s "
		     "WWN=%08X%08X\n", cfg->type_status,
		     ipr_format_res_path(hostrcb->ioa_cfg,
			cfg->res_path, buffer, sizeof(buffer)),
			link_rate[cfg->link_rate & IPR_PHY_LINK_RATE_MASK],
			be32_to_cpu(cfg->wwid[0]), be32_to_cpu(cfg->wwid[1]));
}

/**
 * ipr_log_fabric_error - Log a fabric error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_fabric_error(struct ipr_ioa_cfg *ioa_cfg,
				 struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_20_error *error;
	struct ipr_hostrcb_fabric_desc *fabric;
	struct ipr_hostrcb_config_element *cfg;
	int i, add_len;

	error = &hostrcb->hcam.u.error.u.type_20_error;
	error->failure_reason[sizeof(error->failure_reason) - 1] = '\0';
	ipr_hcam_err(hostrcb, "%s\n", error->failure_reason);

	add_len = be32_to_cpu(hostrcb->hcam.length) -
		(offsetof(struct ipr_hostrcb_error, u) +
		 offsetof(struct ipr_hostrcb_type_20_error, desc));

	for (i = 0, fabric = error->desc; i < error->num_entries; i++) {
		ipr_log_fabric_path(hostrcb, fabric);
		for_each_fabric_cfg(fabric, cfg)
			ipr_log_path_elem(hostrcb, cfg);

		add_len -= be16_to_cpu(fabric->length);
		fabric = (struct ipr_hostrcb_fabric_desc *)
			((unsigned long)fabric + be16_to_cpu(fabric->length));
	}

	ipr_log_hex_data(ioa_cfg, (__be32 *)fabric, add_len);
}

/**
 * ipr_log_sis64_array_error - Log a sis64 array error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_sis64_array_error(struct ipr_ioa_cfg *ioa_cfg,
				      struct ipr_hostrcb *hostrcb)
{
	int i, num_entries;
	struct ipr_hostrcb_type_24_error *error;
	struct ipr_hostrcb64_array_data_entry *array_entry;
	char buffer[IPR_MAX_RES_PATH_LENGTH];
	const u8 zero_sn[IPR_SERIAL_NUM_LEN] = { [0 ... IPR_SERIAL_NUM_LEN-1] = '0' };

	error = &hostrcb->hcam.u.error64.u.type_24_error;

	ipr_err_separator;

	ipr_err("RAID %s Array Configuration: %s\n",
		error->protection_level,
		ipr_format_res_path(ioa_cfg, error->last_res_path,
			buffer, sizeof(buffer)));

	ipr_err_separator;

	array_entry = error->array_member;
	num_entries = min_t(u32, error->num_entries,
			    ARRAY_SIZE(error->array_member));

	for (i = 0; i < num_entries; i++, array_entry++) {

		if (!memcmp(array_entry->vpd.vpd.sn, zero_sn, IPR_SERIAL_NUM_LEN))
			continue;

		if (error->exposed_mode_adn == i)
			ipr_err("Exposed Array Member %d:\n", i);
		else
			ipr_err("Array Member %d:\n", i);

		ipr_err("Array Member %d:\n", i);
		ipr_log_ext_vpd(&array_entry->vpd);
		ipr_err("Current Location: %s\n",
			 ipr_format_res_path(ioa_cfg, array_entry->res_path,
				buffer, sizeof(buffer)));
		ipr_err("Expected Location: %s\n",
			 ipr_format_res_path(ioa_cfg,
				array_entry->expected_res_path,
				buffer, sizeof(buffer)));

		ipr_err_separator;
	}
}

/**
 * ipr_log_sis64_fabric_error - Log a sis64 fabric error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_sis64_fabric_error(struct ipr_ioa_cfg *ioa_cfg,
				       struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_30_error *error;
	struct ipr_hostrcb64_fabric_desc *fabric;
	struct ipr_hostrcb64_config_element *cfg;
	int i, add_len;

	error = &hostrcb->hcam.u.error64.u.type_30_error;

	error->failure_reason[sizeof(error->failure_reason) - 1] = '\0';
	ipr_hcam_err(hostrcb, "%s\n", error->failure_reason);

	add_len = be32_to_cpu(hostrcb->hcam.length) -
		(offsetof(struct ipr_hostrcb64_error, u) +
		 offsetof(struct ipr_hostrcb_type_30_error, desc));

	for (i = 0, fabric = error->desc; i < error->num_entries; i++) {
		ipr_log64_fabric_path(hostrcb, fabric);
		for_each_fabric_cfg(fabric, cfg)
			ipr_log64_path_elem(hostrcb, cfg);

		add_len -= be16_to_cpu(fabric->length);
		fabric = (struct ipr_hostrcb64_fabric_desc *)
			((unsigned long)fabric + be16_to_cpu(fabric->length));
	}

	ipr_log_hex_data(ioa_cfg, (__be32 *)fabric, add_len);
}

/**
 * ipr_log_sis64_service_required_error - Log a sis64 service required error.
 * @ioa_cfg:    ioa config struct
 * @hostrcb:    hostrcb struct
 *
 * Return value:
 *      none
 **/
static void ipr_log_sis64_service_required_error(struct ipr_ioa_cfg *ioa_cfg,
				       struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_41_error *error;

	error = &hostrcb->hcam.u.error64.u.type_41_error;

	error->failure_reason[sizeof(error->failure_reason) - 1] = '\0';
	ipr_err("Primary Failure Reason: %s\n", error->failure_reason);
	ipr_log_hex_data(ioa_cfg, error->data,
			 be32_to_cpu(hostrcb->hcam.length) -
			 (offsetof(struct ipr_hostrcb_error, u) +
			  offsetof(struct ipr_hostrcb_type_41_error, data)));
}
/**
 * ipr_log_generic_error - Log an adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_generic_error(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_hostrcb *hostrcb)
{
	ipr_log_hex_data(ioa_cfg, hostrcb->hcam.u.raw.data,
			 be32_to_cpu(hostrcb->hcam.length));
}

/**
 * ipr_log_sis64_device_error - Log a cache error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_sis64_device_error(struct ipr_ioa_cfg *ioa_cfg,
					 struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_21_error *error;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	error = &hostrcb->hcam.u.error64.u.type_21_error;

	ipr_err("-----Failing Device Information-----\n");
	ipr_err("World Wide Unique ID: %08X%08X%08X%08X\n",
		be32_to_cpu(error->wwn[0]), be32_to_cpu(error->wwn[1]),
		 be32_to_cpu(error->wwn[2]), be32_to_cpu(error->wwn[3]));
	ipr_err("Device Resource Path: %s\n",
		__ipr_format_res_path(error->res_path,
				      buffer, sizeof(buffer)));
	error->primary_problem_desc[sizeof(error->primary_problem_desc) - 1] = '\0';
	error->second_problem_desc[sizeof(error->second_problem_desc) - 1] = '\0';
	ipr_err("Primary Problem Description: %s\n", error->primary_problem_desc);
	ipr_err("Secondary Problem Description:  %s\n", error->second_problem_desc);
	ipr_err("SCSI Sense Data:\n");
	ipr_log_hex_data(ioa_cfg, error->sense_data, sizeof(error->sense_data));
	ipr_err("SCSI Command Descriptor Block: \n");
	ipr_log_hex_data(ioa_cfg, error->cdb, sizeof(error->cdb));

	ipr_err("Additional IOA Data:\n");
	ipr_log_hex_data(ioa_cfg, error->ioa_data, be32_to_cpu(error->length_of_error));
}

/**
 * ipr_get_error - Find the specfied IOASC in the ipr_error_table.
 * @ioasc:	IOASC
 *
 * This function will return the index of into the ipr_error_table
 * for the specified IOASC. If the IOASC is not in the table,
 * 0 will be returned, which points to the entry used for unknown errors.
 *
 * Return value:
 * 	index into the ipr_error_table
 **/
static u32 ipr_get_error(u32 ioasc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ipr_error_table); i++)
		if (ipr_error_table[i].ioasc == (ioasc & IPR_IOASC_IOASC_MASK))
			return i;

	return 0;
}

/**
 * ipr_handle_log_data - Log an adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * This function logs an adapter error to the system.
 *
 * Return value:
 * 	none
 **/
static void ipr_handle_log_data(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	u32 ioasc;
	int error_index;
	struct ipr_hostrcb_type_21_error *error;

	if (hostrcb->hcam.notify_type != IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY)
		return;

	if (hostrcb->hcam.notifications_lost == IPR_HOST_RCB_NOTIFICATIONS_LOST)
		dev_err(&ioa_cfg->pdev->dev, "Error notifications lost\n");

	if (ioa_cfg->sis64)
		ioasc = be32_to_cpu(hostrcb->hcam.u.error64.fd_ioasc);
	else
		ioasc = be32_to_cpu(hostrcb->hcam.u.error.fd_ioasc);

	if (!ioa_cfg->sis64 && (ioasc == IPR_IOASC_BUS_WAS_RESET ||
	    ioasc == IPR_IOASC_BUS_WAS_RESET_BY_OTHER)) {
		/* Tell the midlayer we had a bus reset so it will handle the UA properly */
		scsi_report_bus_reset(ioa_cfg->host,
				      hostrcb->hcam.u.error.fd_res_addr.bus);
	}

	error_index = ipr_get_error(ioasc);

	if (!ipr_error_table[error_index].log_hcam)
		return;

	if (ioasc == IPR_IOASC_HW_CMD_FAILED &&
	    hostrcb->hcam.overlay_id == IPR_HOST_RCB_OVERLAY_ID_21) {
		error = &hostrcb->hcam.u.error64.u.type_21_error;

		if (((be32_to_cpu(error->sense_data[0]) & 0x0000ff00) >> 8) == ILLEGAL_REQUEST &&
			ioa_cfg->log_level <= IPR_DEFAULT_LOG_LEVEL)
				return;
	}

	ipr_hcam_err(hostrcb, "%s\n", ipr_error_table[error_index].error);

	/* Set indication we have logged an error */
	ioa_cfg->errors_logged++;

	if (ioa_cfg->log_level < ipr_error_table[error_index].log_hcam)
		return;
	if (be32_to_cpu(hostrcb->hcam.length) > sizeof(hostrcb->hcam.u.raw))
		hostrcb->hcam.length = cpu_to_be32(sizeof(hostrcb->hcam.u.raw));

	switch (hostrcb->hcam.overlay_id) {
	case IPR_HOST_RCB_OVERLAY_ID_2:
		ipr_log_cache_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_3:
		ipr_log_config_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_4:
	case IPR_HOST_RCB_OVERLAY_ID_6:
		ipr_log_array_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_7:
		ipr_log_dual_ioa_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_12:
		ipr_log_enhanced_cache_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_13:
		ipr_log_enhanced_config_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_14:
	case IPR_HOST_RCB_OVERLAY_ID_16:
		ipr_log_enhanced_array_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_17:
		ipr_log_enhanced_dual_ioa_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_20:
		ipr_log_fabric_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_21:
		ipr_log_sis64_device_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_23:
		ipr_log_sis64_config_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_24:
	case IPR_HOST_RCB_OVERLAY_ID_26:
		ipr_log_sis64_array_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_30:
		ipr_log_sis64_fabric_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_41:
		ipr_log_sis64_service_required_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_1:
	case IPR_HOST_RCB_OVERLAY_ID_DEFAULT:
	default:
		ipr_log_generic_error(ioa_cfg, hostrcb);
		break;
	}
}

static struct ipr_hostrcb *ipr_get_free_hostrcb(struct ipr_ioa_cfg *ioa)
{
	struct ipr_hostrcb *hostrcb;

	hostrcb = list_first_entry_or_null(&ioa->hostrcb_free_q,
					struct ipr_hostrcb, queue);

	if (unlikely(!hostrcb)) {
		dev_info(&ioa->pdev->dev, "Reclaiming async error buffers.");
		hostrcb = list_first_entry_or_null(&ioa->hostrcb_report_q,
						struct ipr_hostrcb, queue);
	}

	list_del_init(&hostrcb->queue);
	return hostrcb;
}

/**
 * ipr_process_error - Op done function for an adapter error log.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for an error log host
 * controlled async from the adapter. It will log the error and
 * send the HCAM back to the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_process_error(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb = ipr_cmd->u.hostrcb;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);
	u32 fd_ioasc;

	if (ioa_cfg->sis64)
		fd_ioasc = be32_to_cpu(hostrcb->hcam.u.error64.fd_ioasc);
	else
		fd_ioasc = be32_to_cpu(hostrcb->hcam.u.error.fd_ioasc);

	list_del_init(&hostrcb->queue);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);

	if (!ioasc) {
		ipr_handle_log_data(ioa_cfg, hostrcb);
		if (fd_ioasc == IPR_IOASC_NR_IOA_RESET_REQUIRED)
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_ABBREV);
	} else if (ioasc != IPR_IOASC_IOA_WAS_RESET &&
		   ioasc != IPR_IOASC_ABORTED_CMD_TERM_BY_HOST) {
		dev_err(&ioa_cfg->pdev->dev,
			"Host RCB failed with IOASC: 0x%08X\n", ioasc);
	}

	list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_report_q);
	schedule_work(&ioa_cfg->work_q);
	hostrcb = ipr_get_free_hostrcb(ioa_cfg);

	ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_LOG_DATA, hostrcb);
}

/**
 * ipr_timeout -  An internally generated op has timed out.
 * @t: Timer context used to fetch ipr command struct
 *
 * This function blocks host requests and initiates an
 * adapter reset.
 *
 * Return value:
 * 	none
 **/
static void ipr_timeout(struct timer_list *t)
{
	struct ipr_cmnd *ipr_cmd = from_timer(ipr_cmd, t, timer);
	unsigned long lock_flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev,
		"Adapter being reset due to command timeout.\n");

	if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
		ioa_cfg->sdt_state = GET_DUMP;

	if (!ioa_cfg->in_reset_reload || ioa_cfg->reset_cmd == ipr_cmd)
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
}

/**
 * ipr_oper_timeout -  Adapter timed out transitioning to operational
 * @t: Timer context used to fetch ipr command struct
 *
 * This function blocks host requests and initiates an
 * adapter reset.
 *
 * Return value:
 * 	none
 **/
static void ipr_oper_timeout(struct timer_list *t)
{
	struct ipr_cmnd *ipr_cmd = from_timer(ipr_cmd, t, timer);
	unsigned long lock_flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev,
		"Adapter timed out transitioning to operational.\n");

	if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
		ioa_cfg->sdt_state = GET_DUMP;

	if (!ioa_cfg->in_reset_reload || ioa_cfg->reset_cmd == ipr_cmd) {
		if (ipr_fastfail)
			ioa_cfg->reset_retries += IPR_NUM_RESET_RELOAD_RETRIES;
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
}

/**
 * ipr_find_ses_entry - Find matching SES in SES table
 * @res:	resource entry struct of SES
 *
 * Return value:
 * 	pointer to SES table entry / NULL on failure
 **/
static const struct ipr_ses_table_entry *
ipr_find_ses_entry(struct ipr_resource_entry *res)
{
	int i, j, matches;
	struct ipr_std_inq_vpids *vpids;
	const struct ipr_ses_table_entry *ste = ipr_ses_table;

	for (i = 0; i < ARRAY_SIZE(ipr_ses_table); i++, ste++) {
		for (j = 0, matches = 0; j < IPR_PROD_ID_LEN; j++) {
			if (ste->compare_product_id_byte[j] == 'X') {
				vpids = &res->std_inq_data.vpids;
				if (vpids->product_id[j] == ste->product_id[j])
					matches++;
				else
					break;
			} else
				matches++;
		}

		if (matches == IPR_PROD_ID_LEN)
			return ste;
	}

	return NULL;
}

/**
 * ipr_get_max_scsi_speed - Determine max SCSI speed for a given bus
 * @ioa_cfg:	ioa config struct
 * @bus:		SCSI bus
 * @bus_width:	bus width
 *
 * Return value:
 *	SCSI bus speed in units of 100KHz, 1600 is 160 MHz
 *	For a 2-byte wide SCSI bus, the maximum transfer speed is
 *	twice the maximum transfer rate (e.g. for a wide enabled bus,
 *	max 160MHz = max 320MB/sec).
 **/
static u32 ipr_get_max_scsi_speed(struct ipr_ioa_cfg *ioa_cfg, u8 bus, u8 bus_width)
{
	struct ipr_resource_entry *res;
	const struct ipr_ses_table_entry *ste;
	u32 max_xfer_rate = IPR_MAX_SCSI_RATE(bus_width);

	/* Loop through each config table entry in the config table buffer */
	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (!(IPR_IS_SES_DEVICE(res->std_inq_data)))
			continue;

		if (bus != res->bus)
			continue;

		if (!(ste = ipr_find_ses_entry(res)))
			continue;

		max_xfer_rate = (ste->max_bus_speed_limit * 10) / (bus_width / 8);
	}

	return max_xfer_rate;
}

/**
 * ipr_wait_iodbg_ack - Wait for an IODEBUG ACK from the IOA
 * @ioa_cfg:		ioa config struct
 * @max_delay:		max delay in micro-seconds to wait
 *
 * Waits for an IODEBUG ACK from the IOA, doing busy looping.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_wait_iodbg_ack(struct ipr_ioa_cfg *ioa_cfg, int max_delay)
{
	volatile u32 pcii_reg;
	int delay = 1;

	/* Read interrupt reg until IOA signals IO Debug Acknowledge */
	while (delay < max_delay) {
		pcii_reg = readl(ioa_cfg->regs.sense_interrupt_reg);

		if (pcii_reg & IPR_PCII_IO_DEBUG_ACKNOWLEDGE)
			return 0;

		/* udelay cannot be used if delay is more than a few milliseconds */
		if ((delay / 1000) > MAX_UDELAY_MS)
			mdelay(delay / 1000);
		else
			udelay(delay);

		delay += delay;
	}
	return -EIO;
}

/**
 * ipr_get_sis64_dump_data_section - Dump IOA memory
 * @ioa_cfg:			ioa config struct
 * @start_addr:			adapter address to dump
 * @dest:			destination kernel buffer
 * @length_in_words:		length to dump in 4 byte words
 *
 * Return value:
 * 	0 on success
 **/
static int ipr_get_sis64_dump_data_section(struct ipr_ioa_cfg *ioa_cfg,
					   u32 start_addr,
					   __be32 *dest, u32 length_in_words)
{
	int i;

	for (i = 0; i < length_in_words; i++) {
		writel(start_addr+(i*4), ioa_cfg->regs.dump_addr_reg);
		*dest = cpu_to_be32(readl(ioa_cfg->regs.dump_data_reg));
		dest++;
	}

	return 0;
}

/**
 * ipr_get_ldump_data_section - Dump IOA memory
 * @ioa_cfg:			ioa config struct
 * @start_addr:			adapter address to dump
 * @dest:				destination kernel buffer
 * @length_in_words:	length to dump in 4 byte words
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_get_ldump_data_section(struct ipr_ioa_cfg *ioa_cfg,
				      u32 start_addr,
				      __be32 *dest, u32 length_in_words)
{
	volatile u32 temp_pcii_reg;
	int i, delay = 0;

	if (ioa_cfg->sis64)
		return ipr_get_sis64_dump_data_section(ioa_cfg, start_addr,
						       dest, length_in_words);

	/* Write IOA interrupt reg starting LDUMP state  */
	writel((IPR_UPROCI_RESET_ALERT | IPR_UPROCI_IO_DEBUG_ALERT),
	       ioa_cfg->regs.set_uproc_interrupt_reg32);

	/* Wait for IO debug acknowledge */
	if (ipr_wait_iodbg_ack(ioa_cfg,
			       IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC)) {
		dev_err(&ioa_cfg->pdev->dev,
			"IOA dump long data transfer timeout\n");
		return -EIO;
	}

	/* Signal LDUMP interlocked - clear IO debug ack */
	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
	       ioa_cfg->regs.clr_interrupt_reg);

	/* Write Mailbox with starting address */
	writel(start_addr, ioa_cfg->ioa_mailbox);

	/* Signal address valid - clear IOA Reset alert */
	writel(IPR_UPROCI_RESET_ALERT,
	       ioa_cfg->regs.clr_uproc_interrupt_reg32);

	for (i = 0; i < length_in_words; i++) {
		/* Wait for IO debug acknowledge */
		if (ipr_wait_iodbg_ack(ioa_cfg,
				       IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC)) {
			dev_err(&ioa_cfg->pdev->dev,
				"IOA dump short data transfer timeout\n");
			return -EIO;
		}

		/* Read data from mailbox and increment destination pointer */
		*dest = cpu_to_be32(readl(ioa_cfg->ioa_mailbox));
		dest++;

		/* For all but the last word of data, signal data received */
		if (i < (length_in_words - 1)) {
			/* Signal dump data received - Clear IO debug Ack */
			writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
			       ioa_cfg->regs.clr_interrupt_reg);
		}
	}

	/* Signal end of block transfer. Set reset alert then clear IO debug ack */
	writel(IPR_UPROCI_RESET_ALERT,
	       ioa_cfg->regs.set_uproc_interrupt_reg32);

	writel(IPR_UPROCI_IO_DEBUG_ALERT,
	       ioa_cfg->regs.clr_uproc_interrupt_reg32);

	/* Signal dump data received - Clear IO debug Ack */
	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
	       ioa_cfg->regs.clr_interrupt_reg);

	/* Wait for IOA to signal LDUMP exit - IOA reset alert will be cleared */
	while (delay < IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC) {
		temp_pcii_reg =
		    readl(ioa_cfg->regs.sense_uproc_interrupt_reg32);

		if (!(temp_pcii_reg & IPR_UPROCI_RESET_ALERT))
			return 0;

		udelay(10);
		delay += 10;
	}

	return 0;
}

#ifdef CONFIG_SCSI_IPR_DUMP
/**
 * ipr_sdt_copy - Copy Smart Dump Table to kernel buffer
 * @ioa_cfg:		ioa config struct
 * @pci_address:	adapter address
 * @length:			length of data to copy
 *
 * Copy data from PCI adapter to kernel buffer.
 * Note: length MUST be a 4 byte multiple
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_sdt_copy(struct ipr_ioa_cfg *ioa_cfg,
			unsigned long pci_address, u32 length)
{
	int bytes_copied = 0;
	int cur_len, rc, rem_len, rem_page_len, max_dump_size;
	__be32 *page;
	unsigned long lock_flags = 0;
	struct ipr_ioa_dump *ioa_dump = &ioa_cfg->dump->ioa_dump;

	if (ioa_cfg->sis64)
		max_dump_size = IPR_FMT3_MAX_IOA_DUMP_SIZE;
	else
		max_dump_size = IPR_FMT2_MAX_IOA_DUMP_SIZE;

	while (bytes_copied < length &&
	       (ioa_dump->hdr.len + bytes_copied) < max_dump_size) {
		if (ioa_dump->page_offset >= PAGE_SIZE ||
		    ioa_dump->page_offset == 0) {
			page = (__be32 *)__get_free_page(GFP_ATOMIC);

			if (!page) {
				ipr_trace;
				return bytes_copied;
			}

			ioa_dump->page_offset = 0;
			ioa_dump->ioa_data[ioa_dump->next_page_index] = page;
			ioa_dump->next_page_index++;
		} else
			page = ioa_dump->ioa_data[ioa_dump->next_page_index - 1];

		rem_len = length - bytes_copied;
		rem_page_len = PAGE_SIZE - ioa_dump->page_offset;
		cur_len = min(rem_len, rem_page_len);

		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		if (ioa_cfg->sdt_state == ABORT_DUMP) {
			rc = -EIO;
		} else {
			rc = ipr_get_ldump_data_section(ioa_cfg,
							pci_address + bytes_copied,
							&page[ioa_dump->page_offset / 4],
							(cur_len / sizeof(u32)));
		}
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

		if (!rc) {
			ioa_dump->page_offset += cur_len;
			bytes_copied += cur_len;
		} else {
			ipr_trace;
			break;
		}
		schedule();
	}

	return bytes_copied;
}

/**
 * ipr_init_dump_entry_hdr - Initialize a dump entry header.
 * @hdr:	dump entry header struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_init_dump_entry_hdr(struct ipr_dump_entry_header *hdr)
{
	hdr->eye_catcher = IPR_DUMP_EYE_CATCHER;
	hdr->num_elems = 1;
	hdr->offset = sizeof(*hdr);
	hdr->status = IPR_DUMP_STATUS_SUCCESS;
}

/**
 * ipr_dump_ioa_type_data - Fill in the adapter type in the dump.
 * @ioa_cfg:	ioa config struct
 * @driver_dump:	driver dump struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_dump_ioa_type_data(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_driver_dump *driver_dump)
{
	struct ipr_inquiry_page3 *ucode_vpd = &ioa_cfg->vpd_cbs->page3_data;

	ipr_init_dump_entry_hdr(&driver_dump->ioa_type_entry.hdr);
	driver_dump->ioa_type_entry.hdr.len =
		sizeof(struct ipr_dump_ioa_type_entry) -
		sizeof(struct ipr_dump_entry_header);
	driver_dump->ioa_type_entry.hdr.data_type = IPR_DUMP_DATA_TYPE_BINARY;
	driver_dump->ioa_type_entry.hdr.id = IPR_DUMP_DRIVER_TYPE_ID;
	driver_dump->ioa_type_entry.type = ioa_cfg->type;
	driver_dump->ioa_type_entry.fw_version = (ucode_vpd->major_release << 24) |
		(ucode_vpd->card_type << 16) | (ucode_vpd->minor_release[0] << 8) |
		ucode_vpd->minor_release[1];
	driver_dump->hdr.num_entries++;
}

/**
 * ipr_dump_version_data - Fill in the driver version in the dump.
 * @ioa_cfg:	ioa config struct
 * @driver_dump:	driver dump struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_dump_version_data(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_driver_dump *driver_dump)
{
	ipr_init_dump_entry_hdr(&driver_dump->version_entry.hdr);
	driver_dump->version_entry.hdr.len =
		sizeof(struct ipr_dump_version_entry) -
		sizeof(struct ipr_dump_entry_header);
	driver_dump->version_entry.hdr.data_type = IPR_DUMP_DATA_TYPE_ASCII;
	driver_dump->version_entry.hdr.id = IPR_DUMP_DRIVER_VERSION_ID;
	strcpy(driver_dump->version_entry.version, IPR_DRIVER_VERSION);
	driver_dump->hdr.num_entries++;
}

/**
 * ipr_dump_trace_data - Fill in the IOA trace in the dump.
 * @ioa_cfg:	ioa config struct
 * @driver_dump:	driver dump struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_dump_trace_data(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_driver_dump *driver_dump)
{
	ipr_init_dump_entry_hdr(&driver_dump->trace_entry.hdr);
	driver_dump->trace_entry.hdr.len =
		sizeof(struct ipr_dump_trace_entry) -
		sizeof(struct ipr_dump_entry_header);
	driver_dump->trace_entry.hdr.data_type = IPR_DUMP_DATA_TYPE_BINARY;
	driver_dump->trace_entry.hdr.id = IPR_DUMP_TRACE_ID;
	memcpy(driver_dump->trace_entry.trace, ioa_cfg->trace, IPR_TRACE_SIZE);
	driver_dump->hdr.num_entries++;
}

/**
 * ipr_dump_location_data - Fill in the IOA location in the dump.
 * @ioa_cfg:	ioa config struct
 * @driver_dump:	driver dump struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_dump_location_data(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_driver_dump *driver_dump)
{
	ipr_init_dump_entry_hdr(&driver_dump->location_entry.hdr);
	driver_dump->location_entry.hdr.len =
		sizeof(struct ipr_dump_location_entry) -
		sizeof(struct ipr_dump_entry_header);
	driver_dump->location_entry.hdr.data_type = IPR_DUMP_DATA_TYPE_ASCII;
	driver_dump->location_entry.hdr.id = IPR_DUMP_LOCATION_ID;
	strcpy(driver_dump->location_entry.location, dev_name(&ioa_cfg->pdev->dev));
	driver_dump->hdr.num_entries++;
}

/**
 * ipr_get_ioa_dump - Perform a dump of the driver and adapter.
 * @ioa_cfg:	ioa config struct
 * @dump:		dump struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_get_ioa_dump(struct ipr_ioa_cfg *ioa_cfg, struct ipr_dump *dump)
{
	unsigned long start_addr, sdt_word;
	unsigned long lock_flags = 0;
	struct ipr_driver_dump *driver_dump = &dump->driver_dump;
	struct ipr_ioa_dump *ioa_dump = &dump->ioa_dump;
	u32 num_entries, max_num_entries, start_off, end_off;
	u32 max_dump_size, bytes_to_copy, bytes_copied, rc;
	struct ipr_sdt *sdt;
	int valid = 1;
	int i;

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (ioa_cfg->sdt_state != READ_DUMP) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	if (ioa_cfg->sis64) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		ssleep(IPR_DUMP_DELAY_SECONDS);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	start_addr = readl(ioa_cfg->ioa_mailbox);

	if (!ioa_cfg->sis64 && !ipr_sdt_is_fmt2(start_addr)) {
		dev_err(&ioa_cfg->pdev->dev,
			"Invalid dump table format: %lx\n", start_addr);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	dev_err(&ioa_cfg->pdev->dev, "Dump of IOA initiated\n");

	driver_dump->hdr.eye_catcher = IPR_DUMP_EYE_CATCHER;

	/* Initialize the overall dump header */
	driver_dump->hdr.len = sizeof(struct ipr_driver_dump);
	driver_dump->hdr.num_entries = 1;
	driver_dump->hdr.first_entry_offset = sizeof(struct ipr_dump_header);
	driver_dump->hdr.status = IPR_DUMP_STATUS_SUCCESS;
	driver_dump->hdr.os = IPR_DUMP_OS_LINUX;
	driver_dump->hdr.driver_name = IPR_DUMP_DRIVER_NAME;

	ipr_dump_version_data(ioa_cfg, driver_dump);
	ipr_dump_location_data(ioa_cfg, driver_dump);
	ipr_dump_ioa_type_data(ioa_cfg, driver_dump);
	ipr_dump_trace_data(ioa_cfg, driver_dump);

	/* Update dump_header */
	driver_dump->hdr.len += sizeof(struct ipr_dump_entry_header);

	/* IOA Dump entry */
	ipr_init_dump_entry_hdr(&ioa_dump->hdr);
	ioa_dump->hdr.len = 0;
	ioa_dump->hdr.data_type = IPR_DUMP_DATA_TYPE_BINARY;
	ioa_dump->hdr.id = IPR_DUMP_IOA_DUMP_ID;

	/* First entries in sdt are actually a list of dump addresses and
	 lengths to gather the real dump data.  sdt represents the pointer
	 to the ioa generated dump table.  Dump data will be extracted based
	 on entries in this table */
	sdt = &ioa_dump->sdt;

	if (ioa_cfg->sis64) {
		max_num_entries = IPR_FMT3_NUM_SDT_ENTRIES;
		max_dump_size = IPR_FMT3_MAX_IOA_DUMP_SIZE;
	} else {
		max_num_entries = IPR_FMT2_NUM_SDT_ENTRIES;
		max_dump_size = IPR_FMT2_MAX_IOA_DUMP_SIZE;
	}

	bytes_to_copy = offsetof(struct ipr_sdt, entry) +
			(max_num_entries * sizeof(struct ipr_sdt_entry));
	rc = ipr_get_ldump_data_section(ioa_cfg, start_addr, (__be32 *)sdt,
					bytes_to_copy / sizeof(__be32));

	/* Smart Dump table is ready to use and the first entry is valid */
	if (rc || ((be32_to_cpu(sdt->hdr.state) != IPR_FMT3_SDT_READY_TO_USE) &&
	    (be32_to_cpu(sdt->hdr.state) != IPR_FMT2_SDT_READY_TO_USE))) {
		dev_err(&ioa_cfg->pdev->dev,
			"Dump of IOA failed. Dump table not valid: %d, %X.\n",
			rc, be32_to_cpu(sdt->hdr.state));
		driver_dump->hdr.status = IPR_DUMP_STATUS_FAILED;
		ioa_cfg->sdt_state = DUMP_OBTAINED;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	num_entries = be32_to_cpu(sdt->hdr.num_entries_used);

	if (num_entries > max_num_entries)
		num_entries = max_num_entries;

	/* Update dump length to the actual data to be copied */
	dump->driver_dump.hdr.len += sizeof(struct ipr_sdt_header);
	if (ioa_cfg->sis64)
		dump->driver_dump.hdr.len += num_entries * sizeof(struct ipr_sdt_entry);
	else
		dump->driver_dump.hdr.len += max_num_entries * sizeof(struct ipr_sdt_entry);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	for (i = 0; i < num_entries; i++) {
		if (ioa_dump->hdr.len > max_dump_size) {
			driver_dump->hdr.status = IPR_DUMP_STATUS_QUAL_SUCCESS;
			break;
		}

		if (sdt->entry[i].flags & IPR_SDT_VALID_ENTRY) {
			sdt_word = be32_to_cpu(sdt->entry[i].start_token);
			if (ioa_cfg->sis64)
				bytes_to_copy = be32_to_cpu(sdt->entry[i].end_token);
			else {
				start_off = sdt_word & IPR_FMT2_MBX_ADDR_MASK;
				end_off = be32_to_cpu(sdt->entry[i].end_token);

				if (ipr_sdt_is_fmt2(sdt_word) && sdt_word)
					bytes_to_copy = end_off - start_off;
				else
					valid = 0;
			}
			if (valid) {
				if (bytes_to_copy > max_dump_size) {
					sdt->entry[i].flags &= ~IPR_SDT_VALID_ENTRY;
					continue;
				}

				/* Copy data from adapter to driver buffers */
				bytes_copied = ipr_sdt_copy(ioa_cfg, sdt_word,
							    bytes_to_copy);

				ioa_dump->hdr.len += bytes_copied;

				if (bytes_copied != bytes_to_copy) {
					driver_dump->hdr.status = IPR_DUMP_STATUS_QUAL_SUCCESS;
					break;
				}
			}
		}
	}

	dev_err(&ioa_cfg->pdev->dev, "Dump of IOA completed.\n");

	/* Update dump_header */
	driver_dump->hdr.len += ioa_dump->hdr.len;
	wmb();
	ioa_cfg->sdt_state = DUMP_OBTAINED;
	LEAVE;
}

#else
#define ipr_get_ioa_dump(ioa_cfg, dump) do { } while (0)
#endif

/**
 * ipr_release_dump - Free adapter dump memory
 * @kref:	kref struct
 *
 * Return value:
 *	nothing
 **/
static void ipr_release_dump(struct kref *kref)
{
	struct ipr_dump *dump = container_of(kref, struct ipr_dump, kref);
	struct ipr_ioa_cfg *ioa_cfg = dump->ioa_cfg;
	unsigned long lock_flags = 0;
	int i;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ioa_cfg->dump = NULL;
	ioa_cfg->sdt_state = INACTIVE;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	for (i = 0; i < dump->ioa_dump.next_page_index; i++)
		free_page((unsigned long) dump->ioa_dump.ioa_data[i]);

	vfree(dump->ioa_dump.ioa_data);
	kfree(dump);
	LEAVE;
}

static void ipr_add_remove_thread(struct work_struct *work)
{
	unsigned long lock_flags;
	struct ipr_resource_entry *res;
	struct scsi_device *sdev;
	struct ipr_ioa_cfg *ioa_cfg =
		container_of(work, struct ipr_ioa_cfg, scsi_add_work_q);
	u8 bus, target, lun;
	int did_work;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

restart:
	do {
		did_work = 0;
		if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].allow_cmds) {
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			return;
		}

		list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
			if (res->del_from_ml && res->sdev) {
				did_work = 1;
				sdev = res->sdev;
				if (!scsi_device_get(sdev)) {
					if (!res->add_to_ml)
						list_move_tail(&res->queue, &ioa_cfg->free_res_q);
					else
						res->del_from_ml = 0;
					spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
					scsi_remove_device(sdev);
					scsi_device_put(sdev);
					spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
				}
				break;
			}
		}
	} while (did_work);

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (res->add_to_ml) {
			bus = res->bus;
			target = res->target;
			lun = res->lun;
			res->add_to_ml = 0;
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			scsi_add_device(ioa_cfg->host, bus, target, lun);
			spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
			goto restart;
		}
	}

	ioa_cfg->scan_done = 1;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	kobject_uevent(&ioa_cfg->host->shost_dev.kobj, KOBJ_CHANGE);
	LEAVE;
}

/**
 * ipr_worker_thread - Worker thread
 * @work:		ioa config struct
 *
 * Called at task level from a work thread. This function takes care
 * of adding and removing device from the mid-layer as configuration
 * changes are detected by the adapter.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_worker_thread(struct work_struct *work)
{
	unsigned long lock_flags;
	struct ipr_dump *dump;
	struct ipr_ioa_cfg *ioa_cfg =
		container_of(work, struct ipr_ioa_cfg, work_q);

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (ioa_cfg->sdt_state == READ_DUMP) {
		dump = ioa_cfg->dump;
		if (!dump) {
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			return;
		}
		kref_get(&dump->kref);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		ipr_get_ioa_dump(ioa_cfg, dump);
		kref_put(&dump->kref, ipr_release_dump);

		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		if (ioa_cfg->sdt_state == DUMP_OBTAINED && !ioa_cfg->dump_timeout)
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	if (ioa_cfg->scsi_unblock) {
		ioa_cfg->scsi_unblock = 0;
		ioa_cfg->scsi_blocked = 0;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		scsi_unblock_requests(ioa_cfg->host);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		if (ioa_cfg->scsi_blocked)
			scsi_block_requests(ioa_cfg->host);
	}

	if (!ioa_cfg->scan_enabled) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	schedule_work(&ioa_cfg->scsi_add_work_q);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
}

#ifdef CONFIG_SCSI_IPR_TRACE
/**
 * ipr_read_trace - Dump the adapter trace
 * @filp:		open sysfs file
 * @kobj:		kobject struct
 * @bin_attr:		bin_attribute struct
 * @buf:		buffer
 * @off:		offset
 * @count:		buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_read_trace(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	ssize_t ret;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ret = memory_read_from_buffer(buf, count, &off, ioa_cfg->trace,
				IPR_TRACE_SIZE);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return ret;
}

static struct bin_attribute ipr_trace_attr = {
	.attr =	{
		.name = "trace",
		.mode = S_IRUGO,
	},
	.size = 0,
	.read = ipr_read_trace,
};
#endif

/**
 * ipr_show_fw_version - Show the firmware version
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_fw_version(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_inquiry_page3 *ucode_vpd = &ioa_cfg->vpd_cbs->page3_data;
	unsigned long lock_flags = 0;
	int len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	len = snprintf(buf, PAGE_SIZE, "%02X%02X%02X%02X\n",
		       ucode_vpd->major_release, ucode_vpd->card_type,
		       ucode_vpd->minor_release[0],
		       ucode_vpd->minor_release[1]);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_fw_version_attr = {
	.attr = {
		.name =		"fw_version",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_fw_version,
};

/**
 * ipr_show_log_level - Show the adapter's error logging level
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_log_level(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	int len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	len = snprintf(buf, PAGE_SIZE, "%d\n", ioa_cfg->log_level);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

/**
 * ipr_store_log_level - Change the adapter's error logging level
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_store_log_level(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ioa_cfg->log_level = simple_strtoul(buf, NULL, 10);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return strlen(buf);
}

static struct device_attribute ipr_log_level_attr = {
	.attr = {
		.name =		"log_level",
		.mode =		S_IRUGO | S_IWUSR,
	},
	.show = ipr_show_log_level,
	.store = ipr_store_log_level
};

/**
 * ipr_store_diagnostics - IOA Diagnostics interface
 * @dev:	device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * This function will reset the adapter and wait a reasonable
 * amount of time for any errors that the adapter might log.
 *
 * Return value:
 * 	count on success / other on failure
 **/
static ssize_t ipr_store_diagnostics(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	int rc = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	ioa_cfg->errors_logged = 0;
	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NORMAL);

	if (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);

		/* Wait for a second for any errors to be logged */
		msleep(1000);
	} else {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return -EIO;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ioa_cfg->in_reset_reload || ioa_cfg->errors_logged)
		rc = -EIO;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return rc;
}

static struct device_attribute ipr_diagnostics_attr = {
	.attr = {
		.name =		"run_diagnostics",
		.mode =		S_IWUSR,
	},
	.store = ipr_store_diagnostics
};

/**
 * ipr_show_adapter_state - Show the adapter's state
 * @dev:	device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_adapter_state(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	int len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead)
		len = snprintf(buf, PAGE_SIZE, "offline\n");
	else
		len = snprintf(buf, PAGE_SIZE, "online\n");
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

/**
 * ipr_store_adapter_state - Change adapter state
 * @dev:	device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * This function will change the adapter's state.
 *
 * Return value:
 * 	count on success / other on failure
 **/
static ssize_t ipr_store_adapter_state(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags;
	int result = count, i;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead &&
	    !strncmp(buf, "online", 6)) {
		for (i = 0; i < ioa_cfg->hrrq_num; i++) {
			spin_lock(&ioa_cfg->hrrq[i]._lock);
			ioa_cfg->hrrq[i].ioa_is_dead = 0;
			spin_unlock(&ioa_cfg->hrrq[i]._lock);
		}
		wmb();
		ioa_cfg->reset_retries = 0;
		ioa_cfg->in_ioa_bringdown = 0;
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);

	return result;
}

static struct device_attribute ipr_ioa_state_attr = {
	.attr = {
		.name =		"online_state",
		.mode =		S_IRUGO | S_IWUSR,
	},
	.show = ipr_show_adapter_state,
	.store = ipr_store_adapter_state
};

/**
 * ipr_store_reset_adapter - Reset the adapter
 * @dev:	device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * This function will reset the adapter.
 *
 * Return value:
 * 	count on success / other on failure
 **/
static ssize_t ipr_store_reset_adapter(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags;
	int result = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (!ioa_cfg->in_reset_reload)
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NORMAL);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);

	return result;
}

static struct device_attribute ipr_ioa_reset_attr = {
	.attr = {
		.name =		"reset_host",
		.mode =		S_IWUSR,
	},
	.store = ipr_store_reset_adapter
};

static int ipr_iopoll(struct irq_poll *iop, int budget);
 /**
 * ipr_show_iopoll_weight - Show ipr polling mode
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_iopoll_weight(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	int len;

	spin_lock_irqsave(shost->host_lock, lock_flags);
	len = snprintf(buf, PAGE_SIZE, "%d\n", ioa_cfg->iopoll_weight);
	spin_unlock_irqrestore(shost->host_lock, lock_flags);

	return len;
}

/**
 * ipr_store_iopoll_weight - Change the adapter's polling mode
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_store_iopoll_weight(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long user_iopoll_weight;
	unsigned long lock_flags = 0;
	int i;

	if (!ioa_cfg->sis64) {
		dev_info(&ioa_cfg->pdev->dev, "irq_poll not supported on this adapter\n");
		return -EINVAL;
	}
	if (kstrtoul(buf, 10, &user_iopoll_weight))
		return -EINVAL;

	if (user_iopoll_weight > 256) {
		dev_info(&ioa_cfg->pdev->dev, "Invalid irq_poll weight. It must be less than 256\n");
		return -EINVAL;
	}

	if (user_iopoll_weight == ioa_cfg->iopoll_weight) {
		dev_info(&ioa_cfg->pdev->dev, "Current irq_poll weight has the same weight\n");
		return strlen(buf);
	}

	if (ioa_cfg->iopoll_weight && ioa_cfg->sis64 && ioa_cfg->nvectors > 1) {
		for (i = 1; i < ioa_cfg->hrrq_num; i++)
			irq_poll_disable(&ioa_cfg->hrrq[i].iopoll);
	}

	spin_lock_irqsave(shost->host_lock, lock_flags);
	ioa_cfg->iopoll_weight = user_iopoll_weight;
	if (ioa_cfg->iopoll_weight && ioa_cfg->sis64 && ioa_cfg->nvectors > 1) {
		for (i = 1; i < ioa_cfg->hrrq_num; i++) {
			irq_poll_init(&ioa_cfg->hrrq[i].iopoll,
					ioa_cfg->iopoll_weight, ipr_iopoll);
		}
	}
	spin_unlock_irqrestore(shost->host_lock, lock_flags);

	return strlen(buf);
}

static struct device_attribute ipr_iopoll_weight_attr = {
	.attr = {
		.name =		"iopoll_weight",
		.mode =		S_IRUGO | S_IWUSR,
	},
	.show = ipr_show_iopoll_weight,
	.store = ipr_store_iopoll_weight
};

/**
 * ipr_alloc_ucode_buffer - Allocates a microcode download buffer
 * @buf_len:		buffer length
 *
 * Allocates a DMA'able buffer in chunks and assembles a scatter/gather
 * list to use for microcode download
 *
 * Return value:
 * 	pointer to sglist / NULL on failure
 **/
static struct ipr_sglist *ipr_alloc_ucode_buffer(int buf_len)
{
	int sg_size, order;
	struct ipr_sglist *sglist;

	/* Get the minimum size per scatter/gather element */
	sg_size = buf_len / (IPR_MAX_SGLIST - 1);

	/* Get the actual size per element */
	order = get_order(sg_size);

	/* Allocate a scatter/gather list for the DMA */
	sglist = kzalloc(sizeof(struct ipr_sglist), GFP_KERNEL);
	if (sglist == NULL) {
		ipr_trace;
		return NULL;
	}
	sglist->order = order;
	sglist->scatterlist = sgl_alloc_order(buf_len, order, false, GFP_KERNEL,
					      &sglist->num_sg);
	if (!sglist->scatterlist) {
		kfree(sglist);
		return NULL;
	}

	return sglist;
}

/**
 * ipr_free_ucode_buffer - Frees a microcode download buffer
 * @sglist:		scatter/gather list pointer
 *
 * Free a DMA'able ucode download buffer previously allocated with
 * ipr_alloc_ucode_buffer
 *
 * Return value:
 * 	nothing
 **/
static void ipr_free_ucode_buffer(struct ipr_sglist *sglist)
{
	sgl_free_order(sglist->scatterlist, sglist->order);
	kfree(sglist);
}

/**
 * ipr_copy_ucode_buffer - Copy user buffer to kernel buffer
 * @sglist:		scatter/gather list pointer
 * @buffer:		buffer pointer
 * @len:		buffer length
 *
 * Copy a microcode image from a user buffer into a buffer allocated by
 * ipr_alloc_ucode_buffer
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_copy_ucode_buffer(struct ipr_sglist *sglist,
				 u8 *buffer, u32 len)
{
	int bsize_elem, i, result = 0;
	struct scatterlist *sg;
	void *kaddr;

	/* Determine the actual number of bytes per element */
	bsize_elem = PAGE_SIZE * (1 << sglist->order);

	sg = sglist->scatterlist;

	for (i = 0; i < (len / bsize_elem); i++, sg = sg_next(sg),
			buffer += bsize_elem) {
		struct page *page = sg_page(sg);

		kaddr = kmap(page);
		memcpy(kaddr, buffer, bsize_elem);
		kunmap(page);

		sg->length = bsize_elem;

		if (result != 0) {
			ipr_trace;
			return result;
		}
	}

	if (len % bsize_elem) {
		struct page *page = sg_page(sg);

		kaddr = kmap(page);
		memcpy(kaddr, buffer, len % bsize_elem);
		kunmap(page);

		sg->length = len % bsize_elem;
	}

	sglist->buffer_len = len;
	return result;
}

/**
 * ipr_build_ucode_ioadl64 - Build a microcode download IOADL
 * @ipr_cmd:		ipr command struct
 * @sglist:		scatter/gather list
 *
 * Builds a microcode download IOA data list (IOADL).
 *
 **/
static void ipr_build_ucode_ioadl64(struct ipr_cmnd *ipr_cmd,
				    struct ipr_sglist *sglist)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl64_desc *ioadl64 = ipr_cmd->i.ioadl64;
	struct scatterlist *scatterlist = sglist->scatterlist;
	struct scatterlist *sg;
	int i;

	ipr_cmd->dma_use_sg = sglist->num_dma_sg;
	ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
	ioarcb->data_transfer_length = cpu_to_be32(sglist->buffer_len);

	ioarcb->ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl64_desc) * ipr_cmd->dma_use_sg);
	for_each_sg(scatterlist, sg, ipr_cmd->dma_use_sg, i) {
		ioadl64[i].flags = cpu_to_be32(IPR_IOADL_FLAGS_WRITE);
		ioadl64[i].data_len = cpu_to_be32(sg_dma_len(sg));
		ioadl64[i].address = cpu_to_be64(sg_dma_address(sg));
	}

	ioadl64[i-1].flags |= cpu_to_be32(IPR_IOADL_FLAGS_LAST);
}

/**
 * ipr_build_ucode_ioadl - Build a microcode download IOADL
 * @ipr_cmd:	ipr command struct
 * @sglist:		scatter/gather list
 *
 * Builds a microcode download IOA data list (IOADL).
 *
 **/
static void ipr_build_ucode_ioadl(struct ipr_cmnd *ipr_cmd,
				  struct ipr_sglist *sglist)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->i.ioadl;
	struct scatterlist *scatterlist = sglist->scatterlist;
	struct scatterlist *sg;
	int i;

	ipr_cmd->dma_use_sg = sglist->num_dma_sg;
	ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
	ioarcb->data_transfer_length = cpu_to_be32(sglist->buffer_len);

	ioarcb->ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);

	for_each_sg(scatterlist, sg, ipr_cmd->dma_use_sg, i) {
		ioadl[i].flags_and_data_len =
			cpu_to_be32(IPR_IOADL_FLAGS_WRITE | sg_dma_len(sg));
		ioadl[i].address =
			cpu_to_be32(sg_dma_address(sg));
	}

	ioadl[i-1].flags_and_data_len |=
		cpu_to_be32(IPR_IOADL_FLAGS_LAST);
}

/**
 * ipr_update_ioa_ucode - Update IOA's microcode
 * @ioa_cfg:	ioa config struct
 * @sglist:		scatter/gather list
 *
 * Initiate an adapter reset to update the IOA's microcode
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_update_ioa_ucode(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_sglist *sglist)
{
	unsigned long lock_flags;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	if (ioa_cfg->ucode_sglist) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		dev_err(&ioa_cfg->pdev->dev,
			"Microcode download already in progress\n");
		return -EIO;
	}

	sglist->num_dma_sg = dma_map_sg(&ioa_cfg->pdev->dev,
					sglist->scatterlist, sglist->num_sg,
					DMA_TO_DEVICE);

	if (!sglist->num_dma_sg) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		dev_err(&ioa_cfg->pdev->dev,
			"Failed to map microcode download buffer!\n");
		return -EIO;
	}

	ioa_cfg->ucode_sglist = sglist;
	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NORMAL);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ioa_cfg->ucode_sglist = NULL;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return 0;
}

/**
 * ipr_store_update_fw - Update the firmware on the adapter
 * @dev:	device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:	buffer size
 *
 * This function will update the firmware on the adapter.
 *
 * Return value:
 * 	count on success / other on failure
 **/
static ssize_t ipr_store_update_fw(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_ucode_image_header *image_hdr;
	const struct firmware *fw_entry;
	struct ipr_sglist *sglist;
	char fname[100];
	char *src;
	char *endline;
	int result, dnld_size;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	snprintf(fname, sizeof(fname), "%s", buf);

	endline = strchr(fname, '\n');
	if (endline)
		*endline = '\0';

	if (request_firmware(&fw_entry, fname, &ioa_cfg->pdev->dev)) {
		dev_err(&ioa_cfg->pdev->dev, "Firmware file %s not found\n", fname);
		return -EIO;
	}

	image_hdr = (struct ipr_ucode_image_header *)fw_entry->data;

	src = (u8 *)image_hdr + be32_to_cpu(image_hdr->header_length);
	dnld_size = fw_entry->size - be32_to_cpu(image_hdr->header_length);
	sglist = ipr_alloc_ucode_buffer(dnld_size);

	if (!sglist) {
		dev_err(&ioa_cfg->pdev->dev, "Microcode buffer allocation failed\n");
		release_firmware(fw_entry);
		return -ENOMEM;
	}

	result = ipr_copy_ucode_buffer(sglist, src, dnld_size);

	if (result) {
		dev_err(&ioa_cfg->pdev->dev,
			"Microcode buffer copy to DMA buffer failed\n");
		goto out;
	}

	ipr_info("Updating microcode, please be patient.  This may take up to 30 minutes.\n");

	result = ipr_update_ioa_ucode(ioa_cfg, sglist);

	if (!result)
		result = count;
out:
	ipr_free_ucode_buffer(sglist);
	release_firmware(fw_entry);
	return result;
}

static struct device_attribute ipr_update_fw_attr = {
	.attr = {
		.name =		"update_fw",
		.mode =		S_IWUSR,
	},
	.store = ipr_store_update_fw
};

/**
 * ipr_show_fw_type - Show the adapter's firmware type.
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_fw_type(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	unsigned long lock_flags = 0;
	int len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	len = snprintf(buf, PAGE_SIZE, "%d\n", ioa_cfg->sis64);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_ioa_fw_type_attr = {
	.attr = {
		.name =		"fw_type",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_fw_type
};

static ssize_t ipr_read_async_err_log(struct file *filep, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_hostrcb *hostrcb;
	unsigned long lock_flags = 0;
	int ret;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	hostrcb = list_first_entry_or_null(&ioa_cfg->hostrcb_report_q,
					struct ipr_hostrcb, queue);
	if (!hostrcb) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return 0;
	}
	ret = memory_read_from_buffer(buf, count, &off, &hostrcb->hcam,
				sizeof(hostrcb->hcam));
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return ret;
}

static ssize_t ipr_next_async_err_log(struct file *filep, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_hostrcb *hostrcb;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	hostrcb = list_first_entry_or_null(&ioa_cfg->hostrcb_report_q,
					struct ipr_hostrcb, queue);
	if (!hostrcb) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return count;
	}

	/* Reclaim hostrcb before exit */
	list_move_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return count;
}

static struct bin_attribute ipr_ioa_async_err_log = {
	.attr = {
		.name =		"async_err_log",
		.mode =		S_IRUGO | S_IWUSR,
	},
	.size = 0,
	.read = ipr_read_async_err_log,
	.write = ipr_next_async_err_log
};

static struct device_attribute *ipr_ioa_attrs[] = {
	&ipr_fw_version_attr,
	&ipr_log_level_attr,
	&ipr_diagnostics_attr,
	&ipr_ioa_state_attr,
	&ipr_ioa_reset_attr,
	&ipr_update_fw_attr,
	&ipr_ioa_fw_type_attr,
	&ipr_iopoll_weight_attr,
	NULL,
};

#ifdef CONFIG_SCSI_IPR_DUMP
/**
 * ipr_read_dump - Dump the adapter
 * @filp:		open sysfs file
 * @kobj:		kobject struct
 * @bin_attr:		bin_attribute struct
 * @buf:		buffer
 * @off:		offset
 * @count:		buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_read_dump(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr,
			     char *buf, loff_t off, size_t count)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_dump *dump;
	unsigned long lock_flags = 0;
	char *src;
	int len, sdt_end;
	size_t rc = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	dump = ioa_cfg->dump;

	if (ioa_cfg->sdt_state != DUMP_OBTAINED || !dump) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return 0;
	}
	kref_get(&dump->kref);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	if (off > dump->driver_dump.hdr.len) {
		kref_put(&dump->kref, ipr_release_dump);
		return 0;
	}

	if (off + count > dump->driver_dump.hdr.len) {
		count = dump->driver_dump.hdr.len - off;
		rc = count;
	}

	if (count && off < sizeof(dump->driver_dump)) {
		if (off + count > sizeof(dump->driver_dump))
			len = sizeof(dump->driver_dump) - off;
		else
			len = count;
		src = (u8 *)&dump->driver_dump + off;
		memcpy(buf, src, len);
		buf += len;
		off += len;
		count -= len;
	}

	off -= sizeof(dump->driver_dump);

	if (ioa_cfg->sis64)
		sdt_end = offsetof(struct ipr_ioa_dump, sdt.entry) +
			  (be32_to_cpu(dump->ioa_dump.sdt.hdr.num_entries_used) *
			   sizeof(struct ipr_sdt_entry));
	else
		sdt_end = offsetof(struct ipr_ioa_dump, sdt.entry) +
			  (IPR_FMT2_NUM_SDT_ENTRIES * sizeof(struct ipr_sdt_entry));

	if (count && off < sdt_end) {
		if (off + count > sdt_end)
			len = sdt_end - off;
		else
			len = count;
		src = (u8 *)&dump->ioa_dump + off;
		memcpy(buf, src, len);
		buf += len;
		off += len;
		count -= len;
	}

	off -= sdt_end;

	while (count) {
		if ((off & PAGE_MASK) != ((off + count) & PAGE_MASK))
			len = PAGE_ALIGN(off) - off;
		else
			len = count;
		src = (u8 *)dump->ioa_dump.ioa_data[(off & PAGE_MASK) >> PAGE_SHIFT];
		src += off & ~PAGE_MASK;
		memcpy(buf, src, len);
		buf += len;
		off += len;
		count -= len;
	}

	kref_put(&dump->kref, ipr_release_dump);
	return rc;
}

/**
 * ipr_alloc_dump - Prepare for adapter dump
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ipr_alloc_dump(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_dump *dump;
	__be32 **ioa_data;
	unsigned long lock_flags = 0;

	dump = kzalloc(sizeof(struct ipr_dump), GFP_KERNEL);

	if (!dump) {
		ipr_err("Dump memory allocation failed\n");
		return -ENOMEM;
	}

	if (ioa_cfg->sis64)
		ioa_data = vmalloc(array_size(IPR_FMT3_MAX_NUM_DUMP_PAGES,
					      sizeof(__be32 *)));
	else
		ioa_data = vmalloc(array_size(IPR_FMT2_MAX_NUM_DUMP_PAGES,
					      sizeof(__be32 *)));

	if (!ioa_data) {
		ipr_err("Dump memory allocation failed\n");
		kfree(dump);
		return -ENOMEM;
	}

	dump->ioa_dump.ioa_data = ioa_data;

	kref_init(&dump->kref);
	dump->ioa_cfg = ioa_cfg;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (INACTIVE != ioa_cfg->sdt_state) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		vfree(dump->ioa_dump.ioa_data);
		kfree(dump);
		return 0;
	}

	ioa_cfg->dump = dump;
	ioa_cfg->sdt_state = WAIT_FOR_DUMP;
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead && !ioa_cfg->dump_taken) {
		ioa_cfg->dump_taken = 1;
		schedule_work(&ioa_cfg->work_q);
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_free_dump - Free adapter dump memory
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ipr_free_dump(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_dump *dump;
	unsigned long lock_flags = 0;

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	dump = ioa_cfg->dump;
	if (!dump) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return 0;
	}

	ioa_cfg->dump = NULL;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	kref_put(&dump->kref, ipr_release_dump);

	LEAVE;
	return 0;
}

/**
 * ipr_write_dump - Setup dump state of adapter
 * @filp:		open sysfs file
 * @kobj:		kobject struct
 * @bin_attr:		bin_attribute struct
 * @buf:		buffer
 * @off:		offset
 * @count:		buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_write_dump(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      char *buf, loff_t off, size_t count)
{
	struct device *cdev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (buf[0] == '1')
		rc = ipr_alloc_dump(ioa_cfg);
	else if (buf[0] == '0')
		rc = ipr_free_dump(ioa_cfg);
	else
		return -EINVAL;

	if (rc)
		return rc;
	else
		return count;
}

static struct bin_attribute ipr_dump_attr = {
	.attr =	{
		.name = "dump",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 0,
	.read = ipr_read_dump,
	.write = ipr_write_dump
};
#else
static int ipr_free_dump(struct ipr_ioa_cfg *ioa_cfg) { return 0; };
#endif

/**
 * ipr_change_queue_depth - Change the device's queue depth
 * @sdev:	scsi device struct
 * @qdepth:	depth to set
 *
 * Return value:
 * 	actual depth set
 **/
static int ipr_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;

	if (res && ipr_is_gata(res) && qdepth > IPR_MAX_CMD_PER_ATA_LUN)
		qdepth = IPR_MAX_CMD_PER_ATA_LUN;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	scsi_change_queue_depth(sdev, qdepth);
	return sdev->queue_depth;
}

/**
 * ipr_show_adapter_handle - Show the adapter's resource handle for this device
 * @dev:	device struct
 * @attr:	device attribute structure
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_adapter_handle(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len = -ENXIO;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res)
		len = snprintf(buf, PAGE_SIZE, "%08X\n", res->res_handle);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_adapter_handle_attr = {
	.attr = {
		.name = 	"adapter_handle",
		.mode =		S_IRUSR,
	},
	.show = ipr_show_adapter_handle
};

/**
 * ipr_show_resource_path - Show the resource path or the resource address for
 *			    this device.
 * @dev:	device struct
 * @attr:	device attribute structure
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_resource_path(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len = -ENXIO;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res && ioa_cfg->sis64)
		len = snprintf(buf, PAGE_SIZE, "%s\n",
			       __ipr_format_res_path(res->res_path, buffer,
						     sizeof(buffer)));
	else if (res)
		len = snprintf(buf, PAGE_SIZE, "%d:%d:%d:%d\n", ioa_cfg->host->host_no,
			       res->bus, res->target, res->lun);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_resource_path_attr = {
	.attr = {
		.name = 	"resource_path",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_resource_path
};

/**
 * ipr_show_device_id - Show the device_id for this device.
 * @dev:	device struct
 * @attr:	device attribute structure
 * @buf:	buffer
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_device_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len = -ENXIO;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res && ioa_cfg->sis64)
		len = snprintf(buf, PAGE_SIZE, "0x%llx\n", be64_to_cpu(res->dev_id));
	else if (res)
		len = snprintf(buf, PAGE_SIZE, "0x%llx\n", res->lun_wwn);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_device_id_attr = {
	.attr = {
		.name =		"device_id",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_device_id
};

/**
 * ipr_show_resource_type - Show the resource type for this device.
 * @dev:	device struct
 * @attr:	device attribute structure
 * @buf:	buffer
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_resource_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len = -ENXIO;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;

	if (res)
		len = snprintf(buf, PAGE_SIZE, "%x\n", res->type);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_resource_type_attr = {
	.attr = {
		.name =		"resource_type",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_resource_type
};

/**
 * ipr_show_raw_mode - Show the adapter's raw mode
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_raw_mode(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res)
		len = snprintf(buf, PAGE_SIZE, "%d\n", res->raw_mode);
	else
		len = -ENXIO;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

/**
 * ipr_store_raw_mode - Change the adapter's raw mode
 * @dev:	class device struct
 * @attr:	device attribute (unused)
 * @buf:	buffer
 * @count:		buffer size
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_store_raw_mode(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	ssize_t len;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res) {
		if (ipr_is_af_dasd_device(res)) {
			res->raw_mode = simple_strtoul(buf, NULL, 10);
			len = strlen(buf);
			if (res->sdev)
				sdev_printk(KERN_INFO, res->sdev, "raw mode is %s\n",
					res->raw_mode ? "enabled" : "disabled");
		} else
			len = -EINVAL;
	} else
		len = -ENXIO;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return len;
}

static struct device_attribute ipr_raw_mode_attr = {
	.attr = {
		.name =		"raw_mode",
		.mode =		S_IRUGO | S_IWUSR,
	},
	.show = ipr_show_raw_mode,
	.store = ipr_store_raw_mode
};

static struct device_attribute *ipr_dev_attrs[] = {
	&ipr_adapter_handle_attr,
	&ipr_resource_path_attr,
	&ipr_device_id_attr,
	&ipr_resource_type_attr,
	&ipr_raw_mode_attr,
	NULL,
};

/**
 * ipr_biosparam - Return the HSC mapping
 * @sdev:			scsi device struct
 * @block_device:	block device pointer
 * @capacity:		capacity of the device
 * @parm:			Array containing returned HSC values.
 *
 * This function generates the HSC parms that fdisk uses.
 * We want to make sure we return something that places partitions
 * on 4k boundaries for best performance with the IOA.
 *
 * Return value:
 * 	0 on success
 **/
static int ipr_biosparam(struct scsi_device *sdev,
			 struct block_device *block_device,
			 sector_t capacity, int *parm)
{
	int heads, sectors;
	sector_t cylinders;

	heads = 128;
	sectors = 32;

	cylinders = capacity;
	sector_div(cylinders, (128 * 32));

	/* return result */
	parm[0] = heads;
	parm[1] = sectors;
	parm[2] = cylinders;

	return 0;
}

/**
 * ipr_find_starget - Find target based on bus/target.
 * @starget:	scsi target struct
 *
 * Return value:
 * 	resource entry pointer if found / NULL if not found
 **/
static struct ipr_resource_entry *ipr_find_starget(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) shost->hostdata;
	struct ipr_resource_entry *res;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if ((res->bus == starget->channel) &&
		    (res->target == starget->id)) {
			return res;
		}
	}

	return NULL;
}

static struct ata_port_info sata_port_info;

/**
 * ipr_target_alloc - Prepare for commands to a SCSI target
 * @starget:	scsi target struct
 *
 * If the device is a SATA device, this function allocates an
 * ATA port with libata, else it does nothing.
 *
 * Return value:
 * 	0 on success / non-0 on failure
 **/
static int ipr_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) shost->hostdata;
	struct ipr_sata_port *sata_port;
	struct ata_port *ap;
	struct ipr_resource_entry *res;
	unsigned long lock_flags;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = ipr_find_starget(starget);
	starget->hostdata = NULL;

	if (res && ipr_is_gata(res)) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		sata_port = kzalloc(sizeof(*sata_port), GFP_KERNEL);
		if (!sata_port)
			return -ENOMEM;

		ap = ata_sas_port_alloc(&ioa_cfg->ata_host, &sata_port_info, shost);
		if (ap) {
			spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
			sata_port->ioa_cfg = ioa_cfg;
			sata_port->ap = ap;
			sata_port->res = res;

			res->sata_port = sata_port;
			ap->private_data = sata_port;
			starget->hostdata = sata_port;
		} else {
			kfree(sata_port);
			return -ENOMEM;
		}
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_target_destroy - Destroy a SCSI target
 * @starget:	scsi target struct
 *
 * If the device was a SATA device, this function frees the libata
 * ATA port, else it does nothing.
 *
 **/
static void ipr_target_destroy(struct scsi_target *starget)
{
	struct ipr_sata_port *sata_port = starget->hostdata;
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) shost->hostdata;

	if (ioa_cfg->sis64) {
		if (!ipr_find_starget(starget)) {
			if (starget->channel == IPR_ARRAY_VIRTUAL_BUS)
				clear_bit(starget->id, ioa_cfg->array_ids);
			else if (starget->channel == IPR_VSET_VIRTUAL_BUS)
				clear_bit(starget->id, ioa_cfg->vset_ids);
			else if (starget->channel == 0)
				clear_bit(starget->id, ioa_cfg->target_ids);
		}
	}

	if (sata_port) {
		starget->hostdata = NULL;
		ata_sas_port_destroy(sata_port->ap);
		kfree(sata_port);
	}
}

/**
 * ipr_find_sdev - Find device based on bus/target/lun.
 * @sdev:	scsi device struct
 *
 * Return value:
 * 	resource entry pointer if found / NULL if not found
 **/
static struct ipr_resource_entry *ipr_find_sdev(struct scsi_device *sdev)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) sdev->host->hostdata;
	struct ipr_resource_entry *res;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if ((res->bus == sdev->channel) &&
		    (res->target == sdev->id) &&
		    (res->lun == sdev->lun))
			return res;
	}

	return NULL;
}

/**
 * ipr_slave_destroy - Unconfigure a SCSI device
 * @sdev:	scsi device struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_slave_destroy(struct scsi_device *sdev)
{
	struct ipr_resource_entry *res;
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long lock_flags = 0;

	ioa_cfg = (struct ipr_ioa_cfg *) sdev->host->hostdata;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = (struct ipr_resource_entry *) sdev->hostdata;
	if (res) {
		if (res->sata_port)
			res->sata_port->ap->link.device[0].class = ATA_DEV_NONE;
		sdev->hostdata = NULL;
		res->sdev = NULL;
		res->sata_port = NULL;
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
}

/**
 * ipr_slave_configure - Configure a SCSI device
 * @sdev:	scsi device struct
 *
 * This function configures the specified scsi device.
 *
 * Return value:
 * 	0 on success
 **/
static int ipr_slave_configure(struct scsi_device *sdev)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) sdev->host->hostdata;
	struct ipr_resource_entry *res;
	struct ata_port *ap = NULL;
	unsigned long lock_flags = 0;
	char buffer[IPR_MAX_RES_PATH_LENGTH];

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	res = sdev->hostdata;
	if (res) {
		if (ipr_is_af_dasd_device(res))
			sdev->type = TYPE_RAID;
		if (ipr_is_af_dasd_device(res) || ipr_is_ioa_resource(res)) {
			sdev->scsi_level = 4;
			sdev->no_uld_attach = 1;
		}
		if (ipr_is_vset_device(res)) {
			sdev->scsi_level = SCSI_SPC_3;
			sdev->no_report_opcodes = 1;
			blk_queue_rq_timeout(sdev->request_queue,
					     IPR_VSET_RW_TIMEOUT);
			blk_queue_max_hw_sectors(sdev->request_queue, IPR_VSET_MAX_SECTORS);
		}
		if (ipr_is_gata(res) && res->sata_port)
			ap = res->sata_port->ap;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

		if (ap) {
			scsi_change_queue_depth(sdev, IPR_MAX_CMD_PER_ATA_LUN);
			ata_sas_slave_configure(sdev, ap);
		}

		if (ioa_cfg->sis64)
			sdev_printk(KERN_INFO, sdev, "Resource path: %s\n",
				    ipr_format_res_path(ioa_cfg,
				res->res_path, buffer, sizeof(buffer)));
		return 0;
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return 0;
}

/**
 * ipr_ata_slave_alloc - Prepare for commands to a SATA device
 * @sdev:	scsi device struct
 *
 * This function initializes an ATA port so that future commands
 * sent through queuecommand will work.
 *
 * Return value:
 * 	0 on success
 **/
static int ipr_ata_slave_alloc(struct scsi_device *sdev)
{
	struct ipr_sata_port *sata_port = NULL;
	int rc = -ENXIO;

	ENTER;
	if (sdev->sdev_target)
		sata_port = sdev->sdev_target->hostdata;
	if (sata_port) {
		rc = ata_sas_port_init(sata_port->ap);
		if (rc == 0)
			rc = ata_sas_sync_probe(sata_port->ap);
	}

	if (rc)
		ipr_slave_destroy(sdev);

	LEAVE;
	return rc;
}

/**
 * ipr_slave_alloc - Prepare for commands to a device.
 * @sdev:	scsi device struct
 *
 * This function saves a pointer to the resource entry
 * in the scsi device struct if the device exists. We
 * can then use this pointer in ipr_queuecommand when
 * handling new commands.
 *
 * Return value:
 * 	0 on success / -ENXIO if device does not exist
 **/
static int ipr_slave_alloc(struct scsi_device *sdev)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) sdev->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags;
	int rc = -ENXIO;

	sdev->hostdata = NULL;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	res = ipr_find_sdev(sdev);
	if (res) {
		res->sdev = sdev;
		res->add_to_ml = 0;
		res->in_erp = 0;
		sdev->hostdata = res;
		if (!ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		rc = 0;
		if (ipr_is_gata(res)) {
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			return ipr_ata_slave_alloc(sdev);
		}
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return rc;
}

/**
 * ipr_match_lun - Match function for specified LUN
 * @ipr_cmd:	ipr command struct
 * @device:		device to match (sdev)
 *
 * Returns:
 *	1 if command matches sdev / 0 if command does not match sdev
 **/
static int ipr_match_lun(struct ipr_cmnd *ipr_cmd, void *device)
{
	if (ipr_cmd->scsi_cmd && ipr_cmd->scsi_cmd->device == device)
		return 1;
	return 0;
}

/**
 * ipr_cmnd_is_free - Check if a command is free or not
 * @ipr_cmd:	ipr command struct
 *
 * Returns:
 *	true / false
 **/
static bool ipr_cmnd_is_free(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_cmnd *loop_cmd;

	list_for_each_entry(loop_cmd, &ipr_cmd->hrrq->hrrq_free_q, queue) {
		if (loop_cmd == ipr_cmd)
			return true;
	}

	return false;
}

/**
 * ipr_match_res - Match function for specified resource entry
 * @ipr_cmd:	ipr command struct
 * @resource:	resource entry to match
 *
 * Returns:
 *	1 if command matches sdev / 0 if command does not match sdev
 **/
static int ipr_match_res(struct ipr_cmnd *ipr_cmd, void *resource)
{
	struct ipr_resource_entry *res = resource;

	if (res && ipr_cmd->ioarcb.res_handle == res->res_handle)
		return 1;
	return 0;
}

/**
 * ipr_wait_for_ops - Wait for matching commands to complete
 * @ioa_cfg:	ioa config struct
 * @device:		device to match (sdev)
 * @match:		match function to use
 *
 * Returns:
 *	SUCCESS / FAILED
 **/
static int ipr_wait_for_ops(struct ipr_ioa_cfg *ioa_cfg, void *device,
			    int (*match)(struct ipr_cmnd *, void *))
{
	struct ipr_cmnd *ipr_cmd;
	int wait, i;
	unsigned long flags;
	struct ipr_hrr_queue *hrrq;
	signed long timeout = IPR_ABORT_TASK_TIMEOUT;
	DECLARE_COMPLETION_ONSTACK(comp);

	ENTER;
	do {
		wait = 0;

		for_each_hrrq(hrrq, ioa_cfg) {
			spin_lock_irqsave(hrrq->lock, flags);
			for (i = hrrq->min_cmd_id; i <= hrrq->max_cmd_id; i++) {
				ipr_cmd = ioa_cfg->ipr_cmnd_list[i];
				if (!ipr_cmnd_is_free(ipr_cmd)) {
					if (match(ipr_cmd, device)) {
						ipr_cmd->eh_comp = &comp;
						wait++;
					}
				}
			}
			spin_unlock_irqrestore(hrrq->lock, flags);
		}

		if (wait) {
			timeout = wait_for_completion_timeout(&comp, timeout);

			if (!timeout) {
				wait = 0;

				for_each_hrrq(hrrq, ioa_cfg) {
					spin_lock_irqsave(hrrq->lock, flags);
					for (i = hrrq->min_cmd_id; i <= hrrq->max_cmd_id; i++) {
						ipr_cmd = ioa_cfg->ipr_cmnd_list[i];
						if (!ipr_cmnd_is_free(ipr_cmd)) {
							if (match(ipr_cmd, device)) {
								ipr_cmd->eh_comp = NULL;
								wait++;
							}
						}
					}
					spin_unlock_irqrestore(hrrq->lock, flags);
				}

				if (wait)
					dev_err(&ioa_cfg->pdev->dev, "Timed out waiting for aborted commands\n");
				LEAVE;
				return wait ? FAILED : SUCCESS;
			}
		}
	} while (wait);

	LEAVE;
	return SUCCESS;
}

static int ipr_eh_host_reset(struct scsi_cmnd *cmd)
{
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long lock_flags = 0;
	int rc = SUCCESS;

	ENTER;
	ioa_cfg = (struct ipr_ioa_cfg *) cmd->device->host->hostdata;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (!ioa_cfg->in_reset_reload && !ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead) {
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_ABBREV);
		dev_err(&ioa_cfg->pdev->dev,
			"Adapter being reset as a result of error recovery.\n");

		if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
			ioa_cfg->sdt_state = GET_DUMP;
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	/* If we got hit with a host reset while we were already resetting
	 the adapter for some reason, and the reset failed. */
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead) {
		ipr_trace;
		rc = FAILED;
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
	return rc;
}

/**
 * ipr_device_reset - Reset the device
 * @ioa_cfg:	ioa config struct
 * @res:		resource entry struct
 *
 * This function issues a device reset to the affected device.
 * If the device is a SCSI device, a LUN reset will be sent
 * to the device first. If that does not work, a target reset
 * will be sent. If the device is a SATA device, a PHY reset will
 * be sent.
 *
 * Return value:
 *	0 on success / non-zero on failure
 **/
static int ipr_device_reset(struct ipr_ioa_cfg *ioa_cfg,
			    struct ipr_resource_entry *res)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	struct ipr_cmd_pkt *cmd_pkt;
	struct ipr_ioarcb_ata_regs *regs;
	u32 ioasc;

	ENTER;
	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioarcb = &ipr_cmd->ioarcb;
	cmd_pkt = &ioarcb->cmd_pkt;

	if (ipr_cmd->ioa_cfg->sis64) {
		regs = &ipr_cmd->i.ata_ioadl.regs;
		ioarcb->add_cmd_parms_offset = cpu_to_be16(sizeof(*ioarcb));
	} else
		regs = &ioarcb->u.add_data.u.regs;

	ioarcb->res_handle = res->res_handle;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_RESET_DEVICE;
	if (ipr_is_gata(res)) {
		cmd_pkt->cdb[2] = IPR_ATA_PHY_RESET;
		ioarcb->add_cmd_parms_len = cpu_to_be16(sizeof(regs->flags));
		regs->flags |= IPR_ATA_FLAG_STATUS_ON_GOOD_COMPLETION;
	}

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_DEVICE_RESET_TIMEOUT);
	ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	if (ipr_is_gata(res) && res->sata_port && ioasc != IPR_IOASC_IOA_WAS_RESET) {
		if (ipr_cmd->ioa_cfg->sis64)
			memcpy(&res->sata_port->ioasa, &ipr_cmd->s.ioasa64.u.gata,
			       sizeof(struct ipr_ioasa_gata));
		else
			memcpy(&res->sata_port->ioasa, &ipr_cmd->s.ioasa.u.gata,
			       sizeof(struct ipr_ioasa_gata));
	}

	LEAVE;
	return IPR_IOASC_SENSE_KEY(ioasc) ? -EIO : 0;
}

/**
 * ipr_sata_reset - Reset the SATA port
 * @link:	SATA link to reset
 * @classes:	class of the attached device
 * @deadline:	unused
 *
 * This function issues a SATA phy reset to the affected ATA link.
 *
 * Return value:
 *	0 on success / non-zero on failure
 **/
static int ipr_sata_reset(struct ata_link *link, unsigned int *classes,
				unsigned long deadline)
{
	struct ipr_sata_port *sata_port = link->ap->private_data;
	struct ipr_ioa_cfg *ioa_cfg = sata_port->ioa_cfg;
	struct ipr_resource_entry *res;
	unsigned long lock_flags = 0;
	int rc = -ENXIO, ret;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	res = sata_port->res;
	if (res) {
		rc = ipr_device_reset(ioa_cfg, res);
		*classes = res->ata_class;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

		ret = ipr_wait_for_ops(ioa_cfg, res, ipr_match_res);
		if (ret != SUCCESS) {
			spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_ABBREV);
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

			wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		}
	} else
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	LEAVE;
	return rc;
}

/**
 * ipr_eh_dev_reset - Reset the device
 * @scsi_cmd:	scsi command struct
 *
 * This function issues a device reset to the affected device.
 * A LUN reset will be sent to the device first. If that does
 * not work, a target reset will be sent.
 *
 * Return value:
 *	SUCCESS / FAILED
 **/
static int __ipr_eh_dev_reset(struct scsi_cmnd *scsi_cmd)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	struct ata_port *ap;
	int rc = 0, i;
	struct ipr_hrr_queue *hrrq;

	ENTER;
	ioa_cfg = (struct ipr_ioa_cfg *) scsi_cmd->device->host->hostdata;
	res = scsi_cmd->device->hostdata;

	/*
	 * If we are currently going through reset/reload, return failed. This will force the
	 * mid-layer to call ipr_eh_host_reset, which will then go to sleep and wait for the
	 * reset to complete
	 */
	if (ioa_cfg->in_reset_reload)
		return FAILED;
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead)
		return FAILED;

	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		for (i = hrrq->min_cmd_id; i <= hrrq->max_cmd_id; i++) {
			ipr_cmd = ioa_cfg->ipr_cmnd_list[i];

			if (ipr_cmd->ioarcb.res_handle == res->res_handle) {
				if (!ipr_cmd->qc)
					continue;
				if (ipr_cmnd_is_free(ipr_cmd))
					continue;

				ipr_cmd->done = ipr_sata_eh_done;
				if (!(ipr_cmd->qc->flags & ATA_QCFLAG_FAILED)) {
					ipr_cmd->qc->err_mask |= AC_ERR_TIMEOUT;
					ipr_cmd->qc->flags |= ATA_QCFLAG_FAILED;
				}
			}
		}
		spin_unlock(&hrrq->_lock);
	}
	res->resetting_device = 1;
	scmd_printk(KERN_ERR, scsi_cmd, "Resetting device\n");

	if (ipr_is_gata(res) && res->sata_port) {
		ap = res->sata_port->ap;
		spin_unlock_irq(scsi_cmd->device->host->host_lock);
		ata_std_error_handler(ap);
		spin_lock_irq(scsi_cmd->device->host->host_lock);
	} else
		rc = ipr_device_reset(ioa_cfg, res);
	res->resetting_device = 0;
	res->reset_occurred = 1;

	LEAVE;
	return rc ? FAILED : SUCCESS;
}

static int ipr_eh_dev_reset(struct scsi_cmnd *cmd)
{
	int rc;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;

	ioa_cfg = (struct ipr_ioa_cfg *) cmd->device->host->hostdata;
	res = cmd->device->hostdata;

	if (!res)
		return FAILED;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = __ipr_eh_dev_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);

	if (rc == SUCCESS) {
		if (ipr_is_gata(res) && res->sata_port)
			rc = ipr_wait_for_ops(ioa_cfg, res, ipr_match_res);
		else
			rc = ipr_wait_for_ops(ioa_cfg, cmd->device, ipr_match_lun);
	}

	return rc;
}

/**
 * ipr_bus_reset_done - Op done function for bus reset.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for a bus reset
 *
 * Return value:
 * 	none
 **/
static void ipr_bus_reset_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res;

	ENTER;
	if (!ioa_cfg->sis64)
		list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
			if (res->res_handle == ipr_cmd->ioarcb.res_handle) {
				scsi_report_bus_reset(ioa_cfg->host, res->bus);
				break;
			}
		}

	/*
	 * If abort has not completed, indicate the reset has, else call the
	 * abort's done function to wake the sleeping eh thread
	 */
	if (ipr_cmd->sibling->sibling)
		ipr_cmd->sibling->sibling = NULL;
	else
		ipr_cmd->sibling->done(ipr_cmd->sibling);

	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	LEAVE;
}

/**
 * ipr_abort_timeout - An abort task has timed out
 * @t: Timer context used to fetch ipr command struct
 *
 * This function handles when an abort task times out. If this
 * happens we issue a bus reset since we have resources tied
 * up that must be freed before returning to the midlayer.
 *
 * Return value:
 *	none
 **/
static void ipr_abort_timeout(struct timer_list *t)
{
	struct ipr_cmnd *ipr_cmd = from_timer(ipr_cmd, t, timer);
	struct ipr_cmnd *reset_cmd;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_cmd_pkt *cmd_pkt;
	unsigned long lock_flags = 0;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ipr_cmd->completion.done || ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return;
	}

	sdev_printk(KERN_ERR, ipr_cmd->u.sdev, "Abort timed out. Resetting bus.\n");
	reset_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ipr_cmd->sibling = reset_cmd;
	reset_cmd->sibling = ipr_cmd;
	reset_cmd->ioarcb.res_handle = ipr_cmd->ioarcb.res_handle;
	cmd_pkt = &reset_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_RESET_DEVICE;
	cmd_pkt->cdb[2] = IPR_RESET_TYPE_SELECT | IPR_BUS_RESET;

	ipr_do_req(reset_cmd, ipr_bus_reset_done, ipr_timeout, IPR_DEVICE_RESET_TIMEOUT);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
}

/**
 * ipr_cancel_op - Cancel specified op
 * @scsi_cmd:	scsi command struct
 *
 * This function cancels specified op.
 *
 * Return value:
 *	SUCCESS / FAILED
 **/
static int ipr_cancel_op(struct scsi_cmnd *scsi_cmd)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	struct ipr_cmd_pkt *cmd_pkt;
	u32 ioasc;
	int i, op_found = 0;
	struct ipr_hrr_queue *hrrq;

	ENTER;
	ioa_cfg = (struct ipr_ioa_cfg *)scsi_cmd->device->host->hostdata;
	res = scsi_cmd->device->hostdata;

	/* If we are currently going through reset/reload, return failed.
	 * This will force the mid-layer to call ipr_eh_host_reset,
	 * which will then go to sleep and wait for the reset to complete
	 */
	if (ioa_cfg->in_reset_reload ||
	    ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead)
		return FAILED;
	if (!res)
		return FAILED;

	/*
	 * If we are aborting a timed out op, chances are that the timeout was caused
	 * by a still not detected EEH error. In such cases, reading a register will
	 * trigger the EEH recovery infrastructure.
	 */
	readl(ioa_cfg->regs.sense_interrupt_reg);

	if (!ipr_is_gscsi(res))
		return FAILED;

	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		for (i = hrrq->min_cmd_id; i <= hrrq->max_cmd_id; i++) {
			if (ioa_cfg->ipr_cmnd_list[i]->scsi_cmd == scsi_cmd) {
				if (!ipr_cmnd_is_free(ioa_cfg->ipr_cmnd_list[i])) {
					op_found = 1;
					break;
				}
			}
		}
		spin_unlock(&hrrq->_lock);
	}

	if (!op_found)
		return SUCCESS;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ipr_cmd->ioarcb.res_handle = res->res_handle;
	cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_CANCEL_ALL_REQUESTS;
	ipr_cmd->u.sdev = scsi_cmd->device;

	scmd_printk(KERN_ERR, scsi_cmd, "Aborting command: %02X\n",
		    scsi_cmd->cmnd[0]);
	ipr_send_blocking_cmd(ipr_cmd, ipr_abort_timeout, IPR_CANCEL_ALL_TIMEOUT);
	ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	/*
	 * If the abort task timed out and we sent a bus reset, we will get
	 * one the following responses to the abort
	 */
	if (ioasc == IPR_IOASC_BUS_WAS_RESET || ioasc == IPR_IOASC_SYNC_REQUIRED) {
		ioasc = 0;
		ipr_trace;
	}

	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	if (!ipr_is_naca_model(res))
		res->needs_sync_complete = 1;

	LEAVE;
	return IPR_IOASC_SENSE_KEY(ioasc) ? FAILED : SUCCESS;
}

/**
 * ipr_eh_abort - Abort a single op
 * @shost:           scsi host struct
 * @elapsed_time:    elapsed time
 *
 * Return value:
 *	0 if scan in progress / 1 if scan is complete
 **/
static int ipr_scan_finished(struct Scsi_Host *shost, unsigned long elapsed_time)
{
	unsigned long lock_flags;
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) shost->hostdata;
	int rc = 0;

	spin_lock_irqsave(shost->host_lock, lock_flags);
	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead || ioa_cfg->scan_done)
		rc = 1;
	if ((elapsed_time/HZ) > (ioa_cfg->transop_timeout * 2))
		rc = 1;
	spin_unlock_irqrestore(shost->host_lock, lock_flags);
	return rc;
}

/**
 * ipr_eh_host_reset - Reset the host adapter
 * @scsi_cmd:	scsi command struct
 *
 * Return value:
 * 	SUCCESS / FAILED
 **/
static int ipr_eh_abort(struct scsi_cmnd *scsi_cmd)
{
	unsigned long flags;
	int rc;
	struct ipr_ioa_cfg *ioa_cfg;

	ENTER;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_cmd->device->host->hostdata;

	spin_lock_irqsave(scsi_cmd->device->host->host_lock, flags);
	rc = ipr_cancel_op(scsi_cmd);
	spin_unlock_irqrestore(scsi_cmd->device->host->host_lock, flags);

	if (rc == SUCCESS)
		rc = ipr_wait_for_ops(ioa_cfg, scsi_cmd->device, ipr_match_lun);
	LEAVE;
	return rc;
}

/**
 * ipr_handle_other_interrupt - Handle "other" interrupts
 * @ioa_cfg:	ioa config struct
 * @int_reg:	interrupt register
 *
 * Return value:
 * 	IRQ_NONE / IRQ_HANDLED
 **/
static irqreturn_t ipr_handle_other_interrupt(struct ipr_ioa_cfg *ioa_cfg,
					      u32 int_reg)
{
	irqreturn_t rc = IRQ_HANDLED;
	u32 int_mask_reg;

	int_mask_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg32);
	int_reg &= ~int_mask_reg;

	/* If an interrupt on the adapter did not occur, ignore it.
	 * Or in the case of SIS 64, check for a stage change interrupt.
	 */
	if ((int_reg & IPR_PCII_OPER_INTERRUPTS) == 0) {
		if (ioa_cfg->sis64) {
			int_mask_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);
			int_reg = readl(ioa_cfg->regs.sense_interrupt_reg) & ~int_mask_reg;
			if (int_reg & IPR_PCII_IPL_STAGE_CHANGE) {

				/* clear stage change */
				writel(IPR_PCII_IPL_STAGE_CHANGE, ioa_cfg->regs.clr_interrupt_reg);
				int_reg = readl(ioa_cfg->regs.sense_interrupt_reg) & ~int_mask_reg;
				list_del(&ioa_cfg->reset_cmd->queue);
				del_timer(&ioa_cfg->reset_cmd->timer);
				ipr_reset_ioa_job(ioa_cfg->reset_cmd);
				return IRQ_HANDLED;
			}
		}

		return IRQ_NONE;
	}

	if (int_reg & IPR_PCII_IOA_TRANS_TO_OPER) {
		/* Mask the interrupt */
		writel(IPR_PCII_IOA_TRANS_TO_OPER, ioa_cfg->regs.set_interrupt_mask_reg);
		int_reg = readl(ioa_cfg->regs.sense_interrupt_reg);

		list_del(&ioa_cfg->reset_cmd->queue);
		del_timer(&ioa_cfg->reset_cmd->timer);
		ipr_reset_ioa_job(ioa_cfg->reset_cmd);
	} else if ((int_reg & IPR_PCII_HRRQ_UPDATED) == int_reg) {
		if (ioa_cfg->clear_isr) {
			if (ipr_debug && printk_ratelimit())
				dev_err(&ioa_cfg->pdev->dev,
					"Spurious interrupt detected. 0x%08X\n", int_reg);
			writel(IPR_PCII_HRRQ_UPDATED, ioa_cfg->regs.clr_interrupt_reg32);
			int_reg = readl(ioa_cfg->regs.sense_interrupt_reg32);
			return IRQ_NONE;
		}
	} else {
		if (int_reg & IPR_PCII_IOA_UNIT_CHECKED)
			ioa_cfg->ioa_unit_checked = 1;
		else if (int_reg & IPR_PCII_NO_HOST_RRQ)
			dev_err(&ioa_cfg->pdev->dev,
				"No Host RRQ. 0x%08X\n", int_reg);
		else
			dev_err(&ioa_cfg->pdev->dev,
				"Permanent IOA failure. 0x%08X\n", int_reg);

		if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
			ioa_cfg->sdt_state = GET_DUMP;

		ipr_mask_and_clear_interrupts(ioa_cfg, ~0);
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	}

	return rc;
}

/**
 * ipr_isr_eh - Interrupt service routine error handler
 * @ioa_cfg:	ioa config struct
 * @msg:	message to log
 * @number:	various meanings depending on the caller/message
 *
 * Return value:
 * 	none
 **/
static void ipr_isr_eh(struct ipr_ioa_cfg *ioa_cfg, char *msg, u16 number)
{
	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev, "%s %d\n", msg, number);

	if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
		ioa_cfg->sdt_state = GET_DUMP;

	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
}

static int ipr_process_hrrq(struct ipr_hrr_queue *hrr_queue, int budget,
						struct list_head *doneq)
{
	u32 ioasc;
	u16 cmd_index;
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg = hrr_queue->ioa_cfg;
	int num_hrrq = 0;

	/* If interrupts are disabled, ignore the interrupt */
	if (!hrr_queue->allow_interrupts)
		return 0;

	while ((be32_to_cpu(*hrr_queue->hrrq_curr) & IPR_HRRQ_TOGGLE_BIT) ==
	       hrr_queue->toggle_bit) {

		cmd_index = (be32_to_cpu(*hrr_queue->hrrq_curr) &
			     IPR_HRRQ_REQ_RESP_HANDLE_MASK) >>
			     IPR_HRRQ_REQ_RESP_HANDLE_SHIFT;

		if (unlikely(cmd_index > hrr_queue->max_cmd_id ||
			     cmd_index < hrr_queue->min_cmd_id)) {
			ipr_isr_eh(ioa_cfg,
				"Invalid response handle from IOA: ",
				cmd_index);
			break;
		}

		ipr_cmd = ioa_cfg->ipr_cmnd_list[cmd_index];
		ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

		ipr_trc_hook(ipr_cmd, IPR_TRACE_FINISH, ioasc);

		list_move_tail(&ipr_cmd->queue, doneq);

		if (hrr_queue->hrrq_curr < hrr_queue->hrrq_end) {
			hrr_queue->hrrq_curr++;
		} else {
			hrr_queue->hrrq_curr = hrr_queue->hrrq_start;
			hrr_queue->toggle_bit ^= 1u;
		}
		num_hrrq++;
		if (budget > 0 && num_hrrq >= budget)
			break;
	}

	return num_hrrq;
}

static int ipr_iopoll(struct irq_poll *iop, int budget)
{
	struct ipr_hrr_queue *hrrq;
	struct ipr_cmnd *ipr_cmd, *temp;
	unsigned long hrrq_flags;
	int completed_ops;
	LIST_HEAD(doneq);

	hrrq = container_of(iop, struct ipr_hrr_queue, iopoll);

	spin_lock_irqsave(hrrq->lock, hrrq_flags);
	completed_ops = ipr_process_hrrq(hrrq, budget, &doneq);

	if (completed_ops < budget)
		irq_poll_complete(iop);
	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);

	list_for_each_entry_safe(ipr_cmd, temp, &doneq, queue) {
		list_del(&ipr_cmd->queue);
		del_timer(&ipr_cmd->timer);
		ipr_cmd->fast_done(ipr_cmd);
	}

	return completed_ops;
}

/**
 * ipr_isr - Interrupt service routine
 * @irq:	irq number
 * @devp:	pointer to ioa config struct
 *
 * Return value:
 * 	IRQ_NONE / IRQ_HANDLED
 **/
static irqreturn_t ipr_isr(int irq, void *devp)
{
	struct ipr_hrr_queue *hrrq = (struct ipr_hrr_queue *)devp;
	struct ipr_ioa_cfg *ioa_cfg = hrrq->ioa_cfg;
	unsigned long hrrq_flags = 0;
	u32 int_reg = 0;
	int num_hrrq = 0;
	int irq_none = 0;
	struct ipr_cmnd *ipr_cmd, *temp;
	irqreturn_t rc = IRQ_NONE;
	LIST_HEAD(doneq);

	spin_lock_irqsave(hrrq->lock, hrrq_flags);
	/* If interrupts are disabled, ignore the interrupt */
	if (!hrrq->allow_interrupts) {
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		return IRQ_NONE;
	}

	while (1) {
		if (ipr_process_hrrq(hrrq, -1, &doneq)) {
			rc =  IRQ_HANDLED;

			if (!ioa_cfg->clear_isr)
				break;

			/* Clear the PCI interrupt */
			num_hrrq = 0;
			do {
				writel(IPR_PCII_HRRQ_UPDATED,
				     ioa_cfg->regs.clr_interrupt_reg32);
				int_reg = readl(ioa_cfg->regs.sense_interrupt_reg32);
			} while (int_reg & IPR_PCII_HRRQ_UPDATED &&
				num_hrrq++ < IPR_MAX_HRRQ_RETRIES);

		} else if (rc == IRQ_NONE && irq_none == 0) {
			int_reg = readl(ioa_cfg->regs.sense_interrupt_reg32);
			irq_none++;
		} else if (num_hrrq == IPR_MAX_HRRQ_RETRIES &&
			   int_reg & IPR_PCII_HRRQ_UPDATED) {
			ipr_isr_eh(ioa_cfg,
				"Error clearing HRRQ: ", num_hrrq);
			rc = IRQ_HANDLED;
			break;
		} else
			break;
	}

	if (unlikely(rc == IRQ_NONE))
		rc = ipr_handle_other_interrupt(ioa_cfg, int_reg);

	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
	list_for_each_entry_safe(ipr_cmd, temp, &doneq, queue) {
		list_del(&ipr_cmd->queue);
		del_timer(&ipr_cmd->timer);
		ipr_cmd->fast_done(ipr_cmd);
	}
	return rc;
}

/**
 * ipr_isr_mhrrq - Interrupt service routine
 * @irq:	irq number
 * @devp:	pointer to ioa config struct
 *
 * Return value:
 *	IRQ_NONE / IRQ_HANDLED
 **/
static irqreturn_t ipr_isr_mhrrq(int irq, void *devp)
{
	struct ipr_hrr_queue *hrrq = (struct ipr_hrr_queue *)devp;
	struct ipr_ioa_cfg *ioa_cfg = hrrq->ioa_cfg;
	unsigned long hrrq_flags = 0;
	struct ipr_cmnd *ipr_cmd, *temp;
	irqreturn_t rc = IRQ_NONE;
	LIST_HEAD(doneq);

	spin_lock_irqsave(hrrq->lock, hrrq_flags);

	/* If interrupts are disabled, ignore the interrupt */
	if (!hrrq->allow_interrupts) {
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		return IRQ_NONE;
	}

	if (ioa_cfg->iopoll_weight && ioa_cfg->sis64 && ioa_cfg->nvectors > 1) {
		if ((be32_to_cpu(*hrrq->hrrq_curr) & IPR_HRRQ_TOGGLE_BIT) ==
		       hrrq->toggle_bit) {
			irq_poll_sched(&hrrq->iopoll);
			spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
			return IRQ_HANDLED;
		}
	} else {
		if ((be32_to_cpu(*hrrq->hrrq_curr) & IPR_HRRQ_TOGGLE_BIT) ==
			hrrq->toggle_bit)

			if (ipr_process_hrrq(hrrq, -1, &doneq))
				rc =  IRQ_HANDLED;
	}

	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);

	list_for_each_entry_safe(ipr_cmd, temp, &doneq, queue) {
		list_del(&ipr_cmd->queue);
		del_timer(&ipr_cmd->timer);
		ipr_cmd->fast_done(ipr_cmd);
	}
	return rc;
}

/**
 * ipr_build_ioadl64 - Build a scatter/gather list and map the buffer
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	0 on success / -1 on failure
 **/
static int ipr_build_ioadl64(struct ipr_ioa_cfg *ioa_cfg,
			     struct ipr_cmnd *ipr_cmd)
{
	int i, nseg;
	struct scatterlist *sg;
	u32 length;
	u32 ioadl_flags = 0;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl64_desc *ioadl64 = ipr_cmd->i.ioadl64;

	length = scsi_bufflen(scsi_cmd);
	if (!length)
		return 0;

	nseg = scsi_dma_map(scsi_cmd);
	if (nseg < 0) {
		if (printk_ratelimit())
			dev_err(&ioa_cfg->pdev->dev, "scsi_dma_map failed!\n");
		return -1;
	}

	ipr_cmd->dma_use_sg = nseg;

	ioarcb->data_transfer_length = cpu_to_be32(length);
	ioarcb->ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl64_desc) * ipr_cmd->dma_use_sg);

	if (scsi_cmd->sc_data_direction == DMA_TO_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_WRITE;
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
	} else if (scsi_cmd->sc_data_direction == DMA_FROM_DEVICE)
		ioadl_flags = IPR_IOADL_FLAGS_READ;

	scsi_for_each_sg(scsi_cmd, sg, ipr_cmd->dma_use_sg, i) {
		ioadl64[i].flags = cpu_to_be32(ioadl_flags);
		ioadl64[i].data_len = cpu_to_be32(sg_dma_len(sg));
		ioadl64[i].address = cpu_to_be64(sg_dma_address(sg));
	}

	ioadl64[i-1].flags |= cpu_to_be32(IPR_IOADL_FLAGS_LAST);
	return 0;
}

/**
 * ipr_build_ioadl - Build a scatter/gather list and map the buffer
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	0 on success / -1 on failure
 **/
static int ipr_build_ioadl(struct ipr_ioa_cfg *ioa_cfg,
			   struct ipr_cmnd *ipr_cmd)
{
	int i, nseg;
	struct scatterlist *sg;
	u32 length;
	u32 ioadl_flags = 0;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->i.ioadl;

	length = scsi_bufflen(scsi_cmd);
	if (!length)
		return 0;

	nseg = scsi_dma_map(scsi_cmd);
	if (nseg < 0) {
		dev_err(&ioa_cfg->pdev->dev, "scsi_dma_map failed!\n");
		return -1;
	}

	ipr_cmd->dma_use_sg = nseg;

	if (scsi_cmd->sc_data_direction == DMA_TO_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_WRITE;
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
		ioarcb->data_transfer_length = cpu_to_be32(length);
		ioarcb->ioadl_len =
			cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
	} else if (scsi_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_READ;
		ioarcb->read_data_transfer_length = cpu_to_be32(length);
		ioarcb->read_ioadl_len =
			cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
	}

	if (ipr_cmd->dma_use_sg <= ARRAY_SIZE(ioarcb->u.add_data.u.ioadl)) {
		ioadl = ioarcb->u.add_data.u.ioadl;
		ioarcb->write_ioadl_addr = cpu_to_be32((ipr_cmd->dma_addr) +
				    offsetof(struct ipr_ioarcb, u.add_data));
		ioarcb->read_ioadl_addr = ioarcb->write_ioadl_addr;
	}

	scsi_for_each_sg(scsi_cmd, sg, ipr_cmd->dma_use_sg, i) {
		ioadl[i].flags_and_data_len =
			cpu_to_be32(ioadl_flags | sg_dma_len(sg));
		ioadl[i].address = cpu_to_be32(sg_dma_address(sg));
	}

	ioadl[i-1].flags_and_data_len |= cpu_to_be32(IPR_IOADL_FLAGS_LAST);
	return 0;
}

/**
 * __ipr_erp_done - Process completion of ERP for a device
 * @ipr_cmd:		ipr command struct
 *
 * This function copies the sense buffer into the scsi_cmd
 * struct and pushes the scsi_done function.
 *
 * Return value:
 * 	nothing
 **/
static void __ipr_erp_done(struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	if (IPR_IOASC_SENSE_KEY(ioasc) > 0) {
		scsi_cmd->result |= (DID_ERROR << 16);
		scmd_printk(KERN_ERR, scsi_cmd,
			    "Request Sense failed with IOASC: 0x%08X\n", ioasc);
	} else {
		memcpy(scsi_cmd->sense_buffer, ipr_cmd->sense_buffer,
		       SCSI_SENSE_BUFFERSIZE);
	}

	if (res) {
		if (!ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		res->in_erp = 0;
	}
	scsi_dma_unmap(ipr_cmd->scsi_cmd);
	scsi_cmd->scsi_done(scsi_cmd);
	if (ipr_cmd->eh_comp)
		complete(ipr_cmd->eh_comp);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
}

/**
 * ipr_erp_done - Process completion of ERP for a device
 * @ipr_cmd:		ipr command struct
 *
 * This function copies the sense buffer into the scsi_cmd
 * struct and pushes the scsi_done function.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hrr_queue *hrrq = ipr_cmd->hrrq;
	unsigned long hrrq_flags;

	spin_lock_irqsave(&hrrq->_lock, hrrq_flags);
	__ipr_erp_done(ipr_cmd);
	spin_unlock_irqrestore(&hrrq->_lock, hrrq_flags);
}

/**
 * ipr_reinit_ipr_cmnd_for_erp - Re-initialize a cmnd block to be used for ERP
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_reinit_ipr_cmnd_for_erp(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioasa *ioasa = &ipr_cmd->s.ioasa;
	dma_addr_t dma_addr = ipr_cmd->dma_addr;

	memset(&ioarcb->cmd_pkt, 0, sizeof(struct ipr_cmd_pkt));
	ioarcb->data_transfer_length = 0;
	ioarcb->read_data_transfer_length = 0;
	ioarcb->ioadl_len = 0;
	ioarcb->read_ioadl_len = 0;
	ioasa->hdr.ioasc = 0;
	ioasa->hdr.residual_data_len = 0;

	if (ipr_cmd->ioa_cfg->sis64)
		ioarcb->u.sis64_addr_data.data_ioadl_addr =
			cpu_to_be64(dma_addr + offsetof(struct ipr_cmnd, i.ioadl64));
	else {
		ioarcb->write_ioadl_addr =
			cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, i.ioadl));
		ioarcb->read_ioadl_addr = ioarcb->write_ioadl_addr;
	}
}

/**
 * __ipr_erp_request_sense - Send request sense to a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a request sense to a device as a result
 * of a check condition.
 *
 * Return value:
 * 	nothing
 **/
static void __ipr_erp_request_sense(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_cmd_pkt *cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	if (IPR_IOASC_SENSE_KEY(ioasc) > 0) {
		__ipr_erp_done(ipr_cmd);
		return;
	}

	ipr_reinit_ipr_cmnd_for_erp(ipr_cmd);

	cmd_pkt->request_type = IPR_RQTYPE_SCSICDB;
	cmd_pkt->cdb[0] = REQUEST_SENSE;
	cmd_pkt->cdb[4] = SCSI_SENSE_BUFFERSIZE;
	cmd_pkt->flags_hi |= IPR_FLAGS_HI_SYNC_OVERRIDE;
	cmd_pkt->flags_hi |= IPR_FLAGS_HI_NO_ULEN_CHK;
	cmd_pkt->timeout = cpu_to_be16(IPR_REQUEST_SENSE_TIMEOUT / HZ);

	ipr_init_ioadl(ipr_cmd, ipr_cmd->sense_buffer_dma,
		       SCSI_SENSE_BUFFERSIZE, IPR_IOADL_FLAGS_READ_LAST);

	ipr_do_req(ipr_cmd, ipr_erp_done, ipr_timeout,
		   IPR_REQUEST_SENSE_TIMEOUT * 2);
}

/**
 * ipr_erp_request_sense - Send request sense to a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a request sense to a device as a result
 * of a check condition.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_request_sense(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hrr_queue *hrrq = ipr_cmd->hrrq;
	unsigned long hrrq_flags;

	spin_lock_irqsave(&hrrq->_lock, hrrq_flags);
	__ipr_erp_request_sense(ipr_cmd);
	spin_unlock_irqrestore(&hrrq->_lock, hrrq_flags);
}

/**
 * ipr_erp_cancel_all - Send cancel all to a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a cancel all to a device to clear the
 * queue. If we are running TCQ on the device, QERR is set to 1,
 * which means all outstanding ops have been dropped on the floor.
 * Cancel all will return them to us.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_cancel_all(struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	struct ipr_cmd_pkt *cmd_pkt;

	res->in_erp = 1;

	ipr_reinit_ipr_cmnd_for_erp(ipr_cmd);

	if (!scsi_cmd->device->simple_tags) {
		__ipr_erp_request_sense(ipr_cmd);
		return;
	}

	cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_CANCEL_ALL_REQUESTS;

	ipr_do_req(ipr_cmd, ipr_erp_request_sense, ipr_timeout,
		   IPR_CANCEL_ALL_TIMEOUT);
}

/**
 * ipr_dump_ioasa - Dump contents of IOASA
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 *
 * This function is invoked by the interrupt handler when ops
 * fail. It will log the IOASA if appropriate. Only called
 * for GPDD ops.
 *
 * Return value:
 * 	none
 **/
static void ipr_dump_ioasa(struct ipr_ioa_cfg *ioa_cfg,
			   struct ipr_cmnd *ipr_cmd, struct ipr_resource_entry *res)
{
	int i;
	u16 data_len;
	u32 ioasc, fd_ioasc;
	struct ipr_ioasa *ioasa = &ipr_cmd->s.ioasa;
	__be32 *ioasa_data = (__be32 *)ioasa;
	int error_index;

	ioasc = be32_to_cpu(ioasa->hdr.ioasc) & IPR_IOASC_IOASC_MASK;
	fd_ioasc = be32_to_cpu(ioasa->hdr.fd_ioasc) & IPR_IOASC_IOASC_MASK;

	if (0 == ioasc)
		return;

	if (ioa_cfg->log_level < IPR_DEFAULT_LOG_LEVEL)
		return;

	if (ioasc == IPR_IOASC_BUS_WAS_RESET && fd_ioasc)
		error_index = ipr_get_error(fd_ioasc);
	else
		error_index = ipr_get_error(ioasc);

	if (ioa_cfg->log_level < IPR_MAX_LOG_LEVEL) {
		/* Don't log an error if the IOA already logged one */
		if (ioasa->hdr.ilid != 0)
			return;

		if (!ipr_is_gscsi(res))
			return;

		if (ipr_error_table[error_index].log_ioasa == 0)
			return;
	}

	ipr_res_err(ioa_cfg, res, "%s\n", ipr_error_table[error_index].error);

	data_len = be16_to_cpu(ioasa->hdr.ret_stat_len);
	if (ioa_cfg->sis64 && sizeof(struct ipr_ioasa64) < data_len)
		data_len = sizeof(struct ipr_ioasa64);
	else if (!ioa_cfg->sis64 && sizeof(struct ipr_ioasa) < data_len)
		data_len = sizeof(struct ipr_ioasa);

	ipr_err("IOASA Dump:\n");

	for (i = 0; i < data_len / 4; i += 4) {
		ipr_err("%08X: %08X %08X %08X %08X\n", i*4,
			be32_to_cpu(ioasa_data[i]),
			be32_to_cpu(ioasa_data[i+1]),
			be32_to_cpu(ioasa_data[i+2]),
			be32_to_cpu(ioasa_data[i+3]));
	}
}

/**
 * ipr_gen_sense - Generate SCSI sense data from an IOASA
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_gen_sense(struct ipr_cmnd *ipr_cmd)
{
	u32 failing_lba;
	u8 *sense_buf = ipr_cmd->scsi_cmd->sense_buffer;
	struct ipr_resource_entry *res = ipr_cmd->scsi_cmd->device->hostdata;
	struct ipr_ioasa *ioasa = &ipr_cmd->s.ioasa;
	u32 ioasc = be32_to_cpu(ioasa->hdr.ioasc);

	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);

	if (ioasc >= IPR_FIRST_DRIVER_IOASC)
		return;

	ipr_cmd->scsi_cmd->result = SAM_STAT_CHECK_CONDITION;

	if (ipr_is_vset_device(res) &&
	    ioasc == IPR_IOASC_MED_DO_NOT_REALLOC &&
	    ioasa->u.vset.failing_lba_hi != 0) {
		sense_buf[0] = 0x72;
		sense_buf[1] = IPR_IOASC_SENSE_KEY(ioasc);
		sense_buf[2] = IPR_IOASC_SENSE_CODE(ioasc);
		sense_buf[3] = IPR_IOASC_SENSE_QUAL(ioasc);

		sense_buf[7] = 12;
		sense_buf[8] = 0;
		sense_buf[9] = 0x0A;
		sense_buf[10] = 0x80;

		failing_lba = be32_to_cpu(ioasa->u.vset.failing_lba_hi);

		sense_buf[12] = (failing_lba & 0xff000000) >> 24;
		sense_buf[13] = (failing_lba & 0x00ff0000) >> 16;
		sense_buf[14] = (failing_lba & 0x0000ff00) >> 8;
		sense_buf[15] = failing_lba & 0x000000ff;

		failing_lba = be32_to_cpu(ioasa->u.vset.failing_lba_lo);

		sense_buf[16] = (failing_lba & 0xff000000) >> 24;
		sense_buf[17] = (failing_lba & 0x00ff0000) >> 16;
		sense_buf[18] = (failing_lba & 0x0000ff00) >> 8;
		sense_buf[19] = failing_lba & 0x000000ff;
	} else {
		sense_buf[0] = 0x70;
		sense_buf[2] = IPR_IOASC_SENSE_KEY(ioasc);
		sense_buf[12] = IPR_IOASC_SENSE_CODE(ioasc);
		sense_buf[13] = IPR_IOASC_SENSE_QUAL(ioasc);

		/* Illegal request */
		if ((IPR_IOASC_SENSE_KEY(ioasc) == 0x05) &&
		    (be32_to_cpu(ioasa->hdr.ioasc_specific) & IPR_FIELD_POINTER_VALID)) {
			sense_buf[7] = 10;	/* additional length */

			/* IOARCB was in error */
			if (IPR_IOASC_SENSE_CODE(ioasc) == 0x24)
				sense_buf[15] = 0xC0;
			else	/* Parameter data was invalid */
				sense_buf[15] = 0x80;

			sense_buf[16] =
			    ((IPR_FIELD_POINTER_MASK &
			      be32_to_cpu(ioasa->hdr.ioasc_specific)) >> 8) & 0xff;
			sense_buf[17] =
			    (IPR_FIELD_POINTER_MASK &
			     be32_to_cpu(ioasa->hdr.ioasc_specific)) & 0xff;
		} else {
			if (ioasc == IPR_IOASC_MED_DO_NOT_REALLOC) {
				if (ipr_is_vset_device(res))
					failing_lba = be32_to_cpu(ioasa->u.vset.failing_lba_lo);
				else
					failing_lba = be32_to_cpu(ioasa->u.dasd.failing_lba);

				sense_buf[0] |= 0x80;	/* Or in the Valid bit */
				sense_buf[3] = (failing_lba & 0xff000000) >> 24;
				sense_buf[4] = (failing_lba & 0x00ff0000) >> 16;
				sense_buf[5] = (failing_lba & 0x0000ff00) >> 8;
				sense_buf[6] = failing_lba & 0x000000ff;
			}

			sense_buf[7] = 6;	/* additional length */
		}
	}
}

/**
 * ipr_get_autosense - Copy autosense data to sense buffer
 * @ipr_cmd:	ipr command struct
 *
 * This function copies the autosense buffer to the buffer
 * in the scsi_cmd, if there is autosense available.
 *
 * Return value:
 *	1 if autosense was available / 0 if not
 **/
static int ipr_get_autosense(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioasa *ioasa = &ipr_cmd->s.ioasa;
	struct ipr_ioasa64 *ioasa64 = &ipr_cmd->s.ioasa64;

	if ((be32_to_cpu(ioasa->hdr.ioasc_specific) & IPR_AUTOSENSE_VALID) == 0)
		return 0;

	if (ipr_cmd->ioa_cfg->sis64)
		memcpy(ipr_cmd->scsi_cmd->sense_buffer, ioasa64->auto_sense.data,
		       min_t(u16, be16_to_cpu(ioasa64->auto_sense.auto_sense_len),
			   SCSI_SENSE_BUFFERSIZE));
	else
		memcpy(ipr_cmd->scsi_cmd->sense_buffer, ioasa->auto_sense.data,
		       min_t(u16, be16_to_cpu(ioasa->auto_sense.auto_sense_len),
			   SCSI_SENSE_BUFFERSIZE));
	return 1;
}

/**
 * ipr_erp_start - Process an error response for a SCSI op
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * This function determines whether or not to initiate ERP
 * on the affected device.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_start(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);
	u32 masked_ioasc = ioasc & IPR_IOASC_IOASC_MASK;

	if (!res) {
		__ipr_scsi_eh_done(ipr_cmd);
		return;
	}

	if (!ipr_is_gscsi(res) && masked_ioasc != IPR_IOASC_HW_DEV_BUS_STATUS)
		ipr_gen_sense(ipr_cmd);

	ipr_dump_ioasa(ioa_cfg, ipr_cmd, res);

	switch (masked_ioasc) {
	case IPR_IOASC_ABORTED_CMD_TERM_BY_HOST:
		if (ipr_is_naca_model(res))
			scsi_cmd->result |= (DID_ABORT << 16);
		else
			scsi_cmd->result |= (DID_IMM_RETRY << 16);
		break;
	case IPR_IOASC_IR_RESOURCE_HANDLE:
	case IPR_IOASC_IR_NO_CMDS_TO_2ND_IOA:
		scsi_cmd->result |= (DID_NO_CONNECT << 16);
		break;
	case IPR_IOASC_HW_SEL_TIMEOUT:
		scsi_cmd->result |= (DID_NO_CONNECT << 16);
		if (!ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		break;
	case IPR_IOASC_SYNC_REQUIRED:
		if (!res->in_erp)
			res->needs_sync_complete = 1;
		scsi_cmd->result |= (DID_IMM_RETRY << 16);
		break;
	case IPR_IOASC_MED_DO_NOT_REALLOC: /* prevent retries */
	case IPR_IOASA_IR_DUAL_IOA_DISABLED:
		/*
		 * exception: do not set DID_PASSTHROUGH on CHECK CONDITION
		 * so SCSI mid-layer and upper layers handle it accordingly.
		 */
		if (scsi_cmd->result != SAM_STAT_CHECK_CONDITION)
			scsi_cmd->result |= (DID_PASSTHROUGH << 16);
		break;
	case IPR_IOASC_BUS_WAS_RESET:
	case IPR_IOASC_BUS_WAS_RESET_BY_OTHER:
		/*
		 * Report the bus reset and ask for a retry. The device
		 * will give CC/UA the next command.
		 */
		if (!res->resetting_device)
			scsi_report_bus_reset(ioa_cfg->host, scsi_cmd->device->channel);
		scsi_cmd->result |= (DID_ERROR << 16);
		if (!ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		break;
	case IPR_IOASC_HW_DEV_BUS_STATUS:
		scsi_cmd->result |= IPR_IOASC_SENSE_STATUS(ioasc);
		if (IPR_IOASC_SENSE_STATUS(ioasc) == SAM_STAT_CHECK_CONDITION) {
			if (!ipr_get_autosense(ipr_cmd)) {
				if (!ipr_is_naca_model(res)) {
					ipr_erp_cancel_all(ipr_cmd);
					return;
				}
			}
		}
		if (!ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		break;
	case IPR_IOASC_NR_INIT_CMD_REQUIRED:
		break;
	case IPR_IOASC_IR_NON_OPTIMIZED:
		if (res->raw_mode) {
			res->raw_mode = 0;
			scsi_cmd->result |= (DID_IMM_RETRY << 16);
		} else
			scsi_cmd->result |= (DID_ERROR << 16);
		break;
	default:
		if (IPR_IOASC_SENSE_KEY(ioasc) > RECOVERED_ERROR)
			scsi_cmd->result |= (DID_ERROR << 16);
		if (!ipr_is_vset_device(res) && !ipr_is_naca_model(res))
			res->needs_sync_complete = 1;
		break;
	}

	scsi_dma_unmap(ipr_cmd->scsi_cmd);
	scsi_cmd->scsi_done(scsi_cmd);
	if (ipr_cmd->eh_comp)
		complete(ipr_cmd->eh_comp);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
}

/**
 * ipr_scsi_done - mid-layer done function
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer
 *
 * Return value:
 * 	none
 **/
static void ipr_scsi_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);
	unsigned long lock_flags;

	scsi_set_resid(scsi_cmd, be32_to_cpu(ipr_cmd->s.ioasa.hdr.residual_data_len));

	if (likely(IPR_IOASC_SENSE_KEY(ioasc) == 0)) {
		scsi_dma_unmap(scsi_cmd);

		spin_lock_irqsave(ipr_cmd->hrrq->lock, lock_flags);
		scsi_cmd->scsi_done(scsi_cmd);
		if (ipr_cmd->eh_comp)
			complete(ipr_cmd->eh_comp);
		list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
		spin_unlock_irqrestore(ipr_cmd->hrrq->lock, lock_flags);
	} else {
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		spin_lock(&ipr_cmd->hrrq->_lock);
		ipr_erp_start(ioa_cfg, ipr_cmd);
		spin_unlock(&ipr_cmd->hrrq->_lock);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	}
}

/**
 * ipr_queuecommand - Queue a mid-layer request
 * @shost:		scsi host struct
 * @scsi_cmd:	scsi command struct
 *
 * This function queues a request generated by the mid-layer.
 *
 * Return value:
 *	0 on success
 *	SCSI_MLQUEUE_DEVICE_BUSY if device is busy
 *	SCSI_MLQUEUE_HOST_BUSY if host is busy
 **/
static int ipr_queuecommand(struct Scsi_Host *shost,
			    struct scsi_cmnd *scsi_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	struct ipr_ioarcb *ioarcb;
	struct ipr_cmnd *ipr_cmd;
	unsigned long hrrq_flags, lock_flags;
	int rc;
	struct ipr_hrr_queue *hrrq;
	int hrrq_id;

	ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;

	scsi_cmd->result = (DID_OK << 16);
	res = scsi_cmd->device->hostdata;

	if (ipr_is_gata(res) && res->sata_port) {
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		rc = ata_sas_queuecmd(scsi_cmd, res->sata_port->ap);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return rc;
	}

	hrrq_id = ipr_get_hrrq_index(ioa_cfg);
	hrrq = &ioa_cfg->hrrq[hrrq_id];

	spin_lock_irqsave(hrrq->lock, hrrq_flags);
	/*
	 * We are currently blocking all devices due to a host reset
	 * We have told the host to stop giving us new requests, but
	 * ERP ops don't count. FIXME
	 */
	if (unlikely(!hrrq->allow_cmds && !hrrq->ioa_is_dead && !hrrq->removing_ioa)) {
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/*
	 * FIXME - Create scsi_set_host_offline interface
	 *  and the ioa_is_dead check can be removed
	 */
	if (unlikely(hrrq->ioa_is_dead || hrrq->removing_ioa || !res)) {
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		goto err_nodev;
	}

	ipr_cmd = __ipr_get_free_ipr_cmnd(hrrq);
	if (ipr_cmd == NULL) {
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);

	ipr_init_ipr_cmnd(ipr_cmd, ipr_scsi_done);
	ioarcb = &ipr_cmd->ioarcb;

	memcpy(ioarcb->cmd_pkt.cdb, scsi_cmd->cmnd, scsi_cmd->cmd_len);
	ipr_cmd->scsi_cmd = scsi_cmd;
	ipr_cmd->done = ipr_scsi_eh_done;

	if (ipr_is_gscsi(res)) {
		if (scsi_cmd->underflow == 0)
			ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_NO_ULEN_CHK;

		if (res->reset_occurred) {
			res->reset_occurred = 0;
			ioarcb->cmd_pkt.flags_lo |= IPR_FLAGS_LO_DELAY_AFTER_RST;
		}
	}

	if (ipr_is_gscsi(res) || ipr_is_vset_device(res)) {
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_NO_LINK_DESC;

		ioarcb->cmd_pkt.flags_lo |= IPR_FLAGS_LO_ALIGNED_BFR;
		if (scsi_cmd->flags & SCMD_TAGGED)
			ioarcb->cmd_pkt.flags_lo |= IPR_FLAGS_LO_SIMPLE_TASK;
		else
			ioarcb->cmd_pkt.flags_lo |= IPR_FLAGS_LO_UNTAGGED_TASK;
	}

	if (scsi_cmd->cmnd[0] >= 0xC0 &&
	    (!ipr_is_gscsi(res) || scsi_cmd->cmnd[0] == IPR_QUERY_RSRC_STATE)) {
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	}
	if (res->raw_mode && ipr_is_af_dasd_device(res)) {
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_PIPE;

		if (scsi_cmd->underflow == 0)
			ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_NO_ULEN_CHK;
	}

	if (ioa_cfg->sis64)
		rc = ipr_build_ioadl64(ioa_cfg, ipr_cmd);
	else
		rc = ipr_build_ioadl(ioa_cfg, ipr_cmd);

	spin_lock_irqsave(hrrq->lock, hrrq_flags);
	if (unlikely(rc || (!hrrq->allow_cmds && !hrrq->ioa_is_dead))) {
		list_add_tail(&ipr_cmd->queue, &hrrq->hrrq_free_q);
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		if (!rc)
			scsi_dma_unmap(scsi_cmd);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	if (unlikely(hrrq->ioa_is_dead)) {
		list_add_tail(&ipr_cmd->queue, &hrrq->hrrq_free_q);
		spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
		scsi_dma_unmap(scsi_cmd);
		goto err_nodev;
	}

	ioarcb->res_handle = res->res_handle;
	if (res->needs_sync_complete) {
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_SYNC_COMPLETE;
		res->needs_sync_complete = 0;
	}
	list_add_tail(&ipr_cmd->queue, &hrrq->hrrq_pending_q);
	ipr_trc_hook(ipr_cmd, IPR_TRACE_START, IPR_GET_RES_PHYS_LOC(res));
	ipr_send_command(ipr_cmd);
	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
	return 0;

err_nodev:
	spin_lock_irqsave(hrrq->lock, hrrq_flags);
	memset(scsi_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
	scsi_cmd->result = (DID_NO_CONNECT << 16);
	scsi_cmd->scsi_done(scsi_cmd);
	spin_unlock_irqrestore(hrrq->lock, hrrq_flags);
	return 0;
}

/**
 * ipr_ioctl - IOCTL handler
 * @sdev:	scsi device struct
 * @cmd:	IOCTL cmd
 * @arg:	IOCTL arg
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_ioctl(struct scsi_device *sdev, unsigned int cmd,
		     void __user *arg)
{
	struct ipr_resource_entry *res;

	res = (struct ipr_resource_entry *)sdev->hostdata;
	if (res && ipr_is_gata(res)) {
		if (cmd == HDIO_GET_IDENTITY)
			return -ENOTTY;
		return ata_sas_scsi_ioctl(res->sata_port->ap, sdev, cmd, arg);
	}

	return -EINVAL;
}

/**
 * ipr_info - Get information about the card/driver
 * @host:	scsi host struct
 *
 * Return value:
 * 	pointer to buffer with description string
 **/
static const char *ipr_ioa_info(struct Scsi_Host *host)
{
	static char buffer[512];
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long lock_flags = 0;

	ioa_cfg = (struct ipr_ioa_cfg *) host->hostdata;

	spin_lock_irqsave(host->host_lock, lock_flags);
	sprintf(buffer, "IBM %X Storage Adapter", ioa_cfg->type);
	spin_unlock_irqrestore(host->host_lock, lock_flags);

	return buffer;
}

static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = "IPR",
	.info = ipr_ioa_info,
	.ioctl = ipr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ipr_ioctl,
#endif
	.queuecommand = ipr_queuecommand,
	.dma_need_drain = ata_scsi_dma_need_drain,
	.eh_abort_handler = ipr_eh_abort,
	.eh_device_reset_handler = ipr_eh_dev_reset,
	.eh_host_reset_handler = ipr_eh_host_reset,
	.slave_alloc = ipr_slave_alloc,
	.slave_configure = ipr_slave_configure,
	.slave_destroy = ipr_slave_destroy,
	.scan_finished = ipr_scan_finished,
	.target_alloc = ipr_target_alloc,
	.target_destroy = ipr_target_destroy,
	.change_queue_depth = ipr_change_queue_depth,
	.bios_param = ipr_biosparam,
	.can_queue = IPR_MAX_COMMANDS,
	.this_id = -1,
	.sg_tablesize = IPR_MAX_SGLIST,
	.max_sectors = IPR_IOA_MAX_SECTORS,
	.cmd_per_lun = IPR_MAX_CMD_PER_LUN,
	.shost_attrs = ipr_ioa_attrs,
	.sdev_attrs = ipr_dev_attrs,
	.proc_name = IPR_NAME,
};

/**
 * ipr_ata_phy_reset - libata phy_reset handler
 * @ap:		ata port to reset
 *
 **/
static void ipr_ata_phy_reset(struct ata_port *ap)
{
	unsigned long flags;
	struct ipr_sata_port *sata_port = ap->private_data;
	struct ipr_resource_entry *res = sata_port->res;
	struct ipr_ioa_cfg *ioa_cfg = sata_port->ioa_cfg;
	int rc;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	}

	if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].allow_cmds)
		goto out_unlock;

	rc = ipr_device_reset(ioa_cfg, res);

	if (rc) {
		ap->link.device[0].class = ATA_DEV_NONE;
		goto out_unlock;
	}

	ap->link.device[0].class = res->ata_class;
	if (ap->link.device[0].class == ATA_DEV_UNKNOWN)
		ap->link.device[0].class = ATA_DEV_NONE;

out_unlock:
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
	LEAVE;
}

/**
 * ipr_ata_post_internal - Cleanup after an internal command
 * @qc:	ATA queued command
 *
 * Return value:
 * 	none
 **/
static void ipr_ata_post_internal(struct ata_queued_cmd *qc)
{
	struct ipr_sata_port *sata_port = qc->ap->private_data;
	struct ipr_ioa_cfg *ioa_cfg = sata_port->ioa_cfg;
	struct ipr_cmnd *ipr_cmd;
	struct ipr_hrr_queue *hrrq;
	unsigned long flags;

	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	}

	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		list_for_each_entry(ipr_cmd, &hrrq->hrrq_pending_q, queue) {
			if (ipr_cmd->qc == qc) {
				ipr_device_reset(ioa_cfg, sata_port->res);
				break;
			}
		}
		spin_unlock(&hrrq->_lock);
	}
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
}

/**
 * ipr_copy_sata_tf - Copy a SATA taskfile to an IOA data structure
 * @regs:	destination
 * @tf:	source ATA taskfile
 *
 * Return value:
 * 	none
 **/
static void ipr_copy_sata_tf(struct ipr_ioarcb_ata_regs *regs,
			     struct ata_taskfile *tf)
{
	regs->feature = tf->feature;
	regs->nsect = tf->nsect;
	regs->lbal = tf->lbal;
	regs->lbam = tf->lbam;
	regs->lbah = tf->lbah;
	regs->device = tf->device;
	regs->command = tf->command;
	regs->hob_feature = tf->hob_feature;
	regs->hob_nsect = tf->hob_nsect;
	regs->hob_lbal = tf->hob_lbal;
	regs->hob_lbam = tf->hob_lbam;
	regs->hob_lbah = tf->hob_lbah;
	regs->ctl = tf->ctl;
}

/**
 * ipr_sata_done - done function for SATA commands
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer to SATA devices
 *
 * Return value:
 * 	none
 **/
static void ipr_sata_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ata_queued_cmd *qc = ipr_cmd->qc;
	struct ipr_sata_port *sata_port = qc->ap->private_data;
	struct ipr_resource_entry *res = sata_port->res;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	spin_lock(&ipr_cmd->hrrq->_lock);
	if (ipr_cmd->ioa_cfg->sis64)
		memcpy(&sata_port->ioasa, &ipr_cmd->s.ioasa64.u.gata,
		       sizeof(struct ipr_ioasa_gata));
	else
		memcpy(&sata_port->ioasa, &ipr_cmd->s.ioasa.u.gata,
		       sizeof(struct ipr_ioasa_gata));
	ipr_dump_ioasa(ioa_cfg, ipr_cmd, res);

	if (be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc_specific) & IPR_ATA_DEVICE_WAS_RESET)
		scsi_report_device_reset(ioa_cfg->host, res->bus, res->target);

	if (IPR_IOASC_SENSE_KEY(ioasc) > RECOVERED_ERROR)
		qc->err_mask |= __ac_err_mask(sata_port->ioasa.status);
	else
		qc->err_mask |= ac_err_mask(sata_port->ioasa.status);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	spin_unlock(&ipr_cmd->hrrq->_lock);
	ata_qc_complete(qc);
}

/**
 * ipr_build_ata_ioadl64 - Build an ATA scatter/gather list
 * @ipr_cmd:	ipr command struct
 * @qc:		ATA queued command
 *
 **/
static void ipr_build_ata_ioadl64(struct ipr_cmnd *ipr_cmd,
				  struct ata_queued_cmd *qc)
{
	u32 ioadl_flags = 0;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl64_desc *ioadl64 = ipr_cmd->i.ata_ioadl.ioadl64;
	struct ipr_ioadl64_desc *last_ioadl64 = NULL;
	int len = qc->nbytes;
	struct scatterlist *sg;
	unsigned int si;
	dma_addr_t dma_addr = ipr_cmd->dma_addr;

	if (len == 0)
		return;

	if (qc->dma_dir == DMA_TO_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_WRITE;
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
	} else if (qc->dma_dir == DMA_FROM_DEVICE)
		ioadl_flags = IPR_IOADL_FLAGS_READ;

	ioarcb->data_transfer_length = cpu_to_be32(len);
	ioarcb->ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl64_desc) * ipr_cmd->dma_use_sg);
	ioarcb->u.sis64_addr_data.data_ioadl_addr =
		cpu_to_be64(dma_addr + offsetof(struct ipr_cmnd, i.ata_ioadl.ioadl64));

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		ioadl64->flags = cpu_to_be32(ioadl_flags);
		ioadl64->data_len = cpu_to_be32(sg_dma_len(sg));
		ioadl64->address = cpu_to_be64(sg_dma_address(sg));

		last_ioadl64 = ioadl64;
		ioadl64++;
	}

	if (likely(last_ioadl64))
		last_ioadl64->flags |= cpu_to_be32(IPR_IOADL_FLAGS_LAST);
}

/**
 * ipr_build_ata_ioadl - Build an ATA scatter/gather list
 * @ipr_cmd:	ipr command struct
 * @qc:		ATA queued command
 *
 **/
static void ipr_build_ata_ioadl(struct ipr_cmnd *ipr_cmd,
				struct ata_queued_cmd *qc)
{
	u32 ioadl_flags = 0;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->i.ioadl;
	struct ipr_ioadl_desc *last_ioadl = NULL;
	int len = qc->nbytes;
	struct scatterlist *sg;
	unsigned int si;

	if (len == 0)
		return;

	if (qc->dma_dir == DMA_TO_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_WRITE;
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
		ioarcb->data_transfer_length = cpu_to_be32(len);
		ioarcb->ioadl_len =
			cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
	} else if (qc->dma_dir == DMA_FROM_DEVICE) {
		ioadl_flags = IPR_IOADL_FLAGS_READ;
		ioarcb->read_data_transfer_length = cpu_to_be32(len);
		ioarcb->read_ioadl_len =
			cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
	}

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		ioadl->flags_and_data_len = cpu_to_be32(ioadl_flags | sg_dma_len(sg));
		ioadl->address = cpu_to_be32(sg_dma_address(sg));

		last_ioadl = ioadl;
		ioadl++;
	}

	if (likely(last_ioadl))
		last_ioadl->flags_and_data_len |= cpu_to_be32(IPR_IOADL_FLAGS_LAST);
}

/**
 * ipr_qc_defer - Get a free ipr_cmd
 * @qc:	queued command
 *
 * Return value:
 *	0 if success
 **/
static int ipr_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ipr_sata_port *sata_port = ap->private_data;
	struct ipr_ioa_cfg *ioa_cfg = sata_port->ioa_cfg;
	struct ipr_cmnd *ipr_cmd;
	struct ipr_hrr_queue *hrrq;
	int hrrq_id;

	hrrq_id = ipr_get_hrrq_index(ioa_cfg);
	hrrq = &ioa_cfg->hrrq[hrrq_id];

	qc->lldd_task = NULL;
	spin_lock(&hrrq->_lock);
	if (unlikely(hrrq->ioa_is_dead)) {
		spin_unlock(&hrrq->_lock);
		return 0;
	}

	if (unlikely(!hrrq->allow_cmds)) {
		spin_unlock(&hrrq->_lock);
		return ATA_DEFER_LINK;
	}

	ipr_cmd = __ipr_get_free_ipr_cmnd(hrrq);
	if (ipr_cmd == NULL) {
		spin_unlock(&hrrq->_lock);
		return ATA_DEFER_LINK;
	}

	qc->lldd_task = ipr_cmd;
	spin_unlock(&hrrq->_lock);
	return 0;
}

/**
 * ipr_qc_issue - Issue a SATA qc to a device
 * @qc:	queued command
 *
 * Return value:
 * 	0 if success
 **/
static unsigned int ipr_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ipr_sata_port *sata_port = ap->private_data;
	struct ipr_resource_entry *res = sata_port->res;
	struct ipr_ioa_cfg *ioa_cfg = sata_port->ioa_cfg;
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	struct ipr_ioarcb_ata_regs *regs;

	if (qc->lldd_task == NULL)
		ipr_qc_defer(qc);

	ipr_cmd = qc->lldd_task;
	if (ipr_cmd == NULL)
		return AC_ERR_SYSTEM;

	qc->lldd_task = NULL;
	spin_lock(&ipr_cmd->hrrq->_lock);
	if (unlikely(!ipr_cmd->hrrq->allow_cmds ||
			ipr_cmd->hrrq->ioa_is_dead)) {
		list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
		spin_unlock(&ipr_cmd->hrrq->_lock);
		return AC_ERR_SYSTEM;
	}

	ipr_init_ipr_cmnd(ipr_cmd, ipr_lock_and_done);
	ioarcb = &ipr_cmd->ioarcb;

	if (ioa_cfg->sis64) {
		regs = &ipr_cmd->i.ata_ioadl.regs;
		ioarcb->add_cmd_parms_offset = cpu_to_be16(sizeof(*ioarcb));
	} else
		regs = &ioarcb->u.add_data.u.regs;

	memset(regs, 0, sizeof(*regs));
	ioarcb->add_cmd_parms_len = cpu_to_be16(sizeof(*regs));

	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);
	ipr_cmd->qc = qc;
	ipr_cmd->done = ipr_sata_done;
	ipr_cmd->ioarcb.res_handle = res->res_handle;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_ATA_PASSTHRU;
	ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_NO_LINK_DESC;
	ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_NO_ULEN_CHK;
	ipr_cmd->dma_use_sg = qc->n_elem;

	if (ioa_cfg->sis64)
		ipr_build_ata_ioadl64(ipr_cmd, qc);
	else
		ipr_build_ata_ioadl(ipr_cmd, qc);

	regs->flags |= IPR_ATA_FLAG_STATUS_ON_GOOD_COMPLETION;
	ipr_copy_sata_tf(regs, &qc->tf);
	memcpy(ioarcb->cmd_pkt.cdb, qc->cdb, IPR_MAX_CDB_LEN);
	ipr_trc_hook(ipr_cmd, IPR_TRACE_START, IPR_GET_RES_PHYS_LOC(res));

	switch (qc->tf.protocol) {
	case ATA_PROT_NODATA:
	case ATA_PROT_PIO:
		break;

	case ATA_PROT_DMA:
		regs->flags |= IPR_ATA_FLAG_XFER_TYPE_DMA;
		break;

	case ATAPI_PROT_PIO:
	case ATAPI_PROT_NODATA:
		regs->flags |= IPR_ATA_FLAG_PACKET_CMD;
		break;

	case ATAPI_PROT_DMA:
		regs->flags |= IPR_ATA_FLAG_PACKET_CMD;
		regs->flags |= IPR_ATA_FLAG_XFER_TYPE_DMA;
		break;

	default:
		WARN_ON(1);
		spin_unlock(&ipr_cmd->hrrq->_lock);
		return AC_ERR_INVALID;
	}

	ipr_send_command(ipr_cmd);
	spin_unlock(&ipr_cmd->hrrq->_lock);

	return 0;
}

/**
 * ipr_qc_fill_rtf - Read result TF
 * @qc: ATA queued command
 *
 * Return value:
 * 	true
 **/
static bool ipr_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct ipr_sata_port *sata_port = qc->ap->private_data;
	struct ipr_ioasa_gata *g = &sata_port->ioasa;
	struct ata_taskfile *tf = &qc->result_tf;

	tf->feature = g->error;
	tf->nsect = g->nsect;
	tf->lbal = g->lbal;
	tf->lbam = g->lbam;
	tf->lbah = g->lbah;
	tf->device = g->device;
	tf->command = g->status;
	tf->hob_nsect = g->hob_nsect;
	tf->hob_lbal = g->hob_lbal;
	tf->hob_lbam = g->hob_lbam;
	tf->hob_lbah = g->hob_lbah;

	return true;
}

static struct ata_port_operations ipr_sata_ops = {
	.phy_reset = ipr_ata_phy_reset,
	.hardreset = ipr_sata_reset,
	.post_internal_cmd = ipr_ata_post_internal,
	.qc_prep = ata_noop_qc_prep,
	.qc_defer = ipr_qc_defer,
	.qc_issue = ipr_qc_issue,
	.qc_fill_rtf = ipr_qc_fill_rtf,
	.port_start = ata_sas_port_start,
	.port_stop = ata_sas_port_stop
};

static struct ata_port_info sata_port_info = {
	.flags		= ATA_FLAG_SATA | ATA_FLAG_PIO_DMA |
			  ATA_FLAG_SAS_HOST,
	.pio_mask	= ATA_PIO4_ONLY,
	.mwdma_mask	= ATA_MWDMA2,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ipr_sata_ops
};

#ifdef CONFIG_PPC_PSERIES
static const u16 ipr_blocked_processors[] = {
	PVR_NORTHSTAR,
	PVR_PULSAR,
	PVR_POWER4,
	PVR_ICESTAR,
	PVR_SSTAR,
	PVR_POWER4p,
	PVR_630,
	PVR_630p
};

/**
 * ipr_invalid_adapter - Determine if this adapter is supported on this hardware
 * @ioa_cfg:	ioa cfg struct
 *
 * Adapters that use Gemstone revision < 3.1 do not work reliably on
 * certain pSeries hardware. This function determines if the given
 * adapter is in one of these confgurations or not.
 *
 * Return value:
 * 	1 if adapter is not supported / 0 if adapter is supported
 **/
static int ipr_invalid_adapter(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	if ((ioa_cfg->type == 0x5702) && (ioa_cfg->pdev->revision < 4)) {
		for (i = 0; i < ARRAY_SIZE(ipr_blocked_processors); i++) {
			if (pvr_version_is(ipr_blocked_processors[i]))
				return 1;
		}
	}
	return 0;
}
#else
#define ipr_invalid_adapter(ioa_cfg) 0
#endif

/**
 * ipr_ioa_bringdown_done - IOA bring down completion.
 * @ipr_cmd:	ipr command struct
 *
 * This function processes the completion of an adapter bring down.
 * It wakes any reset sleepers.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioa_bringdown_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int i;

	ENTER;
	if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].removing_ioa) {
		ipr_trace;
		ioa_cfg->scsi_unblock = 1;
		schedule_work(&ioa_cfg->work_q);
	}

	ioa_cfg->in_reset_reload = 0;
	ioa_cfg->reset_retries = 0;
	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].ioa_is_dead = 1;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}
	wmb();

	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	wake_up_all(&ioa_cfg->reset_wait_q);
	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioa_reset_done - IOA reset completion.
 * @ipr_cmd:	ipr command struct
 *
 * This function processes the completion of an adapter reset.
 * It schedules any necessary mid-layer add/removes and
 * wakes any reset sleepers.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioa_reset_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res;
	int j;

	ENTER;
	ioa_cfg->in_reset_reload = 0;
	for (j = 0; j < ioa_cfg->hrrq_num; j++) {
		spin_lock(&ioa_cfg->hrrq[j]._lock);
		ioa_cfg->hrrq[j].allow_cmds = 1;
		spin_unlock(&ioa_cfg->hrrq[j]._lock);
	}
	wmb();
	ioa_cfg->reset_cmd = NULL;
	ioa_cfg->doorbell |= IPR_RUNTIME_RESET;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (res->add_to_ml || res->del_from_ml) {
			ipr_trace;
			break;
		}
	}
	schedule_work(&ioa_cfg->work_q);

	for (j = 0; j < IPR_NUM_HCAMS; j++) {
		list_del_init(&ioa_cfg->hostrcb[j]->queue);
		if (j < IPR_NUM_LOG_HCAMS)
			ipr_send_hcam(ioa_cfg,
				IPR_HCAM_CDB_OP_CODE_LOG_DATA,
				ioa_cfg->hostrcb[j]);
		else
			ipr_send_hcam(ioa_cfg,
				IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
				ioa_cfg->hostrcb[j]);
	}

	scsi_report_bus_reset(ioa_cfg->host, IPR_VSET_BUS);
	dev_info(&ioa_cfg->pdev->dev, "IOA initialized.\n");

	ioa_cfg->reset_retries = 0;
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	wake_up_all(&ioa_cfg->reset_wait_q);

	ioa_cfg->scsi_unblock = 1;
	schedule_work(&ioa_cfg->work_q);
	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_set_sup_dev_dflt - Initialize a Set Supported Device buffer
 * @supported_dev:	supported device struct
 * @vpids:			vendor product id struct
 *
 * Return value:
 * 	none
 **/
static void ipr_set_sup_dev_dflt(struct ipr_supported_device *supported_dev,
				 struct ipr_std_inq_vpids *vpids)
{
	memset(supported_dev, 0, sizeof(struct ipr_supported_device));
	memcpy(&supported_dev->vpids, vpids, sizeof(struct ipr_std_inq_vpids));
	supported_dev->num_records = 1;
	supported_dev->data_length =
		cpu_to_be16(sizeof(struct ipr_supported_device));
	supported_dev->reserved = 0;
}

/**
 * ipr_set_supported_devs - Send Set Supported Devices for a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Set Supported Devices to the adapter
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_set_supported_devs(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_supported_device *supp_dev = &ioa_cfg->vpd_cbs->supp_dev;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_resource_entry *res = ipr_cmd->u.res;

	ipr_cmd->job_step = ipr_ioa_reset_done;

	list_for_each_entry_continue(res, &ioa_cfg->used_res_q, queue) {
		if (!ipr_is_scsi_disk(res))
			continue;

		ipr_cmd->u.res = res;
		ipr_set_sup_dev_dflt(supp_dev, &res->std_inq_data.vpids);

		ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

		ioarcb->cmd_pkt.cdb[0] = IPR_SET_SUPPORTED_DEVICES;
		ioarcb->cmd_pkt.cdb[1] = IPR_SET_ALL_SUPPORTED_DEVICES;
		ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_supported_device) >> 8) & 0xff;
		ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_supported_device) & 0xff;

		ipr_init_ioadl(ipr_cmd,
			       ioa_cfg->vpd_cbs_dma +
				 offsetof(struct ipr_misc_cbs, supp_dev),
			       sizeof(struct ipr_supported_device),
			       IPR_IOADL_FLAGS_WRITE_LAST);

		ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout,
			   IPR_SET_SUP_DEVICE_TIMEOUT);

		if (!ioa_cfg->sis64)
			ipr_cmd->job_step = ipr_set_supported_devs;
		LEAVE;
		return IPR_RC_JOB_RETURN;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_get_mode_page - Locate specified mode page
 * @mode_pages:	mode page buffer
 * @page_code:	page code to find
 * @len:		minimum required length for mode page
 *
 * Return value:
 * 	pointer to mode page / NULL on failure
 **/
static void *ipr_get_mode_page(struct ipr_mode_pages *mode_pages,
			       u32 page_code, u32 len)
{
	struct ipr_mode_page_hdr *mode_hdr;
	u32 page_length;
	u32 length;

	if (!mode_pages || (mode_pages->hdr.length == 0))
		return NULL;

	length = (mode_pages->hdr.length + 1) - 4 - mode_pages->hdr.block_desc_len;
	mode_hdr = (struct ipr_mode_page_hdr *)
		(mode_pages->data + mode_pages->hdr.block_desc_len);

	while (length) {
		if (IPR_GET_MODE_PAGE_CODE(mode_hdr) == page_code) {
			if (mode_hdr->page_length >= (len - sizeof(struct ipr_mode_page_hdr)))
				return mode_hdr;
			break;
		} else {
			page_length = (sizeof(struct ipr_mode_page_hdr) +
				       mode_hdr->page_length);
			length -= page_length;
			mode_hdr = (struct ipr_mode_page_hdr *)
				((unsigned long)mode_hdr + page_length);
		}
	}
	return NULL;
}

/**
 * ipr_check_term_power - Check for term power errors
 * @ioa_cfg:	ioa config struct
 * @mode_pages:	IOAFP mode pages buffer
 *
 * Check the IOAFP's mode page 28 for term power errors
 *
 * Return value:
 * 	nothing
 **/
static void ipr_check_term_power(struct ipr_ioa_cfg *ioa_cfg,
				 struct ipr_mode_pages *mode_pages)
{
	int i;
	int entry_length;
	struct ipr_dev_bus_entry *bus;
	struct ipr_mode_page28 *mode_page;

	mode_page = ipr_get_mode_page(mode_pages, 0x28,
				      sizeof(struct ipr_mode_page28));

	entry_length = mode_page->entry_length;

	bus = mode_page->bus;

	for (i = 0; i < mode_page->num_entries; i++) {
		if (bus->flags & IPR_SCSI_ATTR_NO_TERM_PWR) {
			dev_err(&ioa_cfg->pdev->dev,
				"Term power is absent on scsi bus %d\n",
				bus->res_addr.bus);
		}

		bus = (struct ipr_dev_bus_entry *)((char *)bus + entry_length);
	}
}

/**
 * ipr_scsi_bus_speed_limit - Limit the SCSI speed based on SES table
 * @ioa_cfg:	ioa config struct
 *
 * Looks through the config table checking for SES devices. If
 * the SES device is in the SES table indicating a maximum SCSI
 * bus speed, the speed is limited for the bus.
 *
 * Return value:
 * 	none
 **/
static void ipr_scsi_bus_speed_limit(struct ipr_ioa_cfg *ioa_cfg)
{
	u32 max_xfer_rate;
	int i;

	for (i = 0; i < IPR_MAX_NUM_BUSES; i++) {
		max_xfer_rate = ipr_get_max_scsi_speed(ioa_cfg, i,
						       ioa_cfg->bus_attr[i].bus_width);

		if (max_xfer_rate < ioa_cfg->bus_attr[i].max_xfer_rate)
			ioa_cfg->bus_attr[i].max_xfer_rate = max_xfer_rate;
	}
}

/**
 * ipr_modify_ioafp_mode_page_28 - Modify IOAFP Mode Page 28
 * @ioa_cfg:	ioa config struct
 * @mode_pages:	mode page 28 buffer
 *
 * Updates mode page 28 based on driver configuration
 *
 * Return value:
 * 	none
 **/
static void ipr_modify_ioafp_mode_page_28(struct ipr_ioa_cfg *ioa_cfg,
					  struct ipr_mode_pages *mode_pages)
{
	int i, entry_length;
	struct ipr_dev_bus_entry *bus;
	struct ipr_bus_attributes *bus_attr;
	struct ipr_mode_page28 *mode_page;

	mode_page = ipr_get_mode_page(mode_pages, 0x28,
				      sizeof(struct ipr_mode_page28));

	entry_length = mode_page->entry_length;

	/* Loop for each device bus entry */
	for (i = 0, bus = mode_page->bus;
	     i < mode_page->num_entries;
	     i++, bus = (struct ipr_dev_bus_entry *)((u8 *)bus + entry_length)) {
		if (bus->res_addr.bus > IPR_MAX_NUM_BUSES) {
			dev_err(&ioa_cfg->pdev->dev,
				"Invalid resource address reported: 0x%08X\n",
				IPR_GET_PHYS_LOC(bus->res_addr));
			continue;
		}

		bus_attr = &ioa_cfg->bus_attr[i];
		bus->extended_reset_delay = IPR_EXTENDED_RESET_DELAY;
		bus->bus_width = bus_attr->bus_width;
		bus->max_xfer_rate = cpu_to_be32(bus_attr->max_xfer_rate);
		bus->flags &= ~IPR_SCSI_ATTR_QAS_MASK;
		if (bus_attr->qas_enabled)
			bus->flags |= IPR_SCSI_ATTR_ENABLE_QAS;
		else
			bus->flags |= IPR_SCSI_ATTR_DISABLE_QAS;
	}
}

/**
 * ipr_build_mode_select - Build a mode select command
 * @ipr_cmd:	ipr command struct
 * @res_handle:	resource handle to send command to
 * @parm:		Byte 2 of Mode Sense command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	none
 **/
static void ipr_build_mode_select(struct ipr_cmnd *ipr_cmd,
				  __be32 res_handle, u8 parm,
				  dma_addr_t dma_addr, u8 xfer_len)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res_handle;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ioarcb->cmd_pkt.flags_hi |= IPR_FLAGS_HI_WRITE_NOT_READ;
	ioarcb->cmd_pkt.cdb[0] = MODE_SELECT;
	ioarcb->cmd_pkt.cdb[1] = parm;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;

	ipr_init_ioadl(ipr_cmd, dma_addr, xfer_len, IPR_IOADL_FLAGS_WRITE_LAST);
}

/**
 * ipr_ioafp_mode_select_page28 - Issue Mode Select Page 28 to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function sets up the SCSI bus attributes and sends
 * a Mode Select for Page 28 to activate them.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_select_page28(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_mode_pages *mode_pages = &ioa_cfg->vpd_cbs->mode_pages;
	int length;

	ENTER;
	ipr_scsi_bus_speed_limit(ioa_cfg);
	ipr_check_term_power(ioa_cfg, mode_pages);
	ipr_modify_ioafp_mode_page_28(ioa_cfg, mode_pages);
	length = mode_pages->hdr.length + 1;
	mode_pages->hdr.length = 0;

	ipr_build_mode_select(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE), 0x11,
			      ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, mode_pages),
			      length);

	ipr_cmd->job_step = ipr_set_supported_devs;
	ipr_cmd->u.res = list_entry(ioa_cfg->used_res_q.next,
				    struct ipr_resource_entry, queue);
	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_build_mode_sense - Builds a mode sense command
 * @ipr_cmd:	ipr command struct
 * @res_handle:		resource entry struct
 * @parm:		Byte 2 of mode sense command
 * @dma_addr:	DMA address of mode sense buffer
 * @xfer_len:	Size of DMA buffer
 *
 * Return value:
 * 	none
 **/
static void ipr_build_mode_sense(struct ipr_cmnd *ipr_cmd,
				 __be32 res_handle,
				 u8 parm, dma_addr_t dma_addr, u8 xfer_len)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res_handle;
	ioarcb->cmd_pkt.cdb[0] = MODE_SENSE;
	ioarcb->cmd_pkt.cdb[2] = parm;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

	ipr_init_ioadl(ipr_cmd, dma_addr, xfer_len, IPR_IOADL_FLAGS_READ_LAST);
}

/**
 * ipr_reset_cmd_failed - Handle failure of IOA reset command
 * @ipr_cmd:	ipr command struct
 *
 * This function handles the failure of an IOA bringup command.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_cmd_failed(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	dev_err(&ioa_cfg->pdev->dev,
		"0x%02X failed with IOASC: 0x%08X\n",
		ipr_cmd->ioarcb.cmd_pkt.cdb[0], ioasc);

	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_mode_sense_failed - Handle failure of IOAFP mode sense
 * @ipr_cmd:	ipr command struct
 *
 * This function handles the failure of a Mode Sense to the IOAFP.
 * Some adapters do not handle all mode pages.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_mode_sense_failed(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	if (ioasc == IPR_IOASC_IR_INVALID_REQ_TYPE_OR_PKT) {
		ipr_cmd->job_step = ipr_set_supported_devs;
		ipr_cmd->u.res = list_entry(ioa_cfg->used_res_q.next,
					    struct ipr_resource_entry, queue);
		return IPR_RC_JOB_CONTINUE;
	}

	return ipr_reset_cmd_failed(ipr_cmd);
}

/**
 * ipr_ioafp_mode_sense_page28 - Issue Mode Sense Page 28 to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function send a Page 28 mode sense to the IOA to
 * retrieve SCSI bus attributes.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_sense_page28(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ipr_build_mode_sense(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE),
			     0x28, ioa_cfg->vpd_cbs_dma +
			     offsetof(struct ipr_misc_cbs, mode_pages),
			     sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_ioafp_mode_select_page28;
	ipr_cmd->job_step_failed = ipr_reset_mode_sense_failed;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_mode_select_page24 - Issue Mode Select to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function enables dual IOA RAID support if possible.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_select_page24(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_mode_pages *mode_pages = &ioa_cfg->vpd_cbs->mode_pages;
	struct ipr_mode_page24 *mode_page;
	int length;

	ENTER;
	mode_page = ipr_get_mode_page(mode_pages, 0x24,
				      sizeof(struct ipr_mode_page24));

	if (mode_page)
		mode_page->flags |= IPR_ENABLE_DUAL_IOA_AF;

	length = mode_pages->hdr.length + 1;
	mode_pages->hdr.length = 0;

	ipr_build_mode_select(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE), 0x11,
			      ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, mode_pages),
			      length);

	ipr_cmd->job_step = ipr_ioafp_mode_sense_page28;
	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_mode_sense_page24_failed - Handle failure of IOAFP mode sense
 * @ipr_cmd:	ipr command struct
 *
 * This function handles the failure of a Mode Sense to the IOAFP.
 * Some adapters do not handle all mode pages.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_mode_sense_page24_failed(struct ipr_cmnd *ipr_cmd)
{
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	if (ioasc == IPR_IOASC_IR_INVALID_REQ_TYPE_OR_PKT) {
		ipr_cmd->job_step = ipr_ioafp_mode_sense_page28;
		return IPR_RC_JOB_CONTINUE;
	}

	return ipr_reset_cmd_failed(ipr_cmd);
}

/**
 * ipr_ioafp_mode_sense_page24 - Issue Page 24 Mode Sense to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function send a mode sense to the IOA to retrieve
 * the IOA Advanced Function Control mode page.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_sense_page24(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ipr_build_mode_sense(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE),
			     0x24, ioa_cfg->vpd_cbs_dma +
			     offsetof(struct ipr_misc_cbs, mode_pages),
			     sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_ioafp_mode_select_page24;
	ipr_cmd->job_step_failed = ipr_reset_mode_sense_page24_failed;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_init_res_table - Initialize the resource table
 * @ipr_cmd:	ipr command struct
 *
 * This function looks through the existing resource table, comparing
 * it with the config table. This function will take care of old/new
 * devices and schedule adding/removing them from the mid-layer
 * as appropriate.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_init_res_table(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res, *temp;
	struct ipr_config_table_entry_wrapper cfgtew;
	int entries, found, flag, i;
	LIST_HEAD(old_res);

	ENTER;
	if (ioa_cfg->sis64)
		flag = ioa_cfg->u.cfg_table64->hdr64.flags;
	else
		flag = ioa_cfg->u.cfg_table->hdr.flags;

	if (flag & IPR_UCODE_DOWNLOAD_REQ)
		dev_err(&ioa_cfg->pdev->dev, "Microcode download required\n");

	list_for_each_entry_safe(res, temp, &ioa_cfg->used_res_q, queue)
		list_move_tail(&res->queue, &old_res);

	if (ioa_cfg->sis64)
		entries = be16_to_cpu(ioa_cfg->u.cfg_table64->hdr64.num_entries);
	else
		entries = ioa_cfg->u.cfg_table->hdr.num_entries;

	for (i = 0; i < entries; i++) {
		if (ioa_cfg->sis64)
			cfgtew.u.cfgte64 = &ioa_cfg->u.cfg_table64->dev[i];
		else
			cfgtew.u.cfgte = &ioa_cfg->u.cfg_table->dev[i];
		found = 0;

		list_for_each_entry_safe(res, temp, &old_res, queue) {
			if (ipr_is_same_device(res, &cfgtew)) {
				list_move_tail(&res->queue, &ioa_cfg->used_res_q);
				found = 1;
				break;
			}
		}

		if (!found) {
			if (list_empty(&ioa_cfg->free_res_q)) {
				dev_err(&ioa_cfg->pdev->dev, "Too many devices attached\n");
				break;
			}

			found = 1;
			res = list_entry(ioa_cfg->free_res_q.next,
					 struct ipr_resource_entry, queue);
			list_move_tail(&res->queue, &ioa_cfg->used_res_q);
			ipr_init_res_entry(res, &cfgtew);
			res->add_to_ml = 1;
		} else if (res->sdev && (ipr_is_vset_device(res) || ipr_is_scsi_disk(res)))
			res->sdev->allow_restart = 1;

		if (found)
			ipr_update_res_entry(res, &cfgtew);
	}

	list_for_each_entry_safe(res, temp, &old_res, queue) {
		if (res->sdev) {
			res->del_from_ml = 1;
			res->res_handle = IPR_INVALID_RES_HANDLE;
			list_move_tail(&res->queue, &ioa_cfg->used_res_q);
		}
	}

	list_for_each_entry_safe(res, temp, &old_res, queue) {
		ipr_clear_res_target(res);
		list_move_tail(&res->queue, &ioa_cfg->free_res_q);
	}

	if (ioa_cfg->dual_raid && ipr_dual_ioa_raid)
		ipr_cmd->job_step = ipr_ioafp_mode_sense_page24;
	else
		ipr_cmd->job_step = ipr_ioafp_mode_sense_page28;

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_ioafp_query_ioa_cfg - Send a Query IOA Config to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Query IOA Configuration command
 * to the adapter to retrieve the IOA configuration table.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_query_ioa_cfg(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_inquiry_page3 *ucode_vpd = &ioa_cfg->vpd_cbs->page3_data;
	struct ipr_inquiry_cap *cap = &ioa_cfg->vpd_cbs->cap;

	ENTER;
	if (cap->cap & IPR_CAP_DUAL_IOA_RAID)
		ioa_cfg->dual_raid = 1;
	dev_info(&ioa_cfg->pdev->dev, "Adapter firmware version: %02X%02X%02X%02X\n",
		 ucode_vpd->major_release, ucode_vpd->card_type,
		 ucode_vpd->minor_release[0], ucode_vpd->minor_release[1]);
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ioarcb->cmd_pkt.cdb[0] = IPR_QUERY_IOA_CONFIG;
	ioarcb->cmd_pkt.cdb[6] = (ioa_cfg->cfg_table_size >> 16) & 0xff;
	ioarcb->cmd_pkt.cdb[7] = (ioa_cfg->cfg_table_size >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] = ioa_cfg->cfg_table_size & 0xff;

	ipr_init_ioadl(ipr_cmd, ioa_cfg->cfg_table_dma, ioa_cfg->cfg_table_size,
		       IPR_IOADL_FLAGS_READ_LAST);

	ipr_cmd->job_step = ipr_init_res_table;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

static int ipr_ioa_service_action_failed(struct ipr_cmnd *ipr_cmd)
{
	u32 ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

	if (ioasc == IPR_IOASC_IR_INVALID_REQ_TYPE_OR_PKT)
		return IPR_RC_JOB_CONTINUE;

	return ipr_reset_cmd_failed(ipr_cmd);
}

static void ipr_build_ioa_service_action(struct ipr_cmnd *ipr_cmd,
					 __be32 res_handle, u8 sa_code)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res_handle;
	ioarcb->cmd_pkt.cdb[0] = IPR_IOA_SERVICE_ACTION;
	ioarcb->cmd_pkt.cdb[1] = sa_code;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
}

/**
 * ipr_ioafp_set_caching_parameters - Issue Set Cache parameters service
 * action
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 *	none
 **/
static int ipr_ioafp_set_caching_parameters(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_inquiry_pageC4 *pageC4 = &ioa_cfg->vpd_cbs->pageC4_data;

	ENTER;

	ipr_cmd->job_step = ipr_ioafp_query_ioa_cfg;

	if (pageC4->cache_cap[0] & IPR_CAP_SYNC_CACHE) {
		ipr_build_ioa_service_action(ipr_cmd,
					     cpu_to_be32(IPR_IOA_RES_HANDLE),
					     IPR_IOA_SA_CHANGE_CACHE_PARAMS);

		ioarcb->cmd_pkt.cdb[2] = 0x40;

		ipr_cmd->job_step_failed = ipr_ioa_service_action_failed;
		ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout,
			   IPR_SET_SUP_DEVICE_TIMEOUT);

		LEAVE;
		return IPR_RC_JOB_RETURN;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_ioafp_inquiry - Send an Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 * @flags:	flags to send
 * @page:	page to inquire
 * @dma_addr:	DMA address
 * @xfer_len:	transfer data length
 *
 * This utility function sends an inquiry to the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_ioafp_inquiry(struct ipr_cmnd *ipr_cmd, u8 flags, u8 page,
			      dma_addr_t dma_addr, u8 xfer_len)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ENTER;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ioarcb->cmd_pkt.cdb[0] = INQUIRY;
	ioarcb->cmd_pkt.cdb[1] = flags;
	ioarcb->cmd_pkt.cdb[2] = page;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;

	ipr_init_ioadl(ipr_cmd, dma_addr, xfer_len, IPR_IOADL_FLAGS_READ_LAST);

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);
	LEAVE;
}

/**
 * ipr_inquiry_page_supported - Is the given inquiry page supported
 * @page0:		inquiry page 0 buffer
 * @page:		page code.
 *
 * This function determines if the specified inquiry page is supported.
 *
 * Return value:
 *	1 if page is supported / 0 if not
 **/
static int ipr_inquiry_page_supported(struct ipr_inquiry_page0 *page0, u8 page)
{
	int i;

	for (i = 0; i < min_t(u8, page0->len, IPR_INQUIRY_PAGE0_ENTRIES); i++)
		if (page0->page[i] == page)
			return 1;

	return 0;
}

/**
 * ipr_ioafp_pageC4_inquiry - Send a Page 0xC4 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 0xC4 inquiry to the adapter
 * to retrieve software VPD information.
 *
 * Return value:
 *	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_pageC4_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_inquiry_page0 *page0 = &ioa_cfg->vpd_cbs->page0_data;
	struct ipr_inquiry_pageC4 *pageC4 = &ioa_cfg->vpd_cbs->pageC4_data;

	ENTER;
	ipr_cmd->job_step = ipr_ioafp_set_caching_parameters;
	memset(pageC4, 0, sizeof(*pageC4));

	if (ipr_inquiry_page_supported(page0, 0xC4)) {
		ipr_ioafp_inquiry(ipr_cmd, 1, 0xC4,
				  (ioa_cfg->vpd_cbs_dma
				   + offsetof(struct ipr_misc_cbs,
					      pageC4_data)),
				  sizeof(struct ipr_inquiry_pageC4));
		return IPR_RC_JOB_RETURN;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_ioafp_cap_inquiry - Send a Page 0xD0 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 0xD0 inquiry to the adapter
 * to retrieve adapter capabilities.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_cap_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_inquiry_page0 *page0 = &ioa_cfg->vpd_cbs->page0_data;
	struct ipr_inquiry_cap *cap = &ioa_cfg->vpd_cbs->cap;

	ENTER;
	ipr_cmd->job_step = ipr_ioafp_pageC4_inquiry;
	memset(cap, 0, sizeof(*cap));

	if (ipr_inquiry_page_supported(page0, 0xD0)) {
		ipr_ioafp_inquiry(ipr_cmd, 1, 0xD0,
				  ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, cap),
				  sizeof(struct ipr_inquiry_cap));
		return IPR_RC_JOB_RETURN;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_ioafp_page3_inquiry - Send a Page 3 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 3 inquiry to the adapter
 * to retrieve software VPD information.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_page3_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	ipr_cmd->job_step = ipr_ioafp_cap_inquiry;

	ipr_ioafp_inquiry(ipr_cmd, 1, 3,
			  ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, page3_data),
			  sizeof(struct ipr_inquiry_page3));

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_page0_inquiry - Send a Page 0 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 0 inquiry to the adapter
 * to retrieve supported inquiry pages.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_page0_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	char type[5];

	ENTER;

	/* Grab the type out of the VPD and store it away */
	memcpy(type, ioa_cfg->vpd_cbs->ioa_vpd.std_inq_data.vpids.product_id, 4);
	type[4] = '\0';
	ioa_cfg->type = simple_strtoul((char *)type, NULL, 16);

	if (ipr_invalid_adapter(ioa_cfg)) {
		dev_err(&ioa_cfg->pdev->dev,
			"Adapter not supported in this hardware configuration.\n");

		if (!ipr_testmode) {
			ioa_cfg->reset_retries += IPR_NUM_RESET_RELOAD_RETRIES;
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
			list_add_tail(&ipr_cmd->queue,
					&ioa_cfg->hrrq->hrrq_free_q);
			return IPR_RC_JOB_RETURN;
		}
	}

	ipr_cmd->job_step = ipr_ioafp_page3_inquiry;

	ipr_ioafp_inquiry(ipr_cmd, 1, 0,
			  ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, page0_data),
			  sizeof(struct ipr_inquiry_page0));

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_std_inquiry - Send a Standard Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a standard inquiry to the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_std_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ipr_cmd->job_step = ipr_ioafp_page0_inquiry;

	ipr_ioafp_inquiry(ipr_cmd, 0, 0,
			  ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_misc_cbs, ioa_vpd),
			  sizeof(struct ipr_ioa_vpd));

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_identify_hrrq - Send Identify Host RRQ.
 * @ipr_cmd:	ipr command struct
 *
 * This function send an Identify Host Request Response Queue
 * command to establish the HRRQ with the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_identify_hrrq(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_hrr_queue *hrrq;

	ENTER;
	ipr_cmd->job_step = ipr_ioafp_std_inquiry;
	if (ioa_cfg->identify_hrrq_index == 0)
		dev_info(&ioa_cfg->pdev->dev, "Starting IOA initialization sequence.\n");

	if (ioa_cfg->identify_hrrq_index < ioa_cfg->hrrq_num) {
		hrrq = &ioa_cfg->hrrq[ioa_cfg->identify_hrrq_index];

		ioarcb->cmd_pkt.cdb[0] = IPR_ID_HOST_RR_Q;
		ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
		if (ioa_cfg->sis64)
			ioarcb->cmd_pkt.cdb[1] = 0x1;

		if (ioa_cfg->nvectors == 1)
			ioarcb->cmd_pkt.cdb[1] &= ~IPR_ID_HRRQ_SELE_ENABLE;
		else
			ioarcb->cmd_pkt.cdb[1] |= IPR_ID_HRRQ_SELE_ENABLE;

		ioarcb->cmd_pkt.cdb[2] =
			((u64) hrrq->host_rrq_dma >> 24) & 0xff;
		ioarcb->cmd_pkt.cdb[3] =
			((u64) hrrq->host_rrq_dma >> 16) & 0xff;
		ioarcb->cmd_pkt.cdb[4] =
			((u64) hrrq->host_rrq_dma >> 8) & 0xff;
		ioarcb->cmd_pkt.cdb[5] =
			((u64) hrrq->host_rrq_dma) & 0xff;
		ioarcb->cmd_pkt.cdb[7] =
			((sizeof(u32) * hrrq->size) >> 8) & 0xff;
		ioarcb->cmd_pkt.cdb[8] =
			(sizeof(u32) * hrrq->size) & 0xff;

		if (ioarcb->cmd_pkt.cdb[1] & IPR_ID_HRRQ_SELE_ENABLE)
			ioarcb->cmd_pkt.cdb[9] =
					ioa_cfg->identify_hrrq_index;

		if (ioa_cfg->sis64) {
			ioarcb->cmd_pkt.cdb[10] =
				((u64) hrrq->host_rrq_dma >> 56) & 0xff;
			ioarcb->cmd_pkt.cdb[11] =
				((u64) hrrq->host_rrq_dma >> 48) & 0xff;
			ioarcb->cmd_pkt.cdb[12] =
				((u64) hrrq->host_rrq_dma >> 40) & 0xff;
			ioarcb->cmd_pkt.cdb[13] =
				((u64) hrrq->host_rrq_dma >> 32) & 0xff;
		}

		if (ioarcb->cmd_pkt.cdb[1] & IPR_ID_HRRQ_SELE_ENABLE)
			ioarcb->cmd_pkt.cdb[14] =
					ioa_cfg->identify_hrrq_index;

		ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout,
			   IPR_INTERNAL_TIMEOUT);

		if (++ioa_cfg->identify_hrrq_index < ioa_cfg->hrrq_num)
			ipr_cmd->job_step = ipr_ioafp_identify_hrrq;

		LEAVE;
		return IPR_RC_JOB_RETURN;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_timer_done - Adapter reset timer function
 * @t: Timer context used to fetch ipr command struct
 *
 * Description: This function is used in adapter reset processing
 * for timing events. If the reset_cmd pointer in the IOA
 * config struct is not this adapter's we are doing nested
 * resets and fail_all_ops will take care of freeing the
 * command block.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_timer_done(struct timer_list *t)
{
	struct ipr_cmnd *ipr_cmd = from_timer(ipr_cmd, t, timer);
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (ioa_cfg->reset_cmd == ipr_cmd) {
		list_del(&ipr_cmd->queue);
		ipr_cmd->done(ipr_cmd);
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
}

/**
 * ipr_reset_start_timer - Start a timer for adapter reset job
 * @ipr_cmd:	ipr command struct
 * @timeout:	timeout value
 *
 * Description: This function is used in adapter reset processing
 * for timing events. If the reset_cmd pointer in the IOA
 * config struct is not this adapter's we are doing nested
 * resets and fail_all_ops will take care of freeing the
 * command block.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_start_timer(struct ipr_cmnd *ipr_cmd,
				  unsigned long timeout)
{

	ENTER;
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);
	ipr_cmd->done = ipr_reset_ioa_job;

	ipr_cmd->timer.expires = jiffies + timeout;
	ipr_cmd->timer.function = ipr_reset_timer_done;
	add_timer(&ipr_cmd->timer);
}

/**
 * ipr_init_ioa_mem - Initialize ioa_cfg control block
 * @ioa_cfg:	ioa cfg struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_init_ioa_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_hrr_queue *hrrq;

	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		memset(hrrq->host_rrq, 0, sizeof(u32) * hrrq->size);

		/* Initialize Host RRQ pointers */
		hrrq->hrrq_start = hrrq->host_rrq;
		hrrq->hrrq_end = &hrrq->host_rrq[hrrq->size - 1];
		hrrq->hrrq_curr = hrrq->hrrq_start;
		hrrq->toggle_bit = 1;
		spin_unlock(&hrrq->_lock);
	}
	wmb();

	ioa_cfg->identify_hrrq_index = 0;
	if (ioa_cfg->hrrq_num == 1)
		atomic_set(&ioa_cfg->hrrq_index, 0);
	else
		atomic_set(&ioa_cfg->hrrq_index, 1);

	/* Zero out config table */
	memset(ioa_cfg->u.cfg_table, 0, ioa_cfg->cfg_table_size);
}

/**
 * ipr_reset_next_stage - Process IPL stage change based on feedback register.
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_next_stage(struct ipr_cmnd *ipr_cmd)
{
	unsigned long stage, stage_time;
	u32 feedback;
	volatile u32 int_reg;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u64 maskval = 0;

	feedback = readl(ioa_cfg->regs.init_feedback_reg);
	stage = feedback & IPR_IPL_INIT_STAGE_MASK;
	stage_time = feedback & IPR_IPL_INIT_STAGE_TIME_MASK;

	ipr_dbg("IPL stage = 0x%lx, IPL stage time = %ld\n", stage, stage_time);

	/* sanity check the stage_time value */
	if (stage_time == 0)
		stage_time = IPR_IPL_INIT_DEFAULT_STAGE_TIME;
	else if (stage_time < IPR_IPL_INIT_MIN_STAGE_TIME)
		stage_time = IPR_IPL_INIT_MIN_STAGE_TIME;
	else if (stage_time > IPR_LONG_OPERATIONAL_TIMEOUT)
		stage_time = IPR_LONG_OPERATIONAL_TIMEOUT;

	if (stage == IPR_IPL_INIT_STAGE_UNKNOWN) {
		writel(IPR_PCII_IPL_STAGE_CHANGE, ioa_cfg->regs.set_interrupt_mask_reg);
		int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);
		stage_time = ioa_cfg->transop_timeout;
		ipr_cmd->job_step = ipr_ioafp_identify_hrrq;
	} else if (stage == IPR_IPL_INIT_STAGE_TRANSOP) {
		int_reg = readl(ioa_cfg->regs.sense_interrupt_reg32);
		if (int_reg & IPR_PCII_IOA_TRANS_TO_OPER) {
			ipr_cmd->job_step = ipr_ioafp_identify_hrrq;
			maskval = IPR_PCII_IPL_STAGE_CHANGE;
			maskval = (maskval << 32) | IPR_PCII_IOA_TRANS_TO_OPER;
			writeq(maskval, ioa_cfg->regs.set_interrupt_mask_reg);
			int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);
			return IPR_RC_JOB_CONTINUE;
		}
	}

	ipr_cmd->timer.expires = jiffies + stage_time * HZ;
	ipr_cmd->timer.function = ipr_oper_timeout;
	ipr_cmd->done = ipr_reset_ioa_job;
	add_timer(&ipr_cmd->timer);

	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_enable_ioa - Enable the IOA following a reset.
 * @ipr_cmd:	ipr command struct
 *
 * This function reinitializes some control blocks and
 * enables destructive diagnostics on the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_enable_ioa(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	volatile u32 int_reg;
	volatile u64 maskval;
	int i;

	ENTER;
	ipr_cmd->job_step = ipr_ioafp_identify_hrrq;
	ipr_init_ioa_mem(ioa_cfg);

	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].allow_interrupts = 1;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}
	if (ioa_cfg->sis64) {
		/* Set the adapter to the correct endian mode. */
		writel(IPR_ENDIAN_SWAP_KEY, ioa_cfg->regs.endian_swap_reg);
		int_reg = readl(ioa_cfg->regs.endian_swap_reg);
	}

	int_reg = readl(ioa_cfg->regs.sense_interrupt_reg32);

	if (int_reg & IPR_PCII_IOA_TRANS_TO_OPER) {
		writel((IPR_PCII_ERROR_INTERRUPTS | IPR_PCII_HRRQ_UPDATED),
		       ioa_cfg->regs.clr_interrupt_mask_reg32);
		int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);
		return IPR_RC_JOB_CONTINUE;
	}

	/* Enable destructive diagnostics on IOA */
	writel(ioa_cfg->doorbell, ioa_cfg->regs.set_uproc_interrupt_reg32);

	if (ioa_cfg->sis64) {
		maskval = IPR_PCII_IPL_STAGE_CHANGE;
		maskval = (maskval << 32) | IPR_PCII_OPER_INTERRUPTS;
		writeq(maskval, ioa_cfg->regs.clr_interrupt_mask_reg);
	} else
		writel(IPR_PCII_OPER_INTERRUPTS, ioa_cfg->regs.clr_interrupt_mask_reg32);

	int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);

	dev_info(&ioa_cfg->pdev->dev, "Initializing IOA.\n");

	if (ioa_cfg->sis64) {
		ipr_cmd->job_step = ipr_reset_next_stage;
		return IPR_RC_JOB_CONTINUE;
	}

	ipr_cmd->timer.expires = jiffies + (ioa_cfg->transop_timeout * HZ);
	ipr_cmd->timer.function = ipr_oper_timeout;
	ipr_cmd->done = ipr_reset_ioa_job;
	add_timer(&ipr_cmd->timer);
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_wait_for_dump - Wait for a dump to timeout.
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked when an adapter dump has run out
 * of processing time.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_wait_for_dump(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	if (ioa_cfg->sdt_state == GET_DUMP)
		ioa_cfg->sdt_state = WAIT_FOR_DUMP;
	else if (ioa_cfg->sdt_state == READ_DUMP)
		ioa_cfg->sdt_state = ABORT_DUMP;

	ioa_cfg->dump_timeout = 1;
	ipr_cmd->job_step = ipr_reset_alert;

	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_unit_check_no_data - Log a unit check/no data error log
 * @ioa_cfg:		ioa config struct
 *
 * Logs an error indicating the adapter unit checked, but for some
 * reason, we were unable to fetch the unit check buffer.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_unit_check_no_data(struct ipr_ioa_cfg *ioa_cfg)
{
	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev, "IOA unit check with no data\n");
}

/**
 * ipr_get_unit_check_buffer - Get the unit check buffer from the IOA
 * @ioa_cfg:		ioa config struct
 *
 * Fetches the unit check buffer from the adapter by clocking the data
 * through the mailbox register.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_get_unit_check_buffer(struct ipr_ioa_cfg *ioa_cfg)
{
	unsigned long mailbox;
	struct ipr_hostrcb *hostrcb;
	struct ipr_uc_sdt sdt;
	int rc, length;
	u32 ioasc;

	mailbox = readl(ioa_cfg->ioa_mailbox);

	if (!ioa_cfg->sis64 && !ipr_sdt_is_fmt2(mailbox)) {
		ipr_unit_check_no_data(ioa_cfg);
		return;
	}

	memset(&sdt, 0, sizeof(struct ipr_uc_sdt));
	rc = ipr_get_ldump_data_section(ioa_cfg, mailbox, (__be32 *) &sdt,
					(sizeof(struct ipr_uc_sdt)) / sizeof(__be32));

	if (rc || !(sdt.entry[0].flags & IPR_SDT_VALID_ENTRY) ||
	    ((be32_to_cpu(sdt.hdr.state) != IPR_FMT3_SDT_READY_TO_USE) &&
	    (be32_to_cpu(sdt.hdr.state) != IPR_FMT2_SDT_READY_TO_USE))) {
		ipr_unit_check_no_data(ioa_cfg);
		return;
	}

	/* Find length of the first sdt entry (UC buffer) */
	if (be32_to_cpu(sdt.hdr.state) == IPR_FMT3_SDT_READY_TO_USE)
		length = be32_to_cpu(sdt.entry[0].end_token);
	else
		length = (be32_to_cpu(sdt.entry[0].end_token) -
			  be32_to_cpu(sdt.entry[0].start_token)) &
			  IPR_FMT2_MBX_ADDR_MASK;

	hostrcb = list_entry(ioa_cfg->hostrcb_free_q.next,
			     struct ipr_hostrcb, queue);
	list_del_init(&hostrcb->queue);
	memset(&hostrcb->hcam, 0, sizeof(hostrcb->hcam));

	rc = ipr_get_ldump_data_section(ioa_cfg,
					be32_to_cpu(sdt.entry[0].start_token),
					(__be32 *)&hostrcb->hcam,
					min(length, (int)sizeof(hostrcb->hcam)) / sizeof(__be32));

	if (!rc) {
		ipr_handle_log_data(ioa_cfg, hostrcb);
		ioasc = be32_to_cpu(hostrcb->hcam.u.error.fd_ioasc);
		if (ioasc == IPR_IOASC_NR_IOA_RESET_REQUIRED &&
		    ioa_cfg->sdt_state == GET_DUMP)
			ioa_cfg->sdt_state = WAIT_FOR_DUMP;
	} else
		ipr_unit_check_no_data(ioa_cfg);

	list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
}

/**
 * ipr_reset_get_unit_check_job - Call to get the unit check buffer.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function will call to get the unit check buffer.
 *
 * Return value:
 *	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_get_unit_check_job(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ioa_cfg->ioa_unit_checked = 0;
	ipr_get_unit_check_buffer(ioa_cfg);
	ipr_cmd->job_step = ipr_reset_alert;
	ipr_reset_start_timer(ipr_cmd, 0);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

static int ipr_dump_mailbox_wait(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	if (ioa_cfg->sdt_state != GET_DUMP)
		return IPR_RC_JOB_RETURN;

	if (!ioa_cfg->sis64 || !ipr_cmd->u.time_left ||
	    (readl(ioa_cfg->regs.sense_interrupt_reg) &
	     IPR_PCII_MAILBOX_STABLE)) {

		if (!ipr_cmd->u.time_left)
			dev_err(&ioa_cfg->pdev->dev,
				"Timed out waiting for Mailbox register.\n");

		ioa_cfg->sdt_state = READ_DUMP;
		ioa_cfg->dump_timeout = 0;
		if (ioa_cfg->sis64)
			ipr_reset_start_timer(ipr_cmd, IPR_SIS64_DUMP_TIMEOUT);
		else
			ipr_reset_start_timer(ipr_cmd, IPR_SIS32_DUMP_TIMEOUT);
		ipr_cmd->job_step = ipr_reset_wait_for_dump;
		schedule_work(&ioa_cfg->work_q);

	} else {
		ipr_cmd->u.time_left -= IPR_CHECK_FOR_RESET_TIMEOUT;
		ipr_reset_start_timer(ipr_cmd,
				      IPR_CHECK_FOR_RESET_TIMEOUT);
	}

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_restore_cfg_space - Restore PCI config space.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function restores the saved PCI config space of
 * the adapter, fails all outstanding ops back to the callers, and
 * fetches the dump/unit check if applicable to this reset.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_restore_cfg_space(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ioa_cfg->pdev->state_saved = true;
	pci_restore_state(ioa_cfg->pdev);

	if (ipr_set_pcix_cmd_reg(ioa_cfg)) {
		ipr_cmd->s.ioasa.hdr.ioasc = cpu_to_be32(IPR_IOASC_PCI_ACCESS_ERROR);
		return IPR_RC_JOB_CONTINUE;
	}

	ipr_fail_all_ops(ioa_cfg);

	if (ioa_cfg->sis64) {
		/* Set the adapter to the correct endian mode. */
		writel(IPR_ENDIAN_SWAP_KEY, ioa_cfg->regs.endian_swap_reg);
		readl(ioa_cfg->regs.endian_swap_reg);
	}

	if (ioa_cfg->ioa_unit_checked) {
		if (ioa_cfg->sis64) {
			ipr_cmd->job_step = ipr_reset_get_unit_check_job;
			ipr_reset_start_timer(ipr_cmd, IPR_DUMP_DELAY_TIMEOUT);
			return IPR_RC_JOB_RETURN;
		} else {
			ioa_cfg->ioa_unit_checked = 0;
			ipr_get_unit_check_buffer(ioa_cfg);
			ipr_cmd->job_step = ipr_reset_alert;
			ipr_reset_start_timer(ipr_cmd, 0);
			return IPR_RC_JOB_RETURN;
		}
	}

	if (ioa_cfg->in_ioa_bringdown) {
		ipr_cmd->job_step = ipr_ioa_bringdown_done;
	} else if (ioa_cfg->sdt_state == GET_DUMP) {
		ipr_cmd->job_step = ipr_dump_mailbox_wait;
		ipr_cmd->u.time_left = IPR_WAIT_FOR_MAILBOX;
	} else {
		ipr_cmd->job_step = ipr_reset_enable_ioa;
	}

	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_bist_done - BIST has completed on the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * Description: Unblock config space and resume the reset process.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_bist_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	if (ioa_cfg->cfg_locked)
		pci_cfg_access_unlock(ioa_cfg->pdev);
	ioa_cfg->cfg_locked = 0;
	ipr_cmd->job_step = ipr_reset_restore_cfg_space;
	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_start_bist - Run BIST on the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function runs BIST on the adapter, then delays 2 seconds.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_start_bist(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc = PCIBIOS_SUCCESSFUL;

	ENTER;
	if (ioa_cfg->ipr_chip->bist_method == IPR_MMIO)
		writel(IPR_UPROCI_SIS64_START_BIST,
		       ioa_cfg->regs.set_uproc_interrupt_reg32);
	else
		rc = pci_write_config_byte(ioa_cfg->pdev, PCI_BIST, PCI_BIST_START);

	if (rc == PCIBIOS_SUCCESSFUL) {
		ipr_cmd->job_step = ipr_reset_bist_done;
		ipr_reset_start_timer(ipr_cmd, IPR_WAIT_FOR_BIST_TIMEOUT);
		rc = IPR_RC_JOB_RETURN;
	} else {
		if (ioa_cfg->cfg_locked)
			pci_cfg_access_unlock(ipr_cmd->ioa_cfg->pdev);
		ioa_cfg->cfg_locked = 0;
		ipr_cmd->s.ioasa.hdr.ioasc = cpu_to_be32(IPR_IOASC_PCI_ACCESS_ERROR);
		rc = IPR_RC_JOB_CONTINUE;
	}

	LEAVE;
	return rc;
}

/**
 * ipr_reset_slot_reset_done - Clear PCI reset to the adapter
 * @ipr_cmd:	ipr command struct
 *
 * Description: This clears PCI reset to the adapter and delays two seconds.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_slot_reset_done(struct ipr_cmnd *ipr_cmd)
{
	ENTER;
	ipr_cmd->job_step = ipr_reset_bist_done;
	ipr_reset_start_timer(ipr_cmd, IPR_WAIT_FOR_BIST_TIMEOUT);
	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_reset_work - Pulse a PCIe fundamental reset
 * @work:	work struct
 *
 * Description: This pulses warm reset to a slot.
 *
 **/
static void ipr_reset_reset_work(struct work_struct *work)
{
	struct ipr_cmnd *ipr_cmd = container_of(work, struct ipr_cmnd, work);
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct pci_dev *pdev = ioa_cfg->pdev;
	unsigned long lock_flags = 0;

	ENTER;
	pci_set_pcie_reset_state(pdev, pcie_warm_reset);
	msleep(jiffies_to_msecs(IPR_PCI_RESET_TIMEOUT));
	pci_set_pcie_reset_state(pdev, pcie_deassert_reset);

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ioa_cfg->reset_cmd == ipr_cmd)
		ipr_reset_ioa_job(ipr_cmd);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	LEAVE;
}

/**
 * ipr_reset_slot_reset - Reset the PCI slot of the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This asserts PCI reset to the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_slot_reset(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	INIT_WORK(&ipr_cmd->work, ipr_reset_reset_work);
	queue_work(ioa_cfg->reset_work_q, &ipr_cmd->work);
	ipr_cmd->job_step = ipr_reset_slot_reset_done;
	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_block_config_access_wait - Wait for permission to block config access
 * @ipr_cmd:	ipr command struct
 *
 * Description: This attempts to block config access to the IOA.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_block_config_access_wait(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc = IPR_RC_JOB_CONTINUE;

	if (pci_cfg_access_trylock(ioa_cfg->pdev)) {
		ioa_cfg->cfg_locked = 1;
		ipr_cmd->job_step = ioa_cfg->reset;
	} else {
		if (ipr_cmd->u.time_left) {
			rc = IPR_RC_JOB_RETURN;
			ipr_cmd->u.time_left -= IPR_CHECK_FOR_RESET_TIMEOUT;
			ipr_reset_start_timer(ipr_cmd,
					      IPR_CHECK_FOR_RESET_TIMEOUT);
		} else {
			ipr_cmd->job_step = ioa_cfg->reset;
			dev_err(&ioa_cfg->pdev->dev,
				"Timed out waiting to lock config access. Resetting anyway.\n");
		}
	}

	return rc;
}

/**
 * ipr_reset_block_config_access - Block config access to the IOA
 * @ipr_cmd:	ipr command struct
 *
 * Description: This attempts to block config access to the IOA
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_block_config_access(struct ipr_cmnd *ipr_cmd)
{
	ipr_cmd->ioa_cfg->cfg_locked = 0;
	ipr_cmd->job_step = ipr_reset_block_config_access_wait;
	ipr_cmd->u.time_left = IPR_WAIT_FOR_RESET_TIMEOUT;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_allowed - Query whether or not IOA can be reset
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 if reset not allowed / non-zero if reset is allowed
 **/
static int ipr_reset_allowed(struct ipr_ioa_cfg *ioa_cfg)
{
	volatile u32 temp_reg;

	temp_reg = readl(ioa_cfg->regs.sense_interrupt_reg);
	return ((temp_reg & IPR_PCII_CRITICAL_OPERATION) == 0);
}

/**
 * ipr_reset_wait_to_start_bist - Wait for permission to reset IOA.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function waits for adapter permission to run BIST,
 * then runs BIST. If the adapter does not give permission after a
 * reasonable time, we will reset the adapter anyway. The impact of
 * resetting the adapter without warning the adapter is the risk of
 * losing the persistent error log on the adapter. If the adapter is
 * reset while it is writing to the flash on the adapter, the flash
 * segment will have bad ECC and be zeroed.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_wait_to_start_bist(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc = IPR_RC_JOB_RETURN;

	if (!ipr_reset_allowed(ioa_cfg) && ipr_cmd->u.time_left) {
		ipr_cmd->u.time_left -= IPR_CHECK_FOR_RESET_TIMEOUT;
		ipr_reset_start_timer(ipr_cmd, IPR_CHECK_FOR_RESET_TIMEOUT);
	} else {
		ipr_cmd->job_step = ipr_reset_block_config_access;
		rc = IPR_RC_JOB_CONTINUE;
	}

	return rc;
}

/**
 * ipr_reset_alert - Alert the adapter of a pending reset
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function alerts the adapter that it will be reset.
 * If memory space is not currently enabled, proceed directly
 * to running BIST on the adapter. The timer must always be started
 * so we guarantee we do not run BIST from ipr_isr.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_alert(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u16 cmd_reg;
	int rc;

	ENTER;
	rc = pci_read_config_word(ioa_cfg->pdev, PCI_COMMAND, &cmd_reg);

	if ((rc == PCIBIOS_SUCCESSFUL) && (cmd_reg & PCI_COMMAND_MEMORY)) {
		ipr_mask_and_clear_interrupts(ioa_cfg, ~0);
		writel(IPR_UPROCI_RESET_ALERT, ioa_cfg->regs.set_uproc_interrupt_reg32);
		ipr_cmd->job_step = ipr_reset_wait_to_start_bist;
	} else {
		ipr_cmd->job_step = ipr_reset_block_config_access;
	}

	ipr_cmd->u.time_left = IPR_WAIT_FOR_RESET_TIMEOUT;
	ipr_reset_start_timer(ipr_cmd, IPR_CHECK_FOR_RESET_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_quiesce_done - Complete IOA disconnect
 * @ipr_cmd:	ipr command struct
 *
 * Description: Freeze the adapter to complete quiesce processing
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_quiesce_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;
	ipr_cmd->job_step = ipr_ioa_bringdown_done;
	ipr_mask_and_clear_interrupts(ioa_cfg, ~IPR_PCII_IOA_TRANS_TO_OPER);
	LEAVE;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_cancel_hcam_done - Check for outstanding commands
 * @ipr_cmd:	ipr command struct
 *
 * Description: Ensure nothing is outstanding to the IOA and
 *			proceed with IOA disconnect. Otherwise reset the IOA.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN / IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_cancel_hcam_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_cmnd *loop_cmd;
	struct ipr_hrr_queue *hrrq;
	int rc = IPR_RC_JOB_CONTINUE;
	int count = 0;

	ENTER;
	ipr_cmd->job_step = ipr_reset_quiesce_done;

	for_each_hrrq(hrrq, ioa_cfg) {
		spin_lock(&hrrq->_lock);
		list_for_each_entry(loop_cmd, &hrrq->hrrq_pending_q, queue) {
			count++;
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
			list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
			rc = IPR_RC_JOB_RETURN;
			break;
		}
		spin_unlock(&hrrq->_lock);

		if (count)
			break;
	}

	LEAVE;
	return rc;
}

/**
 * ipr_reset_cancel_hcam - Cancel outstanding HCAMs
 * @ipr_cmd:	ipr command struct
 *
 * Description: Cancel any oustanding HCAMs to the IOA.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_cancel_hcam(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc = IPR_RC_JOB_CONTINUE;
	struct ipr_cmd_pkt *cmd_pkt;
	struct ipr_cmnd *hcam_cmd;
	struct ipr_hrr_queue *hrrq = &ioa_cfg->hrrq[IPR_INIT_HRRQ];

	ENTER;
	ipr_cmd->job_step = ipr_reset_cancel_hcam_done;

	if (!hrrq->ioa_is_dead) {
		if (!list_empty(&ioa_cfg->hostrcb_pending_q)) {
			list_for_each_entry(hcam_cmd, &hrrq->hrrq_pending_q, queue) {
				if (hcam_cmd->ioarcb.cmd_pkt.cdb[0] != IPR_HOST_CONTROLLED_ASYNC)
					continue;

				ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
				ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
				cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
				cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
				cmd_pkt->cdb[0] = IPR_CANCEL_REQUEST;
				cmd_pkt->cdb[1] = IPR_CANCEL_64BIT_IOARCB;
				cmd_pkt->cdb[10] = ((u64) hcam_cmd->dma_addr >> 56) & 0xff;
				cmd_pkt->cdb[11] = ((u64) hcam_cmd->dma_addr >> 48) & 0xff;
				cmd_pkt->cdb[12] = ((u64) hcam_cmd->dma_addr >> 40) & 0xff;
				cmd_pkt->cdb[13] = ((u64) hcam_cmd->dma_addr >> 32) & 0xff;
				cmd_pkt->cdb[2] = ((u64) hcam_cmd->dma_addr >> 24) & 0xff;
				cmd_pkt->cdb[3] = ((u64) hcam_cmd->dma_addr >> 16) & 0xff;
				cmd_pkt->cdb[4] = ((u64) hcam_cmd->dma_addr >> 8) & 0xff;
				cmd_pkt->cdb[5] = ((u64) hcam_cmd->dma_addr) & 0xff;

				ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout,
					   IPR_CANCEL_TIMEOUT);

				rc = IPR_RC_JOB_RETURN;
				ipr_cmd->job_step = ipr_reset_cancel_hcam;
				break;
			}
		}
	} else
		ipr_cmd->job_step = ipr_reset_alert;

	LEAVE;
	return rc;
}

/**
 * ipr_reset_ucode_download_done - Microcode download completion
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function unmaps the microcode download buffer.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_ucode_download_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_sglist *sglist = ioa_cfg->ucode_sglist;

	dma_unmap_sg(&ioa_cfg->pdev->dev, sglist->scatterlist,
		     sglist->num_sg, DMA_TO_DEVICE);

	ipr_cmd->job_step = ipr_reset_alert;
	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_ucode_download - Download microcode to the adapter
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function checks to see if it there is microcode
 * to download to the adapter. If there is, a download is performed.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_ucode_download(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_sglist *sglist = ioa_cfg->ucode_sglist;

	ENTER;
	ipr_cmd->job_step = ipr_reset_alert;

	if (!sglist)
		return IPR_RC_JOB_CONTINUE;

	ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
	ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ipr_cmd->ioarcb.cmd_pkt.cdb[0] = WRITE_BUFFER;
	ipr_cmd->ioarcb.cmd_pkt.cdb[1] = IPR_WR_BUF_DOWNLOAD_AND_SAVE;
	ipr_cmd->ioarcb.cmd_pkt.cdb[6] = (sglist->buffer_len & 0xff0000) >> 16;
	ipr_cmd->ioarcb.cmd_pkt.cdb[7] = (sglist->buffer_len & 0x00ff00) >> 8;
	ipr_cmd->ioarcb.cmd_pkt.cdb[8] = sglist->buffer_len & 0x0000ff;

	if (ioa_cfg->sis64)
		ipr_build_ucode_ioadl64(ipr_cmd, sglist);
	else
		ipr_build_ucode_ioadl(ipr_cmd, sglist);
	ipr_cmd->job_step = ipr_reset_ucode_download_done;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout,
		   IPR_WRITE_BUFFER_TIMEOUT);

	LEAVE;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_reset_shutdown_ioa - Shutdown the adapter
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function issues an adapter shutdown of the
 * specified type to the specified adapter as part of the
 * adapter reset job.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_shutdown_ioa(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	enum ipr_shutdown_type shutdown_type = ipr_cmd->u.shutdown_type;
	unsigned long timeout;
	int rc = IPR_RC_JOB_CONTINUE;

	ENTER;
	if (shutdown_type == IPR_SHUTDOWN_QUIESCE)
		ipr_cmd->job_step = ipr_reset_cancel_hcam;
	else if (shutdown_type != IPR_SHUTDOWN_NONE &&
			!ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead) {
		ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
		ipr_cmd->ioarcb.cmd_pkt.cdb[0] = IPR_IOA_SHUTDOWN;
		ipr_cmd->ioarcb.cmd_pkt.cdb[1] = shutdown_type;

		if (shutdown_type == IPR_SHUTDOWN_NORMAL)
			timeout = IPR_SHUTDOWN_TIMEOUT;
		else if (shutdown_type == IPR_SHUTDOWN_PREPARE_FOR_NORMAL)
			timeout = IPR_INTERNAL_TIMEOUT;
		else if (ioa_cfg->dual_raid && ipr_dual_ioa_raid)
			timeout = IPR_DUAL_IOA_ABBR_SHUTDOWN_TO;
		else
			timeout = IPR_ABBREV_SHUTDOWN_TIMEOUT;

		ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, timeout);

		rc = IPR_RC_JOB_RETURN;
		ipr_cmd->job_step = ipr_reset_ucode_download;
	} else
		ipr_cmd->job_step = ipr_reset_alert;

	LEAVE;
	return rc;
}

/**
 * ipr_reset_ioa_job - Adapter reset job
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function is the job router for the adapter reset job.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_ioa_job(struct ipr_cmnd *ipr_cmd)
{
	u32 rc, ioasc;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	do {
		ioasc = be32_to_cpu(ipr_cmd->s.ioasa.hdr.ioasc);

		if (ioa_cfg->reset_cmd != ipr_cmd) {
			/*
			 * We are doing nested adapter resets and this is
			 * not the current reset job.
			 */
			list_add_tail(&ipr_cmd->queue,
					&ipr_cmd->hrrq->hrrq_free_q);
			return;
		}

		if (IPR_IOASC_SENSE_KEY(ioasc)) {
			rc = ipr_cmd->job_step_failed(ipr_cmd);
			if (rc == IPR_RC_JOB_RETURN)
				return;
		}

		ipr_reinit_ipr_cmnd(ipr_cmd);
		ipr_cmd->job_step_failed = ipr_reset_cmd_failed;
		rc = ipr_cmd->job_step(ipr_cmd);
	} while (rc == IPR_RC_JOB_CONTINUE);
}

/**
 * _ipr_initiate_ioa_reset - Initiate an adapter reset
 * @ioa_cfg:		ioa config struct
 * @job_step:		first job step of reset job
 * @shutdown_type:	shutdown type
 *
 * Description: This function will initiate the reset of the given adapter
 * starting at the selected job step.
 * If the caller needs to wait on the completion of the reset,
 * the caller must sleep on the reset_wait_q.
 *
 * Return value:
 * 	none
 **/
static void _ipr_initiate_ioa_reset(struct ipr_ioa_cfg *ioa_cfg,
				    int (*job_step) (struct ipr_cmnd *),
				    enum ipr_shutdown_type shutdown_type)
{
	struct ipr_cmnd *ipr_cmd;
	int i;

	ioa_cfg->in_reset_reload = 1;
	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].allow_cmds = 0;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}
	wmb();
	if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].removing_ioa) {
		ioa_cfg->scsi_unblock = 0;
		ioa_cfg->scsi_blocked = 1;
		scsi_block_requests(ioa_cfg->host);
	}

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioa_cfg->reset_cmd = ipr_cmd;
	ipr_cmd->job_step = job_step;
	ipr_cmd->u.shutdown_type = shutdown_type;

	ipr_reset_ioa_job(ipr_cmd);
}

/**
 * ipr_initiate_ioa_reset - Initiate an adapter reset
 * @ioa_cfg:		ioa config struct
 * @shutdown_type:	shutdown type
 *
 * Description: This function will initiate the reset of the given adapter.
 * If the caller needs to wait on the completion of the reset,
 * the caller must sleep on the reset_wait_q.
 *
 * Return value:
 * 	none
 **/
static void ipr_initiate_ioa_reset(struct ipr_ioa_cfg *ioa_cfg,
				   enum ipr_shutdown_type shutdown_type)
{
	int i;

	if (ioa_cfg->hrrq[IPR_INIT_HRRQ].ioa_is_dead)
		return;

	if (ioa_cfg->in_reset_reload) {
		if (ioa_cfg->sdt_state == GET_DUMP)
			ioa_cfg->sdt_state = WAIT_FOR_DUMP;
		else if (ioa_cfg->sdt_state == READ_DUMP)
			ioa_cfg->sdt_state = ABORT_DUMP;
	}

	if (ioa_cfg->reset_retries++ >= IPR_NUM_RESET_RELOAD_RETRIES) {
		dev_err(&ioa_cfg->pdev->dev,
			"IOA taken offline - error recovery failed\n");

		ioa_cfg->reset_retries = 0;
		for (i = 0; i < ioa_cfg->hrrq_num; i++) {
			spin_lock(&ioa_cfg->hrrq[i]._lock);
			ioa_cfg->hrrq[i].ioa_is_dead = 1;
			spin_unlock(&ioa_cfg->hrrq[i]._lock);
		}
		wmb();

		if (ioa_cfg->in_ioa_bringdown) {
			ioa_cfg->reset_cmd = NULL;
			ioa_cfg->in_reset_reload = 0;
			ipr_fail_all_ops(ioa_cfg);
			wake_up_all(&ioa_cfg->reset_wait_q);

			if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].removing_ioa) {
				ioa_cfg->scsi_unblock = 1;
				schedule_work(&ioa_cfg->work_q);
			}
			return;
		} else {
			ioa_cfg->in_ioa_bringdown = 1;
			shutdown_type = IPR_SHUTDOWN_NONE;
		}
	}

	_ipr_initiate_ioa_reset(ioa_cfg, ipr_reset_shutdown_ioa,
				shutdown_type);
}

/**
 * ipr_reset_freeze - Hold off all I/O activity
 * @ipr_cmd:	ipr command struct
 *
 * Description: If the PCI slot is frozen, hold off all I/O
 * activity; then, as soon as the slot is available again,
 * initiate an adapter reset.
 */
static int ipr_reset_freeze(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int i;

	/* Disallow new interrupts, avoid loop */
	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].allow_interrupts = 0;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}
	wmb();
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_pending_q);
	ipr_cmd->done = ipr_reset_ioa_job;
	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_pci_mmio_enabled - Called when MMIO has been re-enabled
 * @pdev:	PCI device struct
 *
 * Description: This routine is called to tell us that the MMIO
 * access to the IOA has been restored
 */
static pci_ers_result_t ipr_pci_mmio_enabled(struct pci_dev *pdev)
{
	unsigned long flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);

	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	if (!ioa_cfg->probe_done)
		pci_save_state(pdev);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ipr_pci_frozen - Called when slot has experienced a PCI bus error.
 * @pdev:	PCI device struct
 *
 * Description: This routine is called to tell us that the PCI bus
 * is down. Can't do anything here, except put the device driver
 * into a holding pattern, waiting for the PCI bus to come back.
 */
static void ipr_pci_frozen(struct pci_dev *pdev)
{
	unsigned long flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);

	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	if (ioa_cfg->probe_done)
		_ipr_initiate_ioa_reset(ioa_cfg, ipr_reset_freeze, IPR_SHUTDOWN_NONE);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
}

/**
 * ipr_pci_slot_reset - Called when PCI slot has been reset.
 * @pdev:	PCI device struct
 *
 * Description: This routine is called by the pci error recovery
 * code after the PCI slot has been reset, just before we
 * should resume normal operations.
 */
static pci_ers_result_t ipr_pci_slot_reset(struct pci_dev *pdev)
{
	unsigned long flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);

	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	if (ioa_cfg->probe_done) {
		if (ioa_cfg->needs_warm_reset)
			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
		else
			_ipr_initiate_ioa_reset(ioa_cfg, ipr_reset_restore_cfg_space,
						IPR_SHUTDOWN_NONE);
	} else
		wake_up_all(&ioa_cfg->eeh_wait_q);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * ipr_pci_perm_failure - Called when PCI slot is dead for good.
 * @pdev:	PCI device struct
 *
 * Description: This routine is called when the PCI bus has
 * permanently failed.
 */
static void ipr_pci_perm_failure(struct pci_dev *pdev)
{
	unsigned long flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);
	int i;

	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	if (ioa_cfg->probe_done) {
		if (ioa_cfg->sdt_state == WAIT_FOR_DUMP)
			ioa_cfg->sdt_state = ABORT_DUMP;
		ioa_cfg->reset_retries = IPR_NUM_RESET_RELOAD_RETRIES - 1;
		ioa_cfg->in_ioa_bringdown = 1;
		for (i = 0; i < ioa_cfg->hrrq_num; i++) {
			spin_lock(&ioa_cfg->hrrq[i]._lock);
			ioa_cfg->hrrq[i].allow_cmds = 0;
			spin_unlock(&ioa_cfg->hrrq[i]._lock);
		}
		wmb();
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	} else
		wake_up_all(&ioa_cfg->eeh_wait_q);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
}

/**
 * ipr_pci_error_detected - Called when a PCI error is detected.
 * @pdev:	PCI device struct
 * @state:	PCI channel state
 *
 * Description: Called when a PCI error is detected.
 *
 * Return value:
 * 	PCI_ERS_RESULT_NEED_RESET or PCI_ERS_RESULT_DISCONNECT
 */
static pci_ers_result_t ipr_pci_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	switch (state) {
	case pci_channel_io_frozen:
		ipr_pci_frozen(pdev);
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_perm_failure:
		ipr_pci_perm_failure(pdev);
		return PCI_ERS_RESULT_DISCONNECT;
		break;
	default:
		break;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ipr_probe_ioa_part2 - Initializes IOAs found in ipr_probe_ioa(..)
 * @ioa_cfg:	ioa cfg struct
 *
 * Description: This is the second phase of adapter initialization
 * This function takes care of initilizing the adapter to the point
 * where it can accept new commands.
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_probe_ioa_part2(struct ipr_ioa_cfg *ioa_cfg)
{
	int rc = 0;
	unsigned long host_lock_flags = 0;

	ENTER;
	spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);
	dev_dbg(&ioa_cfg->pdev->dev, "ioa_cfg adx: 0x%p\n", ioa_cfg);
	ioa_cfg->probe_done = 1;
	if (ioa_cfg->needs_hard_reset) {
		ioa_cfg->needs_hard_reset = 0;
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	} else
		_ipr_initiate_ioa_reset(ioa_cfg, ipr_reset_enable_ioa,
					IPR_SHUTDOWN_NONE);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);

	LEAVE;
	return rc;
}

/**
 * ipr_free_cmd_blks - Frees command blocks allocated for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	none
 **/
static void ipr_free_cmd_blks(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	if (ioa_cfg->ipr_cmnd_list) {
		for (i = 0; i < IPR_NUM_CMD_BLKS; i++) {
			if (ioa_cfg->ipr_cmnd_list[i])
				dma_pool_free(ioa_cfg->ipr_cmd_pool,
					      ioa_cfg->ipr_cmnd_list[i],
					      ioa_cfg->ipr_cmnd_list_dma[i]);

			ioa_cfg->ipr_cmnd_list[i] = NULL;
		}
	}

	dma_pool_destroy(ioa_cfg->ipr_cmd_pool);

	kfree(ioa_cfg->ipr_cmnd_list);
	kfree(ioa_cfg->ipr_cmnd_list_dma);
	ioa_cfg->ipr_cmnd_list = NULL;
	ioa_cfg->ipr_cmnd_list_dma = NULL;
	ioa_cfg->ipr_cmd_pool = NULL;
}

/**
 * ipr_free_mem - Frees memory allocated for an adapter
 * @ioa_cfg:	ioa cfg struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_free_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	kfree(ioa_cfg->res_entries);
	dma_free_coherent(&ioa_cfg->pdev->dev, sizeof(struct ipr_misc_cbs),
			  ioa_cfg->vpd_cbs, ioa_cfg->vpd_cbs_dma);
	ipr_free_cmd_blks(ioa_cfg);

	for (i = 0; i < ioa_cfg->hrrq_num; i++)
		dma_free_coherent(&ioa_cfg->pdev->dev,
				  sizeof(u32) * ioa_cfg->hrrq[i].size,
				  ioa_cfg->hrrq[i].host_rrq,
				  ioa_cfg->hrrq[i].host_rrq_dma);

	dma_free_coherent(&ioa_cfg->pdev->dev, ioa_cfg->cfg_table_size,
			  ioa_cfg->u.cfg_table, ioa_cfg->cfg_table_dma);

	for (i = 0; i < IPR_MAX_HCAMS; i++) {
		dma_free_coherent(&ioa_cfg->pdev->dev,
				  sizeof(struct ipr_hostrcb),
				  ioa_cfg->hostrcb[i],
				  ioa_cfg->hostrcb_dma[i]);
	}

	ipr_free_dump(ioa_cfg);
	kfree(ioa_cfg->trace);
}

/**
 * ipr_free_irqs - Free all allocated IRQs for the adapter.
 * @ioa_cfg:	ipr cfg struct
 *
 * This function frees all allocated IRQs for the
 * specified adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_free_irqs(struct ipr_ioa_cfg *ioa_cfg)
{
	struct pci_dev *pdev = ioa_cfg->pdev;
	int i;

	for (i = 0; i < ioa_cfg->nvectors; i++)
		free_irq(pci_irq_vector(pdev, i), &ioa_cfg->hrrq[i]);
	pci_free_irq_vectors(pdev);
}

/**
 * ipr_free_all_resources - Free all allocated resources for an adapter.
 * @ioa_cfg:	ioa config struct
 *
 * This function frees all allocated resources for the
 * specified adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_free_all_resources(struct ipr_ioa_cfg *ioa_cfg)
{
	struct pci_dev *pdev = ioa_cfg->pdev;

	ENTER;
	ipr_free_irqs(ioa_cfg);
	if (ioa_cfg->reset_work_q)
		destroy_workqueue(ioa_cfg->reset_work_q);
	iounmap(ioa_cfg->hdw_dma_regs);
	pci_release_regions(pdev);
	ipr_free_mem(ioa_cfg);
	scsi_host_put(ioa_cfg->host);
	pci_disable_device(pdev);
	LEAVE;
}

/**
 * ipr_alloc_cmd_blks - Allocate command blocks for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / -ENOMEM on allocation failure
 **/
static int ipr_alloc_cmd_blks(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	dma_addr_t dma_addr;
	int i, entries_each_hrrq, hrrq_id = 0;

	ioa_cfg->ipr_cmd_pool = dma_pool_create(IPR_NAME, &ioa_cfg->pdev->dev,
						sizeof(struct ipr_cmnd), 512, 0);

	if (!ioa_cfg->ipr_cmd_pool)
		return -ENOMEM;

	ioa_cfg->ipr_cmnd_list = kcalloc(IPR_NUM_CMD_BLKS, sizeof(struct ipr_cmnd *), GFP_KERNEL);
	ioa_cfg->ipr_cmnd_list_dma = kcalloc(IPR_NUM_CMD_BLKS, sizeof(dma_addr_t), GFP_KERNEL);

	if (!ioa_cfg->ipr_cmnd_list || !ioa_cfg->ipr_cmnd_list_dma) {
		ipr_free_cmd_blks(ioa_cfg);
		return -ENOMEM;
	}

	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		if (ioa_cfg->hrrq_num > 1) {
			if (i == 0) {
				entries_each_hrrq = IPR_NUM_INTERNAL_CMD_BLKS;
				ioa_cfg->hrrq[i].min_cmd_id = 0;
				ioa_cfg->hrrq[i].max_cmd_id =
					(entries_each_hrrq - 1);
			} else {
				entries_each_hrrq =
					IPR_NUM_BASE_CMD_BLKS/
					(ioa_cfg->hrrq_num - 1);
				ioa_cfg->hrrq[i].min_cmd_id =
					IPR_NUM_INTERNAL_CMD_BLKS +
					(i - 1) * entries_each_hrrq;
				ioa_cfg->hrrq[i].max_cmd_id =
					(IPR_NUM_INTERNAL_CMD_BLKS +
					i * entries_each_hrrq - 1);
			}
		} else {
			entries_each_hrrq = IPR_NUM_CMD_BLKS;
			ioa_cfg->hrrq[i].min_cmd_id = 0;
			ioa_cfg->hrrq[i].max_cmd_id = (entries_each_hrrq - 1);
		}
		ioa_cfg->hrrq[i].size = entries_each_hrrq;
	}

	BUG_ON(ioa_cfg->hrrq_num == 0);

	i = IPR_NUM_CMD_BLKS -
		ioa_cfg->hrrq[ioa_cfg->hrrq_num - 1].max_cmd_id - 1;
	if (i > 0) {
		ioa_cfg->hrrq[ioa_cfg->hrrq_num - 1].size += i;
		ioa_cfg->hrrq[ioa_cfg->hrrq_num - 1].max_cmd_id += i;
	}

	for (i = 0; i < IPR_NUM_CMD_BLKS; i++) {
		ipr_cmd = dma_pool_zalloc(ioa_cfg->ipr_cmd_pool,
				GFP_KERNEL, &dma_addr);

		if (!ipr_cmd) {
			ipr_free_cmd_blks(ioa_cfg);
			return -ENOMEM;
		}

		ioa_cfg->ipr_cmnd_list[i] = ipr_cmd;
		ioa_cfg->ipr_cmnd_list_dma[i] = dma_addr;

		ioarcb = &ipr_cmd->ioarcb;
		ipr_cmd->dma_addr = dma_addr;
		if (ioa_cfg->sis64)
			ioarcb->a.ioarcb_host_pci_addr64 = cpu_to_be64(dma_addr);
		else
			ioarcb->a.ioarcb_host_pci_addr = cpu_to_be32(dma_addr);

		ioarcb->host_response_handle = cpu_to_be32(i << 2);
		if (ioa_cfg->sis64) {
			ioarcb->u.sis64_addr_data.data_ioadl_addr =
				cpu_to_be64(dma_addr + offsetof(struct ipr_cmnd, i.ioadl64));
			ioarcb->u.sis64_addr_data.ioasa_host_pci_addr =
				cpu_to_be64(dma_addr + offsetof(struct ipr_cmnd, s.ioasa64));
		} else {
			ioarcb->write_ioadl_addr =
				cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, i.ioadl));
			ioarcb->read_ioadl_addr = ioarcb->write_ioadl_addr;
			ioarcb->ioasa_host_pci_addr =
				cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, s.ioasa));
		}
		ioarcb->ioasa_len = cpu_to_be16(sizeof(struct ipr_ioasa));
		ipr_cmd->cmd_index = i;
		ipr_cmd->ioa_cfg = ioa_cfg;
		ipr_cmd->sense_buffer_dma = dma_addr +
			offsetof(struct ipr_cmnd, sense_buffer);

		ipr_cmd->ioarcb.cmd_pkt.hrrq_id = hrrq_id;
		ipr_cmd->hrrq = &ioa_cfg->hrrq[hrrq_id];
		list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
		if (i >= ioa_cfg->hrrq[hrrq_id].max_cmd_id)
			hrrq_id++;
	}

	return 0;
}

/**
 * ipr_alloc_mem - Allocate memory for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / non-zero for error
 **/
static int ipr_alloc_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	struct pci_dev *pdev = ioa_cfg->pdev;
	int i, rc = -ENOMEM;

	ENTER;
	ioa_cfg->res_entries = kcalloc(ioa_cfg->max_devs_supported,
				       sizeof(struct ipr_resource_entry),
				       GFP_KERNEL);

	if (!ioa_cfg->res_entries)
		goto out;

	for (i = 0; i < ioa_cfg->max_devs_supported; i++) {
		list_add_tail(&ioa_cfg->res_entries[i].queue, &ioa_cfg->free_res_q);
		ioa_cfg->res_entries[i].ioa_cfg = ioa_cfg;
	}

	ioa_cfg->vpd_cbs = dma_alloc_coherent(&pdev->dev,
					      sizeof(struct ipr_misc_cbs),
					      &ioa_cfg->vpd_cbs_dma,
					      GFP_KERNEL);

	if (!ioa_cfg->vpd_cbs)
		goto out_free_res_entries;

	if (ipr_alloc_cmd_blks(ioa_cfg))
		goto out_free_vpd_cbs;

	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		ioa_cfg->hrrq[i].host_rrq = dma_alloc_coherent(&pdev->dev,
					sizeof(u32) * ioa_cfg->hrrq[i].size,
					&ioa_cfg->hrrq[i].host_rrq_dma,
					GFP_KERNEL);

		if (!ioa_cfg->hrrq[i].host_rrq)  {
			while (--i >= 0)
				dma_free_coherent(&pdev->dev,
					sizeof(u32) * ioa_cfg->hrrq[i].size,
					ioa_cfg->hrrq[i].host_rrq,
					ioa_cfg->hrrq[i].host_rrq_dma);
			goto out_ipr_free_cmd_blocks;
		}
		ioa_cfg->hrrq[i].ioa_cfg = ioa_cfg;
	}

	ioa_cfg->u.cfg_table = dma_alloc_coherent(&pdev->dev,
						  ioa_cfg->cfg_table_size,
						  &ioa_cfg->cfg_table_dma,
						  GFP_KERNEL);

	if (!ioa_cfg->u.cfg_table)
		goto out_free_host_rrq;

	for (i = 0; i < IPR_MAX_HCAMS; i++) {
		ioa_cfg->hostrcb[i] = dma_alloc_coherent(&pdev->dev,
							 sizeof(struct ipr_hostrcb),
							 &ioa_cfg->hostrcb_dma[i],
							 GFP_KERNEL);

		if (!ioa_cfg->hostrcb[i])
			goto out_free_hostrcb_dma;

		ioa_cfg->hostrcb[i]->hostrcb_dma =
			ioa_cfg->hostrcb_dma[i] + offsetof(struct ipr_hostrcb, hcam);
		ioa_cfg->hostrcb[i]->ioa_cfg = ioa_cfg;
		list_add_tail(&ioa_cfg->hostrcb[i]->queue, &ioa_cfg->hostrcb_free_q);
	}

	ioa_cfg->trace = kcalloc(IPR_NUM_TRACE_ENTRIES,
				 sizeof(struct ipr_trace_entry),
				 GFP_KERNEL);

	if (!ioa_cfg->trace)
		goto out_free_hostrcb_dma;

	rc = 0;
out:
	LEAVE;
	return rc;

out_free_hostrcb_dma:
	while (i-- > 0) {
		dma_free_coherent(&pdev->dev, sizeof(struct ipr_hostrcb),
				  ioa_cfg->hostrcb[i],
				  ioa_cfg->hostrcb_dma[i]);
	}
	dma_free_coherent(&pdev->dev, ioa_cfg->cfg_table_size,
			  ioa_cfg->u.cfg_table, ioa_cfg->cfg_table_dma);
out_free_host_rrq:
	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		dma_free_coherent(&pdev->dev,
				  sizeof(u32) * ioa_cfg->hrrq[i].size,
				  ioa_cfg->hrrq[i].host_rrq,
				  ioa_cfg->hrrq[i].host_rrq_dma);
	}
out_ipr_free_cmd_blocks:
	ipr_free_cmd_blks(ioa_cfg);
out_free_vpd_cbs:
	dma_free_coherent(&pdev->dev, sizeof(struct ipr_misc_cbs),
			  ioa_cfg->vpd_cbs, ioa_cfg->vpd_cbs_dma);
out_free_res_entries:
	kfree(ioa_cfg->res_entries);
	goto out;
}

/**
 * ipr_initialize_bus_attr - Initialize SCSI bus attributes to default values
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	none
 **/
static void ipr_initialize_bus_attr(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	for (i = 0; i < IPR_MAX_NUM_BUSES; i++) {
		ioa_cfg->bus_attr[i].bus = i;
		ioa_cfg->bus_attr[i].qas_enabled = 0;
		ioa_cfg->bus_attr[i].bus_width = IPR_DEFAULT_BUS_WIDTH;
		if (ipr_max_speed < ARRAY_SIZE(ipr_max_bus_speeds))
			ioa_cfg->bus_attr[i].max_xfer_rate = ipr_max_bus_speeds[ipr_max_speed];
		else
			ioa_cfg->bus_attr[i].max_xfer_rate = IPR_U160_SCSI_RATE;
	}
}

/**
 * ipr_init_regs - Initialize IOA registers
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 *	none
 **/
static void ipr_init_regs(struct ipr_ioa_cfg *ioa_cfg)
{
	const struct ipr_interrupt_offsets *p;
	struct ipr_interrupts *t;
	void __iomem *base;

	p = &ioa_cfg->chip_cfg->regs;
	t = &ioa_cfg->regs;
	base = ioa_cfg->hdw_dma_regs;

	t->set_interrupt_mask_reg = base + p->set_interrupt_mask_reg;
	t->clr_interrupt_mask_reg = base + p->clr_interrupt_mask_reg;
	t->clr_interrupt_mask_reg32 = base + p->clr_interrupt_mask_reg32;
	t->sense_interrupt_mask_reg = base + p->sense_interrupt_mask_reg;
	t->sense_interrupt_mask_reg32 = base + p->sense_interrupt_mask_reg32;
	t->clr_interrupt_reg = base + p->clr_interrupt_reg;
	t->clr_interrupt_reg32 = base + p->clr_interrupt_reg32;
	t->sense_interrupt_reg = base + p->sense_interrupt_reg;
	t->sense_interrupt_reg32 = base + p->sense_interrupt_reg32;
	t->ioarrin_reg = base + p->ioarrin_reg;
	t->sense_uproc_interrupt_reg = base + p->sense_uproc_interrupt_reg;
	t->sense_uproc_interrupt_reg32 = base + p->sense_uproc_interrupt_reg32;
	t->set_uproc_interrupt_reg = base + p->set_uproc_interrupt_reg;
	t->set_uproc_interrupt_reg32 = base + p->set_uproc_interrupt_reg32;
	t->clr_uproc_interrupt_reg = base + p->clr_uproc_interrupt_reg;
	t->clr_uproc_interrupt_reg32 = base + p->clr_uproc_interrupt_reg32;

	if (ioa_cfg->sis64) {
		t->init_feedback_reg = base + p->init_feedback_reg;
		t->dump_addr_reg = base + p->dump_addr_reg;
		t->dump_data_reg = base + p->dump_data_reg;
		t->endian_swap_reg = base + p->endian_swap_reg;
	}
}

/**
 * ipr_init_ioa_cfg - Initialize IOA config struct
 * @ioa_cfg:	ioa config struct
 * @host:		scsi host struct
 * @pdev:		PCI dev struct
 *
 * Return value:
 * 	none
 **/
static void ipr_init_ioa_cfg(struct ipr_ioa_cfg *ioa_cfg,
			     struct Scsi_Host *host, struct pci_dev *pdev)
{
	int i;

	ioa_cfg->host = host;
	ioa_cfg->pdev = pdev;
	ioa_cfg->log_level = ipr_log_level;
	ioa_cfg->doorbell = IPR_DOORBELL;
	sprintf(ioa_cfg->eye_catcher, IPR_EYECATCHER);
	sprintf(ioa_cfg->trace_start, IPR_TRACE_START_LABEL);
	sprintf(ioa_cfg->cfg_table_start, IPR_CFG_TBL_START);
	sprintf(ioa_cfg->resource_table_label, IPR_RES_TABLE_LABEL);
	sprintf(ioa_cfg->ipr_hcam_label, IPR_HCAM_LABEL);
	sprintf(ioa_cfg->ipr_cmd_label, IPR_CMD_LABEL);

	INIT_LIST_HEAD(&ioa_cfg->hostrcb_free_q);
	INIT_LIST_HEAD(&ioa_cfg->hostrcb_pending_q);
	INIT_LIST_HEAD(&ioa_cfg->hostrcb_report_q);
	INIT_LIST_HEAD(&ioa_cfg->free_res_q);
	INIT_LIST_HEAD(&ioa_cfg->used_res_q);
	INIT_WORK(&ioa_cfg->work_q, ipr_worker_thread);
	INIT_WORK(&ioa_cfg->scsi_add_work_q, ipr_add_remove_thread);
	init_waitqueue_head(&ioa_cfg->reset_wait_q);
	init_waitqueue_head(&ioa_cfg->msi_wait_q);
	init_waitqueue_head(&ioa_cfg->eeh_wait_q);
	ioa_cfg->sdt_state = INACTIVE;

	ipr_initialize_bus_attr(ioa_cfg);
	ioa_cfg->max_devs_supported = ipr_max_devs;

	if (ioa_cfg->sis64) {
		host->max_channel = IPR_MAX_SIS64_BUSES;
		host->max_id = IPR_MAX_SIS64_TARGETS_PER_BUS;
		host->max_lun = IPR_MAX_SIS64_LUNS_PER_TARGET;
		if (ipr_max_devs > IPR_MAX_SIS64_DEVS)
			ioa_cfg->max_devs_supported = IPR_MAX_SIS64_DEVS;
		ioa_cfg->cfg_table_size = (sizeof(struct ipr_config_table_hdr64)
					   + ((sizeof(struct ipr_config_table_entry64)
					       * ioa_cfg->max_devs_supported)));
	} else {
		host->max_channel = IPR_VSET_BUS;
		host->max_id = IPR_MAX_NUM_TARGETS_PER_BUS;
		host->max_lun = IPR_MAX_NUM_LUNS_PER_TARGET;
		if (ipr_max_devs > IPR_MAX_PHYSICAL_DEVS)
			ioa_cfg->max_devs_supported = IPR_MAX_PHYSICAL_DEVS;
		ioa_cfg->cfg_table_size = (sizeof(struct ipr_config_table_hdr)
					   + ((sizeof(struct ipr_config_table_entry)
					       * ioa_cfg->max_devs_supported)));
	}

	host->unique_id = host->host_no;
	host->max_cmd_len = IPR_MAX_CDB_LEN;
	host->can_queue = ioa_cfg->max_cmds;
	pci_set_drvdata(pdev, ioa_cfg);

	for (i = 0; i < ARRAY_SIZE(ioa_cfg->hrrq); i++) {
		INIT_LIST_HEAD(&ioa_cfg->hrrq[i].hrrq_free_q);
		INIT_LIST_HEAD(&ioa_cfg->hrrq[i].hrrq_pending_q);
		spin_lock_init(&ioa_cfg->hrrq[i]._lock);
		if (i == 0)
			ioa_cfg->hrrq[i].lock = ioa_cfg->host->host_lock;
		else
			ioa_cfg->hrrq[i].lock = &ioa_cfg->hrrq[i]._lock;
	}
}

/**
 * ipr_get_chip_info - Find adapter chip information
 * @dev_id:		PCI device id struct
 *
 * Return value:
 * 	ptr to chip information on success / NULL on failure
 **/
static const struct ipr_chip_t *
ipr_get_chip_info(const struct pci_device_id *dev_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ipr_chip); i++)
		if (ipr_chip[i].vendor == dev_id->vendor &&
		    ipr_chip[i].device == dev_id->device)
			return &ipr_chip[i];
	return NULL;
}

/**
 * ipr_wait_for_pci_err_recovery - Wait for any PCI error recovery to complete
 *						during probe time
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	None
 **/
static void ipr_wait_for_pci_err_recovery(struct ipr_ioa_cfg *ioa_cfg)
{
	struct pci_dev *pdev = ioa_cfg->pdev;

	if (pci_channel_offline(pdev)) {
		wait_event_timeout(ioa_cfg->eeh_wait_q,
				   !pci_channel_offline(pdev),
				   IPR_PCI_ERROR_RECOVERY_TIMEOUT);
		pci_restore_state(pdev);
	}
}

static void name_msi_vectors(struct ipr_ioa_cfg *ioa_cfg)
{
	int vec_idx, n = sizeof(ioa_cfg->vectors_info[0].desc) - 1;

	for (vec_idx = 0; vec_idx < ioa_cfg->nvectors; vec_idx++) {
		snprintf(ioa_cfg->vectors_info[vec_idx].desc, n,
			 "host%d-%d", ioa_cfg->host->host_no, vec_idx);
		ioa_cfg->vectors_info[vec_idx].
			desc[strlen(ioa_cfg->vectors_info[vec_idx].desc)] = 0;
	}
}

static int ipr_request_other_msi_irqs(struct ipr_ioa_cfg *ioa_cfg,
		struct pci_dev *pdev)
{
	int i, rc;

	for (i = 1; i < ioa_cfg->nvectors; i++) {
		rc = request_irq(pci_irq_vector(pdev, i),
			ipr_isr_mhrrq,
			0,
			ioa_cfg->vectors_info[i].desc,
			&ioa_cfg->hrrq[i]);
		if (rc) {
			while (--i > 0)
				free_irq(pci_irq_vector(pdev, i),
					&ioa_cfg->hrrq[i]);
			return rc;
		}
	}
	return 0;
}

/**
 * ipr_test_intr - Handle the interrupt generated in ipr_test_msi().
 * @devp:		PCI device struct
 * @irq:		IRQ number
 *
 * Description: Simply set the msi_received flag to 1 indicating that
 * Message Signaled Interrupts are supported.
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static irqreturn_t ipr_test_intr(int irq, void *devp)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)devp;
	unsigned long lock_flags = 0;
	irqreturn_t rc = IRQ_HANDLED;

	dev_info(&ioa_cfg->pdev->dev, "Received IRQ : %d\n", irq);
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ioa_cfg->msi_received = 1;
	wake_up(&ioa_cfg->msi_wait_q);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return rc;
}

/**
 * ipr_test_msi - Test for Message Signaled Interrupt (MSI) support.
 * @ioa_cfg:		ioa config struct
 * @pdev:		PCI device struct
 *
 * Description: This routine sets up and initiates a test interrupt to determine
 * if the interrupt is received via the ipr_test_intr() service routine.
 * If the tests fails, the driver will fall back to LSI.
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int ipr_test_msi(struct ipr_ioa_cfg *ioa_cfg, struct pci_dev *pdev)
{
	int rc;
	unsigned long lock_flags = 0;
	int irq = pci_irq_vector(pdev, 0);

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	init_waitqueue_head(&ioa_cfg->msi_wait_q);
	ioa_cfg->msi_received = 0;
	ipr_mask_and_clear_interrupts(ioa_cfg, ~IPR_PCII_IOA_TRANS_TO_OPER);
	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE, ioa_cfg->regs.clr_interrupt_mask_reg32);
	readl(ioa_cfg->regs.sense_interrupt_mask_reg);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	rc = request_irq(irq, ipr_test_intr, 0, IPR_NAME, ioa_cfg);
	if (rc) {
		dev_err(&pdev->dev, "Can not assign irq %d\n", irq);
		return rc;
	} else if (ipr_debug)
		dev_info(&pdev->dev, "IRQ assigned: %d\n", irq);

	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE, ioa_cfg->regs.sense_interrupt_reg32);
	readl(ioa_cfg->regs.sense_interrupt_reg);
	wait_event_timeout(ioa_cfg->msi_wait_q, ioa_cfg->msi_received, HZ);
	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ipr_mask_and_clear_interrupts(ioa_cfg, ~IPR_PCII_IOA_TRANS_TO_OPER);

	if (!ioa_cfg->msi_received) {
		/* MSI test failed */
		dev_info(&pdev->dev, "MSI test failed.  Falling back to LSI.\n");
		rc = -EOPNOTSUPP;
	} else if (ipr_debug)
		dev_info(&pdev->dev, "MSI test succeeded.\n");

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	free_irq(irq, ioa_cfg);

	LEAVE;

	return rc;
}

 /* ipr_probe_ioa - Allocates memory and does first stage of initialization
 * @pdev:		PCI device struct
 * @dev_id:		PCI device id struct
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int ipr_probe_ioa(struct pci_dev *pdev,
			 const struct pci_device_id *dev_id)
{
	struct ipr_ioa_cfg *ioa_cfg;
	struct Scsi_Host *host;
	unsigned long ipr_regs_pci;
	void __iomem *ipr_regs;
	int rc = PCIBIOS_SUCCESSFUL;
	volatile u32 mask, uproc, interrupts;
	unsigned long lock_flags, driver_lock_flags;
	unsigned int irq_flag;

	ENTER;

	dev_info(&pdev->dev, "Found IOA with IRQ: %d\n", pdev->irq);
	host = scsi_host_alloc(&driver_template, sizeof(*ioa_cfg));

	if (!host) {
		dev_err(&pdev->dev, "call to scsi_host_alloc failed!\n");
		rc = -ENOMEM;
		goto out;
	}

	ioa_cfg = (struct ipr_ioa_cfg *)host->hostdata;
	memset(ioa_cfg, 0, sizeof(struct ipr_ioa_cfg));
	ata_host_init(&ioa_cfg->ata_host, &pdev->dev, &ipr_sata_ops);

	ioa_cfg->ipr_chip = ipr_get_chip_info(dev_id);

	if (!ioa_cfg->ipr_chip) {
		dev_err(&pdev->dev, "Unknown adapter chipset 0x%04X 0x%04X\n",
			dev_id->vendor, dev_id->device);
		goto out_scsi_host_put;
	}

	/* set SIS 32 or SIS 64 */
	ioa_cfg->sis64 = ioa_cfg->ipr_chip->sis_type == IPR_SIS64 ? 1 : 0;
	ioa_cfg->chip_cfg = ioa_cfg->ipr_chip->cfg;
	ioa_cfg->clear_isr = ioa_cfg->chip_cfg->clear_isr;
	ioa_cfg->max_cmds = ioa_cfg->chip_cfg->max_cmds;

	if (ipr_transop_timeout)
		ioa_cfg->transop_timeout = ipr_transop_timeout;
	else if (dev_id->driver_data & IPR_USE_LONG_TRANSOP_TIMEOUT)
		ioa_cfg->transop_timeout = IPR_LONG_OPERATIONAL_TIMEOUT;
	else
		ioa_cfg->transop_timeout = IPR_OPERATIONAL_TIMEOUT;

	ioa_cfg->revid = pdev->revision;

	ipr_init_ioa_cfg(ioa_cfg, host, pdev);

	ipr_regs_pci = pci_resource_start(pdev, 0);

	rc = pci_request_regions(pdev, IPR_NAME);
	if (rc < 0) {
		dev_err(&pdev->dev,
			"Couldn't register memory range of registers\n");
		goto out_scsi_host_put;
	}

	rc = pci_enable_device(pdev);

	if (rc || pci_channel_offline(pdev)) {
		if (pci_channel_offline(pdev)) {
			ipr_wait_for_pci_err_recovery(ioa_cfg);
			rc = pci_enable_device(pdev);
		}

		if (rc) {
			dev_err(&pdev->dev, "Cannot enable adapter\n");
			ipr_wait_for_pci_err_recovery(ioa_cfg);
			goto out_release_regions;
		}
	}

	ipr_regs = pci_ioremap_bar(pdev, 0);

	if (!ipr_regs) {
		dev_err(&pdev->dev,
			"Couldn't map memory range of registers\n");
		rc = -ENOMEM;
		goto out_disable;
	}

	ioa_cfg->hdw_dma_regs = ipr_regs;
	ioa_cfg->hdw_dma_regs_pci = ipr_regs_pci;
	ioa_cfg->ioa_mailbox = ioa_cfg->chip_cfg->mailbox + ipr_regs;

	ipr_init_regs(ioa_cfg);

	if (ioa_cfg->sis64) {
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
		if (rc < 0) {
			dev_dbg(&pdev->dev, "Failed to set 64 bit DMA mask\n");
			rc = dma_set_mask_and_coherent(&pdev->dev,
						       DMA_BIT_MASK(32));
		}
	} else
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		goto cleanup_nomem;
	}

	rc = pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
				   ioa_cfg->chip_cfg->cache_line_size);

	if (rc != PCIBIOS_SUCCESSFUL) {
		dev_err(&pdev->dev, "Write of cache line size failed\n");
		ipr_wait_for_pci_err_recovery(ioa_cfg);
		rc = -EIO;
		goto cleanup_nomem;
	}

	/* Issue MMIO read to ensure card is not in EEH */
	interrupts = readl(ioa_cfg->regs.sense_interrupt_reg);
	ipr_wait_for_pci_err_recovery(ioa_cfg);

	if (ipr_number_of_msix > IPR_MAX_MSIX_VECTORS) {
		dev_err(&pdev->dev, "The max number of MSIX is %d\n",
			IPR_MAX_MSIX_VECTORS);
		ipr_number_of_msix = IPR_MAX_MSIX_VECTORS;
	}

	irq_flag = PCI_IRQ_LEGACY;
	if (ioa_cfg->ipr_chip->has_msi)
		irq_flag |= PCI_IRQ_MSI | PCI_IRQ_MSIX;
	rc = pci_alloc_irq_vectors(pdev, 1, ipr_number_of_msix, irq_flag);
	if (rc < 0) {
		ipr_wait_for_pci_err_recovery(ioa_cfg);
		goto cleanup_nomem;
	}
	ioa_cfg->nvectors = rc;

	if (!pdev->msi_enabled && !pdev->msix_enabled)
		ioa_cfg->clear_isr = 1;

	pci_set_master(pdev);

	if (pci_channel_offline(pdev)) {
		ipr_wait_for_pci_err_recovery(ioa_cfg);
		pci_set_master(pdev);
		if (pci_channel_offline(pdev)) {
			rc = -EIO;
			goto out_msi_disable;
		}
	}

	if (pdev->msi_enabled || pdev->msix_enabled) {
		rc = ipr_test_msi(ioa_cfg, pdev);
		switch (rc) {
		case 0:
			dev_info(&pdev->dev,
				"Request for %d MSI%ss succeeded.", ioa_cfg->nvectors,
				pdev->msix_enabled ? "-X" : "");
			break;
		case -EOPNOTSUPP:
			ipr_wait_for_pci_err_recovery(ioa_cfg);
			pci_free_irq_vectors(pdev);

			ioa_cfg->nvectors = 1;
			ioa_cfg->clear_isr = 1;
			break;
		default:
			goto out_msi_disable;
		}
	}

	ioa_cfg->hrrq_num = min3(ioa_cfg->nvectors,
				(unsigned int)num_online_cpus(),
				(unsigned int)IPR_MAX_HRRQ_NUM);

	if ((rc = ipr_save_pcix_cmd_reg(ioa_cfg)))
		goto out_msi_disable;

	if ((rc = ipr_set_pcix_cmd_reg(ioa_cfg)))
		goto out_msi_disable;

	rc = ipr_alloc_mem(ioa_cfg);
	if (rc < 0) {
		dev_err(&pdev->dev,
			"Couldn't allocate enough memory for device driver!\n");
		goto out_msi_disable;
	}

	/* Save away PCI config space for use following IOA reset */
	rc = pci_save_state(pdev);

	if (rc != PCIBIOS_SUCCESSFUL) {
		dev_err(&pdev->dev, "Failed to save PCI config space\n");
		rc = -EIO;
		goto cleanup_nolog;
	}

	/*
	 * If HRRQ updated interrupt is not masked, or reset alert is set,
	 * the card is in an unknown state and needs a hard reset
	 */
	mask = readl(ioa_cfg->regs.sense_interrupt_mask_reg32);
	interrupts = readl(ioa_cfg->regs.sense_interrupt_reg32);
	uproc = readl(ioa_cfg->regs.sense_uproc_interrupt_reg32);
	if ((mask & IPR_PCII_HRRQ_UPDATED) == 0 || (uproc & IPR_UPROCI_RESET_ALERT))
		ioa_cfg->needs_hard_reset = 1;
	if ((interrupts & IPR_PCII_ERROR_INTERRUPTS) || reset_devices)
		ioa_cfg->needs_hard_reset = 1;
	if (interrupts & IPR_PCII_IOA_UNIT_CHECKED)
		ioa_cfg->ioa_unit_checked = 1;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ipr_mask_and_clear_interrupts(ioa_cfg, ~IPR_PCII_IOA_TRANS_TO_OPER);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	if (pdev->msi_enabled || pdev->msix_enabled) {
		name_msi_vectors(ioa_cfg);
		rc = request_irq(pci_irq_vector(pdev, 0), ipr_isr, 0,
			ioa_cfg->vectors_info[0].desc,
			&ioa_cfg->hrrq[0]);
		if (!rc)
			rc = ipr_request_other_msi_irqs(ioa_cfg, pdev);
	} else {
		rc = request_irq(pdev->irq, ipr_isr,
			 IRQF_SHARED,
			 IPR_NAME, &ioa_cfg->hrrq[0]);
	}
	if (rc) {
		dev_err(&pdev->dev, "Couldn't register IRQ %d! rc=%d\n",
			pdev->irq, rc);
		goto cleanup_nolog;
	}

	if ((dev_id->driver_data & IPR_USE_PCI_WARM_RESET) ||
	    (dev_id->device == PCI_DEVICE_ID_IBM_OBSIDIAN_E && !ioa_cfg->revid)) {
		ioa_cfg->needs_warm_reset = 1;
		ioa_cfg->reset = ipr_reset_slot_reset;

		ioa_cfg->reset_work_q = alloc_ordered_workqueue("ipr_reset_%d",
								WQ_MEM_RECLAIM, host->host_no);

		if (!ioa_cfg->reset_work_q) {
			dev_err(&pdev->dev, "Couldn't register reset workqueue\n");
			rc = -ENOMEM;
			goto out_free_irq;
		}
	} else
		ioa_cfg->reset = ipr_reset_start_bist;

	spin_lock_irqsave(&ipr_driver_lock, driver_lock_flags);
	list_add_tail(&ioa_cfg->queue, &ipr_ioa_head);
	spin_unlock_irqrestore(&ipr_driver_lock, driver_lock_flags);

	LEAVE;
out:
	return rc;

out_free_irq:
	ipr_free_irqs(ioa_cfg);
cleanup_nolog:
	ipr_free_mem(ioa_cfg);
out_msi_disable:
	ipr_wait_for_pci_err_recovery(ioa_cfg);
	pci_free_irq_vectors(pdev);
cleanup_nomem:
	iounmap(ipr_regs);
out_disable:
	pci_disable_device(pdev);
out_release_regions:
	pci_release_regions(pdev);
out_scsi_host_put:
	scsi_host_put(host);
	goto out;
}

/**
 * ipr_initiate_ioa_bringdown - Bring down an adapter
 * @ioa_cfg:		ioa config struct
 * @shutdown_type:	shutdown type
 *
 * Description: This function will initiate bringing down the adapter.
 * This consists of issuing an IOA shutdown to the adapter
 * to flush the cache, and running BIST.
 * If the caller needs to wait on the completion of the reset,
 * the caller must sleep on the reset_wait_q.
 *
 * Return value:
 * 	none
 **/
static void ipr_initiate_ioa_bringdown(struct ipr_ioa_cfg *ioa_cfg,
				       enum ipr_shutdown_type shutdown_type)
{
	ENTER;
	if (ioa_cfg->sdt_state == WAIT_FOR_DUMP)
		ioa_cfg->sdt_state = ABORT_DUMP;
	ioa_cfg->reset_retries = 0;
	ioa_cfg->in_ioa_bringdown = 1;
	ipr_initiate_ioa_reset(ioa_cfg, shutdown_type);
	LEAVE;
}

/**
 * __ipr_remove - Remove a single adapter
 * @pdev:	pci device struct
 *
 * Adapter hot plug remove entry point.
 *
 * Return value:
 * 	none
 **/
static void __ipr_remove(struct pci_dev *pdev)
{
	unsigned long host_lock_flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);
	int i;
	unsigned long driver_lock_flags;
	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);
	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);
	}

	for (i = 0; i < ioa_cfg->hrrq_num; i++) {
		spin_lock(&ioa_cfg->hrrq[i]._lock);
		ioa_cfg->hrrq[i].removing_ioa = 1;
		spin_unlock(&ioa_cfg->hrrq[i]._lock);
	}
	wmb();
	ipr_initiate_ioa_bringdown(ioa_cfg, IPR_SHUTDOWN_NORMAL);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
	flush_work(&ioa_cfg->work_q);
	if (ioa_cfg->reset_work_q)
		flush_workqueue(ioa_cfg->reset_work_q);
	INIT_LIST_HEAD(&ioa_cfg->used_res_q);
	spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);

	spin_lock_irqsave(&ipr_driver_lock, driver_lock_flags);
	list_del(&ioa_cfg->queue);
	spin_unlock_irqrestore(&ipr_driver_lock, driver_lock_flags);

	if (ioa_cfg->sdt_state == ABORT_DUMP)
		ioa_cfg->sdt_state = WAIT_FOR_DUMP;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);

	ipr_free_all_resources(ioa_cfg);

	LEAVE;
}

/**
 * ipr_remove - IOA hot plug remove entry point
 * @pdev:	pci device struct
 *
 * Adapter hot plug remove entry point.
 *
 * Return value:
 * 	none
 **/
static void ipr_remove(struct pci_dev *pdev)
{
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);

	ENTER;

	ipr_remove_trace_file(&ioa_cfg->host->shost_dev.kobj,
			      &ipr_trace_attr);
	ipr_remove_dump_file(&ioa_cfg->host->shost_dev.kobj,
			     &ipr_dump_attr);
	sysfs_remove_bin_file(&ioa_cfg->host->shost_dev.kobj,
			&ipr_ioa_async_err_log);
	scsi_remove_host(ioa_cfg->host);

	__ipr_remove(pdev);

	LEAVE;
}

/**
 * ipr_probe - Adapter hot plug add entry point
 * @pdev:	pci device struct
 * @dev_id:	pci device ID
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int ipr_probe(struct pci_dev *pdev, const struct pci_device_id *dev_id)
{
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long flags;
	int rc, i;

	rc = ipr_probe_ioa(pdev, dev_id);

	if (rc)
		return rc;

	ioa_cfg = pci_get_drvdata(pdev);
	rc = ipr_probe_ioa_part2(ioa_cfg);

	if (rc) {
		__ipr_remove(pdev);
		return rc;
	}

	rc = scsi_add_host(ioa_cfg->host, &pdev->dev);

	if (rc) {
		__ipr_remove(pdev);
		return rc;
	}

	rc = ipr_create_trace_file(&ioa_cfg->host->shost_dev.kobj,
				   &ipr_trace_attr);

	if (rc) {
		scsi_remove_host(ioa_cfg->host);
		__ipr_remove(pdev);
		return rc;
	}

	rc = sysfs_create_bin_file(&ioa_cfg->host->shost_dev.kobj,
			&ipr_ioa_async_err_log);

	if (rc) {
		ipr_remove_dump_file(&ioa_cfg->host->shost_dev.kobj,
				&ipr_dump_attr);
		ipr_remove_trace_file(&ioa_cfg->host->shost_dev.kobj,
				&ipr_trace_attr);
		scsi_remove_host(ioa_cfg->host);
		__ipr_remove(pdev);
		return rc;
	}

	rc = ipr_create_dump_file(&ioa_cfg->host->shost_dev.kobj,
				   &ipr_dump_attr);

	if (rc) {
		sysfs_remove_bin_file(&ioa_cfg->host->shost_dev.kobj,
				      &ipr_ioa_async_err_log);
		ipr_remove_trace_file(&ioa_cfg->host->shost_dev.kobj,
				      &ipr_trace_attr);
		scsi_remove_host(ioa_cfg->host);
		__ipr_remove(pdev);
		return rc;
	}
	spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
	ioa_cfg->scan_enabled = 1;
	schedule_work(&ioa_cfg->work_q);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);

	ioa_cfg->iopoll_weight = ioa_cfg->chip_cfg->iopoll_weight;

	if (ioa_cfg->iopoll_weight && ioa_cfg->sis64 && ioa_cfg->nvectors > 1) {
		for (i = 1; i < ioa_cfg->hrrq_num; i++) {
			irq_poll_init(&ioa_cfg->hrrq[i].iopoll,
					ioa_cfg->iopoll_weight, ipr_iopoll);
		}
	}

	scsi_scan_host(ioa_cfg->host);

	return 0;
}

/**
 * ipr_shutdown - Shutdown handler.
 * @pdev:	pci device struct
 *
 * This function is invoked upon system shutdown/reboot. It will issue
 * an adapter shutdown to the adapter to flush the write cache.
 *
 * Return value:
 * 	none
 **/
static void ipr_shutdown(struct pci_dev *pdev)
{
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);
	unsigned long lock_flags = 0;
	enum ipr_shutdown_type shutdown_type = IPR_SHUTDOWN_NORMAL;
	int i;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	if (ioa_cfg->iopoll_weight && ioa_cfg->sis64 && ioa_cfg->nvectors > 1) {
		ioa_cfg->iopoll_weight = 0;
		for (i = 1; i < ioa_cfg->hrrq_num; i++)
			irq_poll_disable(&ioa_cfg->hrrq[i].iopoll);
	}

	while (ioa_cfg->in_reset_reload) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	if (ipr_fast_reboot && system_state == SYSTEM_RESTART && ioa_cfg->sis64)
		shutdown_type = IPR_SHUTDOWN_QUIESCE;

	ipr_initiate_ioa_bringdown(ioa_cfg, shutdown_type);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	wait_event(ioa_cfg->reset_wait_q, !ioa_cfg->in_reset_reload);
	if (ipr_fast_reboot && system_state == SYSTEM_RESTART && ioa_cfg->sis64) {
		ipr_free_irqs(ioa_cfg);
		pci_disable_device(ioa_cfg->pdev);
	}
}

static struct pci_device_id ipr_pci_table[] = {
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_5702, 0, 0, 0 },
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_5703, 0, 0, 0 },
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_573D, 0, 0, 0 },
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_573E, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CITRINE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_571B, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CITRINE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572E, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CITRINE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_571A, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CITRINE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_575B, 0, 0,
		IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572A, 0, 0, 0 },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572B, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_575C, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572A, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572B, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT},
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_575C, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN_E,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_574E, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN_E,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B3, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN_E,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57CC, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_OBSIDIAN_E,
	      PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B7, 0, 0,
	      IPR_USE_LONG_TRANSOP_TIMEOUT | IPR_USE_PCI_WARM_RESET },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_SNIPE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_2780, 0, 0, 0 },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_SCAMP,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_571E, 0, 0, 0 },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_SCAMP,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_571F, 0, 0,
		IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_ADAPTEC2, PCI_DEVICE_ID_ADAPTEC2_SCAMP,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_572F, 0, 0,
		IPR_USE_LONG_TRANSOP_TIMEOUT },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B5, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_574D, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B2, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57C0, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57C3, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROC_FPGA_E2,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57C4, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B4, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57B1, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57C6, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57C8, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57CE, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57D5, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57D6, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57D7, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57D8, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57D9, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57DA, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57EB, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57EC, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57ED, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57EE, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57EF, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_57F0, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_2CCA, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_2CD2, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CROCODILE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_2CCD, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_RATTLESNAKE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_580A, 0, 0, 0 },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_RATTLESNAKE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_580B, 0, 0, 0 },
	{ }
};
MODULE_DEVICE_TABLE(pci, ipr_pci_table);

static const struct pci_error_handlers ipr_err_handler = {
	.error_detected = ipr_pci_error_detected,
	.mmio_enabled = ipr_pci_mmio_enabled,
	.slot_reset = ipr_pci_slot_reset,
};

static struct pci_driver ipr_driver = {
	.name = IPR_NAME,
	.id_table = ipr_pci_table,
	.probe = ipr_probe,
	.remove = ipr_remove,
	.shutdown = ipr_shutdown,
	.err_handler = &ipr_err_handler,
};

/**
 * ipr_halt_done - Shutdown prepare completion
 * @ipr_cmd:   ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_halt_done(struct ipr_cmnd *ipr_cmd)
{
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->hrrq->hrrq_free_q);
}

/**
 * ipr_halt - Issue shutdown prepare to all adapters
 * @nb: Notifier block
 * @event: Notifier event
 * @buf: Notifier data (unused)
 *
 * Return value:
 * 	NOTIFY_OK on success / NOTIFY_DONE on failure
 **/
static int ipr_halt(struct notifier_block *nb, ulong event, void *buf)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long flags = 0, driver_lock_flags;

	if (event != SYS_RESTART && event != SYS_HALT && event != SYS_POWER_OFF)
		return NOTIFY_DONE;

	spin_lock_irqsave(&ipr_driver_lock, driver_lock_flags);

	list_for_each_entry(ioa_cfg, &ipr_ioa_head, queue) {
		spin_lock_irqsave(ioa_cfg->host->host_lock, flags);
		if (!ioa_cfg->hrrq[IPR_INIT_HRRQ].allow_cmds ||
		    (ipr_fast_reboot && event == SYS_RESTART && ioa_cfg->sis64)) {
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
			continue;
		}

		ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
		ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
		ipr_cmd->ioarcb.cmd_pkt.cdb[0] = IPR_IOA_SHUTDOWN;
		ipr_cmd->ioarcb.cmd_pkt.cdb[1] = IPR_SHUTDOWN_PREPARE_FOR_NORMAL;

		ipr_do_req(ipr_cmd, ipr_halt_done, ipr_timeout, IPR_DEVICE_RESET_TIMEOUT);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, flags);
	}
	spin_unlock_irqrestore(&ipr_driver_lock, driver_lock_flags);

	return NOTIFY_OK;
}

static struct notifier_block ipr_notifier = {
	ipr_halt, NULL, 0
};

/**
 * ipr_init - Module entry point
 *
 * Return value:
 * 	0 on success / negative value on failure
 **/
static int __init ipr_init(void)
{
	int rc;

	ipr_info("IBM Power RAID SCSI Device Driver version: %s %s\n",
		 IPR_DRIVER_VERSION, IPR_DRIVER_DATE);

	register_reboot_notifier(&ipr_notifier);
	rc = pci_register_driver(&ipr_driver);
	if (rc) {
		unregister_reboot_notifier(&ipr_notifier);
		return rc;
	}

	return 0;
}

/**
 * ipr_exit - Module unload
 *
 * Module unload entry point.
 *
 * Return value:
 * 	none
 **/
static void __exit ipr_exit(void)
{
	unregister_reboot_notifier(&ipr_notifier);
	pci_unregister_driver(&ipr_driver);
}

module_init(ipr_init);
module_exit(ipr_exit);

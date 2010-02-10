/*
 *	procfs handler for Linux I2O subsystem
 *
 *	(c) Copyright 1999	Deepak Saxena
 *
 *	Originally written by Deepak Saxena(deepak@plexity.net)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	This is an initial test release. The code is based on the design of the
 *	ide procfs system (drivers/block/ide-proc.c). Some code taken from
 *	i2o-core module by Alan Cox.
 *
 *	DISCLAIMER: This code is still under development/test and may cause
 *	your system to behave unpredictably.  Use at your own discretion.
 *
 *
 *	Fixes/additions:
 *		Juha Sievänen (Juha.Sievanen@cs.Helsinki.FI),
 *		Auvo Häkkinen (Auvo.Hakkinen@cs.Helsinki.FI)
 *		University of Helsinki, Department of Computer Science
 *			LAN entries
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			Changes for new I2O API
 */

#define OSM_NAME	"proc-osm"
#define OSM_VERSION	"1.316"
#define OSM_DESCRIPTION	"I2O ProcFS OSM"

#define I2O_MAX_MODULES 4
// FIXME!
#define FMT_U64_HEX "0x%08x%08x"
#define U64_VAL(pu64) *((u32*)(pu64)+1), *((u32*)(pu64))

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/i2o.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

/* Structure used to define /proc entries */
typedef struct _i2o_proc_entry_t {
	char *name;		/* entry name */
	mode_t mode;		/* mode */
	const struct file_operations *fops;	/* open function */
} i2o_proc_entry;

/* global I2O /proc/i2o entry */
static struct proc_dir_entry *i2o_proc_dir_root;

/* proc OSM driver struct */
static struct i2o_driver i2o_proc_driver = {
	.name = OSM_NAME,
};

static int print_serial_number(struct seq_file *seq, u8 * serialno, int max_len)
{
	int i;

	/* 19990419 -sralston
	 *      The I2O v1.5 (and v2.0 so far) "official specification"
	 *      got serial numbers WRONG!
	 *      Apparently, and despite what Section 3.4.4 says and
	 *      Figure 3-35 shows (pg 3-39 in the pdf doc),
	 *      the convention / consensus seems to be:
	 *        + First byte is SNFormat
	 *        + Second byte is SNLen (but only if SNFormat==7 (?))
	 *        + (v2.0) SCSI+BS may use IEEE Registered (64 or 128 bit) format
	 */
	switch (serialno[0]) {
	case I2O_SNFORMAT_BINARY:	/* Binary */
		seq_printf(seq, "0x");
		for (i = 0; i < serialno[1]; i++) {
			seq_printf(seq, "%02X", serialno[2 + i]);
		}
		break;

	case I2O_SNFORMAT_ASCII:	/* ASCII */
		if (serialno[1] < ' ') {	/* printable or SNLen? */
			/* sanity */
			max_len =
			    (max_len < serialno[1]) ? max_len : serialno[1];
			serialno[1 + max_len] = '\0';

			/* just print it */
			seq_printf(seq, "%s", &serialno[2]);
		} else {
			/* print chars for specified length */
			for (i = 0; i < serialno[1]; i++) {
				seq_printf(seq, "%c", serialno[2 + i]);
			}
		}
		break;

	case I2O_SNFORMAT_UNICODE:	/* UNICODE */
		seq_printf(seq, "UNICODE Format.  Can't Display\n");
		break;

	case I2O_SNFORMAT_LAN48_MAC:	/* LAN-48 MAC Address */
		seq_printf(seq, "LAN-48 MAC address @ %pM", &serialno[2]);
		break;

	case I2O_SNFORMAT_WAN:	/* WAN MAC Address */
		/* FIXME: Figure out what a WAN access address looks like?? */
		seq_printf(seq, "WAN Access Address");
		break;

/* plus new in v2.0 */
	case I2O_SNFORMAT_LAN64_MAC:	/* LAN-64 MAC Address */
		/* FIXME: Figure out what a LAN-64 address really looks like?? */
		seq_printf(seq,
			   "LAN-64 MAC address @ [?:%02X:%02X:?] %pM",
			   serialno[8], serialno[9], &serialno[2]);
		break;

	case I2O_SNFORMAT_DDM:	/* I2O DDM */
		seq_printf(seq,
			   "DDM: Tid=%03Xh, Rsvd=%04Xh, OrgId=%04Xh",
			   *(u16 *) & serialno[2],
			   *(u16 *) & serialno[4], *(u16 *) & serialno[6]);
		break;

	case I2O_SNFORMAT_IEEE_REG64:	/* IEEE Registered (64-bit) */
	case I2O_SNFORMAT_IEEE_REG128:	/* IEEE Registered (128-bit) */
		/* FIXME: Figure if this is even close?? */
		seq_printf(seq,
			   "IEEE NodeName(hi,lo)=(%08Xh:%08Xh), PortName(hi,lo)=(%08Xh:%08Xh)\n",
			   *(u32 *) & serialno[2],
			   *(u32 *) & serialno[6],
			   *(u32 *) & serialno[10], *(u32 *) & serialno[14]);
		break;

	case I2O_SNFORMAT_UNKNOWN:	/* Unknown 0    */
	case I2O_SNFORMAT_UNKNOWN2:	/* Unknown 0xff */
	default:
		seq_printf(seq, "Unknown data format (0x%02x)", serialno[0]);
		break;
	}

	return 0;
}

/**
 *	i2o_get_class_name - 	do i2o class name lookup
 *	@class: class number
 *
 *	Return a descriptive string for an i2o class.
 */
static const char *i2o_get_class_name(int class)
{
	int idx = 16;
	static char *i2o_class_name[] = {
		"Executive",
		"Device Driver Module",
		"Block Device",
		"Tape Device",
		"LAN Interface",
		"WAN Interface",
		"Fibre Channel Port",
		"Fibre Channel Device",
		"SCSI Device",
		"ATE Port",
		"ATE Device",
		"Floppy Controller",
		"Floppy Device",
		"Secondary Bus Port",
		"Peer Transport Agent",
		"Peer Transport",
		"Unknown"
	};

	switch (class & 0xfff) {
	case I2O_CLASS_EXECUTIVE:
		idx = 0;
		break;
	case I2O_CLASS_DDM:
		idx = 1;
		break;
	case I2O_CLASS_RANDOM_BLOCK_STORAGE:
		idx = 2;
		break;
	case I2O_CLASS_SEQUENTIAL_STORAGE:
		idx = 3;
		break;
	case I2O_CLASS_LAN:
		idx = 4;
		break;
	case I2O_CLASS_WAN:
		idx = 5;
		break;
	case I2O_CLASS_FIBRE_CHANNEL_PORT:
		idx = 6;
		break;
	case I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL:
		idx = 7;
		break;
	case I2O_CLASS_SCSI_PERIPHERAL:
		idx = 8;
		break;
	case I2O_CLASS_ATE_PORT:
		idx = 9;
		break;
	case I2O_CLASS_ATE_PERIPHERAL:
		idx = 10;
		break;
	case I2O_CLASS_FLOPPY_CONTROLLER:
		idx = 11;
		break;
	case I2O_CLASS_FLOPPY_DEVICE:
		idx = 12;
		break;
	case I2O_CLASS_BUS_ADAPTER:
		idx = 13;
		break;
	case I2O_CLASS_PEER_TRANSPORT_AGENT:
		idx = 14;
		break;
	case I2O_CLASS_PEER_TRANSPORT:
		idx = 15;
		break;
	}

	return i2o_class_name[idx];
}

#define SCSI_TABLE_SIZE	13
static char *scsi_devices[] = {
	"Direct-Access Read/Write",
	"Sequential-Access Storage",
	"Printer",
	"Processor",
	"WORM Device",
	"CD-ROM Device",
	"Scanner Device",
	"Optical Memory Device",
	"Medium Changer Device",
	"Communications Device",
	"Graphics Art Pre-Press Device",
	"Graphics Art Pre-Press Device",
	"Array Controller Device"
};

static char *chtostr(u8 * chars, int n)
{
	char tmp[256];
	tmp[0] = 0;
	return strncat(tmp, (char *)chars, n);
}

static int i2o_report_query_status(struct seq_file *seq, int block_status,
				   char *group)
{
	switch (block_status) {
	case -ETIMEDOUT:
		return seq_printf(seq, "Timeout reading group %s.\n", group);
	case -ENOMEM:
		return seq_printf(seq, "No free memory to read the table.\n");
	case -I2O_PARAMS_STATUS_INVALID_GROUP_ID:
		return seq_printf(seq, "Group %s not supported.\n", group);
	default:
		return seq_printf(seq,
				  "Error reading group %s. BlockStatus 0x%02X\n",
				  group, -block_status);
	}
}

static char *bus_strings[] = {
	"Local Bus",
	"ISA",
	"EISA",
	"MCA",
	"PCI",
	"PCMCIA",
	"NUBUS",
	"CARDBUS"
};

static int i2o_seq_show_hrt(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	i2o_hrt *hrt = (i2o_hrt *) c->hrt.virt;
	u32 bus;
	int i;

	if (hrt->hrt_version) {
		seq_printf(seq,
			   "HRT table for controller is too new a version.\n");
		return 0;
	}

	seq_printf(seq, "HRT has %d entries of %d bytes each.\n",
		   hrt->num_entries, hrt->entry_len << 2);

	for (i = 0; i < hrt->num_entries; i++) {
		seq_printf(seq, "Entry %d:\n", i);
		seq_printf(seq, "   Adapter ID: %0#10x\n",
			   hrt->hrt_entry[i].adapter_id);
		seq_printf(seq, "   Controlling tid: %0#6x\n",
			   hrt->hrt_entry[i].parent_tid);

		if (hrt->hrt_entry[i].bus_type != 0x80) {
			bus = hrt->hrt_entry[i].bus_type;
			seq_printf(seq, "   %s Information\n",
				   bus_strings[bus]);

			switch (bus) {
			case I2O_BUS_LOCAL:
				seq_printf(seq, "     IOBase: %0#6x,",
					   hrt->hrt_entry[i].bus.local_bus.
					   LbBaseIOPort);
				seq_printf(seq, " MemoryBase: %0#10x\n",
					   hrt->hrt_entry[i].bus.local_bus.
					   LbBaseMemoryAddress);
				break;

			case I2O_BUS_ISA:
				seq_printf(seq, "     IOBase: %0#6x,",
					   hrt->hrt_entry[i].bus.isa_bus.
					   IsaBaseIOPort);
				seq_printf(seq, " MemoryBase: %0#10x,",
					   hrt->hrt_entry[i].bus.isa_bus.
					   IsaBaseMemoryAddress);
				seq_printf(seq, " CSN: %0#4x,",
					   hrt->hrt_entry[i].bus.isa_bus.CSN);
				break;

			case I2O_BUS_EISA:
				seq_printf(seq, "     IOBase: %0#6x,",
					   hrt->hrt_entry[i].bus.eisa_bus.
					   EisaBaseIOPort);
				seq_printf(seq, " MemoryBase: %0#10x,",
					   hrt->hrt_entry[i].bus.eisa_bus.
					   EisaBaseMemoryAddress);
				seq_printf(seq, " Slot: %0#4x,",
					   hrt->hrt_entry[i].bus.eisa_bus.
					   EisaSlotNumber);
				break;

			case I2O_BUS_MCA:
				seq_printf(seq, "     IOBase: %0#6x,",
					   hrt->hrt_entry[i].bus.mca_bus.
					   McaBaseIOPort);
				seq_printf(seq, " MemoryBase: %0#10x,",
					   hrt->hrt_entry[i].bus.mca_bus.
					   McaBaseMemoryAddress);
				seq_printf(seq, " Slot: %0#4x,",
					   hrt->hrt_entry[i].bus.mca_bus.
					   McaSlotNumber);
				break;

			case I2O_BUS_PCI:
				seq_printf(seq, "     Bus: %0#4x",
					   hrt->hrt_entry[i].bus.pci_bus.
					   PciBusNumber);
				seq_printf(seq, " Dev: %0#4x",
					   hrt->hrt_entry[i].bus.pci_bus.
					   PciDeviceNumber);
				seq_printf(seq, " Func: %0#4x",
					   hrt->hrt_entry[i].bus.pci_bus.
					   PciFunctionNumber);
				seq_printf(seq, " Vendor: %0#6x",
					   hrt->hrt_entry[i].bus.pci_bus.
					   PciVendorID);
				seq_printf(seq, " Device: %0#6x\n",
					   hrt->hrt_entry[i].bus.pci_bus.
					   PciDeviceID);
				break;

			default:
				seq_printf(seq, "      Unsupported Bus Type\n");
			}
		} else
			seq_printf(seq, "   Unknown Bus Type\n");
	}

	return 0;
}

static int i2o_seq_show_lct(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	i2o_lct *lct = (i2o_lct *) c->lct;
	int entries;
	int i;

#define BUS_TABLE_SIZE 3
	static char *bus_ports[] = {
		"Generic Bus",
		"SCSI Bus",
		"Fibre Channel Bus"
	};

	entries = (lct->table_size - 3) / 9;

	seq_printf(seq, "LCT contains %d %s\n", entries,
		   entries == 1 ? "entry" : "entries");
	if (lct->boot_tid)
		seq_printf(seq, "Boot Device @ ID %d\n", lct->boot_tid);

	seq_printf(seq, "Current Change Indicator: %#10x\n", lct->change_ind);

	for (i = 0; i < entries; i++) {
		seq_printf(seq, "Entry %d\n", i);
		seq_printf(seq, "  Class, SubClass  : %s",
			   i2o_get_class_name(lct->lct_entry[i].class_id));

		/*
		 *      Classes which we'll print subclass info for
		 */
		switch (lct->lct_entry[i].class_id & 0xFFF) {
		case I2O_CLASS_RANDOM_BLOCK_STORAGE:
			switch (lct->lct_entry[i].sub_class) {
			case 0x00:
				seq_printf(seq, ", Direct-Access Read/Write");
				break;

			case 0x04:
				seq_printf(seq, ", WORM Drive");
				break;

			case 0x05:
				seq_printf(seq, ", CD-ROM Drive");
				break;

			case 0x07:
				seq_printf(seq, ", Optical Memory Device");
				break;

			default:
				seq_printf(seq, ", Unknown (0x%02x)",
					   lct->lct_entry[i].sub_class);
				break;
			}
			break;

		case I2O_CLASS_LAN:
			switch (lct->lct_entry[i].sub_class & 0xFF) {
			case 0x30:
				seq_printf(seq, ", Ethernet");
				break;

			case 0x40:
				seq_printf(seq, ", 100base VG");
				break;

			case 0x50:
				seq_printf(seq, ", IEEE 802.5/Token-Ring");
				break;

			case 0x60:
				seq_printf(seq, ", ANSI X3T9.5 FDDI");
				break;

			case 0x70:
				seq_printf(seq, ", Fibre Channel");
				break;

			default:
				seq_printf(seq, ", Unknown Sub-Class (0x%02x)",
					   lct->lct_entry[i].sub_class & 0xFF);
				break;
			}
			break;

		case I2O_CLASS_SCSI_PERIPHERAL:
			if (lct->lct_entry[i].sub_class < SCSI_TABLE_SIZE)
				seq_printf(seq, ", %s",
					   scsi_devices[lct->lct_entry[i].
							sub_class]);
			else
				seq_printf(seq, ", Unknown Device Type");
			break;

		case I2O_CLASS_BUS_ADAPTER:
			if (lct->lct_entry[i].sub_class < BUS_TABLE_SIZE)
				seq_printf(seq, ", %s",
					   bus_ports[lct->lct_entry[i].
						     sub_class]);
			else
				seq_printf(seq, ", Unknown Bus Type");
			break;
		}
		seq_printf(seq, "\n");

		seq_printf(seq, "  Local TID        : 0x%03x\n",
			   lct->lct_entry[i].tid);
		seq_printf(seq, "  User TID         : 0x%03x\n",
			   lct->lct_entry[i].user_tid);
		seq_printf(seq, "  Parent TID       : 0x%03x\n",
			   lct->lct_entry[i].parent_tid);
		seq_printf(seq, "  Identity Tag     : 0x%x%x%x%x%x%x%x%x\n",
			   lct->lct_entry[i].identity_tag[0],
			   lct->lct_entry[i].identity_tag[1],
			   lct->lct_entry[i].identity_tag[2],
			   lct->lct_entry[i].identity_tag[3],
			   lct->lct_entry[i].identity_tag[4],
			   lct->lct_entry[i].identity_tag[5],
			   lct->lct_entry[i].identity_tag[6],
			   lct->lct_entry[i].identity_tag[7]);
		seq_printf(seq, "  Change Indicator : %0#10x\n",
			   lct->lct_entry[i].change_ind);
		seq_printf(seq, "  Event Capab Mask : %0#10x\n",
			   lct->lct_entry[i].device_flags);
	}

	return 0;
}

static int i2o_seq_show_status(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	char prodstr[25];
	int version;
	i2o_status_block *sb = c->status_block.virt;

	i2o_status_get(c);	// reread the status block

	seq_printf(seq, "Organization ID        : %0#6x\n", sb->org_id);

	version = sb->i2o_version;

/* FIXME for Spec 2.0
	if (version == 0x02) {
		seq_printf(seq, "Lowest I2O version supported: ");
		switch(workspace[2]) {
			case 0x00:
				seq_printf(seq, "1.0\n");
				break;
			case 0x01:
				seq_printf(seq, "1.5\n");
				break;
			case 0x02:
				seq_printf(seq, "2.0\n");
				break;
		}

		seq_printf(seq, "Highest I2O version supported: ");
		switch(workspace[3]) {
			case 0x00:
				seq_printf(seq, "1.0\n");
				break;
			case 0x01:
				seq_printf(seq, "1.5\n");
				break;
			case 0x02:
				seq_printf(seq, "2.0\n");
				break;
		}
	}
*/
	seq_printf(seq, "IOP ID                 : %0#5x\n", sb->iop_id);
	seq_printf(seq, "Host Unit ID           : %0#6x\n", sb->host_unit_id);
	seq_printf(seq, "Segment Number         : %0#5x\n", sb->segment_number);

	seq_printf(seq, "I2O version            : ");
	switch (version) {
	case 0x00:
		seq_printf(seq, "1.0\n");
		break;
	case 0x01:
		seq_printf(seq, "1.5\n");
		break;
	case 0x02:
		seq_printf(seq, "2.0\n");
		break;
	default:
		seq_printf(seq, "Unknown version\n");
	}

	seq_printf(seq, "IOP State              : ");
	switch (sb->iop_state) {
	case 0x01:
		seq_printf(seq, "INIT\n");
		break;

	case 0x02:
		seq_printf(seq, "RESET\n");
		break;

	case 0x04:
		seq_printf(seq, "HOLD\n");
		break;

	case 0x05:
		seq_printf(seq, "READY\n");
		break;

	case 0x08:
		seq_printf(seq, "OPERATIONAL\n");
		break;

	case 0x10:
		seq_printf(seq, "FAILED\n");
		break;

	case 0x11:
		seq_printf(seq, "FAULTED\n");
		break;

	default:
		seq_printf(seq, "Unknown\n");
		break;
	}

	seq_printf(seq, "Messenger Type         : ");
	switch (sb->msg_type) {
	case 0x00:
		seq_printf(seq, "Memory mapped\n");
		break;
	case 0x01:
		seq_printf(seq, "Memory mapped only\n");
		break;
	case 0x02:
		seq_printf(seq, "Remote only\n");
		break;
	case 0x03:
		seq_printf(seq, "Memory mapped and remote\n");
		break;
	default:
		seq_printf(seq, "Unknown\n");
	}

	seq_printf(seq, "Inbound Frame Size     : %d bytes\n",
		   sb->inbound_frame_size << 2);
	seq_printf(seq, "Max Inbound Frames     : %d\n",
		   sb->max_inbound_frames);
	seq_printf(seq, "Current Inbound Frames : %d\n",
		   sb->cur_inbound_frames);
	seq_printf(seq, "Max Outbound Frames    : %d\n",
		   sb->max_outbound_frames);

	/* Spec doesn't say if NULL terminated or not... */
	memcpy(prodstr, sb->product_id, 24);
	prodstr[24] = '\0';
	seq_printf(seq, "Product ID             : %s\n", prodstr);
	seq_printf(seq, "Expected LCT Size      : %d bytes\n",
		   sb->expected_lct_size);

	seq_printf(seq, "IOP Capabilities\n");
	seq_printf(seq, "    Context Field Size Support : ");
	switch (sb->iop_capabilities & 0x0000003) {
	case 0:
		seq_printf(seq, "Supports only 32-bit context fields\n");
		break;
	case 1:
		seq_printf(seq, "Supports only 64-bit context fields\n");
		break;
	case 2:
		seq_printf(seq, "Supports 32-bit and 64-bit context fields, "
			   "but not concurrently\n");
		break;
	case 3:
		seq_printf(seq, "Supports 32-bit and 64-bit context fields "
			   "concurrently\n");
		break;
	default:
		seq_printf(seq, "0x%08x\n", sb->iop_capabilities);
	}
	seq_printf(seq, "    Current Context Field Size : ");
	switch (sb->iop_capabilities & 0x0000000C) {
	case 0:
		seq_printf(seq, "not configured\n");
		break;
	case 4:
		seq_printf(seq, "Supports only 32-bit context fields\n");
		break;
	case 8:
		seq_printf(seq, "Supports only 64-bit context fields\n");
		break;
	case 12:
		seq_printf(seq, "Supports both 32-bit or 64-bit context fields "
			   "concurrently\n");
		break;
	default:
		seq_printf(seq, "\n");
	}
	seq_printf(seq, "    Inbound Peer Support       : %s\n",
		   (sb->
		    iop_capabilities & 0x00000010) ? "Supported" :
		   "Not supported");
	seq_printf(seq, "    Outbound Peer Support      : %s\n",
		   (sb->
		    iop_capabilities & 0x00000020) ? "Supported" :
		   "Not supported");
	seq_printf(seq, "    Peer to Peer Support       : %s\n",
		   (sb->
		    iop_capabilities & 0x00000040) ? "Supported" :
		   "Not supported");

	seq_printf(seq, "Desired private memory size   : %d kB\n",
		   sb->desired_mem_size >> 10);
	seq_printf(seq, "Allocated private memory size : %d kB\n",
		   sb->current_mem_size >> 10);
	seq_printf(seq, "Private memory base address   : %0#10x\n",
		   sb->current_mem_base);
	seq_printf(seq, "Desired private I/O size      : %d kB\n",
		   sb->desired_io_size >> 10);
	seq_printf(seq, "Allocated private I/O size    : %d kB\n",
		   sb->current_io_size >> 10);
	seq_printf(seq, "Private I/O base address      : %0#10x\n",
		   sb->current_io_base);

	return 0;
}

static int i2o_seq_show_hw(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	static u32 work32[5];
	static u8 *work8 = (u8 *) work32;
	static u16 *work16 = (u16 *) work32;
	int token;
	u32 hwcap;

	static char *cpu_table[] = {
		"Intel 80960 series",
		"AMD2900 series",
		"Motorola 68000 series",
		"ARM series",
		"MIPS series",
		"Sparc series",
		"PowerPC series",
		"Intel x86 series"
	};

	token =
	    i2o_parm_field_get(c->exec, 0x0000, -1, &work32, sizeof(work32));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0x0000 IOP Hardware");
		return 0;
	}

	seq_printf(seq, "I2O Vendor ID    : %0#6x\n", work16[0]);
	seq_printf(seq, "Product ID       : %0#6x\n", work16[1]);
	seq_printf(seq, "CPU              : ");
	if (work8[16] > 8)
		seq_printf(seq, "Unknown\n");
	else
		seq_printf(seq, "%s\n", cpu_table[work8[16]]);
	/* Anyone using ProcessorVersion? */

	seq_printf(seq, "RAM              : %dkB\n", work32[1] >> 10);
	seq_printf(seq, "Non-Volatile Mem : %dkB\n", work32[2] >> 10);

	hwcap = work32[3];
	seq_printf(seq, "Capabilities : 0x%08x\n", hwcap);
	seq_printf(seq, "   [%s] Self booting\n",
		   (hwcap & 0x00000001) ? "+" : "-");
	seq_printf(seq, "   [%s] Upgradable IRTOS\n",
		   (hwcap & 0x00000002) ? "+" : "-");
	seq_printf(seq, "   [%s] Supports downloading DDMs\n",
		   (hwcap & 0x00000004) ? "+" : "-");
	seq_printf(seq, "   [%s] Supports installing DDMs\n",
		   (hwcap & 0x00000008) ? "+" : "-");
	seq_printf(seq, "   [%s] Battery-backed RAM\n",
		   (hwcap & 0x00000010) ? "+" : "-");

	return 0;
}

/* Executive group 0003h - Executing DDM List (table) */
static int i2o_seq_show_ddm_table(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	int token;
	int i;

	typedef struct _i2o_exec_execute_ddm_table {
		u16 ddm_tid;
		u8 module_type;
		u8 reserved;
		u16 i2o_vendor_id;
		u16 module_id;
		u8 module_name_version[28];
		u32 data_size;
		u32 code_size;
	} i2o_exec_execute_ddm_table;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_exec_execute_ddm_table ddm_table[I2O_MAX_MODULES];
	} *result;

	i2o_exec_execute_ddm_table ddm_table;

	result = kmalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	token = i2o_parm_table_get(c->exec, I2O_PARAMS_TABLE_GET, 0x0003, -1,
				   NULL, 0, result, sizeof(*result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0x0003 Executing DDM List");
		goto out;
	}

	seq_printf(seq,
		   "Tid   Module_type     Vendor Mod_id  Module_name             Vrs  Data_size Code_size\n");
	ddm_table = result->ddm_table[0];

	for (i = 0; i < result->row_count; ddm_table = result->ddm_table[++i]) {
		seq_printf(seq, "0x%03x ", ddm_table.ddm_tid & 0xFFF);

		switch (ddm_table.module_type) {
		case 0x01:
			seq_printf(seq, "Downloaded DDM  ");
			break;
		case 0x22:
			seq_printf(seq, "Embedded DDM    ");
			break;
		default:
			seq_printf(seq, "                ");
		}

		seq_printf(seq, "%-#7x", ddm_table.i2o_vendor_id);
		seq_printf(seq, "%-#8x", ddm_table.module_id);
		seq_printf(seq, "%-29s",
			   chtostr(ddm_table.module_name_version, 28));
		seq_printf(seq, "%9d  ", ddm_table.data_size);
		seq_printf(seq, "%8d", ddm_table.code_size);

		seq_printf(seq, "\n");
	}
      out:
	kfree(result);
	return 0;
}

/* Executive group 0004h - Driver Store (scalar) */
static int i2o_seq_show_driver_store(struct seq_file *seq, void *v)
{
	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	u32 work32[8];
	int token;

	token =
	    i2o_parm_field_get(c->exec, 0x0004, -1, &work32, sizeof(work32));
	if (token < 0) {
		i2o_report_query_status(seq, token, "0x0004 Driver Store");
		return 0;
	}

	seq_printf(seq, "Module limit  : %d\n"
		   "Module count  : %d\n"
		   "Current space : %d kB\n"
		   "Free space    : %d kB\n",
		   work32[0], work32[1], work32[2] >> 10, work32[3] >> 10);

	return 0;
}

/* Executive group 0005h - Driver Store Table (table) */
static int i2o_seq_show_drivers_stored(struct seq_file *seq, void *v)
{
	typedef struct _i2o_driver_store {
		u16 stored_ddm_index;
		u8 module_type;
		u8 reserved;
		u16 i2o_vendor_id;
		u16 module_id;
		u8 module_name_version[28];
		u8 date[8];
		u32 module_size;
		u32 mpb_size;
		u32 module_flags;
	} i2o_driver_store_table;

	struct i2o_controller *c = (struct i2o_controller *)seq->private;
	int token;
	int i;

	typedef struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_driver_store_table dst[I2O_MAX_MODULES];
	} i2o_driver_result_table;

	i2o_driver_result_table *result;
	i2o_driver_store_table *dst;

	result = kmalloc(sizeof(i2o_driver_result_table), GFP_KERNEL);
	if (result == NULL)
		return -ENOMEM;

	token = i2o_parm_table_get(c->exec, I2O_PARAMS_TABLE_GET, 0x0005, -1,
				   NULL, 0, result, sizeof(*result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0x0005 DRIVER STORE TABLE");
		kfree(result);
		return 0;
	}

	seq_printf(seq,
		   "#  Module_type     Vendor Mod_id  Module_name             Vrs"
		   "Date     Mod_size Par_size Flags\n");
	for (i = 0, dst = &result->dst[0]; i < result->row_count;
	     dst = &result->dst[++i]) {
		seq_printf(seq, "%-3d", dst->stored_ddm_index);
		switch (dst->module_type) {
		case 0x01:
			seq_printf(seq, "Downloaded DDM  ");
			break;
		case 0x22:
			seq_printf(seq, "Embedded DDM    ");
			break;
		default:
			seq_printf(seq, "                ");
		}

		seq_printf(seq, "%-#7x", dst->i2o_vendor_id);
		seq_printf(seq, "%-#8x", dst->module_id);
		seq_printf(seq, "%-29s", chtostr(dst->module_name_version, 28));
		seq_printf(seq, "%-9s", chtostr(dst->date, 8));
		seq_printf(seq, "%8d ", dst->module_size);
		seq_printf(seq, "%8d ", dst->mpb_size);
		seq_printf(seq, "0x%04x", dst->module_flags);
		seq_printf(seq, "\n");
	}

	kfree(result);
	return 0;
}

/* Generic group F000h - Params Descriptor (table) */
static int i2o_seq_show_groups(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;
	u8 properties;

	typedef struct _i2o_group_info {
		u16 group_number;
		u16 field_count;
		u16 row_count;
		u8 properties;
		u8 reserved;
	} i2o_group_info;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_group_info group[256];
	} *result;

	result = kmalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF000, -1, NULL, 0,
				   result, sizeof(*result));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF000 Params Descriptor");
		goto out;
	}

	seq_printf(seq,
		   "#  Group   FieldCount RowCount Type   Add Del Clear\n");

	for (i = 0; i < result->row_count; i++) {
		seq_printf(seq, "%-3d", i);
		seq_printf(seq, "0x%04X ", result->group[i].group_number);
		seq_printf(seq, "%10d ", result->group[i].field_count);
		seq_printf(seq, "%8d ", result->group[i].row_count);

		properties = result->group[i].properties;
		if (properties & 0x1)
			seq_printf(seq, "Table  ");
		else
			seq_printf(seq, "Scalar ");
		if (properties & 0x2)
			seq_printf(seq, " + ");
		else
			seq_printf(seq, " - ");
		if (properties & 0x4)
			seq_printf(seq, "  + ");
		else
			seq_printf(seq, "  - ");
		if (properties & 0x8)
			seq_printf(seq, "  + ");
		else
			seq_printf(seq, "  - ");

		seq_printf(seq, "\n");
	}

	if (result->more_flag)
		seq_printf(seq, "There is more...\n");
      out:
	kfree(result);
	return 0;
}

/* Generic group F001h - Physical Device Table (table) */
static int i2o_seq_show_phys_device(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		u32 adapter_id[64];
	} result;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF001, -1, NULL, 0,
				   &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0xF001 Physical Device Table");
		return 0;
	}

	if (result.row_count)
		seq_printf(seq, "#  AdapterId\n");

	for (i = 0; i < result.row_count; i++) {
		seq_printf(seq, "%-2d", i);
		seq_printf(seq, "%#7x\n", result.adapter_id[i]);
	}

	if (result.more_flag)
		seq_printf(seq, "There is more...\n");

	return 0;
}

/* Generic group F002h - Claimed Table (table) */
static int i2o_seq_show_claimed(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		u16 claimed_tid[64];
	} result;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF002, -1, NULL, 0,
				   &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF002 Claimed Table");
		return 0;
	}

	if (result.row_count)
		seq_printf(seq, "#  ClaimedTid\n");

	for (i = 0; i < result.row_count; i++) {
		seq_printf(seq, "%-2d", i);
		seq_printf(seq, "%#7x\n", result.claimed_tid[i]);
	}

	if (result.more_flag)
		seq_printf(seq, "There is more...\n");

	return 0;
}

/* Generic group F003h - User Table (table) */
static int i2o_seq_show_users(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;

	typedef struct _i2o_user_table {
		u16 instance;
		u16 user_tid;
		u8 claim_type;
		u8 reserved1;
		u16 reserved2;
	} i2o_user_table;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_user_table user[64];
	} *result;

	result = kmalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF003, -1, NULL, 0,
				   result, sizeof(*result));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF003 User Table");
		goto out;
	}

	seq_printf(seq, "#  Instance UserTid ClaimType\n");

	for (i = 0; i < result->row_count; i++) {
		seq_printf(seq, "%-3d", i);
		seq_printf(seq, "%#8x ", result->user[i].instance);
		seq_printf(seq, "%#7x ", result->user[i].user_tid);
		seq_printf(seq, "%#9x\n", result->user[i].claim_type);
	}

	if (result->more_flag)
		seq_printf(seq, "There is more...\n");
      out:
	kfree(result);
	return 0;
}

/* Generic group F005h - Private message extensions (table) (optional) */
static int i2o_seq_show_priv_msgs(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;

	typedef struct _i2o_private {
		u16 ext_instance;
		u16 organization_id;
		u16 x_function_code;
	} i2o_private;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_private extension[64];
	} result;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF000, -1, NULL, 0,
				   &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0xF005 Private Message Extensions (optional)");
		return 0;
	}

	seq_printf(seq, "Instance#  OrgId  FunctionCode\n");

	for (i = 0; i < result.row_count; i++) {
		seq_printf(seq, "%0#9x ", result.extension[i].ext_instance);
		seq_printf(seq, "%0#6x ", result.extension[i].organization_id);
		seq_printf(seq, "%0#6x", result.extension[i].x_function_code);

		seq_printf(seq, "\n");
	}

	if (result.more_flag)
		seq_printf(seq, "There is more...\n");

	return 0;
}

/* Generic group F006h - Authorized User Table (table) */
static int i2o_seq_show_authorized_users(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;
	int i;

	struct {
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8 block_status;
		u8 error_info_size;
		u16 row_count;
		u16 more_flag;
		u32 alternate_tid[64];
	} result;

	token = i2o_parm_table_get(d, I2O_PARAMS_TABLE_GET, 0xF006, -1, NULL, 0,
				   &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0xF006 Autohorized User Table");
		return 0;
	}

	if (result.row_count)
		seq_printf(seq, "#  AlternateTid\n");

	for (i = 0; i < result.row_count; i++) {
		seq_printf(seq, "%-2d", i);
		seq_printf(seq, "%#7x ", result.alternate_tid[i]);
	}

	if (result.more_flag)
		seq_printf(seq, "There is more...\n");

	return 0;
}

/* Generic group F100h - Device Identity (scalar) */
static int i2o_seq_show_dev_identity(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	static u32 work32[128];	// allow for "stuff" + up to 256 byte (max) serial number
	// == (allow) 512d bytes (max)
	static u16 *work16 = (u16 *) work32;
	int token;

	token = i2o_parm_field_get(d, 0xF100, -1, &work32, sizeof(work32));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF100 Device Identity");
		return 0;
	}

	seq_printf(seq, "Device Class  : %s\n", i2o_get_class_name(work16[0]));
	seq_printf(seq, "Owner TID     : %0#5x\n", work16[2]);
	seq_printf(seq, "Parent TID    : %0#5x\n", work16[3]);
	seq_printf(seq, "Vendor info   : %s\n",
		   chtostr((u8 *) (work32 + 2), 16));
	seq_printf(seq, "Product info  : %s\n",
		   chtostr((u8 *) (work32 + 6), 16));
	seq_printf(seq, "Description   : %s\n",
		   chtostr((u8 *) (work32 + 10), 16));
	seq_printf(seq, "Product rev.  : %s\n",
		   chtostr((u8 *) (work32 + 14), 8));

	seq_printf(seq, "Serial number : ");
	print_serial_number(seq, (u8 *) (work32 + 16),
			    /* allow for SNLen plus
			     * possible trailing '\0'
			     */
			    sizeof(work32) - (16 * sizeof(u32)) - 2);
	seq_printf(seq, "\n");

	return 0;
}

static int i2o_seq_show_dev_name(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;

	seq_printf(seq, "%s\n", dev_name(&d->device));

	return 0;
}

/* Generic group F101h - DDM Identity (scalar) */
static int i2o_seq_show_ddm_identity(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;

	struct {
		u16 ddm_tid;
		u8 module_name[24];
		u8 module_rev[8];
		u8 sn_format;
		u8 serial_number[12];
		u8 pad[256];	// allow up to 256 byte (max) serial number
	} result;

	token = i2o_parm_field_get(d, 0xF101, -1, &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF101 DDM Identity");
		return 0;
	}

	seq_printf(seq, "Registering DDM TID : 0x%03x\n", result.ddm_tid);
	seq_printf(seq, "Module name         : %s\n",
		   chtostr(result.module_name, 24));
	seq_printf(seq, "Module revision     : %s\n",
		   chtostr(result.module_rev, 8));

	seq_printf(seq, "Serial number       : ");
	print_serial_number(seq, result.serial_number, sizeof(result) - 36);
	/* allow for SNLen plus possible trailing '\0' */

	seq_printf(seq, "\n");

	return 0;
}

/* Generic group F102h - User Information (scalar) */
static int i2o_seq_show_uinfo(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;

	struct {
		u8 device_name[64];
		u8 service_name[64];
		u8 physical_location[64];
		u8 instance_number[4];
	} result;

	token = i2o_parm_field_get(d, 0xF102, -1, &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token, "0xF102 User Information");
		return 0;
	}

	seq_printf(seq, "Device name     : %s\n",
		   chtostr(result.device_name, 64));
	seq_printf(seq, "Service name    : %s\n",
		   chtostr(result.service_name, 64));
	seq_printf(seq, "Physical name   : %s\n",
		   chtostr(result.physical_location, 64));
	seq_printf(seq, "Instance number : %s\n",
		   chtostr(result.instance_number, 4));

	return 0;
}

/* Generic group F103h - SGL Operating Limits (scalar) */
static int i2o_seq_show_sgl_limits(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	static u32 work32[12];
	static u16 *work16 = (u16 *) work32;
	static u8 *work8 = (u8 *) work32;
	int token;

	token = i2o_parm_field_get(d, 0xF103, -1, &work32, sizeof(work32));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0xF103 SGL Operating Limits");
		return 0;
	}

	seq_printf(seq, "SGL chain size        : %d\n", work32[0]);
	seq_printf(seq, "Max SGL chain size    : %d\n", work32[1]);
	seq_printf(seq, "SGL chain size target : %d\n", work32[2]);
	seq_printf(seq, "SGL frag count        : %d\n", work16[6]);
	seq_printf(seq, "Max SGL frag count    : %d\n", work16[7]);
	seq_printf(seq, "SGL frag count target : %d\n", work16[8]);

/* FIXME
	if (d->i2oversion == 0x02)
	{
*/
	seq_printf(seq, "SGL data alignment    : %d\n", work16[8]);
	seq_printf(seq, "SGL addr limit        : %d\n", work8[20]);
	seq_printf(seq, "SGL addr sizes supported : ");
	if (work8[21] & 0x01)
		seq_printf(seq, "32 bit ");
	if (work8[21] & 0x02)
		seq_printf(seq, "64 bit ");
	if (work8[21] & 0x04)
		seq_printf(seq, "96 bit ");
	if (work8[21] & 0x08)
		seq_printf(seq, "128 bit ");
	seq_printf(seq, "\n");
/*
	}
*/

	return 0;
}

/* Generic group F200h - Sensors (scalar) */
static int i2o_seq_show_sensors(struct seq_file *seq, void *v)
{
	struct i2o_device *d = (struct i2o_device *)seq->private;
	int token;

	struct {
		u16 sensor_instance;
		u8 component;
		u16 component_instance;
		u8 sensor_class;
		u8 sensor_type;
		u8 scaling_exponent;
		u32 actual_reading;
		u32 minimum_reading;
		u32 low2lowcat_treshold;
		u32 lowcat2low_treshold;
		u32 lowwarn2low_treshold;
		u32 low2lowwarn_treshold;
		u32 norm2lowwarn_treshold;
		u32 lowwarn2norm_treshold;
		u32 nominal_reading;
		u32 hiwarn2norm_treshold;
		u32 norm2hiwarn_treshold;
		u32 high2hiwarn_treshold;
		u32 hiwarn2high_treshold;
		u32 hicat2high_treshold;
		u32 hi2hicat_treshold;
		u32 maximum_reading;
		u8 sensor_state;
		u16 event_enable;
	} result;

	token = i2o_parm_field_get(d, 0xF200, -1, &result, sizeof(result));

	if (token < 0) {
		i2o_report_query_status(seq, token,
					"0xF200 Sensors (optional)");
		return 0;
	}

	seq_printf(seq, "Sensor instance       : %d\n", result.sensor_instance);

	seq_printf(seq, "Component             : %d = ", result.component);
	switch (result.component) {
	case 0:
		seq_printf(seq, "Other");
		break;
	case 1:
		seq_printf(seq, "Planar logic Board");
		break;
	case 2:
		seq_printf(seq, "CPU");
		break;
	case 3:
		seq_printf(seq, "Chassis");
		break;
	case 4:
		seq_printf(seq, "Power Supply");
		break;
	case 5:
		seq_printf(seq, "Storage");
		break;
	case 6:
		seq_printf(seq, "External");
		break;
	}
	seq_printf(seq, "\n");

	seq_printf(seq, "Component instance    : %d\n",
		   result.component_instance);
	seq_printf(seq, "Sensor class          : %s\n",
		   result.sensor_class ? "Analog" : "Digital");

	seq_printf(seq, "Sensor type           : %d = ", result.sensor_type);
	switch (result.sensor_type) {
	case 0:
		seq_printf(seq, "Other\n");
		break;
	case 1:
		seq_printf(seq, "Thermal\n");
		break;
	case 2:
		seq_printf(seq, "DC voltage (DC volts)\n");
		break;
	case 3:
		seq_printf(seq, "AC voltage (AC volts)\n");
		break;
	case 4:
		seq_printf(seq, "DC current (DC amps)\n");
		break;
	case 5:
		seq_printf(seq, "AC current (AC volts)\n");
		break;
	case 6:
		seq_printf(seq, "Door open\n");
		break;
	case 7:
		seq_printf(seq, "Fan operational\n");
		break;
	}

	seq_printf(seq, "Scaling exponent      : %d\n",
		   result.scaling_exponent);
	seq_printf(seq, "Actual reading        : %d\n", result.actual_reading);
	seq_printf(seq, "Minimum reading       : %d\n", result.minimum_reading);
	seq_printf(seq, "Low2LowCat treshold   : %d\n",
		   result.low2lowcat_treshold);
	seq_printf(seq, "LowCat2Low treshold   : %d\n",
		   result.lowcat2low_treshold);
	seq_printf(seq, "LowWarn2Low treshold  : %d\n",
		   result.lowwarn2low_treshold);
	seq_printf(seq, "Low2LowWarn treshold  : %d\n",
		   result.low2lowwarn_treshold);
	seq_printf(seq, "Norm2LowWarn treshold : %d\n",
		   result.norm2lowwarn_treshold);
	seq_printf(seq, "LowWarn2Norm treshold : %d\n",
		   result.lowwarn2norm_treshold);
	seq_printf(seq, "Nominal reading       : %d\n", result.nominal_reading);
	seq_printf(seq, "HiWarn2Norm treshold  : %d\n",
		   result.hiwarn2norm_treshold);
	seq_printf(seq, "Norm2HiWarn treshold  : %d\n",
		   result.norm2hiwarn_treshold);
	seq_printf(seq, "High2HiWarn treshold  : %d\n",
		   result.high2hiwarn_treshold);
	seq_printf(seq, "HiWarn2High treshold  : %d\n",
		   result.hiwarn2high_treshold);
	seq_printf(seq, "HiCat2High treshold   : %d\n",
		   result.hicat2high_treshold);
	seq_printf(seq, "High2HiCat treshold   : %d\n",
		   result.hi2hicat_treshold);
	seq_printf(seq, "Maximum reading       : %d\n", result.maximum_reading);

	seq_printf(seq, "Sensor state          : %d = ", result.sensor_state);
	switch (result.sensor_state) {
	case 0:
		seq_printf(seq, "Normal\n");
		break;
	case 1:
		seq_printf(seq, "Abnormal\n");
		break;
	case 2:
		seq_printf(seq, "Unknown\n");
		break;
	case 3:
		seq_printf(seq, "Low Catastrophic (LoCat)\n");
		break;
	case 4:
		seq_printf(seq, "Low (Low)\n");
		break;
	case 5:
		seq_printf(seq, "Low Warning (LoWarn)\n");
		break;
	case 6:
		seq_printf(seq, "High Warning (HiWarn)\n");
		break;
	case 7:
		seq_printf(seq, "High (High)\n");
		break;
	case 8:
		seq_printf(seq, "High Catastrophic (HiCat)\n");
		break;
	}

	seq_printf(seq, "Event_enable : 0x%02X\n", result.event_enable);
	seq_printf(seq, "    [%s] Operational state change. \n",
		   (result.event_enable & 0x01) ? "+" : "-");
	seq_printf(seq, "    [%s] Low catastrophic. \n",
		   (result.event_enable & 0x02) ? "+" : "-");
	seq_printf(seq, "    [%s] Low reading. \n",
		   (result.event_enable & 0x04) ? "+" : "-");
	seq_printf(seq, "    [%s] Low warning. \n",
		   (result.event_enable & 0x08) ? "+" : "-");
	seq_printf(seq,
		   "    [%s] Change back to normal from out of range state. \n",
		   (result.event_enable & 0x10) ? "+" : "-");
	seq_printf(seq, "    [%s] High warning. \n",
		   (result.event_enable & 0x20) ? "+" : "-");
	seq_printf(seq, "    [%s] High reading. \n",
		   (result.event_enable & 0x40) ? "+" : "-");
	seq_printf(seq, "    [%s] High catastrophic. \n",
		   (result.event_enable & 0x80) ? "+" : "-");

	return 0;
}

static int i2o_seq_open_hrt(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_hrt, PDE(inode)->data);
};

static int i2o_seq_open_lct(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_lct, PDE(inode)->data);
};

static int i2o_seq_open_status(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_status, PDE(inode)->data);
};

static int i2o_seq_open_hw(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_hw, PDE(inode)->data);
};

static int i2o_seq_open_ddm_table(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_ddm_table, PDE(inode)->data);
};

static int i2o_seq_open_driver_store(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_driver_store, PDE(inode)->data);
};

static int i2o_seq_open_drivers_stored(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_drivers_stored, PDE(inode)->data);
};

static int i2o_seq_open_groups(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_groups, PDE(inode)->data);
};

static int i2o_seq_open_phys_device(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_phys_device, PDE(inode)->data);
};

static int i2o_seq_open_claimed(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_claimed, PDE(inode)->data);
};

static int i2o_seq_open_users(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_users, PDE(inode)->data);
};

static int i2o_seq_open_priv_msgs(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_priv_msgs, PDE(inode)->data);
};

static int i2o_seq_open_authorized_users(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_authorized_users,
			   PDE(inode)->data);
};

static int i2o_seq_open_dev_identity(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_dev_identity, PDE(inode)->data);
};

static int i2o_seq_open_ddm_identity(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_ddm_identity, PDE(inode)->data);
};

static int i2o_seq_open_uinfo(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_uinfo, PDE(inode)->data);
};

static int i2o_seq_open_sgl_limits(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_sgl_limits, PDE(inode)->data);
};

static int i2o_seq_open_sensors(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_sensors, PDE(inode)->data);
};

static int i2o_seq_open_dev_name(struct inode *inode, struct file *file)
{
	return single_open(file, i2o_seq_show_dev_name, PDE(inode)->data);
};

static const struct file_operations i2o_seq_fops_lct = {
	.open = i2o_seq_open_lct,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_hrt = {
	.open = i2o_seq_open_hrt,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_status = {
	.open = i2o_seq_open_status,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_hw = {
	.open = i2o_seq_open_hw,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_ddm_table = {
	.open = i2o_seq_open_ddm_table,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_driver_store = {
	.open = i2o_seq_open_driver_store,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_drivers_stored = {
	.open = i2o_seq_open_drivers_stored,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_groups = {
	.open = i2o_seq_open_groups,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_phys_device = {
	.open = i2o_seq_open_phys_device,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_claimed = {
	.open = i2o_seq_open_claimed,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_users = {
	.open = i2o_seq_open_users,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_priv_msgs = {
	.open = i2o_seq_open_priv_msgs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_authorized_users = {
	.open = i2o_seq_open_authorized_users,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_dev_name = {
	.open = i2o_seq_open_dev_name,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_dev_identity = {
	.open = i2o_seq_open_dev_identity,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_ddm_identity = {
	.open = i2o_seq_open_ddm_identity,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_uinfo = {
	.open = i2o_seq_open_uinfo,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_sgl_limits = {
	.open = i2o_seq_open_sgl_limits,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations i2o_seq_fops_sensors = {
	.open = i2o_seq_open_sensors,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * IOP specific entries...write field just in case someone
 * ever wants one.
 */
static i2o_proc_entry i2o_proc_generic_iop_entries[] = {
	{"hrt", S_IFREG | S_IRUGO, &i2o_seq_fops_hrt},
	{"lct", S_IFREG | S_IRUGO, &i2o_seq_fops_lct},
	{"status", S_IFREG | S_IRUGO, &i2o_seq_fops_status},
	{"hw", S_IFREG | S_IRUGO, &i2o_seq_fops_hw},
	{"ddm_table", S_IFREG | S_IRUGO, &i2o_seq_fops_ddm_table},
	{"driver_store", S_IFREG | S_IRUGO, &i2o_seq_fops_driver_store},
	{"drivers_stored", S_IFREG | S_IRUGO, &i2o_seq_fops_drivers_stored},
	{NULL, 0, NULL}
};

/*
 * Device specific entries
 */
static i2o_proc_entry generic_dev_entries[] = {
	{"groups", S_IFREG | S_IRUGO, &i2o_seq_fops_groups},
	{"phys_dev", S_IFREG | S_IRUGO, &i2o_seq_fops_phys_device},
	{"claimed", S_IFREG | S_IRUGO, &i2o_seq_fops_claimed},
	{"users", S_IFREG | S_IRUGO, &i2o_seq_fops_users},
	{"priv_msgs", S_IFREG | S_IRUGO, &i2o_seq_fops_priv_msgs},
	{"authorized_users", S_IFREG | S_IRUGO, &i2o_seq_fops_authorized_users},
	{"dev_identity", S_IFREG | S_IRUGO, &i2o_seq_fops_dev_identity},
	{"ddm_identity", S_IFREG | S_IRUGO, &i2o_seq_fops_ddm_identity},
	{"user_info", S_IFREG | S_IRUGO, &i2o_seq_fops_uinfo},
	{"sgl_limits", S_IFREG | S_IRUGO, &i2o_seq_fops_sgl_limits},
	{"sensors", S_IFREG | S_IRUGO, &i2o_seq_fops_sensors},
	{NULL, 0, NULL}
};

/*
 *  Storage unit specific entries (SCSI Periph, BS) with device names
 */
static i2o_proc_entry rbs_dev_entries[] = {
	{"dev_name", S_IFREG | S_IRUGO, &i2o_seq_fops_dev_name},
	{NULL, 0, NULL}
};

/**
 *	i2o_proc_create_entries - Creates proc dir entries
 *	@dir: proc dir entry under which the entries should be placed
 *	@i2o_pe: pointer to the entries which should be added
 *	@data: pointer to I2O controller or device
 *
 *	Create proc dir entries for a I2O controller or I2O device.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_proc_create_entries(struct proc_dir_entry *dir,
				   i2o_proc_entry * i2o_pe, void *data)
{
	struct proc_dir_entry *tmp;

	while (i2o_pe->name) {
		tmp = proc_create_data(i2o_pe->name, i2o_pe->mode, dir,
				       i2o_pe->fops, data);
		if (!tmp)
			return -1;

		i2o_pe++;
	}

	return 0;
}

/**
 *	i2o_proc_subdir_remove - Remove child entries from a proc entry
 *	@dir: proc dir entry from which the childs should be removed
 *
 *	Iterate over each i2o proc entry under dir and remove it. If the child
 *	also has entries, remove them too.
 */
static void i2o_proc_subdir_remove(struct proc_dir_entry *dir)
{
	struct proc_dir_entry *pe, *tmp;
	pe = dir->subdir;
	while (pe) {
		tmp = pe->next;
		i2o_proc_subdir_remove(pe);
		remove_proc_entry(pe->name, dir);
		pe = tmp;
	}
};

/**
 *	i2o_proc_device_add - Add an I2O device to the proc dir
 *	@dir: proc dir entry to which the device should be added
 *	@dev: I2O device which should be added
 *
 *	Add an I2O device to the proc dir entry dir and create the entries for
 *	the device depending on the class of the I2O device.
 */
static void i2o_proc_device_add(struct proc_dir_entry *dir,
				struct i2o_device *dev)
{
	char buff[10];
	struct proc_dir_entry *devdir;
	i2o_proc_entry *i2o_pe = NULL;

	sprintf(buff, "%03x", dev->lct_data.tid);

	osm_debug("adding device /proc/i2o/%s/%s\n", dev->iop->name, buff);

	devdir = proc_mkdir(buff, dir);
	if (!devdir) {
		osm_warn("Could not allocate procdir!\n");
		return;
	}

	devdir->data = dev;

	i2o_proc_create_entries(devdir, generic_dev_entries, dev);

	/* Inform core that we want updates about this device's status */
	switch (dev->lct_data.class_id) {
	case I2O_CLASS_SCSI_PERIPHERAL:
	case I2O_CLASS_RANDOM_BLOCK_STORAGE:
		i2o_pe = rbs_dev_entries;
		break;
	default:
		break;
	}
	if (i2o_pe)
		i2o_proc_create_entries(devdir, i2o_pe, dev);
}

/**
 *	i2o_proc_iop_add - Add an I2O controller to the i2o proc tree
 *	@dir: parent proc dir entry
 *	@c: I2O controller which should be added
 *
 *	Add the entries to the parent proc dir entry. Also each device is added
 *	to the controllers proc dir entry.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_proc_iop_add(struct proc_dir_entry *dir,
			    struct i2o_controller *c)
{
	struct proc_dir_entry *iopdir;
	struct i2o_device *dev;

	osm_debug("adding IOP /proc/i2o/%s\n", c->name);

	iopdir = proc_mkdir(c->name, dir);
	if (!iopdir)
		return -1;

	iopdir->data = c;

	i2o_proc_create_entries(iopdir, i2o_proc_generic_iop_entries, c);

	list_for_each_entry(dev, &c->devices, list)
	    i2o_proc_device_add(iopdir, dev);

	return 0;
}

/**
 *	i2o_proc_iop_remove - Removes an I2O controller from the i2o proc tree
 *	@dir: parent proc dir entry
 *	@c: I2O controller which should be removed
 *
 *	Iterate over each i2o proc entry and search controller c. If it is found
 *	remove it from the tree.
 */
static void i2o_proc_iop_remove(struct proc_dir_entry *dir,
				struct i2o_controller *c)
{
	struct proc_dir_entry *pe, *tmp;

	pe = dir->subdir;
	while (pe) {
		tmp = pe->next;
		if (pe->data == c) {
			i2o_proc_subdir_remove(pe);
			remove_proc_entry(pe->name, dir);
		}
		osm_debug("removing IOP /proc/i2o/%s\n", c->name);
		pe = tmp;
	}
}

/**
 *	i2o_proc_fs_create - Create the i2o proc fs.
 *
 *	Iterate over each I2O controller and create the entries for it.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_proc_fs_create(void)
{
	struct i2o_controller *c;

	i2o_proc_dir_root = proc_mkdir("i2o", NULL);
	if (!i2o_proc_dir_root)
		return -1;

	list_for_each_entry(c, &i2o_controllers, list)
	    i2o_proc_iop_add(i2o_proc_dir_root, c);

	return 0;
};

/**
 *	i2o_proc_fs_destroy - Cleanup the all i2o proc entries
 *
 *	Iterate over each I2O controller and remove the entries for it.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __exit i2o_proc_fs_destroy(void)
{
	struct i2o_controller *c;

	list_for_each_entry(c, &i2o_controllers, list)
	    i2o_proc_iop_remove(i2o_proc_dir_root, c);

	remove_proc_entry("i2o", NULL);

	return 0;
};

/**
 *	i2o_proc_init - Init function for procfs
 *
 *	Registers Proc OSM and creates procfs entries.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_proc_init(void)
{
	int rc;

	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	rc = i2o_driver_register(&i2o_proc_driver);
	if (rc)
		return rc;

	rc = i2o_proc_fs_create();
	if (rc) {
		i2o_driver_unregister(&i2o_proc_driver);
		return rc;
	}

	return 0;
};

/**
 *	i2o_proc_exit - Exit function for procfs
 *
 *	Unregisters Proc OSM and removes procfs entries.
 */
static void __exit i2o_proc_exit(void)
{
	i2o_driver_unregister(&i2o_proc_driver);
	i2o_proc_fs_destroy();
};

MODULE_AUTHOR("Deepak Saxena");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

module_init(i2o_proc_init);
module_exit(i2o_proc_exit);

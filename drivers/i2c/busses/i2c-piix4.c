// SPDX-License-Identifier: GPL-2.0-or-later
/*
    Copyright (c) 1998 - 2002 Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com>

*/

/*
   Supports:
	Intel PIIX4, 440MX
	Serverworks OSB4, CSB5, CSB6, HT-1000, HT-1100
	ATI IXP200, IXP300, IXP400, SB600, SB700/SP5100, SB800
	AMD Hudson-2, ML, CZ
	Hygon CZ
	SMSC Victory66

   Note: we assume there can only be one device, with one or more
   SMBus interfaces.
   The device can register multiple i2c_adapters (up to PIIX4_MAX_ADAPTERS).
   For devices supporting multiple ports the i2c_adapter should provide
   an i2c_algorithm to access them.
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/io.h>


/* PIIX4 SMBus address offsets */
#define SMBHSTSTS	(0 + piix4_smba)
#define SMBHSLVSTS	(1 + piix4_smba)
#define SMBHSTCNT	(2 + piix4_smba)
#define SMBHSTCMD	(3 + piix4_smba)
#define SMBHSTADD	(4 + piix4_smba)
#define SMBHSTDAT0	(5 + piix4_smba)
#define SMBHSTDAT1	(6 + piix4_smba)
#define SMBBLKDAT	(7 + piix4_smba)
#define SMBSLVCNT	(8 + piix4_smba)
#define SMBSHDWCMD	(9 + piix4_smba)
#define SMBSLVEVT	(0xA + piix4_smba)
#define SMBSLVDAT	(0xC + piix4_smba)

/* count for request_region */
#define SMBIOSIZE	9

/* PCI Address Constants */
#define SMBBA		0x090
#define SMBHSTCFG	0x0D2
#define SMBSLVC		0x0D3
#define SMBSHDW1	0x0D4
#define SMBSHDW2	0x0D5
#define SMBREV		0x0D6

/* Other settings */
#define MAX_TIMEOUT	500
#define  ENABLE_INT9	0

/* PIIX4 constants */
#define PIIX4_QUICK		0x00
#define PIIX4_BYTE		0x04
#define PIIX4_BYTE_DATA		0x08
#define PIIX4_WORD_DATA		0x0C
#define PIIX4_BLOCK_DATA	0x14

/* Multi-port constants */
#define PIIX4_MAX_ADAPTERS	4
#define HUDSON2_MAIN_PORTS	2 /* HUDSON2, KERNCZ reserves ports 3, 4 */

/* SB800 constants */
#define SB800_PIIX4_SMB_IDX		0xcd6

#define KERNCZ_IMC_IDX			0x3e
#define KERNCZ_IMC_DATA			0x3f

/*
 * SB800 port is selected by bits 2:1 of the smb_en register (0x2c)
 * or the smb_sel register (0x2e), depending on bit 0 of register 0x2f.
 * Hudson-2/Bolton port is always selected by bits 2:1 of register 0x2f.
 */
#define SB800_PIIX4_PORT_IDX		0x2c
#define SB800_PIIX4_PORT_IDX_ALT	0x2e
#define SB800_PIIX4_PORT_IDX_SEL	0x2f
#define SB800_PIIX4_PORT_IDX_MASK	0x06
#define SB800_PIIX4_PORT_IDX_SHIFT	1

/* On kerncz and Hudson2, SmBus0Sel is at bit 20:19 of PMx00 DecodeEn */
#define SB800_PIIX4_PORT_IDX_KERNCZ		0x02
#define SB800_PIIX4_PORT_IDX_MASK_KERNCZ	0x18
#define SB800_PIIX4_PORT_IDX_SHIFT_KERNCZ	3

/* insmod parameters */

/* If force is set to anything different from 0, we forcibly enable the
   PIIX4. DANGEROUS! */
static int force;
module_param (force, int, 0);
MODULE_PARM_DESC(force, "Forcibly enable the PIIX4. DANGEROUS!");

/* If force_addr is set to anything different from 0, we forcibly enable
   the PIIX4 at the given address. VERY DANGEROUS! */
static int force_addr;
module_param_hw(force_addr, int, ioport, 0);
MODULE_PARM_DESC(force_addr,
		 "Forcibly enable the PIIX4 at the given address. "
		 "EXTREMELY DANGEROUS!");

static int srvrworks_csb5_delay;
static struct pci_driver piix4_driver;

static const struct dmi_system_id piix4_dmi_blacklist[] = {
	{
		.ident = "Sapphire AM2RD790",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "SAPPHIRE Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "PC-AM2RD790"),
		},
	},
	{
		.ident = "DFI Lanparty UT 790FX",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "DFI Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "LP UT 790FX"),
		},
	},
	{ }
};

/* The IBM entry is in a separate table because we only check it
   on Intel-based systems */
static const struct dmi_system_id piix4_dmi_ibm[] = {
	{
		.ident = "IBM",
		.matches = { DMI_MATCH(DMI_SYS_VENDOR, "IBM"), },
	},
	{ },
};

/*
 * SB800 globals
 */
static u8 piix4_port_sel_sb800;
static u8 piix4_port_mask_sb800;
static u8 piix4_port_shift_sb800;
static const char *piix4_main_port_names_sb800[PIIX4_MAX_ADAPTERS] = {
	" port 0", " port 2", " port 3", " port 4"
};
static const char *piix4_aux_port_name_sb800 = " port 1";

struct i2c_piix4_adapdata {
	unsigned short smba;

	/* SB800 */
	bool sb800_main;
	bool notify_imc;
	u8 port;		/* Port number, shifted */
};

static int piix4_setup(struct pci_dev *PIIX4_dev,
		       const struct pci_device_id *id)
{
	unsigned char temp;
	unsigned short piix4_smba;

	if ((PIIX4_dev->vendor == PCI_VENDOR_ID_SERVERWORKS) &&
	    (PIIX4_dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5))
		srvrworks_csb5_delay = 1;

	/* On some motherboards, it was reported that accessing the SMBus
	   caused severe hardware problems */
	if (dmi_check_system(piix4_dmi_blacklist)) {
		dev_err(&PIIX4_dev->dev,
			"Accessing the SMBus on this system is unsafe!\n");
		return -EPERM;
	}

	/* Don't access SMBus on IBM systems which get corrupted eeproms */
	if (dmi_check_system(piix4_dmi_ibm) &&
			PIIX4_dev->vendor == PCI_VENDOR_ID_INTEL) {
		dev_err(&PIIX4_dev->dev, "IBM system detected; this module "
			"may corrupt your serial eeprom! Refusing to load "
			"module!\n");
		return -EPERM;
	}

	/* Determine the address of the SMBus areas */
	if (force_addr) {
		piix4_smba = force_addr & 0xfff0;
		force = 0;
	} else {
		pci_read_config_word(PIIX4_dev, SMBBA, &piix4_smba);
		piix4_smba &= 0xfff0;
		if(piix4_smba == 0) {
			dev_err(&PIIX4_dev->dev, "SMBus base address "
				"uninitialized - upgrade BIOS or use "
				"force_addr=0xaddr\n");
			return -ENODEV;
		}
	}

	if (acpi_check_region(piix4_smba, SMBIOSIZE, piix4_driver.name))
		return -ENODEV;

	if (!request_region(piix4_smba, SMBIOSIZE, piix4_driver.name)) {
		dev_err(&PIIX4_dev->dev, "SMBus region 0x%x already in use!\n",
			piix4_smba);
		return -EBUSY;
	}

	pci_read_config_byte(PIIX4_dev, SMBHSTCFG, &temp);

	/* If force_addr is set, we program the new address here. Just to make
	   sure, we disable the PIIX4 first. */
	if (force_addr) {
		pci_write_config_byte(PIIX4_dev, SMBHSTCFG, temp & 0xfe);
		pci_write_config_word(PIIX4_dev, SMBBA, piix4_smba);
		pci_write_config_byte(PIIX4_dev, SMBHSTCFG, temp | 0x01);
		dev_info(&PIIX4_dev->dev, "WARNING: SMBus interface set to "
			"new address %04x!\n", piix4_smba);
	} else if ((temp & 1) == 0) {
		if (force) {
			/* This should never need to be done, but has been
			 * noted that many Dell machines have the SMBus
			 * interface on the PIIX4 disabled!? NOTE: This assumes
			 * I/O space and other allocations WERE done by the
			 * Bios!  Don't complain if your hardware does weird
			 * things after enabling this. :') Check for Bios
			 * updates before resorting to this.
			 */
			pci_write_config_byte(PIIX4_dev, SMBHSTCFG,
					      temp | 1);
			dev_notice(&PIIX4_dev->dev,
				   "WARNING: SMBus interface has been FORCEFULLY ENABLED!\n");
		} else {
			dev_err(&PIIX4_dev->dev,
				"SMBus Host Controller not enabled!\n");
			release_region(piix4_smba, SMBIOSIZE);
			return -ENODEV;
		}
	}

	if (((temp & 0x0E) == 8) || ((temp & 0x0E) == 2))
		dev_dbg(&PIIX4_dev->dev, "Using IRQ for SMBus\n");
	else if ((temp & 0x0E) == 0)
		dev_dbg(&PIIX4_dev->dev, "Using SMI# for SMBus\n");
	else
		dev_err(&PIIX4_dev->dev, "Illegal Interrupt configuration "
			"(or code out of date)!\n");

	pci_read_config_byte(PIIX4_dev, SMBREV, &temp);
	dev_info(&PIIX4_dev->dev,
		 "SMBus Host Controller at 0x%x, revision %d\n",
		 piix4_smba, temp);

	return piix4_smba;
}

static int piix4_setup_sb800(struct pci_dev *PIIX4_dev,
			     const struct pci_device_id *id, u8 aux)
{
	unsigned short piix4_smba;
	u8 smba_en_lo, smba_en_hi, smb_en, smb_en_status, port_sel;
	u8 i2ccfg, i2ccfg_offset = 0x10;

	/* SB800 and later SMBus does not support forcing address */
	if (force || force_addr) {
		dev_err(&PIIX4_dev->dev, "SMBus does not support "
			"forcing address!\n");
		return -EINVAL;
	}

	/* Determine the address of the SMBus areas */
	if ((PIIX4_dev->vendor == PCI_VENDOR_ID_AMD &&
	     PIIX4_dev->device == PCI_DEVICE_ID_AMD_HUDSON2_SMBUS &&
	     PIIX4_dev->revision >= 0x41) ||
	    (PIIX4_dev->vendor == PCI_VENDOR_ID_AMD &&
	     PIIX4_dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS &&
	     PIIX4_dev->revision >= 0x49) ||
	    (PIIX4_dev->vendor == PCI_VENDOR_ID_HYGON &&
	     PIIX4_dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS))
		smb_en = 0x00;
	else
		smb_en = (aux) ? 0x28 : 0x2c;

	if (!request_muxed_region(SB800_PIIX4_SMB_IDX, 2, "sb800_piix4_smb")) {
		dev_err(&PIIX4_dev->dev,
			"SMB base address index region 0x%x already in use.\n",
			SB800_PIIX4_SMB_IDX);
		return -EBUSY;
	}

	outb_p(smb_en, SB800_PIIX4_SMB_IDX);
	smba_en_lo = inb_p(SB800_PIIX4_SMB_IDX + 1);
	outb_p(smb_en + 1, SB800_PIIX4_SMB_IDX);
	smba_en_hi = inb_p(SB800_PIIX4_SMB_IDX + 1);

	release_region(SB800_PIIX4_SMB_IDX, 2);

	if (!smb_en) {
		smb_en_status = smba_en_lo & 0x10;
		piix4_smba = smba_en_hi << 8;
		if (aux)
			piix4_smba |= 0x20;
	} else {
		smb_en_status = smba_en_lo & 0x01;
		piix4_smba = ((smba_en_hi << 8) | smba_en_lo) & 0xffe0;
	}

	if (!smb_en_status) {
		dev_err(&PIIX4_dev->dev,
			"SMBus Host Controller not enabled!\n");
		return -ENODEV;
	}

	if (acpi_check_region(piix4_smba, SMBIOSIZE, piix4_driver.name))
		return -ENODEV;

	if (!request_region(piix4_smba, SMBIOSIZE, piix4_driver.name)) {
		dev_err(&PIIX4_dev->dev, "SMBus region 0x%x already in use!\n",
			piix4_smba);
		return -EBUSY;
	}

	/* Aux SMBus does not support IRQ information */
	if (aux) {
		dev_info(&PIIX4_dev->dev,
			 "Auxiliary SMBus Host Controller at 0x%x\n",
			 piix4_smba);
		return piix4_smba;
	}

	/* Request the SMBus I2C bus config region */
	if (!request_region(piix4_smba + i2ccfg_offset, 1, "i2ccfg")) {
		dev_err(&PIIX4_dev->dev, "SMBus I2C bus config region "
			"0x%x already in use!\n", piix4_smba + i2ccfg_offset);
		release_region(piix4_smba, SMBIOSIZE);
		return -EBUSY;
	}
	i2ccfg = inb_p(piix4_smba + i2ccfg_offset);
	release_region(piix4_smba + i2ccfg_offset, 1);

	if (i2ccfg & 1)
		dev_dbg(&PIIX4_dev->dev, "Using IRQ for SMBus\n");
	else
		dev_dbg(&PIIX4_dev->dev, "Using SMI# for SMBus\n");

	dev_info(&PIIX4_dev->dev,
		 "SMBus Host Controller at 0x%x, revision %d\n",
		 piix4_smba, i2ccfg >> 4);

	/* Find which register is used for port selection */
	if (PIIX4_dev->vendor == PCI_VENDOR_ID_AMD ||
	    PIIX4_dev->vendor == PCI_VENDOR_ID_HYGON) {
		if (PIIX4_dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS ||
		    (PIIX4_dev->device == PCI_DEVICE_ID_AMD_HUDSON2_SMBUS &&
		     PIIX4_dev->revision >= 0x1F)) {
			piix4_port_sel_sb800 = SB800_PIIX4_PORT_IDX_KERNCZ;
			piix4_port_mask_sb800 = SB800_PIIX4_PORT_IDX_MASK_KERNCZ;
			piix4_port_shift_sb800 = SB800_PIIX4_PORT_IDX_SHIFT_KERNCZ;
		} else {
			piix4_port_sel_sb800 = SB800_PIIX4_PORT_IDX_ALT;
			piix4_port_mask_sb800 = SB800_PIIX4_PORT_IDX_MASK;
			piix4_port_shift_sb800 = SB800_PIIX4_PORT_IDX_SHIFT;
		}
	} else {
		if (!request_muxed_region(SB800_PIIX4_SMB_IDX, 2,
					  "sb800_piix4_smb")) {
			release_region(piix4_smba, SMBIOSIZE);
			return -EBUSY;
		}

		outb_p(SB800_PIIX4_PORT_IDX_SEL, SB800_PIIX4_SMB_IDX);
		port_sel = inb_p(SB800_PIIX4_SMB_IDX + 1);
		piix4_port_sel_sb800 = (port_sel & 0x01) ?
				       SB800_PIIX4_PORT_IDX_ALT :
				       SB800_PIIX4_PORT_IDX;
		piix4_port_mask_sb800 = SB800_PIIX4_PORT_IDX_MASK;
		piix4_port_shift_sb800 = SB800_PIIX4_PORT_IDX_SHIFT;
		release_region(SB800_PIIX4_SMB_IDX, 2);
	}

	dev_info(&PIIX4_dev->dev,
		 "Using register 0x%02x for SMBus port selection\n",
		 (unsigned int)piix4_port_sel_sb800);

	return piix4_smba;
}

static int piix4_setup_aux(struct pci_dev *PIIX4_dev,
			   const struct pci_device_id *id,
			   unsigned short base_reg_addr)
{
	/* Set up auxiliary SMBus controllers found on some
	 * AMD chipsets e.g. SP5100 (SB700 derivative) */

	unsigned short piix4_smba;

	/* Read address of auxiliary SMBus controller */
	pci_read_config_word(PIIX4_dev, base_reg_addr, &piix4_smba);
	if ((piix4_smba & 1) == 0) {
		dev_dbg(&PIIX4_dev->dev,
			"Auxiliary SMBus controller not enabled\n");
		return -ENODEV;
	}

	piix4_smba &= 0xfff0;
	if (piix4_smba == 0) {
		dev_dbg(&PIIX4_dev->dev,
			"Auxiliary SMBus base address uninitialized\n");
		return -ENODEV;
	}

	if (acpi_check_region(piix4_smba, SMBIOSIZE, piix4_driver.name))
		return -ENODEV;

	if (!request_region(piix4_smba, SMBIOSIZE, piix4_driver.name)) {
		dev_err(&PIIX4_dev->dev, "Auxiliary SMBus region 0x%x "
			"already in use!\n", piix4_smba);
		return -EBUSY;
	}

	dev_info(&PIIX4_dev->dev,
		 "Auxiliary SMBus Host Controller at 0x%x\n",
		 piix4_smba);

	return piix4_smba;
}

static int piix4_transaction(struct i2c_adapter *piix4_adapter)
{
	struct i2c_piix4_adapdata *adapdata = i2c_get_adapdata(piix4_adapter);
	unsigned short piix4_smba = adapdata->smba;
	int temp;
	int result = 0;
	int timeout = 0;

	dev_dbg(&piix4_adapter->dev, "Transaction (pre): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0),
		inb_p(SMBHSTDAT1));

	/* Make sure the SMBus host is ready to start transmitting */
	if ((temp = inb_p(SMBHSTSTS)) != 0x00) {
		dev_dbg(&piix4_adapter->dev, "SMBus busy (%02x). "
			"Resetting...\n", temp);
		outb_p(temp, SMBHSTSTS);
		if ((temp = inb_p(SMBHSTSTS)) != 0x00) {
			dev_err(&piix4_adapter->dev, "Failed! (%02x)\n", temp);
			return -EBUSY;
		} else {
			dev_dbg(&piix4_adapter->dev, "Successful!\n");
		}
	}

	/* start the transaction by setting bit 6 */
	outb_p(inb(SMBHSTCNT) | 0x040, SMBHSTCNT);

	/* We will always wait for a fraction of a second! (See PIIX4 docs errata) */
	if (srvrworks_csb5_delay) /* Extra delay for SERVERWORKS_CSB5 */
		usleep_range(2000, 2100);
	else
		usleep_range(250, 500);

	while ((++timeout < MAX_TIMEOUT) &&
	       ((temp = inb_p(SMBHSTSTS)) & 0x01))
		usleep_range(250, 500);

	/* If the SMBus is still busy, we give up */
	if (timeout == MAX_TIMEOUT) {
		dev_err(&piix4_adapter->dev, "SMBus Timeout!\n");
		result = -ETIMEDOUT;
	}

	if (temp & 0x10) {
		result = -EIO;
		dev_err(&piix4_adapter->dev, "Error: Failed bus transaction\n");
	}

	if (temp & 0x08) {
		result = -EIO;
		dev_dbg(&piix4_adapter->dev, "Bus collision! SMBus may be "
			"locked until next hard reset. (sorry!)\n");
		/* Clock stops and slave is stuck in mid-transmission */
	}

	if (temp & 0x04) {
		result = -ENXIO;
		dev_dbg(&piix4_adapter->dev, "Error: no response!\n");
	}

	if (inb_p(SMBHSTSTS) != 0x00)
		outb_p(inb(SMBHSTSTS), SMBHSTSTS);

	if ((temp = inb_p(SMBHSTSTS)) != 0x00) {
		dev_err(&piix4_adapter->dev, "Failed reset at end of "
			"transaction (%02x)\n", temp);
	}
	dev_dbg(&piix4_adapter->dev, "Transaction (post): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0),
		inb_p(SMBHSTDAT1));
	return result;
}

/* Return negative errno on error. */
static s32 piix4_access(struct i2c_adapter * adap, u16 addr,
		 unsigned short flags, char read_write,
		 u8 command, int size, union i2c_smbus_data * data)
{
	struct i2c_piix4_adapdata *adapdata = i2c_get_adapdata(adap);
	unsigned short piix4_smba = adapdata->smba;
	int i, len;
	int status;

	switch (size) {
	case I2C_SMBUS_QUICK:
		outb_p((addr << 1) | read_write,
		       SMBHSTADD);
		size = PIIX4_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outb_p((addr << 1) | read_write,
		       SMBHSTADD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD);
		size = PIIX4_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p((addr << 1) | read_write,
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0);
		size = PIIX4_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p((addr << 1) | read_write,
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0);
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1);
		}
		size = PIIX4_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outb_p((addr << 1) | read_write,
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
				return -EINVAL;
			outb_p(len, SMBHSTDAT0);
			inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
			for (i = 1; i <= len; i++)
				outb_p(data->block[i], SMBBLKDAT);
		}
		size = PIIX4_BLOCK_DATA;
		break;
	default:
		dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	outb_p((size & 0x1C) + (ENABLE_INT9 & 1), SMBHSTCNT);

	status = piix4_transaction(adap);
	if (status)
		return status;

	if ((read_write == I2C_SMBUS_WRITE) || (size == PIIX4_QUICK))
		return 0;


	switch (size) {
	case PIIX4_BYTE:
	case PIIX4_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case PIIX4_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0) + (inb_p(SMBHSTDAT1) << 8);
		break;
	case PIIX4_BLOCK_DATA:
		data->block[0] = inb_p(SMBHSTDAT0);
		if (data->block[0] == 0 || data->block[0] > I2C_SMBUS_BLOCK_MAX)
			return -EPROTO;
		inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = inb_p(SMBBLKDAT);
		break;
	}
	return 0;
}

static uint8_t piix4_imc_read(uint8_t idx)
{
	outb_p(idx, KERNCZ_IMC_IDX);
	return inb_p(KERNCZ_IMC_DATA);
}

static void piix4_imc_write(uint8_t idx, uint8_t value)
{
	outb_p(idx, KERNCZ_IMC_IDX);
	outb_p(value, KERNCZ_IMC_DATA);
}

static int piix4_imc_sleep(void)
{
	int timeout = MAX_TIMEOUT;

	if (!request_muxed_region(KERNCZ_IMC_IDX, 2, "smbus_kerncz_imc"))
		return -EBUSY;

	/* clear response register */
	piix4_imc_write(0x82, 0x00);
	/* request ownership flag */
	piix4_imc_write(0x83, 0xB4);
	/* kick off IMC Mailbox command 96 */
	piix4_imc_write(0x80, 0x96);

	while (timeout--) {
		if (piix4_imc_read(0x82) == 0xfa) {
			release_region(KERNCZ_IMC_IDX, 2);
			return 0;
		}
		usleep_range(1000, 2000);
	}

	release_region(KERNCZ_IMC_IDX, 2);
	return -ETIMEDOUT;
}

static void piix4_imc_wakeup(void)
{
	int timeout = MAX_TIMEOUT;

	if (!request_muxed_region(KERNCZ_IMC_IDX, 2, "smbus_kerncz_imc"))
		return;

	/* clear response register */
	piix4_imc_write(0x82, 0x00);
	/* release ownership flag */
	piix4_imc_write(0x83, 0xB5);
	/* kick off IMC Mailbox command 96 */
	piix4_imc_write(0x80, 0x96);

	while (timeout--) {
		if (piix4_imc_read(0x82) == 0xfa)
			break;
		usleep_range(1000, 2000);
	}

	release_region(KERNCZ_IMC_IDX, 2);
}

/*
 * Handles access to multiple SMBus ports on the SB800.
 * The port is selected by bits 2:1 of the smb_en register (0x2c).
 * Returns negative errno on error.
 *
 * Note: The selected port must be returned to the initial selection to avoid
 * problems on certain systems.
 */
static s32 piix4_access_sb800(struct i2c_adapter *adap, u16 addr,
		 unsigned short flags, char read_write,
		 u8 command, int size, union i2c_smbus_data *data)
{
	struct i2c_piix4_adapdata *adapdata = i2c_get_adapdata(adap);
	unsigned short piix4_smba = adapdata->smba;
	int retries = MAX_TIMEOUT;
	int smbslvcnt;
	u8 smba_en_lo;
	u8 port;
	int retval;

	if (!request_muxed_region(SB800_PIIX4_SMB_IDX, 2, "sb800_piix4_smb"))
		return -EBUSY;

	/* Request the SMBUS semaphore, avoid conflicts with the IMC */
	smbslvcnt  = inb_p(SMBSLVCNT);
	do {
		outb_p(smbslvcnt | 0x10, SMBSLVCNT);

		/* Check the semaphore status */
		smbslvcnt  = inb_p(SMBSLVCNT);
		if (smbslvcnt & 0x10)
			break;

		usleep_range(1000, 2000);
	} while (--retries);
	/* SMBus is still owned by the IMC, we give up */
	if (!retries) {
		retval = -EBUSY;
		goto release;
	}

	/*
	 * Notify the IMC (Integrated Micro Controller) if required.
	 * Among other responsibilities, the IMC is in charge of monitoring
	 * the System fans and temperature sensors, and act accordingly.
	 * All this is done through SMBus and can/will collide
	 * with our transactions if they are long (BLOCK_DATA).
	 * Therefore we need to request the ownership flag during those
	 * transactions.
	 */
	if ((size == I2C_SMBUS_BLOCK_DATA) && adapdata->notify_imc) {
		int ret;

		ret = piix4_imc_sleep();
		switch (ret) {
		case -EBUSY:
			dev_warn(&adap->dev,
				 "IMC base address index region 0x%x already in use.\n",
				 KERNCZ_IMC_IDX);
			break;
		case -ETIMEDOUT:
			dev_warn(&adap->dev,
				 "Failed to communicate with the IMC.\n");
			break;
		default:
			break;
		}

		/* If IMC communication fails do not retry */
		if (ret) {
			dev_warn(&adap->dev,
				 "Continuing without IMC notification.\n");
			adapdata->notify_imc = false;
		}
	}

	outb_p(piix4_port_sel_sb800, SB800_PIIX4_SMB_IDX);
	smba_en_lo = inb_p(SB800_PIIX4_SMB_IDX + 1);

	port = adapdata->port;
	if ((smba_en_lo & piix4_port_mask_sb800) != port)
		outb_p((smba_en_lo & ~piix4_port_mask_sb800) | port,
		       SB800_PIIX4_SMB_IDX + 1);

	retval = piix4_access(adap, addr, flags, read_write,
			      command, size, data);

	outb_p(smba_en_lo, SB800_PIIX4_SMB_IDX + 1);

	/* Release the semaphore */
	outb_p(smbslvcnt | 0x20, SMBSLVCNT);

	if ((size == I2C_SMBUS_BLOCK_DATA) && adapdata->notify_imc)
		piix4_imc_wakeup();

release:
	release_region(SB800_PIIX4_SMB_IDX, 2);
	return retval;
}

static u32 piix4_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= piix4_access,
	.functionality	= piix4_func,
};

static const struct i2c_algorithm piix4_smbus_algorithm_sb800 = {
	.smbus_xfer	= piix4_access_sb800,
	.functionality	= piix4_func,
};

static const struct pci_device_id piix4_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443MX_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP200_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP300_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP400_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_SBX00_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_HUDSON2_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_KERNCZ_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HYGON, PCI_DEVICE_ID_AMD_KERNCZ_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS,
		     PCI_DEVICE_ID_SERVERWORKS_OSB4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS,
		     PCI_DEVICE_ID_SERVERWORKS_CSB5) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS,
		     PCI_DEVICE_ID_SERVERWORKS_CSB6) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS,
		     PCI_DEVICE_ID_SERVERWORKS_HT1000SB) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS,
		     PCI_DEVICE_ID_SERVERWORKS_HT1100LD) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, piix4_ids);

static struct i2c_adapter *piix4_main_adapters[PIIX4_MAX_ADAPTERS];
static struct i2c_adapter *piix4_aux_adapter;
static int piix4_adapter_count;

static int piix4_add_adapter(struct pci_dev *dev, unsigned short smba,
			     bool sb800_main, u8 port, bool notify_imc,
			     u8 hw_port_nr, const char *name,
			     struct i2c_adapter **padap)
{
	struct i2c_adapter *adap;
	struct i2c_piix4_adapdata *adapdata;
	int retval;

	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (adap == NULL) {
		release_region(smba, SMBIOSIZE);
		return -ENOMEM;
	}

	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adap->algo = sb800_main ? &piix4_smbus_algorithm_sb800
				: &smbus_algorithm;

	adapdata = kzalloc(sizeof(*adapdata), GFP_KERNEL);
	if (adapdata == NULL) {
		kfree(adap);
		release_region(smba, SMBIOSIZE);
		return -ENOMEM;
	}

	adapdata->smba = smba;
	adapdata->sb800_main = sb800_main;
	adapdata->port = port << piix4_port_shift_sb800;
	adapdata->notify_imc = notify_imc;

	/* set up the sysfs linkage to our parent device */
	adap->dev.parent = &dev->dev;

	if (has_acpi_companion(&dev->dev)) {
		acpi_preset_companion(&adap->dev,
				      ACPI_COMPANION(&dev->dev),
				      hw_port_nr);
	}

	snprintf(adap->name, sizeof(adap->name),
		"SMBus PIIX4 adapter%s at %04x", name, smba);

	i2c_set_adapdata(adap, adapdata);

	retval = i2c_add_adapter(adap);
	if (retval) {
		kfree(adapdata);
		kfree(adap);
		release_region(smba, SMBIOSIZE);
		return retval;
	}

	*padap = adap;
	return 0;
}

static int piix4_add_adapters_sb800(struct pci_dev *dev, unsigned short smba,
				    bool notify_imc)
{
	struct i2c_piix4_adapdata *adapdata;
	int port;
	int retval;

	if (dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS ||
	    (dev->device == PCI_DEVICE_ID_AMD_HUDSON2_SMBUS &&
	     dev->revision >= 0x1F)) {
		piix4_adapter_count = HUDSON2_MAIN_PORTS;
	} else {
		piix4_adapter_count = PIIX4_MAX_ADAPTERS;
	}

	for (port = 0; port < piix4_adapter_count; port++) {
		u8 hw_port_nr = port == 0 ? 0 : port + 1;

		retval = piix4_add_adapter(dev, smba, true, port, notify_imc,
					   hw_port_nr,
					   piix4_main_port_names_sb800[port],
					   &piix4_main_adapters[port]);
		if (retval < 0)
			goto error;
	}

	return retval;

error:
	dev_err(&dev->dev,
		"Error setting up SB800 adapters. Unregistering!\n");
	while (--port >= 0) {
		adapdata = i2c_get_adapdata(piix4_main_adapters[port]);
		if (adapdata->smba) {
			i2c_del_adapter(piix4_main_adapters[port]);
			kfree(adapdata);
			kfree(piix4_main_adapters[port]);
			piix4_main_adapters[port] = NULL;
		}
	}

	return retval;
}

static int piix4_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;
	bool is_sb800 = false;

	if ((dev->vendor == PCI_VENDOR_ID_ATI &&
	     dev->device == PCI_DEVICE_ID_ATI_SBX00_SMBUS &&
	     dev->revision >= 0x40) ||
	    dev->vendor == PCI_VENDOR_ID_AMD ||
	    dev->vendor == PCI_VENDOR_ID_HYGON) {
		bool notify_imc = false;
		is_sb800 = true;

		if ((dev->vendor == PCI_VENDOR_ID_AMD ||
		     dev->vendor == PCI_VENDOR_ID_HYGON) &&
		    dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS) {
			u8 imc;

			/*
			 * Detect if IMC is active or not, this method is
			 * described on coreboot's AMD IMC notes
			 */
			pci_bus_read_config_byte(dev->bus, PCI_DEVFN(0x14, 3),
						 0x40, &imc);
			if (imc & 0x80)
				notify_imc = true;
		}

		/* base address location etc changed in SB800 */
		retval = piix4_setup_sb800(dev, id, 0);
		if (retval < 0)
			return retval;

		/*
		 * Try to register multiplexed main SMBus adapter,
		 * give up if we can't
		 */
		retval = piix4_add_adapters_sb800(dev, retval, notify_imc);
		if (retval < 0)
			return retval;
	} else {
		retval = piix4_setup(dev, id);
		if (retval < 0)
			return retval;

		/* Try to register main SMBus adapter, give up if we can't */
		retval = piix4_add_adapter(dev, retval, false, 0, false, 0,
					   "", &piix4_main_adapters[0]);
		if (retval < 0)
			return retval;
	}

	/* Check for auxiliary SMBus on some AMD chipsets */
	retval = -ENODEV;

	if (dev->vendor == PCI_VENDOR_ID_ATI &&
	    dev->device == PCI_DEVICE_ID_ATI_SBX00_SMBUS) {
		if (dev->revision < 0x40) {
			retval = piix4_setup_aux(dev, id, 0x58);
		} else {
			/* SB800 added aux bus too */
			retval = piix4_setup_sb800(dev, id, 1);
		}
	}

	if (dev->vendor == PCI_VENDOR_ID_AMD &&
	    (dev->device == PCI_DEVICE_ID_AMD_HUDSON2_SMBUS ||
	     dev->device == PCI_DEVICE_ID_AMD_KERNCZ_SMBUS)) {
		retval = piix4_setup_sb800(dev, id, 1);
	}

	if (retval > 0) {
		/* Try to add the aux adapter if it exists,
		 * piix4_add_adapter will clean up if this fails */
		piix4_add_adapter(dev, retval, false, 0, false, 1,
				  is_sb800 ? piix4_aux_port_name_sb800 : "",
				  &piix4_aux_adapter);
	}

	return 0;
}

static void piix4_adap_remove(struct i2c_adapter *adap)
{
	struct i2c_piix4_adapdata *adapdata = i2c_get_adapdata(adap);

	if (adapdata->smba) {
		i2c_del_adapter(adap);
		if (adapdata->port == (0 << piix4_port_shift_sb800))
			release_region(adapdata->smba, SMBIOSIZE);
		kfree(adapdata);
		kfree(adap);
	}
}

static void piix4_remove(struct pci_dev *dev)
{
	int port = piix4_adapter_count;

	while (--port >= 0) {
		if (piix4_main_adapters[port]) {
			piix4_adap_remove(piix4_main_adapters[port]);
			piix4_main_adapters[port] = NULL;
		}
	}

	if (piix4_aux_adapter) {
		piix4_adap_remove(piix4_aux_adapter);
		piix4_aux_adapter = NULL;
	}
}

static struct pci_driver piix4_driver = {
	.name		= "piix4_smbus",
	.id_table	= piix4_ids,
	.probe		= piix4_probe,
	.remove		= piix4_remove,
};

module_pci_driver(piix4_driver);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_AUTHOR("Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("PIIX4 SMBus driver");
MODULE_LICENSE("GPL");

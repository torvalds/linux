/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_eeprom.c - Code used to access the device's EEPROM
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "et131x_version.h"
#include "et131x_defs.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include "et1310_phy.h"
#include "et131x_adapter.h"
#include "et131x.h"

/*
 * EEPROM Defines
 */

/* LBCIF Register Groups (addressed via 32-bit offsets) */
#define LBCIF_DWORD0_GROUP       0xAC
#define LBCIF_DWORD1_GROUP       0xB0

/* LBCIF Registers (addressed via 8-bit offsets) */
#define LBCIF_ADDRESS_REGISTER   0xAC
#define LBCIF_DATA_REGISTER      0xB0
#define LBCIF_CONTROL_REGISTER   0xB1
#define LBCIF_STATUS_REGISTER    0xB2

/* LBCIF Control Register Bits */
#define LBCIF_CONTROL_SEQUENTIAL_READ   0x01
#define LBCIF_CONTROL_PAGE_WRITE        0x02
#define LBCIF_CONTROL_EEPROM_RELOAD     0x08
#define LBCIF_CONTROL_TWO_BYTE_ADDR     0x20
#define LBCIF_CONTROL_I2C_WRITE         0x40
#define LBCIF_CONTROL_LBCIF_ENABLE      0x80

/* LBCIF Status Register Bits */
#define LBCIF_STATUS_PHY_QUEUE_AVAIL    0x01
#define LBCIF_STATUS_I2C_IDLE           0x02
#define LBCIF_STATUS_ACK_ERROR          0x04
#define LBCIF_STATUS_GENERAL_ERROR      0x08
#define LBCIF_STATUS_CHECKSUM_ERROR     0x40
#define LBCIF_STATUS_EEPROM_PRESENT     0x80

/* Miscellaneous Constraints */
#define MAX_NUM_REGISTER_POLLS          1000
#define MAX_NUM_WRITE_RETRIES           2

static int eeprom_wait_ready(struct pci_dev *pdev, u32 *status)
{
	u32 reg;
	int i;

	/*
	 * 1. Check LBCIF Status Register for bits 6 & 3:2 all equal to 0 and
	 *    bits 7,1:0 both equal to 1, at least once after reset.
	 *    Subsequent operations need only to check that bits 1:0 are equal
	 *    to 1 prior to starting a single byte read/write
	 */

	for (i = 0; i < MAX_NUM_REGISTER_POLLS; i++) {
		/* Read registers grouped in DWORD1 */
		if (pci_read_config_dword(pdev, LBCIF_DWORD1_GROUP, &reg))
			return -EIO;

		/* I2C idle and Phy Queue Avail both true */
		if ((reg & 0x3000) == 0x3000) {
			if (status)
				*status = reg;
			return reg & 0xFF;
		}
	}
	return -ETIMEDOUT;
}


/**
 * eeprom_write - Write a byte to the ET1310's EEPROM
 * @etdev: pointer to our private adapter structure
 * @addr: the address to write
 * @data: the value to write
 *
 * Returns 1 for a successful write.
 */
static int eeprom_write(struct et131x_adapter *etdev, u32 addr, u8 data)
{
	struct pci_dev *pdev = etdev->pdev;
	int index = 0;
	int retries;
	int err = 0;
	int i2c_wack = 0;
	int writeok = 0;
	u32 status;
	u32 val = 0;

	/*
	 * For an EEPROM, an I2C single byte write is defined as a START
	 * condition followed by the device address, EEPROM address, one byte
	 * of data and a STOP condition.  The STOP condition will trigger the
	 * EEPROM's internally timed write cycle to the nonvolatile memory.
	 * All inputs are disabled during this write cycle and the EEPROM will
	 * not respond to any access until the internal write is complete.
	 */

	err = eeprom_wait_ready(pdev, NULL);
	if (err)
		return err;

	 /*
	 * 2. Write to the LBCIF Control Register:  bit 7=1, bit 6=1, bit 3=0,
	 *    and bits 1:0 both =0.  Bit 5 should be set according to the
	 *    type of EEPROM being accessed (1=two byte addressing, 0=one
	 *    byte addressing).
	 */
	if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
			LBCIF_CONTROL_LBCIF_ENABLE | LBCIF_CONTROL_I2C_WRITE))
		return -EIO;

	i2c_wack = 1;

	/* Prepare EEPROM address for Step 3 */

	for (retries = 0; retries < MAX_NUM_WRITE_RETRIES; retries++) {
		/* Write the address to the LBCIF Address Register */
		if (pci_write_config_dword(pdev, LBCIF_ADDRESS_REGISTER, addr))
			break;
		/*
		 * Write the data to the LBCIF Data Register (the I2C write
		 * will begin).
		 */
		if (pci_write_config_byte(pdev, LBCIF_DATA_REGISTER, data))
			break;
		/*
		 * Monitor bit 1:0 of the LBCIF Status Register.  When bits
		 * 1:0 are both equal to 1, the I2C write has completed and the
		 * internal write cycle of the EEPROM is about to start.
		 * (bits 1:0 = 01 is a legal state while waiting from both
		 * equal to 1, but bits 1:0 = 10 is invalid and implies that
		 * something is broken).
		 */
		err = eeprom_wait_ready(pdev, &status);
		if (err < 0)
			return 0;

		/*
		 * Check bit 3 of the LBCIF Status Register.  If  equal to 1,
		 * an error has occurred.Don't break here if we are revision
		 * 1, this is so we do a blind write for load bug.
		 */
		if ((status & LBCIF_STATUS_GENERAL_ERROR)
			&& etdev->pdev->revision == 0)
			break;

		/*
		 * Check bit 2 of the LBCIF Status Register.  If equal to 1 an
		 * ACK error has occurred on the address phase of the write.
		 * This could be due to an actual hardware failure or the
		 * EEPROM may still be in its internal write cycle from a
		 * previous write. This write operation was ignored and must be
		  *repeated later.
		 */
		if (status & LBCIF_STATUS_ACK_ERROR) {
			/*
			 * This could be due to an actual hardware failure
			 * or the EEPROM may still be in its internal write
			 * cycle from a previous write. This write operation
			 * was ignored and must be repeated later.
			 */
			udelay(10);
			continue;
		}

		writeok = 1;
		break;
	}

	/*
	 * Set bit 6 of the LBCIF Control Register = 0.
	 */
	udelay(10);

	while (i2c_wack) {
		if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
			LBCIF_CONTROL_LBCIF_ENABLE))
			writeok = 0;

		/* Do read until internal ACK_ERROR goes away meaning write
		 * completed
		 */
		do {
			pci_write_config_dword(pdev,
					       LBCIF_ADDRESS_REGISTER,
					       addr);
			do {
				pci_read_config_dword(pdev,
					LBCIF_DATA_REGISTER, &val);
			} while ((val & 0x00010000) == 0);
		} while (val & 0x00040000);

		if ((val & 0xFF00) != 0xC000 || index == 10000)
			break;
		index++;
	}
	return writeok ? 0 : -EIO;
}

/**
 * eeprom_read - Read a byte from the ET1310's EEPROM
 * @etdev: pointer to our private adapter structure
 * @addr: the address from which to read
 * @pdata: a pointer to a byte in which to store the value of the read
 * @eeprom_id: the ID of the EEPROM
 * @addrmode: how the EEPROM is to be accessed
 *
 * Returns 1 for a successful read
 */
static int eeprom_read(struct et131x_adapter *etdev, u32 addr, u8 *pdata)
{
	struct pci_dev *pdev = etdev->pdev;
	int err;
	u32 status;

	/*
	 * A single byte read is similar to the single byte write, with the
	 * exception of the data flow:
	 */

	err = eeprom_wait_ready(pdev, NULL);
	if (err)
		return err;
	/*
	 * Write to the LBCIF Control Register:  bit 7=1, bit 6=0, bit 3=0,
	 * and bits 1:0 both =0.  Bit 5 should be set according to the type
	 * of EEPROM being accessed (1=two byte addressing, 0=one byte
	 * addressing).
	 */
	if (pci_write_config_byte(pdev, LBCIF_CONTROL_REGISTER,
				  LBCIF_CONTROL_LBCIF_ENABLE))
		return -EIO;
	/*
	 * Write the address to the LBCIF Address Register (I2C read will
	 * begin).
	 */
	if (pci_write_config_dword(pdev, LBCIF_ADDRESS_REGISTER, addr))
		return -EIO;
	/*
	 * Monitor bit 0 of the LBCIF Status Register.  When = 1, I2C read
	 * is complete. (if bit 1 =1 and bit 0 stays = 0, a hardware failure
	 * has occurred).
	 */
	err = eeprom_wait_ready(pdev, &status);
	if (err < 0)
		return err;
	/*
	 * Regardless of error status, read data byte from LBCIF Data
	 * Register.
	 */
	*pdata = err;
	/*
	 * Check bit 2 of the LBCIF Status Register.  If = 1,
	 * then an error has occurred.
	 */
	return (status & LBCIF_STATUS_ACK_ERROR) ? -EIO : 0;
}

int et131x_init_eeprom(struct et131x_adapter *etdev)
{
	struct pci_dev *pdev = etdev->pdev;
	u8 eestatus;

	/* We first need to check the EEPROM Status code located at offset
	 * 0xB2 of config space
	 */
	pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS,
				      &eestatus);

	/* THIS IS A WORKAROUND:
	 * I need to call this function twice to get my card in a
	 * LG M1 Express Dual running. I tried also a msleep before this
	 * function, because I thougth there could be some time condidions
	 * but it didn't work. Call the whole function twice also work.
	 */
	if (pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS, &eestatus)) {
		dev_err(&pdev->dev,
		       "Could not read PCI config space for EEPROM Status\n");
		return -EIO;
	}

	/* Determine if the error(s) we care about are present. If they are
	 * present we need to fail.
	 */
	if (eestatus & 0x4C) {
		int write_failed = 0;
		if (pdev->revision == 0x01) {
			int	i;
			static const u8 eedata[4] = { 0xFE, 0x13, 0x10, 0xFF };

			/* Re-write the first 4 bytes if we have an eeprom
			 * present and the revision id is 1, this fixes the
			 * corruption seen with 1310 B Silicon
			 */
			for (i = 0; i < 3; i++)
				if (eeprom_write(etdev, i, eedata[i]) < 0)
					write_failed = 1;
		}
		if (pdev->revision  != 0x01 || write_failed) {
			dev_err(&pdev->dev,
			    "Fatal EEPROM Status Error - 0x%04x\n", eestatus);

			/* This error could mean that there was an error
			 * reading the eeprom or that the eeprom doesn't exist.
			 * We will treat each case the same and not try to
			 * gather additional information that normally would
			 * come from the eeprom, like MAC Address
			 */
			etdev->has_eeprom = 0;
			return -EIO;
		}
	}
	etdev->has_eeprom = 1;

	/* Read the EEPROM for information regarding LED behavior. Refer to
	 * ET1310_phy.c, et131x_xcvr_init(), for its use.
	 */
	eeprom_read(etdev, 0x70, &etdev->eepromData[0]);
	eeprom_read(etdev, 0x71, &etdev->eepromData[1]);

	if (etdev->eepromData[0] != 0xcd)
		/* Disable all optional features */
		etdev->eepromData[1] = 0x00;

	return 0;
}

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
 * et131x_initpci.c - Routines and data used to register the driver with the
 *                    PCI (and PCI Express) subsystem, as well as basic driver
 *                    init and startup.
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
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
#include "et131x_debug.h"
#include "et131x_defs.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/random.h>

#include "et1310_phy.h"
#include "et1310_pm.h"
#include "et1310_jagcore.h"

#include "et131x_adapter.h"
#include "et131x_netdev.h"
#include "et131x_config.h"
#include "et131x_isr.h"

#include "et1310_address_map.h"
#include "et1310_tx.h"
#include "et1310_rx.h"
#include "et1310_mac.h"
#include "et1310_eeprom.h"


int __devinit et131x_pci_setup(struct pci_dev *pdev,
			       const struct pci_device_id *ent);
void __devexit et131x_pci_remove(struct pci_dev *pdev);


/* Modinfo parameters (filled out using defines from et131x_version.h) */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_INFO);
MODULE_LICENSE(DRIVER_LICENSE);

/* Module Parameters and related data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
static u32 et131x_debug_level = DBG_LVL;
static u32 et131x_debug_flags = DBG_DEFAULTS;

/*
et131x_debug_level :
 Level of debugging desired (0-7)
   7 : DBG_RX_ON | DBG_TX_ON
   6 : DBG_PARAM_ON
   5 : DBG_VERBOSE_ON
   4 : DBG_TRACE_ON
   3 : DBG_NOTICE_ON
   2 : no debug info
   1 : no debug info
   0 : no debug info
*/

module_param(et131x_debug_level, uint, 0);
module_param(et131x_debug_flags, uint, 0);

MODULE_PARM_DESC(et131x_debug_level, "Level of debugging desired (0-7)");

static dbg_info_t et131x_info = { DRIVER_NAME_EXT, 0, 0 };
dbg_info_t *et131x_dbginfo = &et131x_info;
#endif /* CONFIG_ET131X_DEBUG */

static struct pci_device_id et131x_pci_table[] __devinitdata = {
	{ET131X_PCI_VENDOR_ID, ET131X_PCI_DEVICE_ID_GIG, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0UL},
	{ET131X_PCI_VENDOR_ID, ET131X_PCI_DEVICE_ID_FAST, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0UL},
	{0,}
};

MODULE_DEVICE_TABLE(pci, et131x_pci_table);

static struct pci_driver et131x_driver = {
      .name	= DRIVER_NAME,
      .id_table	= et131x_pci_table,
      .probe	= et131x_pci_setup,
      .remove	= __devexit_p(et131x_pci_remove),
      .suspend	= NULL,		//et131x_pci_suspend,
      .resume	= NULL,		//et131x_pci_resume,
};


/**
 * et131x_init_module - The "main" entry point called on driver initialization
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_init_module(void)
{
	int result;

#ifdef CONFIG_ET131X_DEBUG
	/* Set the level of debug messages displayed using the module
	 * parameter
	 */
	et131x_dbginfo->dbgFlags = et131x_debug_flags;

	switch (et131x_debug_level) {
	case 7:
		et131x_dbginfo->dbgFlags |= (DBG_RX_ON | DBG_TX_ON);

	case 6:
		et131x_dbginfo->dbgFlags |= DBG_PARAM_ON;

	case 5:
		et131x_dbginfo->dbgFlags |= DBG_VERBOSE_ON;

	case 4:
		et131x_dbginfo->dbgFlags |= DBG_TRACE_ON;

	case 3:
		et131x_dbginfo->dbgFlags |= DBG_NOTICE_ON;

	case 2:
	case 1:
	case 0:
	default:
		break;
	}
#endif /* CONFIG_ET131X_DEBUG */

	DBG_ENTER(et131x_dbginfo);
	DBG_PRINT("%s\n", DRIVER_INFO);

	result = pci_register_driver(&et131x_driver);

	DBG_LEAVE(et131x_dbginfo);
	return result;
}

/**
 * et131x_cleanup_module - The entry point called on driver cleanup
 */
void et131x_cleanup_module(void)
{
	DBG_ENTER(et131x_dbginfo);

	pci_unregister_driver(&et131x_driver);

	DBG_LEAVE(et131x_dbginfo);
}

/*
 * These macros map the driver-specific init_module() and cleanup_module()
 * routines so they can be called by the kernel.
 */
module_init(et131x_init_module);
module_exit(et131x_cleanup_module);


/**
 * et131x_find_adapter - Find the adapter and get all the assigned resources
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_find_adapter(struct et131x_adapter *adapter, struct pci_dev *pdev)
{
	int result;
	uint8_t eepromStat;
	uint8_t maxPayload = 0;
	uint8_t read_size_reg;

	DBG_ENTER(et131x_dbginfo);

	/* Allow disabling of Non-Maskable Interrupts in I/O space, to
	 * support validation.
	 */
	if (adapter->RegistryNMIDisable) {
		uint8_t RegisterVal;

		RegisterVal = inb(ET1310_NMI_DISABLE);
		RegisterVal &= 0xf3;

		if (adapter->RegistryNMIDisable == 2) {
			RegisterVal |= 0xc;
		}

		outb(ET1310_NMI_DISABLE, RegisterVal);
	}

	/* We first need to check the EEPROM Status code located at offset
	 * 0xB2 of config space
	 */
	result = pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS,
				      &eepromStat);

	/* THIS IS A WORKAROUND:
 	 * I need to call this function twice to get my card in a
	 * LG M1 Express Dual running. I tried also a msleep before this
	 * function, because I thougth there could be some time condidions
	 * but it didn't work. Call the whole function twice also work.
	 */
	result = pci_read_config_byte(pdev, ET1310_PCI_EEPROM_STATUS,
				      &eepromStat);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo, "Could not read PCI config space for "
			  "EEPROM Status\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* Determine if the error(s) we care about are present.  If they are
	 * present, we need to fail.
	 */
	if (eepromStat & 0x4C) {
		result = pci_read_config_byte(pdev, PCI_REVISION_ID,
					      &adapter->RevisionID);
		if (result != PCIBIOS_SUCCESSFUL) {
			DBG_ERROR(et131x_dbginfo,
				  "Could not read PCI config space for "
				  "Revision ID\n");
			DBG_LEAVE(et131x_dbginfo);
			return -EIO;
		} else if (adapter->RevisionID == 0x01) {
			int32_t nLoop;
			uint8_t ucTemp[4] = { 0xFE, 0x13, 0x10, 0xFF };

			/* Re-write the first 4 bytes if we have an eeprom
			 * present and the revision id is 1, this fixes the
			 * corruption seen with 1310 B Silicon
			 */
			for (nLoop = 0; nLoop < 3; nLoop++) {
				EepromWriteByte(adapter, nLoop, ucTemp[nLoop],
						0, SINGLE_BYTE);
			}
		}

		DBG_ERROR(et131x_dbginfo,
			  "Fatal EEPROM Status Error - 0x%04x\n", eepromStat);

		/* This error could mean that there was an error reading the
		 * eeprom or that the eeprom doesn't exist.  We will treat
		 * each case the same and not try to gather additional
		 * information that normally would come from the eeprom, like
		 * MAC Address
		 */
		adapter->bEepromPresent = false;

		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	} else {
		DBG_TRACE(et131x_dbginfo, "EEPROM Status Code - 0x%04x\n",
			  eepromStat);
		adapter->bEepromPresent = true;
	}

	/* Read the EEPROM for information regarding LED behavior. Refer to
	 * ET1310_phy.c, et131x_xcvr_init(), for its use.
	 */
	EepromReadByte(adapter, 0x70, &adapter->eepromData[0], 0, SINGLE_BYTE);
	EepromReadByte(adapter, 0x71, &adapter->eepromData[1], 0, SINGLE_BYTE);

	if (adapter->eepromData[0] != 0xcd) {
		adapter->eepromData[1] = 0x00;	// Disable all optional features
	}

	/* Let's set up the PORT LOGIC Register.  First we need to know what
	 * the max_payload_size is
	 */
	result = pci_read_config_byte(pdev, ET1310_PCI_MAX_PYLD, &maxPayload);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo, "Could not read PCI config space for "
			  "Max Payload Size\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* Program the Ack/Nak latency and replay timers */
	maxPayload &= 0x07;	// Only the lower 3 bits are valid

	if (maxPayload < 2) {
		const uint16_t AckNak[2] = { 0x76, 0xD0 };
		const uint16_t Replay[2] = { 0x1E0, 0x2ED };

		result = pci_write_config_word(pdev, ET1310_PCI_ACK_NACK,
					       AckNak[maxPayload]);
		if (result != PCIBIOS_SUCCESSFUL) {
			DBG_ERROR(et131x_dbginfo,
				  "Could not write PCI config space "
				  "for ACK/NAK\n");
			DBG_LEAVE(et131x_dbginfo);
			return -EIO;
		}

		result = pci_write_config_word(pdev, ET1310_PCI_REPLAY,
					       Replay[maxPayload]);
		if (result != PCIBIOS_SUCCESSFUL) {
			DBG_ERROR(et131x_dbginfo,
				  "Could not write PCI config space "
				  "for Replay Timer\n");
			DBG_LEAVE(et131x_dbginfo);
			return -EIO;
		}
	}

	/* l0s and l1 latency timers.  We are using default values.
	 * Representing 001 for L0s and 010 for L1
	 */
	result = pci_write_config_byte(pdev, ET1310_PCI_L0L1LATENCY, 0x11);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo,
			  "Could not write PCI config space for "
			  "Latency Timers\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* Change the max read size to 2k */
	result = pci_read_config_byte(pdev, 0x51, &read_size_reg);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo,
			  "Could not read PCI config space for Max read size\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	read_size_reg &= 0x8f;
	read_size_reg |= 0x40;

	result = pci_write_config_byte(pdev, 0x51, read_size_reg);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo,
			  "Could not write PCI config space for Max read size\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* PCI Express Configuration registers 0x48-0x5B (Device Control) */
	result = pci_read_config_word(pdev, ET1310_PCI_DEV_CTRL,
				      &adapter->PciXDevCtl);
	if (result != PCIBIOS_SUCCESSFUL) {
		DBG_ERROR(et131x_dbginfo,
			  "Could not read PCI config space for PCI Express Dev Ctl\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* Get MAC address from config space if an eeprom exists, otherwise
	 * the MAC address there will not be valid
	 */
	if (adapter->bEepromPresent) {
		int i;

		for (i = 0; i < ETH_ALEN; i++) {
			result = pci_read_config_byte(
					pdev, ET1310_PCI_MAC_ADDRESS + i,
					adapter->PermanentAddress + i);
			if (result != PCIBIOS_SUCCESSFUL) {
				DBG_ERROR(et131x_dbginfo,
					  "Could not read PCI config space for MAC address\n");
				DBG_LEAVE(et131x_dbginfo);
				return -EIO;
			}
		}
	}

	DBG_LEAVE(et131x_dbginfo);
	return 0;
}

/**
 * et131x_error_timer_handler
 * @data: timer-specific variable; here a pointer to our adapter structure
 *
 * The routine called when the error timer expires, to track the number of
 * recurring errors.
 */
void et131x_error_timer_handler(unsigned long data)
{
	struct et131x_adapter *pAdapter = (struct et131x_adapter *) data;
	PM_CSR_t pm_csr;

	pm_csr.value = readl(&pAdapter->CSRAddress->global.pm_csr.value);

	if (pm_csr.bits.pm_phy_sw_coma == 0) {
		if (pAdapter->RegistryMACStat) {
			UpdateMacStatHostCounters(pAdapter);
		}
	} else {
		DBG_VERBOSE(et131x_dbginfo,
			    "No interrupts, in PHY coma, pm_csr = 0x%x\n",
			    pm_csr.value);
	}

	if (!pAdapter->Bmsr.bits.link_status &&
	    pAdapter->RegistryPhyComa &&
	    pAdapter->PoMgmt.TransPhyComaModeOnBoot < 11) {
		pAdapter->PoMgmt.TransPhyComaModeOnBoot++;
	}

	if (pAdapter->PoMgmt.TransPhyComaModeOnBoot == 10) {
		if (!pAdapter->Bmsr.bits.link_status
		    && pAdapter->RegistryPhyComa) {
			if (pm_csr.bits.pm_phy_sw_coma == 0) {
				// NOTE - This was originally a 'sync with interrupt'. How
				//        to do that under Linux?
				et131x_enable_interrupts(pAdapter);
				EnablePhyComa(pAdapter);
			}
		}
	}

	/* This is a periodic timer, so reschedule */
	mod_timer(&pAdapter->ErrorTimer, jiffies +
		  TX_ERROR_PERIOD * HZ / 1000);
}

/**
 * et131x_link_detection_handler
 *
 * Timer function for link up at driver load time
 */
void et131x_link_detection_handler(unsigned long data)
{
	struct et131x_adapter *pAdapter = (struct et131x_adapter *) data;
	unsigned long lockflags;

	/* Let everyone know that we have run */
	pAdapter->bLinkTimerActive = false;

	if (pAdapter->MediaState == 0) {
		spin_lock_irqsave(&pAdapter->Lock, lockflags);

		pAdapter->MediaState = NETIF_STATUS_MEDIA_DISCONNECT;
		MP_CLEAR_FLAG(pAdapter, fMP_ADAPTER_LINK_DETECTION);

		spin_unlock_irqrestore(&pAdapter->Lock, lockflags);

		netif_carrier_off(pAdapter->netdev);

		pAdapter->bSetPending = false;
	}
}

/**
 * et131x_adapter_setup - Set the adapter up as per cassini+ documentation
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_adapter_setup(struct et131x_adapter *pAdapter)
{
	int status = 0;

	DBG_ENTER(et131x_dbginfo);

	/* Configure the JAGCore */
	ConfigGlobalRegs(pAdapter);

	ConfigMACRegs1(pAdapter);
	ConfigMMCRegs(pAdapter);

	ConfigRxMacRegs(pAdapter);
	ConfigTxMacRegs(pAdapter);

	ConfigRxDmaRegs(pAdapter);
	ConfigTxDmaRegs(pAdapter);

	ConfigMacStatRegs(pAdapter);

	/* Move the following code to Timer function?? */
	status = et131x_xcvr_find(pAdapter);

	if (status != 0) {
		DBG_WARNING(et131x_dbginfo, "Could not find the xcvr\n");
	}

	/* Prepare the TRUEPHY library. */
	ET1310_PhyInit(pAdapter);

	/* Reset the phy now so changes take place */
	ET1310_PhyReset(pAdapter);

	/* Power down PHY */
	ET1310_PhyPowerDown(pAdapter, 1);

	/*
	 * We need to turn off 1000 base half dulplex, the mac does not
	 * support it. For the 10/100 part, turn off all gig advertisement
	 */
	if (pAdapter->DeviceID != ET131X_PCI_DEVICE_ID_FAST) {
		ET1310_PhyAdvertise1000BaseT(pAdapter, TRUEPHY_ADV_DUPLEX_FULL);
	} else {
		ET1310_PhyAdvertise1000BaseT(pAdapter, TRUEPHY_ADV_DUPLEX_NONE);
	}

	/* Power up PHY */
	ET1310_PhyPowerDown(pAdapter, 0);

	et131x_setphy_normal(pAdapter);

	DBG_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_setup_hardware_properties - set up the MAC Address on the ET1310
 * @adapter: pointer to our private adapter structure
 */
void et131x_setup_hardware_properties(struct et131x_adapter *adapter)
{
	DBG_ENTER(et131x_dbginfo);

	/* If have our default mac from registry and no mac address from
	 * EEPROM then we need to generate the last octet and set it on the
	 * device
	 */
	if (!adapter->bOverrideAddress) {
		if (adapter->PermanentAddress[0] == 0x00 &&
		    adapter->PermanentAddress[1] == 0x00 &&
		    adapter->PermanentAddress[2] == 0x00 &&
		    adapter->PermanentAddress[3] == 0x00 &&
		    adapter->PermanentAddress[4] == 0x00 &&
		    adapter->PermanentAddress[5] == 0x00) {
			/*
			 * We need to randomly generate the last octet so we
			 * decrease our chances of setting the mac address to
			 * same as another one of our cards in the system
			 */
			get_random_bytes(&adapter->CurrentAddress[5], 1);

			/*
			 * We have the default value in the register we are
			 * working with so we need to copy the current
			 * address into the permanent address
			 */
			memcpy(adapter->PermanentAddress,
			       adapter->CurrentAddress, ETH_ALEN);
		} else {
			/* We do not have an override address, so set the
			 * current address to the permanent address and add
			 * it to the device
			 */
			memcpy(adapter->CurrentAddress,
			       adapter->PermanentAddress, ETH_ALEN);
		}
	}

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_soft_reset - Issue a soft reset to the hardware, complete for ET1310
 * @adapter: pointer to our private adapter structure
 */
void et131x_soft_reset(struct et131x_adapter *adapter)
{
	DBG_ENTER(et131x_dbginfo);

	/* Disable MAC Core */
	writel(0xc00f0000, &adapter->CSRAddress->mac.cfg1.value);

	/* Set everything to a reset value */
	writel(0x7F, &adapter->CSRAddress->global.sw_reset.value);
	writel(0x000f0000, &adapter->CSRAddress->mac.cfg1.value);
	writel(0x00000000, &adapter->CSRAddress->mac.cfg1.value);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_align_allocated_memory - Align allocated memory on a given boundary
 * @adapter: pointer to our adapter structure
 * @phys_addr: pointer to Physical address
 * @offset: pointer to the offset variable
 * @mask: correct mask
 */
void et131x_align_allocated_memory(struct et131x_adapter *adapter,
				   uint64_t *phys_addr,
				   uint64_t *offset, uint64_t mask)
{
	uint64_t new_addr;

	DBG_ENTER(et131x_dbginfo);

	*offset = 0;

	new_addr = *phys_addr & ~mask;

	if (new_addr != *phys_addr) {
		/* Move to next aligned block */
		new_addr += mask + 1;
		/* Return offset for adjusting virt addr */
		*offset = new_addr - *phys_addr;
		/* Return new physical address */
		*phys_addr = new_addr;
	}

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_adapter_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h).
 *
 * Allocate all the memory blocks for send, receive and others.
 */
int et131x_adapter_memory_alloc(struct et131x_adapter *adapter)
{
	int status = 0;

	DBG_ENTER(et131x_dbginfo);

	do {
		/* Allocate memory for the Tx Ring */
		status = et131x_tx_dma_memory_alloc(adapter);
		if (status != 0) {
			DBG_ERROR(et131x_dbginfo,
				  "et131x_tx_dma_memory_alloc FAILED\n");
			break;
		}

		/* Receive buffer memory allocation */
		status = et131x_rx_dma_memory_alloc(adapter);
		if (status != 0) {
			DBG_ERROR(et131x_dbginfo,
				  "et131x_rx_dma_memory_alloc FAILED\n");
			et131x_tx_dma_memory_free(adapter);
			break;
		}

		/* Init receive data structures */
		status = et131x_init_recv(adapter);
		if (status != 0) {
			DBG_ERROR(et131x_dbginfo, "et131x_init_recv FAILED\n");
			et131x_tx_dma_memory_free(adapter);
			et131x_rx_dma_memory_free(adapter);
			break;
		}
	} while (0);

	DBG_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_adapter_memory_free - Free all memory allocated for use by Tx & Rx
 * @adapter: pointer to our private adapter structure
 */
void et131x_adapter_memory_free(struct et131x_adapter *adapter)
{
	DBG_ENTER(et131x_dbginfo);

	/* Free DMA memory */
	et131x_tx_dma_memory_free(adapter);
	et131x_rx_dma_memory_free(adapter);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_pci_remove
 * @pdev: a pointer to the device's pci_dev structure
 *
 * Registered in the pci_driver structure, this function is called when the
 * PCI subsystem detects that a PCI device which matches the information
 * contained in the pci_device_id table has been removed.
 */
void __devexit et131x_pci_remove(struct pci_dev *pdev)
{
	struct net_device *netdev;
	struct et131x_adapter *adapter;

	DBG_ENTER(et131x_dbginfo);

	/* Retrieve the net_device pointer from the pci_dev struct, as well
	 * as the private adapter struct
	 */
	netdev = (struct net_device *) pci_get_drvdata(pdev);
	adapter = netdev_priv(netdev);

	/* Perform device cleanup */
	unregister_netdev(netdev);
	et131x_adapter_memory_free(adapter);
	iounmap(adapter->CSRAddress);
	free_netdev(netdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_pci_setup - Perform device initialization
 * @pdev: a pointer to the device's pci_dev structure
 * @ent: this device's entry in the pci_device_id table
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 *
 * Registered in the pci_driver structure, this function is called when the
 * PCI subsystem finds a new PCI device which matches the information
 * contained in the pci_device_id table. This routine is the equivalent to
 * a device insertion routine.
 */
int __devinit et131x_pci_setup(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	int result = 0;
	int pm_cap;
	bool pci_using_dac;
	struct net_device *netdev = NULL;
	struct et131x_adapter *adapter = NULL;

	DBG_ENTER(et131x_dbginfo);

	/* Enable the device via the PCI subsystem */
	result = pci_enable_device(pdev);
	if (result != 0) {
		DBG_ERROR(et131x_dbginfo, "pci_enable_device() failed\n");
		goto out;
	}

	/* Perform some basic PCI checks */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		DBG_ERROR(et131x_dbginfo,
			  "Can't find PCI device's base address\n");
		result = -ENODEV;
		goto out;
	}

	result = pci_request_regions(pdev, DRIVER_NAME);
	if (result != 0) {
		DBG_ERROR(et131x_dbginfo, "Can't get PCI resources\n");
		goto err_disable;
	}

	/* Enable PCI bus mastering */
	DBG_TRACE(et131x_dbginfo, "Setting PCI Bus Mastering...\n");
	pci_set_master(pdev);

	/* Query PCI for Power Mgmt Capabilities
	 *
	 * NOTE: Now reading PowerMgmt in another location; is this still
	 * needed?
	 */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap == 0) {
		DBG_ERROR(et131x_dbginfo,
			  "Cannot find Power Management capabilities\n");
		result = -EIO;
		goto err_release_res;
	}

	/* Check the DMA addressing support of this device */
	if (!pci_set_dma_mask(pdev, 0xffffffffffffffffULL)) {
		DBG_TRACE(et131x_dbginfo, "64-bit DMA addressing supported\n");
		pci_using_dac = true;

		result =
		    pci_set_consistent_dma_mask(pdev, 0xffffffffffffffffULL);
		if (result != 0) {
			DBG_ERROR(et131x_dbginfo,
				  "Unable to obtain 64 bit DMA for consistent allocations\n");
			goto err_release_res;
		}
	} else if (!pci_set_dma_mask(pdev, 0xffffffffULL)) {
		DBG_TRACE(et131x_dbginfo,
			  "64-bit DMA addressing NOT supported\n");
		DBG_TRACE(et131x_dbginfo,
			  "32-bit DMA addressing will be used\n");
		pci_using_dac = false;
	} else {
		DBG_ERROR(et131x_dbginfo, "No usable DMA addressing method\n");
		result = -EIO;
		goto err_release_res;
	}

	/* Allocate netdev and private adapter structs */
	DBG_TRACE(et131x_dbginfo,
		  "Allocate netdev and private adapter structs...\n");
	netdev = et131x_device_alloc();
	if (netdev == NULL) {
		DBG_ERROR(et131x_dbginfo, "Couldn't alloc netdev struct\n");
		result = -ENOMEM;
		goto err_release_res;
	}

	/* Setup the fundamental net_device and private adapter structure elements  */
	DBG_TRACE(et131x_dbginfo, "Setting fundamental net_device info...\n");
	SET_NETDEV_DEV(netdev, &pdev->dev);
	if (pci_using_dac) {
		//netdev->features |= NETIF_F_HIGHDMA;
	}

	/*
	 * NOTE - Turn this on when we're ready to deal with SG-DMA
	 *
	 * NOTE: According to "Linux Device Drivers", 3rd ed, Rubini et al,
	 * if checksumming is not performed in HW, then the kernel will not
	 * use SG.
	 * From pp 510-511:
	 *
	 * "Note that the kernel does not perform scatter/gather I/O to your
	 * device if it does not also provide some form of checksumming as
	 * well. The reason is that, if the kernel has to make a pass over a
	 * fragmented ("nonlinear") packet to calculate the checksum, it
	 * might as well copy the data and coalesce the packet at the same
	 * time."
	 *
	 * This has been verified by setting the flags below and still not
	 * receiving a scattered buffer from the network stack, so leave it
	 * off until checksums are calculated in HW.
	 */
	//netdev->features |= NETIF_F_SG;
	//netdev->features |= NETIF_F_NO_CSUM;
	//netdev->features |= NETIF_F_LLTX;

	/* Allocate private adapter struct and copy in relevant information */
	adapter = netdev_priv(netdev);
	adapter->pdev = pdev;
	adapter->netdev = netdev;
	adapter->VendorID = pdev->vendor;
	adapter->DeviceID = pdev->device;

	/* Do the same for the netdev struct */
	netdev->irq = pdev->irq;
	netdev->base_addr = pdev->resource[0].start;

	/* Initialize spinlocks here */
	DBG_TRACE(et131x_dbginfo, "Initialize spinlocks...\n");

	spin_lock_init(&adapter->Lock);
	spin_lock_init(&adapter->TCBSendQLock);
	spin_lock_init(&adapter->TCBReadyQLock);
	spin_lock_init(&adapter->SendHWLock);
	spin_lock_init(&adapter->SendWaitLock);
	spin_lock_init(&adapter->RcvLock);
	spin_lock_init(&adapter->RcvPendLock);
	spin_lock_init(&adapter->FbrLock);
	spin_lock_init(&adapter->PHYLock);

	/* Parse configuration parameters into the private adapter struct */
	et131x_config_parse(adapter);

	/* Find the physical adapter
	 *
	 * NOTE: This is the equivalent of the MpFindAdapter() routine; can we
	 *       lump it's init with the device specific init below into a
	 *       single init function?
	 */
	//while (et131x_find_adapter(adapter, pdev) != 0);
	et131x_find_adapter(adapter, pdev);

	/* Map the bus-relative registers to system virtual memory */
	DBG_TRACE(et131x_dbginfo,
		  "Mapping bus-relative registers to virtual memory...\n");

	adapter->CSRAddress = ioremap_nocache(pci_resource_start(pdev, 0),
					      pci_resource_len(pdev, 0));
	if (adapter->CSRAddress == NULL) {
		DBG_ERROR(et131x_dbginfo, "Cannot map device registers\n");
		result = -ENOMEM;
		goto err_free_dev;
	}

	/* Perform device-specific initialization here (See code below) */

	/* If Phy COMA mode was enabled when we went down, disable it here. */
	{
		PM_CSR_t GlobalPmCSR = { 0 };

		GlobalPmCSR.bits.pm_sysclk_gate = 1;
		GlobalPmCSR.bits.pm_txclk_gate = 1;
		GlobalPmCSR.bits.pm_rxclk_gate = 1;
		writel(GlobalPmCSR.value,
		       &adapter->CSRAddress->global.pm_csr.value);
	}

	/* Issue a global reset to the et1310 */
	DBG_TRACE(et131x_dbginfo, "Issuing soft reset...\n");
	et131x_soft_reset(adapter);

	/* Disable all interrupts (paranoid) */
	DBG_TRACE(et131x_dbginfo, "Disable device interrupts...\n");
	et131x_disable_interrupts(adapter);

	/* Allocate DMA memory */
	result = et131x_adapter_memory_alloc(adapter);
	if (result != 0) {
		DBG_ERROR(et131x_dbginfo,
			  "Could not alloc adapater memory (DMA)\n");
		goto err_iounmap;
	}

	/* Init send data structures */
	DBG_TRACE(et131x_dbginfo, "Init send data structures...\n");
	et131x_init_send(adapter);

	adapter->PoMgmt.PowerState = NdisDeviceStateD0;

	/* Register the interrupt
	 *
	 * NOTE - This is being done in the open routine, where most other
	 *         Linux drivers setup IRQ handlers. Make sure device
	 *         interrupts are not turned on before the IRQ is registered!!
	 *
	 *         What we will do here is setup the task structure for the
	 *         ISR's deferred handler
	 */
	INIT_WORK(&adapter->task, et131x_isr_handler);

	/* Determine MAC Address, and copy into the net_device struct */
	DBG_TRACE(et131x_dbginfo, "Retrieve MAC address...\n");
	et131x_setup_hardware_properties(adapter);

	memcpy(netdev->dev_addr, adapter->CurrentAddress, ETH_ALEN);

	/* Setup et1310 as per the documentation */
	DBG_TRACE(et131x_dbginfo, "Setup the adapter...\n");
	et131x_adapter_setup(adapter);

	/* Create a timer to count errors received by the NIC */
	init_timer(&adapter->ErrorTimer);

	adapter->ErrorTimer.expires = jiffies + TX_ERROR_PERIOD * HZ / 1000;
	adapter->ErrorTimer.function = et131x_error_timer_handler;
	adapter->ErrorTimer.data = (unsigned long)adapter;

	/* Initialize link state */
	et131x_link_detection_handler((unsigned long)adapter);

	/* Intialize variable for counting how long we do not have link status */
	adapter->PoMgmt.TransPhyComaModeOnBoot = 0;

	/* We can enable interrupts now
	 *
	 *  NOTE - Because registration of interrupt handler is done in the
	 *         device's open(), defer enabling device interrupts to that
	 *         point
	 */

	/* Register the net_device struct with the Linux network layer */
	DBG_TRACE(et131x_dbginfo, "Registering net_device...\n");
	if ((result = register_netdev(netdev)) != 0) {
		DBG_ERROR(et131x_dbginfo, "register_netdev() failed\n");
		goto err_mem_free;
	}

	/* Register the net_device struct with the PCI subsystem. Save a copy
	 * of the PCI config space for this device now that the device has
	 * been initialized, just in case it needs to be quickly restored.
	 */
	pci_set_drvdata(pdev, netdev);

	pci_save_state(adapter->pdev);

out:
	DBG_LEAVE(et131x_dbginfo);
	return result;

err_mem_free:
	et131x_adapter_memory_free(adapter);
err_iounmap:
	iounmap(adapter->CSRAddress);
err_free_dev:
	free_netdev(netdev);
err_release_res:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	goto out;
}

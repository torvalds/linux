/*
 *  rar_register.c - An Intel Restricted Access Region register driver
 *
 *  Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * -------------------------------------------------------------------
 *  20091204 Mark Allyn <mark.a.allyn@intel.com>
 *	     Ossama Othman <ossama.othman@intel.com>
 *	Cleanup per feedback from Alan Cox and Arjan Van De Ven
 *
 *  20090806 Ossama Othman <ossama.othman@intel.com>
 *      Return zero high address if upper 22 bits is zero.
 *      Cleaned up checkpatch errors.
 *      Clarified that driver is dealing with bus addresses.
 *
 *  20090702 Ossama Othman <ossama.othman@intel.com>
 *      Removed unnecessary include directives
 *      Cleaned up spinlocks.
 *      Cleaned up logging.
 *      Improved invalid parameter checks.
 *      Fixed and simplified RAR address retrieval and RAR locking
 *      code.
 *
 *  20090626 Mark Allyn <mark.a.allyn@intel.com>
 *      Initial publish
 */

#define DEBUG 1

#include "rar_register.h"

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/kernel.h>

/* === Lincroft Message Bus Interface === */
/* Message Control Register */
#define LNC_MCR_OFFSET 0xD0

/* Maximum number of clients (other drivers using this driver) */
#define MAX_RAR_CLIENTS 10

/* Message Data Register */
#define LNC_MDR_OFFSET 0xD4

/* Message Opcodes */
#define LNC_MESSAGE_READ_OPCODE 0xD0
#define LNC_MESSAGE_WRITE_OPCODE 0xE0

/* Message Write Byte Enables */
#define LNC_MESSAGE_BYTE_WRITE_ENABLES 0xF

/* B-unit Port */
#define LNC_BUNIT_PORT 0x3

/* === Lincroft B-Unit Registers - Programmed by IA32 firmware === */
#define LNC_BRAR0L 0x10
#define LNC_BRAR0H 0x11
#define LNC_BRAR1L 0x12
#define LNC_BRAR1H 0x13

/* Reserved for SeP */
#define LNC_BRAR2L 0x14
#define LNC_BRAR2H 0x15

/* Moorestown supports three restricted access regions. */
#define MRST_NUM_RAR 3


/* RAR Bus Address Range */
struct RAR_address_range {
	dma_addr_t low;
	dma_addr_t high;
};

/* Structure containing low and high RAR register offsets. */
struct RAR_offsets {
	u32 low;  /* Register offset for low  RAR bus address. */
	u32 high; /* Register offset for high RAR bus address. */
};

struct client {
	int (*client_callback)(void *client_data);
	void *customer_data;
	int client_called;
	};

static DEFINE_MUTEX(rar_mutex);
static DEFINE_MUTEX(lnc_reg_mutex);

struct RAR_device {
	struct RAR_offsets const rar_offsets[MRST_NUM_RAR];
	struct RAR_address_range rar_addr[MRST_NUM_RAR];
	struct pci_dev *rar_dev;
	bool registered;
	};

/* this platform has only one rar_device for 3 rar regions */
static struct RAR_device my_rar_device = {
	.rar_offsets = {
		[0].low = LNC_BRAR0L,
		[0].high = LNC_BRAR0H,
		[1].low = LNC_BRAR1L,
		[1].high = LNC_BRAR1H,
		[2].low = LNC_BRAR2L,
		[2].high = LNC_BRAR2H
	}
};

/* this data is for handling requests from other drivers which arrive
 * prior to this driver initializing
 */

static struct client clients[MAX_RAR_CLIENTS];
static int num_clients;

/*
 * This function is used to retrieved RAR info using the Lincroft
 * message bus interface.
 */
static int retrieve_rar_addr(struct pci_dev *pdev,
	int offset,
	dma_addr_t *addr)
{
	/*
	 * ======== The Lincroft Message Bus Interface ========
	 * Lincroft registers may be obtained from the PCI
	 * (the Host Bridge) using the Lincroft Message Bus
	 * Interface.  That message bus interface is generally
	 * comprised of two registers: a control register (MCR, 0xDO)
	 * and a data register (MDR, 0xD4).
	 *
	 * The MCR (message control register) format is the following:
	 *   1.  [31:24]: Opcode
	 *   2.  [23:16]: Port
	 *   3.  [15:8]: Register Offset
	 *   4.  [7:4]: Byte Enables (use 0xF to set all of these bits
	 *              to 1)
	 *   5.  [3:0]: reserved
	 *
	 *  Read (0xD0) and write (0xE0) opcodes are written to the
	 *  control register when reading and writing to Lincroft
	 *  registers, respectively.
	 *
	 *  We're interested in registers found in the Lincroft
	 *  B-unit.  The B-unit port is 0x3.
	 *
	 *  The six B-unit RAR register offsets we use are listed
	 *  earlier in this file.
	 *
	 *  Lastly writing to the MCR register requires the "Byte
	 *  enables" bits to be set to 1.  This may be achieved by
	 *  writing 0xF at bit 4.
	 *
	 * The MDR (message data register) format is the following:
	 *   1. [31:0]: Read/Write Data
	 *
	 *  Data being read from this register is only available after
	 *  writing the appropriate control message to the MCR
	 *  register.
	 *
	 *  Data being written to this register must be written before
	 *  writing the appropriate control message to the MCR
	 *  register.
	*/

	int result;

	/* Construct control message */
	u32 const message =
		 (LNC_MESSAGE_READ_OPCODE << 24)
		 | (LNC_BUNIT_PORT << 16)
		 | (offset << 8)
		 | (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

	dev_dbg(&pdev->dev, "Offset for 'get' LNC MSG is %x\n", offset);

	if (addr == 0) {
		WARN_ON(1);
		return -EINVAL;
	}

	/*
	* We synchronize access to the Lincroft MCR and MDR registers
	* until BOTH the command is issued through the MCR register
	* and the corresponding data is read from the MDR register.
	* Otherwise a race condition would exist between accesses to
	* both registers.
	*/

	mutex_lock(&lnc_reg_mutex);

	/* Send the control message */
	result = pci_write_config_dword(pdev, LNC_MCR_OFFSET, message);

	dev_dbg(&pdev->dev, "Result from send ctl register is %x\n", result);

	if (!result) {
		result = pci_read_config_dword(pdev, LNC_MDR_OFFSET,
			(u32 *)addr);
		dev_dbg(&pdev->dev,
			"Result from read data register is %x\n", result);

		dev_dbg(&pdev->dev,
			"Value read from data register is %lx\n",
			 (unsigned long)*addr);
	}

	mutex_unlock(&lnc_reg_mutex);

	return result;
}

static int set_rar_address(struct pci_dev *pdev,
	int offset,
	dma_addr_t addr)
{
	/*
	* Data being written to this register must be written before
	* writing the appropriate control message to the MCR
	* register.
	* @note See rar_get_address() for a description of the
	* message bus interface being used here.
	*/

	int result = 0;

	/* Construct control message */
	u32 const message = (LNC_MESSAGE_WRITE_OPCODE << 24)
		| (LNC_BUNIT_PORT << 16)
		| (offset << 8)
		| (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

	if (addr == 0) {
		WARN_ON(1);
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "Offset for 'set' LNC MSG is %x\n", offset);

	/*
	* We synchronize access to the Lincroft MCR and MDR registers
	* until BOTH the command is issued through the MCR register
	* and the corresponding data is read from the MDR register.
	* Otherwise a race condition would exist between accesses to
	* both registers.
	*/

	mutex_lock(&lnc_reg_mutex);

	/* Send the control message */
	result = pci_write_config_dword(pdev, LNC_MDR_OFFSET, addr);

	dev_dbg(&pdev->dev, "Result from write data register is %x\n", result);

	if (!result) {
		dev_dbg(&pdev->dev,
			"Value written to data register is %lx\n",
			 (unsigned long)addr);

		result = pci_write_config_dword(pdev, LNC_MCR_OFFSET, message);

		dev_dbg(&pdev->dev, "Result from send ctl register is %x\n",
			result);
	}

	mutex_unlock(&lnc_reg_mutex);

	return result;
}

/*
* Initialize RAR parameters, such as bus addresses, etc.
*/
static int init_rar_params(struct pci_dev *pdev)
{
	unsigned int i;
	int result = 0;

	/* Retrieve RAR start and end bus addresses.
	* Access the RAR registers through the Lincroft Message Bus
	* Interface on PCI device: 00:00.0 Host bridge.
	*/

	for (i = 0; i < MRST_NUM_RAR; ++i) {
		struct RAR_offsets const *offset =
			&my_rar_device.rar_offsets[i];
		struct RAR_address_range *addr = &my_rar_device.rar_addr[i];

	if ((retrieve_rar_addr(pdev, offset->low, &addr->low) != 0)
		|| (retrieve_rar_addr(pdev, offset->high, &addr->high) != 0)) {
		result = -1;
		break;
		}

		/*
		* Only the upper 22 bits of the RAR addresses are
		* stored in their corresponding RAR registers so we
		* must set the lower 10 bits accordingly.

		* The low address has its lower 10 bits cleared, and
		* the high address has all its lower 10 bits set,
		* e.g.:
		* low = 0x2ffffc00
		*/

		addr->low &= (dma_addr_t)0xfffffc00u;

		/*
		* Set bits 9:0 on uppser address if bits 31:10 are non
		* zero; otherwize clear all bits
		*/

		if ((addr->high & 0xfffffc00u) == 0)
			addr->high = 0;
		else
			addr->high |= 0x3ffu;
	}
	/* Done accessing the device. */

	if (result == 0) {
		int z;
		for (z = 0; z != MRST_NUM_RAR; ++z) {
			/*
			* "BRAR" refers to the RAR registers in the
			* Lincroft B-unit.
			*/
			dev_info(&pdev->dev, "BRAR[%u] bus address range = "
			  "[%lx, %lx]\n", z,
			  (unsigned long)my_rar_device.rar_addr[z].low,
			  (unsigned long)my_rar_device.rar_addr[z].high);
		}
	}

	return result;
}

/*
 * The rar_get_address function is used by other device drivers
 * to obtain RAR address information on a RAR. It takes three
 * parameters:
 *
 * int rar_index
 * The rar_index is an index to the rar for which you wish to retrieve
 * the address information.
 * Values can be 0,1, or 2.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int rar_get_address(int rar_index,
	dma_addr_t *start_address,
	dma_addr_t *end_address)
{
	int result = -ENODEV;

	if (my_rar_device.registered) {
		if (start_address == 0 || end_address == 0
			|| rar_index >= MRST_NUM_RAR || rar_index < 0) {
			result = -EINVAL;
		} else {
			*start_address =
				my_rar_device.rar_addr[rar_index].low;
			*end_address =
				my_rar_device.rar_addr[rar_index].high;

			result = 0;
		}
	}

	return result;
}
EXPORT_SYMBOL(rar_get_address);

/*
 * The rar_lock function is ued by other device drivers to lock an RAR.
 * once an RAR is locked, it stays locked until the next system reboot.
 * The function takes one parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar that you want to lock.
 * Values can be 0,1, or 2.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int rar_lock(int rar_index)
{
	int result = -ENODEV;

	if (rar_index >= MRST_NUM_RAR || rar_index < 0) {
		result = -EINVAL;
		return result;
	}

	dev_dbg(&my_rar_device.rar_dev->dev, "rar_lock mutex locking\n");
	mutex_lock(&rar_mutex);

	if (my_rar_device.registered) {

		dma_addr_t low = my_rar_device.rar_addr[rar_index].low &
			0xfffffc00u;

		dma_addr_t high = my_rar_device.rar_addr[rar_index].high &
			0xfffffc00u;

		/*
		* Only allow I/O from the graphics and Langwell;
		* Not from the x96 processor
		*/
		if (rar_index == (int)RAR_TYPE_VIDEO) {
			low |= 0x00000009;
			high |= 0x00000015;
		}

		else if (rar_index == (int)RAR_TYPE_AUDIO) {
			/* Only allow I/O from Langwell; nothing from x86 */
			low |= 0x00000008;
			high |= 0x00000018;
		}

		else
			/* Read-only from all agents */
			high |= 0x00000018;

		/*
		* Now program the register using the Lincroft message
		* bus interface.
		*/
		result = set_rar_address(my_rar_device.rar_dev,
			my_rar_device.rar_offsets[rar_index].low,
			low);

		if (result == 0)
			result = set_rar_address(
			my_rar_device.rar_dev,
			my_rar_device.rar_offsets[rar_index].high,
			high);
	}

	dev_dbg(&my_rar_device.rar_dev->dev, "rar_lock mutex unlocking\n");
	mutex_unlock(&rar_mutex);
	return result;
}
EXPORT_SYMBOL(rar_lock);

/* The register_rar function is to used by other device drivers
 * to ensure that this driver is ready. As we cannot be sure of
 * the compile/execute order of dirvers in ther kernel, it is
 * best to give this driver a callback function to call when
 * it is ready to give out addresses. The callback function
 * would have those steps that continue the initialization of
 * a driver that do require a valid RAR address. One of those
 * steps would be to call rar_get_address()
 * This function return 0 on success an -1 on failure.
*/
int register_rar(int (*callback)(void *yourparameter), void *yourparameter)
{

	int result = -ENODEV;

	if (callback == NULL)
		return -EINVAL;

	mutex_lock(&rar_mutex);

	if (my_rar_device.registered) {

		mutex_unlock(&rar_mutex);
		/*
		* if the driver already registered, then we can simply
		* call the callback right now
		*/

		return (*callback)(yourparameter);
	}

	if (num_clients < MRST_NUM_RAR) {

		clients[num_clients].client_callback = callback;
		clients[num_clients].customer_data = yourparameter;
		num_clients += 1;
		result = 0;
	}

	mutex_unlock(&rar_mutex);
	return result;

}
EXPORT_SYMBOL(register_rar);

/* Suspend - returns -ENOSYS */
static int rar_suspend(struct pci_dev *dev, pm_message_t state)
{
	return -ENOSYS;
}

static int rar_resume(struct pci_dev *dev)
{
	return -ENOSYS;
}

/*
 * This function registers the driver with the device subsystem (
 * either PCI, USB, etc).
 * Function that is activaed on the succesful probe of the RAR device
 * (Moorestown host controller).
 */
static int rar_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int error;
	int counter;

	dev_dbg(&dev->dev, "PCI probe starting\n");

	/* enable the device */
	error = pci_enable_device(dev);
	if (error) {
		dev_err(&dev->dev,
			"Error enabling RAR register PCI device\n");
		goto end_function;
	}

	/* we have only one device; fill in the rar_device structure */
	my_rar_device.rar_dev = dev;

	/*
	* Initialize the RAR parameters, which have to be retrieved
	* via the message bus interface.
	*/
	error = init_rar_params(dev);
	if (error) {
		pci_disable_device(dev);

		dev_err(&dev->dev,
			"Error retrieving RAR addresses\n");

		goto end_function;
	}

	dev_dbg(&dev->dev, "PCI probe locking\n");
	mutex_lock(&rar_mutex);
	my_rar_device.registered = 1;

	/* now call anyone who has registered (using callbacks) */
	for (counter = 0; counter < num_clients; counter += 1) {
		if (clients[counter].client_callback) {
			error = (*clients[counter].client_callback)(
				clients[counter].customer_data);
			/* set callback to NULL to indicate it has been done */
			clients[counter].client_callback = NULL;
				dev_dbg(&my_rar_device.rar_dev->dev,
				"Callback called for %d\n",
			counter);
		}
	}

	dev_dbg(&dev->dev, "PCI probe unlocking\n");
	mutex_unlock(&rar_mutex);

end_function:

	return error;
}

const struct pci_device_id rar_pci_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_RAR_DEVICE_ID) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, rar_pci_id_tbl);

const struct pci_device_id *my_id_table = rar_pci_id_tbl;

/* field for registering driver to PCI device */
static struct pci_driver rar_pci_driver = {
	.name = "rar_register_driver",
	.id_table = rar_pci_id_tbl,
	.probe = rar_probe,
	.suspend = rar_suspend,
	.resume = rar_resume
};

static int __init rar_init_handler(void)
{
	return pci_register_driver(&rar_pci_driver);
}

static void __exit rar_exit_handler(void)
{
	pci_unregister_driver(&rar_pci_driver);
}

module_init(rar_init_handler);
module_exit(rar_exit_handler);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Restricted Access Region Register Driver");

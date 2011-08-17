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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/rar_register.h>

/* === Lincroft Message Bus Interface === */
#define LNC_MCR_OFFSET		0xD0	/* Message Control Register */
#define LNC_MDR_OFFSET		0xD4	/* Message Data Register */

/* Message Opcodes */
#define LNC_MESSAGE_READ_OPCODE	0xD0
#define LNC_MESSAGE_WRITE_OPCODE 0xE0

/* Message Write Byte Enables */
#define LNC_MESSAGE_BYTE_WRITE_ENABLES	0xF

/* B-unit Port */
#define LNC_BUNIT_PORT	0x3

/* === Lincroft B-Unit Registers - Programmed by IA32 firmware === */
#define LNC_BRAR0L	0x10
#define LNC_BRAR0H	0x11
#define LNC_BRAR1L	0x12
#define LNC_BRAR1H	0x13
/* Reserved for SeP */
#define LNC_BRAR2L	0x14
#define LNC_BRAR2H	0x15

/* Moorestown supports three restricted access regions. */
#define MRST_NUM_RAR 3

/* RAR Bus Address Range */
struct rar_addr {
	dma_addr_t low;
	dma_addr_t high;
};

/*
 *	We create one of these for each RAR
 */
struct client {
	int (*callback)(unsigned long data);
	unsigned long driver_priv;
	bool busy;
};

static DEFINE_MUTEX(rar_mutex);
static DEFINE_MUTEX(lnc_reg_mutex);

/*
 *	One per RAR device (currently only one device)
 */
struct rar_device {
	struct rar_addr rar_addr[MRST_NUM_RAR];
	struct pci_dev *rar_dev;
	bool registered;
	bool allocated;
	struct client client[MRST_NUM_RAR];
};

/* Current platforms have only one rar_device for 3 rar regions */
static struct rar_device my_rar_device;

/*
 *	Abstract out multiple device support. Current platforms only
 *	have a single RAR device.
 */

/**
 *	alloc_rar_device	-	return a new RAR structure
 *
 *	Return a new (but not yet ready) RAR device object
 */
static struct rar_device *alloc_rar_device(void)
{
	if (my_rar_device.allocated)
		return NULL;
	my_rar_device.allocated = 1;
	return &my_rar_device;
}

/**
 *	free_rar_device		-	free a RAR object
 *	@rar: the RAR device being freed
 *
 *	Release a RAR object and any attached resources
 */
static void free_rar_device(struct rar_device *rar)
{
	pci_dev_put(rar->rar_dev);
	rar->allocated = 0;
}

/**
 *	_rar_to_device		-	return the device handling this RAR
 *	@rar: RAR number
 *	@off: returned offset
 *
 *	Internal helper for looking up RAR devices. This and alloc are the
 *	two functions that need touching to go to multiple RAR devices.
 */
static struct rar_device *_rar_to_device(int rar, int *off)
{
	if (rar >= 0 && rar < MRST_NUM_RAR) {
		*off = rar;
		return &my_rar_device;
	}
	return NULL;
}

/**
 *	rar_to_device		-	return the device handling this RAR
 *	@rar: RAR number
 *	@off: returned offset
 *
 *	Return the device this RAR maps to if one is present, otherwise
 *	returns NULL. Reports the offset relative to the base of this
 *	RAR device in off.
 */
static struct rar_device *rar_to_device(int rar, int *off)
{
	struct rar_device *rar_dev = _rar_to_device(rar, off);
	if (rar_dev == NULL || !rar_dev->registered)
		return NULL;
	return rar_dev;
}

/**
 *	rar_to_client		-	return the client handling this RAR
 *	@rar: RAR number
 *
 *	Return the client this RAR maps to if a mapping is known, otherwise
 *	returns NULL.
 */
static struct client *rar_to_client(int rar)
{
	int idx;
	struct rar_device *r = _rar_to_device(rar, &idx);
	if (r != NULL)
		return &r->client[idx];
	return NULL;
}

/**
 *	rar_read_addr		-	retrieve a RAR mapping
 *	@pdev: PCI device for the RAR
 *	@offset: offset for message
 *	@addr: returned address
 *
 *	Reads the address of a given RAR register. Returns 0 on success
 *	or an error code on failure.
 */
static int rar_read_addr(struct pci_dev *pdev, int offset, dma_addr_t *addr)
{
	/*
	 * ======== The Lincroft Message Bus Interface ========
	 * Lincroft registers may be obtained via PCI from
	 * the host bridge using the Lincroft Message Bus
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
	u32 addr32;

	/* Construct control message */
	u32 const message =
		 (LNC_MESSAGE_READ_OPCODE << 24)
		 | (LNC_BUNIT_PORT << 16)
		 | (offset << 8)
		 | (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

	dev_dbg(&pdev->dev, "Offset for 'get' LNC MSG is %x\n", offset);

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
	if (!result) {
		/* Read back the address as a 32bit value */
		result = pci_read_config_dword(pdev, LNC_MDR_OFFSET, &addr32);
		*addr = (dma_addr_t)addr32;
	}
	mutex_unlock(&lnc_reg_mutex);
	return result;
}

/**
 *	rar_set_addr		-	Set a RAR mapping
 *	@pdev: PCI device for the RAR
 *	@offset: offset for message
 *	@addr: address to set
 *
 *	Sets the address of a given RAR register. Returns 0 on success
 *	or an error code on failure.
 */
static int rar_set_addr(struct pci_dev *pdev,
	int offset,
	dma_addr_t addr)
{
	/*
	* Data being written to this register must be written before
	* writing the appropriate control message to the MCR
	* register.
	* See rar_get_addrs() for a description of the
	* message bus interface being used here.
	*/

	int result;

	/* Construct control message */
	u32 const message = (LNC_MESSAGE_WRITE_OPCODE << 24)
		| (LNC_BUNIT_PORT << 16)
		| (offset << 8)
		| (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

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
	if (!result)
		/* And address */
		result = pci_write_config_dword(pdev, LNC_MCR_OFFSET, message);

	mutex_unlock(&lnc_reg_mutex);
	return result;
}

/*
 *	rar_init_params		-	Initialize RAR parameters
 *	@rar: RAR device to initialise
 *
 *	Initialize RAR parameters, such as bus addresses, etc. Returns 0
 *	on success, or an error code on failure.
 */
static int init_rar_params(struct rar_device *rar)
{
	struct pci_dev *pdev = rar->rar_dev;
	unsigned int i;
	int result = 0;
	int offset = 0x10;	/* RAR 0 to 2 in order low/high/low/high/... */

	/* Retrieve RAR start and end bus addresses.
	* Access the RAR registers through the Lincroft Message Bus
	* Interface on PCI device: 00:00.0 Host bridge.
	*/

	for (i = 0; i < MRST_NUM_RAR; ++i) {
		struct rar_addr *addr = &rar->rar_addr[i];

		result = rar_read_addr(pdev, offset++, &addr->low);
		if (result != 0)
			return result;

		result = rar_read_addr(pdev, offset++, &addr->high);
		if (result != 0)
			return result;


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
		for (i = 0; i != MRST_NUM_RAR; ++i) {
			/*
			* "BRAR" refers to the RAR registers in the
			* Lincroft B-unit.
			*/
			dev_info(&pdev->dev, "BRAR[%u] bus address range = "
			  "[%lx, %lx]\n", i,
			  (unsigned long)rar->rar_addr[i].low,
			  (unsigned long)rar->rar_addr[i].high);
		}
	}
	return result;
}

/**
 *	rar_get_address		-	get the bus address in a RAR
 *	@start: return value of start address of block
 *	@end: return value of end address of block
 *
 *	The rar_get_address function is used by other device drivers
 *	to obtain RAR address information on a RAR. It takes three
 *	parameters:
 *
 *	The function returns a 0 upon success or an error if there is no RAR
 *	facility on this system.
 */
int rar_get_address(int rar_index, dma_addr_t *start, dma_addr_t *end)
{
	int idx;
	struct rar_device *rar = rar_to_device(rar_index, &idx);

	if (rar == NULL) {
		WARN_ON(1);
		return -ENODEV;
	}

	*start = rar->rar_addr[idx].low;
	*end = rar->rar_addr[idx].high;
	return 0;
}
EXPORT_SYMBOL(rar_get_address);

/**
 *	rar_lock	-	lock a RAR register
 *	@rar_index: RAR to lock (0-2)
 *
 *	The rar_lock function is ued by other device drivers to lock an RAR.
 *	once a RAR is locked, it stays locked until the next system reboot.
 *
 *	The function returns a 0 upon success or an error if there is no RAR
 *	facility on this system, or the locking fails
 */
int rar_lock(int rar_index)
{
	struct rar_device *rar;
	int result;
	int idx;
	dma_addr_t low, high;

	rar = rar_to_device(rar_index, &idx);

	if (rar == NULL) {
		WARN_ON(1);
		return -EINVAL;
	}

	low = rar->rar_addr[idx].low & 0xfffffc00u;
	high = rar->rar_addr[idx].high & 0xfffffc00u;

	/*
	* Only allow I/O from the graphics and Langwell;
	* not from the x86 processor
	*/

	if (rar_index == RAR_TYPE_VIDEO) {
		low |= 0x00000009;
		high |= 0x00000015;
	} else if (rar_index == RAR_TYPE_AUDIO) {
		/* Only allow I/O from Langwell; nothing from x86 */
		low |= 0x00000008;
		high |= 0x00000018;
	} else
		/* Read-only from all agents */
		high |= 0x00000018;

	/*
	* Now program the register using the Lincroft message
	* bus interface.
	*/
	result = rar_set_addr(rar->rar_dev,
				2 * idx, low);

	if (result == 0)
		result = rar_set_addr(rar->rar_dev,
				2 * idx + 1, high);

	return result;
}
EXPORT_SYMBOL(rar_lock);

/**
 *	register_rar		-	register a RAR handler
 *	@num: RAR we wish to register for
 *	@callback: function to call when RAR support is available
 *	@data: data to pass to this function
 *
 *	The register_rar function is to used by other device drivers
 *	to ensure that this driver is ready. As we cannot be sure of
 *	the compile/execute order of drivers in the kernel, it is
 *	best to give this driver a callback function to call when
 *	it is ready to give out addresses. The callback function
 *	would have those steps that continue the initialization of
 *	a driver that do require a valid RAR address. One of those
 *	steps would be to call rar_get_address()
 *
 *	This function return 0 on success or an error code on failure.
 */
int register_rar(int num, int (*callback)(unsigned long data),
							unsigned long data)
{
	/* For now we hardcode a single RAR device */
	struct rar_device *rar;
	struct client *c;
	int idx;
	int retval = 0;

	mutex_lock(&rar_mutex);

	/* Do we have a client mapping for this RAR number ? */
	c = rar_to_client(num);
	if (c == NULL) {
		retval = -ERANGE;
		goto done;
	}
	/* Is it claimed ? */
	if (c->busy) {
		retval = -EBUSY;
		goto done;
	}
	c->busy = 1;

	/* See if we have a handler for this RAR yet, if we do then fire it */
	rar = rar_to_device(num, &idx);

	if (rar) {
		/*
		* if the driver already registered, then we can simply
		* call the callback right now
		*/
		(*callback)(data);
		goto done;
	}

	/* Arrange to be called back when the hardware is found */
	c->callback = callback;
	c->driver_priv = data;
done:
	mutex_unlock(&rar_mutex);
	return retval;
}
EXPORT_SYMBOL(register_rar);

/**
 *	unregister_rar	-	release a RAR allocation
 *	@num: RAR number
 *
 *	Releases a RAR allocation, or pending allocation. If a callback is
 *	pending then this function will either complete before the unregister
 *	returns or not at all.
 */

void unregister_rar(int num)
{
	struct client *c;

	mutex_lock(&rar_mutex);
	c = rar_to_client(num);
	if (c == NULL || !c->busy)
		WARN_ON(1);
	else
		c->busy = 0;
	mutex_unlock(&rar_mutex);
}
EXPORT_SYMBOL(unregister_rar);

/**
 *	rar_callback		-	Process callbacks
 *	@rar: new RAR device
 *
 *	Process the callbacks for a newly found RAR device.
 */

static void rar_callback(struct rar_device *rar)
{
	struct client *c = &rar->client[0];
	int i;

	mutex_lock(&rar_mutex);

	rar->registered = 1;	/* Ensure no more callbacks queue */

	for (i = 0; i < MRST_NUM_RAR; i++) {
		if (c->callback && c->busy) {
			c->callback(c->driver_priv);
			c->callback = NULL;
		}
		c++;
	}
	mutex_unlock(&rar_mutex);
}

/**
 *	rar_probe		-	PCI probe callback
 *	@dev: PCI device
 *	@id: matching entry in the match table
 *
 *	A RAR device has been discovered. Initialise it and if successful
 *	process any pending callbacks that can now be completed.
 */
static int rar_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int error;
	struct rar_device *rar;

	dev_dbg(&dev->dev, "PCI probe starting\n");

	rar = alloc_rar_device();
	if (rar == NULL)
		return -EBUSY;

	/* Enable the device */
	error = pci_enable_device(dev);
	if (error) {
		dev_err(&dev->dev,
			"Error enabling RAR register PCI device\n");
		goto end_function;
	}

	/* Fill in the rar_device structure */
	rar->rar_dev = pci_dev_get(dev);
	pci_set_drvdata(dev, rar);

	/*
	 * Initialize the RAR parameters, which have to be retrieved
	 * via the message bus interface.
	 */
	error = init_rar_params(rar);
	if (error) {
		pci_disable_device(dev);
		dev_err(&dev->dev, "Error retrieving RAR addresses\n");
		goto end_function;
	}
	/* now call anyone who has registered (using callbacks) */
	rar_callback(rar);
	return 0;
end_function:
	free_rar_device(rar);
	return error;
}

static DEFINE_PCI_DEVICE_TABLE(rar_pci_id_tbl) = {
	{ PCI_VDEVICE(INTEL, 0x4110) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, rar_pci_id_tbl);

/* field for registering driver to PCI device */
static struct pci_driver rar_pci_driver = {
	.name = "rar_register_driver",
	.id_table = rar_pci_id_tbl,
	.probe = rar_probe,
	/* Cannot be unplugged - no remove */
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

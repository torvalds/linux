#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/semaphore.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/ioctl.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include "rar_driver.h"

/* The following defines are for the IPC process to retrieve RAR in */

/* === Lincroft Message Bus Interface === */
/* Message Control Register */
#define LNC_MCR_OFFSET 0xD0

/* Message Data Register */
#define LNC_MDR_OFFSET 0xD4

/* Message Opcodes */
#define LNC_MESSAGE_READ_OPCODE  0xD0
#define LNC_MESSAGE_WRITE_OPCODE 0xE0

/* Message Write Byte Enables */
#define LNC_MESSAGE_BYTE_WRITE_ENABLES 0xF

/* B-unit Port */
#define LNC_BUNIT_PORT 0x3

/* === Lincroft B-Unit Registers - Programmed by IA32 firmware === */
#define LNC_BRAR0L  0x10
#define LNC_BRAR0H  0x11
#define LNC_BRAR1L  0x12
#define LNC_BRAR1H  0x13

/* Reserved for SeP */
#define LNC_BRAR2L  0x14
#define LNC_BRAR2H  0x15


/* This structure is only used during module initialization. */
struct RAR_offsets {
	int low; /* Register offset for low RAR physical address. */
	int high; /* Register offset for high RAR physical address. */
};

struct pci_dev *rar_dev;
static uint32_t registered;

/* Moorestown supports three restricted access regions. */
#define MRST_NUM_RAR 3

struct RAR_address_struct rar_addr[MRST_NUM_RAR];

/* prototype for init */
static int __init rar_init_handler(void);
static void __exit rar_exit_handler(void);

/*
  function that is activated on the succesfull probe of the RAR device
*/
static int __devinit rar_probe(struct pci_dev *pdev, const struct pci_device_id *ent);

static struct pci_device_id rar_pci_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x4110) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, rar_pci_id_tbl);

/* field for registering driver to PCI device */
static struct pci_driver rar_pci_driver = {
	.name = "rar_driver",
	.id_table = rar_pci_id_tbl,
	.probe = rar_probe
};

/* This function is used to retrieved RAR info using the IPC message
   bus interface */
static int memrar_get_rar_addr(struct pci_dev* pdev,
	                      int offset,
	                      u32 *addr)
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

	int result = 0; /* result */
	/* Construct control message */
	u32 const message =
	       (LNC_MESSAGE_READ_OPCODE << 24)
	       | (LNC_BUNIT_PORT << 16)
	       | (offset << 8)
	       | (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

	printk(KERN_WARNING "rar- offset to LNC MSG is %x\n",offset);

	if (addr == 0)
		return -EINVAL;

	/* Send the control message */
	result = pci_write_config_dword(pdev,
	                          LNC_MCR_OFFSET,
	                          message);

	printk(KERN_WARNING "rar- result from send ctl register is %x\n"
	  ,result);

	if (!result)
		result = pci_read_config_dword(pdev,
		                              LNC_MDR_OFFSET,
				              addr);

	printk(KERN_WARNING "rar- result from read data register is %x\n",
	  result);

	printk(KERN_WARNING "rar- value read from data register is %x\n",
	  *addr);

	if (result)
		return -1;
	else
		return 0;
}

static int memrar_set_rar_addr(struct pci_dev* pdev,
	                      int offset,
	                      u32 addr)
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

	int result = 0; /* result */

	/* Construct control message */
	u32 const message =
	       (LNC_MESSAGE_WRITE_OPCODE << 24)
	       | (LNC_BUNIT_PORT << 16)
	       | (offset << 8)
	       | (LNC_MESSAGE_BYTE_WRITE_ENABLES << 4);

	printk(KERN_WARNING "rar- offset to LNC MSG is %x\n",offset);

	if (addr == 0)
		return -EINVAL;

	/* Send the control message */
	result = pci_write_config_dword(pdev,
	                          LNC_MDR_OFFSET,
	                          addr);

	printk(KERN_WARNING "rar- result from send ctl register is %x\n"
	  ,result);

	if (!result)
		result = pci_write_config_dword(pdev,
		                              LNC_MCR_OFFSET,
				              message);

	printk(KERN_WARNING "rar- result from write data register is %x\n",
	  result);

	printk(KERN_WARNING "rar- value read to data register is %x\n",
	  addr);

	if (result)
		return -1;
	else
		return 0;
}

/*

 * Initialize RAR parameters, such as physical addresses, etc.

 */
static int memrar_init_rar_params(struct pci_dev *pdev)
{
	struct RAR_offsets const offsets[] = {
	       { LNC_BRAR0L, LNC_BRAR0H },
	       { LNC_BRAR1L, LNC_BRAR1H },
	       { LNC_BRAR2L, LNC_BRAR2H }
	};

	size_t const num_offsets = sizeof(offsets) / sizeof(offsets[0]);
	struct RAR_offsets const *end = offsets + num_offsets;
	struct RAR_offsets const *i;
	unsigned int n = 0;
	int result = 0;

	/* Retrieve RAR start and end physical addresses. */

	/*
	 * Access the RAR registers through the Lincroft Message Bus
	 * Interface on PCI device: 00:00.0 Host bridge.
	 */

	/* struct pci_dev *pdev = pci_get_bus_and_slot(0, PCI_DEVFN(0,0)); */

	if (pdev == NULL)
	       return -ENODEV;

	for (i = offsets; i != end; ++i, ++n) {
	       if (memrar_get_rar_addr (pdev,
		                       (*i).low,
		                       &(rar_addr[n].low)) != 0
		   || memrar_get_rar_addr (pdev,
		                          (*i).high,
		                          &(rar_addr[n].high)) != 0) {
		       result = -1;
		       break;
	       }
	}

	/* Done accessing the device. */
	/* pci_dev_put(pdev); */

	if (result == 0) {
	if(1) {
	       size_t z;
	       for (z = 0; z != MRST_NUM_RAR; ++z) {
			printk(KERN_WARNING "rar - BRAR[%Zd] physical address low\n"
			     "\tlow:  0x%08x\n"
			     "\thigh: 0x%08x\n",
			     z,
			     rar_addr[z].low,
			     rar_addr[z].high);
			}
	       }
	}

	return result;
}

/*
  function that is activaed on the succesfull probe of the RAR device
*/
static int __devinit rar_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	/* error */
	int error;

	/*------------------------
	CODE
	---------------------------*/

	DEBUG_PRINT_0(RAR_DEBUG_LEVEL_EXTENDED,
	  "Rar pci probe starting\n");
	error = 0;

	/* enable the device */
	error = pci_enable_device(pdev);
	if (error) {
		DEBUG_PRINT_0(RAR_DEBUG_LEVEL_EXTENDED,
		  "error enabling pci device\n");
		goto end_function;
	}

	rar_dev = pdev;
	registered = 1;

	/* Initialize the RAR parameters, which have to be retrieved */
	/* via the message bus service */
	error=memrar_init_rar_params(rar_dev);

	if (error) {
		DEBUG_PRINT_0(RAR_DEBUG_LEVEL_EXTENDED,
		  "error getting RAR addresses device\n");
		registered = 0;
		goto end_function;
		}

end_function:

	return error;
}

/*
  this function registers th driver to
  the device subsystem( either PCI, USB, etc)
*/
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


/* The get_rar_address function is used by other device drivers
 * to obtain RAR address information on a RAR. It takes two
 * parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar for which you wish to retrieve
 * the address information.
 * Values can be 0,1, or 2.
 *
 * struct RAR_address_struct is a pointer to a place to which the function
 * can return the address structure for the RAR.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int get_rar_address(int rar_index,struct RAR_address_struct *addresses)
{
	if (registered && (rar_index < 3) && (rar_index >= 0)) {
		*addresses=rar_addr[rar_index];
		/* strip off lock bit information  */
		addresses->low = addresses->low & 0xfffffff0;
		addresses->high = addresses->high & 0xfffffff0;
		return 0;
		}

	else {
		return -ENODEV;
		}
}


EXPORT_SYMBOL(get_rar_address);

/* The lock_rar function is ued by other device drivers to lock an RAR.
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
int lock_rar(int rar_index)
{
	u32 working_addr;
	int result;
if (registered && (rar_index < 3) && (rar_index >= 0)) {
	/* first make sure that lock bits are clear (this does lock) */
	working_addr=rar_addr[rar_index].low & 0xfffffff0;

	/* now send that value to the register using the IPC */
        result=memrar_set_rar_addr(rar_dev,rar_index,working_addr);
	return result;
	}

else {
	return -ENODEV;
	}
}

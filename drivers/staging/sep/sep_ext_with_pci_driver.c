/*
 *
 *  sep_ext_with_pci_driver.c - Security Processor Driver
 *  pci initialization functions
 *
 *  Copyright(c) 2009 Intel Corporation. All rights reserved.
 *  Copyright(c) 2009 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *
 *  CHANGES:
 *
 *  2009.06.26	Initial publish
 *
 */

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
#include "sep_driver_hw_defs.h"
#include "sep_driver_config.h"
#include "sep_driver_api.h"
#include "sep_driver_ext_api.h"

#if SEP_DRIVER_ARM_DEBUG_MODE

#define  CRYS_SEP_ROM_length                  0x4000

#define  CRYS_SEP_ROM_start_address           0x8000C000UL

#define  CRYS_SEP_ROM_start_address_offset    0xC000UL

#define  SEP_ROM_BANK_register                0x80008420UL

#define  SEP_ROM_BANK_register_offset         0x8420UL

#define SEP_RAR_IO_MEM_REGION_START_ADDRESS   0x82000000

/* 2M size */
/* #define SEP_RAR_IO_MEM_REGION_SIZE            (1024*1024*2)

static unsigned long CRYS_SEP_ROM[] = {
	#include "SEP_ROM_image.h"
};

#else
*/

/*-------------
 THOSE 2 definitions are specific to the board - must be
 defined during integration
---------------*/
#define SEP_RAR_IO_MEM_REGION_START_ADDRESS   0xFF0D0000

/* 2M size */

#endif /* SEP_DRIVER_ARM_DEBUG_MODE */

#define BASE_ADDRESS_FOR_SYSTEM 0xfffc0000
#define SEP_RAR_IO_MEM_REGION_SIZE 0x40000

irqreturn_t sep_inthandler(int irq , void* dev_id);

/* NOTE - must be defined specific to the board */
#define VENDOR_ID                             0x8086

/* io memory (register area) */
static unsigned long io_memory_start_physical_address;
static unsigned long io_memory_end_physical_address;
static unsigned long io_memory_size;
void *io_memory_start_virtual_address;

/* restricted access region */
static unsigned long rar_physical_address;
static void *rar_virtual_address;

/* shared memory region */
static unsigned long shared_physical_address;
static void *shared_virtual_address;

/* firmware regions */
static unsigned long cache_physical_address;
static unsigned long cache_size;
static void *cache_virtual_address;

static unsigned long resident_physical_address;
static unsigned long resident_size;
static void *resident_virtual_address;

/* device interrupt (as retrieved from PCI) */
int sep_irq;

/* temporary */
unsigned long jiffies_future;

/*-----------------------------
    private functions
--------------------------------*/

/*
  function that is activated on the succesfull probe of the SEP device
*/
static int __devinit sep_probe(struct pci_dev *pdev,
  const struct pci_device_id *ent);

static struct pci_device_id sep_pci_id_tbl[] = {
	{ PCI_DEVICE(VENDOR_ID, 0x080c) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, sep_pci_id_tbl);

static unsigned long    rar_region_addr;


/* field for registering driver to PCI device */
static struct pci_driver sep_pci_driver = {
	.name = "sep_sec_driver",
	.id_table = sep_pci_id_tbl,
	.probe = sep_probe
};

/* pointer to pci dev received during probe */
struct pci_dev *sep_pci_dev_ptr;

/*
  This functions locks the area of the resisnd and cache sep code
*/
void sep_lock_cache_resident_area(void)
{
	return;
}


/*
  This functions copies the cache and resident from their source location into
  destination memory, which is external to Linux VM and is given as
   physical address
*/
int sep_copy_cache_resident_to_area(unsigned long   src_cache_addr,
				unsigned long   cache_size_in_bytes,
				unsigned long   src_resident_addr,
				unsigned long   resident_size_in_bytes,
				unsigned long *dst_new_cache_addr_ptr,
				unsigned long *dst_new_resident_addr_ptr)
{
	/* resident address in user space */
	unsigned long resident_addr;

	/* cahce address in user space */
	unsigned long cache_addr;

	const struct firmware *fw;

	char *cache_name = "cache.image.bin";
	char *res_name =  "resident.image.bin";

	/* error */
	int error;

	/*--------------------------------
	    CODE
	-------------------------------------*/
	error = 0;

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:rar_virtual is %p\n",
	  rar_virtual_address);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:rar_physical is %08lx\n",
	  rar_physical_address);

	rar_region_addr = (unsigned long)rar_virtual_address;

	cache_physical_address = rar_physical_address;
	cache_virtual_address = rar_virtual_address;

	/* load cache */
	error = request_firmware(&fw, cache_name, &sep_pci_dev_ptr->dev);
	if (error) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver:cant request cache fw\n");
		goto end_function;
	}

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:cache data loc is %p\n",
	  (void *)fw->data);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:cache data size is %08Zx\n",
	  fw->size);

	memcpy((void *)cache_virtual_address, (void *)fw->data, fw->size);

	cache_size = fw->size;

	cache_addr = (unsigned long)cache_virtual_address;

	release_firmware(fw);

	resident_physical_address = cache_physical_address+cache_size;
	resident_virtual_address = cache_virtual_address+cache_size;

	/* load resident */
	error = request_firmware(&fw, res_name, &sep_pci_dev_ptr->dev);
	if (error) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver:cant request res fw\n");
		goto end_function;
	}

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:res data loc is %p\n",
	  (void *)fw->data);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:res data size is %08Zx\n",
	  fw->size);

	memcpy((void *)resident_virtual_address, (void *)fw->data, fw->size);

	resident_size = fw->size;

	release_firmware(fw);

	resident_addr = (unsigned long)resident_virtual_address;

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:resident_addr (physical )is %08lx\n",
	  resident_physical_address);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:cache_addr (physical) is %08lx\n",
	  cache_physical_address);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:resident_addr (logical )is %08lx\n",
	  resident_addr);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:cache_addr (logical) is %08lx\n",
	  cache_addr);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:resident_size is %08lx\n", resident_size);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:cache_size is %08lx\n", cache_size);



	/* physical addresses */
	*dst_new_cache_addr_ptr = cache_physical_address;
	*dst_new_resident_addr_ptr = resident_physical_address;

end_function:

	return error;
}

/*
  This functions maps and allocates the
  shared area on the  external RAM (device)
  The input is shared_area_size - the size of the memory to
  allocate. The outputs
  are kernel_shared_area_addr_ptr - the kerenl
  address of the mapped and allocated
  shared area, and phys_shared_area_addr_ptr
  - the physical address of the shared area
*/
int sep_map_and_alloc_shared_area(unsigned long shared_area_size,
				unsigned long *kernel_shared_area_addr_ptr,
				unsigned long *phys_shared_area_addr_ptr)
{
	// shared_virtual_address = ioremap_nocache(0xda00000,shared_area_size);
	shared_virtual_address = kmalloc(shared_area_size, GFP_KERNEL);
	if (!shared_virtual_address) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "sep_driver:shared memory kmalloc failed\n");
		return -1;
	}

	shared_physical_address = __pa(shared_virtual_address);
	// shared_physical_address = 0xda00000;

	*kernel_shared_area_addr_ptr = (unsigned long)shared_virtual_address;
	/* set the physical address of the shared area */
	*phys_shared_area_addr_ptr = shared_physical_address;

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:shared_virtual_address is %p\n",
	shared_virtual_address);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:shared_region_size is %08lx\n",
	shared_area_size);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:shared_physical_addr is %08lx\n",
	*phys_shared_area_addr_ptr);

	return 0;
}

/*
  This functions unmaps and deallocates the shared area
  on the  external RAM (device)
  The input is shared_area_size - the size of the memory to deallocate,kernel_
  shared_area_addr_ptr - the kernel address of the mapped and allocated
  shared area,phys_shared_area_addr_ptr - the physical address of
  the shared area
*/
void sep_unmap_and_free_shared_area(unsigned long   shared_area_size,
					unsigned long   kernel_shared_area_addr,
					unsigned long   phys_shared_area_addr)
{
	kfree((void *)kernel_shared_area_addr);
	return;
}

/*
  This functions returns the physical address inside shared area according
  to the virtual address. It can be either on the externa RAM device
  (ioremapped), or on the system RAM
  This implementation is for the external RAM
*/
unsigned long sep_shared_area_virt_to_phys(unsigned long virt_address)
{
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:sh virt to phys v %08lx\n",
	  virt_address);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:sh virt to phys p %08lx\n",
	  shared_physical_address
	  + (virt_address - (unsigned long)shared_virtual_address));

	return (unsigned long)shared_physical_address +
	  (virt_address - (unsigned long)shared_virtual_address);
}

/*
  This functions returns the virtual address inside shared area
  according to the physical address. It can be either on the
  externa RAM device (ioremapped), or on the system RAM This implementation
  is for the external RAM
*/
unsigned long sep_shared_area_phys_to_virt(unsigned long phys_address)
{
	return (unsigned long)shared_virtual_address
	  + (phys_address - shared_physical_address);
}


/*
  function that is activaed on the succesfull probe of the SEP device
*/
static int __devinit sep_probe(struct pci_dev *pdev,
			const struct pci_device_id *ent)
{
	/* error */
	int error;

	/*------------------------
	CODE
	---------------------------*/

	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "Sep pci probe starting\n");
	error = 0;

	/* enable the device */
	error = pci_enable_device(pdev);
	if (error) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "error enabling pci device\n");
		goto end_function;
	}

	/* set the pci dev pointer */
	sep_pci_dev_ptr = pdev;

	/* get the io memory start address */
	io_memory_start_physical_address = pci_resource_start(pdev, 0);
	if (!io_memory_start_physical_address) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver error pci resource start\n");
		goto end_function;
	}

	/* get the io memory end address */
	io_memory_end_physical_address = pci_resource_end(pdev, 0);
	if (!io_memory_end_physical_address) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver error pci resource end\n");
		goto end_function;
	}

	io_memory_size = io_memory_end_physical_address -
	  io_memory_start_physical_address + 1;

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:io_memory_start_physical_address is %08lx\n",
	io_memory_start_physical_address);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:io_memory_end_phyaical_address is %08lx\n",
	io_memory_end_physical_address);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:io_memory_size is %08lx\n",
	io_memory_size);

	io_memory_start_virtual_address =
	  ioremap_nocache(io_memory_start_physical_address,
	  io_memory_size);
	if (!io_memory_start_virtual_address) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver error ioremap of io memory\n");
		goto end_function;
	}

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:io_memory_start_virtual_address is %p\n",
	io_memory_start_virtual_address);

	g_sep_reg_base_address = (unsigned long)io_memory_start_virtual_address;


	/* set up system base address and shared memory location */

	rar_virtual_address = kmalloc(2 * SEP_RAR_IO_MEM_REGION_SIZE,
	  GFP_KERNEL);

	if (!rar_virtual_address) {
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver:cant kmalloc rar\n");
		goto end_function;
		}

	rar_physical_address = __pa(rar_virtual_address);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:rar_physical is %08lx\n",
	rar_physical_address);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver:rar_virtual is %p\n",
	rar_virtual_address);


#if !SEP_DRIVER_POLLING_MODE

	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: about to write IMR and ICR REG_ADDR\n");

	/* clear ICR register */
	SEP_WRITE_REGISTER(g_sep_reg_base_address + HW_HOST_ICR_REG_ADDR,
	  0xFFFFFFFF);

	/* set the IMR register - open only GPR 2 */
	SEP_WRITE_REGISTER(g_sep_reg_base_address + HW_HOST_IMR_REG_ADDR,
	  (~(0x1 << 13)));

	/* figure out our irq */
	error = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, (u8 *)&sep_irq);

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: my irq is %d\n", sep_irq);

	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: about to call request_irq\n");
	/* get the interrupt line */
	error = request_irq(sep_irq, sep_inthandler, IRQF_SHARED,
	  "sep_driver", &g_sep_reg_base_address);
	if (error)
		goto end_function;

	goto end_function;
	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: about to write IMR REG_ADDR");

	/* set the IMR register - open only GPR 2 */
	SEP_WRITE_REGISTER(g_sep_reg_base_address + HW_HOST_IMR_REG_ADDR,
	  (~(0x1 << 13)));

#endif /* SEP_DRIVER_POLLING_MODE */

end_function:

	return error;
}

/*
  this function registers th driver to
  the device subsystem( either PCI, USB, etc)
*/
int sep_register_driver_to_device(void)
{
	return pci_register_driver(&sep_pci_driver);
}



void sep_load_rom_code()
{
#if SEP_DRIVER_ARM_DEBUG_MODE
	/* Index variables */
	unsigned long i, k, j;
	unsigned long regVal;
	unsigned long Error;
	unsigned long warning;

	/* Loading ROM from SEP_ROM_image.h file */
	k = sizeof(CRYS_SEP_ROM);

	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: DX_CC_TST_SepRomLoader start\n");

	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: k is %lu\n", k);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: g_sep_reg_base_address is %p\n",
	  g_sep_reg_base_address);
	DEBUG_PRINT_1(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: CRYS_SEP_ROM_start_address_offset is %p\n",
	  CRYS_SEP_ROM_start_address_offset);

	for (i = 0; i < 4; i++) {
		/* write bank */
		SEP_WRITE_REGISTER(g_sep_reg_base_address
		  + SEP_ROM_BANK_register_offset, i);

		for (j = 0; j < CRYS_SEP_ROM_length / 4; j++) {
			SEP_WRITE_REGISTER(g_sep_reg_base_address +
			  CRYS_SEP_ROM_start_address_offset + 4*j,
			  CRYS_SEP_ROM[i * 0x1000 + j]);

			k = k - 4;

			if (k == 0) {
				j = CRYS_SEP_ROM_length;
				i = 4;
			}
		}
	}

	/* reset the SEP*/
	SEP_WRITE_REGISTER(g_sep_reg_base_address
	  + HW_HOST_SEP_SW_RST_REG_ADDR, 0x1);

	/* poll for SEP ROM boot finish */
	do {
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR3_REG_ADDR, regVal);
	} while (!regVal);

	DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
	  "SEP Driver: ROM polling ended\n");

	switch (regVal) {
	case 0x1:
		/* fatal error - read erro status from GPRO */
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR0_REG_ADDR, Error);
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 1\n");
		break;
	case 0x2:
		/* Boot First Phase ended  */
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR0_REG_ADDR, warning);
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 2\n");
		break;
	case 0x4:
		/* Cold boot ended successfully  */
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR0_REG_ADDR, warning);
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 4\n");
		Error = 0;
		break;
	case 0x8:
		/* Warmboot ended successfully */
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR0_REG_ADDR, warning);
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 8\n");
		Error = 0;
		break;
	case 0x10:
		/* ColdWarm boot ended successfully */
		SEP_READ_REGISTER(g_sep_reg_base_address
		  + HW_HOST_SEP_HOST_GPR0_REG_ADDR, warning);
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 16\n");
		Error = 0;
		break;
	case 0x20:
		DEBUG_PRINT_0(SEP_DEBUG_LEVEL_EXTENDED,
		  "SEP Driver: ROM polling case 32\n");
		break;
	}

#endif
}


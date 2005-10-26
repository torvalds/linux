/*
 *
 * Procedures for firmware flash updates on pSeries systems.
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/semaphore.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/system.h>
#include <asm/abs_addr.h>
#include <asm/udbg.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/systemcfg.h>

#include "rtas-fw.h"

struct flash_block_list_header rtas_firmware_flash_list = {0, NULL};

#define FLASH_BLOCK_LIST_VERSION (1UL)

static void rtas_flash_firmware(void)
{
	unsigned long image_size;
	struct flash_block_list *f, *next, *flist;
	unsigned long rtas_block_list;
	int i, status, update_token;

	update_token = rtas_token("ibm,update-flash-64-and-reboot");
	if (update_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_ALERT "FLASH: ibm,update-flash-64-and-reboot is not available -- not a service partition?\n");
		printk(KERN_ALERT "FLASH: firmware will not be flashed\n");
		return;
	}

	/* NOTE: the "first" block list is a global var with no data
	 * blocks in the kernel data segment.  We do this because
	 * we want to ensure this block_list addr is under 4GB.
	 */
	rtas_firmware_flash_list.num_blocks = 0;
	flist = (struct flash_block_list *)&rtas_firmware_flash_list;
	rtas_block_list = virt_to_abs(flist);
	if (rtas_block_list >= 4UL*1024*1024*1024) {
		printk(KERN_ALERT "FLASH: kernel bug...flash list header addr above 4GB\n");
		return;
	}

	printk(KERN_ALERT "FLASH: preparing saved firmware image for flash\n");
	/* Update the block_list in place. */
	image_size = 0;
	for (f = flist; f; f = next) {
		/* Translate data addrs to absolute */
		for (i = 0; i < f->num_blocks; i++) {
			f->blocks[i].data = (char *)virt_to_abs(f->blocks[i].data);
			image_size += f->blocks[i].length;
		}
		next = f->next;
		/* Don't translate NULL pointer for last entry */
		if (f->next)
			f->next = (struct flash_block_list *)virt_to_abs(f->next);
		else
			f->next = NULL;
		/* make num_blocks into the version/length field */
		f->num_blocks = (FLASH_BLOCK_LIST_VERSION << 56) | ((f->num_blocks+1)*16);
	}

	printk(KERN_ALERT "FLASH: flash image is %ld bytes\n", image_size);
	printk(KERN_ALERT "FLASH: performing flash and reboot\n");
	rtas_progress("Flashing        \n", 0x0);
	rtas_progress("Please Wait...  ", 0x0);
	printk(KERN_ALERT "FLASH: this will take several minutes.  Do not power off!\n");
	status = rtas_call(update_token, 1, 1, NULL, rtas_block_list);
	switch (status) {	/* should only get "bad" status */
	    case 0:
		printk(KERN_ALERT "FLASH: success\n");
		break;
	    case -1:
		printk(KERN_ALERT "FLASH: hardware error.  Firmware may not be not flashed\n");
		break;
	    case -3:
		printk(KERN_ALERT "FLASH: image is corrupt or not correct for this platform.  Firmware not flashed\n");
		break;
	    case -4:
		printk(KERN_ALERT "FLASH: flash failed when partially complete.  System may not reboot\n");
		break;
	    default:
		printk(KERN_ALERT "FLASH: unknown flash return code %d\n", status);
		break;
	}
}

void rtas_flash_bypass_warning(void)
{
	printk(KERN_ALERT "FLASH: firmware flash requires a reboot\n");
	printk(KERN_ALERT "FLASH: the firmware image will NOT be flashed\n");
}


void rtas_fw_restart(char *cmd)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_firmware();
	rtas_restart(cmd);
}

void rtas_fw_power_off(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
	rtas_power_off();
}

void rtas_fw_halt(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
	rtas_halt();
}

EXPORT_SYMBOL(rtas_firmware_flash_list);

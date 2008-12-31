/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "global.h"

/* Get frame buffer size from VGA BIOS */

unsigned int viafb_get_memsize(void)
{
	unsigned int m;

	/* If memory size provided by user */
	if (viafb_memsize)
		m = viafb_memsize * Mb;
	else {
		m = (unsigned int)viafb_read_reg(VIASR, SR39);
		m = m * (4 * Mb);

		if ((m < (16 * Mb)) || (m > (64 * Mb)))
			m = 16 * Mb;
	}
	DEBUG_MSG(KERN_INFO "framebuffer size = %d Mb\n", m / Mb);
	return m;
}

/* Get Video Buffer Starting Physical Address(back door)*/

unsigned long viafb_get_videobuf_addr(void)
{
	struct pci_dev *pdev = NULL;
	unsigned char sys_mem;
	unsigned char video_mem;
	unsigned long sys_mem_size;
	unsigned long video_mem_size;
	/*system memory = 256 MB, video memory 64 MB */
	unsigned long vmem_starting_adr = 0x0C000000;

	pdev =
	    (struct pci_dev *)pci_get_device(VIA_K800_BRIDGE_VID,
					     VIA_K800_BRIDGE_DID, NULL);
	if (pdev != NULL) {
		pci_read_config_byte(pdev, VIA_K800_SYSTEM_MEMORY_REG,
				     &sys_mem);
		pci_read_config_byte(pdev, VIA_K800_VIDEO_MEMORY_REG,
				     &video_mem);
		video_mem = (video_mem & 0x70) >> 4;
		sys_mem_size = ((unsigned long)sys_mem) << 24;
		if (video_mem != 0)
			video_mem_size = (1 << (video_mem)) * 1024 * 1024;
		else
			video_mem_size = 0;

		vmem_starting_adr = sys_mem_size - video_mem_size;
		pci_dev_put(pdev);
	}

	DEBUG_MSG(KERN_INFO "Video Memory Starting Address = %lx \n",
		  vmem_starting_adr);
	return vmem_starting_adr;
}

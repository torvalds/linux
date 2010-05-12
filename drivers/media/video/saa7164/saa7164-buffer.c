/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2009 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>

#include "saa7164.h"

/* The PCI address space for buffer handling looks like this:

 +-u32 wide-------------+
 |                      +
 +-u64 wide------------------------------------+
 +                                             +
 +----------------------+
 | CurrentBufferPtr     + Pointer to current PCI buffer >-+
 +----------------------+                                 |
 | Unused               +                                 |
 +----------------------+                                 |
 | Pitch                + = 188 (bytes)                   |
 +----------------------+                                 |
 | PCI buffer size      + = pitch * number of lines (312) |
 +----------------------+                                 |
 |0| Buf0 Write Offset  +                                 |
 +----------------------+                                 v
 |1| Buf1 Write Offset  +                                 |
 +----------------------+                                 |
 |2| Buf2 Write Offset  +                                 |
 +----------------------+                                 |
 |3| Buf3 Write Offset  +                                 |
 +----------------------+                                 |
 ... More write offsets                                   |
 +---------------------------------------------+          |
 +0| set of ptrs to PCI pagetables             +          |
 +---------------------------------------------+          |
 +1| set of ptrs to PCI pagetables             + <--------+
 +---------------------------------------------+
 +2| set of ptrs to PCI pagetables             +
 +---------------------------------------------+
 +3| set of ptrs to PCI pagetables             + >--+
 +---------------------------------------------+    |
 ... More buffer pointers                           |  +----------------+
						    +->| pt[0] TS data  |
						    |  +----------------+
						    |
						    |  +----------------+
						    +->| pt[1] TS data  |
						    |  +----------------+
						    | etc
 */

/* Allocate a new buffer structure and associated PCI space in bytes.
 * len must be a multiple of sizeof(u64)
 */
struct saa7164_buffer *saa7164_buffer_alloc(struct saa7164_tsport *port,
	u32 len)
{
	struct saa7164_buffer *buf = 0;
	struct saa7164_dev *dev = port->dev;
	int i;

	if ((len == 0) || (len >= 65536) || (len % sizeof(u64))) {
		log_warn("%s() SAA_ERR_BAD_PARAMETER\n", __func__);
		goto ret;
	}

	buf = kzalloc(sizeof(struct saa7164_buffer), GFP_KERNEL);
	if (buf == NULL) {
		log_warn("%s() SAA_ERR_NO_RESOURCES\n", __func__);
		goto ret;
	}

	buf->port = port;
	buf->flags = SAA7164_BUFFER_FREE;
	/* TODO: arg len is being ignored */
	buf->pci_size = SAA7164_PT_ENTRIES * 0x1000;
	buf->pt_size = (SAA7164_PT_ENTRIES * sizeof(u64)) + 0x1000;

	/* Allocate contiguous memory */
	buf->cpu = pci_alloc_consistent(port->dev->pci, buf->pci_size,
		&buf->dma);
	if (!buf->cpu)
		goto fail1;

	buf->pt_cpu = pci_alloc_consistent(port->dev->pci, buf->pt_size,
		&buf->pt_dma);
	if (!buf->pt_cpu)
		goto fail2;

	/* init the buffers to a known pattern, easier during debugging */
	memset(buf->cpu, 0xff, buf->pci_size);
	memset(buf->pt_cpu, 0xff, buf->pt_size);

	dprintk(DBGLVL_BUF, "%s()   allocated buffer @ 0x%p\n", __func__, buf);
	dprintk(DBGLVL_BUF, "  pci_cpu @ 0x%p    dma @ 0x%08lx len = 0x%x\n",
		buf->cpu, (long)buf->dma, buf->pci_size);
	dprintk(DBGLVL_BUF, "   pt_cpu @ 0x%p pt_dma @ 0x%08lx len = 0x%x\n",
		buf->pt_cpu, (long)buf->pt_dma, buf->pt_size);

	/* Format the Page Table Entries to point into the data buffer */
	for (i = 0 ; i < SAA7164_PT_ENTRIES; i++) {

		*(buf->pt_cpu + i) = buf->dma + (i * 0x1000); /* TODO */

	}

	goto ret;

fail2:
	pci_free_consistent(port->dev->pci, buf->pci_size, buf->cpu, buf->dma);
fail1:
	kfree(buf);

	buf = 0;
ret:
	return buf;
}

int saa7164_buffer_dealloc(struct saa7164_tsport *port,
	struct saa7164_buffer *buf)
{
	struct saa7164_dev *dev = port->dev;

	if ((buf == 0) || (port == 0))
		return SAA_ERR_BAD_PARAMETER;

	dprintk(DBGLVL_BUF, "%s() deallocating buffer @ 0x%p\n", __func__, buf);

	if (buf->flags != SAA7164_BUFFER_FREE)
		log_warn(" freeing a non-free buffer\n");

	pci_free_consistent(port->dev->pci, buf->pci_size, buf->cpu, buf->dma);
	pci_free_consistent(port->dev->pci, buf->pt_size, buf->pt_cpu,
		buf->pt_dma);

	kfree(buf);

	return SAA_OK;
}


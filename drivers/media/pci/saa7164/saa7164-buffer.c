// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 */

#include <linux/slab.h>

#include "saa7164.h"

/* The PCI address space for buffer handling looks like this:
 *
 * +-u32 wide-------------+
 * |                      +
 * +-u64 wide------------------------------------+
 * +                                             +
 * +----------------------+
 * | CurrentBufferPtr     + Pointer to current PCI buffer >-+
 * +----------------------+                                 |
 * | Unused               +                                 |
 * +----------------------+                                 |
 * | Pitch                + = 188 (bytes)                   |
 * +----------------------+                                 |
 * | PCI buffer size      + = pitch * number of lines (312) |
 * +----------------------+                                 |
 * |0| Buf0 Write Offset  +                                 |
 * +----------------------+                                 v
 * |1| Buf1 Write Offset  +                                 |
 * +----------------------+                                 |
 * |2| Buf2 Write Offset  +                                 |
 * +----------------------+                                 |
 * |3| Buf3 Write Offset  +                                 |
 * +----------------------+                                 |
 * ... More write offsets                                   |
 * +---------------------------------------------+          |
 * +0| set of ptrs to PCI pagetables             +          |
 * +---------------------------------------------+          |
 * +1| set of ptrs to PCI pagetables             + <--------+
 * +---------------------------------------------+
 * +2| set of ptrs to PCI pagetables             +
 * +---------------------------------------------+
 * +3| set of ptrs to PCI pagetables             + >--+
 * +---------------------------------------------+    |
 * ... More buffer pointers                           |  +----------------+
 *						    +->| pt[0] TS data  |
 *						    |  +----------------+
 *						    |
 *						    |  +----------------+
 *						    +->| pt[1] TS data  |
 *						    |  +----------------+
 *						    | etc
 */

/* Allocate a new buffer structure and associated PCI space in bytes.
 * len must be a multiple of sizeof(u64)
 */
struct saa7164_buffer *saa7164_buffer_alloc(struct saa7164_port *port,
	u32 len)
{
	struct tmHWStreamParameters *params = &port->hw_streamingparams;
	struct saa7164_buffer *buf = NULL;
	struct saa7164_dev *dev = port->dev;
	int i;

	if ((len == 0) || (len >= 65536) || (len % sizeof(u64))) {
		log_warn("%s() SAA_ERR_BAD_PARAMETER\n", __func__);
		goto ret;
	}

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		goto ret;

	buf->idx = -1;
	buf->port = port;
	buf->flags = SAA7164_BUFFER_FREE;
	buf->pos = 0;
	buf->actual_size = params->pitch * params->numberoflines;
	buf->crc = 0;
	/* TODO: arg len is being ignored */
	buf->pci_size = SAA7164_PT_ENTRIES * 0x1000;
	buf->pt_size = (SAA7164_PT_ENTRIES * sizeof(u64)) + 0x1000;

	/* Allocate contiguous memory */
	buf->cpu = dma_alloc_coherent(&port->dev->pci->dev, buf->pci_size,
				      &buf->dma, GFP_KERNEL);
	if (!buf->cpu)
		goto fail1;

	buf->pt_cpu = dma_alloc_coherent(&port->dev->pci->dev, buf->pt_size,
					 &buf->pt_dma, GFP_KERNEL);
	if (!buf->pt_cpu)
		goto fail2;

	/* init the buffers to a known pattern, easier during debugging */
	memset(buf->cpu, 0xff, buf->pci_size);
	buf->crc = crc32(0, buf->cpu, buf->actual_size);
	memset(buf->pt_cpu, 0xff, buf->pt_size);

	dprintk(DBGLVL_BUF, "%s()   allocated buffer @ 0x%p (%d pageptrs)\n",
		__func__, buf, params->numpagetables);
	dprintk(DBGLVL_BUF, "  pci_cpu @ 0x%p    dma @ 0x%08lx len = 0x%x\n",
		buf->cpu, (long)buf->dma, buf->pci_size);
	dprintk(DBGLVL_BUF, "   pt_cpu @ 0x%p pt_dma @ 0x%08lx len = 0x%x\n",
		buf->pt_cpu, (long)buf->pt_dma, buf->pt_size);

	/* Format the Page Table Entries to point into the data buffer */
	for (i = 0 ; i < params->numpagetables; i++) {

		*(buf->pt_cpu + i) = buf->dma + (i * 0x1000); /* TODO */
		dprintk(DBGLVL_BUF, "    pt[%02d] = 0x%p -> 0x%llx\n",
			i, buf->pt_cpu, (u64)*(buf->pt_cpu));

	}

	goto ret;

fail2:
	dma_free_coherent(&port->dev->pci->dev, buf->pci_size, buf->cpu,
			  buf->dma);
fail1:
	kfree(buf);

	buf = NULL;
ret:
	return buf;
}

int saa7164_buffer_dealloc(struct saa7164_buffer *buf)
{
	struct saa7164_dev *dev;

	if (!buf || !buf->port)
		return SAA_ERR_BAD_PARAMETER;
	dev = buf->port->dev;

	dprintk(DBGLVL_BUF, "%s() deallocating buffer @ 0x%p\n",
		__func__, buf);

	if (buf->flags != SAA7164_BUFFER_FREE)
		log_warn(" freeing a non-free buffer\n");

	dma_free_coherent(&dev->pci->dev, buf->pci_size, buf->cpu, buf->dma);
	dma_free_coherent(&dev->pci->dev, buf->pt_size, buf->pt_cpu,
			  buf->pt_dma);

	kfree(buf);

	return SAA_OK;
}

int saa7164_buffer_zero_offsets(struct saa7164_port *port, int i)
{
	struct saa7164_dev *dev = port->dev;

	if ((i < 0) || (i >= port->hwcfg.buffercount))
		return -EINVAL;

	dprintk(DBGLVL_BUF, "%s(idx = %d)\n", __func__, i);

	saa7164_writel(port->bufoffset + (sizeof(u32) * i), 0);

	return 0;
}

/* Write a buffer into the hardware */
int saa7164_buffer_activate(struct saa7164_buffer *buf, int i)
{
	struct saa7164_port *port = buf->port;
	struct saa7164_dev *dev = port->dev;

	if ((i < 0) || (i >= port->hwcfg.buffercount))
		return -EINVAL;

	dprintk(DBGLVL_BUF, "%s(idx = %d)\n", __func__, i);

	buf->idx = i; /* Note of which buffer list index position we occupy */
	buf->flags = SAA7164_BUFFER_BUSY;
	buf->pos = 0;

	/* TODO: Review this in light of 32v64 assignments */
	saa7164_writel(port->bufoffset + (sizeof(u32) * i), 0);
	saa7164_writel(port->bufptr32h + ((sizeof(u32) * 2) * i), buf->pt_dma);
	saa7164_writel(port->bufptr32l + ((sizeof(u32) * 2) * i), 0);

	dprintk(DBGLVL_BUF, "	buf[%d] offset 0x%llx (0x%x) buf 0x%llx/%llx (0x%x/%x) nr=%d\n",
		buf->idx,
		(u64)port->bufoffset + (i * sizeof(u32)),
		saa7164_readl(port->bufoffset + (sizeof(u32) * i)),
		(u64)port->bufptr32h + ((sizeof(u32) * 2) * i),
		(u64)port->bufptr32l + ((sizeof(u32) * 2) * i),
		saa7164_readl(port->bufptr32h + ((sizeof(u32) * i) * 2)),
		saa7164_readl(port->bufptr32l + ((sizeof(u32) * i) * 2)),
		buf->idx);

	return 0;
}

int saa7164_buffer_cfg_port(struct saa7164_port *port)
{
	struct tmHWStreamParameters *params = &port->hw_streamingparams;
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct list_head *c, *n;
	int i = 0;

	dprintk(DBGLVL_BUF, "%s(port=%d)\n", __func__, port->nr);

	saa7164_writel(port->bufcounter, 0);
	saa7164_writel(port->pitch, params->pitch);
	saa7164_writel(port->bufsize, params->pitch * params->numberoflines);

	dprintk(DBGLVL_BUF, " configured:\n");
	dprintk(DBGLVL_BUF, "   lmmio       0x%p\n", dev->lmmio);
	dprintk(DBGLVL_BUF, "   bufcounter  0x%x = 0x%x\n", port->bufcounter,
		saa7164_readl(port->bufcounter));

	dprintk(DBGLVL_BUF, "   pitch       0x%x = %d\n", port->pitch,
		saa7164_readl(port->pitch));

	dprintk(DBGLVL_BUF, "   bufsize     0x%x = %d\n", port->bufsize,
		saa7164_readl(port->bufsize));

	dprintk(DBGLVL_BUF, "   buffercount = %d\n", port->hwcfg.buffercount);
	dprintk(DBGLVL_BUF, "   bufoffset = 0x%x\n", port->bufoffset);
	dprintk(DBGLVL_BUF, "   bufptr32h = 0x%x\n", port->bufptr32h);
	dprintk(DBGLVL_BUF, "   bufptr32l = 0x%x\n", port->bufptr32l);

	/* Poke the buffers and offsets into PCI space */
	mutex_lock(&port->dmaqueue_lock);
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);

		BUG_ON(buf->flags != SAA7164_BUFFER_FREE);

		/* Place the buffer in the h/w queue */
		saa7164_buffer_activate(buf, i);

		/* Don't exceed the device maximum # bufs */
		BUG_ON(i > port->hwcfg.buffercount);
		i++;

	}
	mutex_unlock(&port->dmaqueue_lock);

	return 0;
}

struct saa7164_user_buffer *saa7164_buffer_alloc_user(struct saa7164_dev *dev,
	u32 len)
{
	struct saa7164_user_buffer *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->data = kzalloc(len, GFP_KERNEL);

	if (!buf->data) {
		kfree(buf);
		return NULL;
	}

	buf->actual_size = len;
	buf->pos = 0;
	buf->crc = 0;

	dprintk(DBGLVL_BUF, "%s()   allocated user buffer @ 0x%p\n",
		__func__, buf);

	return buf;
}

void saa7164_buffer_dealloc_user(struct saa7164_user_buffer *buf)
{
	if (!buf)
		return;

	kfree(buf->data);
	buf->data = NULL;

	kfree(buf);
}

/*
    module/drivers.c
    functions for manipulating drivers

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#define _GNU_SOURCE

#define __NO_VERSION__
#include "comedi_fops.h"
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include "comedidev.h"
#include "wrapper.h"
#include <linux/highmem.h>	/* for SuSE brokenness */
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/system.h>

static int postconfig(struct comedi_device *dev);
static int insn_rw_emulate_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static void *comedi_recognize(struct comedi_driver * driv, const char *name);
static void comedi_report_boards(struct comedi_driver *driv);
static int poll_invalid(struct comedi_device *dev, struct comedi_subdevice *s);
int comedi_buf_alloc(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned long new_size);

struct comedi_driver *comedi_drivers;

int comedi_modprobe(int minor)
{
	return -EINVAL;
}

static void cleanup_device(struct comedi_device *dev)
{
	int i;
	struct comedi_subdevice *s;

	if (dev->subdevices) {
		for (i = 0; i < dev->n_subdevices; i++) {
			s = dev->subdevices + i;
			comedi_free_subdevice_minor(s);
			if (s->async) {
				comedi_buf_alloc(dev, s, 0);
				kfree(s->async);
			}
		}
		kfree(dev->subdevices);
		dev->subdevices = NULL;
		dev->n_subdevices = 0;
	}
	kfree(dev->private);
	dev->private = NULL;
	dev->driver = 0;
	dev->board_name = NULL;
	dev->board_ptr = NULL;
	dev->iobase = 0;
	dev->irq = 0;
	dev->read_subdev = NULL;
	dev->write_subdev = NULL;
	dev->open = NULL;
	dev->close = NULL;
	comedi_set_hw_dev(dev, NULL);
}

static void __comedi_device_detach(struct comedi_device *dev)
{
	dev->attached = 0;
	if (dev->driver) {
		dev->driver->detach(dev);
	} else {
		printk("BUG: dev->driver=NULL in comedi_device_detach()\n");
	}
	cleanup_device(dev);
}

void comedi_device_detach(struct comedi_device *dev)
{
	if (!dev->attached)
		return;
	__comedi_device_detach(dev);
}

int comedi_device_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_driver *driv;
	int ret;

	if (dev->attached)
		return -EBUSY;

	for (driv = comedi_drivers; driv; driv = driv->next) {
		if (!try_module_get(driv->module)) {
			printk("comedi: failed to increment module count, skipping\n");
			continue;
		}
		if (driv->num_names) {
			dev->board_ptr = comedi_recognize(driv, it->board_name);
			if (dev->board_ptr == NULL) {
				module_put(driv->module);
				continue;
			}
		} else {
			if (strcmp(driv->driver_name, it->board_name)) {
				module_put(driv->module);
				continue;
			}
		}
		/* initialize dev->driver here so comedi_error() can be called from attach */
		dev->driver = driv;
		ret = driv->attach(dev, it);
		if (ret < 0) {
			module_put(dev->driver->module);
			__comedi_device_detach(dev);
			return ret;
		}
		goto attached;
	}

	/*  recognize has failed if we get here */
	/*  report valid board names before returning error */
	for (driv = comedi_drivers; driv; driv = driv->next) {
		if (!try_module_get(driv->module)) {
			printk("comedi: failed to increment module count\n");
			continue;
		}
		comedi_report_boards(driv);
		module_put(driv->module);
	}
	return -EIO;

attached:
	/* do a little post-config cleanup */
	ret = postconfig(dev);
	module_put(dev->driver->module);
	if (ret < 0) {
		__comedi_device_detach(dev);
		return ret;
	}

	if (!dev->board_name) {
		printk("BUG: dev->board_name=<%p>\n", dev->board_name);
		dev->board_name = "BUG";
	}
	smp_wmb();
	dev->attached = 1;

	return 0;
}

int comedi_driver_register(struct comedi_driver *driver)
{
	driver->next = comedi_drivers;
	comedi_drivers = driver;

	return 0;
}

int comedi_driver_unregister(struct comedi_driver *driver)
{
	struct comedi_driver *prev;
	int i;

	/* check for devices using this driver */
	for (i = 0; i < COMEDI_NUM_BOARD_MINORS; i++) {
		struct comedi_device_file_info *dev_file_info = comedi_get_device_file_info(i);
		struct comedi_device *dev;

		if(dev_file_info == NULL) continue;
		dev = dev_file_info->device;

		mutex_lock(&dev->mutex);
		if (dev->attached && dev->driver == driver) {
			if (dev->use_count)
				printk("BUG! detaching device with use_count=%d\n", dev->use_count);
			comedi_device_detach(dev);
		}
		mutex_unlock(&dev->mutex);
	}

	if (comedi_drivers == driver) {
		comedi_drivers = driver->next;
		return 0;
	}

	for (prev = comedi_drivers; prev->next; prev = prev->next) {
		if (prev->next == driver) {
			prev->next = driver->next;
			return 0;
		}
	}
	return -EINVAL;
}

static int postconfig(struct comedi_device *dev)
{
	int i;
	struct comedi_subdevice *s;
	struct comedi_async *async = NULL;
	int ret;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = dev->subdevices + i;

		if (s->type == COMEDI_SUBD_UNUSED)
			continue;

		if (s->len_chanlist == 0)
			s->len_chanlist = 1;

		if (s->do_cmd) {
			BUG_ON((s->subdev_flags & (SDF_CMD_READ |
				SDF_CMD_WRITE)) == 0);
			BUG_ON(!s->do_cmdtest);

			async = kzalloc(sizeof(struct comedi_async), GFP_KERNEL);
			if (async == NULL) {
				printk("failed to allocate async struct\n");
				return -ENOMEM;
			}
			init_waitqueue_head(&async->wait_head);
			async->subdevice = s;
			s->async = async;

#define DEFAULT_BUF_MAXSIZE (64*1024)
#define DEFAULT_BUF_SIZE (64*1024)

			async->max_bufsize = DEFAULT_BUF_MAXSIZE;

			async->prealloc_buf = NULL;
			async->prealloc_bufsz = 0;
			if (comedi_buf_alloc(dev, s, DEFAULT_BUF_SIZE) < 0) {
				printk("Buffer allocation failed\n");
				return -ENOMEM;
			}
			if (s->buf_change) {
				ret = s->buf_change(dev, s, DEFAULT_BUF_SIZE);
				if (ret < 0)
					return ret;
			}
			comedi_alloc_subdevice_minor(dev, s);
		}

		if (!s->range_table && !s->range_table_list)
			s->range_table = &range_unknown;

		if (!s->insn_read && s->insn_bits)
			s->insn_read = insn_rw_emulate_bits;
		if (!s->insn_write && s->insn_bits)
			s->insn_write = insn_rw_emulate_bits;

		if (!s->insn_read)
			s->insn_read = insn_inval;
		if (!s->insn_write)
			s->insn_write = insn_inval;
		if (!s->insn_bits)
			s->insn_bits = insn_inval;
		if (!s->insn_config)
			s->insn_config = insn_inval;

		if (!s->poll)
			s->poll = poll_invalid;
	}

	return 0;
}

/*  generic recognize function for drivers that register their supported board names */
void *comedi_recognize(struct comedi_driver * driv, const char *name)
{
	unsigned i;
	const char *const *name_ptr = driv->board_name;
	for (i = 0; i < driv->num_names; i++) {
		if (strcmp(*name_ptr, name) == 0)
			return (void *)name_ptr;
		name_ptr =
			(const char *const *)((const char *)name_ptr +
			driv->offset);
	}

	return NULL;
}

void comedi_report_boards(struct comedi_driver *driv)
{
	unsigned int i;
	const char *const *name_ptr;

	printk("comedi: valid board names for %s driver are:\n",
		driv->driver_name);

	name_ptr = driv->board_name;
	for (i = 0; i < driv->num_names; i++) {
		printk(" %s\n", *name_ptr);
		name_ptr = (const char **)((char *)name_ptr + driv->offset);
	}

	if (driv->num_names == 0)
		printk(" %s\n", driv->driver_name);
}

static int poll_invalid(struct comedi_device *dev, struct comedi_subdevice *s)
{
	return -EINVAL;
}

int insn_inval(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	return -EINVAL;
}

static int insn_rw_emulate_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_insn new_insn;
	int ret;
	static const unsigned channels_per_bitfield = 32;

	unsigned chan = CR_CHAN(insn->chanspec);
	const unsigned base_bitfield_channel =
		(chan < channels_per_bitfield) ? 0 : chan;
	unsigned int new_data[2];
	memset(new_data, 0, sizeof(new_data));
	memset(&new_insn, 0, sizeof(new_insn));
	new_insn.insn = INSN_BITS;
	new_insn.chanspec = base_bitfield_channel;
	new_insn.n = 2;
	new_insn.data = new_data;
	new_insn.subdev = insn->subdev;

	if (insn->insn == INSN_WRITE) {
		if (!(s->subdev_flags & SDF_WRITABLE))
			return -EINVAL;
		new_data[0] = 1 << (chan - base_bitfield_channel);	/* mask */
		new_data[1] = data[0] ? (1 << (chan - base_bitfield_channel)) : 0;	/* bits */
	}

	ret = s->insn_bits(dev, s, &new_insn, new_data);
	if (ret < 0)
		return ret;

	if (insn->insn == INSN_READ) {
		data[0] = (new_data[1] >> (chan - base_bitfield_channel)) & 1;
	}

	return 1;
}

static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;
	pud_t *pud;

	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, adr);
		pmd = pmd_offset(pud, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset_kernel(pmd, adr);
			pte = *ptep;
			if (pte_present(pte)) {
				ret = (unsigned long)
					page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
			}
		}
	}
	return ret;
}

static inline unsigned long kvirt_to_kva(unsigned long adr)
{
	unsigned long va, kva;

	va = adr;
	kva = uvirt_to_kva(pgd_offset_k(va), va);

	return kva;
}

int comedi_buf_alloc(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned long new_size)
{
	struct comedi_async *async = s->async;

	/* Round up new_size to multiple of PAGE_SIZE */
	new_size = (new_size + PAGE_SIZE - 1) & PAGE_MASK;

	/* if no change is required, do nothing */
	if (async->prealloc_buf && async->prealloc_bufsz == new_size) {
		return 0;
	}
	/*  deallocate old buffer */
	if (async->prealloc_buf) {
		vunmap(async->prealloc_buf);
		async->prealloc_buf = NULL;
		async->prealloc_bufsz = 0;
	}
	if (async->buf_page_list) {
		unsigned i;
		for (i = 0; i < async->n_buf_pages; ++i) {
			if (async->buf_page_list[i].virt_addr) {
				mem_map_unreserve(virt_to_page(async->
						buf_page_list[i].virt_addr));
				if (s->async_dma_dir != DMA_NONE) {
					dma_free_coherent(dev->hw_dev,
						PAGE_SIZE,
						async->buf_page_list[i].
						virt_addr,
						async->buf_page_list[i].
						dma_addr);
				} else {
					free_page((unsigned long)async->
						buf_page_list[i].virt_addr);
				}
			}
		}
		vfree(async->buf_page_list);
		async->buf_page_list = NULL;
		async->n_buf_pages = 0;
	}
	/*  allocate new buffer */
	if (new_size) {
		unsigned i = 0;
		unsigned n_pages = new_size >> PAGE_SHIFT;
		struct page **pages = NULL;

		async->buf_page_list =
			vmalloc(sizeof(struct comedi_buf_page) * n_pages);
		if (async->buf_page_list) {
			memset(async->buf_page_list, 0,
				sizeof(struct comedi_buf_page) * n_pages);
			pages = vmalloc(sizeof(struct page *) * n_pages);
		}
		if (pages) {
			for (i = 0; i < n_pages; i++) {
				if (s->async_dma_dir != DMA_NONE) {
					async->buf_page_list[i].virt_addr =
						dma_alloc_coherent(dev->hw_dev,
						PAGE_SIZE,
						&async->buf_page_list[i].
						dma_addr,
						GFP_KERNEL | __GFP_COMP);
				} else {
					async->buf_page_list[i].virt_addr =
						(void *)
						get_zeroed_page(GFP_KERNEL);
				}
				if (async->buf_page_list[i].virt_addr == NULL) {
					break;
				}
				mem_map_reserve(virt_to_page(async->
						buf_page_list[i].virt_addr));
				pages[i] =
					virt_to_page(async->buf_page_list[i].
					virt_addr);
			}
		}
		if (i == n_pages) {
			async->prealloc_buf =
				vmap(pages, n_pages, VM_MAP,
				PAGE_KERNEL_NOCACHE);
		}
		if (pages) {
			vfree(pages);
		}
		if (async->prealloc_buf == NULL) {
			/* Some allocation failed above. */
			if (async->buf_page_list) {
				for (i = 0; i < n_pages; i++) {
					if (async->buf_page_list[i].virt_addr ==
						NULL) {
						break;
					}
					mem_map_unreserve(virt_to_page(async->
							buf_page_list[i].
							virt_addr));
					if (s->async_dma_dir != DMA_NONE) {
						dma_free_coherent(dev->hw_dev,
							PAGE_SIZE,
							async->buf_page_list[i].
							virt_addr,
							async->buf_page_list[i].
							dma_addr);
					} else {
						free_page((unsigned long)async->
							buf_page_list[i].
							virt_addr);
					}
				}
				vfree(async->buf_page_list);
				async->buf_page_list = NULL;
			}
			return -ENOMEM;
		}
		async->n_buf_pages = n_pages;
	}
	async->prealloc_bufsz = new_size;

	return 0;
}

/* munging is applied to data by core as it passes between user
 * and kernel space */
unsigned int comedi_buf_munge(struct comedi_async *async, unsigned int num_bytes)
{
	struct comedi_subdevice *s = async->subdevice;
	unsigned int count = 0;
	const unsigned num_sample_bytes = bytes_per_sample(s);

	if (s->munge == NULL || (async->cmd.flags & CMDF_RAWDATA)) {
		async->munge_count += num_bytes;
		BUG_ON((int)(async->munge_count - async->buf_write_count) > 0);
		return num_bytes;
	}
	/* don't munge partial samples */
	num_bytes -= num_bytes % num_sample_bytes;
	while (count < num_bytes) {
		int block_size;

		block_size = num_bytes - count;
		if (block_size < 0) {
			rt_printk("%s: %s: bug! block_size is negative\n",
				__FILE__, __func__);
			break;
		}
		if ((int)(async->munge_ptr + block_size -
				async->prealloc_bufsz) > 0)
			block_size = async->prealloc_bufsz - async->munge_ptr;

		s->munge(s->device, s, async->prealloc_buf + async->munge_ptr,
			block_size, async->munge_chan);

		smp_wmb();	/* barrier insures data is munged in buffer before munge_count is incremented */

		async->munge_chan += block_size / num_sample_bytes;
		async->munge_chan %= async->cmd.chanlist_len;
		async->munge_count += block_size;
		async->munge_ptr += block_size;
		async->munge_ptr %= async->prealloc_bufsz;
		count += block_size;
	}
	BUG_ON((int)(async->munge_count - async->buf_write_count) > 0);
	return count;
}

unsigned int comedi_buf_write_n_available(struct comedi_async *async)
{
	unsigned int free_end;
	unsigned int nbytes;

	if (async == NULL)
		return 0;

	free_end = async->buf_read_count + async->prealloc_bufsz;
	nbytes = free_end - async->buf_write_alloc_count;
	nbytes -= nbytes % bytes_per_sample(async->subdevice);
	/* barrier insures the read of buf_read_count in this
	   query occurs before any following writes to the buffer which
	   might be based on the return value from this query.
	 */
	smp_mb();
	return nbytes;
}

/* allocates chunk for the writer from free buffer space */
unsigned int comedi_buf_write_alloc(struct comedi_async *async, unsigned int nbytes)
{
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	if ((int)(async->buf_write_alloc_count + nbytes - free_end) > 0) {
		nbytes = free_end - async->buf_write_alloc_count;
	}
	async->buf_write_alloc_count += nbytes;
	/* barrier insures the read of buf_read_count above occurs before
	   we write data to the write-alloc'ed buffer space */
	smp_mb();
	return nbytes;
}

/* allocates nothing unless it can completely fulfill the request */
unsigned int comedi_buf_write_alloc_strict(struct comedi_async *async,
	unsigned int nbytes)
{
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	if ((int)(async->buf_write_alloc_count + nbytes - free_end) > 0) {
		nbytes = 0;
	}
	async->buf_write_alloc_count += nbytes;
	/* barrier insures the read of buf_read_count above occurs before
	   we write data to the write-alloc'ed buffer space */
	smp_mb();
	return nbytes;
}

/* transfers a chunk from writer to filled buffer space */
unsigned comedi_buf_write_free(struct comedi_async *async, unsigned int nbytes)
{
	if ((int)(async->buf_write_count + nbytes -
			async->buf_write_alloc_count) > 0) {
		rt_printk
			("comedi: attempted to write-free more bytes than have been write-allocated.\n");
		nbytes = async->buf_write_alloc_count - async->buf_write_count;
	}
	async->buf_write_count += nbytes;
	async->buf_write_ptr += nbytes;
	comedi_buf_munge(async, async->buf_write_count - async->munge_count);
	if (async->buf_write_ptr >= async->prealloc_bufsz) {
		async->buf_write_ptr %= async->prealloc_bufsz;
	}
	return nbytes;
}

/* allocates a chunk for the reader from filled (and munged) buffer space */
unsigned comedi_buf_read_alloc(struct comedi_async *async, unsigned nbytes)
{
	if ((int)(async->buf_read_alloc_count + nbytes - async->munge_count) >
		0) {
		nbytes = async->munge_count - async->buf_read_alloc_count;
	}
	async->buf_read_alloc_count += nbytes;
	/* barrier insures read of munge_count occurs before we actually read
	   data out of buffer */
	smp_rmb();
	return nbytes;
}

/* transfers control of a chunk from reader to free buffer space */
unsigned comedi_buf_read_free(struct comedi_async *async, unsigned int nbytes)
{
	/*  barrier insures data has been read out of buffer before read count is incremented */
	smp_mb();
	if ((int)(async->buf_read_count + nbytes -
			async->buf_read_alloc_count) > 0) {
		rt_printk
			("comedi: attempted to read-free more bytes than have been read-allocated.\n");
		nbytes = async->buf_read_alloc_count - async->buf_read_count;
	}
	async->buf_read_count += nbytes;
	async->buf_read_ptr += nbytes;
	async->buf_read_ptr %= async->prealloc_bufsz;
	return nbytes;
}

void comedi_buf_memcpy_to(struct comedi_async *async, unsigned int offset,
	const void *data, unsigned int num_bytes)
{
	unsigned int write_ptr = async->buf_write_ptr + offset;

	if (write_ptr >= async->prealloc_bufsz)
		write_ptr %= async->prealloc_bufsz;

	while (num_bytes) {
		unsigned int block_size;

		if (write_ptr + num_bytes > async->prealloc_bufsz)
			block_size = async->prealloc_bufsz - write_ptr;
		else
			block_size = num_bytes;

		memcpy(async->prealloc_buf + write_ptr, data, block_size);

		data += block_size;
		num_bytes -= block_size;

		write_ptr = 0;
	}
}

void comedi_buf_memcpy_from(struct comedi_async *async, unsigned int offset,
	void *dest, unsigned int nbytes)
{
	void *src;
	unsigned int read_ptr = async->buf_read_ptr + offset;

	if (read_ptr >= async->prealloc_bufsz)
		read_ptr %= async->prealloc_bufsz;

	while (nbytes) {
		unsigned int block_size;

		src = async->prealloc_buf + read_ptr;

		if (nbytes >= async->prealloc_bufsz - read_ptr)
			block_size = async->prealloc_bufsz - read_ptr;
		else
			block_size = nbytes;

		memcpy(dest, src, block_size);
		nbytes -= block_size;
		dest += block_size;
		read_ptr = 0;
	}
}

unsigned int comedi_buf_read_n_available(struct comedi_async *async)
{
	unsigned num_bytes;

	if (async == NULL)
		return 0;
	num_bytes = async->munge_count - async->buf_read_count;
	/* barrier insures the read of munge_count in this
	   query occurs before any following reads of the buffer which
	   might be based on the return value from this query.
	 */
	smp_rmb();
	return num_bytes;
}

int comedi_buf_get(struct comedi_async *async, short *x)
{
	unsigned int n = comedi_buf_read_n_available(async);

	if (n < sizeof(short))
		return 0;
	comedi_buf_read_alloc(async, sizeof(short));
	*x = *(short *) (async->prealloc_buf + async->buf_read_ptr);
	comedi_buf_read_free(async, sizeof(short));
	return 1;
}

int comedi_buf_put(struct comedi_async *async, short x)
{
	unsigned int n = comedi_buf_write_alloc_strict(async, sizeof(short));

	if (n < sizeof(short)) {
		async->events |= COMEDI_CB_ERROR;
		return 0;
	}
	*(short *) (async->prealloc_buf + async->buf_write_ptr) = x;
	comedi_buf_write_free(async, sizeof(short));
	return 1;
}

void comedi_reset_async_buf(struct comedi_async *async)
{
	async->buf_write_alloc_count = 0;
	async->buf_write_count = 0;
	async->buf_read_alloc_count = 0;
	async->buf_read_count = 0;

	async->buf_write_ptr = 0;
	async->buf_read_ptr = 0;

	async->cur_chan = 0;
	async->scan_progress = 0;
	async->munge_chan = 0;
	async->munge_count = 0;
	async->munge_ptr = 0;

	async->events = 0;
}

int comedi_auto_config(struct device *hardware_device, const char *board_name, const int *options, unsigned num_options)
{
	struct comedi_devconfig it;
	int minor;
	struct comedi_device_file_info *dev_file_info;
	int retval;
	unsigned *private_data = NULL;

	if (!comedi_autoconfig) {
		dev_set_drvdata(hardware_device, NULL);
		return 0;
	}

	minor = comedi_alloc_board_minor(hardware_device);
	if(minor < 0) return minor;

	private_data = kmalloc(sizeof(unsigned), GFP_KERNEL);
	if (private_data == NULL) {
		retval = -ENOMEM;
		goto cleanup;
	}
	*private_data = minor;
	dev_set_drvdata(hardware_device, private_data);

	dev_file_info = comedi_get_device_file_info(minor);

	memset(&it, 0, sizeof(it));
	strncpy(it.board_name, board_name, COMEDI_NAMELEN);
	it.board_name[COMEDI_NAMELEN - 1] = '\0';
	BUG_ON(num_options > COMEDI_NDEVCONFOPTS);
	memcpy(it.options, options, num_options * sizeof(int));

	mutex_lock(&dev_file_info->device->mutex);
	retval = comedi_device_attach(dev_file_info->device, &it);
	mutex_unlock(&dev_file_info->device->mutex);

cleanup:
	if(retval < 0)
	{
		kfree(private_data);
		comedi_free_board_minor(minor);
	}
	return retval;
}

void comedi_auto_unconfig(struct device *hardware_device)
{
	unsigned *minor = (unsigned *)dev_get_drvdata(hardware_device);
	if(minor == NULL) return;

	BUG_ON(*minor >= COMEDI_NUM_BOARD_MINORS);

	comedi_free_board_minor(*minor);
	dev_set_drvdata(hardware_device, NULL);
	kfree(minor);
}

int comedi_pci_auto_config(struct pci_dev *pcidev, const char *board_name)
{
	int options[2];

	/*  pci bus */
	options[0] = pcidev->bus->number;
	/*  pci slot */
	options[1] = PCI_SLOT(pcidev->devfn);

	return comedi_auto_config(&pcidev->dev, board_name, options, sizeof(options) / sizeof(options[0]));
}

void comedi_pci_auto_unconfig(struct pci_dev *pcidev)
{
	comedi_auto_unconfig(&pcidev->dev);
}

int comedi_usb_auto_config(struct usb_device *usbdev,
	const char *board_name)
{
	BUG_ON(usbdev == NULL);
	return comedi_auto_config(&usbdev->dev, board_name, NULL, 0);
}

void comedi_usb_auto_unconfig(struct usb_device *usbdev)
{
	BUG_ON(usbdev == NULL);
	comedi_auto_unconfig(&usbdev->dev);
}

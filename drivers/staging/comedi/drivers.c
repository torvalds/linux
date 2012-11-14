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
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>	/* for SuSE brokenness */
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include "comedidev.h"
#include "comedi_internal.h"

static int postconfig(struct comedi_device *dev);
static int insn_rw_emulate_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);
static void *comedi_recognize(struct comedi_driver *driv, const char *name);
static void comedi_report_boards(struct comedi_driver *driv);
static int poll_invalid(struct comedi_device *dev, struct comedi_subdevice *s);

struct comedi_driver *comedi_drivers;

int comedi_alloc_subdevices(struct comedi_device *dev, int num_subdevices)
{
	struct comedi_subdevice *s;
	int i;

	if (num_subdevices < 1)
		return -EINVAL;

	s = kcalloc(num_subdevices, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	dev->subdevices = s;
	dev->n_subdevices = num_subdevices;

	for (i = 0; i < num_subdevices; ++i) {
		s = &dev->subdevices[i];
		s->device = dev;
		s->async_dma_dir = DMA_NONE;
		spin_lock_init(&s->spin_lock);
		s->minor = -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(comedi_alloc_subdevices);

static void cleanup_device(struct comedi_device *dev)
{
	int i;
	struct comedi_subdevice *s;

	if (dev->subdevices) {
		for (i = 0; i < dev->n_subdevices; i++) {
			s = &dev->subdevices[i];
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
	dev->driver = NULL;
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
	if (dev->driver)
		dev->driver->detach(dev);
	else
		dev_warn(dev->class_dev,
			 "BUG: dev->driver=NULL in comedi_device_detach()\n");
	cleanup_device(dev);
}

void comedi_device_detach(struct comedi_device *dev)
{
	if (!dev->attached)
		return;
	__comedi_device_detach(dev);
}

/* do a little post-config cleanup */
/* called with module refcount incremented, decrements it */
static int comedi_device_postconfig(struct comedi_device *dev)
{
	int ret = postconfig(dev);
	module_put(dev->driver->module);
	if (ret < 0) {
		__comedi_device_detach(dev);
		return ret;
	}
	if (!dev->board_name) {
		dev_warn(dev->class_dev, "BUG: dev->board_name=NULL\n");
		dev->board_name = "BUG";
	}
	smp_wmb();
	dev->attached = 1;
	return 0;
}

int comedi_device_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_driver *driv;
	int ret;

	if (dev->attached)
		return -EBUSY;

	for (driv = comedi_drivers; driv; driv = driv->next) {
		if (!try_module_get(driv->module))
			continue;
		if (driv->num_names) {
			dev->board_ptr = comedi_recognize(driv, it->board_name);
			if (dev->board_ptr)
				break;
		} else if (strcmp(driv->driver_name, it->board_name) == 0)
			break;
		module_put(driv->module);
	}
	if (driv == NULL) {
		/*  recognize has failed if we get here */
		/*  report valid board names before returning error */
		for (driv = comedi_drivers; driv; driv = driv->next) {
			if (!try_module_get(driv->module))
				continue;
			comedi_report_boards(driv);
			module_put(driv->module);
		}
		return -EIO;
	}
	if (driv->attach == NULL) {
		/* driver does not support manual configuration */
		dev_warn(dev->class_dev,
			 "driver '%s' does not support attach using comedi_config\n",
			 driv->driver_name);
		module_put(driv->module);
		return -ENOSYS;
	}
	/* initialize dev->driver here so
	 * comedi_error() can be called from attach */
	dev->driver = driv;
	ret = driv->attach(dev, it);
	if (ret < 0) {
		module_put(dev->driver->module);
		__comedi_device_detach(dev);
		return ret;
	}
	return comedi_device_postconfig(dev);
}

int comedi_driver_register(struct comedi_driver *driver)
{
	driver->next = comedi_drivers;
	comedi_drivers = driver;

	return 0;
}
EXPORT_SYMBOL(comedi_driver_register);

int comedi_driver_unregister(struct comedi_driver *driver)
{
	struct comedi_driver *prev;
	int i;

	/* check for devices using this driver */
	for (i = 0; i < COMEDI_NUM_BOARD_MINORS; i++) {
		struct comedi_device_file_info *dev_file_info =
		    comedi_get_device_file_info(i);
		struct comedi_device *dev;

		if (dev_file_info == NULL)
			continue;
		dev = dev_file_info->device;

		mutex_lock(&dev->mutex);
		if (dev->attached && dev->driver == driver) {
			if (dev->use_count)
				dev_warn(dev->class_dev,
					 "BUG! detaching device with use_count=%d\n",
					 dev->use_count);
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
EXPORT_SYMBOL(comedi_driver_unregister);

static int postconfig(struct comedi_device *dev)
{
	int i;
	struct comedi_subdevice *s;
	struct comedi_async *async = NULL;
	int ret;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];

		if (s->type == COMEDI_SUBD_UNUSED)
			continue;

		if (s->len_chanlist == 0)
			s->len_chanlist = 1;

		if (s->do_cmd) {
			unsigned int buf_size;

			BUG_ON((s->subdev_flags & (SDF_CMD_READ |
						   SDF_CMD_WRITE)) == 0);
			BUG_ON(!s->do_cmdtest);

			async =
			    kzalloc(sizeof(struct comedi_async), GFP_KERNEL);
			if (async == NULL) {
				dev_warn(dev->class_dev,
					 "failed to allocate async struct\n");
				return -ENOMEM;
			}
			init_waitqueue_head(&async->wait_head);
			async->subdevice = s;
			s->async = async;

			async->max_bufsize =
				comedi_default_buf_maxsize_kb * 1024;
			buf_size = comedi_default_buf_size_kb * 1024;
			if (buf_size > async->max_bufsize)
				buf_size = async->max_bufsize;

			async->prealloc_buf = NULL;
			async->prealloc_bufsz = 0;
			if (comedi_buf_alloc(dev, s, buf_size) < 0) {
				dev_warn(dev->class_dev,
					 "Buffer allocation failed\n");
				return -ENOMEM;
			}
			if (s->buf_change) {
				ret = s->buf_change(dev, s, buf_size);
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

/*
 * Generic recognize function for drivers that register their supported
 * board names.
 *
 * 'driv->board_name' points to a 'const char *' member within the
 * zeroth element of an array of some private board information
 * structure, say 'struct foo_board' containing a member 'const char
 * *board_name' that is initialized to point to a board name string that
 * is one of the candidates matched against this function's 'name'
 * parameter.
 *
 * 'driv->offset' is the size of the private board information
 * structure, say 'sizeof(struct foo_board)', and 'driv->num_names' is
 * the length of the array of private board information structures.
 *
 * If one of the board names in the array of private board information
 * structures matches the name supplied to this function, the function
 * returns a pointer to the pointer to the board name, otherwise it
 * returns NULL.  The return value ends up in the 'board_ptr' member of
 * a 'struct comedi_device' that the low-level comedi driver's
 * 'attach()' hook can convert to a point to a particular element of its
 * array of private board information structures by subtracting the
 * offset of the member that points to the board name.  (No subtraction
 * is required if the board name pointer is the first member of the
 * private board information structure, which is generally the case.)
 */
static void *comedi_recognize(struct comedi_driver *driv, const char *name)
{
	char **name_ptr = (char **)driv->board_name;
	int i;

	for (i = 0; i < driv->num_names; i++) {
		if (strcmp(*name_ptr, name) == 0)
			return name_ptr;
		name_ptr = (void *)name_ptr + driv->offset;
	}

	return NULL;
}

static void comedi_report_boards(struct comedi_driver *driv)
{
	unsigned int i;
	const char *const *name_ptr;

	pr_info("comedi: valid board names for %s driver are:\n",
		driv->driver_name);

	name_ptr = driv->board_name;
	for (i = 0; i < driv->num_names; i++) {
		pr_info(" %s\n", *name_ptr);
		name_ptr = (const char **)((char *)name_ptr + driv->offset);
	}

	if (driv->num_names == 0)
		pr_info(" %s\n", driv->driver_name);
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

static int insn_rw_emulate_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
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
	new_insn.subdev = insn->subdev;

	if (insn->insn == INSN_WRITE) {
		if (!(s->subdev_flags & SDF_WRITABLE))
			return -EINVAL;
		new_data[0] = 1 << (chan - base_bitfield_channel); /* mask */
		new_data[1] = data[0] ? (1 << (chan - base_bitfield_channel))
			      : 0; /* bits */
	}

	ret = s->insn_bits(dev, s, &new_insn, new_data);
	if (ret < 0)
		return ret;

	if (insn->insn == INSN_READ)
		data[0] = (new_data[1] >> (chan - base_bitfield_channel)) & 1;

	return 1;
}

int comedi_buf_alloc(struct comedi_device *dev, struct comedi_subdevice *s,
		     unsigned long new_size)
{
	struct comedi_async *async = s->async;

	/* Round up new_size to multiple of PAGE_SIZE */
	new_size = (new_size + PAGE_SIZE - 1) & PAGE_MASK;

	/* if no change is required, do nothing */
	if (async->prealloc_buf && async->prealloc_bufsz == new_size)
		return 0;

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
				clear_bit(PG_reserved,
					&(virt_to_page(async->buf_page_list[i].
							virt_addr)->flags));
				if (s->async_dma_dir != DMA_NONE) {
					dma_free_coherent(dev->hw_dev,
							  PAGE_SIZE,
							  async->
							  buf_page_list
							  [i].virt_addr,
							  async->
							  buf_page_list
							  [i].dma_addr);
				} else {
					free_page((unsigned long)
						  async->buf_page_list[i].
						  virt_addr);
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
		    vzalloc(sizeof(struct comedi_buf_page) * n_pages);
		if (async->buf_page_list)
			pages = vmalloc(sizeof(struct page *) * n_pages);

		if (pages) {
			for (i = 0; i < n_pages; i++) {
				if (s->async_dma_dir != DMA_NONE) {
					async->buf_page_list[i].virt_addr =
					    dma_alloc_coherent(dev->hw_dev,
							       PAGE_SIZE,
							       &async->
							       buf_page_list
							       [i].dma_addr,
							       GFP_KERNEL |
							       __GFP_COMP);
				} else {
					async->buf_page_list[i].virt_addr =
					    (void *)
					    get_zeroed_page(GFP_KERNEL);
				}
				if (async->buf_page_list[i].virt_addr == NULL)
					break;

				set_bit(PG_reserved,
					&(virt_to_page(async->buf_page_list[i].
							virt_addr)->flags));
				pages[i] = virt_to_page(async->buf_page_list[i].
								virt_addr);
			}
		}
		if (i == n_pages) {
			async->prealloc_buf =
#ifdef PAGE_KERNEL_NOCACHE
			    vmap(pages, n_pages, VM_MAP, PAGE_KERNEL_NOCACHE);
#else
			    vmap(pages, n_pages, VM_MAP, PAGE_KERNEL);
#endif
		}
		vfree(pages);

		if (async->prealloc_buf == NULL) {
			/* Some allocation failed above. */
			if (async->buf_page_list) {
				for (i = 0; i < n_pages; i++) {
					if (async->buf_page_list[i].virt_addr ==
					    NULL) {
						break;
					}
					clear_bit(PG_reserved,
						&(virt_to_page(async->
							buf_page_list[i].
							virt_addr)->flags));
					if (s->async_dma_dir != DMA_NONE) {
						dma_free_coherent(dev->hw_dev,
								  PAGE_SIZE,
								  async->
								  buf_page_list
								  [i].virt_addr,
								  async->
								  buf_page_list
								  [i].dma_addr);
					} else {
						free_page((unsigned long)
							  async->buf_page_list
							  [i].virt_addr);
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
static unsigned int comedi_buf_munge(struct comedi_async *async,
				     unsigned int num_bytes)
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
			dev_warn(s->device->class_dev,
				 "%s: %s: bug! block_size is negative\n",
				 __FILE__, __func__);
			break;
		}
		if ((int)(async->munge_ptr + block_size -
			  async->prealloc_bufsz) > 0)
			block_size = async->prealloc_bufsz - async->munge_ptr;

		s->munge(s->device, s, async->prealloc_buf + async->munge_ptr,
			 block_size, async->munge_chan);

		smp_wmb();	/* barrier insures data is munged in buffer
				 * before munge_count is incremented */

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
unsigned int comedi_buf_write_alloc(struct comedi_async *async,
				    unsigned int nbytes)
{
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	if ((int)(async->buf_write_alloc_count + nbytes - free_end) > 0)
		nbytes = free_end - async->buf_write_alloc_count;

	async->buf_write_alloc_count += nbytes;
	/* barrier insures the read of buf_read_count above occurs before
	   we write data to the write-alloc'ed buffer space */
	smp_mb();
	return nbytes;
}
EXPORT_SYMBOL(comedi_buf_write_alloc);

/* allocates nothing unless it can completely fulfill the request */
unsigned int comedi_buf_write_alloc_strict(struct comedi_async *async,
					   unsigned int nbytes)
{
	unsigned int free_end = async->buf_read_count + async->prealloc_bufsz;

	if ((int)(async->buf_write_alloc_count + nbytes - free_end) > 0)
		nbytes = 0;

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
		dev_info(async->subdevice->device->class_dev,
			 "attempted to write-free more bytes than have been write-allocated.\n");
		nbytes = async->buf_write_alloc_count - async->buf_write_count;
	}
	async->buf_write_count += nbytes;
	async->buf_write_ptr += nbytes;
	comedi_buf_munge(async, async->buf_write_count - async->munge_count);
	if (async->buf_write_ptr >= async->prealloc_bufsz)
		async->buf_write_ptr %= async->prealloc_bufsz;

	return nbytes;
}
EXPORT_SYMBOL(comedi_buf_write_free);

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
EXPORT_SYMBOL(comedi_buf_read_alloc);

/* transfers control of a chunk from reader to free buffer space */
unsigned comedi_buf_read_free(struct comedi_async *async, unsigned int nbytes)
{
	/* barrier insures data has been read out of
	 * buffer before read count is incremented */
	smp_mb();
	if ((int)(async->buf_read_count + nbytes -
		  async->buf_read_alloc_count) > 0) {
		dev_info(async->subdevice->device->class_dev,
			 "attempted to read-free more bytes than have been read-allocated.\n");
		nbytes = async->buf_read_alloc_count - async->buf_read_count;
	}
	async->buf_read_count += nbytes;
	async->buf_read_ptr += nbytes;
	async->buf_read_ptr %= async->prealloc_bufsz;
	return nbytes;
}
EXPORT_SYMBOL(comedi_buf_read_free);

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
EXPORT_SYMBOL(comedi_buf_memcpy_to);

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
EXPORT_SYMBOL(comedi_buf_memcpy_from);

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
EXPORT_SYMBOL(comedi_buf_read_n_available);

int comedi_buf_get(struct comedi_async *async, short *x)
{
	unsigned int n = comedi_buf_read_n_available(async);

	if (n < sizeof(short))
		return 0;
	comedi_buf_read_alloc(async, sizeof(short));
	*x = *(short *)(async->prealloc_buf + async->buf_read_ptr);
	comedi_buf_read_free(async, sizeof(short));
	return 1;
}
EXPORT_SYMBOL(comedi_buf_get);

int comedi_buf_put(struct comedi_async *async, short x)
{
	unsigned int n = comedi_buf_write_alloc_strict(async, sizeof(short));

	if (n < sizeof(short)) {
		async->events |= COMEDI_CB_ERROR;
		return 0;
	}
	*(short *)(async->prealloc_buf + async->buf_write_ptr) = x;
	comedi_buf_write_free(async, sizeof(short));
	return 1;
}
EXPORT_SYMBOL(comedi_buf_put);

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

int comedi_auto_config(struct device *hardware_device,
		       struct comedi_driver *driver, unsigned long context)
{
	int minor;
	struct comedi_device_file_info *dev_file_info;
	struct comedi_device *comedi_dev;
	int ret;

	if (!comedi_autoconfig)
		return 0;

	if (!driver->auto_attach) {
		dev_warn(hardware_device,
			 "BUG! comedi driver '%s' has no auto_attach handler\n",
			 driver->driver_name);
		return -EINVAL;
	}

	minor = comedi_alloc_board_minor(hardware_device);
	if (minor < 0)
		return minor;

	dev_file_info = comedi_get_device_file_info(minor);
	comedi_dev = dev_file_info->device;

	mutex_lock(&comedi_dev->mutex);
	if (comedi_dev->attached)
		ret = -EBUSY;
	else if (!try_module_get(driver->module))
		ret = -EIO;
	else {
		comedi_set_hw_dev(comedi_dev, hardware_device);
		comedi_dev->driver = driver;
		ret = driver->auto_attach(comedi_dev, context);
		if (ret < 0) {
			module_put(driver->module);
			__comedi_device_detach(comedi_dev);
		} else {
			ret = comedi_device_postconfig(comedi_dev);
		}
	}
	mutex_unlock(&comedi_dev->mutex);

	if (ret < 0)
		comedi_free_board_minor(minor);
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_auto_config);

void comedi_auto_unconfig(struct device *hardware_device)
{
	int minor;

	if (hardware_device == NULL)
		return;
	minor = comedi_find_board_minor(hardware_device);
	if (minor < 0)
		return;
	BUG_ON(minor >= COMEDI_NUM_BOARD_MINORS);
	comedi_free_board_minor(minor);
}
EXPORT_SYMBOL_GPL(comedi_auto_unconfig);

/**
 * comedi_pci_enable() - Enable the PCI device and request the regions.
 * @pdev: pci_dev struct
 * @res_name: name for the requested reqource
 */
int comedi_pci_enable(struct pci_dev *pdev, const char *res_name)
{
	int rc;

	rc = pci_enable_device(pdev);
	if (rc < 0)
		return rc;

	rc = pci_request_regions(pdev, res_name);
	if (rc < 0)
		pci_disable_device(pdev);

	return rc;
}
EXPORT_SYMBOL_GPL(comedi_pci_enable);

/**
 * comedi_pci_disable() - Release the regions and disable the PCI device.
 * @pdev: pci_dev struct
 *
 * This must be matched with a previous successful call to comedi_pci_enable().
 */
void comedi_pci_disable(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}
EXPORT_SYMBOL_GPL(comedi_pci_disable);

int comedi_pci_driver_register(struct comedi_driver *comedi_driver,
		struct pci_driver *pci_driver)
{
	int ret;

	ret = comedi_driver_register(comedi_driver);
	if (ret < 0)
		return ret;

	/* FIXME: Remove this test after auditing all comedi pci drivers */
	if (!pci_driver->name)
		pci_driver->name = comedi_driver->driver_name;

	ret = pci_register_driver(pci_driver);
	if (ret < 0) {
		comedi_driver_unregister(comedi_driver);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_pci_driver_register);

void comedi_pci_driver_unregister(struct comedi_driver *comedi_driver,
		struct pci_driver *pci_driver)
{
	pci_unregister_driver(pci_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_pci_driver_unregister);

#if IS_ENABLED(CONFIG_USB)

int comedi_usb_driver_register(struct comedi_driver *comedi_driver,
		struct usb_driver *usb_driver)
{
	int ret;

	ret = comedi_driver_register(comedi_driver);
	if (ret < 0)
		return ret;

	ret = usb_register(usb_driver);
	if (ret < 0) {
		comedi_driver_unregister(comedi_driver);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_usb_driver_register);

void comedi_usb_driver_unregister(struct comedi_driver *comedi_driver,
		struct usb_driver *usb_driver)
{
	usb_deregister(usb_driver);
	comedi_driver_unregister(comedi_driver);
}
EXPORT_SYMBOL_GPL(comedi_usb_driver_unregister);

#endif

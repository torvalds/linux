/*
    include/linux/comedidev.h
    header file for kernel-only structures, variables, and constants

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

#ifndef _COMEDIDEV_H
#define _COMEDIDEV_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/timer.h>
#include <linux/pci.h>

#include "comedi.h"

#define DPRINTK(format, args...)	do {		\
	if (comedi_debug)				\
		pr_debug("comedi: " format, ## args);	\
} while (0)

#define COMEDI_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define COMEDI_VERSION_CODE COMEDI_VERSION(COMEDI_MAJORVERSION, \
	COMEDI_MINORVERSION, COMEDI_MICROVERSION)
#define COMEDI_RELEASE VERSION

#define PCI_VENDOR_ID_ADLINK		0x144a
#define PCI_VENDOR_ID_ICP		0x104c
#define PCI_VENDOR_ID_CONTEC		0x1221

#define COMEDI_NUM_MINORS 0x100
#define COMEDI_NUM_BOARD_MINORS 0x30
#define COMEDI_FIRST_SUBDEVICE_MINOR COMEDI_NUM_BOARD_MINORS

struct comedi_subdevice {
	struct comedi_device *device;
	int type;
	int n_chan;
	int subdev_flags;
	int len_chanlist;	/* maximum length of channel/gain list */

	void *private;

	struct comedi_async *async;

	void *lock;
	void *busy;
	unsigned runflags;
	spinlock_t spin_lock;

	unsigned int io_bits;

	unsigned int maxdata;	/* if maxdata==0, use list */
	const unsigned int *maxdata_list;	/* list is channel specific */

	unsigned int flags;
	const unsigned int *flaglist;

	unsigned int settling_time_0;

	const struct comedi_lrange *range_table;
	const struct comedi_lrange *const *range_table_list;

	unsigned int *chanlist;	/* driver-owned chanlist (not used) */

	int (*insn_read) (struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);
	int (*insn_write) (struct comedi_device *, struct comedi_subdevice *,
			   struct comedi_insn *, unsigned int *);
	int (*insn_bits) (struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);
	int (*insn_config) (struct comedi_device *, struct comedi_subdevice *,
			    struct comedi_insn *, unsigned int *);

	int (*do_cmd) (struct comedi_device *, struct comedi_subdevice *);
	int (*do_cmdtest) (struct comedi_device *, struct comedi_subdevice *,
			   struct comedi_cmd *);
	int (*poll) (struct comedi_device *, struct comedi_subdevice *);
	int (*cancel) (struct comedi_device *, struct comedi_subdevice *);
	/* int (*do_lock)(struct comedi_device *, struct comedi_subdevice *); */
	/* int (*do_unlock)(struct comedi_device *, \
			struct comedi_subdevice *); */

	/* called when the buffer changes */
	int (*buf_change) (struct comedi_device *dev,
			   struct comedi_subdevice *s, unsigned long new_size);

	void (*munge) (struct comedi_device *dev, struct comedi_subdevice *s,
		       void *data, unsigned int num_bytes,
		       unsigned int start_chan_index);
	enum dma_data_direction async_dma_dir;

	unsigned int state;

	struct device *class_dev;
	int minor;
};

struct comedi_buf_page {
	void *virt_addr;
	dma_addr_t dma_addr;
};

struct comedi_async {
	struct comedi_subdevice *subdevice;

	void *prealloc_buf;	/* pre-allocated buffer */
	unsigned int prealloc_bufsz;	/* buffer size, in bytes */
	/* virtual and dma address of each page */
	struct comedi_buf_page *buf_page_list;
	unsigned n_buf_pages;	/* num elements in buf_page_list */

	unsigned int max_bufsize;	/* maximum buffer size, bytes */
	/* current number of mmaps of prealloc_buf */
	unsigned int mmap_count;

	/* byte count for writer (write completed) */
	unsigned int buf_write_count;
	/* byte count for writer (allocated for writing) */
	unsigned int buf_write_alloc_count;
	/* byte count for reader (read completed) */
	unsigned int buf_read_count;
	/* byte count for reader (allocated for reading) */
	unsigned int buf_read_alloc_count;

	unsigned int buf_write_ptr;	/* buffer marker for writer */
	unsigned int buf_read_ptr;	/* buffer marker for reader */

	unsigned int cur_chan;	/* useless channel marker for interrupt */
	/* number of bytes that have been received for current scan */
	unsigned int scan_progress;
	/* keeps track of where we are in chanlist as for munging */
	unsigned int munge_chan;
	/* number of bytes that have been munged */
	unsigned int munge_count;
	/* buffer marker for munging */
	unsigned int munge_ptr;

	unsigned int events;	/* events that have occurred */

	struct comedi_cmd cmd;

	wait_queue_head_t wait_head;

	/* callback stuff */
	unsigned int cb_mask;
	int (*cb_func) (unsigned int flags, void *);
	void *cb_arg;

	int (*inttrig) (struct comedi_device *dev, struct comedi_subdevice *s,
			unsigned int x);
};

struct usb_interface;

struct comedi_driver {
	struct comedi_driver *next;

	const char *driver_name;
	struct module *module;
	int (*attach) (struct comedi_device *, struct comedi_devconfig *);
	void (*detach) (struct comedi_device *);
	int (*attach_pci) (struct comedi_device *, struct pci_dev *);
	int (*attach_usb) (struct comedi_device *, struct usb_interface *);

	/* number of elements in board_name and board_id arrays */
	unsigned int num_names;
	const char *const *board_name;
	/* offset in bytes from one board name pointer to the next */
	int offset;
};

struct comedi_device {
	int use_count;
	struct comedi_driver *driver;
	void *private;

	struct device *class_dev;
	int minor;
	/* hw_dev is passed to dma_alloc_coherent when allocating async buffers
	 * for subdevices that have async_dma_dir set to something other than
	 * DMA_NONE */
	struct device *hw_dev;

	const char *board_name;
	const void *board_ptr;
	int attached;
	spinlock_t spinlock;
	struct mutex mutex;
	int in_request_module;

	int n_subdevices;
	struct comedi_subdevice *subdevices;

	/* dumb */
	unsigned long iobase;
	unsigned int irq;

	struct comedi_subdevice *read_subdev;
	struct comedi_subdevice *write_subdev;

	struct fasync_struct *async_queue;

	int (*open) (struct comedi_device *dev);
	void (*close) (struct comedi_device *dev);
};

static inline const void *comedi_board(struct comedi_device *dev)
{
	return dev->board_ptr;
}

struct comedi_device_file_info {
	struct comedi_device *device;
	struct comedi_subdevice *read_subdevice;
	struct comedi_subdevice *write_subdevice;
	struct device *hardware_device;
};

#ifdef CONFIG_COMEDI_DEBUG
extern int comedi_debug;
#else
static const int comedi_debug;
#endif

/*
 * function prototypes
 */

void comedi_event(struct comedi_device *dev, struct comedi_subdevice *s);
void comedi_error(const struct comedi_device *dev, const char *s);

/* we can expand the number of bits used to encode devices/subdevices into
 the minor number soon, after more distros support > 8 bit minor numbers
 (like after Debian Etch gets released) */
enum comedi_minor_bits {
	COMEDI_DEVICE_MINOR_MASK = 0xf,
	COMEDI_SUBDEVICE_MINOR_MASK = 0xf0
};
static const unsigned COMEDI_SUBDEVICE_MINOR_SHIFT = 4;
static const unsigned COMEDI_SUBDEVICE_MINOR_OFFSET = 1;

struct comedi_device_file_info *comedi_get_device_file_info(unsigned minor);

static inline struct comedi_subdevice *comedi_get_read_subdevice(
	const struct comedi_device_file_info *info)
{
	if (info->read_subdevice)
		return info->read_subdevice;
	if (info->device == NULL)
		return NULL;
	return info->device->read_subdev;
}

static inline struct comedi_subdevice *comedi_get_write_subdevice(
	const struct comedi_device_file_info *info)
{
	if (info->write_subdevice)
		return info->write_subdevice;
	if (info->device == NULL)
		return NULL;
	return info->device->write_subdev;
}

int comedi_alloc_subdevices(struct comedi_device *, int);

void comedi_device_detach(struct comedi_device *dev);
int comedi_device_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
int comedi_driver_register(struct comedi_driver *);
int comedi_driver_unregister(struct comedi_driver *);

/**
 * module_comedi_driver() - Helper macro for registering a comedi driver
 * @__comedi_driver: comedi_driver struct
 *
 * Helper macro for comedi drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only use
 * this macro once, and calling it replaces module_init() and module_exit().
 */
#define module_comedi_driver(__comedi_driver) \
	module_driver(__comedi_driver, comedi_driver_register, \
			comedi_driver_unregister)

int comedi_pci_enable(struct pci_dev *, const char *);
void comedi_pci_disable(struct pci_dev *);

int comedi_pci_driver_register(struct comedi_driver *, struct pci_driver *);
void comedi_pci_driver_unregister(struct comedi_driver *, struct pci_driver *);

/**
 * module_comedi_pci_driver() - Helper macro for registering a comedi PCI driver
 * @__comedi_driver: comedi_driver struct
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for comedi PCI drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_pci_driver(__comedi_driver, __pci_driver) \
	module_driver(__comedi_driver, comedi_pci_driver_register, \
			comedi_pci_driver_unregister, &(__pci_driver))

struct usb_driver;

int comedi_usb_driver_register(struct comedi_driver *, struct usb_driver *);
void comedi_usb_driver_unregister(struct comedi_driver *, struct usb_driver *);

/**
 * module_comedi_usb_driver() - Helper macro for registering a comedi USB driver
 * @__comedi_driver: comedi_driver struct
 * @__usb_driver: usb_driver struct
 *
 * Helper macro for comedi USB drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_usb_driver(__comedi_driver, __usb_driver) \
	module_driver(__comedi_driver, comedi_usb_driver_register, \
			comedi_usb_driver_unregister, &(__usb_driver))

void init_polling(void);
void cleanup_polling(void);
void start_polling(struct comedi_device *);
void stop_polling(struct comedi_device *);

#ifdef CONFIG_PROC_FS
void comedi_proc_init(void);
void comedi_proc_cleanup(void);
#else
static inline void comedi_proc_init(void)
{
}

static inline void comedi_proc_cleanup(void)
{
}
#endif

/* subdevice runflags */
enum subdevice_runflags {
	SRF_USER = 0x00000001,
	SRF_RT = 0x00000002,
	/* indicates an COMEDI_CB_ERROR event has occurred since the last
	 * command was started */
	SRF_ERROR = 0x00000004,
	SRF_RUNNING = 0x08000000
};

int comedi_check_chanlist(struct comedi_subdevice *s,
			  int n,
			  unsigned int *chanlist);
unsigned comedi_get_subdevice_runflags(struct comedi_subdevice *s);

/* range stuff */

#define RANGE(a, b)		{(a)*1e6, (b)*1e6, 0}
#define RANGE_ext(a, b)		{(a)*1e6, (b)*1e6, RF_EXTERNAL}
#define RANGE_mA(a, b)		{(a)*1e6, (b)*1e6, UNIT_mA}
#define RANGE_unitless(a, b)	{(a)*1e6, (b)*1e6, 0}
#define BIP_RANGE(a)		{-(a)*1e6, (a)*1e6, 0}
#define UNI_RANGE(a)		{0, (a)*1e6, 0}

extern const struct comedi_lrange range_bipolar10;
extern const struct comedi_lrange range_bipolar5;
extern const struct comedi_lrange range_bipolar2_5;
extern const struct comedi_lrange range_unipolar10;
extern const struct comedi_lrange range_unipolar5;
extern const struct comedi_lrange range_unknown;

#define range_digital		range_unipolar5

#if __GNUC__ >= 3
#define GCC_ZERO_LENGTH_ARRAY
#else
#define GCC_ZERO_LENGTH_ARRAY 0
#endif

struct comedi_lrange {
	int length;
	struct comedi_krange range[GCC_ZERO_LENGTH_ARRAY];
};

/* some silly little inline functions */

static inline int alloc_private(struct comedi_device *dev, int size)
{
	dev->private = kzalloc(size, GFP_KERNEL);
	if (!dev->private)
		return -ENOMEM;
	return 0;
}

static inline unsigned int bytes_per_sample(const struct comedi_subdevice *subd)
{
	if (subd->subdev_flags & SDF_LSAMPL)
		return sizeof(unsigned int);
	else
		return sizeof(short);
}

/* must be used in attach to set dev->hw_dev if you wish to dma directly
into comedi's buffer */
static inline void comedi_set_hw_dev(struct comedi_device *dev,
				     struct device *hw_dev)
{
	if (dev->hw_dev)
		put_device(dev->hw_dev);

	dev->hw_dev = hw_dev;
	if (dev->hw_dev) {
		dev->hw_dev = get_device(dev->hw_dev);
		BUG_ON(dev->hw_dev == NULL);
	}
}

static inline struct pci_dev *comedi_to_pci_dev(struct comedi_device *dev)
{
	return dev->hw_dev ? to_pci_dev(dev->hw_dev) : NULL;
}

int comedi_buf_put(struct comedi_async *async, short x);
int comedi_buf_get(struct comedi_async *async, short *x);

unsigned int comedi_buf_write_n_available(struct comedi_async *async);
unsigned int comedi_buf_write_alloc(struct comedi_async *async,
				    unsigned int nbytes);
unsigned int comedi_buf_write_alloc_strict(struct comedi_async *async,
					   unsigned int nbytes);
unsigned comedi_buf_write_free(struct comedi_async *async, unsigned int nbytes);
unsigned comedi_buf_read_alloc(struct comedi_async *async, unsigned nbytes);
unsigned comedi_buf_read_free(struct comedi_async *async, unsigned int nbytes);
unsigned int comedi_buf_read_n_available(struct comedi_async *async);
void comedi_buf_memcpy_to(struct comedi_async *async, unsigned int offset,
			  const void *source, unsigned int num_bytes);
void comedi_buf_memcpy_from(struct comedi_async *async, unsigned int offset,
			    void *destination, unsigned int num_bytes);
static inline unsigned comedi_buf_write_n_allocated(struct comedi_async *async)
{
	return async->buf_write_alloc_count - async->buf_write_count;
}

static inline unsigned comedi_buf_read_n_allocated(struct comedi_async *async)
{
	return async->buf_read_alloc_count - async->buf_read_count;
}

static inline void *comedi_aux_data(int options[], int n)
{
	unsigned long address;
	unsigned long addressLow;
	int bit_shift;
	if (sizeof(int) >= sizeof(void *))
		address = options[COMEDI_DEVCONF_AUX_DATA_LO];
	else {
		address = options[COMEDI_DEVCONF_AUX_DATA_HI];
		bit_shift = sizeof(int) * 8;
		address <<= bit_shift;
		addressLow = options[COMEDI_DEVCONF_AUX_DATA_LO];
		addressLow &= (1UL << bit_shift) - 1;
		address |= addressLow;
	}
	if (n >= 1)
		address += options[COMEDI_DEVCONF_AUX_DATA0_LENGTH];
	if (n >= 2)
		address += options[COMEDI_DEVCONF_AUX_DATA1_LENGTH];
	if (n >= 3)
		address += options[COMEDI_DEVCONF_AUX_DATA2_LENGTH];
	BUG_ON(n > 3);
	return (void *)address;
}

int comedi_alloc_subdevice_minor(struct comedi_device *dev,
				 struct comedi_subdevice *s);
void comedi_free_subdevice_minor(struct comedi_subdevice *s);
int comedi_pci_auto_config(struct pci_dev *pcidev,
			   struct comedi_driver *driver);
void comedi_pci_auto_unconfig(struct pci_dev *pcidev);
int comedi_usb_auto_config(struct usb_interface *intf,
			   struct comedi_driver *driver);
void comedi_usb_auto_unconfig(struct usb_interface *intf);

#endif /* _COMEDIDEV_H */

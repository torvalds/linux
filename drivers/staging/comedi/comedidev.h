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
*/

#ifndef _COMEDIDEV_H
#define _COMEDIDEV_H

#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/rwsem.h>
#include <linux/kref.h>

#include "comedi.h"

#define COMEDI_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define COMEDI_VERSION_CODE COMEDI_VERSION(COMEDI_MAJORVERSION, \
	COMEDI_MINORVERSION, COMEDI_MICROVERSION)
#define COMEDI_RELEASE VERSION

#define COMEDI_NUM_BOARD_MINORS 0x30

struct comedi_subdevice {
	struct comedi_device *device;
	int index;
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

	const struct comedi_lrange *range_table;
	const struct comedi_lrange *const *range_table_list;

	unsigned int *chanlist;	/* driver-owned chanlist (not used) */

	int (*insn_read)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*insn_write)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);
	int (*insn_bits)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*insn_config)(struct comedi_device *, struct comedi_subdevice *,
			   struct comedi_insn *, unsigned int *);

	int (*do_cmd)(struct comedi_device *, struct comedi_subdevice *);
	int (*do_cmdtest)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_cmd *);
	int (*poll)(struct comedi_device *, struct comedi_subdevice *);
	int (*cancel)(struct comedi_device *, struct comedi_subdevice *);
	/* int (*do_lock)(struct comedi_device *, struct comedi_subdevice *); */
	/* int (*do_unlock)(struct comedi_device *, \
			struct comedi_subdevice *); */

	/* called when the buffer changes */
	int (*buf_change)(struct comedi_device *dev,
			  struct comedi_subdevice *s, unsigned long new_size);

	void (*munge)(struct comedi_device *dev, struct comedi_subdevice *s,
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

struct comedi_buf_map {
	struct device *dma_hw_dev;
	struct comedi_buf_page *page_list;
	unsigned int n_pages;
	enum dma_data_direction dma_dir;
	struct kref refcount;
};

/**
 * struct comedi_async - control data for asynchronous comedi commands
 * @prealloc_buf:	preallocated buffer
 * @prealloc_bufsz:	buffer size (in bytes)
 * @buf_map:		map of buffer pages
 * @max_bufsize:	maximum buffer size (in bytes)
 * @buf_write_count:	"write completed" count (in bytes, modulo 2**32)
 * @buf_write_alloc_count: "allocated for writing" count (in bytes,
 *			modulo 2**32)
 * @buf_read_count:	"read completed" count (in bytes, modulo 2**32)
 * @buf_read_alloc_count: "allocated for reading" count (in bytes,
 *			modulo 2**32)
 * @buf_write_ptr:	buffer position for writer
 * @buf_read_ptr:	buffer position for reader
 * @cur_chan:		current position in chanlist for scan (for those
 *			drivers that use it)
 * @scan_progress:	amount received or sent for current scan (in bytes)
 * @munge_chan:		current position in chanlist for "munging"
 * @munge_count:	"munge" count (in bytes, modulo 2**32)
 * @munge_ptr:		buffer position for "munging"
 * @events:		bit-vector of events that have occurred
 * @cmd:		details of comedi command in progress
 * @wait_head:		task wait queue for file reader or writer
 * @cb_mask:		bit-vector of events that should wake waiting tasks
 * @inttrig:		software trigger function for command, or NULL
 *
 * Note about the ..._count and ..._ptr members:
 *
 * Think of the _Count values being integers of unlimited size, indexing
 * into a buffer of infinite length (though only an advancing portion
 * of the buffer of fixed length prealloc_bufsz is accessible at any time).
 * Then:
 *
 *   Buf_Read_Count <= Buf_Read_Alloc_Count <= Munge_Count <=
 *   Buf_Write_Count <= Buf_Write_Alloc_Count <=
 *   (Buf_Read_Count + prealloc_bufsz)
 *
 * (Those aren't the actual members, apart from prealloc_bufsz.) When
 * the buffer is reset, those _Count values start at 0 and only increase
 * in value, maintaining the above inequalities until the next time the
 * buffer is reset.  The buffer is divided into the following regions by
 * the inequalities:
 *
 *   [0, Buf_Read_Count):
 *     old region no longer accessible
 *   [Buf_Read_Count, Buf_Read_Alloc_Count):
 *     filled and munged region allocated for reading but not yet read
 *   [Buf_Read_Alloc_Count, Munge_Count):
 *     filled and munged region not yet allocated for reading
 *   [Munge_Count, Buf_Write_Count):
 *     filled region not yet munged
 *   [Buf_Write_Count, Buf_Write_Alloc_Count):
 *     unfilled region allocated for writing but not yet written
 *   [Buf_Write_Alloc_Count, Buf_Read_Count + prealloc_bufsz):
 *     unfilled region not yet allocated for writing
 *   [Buf_Read_Count + prealloc_bufsz, infinity):
 *     unfilled region not yet accessible
 *
 * Data needs to be written into the buffer before it can be read out,
 * and may need to be converted (or "munged") between the two
 * operations.  Extra unfilled buffer space may need to allocated for
 * writing (advancing Buf_Write_Alloc_Count) before new data is written.
 * After writing new data, the newly filled space needs to be released
 * (advancing Buf_Write_Count).  This also results in the new data being
 * "munged" (advancing Munge_Count).  Before data is read out of the
 * buffer, extra space may need to be allocated for reading (advancing
 * Buf_Read_Alloc_Count).  After the data has been read out, the space
 * needs to be released (advancing Buf_Read_Count).
 *
 * The actual members, buf_read_count, buf_read_alloc_count,
 * munge_count, buf_write_count, and buf_write_alloc_count take the
 * value of the corresponding capitalized _Count values modulo 2^32
 * (UINT_MAX+1).  Subtracting a "higher" _count value from a "lower"
 * _count value gives the same answer as subtracting a "higher" _Count
 * value from a lower _Count value because prealloc_bufsz < UINT_MAX+1.
 * The modulo operation is done implicitly.
 *
 * The buf_read_ptr, munge_ptr, and buf_write_ptr members take the value
 * of the corresponding capitalized _Count values modulo prealloc_bufsz.
 * These correspond to byte indices in the physical buffer.  The modulo
 * operation is done by subtracting prealloc_bufsz when the value
 * exceeds prealloc_bufsz (assuming prealloc_bufsz plus the increment is
 * less than or equal to UINT_MAX).
 */
struct comedi_async {
	void *prealloc_buf;
	unsigned int prealloc_bufsz;
	struct comedi_buf_map *buf_map;
	unsigned int max_bufsize;
	unsigned int buf_write_count;
	unsigned int buf_write_alloc_count;
	unsigned int buf_read_count;
	unsigned int buf_read_alloc_count;
	unsigned int buf_write_ptr;
	unsigned int buf_read_ptr;
	unsigned int cur_chan;
	unsigned int scan_progress;
	unsigned int munge_chan;
	unsigned int munge_count;
	unsigned int munge_ptr;
	unsigned int events;
	struct comedi_cmd cmd;
	wait_queue_head_t wait_head;
	unsigned int cb_mask;
	int (*inttrig)(struct comedi_device *dev, struct comedi_subdevice *s,
		       unsigned int x);
};

struct comedi_driver {
	struct comedi_driver *next;

	const char *driver_name;
	struct module *module;
	int (*attach)(struct comedi_device *, struct comedi_devconfig *);
	void (*detach)(struct comedi_device *);
	int (*auto_attach)(struct comedi_device *, unsigned long);

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
	unsigned int detach_count;
	/* hw_dev is passed to dma_alloc_coherent when allocating async buffers
	 * for subdevices that have async_dma_dir set to something other than
	 * DMA_NONE */
	struct device *hw_dev;

	const char *board_name;
	const void *board_ptr;
	bool attached:1;
	bool ioenabled:1;
	spinlock_t spinlock;
	struct mutex mutex;
	struct rw_semaphore attach_lock;
	struct kref refcount;

	int n_subdevices;
	struct comedi_subdevice *subdevices;

	/* dumb */
	unsigned long iobase;
	unsigned long iolen;
	unsigned int irq;

	struct comedi_subdevice *read_subdev;
	struct comedi_subdevice *write_subdev;

	struct fasync_struct *async_queue;

	int (*open)(struct comedi_device *dev);
	void (*close)(struct comedi_device *dev);
};

static inline const void *comedi_board(const struct comedi_device *dev)
{
	return dev->board_ptr;
}

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

struct comedi_device *comedi_dev_get_from_minor(unsigned minor);
int comedi_dev_put(struct comedi_device *dev);

void init_polling(void);
void cleanup_polling(void);
void start_polling(struct comedi_device *);
void stop_polling(struct comedi_device *);

/* subdevice runflags */
enum subdevice_runflags {
	SRF_RT = 0x00000002,
	/* indicates an COMEDI_CB_ERROR event has occurred since the last
	 * command was started */
	SRF_ERROR = 0x00000004,
	SRF_RUNNING = 0x08000000,
	SRF_FREE_SPRIV = 0x80000000,	/* free s->private on detach */
};

bool comedi_is_subdevice_running(struct comedi_subdevice *s);

void *comedi_alloc_spriv(struct comedi_subdevice *s, size_t size);

int comedi_check_chanlist(struct comedi_subdevice *s,
			  int n,
			  unsigned int *chanlist);

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
extern const struct comedi_lrange range_unipolar2_5;
extern const struct comedi_lrange range_0_20mA;
extern const struct comedi_lrange range_4_20mA;
extern const struct comedi_lrange range_0_32mA;
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

static inline bool comedi_range_is_bipolar(struct comedi_subdevice *s,
					   unsigned int range)
{
	return s->range_table->range[range].min < 0;
}

static inline bool comedi_range_is_unipolar(struct comedi_subdevice *s,
					    unsigned int range)
{
	return s->range_table->range[range].min >= 0;
}

static inline bool comedi_range_is_external(struct comedi_subdevice *s,
					    unsigned int range)
{
	return !!(s->range_table->range[range].flags & RF_EXTERNAL);
}

static inline bool comedi_chan_range_is_bipolar(struct comedi_subdevice *s,
						unsigned int chan,
						unsigned int range)
{
	return s->range_table_list[chan]->range[range].min < 0;
}

static inline bool comedi_chan_range_is_unipolar(struct comedi_subdevice *s,
						 unsigned int chan,
						 unsigned int range)
{
	return s->range_table_list[chan]->range[range].min >= 0;
}

static inline bool comedi_chan_range_is_external(struct comedi_subdevice *s,
						 unsigned int chan,
						 unsigned int range)
{
	return !!(s->range_table_list[chan]->range[range].flags & RF_EXTERNAL);
}

/* munge between offset binary and two's complement values */
static inline unsigned int comedi_offset_munge(struct comedi_subdevice *s,
					       unsigned int val)
{
	return val ^ s->maxdata ^ (s->maxdata >> 1);
}

static inline unsigned int bytes_per_sample(const struct comedi_subdevice *subd)
{
	if (subd->subdev_flags & SDF_LSAMPL)
		return sizeof(unsigned int);

	return sizeof(short);
}

/*
 * Must set dev->hw_dev if you wish to dma directly into comedi's buffer.
 * Also useful for retrieving a previously configured hardware device of
 * known bus type.  Set automatically for auto-configured devices.
 * Automatically set to NULL when detaching hardware device.
 */
int comedi_set_hw_dev(struct comedi_device *dev, struct device *hw_dev);

static inline unsigned int comedi_buf_n_bytes_ready(struct comedi_subdevice *s)
{
	return s->async->buf_write_count - s->async->buf_read_count;
}

unsigned int comedi_buf_write_alloc(struct comedi_subdevice *s, unsigned int n);
unsigned int comedi_buf_write_free(struct comedi_subdevice *s, unsigned int n);

unsigned int comedi_buf_read_n_available(struct comedi_subdevice *s);
unsigned int comedi_buf_read_alloc(struct comedi_subdevice *s, unsigned int n);
unsigned int comedi_buf_read_free(struct comedi_subdevice *s, unsigned int n);

int comedi_buf_put(struct comedi_subdevice *s, unsigned short x);
int comedi_buf_get(struct comedi_subdevice *s, unsigned short *x);

void comedi_buf_memcpy_to(struct comedi_subdevice *s, unsigned int offset,
			  const void *source, unsigned int num_bytes);
void comedi_buf_memcpy_from(struct comedi_subdevice *s, unsigned int offset,
			    void *destination, unsigned int num_bytes);

/* drivers.c - general comedi driver functions */

#define COMEDI_TIMEOUT_MS	1000

int comedi_timeout(struct comedi_device *, struct comedi_subdevice *,
		   struct comedi_insn *,
		   int (*cb)(struct comedi_device *, struct comedi_subdevice *,
			     struct comedi_insn *, unsigned long context),
		   unsigned long context);

int comedi_dio_insn_config(struct comedi_device *, struct comedi_subdevice *,
			   struct comedi_insn *, unsigned int *data,
			   unsigned int mask);
unsigned int comedi_dio_update_state(struct comedi_subdevice *,
				     unsigned int *data);

void *comedi_alloc_devpriv(struct comedi_device *, size_t);
int comedi_alloc_subdevices(struct comedi_device *, int);

int comedi_load_firmware(struct comedi_device *, struct device *,
			 const char *name,
			 int (*cb)(struct comedi_device *,
				   const u8 *data, size_t size,
				   unsigned long context),
			 unsigned long context);

int __comedi_request_region(struct comedi_device *,
			    unsigned long start, unsigned long len);
int comedi_request_region(struct comedi_device *,
			  unsigned long start, unsigned long len);
void comedi_legacy_detach(struct comedi_device *);

int comedi_auto_config(struct device *, struct comedi_driver *,
		       unsigned long context);
void comedi_auto_unconfig(struct device *);

int comedi_driver_register(struct comedi_driver *);
void comedi_driver_unregister(struct comedi_driver *);

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

#ifdef CONFIG_COMEDI_PCI_DRIVERS

/* comedi_pci.c - comedi PCI driver specific functions */

/*
 * PCI Vendor IDs not in <linux/pci_ids.h>
 */
#define PCI_VENDOR_ID_KOLTER		0x1001
#define PCI_VENDOR_ID_ICP		0x104c
#define PCI_VENDOR_ID_DT		0x1116
#define PCI_VENDOR_ID_IOTECH		0x1616
#define PCI_VENDOR_ID_CONTEC		0x1221
#define PCI_VENDOR_ID_RTD		0x1435
#define PCI_VENDOR_ID_HUMUSOFT		0x186c

struct pci_dev;
struct pci_driver;

struct pci_dev *comedi_to_pci_dev(struct comedi_device *);

int comedi_pci_enable(struct comedi_device *);
void comedi_pci_disable(struct comedi_device *);

int comedi_pci_auto_config(struct pci_dev *, struct comedi_driver *,
			   unsigned long context);
void comedi_pci_auto_unconfig(struct pci_dev *);

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

#else

/*
 * Some of the comedi mixed ISA/PCI drivers call the PCI specific
 * functions. Provide some dummy functions if CONFIG_COMEDI_PCI_DRIVERS
 * is not enabled.
 */

static inline struct pci_dev *comedi_to_pci_dev(struct comedi_device *dev)
{
	return NULL;
}

static inline int comedi_pci_enable(struct comedi_device *dev)
{
	return -ENOSYS;
}

static inline void comedi_pci_disable(struct comedi_device *dev)
{
}

#endif /* CONFIG_COMEDI_PCI_DRIVERS */

#ifdef CONFIG_COMEDI_PCMCIA_DRIVERS

/* comedi_pcmcia.c - comedi PCMCIA driver specific functions */

struct pcmcia_driver;
struct pcmcia_device;

struct pcmcia_device *comedi_to_pcmcia_dev(struct comedi_device *);

int comedi_pcmcia_enable(struct comedi_device *,
			 int (*conf_check)(struct pcmcia_device *, void *));
void comedi_pcmcia_disable(struct comedi_device *);

int comedi_pcmcia_auto_config(struct pcmcia_device *, struct comedi_driver *);
void comedi_pcmcia_auto_unconfig(struct pcmcia_device *);

int comedi_pcmcia_driver_register(struct comedi_driver *,
					struct pcmcia_driver *);
void comedi_pcmcia_driver_unregister(struct comedi_driver *,
					struct pcmcia_driver *);

/**
 * module_comedi_pcmcia_driver() - Helper macro for registering a comedi PCMCIA driver
 * @__comedi_driver: comedi_driver struct
 * @__pcmcia_driver: pcmcia_driver struct
 *
 * Helper macro for comedi PCMCIA drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_pcmcia_driver(__comedi_driver, __pcmcia_driver) \
	module_driver(__comedi_driver, comedi_pcmcia_driver_register, \
			comedi_pcmcia_driver_unregister, &(__pcmcia_driver))

#endif /* CONFIG_COMEDI_PCMCIA_DRIVERS */

#ifdef CONFIG_COMEDI_USB_DRIVERS

/* comedi_usb.c - comedi USB driver specific functions */

struct usb_driver;
struct usb_interface;

struct usb_interface *comedi_to_usb_interface(struct comedi_device *);
struct usb_device *comedi_to_usb_dev(struct comedi_device *);

int comedi_usb_auto_config(struct usb_interface *, struct comedi_driver *,
			   unsigned long context);
void comedi_usb_auto_unconfig(struct usb_interface *);

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

#endif /* CONFIG_COMEDI_USB_DRIVERS */

#endif /* _COMEDIDEV_H */

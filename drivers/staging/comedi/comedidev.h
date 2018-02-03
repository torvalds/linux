// SPDX-License-Identifier: GPL-2.0+
/*
 * comedidev.h
 * header file for kernel-only structures, variables, and constants
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
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

/**
 * struct comedi_subdevice - Working data for a COMEDI subdevice
 * @device: COMEDI device to which this subdevice belongs.  (Initialized by
 *	comedi_alloc_subdevices().)
 * @index: Index of this subdevice within device's array of subdevices.
 *	(Initialized by comedi_alloc_subdevices().)
 * @type: Type of subdevice from &enum comedi_subdevice_type.  (Initialized by
 *	the low-level driver.)
 * @n_chan: Number of channels the subdevice supports.  (Initialized by the
 *	low-level driver.)
 * @subdev_flags: Various "SDF" flags indicating aspects of the subdevice to
 *	the COMEDI core and user application.  (Initialized by the low-level
 *	driver.)
 * @len_chanlist: Maximum length of a channel list if the subdevice supports
 *	asynchronous acquisition commands.  (Optionally initialized by the
 *	low-level driver, or changed from 0 to 1 during post-configuration.)
 * @private: Private data pointer which is either set by the low-level driver
 *	itself, or by a call to comedi_alloc_spriv() which allocates storage.
 *	In the latter case, the storage is automatically freed after the
 *	low-level driver's "detach" handler is called for the device.
 *	(Initialized by the low-level driver.)
 * @async: Pointer to &struct comedi_async id the subdevice supports
 *	asynchronous acquisition commands.  (Allocated and initialized during
 *	post-configuration if needed.)
 * @lock: Pointer to a file object that performed a %COMEDI_LOCK ioctl on the
 *	subdevice.  (Initially NULL.)
 * @busy: Pointer to a file object that is performing an asynchronous
 *	acquisition command on the subdevice.  (Initially NULL.)
 * @runflags: Internal flags for use by COMEDI core, mostly indicating whether
 *	an asynchronous acquisition command is running.
 * @spin_lock: Generic spin-lock for use by the COMEDI core and the low-level
 *	driver.  (Initialized by comedi_alloc_subdevices().)
 * @io_bits: Bit-mask indicating the channel directions for a DIO subdevice
 *	with no more than 32 channels.  A '1' at a bit position indicates the
 *	corresponding channel is configured as an output.  (Initialized by the
 *	low-level driver for a DIO subdevice.  Forced to all-outputs during
 *	post-configuration for a digital output subdevice.)
 * @maxdata: If non-zero, this is the maximum raw data value of each channel.
 *	If zero, the maximum data value is channel-specific.  (Initialized by
 *	the low-level driver.)
 * @maxdata_list: If the maximum data value is channel-specific, this points
 *	to an array of maximum data values indexed by channel index.
 *	(Initialized by the low-level driver.)
 * @range_table: If non-NULL, this points to a COMEDI range table for the
 *	subdevice.  If NULL, the range table is channel-specific.  (Initialized
 *	by the low-level driver, will be set to an "invalid" range table during
 *	post-configuration if @range_table and @range_table_list are both
 *	NULL.)
 * @range_table_list: If the COMEDI range table is channel-specific, this
 *	points to an array of pointers to COMEDI range tables indexed by
 *	channel number.  (Initialized by the low-level driver.)
 * @chanlist: Not used.
 * @insn_read: Optional pointer to a handler for the %INSN_READ instruction.
 *	(Initialized by the low-level driver, or set to a default handler
 *	during post-configuration.)
 * @insn_write: Optional pointer to a handler for the %INSN_WRITE instruction.
 *	(Initialized by the low-level driver, or set to a default handler
 *	during post-configuration.)
 * @insn_bits: Optional pointer to a handler for the %INSN_BITS instruction
 *	for a digital input, digital output or digital input/output subdevice.
 *	(Initialized by the low-level driver, or set to a default handler
 *	during post-configuration.)
 * @insn_config: Optional pointer to a handler for the %INSN_CONFIG
 *	instruction.  (Initialized by the low-level driver, or set to a default
 *	handler during post-configuration.)
 * @do_cmd: If the subdevice supports asynchronous acquisition commands, this
 *	points to a handler to set it up in hardware.  (Initialized by the
 *	low-level driver.)
 * @do_cmdtest: If the subdevice supports asynchronous acquisition commands,
 *	this points to a handler used to check and possibly tweak a prospective
 *	acquisition command without setting it up in hardware.  (Initialized by
 *	the low-level driver.)
 * @poll: If the subdevice supports asynchronous acquisition commands, this
 *	is an optional pointer to a handler for the %COMEDI_POLL ioctl which
 *	instructs the low-level driver to synchronize buffers.  (Initialized by
 *	the low-level driver if needed.)
 * @cancel: If the subdevice supports asynchronous acquisition commands, this
 *	points to a handler used to terminate a running command.  (Initialized
 *	by the low-level driver.)
 * @buf_change: If the subdevice supports asynchronous acquisition commands,
 *	this is an optional pointer to a handler that is called when the data
 *	buffer for handling asynchronous commands is allocated or reallocated.
 *	(Initialized by the low-level driver if needed.)
 * @munge: If the subdevice supports asynchronous acquisition commands and
 *	uses DMA to transfer data from the hardware to the acquisition buffer,
 *	this points to a function used to "munge" the data values from the
 *	hardware into the format expected by COMEDI.  (Initialized by the
 *	low-level driver if needed.)
 * @async_dma_dir: If the subdevice supports asynchronous acquisition commands
 *	and uses DMA to transfer data from the hardware to the acquisition
 *	buffer, this sets the DMA direction for the buffer. (initialized to
 *	%DMA_NONE by comedi_alloc_subdevices() and changed by the low-level
 *	driver if necessary.)
 * @state: Handy bit-mask indicating the output states for a DIO or digital
 *	output subdevice with no more than 32 channels. (Initialized by the
 *	low-level driver.)
 * @class_dev: If the subdevice supports asynchronous acquisition commands,
 *	this points to a sysfs comediX_subdY device where X is the minor device
 *	number of the COMEDI device and Y is the subdevice number.  The minor
 *	device number for the sysfs device is allocated dynamically in the
 *	range 48 to 255.  This is used to allow the COMEDI device to be opened
 *	with a different default read or write subdevice.  (Allocated during
 *	post-configuration if needed.)
 * @minor: If @class_dev is set, this is its dynamically allocated minor
 *	device number.  (Set during post-configuration if necessary.)
 * @readback: Optional pointer to memory allocated by
 *	comedi_alloc_subdev_readback() used to hold the values written to
 *	analog output channels so they can be read back.  The storage is
 *	automatically freed after the low-level driver's "detach" handler is
 *	called for the device.  (Initialized by the low-level driver.)
 *
 * This is the main control structure for a COMEDI subdevice.  If the subdevice
 * supports asynchronous acquisition commands, additional information is stored
 * in the &struct comedi_async pointed to by @async.
 *
 * Most of the subdevice is initialized by the low-level driver's "attach" or
 * "auto_attach" handlers but parts of it are initialized by
 * comedi_alloc_subdevices(), and other parts are initialized during
 * post-configuration on return from that handler.
 *
 * A low-level driver that sets @insn_bits for a digital input, digital output,
 * or DIO subdevice may leave @insn_read and @insn_write uninitialized, in
 * which case they will be set to a default handler during post-configuration
 * that uses @insn_bits to emulate the %INSN_READ and %INSN_WRITE instructions.
 */
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
	unsigned int runflags;
	spinlock_t spin_lock;	/* generic spin-lock for COMEDI and drivers */

	unsigned int io_bits;

	unsigned int maxdata;	/* if maxdata==0, use list */
	const unsigned int *maxdata_list;	/* list is channel specific */

	const struct comedi_lrange *range_table;
	const struct comedi_lrange *const *range_table_list;

	unsigned int *chanlist;	/* driver-owned chanlist (not used) */

	int (*insn_read)(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data);
	int (*insn_write)(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data);
	int (*insn_bits)(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data);
	int (*insn_config)(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data);

	int (*do_cmd)(struct comedi_device *dev, struct comedi_subdevice *s);
	int (*do_cmdtest)(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_cmd *cmd);
	int (*poll)(struct comedi_device *dev, struct comedi_subdevice *s);
	int (*cancel)(struct comedi_device *dev, struct comedi_subdevice *s);

	/* called when the buffer changes */
	int (*buf_change)(struct comedi_device *dev,
			  struct comedi_subdevice *s);

	void (*munge)(struct comedi_device *dev, struct comedi_subdevice *s,
		      void *data, unsigned int num_bytes,
		      unsigned int start_chan_index);
	enum dma_data_direction async_dma_dir;

	unsigned int state;

	struct device *class_dev;
	int minor;

	unsigned int *readback;
};

/**
 * struct comedi_buf_page - Describe a page of a COMEDI buffer
 * @virt_addr: Kernel address of page.
 * @dma_addr: DMA address of page if in DMA coherent memory.
 */
struct comedi_buf_page {
	void *virt_addr;
	dma_addr_t dma_addr;
};

/**
 * struct comedi_buf_map - Describe pages in a COMEDI buffer
 * @dma_hw_dev: Low-level hardware &struct device pointer copied from the
 *	COMEDI device's hw_dev member.
 * @page_list: Pointer to array of &struct comedi_buf_page, one for each
 *	page in the buffer.
 * @n_pages: Number of pages in the buffer.
 * @dma_dir: DMA direction used to allocate pages of DMA coherent memory,
 *	or %DMA_NONE if pages allocated from regular memory.
 * @refcount: &struct kref reference counter used to free the buffer.
 *
 * A COMEDI data buffer is allocated as individual pages, either in
 * conventional memory or DMA coherent memory, depending on the attached,
 * low-level hardware device.  (The buffer pages also get mapped into the
 * kernel's contiguous virtual address space pointed to by the 'prealloc_buf'
 * member of &struct comedi_async.)
 *
 * The buffer is normally freed when the COMEDI device is detached from the
 * low-level driver (which may happen due to device removal), but if it happens
 * to be mmapped at the time, the pages cannot be freed until the buffer has
 * been munmapped.  That is what the reference counter is for.  (The virtual
 * address space pointed by 'prealloc_buf' is freed when the COMEDI device is
 * detached.)
 */
struct comedi_buf_map {
	struct device *dma_hw_dev;
	struct comedi_buf_page *page_list;
	unsigned int n_pages;
	enum dma_data_direction dma_dir;
	struct kref refcount;
};

/**
 * struct comedi_async - Control data for asynchronous COMEDI commands
 * @prealloc_buf: Kernel virtual address of allocated acquisition buffer.
 * @prealloc_bufsz: Buffer size (in bytes).
 * @buf_map: Map of buffer pages.
 * @max_bufsize: Maximum allowed buffer size (in bytes).
 * @buf_write_count: "Write completed" count (in bytes, modulo 2**32).
 * @buf_write_alloc_count: "Allocated for writing" count (in bytes,
 *	modulo 2**32).
 * @buf_read_count: "Read completed" count (in bytes, modulo 2**32).
 * @buf_read_alloc_count: "Allocated for reading" count (in bytes,
 *	modulo 2**32).
 * @buf_write_ptr: Buffer position for writer.
 * @buf_read_ptr: Buffer position for reader.
 * @cur_chan: Current position in chanlist for scan (for those drivers that
 *	use it).
 * @scans_done: The number of scans completed.
 * @scan_progress: Amount received or sent for current scan (in bytes).
 * @munge_chan: Current position in chanlist for "munging".
 * @munge_count: "Munge" count (in bytes, modulo 2**32).
 * @munge_ptr: Buffer position for "munging".
 * @events: Bit-vector of events that have occurred.
 * @cmd: Details of comedi command in progress.
 * @wait_head: Task wait queue for file reader or writer.
 * @cb_mask: Bit-vector of events that should wake waiting tasks.
 * @inttrig: Software trigger function for command, or NULL.
 *
 * Note about the ..._count and ..._ptr members:
 *
 * Think of the _Count values being integers of unlimited size, indexing
 * into a buffer of infinite length (though only an advancing portion
 * of the buffer of fixed length prealloc_bufsz is accessible at any
 * time).  Then:
 *
 *   Buf_Read_Count <= Buf_Read_Alloc_Count <= Munge_Count <=
 *   Buf_Write_Count <= Buf_Write_Alloc_Count <=
 *   (Buf_Read_Count + prealloc_bufsz)
 *
 * (Those aren't the actual members, apart from prealloc_bufsz.) When the
 * buffer is reset, those _Count values start at 0 and only increase in value,
 * maintaining the above inequalities until the next time the buffer is
 * reset.  The buffer is divided into the following regions by the inequalities:
 *
 *   [0, Buf_Read_Count):
 *     old region no longer accessible
 *
 *   [Buf_Read_Count, Buf_Read_Alloc_Count):
 *     filled and munged region allocated for reading but not yet read
 *
 *   [Buf_Read_Alloc_Count, Munge_Count):
 *     filled and munged region not yet allocated for reading
 *
 *   [Munge_Count, Buf_Write_Count):
 *     filled region not yet munged
 *
 *   [Buf_Write_Count, Buf_Write_Alloc_Count):
 *     unfilled region allocated for writing but not yet written
 *
 *   [Buf_Write_Alloc_Count, Buf_Read_Count + prealloc_bufsz):
 *     unfilled region not yet allocated for writing
 *
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
	unsigned int scans_done;
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

/**
 * enum comedi_cb - &struct comedi_async callback "events"
 * @COMEDI_CB_EOS:		end-of-scan
 * @COMEDI_CB_EOA:		end-of-acquisition/output
 * @COMEDI_CB_BLOCK:		data has arrived, wakes up read() / write()
 * @COMEDI_CB_EOBUF:		DEPRECATED: end of buffer
 * @COMEDI_CB_ERROR:		card error during acquisition
 * @COMEDI_CB_OVERFLOW:		buffer overflow/underflow
 * @COMEDI_CB_ERROR_MASK:	events that indicate an error has occurred
 * @COMEDI_CB_CANCEL_MASK:	events that will cancel an async command
 */
enum comedi_cb {
	COMEDI_CB_EOS		= BIT(0),
	COMEDI_CB_EOA		= BIT(1),
	COMEDI_CB_BLOCK		= BIT(2),
	COMEDI_CB_EOBUF		= BIT(3),
	COMEDI_CB_ERROR		= BIT(4),
	COMEDI_CB_OVERFLOW	= BIT(5),
	/* masks */
	COMEDI_CB_ERROR_MASK	= (COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW),
	COMEDI_CB_CANCEL_MASK	= (COMEDI_CB_EOA | COMEDI_CB_ERROR_MASK)
};

/**
 * struct comedi_driver - COMEDI driver registration
 * @driver_name: Name of driver.
 * @module: Owning module.
 * @attach: The optional "attach" handler for manually configured COMEDI
 *	devices.
 * @detach: The "detach" handler for deconfiguring COMEDI devices.
 * @auto_attach: The optional "auto_attach" handler for automatically
 *	configured COMEDI devices.
 * @num_names: Optional number of "board names" supported.
 * @board_name: Optional pointer to a pointer to a board name.  The pointer
 *	to a board name is embedded in an element of a driver-defined array
 *	of static, read-only board type information.
 * @offset: Optional size of each element of the driver-defined array of
 *	static, read-only board type information, i.e. the offset between each
 *	pointer to a board name.
 *
 * This is used with comedi_driver_register() and comedi_driver_unregister() to
 * register and unregister a low-level COMEDI driver with the COMEDI core.
 *
 * If @num_names is non-zero, @board_name should be non-NULL, and @offset
 * should be at least sizeof(*board_name).  These are used by the handler for
 * the %COMEDI_DEVCONFIG ioctl to match a hardware device and its driver by
 * board name.  If @num_names is zero, the %COMEDI_DEVCONFIG ioctl matches a
 * hardware device and its driver by driver name.  This is only useful if the
 * @attach handler is set.  If @num_names is non-zero, the driver's @attach
 * handler will be called with the COMEDI device structure's board_ptr member
 * pointing to the matched pointer to a board name within the driver's private
 * array of static, read-only board type information.
 *
 * The @detach handler has two roles.  If a COMEDI device was successfully
 * configured by the @attach or @auto_attach handler, it is called when the
 * device is being deconfigured (by the %COMEDI_DEVCONFIG ioctl, or due to
 * unloading of the driver, or due to device removal).  It is also called when
 * the @attach or @auto_attach handler returns an error.  Therefore, the
 * @attach or @auto_attach handlers can defer clean-up on error until the
 * @detach handler is called.  If the @attach or @auto_attach handlers free
 * any resources themselves, they must prevent the @detach handler from
 * freeing the same resources.  The @detach handler must not assume that all
 * resources requested by the @attach or @auto_attach handler were
 * successfully allocated.
 */
struct comedi_driver {
	/* private: */
	struct comedi_driver *next;	/* Next in list of COMEDI drivers. */
	/* public: */
	const char *driver_name;
	struct module *module;
	int (*attach)(struct comedi_device *dev, struct comedi_devconfig *it);
	void (*detach)(struct comedi_device *dev);
	int (*auto_attach)(struct comedi_device *dev, unsigned long context);
	unsigned int num_names;
	const char *const *board_name;
	int offset;
};

/**
 * struct comedi_device - Working data for a COMEDI device
 * @use_count: Number of open file objects.
 * @driver: Low-level COMEDI driver attached to this COMEDI device.
 * @pacer: Optional pointer to a dynamically allocated acquisition pacer
 *	control.  It is freed automatically after the COMEDI device is
 *	detached from the low-level driver.
 * @private: Optional pointer to private data allocated by the low-level
 *	driver.  It is freed automatically after the COMEDI device is
 *	detached from the low-level driver.
 * @class_dev: Sysfs comediX device.
 * @minor: Minor device number of COMEDI char device (0-47).
 * @detach_count: Counter incremented every time the COMEDI device is detached.
 *	Used for checking a previous attachment is still valid.
 * @hw_dev: Optional pointer to the low-level hardware &struct device.  It is
 *	required for automatically configured COMEDI devices and optional for
 *	COMEDI devices configured by the %COMEDI_DEVCONFIG ioctl, although
 *	the bus-specific COMEDI functions only work if it is set correctly.
 *	It is also passed to dma_alloc_coherent() for COMEDI subdevices that
 *	have their 'async_dma_dir' member set to something other than
 *	%DMA_NONE.
 * @board_name: Pointer to a COMEDI board name or a COMEDI driver name.  When
 *	the low-level driver's "attach" handler is called by the handler for
 *	the %COMEDI_DEVCONFIG ioctl, it either points to a matched board name
 *	string if the 'num_names' member of the &struct comedi_driver is
 *	non-zero, otherwise it points to the low-level driver name string.
 *	When the low-lever driver's "auto_attach" handler is called for an
 *	automatically configured COMEDI device, it points to the low-level
 *	driver name string.  The low-level driver is free to change it in its
 *	"attach" or "auto_attach" handler if it wishes.
 * @board_ptr: Optional pointer to private, read-only board type information in
 *	the low-level driver.  If the 'num_names' member of the &struct
 *	comedi_driver is non-zero, the handler for the %COMEDI_DEVCONFIG ioctl
 *	will point it to a pointer to a matched board name string within the
 *	driver's private array of static, read-only board type information when
 *	calling the driver's "attach" handler.  The low-level driver is free to
 *	change it.
 * @attached: Flag indicating that the COMEDI device is attached to a low-level
 *	driver.
 * @ioenabled: Flag used to indicate that a PCI device has been enabled and
 *	its regions requested.
 * @spinlock: Generic spin-lock for use by the low-level driver.
 * @mutex: Generic mutex for use by the COMEDI core module.
 * @attach_lock: &struct rw_semaphore used to guard against the COMEDI device
 *	being detached while an operation is in progress.  The down_write()
 *	operation is only allowed while @mutex is held and is used when
 *	changing @attached and @detach_count and calling the low-level driver's
 *	"detach" handler.  The down_read() operation is generally used without
 *	holding @mutex.
 * @refcount: &struct kref reference counter for freeing COMEDI device.
 * @n_subdevices: Number of COMEDI subdevices allocated by the low-level
 *	driver for this device.
 * @subdevices: Dynamically allocated array of COMEDI subdevices.
 * @mmio: Optional pointer to a remapped MMIO region set by the low-level
 *	driver.
 * @iobase: Optional base of an I/O port region requested by the low-level
 *	driver.
 * @iolen: Length of I/O port region requested at @iobase.
 * @irq: Optional IRQ number requested by the low-level driver.
 * @read_subdev: Optional pointer to a default COMEDI subdevice operated on by
 *	the read() file operation.  Set by the low-level driver.
 * @write_subdev: Optional pointer to a default COMEDI subdevice operated on by
 *	the write() file operation.  Set by the low-level driver.
 * @async_queue: Storage for fasync_helper().
 * @open: Optional pointer to a function set by the low-level driver to be
 *	called when @use_count changes from 0 to 1.
 * @close: Optional pointer to a function set by the low-level driver to be
 *	called when @use_count changed from 1 to 0.
 *
 * This is the main control data structure for a COMEDI device (as far as the
 * COMEDI core is concerned).  There are two groups of COMEDI devices -
 * "legacy" devices that are configured by the handler for the
 * %COMEDI_DEVCONFIG ioctl, and automatically configured devices resulting
 * from a call to comedi_auto_config() as a result of a bus driver probe in
 * a low-level COMEDI driver.  The "legacy" COMEDI devices are allocated
 * during module initialization if the "comedi_num_legacy_minors" module
 * parameter is non-zero and use minor device numbers from 0 to
 * comedi_num_legacy_minors minus one.  The automatically configured COMEDI
 * devices are allocated on demand and use minor device numbers from
 * comedi_num_legacy_minors to 47.
 */
struct comedi_device {
	int use_count;
	struct comedi_driver *driver;
	struct comedi_8254 *pacer;
	void *private;

	struct device *class_dev;
	int minor;
	unsigned int detach_count;
	struct device *hw_dev;

	const char *board_name;
	const void *board_ptr;
	bool attached:1;
	bool ioenabled:1;
	spinlock_t spinlock;	/* generic spin-lock for low-level driver */
	struct mutex mutex;	/* generic mutex for COMEDI core */
	struct rw_semaphore attach_lock;
	struct kref refcount;

	int n_subdevices;
	struct comedi_subdevice *subdevices;

	/* dumb */
	void __iomem *mmio;
	unsigned long iobase;
	unsigned long iolen;
	unsigned int irq;

	struct comedi_subdevice *read_subdev;
	struct comedi_subdevice *write_subdev;

	struct fasync_struct *async_queue;

	int (*open)(struct comedi_device *dev);
	void (*close)(struct comedi_device *dev);
};

/*
 * function prototypes
 */

void comedi_event(struct comedi_device *dev, struct comedi_subdevice *s);

struct comedi_device *comedi_dev_get_from_minor(unsigned int minor);
int comedi_dev_put(struct comedi_device *dev);

bool comedi_is_subdevice_running(struct comedi_subdevice *s);

void *comedi_alloc_spriv(struct comedi_subdevice *s, size_t size);
void comedi_set_spriv_auto_free(struct comedi_subdevice *s);

int comedi_check_chanlist(struct comedi_subdevice *s,
			  int n,
			  unsigned int *chanlist);

/* range stuff */

#define RANGE(a, b)		{(a) * 1e6, (b) * 1e6, 0}
#define RANGE_ext(a, b)		{(a) * 1e6, (b) * 1e6, RF_EXTERNAL}
#define RANGE_mA(a, b)		{(a) * 1e6, (b) * 1e6, UNIT_mA}
#define RANGE_unitless(a, b)	{(a) * 1e6, (b) * 1e6, 0}
#define BIP_RANGE(a)		{-(a) * 1e6, (a) * 1e6, 0}
#define UNI_RANGE(a)		{0, (a) * 1e6, 0}

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

/**
 * struct comedi_lrange - Describes a COMEDI range table
 * @length: Number of entries in the range table.
 * @range: Array of &struct comedi_krange, one for each range.
 *
 * Each element of @range[] describes the minimum and maximum physical range
 * range and the type of units.  Typically, the type of unit is %UNIT_volt
 * (i.e. volts) and the minimum and maximum are in millionths of a volt.
 * There may also be a flag that indicates the minimum and maximum are merely
 * scale factors for an unknown, external reference.
 */
struct comedi_lrange {
	int length;
	struct comedi_krange range[];
};

/**
 * comedi_range_is_bipolar() - Test if subdevice range is bipolar
 * @s: COMEDI subdevice.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is bipolar by checking whether its minimum value
 * is negative.
 *
 * Assumes @range is valid.  Does not work for subdevices using a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is bipolar.
 *	%false if the range is unipolar.
 */
static inline bool comedi_range_is_bipolar(struct comedi_subdevice *s,
					   unsigned int range)
{
	return s->range_table->range[range].min < 0;
}

/**
 * comedi_range_is_unipolar() - Test if subdevice range is unipolar
 * @s: COMEDI subdevice.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is unipolar by checking whether its minimum value
 * is at least 0.
 *
 * Assumes @range is valid.  Does not work for subdevices using a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is unipolar.
 *	%false if the range is bipolar.
 */
static inline bool comedi_range_is_unipolar(struct comedi_subdevice *s,
					    unsigned int range)
{
	return s->range_table->range[range].min >= 0;
}

/**
 * comedi_range_is_external() - Test if subdevice range is external
 * @s: COMEDI subdevice.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is externally reference by checking whether its
 * %RF_EXTERNAL flag is set.
 *
 * Assumes @range is valid.  Does not work for subdevices using a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is external.
 *	%false if the range is internal.
 */
static inline bool comedi_range_is_external(struct comedi_subdevice *s,
					    unsigned int range)
{
	return !!(s->range_table->range[range].flags & RF_EXTERNAL);
}

/**
 * comedi_chan_range_is_bipolar() - Test if channel-specific range is bipolar
 * @s: COMEDI subdevice.
 * @chan: The channel number.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is bipolar by checking whether its minimum value
 * is negative.
 *
 * Assumes @chan and @range are valid.  Only works for subdevices with a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is bipolar.
 *	%false if the range is unipolar.
 */
static inline bool comedi_chan_range_is_bipolar(struct comedi_subdevice *s,
						unsigned int chan,
						unsigned int range)
{
	return s->range_table_list[chan]->range[range].min < 0;
}

/**
 * comedi_chan_range_is_unipolar() - Test if channel-specific range is unipolar
 * @s: COMEDI subdevice.
 * @chan: The channel number.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is unipolar by checking whether its minimum value
 * is at least 0.
 *
 * Assumes @chan and @range are valid.  Only works for subdevices with a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is unipolar.
 *	%false if the range is bipolar.
 */
static inline bool comedi_chan_range_is_unipolar(struct comedi_subdevice *s,
						 unsigned int chan,
						 unsigned int range)
{
	return s->range_table_list[chan]->range[range].min >= 0;
}

/**
 * comedi_chan_range_is_external() - Test if channel-specific range is external
 * @s: COMEDI subdevice.
 * @chan: The channel number.
 * @range: Index of range within a range table.
 *
 * Tests whether a range is externally reference by checking whether its
 * %RF_EXTERNAL flag is set.
 *
 * Assumes @chan and @range are valid.  Only works for subdevices with a
 * channel-specific range table list.
 *
 * Return:
 *	%true if the range is bipolar.
 *	%false if the range is unipolar.
 */
static inline bool comedi_chan_range_is_external(struct comedi_subdevice *s,
						 unsigned int chan,
						 unsigned int range)
{
	return !!(s->range_table_list[chan]->range[range].flags & RF_EXTERNAL);
}

/**
 * comedi_offset_munge() - Convert between offset binary and 2's complement
 * @s: COMEDI subdevice.
 * @val: Value to be converted.
 *
 * Toggles the highest bit of a sample value to toggle between offset binary
 * and 2's complement.  Assumes that @s->maxdata is a power of 2 minus 1.
 *
 * Return: The converted value.
 */
static inline unsigned int comedi_offset_munge(struct comedi_subdevice *s,
					       unsigned int val)
{
	return val ^ s->maxdata ^ (s->maxdata >> 1);
}

/**
 * comedi_bytes_per_sample() - Determine subdevice sample size
 * @s: COMEDI subdevice.
 *
 * The sample size will be 4 (sizeof int) or 2 (sizeof short) depending on
 * whether the %SDF_LSAMPL subdevice flag is set or not.
 *
 * Return: The subdevice sample size.
 */
static inline unsigned int comedi_bytes_per_sample(struct comedi_subdevice *s)
{
	return s->subdev_flags & SDF_LSAMPL ? sizeof(int) : sizeof(short);
}

/**
 * comedi_sample_shift() - Determine log2 of subdevice sample size
 * @s: COMEDI subdevice.
 *
 * The sample size will be 4 (sizeof int) or 2 (sizeof short) depending on
 * whether the %SDF_LSAMPL subdevice flag is set or not.  The log2 of the
 * sample size will be 2 or 1 and can be used as the right operand of a
 * bit-shift operator to multiply or divide something by the sample size.
 *
 * Return: log2 of the subdevice sample size.
 */
static inline unsigned int comedi_sample_shift(struct comedi_subdevice *s)
{
	return s->subdev_flags & SDF_LSAMPL ? 2 : 1;
}

/**
 * comedi_bytes_to_samples() - Convert a number of bytes to a number of samples
 * @s: COMEDI subdevice.
 * @nbytes: Number of bytes
 *
 * Return: The number of bytes divided by the subdevice sample size.
 */
static inline unsigned int comedi_bytes_to_samples(struct comedi_subdevice *s,
						   unsigned int nbytes)
{
	return nbytes >> comedi_sample_shift(s);
}

/**
 * comedi_samples_to_bytes() - Convert a number of samples to a number of bytes
 * @s: COMEDI subdevice.
 * @nsamples: Number of samples.
 *
 * Return: The number of samples multiplied by the subdevice sample size.
 * (Does not check for arithmetic overflow.)
 */
static inline unsigned int comedi_samples_to_bytes(struct comedi_subdevice *s,
						   unsigned int nsamples)
{
	return nsamples << comedi_sample_shift(s);
}

/**
 * comedi_check_trigger_src() - Trivially validate a comedi_cmd trigger source
 * @src: Pointer to the trigger source to validate.
 * @flags: Bitmask of valid %TRIG_* for the trigger.
 *
 * This is used in "step 1" of the do_cmdtest functions of comedi drivers
 * to validate the comedi_cmd triggers. The mask of the @src against the
 * @flags allows the userspace comedilib to pass all the comedi_cmd
 * triggers as %TRIG_ANY and get back a bitmask of the valid trigger sources.
 *
 * Return:
 *	0 if trigger sources in *@src are all supported.
 *	-EINVAL if any trigger source in *@src is unsupported.
 */
static inline int comedi_check_trigger_src(unsigned int *src,
					   unsigned int flags)
{
	unsigned int orig_src = *src;

	*src = orig_src & flags;
	if (*src == TRIG_INVALID || *src != orig_src)
		return -EINVAL;
	return 0;
}

/**
 * comedi_check_trigger_is_unique() - Make sure a trigger source is unique
 * @src: The trigger source to check.
 *
 * Return:
 *	0 if no more than one trigger source is set.
 *	-EINVAL if more than one trigger source is set.
 */
static inline int comedi_check_trigger_is_unique(unsigned int src)
{
	/* this test is true if more than one _src bit is set */
	if ((src & (src - 1)) != 0)
		return -EINVAL;
	return 0;
}

/**
 * comedi_check_trigger_arg_is() - Trivially validate a trigger argument
 * @arg: Pointer to the trigger arg to validate.
 * @val: The value the argument should be.
 *
 * Forces *@arg to be @val.
 *
 * Return:
 *	0 if *@arg was already @val.
 *	-EINVAL if *@arg differed from @val.
 */
static inline int comedi_check_trigger_arg_is(unsigned int *arg,
					      unsigned int val)
{
	if (*arg != val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

/**
 * comedi_check_trigger_arg_min() - Trivially validate a trigger argument min
 * @arg: Pointer to the trigger arg to validate.
 * @val: The minimum value the argument should be.
 *
 * Forces *@arg to be at least @val, setting it to @val if necessary.
 *
 * Return:
 *	0 if *@arg was already at least @val.
 *	-EINVAL if *@arg was less than @val.
 */
static inline int comedi_check_trigger_arg_min(unsigned int *arg,
					       unsigned int val)
{
	if (*arg < val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

/**
 * comedi_check_trigger_arg_max() - Trivially validate a trigger argument max
 * @arg: Pointer to the trigger arg to validate.
 * @val: The maximum value the argument should be.
 *
 * Forces *@arg to be no more than @val, setting it to @val if necessary.
 *
 * Return:
 *	0 if*@arg was already no more than @val.
 *	-EINVAL if *@arg was greater than @val.
 */
static inline int comedi_check_trigger_arg_max(unsigned int *arg,
					       unsigned int val)
{
	if (*arg > val) {
		*arg = val;
		return -EINVAL;
	}
	return 0;
}

/*
 * Must set dev->hw_dev if you wish to dma directly into comedi's buffer.
 * Also useful for retrieving a previously configured hardware device of
 * known bus type.  Set automatically for auto-configured devices.
 * Automatically set to NULL when detaching hardware device.
 */
int comedi_set_hw_dev(struct comedi_device *dev, struct device *hw_dev);

/**
 * comedi_buf_n_bytes_ready - Determine amount of unread data in buffer
 * @s: COMEDI subdevice.
 *
 * Determines the number of bytes of unread data in the asynchronous
 * acquisition data buffer for a subdevice.  The data in question might not
 * have been fully "munged" yet.
 *
 * Returns: The amount of unread data in bytes.
 */
static inline unsigned int comedi_buf_n_bytes_ready(struct comedi_subdevice *s)
{
	return s->async->buf_write_count - s->async->buf_read_count;
}

unsigned int comedi_buf_write_alloc(struct comedi_subdevice *s, unsigned int n);
unsigned int comedi_buf_write_free(struct comedi_subdevice *s, unsigned int n);

unsigned int comedi_buf_read_n_available(struct comedi_subdevice *s);
unsigned int comedi_buf_read_alloc(struct comedi_subdevice *s, unsigned int n);
unsigned int comedi_buf_read_free(struct comedi_subdevice *s, unsigned int n);

unsigned int comedi_buf_write_samples(struct comedi_subdevice *s,
				      const void *data, unsigned int nsamples);
unsigned int comedi_buf_read_samples(struct comedi_subdevice *s,
				     void *data, unsigned int nsamples);

/* drivers.c - general comedi driver functions */

#define COMEDI_TIMEOUT_MS	1000

int comedi_timeout(struct comedi_device *dev, struct comedi_subdevice *s,
		   struct comedi_insn *insn,
		   int (*cb)(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned long context),
		   unsigned long context);

unsigned int comedi_handle_events(struct comedi_device *dev,
				  struct comedi_subdevice *s);

int comedi_dio_insn_config(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn, unsigned int *data,
			   unsigned int mask);
unsigned int comedi_dio_update_state(struct comedi_subdevice *s,
				     unsigned int *data);
unsigned int comedi_bytes_per_scan(struct comedi_subdevice *s);
unsigned int comedi_nscans_left(struct comedi_subdevice *s,
				unsigned int nscans);
unsigned int comedi_nsamples_left(struct comedi_subdevice *s,
				  unsigned int nsamples);
void comedi_inc_scan_progress(struct comedi_subdevice *s,
			      unsigned int num_bytes);

void *comedi_alloc_devpriv(struct comedi_device *dev, size_t size);
int comedi_alloc_subdevices(struct comedi_device *dev, int num_subdevices);
int comedi_alloc_subdev_readback(struct comedi_subdevice *s);

int comedi_readback_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);

int comedi_load_firmware(struct comedi_device *dev, struct device *hw_dev,
			 const char *name,
			 int (*cb)(struct comedi_device *dev,
				   const u8 *data, size_t size,
				   unsigned long context),
			 unsigned long context);

int __comedi_request_region(struct comedi_device *dev,
			    unsigned long start, unsigned long len);
int comedi_request_region(struct comedi_device *dev,
			  unsigned long start, unsigned long len);
void comedi_legacy_detach(struct comedi_device *dev);

int comedi_auto_config(struct device *hardware_device,
		       struct comedi_driver *driver, unsigned long context);
void comedi_auto_unconfig(struct device *hardware_device);

int comedi_driver_register(struct comedi_driver *driver);
void comedi_driver_unregister(struct comedi_driver *driver);

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

#endif /* _COMEDIDEV_H */

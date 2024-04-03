USB DMA
~~~~~~~

In Linux 2.5 kernels (and later), USB device drivers have additional control
over how DMA may be used to perform I/O operations.  The APIs are detailed
in the kernel usb programming guide (kerneldoc, from the source code).

API overview
============

The big picture is that USB drivers can continue to ignore most DMA issues,
though they still must provide DMA-ready buffers (see
Documentation/core-api/dma-api-howto.rst).  That's how they've worked through
the 2.4 (and earlier) kernels, or they can now be DMA-aware.

DMA-aware usb drivers:

- New calls enable DMA-aware drivers, letting them allocate dma buffers and
  manage dma mappings for existing dma-ready buffers (see below).

- URBs have an additional "transfer_dma" field, as well as a transfer_flags
  bit saying if it's valid.  (Control requests also have "setup_dma", but
  drivers must not use it.)

- "usbcore" will map this DMA address, if a DMA-aware driver didn't do
  it first and set ``URB_NO_TRANSFER_DMA_MAP``.  HCDs
  don't manage dma mappings for URBs.

- There's a new "generic DMA API", parts of which are usable by USB device
  drivers.  Never use dma_set_mask() on any USB interface or device; that
  would potentially break all devices sharing that bus.

Eliminating copies
==================

It's good to avoid making CPUs copy data needlessly.  The costs can add up,
and effects like cache-trashing can impose subtle penalties.

- If you're doing lots of small data transfers from the same buffer all
  the time, that can really burn up resources on systems which use an
  IOMMU to manage the DMA mappings.  It can cost MUCH more to set up and
  tear down the IOMMU mappings with each request than perform the I/O!

  For those specific cases, USB has primitives to allocate less expensive
  memory.  They work like kmalloc and kfree versions that give you the right
  kind of addresses to store in urb->transfer_buffer and urb->transfer_dma.
  You'd also set ``URB_NO_TRANSFER_DMA_MAP`` in urb->transfer_flags::

	void *usb_alloc_coherent (struct usb_device *dev, size_t size,
		int mem_flags, dma_addr_t *dma);

	void usb_free_coherent (struct usb_device *dev, size_t size,
		void *addr, dma_addr_t dma);

  Most drivers should **NOT** be using these primitives; they don't need
  to use this type of memory ("dma-coherent"), and memory returned from
  :c:func:`kmalloc` will work just fine.

  The memory buffer returned is "dma-coherent"; sometimes you might need to
  force a consistent memory access ordering by using memory barriers.  It's
  not using a streaming DMA mapping, so it's good for small transfers on
  systems where the I/O would otherwise thrash an IOMMU mapping.  (See
  Documentation/core-api/dma-api-howto.rst for definitions of "coherent" and
  "streaming" DMA mappings.)

  Asking for 1/Nth of a page (as well as asking for N pages) is reasonably
  space-efficient.

  On most systems the memory returned will be uncached, because the
  semantics of dma-coherent memory require either bypassing CPU caches
  or using cache hardware with bus-snooping support.  While x86 hardware
  has such bus-snooping, many other systems use software to flush cache
  lines to prevent DMA conflicts.

- Devices on some EHCI controllers could handle DMA to/from high memory.

  Unfortunately, the current Linux DMA infrastructure doesn't have a sane
  way to expose these capabilities ... and in any case, HIGHMEM is mostly a
  design wart specific to x86_32.  So your best bet is to ensure you never
  pass a highmem buffer into a USB driver.  That's easy; it's the default
  behavior.  Just don't override it; e.g. with ``NETIF_F_HIGHDMA``.

  This may force your callers to do some bounce buffering, copying from
  high memory to "normal" DMA memory.  If you can come up with a good way
  to fix this issue (for x86_32 machines with over 1 GByte of memory),
  feel free to submit patches.

Working with existing buffers
=============================

Existing buffers aren't usable for DMA without first being mapped into the
DMA address space of the device.  However, most buffers passed to your
driver can safely be used with such DMA mapping.  (See the first section
of Documentation/core-api/dma-api-howto.rst, titled "What memory is DMA-able?")

- When you have the scatterlists which have been mapped for the USB controller,
  you could use the new ``usb_sg_*()`` calls, which would turn scatterlist
  into URBs::

	int usb_sg_init(struct usb_sg_request *io, struct usb_device *dev,
		unsigned pipe, unsigned	period, struct scatterlist *sg,
		int nents, size_t length, gfp_t mem_flags);

	void usb_sg_wait(struct usb_sg_request *io);

	void usb_sg_cancel(struct usb_sg_request *io);

  When the USB controller doesn't support DMA, the ``usb_sg_init()`` would try
  to submit URBs in PIO way as long as the page in scatterlists is not in the
  Highmem, which could be very rare in modern architectures.

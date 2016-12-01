VME Device Drivers
==================

Driver registration
-------------------

As with other subsystems within the Linux kernel, VME device drivers register
with the VME subsystem, typically called from the devices init routine.  This is
achieved via a call to the following function:

.. code-block:: c

	int vme_register_driver (struct vme_driver *driver, unsigned int ndevs);

If driver registration is successful this function returns zero, if an error
occurred a negative error code will be returned.

A pointer to a structure of type 'vme_driver' must be provided to the
registration function. Along with ndevs, which is the number of devices your
driver is able to support. The structure is as follows:

.. code-block:: c

	struct vme_driver {
		struct list_head node;
		const char *name;
		int (*match)(struct vme_dev *);
		int (*probe)(struct vme_dev *);
		int (*remove)(struct vme_dev *);
		void (*shutdown)(void);
		struct device_driver driver;
		struct list_head devices;
		unsigned int ndev;
	};

At the minimum, the '.name', '.match' and '.probe' elements of this structure
should be correctly set. The '.name' element is a pointer to a string holding
the device driver's name.

The '.match' function allows control over which VME devices should be registered
with the driver. The match function should return 1 if a device should be
probed and 0 otherwise. This example match function (from vme_user.c) limits
the number of devices probed to one:

.. code-block:: c

	#define USER_BUS_MAX	1
	...
	static int vme_user_match(struct vme_dev *vdev)
	{
		if (vdev->id.num >= USER_BUS_MAX)
			return 0;
		return 1;
	}

The '.probe' element should contain a pointer to the probe routine. The
probe routine is passed a 'struct vme_dev' pointer as an argument. The
'struct vme_dev' structure looks like the following:

.. code-block:: c

	struct vme_dev {
		int num;
		struct vme_bridge *bridge;
		struct device dev;
		struct list_head drv_list;
		struct list_head bridge_list;
	};

Here, the 'num' field refers to the sequential device ID for this specific
driver. The bridge number (or bus number) can be accessed using
dev->bridge->num.

A function is also provided to unregister the driver from the VME core and is
usually called from the device driver's exit routine:

.. code-block:: c

	void vme_unregister_driver (struct vme_driver *driver);


Resource management
-------------------

Once a driver has registered with the VME core the provided match routine will
be called the number of times specified during the registration. If a match
succeeds, a non-zero value should be returned. A zero return value indicates
failure. For all successful matches, the probe routine of the corresponding
driver is called. The probe routine is passed a pointer to the devices
device structure. This pointer should be saved, it will be required for
requesting VME resources.

The driver can request ownership of one or more master windows, slave windows
and/or dma channels. Rather than allowing the device driver to request a
specific window or DMA channel (which may be used by a different driver) this
driver allows a resource to be assigned based on the required attributes of the
driver in question:

.. code-block:: c

	struct vme_resource * vme_master_request(struct vme_dev *dev,
		u32 aspace, u32 cycle, u32 width);

	struct vme_resource * vme_slave_request(struct vme_dev *dev, u32 aspace,
		u32 cycle);

	struct vme_resource *vme_dma_request(struct vme_dev *dev, u32 route);

For slave windows these attributes are split into the VME address spaces that
need to be accessed in 'aspace' and VME bus cycle types required in 'cycle'.
Master windows add a further set of attributes in 'width' specifying the
required data transfer widths. These attributes are defined as bitmasks and as
such any combination of the attributes can be requested for a single window,
the core will assign a window that meets the requirements, returning a pointer
of type vme_resource that should be used to identify the allocated resource
when it is used. For DMA controllers, the request function requires the
potential direction of any transfers to be provided in the route attributes.
This is typically VME-to-MEM and/or MEM-to-VME, though some hardware can
support VME-to-VME and MEM-to-MEM transfers as well as test pattern generation.
If an unallocated window fitting the requirements can not be found a NULL
pointer will be returned.

Functions are also provided to free window allocations once they are no longer
required. These functions should be passed the pointer to the resource provided
during resource allocation:

.. code-block:: c

	void vme_master_free(struct vme_resource *res);

	void vme_slave_free(struct vme_resource *res);

	void vme_dma_free(struct vme_resource *res);


Master windows
--------------

Master windows provide access from the local processor[s] out onto the VME bus.
The number of windows available and the available access modes is dependent on
the underlying chipset. A window must be configured before it can be used.


Master window configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Once a master window has been assigned the following functions can be used to
configure it and retrieve the current settings:

.. code-block:: c

	int vme_master_set (struct vme_resource *res, int enabled,
		unsigned long long base, unsigned long long size, u32 aspace,
		u32 cycle, u32 width);

	int vme_master_get (struct vme_resource *res, int *enabled,
		unsigned long long *base, unsigned long long *size, u32 *aspace,
		u32 *cycle, u32 *width);

The address spaces, transfer widths and cycle types are the same as described
under resource management, however some of the options are mutually exclusive.
For example, only one address space may be specified.

These functions return 0 on success or an error code should the call fail.


Master window access
~~~~~~~~~~~~~~~~~~~~

The following functions can be used to read from and write to configured master
windows. These functions return the number of bytes copied:

.. code-block:: c

	ssize_t vme_master_read(struct vme_resource *res, void *buf,
		size_t count, loff_t offset);

	ssize_t vme_master_write(struct vme_resource *res, void *buf,
		size_t count, loff_t offset);

In addition to simple reads and writes, a function is provided to do a
read-modify-write transaction. This function returns the original value of the
VME bus location :

.. code-block:: c

	unsigned int vme_master_rmw (struct vme_resource *res,
		unsigned int mask, unsigned int compare, unsigned int swap,
		loff_t offset);

This functions by reading the offset, applying the mask. If the bits selected in
the mask match with the values of the corresponding bits in the compare field,
the value of swap is written the specified offset.

Parts of a VME window can be mapped into user space memory using the following
function:

.. code-block:: c

	int vme_master_mmap(struct vme_resource *resource,
		struct vm_area_struct *vma)


Slave windows
-------------

Slave windows provide devices on the VME bus access into mapped portions of the
local memory. The number of windows available and the access modes that can be
used is dependent on the underlying chipset. A window must be configured before
it can be used.


Slave window configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

Once a slave window has been assigned the following functions can be used to
configure it and retrieve the current settings:

.. code-block:: c

	int vme_slave_set (struct vme_resource *res, int enabled,
		unsigned long long base, unsigned long long size,
		dma_addr_t mem, u32 aspace, u32 cycle);

	int vme_slave_get (struct vme_resource *res, int *enabled,
		unsigned long long *base, unsigned long long *size,
		dma_addr_t *mem, u32 *aspace, u32 *cycle);

The address spaces, transfer widths and cycle types are the same as described
under resource management, however some of the options are mutually exclusive.
For example, only one address space may be specified.

These functions return 0 on success or an error code should the call fail.


Slave window buffer allocation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Functions are provided to allow the user to allocate and free a contiguous
buffers which will be accessible by the VME bridge. These functions do not have
to be used, other methods can be used to allocate a buffer, though care must be
taken to ensure that they are contiguous and accessible by the VME bridge:

.. code-block:: c

	void * vme_alloc_consistent(struct vme_resource *res, size_t size,
		dma_addr_t *mem);

	void vme_free_consistent(struct vme_resource *res, size_t size,
		void *virt,	dma_addr_t mem);


Slave window access
~~~~~~~~~~~~~~~~~~~

Slave windows map local memory onto the VME bus, the standard methods for
accessing memory should be used.


DMA channels
------------

The VME DMA transfer provides the ability to run link-list DMA transfers. The
API introduces the concept of DMA lists. Each DMA list is a link-list which can
be passed to a DMA controller. Multiple lists can be created, extended,
executed, reused and destroyed.


List Management
~~~~~~~~~~~~~~~

The following functions are provided to create and destroy DMA lists. Execution
of a list will not automatically destroy the list, thus enabling a list to be
reused for repetitive tasks:

.. code-block:: c

	struct vme_dma_list *vme_new_dma_list(struct vme_resource *res);

	int vme_dma_list_free(struct vme_dma_list *list);


List Population
~~~~~~~~~~~~~~~

An item can be added to a list using the following function ( the source and
destination attributes need to be created before calling this function, this is
covered under "Transfer Attributes"):

.. code-block:: c

	int vme_dma_list_add(struct vme_dma_list *list,
		struct vme_dma_attr *src, struct vme_dma_attr *dest,
		size_t count);

.. note::

	The detailed attributes of the transfers source and destination
	are not checked until an entry is added to a DMA list, the request
	for a DMA channel purely checks the directions in which the
	controller is expected to transfer data. As a result it is
	possible for this call to return an error, for example if the
	source or destination is in an unsupported VME address space.

Transfer Attributes
~~~~~~~~~~~~~~~~~~~

The attributes for the source and destination are handled separately from adding
an item to a list. This is due to the diverse attributes required for each type
of source and destination. There are functions to create attributes for PCI, VME
and pattern sources and destinations (where appropriate):

Pattern source:

.. code-block:: c

	struct vme_dma_attr *vme_dma_pattern_attribute(u32 pattern, u32 type);

PCI source or destination:

.. code-block:: c

	struct vme_dma_attr *vme_dma_pci_attribute(dma_addr_t mem);

VME source or destination:

.. code-block:: c

	struct vme_dma_attr *vme_dma_vme_attribute(unsigned long long base,
		u32 aspace, u32 cycle, u32 width);

The following function should be used to free an attribute:

.. code-block:: c

	void vme_dma_free_attribute(struct vme_dma_attr *attr);


List Execution
~~~~~~~~~~~~~~

The following function queues a list for execution. The function will return
once the list has been executed:

.. code-block:: c

	int vme_dma_list_exec(struct vme_dma_list *list);


Interrupts
----------

The VME API provides functions to attach and detach callbacks to specific VME
level and status ID combinations and for the generation of VME interrupts with
specific VME level and status IDs.


Attaching Interrupt Handlers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following functions can be used to attach and free a specific VME level and
status ID combination. Any given combination can only be assigned a single
callback function. A void pointer parameter is provided, the value of which is
passed to the callback function, the use of this pointer is user undefined:

.. code-block:: c

	int vme_irq_request(struct vme_dev *dev, int level, int statid,
		void (*callback)(int, int, void *), void *priv);

	void vme_irq_free(struct vme_dev *dev, int level, int statid);

The callback parameters are as follows. Care must be taken in writing a callback
function, callback functions run in interrupt context:

.. code-block:: c

	void callback(int level, int statid, void *priv);


Interrupt Generation
~~~~~~~~~~~~~~~~~~~~

The following function can be used to generate a VME interrupt at a given VME
level and VME status ID:

.. code-block:: c

	int vme_irq_generate(struct vme_dev *dev, int level, int statid);


Location monitors
-----------------

The VME API provides the following functionality to configure the location
monitor.


Location Monitor Management
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following functions are provided to request the use of a block of location
monitors and to free them after they are no longer required:

.. code-block:: c

	struct vme_resource * vme_lm_request(struct vme_dev *dev);

	void vme_lm_free(struct vme_resource * res);

Each block may provide a number of location monitors, monitoring adjacent
locations. The following function can be used to determine how many locations
are provided:

.. code-block:: c

	int vme_lm_count(struct vme_resource * res);


Location Monitor Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Once a bank of location monitors has been allocated, the following functions
are provided to configure the location and mode of the location monitor:

.. code-block:: c

	int vme_lm_set(struct vme_resource *res, unsigned long long base,
		u32 aspace, u32 cycle);

	int vme_lm_get(struct vme_resource *res, unsigned long long *base,
		u32 *aspace, u32 *cycle);


Location Monitor Use
~~~~~~~~~~~~~~~~~~~~

The following functions allow a callback to be attached and detached from each
location monitor location. Each location monitor can monitor a number of
adjacent locations:

.. code-block:: c

	int vme_lm_attach(struct vme_resource *res, int num,
		void (*callback)(void *));

	int vme_lm_detach(struct vme_resource *res, int num);

The callback function is declared as follows.

.. code-block:: c

	void callback(void *data);


Slot Detection
--------------

This function returns the slot ID of the provided bridge.

.. code-block:: c

	int vme_slot_num(struct vme_dev *dev);


Bus Detection
-------------

This function returns the bus ID of the provided bridge.

.. code-block:: c

	int vme_bus_num(struct vme_dev *dev);


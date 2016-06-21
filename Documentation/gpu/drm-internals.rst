=============
DRM Internals
=============

This chapter documents DRM internals relevant to driver authors and
developers working to add support for the latest features to existing
drivers.

First, we go over some typical driver initialization requirements, like
setting up command buffers, creating an initial output configuration,
and initializing core services. Subsequent sections cover core internals
in more detail, providing implementation notes and examples.

The DRM layer provides several services to graphics drivers, many of
them driven by the application interfaces it provides through libdrm,
the library that wraps most of the DRM ioctls. These include vblank
event handling, memory management, output management, framebuffer
management, command submission & fencing, suspend/resume support, and
DMA services.

Driver Initialization
=====================

At the core of every DRM driver is a :c:type:`struct drm_driver
<drm_driver>` structure. Drivers typically statically initialize
a drm_driver structure, and then pass it to
:c:func:`drm_dev_alloc()` to allocate a device instance. After the
device instance is fully initialized it can be registered (which makes
it accessible from userspace) using :c:func:`drm_dev_register()`.

The :c:type:`struct drm_driver <drm_driver>` structure
contains static information that describes the driver and features it
supports, and pointers to methods that the DRM core will call to
implement the DRM API. We will first go through the :c:type:`struct
drm_driver <drm_driver>` static information fields, and will
then describe individual operations in details as they get used in later
sections.

Driver Information
------------------

Driver Features
^^^^^^^^^^^^^^^

Drivers inform the DRM core about their requirements and supported
features by setting appropriate flags in the driver_features field.
Since those flags influence the DRM core behaviour since registration
time, most of them must be set to registering the :c:type:`struct
drm_driver <drm_driver>` instance.

u32 driver_features;

DRIVER_USE_AGP
    Driver uses AGP interface, the DRM core will manage AGP resources.

DRIVER_REQUIRE_AGP
    Driver needs AGP interface to function. AGP initialization failure
    will become a fatal error.

DRIVER_PCI_DMA
    Driver is capable of PCI DMA, mapping of PCI DMA buffers to
    userspace will be enabled. Deprecated.

DRIVER_SG
    Driver can perform scatter/gather DMA, allocation and mapping of
    scatter/gather buffers will be enabled. Deprecated.

DRIVER_HAVE_DMA
    Driver supports DMA, the userspace DMA API will be supported.
    Deprecated.

DRIVER_HAVE_IRQ; DRIVER_IRQ_SHARED
    DRIVER_HAVE_IRQ indicates whether the driver has an IRQ handler
    managed by the DRM Core. The core will support simple IRQ handler
    installation when the flag is set. The installation process is
    described in ?.

    DRIVER_IRQ_SHARED indicates whether the device & handler support
    shared IRQs (note that this is required of PCI drivers).

DRIVER_GEM
    Driver use the GEM memory manager.

DRIVER_MODESET
    Driver supports mode setting interfaces (KMS).

DRIVER_PRIME
    Driver implements DRM PRIME buffer sharing.

DRIVER_RENDER
    Driver supports dedicated render nodes.

DRIVER_ATOMIC
    Driver supports atomic properties. In this case the driver must
    implement appropriate obj->atomic_get_property() vfuncs for any
    modeset objects with driver specific properties.

Major, Minor and Patchlevel
^^^^^^^^^^^^^^^^^^^^^^^^^^^

int major; int minor; int patchlevel;
The DRM core identifies driver versions by a major, minor and patch
level triplet. The information is printed to the kernel log at
initialization time and passed to userspace through the
DRM_IOCTL_VERSION ioctl.

The major and minor numbers are also used to verify the requested driver
API version passed to DRM_IOCTL_SET_VERSION. When the driver API
changes between minor versions, applications can call
DRM_IOCTL_SET_VERSION to select a specific version of the API. If the
requested major isn't equal to the driver major, or the requested minor
is larger than the driver minor, the DRM_IOCTL_SET_VERSION call will
return an error. Otherwise the driver's set_version() method will be
called with the requested version.

Name, Description and Date
^^^^^^^^^^^^^^^^^^^^^^^^^^

char \*name; char \*desc; char \*date;
The driver name is printed to the kernel log at initialization time,
used for IRQ registration and passed to userspace through
DRM_IOCTL_VERSION.

The driver description is a purely informative string passed to
userspace through the DRM_IOCTL_VERSION ioctl and otherwise unused by
the kernel.

The driver date, formatted as YYYYMMDD, is meant to identify the date of
the latest modification to the driver. However, as most drivers fail to
update it, its value is mostly useless. The DRM core prints it to the
kernel log at initialization time and passes it to userspace through the
DRM_IOCTL_VERSION ioctl.

Device Instance and Driver Handling
-----------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_drv.c
   :doc: driver instance overview

.. kernel-doc:: drivers/gpu/drm/drm_drv.c
   :export:

Driver Load
-----------

IRQ Registration
^^^^^^^^^^^^^^^^

The DRM core tries to facilitate IRQ handler registration and
unregistration by providing :c:func:`drm_irq_install()` and
:c:func:`drm_irq_uninstall()` functions. Those functions only
support a single interrupt per device, devices that use more than one
IRQs need to be handled manually.

Managed IRQ Registration
''''''''''''''''''''''''

:c:func:`drm_irq_install()` starts by calling the irq_preinstall
driver operation. The operation is optional and must make sure that the
interrupt will not get fired by clearing all pending interrupt flags or
disabling the interrupt.

The passed-in IRQ will then be requested by a call to
:c:func:`request_irq()`. If the DRIVER_IRQ_SHARED driver feature
flag is set, a shared (IRQF_SHARED) IRQ handler will be requested.

The IRQ handler function must be provided as the mandatory irq_handler
driver operation. It will get passed directly to
:c:func:`request_irq()` and thus has the same prototype as all IRQ
handlers. It will get called with a pointer to the DRM device as the
second argument.

Finally the function calls the optional irq_postinstall driver
operation. The operation usually enables interrupts (excluding the
vblank interrupt, which is enabled separately), but drivers may choose
to enable/disable interrupts at a different time.

:c:func:`drm_irq_uninstall()` is similarly used to uninstall an
IRQ handler. It starts by waking up all processes waiting on a vblank
interrupt to make sure they don't hang, and then calls the optional
irq_uninstall driver operation. The operation must disable all hardware
interrupts. Finally the function frees the IRQ by calling
:c:func:`free_irq()`.

Manual IRQ Registration
'''''''''''''''''''''''

Drivers that require multiple interrupt handlers can't use the managed
IRQ registration functions. In that case IRQs must be registered and
unregistered manually (usually with the :c:func:`request_irq()` and
:c:func:`free_irq()` functions, or their devm_\* equivalent).

When manually registering IRQs, drivers must not set the
DRIVER_HAVE_IRQ driver feature flag, and must not provide the
irq_handler driver operation. They must set the :c:type:`struct
drm_device <drm_device>` irq_enabled field to 1 upon
registration of the IRQs, and clear it to 0 after unregistering the
IRQs.

Memory Manager Initialization
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Every DRM driver requires a memory manager which must be initialized at
load time. DRM currently contains two memory managers, the Translation
Table Manager (TTM) and the Graphics Execution Manager (GEM). This
document describes the use of the GEM memory manager only. See ? for
details.

Miscellaneous Device Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Another task that may be necessary for PCI devices during configuration
is mapping the video BIOS. On many devices, the VBIOS describes device
configuration, LCD panel timings (if any), and contains flags indicating
device state. Mapping the BIOS can be done using the pci_map_rom()
call, a convenience function that takes care of mapping the actual ROM,
whether it has been shadowed into memory (typically at address 0xc0000)
or exists on the PCI device in the ROM BAR. Note that after the ROM has
been mapped and any necessary information has been extracted, it should
be unmapped; on many devices, the ROM address decoder is shared with
other BARs, so leaving it mapped could cause undesired behaviour like
hangs or memory corruption.

Bus-specific Device Registration and PCI Support
------------------------------------------------

A number of functions are provided to help with device registration. The
functions deal with PCI and platform devices respectively and are only
provided for historical reasons. These are all deprecated and shouldn't
be used in new drivers. Besides that there's a few helpers for pci
drivers.

.. kernel-doc:: drivers/gpu/drm/drm_pci.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_platform.c
   :export:

Memory management
=================

Modern Linux systems require large amount of graphics memory to store
frame buffers, textures, vertices and other graphics-related data. Given
the very dynamic nature of many of that data, managing graphics memory
efficiently is thus crucial for the graphics stack and plays a central
role in the DRM infrastructure.

The DRM core includes two memory managers, namely Translation Table Maps
(TTM) and Graphics Execution Manager (GEM). TTM was the first DRM memory
manager to be developed and tried to be a one-size-fits-them all
solution. It provides a single userspace API to accommodate the need of
all hardware, supporting both Unified Memory Architecture (UMA) devices
and devices with dedicated video RAM (i.e. most discrete video cards).
This resulted in a large, complex piece of code that turned out to be
hard to use for driver development.

GEM started as an Intel-sponsored project in reaction to TTM's
complexity. Its design philosophy is completely different: instead of
providing a solution to every graphics memory-related problems, GEM
identified common code between drivers and created a support library to
share it. GEM has simpler initialization and execution requirements than
TTM, but has no video RAM management capabilities and is thus limited to
UMA devices.

The Translation Table Manager (TTM)
-----------------------------------

TTM design background and information belongs here.

TTM initialization
^^^^^^^^^^^^^^^^^^

    **Warning**

    This section is outdated.

Drivers wishing to support TTM must fill out a drm_bo_driver
structure. The structure contains several fields with function pointers
for initializing the TTM, allocating and freeing memory, waiting for
command completion and fence synchronization, and memory migration. See
the radeon_ttm.c file for an example of usage.

The ttm_global_reference structure is made up of several fields:

::

              struct ttm_global_reference {
                      enum ttm_global_types global_type;
                      size_t size;
                      void *object;
                      int (*init) (struct ttm_global_reference *);
                      void (*release) (struct ttm_global_reference *);
              };


There should be one global reference structure for your memory manager
as a whole, and there will be others for each object created by the
memory manager at runtime. Your global TTM should have a type of
TTM_GLOBAL_TTM_MEM. The size field for the global object should be
sizeof(struct ttm_mem_global), and the init and release hooks should
point at your driver-specific init and release routines, which probably
eventually call ttm_mem_global_init and ttm_mem_global_release,
respectively.

Once your global TTM accounting structure is set up and initialized by
calling ttm_global_item_ref() on it, you need to create a buffer
object TTM to provide a pool for buffer object allocation by clients and
the kernel itself. The type of this object should be
TTM_GLOBAL_TTM_BO, and its size should be sizeof(struct
ttm_bo_global). Again, driver-specific init and release functions may
be provided, likely eventually calling ttm_bo_global_init() and
ttm_bo_global_release(), respectively. Also, like the previous
object, ttm_global_item_ref() is used to create an initial reference
count for the TTM, which will call your initialization function.

The Graphics Execution Manager (GEM)
------------------------------------

The GEM design approach has resulted in a memory manager that doesn't
provide full coverage of all (or even all common) use cases in its
userspace or kernel API. GEM exposes a set of standard memory-related
operations to userspace and a set of helper functions to drivers, and
let drivers implement hardware-specific operations with their own
private API.

The GEM userspace API is described in the `GEM - the Graphics Execution
Manager <http://lwn.net/Articles/283798/>`__ article on LWN. While
slightly outdated, the document provides a good overview of the GEM API
principles. Buffer allocation and read and write operations, described
as part of the common GEM API, are currently implemented using
driver-specific ioctls.

GEM is data-agnostic. It manages abstract buffer objects without knowing
what individual buffers contain. APIs that require knowledge of buffer
contents or purpose, such as buffer allocation or synchronization
primitives, are thus outside of the scope of GEM and must be implemented
using driver-specific ioctls.

On a fundamental level, GEM involves several operations:

-  Memory allocation and freeing
-  Command execution
-  Aperture management at command execution time

Buffer object allocation is relatively straightforward and largely
provided by Linux's shmem layer, which provides memory to back each
object.

Device-specific operations, such as command execution, pinning, buffer
read & write, mapping, and domain ownership transfers are left to
driver-specific ioctls.

GEM Initialization
^^^^^^^^^^^^^^^^^^

Drivers that use GEM must set the DRIVER_GEM bit in the struct
:c:type:`struct drm_driver <drm_driver>` driver_features
field. The DRM core will then automatically initialize the GEM core
before calling the load operation. Behind the scene, this will create a
DRM Memory Manager object which provides an address space pool for
object allocation.

In a KMS configuration, drivers need to allocate and initialize a
command ring buffer following core GEM initialization if required by the
hardware. UMA devices usually have what is called a "stolen" memory
region, which provides space for the initial framebuffer and large,
contiguous memory regions required by the device. This space is
typically not managed by GEM, and must be initialized separately into
its own DRM MM object.

GEM Objects Creation
^^^^^^^^^^^^^^^^^^^^

GEM splits creation of GEM objects and allocation of the memory that
backs them in two distinct operations.

GEM objects are represented by an instance of struct :c:type:`struct
drm_gem_object <drm_gem_object>`. Drivers usually need to
extend GEM objects with private information and thus create a
driver-specific GEM object structure type that embeds an instance of
struct :c:type:`struct drm_gem_object <drm_gem_object>`.

To create a GEM object, a driver allocates memory for an instance of its
specific GEM object type and initializes the embedded struct
:c:type:`struct drm_gem_object <drm_gem_object>` with a call
to :c:func:`drm_gem_object_init()`. The function takes a pointer
to the DRM device, a pointer to the GEM object and the buffer object
size in bytes.

GEM uses shmem to allocate anonymous pageable memory.
:c:func:`drm_gem_object_init()` will create an shmfs file of the
requested size and store it into the struct :c:type:`struct
drm_gem_object <drm_gem_object>` filp field. The memory is
used as either main storage for the object when the graphics hardware
uses system memory directly or as a backing store otherwise.

Drivers are responsible for the actual physical pages allocation by
calling :c:func:`shmem_read_mapping_page_gfp()` for each page.
Note that they can decide to allocate pages when initializing the GEM
object, or to delay allocation until the memory is needed (for instance
when a page fault occurs as a result of a userspace memory access or
when the driver needs to start a DMA transfer involving the memory).

Anonymous pageable memory allocation is not always desired, for instance
when the hardware requires physically contiguous system memory as is
often the case in embedded devices. Drivers can create GEM objects with
no shmfs backing (called private GEM objects) by initializing them with
a call to :c:func:`drm_gem_private_object_init()` instead of
:c:func:`drm_gem_object_init()`. Storage for private GEM objects
must be managed by drivers.

GEM Objects Lifetime
^^^^^^^^^^^^^^^^^^^^

All GEM objects are reference-counted by the GEM core. References can be
acquired and release by :c:func:`calling
drm_gem_object_reference()` and
:c:func:`drm_gem_object_unreference()` respectively. The caller
must hold the :c:type:`struct drm_device <drm_device>`
struct_mutex lock when calling
:c:func:`drm_gem_object_reference()`. As a convenience, GEM
provides :c:func:`drm_gem_object_unreference_unlocked()`
functions that can be called without holding the lock.

When the last reference to a GEM object is released the GEM core calls
the :c:type:`struct drm_driver <drm_driver>` gem_free_object
operation. That operation is mandatory for GEM-enabled drivers and must
free the GEM object and all associated resources.

void (\*gem_free_object) (struct drm_gem_object \*obj); Drivers are
responsible for freeing all GEM object resources. This includes the
resources created by the GEM core, which need to be released with
:c:func:`drm_gem_object_release()`.

GEM Objects Naming
^^^^^^^^^^^^^^^^^^

Communication between userspace and the kernel refers to GEM objects
using local handles, global names or, more recently, file descriptors.
All of those are 32-bit integer values; the usual Linux kernel limits
apply to the file descriptors.

GEM handles are local to a DRM file. Applications get a handle to a GEM
object through a driver-specific ioctl, and can use that handle to refer
to the GEM object in other standard or driver-specific ioctls. Closing a
DRM file handle frees all its GEM handles and dereferences the
associated GEM objects.

To create a handle for a GEM object drivers call
:c:func:`drm_gem_handle_create()`. The function takes a pointer
to the DRM file and the GEM object and returns a locally unique handle.
When the handle is no longer needed drivers delete it with a call to
:c:func:`drm_gem_handle_delete()`. Finally the GEM object
associated with a handle can be retrieved by a call to
:c:func:`drm_gem_object_lookup()`.

Handles don't take ownership of GEM objects, they only take a reference
to the object that will be dropped when the handle is destroyed. To
avoid leaking GEM objects, drivers must make sure they drop the
reference(s) they own (such as the initial reference taken at object
creation time) as appropriate, without any special consideration for the
handle. For example, in the particular case of combined GEM object and
handle creation in the implementation of the dumb_create operation,
drivers must drop the initial reference to the GEM object before
returning the handle.

GEM names are similar in purpose to handles but are not local to DRM
files. They can be passed between processes to reference a GEM object
globally. Names can't be used directly to refer to objects in the DRM
API, applications must convert handles to names and names to handles
using the DRM_IOCTL_GEM_FLINK and DRM_IOCTL_GEM_OPEN ioctls
respectively. The conversion is handled by the DRM core without any
driver-specific support.

GEM also supports buffer sharing with dma-buf file descriptors through
PRIME. GEM-based drivers must use the provided helpers functions to
implement the exporting and importing correctly. See ?. Since sharing
file descriptors is inherently more secure than the easily guessable and
global GEM names it is the preferred buffer sharing mechanism. Sharing
buffers through GEM names is only supported for legacy userspace.
Furthermore PRIME also allows cross-device buffer sharing since it is
based on dma-bufs.

GEM Objects Mapping
^^^^^^^^^^^^^^^^^^^

Because mapping operations are fairly heavyweight GEM favours
read/write-like access to buffers, implemented through driver-specific
ioctls, over mapping buffers to userspace. However, when random access
to the buffer is needed (to perform software rendering for instance),
direct access to the object can be more efficient.

The mmap system call can't be used directly to map GEM objects, as they
don't have their own file handle. Two alternative methods currently
co-exist to map GEM objects to userspace. The first method uses a
driver-specific ioctl to perform the mapping operation, calling
:c:func:`do_mmap()` under the hood. This is often considered
dubious, seems to be discouraged for new GEM-enabled drivers, and will
thus not be described here.

The second method uses the mmap system call on the DRM file handle. void
\*mmap(void \*addr, size_t length, int prot, int flags, int fd, off_t
offset); DRM identifies the GEM object to be mapped by a fake offset
passed through the mmap offset argument. Prior to being mapped, a GEM
object must thus be associated with a fake offset. To do so, drivers
must call :c:func:`drm_gem_create_mmap_offset()` on the object.

Once allocated, the fake offset value must be passed to the application
in a driver-specific way and can then be used as the mmap offset
argument.

The GEM core provides a helper method :c:func:`drm_gem_mmap()` to
handle object mapping. The method can be set directly as the mmap file
operation handler. It will look up the GEM object based on the offset
value and set the VMA operations to the :c:type:`struct drm_driver
<drm_driver>` gem_vm_ops field. Note that
:c:func:`drm_gem_mmap()` doesn't map memory to userspace, but
relies on the driver-provided fault handler to map pages individually.

To use :c:func:`drm_gem_mmap()`, drivers must fill the struct
:c:type:`struct drm_driver <drm_driver>` gem_vm_ops field
with a pointer to VM operations.

struct vm_operations_struct \*gem_vm_ops struct
vm_operations_struct { void (\*open)(struct vm_area_struct \* area);
void (\*close)(struct vm_area_struct \* area); int (\*fault)(struct
vm_area_struct \*vma, struct vm_fault \*vmf); };

The open and close operations must update the GEM object reference
count. Drivers can use the :c:func:`drm_gem_vm_open()` and
:c:func:`drm_gem_vm_close()` helper functions directly as open
and close handlers.

The fault operation handler is responsible for mapping individual pages
to userspace when a page fault occurs. Depending on the memory
allocation scheme, drivers can allocate pages at fault time, or can
decide to allocate memory for the GEM object at the time the object is
created.

Drivers that want to map the GEM object upfront instead of handling page
faults can implement their own mmap file operation handler.

Memory Coherency
^^^^^^^^^^^^^^^^

When mapped to the device or used in a command buffer, backing pages for
an object are flushed to memory and marked write combined so as to be
coherent with the GPU. Likewise, if the CPU accesses an object after the
GPU has finished rendering to the object, then the object must be made
coherent with the CPU's view of memory, usually involving GPU cache
flushing of various kinds. This core CPU<->GPU coherency management is
provided by a device-specific ioctl, which evaluates an object's current
domain and performs any necessary flushing or synchronization to put the
object into the desired coherency domain (note that the object may be
busy, i.e. an active render target; in that case, setting the domain
blocks the client and waits for rendering to complete before performing
any necessary flushing operations).

Command Execution
^^^^^^^^^^^^^^^^^

Perhaps the most important GEM function for GPU devices is providing a
command execution interface to clients. Client programs construct
command buffers containing references to previously allocated memory
objects, and then submit them to GEM. At that point, GEM takes care to
bind all the objects into the GTT, execute the buffer, and provide
necessary synchronization between clients accessing the same buffers.
This often involves evicting some objects from the GTT and re-binding
others (a fairly expensive operation), and providing relocation support
which hides fixed GTT offsets from clients. Clients must take care not
to submit command buffers that reference more objects than can fit in
the GTT; otherwise, GEM will reject them and no rendering will occur.
Similarly, if several objects in the buffer require fence registers to
be allocated for correct rendering (e.g. 2D blits on pre-965 chips),
care must be taken not to require more fence registers than are
available to the client. Such resource management should be abstracted
from the client in libdrm.

GEM Function Reference
----------------------

.. kernel-doc:: drivers/gpu/drm/drm_gem.c
   :export:

.. kernel-doc:: include/drm/drm_gem.h
   :internal:

VMA Offset Manager
------------------

.. kernel-doc:: drivers/gpu/drm/drm_vma_manager.c
   :doc: vma offset manager

.. kernel-doc:: drivers/gpu/drm/drm_vma_manager.c
   :export:

.. kernel-doc:: include/drm/drm_vma_manager.h
   :internal:

PRIME Buffer Sharing
--------------------

PRIME is the cross device buffer sharing framework in drm, originally
created for the OPTIMUS range of multi-gpu platforms. To userspace PRIME
buffers are dma-buf based file descriptors.

Overview and Driver Interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Similar to GEM global names, PRIME file descriptors are also used to
share buffer objects across processes. They offer additional security:
as file descriptors must be explicitly sent over UNIX domain sockets to
be shared between applications, they can't be guessed like the globally
unique GEM names.

Drivers that support the PRIME API must set the DRIVER_PRIME bit in the
struct :c:type:`struct drm_driver <drm_driver>`
driver_features field, and implement the prime_handle_to_fd and
prime_fd_to_handle operations.

int (\*prime_handle_to_fd)(struct drm_device \*dev, struct drm_file
\*file_priv, uint32_t handle, uint32_t flags, int \*prime_fd); int
(\*prime_fd_to_handle)(struct drm_device \*dev, struct drm_file
\*file_priv, int prime_fd, uint32_t \*handle); Those two operations
convert a handle to a PRIME file descriptor and vice versa. Drivers must
use the kernel dma-buf buffer sharing framework to manage the PRIME file
descriptors. Similar to the mode setting API PRIME is agnostic to the
underlying buffer object manager, as long as handles are 32bit unsigned
integers.

While non-GEM drivers must implement the operations themselves, GEM
drivers must use the :c:func:`drm_gem_prime_handle_to_fd()` and
:c:func:`drm_gem_prime_fd_to_handle()` helper functions. Those
helpers rely on the driver gem_prime_export and gem_prime_import
operations to create a dma-buf instance from a GEM object (dma-buf
exporter role) and to create a GEM object from a dma-buf instance
(dma-buf importer role).

struct dma_buf \* (\*gem_prime_export)(struct drm_device \*dev,
struct drm_gem_object \*obj, int flags); struct drm_gem_object \*
(\*gem_prime_import)(struct drm_device \*dev, struct dma_buf
\*dma_buf); These two operations are mandatory for GEM drivers that
support PRIME.

PRIME Helper Functions
^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_prime.c
   :doc: PRIME Helpers

PRIME Function References
-------------------------

.. kernel-doc:: drivers/gpu/drm/drm_prime.c
   :export:

DRM MM Range Allocator
----------------------

Overview
^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_mm.c
   :doc: Overview

LRU Scan/Eviction Support
^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_mm.c
   :doc: lru scan roaster

DRM MM Range Allocator Function References
------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_mm.c
   :export:

.. kernel-doc:: include/drm/drm_mm.h
   :internal:

CMA Helper Functions Reference
------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_gem_cma_helper.c
   :doc: cma helpers

.. kernel-doc:: drivers/gpu/drm/drm_gem_cma_helper.c
   :export:

.. kernel-doc:: include/drm/drm_gem_cma_helper.h
   :internal:

Mode Setting
============

Drivers must initialize the mode setting core by calling
:c:func:`drm_mode_config_init()` on the DRM device. The function
initializes the :c:type:`struct drm_device <drm_device>`
mode_config field and never fails. Once done, mode configuration must
be setup by initializing the following fields.

-  int min_width, min_height; int max_width, max_height;
   Minimum and maximum width and height of the frame buffers in pixel
   units.

-  struct drm_mode_config_funcs \*funcs;
   Mode setting functions.

Display Modes Function Reference
--------------------------------

.. kernel-doc:: include/drm/drm_modes.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_modes.c
   :export:

Atomic Mode Setting Function Reference
--------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_atomic.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_atomic.c
   :internal:

Frame Buffer Abstraction
------------------------

Frame buffers are abstract memory objects that provide a source of
pixels to scanout to a CRTC. Applications explicitly request the
creation of frame buffers through the DRM_IOCTL_MODE_ADDFB(2) ioctls
and receive an opaque handle that can be passed to the KMS CRTC control,
plane configuration and page flip functions.

Frame buffers rely on the underneath memory manager for low-level memory
operations. When creating a frame buffer applications pass a memory
handle (or a list of memory handles for multi-planar formats) through
the ``drm_mode_fb_cmd2`` argument. For drivers using GEM as their
userspace buffer management interface this would be a GEM handle.
Drivers are however free to use their own backing storage object
handles, e.g. vmwgfx directly exposes special TTM handles to userspace
and so expects TTM handles in the create ioctl and not GEM handles.

The lifetime of a drm framebuffer is controlled with a reference count,
drivers can grab additional references with
:c:func:`drm_framebuffer_reference()`and drop them again with
:c:func:`drm_framebuffer_unreference()`. For driver-private
framebuffers for which the last reference is never dropped (e.g. for the
fbdev framebuffer when the struct :c:type:`struct drm_framebuffer
<drm_framebuffer>` is embedded into the fbdev helper struct)
drivers can manually clean up a framebuffer at module unload time with
:c:func:`drm_framebuffer_unregister_private()`.

DRM Format Handling
-------------------

.. kernel-doc:: include/drm/drm_fourcc.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_fourcc.c
   :export:

Dumb Buffer Objects
-------------------

The KMS API doesn't standardize backing storage object creation and
leaves it to driver-specific ioctls. Furthermore actually creating a
buffer object even for GEM-based drivers is done through a
driver-specific ioctl - GEM only has a common userspace interface for
sharing and destroying objects. While not an issue for full-fledged
graphics stacks that include device-specific userspace components (in
libdrm for instance), this limit makes DRM-based early boot graphics
unnecessarily complex.

Dumb objects partly alleviate the problem by providing a standard API to
create dumb buffers suitable for scanout, which can then be used to
create KMS frame buffers.

To support dumb objects drivers must implement the dumb_create,
dumb_destroy and dumb_map_offset operations.

-  int (\*dumb_create)(struct drm_file \*file_priv, struct
   drm_device \*dev, struct drm_mode_create_dumb \*args);
   The dumb_create operation creates a driver object (GEM or TTM
   handle) suitable for scanout based on the width, height and depth
   from the struct :c:type:`struct drm_mode_create_dumb
   <drm_mode_create_dumb>` argument. It fills the argument's
   handle, pitch and size fields with a handle for the newly created
   object and its line pitch and size in bytes.

-  int (\*dumb_destroy)(struct drm_file \*file_priv, struct
   drm_device \*dev, uint32_t handle);
   The dumb_destroy operation destroys a dumb object created by
   dumb_create.

-  int (\*dumb_map_offset)(struct drm_file \*file_priv, struct
   drm_device \*dev, uint32_t handle, uint64_t \*offset);
   The dumb_map_offset operation associates an mmap fake offset with
   the object given by the handle and returns it. Drivers must use the
   :c:func:`drm_gem_create_mmap_offset()` function to associate
   the fake offset as described in ?.

Note that dumb objects may not be used for gpu acceleration, as has been
attempted on some ARM embedded platforms. Such drivers really must have
a hardware-specific ioctl to allocate suitable buffer objects.

Output Polling
--------------

void (\*output_poll_changed)(struct drm_device \*dev);
This operation notifies the driver that the status of one or more
connectors has changed. Drivers that use the fb helper can just call the
:c:func:`drm_fb_helper_hotplug_event()` function to handle this
operation.

KMS Initialization and Cleanup
==============================

A KMS device is abstracted and exposed as a set of planes, CRTCs,
encoders and connectors. KMS drivers must thus create and initialize all
those objects at load time after initializing mode setting.

CRTCs (:c:type:`struct drm_crtc <drm_crtc>`)
--------------------------------------------

A CRTC is an abstraction representing a part of the chip that contains a
pointer to a scanout buffer. Therefore, the number of CRTCs available
determines how many independent scanout buffers can be active at any
given time. The CRTC structure contains several fields to support this:
a pointer to some video memory (abstracted as a frame buffer object), a
display mode, and an (x, y) offset into the video memory to support
panning or configurations where one piece of video memory spans multiple
CRTCs.

CRTC Initialization
^^^^^^^^^^^^^^^^^^^

A KMS device must create and register at least one struct
:c:type:`struct drm_crtc <drm_crtc>` instance. The instance is
allocated and zeroed by the driver, possibly as part of a larger
structure, and registered with a call to :c:func:`drm_crtc_init()`
with a pointer to CRTC functions.

Planes (:c:type:`struct drm_plane <drm_plane>`)
-----------------------------------------------

A plane represents an image source that can be blended with or overlayed
on top of a CRTC during the scanout process. Planes are associated with
a frame buffer to crop a portion of the image memory (source) and
optionally scale it to a destination size. The result is then blended
with or overlayed on top of a CRTC.

The DRM core recognizes three types of planes:

-  DRM_PLANE_TYPE_PRIMARY represents a "main" plane for a CRTC.
   Primary planes are the planes operated upon by CRTC modesetting and
   flipping operations described in the page_flip hook in
   :c:type:`struct drm_crtc_funcs <drm_crtc_funcs>`.
-  DRM_PLANE_TYPE_CURSOR represents a "cursor" plane for a CRTC.
   Cursor planes are the planes operated upon by the
   DRM_IOCTL_MODE_CURSOR and DRM_IOCTL_MODE_CURSOR2 ioctls.
-  DRM_PLANE_TYPE_OVERLAY represents all non-primary, non-cursor
   planes. Some drivers refer to these types of planes as "sprites"
   internally.

For compatibility with legacy userspace, only overlay planes are made
available to userspace by default. Userspace clients may set the
DRM_CLIENT_CAP_UNIVERSAL_PLANES client capability bit to indicate
that they wish to receive a universal plane list containing all plane
types.

Plane Initialization
^^^^^^^^^^^^^^^^^^^^

To create a plane, a KMS drivers allocates and zeroes an instances of
:c:type:`struct drm_plane <drm_plane>` (possibly as part of a
larger structure) and registers it with a call to
:c:func:`drm_universal_plane_init()`. The function takes a
bitmask of the CRTCs that can be associated with the plane, a pointer to
the plane functions, a list of format supported formats, and the type of
plane (primary, cursor, or overlay) being initialized.

Cursor and overlay planes are optional. All drivers should provide one
primary plane per CRTC (although this requirement may change in the
future); drivers that do not wish to provide special handling for
primary planes may make use of the helper functions described in ? to
create and register a primary plane with standard capabilities.

Encoders (:c:type:`struct drm_encoder <drm_encoder>`)
-----------------------------------------------------

An encoder takes pixel data from a CRTC and converts it to a format
suitable for any attached connectors. On some devices, it may be
possible to have a CRTC send data to more than one encoder. In that
case, both encoders would receive data from the same scanout buffer,
resulting in a "cloned" display configuration across the connectors
attached to each encoder.

Encoder Initialization
^^^^^^^^^^^^^^^^^^^^^^

As for CRTCs, a KMS driver must create, initialize and register at least
one :c:type:`struct drm_encoder <drm_encoder>` instance. The
instance is allocated and zeroed by the driver, possibly as part of a
larger structure.

Drivers must initialize the :c:type:`struct drm_encoder
<drm_encoder>` possible_crtcs and possible_clones fields before
registering the encoder. Both fields are bitmasks of respectively the
CRTCs that the encoder can be connected to, and sibling encoders
candidate for cloning.

After being initialized, the encoder must be registered with a call to
:c:func:`drm_encoder_init()`. The function takes a pointer to the
encoder functions and an encoder type. Supported types are

-  DRM_MODE_ENCODER_DAC for VGA and analog on DVI-I/DVI-A
-  DRM_MODE_ENCODER_TMDS for DVI, HDMI and (embedded) DisplayPort
-  DRM_MODE_ENCODER_LVDS for display panels
-  DRM_MODE_ENCODER_TVDAC for TV output (Composite, S-Video,
   Component, SCART)
-  DRM_MODE_ENCODER_VIRTUAL for virtual machine displays

Encoders must be attached to a CRTC to be used. DRM drivers leave
encoders unattached at initialization time. Applications (or the fbdev
compatibility layer when implemented) are responsible for attaching the
encoders they want to use to a CRTC.

Connectors (:c:type:`struct drm_connector <drm_connector>`)
-----------------------------------------------------------

A connector is the final destination for pixel data on a device, and
usually connects directly to an external display device like a monitor
or laptop panel. A connector can only be attached to one encoder at a
time. The connector is also the structure where information about the
attached display is kept, so it contains fields for display data, EDID
data, DPMS & connection status, and information about modes supported on
the attached displays.

Connector Initialization
^^^^^^^^^^^^^^^^^^^^^^^^

Finally a KMS driver must create, initialize, register and attach at
least one :c:type:`struct drm_connector <drm_connector>`
instance. The instance is created as other KMS objects and initialized
by setting the following fields.

interlace_allowed
    Whether the connector can handle interlaced modes.

doublescan_allowed
    Whether the connector can handle doublescan.

display_info
    Display information is filled from EDID information when a display
    is detected. For non hot-pluggable displays such as flat panels in
    embedded systems, the driver should initialize the
    display_info.width_mm and display_info.height_mm fields with the
    physical size of the display.

polled
    Connector polling mode, a combination of

    DRM_CONNECTOR_POLL_HPD
        The connector generates hotplug events and doesn't need to be
        periodically polled. The CONNECT and DISCONNECT flags must not
        be set together with the HPD flag.

    DRM_CONNECTOR_POLL_CONNECT
        Periodically poll the connector for connection.

    DRM_CONNECTOR_POLL_DISCONNECT
        Periodically poll the connector for disconnection.

    Set to 0 for connectors that don't support connection status
    discovery.

The connector is then registered with a call to
:c:func:`drm_connector_init()` with a pointer to the connector
functions and a connector type, and exposed through sysfs with a call to
:c:func:`drm_connector_register()`.

Supported connector types are

-  DRM_MODE_CONNECTOR_VGA
-  DRM_MODE_CONNECTOR_DVII
-  DRM_MODE_CONNECTOR_DVID
-  DRM_MODE_CONNECTOR_DVIA
-  DRM_MODE_CONNECTOR_Composite
-  DRM_MODE_CONNECTOR_SVIDEO
-  DRM_MODE_CONNECTOR_LVDS
-  DRM_MODE_CONNECTOR_Component
-  DRM_MODE_CONNECTOR_9PinDIN
-  DRM_MODE_CONNECTOR_DisplayPort
-  DRM_MODE_CONNECTOR_HDMIA
-  DRM_MODE_CONNECTOR_HDMIB
-  DRM_MODE_CONNECTOR_TV
-  DRM_MODE_CONNECTOR_eDP
-  DRM_MODE_CONNECTOR_VIRTUAL

Connectors must be attached to an encoder to be used. For devices that
map connectors to encoders 1:1, the connector should be attached at
initialization time with a call to
:c:func:`drm_mode_connector_attach_encoder()`. The driver must
also set the :c:type:`struct drm_connector <drm_connector>`
encoder field to point to the attached encoder.

Finally, drivers must initialize the connectors state change detection
with a call to :c:func:`drm_kms_helper_poll_init()`. If at least
one connector is pollable but can't generate hotplug interrupts
(indicated by the DRM_CONNECTOR_POLL_CONNECT and
DRM_CONNECTOR_POLL_DISCONNECT connector flags), a delayed work will
automatically be queued to periodically poll for changes. Connectors
that can generate hotplug interrupts must be marked with the
DRM_CONNECTOR_POLL_HPD flag instead, and their interrupt handler must
call :c:func:`drm_helper_hpd_irq_event()`. The function will
queue a delayed work to check the state of all connectors, but no
periodic polling will be done.

Connector Operations
^^^^^^^^^^^^^^^^^^^^

    **Note**

    Unless otherwise state, all operations are mandatory.

DPMS
''''

void (\*dpms)(struct drm_connector \*connector, int mode);
The DPMS operation sets the power state of a connector. The mode
argument is one of

-  DRM_MODE_DPMS_ON

-  DRM_MODE_DPMS_STANDBY

-  DRM_MODE_DPMS_SUSPEND

-  DRM_MODE_DPMS_OFF

In all but DPMS_ON mode the encoder to which the connector is attached
should put the display in low-power mode by driving its signals
appropriately. If more than one connector is attached to the encoder
care should be taken not to change the power state of other displays as
a side effect. Low-power mode should be propagated to the encoders and
CRTCs when all related connectors are put in low-power mode.

Modes
'''''

int (\*fill_modes)(struct drm_connector \*connector, uint32_t
max_width, uint32_t max_height);
Fill the mode list with all supported modes for the connector. If the
``max_width`` and ``max_height`` arguments are non-zero, the
implementation must ignore all modes wider than ``max_width`` or higher
than ``max_height``.

The connector must also fill in this operation its display_info
width_mm and height_mm fields with the connected display physical size
in millimeters. The fields should be set to 0 if the value isn't known
or is not applicable (for instance for projector devices).

Connection Status
'''''''''''''''''

The connection status is updated through polling or hotplug events when
supported (see ?). The status value is reported to userspace through
ioctls and must not be used inside the driver, as it only gets
initialized by a call to :c:func:`drm_mode_getconnector()` from
userspace.

enum drm_connector_status (\*detect)(struct drm_connector
\*connector, bool force);
Check to see if anything is attached to the connector. The ``force``
parameter is set to false whilst polling or to true when checking the
connector due to user request. ``force`` can be used by the driver to
avoid expensive, destructive operations during automated probing.

Return connector_status_connected if something is connected to the
connector, connector_status_disconnected if nothing is connected and
connector_status_unknown if the connection state isn't known.

Drivers should only return connector_status_connected if the
connection status has really been probed as connected. Connectors that
can't detect the connection status, or failed connection status probes,
should return connector_status_unknown.

Cleanup
-------

The DRM core manages its objects' lifetime. When an object is not needed
anymore the core calls its destroy function, which must clean up and
free every resource allocated for the object. Every
:c:func:`drm_\*_init()` call must be matched with a corresponding
:c:func:`drm_\*_cleanup()` call to cleanup CRTCs
(:c:func:`drm_crtc_cleanup()`), planes
(:c:func:`drm_plane_cleanup()`), encoders
(:c:func:`drm_encoder_cleanup()`) and connectors
(:c:func:`drm_connector_cleanup()`). Furthermore, connectors that
have been added to sysfs must be removed by a call to
:c:func:`drm_connector_unregister()` before calling
:c:func:`drm_connector_cleanup()`.

Connectors state change detection must be cleanup up with a call to
:c:func:`drm_kms_helper_poll_fini()`.

Output discovery and initialization example
-------------------------------------------

::

    void intel_crt_init(struct drm_device *dev)
    {
        struct drm_connector *connector;
        struct intel_output *intel_output;

        intel_output = kzalloc(sizeof(struct intel_output), GFP_KERNEL);
        if (!intel_output)
            return;

        connector = &intel_output->base;
        drm_connector_init(dev, &intel_output->base,
                   &intel_crt_connector_funcs, DRM_MODE_CONNECTOR_VGA);

        drm_encoder_init(dev, &intel_output->enc, &intel_crt_enc_funcs,
                 DRM_MODE_ENCODER_DAC);

        drm_mode_connector_attach_encoder(&intel_output->base,
                          &intel_output->enc);

        /* Set up the DDC bus. */
        intel_output->ddc_bus = intel_i2c_create(dev, GPIOA, "CRTDDC_A");
        if (!intel_output->ddc_bus) {
            dev_printk(KERN_ERR, &dev->pdev->dev, "DDC bus registration "
                   "failed.\n");
            return;
        }

        intel_output->type = INTEL_OUTPUT_ANALOG;
        connector->interlace_allowed = 0;
        connector->doublescan_allowed = 0;

        drm_encoder_helper_add(&intel_output->enc, &intel_crt_helper_funcs);
        drm_connector_helper_add(connector, &intel_crt_connector_helper_funcs);

        drm_connector_register(connector);
    }

In the example above (taken from the i915 driver), a CRTC, connector and
encoder combination is created. A device-specific i2c bus is also
created for fetching EDID data and performing monitor detection. Once
the process is complete, the new connector is registered with sysfs to
make its properties available to applications.

KMS API Functions
-----------------

.. kernel-doc:: drivers/gpu/drm/drm_crtc.c
   :export:

KMS Data Structures
-------------------

.. kernel-doc:: include/drm/drm_crtc.h
   :internal:

KMS Locking
-----------

.. kernel-doc:: drivers/gpu/drm/drm_modeset_lock.c
   :doc: kms locking

.. kernel-doc:: include/drm/drm_modeset_lock.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_modeset_lock.c
   :export:

Mode Setting Helper Functions
=============================

The plane, CRTC, encoder and connector functions provided by the drivers
implement the DRM API. They're called by the DRM core and ioctl handlers
to handle device state changes and configuration request. As
implementing those functions often requires logic not specific to
drivers, mid-layer helper functions are available to avoid duplicating
boilerplate code.

The DRM core contains one mid-layer implementation. The mid-layer
provides implementations of several plane, CRTC, encoder and connector
functions (called from the top of the mid-layer) that pre-process
requests and call lower-level functions provided by the driver (at the
bottom of the mid-layer). For instance, the
:c:func:`drm_crtc_helper_set_config()` function can be used to
fill the :c:type:`struct drm_crtc_funcs <drm_crtc_funcs>`
set_config field. When called, it will split the set_config operation
in smaller, simpler operations and call the driver to handle them.

To use the mid-layer, drivers call
:c:func:`drm_crtc_helper_add()`,
:c:func:`drm_encoder_helper_add()` and
:c:func:`drm_connector_helper_add()` functions to install their
mid-layer bottom operations handlers, and fill the :c:type:`struct
drm_crtc_funcs <drm_crtc_funcs>`, :c:type:`struct
drm_encoder_funcs <drm_encoder_funcs>` and :c:type:`struct
drm_connector_funcs <drm_connector_funcs>` structures with
pointers to the mid-layer top API functions. Installing the mid-layer
bottom operation handlers is best done right after registering the
corresponding KMS object.

The mid-layer is not split between CRTC, encoder and connector
operations. To use it, a driver must provide bottom functions for all of
the three KMS entities.

Atomic Modeset Helper Functions Reference
-----------------------------------------

Overview
^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: overview

Implementing Asynchronous Atomic Commit
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: implementing nonblocking commit

Atomic State Reset and Initialization
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :doc: atomic state reset and initialization

.. kernel-doc:: include/drm/drm_atomic_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_atomic_helper.c
   :export:

Modeset Helper Reference for Common Vtables
-------------------------------------------

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :internal:

.. kernel-doc:: include/drm/drm_modeset_helper_vtables.h
   :doc: overview

Legacy CRTC/Modeset Helper Functions Reference
----------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_crtc_helper.c
   :doc: overview

Output Probing Helper Functions Reference
-----------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :doc: output probing helper overview

.. kernel-doc:: drivers/gpu/drm/drm_probe_helper.c
   :export:

fbdev Helper Functions Reference
--------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_fb_helper.c
   :doc: fbdev helpers

.. kernel-doc:: drivers/gpu/drm/drm_fb_helper.c
   :export:

.. kernel-doc:: include/drm/drm_fb_helper.h
   :internal:

Framebuffer CMA Helper Functions Reference
------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_fb_cma_helper.c
   :doc: framebuffer cma helper functions

.. kernel-doc:: drivers/gpu/drm/drm_fb_cma_helper.c
   :export:

Display Port Helper Functions Reference
---------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_dp_helper.c
   :doc: dp helpers

.. kernel-doc:: include/drm/drm_dp_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_helper.c
   :export:

Display Port Dual Mode Adaptor Helper Functions Reference
---------------------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_dp_dual_mode_helper.c
   :doc: dp dual mode helpers

.. kernel-doc:: include/drm/drm_dp_dual_mode_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_dual_mode_helper.c
   :export:

Display Port MST Helper Functions Reference
-------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_dp_mst_topology.c
   :doc: dp mst helper

.. kernel-doc:: include/drm/drm_dp_mst_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_dp_mst_topology.c
   :export:

MIPI DSI Helper Functions Reference
-----------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_mipi_dsi.c
   :doc: dsi helpers

.. kernel-doc:: include/drm/drm_mipi_dsi.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_mipi_dsi.c
   :export:

EDID Helper Functions Reference
-------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_edid.c
   :export:

Rectangle Utilities Reference
-----------------------------

.. kernel-doc:: include/drm/drm_rect.h
   :doc: rect utils

.. kernel-doc:: include/drm/drm_rect.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_rect.c
   :export:

Flip-work Helper Reference
--------------------------

.. kernel-doc:: include/drm/drm_flip_work.h
   :doc: flip utils

.. kernel-doc:: include/drm/drm_flip_work.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_flip_work.c
   :export:

HDMI Infoframes Helper Reference
--------------------------------

Strictly speaking this is not a DRM helper library but generally useable
by any driver interfacing with HDMI outputs like v4l or alsa drivers.
But it nicely fits into the overall topic of mode setting helper
libraries and hence is also included here.

.. kernel-doc:: include/linux/hdmi.h
   :internal:

.. kernel-doc:: drivers/video/hdmi.c
   :export:

Plane Helper Reference
----------------------

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_plane_helper.c
   :doc: overview

Tile group
----------

.. kernel-doc:: drivers/gpu/drm/drm_crtc.c
   :doc: Tile group

Bridges
-------

Overview
^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :doc: overview

Default bridge callback sequence
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :doc: bridge callbacks

.. kernel-doc:: drivers/gpu/drm/drm_bridge.c
   :export:

Panel Helper Reference
----------------------

.. kernel-doc:: include/drm/drm_panel.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_panel.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_panel.c
   :doc: drm panel

Simple KMS Helper Reference
---------------------------

.. kernel-doc:: include/drm/drm_simple_kms_helper.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_simple_kms_helper.c
   :doc: overview

KMS Properties
==============

Drivers may need to expose additional parameters to applications than
those described in the previous sections. KMS supports attaching
properties to CRTCs, connectors and planes and offers a userspace API to
list, get and set the property values.

Properties are identified by a name that uniquely defines the property
purpose, and store an associated value. For all property types except
blob properties the value is a 64-bit unsigned integer.

KMS differentiates between properties and property instances. Drivers
first create properties and then create and associate individual
instances of those properties to objects. A property can be instantiated
multiple times and associated with different objects. Values are stored
in property instances, and all other property information are stored in
the property and shared between all instances of the property.

Every property is created with a type that influences how the KMS core
handles the property. Supported property types are

DRM_MODE_PROP_RANGE
    Range properties report their minimum and maximum admissible values.
    The KMS core verifies that values set by application fit in that
    range.

DRM_MODE_PROP_ENUM
    Enumerated properties take a numerical value that ranges from 0 to
    the number of enumerated values defined by the property minus one,
    and associate a free-formed string name to each value. Applications
    can retrieve the list of defined value-name pairs and use the
    numerical value to get and set property instance values.

DRM_MODE_PROP_BITMASK
    Bitmask properties are enumeration properties that additionally
    restrict all enumerated values to the 0..63 range. Bitmask property
    instance values combine one or more of the enumerated bits defined
    by the property.

DRM_MODE_PROP_BLOB
    Blob properties store a binary blob without any format restriction.
    The binary blobs are created as KMS standalone objects, and blob
    property instance values store the ID of their associated blob
    object.

    Blob properties are only used for the connector EDID property and
    cannot be created by drivers.

To create a property drivers call one of the following functions
depending on the property type. All property creation functions take
property flags and name, as well as type-specific arguments.

-  struct drm_property \*drm_property_create_range(struct
   drm_device \*dev, int flags, const char \*name, uint64_t min,
   uint64_t max);
   Create a range property with the given minimum and maximum values.

-  struct drm_property \*drm_property_create_enum(struct drm_device
   \*dev, int flags, const char \*name, const struct
   drm_prop_enum_list \*props, int num_values);
   Create an enumerated property. The ``props`` argument points to an
   array of ``num_values`` value-name pairs.

-  struct drm_property \*drm_property_create_bitmask(struct
   drm_device \*dev, int flags, const char \*name, const struct
   drm_prop_enum_list \*props, int num_values);
   Create a bitmask property. The ``props`` argument points to an array
   of ``num_values`` value-name pairs.

Properties can additionally be created as immutable, in which case they
will be read-only for applications but can be modified by the driver. To
create an immutable property drivers must set the
DRM_MODE_PROP_IMMUTABLE flag at property creation time.

When no array of value-name pairs is readily available at property
creation time for enumerated or range properties, drivers can create the
property using the :c:func:`drm_property_create()` function and
manually add enumeration value-name pairs by calling the
:c:func:`drm_property_add_enum()` function. Care must be taken to
properly specify the property type through the ``flags`` argument.

After creating properties drivers can attach property instances to CRTC,
connector and plane objects by calling the
:c:func:`drm_object_attach_property()`. The function takes a
pointer to the target object, a pointer to the previously created
property and an initial instance value.

Existing KMS Properties
-----------------------

The following table gives description of drm properties exposed by
various modules/drivers.

.. csv-table::
   :header-rows: 1
   :file: kms-properties.csv

Vertical Blanking
=================

Vertical blanking plays a major role in graphics rendering. To achieve
tear-free display, users must synchronize page flips and/or rendering to
vertical blanking. The DRM API offers ioctls to perform page flips
synchronized to vertical blanking and wait for vertical blanking.

The DRM core handles most of the vertical blanking management logic,
which involves filtering out spurious interrupts, keeping race-free
blanking counters, coping with counter wrap-around and resets and
keeping use counts. It relies on the driver to generate vertical
blanking interrupts and optionally provide a hardware vertical blanking
counter. Drivers must implement the following operations.

-  int (\*enable_vblank) (struct drm_device \*dev, int crtc); void
   (\*disable_vblank) (struct drm_device \*dev, int crtc);
   Enable or disable vertical blanking interrupts for the given CRTC.

-  u32 (\*get_vblank_counter) (struct drm_device \*dev, int crtc);
   Retrieve the value of the vertical blanking counter for the given
   CRTC. If the hardware maintains a vertical blanking counter its value
   should be returned. Otherwise drivers can use the
   :c:func:`drm_vblank_count()` helper function to handle this
   operation.

Drivers must initialize the vertical blanking handling core with a call
to :c:func:`drm_vblank_init()` in their load operation.

Vertical blanking interrupts can be enabled by the DRM core or by
drivers themselves (for instance to handle page flipping operations).
The DRM core maintains a vertical blanking use count to ensure that the
interrupts are not disabled while a user still needs them. To increment
the use count, drivers call :c:func:`drm_vblank_get()`. Upon
return vertical blanking interrupts are guaranteed to be enabled.

To decrement the use count drivers call
:c:func:`drm_vblank_put()`. Only when the use count drops to zero
will the DRM core disable the vertical blanking interrupts after a delay
by scheduling a timer. The delay is accessible through the
vblankoffdelay module parameter or the ``drm_vblank_offdelay`` global
variable and expressed in milliseconds. Its default value is 5000 ms.
Zero means never disable, and a negative value means disable
immediately. Drivers may override the behaviour by setting the
:c:type:`struct drm_device <drm_device>`
vblank_disable_immediate flag, which when set causes vblank interrupts
to be disabled immediately regardless of the drm_vblank_offdelay
value. The flag should only be set if there's a properly working
hardware vblank counter present.

When a vertical blanking interrupt occurs drivers only need to call the
:c:func:`drm_handle_vblank()` function to account for the
interrupt.

Resources allocated by :c:func:`drm_vblank_init()` must be freed
with a call to :c:func:`drm_vblank_cleanup()` in the driver unload
operation handler.

Vertical Blanking and Interrupt Handling Functions Reference
------------------------------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_irq.c
   :export:

.. kernel-doc:: include/drm/drmP.h
   :functions: drm_crtc_vblank_waitqueue

Open/Close, File Operations and IOCTLs
======================================

Open and Close
--------------

int (\*firstopen) (struct drm_device \*); void (\*lastclose) (struct
drm_device \*); int (\*open) (struct drm_device \*, struct drm_file
\*); void (\*preclose) (struct drm_device \*, struct drm_file \*);
void (\*postclose) (struct drm_device \*, struct drm_file \*);
    Open and close handlers. None of those methods are mandatory.

The firstopen method is called by the DRM core for legacy UMS (User Mode
Setting) drivers only when an application opens a device that has no
other opened file handle. UMS drivers can implement it to acquire device
resources. KMS drivers can't use the method and must acquire resources
in the load method instead.

Similarly the lastclose method is called when the last application
holding a file handle opened on the device closes it, for both UMS and
KMS drivers. Additionally, the method is also called at module unload
time or, for hot-pluggable devices, when the device is unplugged. The
firstopen and lastclose calls can thus be unbalanced.

The open method is called every time the device is opened by an
application. Drivers can allocate per-file private data in this method
and store them in the struct :c:type:`struct drm_file
<drm_file>` driver_priv field. Note that the open method is
called before firstopen.

The close operation is split into preclose and postclose methods.
Drivers must stop and cleanup all per-file operations in the preclose
method. For instance pending vertical blanking and page flip events must
be cancelled. No per-file operation is allowed on the file handle after
returning from the preclose method.

Finally the postclose method is called as the last step of the close
operation, right before calling the lastclose method if no other open
file handle exists for the device. Drivers that have allocated per-file
private data in the open method should free it here.

The lastclose method should restore CRTC and plane properties to default
value, so that a subsequent open of the device will not inherit state
from the previous user. It can also be used to execute delayed power
switching state changes, e.g. in conjunction with the vga_switcheroo
infrastructure (see ?). Beyond that KMS drivers should not do any
further cleanup. Only legacy UMS drivers might need to clean up device
state so that the vga console or an independent fbdev driver could take
over.

File Operations
---------------

.. kernel-doc:: drivers/gpu/drm/drm_fops.c
   :doc: file operations

.. kernel-doc:: drivers/gpu/drm/drm_fops.c
   :export:

IOCTLs
------

struct drm_ioctl_desc \*ioctls; int num_ioctls;
    Driver-specific ioctls descriptors table.

Driver-specific ioctls numbers start at DRM_COMMAND_BASE. The ioctls
descriptors table is indexed by the ioctl number offset from the base
value. Drivers can use the DRM_IOCTL_DEF_DRV() macro to initialize
the table entries.

::

    DRM_IOCTL_DEF_DRV(ioctl, func, flags)

``ioctl`` is the ioctl name. Drivers must define the DRM_##ioctl and
DRM_IOCTL_##ioctl macros to the ioctl number offset from
DRM_COMMAND_BASE and the ioctl number respectively. The first macro is
private to the device while the second must be exposed to userspace in a
public header.

``func`` is a pointer to the ioctl handler function compatible with the
``drm_ioctl_t`` type.

::

    typedef int drm_ioctl_t(struct drm_device *dev, void *data,
            struct drm_file *file_priv);

``flags`` is a bitmask combination of the following values. It restricts
how the ioctl is allowed to be called.

-  DRM_AUTH - Only authenticated callers allowed

-  DRM_MASTER - The ioctl can only be called on the master file handle

-  DRM_ROOT_ONLY - Only callers with the SYSADMIN capability allowed

-  DRM_CONTROL_ALLOW - The ioctl can only be called on a control
   device

-  DRM_UNLOCKED - The ioctl handler will be called without locking the
   DRM global mutex. This is the enforced default for kms drivers (i.e.
   using the DRIVER_MODESET flag) and hence shouldn't be used any more
   for new drivers.

.. kernel-doc:: drivers/gpu/drm/drm_ioctl.c
   :export:

Legacy Support Code
===================

The section very briefly covers some of the old legacy support code
which is only used by old DRM drivers which have done a so-called
shadow-attach to the underlying device instead of registering as a real
driver. This also includes some of the old generic buffer management and
command submission code. Do not use any of this in new and modern
drivers.

Legacy Suspend/Resume
---------------------

The DRM core provides some suspend/resume code, but drivers wanting full
suspend/resume support should provide save() and restore() functions.
These are called at suspend, hibernate, or resume time, and should
perform any state save or restore required by your device across suspend
or hibernate states.

int (\*suspend) (struct drm_device \*, pm_message_t state); int
(\*resume) (struct drm_device \*);
Those are legacy suspend and resume methods which *only* work with the
legacy shadow-attach driver registration functions. New driver should
use the power management interface provided by their bus type (usually
through the :c:type:`struct device_driver <device_driver>`
dev_pm_ops) and set these methods to NULL.

Legacy DMA Services
-------------------

This should cover how DMA mapping etc. is supported by the core. These
functions are deprecated and should not be used.

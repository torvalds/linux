.. Copyright 2020 DisplayLink (UK) Ltd.

===================
Userland interfaces
===================

The DRM core exports several interfaces to applications, generally
intended to be used through corresponding libdrm wrapper functions. In
addition, drivers export device-specific interfaces for use by userspace
drivers & device-aware applications through ioctls and sysfs files.

External interfaces include: memory mapping, context management, DMA
operations, AGP management, vblank control, fence management, memory
management, and output management.

Cover generic ioctls and sysfs layout here. We only need high-level
info, since man pages should cover the rest.

libdrm Device Lookup
====================

.. kernel-doc:: drivers/gpu/drm/drm_ioctl.c
   :doc: getunique and setversion story


.. _drm_primary_node:

Primary Nodes, DRM Master and Authentication
============================================

.. kernel-doc:: drivers/gpu/drm/drm_auth.c
   :doc: master and authentication

.. kernel-doc:: drivers/gpu/drm/drm_auth.c
   :export:

.. kernel-doc:: include/drm/drm_auth.h
   :internal:


.. _drm_leasing:

DRM Display Resource Leasing
============================

.. kernel-doc:: drivers/gpu/drm/drm_lease.c
   :doc: drm leasing

Open-Source Userspace Requirements
==================================

The DRM subsystem has stricter requirements than most other kernel subsystems on
what the userspace side for new uAPI needs to look like. This section here
explains what exactly those requirements are, and why they exist.

The short summary is that any addition of DRM uAPI requires corresponding
open-sourced userspace patches, and those patches must be reviewed and ready for
merging into a suitable and canonical upstream project.

GFX devices (both display and render/GPU side) are really complex bits of
hardware, with userspace and kernel by necessity having to work together really
closely.  The interfaces, for rendering and modesetting, must be extremely wide
and flexible, and therefore it is almost always impossible to precisely define
them for every possible corner case. This in turn makes it really practically
infeasible to differentiate between behaviour that's required by userspace, and
which must not be changed to avoid regressions, and behaviour which is only an
accidental artifact of the current implementation.

Without access to the full source code of all userspace users that means it
becomes impossible to change the implementation details, since userspace could
depend upon the accidental behaviour of the current implementation in minute
details. And debugging such regressions without access to source code is pretty
much impossible. As a consequence this means:

- The Linux kernel's "no regression" policy holds in practice only for
  open-source userspace of the DRM subsystem. DRM developers are perfectly fine
  if closed-source blob drivers in userspace use the same uAPI as the open
  drivers, but they must do so in the exact same way as the open drivers.
  Creative (ab)use of the interfaces will, and in the past routinely has, lead
  to breakage.

- Any new userspace interface must have an open-source implementation as
  demonstration vehicle.

The other reason for requiring open-source userspace is uAPI review. Since the
kernel and userspace parts of a GFX stack must work together so closely, code
review can only assess whether a new interface achieves its goals by looking at
both sides. Making sure that the interface indeed covers the use-case fully
leads to a few additional requirements:

- The open-source userspace must not be a toy/test application, but the real
  thing. Specifically it needs to handle all the usual error and corner cases.
  These are often the places where new uAPI falls apart and hence essential to
  assess the fitness of a proposed interface.

- The userspace side must be fully reviewed and tested to the standards of that
  userspace project. For e.g. mesa this means piglit testcases and review on the
  mailing list. This is again to ensure that the new interface actually gets the
  job done.  The userspace-side reviewer should also provide an Acked-by on the
  kernel uAPI patch indicating that they believe the proposed uAPI is sound and
  sufficiently documented and validated for userspace's consumption.

- The userspace patches must be against the canonical upstream, not some vendor
  fork. This is to make sure that no one cheats on the review and testing
  requirements by doing a quick fork.

- The kernel patch can only be merged after all the above requirements are met,
  but it **must** be merged to either drm-next or drm-misc-next **before** the
  userspace patches land. uAPI always flows from the kernel, doing things the
  other way round risks divergence of the uAPI definitions and header files.

These are fairly steep requirements, but have grown out from years of shared
pain and experience with uAPI added hastily, and almost always regretted about
just as fast. GFX devices change really fast, requiring a paradigm shift and
entire new set of uAPI interfaces every few years at least. Together with the
Linux kernel's guarantee to keep existing userspace running for 10+ years this
is already rather painful for the DRM subsystem, with multiple different uAPIs
for the same thing co-existing. If we add a few more complete mistakes into the
mix every year it would be entirely unmanageable.

.. _drm_render_node:

Render nodes
============

DRM core provides multiple character-devices for user-space to use.
Depending on which device is opened, user-space can perform a different
set of operations (mainly ioctls). The primary node is always created
and called card<num>. Additionally, a currently unused control node,
called controlD<num> is also created. The primary node provides all
legacy operations and historically was the only interface used by
userspace. With KMS, the control node was introduced. However, the
planned KMS control interface has never been written and so the control
node stays unused to date.

With the increased use of offscreen renderers and GPGPU applications,
clients no longer require running compositors or graphics servers to
make use of a GPU. But the DRM API required unprivileged clients to
authenticate to a DRM-Master prior to getting GPU access. To avoid this
step and to grant clients GPU access without authenticating, render
nodes were introduced. Render nodes solely serve render clients, that
is, no modesetting or privileged ioctls can be issued on render nodes.
Only non-global rendering commands are allowed. If a driver supports
render nodes, it must advertise it via the DRIVER_RENDER DRM driver
capability. If not supported, the primary node must be used for render
clients together with the legacy drmAuth authentication procedure.

If a driver advertises render node support, DRM core will create a
separate render node called renderD<num>. There will be one render node
per device. No ioctls except PRIME-related ioctls will be allowed on
this node. Especially GEM_OPEN will be explicitly prohibited. For a
complete list of driver-independent ioctls that can be used on render
nodes, see the ioctls marked DRM_RENDER_ALLOW in drm_ioctl.c  Render
nodes are designed to avoid the buffer-leaks, which occur if clients
guess the flink names or mmap offsets on the legacy interface.
Additionally to this basic interface, drivers must mark their
driver-dependent render-only ioctls as DRM_RENDER_ALLOW so render
clients can use them. Driver authors must be careful not to allow any
privileged ioctls on render nodes.

With render nodes, user-space can now control access to the render node
via basic file-system access-modes. A running graphics server which
authenticates clients on the privileged primary/legacy node is no longer
required. Instead, a client can open the render node and is immediately
granted GPU access. Communication between clients (or servers) is done
via PRIME. FLINK from render node to legacy node is not supported. New
clients must not use the insecure FLINK interface.

Besides dropping all modeset/global ioctls, render nodes also drop the
DRM-Master concept. There is no reason to associate render clients with
a DRM-Master as they are independent of any graphics server. Besides,
they must work without any running master, anyway. Drivers must be able
to run without a master object if they support render nodes. If, on the
other hand, a driver requires shared state between clients which is
visible to user-space and accessible beyond open-file boundaries, they
cannot support render nodes.

Device Hot-Unplug
=================

.. note::
   The following is the plan. Implementation is not there yet
   (2020 May).

Graphics devices (display and/or render) may be connected via USB (e.g.
display adapters or docking stations) or Thunderbolt (e.g. eGPU). An end
user is able to hot-unplug this kind of devices while they are being
used, and expects that the very least the machine does not crash. Any
damage from hot-unplugging a DRM device needs to be limited as much as
possible and userspace must be given the chance to handle it if it wants
to. Ideally, unplugging a DRM device still lets a desktop continue to
run, but that is going to need explicit support throughout the whole
graphics stack: from kernel and userspace drivers, through display
servers, via window system protocols, and in applications and libraries.

Other scenarios that should lead to the same are: unrecoverable GPU
crash, PCI device disappearing off the bus, or forced unbind of a driver
from the physical device.

In other words, from userspace perspective everything needs to keep on
working more or less, until userspace stops using the disappeared DRM
device and closes it completely. Userspace will learn of the device
disappearance from the device removed uevent, ioctls returning ENODEV
(or driver-specific ioctls returning driver-specific things), or open()
returning ENXIO.

Only after userspace has closed all relevant DRM device and dmabuf file
descriptors and removed all mmaps, the DRM driver can tear down its
instance for the device that no longer exists. If the same physical
device somehow comes back in the mean time, it shall be a new DRM
device.

Similar to PIDs, chardev minor numbers are not recycled immediately. A
new DRM device always picks the next free minor number compared to the
previous one allocated, and wraps around when minor numbers are
exhausted.

The goal raises at least the following requirements for the kernel and
drivers.

Requirements for KMS UAPI
-------------------------

- KMS connectors must change their status to disconnected.

- Legacy modesets and pageflips, and atomic commits, both real and
  TEST_ONLY, and any other ioctls either fail with ENODEV or fake
  success.

- Pending non-blocking KMS operations deliver the DRM events userspace
  is expecting. This applies also to ioctls that faked success.

- open() on a device node whose underlying device has disappeared will
  fail with ENXIO.

- Attempting to create a DRM lease on a disappeared DRM device will
  fail with ENODEV. Existing DRM leases remain and work as listed
  above.

Requirements for Render and Cross-Device UAPI
---------------------------------------------

- All GPU jobs that can no longer run must have their fences
  force-signalled to avoid inflicting hangs on userspace.
  The associated error code is ENODEV.

- Some userspace APIs already define what should happen when the device
  disappears (OpenGL, GL ES: `GL_KHR_robustness`_; `Vulkan`_:
  VK_ERROR_DEVICE_LOST; etc.). DRM drivers are free to implement this
  behaviour the way they see best, e.g. returning failures in
  driver-specific ioctls and handling those in userspace drivers, or
  rely on uevents, and so on.

- dmabuf which point to memory that has disappeared will either fail to
  import with ENODEV or continue to be successfully imported if it would
  have succeeded before the disappearance. See also about memory maps
  below for already imported dmabufs.

- Attempting to import a dmabuf to a disappeared device will either fail
  with ENODEV or succeed if it would have succeeded without the
  disappearance.

- open() on a device node whose underlying device has disappeared will
  fail with ENXIO.

.. _GL_KHR_robustness: https://www.khronos.org/registry/OpenGL/extensions/KHR/KHR_robustness.txt
.. _Vulkan: https://www.khronos.org/vulkan/

Requirements for Memory Maps
----------------------------

Memory maps have further requirements that apply to both existing maps
and maps created after the device has disappeared. If the underlying
memory disappears, the map is created or modified such that reads and
writes will still complete successfully but the result is undefined.
This applies to both userspace mmap()'d memory and memory pointed to by
dmabuf which might be mapped to other devices (cross-device dmabuf
imports).

Raising SIGBUS is not an option, because userspace cannot realistically
handle it. Signal handlers are global, which makes them extremely
difficult to use correctly from libraries like those that Mesa produces.
Signal handlers are not composable, you can't have different handlers
for GPU1 and GPU2 from different vendors, and a third handler for
mmapped regular files. Threads cause additional pain with signal
handling as well.

Device reset
============

The GPU stack is really complex and is prone to errors, from hardware bugs,
faulty applications and everything in between the many layers. Some errors
require resetting the device in order to make the device usable again. This
section describes the expectations for DRM and usermode drivers when a
device resets and how to propagate the reset status.

Device resets can not be disabled without tainting the kernel, which can lead to
hanging the entire kernel through shrinkers/mmu_notifiers. Userspace role in
device resets is to propagate the message to the application and apply any
special policy for blocking guilty applications, if any. Corollary is that
debugging a hung GPU context require hardware support to be able to preempt such
a GPU context while it's stopped.

Kernel Mode Driver
------------------

The KMD is responsible for checking if the device needs a reset, and to perform
it as needed. Usually a hang is detected when a job gets stuck executing.

Propagation of errors to userspace has proven to be tricky since it goes in
the opposite direction of the usual flow of commands. Because of this vendor
independent error handling was added to the &dma_fence object, this way drivers
can add an error code to their fences before signaling them. See function
dma_fence_set_error() on how to do this and for examples of error codes to use.

The DRM scheduler also allows setting error codes on all pending fences when
hardware submissions are restarted after an reset. Error codes are also
forwarded from the hardware fence to the scheduler fence to bubble up errors
to the higher levels of the stack and eventually userspace.

Fence errors can be queried by userspace through the generic SYNC_IOC_FILE_INFO
IOCTL as well as through driver specific interfaces.

Additional to setting fence errors drivers should also keep track of resets per
context, the DRM scheduler provides the drm_sched_entity_error() function as
helper for this use case. After a reset, KMD should reject new command
submissions for affected contexts.

User Mode Driver
----------------

After command submission, UMD should check if the submission was accepted or
rejected. After a reset, KMD should reject submissions, and UMD can issue an
ioctl to the KMD to check the reset status, and this can be checked more often
if the UMD requires it. After detecting a reset, UMD will then proceed to report
it to the application using the appropriate API error code, as explained in the
section below about robustness.

Robustness
----------

The only way to try to keep a graphical API context working after a reset is if
it complies with the robustness aspects of the graphical API that it is using.

Graphical APIs provide ways to applications to deal with device resets. However,
there is no guarantee that the app will use such features correctly, and a
userspace that doesn't support robust interfaces (like a non-robust
OpenGL context or API without any robustness support like libva) leave the
robustness handling entirely to the userspace driver. There is no strong
community consensus on what the userspace driver should do in that case,
since all reasonable approaches have some clear downsides.

OpenGL
~~~~~~

Apps using OpenGL should use the available robust interfaces, like the
extension ``GL_ARB_robustness`` (or ``GL_EXT_robustness`` for OpenGL ES). This
interface tells if a reset has happened, and if so, all the context state is
considered lost and the app proceeds by creating new ones. There's no consensus
on what to do to if robustness is not in use.

Vulkan
~~~~~~

Apps using Vulkan should check for ``VK_ERROR_DEVICE_LOST`` for submissions.
This error code means, among other things, that a device reset has happened and
it needs to recreate the contexts to keep going.

Reporting causes of resets
--------------------------

Apart from propagating the reset through the stack so apps can recover, it's
really useful for driver developers to learn more about what caused the reset in
the first place. For this, drivers can make use of devcoredump to store relevant
information about the reset and send device wedged event with ``none`` recovery
method (as explained in "Device Wedging" chapter) to notify userspace, so this
information can be collected and added to user bug reports.

Device Wedging
==============

Drivers can optionally make use of device wedged event (implemented as
drm_dev_wedged_event() in DRM subsystem), which notifies userspace of 'wedged'
(hanged/unusable) state of the DRM device through a uevent. This is useful
especially in cases where the device is no longer operating as expected and has
become unrecoverable from driver context. Purpose of this implementation is to
provide drivers a generic way to recover the device with the help of userspace
intervention, without taking any drastic measures (like resetting or
re-enumerating the full bus, on which the underlying physical device is sitting)
in the driver.

A 'wedged' device is basically a device that is declared dead by the driver
after exhausting all possible attempts to recover it from driver context. The
uevent is the notification that is sent to userspace along with a hint about
what could possibly be attempted to recover the device from userspace and bring
it back to usable state. Different drivers may have different ideas of a
'wedged' device depending on hardware implementation of the underlying physical
device, and hence the vendor agnostic nature of the event. It is up to the
drivers to decide when they see the need for device recovery and how they want
to recover from the available methods.

Driver prerequisites
--------------------

The driver, before opting for recovery, needs to make sure that the 'wedged'
device doesn't harm the system as a whole by taking care of the prerequisites.
Necessary actions must include disabling DMA to system memory as well as any
communication channels with other devices. Further, the driver must ensure
that all dma_fences are signalled and any device state that the core kernel
might depend on is cleaned up. All existing mmaps should be invalidated and
page faults should be redirected to a dummy page. Once the event is sent, the
device must be kept in 'wedged' state until the recovery is performed. New
accesses to the device (IOCTLs) should be rejected, preferably with an error
code that resembles the type of failure the device has encountered. This will
signify the reason for wedging, which can be reported to the application if
needed.

Recovery
--------

Current implementation defines three recovery methods, out of which, drivers
can use any one, multiple or none. Method(s) of choice will be sent in the
uevent environment as ``WEDGED=<method1>[,..,<methodN>]`` in order of less to
more side-effects. If driver is unsure about recovery or method is unknown
(like soft/hard system reboot, firmware flashing, physical device replacement
or any other procedure which can't be attempted on the fly), ``WEDGED=unknown``
will be sent instead.

Userspace consumers can parse this event and attempt recovery as per the
following expectations.

    =============== ========================================
    Recovery method Consumer expectations
    =============== ========================================
    none            optional telemetry collection
    rebind          unbind + bind driver
    bus-reset       unbind + bus reset/re-enumeration + bind
    unknown         consumer policy
    =============== ========================================

The only exception to this is ``WEDGED=none``, which signifies that the device
was temporarily 'wedged' at some point but was recovered from driver context
using device specific methods like reset. No explicit recovery is expected from
the consumer in this case, but it can still take additional steps like gathering
telemetry information (devcoredump, syslog). This is useful because the first
hang is usually the most critical one which can result in consequential hangs or
complete wedging.

Consumer prerequisites
----------------------

It is the responsibility of the consumer to make sure that the device or its
resources are not in use by any process before attempting recovery. With IOCTLs
erroring out, all device memory should be unmapped and file descriptors should
be closed to prevent leaks or undefined behaviour. The idea here is to clear the
device of all user context beforehand and set the stage for a clean recovery.

Example
-------

Udev rule::

    SUBSYSTEM=="drm", ENV{WEDGED}=="rebind", DEVPATH=="*/drm/card[0-9]",
    RUN+="/path/to/rebind.sh $env{DEVPATH}"

Recovery script::

    #!/bin/sh

    DEVPATH=$(readlink -f /sys/$1/device)
    DEVICE=$(basename $DEVPATH)
    DRIVER=$(readlink -f $DEVPATH/driver)

    echo -n $DEVICE > $DRIVER/unbind
    echo -n $DEVICE > $DRIVER/bind

Customization
-------------

Although basic recovery is possible with a simple script, consumers can define
custom policies around recovery. For example, if the driver supports multiple
recovery methods, consumers can opt for the suitable one depending on scenarios
like repeat offences or vendor specific failures. Consumers can also choose to
have the device available for debugging or telemetry collection and base their
recovery decision on the findings. This is useful especially when the driver is
unsure about recovery or method is unknown.

.. _drm_driver_ioctl:

IOCTL Support on Device Nodes
=============================

.. kernel-doc:: drivers/gpu/drm/drm_ioctl.c
   :doc: driver specific ioctls

Recommended IOCTL Return Values
-------------------------------

In theory a driver's IOCTL callback is only allowed to return very few error
codes. In practice it's good to abuse a few more. This section documents common
practice within the DRM subsystem:

ENOENT:
        Strictly this should only be used when a file doesn't exist e.g. when
        calling the open() syscall. We reuse that to signal any kind of object
        lookup failure, e.g. for unknown GEM buffer object handles, unknown KMS
        object handles and similar cases.

ENOSPC:
        Some drivers use this to differentiate "out of kernel memory" from "out
        of VRAM". Sometimes also applies to other limited gpu resources used for
        rendering (e.g. when you have a special limited compression buffer).
        Sometimes resource allocation/reservation issues in command submission
        IOCTLs are also signalled through EDEADLK.

        Simply running out of kernel/system memory is signalled through ENOMEM.

EPERM/EACCES:
        Returned for an operation that is valid, but needs more privileges.
        E.g. root-only or much more common, DRM master-only operations return
        this when called by unpriviledged clients. There's no clear
        difference between EACCES and EPERM.

ENODEV:
        The device is not present anymore or is not yet fully initialized.

EOPNOTSUPP:
        Feature (like PRIME, modesetting, GEM) is not supported by the driver.

ENXIO:
        Remote failure, either a hardware transaction (like i2c), but also used
        when the exporting driver of a shared dma-buf or fence doesn't support a
        feature needed.

EINTR:
        DRM drivers assume that userspace restarts all IOCTLs. Any DRM IOCTL can
        return EINTR and in such a case should be restarted with the IOCTL
        parameters left unchanged.

EIO:
        The GPU died and couldn't be resurrected through a reset. Modesetting
        hardware failures are signalled through the "link status" connector
        property.

EINVAL:
        Catch-all for anything that is an invalid argument combination which
        cannot work.

IOCTL also use other error codes like ETIME, EFAULT, EBUSY, ENOTTY but their
usage is in line with the common meanings. The above list tries to just document
DRM specific patterns. Note that ENOTTY has the slightly unintuitive meaning of
"this IOCTL does not exist", and is used exactly as such in DRM.

.. kernel-doc:: include/drm/drm_ioctl.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_ioctl.c
   :export:

.. kernel-doc:: drivers/gpu/drm/drm_ioc32.c
   :export:

Testing and validation
======================

Testing Requirements for userspace API
--------------------------------------

New cross-driver userspace interface extensions, like new IOCTL, new KMS
properties, new files in sysfs or anything else that constitutes an API change
should have driver-agnostic testcases in IGT for that feature, if such a test
can be reasonably made using IGT for the target hardware.

Validating changes with IGT
---------------------------

There's a collection of tests that aims to cover the whole functionality of
DRM drivers and that can be used to check that changes to DRM drivers or the
core don't regress existing functionality. This test suite is called IGT and
its code and instructions to build and run can be found in
https://gitlab.freedesktop.org/drm/igt-gpu-tools/.

Using VKMS to test DRM API
--------------------------

VKMS is a software-only model of a KMS driver that is useful for testing
and for running compositors. VKMS aims to enable a virtual display without
the need for a hardware display capability. These characteristics made VKMS
a perfect tool for validating the DRM core behavior and also support the
compositor developer. VKMS makes it possible to test DRM functions in a
virtual machine without display, simplifying the validation of some of the
core changes.

To Validate changes in DRM API with VKMS, start setting the kernel: make
sure to enable VKMS module; compile the kernel with the VKMS enabled and
install it in the target machine. VKMS can be run in a Virtual Machine
(QEMU, virtme or similar). It's recommended the use of KVM with the minimum
of 1GB of RAM and four cores.

It's possible to run the IGT-tests in a VM in two ways:

	1. Use IGT inside a VM
	2. Use IGT from the host machine and write the results in a shared directory.

Following is an example of using a VM with a shared directory with
the host machine to run igt-tests. This example uses virtme::

	$ virtme-run --rwdir /path/for/shared_dir --kdir=path/for/kernel/directory --mods=auto

Run the igt-tests in the guest machine. This example runs the 'kms_flip'
tests::

	$ /path/for/igt-gpu-tools/scripts/run-tests.sh -p -s -t "kms_flip.*" -v

In this example, instead of building the igt_runner, Piglit is used
(-p option). It creates an HTML summary of the test results and saves
them in the folder "igt-gpu-tools/results". It executes only the igt-tests
matching the -t option.

Display CRC Support
-------------------

.. kernel-doc:: drivers/gpu/drm/drm_debugfs_crc.c
   :doc: CRC ABI

.. kernel-doc:: drivers/gpu/drm/drm_debugfs_crc.c
   :export:

Debugfs Support
---------------

.. kernel-doc:: include/drm/drm_debugfs.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_debugfs.c
   :export:

Sysfs Support
=============

.. kernel-doc:: drivers/gpu/drm/drm_sysfs.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/drm_sysfs.c
   :export:


VBlank event handling
=====================

The DRM core exposes two vertical blank related ioctls:

:c:macro:`DRM_IOCTL_WAIT_VBLANK`
    This takes a struct drm_wait_vblank structure as its argument, and
    it is used to block or request a signal when a specified vblank
    event occurs.

:c:macro:`DRM_IOCTL_MODESET_CTL`
    This was only used for user-mode-settind drivers around modesetting
    changes to allow the kernel to update the vblank interrupt after
    mode setting, since on many devices the vertical blank counter is
    reset to 0 at some point during modeset. Modern drivers should not
    call this any more since with kernel mode setting it is a no-op.

Userspace API Structures
========================

.. kernel-doc:: include/uapi/drm/drm_mode.h
   :doc: overview

.. _crtc_index:

CRTC index
----------

CRTC's have both an object ID and an index, and they are not the same thing.
The index is used in cases where a densely packed identifier for a CRTC is
needed, for instance a bitmask of CRTC's. The member possible_crtcs of struct
drm_mode_get_plane is an example.

:c:macro:`DRM_IOCTL_MODE_GETRESOURCES` populates a structure with an array of
CRTC ID's, and the CRTC index is its position in this array.

.. kernel-doc:: include/uapi/drm/drm.h
   :internal:

.. kernel-doc:: include/uapi/drm/drm_mode.h
   :internal:


dma-buf interoperability
========================

Please see Documentation/userspace-api/dma-buf-alloc-exchange.rst for
information on how dma-buf is integrated and exposed within DRM.

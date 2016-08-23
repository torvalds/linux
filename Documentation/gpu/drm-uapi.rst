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


Primary Nodes, DRM Master and Authentication
============================================

.. kernel-doc:: drivers/gpu/drm/drm_auth.c
   :doc: master and authentication

.. kernel-doc:: drivers/gpu/drm/drm_auth.c
   :export:

.. kernel-doc:: include/drm/drm_auth.h
   :internal:

Open-Source Userspace Requirements
==================================

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
this node. Especially GEM_OPEN will be explicitly prohibited. Render
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

VBlank event handling
=====================

The DRM core exposes two vertical blank related ioctls:

DRM_IOCTL_WAIT_VBLANK
    This takes a struct drm_wait_vblank structure as its argument, and
    it is used to block or request a signal when a specified vblank
    event occurs.

DRM_IOCTL_MODESET_CTL
    This was only used for user-mode-settind drivers around modesetting
    changes to allow the kernel to update the vblank interrupt after
    mode setting, since on many devices the vertical blank counter is
    reset to 0 at some point during modeset. Modern drivers should not
    call this any more since with kernel mode setting it is a no-op.

This second part of the GPU Driver Developer's Guide documents driver
code, implementation details and also all the driver-specific userspace
interfaces. Especially since all hardware-acceleration interfaces to
userspace are driver specific for efficiency and other reasons these
interfaces can be rather substantial. Hence every driver has its own
chapter.

=========================
Kernel Mode Setting (KMS)
=========================

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

Modeset Base Object Abstraction
===============================

.. kernel-doc:: include/drm/drm_mode_object.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_mode_object.c
   :export:

KMS Data Structures
===================

.. kernel-doc:: include/drm/drm_crtc.h
   :internal:

KMS API Functions
=================

.. kernel-doc:: drivers/gpu/drm/drm_crtc.c
   :export:

Atomic Mode Setting Function Reference
======================================

.. kernel-doc:: drivers/gpu/drm/drm_atomic.c
   :export:

.. kernel-doc:: include/drm/drm_atomic.h
   :internal:

Frame Buffer Abstraction
========================

.. kernel-doc:: drivers/gpu/drm/drm_framebuffer.c
   :doc: overview

Frame Buffer Functions Reference
--------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_framebuffer.c
   :export:

.. kernel-doc:: include/drm/drm_framebuffer.h
   :internal:

DRM Format Handling
===================

.. kernel-doc:: drivers/gpu/drm/drm_fourcc.c
   :export:

Dumb Buffer Objects
===================

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

Plane Abstraction
=================

.. kernel-doc:: drivers/gpu/drm/drm_plane.c
   :doc: overview

Plane Functions Reference
-------------------------

.. kernel-doc:: include/drm/drm_plane.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_plane.c
   :export:

Display Modes Function Reference
================================

.. kernel-doc:: include/drm/drm_modes.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_modes.c
   :export:

Connector Abstraction
=====================

.. kernel-doc:: drivers/gpu/drm/drm_connector.c
   :doc: overview

Connector Functions Reference
-----------------------------

.. kernel-doc:: include/drm/drm_connector.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_connector.c
   :export:

Encoder Abstraction
===================

.. kernel-doc:: drivers/gpu/drm/drm_encoder.c
   :doc: overview

Encoder Functions Reference
---------------------------

.. kernel-doc:: include/drm/drm_encoder.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_encoder.c
   :export:

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
~~~~~~~~~~~~~~~~~~~

A KMS device must create and register at least one struct
:c:type:`struct drm_crtc <drm_crtc>` instance. The instance is
allocated and zeroed by the driver, possibly as part of a larger
structure, and registered with a call to :c:func:`drm_crtc_init()`
with a pointer to CRTC functions.


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

.. code-block:: c

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

KMS Locking
===========

.. kernel-doc:: drivers/gpu/drm/drm_modeset_lock.c
   :doc: kms locking

.. kernel-doc:: include/drm/drm_modeset_lock.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_modeset_lock.c
   :export:

KMS Properties
==============

Property Types and Blob Property Support
----------------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_property.c
   :doc: overview

.. kernel-doc:: include/drm/drm_property.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_property.c
   :export:

Plane Composition Properties
----------------------------

.. kernel-doc:: drivers/gpu/drm/drm_blend.c
   :doc: overview

.. kernel-doc:: drivers/gpu/drm/drm_blend.c
   :export:

Color Management Properties
---------------------------

.. kernel-doc:: drivers/gpu/drm/drm_color_mgmt.c
   :doc: overview

.. kernel-doc:: include/drm/drm_color_mgmt.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/drm_color_mgmt.c
   :export:

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

.. kernel-doc:: include/drm/drm_irq.h
   :internal:

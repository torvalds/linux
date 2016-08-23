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
~~~~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~~~~~~

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

Blending and Z-Position properties
----------------------------------

.. kernel-doc:: drivers/gpu/drm/drm_blend.c
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

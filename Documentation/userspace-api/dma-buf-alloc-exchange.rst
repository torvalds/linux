.. SPDX-License-Identifier: GPL-2.0
.. Copyright 2021-2023 Collabora Ltd.

========================
Exchanging pixel buffers
========================

As originally designed, the Linux graphics subsystem had extremely limited
support for sharing pixel-buffer allocations between processes, devices, and
subsystems. Modern systems require extensive integration between all three
classes; this document details how applications and kernel subsystems should
approach this sharing for two-dimensional image data.

It is written with reference to the DRM subsystem for GPU and display devices,
V4L2 for media devices, and also to Vulkan, EGL and Wayland, for userspace
support, however any other subsystems should also follow this design and advice.


Glossary of terms
=================

.. glossary::

    image:
      Conceptually a two-dimensional array of pixels. The pixels may be stored
      in one or more memory buffers. Has width and height in pixels, pixel
      format and modifier (implicit or explicit).

    row:
      A span along a single y-axis value, e.g. from co-ordinates (0,100) to
      (200,100).

    scanline:
      Synonym for row.

    column:
      A span along a single x-axis value, e.g. from co-ordinates (100,0) to
      (100,100).

    memory buffer:
      A piece of memory for storing (parts of) pixel data. Has stride and size
      in bytes and at least one handle in some API. May contain one or more
      planes.

    plane:
      A two-dimensional array of some or all of an image's color and alpha
      channel values.

    pixel:
      A picture element. Has a single color value which is defined by one or
      more color channels values, e.g. R, G and B, or Y, Cb and Cr. May also
      have an alpha value as an additional channel.

    pixel data:
      Bytes or bits that represent some or all of the color/alpha channel values
      of a pixel or an image. The data for one pixel may be spread over several
      planes or memory buffers depending on format and modifier.

    color value:
      A tuple of numbers, representing a color. Each element in the tuple is a
      color channel value.

    color channel:
      One of the dimensions in a color model. For example, RGB model has
      channels R, G, and B. Alpha channel is sometimes counted as a color
      channel as well.

    pixel format:
      A description of how pixel data represents the pixel's color and alpha
      values.

    modifier:
      A description of how pixel data is laid out in memory buffers.

    alpha:
      A value that denotes the color coverage in a pixel. Sometimes used for
      translucency instead.

    stride:
      A value that denotes the relationship between pixel-location co-ordinates
      and byte-offset values. Typically used as the byte offset between two
      pixels at the start of vertically-consecutive tiling blocks. For linear
      layouts, the byte offset between two vertically-adjacent pixels. For
      non-linear formats the stride must be computed in a consistent way, which
      usually is done as-if the layout was linear.

    pitch:
      Synonym for stride.


Formats and modifiers
=====================

Each buffer must have an underlying format. This format describes the color
values provided for each pixel. Although each subsystem has its own format
descriptions (e.g. V4L2 and fbdev), the ``DRM_FORMAT_*`` tokens should be reused
wherever possible, as they are the standard descriptions used for interchange.
These tokens are described in the ``drm_fourcc.h`` file, which is a part of
DRM's uAPI.

Each ``DRM_FORMAT_*`` token describes the translation between a pixel
co-ordinate in an image, and the color values for that pixel contained within
its memory buffers. The number and type of color channels are described:
whether they are RGB or YUV, integer or floating-point, the size of each channel
and their locations within the pixel memory, and the relationship between color
planes.

For example, ``DRM_FORMAT_ARGB8888`` describes a format in which each pixel has
a single 32-bit value in memory. Alpha, red, green, and blue, color channels are
available at 8-bit precision per channel, ordered respectively from most to
least significant bits in little-endian storage. ``DRM_FORMAT_*`` is not
affected by either CPU or device endianness; the byte pattern in memory is
always as described in the format definition, which is usually little-endian.

As a more complex example, ``DRM_FORMAT_NV12`` describes a format in which luma
and chroma YUV samples are stored in separate planes, where the chroma plane is
stored at half the resolution in both dimensions (i.e. one U/V chroma
sample is stored for each 2x2 pixel grouping).

Format modifiers describe a translation mechanism between these per-pixel memory
samples, and the actual memory storage for the buffer. The most straightforward
modifier is ``DRM_FORMAT_MOD_LINEAR``, describing a scheme in which each plane
is laid out row-sequentially, from the top-left to the bottom-right corner.
This is considered the baseline interchange format, and most convenient for CPU
access.

Modern hardware employs much more sophisticated access mechanisms, typically
making use of tiled access and possibly also compression. For example, the
``DRM_FORMAT_MOD_VIVANTE_TILED`` modifier describes memory storage where pixels
are stored in 4x4 blocks arranged in row-major ordering, i.e. the first tile in
a plane stores pixels (0,0) to (3,3) inclusive, and the second tile in a plane
stores pixels (4,0) to (7,3) inclusive.

Some modifiers may modify the number of planes required for an image; for
example, the ``I915_FORMAT_MOD_Y_TILED_CCS`` modifier adds a second plane to RGB
formats in which it stores data about the status of every tile, notably
including whether the tile is fully populated with pixel data, or can be
expanded from a single solid color.

These extended layouts are highly vendor-specific, and even specific to
particular generations or configurations of devices per-vendor. For this reason,
support of modifiers must be explicitly enumerated and negotiated by all users
in order to ensure a compatible and optimal pipeline, as discussed below.


Dimensions and size
===================

Each pixel buffer must be accompanied by logical pixel dimensions. This refers
to the number of unique samples which can be extracted from, or stored to, the
underlying memory storage. For example, even though a 1920x1080
``DRM_FORMAT_NV12`` buffer has a luma plane containing 1920x1080 samples for the Y
component, and 960x540 samples for the U and V components, the overall buffer is
still described as having dimensions of 1920x1080.

The in-memory storage of a buffer is not guaranteed to begin immediately at the
base address of the underlying memory, nor is it guaranteed that the memory
storage is tightly clipped to either dimension.

Each plane must therefore be described with an ``offset`` in bytes, which will be
added to the base address of the memory storage before performing any per-pixel
calculations. This may be used to combine multiple planes into a single memory
buffer; for example, ``DRM_FORMAT_NV12`` may be stored in a single memory buffer
where the luma plane's storage begins immediately at the start of the buffer
with an offset of 0, and the chroma plane's storage follows within the same buffer
beginning from the byte offset for that plane.

Each plane must also have a ``stride`` in bytes, expressing the offset in memory
between two contiguous row. For example, a ``DRM_FORMAT_MOD_LINEAR`` buffer
with dimensions of 1000x1000 may have been allocated as if it were 1024x1000, in
order to allow for aligned access patterns. In this case, the buffer will still
be described with a width of 1000, however the stride will be ``1024 * bpp``,
indicating that there are 24 pixels at the positive extreme of the x axis whose
values are not significant.

Buffers may also be padded further in the y dimension, simply by allocating a
larger area than would ordinarily be required. For example, many media decoders
are not able to natively output buffers of height 1080, but instead require an
effective height of 1088 pixels. In this case, the buffer continues to be
described as having a height of 1080, with the memory allocation for each buffer
being increased to account for the extra padding.


Enumeration
===========

Every user of pixel buffers must be able to enumerate a set of supported formats
and modifiers, described together. Within KMS, this is achieved with the
``IN_FORMATS`` property on each DRM plane, listing the supported DRM formats, and
the modifiers supported for each format. In userspace, this is supported through
the `EGL_EXT_image_dma_buf_import_modifiers`_ extension entrypoints for EGL, the
`VK_EXT_image_drm_format_modifier`_ extension for Vulkan, and the
`zwp_linux_dmabuf_v1`_ extension for Wayland.

Each of these interfaces allows users to query a set of supported
format+modifier combinations.


Negotiation
===========

It is the responsibility of userspace to negotiate an acceptable format+modifier
combination for its usage. This is performed through a simple intersection of
lists. For example, if a user wants to use Vulkan to render an image to be
displayed on a KMS plane, it must:

 - query KMS for the ``IN_FORMATS`` property for the given plane
 - query Vulkan for the supported formats for its physical device, making sure
   to pass the ``VkImageUsageFlagBits`` and ``VkImageCreateFlagBits``
   corresponding to the intended rendering use
 - intersect these formats to determine the most appropriate one
 - for this format, intersect the lists of supported modifiers for both KMS and
   Vulkan, to obtain a final list of acceptable modifiers for that format

This intersection must be performed for all usages. For example, if the user
also wishes to encode the image to a video stream, it must query the media API
it intends to use for encoding for the set of modifiers it supports, and
additionally intersect against this list.

If the intersection of all lists is an empty list, it is not possible to share
buffers in this way, and an alternate strategy must be considered (e.g. using
CPU access routines to copy data between the different uses, with the
corresponding performance cost).

The resulting modifier list is unsorted; the order is not significant.


Allocation
==========

Once userspace has determined an appropriate format, and corresponding list of
acceptable modifiers, it must allocate the buffer. As there is no universal
buffer-allocation interface available at either kernel or userspace level, the
client makes an arbitrary choice of allocation interface such as Vulkan, GBM, or
a media API.

Each allocation request must take, at a minimum: the pixel format, a list of
acceptable modifiers, and the buffer's width and height. Each API may extend
this set of properties in different ways, such as allowing allocation in more
than two dimensions, intended usage patterns, etc.

The component which allocates the buffer will make an arbitrary choice of what
it considers the 'best' modifier within the acceptable list for the requested
allocation, any padding required, and further properties of the underlying
memory buffers such as whether they are stored in system or device-specific
memory, whether or not they are physically contiguous, and their cache mode.
These properties of the memory buffer are not visible to userspace, however the
``dma-heaps`` API is an effort to address this.

After allocation, the client must query the allocator to determine the actual
modifier selected for the buffer, as well as the per-plane offset and stride.
Allocators are not permitted to vary the format in use, to select a modifier not
provided within the acceptable list, nor to vary the pixel dimensions other than
the padding expressed through offset, stride, and size.

Communicating additional constraints, such as alignment of stride or offset,
placement within a particular memory area, etc, is out of scope of dma-buf,
and is not solved by format and modifier tokens.


Import
======

To use a buffer within a different context, device, or subsystem, the user
passes these parameters (format, modifier, width, height, and per-plane offset
and stride) to an importing API.

Each memory buffer is referred to by a buffer handle, which may be unique or
duplicated within an image. For example, a ``DRM_FORMAT_NV12`` buffer may have
the luma and chroma buffers combined into a single memory buffer by use of the
per-plane offset parameters, or they may be completely separate allocations in
memory. For this reason, each import and allocation API must provide a separate
handle for each plane.

Each kernel subsystem has its own types and interfaces for buffer management.
DRM uses GEM buffer objects (BOs), V4L2 has its own references, etc. These types
are not portable between contexts, processes, devices, or subsystems.

To address this, ``dma-buf`` handles are used as the universal interchange for
buffers. Subsystem-specific operations are used to export native buffer handles
to a ``dma-buf`` file descriptor, and to import those file descriptors into a
native buffer handle. dma-buf file descriptors can be transferred between
contexts, processes, devices, and subsystems.

For example, a Wayland media player may use V4L2 to decode a video frame into a
``DRM_FORMAT_NV12`` buffer. This will result in two memory planes (luma and
chroma) being dequeued by the user from V4L2. These planes are then exported to
one dma-buf file descriptor per plane, these descriptors are then sent along
with the metadata (format, modifier, width, height, per-plane offset and stride)
to the Wayland server. The Wayland server will then import these file
descriptors as an EGLImage for use through EGL/OpenGL (ES), a VkImage for use
through Vulkan, or a KMS framebuffer object; each of these import operations
will take the same metadata and convert the dma-buf file descriptors into their
native buffer handles.

Having a non-empty intersection of supported modifiers does not guarantee that
import will succeed into all consumers; they may have constraints beyond those
implied by modifiers which must be satisfied.


Implicit modifiers
==================

The concept of modifiers post-dates all of the subsystems mentioned above. As
such, it has been retrofitted into all of these APIs, and in order to ensure
backwards compatibility, support is needed for drivers and userspace which do
not (yet) support modifiers.

As an example, GBM is used to allocate buffers to be shared between EGL for
rendering and KMS for display. It has two entrypoints for allocating buffers:
``gbm_bo_create`` which only takes the format, width, height, and a usage token,
and ``gbm_bo_create_with_modifiers`` which extends this with a list of modifiers.

In the latter case, the allocation is as discussed above, being provided with a
list of acceptable modifiers that the implementation can choose from (or fail if
it is not possible to allocate within those constraints). In the former case
where modifiers are not provided, the GBM implementation must make its own
choice as to what is likely to be the 'best' layout. Such a choice is entirely
implementation-specific: some will internally use tiled layouts which are not
CPU-accessible if the implementation decides that is a good idea through
whatever heuristic. It is the implementation's responsibility to ensure that
this choice is appropriate.

To support this case where the layout is not known because there is no awareness
of modifiers, a special ``DRM_FORMAT_MOD_INVALID`` token has been defined. This
pseudo-modifier declares that the layout is not known, and that the driver
should use its own logic to determine what the underlying layout may be.

.. note::

  ``DRM_FORMAT_MOD_INVALID`` is a non-zero value. The modifier value zero is
  ``DRM_FORMAT_MOD_LINEAR``, which is an explicit guarantee that the image
  has the linear layout. Care and attention should be taken to ensure that
  zero as a default value is not mixed up with either no modifier or the linear
  modifier. Also note that in some APIs the invalid modifier value is specified
  with an out-of-band flag, like in ``DRM_IOCTL_MODE_ADDFB2``.

There are four cases where this token may be used:
  - during enumeration, an interface may return ``DRM_FORMAT_MOD_INVALID``, either
    as the sole member of a modifier list to declare that explicit modifiers are
    not supported, or as part of a larger list to declare that implicit modifiers
    may be used
  - during allocation, a user may supply ``DRM_FORMAT_MOD_INVALID``, either as the
    sole member of a modifier list (equivalent to not supplying a modifier list
    at all) to declare that explicit modifiers are not supported and must not be
    used, or as part of a larger list to declare that an allocation using implicit
    modifiers is acceptable
  - in a post-allocation query, an implementation may return
    ``DRM_FORMAT_MOD_INVALID`` as the modifier of the allocated buffer to declare
    that the underlying layout is implementation-defined and that an explicit
    modifier description is not available; per the above rules, this may only be
    returned when the user has included ``DRM_FORMAT_MOD_INVALID`` as part of the
    list of acceptable modifiers, or not provided a list
  - when importing a buffer, the user may supply ``DRM_FORMAT_MOD_INVALID`` as the
    buffer modifier (or not supply a modifier) to indicate that the modifier is
    unknown for whatever reason; this is only acceptable when the buffer has
    not been allocated with an explicit modifier

It follows from this that for any single buffer, the complete chain of operations
formed by the producer and all the consumers must be either fully implicit or fully
explicit. For example, if a user wishes to allocate a buffer for use between
GPU, display, and media, but the media API does not support modifiers, then the
user **must not** allocate the buffer with explicit modifiers and attempt to
import the buffer into the media API with no modifier, but either perform the
allocation using implicit modifiers, or allocate the buffer for media use
separately and copy between the two buffers.

As one exception to the above, allocations may be 'upgraded' from implicit
to explicit modifiers. For example, if the buffer is allocated with
``gbm_bo_create`` (taking no modifiers), the user may then query the modifier with
``gbm_bo_get_modifier`` and then use this modifier as an explicit modifier token
if a valid modifier is returned.

When allocating buffers for exchange between different users and modifiers are
not available, implementations are strongly encouraged to use
``DRM_FORMAT_MOD_LINEAR`` for their allocation, as this is the universal baseline
for exchange. However, it is not guaranteed that this will result in the correct
interpretation of buffer content, as implicit modifier operation may still be
subject to driver-specific heuristics.

Any new users - userspace programs and protocols, kernel subsystems, etc -
wishing to exchange buffers must offer interoperability through dma-buf file
descriptors for memory planes, DRM format tokens to describe the format, DRM
format modifiers to describe the layout in memory, at least width and height for
dimensions, and at least offset and stride for each memory plane.

.. _zwp_linux_dmabuf_v1: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
.. _VK_EXT_image_drm_format_modifier: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_image_drm_format_modifier.html
.. _EGL_EXT_image_dma_buf_import_modifiers: https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import_modifiers.txt

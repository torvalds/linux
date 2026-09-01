.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-isp:

************************
Generic V4L2 ISP formats
************************

Generic ISP formats are metadata formats that define a mechanism to pass ISP
parameters and statistics between userspace and drivers in V4L2 buffers. They
are designed to allow extending them in a backward-compatible way.

ISP parameters
==============

The generic ISP configuration parameters format is realized by a defining a
single C structure that contains a header, followed by a binary buffer where
userspace programs a variable number of ISP configuration data block, one for
each supported ISP feature.

The :c:type:`v4l2_isp_buffer` structure defines the buffer header which is
followed by a binary buffer of ISP configuration data. Userspace shall correctly
populate the buffer header with the serialization format version and with the
size (in bytes) of the binary data buffer where it will store the ISP blocks
configuration.

Each *ISP configuration block* is preceded by a header implemented by the
:c:type:`v4l2_isp_block_header` structure, followed by the configuration
parameters for that specific block, defined by the ISP driver specific data
types.

Userspace applications are responsible for correctly populating each block's
header fields (type, flags and size) and the block-specific parameters.

ISP parameters block enabling, disabling and configuration
----------------------------------------------------------

When userspace wants to configure and enable an ISP block it shall fully
populate the block configuration and set the V4L2_ISP_PARAMS_FL_BLOCK_ENABLE
bit in the block header's `flags` field.

When userspace simply wants to disable an ISP block the
V4L2_ISP_PARAMS_FL_BLOCK_DISABLE bit should be set in block header's `flags`
field. Drivers accept a configuration parameters block with no additional
data after the header in this case.

If the configuration of an already active ISP block has to be updated,
userspace shall fully populate the ISP block parameters and omit setting the
V4L2_ISP_PARAMS_FL_BLOCK_ENABLE and V4L2_ISP_PARAMS_FL_BLOCK_DISABLE bits in the
header's `flags` field.

Setting both the V4L2_ISP_PARAMS_FL_BLOCK_ENABLE and
V4L2_ISP_PARAMS_FL_BLOCK_DISABLE bits in the flags field is not allowed and
returns an error.

Extension to the parameters format can be implemented by adding new blocks
definition without invalidating the existing ones.

ISP statistics
==============

The generic ISP statistics format is identical to the generic ISP configuration
parameters format. It is realized by defining a C structure that contains a
header, followed by binary buffer where the ISP driver copies a variable number
of ISP statistics blocks.

Extensible statistics buffers have :c:type:`v4l2_isp_buffer` header followed by
a binary buffer of ISP statistics data. ISP drivers populate the buffer header
with the serialization format version and with the size (in bytes) of the binary
data buffer where ISP statistics data are serialized. Applications shall
validate that the serialization format version matches the expected one and that
the buffer size doesn't exceed the maximum size for a statistics buffer as
declared by the driver's uAPI header.

Each *ISP statistics block* is preceded by a header implemented by the
:c:type:`v4l2_isp_block_header` structure, followed by the statistics data for
that specific block. The driver might optionally report platform-specific flags
associated with each statistics block.

Applications inspect the statistics block type as reported in the header and
validates the reported size matches the block's expected size before accessing
the ISP statistics data.

Extension to the statistics format can be implemented by adding new blocks
definition without invalidating the existing ones.

V4L2 ISP uAPI data types
========================

.. kernel-doc:: include/uapi/linux/media/v4l2-isp.h

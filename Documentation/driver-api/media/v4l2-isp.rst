.. SPDX-License-Identifier: GPL-2.0

V4L2 generic ISP parameters and statistics support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Design rationale
================

ISP configuration parameters and statistics are processed and collected by
drivers and exchanged with userspace through data types that usually
reflect the ISP peripheral registers layout.

Each ISP driver defines its own metadata output format for parameters and
a metadata capture format for statistics. The buffer layout is realized by a
set of C structures that reflects the registers layout. The number and types
of C structures is fixed by the format definition and becomes part of the Linux
kernel uAPI/uABI interface.

Because of the hard requirement of backward compatibility when extending the
user API/ABI interface, modifying an ISP driver capture or output metadata
format after it has been accepted by mainline is very hard if not impossible.

It generally happens, in fact, that after the first accepted revision of an ISP
driver the buffers layout need to be modified, either to support new hardware
blocks, to fix bugs or to support different revisions of the hardware.

Each of these situations would require defining a new metadata format, making it
really hard to maintain and extend drivers and requiring userspace to use
the correct format depending on the kernel revision in use.

V4L2 ISP configuration parameters
=================================

For these reasons, Video4Linux2 defines generic types for ISP configuration
parameters and statistics. Drivers are still expected to define their own
formats for their metadata output and capture nodes, but the buffers layout can
be defined using the extensible and versioned types defined by
include/uapi/linux/media/v4l2-isp.h.

Drivers are expected to provide the definitions of their supported ISP blocks
and the expected maximum size of a buffer.

For driver developers a set of helper functions to assist them with validation
of the buffer received from userspace is available in
drivers/media/v4l2-core/v4l2-isp.c

V4L2 ISP support driver documentation
=====================================
.. kernel-doc:: include/media/v4l2-isp.h

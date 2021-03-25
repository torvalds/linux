.. SPDX-License-Identifier: GPL-2.0+

.. |u8| replace:: :c:type:`u8 <u8>`
.. |u16| replace:: :c:type:`u16 <u16>`
.. |ssam_cdev_request| replace:: :c:type:`struct ssam_cdev_request <ssam_cdev_request>`
.. |ssam_cdev_request_flags| replace:: :c:type:`enum ssam_cdev_request_flags <ssam_cdev_request_flags>`

==============================
User-Space EC Interface (cdev)
==============================

The ``surface_aggregator_cdev`` module provides a misc-device for the SSAM
controller to allow for a (more or less) direct connection from user-space to
the SAM EC. It is intended to be used for development and debugging, and
therefore should not be used or relied upon in any other way. Note that this
module is not loaded automatically, but instead must be loaded manually.

The provided interface is accessible through the ``/dev/surface/aggregator``
device-file. All functionality of this interface is provided via IOCTLs.
These IOCTLs and their respective input/output parameter structs are defined in
``include/uapi/linux/surface_aggregator/cdev.h``.

A small python library and scripts for accessing this interface can be found
at https://github.com/linux-surface/surface-aggregator-module/tree/master/scripts/ssam.


Controller IOCTLs
=================

The following IOCTLs are provided:

.. flat-table:: Controller IOCTLs
   :widths: 1 1 1 1 4
   :header-rows: 1

   * - Type
     - Number
     - Direction
     - Name
     - Description

   * - ``0xA5``
     - ``1``
     - ``WR``
     - ``REQUEST``
     - Perform synchronous SAM request.


``REQUEST``
-----------

Defined as ``_IOWR(0xA5, 1, struct ssam_cdev_request)``.

Executes a synchronous SAM request. The request specification is passed in
as argument of type |ssam_cdev_request|, which is then written to/modified
by the IOCTL to return status and result of the request.

Request payload data must be allocated separately and is passed in via the
``payload.data`` and ``payload.length`` members. If a response is required,
the response buffer must be allocated by the caller and passed in via the
``response.data`` member. The ``response.length`` member must be set to the
capacity of this buffer, or if no response is required, zero. Upon
completion of the request, the call will write the response to the response
buffer (if its capacity allows it) and overwrite the length field with the
actual size of the response, in bytes.

Additionally, if the request has a response, this must be indicated via the
request flags, as is done with in-kernel requests. Request flags can be set
via the ``flags`` member and the values correspond to the values found in
|ssam_cdev_request_flags|.

Finally, the status of the request itself is returned in the ``status``
member (a negative errno value indicating failure). Note that failure
indication of the IOCTL is separated from failure indication of the request:
The IOCTL returns a negative status code if anything failed during setup of
the request (``-EFAULT``) or if the provided argument or any of its fields
are invalid (``-EINVAL``). In this case, the status value of the request
argument may be set, providing more detail on what went wrong (e.g.
``-ENOMEM`` for out-of-memory), but this value may also be zero. The IOCTL
will return with a zero status code in case the request has been set up,
submitted, and completed (i.e. handed back to user-space) successfully from
inside the IOCTL, but the request ``status`` member may still be negative in
case the actual execution of the request failed after it has been submitted.

A full definition of the argument struct is provided below:

.. kernel-doc:: include/uapi/linux/surface_aggregator/cdev.h

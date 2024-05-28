.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_SUBDEV_G_ROUTING:

******************************************************
ioctl VIDIOC_SUBDEV_G_ROUTING, VIDIOC_SUBDEV_S_ROUTING
******************************************************

Name
====

VIDIOC_SUBDEV_G_ROUTING - VIDIOC_SUBDEV_S_ROUTING - Get or set routing between streams of media pads in a media entity.


Synopsis
========

.. c:macro:: VIDIOC_SUBDEV_G_ROUTING

``int ioctl(int fd, VIDIOC_SUBDEV_G_ROUTING, struct v4l2_subdev_routing *argp)``

.. c:macro:: VIDIOC_SUBDEV_S_ROUTING

``int ioctl(int fd, VIDIOC_SUBDEV_S_ROUTING, struct v4l2_subdev_routing *argp)``

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_subdev_routing`.


Description
===========

These ioctls are used to get and set the routing in a media entity.
The routing configuration determines the flows of data inside an entity.

Drivers report their current routing tables using the
``VIDIOC_SUBDEV_G_ROUTING`` ioctl and application may enable or disable routes
with the ``VIDIOC_SUBDEV_S_ROUTING`` ioctl, by adding or removing routes and
setting or clearing flags of the  ``flags`` field of a
struct :c:type:`v4l2_subdev_route`.

All stream configurations are reset when ``VIDIOC_SUBDEV_S_ROUTING`` is called. This
means that the userspace must reconfigure all streams after calling the ioctl
with e.g. ``VIDIOC_SUBDEV_S_FMT``.

Only subdevices which have both sink and source pads can support routing.

When inspecting routes through ``VIDIOC_SUBDEV_G_ROUTING`` and the application
provided ``num_routes`` is not big enough to contain all the available routes
the subdevice exposes, drivers return the ENOSPC error code and adjust the
value of the ``num_routes`` field. Application should then reserve enough memory
for all the route entries and call ``VIDIOC_SUBDEV_G_ROUTING`` again.

On a successful ``VIDIOC_SUBDEV_G_ROUTING`` call the driver updates the
``num_routes`` field to reflect the actual number of routes returned.

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_subdev_routing

.. flat-table:: struct v4l2_subdev_routing
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``which``
      - Routing table to be accessed, from enum
        :ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.
    * - struct :c:type:`v4l2_subdev_route`
      - ``routes[]``
      - Array of struct :c:type:`v4l2_subdev_route` entries
    * - __u32
      - ``num_routes``
      - Number of entries of the routes array
    * - __u32
      - ``reserved``\ [5]
      - Reserved for future extensions. Applications and drivers must set
	the array to zero.

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_subdev_route

.. flat-table:: struct v4l2_subdev_route
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``sink_pad``
      - Sink pad number.
    * - __u32
      - ``sink_stream``
      - Sink pad stream number.
    * - __u32
      - ``source_pad``
      - Source pad number.
    * - __u32
      - ``source_stream``
      - Source pad stream number.
    * - __u32
      - ``flags``
      - Route enable/disable flags
	:ref:`v4l2_subdev_routing_flags <v4l2-subdev-routing-flags>`.
    * - __u32
      - ``reserved``\ [5]
      - Reserved for future extensions. Applications and drivers must set
	the array to zero.

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _v4l2-subdev-routing-flags:

.. flat-table:: enum v4l2_subdev_routing_flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - V4L2_SUBDEV_ROUTE_FL_ACTIVE
      - 0x0001
      - The route is enabled. Set by applications.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENOSPC
   The application provided ``num_routes`` is not big enough to contain
   all the available routes the subdevice exposes.

EINVAL
   The sink or source pad identifiers reference a non-existing pad or reference
   pads of different types (ie. the sink_pad identifiers refers to a source
   pad), or the ``which`` field has an unsupported value.

E2BIG
   The application provided ``num_routes`` for ``VIDIOC_SUBDEV_S_ROUTING`` is
   larger than the number of routes the driver can handle.

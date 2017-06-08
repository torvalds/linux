.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_CTRL:

**********************************
ioctl VIDIOC_G_CTRL, VIDIOC_S_CTRL
**********************************

Name
====

VIDIOC_G_CTRL - VIDIOC_S_CTRL - Get or set the value of a control


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_CTRL, struct v4l2_control *argp )
    :name: VIDIOC_G_CTRL

.. c:function:: int ioctl( int fd, VIDIOC_S_CTRL, struct v4l2_control *argp )
    :name: VIDIOC_S_CTRL


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

To get the current value of a control applications initialize the ``id``
field of a struct :c:type:`v4l2_control` and call the
:ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` ioctl with a pointer to this structure. To change the
value of a control applications initialize the ``id`` and ``value``
fields of a struct :c:type:`v4l2_control` and call the
:ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctl.

When the ``id`` is invalid drivers return an ``EINVAL`` error code. When the
``value`` is out of bounds drivers can choose to take the closest valid
value or return an ``ERANGE`` error code, whatever seems more appropriate.
However, :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` is a write-only ioctl, it does not return the
actual new value. If the ``value`` is inappropriate for the control
(e.g. if it refers to an unsupported menu index of a menu control), then
EINVAL error code is returned as well.

These ioctls work only with user controls. For other control classes the
:ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`,
:ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` or
:ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` must be used.


.. c:type:: v4l2_control

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_control
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``id``
      - Identifies the control, set by the application.
    * - __s32
      - ``value``
      - New value or current value.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_control` ``id`` is invalid
    or the ``value`` is inappropriate for the given control (i.e. if a
    menu item is selected that is not supported by the driver according
    to :ref:`VIDIOC_QUERYMENU <VIDIOC_QUERYCTRL>`).

ERANGE
    The struct :c:type:`v4l2_control` ``value`` is out of
    bounds.

EBUSY
    The control is temporarily not changeable, possibly because another
    applications took over control of the device function this control
    belongs to.

EACCES
    Attempt to set a read-only control or to get a write-only control.

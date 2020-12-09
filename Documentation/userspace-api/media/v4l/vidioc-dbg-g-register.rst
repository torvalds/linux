.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_DBG_G_REGISTER:

**************************************************
ioctl VIDIOC_DBG_G_REGISTER, VIDIOC_DBG_S_REGISTER
**************************************************

Name
====

VIDIOC_DBG_G_REGISTER - VIDIOC_DBG_S_REGISTER - Read or write hardware registers

Synopsis
========

.. c:macro:: VIDIOC_DBG_G_REGISTER

``int ioctl(int fd, VIDIOC_DBG_G_REGISTER, struct v4l2_dbg_register *argp)``

.. c:macro:: VIDIOC_DBG_S_REGISTER

``int ioctl(int fd, VIDIOC_DBG_S_REGISTER, const struct v4l2_dbg_register *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_dbg_register`.

Description
===========

.. note::

    This is an :ref:`experimental` interface and may
    change in the future.

For driver debugging purposes these ioctls allow test applications to
access hardware registers directly. Regular applications must not use
them.

Since writing or even reading registers can jeopardize the system
security, its stability and damage the hardware, both ioctls require
superuser privileges. Additionally the Linux kernel must be compiled
with the ``CONFIG_VIDEO_ADV_DEBUG`` option to enable these ioctls.

To write a register applications must initialize all fields of a struct
:c:type:`v4l2_dbg_register` except for ``size`` and
call ``VIDIOC_DBG_S_REGISTER`` with a pointer to this structure. The
``match.type`` and ``match.addr`` or ``match.name`` fields select a chip
on the TV card, the ``reg`` field specifies a register number and the
``val`` field the value to be written into the register.

To read a register applications must initialize the ``match.type``,
``match.addr`` or ``match.name`` and ``reg`` fields, and call
``VIDIOC_DBG_G_REGISTER`` with a pointer to this structure. On success
the driver stores the register value in the ``val`` field and the size
(in bytes) of the value in ``size``.

When ``match.type`` is ``V4L2_CHIP_MATCH_BRIDGE``, ``match.addr``
selects the nth non-sub-device chip on the TV card. The number zero
always selects the host chip, e. g. the chip connected to the PCI or USB
bus. You can find out which chips are present with the
:ref:`VIDIOC_DBG_G_CHIP_INFO` ioctl.

When ``match.type`` is ``V4L2_CHIP_MATCH_SUBDEV``, ``match.addr``
selects the nth sub-device.

These ioctls are optional, not all drivers may support them. However
when a driver supports these ioctls it must also support
:ref:`VIDIOC_DBG_G_CHIP_INFO`. Conversely
it may support ``VIDIOC_DBG_G_CHIP_INFO`` but not these ioctls.

``VIDIOC_DBG_G_REGISTER`` and ``VIDIOC_DBG_S_REGISTER`` were introduced
in Linux 2.6.21, but their API was changed to the one described here in
kernel 2.6.29.

We recommended the v4l2-dbg utility over calling these ioctls directly.
It is available from the LinuxTV v4l-dvb repository; see
`https://linuxtv.org/repo/ <https://linuxtv.org/repo/>`__ for access
instructions.

.. tabularcolumns:: |p{3.5cm}|p{3.5cm}|p{3.5cm}|p{7.0cm}|

.. c:type:: v4l2_dbg_match

.. flat-table:: struct v4l2_dbg_match
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - See :ref:`chip-match-types` for a list of possible types.
    * - union {
      - (anonymous)
    * - __u32
      - ``addr``
      - Match a chip by this number, interpreted according to the ``type``
	field.
    * - char
      - ``name[32]``
      - Match a chip by this name, interpreted according to the ``type``
	field. Currently unused.
    * - }
      -


.. c:type:: v4l2_dbg_register

.. flat-table:: struct v4l2_dbg_register
    :header-rows:  0
    :stub-columns: 0

    * - struct v4l2_dbg_match
      - ``match``
      - How to match the chip, see :c:type:`v4l2_dbg_match`.
    * - __u32
      - ``size``
      - The register size in bytes.
    * - __u64
      - ``reg``
      - A register number.
    * - __u64
      - ``val``
      - The value read from, or to be written into the register.


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _chip-match-types:

.. flat-table:: Chip Match Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_CHIP_MATCH_BRIDGE``
      - 0
      - Match the nth chip on the card, zero for the bridge chip. Does not
	match sub-devices.
    * - ``V4L2_CHIP_MATCH_SUBDEV``
      - 4
      - Match the nth sub-device.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EPERM
    Insufficient permissions. Root privileges are required to execute
    these ioctls.

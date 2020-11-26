.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_GET_PROPERTY:

**************************************
ioctl FE_SET_PROPERTY, FE_GET_PROPERTY
**************************************

Name
====

FE_SET_PROPERTY - FE_GET_PROPERTY - FE_SET_PROPERTY sets one or more frontend properties. - FE_GET_PROPERTY returns one or more frontend properties.

Synopsis
========

.. c:macro:: FE_GET_PROPERTY

``int ioctl(int fd, FE_GET_PROPERTY, struct dtv_properties *argp)``

.. c:macro:: FE_SET_PROPERTY

``int ioctl(int fd, FE_SET_PROPERTY, struct dtv_properties *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`dtv_properties`.

Description
===========

All Digital TV frontend devices support the ``FE_SET_PROPERTY`` and
``FE_GET_PROPERTY`` ioctls. The supported properties and statistics
depends on the delivery system and on the device:

-  ``FE_SET_PROPERTY:``

   -  This ioctl is used to set one or more frontend properties.

   -  This is the basic command to request the frontend to tune into
      some frequency and to start decoding the digital TV signal.

   -  This call requires read/write access to the device.

.. note::

   At return, the values aren't updated to reflect the actual
   parameters used. If the actual parameters are needed, an explicit
   call to ``FE_GET_PROPERTY`` is needed.

-  ``FE_GET_PROPERTY:``

   -  This ioctl is used to get properties and statistics from the
      frontend.

   -  No properties are changed, and statistics aren't reset.

   -  This call only requires read-only access to the device.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

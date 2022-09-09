.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_GET_INFO:

*****************
ioctl FE_GET_INFO
*****************

Name
====

FE_GET_INFO - Query Digital TV frontend capabilities and returns information
about the - front-end. This call only requires read-only access to the device.

Synopsis
========

.. c:macro:: FE_GET_INFO

``int ioctl(int fd, FE_GET_INFO, struct dvb_frontend_info *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    pointer to struct :c:type:`dvb_frontend_info`

Description
===========

All Digital TV frontend devices support the :ref:`FE_GET_INFO` ioctl. It is
used to identify kernel devices compatible with this specification and to
obtain information about driver and hardware capabilities. The ioctl
takes a pointer to dvb_frontend_info which is filled by the driver.
When the driver is not compatible with this specification the ioctl
returns an error.

frontend capabilities
=====================

Capabilities describe what a frontend can do. Some capabilities are
supported only on some specific frontend types.

The frontend capabilities are described at :c:type:`fe_caps`.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

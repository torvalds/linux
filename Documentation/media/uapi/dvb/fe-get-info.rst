.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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

.. c:function:: int ioctl( int fd, FE_GET_INFO, struct dvb_frontend_info *argp )
    :name: FE_GET_INFO


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``argp``
    pointer to struct struct
    :c:type:`dvb_frontend_info`


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

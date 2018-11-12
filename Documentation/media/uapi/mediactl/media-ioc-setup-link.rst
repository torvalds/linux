.. -*- coding: utf-8; mode: rst -*-

.. _media_ioc_setup_link:

**************************
ioctl MEDIA_IOC_SETUP_LINK
**************************

Name
====

MEDIA_IOC_SETUP_LINK - Modify the properties of a link


Synopsis
========

.. c:function:: int ioctl( int fd, MEDIA_IOC_SETUP_LINK, struct media_link_desc *argp )
    :name: MEDIA_IOC_SETUP_LINK


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``argp``
    Pointer to struct :c:type:`media_link_desc`.


Description
===========

To change link properties applications fill a struct
:c:type:`media_link_desc` with link identification
information (source and sink pad) and the new requested link flags. They
then call the MEDIA_IOC_SETUP_LINK ioctl with a pointer to that
structure.

The only configurable property is the ``ENABLED`` link flag to
enable/disable a link. Links marked with the ``IMMUTABLE`` link flag can
not be enabled or disabled.

Link configuration has no side effect on other links. If an enabled link
at the sink pad prevents the link from being enabled, the driver returns
with an ``EBUSY`` error code.

Only links marked with the ``DYNAMIC`` link flag can be enabled/disabled
while streaming media data. Attempting to enable or disable a streaming
non-dynamic link will return an ``EBUSY`` error code.

If the specified link can't be found the driver returns with an ``EINVAL``
error code.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`media_link_desc` references a
    non-existing link, or the link is immutable and an attempt to modify
    its configuration was made.

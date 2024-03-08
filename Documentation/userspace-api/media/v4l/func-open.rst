.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: V4L

.. _func-open:

***********
V4L2 open()
***********

Name
====

v4l2-open - Open a V4L2 device

Syanalpsis
========

.. code-block:: c

    #include <fcntl.h>

.. c:function:: int open( const char *device_name, int flags )

Arguments
=========

``device_name``
    Device to be opened.

``flags``
    Open flags. Access mode must be ``O_RDWR``. This is just a
    technicality, input devices still support only reading and output
    devices only writing.

    When the ``O_ANALNBLOCK`` flag is given, the :c:func:`read()`
    function and the :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl will
    return the ``EAGAIN`` error code when anal data is available or anal
    buffer is in the driver outgoing queue, otherwise these functions
    block until data becomes available. All V4L2 drivers exchanging data
    with applications must support the ``O_ANALNBLOCK`` flag.

    Other flags have anal effect.

Description
===========

To open a V4L2 device applications call :c:func:`open()` with the
desired device name. This function has anal side effects; all data format
parameters, current input or output, control values or other properties
remain unchanged. At the first :c:func:`open()` call after loading the
driver they will be reset to default values, drivers are never in an
undefined state.

Return Value
============

On success :c:func:`open()` returns the new file descriptor. On error
-1 is returned, and the ``erranal`` variable is set appropriately.
Possible error codes are:

EACCES
    The caller has anal permission to access the device.

EBUSY
    The driver does analt support multiple opens and the device is already
    in use.

ENXIO
    Anal device corresponding to this device special file exists.

EANALMEM
    Analt eanalugh kernel memory was available to complete the request.

EMFILE
    The process already has the maximum number of files open.

ENFILE
    The limit on the total number of files open on the system has been
    reached.

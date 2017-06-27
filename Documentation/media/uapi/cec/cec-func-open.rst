.. -*- coding: utf-8; mode: rst -*-

.. _cec-func-open:

**********
cec open()
**********

Name
====

cec-open - Open a cec device

Synopsis
========

.. code-block:: c

    #include <fcntl.h>


.. c:function:: int open( const char *device_name, int flags )
   :name: cec-open


Arguments
=========

``device_name``
    Device to be opened.

``flags``
    Open flags. Access mode must be ``O_RDWR``.

    When the ``O_NONBLOCK`` flag is given, the
    :ref:`CEC_RECEIVE <CEC_RECEIVE>` and :ref:`CEC_DQEVENT <CEC_DQEVENT>` ioctls
    will return the ``EAGAIN`` error code when no message or event is available, and
    ioctls :ref:`CEC_TRANSMIT <CEC_TRANSMIT>`,
    :ref:`CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>` and
    :ref:`CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`
    all return 0.

    Other flags have no effect.


Description
===========

To open a cec device applications call :c:func:`open()` with the
desired device name. The function has no side effects; the device
configuration remain unchanged.

When the device is opened in read-only mode, attempts to modify its
configuration will result in an error, and ``errno`` will be set to
EBADF.


Return Value
============

:c:func:`open()` returns the new file descriptor on success. On error,
-1 is returned, and ``errno`` is set appropriately. Possible error codes
include:

``EACCES``
    The requested access to the file is not allowed.

``EMFILE``
    The process already has the maximum number of files open.

``ENFILE``
    The system limit on the total number of open files has been reached.

``ENOMEM``
    Insufficient kernel memory was available.

``ENXIO``
    No device corresponding to this device special file exists.

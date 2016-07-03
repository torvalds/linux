.. -*- coding: utf-8; mode: rst -*-

.. _ca_function_calls:

*****************
CA Function Calls
*****************


.. _ca_fopen:

open()
======

DESCRIPTION

This system call opens a named ca device (e.g. /dev/ost/ca) for
subsequent use.

When an open() call has succeeded, the device will be ready for use. The
significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the open() call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the F_SETFL command of the fcntl system call. This is a standard
system call, documented in the Linux manual page for fcntl. Only one
user can open the CA Device in O_RDWR mode. All other attempts to open
the device in this mode will fail, and an error code will be returned.

SYNOPSIS

int open(const char *deviceName, int flags);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char *deviceName

       -  Name of specific video device.

    -  .. row 2

       -  int flags

       -  A bit-wise OR of the following flags:

    -  .. row 3

       -  
       -  O_RDONLY read-only access

    -  .. row 4

       -  
       -  O_RDWR read/write access

    -  .. row 5

       -  
       -  O_NONBLOCK open in non-blocking mode

    -  .. row 6

       -  
       -  (blocking mode is the default)


RETURN VALUE



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``ENODEV``

       -  Device driver not loaded/available.

    -  .. row 2

       -  ``EINTERNAL``

       -  Internal error.

    -  .. row 3

       -  ``EBUSY``

       -  Device or resource busy.

    -  .. row 4

       -  ``EINVAL``

       -  Invalid argument.



.. _ca_fclose:

close()
=======

DESCRIPTION

This system call closes a previously opened audio device.

SYNOPSIS

int close(int fd);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


RETURN VALUE



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.



.. _CA_RESET:

CA_RESET
========

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_RESET);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_RESET for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_GET_CAP:

CA_GET_CAP
==========

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_GET_CAP, ca_caps_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_GET_CAP for this command.

    -  .. row 3

       -  ca_caps_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_GET_SLOT_INFO:

CA_GET_SLOT_INFO
================

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_GET_SLOT_INFO, ca_slot_info_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_GET_SLOT_INFO for this command.

    -  .. row 3

       -  ca_slot_info_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_GET_DESCR_INFO:

CA_GET_DESCR_INFO
=================

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_GET_DESCR_INFO, ca_descr_info_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_GET_DESCR_INFO for this command.

    -  .. row 3

       -  ca_descr_info_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_GET_MSG:

CA_GET_MSG
==========

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_GET_MSG, ca_msg_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_GET_MSG for this command.

    -  .. row 3

       -  ca_msg_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_SEND_MSG:

CA_SEND_MSG
===========

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_SEND_MSG, ca_msg_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_SEND_MSG for this command.

    -  .. row 3

       -  ca_msg_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_SET_DESCR:

CA_SET_DESCR
============

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_SET_DESCR, ca_descr_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_SET_DESCR for this command.

    -  .. row 3

       -  ca_descr_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _CA_SET_PID:

CA_SET_PID
==========

DESCRIPTION

This ioctl is undocumented. Documentation is welcome.

SYNOPSIS

int ioctl(fd, int request = CA_SET_PID, ca_pid_t *);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals CA_SET_PID for this command.

    -  .. row 3

       -  ca_pid_t *

       -  Undocumented.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------

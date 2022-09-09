.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.ca

.. _ca_fclose:

=====================
Digital TV CA close()
=====================

Name
----

Digital TV CA close()

Synopsis
--------

.. c:function:: int close(int fd)

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

Description
-----------

This system call closes a previously opened CA device.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

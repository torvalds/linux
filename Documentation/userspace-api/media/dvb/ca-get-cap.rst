.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.ca

.. _CA_GET_CAP:

==========
CA_GET_CAP
==========

Name
----

CA_GET_CAP

Synopsis
--------

.. c:macro:: CA_GET_CAP

``int ioctl(fd, CA_GET_CAP, struct ca_caps *caps)``

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

``caps``
  Pointer to struct :c:type:`ca_caps`.

Description
-----------

Queries the Kernel for information about the available CA and descrambler
slots, and their types.

Return Value
------------

On success 0 is returned and :c:type:`ca_caps` is filled.

On error, -1 is returned and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

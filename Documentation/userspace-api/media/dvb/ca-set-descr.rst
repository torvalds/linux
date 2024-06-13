.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.ca

.. _CA_SET_DESCR:

============
CA_SET_DESCR
============

Name
----

CA_SET_DESCR

Synopsis
--------

.. c:macro:: CA_SET_DESCR

``int ioctl(fd, CA_SET_DESCR, struct ca_descr *desc)``

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

``msg``
  Pointer to struct :c:type:`ca_descr`.

Description
-----------

CA_SET_DESCR is used for feeding descrambler CA slots with descrambling
keys (referred as control words).

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

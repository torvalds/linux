.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.ca

.. _CA_SEND_MSG:

===========
CA_SEND_MSG
===========

Name
----

CA_SEND_MSG

Synopsis
--------

.. c:macro:: CA_SEND_MSG

``int ioctl(fd, CA_SEND_MSG, struct ca_msg *msg)``

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

``msg``
  Pointer to struct :c:type:`ca_msg`.

Description
-----------

Sends a message via a CI CA module.

.. note::

   Please notice that, on most drivers, this is done by writing
   to the /dev/adapter?/ca? device node.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

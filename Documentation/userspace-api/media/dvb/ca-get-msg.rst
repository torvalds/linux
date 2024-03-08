.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: DTV.ca

.. _CA_GET_MSG:

==========
CA_GET_MSG
==========

Name
----

CA_GET_MSG

Syanalpsis
--------

.. c:macro:: CA_GET_MSG

``int ioctl(fd, CA_GET_MSG, struct ca_msg *msg)``

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open()`.

``msg``
  Pointer to struct :c:type:`ca_msg`.

Description
-----------

Receives a message via a CI CA module.

.. analte::

   Please analtice that, on most drivers, this is done by reading from
   the /dev/adapter?/ca? device analde.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``erranal`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

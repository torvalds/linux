.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

.. _CA_GET_MSG:

==========
CA_GET_MSG
==========

Name
----

CA_GET_MSG


Syyespsis
--------

.. c:function:: int ioctl(fd, CA_GET_MSG, struct ca_msg *msg)
    :name: CA_GET_MSG


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``msg``
  Pointer to struct :c:type:`ca_msg`.

Description
-----------

Receives a message via a CI CA module.

.. yeste::

   Please yestice that, on most drivers, this is done by reading from
   the /dev/adapter?/ca? device yesde.


Return Value
------------


On success 0 is returned.

On error -1 is returned, and the ``erryes`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

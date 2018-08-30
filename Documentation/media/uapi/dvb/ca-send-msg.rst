.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _CA_SEND_MSG:

===========
CA_SEND_MSG
===========

Name
----

CA_SEND_MSG


Synopsis
--------

.. c:function:: int ioctl(fd, CA_SEND_MSG, struct ca_msg *msg)
    :name: CA_SEND_MSG


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

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

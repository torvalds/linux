.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _CA_SET_DESCR:

============
CA_SET_DESCR
============

Name
----

CA_SET_DESCR


Synopsis
--------

.. c:function:: int ioctl(fd, CA_SET_DESCR, struct ca_descr *desc)
    :name: CA_SET_DESCR


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

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

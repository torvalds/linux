.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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
    :name: dvb-ca-close


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

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

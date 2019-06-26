.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dmx_fclose:

========================
Digital TV demux close()
========================

Name
----

Digital TV demux close()


Synopsis
--------

.. c:function:: int close(int fd)
    :name: dvb-dmx-close


Arguments
---------

``fd``
  File descriptor returned by a previous call to
  :c:func:`open() <dvb-dmx-open>`.

Description
-----------

This system call deactivates and deallocates a filter that was
previously allocated via the :c:func:`open() <dvb-dmx-open>` call.


Return Value
------------

On success 0 is returned.

On error, -1 is returned and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

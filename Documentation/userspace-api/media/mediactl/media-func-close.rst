.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _media-func-close:

*************
media close()
*************

Name
====

media-close - Close a media device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: int close( int fd )
    :name: mc-close

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <mc-open>`.


Description
===========

Closes the media device. Resources associated with the file descriptor
are freed. The device configuration remain unchanged.


Return Value
============

:ref:`close() <media-func-close>` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid open file descriptor.

.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_ENABLE_HIGH_LNB_VOLTAGE:

********************************
ioctl FE_ENABLE_HIGH_LNB_VOLTAGE
********************************

Name
====

FE_ENABLE_HIGH_LNB_VOLTAGE - Select output DC level between normal LNBf voltages or higher LNBf - voltages.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_ENABLE_HIGH_LNB_VOLTAGE, unsigned int high )
    :name: FE_ENABLE_HIGH_LNB_VOLTAGE


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``high``
    Valid flags:

    -  0 - normal 13V and 18V.

    -  >0 - enables slightly higher voltages instead of 13/18V, in order
       to compensate for long antenna cables.


Description
===========

Select output DC level between normal LNBf voltages or higher LNBf
voltages between 0 (normal) or a value grater than 0 for higher
voltages.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

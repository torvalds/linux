.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_DISHNETWORK_SEND_LEGACY_CMD:

******************************
FE_DISHNETWORK_SEND_LEGACY_CMD
******************************

Name
====

FE_DISHNETWORK_SEND_LEGACY_CMD


Synopsis
========

.. c:function:: int  ioctl(int fd, FE_DISHNETWORK_SEND_LEGACY_CMD, unsigned long cmd)
    :name: FE_DISHNETWORK_SEND_LEGACY_CMD


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``cmd``
    Sends the specified raw cmd to the dish via DISEqC.


Description
===========

.. warning::
   This is a very obscure legacy command, used only at stv0299
   driver. Should not be used on newer drivers.

It provides a non-standard method for selecting Diseqc voltage on the
frontend, for Dish Network legacy switches.

As support for this ioctl were added in 2004, this means that such
dishes were already legacy in 2004.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

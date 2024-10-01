.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_DISHNETWORK_SEND_LEGACY_CMD:

******************************
FE_DISHNETWORK_SEND_LEGACY_CMD
******************************

Name
====

FE_DISHNETWORK_SEND_LEGACY_CMD

Synopsis
========

.. c:macro:: FE_DISHNETWORK_SEND_LEGACY_CMD

``int ioctl(int fd, FE_DISHNETWORK_SEND_LEGACY_CMD, unsigned long cmd)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

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

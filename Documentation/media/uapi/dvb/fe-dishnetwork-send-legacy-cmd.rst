.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISHNETWORK_SEND_LEGACY_CMD:

******************************
FE_DISHNETWORK_SEND_LEGACY_CMD
******************************

Name
====

FE_DISHNETWORK_SEND_LEGACY_CMD


Synopsis
========

.. c:function:: int  ioctl(int fd, int request = FE_DISHNETWORK_SEND_LEGACY_CMD, unsigned long cmd)


Arguments
=========

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  unsigned long cmd

       -  sends the specified raw cmd to the dish via DISEqC.


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

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

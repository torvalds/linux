.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_SEND_BURST:

**************************
ioctl FE_DISEQC_SEND_BURST
**************************

Name
====

FE_DISEQC_SEND_BURST - Sends a 22KHz tone burst for 2x1 mini DiSEqC satellite selection.


Synopsis
========

.. c:function:: int ioctl( int fd, int request, enum fe_sec_mini_cmd *tone )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_DISEQC_SEND_BURST

``tone``
    pointer to enum :ref:`fe_sec_mini_cmd <fe-sec-mini-cmd>`


Description
===========

This ioctl is used to set the generation of a 22kHz tone burst for mini
DiSEqC satellite selection for 2x1 switches. This call requires
read/write permissions.

It provides support for what's specified at
`Digital Satellite Equipment Control (DiSEqC) - Simple "ToneBurst" Detection Circuit specification. <http://www.eutelsat.com/files/contributed/satellites/pdf/Diseqc/associated%20docs/simple_tone_burst_detec.pdf>`__

.. _fe-sec-mini-cmd-t:

enum fe_sec_mini_cmd
====================

.. _fe-sec-mini-cmd:

.. flat-table:: enum fe_sec_mini_cmd
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _SEC-MINI-A:

	  ``SEC_MINI_A``

       -  Sends a mini-DiSEqC 22kHz '0' Tone Burst to select satellite-A

    -  .. row 3

       -  .. _SEC-MINI-B:

	  ``SEC_MINI_B``

       -  Sends a mini-DiSEqC 22kHz '1' Data Burst to select satellite-B


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

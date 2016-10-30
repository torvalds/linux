.. -*- coding: utf-8; mode: rst -*-

.. _FE_SET_TONE:

*****************
ioctl FE_SET_TONE
*****************

Name
====

FE_SET_TONE - Sets/resets the generation of the continuous 22kHz tone.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_SET_TONE, enum fe_sec_tone_mode *tone )
    :name: FE_SET_TONE


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``tone``
    pointer to enum :c:type:`fe_sec_tone_mode`


Description
===========

This ioctl is used to set the generation of the continuous 22kHz tone.
This call requires read/write permissions.

Usually, satellite antenna subsystems require that the digital TV device
to send a 22kHz tone in order to select between high/low band on some
dual-band LNBf. It is also used to send signals to DiSEqC equipment, but
this is done using the DiSEqC ioctls.

.. attention:: If more than one device is connected to the same antenna,
   setting a tone may interfere on other devices, as they may lose the
   capability of selecting the band. So, it is recommended that applications
   would change to SEC_TONE_OFF when the device is not used.

.. c:type:: fe_sec_tone_mode

.. flat-table:: enum fe_sec_tone_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _SEC-TONE-ON:

	  ``SEC_TONE_ON``

       -  Sends a 22kHz tone burst to the antenna

    -  .. row 3

       -  .. _SEC-TONE-OFF:

	  ``SEC_TONE_OFF``

       -  Don't send a 22kHz tone to the antenna (except if the
	  FE_DISEQC_* ioctls are called)


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

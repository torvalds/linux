.. -*- coding: utf-8; mode: rst -*-

.. include:: <isonum.txt>

##############################
LINUX MEDIA INFRASTRUCTURE API
##############################

**Copyright** |copy| 2009-2016 : LinuxTV Developers

Permission is granted to copy, distribute and/or modify this document
under the terms of the GNU Free Documentation License, Version 1.1 or
any later version published by the Free Software Foundation. A copy of
the license is included in the chapter entitled "GNU Free Documentation
License".


============
Introduction
============

This document covers the Linux Kernel to Userspace API's used by video
and radio streaming devices, including video cameras, analog and digital
TV receiver cards, AM/FM receiver cards, Software Defined Radio (SDR),
streaming capture and output devices, codec devices and remote controllers.

A typical media device hardware is shown at
:ref:`typical_media_device`.


.. _typical_media_device:

.. figure::  media_api_files/typical_media_device.*
    :alt:    typical_media_device.svg
    :align:  center

    Typical Media Device

The media infrastructure API was designed to control such devices. It is
divided into four parts.

The :Ref:`first part <v4l2spec>` covers radio, video capture and output,
cameras, analog TV devices and codecs.

The :Ref:`second part <dvbapi>` covers the API used for digital TV and
Internet reception via one of the several digital tv standards. While it
is called as DVB API, in fact it covers several different video
standards including DVB-T/T2, DVB-S/S2, DVB-C, ATSC, ISDB-T, ISDB-S,
DTMB, etc. The complete list of supported standards can be found at
:ref:`fe-delivery-system-t`.

The :Ref:`third part <remote_controllers>` covers the Remote Controller API.

The :Ref:`fourth part <media_controller>` covers the Media Controller API.

It should also be noted that a media device may also have audio
components, like mixers, PCM capture, PCM playback, etc, which are
controlled via ALSA API.

For additional information and for the latest development code, see:
`https://linuxtv.org <https://linuxtv.org>`__.

For discussing improvements, reporting troubles, sending new drivers,
etc, please mail to:
`Linux Media Mailing List (LMML). <http://vger.kernel.org/vger-lists.html#linux-media>`__.


.. toctree::
    :maxdepth: 1

    media/v4l/v4l2
    media/dvb/dvbapi
    media/v4l/remote_controllers
    media/v4l/media-controller
    media/v4l/gen-errors
    media/v4l/fdl-appendix

.. only:: html

  Retrieval
  =========

  * :ref:`genindex`

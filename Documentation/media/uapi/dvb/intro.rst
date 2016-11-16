.. -*- coding: utf-8; mode: rst -*-

.. _dvb_introdution:

************
Introduction
************


.. _requisites:

What you need to know
=====================

The reader of this document is required to have some knowledge in the
area of digital video broadcasting (DVB) and should be familiar with
part I of the MPEG2 specification ISO/IEC 13818 (aka ITU-T H.222), i.e
you should know what a program/transport stream (PS/TS) is and what is
meant by a packetized elementary stream (PES) or an I-frame.

Various DVB standards documents are available from http://www.dvb.org
and/or http://www.etsi.org.

It is also necessary to know how to access unix/linux devices and how to
use ioctl calls. This also includes the knowledge of C or C++.


.. _history:

History
=======

The first API for DVB cards we used at Convergence in late 1999 was an
extension of the Video4Linux API which was primarily developed for frame
grabber cards. As such it was not really well suited to be used for DVB
cards and their new features like recording MPEG streams and filtering
several section and PES data streams at the same time.

In early 2000, we were approached by Nokia with a proposal for a new
standard Linux DVB API. As a commitment to the development of terminals
based on open standards, Nokia and Convergence made it available to all
Linux developers and published it on https://linuxtv.org in September
2000. Convergence is the maintainer of the Linux DVB API. Together with
the LinuxTV community (i.e. you, the reader of this document), the Linux
DVB API will be constantly reviewed and improved. With the Linux driver
for the Siemens/Hauppauge DVB PCI card Convergence provides a first
implementation of the Linux DVB API.


.. _overview:

Overview
========


.. _stb_components:

.. figure::  dvbstb.*
    :alt:    dvbstb.pdf / dvbstb.svg
    :align:  center

    Components of a DVB card/STB

A DVB PCI card or DVB set-top-box (STB) usually consists of the
following main hardware components:

-  Frontend consisting of tuner and DVB demodulator

   Here the raw signal reaches the DVB hardware from a satellite dish or
   antenna or directly from cable. The frontend down-converts and
   demodulates this signal into an MPEG transport stream (TS). In case
   of a satellite frontend, this includes a facility for satellite
   equipment control (SEC), which allows control of LNB polarization,
   multi feed switches or dish rotors.

-  Conditional Access (CA) hardware like CI adapters and smartcard slots

   The complete TS is passed through the CA hardware. Programs to which
   the user has access (controlled by the smart card) are decoded in
   real time and re-inserted into the TS.

-  Demultiplexer which filters the incoming DVB stream

   The demultiplexer splits the TS into its components like audio and
   video streams. Besides usually several of such audio and video
   streams it also contains data streams with information about the
   programs offered in this or other streams of the same provider.

-  MPEG2 audio and video decoder

   The main targets of the demultiplexer are the MPEG2 audio and video
   decoders. After decoding they pass on the uncompressed audio and
   video to the computer screen or (through a PAL/NTSC encoder) to a TV
   set.

:ref:`stb_components` shows a crude schematic of the control and data
flow between those components.

On a DVB PCI card not all of these have to be present since some
functionality can be provided by the main CPU of the PC (e.g. MPEG
picture and sound decoding) or is not needed (e.g. for data-only uses
like “internet over satellite”). Also not every card or STB provides
conditional access hardware.


.. _dvb_devices:

Linux DVB Devices
=================

The Linux DVB API lets you control these hardware components through
currently six Unix-style character devices for video, audio, frontend,
demux, CA and IP-over-DVB networking. The video and audio devices
control the MPEG2 decoder hardware, the frontend device the tuner and
the DVB demodulator. The demux device gives you control over the PES and
section filters of the hardware. If the hardware does not support
filtering these filters can be implemented in software. Finally, the CA
device controls all the conditional access capabilities of the hardware.
It can depend on the individual security requirements of the platform,
if and how many of the CA functions are made available to the
application through this device.

All devices can be found in the ``/dev`` tree under ``/dev/dvb``. The
individual devices are called:

-  ``/dev/dvb/adapterN/audioM``,

-  ``/dev/dvb/adapterN/videoM``,

-  ``/dev/dvb/adapterN/frontendM``,

-  ``/dev/dvb/adapterN/netM``,

-  ``/dev/dvb/adapterN/demuxM``,

-  ``/dev/dvb/adapterN/dvrM``,

-  ``/dev/dvb/adapterN/caM``,

where N enumerates the DVB PCI cards in a system starting from 0, and M
enumerates the devices of each type within each adapter, starting
from 0, too. We will omit the “ ``/dev/dvb/adapterN/``\ ” in the further
discussion of these devices.

More details about the data structures and function calls of all the
devices are described in the following chapters.


.. _include_files:

API include files
=================

For each of the DVB devices a corresponding include file exists. The DVB
API include files should be included in application sources with a
partial path like:


.. code-block:: c

	#include <linux/dvb/ca.h>

	#include <linux/dvb/dmx.h>

	#include <linux/dvb/frontend.h>

	#include <linux/dvb/net.h>


To enable applications to support different API version, an additional
include file ``linux/dvb/version.h`` exists, which defines the constant
``DVB_API_VERSION``. This document describes ``DVB_API_VERSION 5.10``.

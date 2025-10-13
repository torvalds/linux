.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _dvb_introduction:

************
Introduction
************


.. _requisites:

What you need to know
=====================

The reader of this document is required to have some knowledge in the
area of digital video broadcasting (Digital TV) and should be familiar with
part I of the MPEG2 specification ISO/IEC 13818 (aka ITU-T H.222), i.e
you should know what a program/transport stream (PS/TS) is and what is
meant by a packetized elementary stream (PES) or an I-frame.

Various Digital TV standards documents are available for download at:

- European standards (DVB): http://www.dvb.org and/or http://www.etsi.org.
- American standards (ATSC): https://www.atsc.org/standards/
- Japanese standards (ISDB): http://www.dibeg.org/

It is also necessary to know how to access Linux devices and how to
use ioctl calls. This also includes the knowledge of C or C++.


.. _history:

History
=======

The first API for Digital TV cards we used at Convergence in late 1999 was an
extension of the Video4Linux API which was primarily developed for frame
grabber cards. As such it was not really well suited to be used for Digital
TV cards and their new features like recording MPEG streams and filtering
several section and PES data streams at the same time.

In early 2000, Convergence was approached by Nokia with a proposal for a new
standard Linux Digital TV API. As a commitment to the development of terminals
based on open standards, Nokia and Convergence made it available to all
Linux developers and published it on https://linuxtv.org in September
2000. With the Linux driver for the Siemens/Hauppauge DVB PCI card,
Convergence provided a first implementation of the Linux Digital TV API.
Convergence was the maintainer of the Linux Digital TV API in the early
days.

Now, the API is maintained by the LinuxTV community (i.e. you, the reader
of this document). The Linux  Digital TV API is constantly reviewed and
improved together with the improvements at the subsystem's core at the
Kernel.


.. _overview:

Overview
========


.. _stb_components:

.. kernel-figure:: dvbstb.svg
    :alt:   dvbstb.svg
    :align: center

    Components of a Digital TV card/STB

A Digital TV card or set-top-box (STB) usually consists of the
following main hardware components:

Frontend consisting of tuner and digital TV demodulator
   Here the raw signal reaches the digital TV hardware from a satellite dish or
   antenna or directly from cable. The frontend down-converts and
   demodulates this signal into an MPEG transport stream (TS). In case
   of a satellite frontend, this includes a facility for satellite
   equipment control (SEC), which allows control of LNB polarization,
   multi feed switches or dish rotors.

Conditional Access (CA) hardware like CI adapters and smartcard slots
   The complete TS is passed through the CA hardware. Programs to which
   the user has access (controlled by the smart card) are decoded in
   real time and re-inserted into the TS.

   .. note::

      Not every digital TV hardware provides conditional access hardware.

Demultiplexer which filters the incoming Digital TV MPEG-TS stream
   The demultiplexer splits the TS into its components like audio and
   video streams. Besides usually several of such audio and video
   streams it also contains data streams with information about the
   programs offered in this or other streams of the same provider.

Audio and video decoder
   The main targets of the demultiplexer are audio and video
   decoders. After decoding, they pass on the uncompressed audio and
   video to the computer screen or to a TV set.

   .. note::

      Modern hardware usually doesn't have a separate decoder hardware, as
      such functionality can be provided by the main CPU, by the graphics
      adapter of the system or by a signal processing hardware embedded on
      a Systems on a Chip (SoC) integrated circuit.

      It may also not be needed for certain usages (e.g. for data-only
      uses like "internet over satellite").

:ref:`stb_components` shows a crude schematic of the control and data
flow between those components.



.. _dvb_devices:

Linux Digital TV Devices
========================

The Linux Digital TV API lets you control these hardware components through
currently six Unix-style character devices for video, audio, frontend,
demux, CA and IP-over-DVB networking. The video and audio devices
control the MPEG2 decoder hardware, the frontend device the tuner and
the Digital TV demodulator. The demux device gives you control over the PES
and section filters of the hardware. If the hardware does not support
filtering, these filters can be implemented in software. Finally, the CA
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

where ``N`` enumerates the Digital TV cards in a system starting from 0, and
``M`` enumerates the devices of each type within each adapter, starting
from 0, too. We will omit the "``/dev/dvb/adapterN/``\ " in the further
discussion of these devices.

More details about the data structures and function calls of all the
devices are described in the following chapters.


.. _include_files:

API include files
=================

For each of the Digital TV devices a corresponding include file exists. The
Digital TV API include files should be included in application sources with a
partial path like:


.. code-block:: c

	#include <linux/dvb/ca.h>

	#include <linux/dvb/dmx.h>

	#include <linux/dvb/frontend.h>

	#include <linux/dvb/net.h>


To enable applications to support different API version, an additional
include file ``linux/dvb/version.h`` exists, which defines the constant
``DVB_API_VERSION``. This document describes ``DVB_API_VERSION 5.10``.

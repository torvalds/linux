.. -*- coding: utf-8; mode: rst -*-

.. _ttx:

******************
Teletext Interface
******************

This interface was aimed at devices receiving and demodulating Teletext
data [:ref:`ets300706`, :ref:`itu653`], evaluating the Teletext
packages and storing formatted pages in cache memory. Such devices are
usually implemented as microcontrollers with serial interface
(I:sup:`2`\ C) and could be found on old TV cards, dedicated Teletext
decoding cards and home-brew devices connected to the PC parallel port.

The Teletext API was designed by Martin Buck. It was defined in the
kernel header file ``linux/videotext.h``, the specification is available
from
`ftp://ftp.gwdg.de/pub/linux/misc/videotext/ <ftp://ftp.gwdg.de/pub/linux/misc/videotext/>`__.
(Videotext is the name of the German public television Teletext
service.)

Eventually the Teletext API was integrated into the V4L API with
character device file names ``/dev/vtx0`` to ``/dev/vtx31``, device
major number 81, minor numbers 192 to 223.

However, teletext decoders were quickly replaced by more generic VBI
demodulators and those dedicated teletext decoders no longer exist. For
many years the vtx devices were still around, even though nobody used
them. So the decision was made to finally remove support for the
Teletext API in kernel 2.6.37.

Modern devices all use the :ref:`raw <raw-vbi>` or
:ref:`sliced` VBI API.

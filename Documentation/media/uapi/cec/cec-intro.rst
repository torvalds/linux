.. _cec-intro:

Introduction
============

.. note:: This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

HDMI connectors provide a single pin for use by the Consumer Electronics
Control protocol. This protocol allows different devices connected by an
HDMI cable to communicate. The protocol for CEC version 1.4 is defined
in supplements 1 (CEC) and 2 (HEAC or HDMI Ethernet and Audio Return
Channel) of the HDMI 1.4a (:ref:`hdmi`) specification and the
extensions added to CEC version 2.0 are defined in chapter 11 of the
HDMI 2.0 (:ref:`hdmi2`) specification.

The bitrate is very slow (effectively no more than 36 bytes per second)
and is based on the ancient AV.link protocol used in old SCART
connectors. The protocol closely resembles a crazy Rube Goldberg
contraption and is an unholy mix of low and high level messages. Some
messages, especially those part of the HEAC protocol layered on top of
CEC, need to be handled by the kernel, others can be handled either by
the kernel or by userspace.

In addition, CEC can be implemented in HDMI receivers, transmitters and
in USB devices that have an HDMI input and an HDMI output and that
control just the CEC pin.

Drivers that support CEC will create a CEC device node (/dev/cecX) to
give userspace access to the CEC adapter. The
:ref:`CEC_ADAP_G_CAPS` ioctl will tell userspace what it is allowed to do.

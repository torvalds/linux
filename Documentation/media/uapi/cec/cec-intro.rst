.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _cec-intro:

Introduction
============

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

In order to check the support and test it, it is suggested to download
the `v4l-utils <https://git.linuxtv.org/v4l-utils.git/>`_ package. It
provides three tools to handle CEC:

- cec-ctl: the Swiss army knife of CEC. Allows you to configure, transmit
  and monitor CEC messages.

- cec-compliance: does a CEC compliance test of a remote CEC device to
  determine how compliant the CEC implementation is.

- cec-follower: emulates a CEC follower.

.. SPDX-License-Identifier: GPL-2.0-only

.. Copyright (C) 2020-21 Intel Corporation

.. _t7xx_driver_doc:

============================================
t7xx driver for MTK PCIe based T700 5G modem
============================================
The t7xx driver is a WWAN PCIe host driver developed for linux or Chrome OS platforms
for data exchange over PCIe interface between Host platform & MediaTek's T700 5G modem.
The driver exposes an interface conforming to the MBIM protocol [1]. Any front end
application (e.g. Modem Manager) could easily manage the MBIM interface to enable
data communication towards WWAN. The driver also provides an interface to interact
with the MediaTek's modem via AT commands.

Basic usage
===========
MBIM & AT functions are inactive when unmanaged. The t7xx driver provides
WWAN port userspace interfaces representing MBIM & AT control channels and does
not play any role in managing their functionality. It is the job of a userspace
application to detect port enumeration and enable MBIM & AT functionalities.

Examples of few such userspace applications are:

- mbimcli (included with the libmbim [2] library), and
- Modem Manager [3]

Management Applications to carry out below required actions for establishing
MBIM IP session:

- open the MBIM control channel
- configure network connection settings
- connect to network
- configure IP network interface

Management Applications to carry out below required actions for send an AT
command and receive response:

- open the AT control channel using a UART tool or a special user tool

Sysfs
=====
The driver provides sysfs interfaces to userspace.

t7xx_mode
---------
The sysfs interface provides userspace with access to the device mode, this interface
supports read and write operations.

Device mode:

- ``unknown`` represents that device in unknown status
- ``ready`` represents that device in ready status
- ``reset`` represents that device in reset status
- ``fastboot_switching`` represents that device in fastboot switching status
- ``fastboot_download`` represents that device in fastboot download status
- ``fastboot_dump`` represents that device in fastboot dump status

Read from userspace to get the current device mode.

::
  $ cat /sys/bus/pci/devices/${bdf}/t7xx_mode

Write from userspace to set the device mode.

::
  $ echo fastboot_switching > /sys/bus/pci/devices/${bdf}/t7xx_mode

Management application development
==================================
The driver and userspace interfaces are described below. The MBIM protocol is
described in [1] Mobile Broadband Interface Model v1.0 Errata-1.

MBIM control channel userspace ABI
----------------------------------

/dev/wwan0mbim0 character device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The driver exposes an MBIM interface to the MBIM function by implementing
MBIM WWAN Port. The userspace end of the control channel pipe is a
/dev/wwan0mbim0 character device. Application shall use this interface for
MBIM protocol communication.

Fragmentation
~~~~~~~~~~~~~
The userspace application is responsible for all control message fragmentation
and defragmentation as per MBIM specification.

/dev/wwan0mbim0 write()
~~~~~~~~~~~~~~~~~~~~~~~
The MBIM control messages from the management application must not exceed the
negotiated control message size.

/dev/wwan0mbim0 read()
~~~~~~~~~~~~~~~~~~~~~~
The management application must accept control messages of up the negotiated
control message size.

MBIM data channel userspace ABI
-------------------------------

wwan0-X network device
~~~~~~~~~~~~~~~~~~~~~~
The t7xx driver exposes IP link interface "wwan0-X" of type "wwan" for IP
traffic. Iproute network utility is used for creating "wwan0-X" network
interface and for associating it with MBIM IP session.

The userspace management application is responsible for creating new IP link
prior to establishing MBIM IP session where the SessionId is greater than 0.

For example, creating new IP link for a MBIM IP session with SessionId 1:

  ip link add dev wwan0-1 parentdev wwan0 type wwan linkid 1

The driver will automatically map the "wwan0-1" network device to MBIM IP
session 1.

AT port userspace ABI
----------------------------------

/dev/wwan0at0 character device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The driver exposes an AT port by implementing AT WWAN Port.
The userspace end of the control port is a /dev/wwan0at0 character
device. Application shall use this interface to issue AT commands.

fastboot port userspace ABI
---------------------------

/dev/wwan0fastboot0 character device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The driver exposes a fastboot protocol interface by implementing
fastboot WWAN Port. The userspace end of the fastboot channel pipe is a
/dev/wwan0fastboot0 character device. Application shall use this interface for
fastboot protocol communication.

Please note that driver needs to be reloaded to export /dev/wwan0fastboot0
port, because device needs a cold reset after enter ``fastboot_switching``
mode.

The MediaTek's T700 modem supports the 3GPP TS 27.007 [4] specification.

References
==========
[1] *MBIM (Mobile Broadband Interface Model) Errata-1*

- https://www.usb.org/document-library/

[2] *libmbim "a glib-based library for talking to WWAN modems and devices which
speak the Mobile Interface Broadband Model (MBIM) protocol"*

- http://www.freedesktop.org/wiki/Software/libmbim/

[3] *Modem Manager "a DBus-activated daemon which controls mobile broadband
(2G/3G/4G/5G) devices and connections"*

- http://www.freedesktop.org/wiki/Software/ModemManager/

[4] *Specification # 27.007 - 3GPP*

- https://www.3gpp.org/DynaReport/27007.htm

[5] *fastboot "a mechanism for communicating with bootloaders"*

- https://android.googlesource.com/platform/system/core/+/refs/heads/main/fastboot/README.md

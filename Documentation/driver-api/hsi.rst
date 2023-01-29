High Speed Synchronous Serial Interface (HSI)
=============================================

Introduction
---------------

High Speed Synchronous Interface (HSI) is a full duplex, low latency protocol,
that is optimized for die-level interconnect between an Application Processor
and a Baseband chipset. It has been specified by the MIPI alliance in 2003 and
implemented by multiple vendors since then.

The HSI interface supports full duplex communication over multiple channels
(typically 8) and is capable of reaching speeds up to 200 Mbit/s.

The serial protocol uses two signals, DATA and FLAG as combined data and clock
signals and an additional READY signal for flow control. An additional WAKE
signal can be used to wakeup the chips from standby modes. The signals are
commonly prefixed by AC for signals going from the application die to the
cellular die and CA for signals going the other way around.

::

    +------------+                                 +---------------+
    |  Cellular  |                                 |  Application  |
    |    Die     |                                 |      Die      |
    |            | - - - - - - CAWAKE - - - - - - >|               |
    |           T|------------ CADATA ------------>|R              |
    |           X|------------ CAFLAG ------------>|X              |
    |            |<----------- ACREADY ------------|               |
    |            |                                 |               |
    |            |                                 |               |
    |            |< - - - - -  ACWAKE - - - - - - -|               |
    |           R|<----------- ACDATA -------------|T              |
    |           X|<----------- ACFLAG -------------|X              |
    |            |------------ CAREADY ----------->|               |
    |            |                                 |               |
    |            |                                 |               |
    +------------+                                 +---------------+

HSI Subsystem in Linux
-------------------------

In the Linux kernel the hsi subsystem is supposed to be used for HSI devices.
The hsi subsystem contains drivers for hsi controllers including support for
multi-port controllers and provides a generic API for using the HSI ports.

It also contains HSI client drivers, which make use of the generic API to
implement a protocol used on the HSI interface. These client drivers can
use an arbitrary number of channels.

hsi-char Device
------------------

Each port automatically registers a generic client driver called hsi_char,
which provides a character device for userspace representing the HSI port.
It can be used to communicate via HSI from userspace. Userspace may
configure the hsi_char device using the following ioctl commands:

HSC_RESET
 flush the HSI port

HSC_SET_PM
 enable or disable the client.

HSC_SEND_BREAK
 send break

HSC_SET_RX
 set RX configuration

HSC_GET_RX
 get RX configuration

HSC_SET_TX
 set TX configuration

HSC_GET_TX
 get TX configuration

The kernel HSI API
------------------

.. kernel-doc:: include/linux/hsi/hsi.h
   :internal:

.. kernel-doc:: drivers/hsi/hsi_core.c
   :export:


.. SPDX-License-Identifier: GPL-2.0

============
Rmnet Driver
============

1. Introduction
===============

rmnet driver is used for supporting the Multiplexing and aggregation
Protocol (MAP). This protocol is used by all recent chipsets using Qualcomm
Technologies, Inc. modems.

This driver can be used to register onto any physical network device in
IP mode. Physical transports include USB, HSIC, PCIe and IP accelerator.

Multiplexing allows for creation of logical netdevices (rmnet devices) to
handle multiple private data networks (PDN) like a default internet, tethering,
multimedia messaging service (MMS) or IP media subsystem (IMS). Hardware sends
packets with MAP headers to rmnet. Based on the multiplexer id, rmnet
routes to the appropriate PDN after removing the MAP header.

Aggregation is required to achieve high data rates. This involves hardware
sending aggregated bunch of MAP frames. rmnet driver will de-aggregate
these MAP frames and send them to appropriate PDN's.

2. Packet format
================

a. MAP packet (data / control)

MAP header has the same endianness of the IP packet.

Packet format::

  Bit             0             1           2-7      8 - 15           16 - 31
  Function   Command / Data   Reserved     Pad   Multiplexer ID    Payload length
  Bit            32 - x
  Function     Raw  Bytes

Command (1)/ Data (0) bit value is to indicate if the packet is a MAP command
or data packet. Control packet is used for transport level flow control. Data
packets are standard IP packets.

Reserved bits are usually zeroed out and to be ignored by receiver.

Padding is number of bytes to be added for 4 byte alignment if required by
hardware.

Multiplexer ID is to indicate the PDN on which data has to be sent.

Payload length includes the padding length but does not include MAP header
length.

b. MAP packet (command specific)::

    Bit             0             1           2-7      8 - 15           16 - 31
    Function   Command         Reserved     Pad   Multiplexer ID    Payload length
    Bit          32 - 39        40 - 45    46 - 47       48 - 63
    Function   Command name    Reserved   Command Type   Reserved
    Bit          64 - 95
    Function   Transaction ID
    Bit          96 - 127
    Function   Command data

Command 1 indicates disabling flow while 2 is enabling flow

Command types

= ==========================================
0 for MAP command request
1 is to acknowledge the receipt of a command
2 is for unsupported commands
3 is for error during processing of commands
= ==========================================

c. Aggregation

Aggregation is multiple MAP packets (can be data or command) delivered to
rmnet in a single linear skb. rmnet will process the individual
packets and either ACK the MAP command or deliver the IP packet to the
network stack as needed

MAP header|IP Packet|Optional padding|MAP header|IP Packet|Optional padding....

MAP header|IP Packet|Optional padding|MAP header|Command Packet|Optional pad...

3. Userspace configuration
==========================

rmnet userspace configuration is done through netlink library librmnetctl
and command line utility rmnetcli. Utility is hosted in codeaurora forum git.
The driver uses rtnl_link_ops for communication.

https://source.codeaurora.org/quic/la/platform/vendor/qcom-opensource/dataservices/tree/rmnetctl

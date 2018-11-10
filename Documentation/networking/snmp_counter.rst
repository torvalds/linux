===========
SNMP counter
===========

This document explains the meaning of SNMP counters.

General IPv4 counters
====================
All layer 4 packets and ICMP packets will change these counters, but
these counters won't be changed by layer 2 packets (such as STP) or
ARP packets.

* IpInReceives
Defined in `RFC1213 ipInReceives`_

.. _RFC1213 ipInReceives: https://tools.ietf.org/html/rfc1213#page-26

The number of packets received by the IP layer. It gets increasing at the
beginning of ip_rcv function, always be updated together with
IpExtInOctets. It indicates the number of aggregated segments after
GRO/LRO.

* IpInDelivers
Defined in `RFC1213 ipInDelivers`_

.. _RFC1213 ipInDelivers: https://tools.ietf.org/html/rfc1213#page-28

The number of packets delivers to the upper layer protocols. E.g. TCP, UDP,
ICMP and so on. If no one listens on a raw socket, only kernel
supported protocols will be delivered, if someone listens on the raw
socket, all valid IP packets will be delivered.

* IpOutRequests
Defined in `RFC1213 ipOutRequests`_

.. _RFC1213 ipOutRequests: https://tools.ietf.org/html/rfc1213#page-28

The number of packets sent via IP layer, for both single cast and
multicast packets, and would always be updated together with
IpExtOutOctets.

* IpExtInOctets and IpExtOutOctets
They are linux kernel extensions, no RFC definitions. Please note,
RFC1213 indeed defines ifInOctets  and ifOutOctets, but they
are different things. The ifInOctets and ifOutOctets include the MAC
layer header size but IpExtInOctets and IpExtOutOctets don't, they
only include the IP layer header and the IP layer data.

* IpExtInNoECTPkts, IpExtInECT1Pkts, IpExtInECT0Pkts, IpExtInCEPkts
They indicate the number of four kinds of ECN IP packets, please refer
`Explicit Congestion Notification`_ for more details.

.. _Explicit Congestion Notification: https://tools.ietf.org/html/rfc3168#page-6

These 4 counters calculate how many packets received per ECN
status. They count the real frame number regardless the LRO/GRO. So
for the same packet, you might find that IpInReceives count 1, but
IpExtInNoECTPkts counts 2 or more.

ICMP counters
============
* IcmpInMsgs and IcmpOutMsgs
Defined by `RFC1213 icmpInMsgs`_ and `RFC1213 icmpOutMsgs`_

.. _RFC1213 icmpInMsgs: https://tools.ietf.org/html/rfc1213#page-41
.. _RFC1213 icmpOutMsgs: https://tools.ietf.org/html/rfc1213#page-43

As mentioned in the RFC1213, these two counters include errors, they
would be increased even if the ICMP packet has an invalid type. The
ICMP output path will check the header of a raw socket, so the
IcmpOutMsgs would still be updated if the IP header is constructed by
a userspace program.

* ICMP named types
| These counters include most of common ICMP types, they are:
| IcmpInDestUnreachs: `RFC1213 icmpInDestUnreachs`_
| IcmpInTimeExcds: `RFC1213 icmpInTimeExcds`_
| IcmpInParmProbs: `RFC1213 icmpInParmProbs`_
| IcmpInSrcQuenchs: `RFC1213 icmpInSrcQuenchs`_
| IcmpInRedirects: `RFC1213 icmpInRedirects`_
| IcmpInEchos: `RFC1213 icmpInEchos`_
| IcmpInEchoReps: `RFC1213 icmpInEchoReps`_
| IcmpInTimestamps: `RFC1213 icmpInTimestamps`_
| IcmpInTimestampReps: `RFC1213 icmpInTimestampReps`_
| IcmpInAddrMasks: `RFC1213 icmpInAddrMasks`_
| IcmpInAddrMaskReps: `RFC1213 icmpInAddrMaskReps`_
| IcmpOutDestUnreachs: `RFC1213 icmpOutDestUnreachs`_
| IcmpOutTimeExcds: `RFC1213 icmpOutTimeExcds`_
| IcmpOutParmProbs: `RFC1213 icmpOutParmProbs`_
| IcmpOutSrcQuenchs: `RFC1213 icmpOutSrcQuenchs`_
| IcmpOutRedirects: `RFC1213 icmpOutRedirects`_
| IcmpOutEchos: `RFC1213 icmpOutEchos`_
| IcmpOutEchoReps: `RFC1213 icmpOutEchoReps`_
| IcmpOutTimestamps: `RFC1213 icmpOutTimestamps`_
| IcmpOutTimestampReps: `RFC1213 icmpOutTimestampReps`_
| IcmpOutAddrMasks: `RFC1213 icmpOutAddrMasks`_
| IcmpOutAddrMaskReps: `RFC1213 icmpOutAddrMaskReps`_

.. _RFC1213 icmpInDestUnreachs: https://tools.ietf.org/html/rfc1213#page-41
.. _RFC1213 icmpInTimeExcds: https://tools.ietf.org/html/rfc1213#page-41
.. _RFC1213 icmpInParmProbs: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInSrcQuenchs: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInRedirects: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInEchos: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInEchoReps: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInTimestamps: https://tools.ietf.org/html/rfc1213#page-42
.. _RFC1213 icmpInTimestampReps: https://tools.ietf.org/html/rfc1213#page-43
.. _RFC1213 icmpInAddrMasks: https://tools.ietf.org/html/rfc1213#page-43
.. _RFC1213 icmpInAddrMaskReps: https://tools.ietf.org/html/rfc1213#page-43

.. _RFC1213 icmpOutDestUnreachs: https://tools.ietf.org/html/rfc1213#page-44
.. _RFC1213 icmpOutTimeExcds: https://tools.ietf.org/html/rfc1213#page-44
.. _RFC1213 icmpOutParmProbs: https://tools.ietf.org/html/rfc1213#page-44
.. _RFC1213 icmpOutSrcQuenchs: https://tools.ietf.org/html/rfc1213#page-44
.. _RFC1213 icmpOutRedirects: https://tools.ietf.org/html/rfc1213#page-44
.. _RFC1213 icmpOutEchos: https://tools.ietf.org/html/rfc1213#page-45
.. _RFC1213 icmpOutEchoReps: https://tools.ietf.org/html/rfc1213#page-45
.. _RFC1213 icmpOutTimestamps: https://tools.ietf.org/html/rfc1213#page-45
.. _RFC1213 icmpOutTimestampReps: https://tools.ietf.org/html/rfc1213#page-45
.. _RFC1213 icmpOutAddrMasks: https://tools.ietf.org/html/rfc1213#page-45
.. _RFC1213 icmpOutAddrMaskReps: https://tools.ietf.org/html/rfc1213#page-46

Every ICMP type has two counters: 'In' and 'Out'. E.g., for the ICMP
Echo packet, they are IcmpInEchos and IcmpOutEchos. Their meanings are
straightforward. The 'In' counter means kernel receives such a packet
and the 'Out' counter means kernel sends such a packet.

* ICMP numeric types
They are IcmpMsgInType[N] and IcmpMsgOutType[N], the [N] indicates the
ICMP type number. These counters track all kinds of ICMP packets. The
ICMP type number definition could be found in the `ICMP parameters`_
document.

.. _ICMP parameters: https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml

For example, if the Linux kernel sends an ICMP Echo packet, the
IcmpMsgOutType8 would increase 1. And if kernel gets an ICMP Echo Reply
packet, IcmpMsgInType0 would increase 1.

* IcmpInCsumErrors
This counter indicates the checksum of the ICMP packet is
wrong. Kernel verifies the checksum after updating the IcmpInMsgs and
before updating IcmpMsgInType[N]. If a packet has bad checksum, the
IcmpInMsgs would be updated but none of IcmpMsgInType[N] would be updated.

* IcmpInErrors and IcmpOutErrors
Defined by `RFC1213 icmpInErrors`_ and `RFC1213 icmpOutErrors`_

.. _RFC1213 icmpInErrors: https://tools.ietf.org/html/rfc1213#page-41
.. _RFC1213 icmpOutErrors: https://tools.ietf.org/html/rfc1213#page-43

When an error occurs in the ICMP packet handler path, these two
counters would be updated. The receiving packet path use IcmpInErrors
and the sending packet path use IcmpOutErrors. When IcmpInCsumErrors
is increased, IcmpInErrors would always be increased too.

relationship of the ICMP counters
-------------------------------
The sum of IcmpMsgOutType[N] is always equal to IcmpOutMsgs, as they
are updated at the same time. The sum of IcmpMsgInType[N] plus
IcmpInErrors should be equal or larger than IcmpInMsgs. When kernel
receives an ICMP packet, kernel follows below logic:

1. increase IcmpInMsgs
2. if has any error, update IcmpInErrors and finish the process
3. update IcmpMsgOutType[N]
4. handle the packet depending on the type, if has any error, update
   IcmpInErrors and finish the process

So if all errors occur in step (2), IcmpInMsgs should be equal to the
sum of IcmpMsgOutType[N] plus IcmpInErrors. If all errors occur in
step (4), IcmpInMsgs should be equal to the sum of
IcmpMsgOutType[N]. If the errors occur in both step (2) and step (4),
IcmpInMsgs should be less than the sum of IcmpMsgOutType[N] plus
IcmpInErrors.

examples
=======

ping test
--------
Run the ping command against the public dns server 8.8.8.8::

  nstatuser@nstat-a:~$ ping 8.8.8.8 -c 1
  PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.
  64 bytes from 8.8.8.8: icmp_seq=1 ttl=119 time=17.8 ms

  --- 8.8.8.8 ping statistics ---
  1 packets transmitted, 1 received, 0% packet loss, time 0ms
  rtt min/avg/max/mdev = 17.875/17.875/17.875/0.000 ms

The nstayt result::

  nstatuser@nstat-a:~$ nstat
  #kernel
  IpInReceives                    1                  0.0
  IpInDelivers                    1                  0.0
  IpOutRequests                   1                  0.0
  IcmpInMsgs                      1                  0.0
  IcmpInEchoReps                  1                  0.0
  IcmpOutMsgs                     1                  0.0
  IcmpOutEchos                    1                  0.0
  IcmpMsgInType0                  1                  0.0
  IcmpMsgOutType8                 1                  0.0
  IpExtInOctets                   84                 0.0
  IpExtOutOctets                  84                 0.0
  IpExtInNoECTPkts                1                  0.0

The Linux server sent an ICMP Echo packet, so IpOutRequests,
IcmpOutMsgs, IcmpOutEchos and IcmpMsgOutType8 were increased 1. The
server got ICMP Echo Reply from 8.8.8.8, so IpInReceives, IcmpInMsgs,
IcmpInEchoReps and IcmpMsgInType0 were increased 1. The ICMP Echo Reply
was passed to the ICMP layer via IP layer, so IpInDelivers was
increased 1. The default ping data size is 48, so an ICMP Echo packet
and its corresponding Echo Reply packet are constructed by:

* 14 bytes MAC header
* 20 bytes IP header
* 16 bytes ICMP header
* 48 bytes data (default value of the ping command)

So the IpExtInOctets and IpExtOutOctets are 20+16+48=84.

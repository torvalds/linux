.. SPDX-License-Identifier: GPL-2.0

==================
Linux X.25 Project
==================

As my third year dissertation at University I have taken it upon myself to
write an X.25 implementation for Linux. My aim is to provide a complete X.25
Packet Layer and a LAPB module to allow for "normal" X.25 to be run using
Linux. There are two sorts of X.25 cards available, intelligent ones that
implement LAPB on the card itself, and unintelligent ones that simply do
framing, bit-stuffing and checksumming. These both need to be handled by the
system.

I therefore decided to write the implementation such that as far as the
Packet Layer is concerned, the link layer was being performed by a lower
layer of the Linux kernel and therefore it did not concern itself with
implementation of LAPB. Therefore the LAPB modules would be called by
unintelligent X.25 card drivers and not by intelligent ones, this would
provide a uniform device driver interface, and simplify configuration.

To confuse matters a little, an 802.2 LLC implementation is also possible
which could allow X.25 to be run over an Ethernet (or Token Ring) and
conform with the JNT "Pink Book", this would have a different interface to
the Packet Layer but there would be no confusion since the class of device
being served by the LLC would be completely separate from LAPB.

Just when you thought that it could not become more confusing, another
option appeared, XOT. This allows X.25 Packet Layer frames to operate over
the Internet using TCP/IP as a reliable link layer. RFC1613 specifies the
format and behaviour of the protocol. If time permits this option will also
be actively considered.

A linux-x25 mailing list has been created at vger.kernel.org to support the
development and use of Linux X.25. It is early days yet, but interested
parties are welcome to subscribe to it. Just send a message to
majordomo@vger.kernel.org with the following in the message body:

subscribe linux-x25
end

The contents of the Subject line are ignored.

Jonathan

g4klx@g4klx.demon.co.uk

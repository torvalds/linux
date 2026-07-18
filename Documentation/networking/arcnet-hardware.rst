.. SPDX-License-Identifier: GPL-2.0

===============
ARCnet Hardware
===============

:Author: Avery Pennarun <apenwarr@worldvisions.ca>

.. note::

   This file is a supplement to arcnet.rst.  Please read that for general
   driver configuration help.

Because so many people (myself included) seem to have obtained ARCnet cards
without manuals, this file contains a quick introduction to ARCnet hardware
and some cabling tips. If you have any other information, do not hesitate to
:ref:`send an email to netdev <arcnet-netdev>`.


Introduction to ARCnet
======================

ARCnet is a network type which works in a way similar to popular Ethernet
networks but which is also different in some very important ways.

First of all, you can get ARCnet cards in at least two speeds: 2.5 Mbps
(slower than Ethernet) and 100 Mbps (faster than normal Ethernet).  In fact,
there are others as well, but these are less common.  The different hardware
types, as far as I'm aware, are not compatible and so you cannot wire a
100 Mbps card to a 2.5 Mbps card, and so on.  From what I hear, my driver does
work with 100 Mbps cards, but I haven't been able to verify this myself,
since I only have the 2.5 Mbps variety.  It is probably not going to saturate
your 100 Mbps card.  Stop complaining. :)

You also cannot connect an ARCnet card to any kind of Ethernet card and
expect it to work.

There are two "types" of ARCnet - STAR topology and BUS topology.  This
refers to how the cards are meant to be wired together.  According to most
available documentation, you can only connect STAR cards to STAR cards and
BUS cards to BUS cards.  That makes sense, right?  Well, it's not quite
true; see below under "Cabling."

Once you get past these little stumbling blocks, ARCnet is actually quite a
well-designed standard.  It uses something called "modified token passing"
which makes it completely incompatible with so-called "Token Ring" cards,
but which makes transfers much more reliable than Ethernet does.  In fact,
ARCnet will guarantee that a packet arrives safely at the destination, and
even if it can't possibly be delivered properly (ie. because of a cable
break, or because the destination computer does not exist) it will at least
tell the sender about it.

Because of the carefully defined action of the "token", it will always make
a pass around the "ring" within a maximum length of time.  This makes it
useful for realtime networks.

In addition, all known ARCnet cards have an (almost) identical programming
interface.  This means that with one ARCnet driver you can support any
card, whereas with Ethernet each manufacturer uses what is sometimes a
completely different programming interface, leading to a lot of different,
sometimes very similar, Ethernet drivers.  Of course, always using the same
programming interface also means that when high-performance hardware
facilities like PCI bus mastering DMA appear, it's hard to take advantage of
them.  Let's not go into that.

One thing that makes ARCnet cards difficult to program for, however, is the
limit on their packet sizes; standard ARCnet can only send packets that are
up to 508 bytes in length.  This is smaller than the Internet "bare minimum"
of 576 bytes, let alone the Ethernet MTU of 1500.  To compensate, an extra
level of encapsulation is defined by RFC1201, which I call "packet
splitting," that allows "virtual packets" to grow as large as 64K each,
although they are generally kept down to the Ethernet-style 1500 bytes.

For more information on ARCnet networks, visit the "ARCNET Resource Center"
WWW page at:

	https://www.arcnet.cc


Cabling ARCnet Networks
=======================

This section was rewritten by

	Vojtech Pavlik     <vojtech@suse.cz>

using information from several people, including:

	- Avery Pennraun     <apenwarr@worldvisions.ca>
	- Stephen A. Wood    <saw@hallc1.cebaf.gov>
	- John Paul Morrison <jmorriso@bogomips.ee.ubc.ca>
	- Joachim Koenig     <jojo@repas.de>

and Avery touched it up a bit, at Vojtech's request.

ARCnet (the classic 2.5 Mbps version) can be connected by two different
types of cabling: coax and twisted pair.  The other ARCnet-type networks
(100 Mbps TCNS and 320 kbps - 32 Mbps ARCnet Plus) use different types of
cabling (Type1, Fiber, C1, C4, C5).

For a coax network, you "should" use 93 Ohm RG-62 cable.  But other cables
also work fine, because ARCnet is a very stable network. I personally use 75
Ohm TV antenna cable.

Cards for coax cabling are shipped in two different variants: for BUS and
STAR network topologies.  They are mostly the same.  The only difference
lies in the hybrid chip installed.  BUS cards use high impedance output,
while STAR use low impedance.  Low impedance card (STAR) is electrically
equal to a high impedance one with a terminator installed.

Usually, the ARCnet networks are built up from STAR cards and hubs.  There
are two types of hubs - active and passive.  Passive hubs are small boxes
with four BNC connectors containing four 47 Ohm resistors::

	   |         | wires
	   R         + junction
	-R-+-R-      R 47 Ohm resistors
	   R
	   |

The shielding is connected together.  Active hubs are much more complicated;
they are powered and contain electronics to amplify the signal and send it
to other segments of the net.  They usually have eight connectors.  Active
hubs come in two variants - dumb and smart.  The dumb variant just
amplifies, but the smart one decodes to digital and encodes back all packets
coming through.  This is much better if you have several hubs in the net,
since many dumb active hubs may worsen the signal quality.

And now to the cabling.  What you can connect together:

1. A card to a card.  This is the simplest way of creating a 2-computer
   network.

2. A card to a passive hub.  Remember that all unused connectors on the hub
   must be properly terminated with 93 Ohm terminators (or something else if you
   don't have the right ones), although the network may work without
   terminators.

3. A card to an active hub.  Here there is no need to terminate the unused
   connectors except some kind of aesthetic feeling.  But, there may not be
   more than eleven active hubs between any two computers.  That of course
   doesn't limit the number of active hubs on the network.

4. An active hub to another.

5. An active hub to passive hub.

Remember that you cannot connect two passive hubs together.  The power loss
implied by such a connection is too high for the network to operate reliably.

An example of a typical ARCnet network::

	   R                     S - STAR type card
    S------H--------A-------S    R - Terminator
	   |        |            H - Hub
	   |        |            A - Active hub
	   |   S----H----S
	   S        |
		    |
		    S

The BUS topology is very similar to the one used by 10BASE2 Ethernet.  The only
difference is in cable and terminators: they should be 93 Ohm. 10BASE2 Ethernet
uses 50 Ohm impedance. You use T connectors to put the computers on a single
line of cable, the bus. You have to put terminators at both ends of the
cable. A typical BUS ARCnet network looks like::

    RT----T------T------T------T------TR
     B    B      B      B      B      B

  B - BUS type card
  R - Terminator
  T - T connector

But that is not all! The two types can be connected together.  According to
the official documentation, the only way of connecting them is using an active
hub::

	 A------T------T------TR
	 |      B      B      B
     S---H---S
	 |
	 S

The official docs also state that you can use STAR cards at the ends of a
BUS network in place of a BUS card and a terminator::

     S------T------T------S
	    B      B

But, according to my own experiments, you can simply hang a BUS type card
anywhere in middle of a cable in a STAR topology network.  And more - you
can use the bus card in place of any star card if you use a terminator. Then
you can build very complicated networks fulfilling all your needs!  An
example::

				  S
				  |
	   RT------T-------T------H------S
	    B      B       B      |
				  |       R
    S------A------T-------T-------A-------H------TR
	   |      B       B       |       |      B
	   |   S                 BT       |
	   |   |                  |  S----A-----S
    S------H---A----S             |       |
	   |   |      S------T----H---S   |
	   S   S             B    R       S

A completely different cabling scheme is used with Twisted Pair cabling. Each
of the TP cards has two RJ (phone-cord style) connectors.  The cards are
then daisy-chained together using a cable connecting every two neighboring
cards.  The ends are terminated with RJ 93 Ohm terminators which plug into
the empty connectors of cards on the ends of the chain.  An example::

	  ___________   ___________
      _R_|_         _|_|_         _|_R_
     |     |       |     |       |     |
     |Card |       |Card |       |Card |
     |_____|       |_____|       |_____|


There are also hubs for the TP topology.  There is nothing difficult
involved in using them; you just connect a TP chain to a hub on any end or
even at both.  This way you can create almost any network configuration.
The maximum of 11 hubs between any two computers on the net applies here as
well.  An example::

    RP-------P--------P--------H-----P------P-----PR
			       |
      RP-----H--------P--------H-----P------PR
	     |                 |
	     PR                PR

    R - RJ Terminator
    P - TP Card
    H - TP Hub

Like any network, ARCnet has a limited cable length.  These are the maximum
cable lengths between two active ends (an active end being an active hub or
a STAR card).

		========== ======= ===========
		RG-62       93 Ohm up to 650 m
		RG-59/U     75 Ohm up to 457 m
		RG-11/U     75 Ohm up to 533 m
		IBM Type 1 150 Ohm up to 200 m
		IBM Type 3 100 Ohm up to 100 m
		========== ======= ===========

The maximum length of all cables connected to a passive hub is limited to 65
meters for RG-62 cabling; less for others.  You can see that using passive
hubs in a large network is a bad idea. The maximum length of a single "BUS
Trunk" is about 300 meters for RG-62. The maximum distance between the two
most distant points of the net is limited to 3000 meters. The maximum length
of a TP cable between two cards/hubs is 650 meters.


Setting the Jumpers
===================

  - Every ARCnet card has its own "unique" network address from 0 to 255.
    Unlike Ethernet, you can set this address yourself with a jumper or switch
    (or on some cards, with special software).  Since it's only 8 bits, you can
    only have 254 ARCnet cards on a network.  DON'T use 0 or 255, since these
    are reserved (although neat stuff will probably happen if you DO use them).
    By the way, if you haven't already guessed, don't set this the same as any
    other ARCnet device on your network!

  - There may be ETS1 and ETS2 settings.  These may or may not make a
    difference on your card (many manuals call them "reserved"), but are
    used to change the delays used when powering up a computer on the
    network.  This is only necessary when wiring VERY long range ARCnet
    networks, on the order of 4km or so; in any case, the only real
    requirement here is that all cards on the network with ETS1 and ETS2
    jumpers have them in the same position.  Chris Hindy <chrish@io.org>
    sent in a chart with actual values for this:

	======= ======= =============== ====================
	ET1	ET2	Response Time	Reconfiguration Time
	======= ======= =============== ====================
	open	open	74.7us		840us
	open	closed	283.4us		1680us
	closed	open	561.8us		1680us
	closed	closed	1118.6us	1680us
	======= ======= =============== ====================

    Make sure you set ETS1 and ETS2 to the SAME VALUE for all cards on your
    network.

LED Indicators
==============

Many cards have red and green LEDs, which have the following meanings:

	=============== =============== =====================================
	Green           Red             Status
	=============== =============== =====================================
	OFF             OFF             Power off
	OFF             Short flashes   Cabling problems (broken cable or not
					terminated)
	OFF (short)     ON              Card init
	ON              ON              Normal state - everything OK, nothing
					happens
	ON              Long flashes    Data transfer
	ON              OFF             Never happens (maybe when wrong ID)
	=============== =============== =====================================

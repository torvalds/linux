.. SPDX-License-Identifier: GPL-2.0

Digital TV Conditional Access Interface
=======================================


.. note::

   This documentation is outdated.

This document describes the usage of the high level CI API as
in accordance to the Linux DVB API. This is a not a documentation for the,
existing low level CI API.

.. note::

   For the Twinhan/Twinhan clones, the dst_ca module handles the CI
   hardware handling. This module is loaded automatically if a CI
   (Common Interface, that holds the CAM (Conditional Access Module)
   is detected.

ca_zap
~~~~~~

A userspace application, like ``ca_zap`` is required to handle encrypted
MPEG-TS streams.

The ``ca_zap`` userland application is in charge of sending the
descrambling related information to the Conditional Access Module (CAM).

This application requires the following to function properly as of now.

a) Tune to a valid channel, with szap.

  eg: $ szap -c channels.conf -r "TMC" -x

b) a channels.conf containing a valid PMT PID

  eg: TMC:11996:h:0:27500:278:512:650:321

  here 278 is a valid PMT PID. the rest of the values are the
  same ones that szap uses.

c) after running a szap, you have to run ca_zap, for the
   descrambler to function,

  eg: $ ca_zap channels.conf "TMC"

d) Hopefully enjoy your favourite subscribed channel as you do with
   a FTA card.

.. note::

  Currently ca_zap, and dst_test, both are meant for demonstration
  purposes only, they can become full fledged applications if necessary.


Cards that fall in this category
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

At present the cards that fall in this category are the Twinhan and its
clones, these cards are available as VVMER, Tomato, Hercules, Orange and
so on.

CI modules that are supported
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The CI module support is largely dependent upon the firmware on the cards
Some cards do support almost all of the available CI modules. There is
nothing much that can be done in order to make additional CI modules
working with these cards.

Modules that have been tested by this driver at present are

(1) Irdeto 1 and 2 from SCM
(2) Viaccess from SCM
(3) Dragoncam

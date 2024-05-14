.. SPDX-License-Identifier: GPL-2.0-only

========
Overview
========

The Platform Environment Control Interface (PECI) is a communication
interface between Intel processor and management controllers
(e.g. Baseboard Management Controller, BMC).
PECI provides services that allow the management controller to
configure, monitor and debug platform by accessing various registers.
It defines a dedicated command protocol, where the management
controller is acting as a PECI originator and the processor - as
a PECI responder.
PECI can be used in both single processor and multiple-processor based
systems.

NOTE:
Intel PECI specification is not released as a dedicated document,
instead it is a part of External Design Specification (EDS) for given
Intel CPU. External Design Specifications are usually not publicly
available.

PECI Wire
---------

PECI Wire interface uses a single wire for self-clocking and data
transfer. It does not require any additional control lines - the
physical layer is a self-clocked one-wire bus signal that begins each
bit with a driven, rising edge from an idle near zero volts. The
duration of the signal driven high allows to determine whether the bit
value is logic '0' or logic '1'. PECI Wire also includes variable data
rate established with every message.

For PECI Wire, each processor package will utilize unique, fixed
addresses within a defined range and that address should
have a fixed relationship with the processor socket ID - if one of the
processors is removed, it does not affect addresses of remaining
processors.

PECI subsystem internals
------------------------

.. kernel-doc:: include/linux/peci.h
.. kernel-doc:: drivers/peci/internal.h
.. kernel-doc:: drivers/peci/core.c
.. kernel-doc:: drivers/peci/request.c

PECI CPU Driver API
-------------------
.. kernel-doc:: drivers/peci/cpu.c

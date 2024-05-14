.. SPDX-License-Identifier: GPL-2.0

==============================
Trace Buffer Extension (TRBE).
==============================

    :Author:   Anshuman Khandual <anshuman.khandual@arm.com>
    :Date:     November 2020

Hardware Description
--------------------

Trace Buffer Extension (TRBE) is a percpu hardware which captures in system
memory, CPU traces generated from a corresponding percpu tracing unit. This
gets plugged in as a coresight sink device because the corresponding trace
generators (ETE), are plugged in as source device.

The TRBE is not compliant to CoreSight architecture specifications, but is
driven via the CoreSight driver framework to support the ETE (which is
CoreSight compliant) integration.

Sysfs files and directories
---------------------------

The TRBE devices appear on the existing coresight bus alongside the other
coresight devices::

	>$ ls /sys/bus/coresight/devices
	trbe0  trbe1  trbe2 trbe3

The ``trbe<N>`` named TRBEs are associated with a CPU.::

	>$ ls /sys/bus/coresight/devices/trbe0/
        align flag

*Key file items are:-*
   * ``align``: TRBE write pointer alignment
   * ``flag``: TRBE updates memory with access and dirty flags

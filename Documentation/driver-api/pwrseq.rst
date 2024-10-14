.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright 2024 Linaro Ltd.

====================
Power Sequencing API
====================

:Author: Bartosz Golaszewski

Introduction
============

This framework is designed to abstract complex power-up sequences that are
shared between multiple logical devices in the linux kernel.

The intention is to allow consumers to obtain a power sequencing handle
exposed by the power sequence provider and delegate the actual requesting and
control of the underlying resources as well as to allow the provider to
mitigate any potential conflicts between multiple users behind the scenes.

Glossary
--------

The power sequencing API uses a number of terms specific to the subsystem:

Unit

    A unit is a discreet chunk of a power sequence. For instance one unit may
    enable a set of regulators, another may enable a specific GPIO. Units can
    define dependencies in the form of other units that must be enabled before
    it itself can be.

Target

    A target is a set of units (composed of the "final" unit and its
    dependencies) that a consumer selects by its name when requesting a handle
    to the power sequencer. Via the dependency system, multiple targets may
    share the same parts of a power sequence but ignore parts that are
    irrelevant.

Descriptor

    A handle passed by the pwrseq core to every consumer that serves as the
    entry point to the provider layer. It ensures coherence between different
    users and keeps reference counting consistent.

Consumer interface
==================

The consumer API is aimed to be as simple as possible. The driver interested in
getting a descriptor from the power sequencer should call pwrseq_get() and
specify the name of the target it wants to reach in the sequence after calling
pwrseq_power_up(). The descriptor can be released by calling pwrseq_put() and
the consumer can request the powering down of its target with
pwrseq_power_off(). Note that there is no guarantee that pwrseq_power_off()
will have any effect as there may be multiple users of the underlying resources
who may keep them active.

Provider interface
==================

The provider API is admittedly not nearly as straightforward as the one for
consumers but it makes up for it in flexibility.

Each provider can logically split the power-up sequence into descrete chunks
(units) and define their dependencies. They can then expose named targets that
consumers may use as the final point in the sequence that they wish to reach.

To that end the providers fill out a set of configuration structures and
register with the pwrseq subsystem by calling pwrseq_device_register().

Dynamic consumer matching
-------------------------

The main difference between pwrseq and other linux kernel providers is the
mechanism for dynamic matching of consumers and providers. Every power sequence
provider driver must implement the `match()` callback and pass it to the pwrseq
core when registering with the subsystems.

When a client requests a sequencer handle, the core will call this callback for
every registered provider and let it flexibly figure out whether the proposed
client device is indeed its consumer. For example: if the provider binds to the
device-tree node representing a power management unit of a chipset and the
consumer driver controls one of its modules, the provider driver may parse the
relevant regulator supply properties in device tree and see if they lead from
the PMU to the consumer.

API reference
=============

.. kernel-doc:: include/linux/pwrseq/provider.h
   :internal:

.. kernel-doc:: drivers/power/sequencing/core.c
   :export:

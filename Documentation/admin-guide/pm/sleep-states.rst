===================
System Sleep States
===================

::

 Copyright (c) 2017 Intel Corp., Rafael J. Wysocki <rafael.j.wysocki@intel.com>

Sleep states are global low-power states of the entire system in which user
space code cannot be executed and the overall system activity is significantly
reduced.


Sleep States That Can Be Supported
==================================

Depending on its configuration and the capabilities of the platform it runs on,
the Linux kernel can support up to four system sleep states, including
hibernation and up to three variants of system suspend.  The sleep states that
can be supported by the kernel are listed below.

.. _s2idle:

Suspend-to-Idle
---------------

This is a generic, pure software, light-weight variant of system suspend (also
referred to as S2I or S2Idle).  It allows more energy to be saved relative to
runtime idle by freezing user space, suspending the timekeeping and putting all
I/O devices into low-power states (possibly lower-power than available in the
working state), such that the processors can spend time in their deepest idle
states while the system is suspended.

The system is woken up from this state by in-band interrupts, so theoretically
any devices that can cause interrupts to be generated in the working state can
also be set up as wakeup devices for S2Idle.

This state can be used on platforms without support for :ref:`standby <standby>`
or :ref:`suspend-to-RAM <s2ram>`, or it can be used in addition to any of the
deeper system suspend variants to provide reduced resume latency.  It is always
supported if the :c:macro:`CONFIG_SUSPEND` kernel configuration option is set.

.. _standby:

Standby
-------

This state, if supported, offers moderate, but real, energy savings, while
providing a relatively straightforward transition back to the working state.  No
operating state is lost (the system core logic retains power), so the system can
go back to where it left off easily enough.

In addition to freezing user space, suspending the timekeeping and putting all
I/O devices into low-power states, which is done for :ref:`suspend-to-idle
<s2idle>` too, nonboot CPUs are taken offline and all low-level system functions
are suspended during transitions into this state.  For this reason, it should
allow more energy to be saved relative to :ref:`suspend-to-idle <s2idle>`, but
the resume latency will generally be greater than for that state.

The set of devices that can wake up the system from this state usually is
reduced relative to :ref:`suspend-to-idle <s2idle>` and it may be necessary to
rely on the platform for setting up the wakeup functionality as appropriate.

This state is supported if the :c:macro:`CONFIG_SUSPEND` kernel configuration
option is set and the support for it is registered by the platform with the
core system suspend subsystem.  On ACPI-based systems this state is mapped to
the S1 system state defined by ACPI.

.. _s2ram:

Suspend-to-RAM
--------------

This state (also referred to as STR or S2RAM), if supported, offers significant
energy savings as everything in the system is put into a low-power state, except
for memory, which should be placed into the self-refresh mode to retain its
contents.  All of the steps carried out when entering :ref:`standby <standby>`
are also carried out during transitions to S2RAM.  Additional operations may
take place depending on the platform capabilities.  In particular, on ACPI-based
systems the kernel passes control to the platform firmware (BIOS) as the last
step during S2RAM transitions and that usually results in powering down some
more low-level components that are not directly controlled by the kernel.

The state of devices and CPUs is saved and held in memory.  All devices are
suspended and put into low-power states.  In many cases, all peripheral buses
lose power when entering S2RAM, so devices must be able to handle the transition
back to the "on" state.

On ACPI-based systems S2RAM requires some minimal boot-strapping code in the
platform firmware to resume the system from it.  This may be the case on other
platforms too.

The set of devices that can wake up the system from S2RAM usually is reduced
relative to :ref:`suspend-to-idle <s2idle>` and :ref:`standby <standby>` and it
may be necessary to rely on the platform for setting up the wakeup functionality
as appropriate.

S2RAM is supported if the :c:macro:`CONFIG_SUSPEND` kernel configuration option
is set and the support for it is registered by the platform with the core system
suspend subsystem.  On ACPI-based systems it is mapped to the S3 system state
defined by ACPI.

.. _hibernation:

Hibernation
-----------

This state (also referred to as Suspend-to-Disk or STD) offers the greatest
energy savings and can be used even in the absence of low-level platform support
for system suspend.  However, it requires some low-level code for resuming the
system to be present for the underlying CPU architecture.

Hibernation is significantly different from any of the system suspend variants.
It takes three system state changes to put it into hibernation and two system
state changes to resume it.

First, when hibernation is triggered, the kernel stops all system activity and
creates a snapshot image of memory to be written into persistent storage.  Next,
the system goes into a state in which the snapshot image can be saved, the image
is written out and finally the system goes into the target low-power state in
which power is cut from almost all of its hardware components, including memory,
except for a limited set of wakeup devices.

Once the snapshot image has been written out, the system may either enter a
special low-power state (like ACPI S4), or it may simply power down itself.
Powering down means minimum power draw and it allows this mechanism to work on
any system.  However, entering a special low-power state may allow additional
means of system wakeup to be used  (e.g. pressing a key on the keyboard or
opening a laptop lid).

After wakeup, control goes to the platform firmware that runs a boot loader
which boots a fresh instance of the kernel (control may also go directly to
the boot loader, depending on the system configuration, but anyway it causes
a fresh instance of the kernel to be booted).  That new instance of the kernel
(referred to as the ``restore kernel``) looks for a hibernation image in
persistent storage and if one is found, it is loaded into memory.  Next, all
activity in the system is stopped and the restore kernel overwrites itself with
the image contents and jumps into a special trampoline area in the original
kernel stored in the image (referred to as the ``image kernel``), which is where
the special architecture-specific low-level code is needed.  Finally, the
image kernel restores the system to the pre-hibernation state and allows user
space to run again.

Hibernation is supported if the :c:macro:`CONFIG_HIBERNATION` kernel
configuration option is set.  However, this option can only be set if support
for the given CPU architecture includes the low-level code for system resume.


Basic ``sysfs`` Interfaces for System Suspend and Hibernation
=============================================================

The following files located in the :file:`/sys/power/` directory can be used by
user space for sleep states control.

``state``
	This file contains a list of strings representing sleep states supported
	by the kernel.  Writing one of these strings into it causes the kernel
	to start a transition of the system into the sleep state represented by
	that string.

	In particular, the strings "disk", "freeze" and "standby" represent the
	:ref:`hibernation <hibernation>`, :ref:`suspend-to-idle <s2idle>` and
	:ref:`standby <standby>` sleep states, respectively.  The string "mem"
	is interpreted in accordance with the contents of the ``mem_sleep`` file
	described below.

	If the kernel does not support any system sleep states, this file is
	not present.

``mem_sleep``
	This file contains a list of strings representing supported system
	suspend	variants and allows user space to select the variant to be
	associated with the "mem" string in the ``state`` file described above.

	The strings that may be present in this file are "s2idle", "shallow"
	and "deep".  The string "s2idle" always represents :ref:`suspend-to-idle
	<s2idle>` and, by convention, "shallow" and "deep" represent
	:ref:`standby <standby>` and :ref:`suspend-to-RAM <s2ram>`,
	respectively.

	Writing one of the listed strings into this file causes the system
	suspend variant represented by it to be associated with the "mem" string
	in the ``state`` file.  The string representing the suspend variant
	currently associated with the "mem" string in the ``state`` file
	is listed in square brackets.

	If the kernel does not support system suspend, this file is not present.

``disk``
	This file contains a list of strings representing different operations
	that can be carried out after the hibernation image has been saved.  The
	possible options are as follows:

	``platform``
		Put the system into a special low-power state (e.g. ACPI S4) to
		make additional wakeup options available and possibly allow the
		platform firmware to take a simplified initialization path after
		wakeup.

	``shutdown``
		Power off the system.

	``reboot``
		Reboot the system (useful for diagnostics mostly).

	``suspend``
		Hybrid system suspend.  Put the system into the suspend sleep
		state selected through the ``mem_sleep`` file described above.
		If the system is successfully woken up from that state, discard
		the hibernation image and continue.  Otherwise, use the image
		to restore the previous state of the system.

	``test_resume``
		Diagnostic operation.  Load the image as though the system had
		just woken up from hibernation and the currently running kernel
		instance was a restore kernel and follow up with full system
		resume.

	Writing one of the listed strings into this file causes the option
	represented by it to be selected.

	The currently selected option is shown in square brackets which means
	that the operation represented by it will be carried out after creating
	and saving the image next time hibernation is triggered by writing
	``disk`` to :file:`/sys/power/state`.

	If the kernel does not support hibernation, this file is not present.

According to the above, there are two ways to make the system go into the
:ref:`suspend-to-idle <s2idle>` state.  The first one is to write "freeze"
directly to :file:`/sys/power/state`.  The second one is to write "s2idle" to
:file:`/sys/power/mem_sleep` and then to write "mem" to
:file:`/sys/power/state`.  Likewise, there are two ways to make the system go
into the :ref:`standby <standby>` state (the strings to write to the control
files in that case are "standby" or "shallow" and "mem", respectively) if that
state is supported by the platform.  However, there is only one way to make the
system go into the :ref:`suspend-to-RAM <s2ram>` state (write "deep" into
:file:`/sys/power/mem_sleep` and "mem" into :file:`/sys/power/state`).

The default suspend variant (ie. the one to be used without writing anything
into :file:`/sys/power/mem_sleep`) is either "deep" (on the majority of systems
supporting :ref:`suspend-to-RAM <s2ram>`) or "s2idle", but it can be overridden
by the value of the "mem_sleep_default" parameter in the kernel command line.
On some ACPI-based systems, depending on the information in the ACPI tables, the
default may be "s2idle" even if :ref:`suspend-to-RAM <s2ram>` is supported.

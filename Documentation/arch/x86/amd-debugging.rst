.. SPDX-License-Identifier: GPL-2.0

Debugging AMD Zen systems
+++++++++++++++++++++++++

Introduction
============

This document describes techniques that are useful for debugging issues with
AMD Zen systems.  It is intended for use by developers and technical users
to help identify and resolve issues.

S3 vs s2idle
============

On AMD systems, it's not possible to simultaneously support suspend-to-RAM (S3)
and suspend-to-idle (s2idle).  To confirm which mode your system supports you
can look at ``cat /sys/power/mem_sleep``.  If it shows ``s2idle [deep]`` then
*S3* is supported.  If it shows ``[s2idle]`` then *s2idle* is
supported.

On systems that support *S3*, the firmware will be utilized to put all hardware into
the appropriate low power state.

On systems that support *s2idle*, the kernel will be responsible for transitioning devices
into the appropriate low power state. When all devices are in the appropriate low
power state, the hardware will transition into a hardware sleep state.

After a suspend cycle you can tell how much time was spent in a hardware sleep
state by looking at ``cat /sys/power/suspend_stats/last_hw_sleep``.

This flowchart explains how the AMD s2idle suspend flow works.

.. kernel-figure:: suspend.svg

This flowchart explains how the amd s2idle resume flow works.

.. kernel-figure:: resume.svg

s2idle debugging tool
=====================

As there are a lot of places that problems can occur, a debugging tool has been
created at
`amd-debug-tools <https://git.kernel.org/pub/scm/linux/kernel/git/superm1/amd-debug-tools.git/about/>`_
that can help test for common problems and offer suggestions.

If you have an s2idle issue, it's best to start with this and follow instructions
from its findings.  If you continue to have an issue, raise a bug with the
report generated from this script to
`drm/amd gitlab <https://gitlab.freedesktop.org/drm/amd/-/issues/new?issuable_template=s2idle_BUG_TEMPLATE>`_.

Spurious s2idle wakeups from an IRQ
===================================

Spurious wakeups will generally have an IRQ set to ``/sys/power/pm_wakeup_irq``.
This can be matched to ``/proc/interrupts`` to determine what device woke the system.

If this isn't enough to debug the problem, then the following sysfs files
can be set to add more verbosity to the wakeup process: ::

  # echo 1 | sudo tee /sys/power/pm_debug_messages
  # echo 1 | sudo tee /sys/power/pm_print_times

After making those changes, the kernel will display messages that can
be traced back to kernel s2idle loop code as well as display any active
GPIO sources while waking up.

If the wakeup is caused by the ACPI SCI, additional ACPI debugging may be
needed.  These commands can enable additional trace data: ::

  # echo enable | sudo tee /sys/module/acpi/parameters/trace_state
  # echo 1 | sudo tee /sys/module/acpi/parameters/aml_debug_output
  # echo 0x0800000f | sudo tee /sys/module/acpi/parameters/debug_level
  # echo 0xffff0000 | sudo tee /sys/module/acpi/parameters/debug_layer

Spurious s2idle wakeups from a GPIO
===================================

If a GPIO is active when waking up the system ideally you would look at the
schematic to determine what device it is associated with. If the schematic
is not available, another tactic is to look at the ACPI _EVT() entry
to determine what device is notified when that GPIO is active.

For a hypothetical example, say that GPIO 59 woke up the system.  You can
look at the SSDT to determine what device is notified when GPIO 59 is active.

First convert the GPIO number into hex. ::

  $ python3 -c "print(hex(59))"
  0x3b

Next determine which ACPI table has the ``_EVT`` entry. For example: ::

  $ sudo grep EVT /sys/firmware/acpi/tables/SSDT*
  grep: /sys/firmware/acpi/tables/SSDT27: binary file matches

Decode this table::

  $ sudo cp /sys/firmware/acpi/tables/SSDT27 .
  $ sudo iasl -d SSDT27

Then look at the table and find the matching entry for GPIO 0x3b. ::

  Case (0x3B)
  {
      M000 (0x393B)
      M460 ("    Notify (\\_SB.PCI0.GP17.XHC1, 0x02)\n", Zero, Zero, Zero, Zero, Zero, Zero)
      Notify (\_SB.PCI0.GP17.XHC1, 0x02) // Device Wake
  }

You can see in this case that the device ``\_SB.PCI0.GP17.XHC1`` is notified
when GPIO 59 is active. It's obvious this is an XHCI controller, but to go a
step further you can figure out which XHCI controller it is by matching it to
ACPI.::

  $ grep "PCI0.GP17.XHC1" /sys/bus/acpi/devices/*/path
  /sys/bus/acpi/devices/device:2d/path:\_SB_.PCI0.GP17.XHC1
  /sys/bus/acpi/devices/device:2e/path:\_SB_.PCI0.GP17.XHC1.RHUB
  /sys/bus/acpi/devices/device:2f/path:\_SB_.PCI0.GP17.XHC1.RHUB.PRT1
  /sys/bus/acpi/devices/device:30/path:\_SB_.PCI0.GP17.XHC1.RHUB.PRT1.CAM0
  /sys/bus/acpi/devices/device:31/path:\_SB_.PCI0.GP17.XHC1.RHUB.PRT1.CAM1
  /sys/bus/acpi/devices/device:32/path:\_SB_.PCI0.GP17.XHC1.RHUB.PRT2
  /sys/bus/acpi/devices/LNXPOWER:0d/path:\_SB_.PCI0.GP17.XHC1.PWRS

Here you can see it matches to ``device:2d``. Look at the ``physical_node``
to determine what PCI device that actually is. ::

  $ ls -l /sys/bus/acpi/devices/device:2d/physical_node
  lrwxrwxrwx 1 root root 0 Feb 12 13:22 /sys/bus/acpi/devices/device:2d/physical_node -> ../../../../../pci0000:00/0000:00:08.1/0000:c2:00.4

So there you have it: the PCI device associated with this GPIO wakeup was ``0000:c2:00.4``.

The ``amd_s2idle.py`` script will capture most of these artifacts for you.

s2idle PM debug messages
========================

During the s2idle flow on AMD systems, the ACPI LPS0 driver is responsible
to check all uPEP constraints.  Failing uPEP constraints does not prevent
s0i3 entry.  This means that if some constraints are not met, it is possible
the kernel may attempt to enter s2idle even if there are some known issues.

To activate PM debugging, either specify ``pm_debug_messagess`` kernel
command-line option at boot or write to ``/sys/power/pm_debug_messages``.
Unmet constraints will be displayed in the kernel log and can be
viewed by logging tools that process kernel ring buffer like ``dmesg`` or
``journalctl``."

If the system freezes on entry/exit before these messages are flushed, a
useful debugging tactic is to unbind the ``amd_pmc`` driver to prevent
notification to the platform to start s0i3 entry.  This will stop the
system from freezing on entry or exit and let you view all the failed
constraints. ::

  cd /sys/bus/platform/drivers/amd_pmc
  ls | grep AMD | sudo tee unbind

After doing this, run the suspend cycle and look specifically for errors around: ::

  ACPI: LPI: Constraint not met; min power state:%s current power state:%s

Historical examples of s2idle issues
====================================

To help understand the types of issues that can occur and how to debug them,
here are some historical examples of s2idle issues that have been resolved.

Core offlining
--------------
An end user had reported that taking a core offline would prevent the system
from properly entering s0i3.  This was debugged using internal AMD tools
to capture and display a stream of metrics from the hardware showing what changed
when a core was offlined.  It was determined that the hardware didn't get
notification the offline cores were in the deepest state, and so it prevented
CPU from going into the deepest state. The issue was debugged to a missing
command to put cores into C3 upon offline.

`commit d6b88ce2eb9d2 ("ACPI: processor idle: Allow playing dead in C3 state") <https://git.kernel.org/torvalds/c/d6b88ce2eb9d2>`_

Corruption after resume
-----------------------
A big problem that occurred with Rembrandt was that there was graphical
corruption after resume.  This happened because of a misalignment of PSP
and driver responsibility.  The PSP will save and restore DMCUB, but the
driver assumed it needed to reset DMCUB on resume.
This actually was a misalignment for earlier silicon as well, but was not
observed.

`commit 79d6b9351f086 ("drm/amd/display: Don't reinitialize DMCUB on s0ix resume") <https://git.kernel.org/torvalds/c/79d6b9351f086>`_

Back to Back suspends fail
--------------------------
When using a wakeup source that triggers the IRQ to wakeup, a bug in the
pinctrl-amd driver may capture the wrong state of the IRQ and prevent the
system going back to sleep properly.

`commit b8c824a869f22 ("pinctrl: amd: Don't save/restore interrupt status and wake status bits") <https://git.kernel.org/torvalds/c/b8c824a869f22>`_

Spurious timer based wakeup after 5 minutes
-------------------------------------------
The HPET was being used to program the wakeup source for the system, however
this was causing a spurious wakeup after 5 minutes.  The correct alarm to use
was the ACPI alarm.

`commit 3d762e21d5637 ("rtc: cmos: Use ACPI alarm for non-Intel x86 systems too") <https://git.kernel.org/torvalds/c/3d762e21d5637>`_

Disk disappears after resume
----------------------------
After resuming from s2idle, the NVME disk would disappear.  This was due to the
BIOS not specifying the _DSD StorageD3Enable property.  This caused the NVME
driver not to put the disk into the expected state at suspend and to fail
on resume.

`commit e79a10652bbd3 ("ACPI: x86: Force StorageD3Enable on more products") <https://git.kernel.org/torvalds/c/e79a10652bbd3>`_

Spurious IRQ1
-------------
A number of Renoir, Lucienne, Cezanne, & Barcelo platforms have a
platform firmware bug where IRQ1 is triggered during s0i3 resume.

This was fixed in the platform firmware, but a number of systems didn't
receive any more platform firmware updates.

`commit 8e60615e89321 ("platform/x86/amd: pmc: Disable IRQ1 wakeup for RN/CZN") <https://git.kernel.org/torvalds/c/8e60615e89321>`_

Hardware timeout
----------------
The hardware performs many actions besides accepting the values from
amd-pmc driver.  As the communication path with the hardware is a mailbox,
it's possible that it might not respond quickly enough.
This issue manifested as a failure to suspend: ::

  PM: dpm_run_callback(): acpi_subsys_suspend_noirq+0x0/0x50 returns -110
  amd_pmc AMDI0005:00: PM: failed to suspend noirq: error -110

The timing problem was identified by comparing the values of the idle mask.

`commit 3c3c8e88c8712 ("platform/x86: amd-pmc: Increase the response register timeout") <https://git.kernel.org/torvalds/c/3c3c8e88c8712>`_

Failed to reach hardware sleep state with panel on
--------------------------------------------------
On some Strix systems certain panels were observed to block the system from
entering a hardware sleep state if the internal panel was on during the sequence.

Even though the panel got turned off during suspend it exposed a timing problem
where an interrupt caused the display hardware to wake up and block low power
state entry.

`commit 40b8c14936bd2 ("drm/amd/display: Disable unneeded hpd interrupts during dm_init") <https://git.kernel.org/torvalds/c/40b8c14936bd2>`_

Runtime power consumption issues
================================

Runtime power consumption is influenced by many factors, including but not
limited to the configuration of the PCIe Active State Power Management (ASPM),
the display brightness, the EPP policy of the CPU, and the power management
of the devices.

ASPM
----
For the best runtime power consumption, ASPM should be programmed as intended
by the BIOS from the hardware vendor.  To accomplish this the Linux kernel
should be compiled with ``CONFIG_PCIEASPM_DEFAULT`` set to ``y`` and the
sysfs file ``/sys/module/pcie_aspm/parameters/policy`` should not be modified.

Most notably, if L1.2 is not configured properly for any devices, the SoC
will not be able to enter the deepest idle state.

EPP Policy
----------
The ``energy_performance_preference`` sysfs file can be used to set a bias
of efficiency or performance for a CPU.  This has a direct relationship on
the battery life when more heavily biased towards performance.


BIOS debug messages
===================

Most OEM machines don't have a serial UART for outputting kernel or BIOS
debug messages. However BIOS debug messages are useful for understanding
both BIOS bugs and bugs with the Linux kernel drivers that call BIOS AML.

As the BIOS on most OEM AMD systems are based off an AMD reference BIOS,
the infrastructure used for exporting debugging messages is often the same
as AMD reference BIOS.

Manually Parsing
----------------
There is generally an ACPI method ``\M460`` that different paths of the AML
will call to emit a message to the BIOS serial log. This method takes
7 arguments, with the first being a string and the rest being optional
integers::

  Method (M460, 7, Serialized)

Here is an example of a string that BIOS AML may call out using ``\M460``::

  M460 ("  OEM-ASL-PCIe Address (0x%X)._REG (%d %d)  PCSA = %d\n", DADR, Arg0, Arg1, PCSA, Zero, Zero)

Normally when executed, the ``\M460`` method would populate the additional
arguments into the string.  In order to get these messages from the Linux
kernel a hook has been added into ACPICA that can capture the *arguments*
sent to ``\M460`` and print them to the kernel ring buffer.
For example the following message could be emitted into kernel ring buffer::

  extrace-0174 ex_trace_args         :  "  OEM-ASL-PCIe Address (0x%X)._REG (%d %d)  PCSA = %d\n", ec106000, 2, 1, 1, 0, 0

In order to get these messages, you need to compile with ``CONFIG_ACPI_DEBUG``
and then turn on the following ACPICA tracing parameters.
This can be done either on the kernel command line or at runtime:

* ``acpi.trace_method_name=\M460``
* ``acpi.trace_state=method``

NOTE: These can be very noisy at bootup. If you turn these parameters on
the kernel command, please also consider turning up ``CONFIG_LOG_BUF_SHIFT``
to a larger size such as 17 to avoid losing early boot messages.

Tool assisted Parsing
---------------------
As mentioned above, parsing by hand can be tedious, especially with a lot of
messages.  To help with this, a tool has been created at
`amd-debug-tools <https://git.kernel.org/pub/scm/linux/kernel/git/superm1/amd-debug-tools.git/about/>`_
to help parse the messages.

Random reboot issues
====================

When a random reboot occurs, the high-level reason for the reboot is stored
in a register that will persist onto the next boot.

There are 6 classes of reasons for the reboot:
 * Software induced
 * Power state transition
 * Pin induced
 * Hardware induced
 * Remote reset
 * Internal CPU event

.. csv-table::
   :header: "Bit", "Type", "Reason"
   :align: left

   "0",  "Pin",      "thermal pin BP_THERMTRIP_L was tripped"
   "1",  "Pin",      "power button was pressed for 4 seconds"
   "2",  "Pin",      "shutdown pin was tripped"
   "4",  "Remote",   "remote ASF power off command was received"
   "9",  "Internal", "internal CPU thermal limit was tripped"
   "16", "Pin",      "system reset pin BP_SYS_RST_L was tripped"
   "17", "Software", "software issued PCI reset"
   "18", "Software", "software wrote 0x4 to reset control register 0xCF9"
   "19", "Software", "software wrote 0x6 to reset control register 0xCF9"
   "20", "Software", "software wrote 0xE to reset control register 0xCF9"
   "21", "ACPI-state", "ACPI power state transition occurred"
   "22", "Pin",      "keyboard reset pin KB_RST_L was tripped"
   "23", "Internal", "internal CPU shutdown event occurred"
   "24", "Hardware", "system failed to boot before failed boot timer expired"
   "25", "Hardware", "hardware watchdog timer expired"
   "26", "Remote",   "remote ASF reset command was received"
   "27", "Internal", "an uncorrected error caused a data fabric sync flood event"
   "29", "Internal", "FCH and MP1 failed warm reset handshake"
   "30", "Internal", "a parity error occurred"
   "31", "Internal", "a software sync flood event occurred"

This information is read by the kernel at bootup and printed into
the syslog. When a random reboot occurs this message can be helpful
to determine the next component to debug.

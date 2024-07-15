.. SPDX-License-Identifier: GPL-2.0

Clocks and Timers
=================

arm64
-----
On arm64, Hyper-V virtualizes the ARMv8 architectural system counter
and timer. Guest VMs use this virtualized hardware as the Linux
clocksource and clockevents via the standard arm_arch_timer.c
driver, just as they would on bare metal. Linux vDSO support for the
architectural system counter is functional in guest VMs on Hyper-V.
While Hyper-V also provides a synthetic system clock and four synthetic
per-CPU timers as described in the TLFS, they are not used by the
Linux kernel in a Hyper-V guest on arm64.  However, older versions
of Hyper-V for arm64 only partially virtualize the ARMv8
architectural timer, such that the timer does not generate
interrupts in the VM. Because of this limitation, running current
Linux kernel versions on these older Hyper-V versions requires an
out-of-tree patch to use the Hyper-V synthetic clocks/timers instead.

x86/x64
-------
On x86/x64, Hyper-V provides guest VMs with a synthetic system clock
and four synthetic per-CPU timers as described in the TLFS. Hyper-V
also provides access to the virtualized TSC via the RDTSC and
related instructions. These TSC instructions do not trap to
the hypervisor and so provide excellent performance in a VM.
Hyper-V performs TSC calibration, and provides the TSC frequency
to the guest VM via a synthetic MSR.  Hyper-V initialization code
in Linux reads this MSR to get the frequency, so it skips TSC
calibration and sets tsc_reliable. Hyper-V provides virtualized
versions of the PIT (in Hyper-V  Generation 1 VMs only), local
APIC timer, and RTC. Hyper-V does not provide a virtualized HPET in
guest VMs.

The Hyper-V synthetic system clock can be read via a synthetic MSR,
but this access traps to the hypervisor. As a faster alternative,
the guest can configure a memory page to be shared between the guest
and the hypervisor.  Hyper-V populates this memory page with a
64-bit scale value and offset value. To read the synthetic clock
value, the guest reads the TSC and then applies the scale and offset
as described in the Hyper-V TLFS. The resulting value advances
at a constant 10 MHz frequency. In the case of a live migration
to a host with a different TSC frequency, Hyper-V adjusts the
scale and offset values in the shared page so that the 10 MHz
frequency is maintained.

Starting with Windows Server 2022 Hyper-V, Hyper-V uses hardware
support for TSC frequency scaling to enable live migration of VMs
across Hyper-V hosts where the TSC frequency may be different.
When a Linux guest detects that this Hyper-V functionality is
available, it prefers to use Linux's standard TSC-based clocksource.
Otherwise, it uses the clocksource for the Hyper-V synthetic system
clock implemented via the shared page (identified as
"hyperv_clocksource_tsc_page").

The Hyper-V synthetic system clock is available to user space via
vDSO, and gettimeofday() and related system calls can execute
entirely in user space.  The vDSO is implemented by mapping the
shared page with scale and offset values into user space.  User
space code performs the same algorithm of reading the TSC and
applying the scale and offset to get the constant 10 MHz clock.

Linux clockevents are based on Hyper-V synthetic timer 0 (stimer0).
While Hyper-V offers 4 synthetic timers for each CPU, Linux only uses
timer 0. In older versions of Hyper-V, an interrupt from stimer0
results in a VMBus control message that is demultiplexed by
vmbus_isr() as described in the Documentation/virt/hyperv/vmbus.rst
documentation. In newer versions of Hyper-V, stimer0 interrupts can
be mapped to an architectural interrupt, which is referred to as
"Direct Mode". Linux prefers to use Direct Mode when available. Since
x86/x64 doesn't support per-CPU interrupts, Direct Mode statically
allocates an x86 interrupt vector (HYPERV_STIMER0_VECTOR) across all CPUs
and explicitly codes it to call the stimer0 interrupt handler. Hence
interrupts from stimer0 are recorded on the "HVS" line in /proc/interrupts
rather than being associated with a Linux IRQ. Clockevents based on the
virtualized PIT and local APIC timer also work, but Hyper-V stimer0
is preferred.

The driver for the Hyper-V synthetic system clock and timers is
drivers/clocksource/hyperv_timer.c.

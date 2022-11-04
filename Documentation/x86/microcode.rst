.. SPDX-License-Identifier: GPL-2.0

==========================
The Linux Microcode Loader
==========================

:Authors: - Fenghua Yu <fenghua.yu@intel.com>
          - Borislav Petkov <bp@suse.de>
	  - Ashok Raj <ashok.raj@intel.com>

The kernel has a x86 microcode loading facility which is supposed to
provide microcode loading methods in the OS. Potential use cases are
updating the microcode on platforms beyond the OEM End-Of-Life support,
and updating the microcode on long-running systems without rebooting.

The loader supports three loading methods:

Early load microcode
====================

The kernel can update microcode very early during boot. Loading
microcode early can fix CPU issues before they are observed during
kernel boot time.

The microcode is stored in an initrd file. During boot, it is read from
it and loaded into the CPU cores.

The format of the combined initrd image is microcode in (uncompressed)
cpio format followed by the (possibly compressed) initrd image. The
loader parses the combined initrd image during boot.

The microcode files in cpio name space are:

on Intel:
  kernel/x86/microcode/GenuineIntel.bin
on AMD  :
  kernel/x86/microcode/AuthenticAMD.bin

During BSP (BootStrapping Processor) boot (pre-SMP), the kernel
scans the microcode file in the initrd. If microcode matching the
CPU is found, it will be applied in the BSP and later on in all APs
(Application Processors).

The loader also saves the matching microcode for the CPU in memory.
Thus, the cached microcode patch is applied when CPUs resume from a
sleep state.

Here's a crude example how to prepare an initrd with microcode (this is
normally done automatically by the distribution, when recreating the
initrd, so you don't really have to do it yourself. It is documented
here for future reference only).
::

  #!/bin/bash

  if [ -z "$1" ]; then
      echo "You need to supply an initrd file"
      exit 1
  fi

  INITRD="$1"

  DSTDIR=kernel/x86/microcode
  TMPDIR=/tmp/initrd

  rm -rf $TMPDIR

  mkdir $TMPDIR
  cd $TMPDIR
  mkdir -p $DSTDIR

  if [ -d /lib/firmware/amd-ucode ]; then
          cat /lib/firmware/amd-ucode/microcode_amd*.bin > $DSTDIR/AuthenticAMD.bin
  fi

  if [ -d /lib/firmware/intel-ucode ]; then
          cat /lib/firmware/intel-ucode/* > $DSTDIR/GenuineIntel.bin
  fi

  find . | cpio -o -H newc >../ucode.cpio
  cd ..
  mv $INITRD $INITRD.orig
  cat ucode.cpio $INITRD.orig > $INITRD

  rm -rf $TMPDIR


The system needs to have the microcode packages installed into
/lib/firmware or you need to fixup the paths above if yours are
somewhere else and/or you've downloaded them directly from the processor
vendor's site.

Late loading
============

You simply install the microcode packages your distro supplies and
run::

  # echo 1 > /sys/devices/system/cpu/microcode/reload

as root.

The loading mechanism looks for microcode blobs in
/lib/firmware/{intel-ucode,amd-ucode}. The default distro installation
packages already put them there.

Since kernel 5.19, late loading is not enabled by default.

The /dev/cpu/microcode method has been removed in 5.19.

Why is late loading dangerous?
==============================

Synchronizing all CPUs
----------------------

The microcode engine which receives the microcode update is shared
between the two logical threads in a SMT system. Therefore, when
the update is executed on one SMT thread of the core, the sibling
"automatically" gets the update.

Since the microcode can "simulate" MSRs too, while the microcode update
is in progress, those simulated MSRs transiently cease to exist. This
can result in unpredictable results if the SMT sibling thread happens to
be in the middle of an access to such an MSR. The usual observation is
that such MSR accesses cause #GPs to be raised to signal that former are
not present.

The disappearing MSRs are just one common issue which is being observed.
Any other instruction that's being patched and gets concurrently
executed by the other SMT sibling, can also result in similar,
unpredictable behavior.

To eliminate this case, a stop_machine()-based CPU synchronization was
introduced as a way to guarantee that all logical CPUs will not execute
any code but just wait in a spin loop, polling an atomic variable.

While this took care of device or external interrupts, IPIs including
LVT ones, such as CMCI etc, it cannot address other special interrupts
that can't be shut off. Those are Machine Check (#MC), System Management
(#SMI) and Non-Maskable interrupts (#NMI).

Machine Checks
--------------

Machine Checks (#MC) are non-maskable. There are two kinds of MCEs.
Fatal un-recoverable MCEs and recoverable MCEs. While un-recoverable
errors are fatal, recoverable errors can also happen in kernel context
are also treated as fatal by the kernel.

On certain Intel machines, MCEs are also broadcast to all threads in a
system. If one thread is in the middle of executing WRMSR, a MCE will be
taken at the end of the flow. Either way, they will wait for the thread
performing the wrmsr(0x79) to rendezvous in the MCE handler and shutdown
eventually if any of the threads in the system fail to check in to the
MCE rendezvous.

To be paranoid and get predictable behavior, the OS can choose to set
MCG_STATUS.MCIP. Since MCEs can be at most one in a system, if an
MCE was signaled, the above condition will promote to a system reset
automatically. OS can turn off MCIP at the end of the update for that
core.

System Management Interrupt
---------------------------

SMIs are also broadcast to all CPUs in the platform. Microcode update
requests exclusive access to the core before writing to MSR 0x79. So if
it does happen such that, one thread is in WRMSR flow, and the 2nd got
an SMI, that thread will be stopped in the first instruction in the SMI
handler.

Since the secondary thread is stopped in the first instruction in SMI,
there is very little chance that it would be in the middle of executing
an instruction being patched. Plus OS has no way to stop SMIs from
happening.

Non-Maskable Interrupts
-----------------------

When thread0 of a core is doing the microcode update, if thread1 is
pulled into NMI, that can cause unpredictable behavior due to the
reasons above.

OS can choose a variety of methods to avoid running into this situation.


Is the microcode suitable for late loading?
-------------------------------------------

Late loading is done when the system is fully operational and running
real workloads. Late loading behavior depends on what the base patch on
the CPU is before upgrading to the new patch.

This is true for Intel CPUs.

Consider, for example, a CPU has patch level 1 and the update is to
patch level 3.

Between patch1 and patch3, patch2 might have deprecated a software-visible
feature.

This is unacceptable if software is even potentially using that feature.
For instance, say MSR_X is no longer available after an update,
accessing that MSR will cause a #GP fault.

Basically there is no way to declare a new microcode update suitable
for late-loading. This is another one of the problems that caused late
loading to be not enabled by default.

Builtin microcode
=================

The loader supports also loading of a builtin microcode supplied through
the regular builtin firmware method CONFIG_EXTRA_FIRMWARE. Only 64-bit is
currently supported.

Here's an example::

  CONFIG_EXTRA_FIRMWARE="intel-ucode/06-3a-09 amd-ucode/microcode_amd_fam15h.bin"
  CONFIG_EXTRA_FIRMWARE_DIR="/lib/firmware"

This basically means, you have the following tree structure locally::

  /lib/firmware/
  |-- amd-ucode
  ...
  |   |-- microcode_amd_fam15h.bin
  ...
  |-- intel-ucode
  ...
  |   |-- 06-3a-09
  ...

so that the build system can find those files and integrate them into
the final kernel image. The early loader finds them and applies them.

Needless to say, this method is not the most flexible one because it
requires rebuilding the kernel each time updated microcode from the CPU
vendor is available.

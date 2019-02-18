Microarchitectural Data Sampling (MDS) mitigation
=================================================

.. _mds:

Overview
--------

Microarchitectural Data Sampling (MDS) is a family of side channel attacks
on internal buffers in Intel CPUs. The variants are:

 - Microarchitectural Store Buffer Data Sampling (MSBDS) (CVE-2018-12126)
 - Microarchitectural Fill Buffer Data Sampling (MFBDS) (CVE-2018-12130)
 - Microarchitectural Load Port Data Sampling (MLPDS) (CVE-2018-12127)

MSBDS leaks Store Buffer Entries which can be speculatively forwarded to a
dependent load (store-to-load forwarding) as an optimization. The forward
can also happen to a faulting or assisting load operation for a different
memory address, which can be exploited under certain conditions. Store
buffers are partitioned between Hyper-Threads so cross thread forwarding is
not possible. But if a thread enters or exits a sleep state the store
buffer is repartitioned which can expose data from one thread to the other.

MFBDS leaks Fill Buffer Entries. Fill buffers are used internally to manage
L1 miss situations and to hold data which is returned or sent in response
to a memory or I/O operation. Fill buffers can forward data to a load
operation and also write data to the cache. When the fill buffer is
deallocated it can retain the stale data of the preceding operations which
can then be forwarded to a faulting or assisting load operation, which can
be exploited under certain conditions. Fill buffers are shared between
Hyper-Threads so cross thread leakage is possible.

MLPDS leaks Load Port Data. Load ports are used to perform load operations
from memory or I/O. The received data is then forwarded to the register
file or a subsequent operation. In some implementations the Load Port can
contain stale data from a previous operation which can be forwarded to
faulting or assisting loads under certain conditions, which again can be
exploited eventually. Load ports are shared between Hyper-Threads so cross
thread leakage is possible.


Exposure assumptions
--------------------

It is assumed that attack code resides in user space or in a guest with one
exception. The rationale behind this assumption is that the code construct
needed for exploiting MDS requires:

 - to control the load to trigger a fault or assist

 - to have a disclosure gadget which exposes the speculatively accessed
   data for consumption through a side channel.

 - to control the pointer through which the disclosure gadget exposes the
   data

The existence of such a construct in the kernel cannot be excluded with
100% certainty, but the complexity involved makes it extremly unlikely.

There is one exception, which is untrusted BPF. The functionality of
untrusted BPF is limited, but it needs to be thoroughly investigated
whether it can be used to create such a construct.


Mitigation strategy
-------------------

All variants have the same mitigation strategy at least for the single CPU
thread case (SMT off): Force the CPU to clear the affected buffers.

This is achieved by using the otherwise unused and obsolete VERW
instruction in combination with a microcode update. The microcode clears
the affected CPU buffers when the VERW instruction is executed.

For virtualization there are two ways to achieve CPU buffer
clearing. Either the modified VERW instruction or via the L1D Flush
command. The latter is issued when L1TF mitigation is enabled so the extra
VERW can be avoided. If the CPU is not affected by L1TF then VERW needs to
be issued.

If the VERW instruction with the supplied segment selector argument is
executed on a CPU without the microcode update there is no side effect
other than a small number of pointlessly wasted CPU cycles.

This does not protect against cross Hyper-Thread attacks except for MSBDS
which is only exploitable cross Hyper-thread when one of the Hyper-Threads
enters a C-state.

The kernel provides a function to invoke the buffer clearing:

    mds_clear_cpu_buffers()

The mitigation is invoked on kernel/userspace, hypervisor/guest and C-state
(idle) transitions.

According to current knowledge additional mitigations inside the kernel
itself are not required because the necessary gadgets to expose the leaked
data cannot be controlled in a way which allows exploitation from malicious
user space or VM guests.

Mitigation points
-----------------

1. Return to user space
^^^^^^^^^^^^^^^^^^^^^^^

   When transitioning from kernel to user space the CPU buffers are flushed
   on affected CPUs when the mitigation is not disabled on the kernel
   command line. The migitation is enabled through the static key
   mds_user_clear.

   The mitigation is invoked in prepare_exit_to_usermode() which covers
   most of the kernel to user space transitions. There are a few exceptions
   which are not invoking prepare_exit_to_usermode() on return to user
   space. These exceptions use the paranoid exit code.

   - Non Maskable Interrupt (NMI):

     Access to sensible data like keys, credentials in the NMI context is
     mostly theoretical: The CPU can do prefetching or execute a
     misspeculated code path and thereby fetching data which might end up
     leaking through a buffer.

     But for mounting other attacks the kernel stack address of the task is
     already valuable information. So in full mitigation mode, the NMI is
     mitigated on the return from do_nmi() to provide almost complete
     coverage.

   - Double fault (#DF):

     A double fault is usually fatal, but the ESPFIX workaround, which can
     be triggered from user space through modify_ldt(2) is a recoverable
     double fault. #DF uses the paranoid exit path, so explicit mitigation
     in the double fault handler is required.

   - Machine Check Exception (#MC):

     Another corner case is a #MC which hits between the CPU buffer clear
     invocation and the actual return to user. As this still is in kernel
     space it takes the paranoid exit path which does not clear the CPU
     buffers. So the #MC handler repopulates the buffers to some
     extent. Machine checks are not reliably controllable and the window is
     extremly small so mitigation would just tick a checkbox that this
     theoretical corner case is covered. To keep the amount of special
     cases small, ignore #MC.

   - Debug Exception (#DB):

     This takes the paranoid exit path only when the INT1 breakpoint is in
     kernel space. #DB on a user space address takes the regular exit path,
     so no extra mitigation required.

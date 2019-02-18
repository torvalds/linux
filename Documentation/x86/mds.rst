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

=========================================
Processor MMIO Stale Data Vulnerabilities
=========================================

Processor MMIO Stale Data Vulnerabilities are a class of memory-mapped I/O
(MMIO) vulnerabilities that can expose data. The sequences of operations for
exposing data range from simple to very complex. Because most of the
vulnerabilities require the attacker to have access to MMIO, many environments
are not affected. System environments using virtualization where MMIO access is
provided to untrusted guests may need mitigation. These vulnerabilities are
not transient execution attacks. However, these vulnerabilities may propagate
stale data into core fill buffers where the data can subsequently be inferred
by an unmitigated transient execution attack. Mitigation for these
vulnerabilities includes a combination of microcode update and software
changes, depending on the platform and usage model. Some of these mitigations
are similar to those used to mitigate Microarchitectural Data Sampling (MDS) or
those used to mitigate Special Register Buffer Data Sampling (SRBDS).

Data Propagators
================
Propagators are operations that result in stale data being copied or moved from
one microarchitectural buffer or register to another. Processor MMIO Stale Data
Vulnerabilities are operations that may result in stale data being directly
read into an architectural, software-visible state or sampled from a buffer or
register.

Fill Buffer Stale Data Propagator (FBSDP)
-----------------------------------------
Stale data may propagate from fill buffers (FB) into the non-coherent portion
of the uncore on some non-coherent writes. Fill buffer propagation by itself
does not make stale data architecturally visible. Stale data must be propagated
to a location where it is subject to reading or sampling.

Sideband Stale Data Propagator (SSDP)
-------------------------------------
The sideband stale data propagator (SSDP) is limited to the client (including
Intel Xeon server E3) uncore implementation. The sideband response buffer is
shared by all client cores. For non-coherent reads that go to sideband
destinations, the uncore logic returns 64 bytes of data to the core, including
both requested data and unrequested stale data, from a transaction buffer and
the sideband response buffer. As a result, stale data from the sideband
response and transaction buffers may now reside in a core fill buffer.

Primary Stale Data Propagator (PSDP)
------------------------------------
The primary stale data propagator (PSDP) is limited to the client (including
Intel Xeon server E3) uncore implementation. Similar to the sideband response
buffer, the primary response buffer is shared by all client cores. For some
processors, MMIO primary reads will return 64 bytes of data to the core fill
buffer including both requested data and unrequested stale data. This is
similar to the sideband stale data propagator.

Vulnerabilities
===============
Device Register Partial Write (DRPW) (CVE-2022-21166)
-----------------------------------------------------
Some endpoint MMIO registers incorrectly handle writes that are smaller than
the register size. Instead of aborting the write or only copying the correct
subset of bytes (for example, 2 bytes for a 2-byte write), more bytes than
specified by the write transaction may be written to the register. On
processors affected by FBSDP, this may expose stale data from the fill buffers
of the core that created the write transaction.

Shared Buffers Data Sampling (SBDS) (CVE-2022-21125)
----------------------------------------------------
After propagators may have moved data around the uncore and copied stale data
into client core fill buffers, processors affected by MFBDS can leak data from
the fill buffer. It is limited to the client (including Intel Xeon server E3)
uncore implementation.

Shared Buffers Data Read (SBDR) (CVE-2022-21123)
------------------------------------------------
It is similar to Shared Buffer Data Sampling (SBDS) except that the data is
directly read into the architectural software-visible state. It is limited to
the client (including Intel Xeon server E3) uncore implementation.

Affected Processors
===================
Not all the CPUs are affected by all the variants. For instance, most
processors for the server market (excluding Intel Xeon E3 processors) are
impacted by only Device Register Partial Write (DRPW).

Below is the list of affected Intel processors [#f1]_:

   ===================  ============  =========
   Common name          Family_Model  Steppings
   ===================  ============  =========
   HASWELL_X            06_3FH        2,4
   SKYLAKE_L            06_4EH        3
   BROADWELL_X          06_4FH        All
   SKYLAKE_X            06_55H        3,4,6,7,11
   BROADWELL_D          06_56H        3,4,5
   SKYLAKE              06_5EH        3
   ICELAKE_X            06_6AH        4,5,6
   ICELAKE_D            06_6CH        1
   ICELAKE_L            06_7EH        5
   ATOM_TREMONT_D       06_86H        All
   LAKEFIELD            06_8AH        1
   KABYLAKE_L           06_8EH        9 to 12
   ATOM_TREMONT         06_96H        1
   ATOM_TREMONT_L       06_9CH        0
   KABYLAKE             06_9EH        9 to 13
   COMETLAKE            06_A5H        2,3,5
   COMETLAKE_L          06_A6H        0,1
   ROCKETLAKE           06_A7H        1
   ===================  ============  =========

If a CPU is in the affected processor list, but not affected by a variant, it
is indicated by new bits in MSR IA32_ARCH_CAPABILITIES. As described in a later
section, mitigation largely remains the same for all the variants, i.e. to
clear the CPU fill buffers via VERW instruction.

New bits in MSRs
================
Newer processors and microcode update on existing affected processors added new
bits to IA32_ARCH_CAPABILITIES MSR. These bits can be used to enumerate
specific variants of Processor MMIO Stale Data vulnerabilities and mitigation
capability.

MSR IA32_ARCH_CAPABILITIES
--------------------------
Bit 13 - SBDR_SSDP_NO - When set, processor is not affected by either the
	 Shared Buffers Data Read (SBDR) vulnerability or the sideband stale
	 data propagator (SSDP).
Bit 14 - FBSDP_NO - When set, processor is not affected by the Fill Buffer
	 Stale Data Propagator (FBSDP).
Bit 15 - PSDP_NO - When set, processor is not affected by Primary Stale Data
	 Propagator (PSDP).
Bit 17 - FB_CLEAR - When set, VERW instruction will overwrite CPU fill buffer
	 values as part of MD_CLEAR operations. Processors that do not
	 enumerate MDS_NO (meaning they are affected by MDS) but that do
	 enumerate support for both L1D_FLUSH and MD_CLEAR implicitly enumerate
	 FB_CLEAR as part of their MD_CLEAR support.
Bit 18 - FB_CLEAR_CTRL - Processor supports read and write to MSR
	 IA32_MCU_OPT_CTRL[FB_CLEAR_DIS]. On such processors, the FB_CLEAR_DIS
	 bit can be set to cause the VERW instruction to not perform the
	 FB_CLEAR action. Not all processors that support FB_CLEAR will support
	 FB_CLEAR_CTRL.

MSR IA32_MCU_OPT_CTRL
---------------------
Bit 3 - FB_CLEAR_DIS - When set, VERW instruction does not perform the FB_CLEAR
action. This may be useful to reduce the performance impact of FB_CLEAR in
cases where system software deems it warranted (for example, when performance
is more critical, or the untrusted software has no MMIO access). Note that
FB_CLEAR_DIS has no impact on enumeration (for example, it does not change
FB_CLEAR or MD_CLEAR enumeration) and it may not be supported on all processors
that enumerate FB_CLEAR.

Mitigation
==========
Like MDS, all variants of Processor MMIO Stale Data vulnerabilities  have the
same mitigation strategy to force the CPU to clear the affected buffers before
an attacker can extract the secrets.

This is achieved by using the otherwise unused and obsolete VERW instruction in
combination with a microcode update. The microcode clears the affected CPU
buffers when the VERW instruction is executed.

Kernel reuses the MDS function to invoke the buffer clearing:

	mds_clear_cpu_buffers()

On MDS affected CPUs, the kernel already invokes CPU buffer clear on
kernel/userspace, hypervisor/guest and C-state (idle) transitions. No
additional mitigation is needed on such CPUs.

For CPUs not affected by MDS or TAA, mitigation is needed only for the attacker
with MMIO capability. Therefore, VERW is not required for kernel/userspace. For
virtualization case, VERW is only needed at VMENTER for a guest with MMIO
capability.

Mitigation points
-----------------
Return to user space
^^^^^^^^^^^^^^^^^^^^
Same mitigation as MDS when affected by MDS/TAA, otherwise no mitigation
needed.

C-State transition
^^^^^^^^^^^^^^^^^^
Control register writes by CPU during C-state transition can propagate data
from fill buffer to uncore buffers. Execute VERW before C-state transition to
clear CPU fill buffers.

Guest entry point
^^^^^^^^^^^^^^^^^
Same mitigation as MDS when processor is also affected by MDS/TAA, otherwise
execute VERW at VMENTER only for MMIO capable guests. On CPUs not affected by
MDS/TAA, guest without MMIO access cannot extract secrets using Processor MMIO
Stale Data vulnerabilities, so there is no need to execute VERW for such guests.

Mitigation control on the kernel command line
---------------------------------------------
The kernel command line allows to control the Processor MMIO Stale Data
mitigations at boot time with the option "mmio_stale_data=". The valid
arguments for this option are:

  ==========  =================================================================
  full        If the CPU is vulnerable, enable mitigation; CPU buffer clearing
              on exit to userspace and when entering a VM. Idle transitions are
              protected as well. It does not automatically disable SMT.
  full,nosmt  Same as full, with SMT disabled on vulnerable CPUs. This is the
              complete mitigation.
  off         Disables mitigation completely.
  ==========  =================================================================

If the CPU is affected and mmio_stale_data=off is not supplied on the kernel
command line, then the kernel selects the appropriate mitigation.

Mitigation status information
-----------------------------
The Linux kernel provides a sysfs interface to enumerate the current
vulnerability status of the system: whether the system is vulnerable, and
which mitigations are active. The relevant sysfs file is:

	/sys/devices/system/cpu/vulnerabilities/mmio_stale_data

The possible values in this file are:

  .. list-table::

     * - 'Not affected'
       - The processor is not vulnerable
     * - 'Vulnerable'
       - The processor is vulnerable, but no mitigation enabled
     * - 'Vulnerable: Clear CPU buffers attempted, no microcode'
       - The processor is vulnerable, but microcode is not updated. The
         mitigation is enabled on a best effort basis.
     * - 'Mitigation: Clear CPU buffers'
       - The processor is vulnerable and the CPU buffer clearing mitigation is
         enabled.
     * - 'Unknown: No mitigations'
       - The processor vulnerability status is unknown because it is
	 out of Servicing period. Mitigation is not attempted.

Definitions:
------------

Servicing period: The process of providing functional and security updates to
Intel processors or platforms, utilizing the Intel Platform Update (IPU)
process or other similar mechanisms.

End of Servicing Updates (ESU): ESU is the date at which Intel will no
longer provide Servicing, such as through IPU or other similar update
processes. ESU dates will typically be aligned to end of quarter.

If the processor is vulnerable then the following information is appended to
the above information:

  ========================  ===========================================
  'SMT vulnerable'          SMT is enabled
  'SMT disabled'            SMT is disabled
  'SMT Host state unknown'  Kernel runs in a VM, Host SMT state unknown
  ========================  ===========================================

References
----------
.. [#f1] Affected Processors
   https://www.intel.com/content/www/us/en/developer/topic-technology/software-security-guidance/processors-affected-consolidated-product-cpu-model.html

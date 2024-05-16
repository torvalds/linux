==================================
Register File Data Sampling (RFDS)
==================================

Register File Data Sampling (RFDS) is a microarchitectural vulnerability that
only affects Intel Atom parts(also branded as E-cores). RFDS may allow
a malicious actor to infer data values previously used in floating point
registers, vector registers, or integer registers. RFDS does not provide the
ability to choose which data is inferred. CVE-2023-28746 is assigned to RFDS.

Affected Processors
===================
Below is the list of affected Intel processors [#f1]_:

   ===================  ============
   Common name          Family_Model
   ===================  ============
   ATOM_GOLDMONT           06_5CH
   ATOM_GOLDMONT_D         06_5FH
   ATOM_GOLDMONT_PLUS      06_7AH
   ATOM_TREMONT_D          06_86H
   ATOM_TREMONT            06_96H
   ALDERLAKE               06_97H
   ALDERLAKE_L             06_9AH
   ATOM_TREMONT_L          06_9CH
   RAPTORLAKE              06_B7H
   RAPTORLAKE_P            06_BAH
   ATOM_GRACEMONT          06_BEH
   RAPTORLAKE_S            06_BFH
   ===================  ============

As an exception to this table, Intel Xeon E family parts ALDERLAKE(06_97H) and
RAPTORLAKE(06_B7H) codenamed Catlow are not affected. They are reported as
vulnerable in Linux because they share the same family/model with an affected
part. Unlike their affected counterparts, they do not enumerate RFDS_CLEAR or
CPUID.HYBRID. This information could be used to distinguish between the
affected and unaffected parts, but it is deemed not worth adding complexity as
the reporting is fixed automatically when these parts enumerate RFDS_NO.

Mitigation
==========
Intel released a microcode update that enables software to clear sensitive
information using the VERW instruction. Like MDS, RFDS deploys the same
mitigation strategy to force the CPU to clear the affected buffers before an
attacker can extract the secrets. This is achieved by using the otherwise
unused and obsolete VERW instruction in combination with a microcode update.
The microcode clears the affected CPU buffers when the VERW instruction is
executed.

Mitigation points
-----------------
VERW is executed by the kernel before returning to user space, and by KVM
before VMentry. None of the affected cores support SMT, so VERW is not required
at C-state transitions.

New bits in IA32_ARCH_CAPABILITIES
----------------------------------
Newer processors and microcode update on existing affected processors added new
bits to IA32_ARCH_CAPABILITIES MSR. These bits can be used to enumerate
vulnerability and mitigation capability:

- Bit 27 - RFDS_NO - When set, processor is not affected by RFDS.
- Bit 28 - RFDS_CLEAR - When set, processor is affected by RFDS, and has the
  microcode that clears the affected buffers on VERW execution.

Mitigation control on the kernel command line
---------------------------------------------
The kernel command line allows to control RFDS mitigation at boot time with the
parameter "reg_file_data_sampling=". The valid arguments are:

  ==========  =================================================================
  on          If the CPU is vulnerable, enable mitigation; CPU buffer clearing
              on exit to userspace and before entering a VM.
  off         Disables mitigation.
  ==========  =================================================================

Mitigation default is selected by CONFIG_MITIGATION_RFDS.

Mitigation status information
-----------------------------
The Linux kernel provides a sysfs interface to enumerate the current
vulnerability status of the system: whether the system is vulnerable, and
which mitigations are active. The relevant sysfs file is:

	/sys/devices/system/cpu/vulnerabilities/reg_file_data_sampling

The possible values in this file are:

  .. list-table::

     * - 'Not affected'
       - The processor is not vulnerable
     * - 'Vulnerable'
       - The processor is vulnerable, but no mitigation enabled
     * - 'Vulnerable: No microcode'
       - The processor is vulnerable but microcode is not updated.
     * - 'Mitigation: Clear Register File'
       - The processor is vulnerable and the CPU buffer clearing mitigation is
	 enabled.

References
----------
.. [#f1] Affected Processors
   https://www.intel.com/content/www/us/en/developer/topic-technology/software-security-guidance/processors-affected-consolidated-product-cpu-model.html

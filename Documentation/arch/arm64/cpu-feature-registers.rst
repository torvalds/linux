===========================
ARM64 CPU Feature Registers
===========================

Author: Suzuki K Poulose <suzuki.poulose@arm.com>


This file describes the ABI for exporting the AArch64 CPU ID/feature
registers to userspace. The availability of this ABI is advertised
via the HWCAP_CPUID in HWCAPs.

1. Motivation
-------------

The ARM architecture defines a set of feature registers, which describe
the capabilities of the CPU/system. Access to these system registers is
restricted from EL0 and there is no reliable way for an application to
extract this information to make better decisions at runtime. There is
limited information available to the application via HWCAPs, however
there are some issues with their usage.

 a) Any change to the HWCAPs requires an update to userspace (e.g libc)
    to detect the new changes, which can take a long time to appear in
    distributions. Exposing the registers allows applications to get the
    information without requiring updates to the toolchains.

 b) Access to HWCAPs is sometimes limited (e.g prior to libc, or
    when ld is initialised at startup time).

 c) HWCAPs cannot represent non-boolean information effectively. The
    architecture defines a canonical format for representing features
    in the ID registers; this is well defined and is capable of
    representing all valid architecture variations.


2. Requirements
---------------

 a) Safety:

    Applications should be able to use the information provided by the
    infrastructure to run safely across the system. This has greater
    implications on a system with heterogeneous CPUs.
    The infrastructure exports a value that is safe across all the
    available CPU on the system.

    e.g, If at least one CPU doesn't implement CRC32 instructions, while
    others do, we should report that the CRC32 is not implemented.
    Otherwise an application could crash when scheduled on the CPU
    which doesn't support CRC32.

 b) Security:

    Applications should only be able to receive information that is
    relevant to the normal operation in userspace. Hence, some of the
    fields are masked out(i.e, made invisible) and their values are set to
    indicate the feature is 'not supported'. See Section 4 for the list
    of visible features. Also, the kernel may manipulate the fields
    based on what it supports. e.g, If FP is not supported by the
    kernel, the values could indicate that the FP is not available
    (even when the CPU provides it).

 c) Implementation Defined Features

    The infrastructure doesn't expose any register which is
    IMPLEMENTATION DEFINED as per ARMv8-A Architecture.

 d) CPU Identification:

    MIDR_EL1 is exposed to help identify the processor. On a
    heterogeneous system, this could be racy (just like getcpu()). The
    process could be migrated to another CPU by the time it uses the
    register value, unless the CPU affinity is set. Hence, there is no
    guarantee that the value reflects the processor that it is
    currently executing on. REVIDR and AIDR are not exposed due to this
    constraint, as these registers only make sense in conjunction with
    the MIDR. Alternately, MIDR_EL1, REVIDR_EL1, and AIDR_EL1 are exposed
    via sysfs at::

	/sys/devices/system/cpu/cpu$ID/regs/identification/
	                                              \- midr_el1
	                                              \- revidr_el1
	                                              \- aidr_el1

3. Implementation
--------------------

The infrastructure is built on the emulation of the 'MRS' instruction.
Accessing a restricted system register from an application generates an
exception and ends up in SIGILL being delivered to the process.
The infrastructure hooks into the exception handler and emulates the
operation if the source belongs to the supported system register space.

The infrastructure emulates only the following system register space::

	Op0=3, Op1=0, CRn=0, CRm=0,2,3,4,5,6,7

(See Table C5-6 'System instruction encodings for non-Debug System
register accesses' in ARMv8 ARM DDI 0487A.h, for the list of
registers).

The following rules are applied to the value returned by the
infrastructure:

 a) The value of an 'IMPLEMENTATION DEFINED' field is set to 0.
 b) The value of a reserved field is populated with the reserved
    value as defined by the architecture.
 c) The value of a 'visible' field holds the system wide safe value
    for the particular feature (except for MIDR_EL1, see section 4).
 d) All other fields (i.e, invisible fields) are set to indicate
    the feature is missing (as defined by the architecture).

4. List of registers with visible features
-------------------------------------------

  ID_AA64FPFR0_EL1 - Floating Point feature ID register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | F8CVT                        | [31]    |
     +------------------------------+---------+
     | F8FMA                        | [30]    |
     +------------------------------+---------+
     | F8DP4                        | [29]    |
     +------------------------------+---------+
     | F8DP2                        | [28]    |
     +------------------------------+---------+
     | F8MM8                        | [27]    |
     +------------------------------+---------+
     | F8MM4                        | [26]    |
     +------------------------------+---------+
     | F16MM2                       | [15]    |
     +------------------------------+---------+
     | F8E4M3                       | [1]     |
     +------------------------------+---------+
     | F8E5M2                       | [0]     |
     +------------------------------+---------+

  ID_AA64ISAR0_EL1 - Instruction Set Attribute Register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | RNDR                         | [63-60] |
     +------------------------------+---------+
     | TS                           | [55-52] |
     +------------------------------+---------+
     | FHM                          | [51-48] |
     +------------------------------+---------+
     | DP                           | [47-44] |
     +------------------------------+---------+
     | SM4                          | [43-40] |
     +------------------------------+---------+
     | SM3                          | [39-36] |
     +------------------------------+---------+
     | SHA3                         | [35-32] |
     +------------------------------+---------+
     | RDM                          | [31-28] |
     +------------------------------+---------+
     | ATOMICS                      | [23-20] |
     +------------------------------+---------+
     | CRC32                        | [19-16] |
     +------------------------------+---------+
     | SHA2                         | [15-12] |
     +------------------------------+---------+
     | SHA1                         | [11-8]  |
     +------------------------------+---------+
     | AES                          | [7-4]   |
     +------------------------------+---------+


  ID_AA64ISAR1_EL1 - Instruction set attribute register 1

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | LS64                         | [63-60] |
     +------------------------------+---------+
     | I8MM                         | [55-52] |
     +------------------------------+---------+
     | DGH                          | [51-48] |
     +------------------------------+---------+
     | BF16                         | [47-44] |
     +------------------------------+---------+
     | SB                           | [39-36] |
     +------------------------------+---------+
     | FRINTTS                      | [35-32] |
     +------------------------------+---------+
     | GPI                          | [31-28] |
     +------------------------------+---------+
     | GPA                          | [27-24] |
     +------------------------------+---------+
     | LRCPC                        | [23-20] |
     +------------------------------+---------+
     | FCMA                         | [19-16] |
     +------------------------------+---------+
     | JSCVT                        | [15-12] |
     +------------------------------+---------+
     | API                          | [11-8]  |
     +------------------------------+---------+
     | APA                          | [7-4]   |
     +------------------------------+---------+
     | DPB                          | [3-0]   |
     +------------------------------+---------+

  ID_AA64ISAR2_EL1 - Instruction set attribute register 2

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | LUT                          | [59-56] |
     +------------------------------+---------+
     | CSSC                         | [55-52] |
     +------------------------------+---------+
     | RPRFM                        | [51-48] |
     +------------------------------+---------+
     | BC                           | [23-20] |
     +------------------------------+---------+
     | MOPS                         | [19-16] |
     +------------------------------+---------+
     | APA3                         | [15-12] |
     +------------------------------+---------+
     | GPA3                         | [11-8]  |
     +------------------------------+---------+
     | RPRES                        | [7-4]   |
     +------------------------------+---------+
     | WFXT                         | [3-0]   |
     +------------------------------+---------+

  ID_AA64ISAR3_EL1 - Instruction set attribute register 3

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | FPRCVT                       | [31-28] |
     +------------------------------+---------+
     | LSFE                         | [19-16] |
     +------------------------------+---------+
     | FAMINMAX                     | [7-4]   |
     +------------------------------+---------+

  ID_AA64MMFR0_EL1 - Memory model feature register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | ECV                          | [63-60] |
     +------------------------------+---------+

  ID_AA64MMFR1_EL1 - Memory model feature register 1

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | AFP                          | [47-44] |
     +------------------------------+---------+

  ID_AA64MMFR2_EL1 - Memory model feature register 2

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | AT                           | [35-32] |
     +------------------------------+---------+

  ID_AA64MMFR3_EL1 - Memory model feature register 3

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | S1POE                        | [19-16] |
     +------------------------------+---------+

  ID_AA64PFR0_EL1 - Processor Feature Register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | DIT                          | [51-48] |
     +------------------------------+---------+
     | SVE                          | [35-32] |
     +------------------------------+---------+
     | AdvSIMD                      | [23-20] |
     +------------------------------+---------+
     | FP                           | [19-16] |
     +------------------------------+---------+


  ID_AA64PFR1_EL1 - Processor Feature Register 1

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | GCS                          | [47-44] |
     +------------------------------+---------+
     | SME                          | [27-24] |
     +------------------------------+---------+
     | MTE                          | [11-8]  |
     +------------------------------+---------+
     | SSBS                         | [7-4]   |
     +------------------------------+---------+
     | BT                           | [3-0]   |
     +------------------------------+---------+

  ID_AA64PFR2_EL1 - Processor Feature Register 2

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | FPMR                         | [35-32] |
     +------------------------------+---------+
     | MTEFAR                       | [11-8]  |
     +------------------------------+---------+
     | MTESTOREONLY                 | [7-4]   |
     +------------------------------+---------+

  ID_AA64SMFR0_EL1 - SME feature ID register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | FA64                         | [63]    |
     +------------------------------+---------+
     | LUT6                         | [61]    |
     +------------------------------+---------+
     | LUTv2                        | [60]    |
     +------------------------------+---------+
     | SMEver                       | [59-56] |
     +------------------------------+---------+
     | I16I64                       | [55-52] |
     +------------------------------+---------+
     | F64F64                       | [48]    |
     +------------------------------+---------+
     | I16I32                       | [47-44] |
     +------------------------------+---------+
     | B16B16                       | [43]    |
     +------------------------------+---------+
     | F16F16                       | [42]    |
     +------------------------------+---------+
     | F8F16                        | [41]    |
     +------------------------------+---------+
     | F8F32                        | [40]    |
     +------------------------------+---------+
     | I8I32                        | [39-36] |
     +------------------------------+---------+
     | F16F32                       | [35]    |
     +------------------------------+---------+
     | B16F32                       | [34]    |
     +------------------------------+---------+
     | BI32I32                      | [33]    |
     +------------------------------+---------+
     | F32F32                       | [32]    |
     +------------------------------+---------+
     | SF8FMA                       | [30]    |
     +------------------------------+---------+
     | SF8DP4                       | [29]    |
     +------------------------------+---------+
     | SF8DP2                       | [28]    |
     +------------------------------+---------+
     | SBitPerm                     | [25]    |
     +------------------------------+---------+
     | AES                          | [24]    |
     +------------------------------+---------+
     | SFEXPA                       | [23]    |
     +------------------------------+---------+
     | STMOP                        | [16]    |
     +------------------------------+---------+
     | SMOP4                        | [0]     |
     +------------------------------+---------+

  ID_AA64ZFR0_EL1 - SVE feature ID register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | F64MM                        | [59-56] |
     +------------------------------+---------+
     | F32MM                        | [55-52] |
     +------------------------------+---------+
     | F16MM                        | [51-48] |
     +------------------------------+---------+
     | I8MM                         | [47-44] |
     +------------------------------+---------+
     | SM4                          | [43-40] |
     +------------------------------+---------+
     | SHA3                         | [35-32] |
     +------------------------------+---------+
     | B16B16                       | [27-24] |
     +------------------------------+---------+
     | BF16                         | [23-20] |
     +------------------------------+---------+
     | BitPerm                      | [19-16] |
     +------------------------------+---------+
     | EltPerm                      | [15-12] |
     +------------------------------+---------+
     | AES                          | [7-4]   |
     +------------------------------+---------+
     | SVEVer                       | [3-0]   |
     +------------------------------+---------+

  ID_ISAR5_EL1 - AArch32 Instruction Set Attribute Register 5

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | CRC32                        | [19-16] |
     +------------------------------+---------+
     | SHA2                         | [15-12] |
     +------------------------------+---------+
     | SHA1                         | [11-8]  |
     +------------------------------+---------+
     | AES                          | [7-4]   |
     +------------------------------+---------+

  ID_ISAR6_EL1 - AArch32 Instruction Set Attribute Register 6

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | I8MM                         | [27-24] |
     +------------------------------+---------+
     | BF16                         | [23-20] |
     +------------------------------+---------+
     | SB                           | [15-12] |
     +------------------------------+---------+
     | FHM                          | [11-8]  |
     +------------------------------+---------+
     | DP                           | [7-4]   |
     +------------------------------+---------+

  ID_PFR2_EL1 - AArch32 Processor Feature Register 2

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | SSBS                         | [7-4]   |
     +------------------------------+---------+

  MIDR_EL1 - Main ID Register

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | Implementer                  | [31-24] |
     +------------------------------+---------+
     | Variant                      | [23-20] |
     +------------------------------+---------+
     | Architecture                 | [19-16] |
     +------------------------------+---------+
     | PartNum                      | [15-4]  |
     +------------------------------+---------+
     | Revision                     | [3-0]   |
     +------------------------------+---------+

   NOTE: The 'visible' fields of MIDR_EL1 will contain the value
   as available on the CPU where it is fetched and is not a system
   wide safe value.

  MVFR0_EL1 - AArch32 Media and VFP Feature Register 0

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | FPDP                         | [11-8]  |
     +------------------------------+---------+

  MVFR1_EL1 - AArch32 Media and VFP Feature Register 1

     +------------------------------+---------+
     | Name                         |  bits   |
     +------------------------------+---------+
     | SIMDFMAC                     | [31-28] |
     +------------------------------+---------+
     | FPHP                         | [27-24] |
     +------------------------------+---------+
     | SIMDHP                       | [23-20] |
     +------------------------------+---------+
     | SIMDSP                       | [19-16] |
     +------------------------------+---------+
     | SIMDInt                      | [15-12] |
     +------------------------------+---------+
     | SIMDLS                       | [11-8]  |
     +------------------------------+---------+


Appendix I: Example
-------------------

::

  /*
   * Sample program to demonstrate the MRS emulation ABI.
   *
   * Copyright (C) 2015-2016, ARM Ltd
   *
   * Author: Suzuki K Poulose <suzuki.poulose@arm.com>
   *
   * This program is free software; you can redistribute it and/or modify
   * it under the terms of the GNU General Public License version 2 as
   * published by the Free Software Foundation.
   *
   * This program is distributed in the hope that it will be useful,
   * but WITHOUT ANY WARRANTY; without even the implied warranty of
   * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   * GNU General Public License for more details.
   * This program is free software; you can redistribute it and/or modify
   * it under the terms of the GNU General Public License version 2 as
   * published by the Free Software Foundation.
   *
   * This program is distributed in the hope that it will be useful,
   * but WITHOUT ANY WARRANTY; without even the implied warranty of
   * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   * GNU General Public License for more details.
   */

  #include <asm/hwcap.h>
  #include <stdio.h>
  #include <sys/auxv.h>

  #define get_cpu_ftr(id) ({					\
		unsigned long __val;				\
		asm("mrs %0, "#id : "=r" (__val));		\
		printf("%-20s: 0x%016lx\n", #id, __val);	\
	})

  int main(void)
  {

	if (!(getauxval(AT_HWCAP) & HWCAP_CPUID)) {
		fputs("CPUID registers unavailable\n", stderr);
		return 1;
	}

	get_cpu_ftr(ID_AA64ISAR0_EL1);
	get_cpu_ftr(ID_AA64ISAR1_EL1);
	get_cpu_ftr(ID_AA64MMFR0_EL1);
	get_cpu_ftr(ID_AA64MMFR1_EL1);
	get_cpu_ftr(ID_AA64PFR0_EL1);
	get_cpu_ftr(ID_AA64PFR1_EL1);
	get_cpu_ftr(ID_AA64DFR0_EL1);
	get_cpu_ftr(ID_AA64DFR1_EL1);

	get_cpu_ftr(MIDR_EL1);
	get_cpu_ftr(MPIDR_EL1);
	get_cpu_ftr(REVIDR_EL1);

  #if 0
	/* Unexposed register access causes SIGILL */
	get_cpu_ftr(ID_MMFR0_EL1);
  #endif

	return 0;
  }

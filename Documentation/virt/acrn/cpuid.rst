.. SPDX-License-Identifier: GPL-2.0

===============
ACRN CPUID bits
===============

A guest VM running on an ACRN hypervisor can check some of its features using
CPUID.

ACRN cpuid functions are:

function: 0x40000000

returns::

   eax = 0x40000010
   ebx = 0x4e524341
   ecx = 0x4e524341
   edx = 0x4e524341

Note that this value in ebx, ecx and edx corresponds to the string
"ACRNACRNACRN". The value in eax corresponds to the maximum cpuid function
present in this leaf, and will be updated if more functions are added in the
future.

function: define ACRN_CPUID_FEATURES (0x40000001)

returns::

          ebx, ecx, edx
          eax = an OR'ed group of (1 << flag)

where ``flag`` is defined as below:

================================= =========== ================================
flag                              value       meaning
================================= =========== ================================
ACRN_FEATURE_PRIVILEGED_VM        0           guest VM is a privileged VM
================================= =========== ================================

function: 0x40000010

returns::

          ebx, ecx, edx
          eax = (Virtual) TSC frequency in kHz.

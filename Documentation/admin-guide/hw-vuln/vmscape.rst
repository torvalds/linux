.. SPDX-License-Identifier: GPL-2.0

VMSCAPE
=======

VMSCAPE is a vulnerability that may allow a guest to influence the branch
prediction in host userspace. It particularly affects hypervisors like QEMU.

Even if a hypervisor may not have any sensitive data like disk encryption keys,
guest-userspace may be able to attack the guest-kernel using the hypervisor as
a confused deputy.

Affected processors
-------------------

The following CPU families are affected by VMSCAPE:

**Intel processors:**
  - Skylake generation (Parts without Enhanced-IBRS)
  - Cascade Lake generation - (Parts affected by ITS guest/host separation)
  - Alder Lake and newer (Parts affected by BHI)

Note that, BHI affected parts that use BHB clearing software mitigation e.g.
Icelake are not vulnerable to VMSCAPE.

**AMD processors:**
  - Zen series (families 0x17, 0x19, 0x1a)

** Hygon processors:**
 - Family 0x18

Mitigation
----------

Conditional IBPB
----------------

Kernel tracks when a CPU has run a potentially malicious guest and issues an
IBPB before the first exit to userspace after VM-exit. If userspace did not run
between VM-exit and the next VM-entry, no IBPB is issued.

Note that the existing userspace mitigation against Spectre-v2 is effective in
protecting the userspace. They are insufficient to protect the userspace VMMs
from a malicious guest. This is because Spectre-v2 mitigations are applied at
context switch time, while the userspace VMM can run after a VM-exit without a
context switch.

Vulnerability enumeration and mitigation is not applied inside a guest. This is
because nested hypervisors should already be deploying IBPB to isolate
themselves from nested guests.

SMT considerations
------------------

When Simultaneous Multi-Threading (SMT) is enabled, hypervisors can be
vulnerable to cross-thread attacks. For complete protection against VMSCAPE
attacks in SMT environments, STIBP should be enabled.

The kernel will issue a warning if SMT is enabled without adequate STIBP
protection. Warning is not issued when:

- SMT is disabled
- STIBP is enabled system-wide
- Intel eIBRS is enabled (which implies STIBP protection)

System information and options
------------------------------

The sysfs file showing VMSCAPE mitigation status is:

  /sys/devices/system/cpu/vulnerabilities/vmscape

The possible values in this file are:

 * 'Not affected':

   The processor is not vulnerable to VMSCAPE attacks.

 * 'Vulnerable':

   The processor is vulnerable and no mitigation has been applied.

 * 'Mitigation: IBPB before exit to userspace':

   Conditional IBPB mitigation is enabled. The kernel tracks when a CPU has
   run a potentially malicious guest and issues an IBPB before the first
   exit to userspace after VM-exit.

 * 'Mitigation: IBPB on VMEXIT':

   IBPB is issued on every VM-exit. This occurs when other mitigations like
   RETBLEED or SRSO are already issuing IBPB on VM-exit.

Mitigation control on the kernel command line
----------------------------------------------

The mitigation can be controlled via the ``vmscape=`` command line parameter:

 * ``vmscape=off``:

   Disable the VMSCAPE mitigation.

 * ``vmscape=ibpb``:

   Enable conditional IBPB mitigation (default when CONFIG_MITIGATION_VMSCAPE=y).

 * ``vmscape=force``:

   Force vulnerability detection and mitigation even on processors that are
   not known to be affected.

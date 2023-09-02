.. SPDX-License-Identifier: GPL-2.0

Speculative Return Stack Overflow (SRSO)
========================================

This is a mitigation for the speculative return stack overflow (SRSO)
vulnerability found on AMD processors. The mechanism is by now the well
known scenario of poisoning CPU functional units - the Branch Target
Buffer (BTB) and Return Address Predictor (RAP) in this case - and then
tricking the elevated privilege domain (the kernel) into leaking
sensitive data.

AMD CPUs predict RET instructions using a Return Address Predictor (aka
Return Address Stack/Return Stack Buffer). In some cases, a non-architectural
CALL instruction (i.e., an instruction predicted to be a CALL but is
not actually a CALL) can create an entry in the RAP which may be used
to predict the target of a subsequent RET instruction.

The specific circumstances that lead to this varies by microarchitecture
but the concern is that an attacker can mis-train the CPU BTB to predict
non-architectural CALL instructions in kernel space and use this to
control the speculative target of a subsequent kernel RET, potentially
leading to information disclosure via a speculative side-channel.

The issue is tracked under CVE-2023-20569.

Affected processors
-------------------

AMD Zen, generations 1-4. That is, all families 0x17 and 0x19. Older
processors have not been investigated.

System information and options
------------------------------

First of all, it is required that the latest microcode be loaded for
mitigations to be effective.

The sysfs file showing SRSO mitigation status is:

  /sys/devices/system/cpu/vulnerabilities/spec_rstack_overflow

The possible values in this file are:

 - 'Not affected'               The processor is not vulnerable

 - 'Vulnerable: no microcode'   The processor is vulnerable, no
                                microcode extending IBPB functionality
                                to address the vulnerability has been
                                applied.

 - 'Mitigation: microcode'      Extended IBPB functionality microcode
                                patch has been applied. It does not
                                address User->Kernel and Guest->Host
                                transitions protection but it does
                                address User->User and VM->VM attack
                                vectors.

                                (spec_rstack_overflow=microcode)

 - 'Mitigation: safe RET'       Software-only mitigation. It complements
                                the extended IBPB microcode patch
                                functionality by addressing User->Kernel
                                and Guest->Host transitions protection.

                                Selected by default or by
                                spec_rstack_overflow=safe-ret

 - 'Mitigation: IBPB'           Similar protection as "safe RET" above
                                but employs an IBPB barrier on privilege
                                domain crossings (User->Kernel,
                                Guest->Host).

                                (spec_rstack_overflow=ibpb)

 - 'Mitigation: IBPB on VMEXIT' Mitigation addressing the cloud provider
                                scenario - the Guest->Host transitions
                                only.

                                (spec_rstack_overflow=ibpb-vmexit)

In order to exploit vulnerability, an attacker needs to:

 - gain local access on the machine

 - break kASLR

 - find gadgets in the running kernel in order to use them in the exploit

 - potentially create and pin an additional workload on the sibling
   thread, depending on the microarchitecture (not necessary on fam 0x19)

 - run the exploit

Considering the performance implications of each mitigation type, the
default one is 'Mitigation: safe RET' which should take care of most
attack vectors, including the local User->Kernel one.

As always, the user is advised to keep her/his system up-to-date by
applying software updates regularly.

The default setting will be reevaluated when needed and especially when
new attack vectors appear.

As one can surmise, 'Mitigation: safe RET' does come at the cost of some
performance depending on the workload. If one trusts her/his userspace
and does not want to suffer the performance impact, one can always
disable the mitigation with spec_rstack_overflow=off.

Similarly, 'Mitigation: IBPB' is another full mitigation type employing
an indrect branch prediction barrier after having applied the required
microcode patch for one's system. This mitigation comes also at
a performance cost.

Mitigation: safe RET
--------------------

The mitigation works by ensuring all RET instructions speculate to
a controlled location, similar to how speculation is controlled in the
retpoline sequence.  To accomplish this, the __x86_return_thunk forces
the CPU to mispredict every function return using a 'safe return'
sequence.

To ensure the safety of this mitigation, the kernel must ensure that the
safe return sequence is itself free from attacker interference.  In Zen3
and Zen4, this is accomplished by creating a BTB alias between the
untraining function srso_untrain_ret_alias() and the safe return
function srso_safe_ret_alias() which results in evicting a potentially
poisoned BTB entry and using that safe one for all function returns.

In older Zen1 and Zen2, this is accomplished using a reinterpretation
technique similar to Retbleed one: srso_untrain_ret() and
srso_safe_ret().

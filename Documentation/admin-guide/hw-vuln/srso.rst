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

 * 'Not affected':

   The processor is not vulnerable

* 'Vulnerable':

   The processor is vulnerable and no mitigations have been applied.

 * 'Vulnerable: No microcode':

   The processor is vulnerable, no microcode extending IBPB
   functionality to address the vulnerability has been applied.

 * 'Vulnerable: Safe RET, no microcode':

   The "Safe RET" mitigation (see below) has been applied to protect the
   kernel, but the IBPB-extending microcode has not been applied.  User
   space tasks may still be vulnerable.

 * 'Vulnerable: Microcode, no safe RET':

   Extended IBPB functionality microcode patch has been applied. It does
   not address User->Kernel and Guest->Host transitions protection but it
   does address User->User and VM->VM attack vectors.

   Note that User->User mitigation is controlled by how the IBPB aspect in
   the Spectre v2 mitigation is selected:

    * conditional IBPB:

      where each process can select whether it needs an IBPB issued
      around it PR_SPEC_DISABLE/_ENABLE etc, see :doc:`spectre`

    * strict:

      i.e., always on - by supplying spectre_v2_user=on on the kernel
      command line

   (spec_rstack_overflow=microcode)

 * 'Mitigation: Safe RET':

   Combined microcode/software mitigation. It complements the
   extended IBPB microcode patch functionality by addressing
   User->Kernel and Guest->Host transitions protection.

   Selected by default or by spec_rstack_overflow=safe-ret

 * 'Mitigation: IBPB':

   Similar protection as "safe RET" above but employs an IBPB barrier on
   privilege domain crossings (User->Kernel, Guest->Host).

  (spec_rstack_overflow=ibpb)

 * 'Mitigation: IBPB on VMEXIT':

   Mitigation addressing the cloud provider scenario - the Guest->Host
   transitions only.

   (spec_rstack_overflow=ibpb-vmexit)

 * 'Mitigation: Reduced Speculation':

   This mitigation gets automatically enabled when the above one "IBPB on
   VMEXIT" has been selected and the CPU supports the BpSpecReduce bit.

   It gets automatically enabled on machines which have the
   SRSO_USER_KERNEL_NO=1 CPUID bit. In that case, the code logic is to switch
   to the above =ibpb-vmexit mitigation because the user/kernel boundary is
   not affected anymore and thus "safe RET" is not needed.

   After enabling the IBPB on VMEXIT mitigation option, the BpSpecReduce bit
   is detected (functionality present on all such machines) and that
   practically overrides IBPB on VMEXIT as it has a lot less performance
   impact and takes care of the guest->host attack vector too.

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
an indirect branch prediction barrier after having applied the required
microcode patch for one's system. This mitigation comes also at
a performance cost.

Mitigation: Safe RET
--------------------

The mitigation works by ensuring all RET instructions speculate to
a controlled location, similar to how speculation is controlled in the
retpoline sequence.  To accomplish this, the __x86_return_thunk forces
the CPU to mispredict every function return using a 'safe return'
sequence.

To ensure the safety of this mitigation, the kernel must ensure that the
safe return sequence is itself free from attacker interference.  In Zen3
and Zen4, this is accomplished by creating a BTB alias between the
untraining function srso_alias_untrain_ret() and the safe return
function srso_alias_safe_ret() which results in evicting a potentially
poisoned BTB entry and using that safe one for all function returns.

In older Zen1 and Zen2, this is accomplished using a reinterpretation
technique similar to Retbleed one: srso_untrain_ret() and
srso_safe_ret().

Checking the safe RET mitigation actually works
-----------------------------------------------

In case one wants to validate whether the SRSO safe RET mitigation works
on a kernel, one could use two performance counters

* PMC_0xc8 - Count of RET/RET lw retired
* PMC_0xc9 - Count of RET/RET lw retired mispredicted

and compare the number of RETs retired properly vs those retired
mispredicted, in kernel mode. Another way of specifying those events
is::

        # perf list ex_ret_near_ret

        List of pre-defined events (to be used in -e or -M):

        core:
          ex_ret_near_ret
               [Retired Near Returns]
          ex_ret_near_ret_mispred
               [Retired Near Returns Mispredicted]

Either the command using the event mnemonics::

        # perf stat -e ex_ret_near_ret:k -e ex_ret_near_ret_mispred:k sleep 10s

or using the raw PMC numbers::

        # perf stat -e cpu/event=0xc8,umask=0/k -e cpu/event=0xc9,umask=0/k sleep 10s

should give the same amount. I.e., every RET retired should be
mispredicted::

        [root@brent: ~/kernel/linux/tools/perf> ./perf stat -e cpu/event=0xc8,umask=0/k -e cpu/event=0xc9,umask=0/k sleep 10s

         Performance counter stats for 'sleep 10s':

                   137,167      cpu/event=0xc8,umask=0/k
                   137,173      cpu/event=0xc9,umask=0/k

              10.004110303 seconds time elapsed

               0.000000000 seconds user
               0.004462000 seconds sys

vs the case when the mitigation is disabled (spec_rstack_overflow=off)
or not functioning properly, showing usually a lot smaller number of
mispredicted retired RETs vs the overall count of retired RETs during
a workload::

       [root@brent: ~/kernel/linux/tools/perf> ./perf stat -e cpu/event=0xc8,umask=0/k -e cpu/event=0xc9,umask=0/k sleep 10s

        Performance counter stats for 'sleep 10s':

                  201,627      cpu/event=0xc8,umask=0/k
                    4,074      cpu/event=0xc9,umask=0/k

             10.003267252 seconds time elapsed

              0.002729000 seconds user
              0.000000000 seconds sys

Also, there is a selftest which performs the above, go to
tools/testing/selftests/x86/ and do::

        make srso
        ./srso

.. SPDX-License-Identifier: GPL-2.0

Integrity Policy Enforcement (IPE) - Kernel Documentation
=========================================================

.. NOTE::

   This is documentation targeted at developers, instead of administrators.
   If you're looking for documentation on the usage of IPE, please see
   :doc:`IPE admin guide </admin-guide/LSM/ipe>`.

Historical Motivation
---------------------

The original issue that prompted IPE's implementation was the creation
of a locked-down system. This system would be born-secure, and have
strong integrity guarantees over both the executable code, and specific
*data files* on the system, that were critical to its function. These
specific data files would not be readable unless they passed integrity
policy. A mandatory access control system would be present, and
as a result, xattrs would have to be protected. This lead to a selection
of what would provide the integrity claims. At the time, there were two
main mechanisms considered that could guarantee integrity for the system
with these requirements:

  1. IMA + EVM Signatures
  2. DM-Verity

Both options were carefully considered, however the choice to use DM-Verity
over IMA+EVM as the *integrity mechanism* in the original use case of IPE
was due to three main reasons:

  1. Protection of additional attack vectors:

    * With IMA+EVM, without an encryption solution, the system is vulnerable
      to offline attack against the aforementioned specific data files.

      Unlike executables, read operations (like those on the protected data
      files), cannot be enforced to be globally integrity verified. This means
      there must be some form of selector to determine whether a read should
      enforce the integrity policy, or it should not.

      At the time, this was done with mandatory access control labels. An IMA
      policy would indicate what labels required integrity verification, which
      presented an issue: EVM would protect the label, but if an attacker could
      modify filesystem offline, the attacker could wipe all the xattrs -
      including the SELinux labels that would be used to determine whether the
      file should be subject to integrity policy.

      With DM-Verity, as the xattrs are saved as part of the Merkel tree, if
      offline mount occurs against the filesystem protected by dm-verity, the
      checksum no longer matches and the file fails to be read.

    * As userspace binaries are paged in Linux, dm-verity also offers the
      additional protection against a hostile block device. In such an attack,
      the block device reports the appropriate content for the IMA hash
      initially, passing the required integrity check. Then, on the page fault
      that accesses the real data, will report the attacker's payload. Since
      dm-verity will check the data when the page fault occurs (and the disk
      access), this attack is mitigated.

  2. Performance:

    * dm-verity provides integrity verification on demand as blocks are
      read versus requiring the entire file being read into memory for
      validation.

  3. Simplicity of signing:

    * No need for two signatures (IMA, then EVM): one signature covers
      an entire block device.
    * Signatures can be stored externally to the filesystem metadata.
    * The signature supports an x.509-based signing infrastructure.

The next step was to choose a *policy* to enforce the integrity mechanism.
The minimum requirements for the policy were:

  1. The policy itself must be integrity verified (preventing trivial
     attack against it).
  2. The policy itself must be resistant to rollback attacks.
  3. The policy enforcement must have a permissive-like mode.
  4. The policy must be able to be updated, in its entirety, without
     a reboot.
  5. Policy updates must be atomic.
  6. The policy must support *revocations* of previously authored
     components.
  7. The policy must be auditable, at any point-of-time.

IMA, as the only integrity policy mechanism at the time, was
considered against these list of requirements, and did not fulfill
all of the minimum requirements. Extending IMA to cover these
requirements was considered, but ultimately discarded for a
two reasons:

  1. Regression risk; many of these changes would result in
     dramatic code changes to IMA, which is already present in the
     kernel, and therefore might impact users.

  2. IMA was used in the system for measurement and attestation;
     separation of measurement policy from local integrity policy
     enforcement was considered favorable.

Due to these reasons, it was decided that a new LSM should be created,
whose responsibility would be only the local integrity policy enforcement.

Role and Scope
--------------

IPE, as its name implies, is fundamentally an integrity policy enforcement
solution; IPE does not mandate how integrity is provided, but instead
leaves that decision to the system administrator to set the security bar,
via the mechanisms that they select that suit their individual needs.
There are several different integrity solutions that provide a different
level of security guarantees; and IPE allows sysadmins to express policy for
theoretically all of them.

IPE does not have an inherent mechanism to ensure integrity on its own.
Instead, there are more effective layers available for building systems that
can guarantee integrity. It's important to note that the mechanism for proving
integrity is independent of the policy for enforcing that integrity claim.

Therefore, IPE was designed around:

  1. Easy integrations with integrity providers.
  2. Ease of use for platform administrators/sysadmins.

Design Rationale:
-----------------

IPE was designed after evaluating existing integrity policy solutions
in other operating systems and environments. In this survey of other
implementations, there were a few pitfalls identified:

  1. Policies were not readable by humans, usually requiring a binary
     intermediary format.
  2. A single, non-customizable action was implicitly taken as a default.
  3. Debugging the policy required manual steps to determine what rule was violated.
  4. Authoring a policy required an in-depth knowledge of the larger system,
     or operating system.

IPE attempts to avoid all of these pitfalls.

Policy
~~~~~~

Plain Text
^^^^^^^^^^

IPE's policy is plain-text. This introduces slightly larger policy files than
other LSMs, but solves two major problems that occurs with some integrity policy
solutions on other platforms.

The first issue is one of code maintenance and duplication. To author policies,
the policy has to be some form of string representation (be it structured,
through XML, JSON, YAML, etcetera), to allow the policy author to understand
what is being written. In a hypothetical binary policy design, a serializer
is necessary to write the policy from the human readable form, to the binary
form, and a deserializer is needed to interpret the binary form into a data
structure in the kernel.

Eventually, another deserializer will be needed to transform the binary from
back into the human-readable form with as much information preserved. This is because a
user of this access control system will have to keep a lookup table of a checksum
and the original file itself to try to understand what policies have been deployed
on this system and what policies have not. For a single user, this may be alright,
as old policies can be discarded almost immediately after the update takes hold.
For users that manage computer fleets in the thousands, if not hundreds of thousands,
with multiple different operating systems, and multiple different operational needs,
this quickly becomes an issue, as stale policies from years ago may be present,
quickly resulting in the need to recover the policy or fund extensive infrastructure
to track what each policy contains.

With now three separate serializer/deserializers, maintenance becomes costly. If the
policy avoids the binary format, there is only one required serializer: from the
human-readable form to the data structure in kernel, saving on code maintenance,
and retaining operability.

The second issue with a binary format is one of transparency. As IPE controls
access based on the trust of the system's resources, it's policy must also be
trusted to be changed. This is done through signatures, resulting in needing
signing as a process. Signing, as a process, is typically done with a
high security bar, as anything signed can be used to attack integrity
enforcement systems. It is also important that, when signing something, that
the signer is aware of what they are signing. A binary policy can cause
obfuscation of that fact; what signers see is an opaque binary blob. A
plain-text policy, on the other hand, the signers see the actual policy
submitted for signing.

Boot Policy
~~~~~~~~~~~

IPE, if configured appropriately, is able to enforce a policy as soon as a
kernel is booted and usermode starts. That implies some level of storage
of the policy to apply the minute usermode starts. Generally, that storage
can be handled in one of three ways:

  1. The policy file(s) live on disk and the kernel loads the policy prior
     to an code path that would result in an enforcement decision.
  2. The policy file(s) are passed by the bootloader to the kernel, who
     parses the policy.
  3. There is a policy file that is compiled into the kernel that is
     parsed and enforced on initialization.

The first option has problems: the kernel reading files from userspace
is typically discouraged and very uncommon in the kernel.

The second option also has problems: Linux supports a variety of bootloaders
across its entire ecosystem - every bootloader would have to support this
new methodology or there must be an independent source. It would likely
result in more drastic changes to the kernel startup than necessary.

The third option is the best but it's important to be aware that the policy
will take disk space against the kernel it's compiled in. It's important to
keep this policy generalized enough that userspace can load a new, more
complicated policy, but restrictive enough that it will not overauthorize
and cause security issues.

The initramfs provides a way that this bootup path can be established. The
kernel starts with a minimal policy, that trusts the initramfs only. Inside
the initramfs, when the real rootfs is mounted, but not yet transferred to,
it deploys and activates a policy that trusts the new root filesystem.
This prevents overauthorization at any step, and keeps the kernel policy
to a minimal size.

Startup
^^^^^^^

Not every system, however starts with an initramfs, so the startup policy
compiled into the kernel will need some flexibility to express how trust
is established for the next phase of the bootup. To this end, if we just
make the compiled-in policy a full IPE policy, it allows system builders
to express the first stage bootup requirements appropriately.

Updatable, Rebootless Policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As requirements change over time (vulnerabilities are found in previously
trusted applications, keys roll, etcetera). Updating a kernel to change the
meet those security goals is not always a suitable option, as updates are not
always risk-free, and blocking a security update leaves systems vulnerable.
This means IPE requires a policy that can be completely updated (allowing
revocations of existing policy) from a source external to the kernel (allowing
policies to be updated without updating the kernel).

Additionally, since the kernel is stateless between invocations, and reading
policy files off the disk from kernel space is a bad idea(tm), then the
policy updates have to be done rebootlessly.

To allow an update from an external source, it could be potentially malicious,
so this policy needs to have a way to be identified as trusted. This is
done via a signature chained to a trust source in the kernel. Arbitrarily,
this is  the ``SYSTEM_TRUSTED_KEYRING``, a keyring that is initially
populated at kernel compile-time, as this matches the expectation that the
author of the compiled-in policy described above is the same entity that can
deploy policy updates.

Anti-Rollback / Anti-Replay
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Over time, vulnerabilities are found and trusted resources may not be
trusted anymore. IPE's policy has no exception to this. There can be
instances where a mistaken policy author deploys an insecure policy,
before correcting it with a secure policy.

Assuming that as soon as the insecure policy is signed, and an attacker
acquires the insecure policy, IPE needs a way to prevent rollback
from the secure policy update to the insecure policy update.

Initially, IPE's policy can have a policy_version that states the
minimum required version across all policies that can be active on
the system. This will prevent rollback while the system is live.

.. WARNING::

  However, since the kernel is stateless across boots, this policy
  version will be reset to 0.0.0 on the next boot. System builders
  need to be aware of this, and ensure the new secure policies are
  deployed ASAP after a boot to ensure that the window of
  opportunity is minimal for an attacker to deploy the insecure policy.

Implicit Actions:
~~~~~~~~~~~~~~~~~

The issue of implicit actions only becomes visible when you consider
a mixed level of security bars across multiple operations in a system.
For example, consider a system that has strong integrity guarantees
over both the executable code, and specific *data files* on the system,
that were critical to its function. In this system, three types of policies
are possible:

  1. A policy in which failure to match any rules in the policy results
     in the action being denied.
  2. A policy in which failure to match any rules in the policy results
     in the action being allowed.
  3. A policy in which the action taken when no rules are matched is
     specified by the policy author.

The first option could make a policy like this::

  op=EXECUTE integrity_verified=YES action=ALLOW

In the example system, this works well for the executables, as all
executables should have integrity guarantees, without exception. The
issue becomes with the second requirement about specific data files.
This would result in a policy like this (assuming each line is
evaluated in order)::

  op=EXECUTE integrity_verified=YES action=ALLOW

  op=READ integrity_verified=NO label=critical_t action=DENY
  op=READ action=ALLOW

This is somewhat clear if you read the docs, understand the policy
is executed in order and that the default is a denial; however, the
last line effectively changes that default to an ALLOW. This is
required, because in a realistic system, there are some unverified
reads (imagine appending to a log file).

The second option, matching no rules results in an allow, is clearer
for the specific data files::

  op=READ integrity_verified=NO label=critical_t action=DENY

And, like the first option, falls short with the execution scenario,
effectively needing to override the default::

  op=EXECUTE integrity_verified=YES action=ALLOW
  op=EXECUTE action=DENY

  op=READ integrity_verified=NO label=critical_t action=DENY

This leaves the third option. Instead of making users be clever
and override the default with an empty rule, force the end-user
to consider what the appropriate default should be for their
scenario and explicitly state it::

  DEFAULT op=EXECUTE action=DENY
  op=EXECUTE integrity_verified=YES action=ALLOW

  DEFAULT op=READ action=ALLOW
  op=READ integrity_verified=NO label=critical_t action=DENY

Policy Debugging:
~~~~~~~~~~~~~~~~~

When developing a policy, it is useful to know what line of the policy
is being violated to reduce debugging costs; narrowing the scope of the
investigation to the exact line that resulted in the action. Some integrity
policy systems do not provide this information, instead providing the
information that was used in the evaluation. This then requires a correlation
with the policy to evaluate what went wrong.

Instead, IPE just emits the rule that was matched. This limits the scope
of the investigation to the exact policy line (in the case of a specific
rule), or the section (in the case of a DEFAULT). This decreases iteration
and investigation times when policy failures are observed while evaluating
policies.

IPE's policy engine is also designed in a way that it makes it obvious to
a human of how to investigate a policy failure. Each line is evaluated in
the sequence that is written, so the algorithm is very simple to follow
for humans to recreate the steps and could have caused the failure. In other
surveyed systems, optimizations occur (sorting rules, for instance) when loading
the policy. In those systems, it requires multiple steps to debug, and the
algorithm may not always be clear to the end-user without reading the code first.

Simplified Policy:
~~~~~~~~~~~~~~~~~~

Finally, IPE's policy is designed for sysadmins, not kernel developers. Instead
of covering individual LSM hooks (or syscalls), IPE covers operations. This means
instead of sysadmins needing to know that the syscalls ``mmap``, ``mprotect``,
``execve``, and ``uselib`` must have rules protecting them, they must simple know
that they want to restrict code execution. This limits the amount of bypasses that
could occur due to a lack of knowledge of the underlying system; whereas the
maintainers of IPE, being kernel developers can make the correct choice to determine
whether something maps to these operations, and under what conditions.

Implementation Notes
--------------------

Anonymous Memory
~~~~~~~~~~~~~~~~

Anonymous memory isn't treated any differently from any other access in IPE.
When anonymous memory is mapped with ``+X``, it still comes into the ``file_mmap``
or ``file_mprotect`` hook, but with a ``NULL`` file object. This is submitted to
the evaluation, like any other file. However, all current trust properties will
evaluate to false, as they are all file-based and the operation is not
associated with a file.

.. WARNING::

  This also occurs with the ``kernel_load_data`` hook, when the kernel is
  loading data from a userspace buffer that is not backed by a file. In this
  scenario all current trust properties will also evaluate to false.

Securityfs Interface
~~~~~~~~~~~~~~~~~~~~

The per-policy securityfs tree is somewhat unique. For example, for
a standard securityfs policy tree::

  MyPolicy
    |- active
    |- delete
    |- name
    |- pkcs7
    |- policy
    |- update
    |- version

The policy is stored in the ``->i_private`` data of the MyPolicy inode.

Tests
-----

IPE has KUnit Tests for the policy parser. Recommended kunitconfig::

  CONFIG_KUNIT=y
  CONFIG_SECURITY=y
  CONFIG_SECURITYFS=y
  CONFIG_PKCS7_MESSAGE_PARSER=y
  CONFIG_SYSTEM_DATA_VERIFICATION=y
  CONFIG_FS_VERITY=y
  CONFIG_FS_VERITY_BUILTIN_SIGNATURES=y
  CONFIG_BLOCK=y
  CONFIG_MD=y
  CONFIG_BLK_DEV_DM=y
  CONFIG_DM_VERITY=y
  CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG=y
  CONFIG_NET=y
  CONFIG_AUDIT=y
  CONFIG_AUDITSYSCALL=y
  CONFIG_BLK_DEV_INITRD=y

  CONFIG_SECURITY_IPE=y
  CONFIG_IPE_PROP_DM_VERITY=y
  CONFIG_IPE_PROP_DM_VERITY_SIGNATURE=y
  CONFIG_IPE_PROP_FS_VERITY=y
  CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG=y
  CONFIG_SECURITY_IPE_KUNIT_TEST=y

In addition, IPE has a python based integration
`test suite <https://github.com/microsoft/ipe/tree/test-suite>`_ that
can test both user interfaces and enforcement functionalities.

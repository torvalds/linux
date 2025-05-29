.. SPDX-License-Identifier: GPL-2.0

Integrity Policy Enforcement (IPE)
==================================

.. NOTE::

   This is the documentation for admins, system builders, or individuals
   attempting to use IPE. If you're looking for more developer-focused
   documentation about IPE please see :doc:`the design docs </security/ipe>`.

Overview
--------

Integrity Policy Enforcement (IPE) is a Linux Security Module that takes a
complementary approach to access control. Unlike traditional access control
mechanisms that rely on labels and paths for decision-making, IPE focuses
on the immutable security properties inherent to system components. These
properties are fundamental attributes or features of a system component
that cannot be altered, ensuring a consistent and reliable basis for
security decisions.

To elaborate, in the context of IPE, system components primarily refer to
files or the devices these files reside on. However, this is just a
starting point. The concept of system components is flexible and can be
extended to include new elements as the system evolves. The immutable
properties include the origin of a file, which remains constant and
unchangeable over time. For example, IPE policies can be crafted to trust
files originating from the initramfs. Since initramfs is typically verified
by the bootloader, its files are deemed trustworthy; "file is from
initramfs" becomes an immutable property under IPE's consideration.

The immutable property concept extends to the security features enabled on
a file's origin, such as dm-verity or fs-verity, which provide a layer of
integrity and trust. For example, IPE allows the definition of policies
that trust files from a dm-verity protected device. dm-verity ensures the
integrity of an entire device by providing a verifiable and immutable state
of its contents. Similarly, fs-verity offers filesystem-level integrity
checks, allowing IPE to enforce policies that trust files protected by
fs-verity. These two features cannot be turned off once established, so
they are considered immutable properties. These examples demonstrate how
IPE leverages immutable properties, such as a file's origin and its
integrity protection mechanisms, to make access control decisions.

For the IPE policy, specifically, it grants the ability to enforce
stringent access controls by assessing security properties against
reference values defined within the policy. This assessment can be based on
the existence of a security property (e.g., verifying if a file originates
from initramfs) or evaluating the internal state of an immutable security
property. The latter includes checking the roothash of a dm-verity
protected device, determining whether dm-verity possesses a valid
signature, assessing the digest of a fs-verity protected file, or
determining whether fs-verity possesses a valid built-in signature. This
nuanced approach to policy enforcement enables a highly secure and
customizable system defense mechanism, tailored to specific security
requirements and trust models.

To enable IPE, ensure that ``CONFIG_SECURITY_IPE`` (under
:menuselection:`Security -> Integrity Policy Enforcement (IPE)`) config
option is enabled.

Use Cases
---------

IPE works best in fixed-function devices: devices in which their purpose
is clearly defined and not supposed to be changed (e.g. network firewall
device in a data center, an IoT device, etcetera), where all software and
configuration is built and provisioned by the system owner.

IPE is a long-way off for use in general-purpose computing: the Linux
community as a whole tends to follow a decentralized trust model (known as
the web of trust), which IPE has no support for it yet. Instead, IPE
supports PKI (public key infrastructure), which generally designates a
set of trusted entities that provide a measure of absolute trust.

Additionally, while most packages are signed today, the files inside
the packages (for instance, the executables), tend to be unsigned. This
makes it difficult to utilize IPE in systems where a package manager is
expected to be functional, without major changes to the package manager
and ecosystem behind it.

The digest_cache LSM [#digest_cache_lsm]_ is a system that when combined with IPE,
could be used to enable and support general-purpose computing use cases.

Known Limitations
-----------------

IPE cannot verify the integrity of anonymous executable memory, such as
the trampolines created by gcc closures and libffi (<3.4.2), or JIT'd code.
Unfortunately, as this is dynamically generated code, there is no way
for IPE to ensure the integrity of this code to form a trust basis.

IPE cannot verify the integrity of programs written in interpreted
languages when these scripts are invoked by passing these program files
to the interpreter. This is because the way interpreters execute these
files; the scripts themselves are not evaluated as executable code
through one of IPE's hooks, but they are merely text files that are read
(as opposed to compiled executables) [#interpreters]_.

Threat Model
------------

IPE specifically targets the risk of tampering with user-space executable
code after the kernel has initially booted, including the kernel modules
loaded from userspace via ``modprobe`` or ``insmod``.

To illustrate, consider a scenario where an untrusted binary, possibly
malicious, is downloaded along with all necessary dependencies, including a
loader and libc. The primary function of IPE in this context is to prevent
the execution of such binaries and their dependencies.

IPE achieves this by verifying the integrity and authenticity of all
executable code before allowing them to run. It conducts a thorough
check to ensure that the code's integrity is intact and that they match an
authorized reference value (digest, signature, etc) as per the defined
policy. If a binary does not pass this verification process, either
because its integrity has been compromised or it does not meet the
authorization criteria, IPE will deny its execution. Additionally, IPE
generates audit logs which may be utilized to detect and analyze failures
resulting from policy violation.

Tampering threat scenarios include modification or replacement of
executable code by a range of actors including:

-  Actors with physical access to the hardware
-  Actors with local network access to the system
-  Actors with access to the deployment system
-  Compromised internal systems under external control
-  Malicious end users of the system
-  Compromised end users of the system
-  Remote (external) compromise of the system

IPE does not mitigate threats arising from malicious but authorized
developers (with access to a signing certificate), or compromised
developer tools used by them (i.e. return-oriented programming attacks).
Additionally, IPE draws hard security boundary between userspace and
kernelspace. As a result, kernel-level exploits are considered outside
the scope of IPE and mitigation is left to other mechanisms.

Policy
------

IPE policy is a plain-text [#devdoc]_ policy composed of multiple statements
over several lines. There is one required line, at the top of the
policy, indicating the policy name, and the policy version, for
instance::

   policy_name=Ex_Policy policy_version=0.0.0

The policy name is a unique key identifying this policy in a human
readable name. This is used to create nodes under securityfs as well as
uniquely identify policies to deploy new policies vs update existing
policies.

The policy version indicates the current version of the policy (NOT the
policy syntax version). This is used to prevent rollback of policy to
potentially insecure previous versions of the policy.

The next portion of IPE policy are rules. Rules are formed by key=value
pairs, known as properties. IPE rules require two properties: ``action``,
which determines what IPE does when it encounters a match against the
rule, and ``op``, which determines when the rule should be evaluated.
The ordering is significant, a rule must start with ``op``, and end with
``action``. Thus, a minimal rule is::

   op=EXECUTE action=ALLOW

This example will allow any execution. Additional properties are used to
assess immutable security properties about the files being evaluated.
These properties are intended to be descriptions of systems within the
kernel that can provide a measure of integrity verification, such that IPE
can determine the trust of the resource based on the value of the property.

Rules are evaluated top-to-bottom. As a result, any revocation rules,
or denies should be placed early in the file to ensure that these rules
are evaluated before a rule with ``action=ALLOW``.

IPE policy supports comments. The character '#' will function as a
comment, ignoring all characters to the right of '#' until the newline.

The default behavior of IPE evaluations can also be expressed in policy,
through the ``DEFAULT`` statement. This can be done at a global level,
or a per-operation level::

   # Global
   DEFAULT action=ALLOW

   # Operation Specific
   DEFAULT op=EXECUTE action=ALLOW

A default must be set for all known operations in IPE. If you want to
preserve older policies being compatible with newer kernels that can introduce
new operations, set a global default of ``ALLOW``, then override the
defaults on a per-operation basis (as above).

With configurable policy-based LSMs, there's several issues with
enforcing the configurable policies at startup, around reading and
parsing the policy:

1. The kernel *should* not read files from userspace, so directly reading
   the policy file is prohibited.
2. The kernel command line has a character limit, and one kernel module
   should not reserve the entire character limit for its own
   configuration.
3. There are various boot loaders in the kernel ecosystem, so handing
   off a memory block would be costly to maintain.

As a result, IPE has addressed this problem through a concept of a "boot
policy". A boot policy is a minimal policy which is compiled into the
kernel. This policy is intended to get the system to a state where
userspace is set up and ready to receive commands, at which point a more
complex policy can be deployed via securityfs. The boot policy can be
specified via ``SECURITY_IPE_BOOT_POLICY`` config option, which accepts
a path to a plain-text version of the IPE policy to apply. This policy
will be compiled into the kernel. If not specified, IPE will be disabled
until a policy is deployed and activated through securityfs.

Deploying Policies
~~~~~~~~~~~~~~~~~~

Policies can be deployed from userspace through securityfs. These policies
are signed through the PKCS#7 message format to enforce some level of
authorization of the policies (prohibiting an attacker from gaining
unconstrained root, and deploying an "allow all" policy). These
policies must be signed by a certificate that chains to the
``SYSTEM_TRUSTED_KEYRING``, or to the secondary and/or platform keyrings if
``CONFIG_IPE_POLICY_SIG_SECONDARY_KEYRING`` and/or
``CONFIG_IPE_POLICY_SIG_PLATFORM_KEYRING`` are enabled, respectively.
With openssl, the policy can be signed by::

   openssl smime -sign \
      -in "$MY_POLICY" \
      -signer "$MY_CERTIFICATE" \
      -inkey "$MY_PRIVATE_KEY" \
      -noattr \
      -nodetach \
      -nosmimecap \
      -outform der \
      -out "$MY_POLICY.p7b"

Deploying the policies is done through securityfs, through the
``new_policy`` node. To deploy a policy, simply cat the file into the
securityfs node::

   cat "$MY_POLICY.p7b" > /sys/kernel/security/ipe/new_policy

Upon success, this will create one subdirectory under
``/sys/kernel/security/ipe/policies/``. The subdirectory will be the
``policy_name`` field of the policy deployed, so for the example above,
the directory will be ``/sys/kernel/security/ipe/policies/Ex_Policy``.
Within this directory, there will be seven files: ``pkcs7``, ``policy``,
``name``, ``version``, ``active``, ``update``, and ``delete``.

The ``pkcs7`` file is read-only. Reading it returns the raw PKCS#7 data
that was provided to the kernel, representing the policy. If the policy being
read is the boot policy, this will return ``ENOENT``, as it is not signed.

The ``policy`` file is read only. Reading it returns the PKCS#7 inner
content of the policy, which will be the plain text policy.

The ``active`` file is used to set a policy as the currently active policy.
This file is rw, and accepts a value of ``"1"`` to set the policy as active.
Since only a single policy can be active at one time, all other policies
will be marked inactive. The policy being marked active must have a policy
version greater or equal to the currently-running version.

The ``update`` file is used to update a policy that is already present
in the kernel. This file is write-only and accepts a PKCS#7 signed
policy. Two checks will always be performed on this policy: First, the
``policy_names`` must match with the updated version and the existing
version. Second the updated policy must have a policy version greater than
the currently-running version. This is to prevent rollback attacks.

The ``delete`` file is used to remove a policy that is no longer needed.
This file is write-only and accepts a value of ``1`` to delete the policy.
On deletion, the securityfs node representing the policy will be removed.
However, delete the current active policy is not allowed and will return
an operation not permitted error.

Similarly, writing to both ``update`` and ``new_policy`` could result in
bad message(policy syntax error) or file exists error. The latter error happens
when trying to deploy a policy with a ``policy_name`` while the kernel already
has a deployed policy with the same ``policy_name``.

Deploying a policy will *not* cause IPE to start enforcing the policy. IPE will
only enforce the policy marked active. Note that only one policy can be active
at a time.

Once deployment is successful, the policy can be activated, by writing file
``/sys/kernel/security/ipe/policies/$policy_name/active``.
For example, the ``Ex_Policy`` can be activated by::

   echo 1 > "/sys/kernel/security/ipe/policies/Ex_Policy/active"

From above point on, ``Ex_Policy`` is now the enforced policy on the
system.

IPE also provides a way to delete policies. This can be done via the
``delete`` securityfs node,
``/sys/kernel/security/ipe/policies/$policy_name/delete``.
Writing ``1`` to that file deletes the policy::

   echo 1 > "/sys/kernel/security/ipe/policies/$policy_name/delete"

There is only one requirement to delete a policy: the policy being deleted
must be inactive.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack), all
   writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Modes
~~~~~

IPE supports two modes of operation: permissive (similar to SELinux's
permissive mode) and enforced. In permissive mode, all events are
checked and policy violations are logged, but the policy is not really
enforced. This allows users to test policies before enforcing them.

The default mode is enforce, and can be changed via the kernel command
line parameter ``ipe.enforce=(0|1)``, or the securityfs node
``/sys/kernel/security/ipe/enforce``.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack, etcetera),
   all writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Audit Events
~~~~~~~~~~~~

1420 AUDIT_IPE_ACCESS
^^^^^^^^^^^^^^^^^^^^^
Event Examples::

   type=1420 audit(1653364370.067:61): ipe_op=EXECUTE ipe_hook=MMAP enforcing=1 pid=2241 comm="ld-linux.so" path="/deny/lib/libc.so.6" dev="sda2" ino=14549020 rule="DEFAULT action=DENY"
   type=1300 audit(1653364370.067:61): SYSCALL arch=c000003e syscall=9 success=no exit=-13 a0=7f1105a28000 a1=195000 a2=5 a3=812 items=0 ppid=2219 pid=2241 auid=0 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=0 fsgid=0 tty=pts0 ses=2 comm="ld-linux.so" exe="/tmp/ipe-test/lib/ld-linux.so" subj=unconfined key=(null)
   type=1327 audit(1653364370.067:61): 707974686F6E3300746573742F6D61696E2E7079002D6E00

   type=1420 audit(1653364735.161:64): ipe_op=EXECUTE ipe_hook=MMAP enforcing=1 pid=2472 comm="mmap_test" path=? dev=? ino=? rule="DEFAULT action=DENY"
   type=1300 audit(1653364735.161:64): SYSCALL arch=c000003e syscall=9 success=no exit=-13 a0=0 a1=1000 a2=4 a3=21 items=0 ppid=2219 pid=2472 auid=0 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=0 fsgid=0 tty=pts0 ses=2 comm="mmap_test" exe="/root/overlake_test/upstream_test/vol_fsverity/bin/mmap_test" subj=unconfined key=(null)
   type=1327 audit(1653364735.161:64): 707974686F6E3300746573742F6D61696E2E7079002D6E00

This event indicates that IPE made an access control decision; the IPE
specific record (1420) is always emitted in conjunction with a
``AUDITSYSCALL`` record.

Determining whether IPE is in permissive or enforced mode can be derived
from ``success`` property and exit code of the ``AUDITSYSCALL`` record.


Field descriptions:

+-----------+------------+-----------+---------------------------------------------------------------------------------+
| Field     | Value Type | Optional? | Description of Value                                                            |
+===========+============+===========+=================================================================================+
| ipe_op    | string     | No        | The IPE operation name associated with the log                                  |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| ipe_hook  | string     | No        | The name of the LSM hook that triggered the IPE event                           |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| enforcing | integer    | No        | The current IPE enforcing state 1 is in enforcing mode, 0 is in permissive mode |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| pid       | integer    | No        | The pid of the process that triggered the IPE event.                            |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| comm      | string     | No        | The command line program name of the process that triggered the IPE event       |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| path      | string     | Yes       | The absolute path to the evaluated file                                         |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| ino       | integer    | Yes       | The inode number of the evaluated file                                          |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| dev       | string     | Yes       | The device name of the evaluated file, e.g. vda                                 |
+-----------+------------+-----------+---------------------------------------------------------------------------------+
| rule      | string     | No        | The matched policy rule                                                         |
+-----------+------------+-----------+---------------------------------------------------------------------------------+

1421 AUDIT_IPE_CONFIG_CHANGE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Event Example::

   type=1421 audit(1653425583.136:54): old_active_pol_name="Allow_All" old_active_pol_version=0.0.0 old_policy_digest=sha256:E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 new_active_pol_name="boot_verified" new_active_pol_version=0.0.0 new_policy_digest=sha256:820EEA5B40CA42B51F68962354BA083122A20BB846F26765076DD8EED7B8F4DB auid=4294967295 ses=4294967295 lsm=ipe res=1
   type=1300 audit(1653425583.136:54): SYSCALL arch=c000003e syscall=1 success=yes exit=2 a0=3 a1=5596fcae1fb0 a2=2 a3=2 items=0 ppid=184 pid=229 auid=4294967295 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=0 fsgid=0 tty=pts0 ses=4294967295 comm="python3" exe="/usr/bin/python3.10" key=(null)
   type=1327 audit(1653425583.136:54): PROCTITLE proctitle=707974686F6E3300746573742F6D61696E2E7079002D66002E2

This event indicates that IPE switched the active poliy from one to another
along with the version and the hash digest of the two policies.
Note IPE can only have one policy active at a time, all access decision
evaluation is based on the current active policy.
The normal procedure to deploy a new policy is loading the policy to deploy
into the kernel first, then switch the active policy to it.

This record will always be emitted in conjunction with a ``AUDITSYSCALL`` record for the ``write`` syscall.

Field descriptions:

+------------------------+------------+-----------+---------------------------------------------------+
| Field                  | Value Type | Optional? | Description of Value                              |
+========================+============+===========+===================================================+
| old_active_pol_name    | string     | Yes       | The name of previous active policy                |
+------------------------+------------+-----------+---------------------------------------------------+
| old_active_pol_version | string     | Yes       | The version of previous active policy             |
+------------------------+------------+-----------+---------------------------------------------------+
| old_policy_digest      | string     | Yes       | The hash of previous active policy                |
+------------------------+------------+-----------+---------------------------------------------------+
| new_active_pol_name    | string     | No        | The name of current active policy                 |
+------------------------+------------+-----------+---------------------------------------------------+
| new_active_pol_version | string     | No        | The version of current active policy              |
+------------------------+------------+-----------+---------------------------------------------------+
| new_policy_digest      | string     | No        | The hash of current active policy                 |
+------------------------+------------+-----------+---------------------------------------------------+
| auid                   | integer    | No        | The login user ID                                 |
+------------------------+------------+-----------+---------------------------------------------------+
| ses                    | integer    | No        | The login session ID                              |
+------------------------+------------+-----------+---------------------------------------------------+
| lsm                    | string     | No        | The lsm name associated with the event            |
+------------------------+------------+-----------+---------------------------------------------------+
| res                    | integer    | No        | The result of the audited operation(success/fail) |
+------------------------+------------+-----------+---------------------------------------------------+

1422 AUDIT_IPE_POLICY_LOAD
^^^^^^^^^^^^^^^^^^^^^^^^^^

Event Example::

   type=1422 audit(1653425529.927:53): policy_name="boot_verified" policy_version=0.0.0 policy_digest=sha256:820EEA5B40CA42B51F68962354BA083122A20BB846F26765076DD8EED7B8F4DB auid=4294967295 ses=4294967295 lsm=ipe res=1 errno=0
   type=1300 audit(1653425529.927:53): arch=c000003e syscall=1 success=yes exit=2567 a0=3 a1=5596fcae1fb0 a2=a07 a3=2 items=0 ppid=184 pid=229 auid=4294967295 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=0 fsgid=0 tty=pts0 ses=4294967295 comm="python3" exe="/usr/bin/python3.10" key=(null)
   type=1327 audit(1653425529.927:53): PROCTITLE proctitle=707974686F6E3300746573742F6D61696E2E7079002D66002E2E

This record indicates a new policy has been loaded into the kernel with the policy name, policy version and policy hash.

This record will always be emitted in conjunction with a ``AUDITSYSCALL`` record for the ``write`` syscall.

Field descriptions:

+----------------+------------+-----------+-------------------------------------------------------------+
| Field          | Value Type | Optional? | Description of Value                                        |
+================+============+===========+=============================================================+
| policy_name    | string     | Yes       | The policy_name                                             |
+----------------+------------+-----------+-------------------------------------------------------------+
| policy_version | string     | Yes       | The policy_version                                          |
+----------------+------------+-----------+-------------------------------------------------------------+
| policy_digest  | string     | Yes       | The policy hash                                             |
+----------------+------------+-----------+-------------------------------------------------------------+
| auid           | integer    | No        | The login user ID                                           |
+----------------+------------+-----------+-------------------------------------------------------------+
| ses            | integer    | No        | The login session ID                                        |
+----------------+------------+-----------+-------------------------------------------------------------+
| lsm            | string     | No        | The lsm name associated with the event                      |
+----------------+------------+-----------+-------------------------------------------------------------+
| res            | integer    | No        | The result of the audited operation(success/fail)           |
+----------------+------------+-----------+-------------------------------------------------------------+
| errno          | integer    | No        | Error code from policy loading operations (see table below) |
+----------------+------------+-----------+-------------------------------------------------------------+

Policy error codes (errno):

The following table lists the error codes that may appear in the errno field while loading or updating the policy:

+----------------+--------------------------------------------------------+
| Error Code     | Description                                            |
+================+========================================================+
| 0              | Success                                                |
+----------------+--------------------------------------------------------+
| -EPERM         | Insufficient permission                                |
+----------------+--------------------------------------------------------+
| -EEXIST        | Same name policy already deployed                      |
+----------------+--------------------------------------------------------+
| -EBADMSG       | Policy is invalid                                      |
+----------------+--------------------------------------------------------+
| -ENOMEM        | Out of memory (OOM)                                    |
+----------------+--------------------------------------------------------+
| -ERANGE        | Policy version number overflow                         |
+----------------+--------------------------------------------------------+
| -EINVAL        | Policy version parsing error                           |
+----------------+--------------------------------------------------------+
| -ENOKEY        | Key used to sign the IPE policy not found in keyring   |
+----------------+--------------------------------------------------------+
| -EKEYREJECTED  | Policy signature verification failed                   |
+----------------+--------------------------------------------------------+
| -ESTALE        | Attempting to update an IPE policy with older version  |
+----------------+--------------------------------------------------------+
| -ENOENT        | Policy was deleted while updating                      |
+----------------+--------------------------------------------------------+

1404 AUDIT_MAC_STATUS
^^^^^^^^^^^^^^^^^^^^^

Event Examples::

   type=1404 audit(1653425689.008:55): enforcing=0 old_enforcing=1 auid=4294967295 ses=4294967295 enabled=1 old-enabled=1 lsm=ipe res=1
   type=1300 audit(1653425689.008:55): arch=c000003e syscall=1 success=yes exit=2 a0=1 a1=55c1065e5c60 a2=2 a3=0 items=0 ppid=405 pid=441 auid=0 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=)
   type=1327 audit(1653425689.008:55): proctitle="-bash"

   type=1404 audit(1653425689.008:55): enforcing=1 old_enforcing=0 auid=4294967295 ses=4294967295 enabled=1 old-enabled=1 lsm=ipe res=1
   type=1300 audit(1653425689.008:55): arch=c000003e syscall=1 success=yes exit=2 a0=1 a1=55c1065e5c60 a2=2 a3=0 items=0 ppid=405 pid=441 auid=0 uid=0 gid=0 euid=0 suid=0 fsuid=0 egid=0 sgid=)
   type=1327 audit(1653425689.008:55): proctitle="-bash"

This record will always be emitted in conjunction with a ``AUDITSYSCALL`` record for the ``write`` syscall.

Field descriptions:

+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| Field         | Value Type | Optional? | Description of Value                                                                            |
+===============+============+===========+=================================================================================================+
| enforcing     | integer    | No        | The enforcing state IPE is being switched to, 1 is in enforcing mode, 0 is in permissive mode   |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| old_enforcing | integer    | No        | The enforcing state IPE is being switched from, 1 is in enforcing mode, 0 is in permissive mode |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| auid          | integer    | No        | The login user ID                                                                               |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| ses           | integer    | No        | The login session ID                                                                            |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| enabled       | integer    | No        | The new TTY audit enabled setting                                                               |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| old-enabled   | integer    | No        | The old TTY audit enabled setting                                                               |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| lsm           | string     | No        | The lsm name associated with the event                                                          |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+
| res           | integer    | No        | The result of the audited operation(success/fail)                                               |
+---------------+------------+-----------+-------------------------------------------------------------------------------------------------+


Success Auditing
^^^^^^^^^^^^^^^^

IPE supports success auditing. When enabled, all events that pass IPE
policy and are not blocked will emit an audit event. This is disabled by
default, and can be enabled via the kernel command line
``ipe.success_audit=(0|1)`` or
``/sys/kernel/security/ipe/success_audit`` securityfs file.

This is *very* noisy, as IPE will check every userspace binary on the
system, but is useful for debugging policies.

.. NOTE::

   If a traditional MAC system is enabled (SELinux, apparmor, smack, etcetera),
   all writes to ipe's securityfs nodes require ``CAP_MAC_ADMIN``.

Properties
----------

As explained above, IPE properties are ``key=value`` pairs expressed in IPE
policy. Two properties are built-into the policy parser: 'op' and 'action'.
The other properties are used to restrict immutable security properties
about the files being evaluated. Currently those properties are:
'``boot_verified``', '``dmverity_signature``', '``dmverity_roothash``',
'``fsverity_signature``', '``fsverity_digest``'. A description of all
properties supported by IPE are listed below:

op
~~

Indicates the operation for a rule to apply to. Must be in every rule,
as the first token. IPE supports the following operations:

   ``EXECUTE``

      Pertains to any file attempting to be executed, or loaded as an
      executable.

   ``FIRMWARE``:

      Pertains to firmware being loaded via the firmware_class interface.
      This covers both the preallocated buffer and the firmware file
      itself.

   ``KMODULE``:

      Pertains to loading kernel modules via ``modprobe`` or ``insmod``.

   ``KEXEC_IMAGE``:

      Pertains to kernel images loading via ``kexec``.

   ``KEXEC_INITRAMFS``

      Pertains to initrd images loading via ``kexec --initrd``.

   ``POLICY``:

      Controls loading policies via reading a kernel-space initiated read.

      An example of such is loading IMA policies by writing the path
      to the policy file to ``$securityfs/ima/policy``

   ``X509_CERT``:

      Controls loading IMA certificates through the Kconfigs,
      ``CONFIG_IMA_X509_PATH`` and ``CONFIG_EVM_X509_PATH``.

action
~~~~~~

   Determines what IPE should do when a rule matches. Must be in every
   rule, as the final clause. Can be one of:

   ``ALLOW``:

      If the rule matches, explicitly allow access to the resource to proceed
      without executing any more rules.

   ``DENY``:

      If the rule matches, explicitly prohibit access to the resource to
      proceed without executing any more rules.

boot_verified
~~~~~~~~~~~~~

   This property can be utilized for authorization of files from initramfs.
   The format of this property is::

         boot_verified=(TRUE|FALSE)


   .. WARNING::

      This property will trust files from initramfs(rootfs). It should
      only be used during early booting stage. Before mounting the real
      rootfs on top of the initramfs, initramfs script will recursively
      remove all files and directories on the initramfs. This is typically
      implemented by using switch_root(8) [#switch_root]_. Therefore the
      initramfs will be empty and not accessible after the real
      rootfs takes over. It is advised to switch to a different policy
      that doesn't rely on the property after this point.
      This ensures that the trust policies remain relevant and effective
      throughout the system's operation.

dmverity_roothash
~~~~~~~~~~~~~~~~~

   This property can be utilized for authorization or revocation of
   specific dm-verity volumes, identified via their root hashes. It has a
   dependency on the DM_VERITY module. This property is controlled by
   the ``IPE_PROP_DM_VERITY`` config option, it will be automatically
   selected when ``SECURITY_IPE`` and ``DM_VERITY`` are all enabled.
   The format of this property is::

      dmverity_roothash=DigestName:HexadecimalString

   The supported DigestNames for dmverity_roothash are [#dmveritydigests]_

      + blake2b-512
      + blake2s-256
      + sha256
      + sha384
      + sha512
      + sha3-224
      + sha3-256
      + sha3-384
      + sha3-512
      + sm3
      + rmd160

dmverity_signature
~~~~~~~~~~~~~~~~~~

   This property can be utilized for authorization of all dm-verity
   volumes that have a signed roothash that validated by a keyring
   specified by dm-verity's configuration, either the system trusted
   keyring, or the secondary keyring. It depends on
   ``DM_VERITY_VERIFY_ROOTHASH_SIG`` config option and is controlled by
   the ``IPE_PROP_DM_VERITY_SIGNATURE`` config option, it will be automatically
   selected when ``SECURITY_IPE``, ``DM_VERITY`` and
   ``DM_VERITY_VERIFY_ROOTHASH_SIG`` are all enabled.
   The format of this property is::

      dmverity_signature=(TRUE|FALSE)

fsverity_digest
~~~~~~~~~~~~~~~

   This property can be utilized for authorization of specific fsverity
   enabled files, identified via their fsverity digests.
   It depends on ``FS_VERITY`` config option and is controlled by
   the ``IPE_PROP_FS_VERITY`` config option, it will be automatically
   selected when ``SECURITY_IPE`` and ``FS_VERITY`` are all enabled.
   The format of this property is::

      fsverity_digest=DigestName:HexadecimalString

   The supported DigestNames for fsverity_digest are [#fsveritydigest]_

      + sha256
      + sha512

fsverity_signature
~~~~~~~~~~~~~~~~~~

   This property is used to authorize all fs-verity enabled files that have
   been verified by fs-verity's built-in signature mechanism. The signature
   verification relies on a key stored within the ".fs-verity" keyring. It
   depends on ``FS_VERITY_BUILTIN_SIGNATURES`` config option and
   it is controlled by the ``IPE_PROP_FS_VERITY`` config option,
   it will be automatically selected when ``SECURITY_IPE``, ``FS_VERITY``
   and ``FS_VERITY_BUILTIN_SIGNATURES`` are all enabled.
   The format of this property is::

      fsverity_signature=(TRUE|FALSE)

Policy Examples
---------------

Allow all
~~~~~~~~~

::

   policy_name=Allow_All policy_version=0.0.0
   DEFAULT action=ALLOW

Allow only initramfs
~~~~~~~~~~~~~~~~~~~~

::

   policy_name=Allow_Initramfs policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE boot_verified=TRUE action=ALLOW

Allow any signed and validated dm-verity volume and the initramfs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name=Allow_Signed_DMV_And_Initramfs policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE boot_verified=TRUE action=ALLOW
   op=EXECUTE dmverity_signature=TRUE action=ALLOW

Prohibit execution from a specific dm-verity volume
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name=Deny_DMV_By_Roothash policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE dmverity_roothash=sha256:cd2c5bae7c6c579edaae4353049d58eb5f2e8be0244bf05345bc8e5ed257baff action=DENY

   op=EXECUTE boot_verified=TRUE action=ALLOW
   op=EXECUTE dmverity_signature=TRUE action=ALLOW

Allow only a specific dm-verity volume
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name=Allow_DMV_By_Roothash policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE dmverity_roothash=sha256:401fcec5944823ae12f62726e8184407a5fa9599783f030dec146938 action=ALLOW

Allow any fs-verity file with a valid built-in signature
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name=Allow_Signed_And_Validated_FSVerity policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE fsverity_signature=TRUE action=ALLOW

Allow execution of a specific fs-verity file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   policy_name=ALLOW_FSV_By_Digest policy_version=0.0.0
   DEFAULT action=DENY

   op=EXECUTE fsverity_digest=sha256:fd88f2b8824e197f850bf4c5109bea5cf0ee38104f710843bb72da796ba5af9e action=ALLOW

Additional Information
----------------------

- `Github Repository <https://github.com/microsoft/ipe>`_
- :doc:`Developer and design docs for IPE </security/ipe>`

FAQ
---

Q:
   What's the difference between other LSMs which provide a measure of
   trust-based access control?

A:

   In general, there's two other LSMs that can provide similar functionality:
   IMA, and Loadpin.

   IMA and IPE are functionally very similar. The significant difference between
   the two is the policy. [#devdoc]_

   Loadpin and IPE differ fairly dramatically, as Loadpin only covers the IPE's
   kernel read operations, whereas IPE is capable of controlling execution
   on top of kernel read. The trust model is also different; Loadpin roots its
   trust in the initial super-block, whereas trust in IPE is stemmed from kernel
   itself (via ``SYSTEM_TRUSTED_KEYS``).

-----------

.. [#digest_cache_lsm] https://lore.kernel.org/lkml/20240415142436.2545003-1-roberto.sassu@huaweicloud.com/

.. [#interpreters] There is `some interest in solving this issue <https://lore.kernel.org/lkml/20220321161557.495388-1-mic@digikod.net/>`_.

.. [#devdoc] Please see :doc:`the design docs </security/ipe>` for more on
             this topic.

.. [#switch_root] https://man7.org/linux/man-pages/man8/switch_root.8.html

.. [#dmveritydigests] These hash algorithms are based on values accepted by
                      the Linux crypto API; IPE does not impose any
                      restrictions on the digest algorithm itself;
                      thus, this list may be out of date.

.. [#fsveritydigest] These hash algorithms are based on values accepted by the
                     kernel's fsverity support; IPE does not impose any
                     restrictions on the digest algorithm itself;
                     thus, this list may be out of date.

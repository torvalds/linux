.. SPDX-License-Identifier: GPL-2.0

Attack Vector Controls
======================

Attack vector controls provide a simple method to configure only the mitigations
for CPU vulnerabilities which are relevant given the intended use of a system.
Administrators are encouraged to consider which attack vectors are relevant and
disable all others in order to recoup system performance.

When new relevant CPU vulnerabilities are found, they will be added to these
attack vector controls so administrators will likely not need to reconfigure
their command line parameters as mitigations will continue to be correctly
applied based on the chosen attack vector controls.

Attack Vectors
--------------

There are 5 sets of attack-vector mitigations currently supported by the kernel:

#. :ref:`user_kernel`
#. :ref:`user_user`
#. :ref:`guest_host`
#. :ref:`guest_guest`
#. :ref:`smt`

To control the enabled attack vectors, see :ref:`cmdline`.

.. _user_kernel:

User-to-Kernel
^^^^^^^^^^^^^^

The user-to-kernel attack vector involves a malicious userspace program
attempting to leak kernel data into userspace by exploiting a CPU vulnerability.
The kernel data involved might be limited to certain kernel memory, or include
all memory in the system, depending on the vulnerability exploited.

If no untrusted userspace applications are being run, such as with single-user
systems, consider disabling user-to-kernel mitigations.

Note that the CPU vulnerabilities mitigated by Linux have generally not been
shown to be exploitable from browser-based sandboxes.  User-to-kernel
mitigations are therefore mostly relevant if unknown userspace applications may
be run by untrusted users.

*user-to-kernel mitigations are enabled by default*

.. _user_user:

User-to-User
^^^^^^^^^^^^

The user-to-user attack vector involves a malicious userspace program attempting
to influence the behavior of another unsuspecting userspace program in order to
exfiltrate data.  The vulnerability of a userspace program is based on the
program itself and the interfaces it provides.

If no untrusted userspace applications are being run, consider disabling
user-to-user mitigations.

Note that because the Linux kernel contains a mapping of all physical memory,
preventing a malicious userspace program from leaking data from another
userspace program requires mitigating user-to-kernel attacks as well for
complete protection.

*user-to-user mitigations are enabled by default*

.. _guest_host:

Guest-to-Host
^^^^^^^^^^^^^

The guest-to-host attack vector involves a malicious VM attempting to leak
hypervisor data into the VM.  The data involved may be limited, or may
potentially include all memory in the system, depending on the vulnerability
exploited.

If no untrusted VMs are being run, consider disabling guest-to-host mitigations.

*guest-to-host mitigations are enabled by default if KVM support is present*

.. _guest_guest:

Guest-to-Guest
^^^^^^^^^^^^^^

The guest-to-guest attack vector involves a malicious VM attempting to influence
the behavior of another unsuspecting VM in order to exfiltrate data.  The
vulnerability of a VM is based on the code inside the VM itself and the
interfaces it provides.

If no untrusted VMs, or only a single VM is being run, consider disabling
guest-to-guest mitigations.

Similar to the user-to-user attack vector, preventing a malicious VM from
leaking data from another VM requires mitigating guest-to-host attacks as well
due to the Linux kernel phys map.

*guest-to-guest mitigations are enabled by default if KVM support is present*

.. _smt:

Cross-Thread
^^^^^^^^^^^^

The cross-thread attack vector involves a malicious userspace program or
malicious VM either observing or attempting to influence the behavior of code
running on the SMT sibling thread in order to exfiltrate data.

Many cross-thread attacks can only be mitigated if SMT is disabled, which will
result in reduced CPU core count and reduced performance.

If cross-thread mitigations are fully enabled ('auto,nosmt'), all mitigations
for cross-thread attacks will be enabled.  SMT may be disabled depending on
which vulnerabilities are present in the CPU.

If cross-thread mitigations are partially enabled ('auto'), mitigations for
cross-thread attacks will be enabled but SMT will not be disabled.

If cross-thread mitigations are disabled, no mitigations for cross-thread
attacks will be enabled.

Cross-thread mitigation may not be required if core-scheduling or similar
techniques are used to prevent untrusted workloads from running on SMT siblings.

*cross-thread mitigations default to partially enabled*

.. _cmdline:

Command Line Controls
---------------------

Attack vectors are controlled through the mitigations= command line option.  The
value provided begins with a global option and then may optionally include one
or more options to disable various attack vectors.

Format:
	| ``mitigations=[global]``
	| ``mitigations=[global],[attack vectors]``

Global options:

============ =============================================================
Option       Description
============ =============================================================
'off'        All attack vectors disabled.
'auto'       All attack vectors enabled, partial cross-thread mitigations.
'auto,nosmt' All attack vectors enabled, full cross-thread mitigations.
============ =============================================================

Attack vector options:

================= =======================================
Option            Description
================= =======================================
'no_user_kernel'  Disables user-to-kernel mitigations.
'no_user_user'    Disables user-to-user mitigations.
'no_guest_host'   Disables guest-to-host mitigations.
'no_guest_guest'  Disables guest-to-guest mitigations
'no_cross_thread' Disables all cross-thread mitigations.
================= =======================================

Multiple attack vector options may be specified in a comma-separated list.  If
the global option is not specified, it defaults to 'auto'.  The global option
'off' is equivalent to disabling all attack vectors.

Examples:
	| ``mitigations=auto,no_user_kernel``

	Enable all attack vectors except user-to-kernel.  Partial cross-thread
	mitigations.

	| ``mitigations=auto,nosmt,no_guest_host,no_guest_guest``

	Enable all attack vectors and cross-thread mitigations except for
	guest-to-host and guest-to-guest mitigations.

	| ``mitigations=,no_cross_thread``

	Enable all attack vectors but not cross-thread mitigations.

Interactions with command-line options
--------------------------------------

Vulnerability-specific controls (e.g. "retbleed=off") take precedence over all
attack vector controls.  Mitigations for individual vulnerabilities may be
turned on or off via their command-line options regardless of the attack vector
controls.

Summary of attack-vector mitigations
------------------------------------

When a vulnerability is mitigated due to an attack-vector control, the default
mitigation option for that particular vulnerability is used.  To use a different
mitigation, please use the vulnerability-specific command line option.

The table below summarizes which vulnerabilities are mitigated when different
attack vectors are enabled and assuming the CPU is vulnerable.

=============== ============== ============ ============= ============== ============ ========
Vulnerability   User-to-Kernel User-to-User Guest-to-Host Guest-to-Guest Cross-Thread Notes
=============== ============== ============ ============= ============== ============ ========
BHI                   X                           X
ITS                   X                           X
GDS                   X              X            X              X            *       (Note 1)
L1TF                  X                           X                           *       (Note 2)
MDS                   X              X            X              X            *       (Note 2)
MMIO                  X              X            X              X            *       (Note 2)
Meltdown              X
Retbleed              X                           X                           *       (Note 3)
RFDS                  X              X            X              X
Spectre_v1            X
Spectre_v2            X                           X
Spectre_v2_user                      X                           X            *       (Note 1)
SRBDS                 X              X            X              X
SRSO                  X              X            X              X
SSB                                  X
TAA                   X              X            X              X            *       (Note 2)
TSA                   X              X            X              X
VMSCAPE                                           X
=============== ============== ============ ============= ============== ============ ========

Notes:
   1 --  Can be mitigated without disabling SMT.

   2 --  Disables SMT if cross-thread mitigations are fully enabled  and the CPU
   is vulnerable

   3 --  Disables SMT if cross-thread mitigations are fully enabled, the CPU is
   vulnerable, and STIBP is not supported

When an attack-vector is disabled, all mitigations for the vulnerabilities
listed in the above table are disabled, unless mitigation is required for a
different enabled attack-vector or a mitigation is explicitly selected via a
vulnerability-specific command line option.

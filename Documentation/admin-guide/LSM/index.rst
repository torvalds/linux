===========================
Linux Security Module Usage
===========================

The Linux Security Module (LSM) framework provides a mechanism for
various security checks to be hooked by new kernel extensions. The name
"module" is a bit of a misnomer since these extensions are not actually
loadable kernel modules. Instead, they are selectable at build-time via
CONFIG_LSM, an ordered list of the LSMs to enable, and can be
overridden at boot-time via the ``"lsm=..."`` kernel command line
argument. The ``"security=..."`` kernel command line argument remains
available to choose a legacy "major" security module, but has been
deprecated by the ``"lsm=..."`` parameter.

The primary users of the LSM interface are Mandatory Access Control
(MAC) extensions which provide a comprehensive security policy. Examples
include SELinux, Smack, Tomoyo, and AppArmor. In addition to the larger
MAC extensions, other extensions can be built using the LSM to provide
specific changes to system operation when these tweaks are not available
in the core functionality of Linux itself.

The Linux capabilities modules will always be included. This may be
followed by any number of "minor" modules and at most one "major" module.
For more details on capabilities, see ``capabilities(7)`` in the Linux
man-pages project.

A list of the active security modules can be found by reading
``/sys/kernel/security/lsm``. This is a comma separated list, and
will always include the capability module. The list reflects the
order in which checks are made. The capability module will be
first, unless CONFIG_SECURITY_LOCKDOWN_LSM_EARLY is enabled, in
which case the lockdown module will precede it. The integrity
modules (e.g. IMA and EVM), if enabled in the kernel
configuration, are always placed at the end of the list. Any
other "minor" modules (e.g. Yama) and the one "major" module
(e.g. SELinux), if there is one configured, appear in between,
in the order given by CONFIG_LSM or the ``"lsm=..."`` kernel
command line parameter.

Process attributes associated with "major" security modules should
be accessed and maintained using the special files in ``/proc/.../attr``.
A security module may maintain a module specific subdirectory there,
named after the module. ``/proc/.../attr/smack`` is provided by the Smack
security module and contains all its special files. The files directly
in ``/proc/.../attr`` remain as legacy interfaces for modules that provide
subdirectories.

.. toctree::
   :maxdepth: 1

   apparmor
   LoadPin
   SELinux
   Smack
   tomoyo
   Yama
   SafeSetID
   ipe
   landlock

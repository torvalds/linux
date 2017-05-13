=======
LoadPin
=======

LoadPin is a Linux Security Module that ensures all kernel-loaded files
(modules, firmware, etc) all originate from the same filesystem, with
the expectation that such a filesystem is backed by a read-only device
such as dm-verity or CDROM. This allows systems that have a verified
and/or unchangeable filesystem to enforce module and firmware loading
restrictions without needing to sign the files individually.

The LSM is selectable at build-time with ``CONFIG_SECURITY_LOADPIN``, and
can be controlled at boot-time with the kernel command line option
"``loadpin.enabled``". By default, it is enabled, but can be disabled at
boot ("``loadpin.enabled=0``").

LoadPin starts pinning when it sees the first file loaded. If the
block device backing the filesystem is not read-only, a sysctl is
created to toggle pinning: ``/proc/sys/kernel/loadpin/enabled``. (Having
a mutable filesystem means pinning is mutable too, but having the
sysctl allows for easy testing on systems with a mutable filesystem.)

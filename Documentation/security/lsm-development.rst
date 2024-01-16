=================================
Linux Security Module Development
=================================

Based on https://lore.kernel.org/r/20071026073721.618b4778@laptopd505.fenrus.org,
a new LSM is accepted into the kernel when its intent (a description of
what it tries to protect against and in what cases one would expect to
use it) has been appropriately documented in ``Documentation/admin-guide/LSM/``.
This allows an LSM's code to be easily compared to its goals, and so
that end users and distros can make a more informed decision about which
LSMs suit their requirements.

For extensive documentation on the available LSM hook interfaces, please
see ``include/linux/lsm_hooks.h`` and associated structures:

.. kernel-doc:: include/linux/lsm_hooks.h
   :internal:

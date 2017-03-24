Tainted kernels
---------------

Some oops reports contain the string **'Tainted: '** after the program
counter. This indicates that the kernel has been tainted by some
mechanism.  The string is followed by a series of position-sensitive
characters, each representing a particular tainted value.

  1) 'G' if all modules loaded have a GPL or compatible license, 'P' if
     any proprietary module has been loaded.  Modules without a
     MODULE_LICENSE or with a MODULE_LICENSE that is not recognised by
     insmod as GPL compatible are assumed to be proprietary.

  2) ``F`` if any module was force loaded by ``insmod -f``, ``' '`` if all
     modules were loaded normally.

  3) ``S`` if the oops occurred on an SMP kernel running on hardware that
     hasn't been certified as safe to run multiprocessor.
     Currently this occurs only on various Athlons that are not
     SMP capable.

  4) ``R`` if a module was force unloaded by ``rmmod -f``, ``' '`` if all
     modules were unloaded normally.

  5) ``M`` if any processor has reported a Machine Check Exception,
     ``' '`` if no Machine Check Exceptions have occurred.

  6) ``B`` if a page-release function has found a bad page reference or
     some unexpected page flags.

  7) ``U`` if a user or user application specifically requested that the
     Tainted flag be set, ``' '`` otherwise.

  8) ``D`` if the kernel has died recently, i.e. there was an OOPS or BUG.

  9) ``A`` if the ACPI table has been overridden.

 10) ``W`` if a warning has previously been issued by the kernel.
     (Though some warnings may set more specific taint flags.)

 11) ``C`` if a staging driver has been loaded.

 12) ``I`` if the kernel is working around a severe bug in the platform
     firmware (BIOS or similar).

 13) ``O`` if an externally-built ("out-of-tree") module has been loaded.

 14) ``E`` if an unsigned module has been loaded in a kernel supporting
     module signature.

 15) ``L`` if a soft lockup has previously occurred on the system.

 16) ``K`` if the kernel has been live patched.

The primary reason for the **'Tainted: '** string is to tell kernel
debuggers if this is a clean kernel or if anything unusual has
occurred.  Tainting is permanent: even if an offending module is
unloaded, the tainted value remains to indicate that the kernel is not
trustworthy.

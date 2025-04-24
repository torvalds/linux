.. _submitchecklist:

=======================================
Linux Kernel patch submission checklist
=======================================

Here are some basic things that developers should do if they want to see their
kernel patch submissions accepted more quickly.

These are all above and beyond the documentation that is provided in
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
and elsewhere regarding submitting Linux kernel patches.

Review your code
================

1) If you use a facility then #include the file that defines/declares
   that facility.  Don't depend on other header files pulling in ones
   that you use.

2) Check your patch for general style as detailed in
   :ref:`Documentation/process/coding-style.rst <codingstyle>`.

3) All memory barriers {e.g., ``barrier()``, ``rmb()``, ``wmb()``} need a
   comment in the source code that explains the logic of what they are doing
   and why.

Review Kconfig changes
======================

1) Any new or modified ``CONFIG`` options do not muck up the config menu and
   default to off unless they meet the exception criteria documented in
   ``Documentation/kbuild/kconfig-language.rst`` Menu attributes: default value.

2) All new ``Kconfig`` options have help text.

3) Has been carefully reviewed with respect to relevant ``Kconfig``
   combinations.  This is very hard to get right with testing---brainpower
   pays off here.

Provide documentation
=====================

1) Include :ref:`kernel-doc <kernel_doc>` to document global kernel APIs.
   (Not required for static functions, but OK there also.)

2) All new ``/proc`` entries are documented under ``Documentation/``

3) All new kernel boot parameters are documented in
   ``Documentation/admin-guide/kernel-parameters.rst``.

4) All new module parameters are documented with ``MODULE_PARM_DESC()``

5) All new userspace interfaces are documented in ``Documentation/ABI/``.
   See Documentation/admin-guide/abi.rst (or ``Documentation/ABI/README``)
   for more information.
   Patches that change userspace interfaces should be CCed to
   linux-api@vger.kernel.org.

6) If any ioctl's are added by the patch, then also update
   ``Documentation/userspace-api/ioctl/ioctl-number.rst``.

Check your code with tools
==========================

1) Check for trivial violations with the patch style checker prior to
   submission (``scripts/checkpatch.pl``).
   You should be able to justify all violations that remain in
   your patch.

2) Check cleanly with sparse.

3) Use ``make checkstack`` and fix any problems that it finds.
   Note that ``checkstack`` does not point out problems explicitly,
   but any one function that uses more than 512 bytes on the stack is a
   candidate for change.

Build your code
===============

1) Builds cleanly:

  a) with applicable or modified ``CONFIG`` options ``=y``, ``=m``, and
     ``=n``.  No ``gcc`` warnings/errors, no linker warnings/errors.

  b) Passes ``allnoconfig``, ``allmodconfig``

  c) Builds successfully when using ``O=builddir``

  d) Any Documentation/ changes build successfully without new warnings/errors.
     Use ``make htmldocs`` or ``make pdfdocs`` to check the build and
     fix any issues.

2) Builds on multiple CPU architectures by using local cross-compile tools
   or some other build farm.
   Note that testing against architectures of different word sizes
   (32- and 64-bit) and different endianness (big- and little-) is effective
   in catching various portability issues due to false assumptions on
   representable quantity range, data alignment, or endianness, among
   others.

3) Newly-added code has been compiled with ``gcc -W`` (use
   ``make KCFLAGS=-W``).  This will generate lots of noise, but is good
   for finding bugs like "warning: comparison between signed and unsigned".

4) If your modified source code depends on or uses any of the kernel
   APIs or features that are related to the following ``Kconfig`` symbols,
   then test multiple builds with the related ``Kconfig`` symbols disabled
   and/or ``=m`` (if that option is available) [not all of these at the
   same time, just various/random combinations of them]:

   ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``,
   ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
   ``CONFIG_NET``, ``CONFIG_INET=n`` (but latter with ``CONFIG_NET=y``).

Test your code
==============

1) Has been tested with ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
   ``CONFIG_SLUB_DEBUG``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
   ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
   ``CONFIG_PROVE_RCU`` and ``CONFIG_DEBUG_OBJECTS_RCU_HEAD`` all
   simultaneously enabled.

2) Has been build- and runtime tested with and without ``CONFIG_SMP`` and
   ``CONFIG_PREEMPT.``

3) All codepaths have been exercised with all lockdep features enabled.

4) Has been checked with injection of at least slab and page-allocation
   failures.  See ``Documentation/fault-injection/``.
   If the new code is substantial, addition of subsystem-specific fault
   injection might be appropriate.

5) Tested with the most recent tag of linux-next to make sure that it still
   works with all of the other queued patches and various changes in the VM,
   VFS, and other subsystems.

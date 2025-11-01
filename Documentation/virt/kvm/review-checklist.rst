.. SPDX-License-Identifier: GPL-2.0

================================
Review checklist for kvm patches
================================

1.  The patch must follow Documentation/process/coding-style.rst and
    Documentation/process/submitting-patches.rst.

2.  Patches should be against kvm.git master or next branches.

3.  If the patch introduces or modifies a new userspace API:
    - the API must be documented in Documentation/virt/kvm/api.rst
    - the API must be discoverable using KVM_CHECK_EXTENSION

4.  New state must include support for save/restore.

5.  New features must default to off (userspace should explicitly request them).
    Performance improvements can and should default to on.

6.  New cpu features should be exposed via KVM_GET_SUPPORTED_CPUID2,
    or its equivalent for non-x86 architectures

7.  The feature should be testable (see below).

8.  Changes should be vendor neutral when possible.  Changes to common code
    are better than duplicating changes to vendor code.

9.  Similarly, prefer changes to arch independent code than to arch dependent
    code.

10. User/kernel interfaces and guest/host interfaces must be 64-bit clean
    (all variables and sizes naturally aligned on 64-bit; use specific types
    only - u64 rather than ulong).

11. New guest visible features must either be documented in a hardware manual
    or be accompanied by documentation.

Testing of KVM code
-------------------

All features contributed to KVM, and in many cases bugfixes too, should be
accompanied by some kind of tests and/or enablement in open source guests
and VMMs.  KVM is covered by multiple test suites:

*Selftests*
  These are low level tests that allow granular testing of kernel APIs.
  This includes API failure scenarios, invoking APIs after specific
  guest instructions, and testing multiple calls to ``KVM_CREATE_VM``
  within a single test.  They are included in the kernel tree at
  ``tools/testing/selftests/kvm``.

``kvm-unit-tests``
  A collection of small guests that test CPU and emulated device features
  from a guest's perspective.  They run under QEMU or ``kvmtool``, and
  are generally not KVM-specific: they can be run with any accelerator
  that QEMU support or even on bare metal, making it possible to compare
  behavior across hypervisors and processor families.

Functional test suites
  Various sets of functional tests exist, such as QEMU's ``tests/functional``
  suite and `avocado-vt <https://avocado-vt.readthedocs.io/en/latest/>`__.
  These typically involve running a full operating system in a virtual
  machine.

The best testing approach depends on the feature's complexity and
operation. Here are some examples and guidelines:

New instructions (no new registers or APIs)
  The corresponding CPU features (if applicable) should be made available
  in QEMU.  If the instructions require emulation support or other code in
  KVM, it is worth adding coverage to ``kvm-unit-tests`` or selftests;
  the latter can be a better choice if the instructions relate to an API
  that already has good selftest coverage.

New hardware features (new registers, no new APIs)
  These should be tested via ``kvm-unit-tests``; this more or less implies
  supporting them in QEMU and/or ``kvmtool``.  In some cases selftests
  can be used instead, similar to the previous case, or specifically to
  test corner cases in guest state save/restore.

Bug fixes and performance improvements
  These usually do not introduce new APIs, but it's worth sharing
  any benchmarks and tests that will validate your contribution,
  ideally in the form of regression tests.  Tests and benchmarks
  can be included in either ``kvm-unit-tests`` or selftests, depending
  on the specifics of your change.  Selftests are especially useful for
  regression tests because they are included directly in Linux's tree.

Large scale internal changes
  While it's difficult to provide a single policy, you should ensure that
  the changed code is covered by either ``kvm-unit-tests`` or selftests.
  In some cases the affected code is run for any guests and functional
  tests suffice.  Explain your testing process in the cover letter,
  as that can help identify gaps in existing test suites.

New APIs
  It is important to demonstrate your use case.  This can be as simple as
  explaining that the feature is already in use on bare metal, or it can be
  a proof-of-concept implementation in userspace.  The latter need not be
  open source, though that is of course preferable for easier testing.
  Selftests should test corner cases of the APIs, and should also cover
  basic host and guest operation if no open source VMM uses the feature.

Bigger features, usually spanning host and guest
  These should be supported by Linux guests, with limited exceptions for
  Hyper-V features that are testable on Windows guests.  It is strongly
  suggested that the feature be usable with an open source host VMM, such
  as at least one of QEMU or crosvm, and guest firmware.  Selftests should
  test at least API error cases.  Guest operation can be covered by
  either selftests of ``kvm-unit-tests`` (this is especially important for
  paravirtualized and Windows-only features).  Strong selftest coverage
  can also be a replacement for implementation in an open source VMM,
  but this is generally not recommended.

Following the above suggestions for testing in selftests and
``kvm-unit-tests`` will make it easier for the maintainers to review
and accept your code.  In fact, even before you contribute your changes
upstream it will make it easier for you to develop for KVM.

Of course, the KVM maintainers reserve the right to require more tests,
though they may also waive the requirement from time to time.

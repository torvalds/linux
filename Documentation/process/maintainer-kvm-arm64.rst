.. SPDX-License-Identifier: GPL-2.0

KVM/arm64
=========

This document describes how KVM/arm64 (``arch/arm64/kvm/`` and the other
files listed under "KERNEL VIRTUAL MACHINE FOR ARM64 (KVM/arm64)" in
MAINTAINERS) is maintained.  It supplements
Documentation/process/submitting-patches.rst.

Overview
--------

The maintainers, reviewers and mailing lists for KVM/arm64 are listed in its
MAINTAINERS entry.

Trees
~~~~~

The KVM/arm64 tree is::

  git://git.kernel.org/pub/scm/linux/kernel/git/kvmarm/kvmarm.git

This tree is used for integration only: no development happens in it, and
most patches should not be based on it (see `Base for patches`_).  Its
branches are:

``next``
  Changes queued for the next merge window.  linux-next merges this branch.

``fixes``
  Fixes for the release currently in its -rc phase.  linux-next merges this
  branch too.

``master``
  Follows the -rc tags in Linus Torvalds' tree.

Changes leave the tree as signed tags, which are pulled into the main KVM tree
(``git://git.kernel.org/pub/scm/virt/kvm/kvm.git``) and from there reach Linus
Torvalds.

Topic branches are managed by the maintainers in their private trees, and only
the result of integrating them is pushed out.  The exception is shared
branches, which are published when necessary.  Where a series also touches
code maintained elsewhere, most often the arm64 architecture code, the
maintainers may set up a shared stable branch, based on an -rc tag, that both
trees merge.  Say in the cover letter which parts of a series touch other
subsystems.

Base for patches
~~~~~~~~~~~~~~~~

Base your patches on a tag published in Linus Torvalds' tree, preferably -rc1
to -rc3.  If you rely on something merged later, for example a fix merged in
-rc4, say why in the cover letter.  Patches based on linux-next are
discouraged.

Do not base patches on ``kvmarm/next`` either: it is rebuilt regularly, so a
commit on it is generally the wrong base.  If your series conflicts with work
queued there, leave the conflict for the maintainers to see, as it is a sign
that something needs a closer look.  A proposed resolution is welcome.  The
exception is a series that strictly continues one that has already been
queued.

If your series depends on something else that is not yet in mainline, such as
another series under review, say so in the cover letter and name exactly what
it applies on top of.

Use ``git format-patch --base`` so that the base commit is recorded in the
patches.

Submit Checklist Addendum
-------------------------

Recipients and threading
~~~~~~~~~~~~~~~~~~~~~~~~

Send the whole series to all of the maintainers and reviewers listed for
KVM/arm64, not a selection of them.  Post each new version as a new thread,
not as a reply to the previous version, with a cover letter for anything
longer than a single patch.

Subject lines
~~~~~~~~~~~~~

Changes to KVM/arm64 use the ``KVM: arm64:`` prefix, often followed by a
sub-topic such as ``nv:`` or ``vgic:``.  Selftest changes use ``KVM: arm64:
selftests:`` or ``KVM: selftests:``.
``git log --oneline`` on the files you touch shows what is in use.  The text
after the prefixes starts with a capital letter, for example ``KVM: arm64: nv:
Fix life cycle of the nested_mmus array``.

Architecture references
~~~~~~~~~~~~~~~~~~~~~~~

KVM/arm64 code tracks the Arm Architecture Reference Manual (the "Arm ARM",
document DDI0487) closely.  Where a change depends on architected behaviour,
explain why the change is needed and cite the Arm ARM in the commit message
or in a comment.  Where the rule has an identifier, such as ``R_QXXPC``, quote
that: identifiers stay the same across revisions of the document, while
section numbers do not.  Otherwise give the revision along with the section,
for example ``DDI0487L.a D24.2.70``.  The same applies to other architecture
documents.

Testing
~~~~~~~

Say in the cover letter how the series was tested: which tests, and on which
hardware or model.  The KVM selftests (``tools/testing/selftests/kvm/``) and
kvm-unit-tests are the usual test suites.  Put selftest changes in patches of
their own, separate from the KVM changes and usually at the end of the series.

KVM/arm64 can run in several modes, which are described under
``kvm-arm.mode=`` in Documentation/admin-guide/kernel-parameters.txt.  Test
the modes your change can affect.

If a patch aims to improve performance, describe exactly how you measured it,
in enough detail for others to reproduce the results.  Measurements made on a
software model such as QEMU or the Arm FVP say nothing about performance on
hardware.

New tests are encouraged, particularly for nested virtualisation, where
coverage is poor.  Depending on the change, selftests may not be enough; the
maintainers may ask for testing with a VMM, for example.

Userspace API
~~~~~~~~~~~~~

Document changes to the userspace API, usually in
Documentation/virt/kvm/api.rst or in the files under
Documentation/virt/kvm/devices/ and Documentation/virt/kvm/arm/.

Fixes
~~~~~

Add a ``Fixes:`` tag for bug fixes.  If the fix should go to stable kernels,
add ``Cc: stable@vger.kernel.org``; the maintainers may add it when applying
if the bug calls for it.

Key Cycle Dates
---------------

Changes for the next merge window are queued on ``next``.  New features are
usually not merged once -rc6 has been published; from then on, only fixes are
queued.  This is not a hard rule, but posting a feature just before -rc6
guarantees nothing.

Fixes for the current release are queued on ``fixes`` and sent upstream at
intervals that depend on how severe the bugs are.  There is no cut-off for
fixes, but the maintainers may defer a low-priority fix to the next merge
window.

Review Cadence
--------------

Unless the maintainers ask for a new version sooner, allow at least a week
between versions of a series, and only post a new version once there has
been enough review to justify it.  Posting more often delays review rather
than speeding it up.

Pinging a series that has had no response is fine.

Answer reviews from bots such as Sashiko as you would any other review, even
if only to say that a finding is wrong.  Pre-existing problems that a bot
points out, and that are unrelated to the series, need not be fixed in its
initial posting.

Applied patches are normally acknowledged in reply to the posting, naming the
branch (``next`` or ``fixes``) and the commits.  The maintainers try not to
change the commit IDs of applied patches, but the IDs of merge commits almost
always change.

Minor problems are often fixed up by the maintainers when applying; ask if
you are unsure whether to resend.

Security issues
---------------

Bugs that let a guest attack its host, or a nested guest attack its guest
hypervisor, should be reported as described in
Documentation/process/security-bugs.rst.

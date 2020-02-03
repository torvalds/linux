.. SPDX-License-Identifier: GPL-2.0
Documentation subsystem maintainer entry profile
================================================

The documentation "subsystem" is the central coordinating point for the
kernel's documentation and associated infrastructure.  It covers the
hierarchy under Documentation/ (with the exception of
Documentation/device-tree), various utilities under scripts/ and, at least
some of the time, LICENSES/.

It's worth noting, though, that the boundaries of this subsystem are rather
fuzzier than normal.  Many other subsystem maintainers like to keep control
of portions of Documentation/, and many more freely apply changes there
when it is convenient.  Beyond that, much of the kernel's documentation is
found in the source as kerneldoc comments; those are usually (but not
always) maintained by the relevant subsystem maintainer.

The mailing list for documentation is linux-doc@vger.kernel.org.  Patches
should be made against the docs-next tree whenever possible.

Submit checklist addendum
-------------------------

When making documentation changes, you should actually build the
documentation and ensure that no new errors or warnings have been
introduced.  Generating HTML documents and looking at the result will help
to avoid unsightly misunderstandings about how things will be rendered.

Key cycle dates
---------------

Patches can be sent anytime, but response will be slower than usual during
the merge window.  The docs tree tends to close late before the merge
window opens, since the risk of regressions from documentation patches is
low.

Review cadence
--------------

I am the sole maintainer for the documentation subsystem, and I am doing
the work on my own time, so the response to patches will occasionally be
slow.  I try to always send out a notification when a patch is merged (or
when I decide that one cannot be).  Do not hesitate to send a ping if you
have not heard back within a week of sending a patch.

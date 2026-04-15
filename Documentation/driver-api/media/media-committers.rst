.. SPDX-License-Identifier: GPL-2.0

.. _Media Committers:

Media Committers
================

Who is a Media Committer?
-------------------------

A Media Committer is a Media Maintainer with patchwork access who has been
granted commit access to push patches from other developers and their own
patches to the
`media-committers <https://gitlab.freedesktop.org/linux-media/media-committers>`_
tree.

These commit rights are granted with expectation of responsibility:
committers are people who care about the Linux Kernel as a whole and
about the Linux media subsystem and want to advance its development. It
is also based on a trust relationship among other committers, maintainers
and the Linux Media community.

As Media Committer you have the following additional responsibilities:

1. Patches you authored must have a ``Signed-off-by``, ``Reviewed-by``
   or ``Acked-by`` from another Media Maintainer;
2. If a patch introduces a regression, then that must be corrected as soon
   as possible. Typically the patch is either reverted, or an additional
   patch is committed to fix the regression;
3. If patches are fixing bugs against already released Kernels, including
   the reverts mentioned above, the Media Committer shall add the needed
   tags. Please see :ref:`Media development workflow` for more details.
4. All Media Committers are responsible for maintaining
   `Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_,
   updating the state of the patches they review or merge.


Becoming a Media Committer
--------------------------

Existing Media Committers can nominate a Media Maintainer to be granted
commit rights. The Media Maintainer must have patchwork access,
have been reviewing patches from third parties for some time, and has
demonstrated a good understanding of the maintainer's duties and processes.

The ultimate responsibility for accepting a nominated committer is up to
the Media Subsystem Maintainers. The nominated committer must have earned a
trust relationship with all Media Subsystem Maintainers, as, by granting you
commit rights, part of their responsibilities are handed over to you.

Due to that, to become a Media Committer, a consensus between all Media
Subsystem Maintainers is required.

.. Note::

   In order to preserve/protect the developers that could have their commit
   rights granted, denied or removed as well as the subsystem maintainers who
   have the task to accept or deny commit rights, all communication related to
   changing commit rights should happen in private as much as possible.

.. _media-committer-agreement:

Media Committer's agreement
---------------------------

Once a nominated committer is accepted by all Media Subsystem Maintainers,
they will ask if the developer is interested in the nomination and discuss
what area(s) of the media subsystem the committer will be responsible for.
Those areas will typically be the same as the areas that the nominated
committer is already maintaining.

When the developer accepts being a committer, the new committer shall
explicitly accept the Kernel development policies described under its
Documentation/, and in particular to the rules in this document, by writing
an e-mail to media-committers@linuxtv.org, with a declaration of intent
following the model below::

   I, John Doe, would like to change my status to: Committer

   As Media Maintainer I accept commit rights for the following areas of
   the media subsystem:

   ...

   For the purpose of committing patches to the media-committers tree,
   I'll be using my user https://gitlab.freedesktop.org/users/<username>.

Followed by a formal declaration of agreement with the Kernel development
rules::

   I agree to follow the Kernel development rules described at:

   https://www.kernel.org/doc/html/latest/driver-api/media/media-committers.rst

   and to the Linux Kernel development process rules.

   I agree to abide by the Code of Conduct as documented in:
   https://www.kernel.org/doc/html/latest/process/code-of-conduct.rst

   I am aware that I can, at any point of time, retire. In that case, I will
   send an e-mail to notify the Media Subsystem Maintainers for them to revoke
   my commit rights.

   I am aware that the Kernel development rules change over time.
   By doing a new push to media-committers tree, I understand that I agree
   to follow the rules in effect at the time of the commit.

That e-mail shall be signed via the Kernel Web of trust with a PGP key cross
signed by other Kernel and media developers. As described at
:ref:`media-developers-gpg`, the PGP signature, together with the gitlab user
security are fundamental components that ensure the authenticity of the merge
requests that will happen at the media-committers.git tree.

In case the kernel development process changes, by merging new commits to the
`media-committers tree <https://gitlab.freedesktop.org/linux-media/media-committers>`_,
the Media Committer implicitly declares their agreement with the latest
version of the documented process including the contents of this file.

If a Media Committer decides to retire, it is the committer's duty to
notify the Media Subsystem Maintainers about that decision.

.. note::

   1. Changes to the kernel media development process shall be announced in
      the media-committers mailing list with a reasonable review period. All
      committers are automatically subscribed to that mailing list;
   2. Due to the distributed nature of the Kernel development, it is
      possible that kernel development process changes may end being
      reviewed/merged at the Linux Docs and/or at the Linux Kernel mailing
      lists, especially for the contents under Documentation/process and for
      trivial typo fixes.

Media Core Committers
---------------------

A Media Core Committer is a Media Core Maintainer with commit rights.

As described in Documentation/driver-api/media/maintainer-entry-profile.rst,
a Media Core Maintainer maintains media core frameworks as well, besides
just drivers, and so is allowed to change core files and the media subsystem's
Kernel API. The extent of the core committer's grants will be detailed by the
Media Subsystem Maintainers when they nominate a Media Core Committer.

Existing Media Committers may become Media Core Committers and vice versa.
Such decisions will be taken in consensus among the Media Subsystem
Maintainers.

Media committers rules
----------------------

Media committers shall do their best efforts to avoid merging patches that
would break any existing drivers. If it breaks, fixup or revert patches
shall be merged as soon as possible, aiming to be merged at the same Kernel
cycle the bug is reported.

Media committers shall behave accordingly to the rights granted by
the Media Subsystem Maintainers, especially with regards of the scope of changes
they may apply directly at the media-committers tree. That scope can
change over time on a mutual agreement between Media Committers and
Media Subsystem Maintainers.

The Media Committer workflow is described at :ref:`Media development workflow`.

.. _Maintain Media Status:

Maintaining Media Maintainer or Committer status
------------------------------------------------

A community of maintainers working together to move the Linux Kernel
forward is essential to creating successful projects that are rewarding
to work on. If there are problems or disagreements within the community,
they can usually be solved through healthy discussion and debate.

In the unhappy event that a Media Maintainer or Committer continues to
disregard good citizenship (or actively disrupts the project), we may need
to revoke that person's status. In such cases, if someone suggests the
revocation with a good reason, then after discussing this among the Media
Maintainers, the final decision is taken by the Media Subsystem Maintainers.

As the decision to become a Media Maintainer or Committer comes from a
consensus between Media Subsystem Maintainers, a single Media Subsystem
Maintainer not trusting the Media Maintainer or Committer anymore is enough
to revoke their maintenance, Patchwork grants and/or commit rights.

Having commit rights revoked doesn't prevent Media Maintainers to keep
contributing to the subsystem either via the pull request or via email workflow
as documented at the :ref:`Media development workflow`.

If a maintainer is inactive for more than a couple of Kernel cycles,
maintainers will try to reach you via e-mail. If not possible, they may
revoke their maintainer/patchwork and committer rights and update MAINTAINERS
file entries accordingly. If you wish to resume contributing as maintainer
later on, then contact the Media Subsystem Maintainers to ask if your
maintenance, Patchwork grants and commit rights can be restored.

References
----------

Much of this was inspired by/copied from the committer policies of:

- `Chromium <https://chromium.googlesource.com/chromium/src/+/main/docs/contributing.md>`_;
- `WebKit <https://webkit.org/commit-and-review-policy/>`_;
- `Mozilla <https://www.mozilla.org/hacking/committer/>`_.

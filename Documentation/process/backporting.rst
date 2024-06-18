.. SPDX-License-Identifier: GPL-2.0

===================================
Backporting and conflict resolution
===================================

:Author: Vegard Nossum <vegard.nossum@oracle.com>

.. contents::
    :local:
    :depth: 3
    :backlinks: none

Introduction
============

Some developers may never really have to deal with backporting patches,
merging branches, or resolving conflicts in their day-to-day work, so
when a merge conflict does pop up, it can be daunting. Luckily,
resolving conflicts is a skill like any other, and there are many useful
techniques you can use to make the process smoother and increase your
confidence in the result.

This document aims to be a comprehensive, step-by-step guide to
backporting and conflict resolution.

Applying the patch to a tree
============================

Sometimes the patch you are backporting already exists as a git commit,
in which case you just cherry-pick it directly using
``git cherry-pick``. However, if the patch comes from an email, as it
often does for the Linux kernel, you will need to apply it to a tree
using ``git am``.

If you've ever used ``git am``, you probably already know that it is
quite picky about the patch applying perfectly to your source tree. In
fact, you've probably had nightmares about ``.rej`` files and trying to
edit the patch to make it apply.

It is strongly recommended to instead find an appropriate base version
where the patch applies cleanly and *then* cherry-pick it over to your
destination tree, as this will make git output conflict markers and let
you resolve conflicts with the help of git and any other conflict
resolution tools you might prefer to use. For example, if you want to
apply a patch that just arrived on LKML to an older stable kernel, you
can apply it to the most recent mainline kernel and then cherry-pick it
to your older stable branch.

It's generally better to use the exact same base as the one the patch
was generated from, but it doesn't really matter that much as long as it
applies cleanly and isn't too far from the original base. The only
problem with applying the patch to the "wrong" base is that it may pull
in more unrelated changes in the context of the diff when cherry-picking
it to the older branch.

A good reason to prefer ``git cherry-pick`` over ``git am`` is that git
knows the precise history of an existing commit, so it will know when
code has moved around and changed the line numbers; this in turn makes
it less likely to apply the patch to the wrong place (which can result
in silent mistakes or messy conflicts).

If you are using `b4`_. and you are applying the patch directly from an
email, you can use ``b4 am`` with the options ``-g``/``--guess-base``
and ``-3``/``--prep-3way`` to do some of this automatically (see the
`b4 presentation`_ for more information). However, the rest of this
article will assume that you are doing a plain ``git cherry-pick``.

.. _b4: https://people.kernel.org/monsieuricon/introducing-b4-and-patch-attestation
.. _b4 presentation: https://youtu.be/mF10hgVIx9o?t=2996

Once you have the patch in git, you can go ahead and cherry-pick it into
your source tree. Don't forget to cherry-pick with ``-x`` if you want a
written record of where the patch came from!

Note that if you are submiting a patch for stable, the format is
slightly different; the first line after the subject line needs tobe
either::

    commit <upstream commit> upstream

or::

    [ Upstream commit <upstream commit> ]

Resolving conflicts
===================

Uh-oh; the cherry-pick failed with a vaguely threatening message::

    CONFLICT (content): Merge conflict

What to do now?

In general, conflicts appear when the context of the patch (i.e., the
lines being changed and/or the lines surrounding the changes) doesn't
match what's in the tree you are trying to apply the patch *to*.

For backports, what likely happened was that the branch you are
backporting from contains patches not in the branch you are backporting
to. However, the reverse is also possible. In any case, the result is a
conflict that needs to be resolved.

If your attempted cherry-pick fails with a conflict, git automatically
edits the files to include so-called conflict markers showing you where
the conflict is and how the two branches have diverged. Resolving the
conflict typically means editing the end result in such a way that it
takes into account these other commits.

Resolving the conflict can be done either by hand in a regular text
editor or using a dedicated conflict resolution tool.

Many people prefer to use their regular text editor and edit the
conflict directly, as it may be easier to understand what you're doing
and to control the final result. There are definitely pros and cons to
each method, and sometimes there's value in using both.

We will not cover using dedicated merge tools here beyond providing some
pointers to various tools that you could use:

-  `Emacs Ediff mode <https://www.emacswiki.org/emacs/EdiffMode>`__
-  `vimdiff/gvimdiff <https://linux.die.net/man/1/vimdiff>`__
-  `KDiff3 <http://kdiff3.sourceforge.net/>`__
-  `TortoiseMerge <https://tortoisesvn.net/TortoiseMerge.html>`__
-  `Meld <https://meldmerge.org/help/>`__
-  `P4Merge <https://www.perforce.com/products/helix-core-apps/merge-diff-tool-p4merge>`__
-  `Beyond Compare <https://www.scootersoftware.com/>`__
-  `IntelliJ <https://www.jetbrains.com/help/idea/resolve-conflicts.html>`__
-  `VSCode <https://code.visualstudio.com/docs/editor/versioncontrol>`__

To configure git to work with these, see ``git mergetool --help`` or
the official `git-mergetool documentation`_.

.. _git-mergetool documentation: https://git-scm.com/docs/git-mergetool

Prerequisite patches
--------------------

Most conflicts happen because the branch you are backporting to is
missing some patches compared to the branch you are backporting *from*.
In the more general case (such as merging two independent branches),
development could have happened on either branch, or the branches have
simply diverged -- perhaps your older branch had some other backports
applied to it that themselves needed conflict resolutions, causing a
divergence.

It's important to always identify the commit or commits that caused the
conflict, as otherwise you cannot be confident in the correctness of
your resolution. As an added bonus, especially if the patch is in an
area you're not that famliar with, the changelogs of these commits will
often give you the context to understand the code and potential problems
or pitfalls with your conflict resolution.

git log
~~~~~~~

A good first step is to look at ``git log`` for the file that has the
conflict -- this is usually sufficient when there aren't a lot of
patches to the file, but may get confusing if the file is big and
frequently patched. You should run ``git log`` on the range of commits
between your currently checked-out branch (``HEAD``) and the parent of
the patch you are picking (``<commit>``), i.e.::

    git log HEAD..<commit>^ -- <path>

Even better, if you want to restrict this output to a single function
(because that's where the conflict appears), you can use the following
syntax::

    git log -L:'\<function\>':<path> HEAD..<commit>^

.. note::
     The ``\<`` and ``\>`` around the function name ensure that the
     matches are anchored on a word boundary. This is important, as this
     part is actually a regex and git only follows the first match, so
     if you use ``-L:thread_stack:kernel/fork.c`` it may only give you
     results for the function ``try_release_thread_stack_to_cache`` even
     though there are many other functions in that file containing the
     string ``thread_stack`` in their names.

Another useful option for ``git log`` is ``-G``, which allows you to
filter on certain strings appearing in the diffs of the commits you are
listing::

    git log -G'regex' HEAD..<commit>^ -- <path>

This can also be a handy way to quickly find when something (e.g. a
function call or a variable) was changed, added, or removed. The search
string is a regular expression, which means you can potentially search
for more specific things like assignments to a specific struct member::

    git log -G'\->index\>.*='

git blame
~~~~~~~~~

Another way to find prerequisite commits (albeit only the most recent
one for a given conflict) is to run ``git blame``. In this case, you
need to run it against the parent commit of the patch you are
cherry-picking and the file where the conflict appared, i.e.::

    git blame <commit>^ -- <path>

This command also accepts the ``-L`` argument (for restricting the
output to a single function), but in this case you specify the filename
at the end of the command as usual::

    git blame -L:'\<function\>' <commit>^ -- <path>

Navigate to the place where the conflict occurred. The first column of
the blame output is the commit ID of the patch that added a given line
of code.

It might be a good idea to ``git show`` these commits and see if they
look like they might be the source of the conflict. Sometimes there will
be more than one of these commits, either because multiple commits
changed different lines of the same conflict area *or* because multiple
subsequent patches changed the same line (or lines) multiple times. In
the latter case, you may have to run ``git blame`` again and specify the
older version of the file to look at in order to dig further back in
the history of the file.

Prerequisite vs. incidental patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Having found the patch that caused the conflict, you need to determine
whether it is a prerequisite for the patch you are backporting or
whether it is just incidental and can be skipped. An incidental patch
would be one that touches the same code as the patch you are
backporting, but does not change the semantics of the code in any
material way. For example, a whitespace cleanup patch is completely
incidental -- likewise, a patch that simply renames a function or a
variable would be incidental as well. On the other hand, if the function
being changed does not even exist in your current branch then this would
not be incidental at all and you need to carefully consider whether the
patch adding the function should be cherry-picked first.

If you find that there is a necessary prerequisite patch, then you need
to stop and cherry-pick that instead. If you've already resolved some
conflicts in a different file and don't want to do it again, you can
create a temporary copy of that file.

To abort the current cherry-pick, go ahead and run
``git cherry-pick --abort``, then restart the cherry-picking process
with the commit ID of the prerequisite patch instead.

Understanding conflict markers
------------------------------

Combined diffs
~~~~~~~~~~~~~~

Let's say you've decided against picking (or reverting) additional
patches and you just want to resolve the conflict. Git will have
inserted conflict markers into your file. Out of the box, this will look
something like::

    <<<<<<< HEAD
    this is what's in your current tree before cherry-picking
    =======
    this is what the patch wants it to be after cherry-picking
    >>>>>>> <commit>... title

This is what you would see if you opened the file in your editor.
However, if you were to run ``git diff`` without any arguments, the
output would look something like this::

    $ git diff
    [...]
    ++<<<<<<<< HEAD
     +this is what's in your current tree before cherry-picking
    ++========
    + this is what the patch wants it to be after cherry-picking
    ++>>>>>>>> <commit>... title

When you are resolving a conflict, the behavior of ``git diff`` differs
from its normal behavior. Notice the two columns of diff markers
instead of the usual one; this is a so-called "`combined diff`_", here
showing the 3-way diff (or diff-of-diffs) between

#. the current branch (before cherry-picking) and the current working
   directory, and
#. the current branch (before cherry-picking) and the file as it looks
   after the original patch has been applied.

.. _combined diff: https://git-scm.com/docs/diff-format#_combined_diff_format


Better diffs
~~~~~~~~~~~~

3-way combined diffs include all the other changes that happened to the
file between your current branch and the branch you are cherry-picking
from. While this is useful for spotting other changes that you need to
take into account, this also makes the output of ``git diff`` somewhat
intimidating and difficult to read. You may instead prefer to run
``git diff HEAD`` (or ``git diff --ours``) which shows only the diff
between the current branch before cherry-picking and the current working
directory. It looks like this::

    $ git diff HEAD
    [...]
    +<<<<<<<< HEAD
     this is what's in your current tree before cherry-picking
    +========
    +this is what the patch wants it to be after cherry-picking
    +>>>>>>>> <commit>... title

As you can see, this reads just like any other diff and makes it clear
which lines are in the current branch and which lines are being added
because they are part of the merge conflict or the patch being
cherry-picked.

Merge styles and diff3
~~~~~~~~~~~~~~~~~~~~~~

The default conflict marker style shown above is known as the ``merge``
style. There is also another style available, known as the ``diff3``
style, which looks like this::

    <<<<<<< HEAD
    this is what is in your current tree before cherry-picking
    ||||||| parent of <commit> (title)
    this is what the patch expected to find there
    =======
    this is what the patch wants it to be after being applied
    >>>>>>> <commit> (title)

As you can see, this has 3 parts instead of 2, and includes what git
expected to find there but didn't. It is *highly recommended* to use
this conflict style as it makes it much clearer what the patch actually
changed; i.e., it allows you to compare the before-and-after versions
of the file for the commit you are cherry-picking. This allows you to
make better decisions about how to resolve the conflict.

To change conflict marker styles, you can use the following command::

    git config merge.conflictStyle diff3

There is a third option, ``zdiff3``, introduced in `Git 2.35`_,
which has the same 3 sections as ``diff3``, but where common lines have
been trimmed off, making the conflict area smaller in some cases.

.. _Git 2.35: https://github.blog/2022-01-24-highlights-from-git-2-35/

Iterating on conflict resolutions
---------------------------------

The first step in any conflict resolution process is to understand the
patch you are backporting. For the Linux kernel this is especially
important, since an incorrect change can lead to the whole system
crashing -- or worse, an undetected security vulnerability.

Understanding the patch can be easy or difficult depending on the patch
itself, the changelog, and your familiarity with the code being changed.
However, a good question for every change (or every hunk of the patch)
might be: "Why is this hunk in the patch?" The answers to these
questions will inform your conflict resolution.

Resolution process
~~~~~~~~~~~~~~~~~~

Sometimes the easiest thing to do is to just remove all but the first
part of the conflict, leaving the file essentially unchanged, and apply
the changes by hand. Perhaps the patch is changing a function call
argument from ``0`` to ``1`` while a conflicting change added an
entirely new (and insignificant) parameter to the end of the parameter
list; in that case, it's easy enough to change the argument from ``0``
to ``1`` by hand and leave the rest of the arguments alone. This
technique of manually applying changes is mostly useful if the conflict
pulled in a lot of unrelated context that you don't really need to care
about.

For particularly nasty conflicts with many conflict markers, you can use
``git add`` or ``git add -i`` to selectively stage your resolutions to
get them out of the way; this also lets you use ``git diff HEAD`` to
always see what remains to be resolved or ``git diff --cached`` to see
what your patch looks like so far.

Dealing with file renames
~~~~~~~~~~~~~~~~~~~~~~~~~

One of the most annoying things that can happen while backporting a
patch is discovering that one of the files being patched has been
renamed, as that typically means git won't even put in conflict markers,
but will just throw up its hands and say (paraphrased): "Unmerged path!
You do the work..."

There are generally a few ways to deal with this. If the patch to the
renamed file is small, like a one-line change, the easiest thing is to
just go ahead and apply the change by hand and be done with it. On the
other hand, if the change is big or complicated, you definitely don't
want to do it by hand.

As a first pass, you can try something like this, which will lower the
rename detection threshold to 30% (by default, git uses 50%, meaning
that two files need to have at least 50% in common for it to consider
an add-delete pair to be a potential rename)::

  git cherry-pick -strategy=recursive -Xrename-threshold=30

Sometimes the right thing to do will be to also backport the patch that
did the rename, but that's definitely not the most common case. Instead,
what you can do is to temporarily rename the file in the branch you're
backporting to (using ``git mv`` and committing the result), restart the
attempt to cherry-pick the patch, rename the file back (``git mv`` and
committing again), and finally squash the result using ``git rebase -i``
(see the `rebase tutorial`_) so it appears as a single commit when you
are done.

.. _rebase tutorial: https://medium.com/@slamflipstrom/a-beginners-guide-to-squashing-commits-with-git-rebase-8185cf6e62ec

Gotchas
-------

Function arguments
~~~~~~~~~~~~~~~~~~

Pay attention to changing function arguments! It's easy to gloss over
details and think that two lines are the same but actually they differ
in some small detail like which variable was passed as an argument
(especially if the two variables are both a single character that look
the same, like i and j).

Error handling
~~~~~~~~~~~~~~

If you cherry-pick a patch that includes a ``goto`` statement (typically
for error handling), it is absolutely imperative to double check that
the target label is still correct in the branch you are backporting to.
The same goes for added ``return``, ``break``, and ``continue``
statements.

Error handling is typically located at the bottom of the function, so it
may not be part of the conflict even though could have been changed by
other patches.

A good way to ensure that you review the error paths is to always use
``git diff -W`` and ``git show -W`` (AKA ``--function-context``) when
inspecting your changes.  For C code, this will show you the whole
function that's being changed in a patch. One of the things that often
go wrong during backports is that something else in the function changed
on either of the branches that you're backporting from or to. By
including the whole function in the diff you get more context and can
more easily spot problems that might otherwise go unnoticed.

Refactored code
~~~~~~~~~~~~~~~

Something that happens quite often is that code gets refactored by
"factoring out" a common code sequence or pattern into a helper
function. When backporting patches to an area where such a refactoring
has taken place, you effectively need to do the reverse when
backporting: a patch to a single location may need to be applied to
multiple locations in the backported version. (One giveaway for this
scenario is that a function was renamed -- but that's not always the
case.)

To avoid incomplete backports, it's worth trying to figure out if the
patch fixes a bug that appears in more than one place. One way to do
this would be to use ``git grep``. (This is actually a good idea to do
in general, not just for backports.) If you do find that the same kind
of fix would apply to other places, it's also worth seeing if those
places exist upstream -- if they don't, it's likely the patch may need
to be adjusted. ``git log`` is your friend to figure out what happened
to these areas as ``git blame`` won't show you code that has been
removed.

If you do find other instances of the same pattern in the upstream tree
and you're not sure whether it's also a bug, it may be worth asking the
patch author. It's not uncommon to find new bugs during backporting!

Verifying the result
====================

colordiff
---------

Having committed a conflict-free new patch, you can now compare your
patch to the original patch. It is highly recommended that you use a
tool such as `colordiff`_ that can show two files side by side and color
them according to the changes between them::

    colordiff -yw -W 200 <(git diff -W <upstream commit>^-) <(git diff -W HEAD^-) | less -SR

.. _colordiff: https://www.colordiff.org/

Here, ``-y`` means to do a side-by-side comparison; ``-w`` ignores
whitespace, and ``-W 200`` sets the width of the output (as otherwise it
will use 130 by default, which is often a bit too little).

The ``rev^-`` syntax is a handy shorthand for ``rev^..rev``, essentially
giving you just the diff for that single commit; also see
the official `git rev-parse documentation`_.

.. _git rev-parse documentation: https://git-scm.com/docs/git-rev-parse#_other_rev_parent_shorthand_notations

Again, note the inclusion of ``-W`` for ``git diff``; this ensures that
you will see the full function for any function that has changed.

One incredibly important thing that colordiff does is to highlight lines
that are different. For example, if an error-handling ``goto`` has
changed labels between the original and backported patch, colordiff will
show these side-by-side but highlighted in a different color.  Thus, it
is easy to see that the two ``goto`` statements are jumping to different
labels. Likewise, lines that were not modified by either patch but
differ in the context will also be highlighted and thus stand out during
a manual inspection.

Of course, this is just a visual inspection; the real test is building
and running the patched kernel (or program).

Build testing
-------------

We won't cover runtime testing here, but it can be a good idea to build
just the files touched by the patch as a quick sanity check. For the
Linux kernel you can build single files like this, assuming you have the
``.config`` and build environment set up correctly::

    make path/to/file.o

Note that this won't discover linker errors, so you should still do a
full build after verifying that the single file compiles. By compiling
the single file first you can avoid having to wait for a full build *in
case* there are compiler errors in any of the files you've changed.

Runtime testing
---------------

Even a successful build or boot test is not necessarily enough to rule
out a missing dependency somewhere. Even though the chances are small,
there could be code changes where two independent changes to the same
file result in no conflicts, no compile-time errors, and runtime errors
only in exceptional cases.

One concrete example of this was a pair of patches to the system call
entry code where the first patch saved/restored a register and a later
patch made use of the same register somewhere in the middle of this
sequence. Since there was no overlap between the changes, one could
cherry-pick the second patch, have no conflicts, and believe that
everything was fine, when in fact the code was now scribbling over an
unsaved register.

Although the vast majority of errors will be caught during compilation
or by superficially exercising the code, the only way to *really* verify
a backport is to review the final patch with the same level of scrutiny
as you would (or should) give to any other patch. Having unit tests and
regression tests or other types of automatic testing can help increase
the confidence in the correctness of a backport.

Submitting backports to stable
==============================

As the stable maintainers try to cherry-pick mainline fixes onto their
stable kernels, they may send out emails asking for backports when when
encountering conflicts, see e.g.
<https://lore.kernel.org/stable/2023101528-jawed-shelving-071a@gregkh/>.
These emails typically include the exact steps you need to cherry-pick
the patch to the correct tree and submit the patch.

One thing to make sure is that your changelog conforms to the expected
format::

  <original patch title>
  
  [ Upstream commit <mainline rev> ]
  
  <rest of the original changelog>
  [ <summary of the conflicts and their resolutions> ]
  Signed-off-by: <your name and email>

The "Upstream commit" line is sometimes slightly different depending on
the stable version. Older version used this format::

  commit <mainline rev> upstream.

It is most common to indicate the kernel version the patch applies to
in the email subject line (using e.g.
``git send-email --subject-prefix='PATCH 6.1.y'``), but you can also put
it in the Signed-off-by:-area or below the ``---`` line.

The stable maintainers expect separate submissions for each active
stable version, and each submission should also be tested separately.

A few final words of advice
===========================

1) Approach the backporting process with humility.
2) Understand the patch you are backporting; this means reading both
   the changelog and the code.
3) Be honest about your confidence in the result when submitting the
   patch.
4) Ask relevant maintainers for explicit acks.

Examples
========

The above shows roughly the idealized process of backporting a patch.
For a more concrete example, see this video tutorial where two patches
are backported from mainline to stable:
`Backporting Linux Kernel Patches`_.

.. _Backporting Linux Kernel Patches: https://youtu.be/sBR7R1V2FeA

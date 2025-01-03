.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)
.. [see the bottom of this file for redistribution information]

======================
Bisecting a regression
======================

This document describes how to use a ``git bisect`` to find the source code
change that broke something -- for example when some functionality stopped
working after upgrading from Linux 6.0 to 6.1.

The text focuses on the gist of the process. If you are new to bisecting the
kernel, better follow Documentation/admin-guide/verify-bugs-and-bisect-regressions.rst
instead: it depicts everything from start to finish while covering multiple
aspects even kernel developers occasionally forget. This includes detecting
situations early where a bisection would be a waste of time, as nobody would
care about the result -- for example, because the problem happens after the
kernel marked itself as 'tainted', occurs in an abandoned version, was already
fixed, or is caused by a .config change you or your Linux distributor performed.

Finding the change causing a kernel issue using a bisection
===========================================================

*Note: the following process assumes you prepared everything for a bisection.
This includes having a Git clone with the appropriate sources, installing the
software required to build and install kernels, as well as a .config file stored
in a safe place (the following example assumes '~/prepared_kernel_.config') to
use as pristine base at each bisection step; ideally, you have also worked out
a fully reliable and straight-forward way to reproduce the regression, too.*

* Preparation: start the bisection and tell Git about the points in the history
  you consider to be working and broken, which Git calls 'good' and 'bad'::

     git bisect start
     git bisect good v6.0
     git bisect bad v6.1

  Instead of Git tags like 'v6.0' and 'v6.1' you can specify commit-ids, too.

1. Copy your prepared .config into the build directory and adjust it to the
   needs of the codebase Git checked out for testing::

     cp ~/prepared_kernel_.config .config
     make olddefconfig

2. Now build, install, and boot a kernel. This might fail for unrelated reasons,
   for example, when a compile error happens at the current stage of the
   bisection a later change resolves. In such cases run ``git bisect skip`` and
   go back to step 1.

3. Check if the functionality that regressed works in the kernel you just built.

   If it works, execute::

     git bisect good

   If it is broken, run::

     git bisect bad

   Note, getting this wrong just once will send the rest of the bisection
   totally off course. To prevent having to start anew later you thus want to
   ensure what you tell Git is correct; it is thus often wise to spend a few
   minutes more on testing in case your reproducer is unreliable.

   After issuing one of these two commands, Git will usually check out another
   bisection point and print something like 'Bisecting: 675 revisions left to
   test after this (roughly 10 steps)'. In that case go back to step 1.

   If Git instead prints something like 'cafecaca0c0dacafecaca0c0dacafecaca0c0da
   is the first bad commit', then you have finished the bisection. In that case
   move to the next point below. Note, right after displaying that line Git will
   show some details about the culprit including its patch description; this can
   easily fill your terminal, so you might need to scroll up to see the message
   mentioning the culprit's commit-id.

   In case you missed Git's output, you can always run ``git bisect log`` to
   print the status: it will show how many steps remain or mention the result of
   the bisection.

* Recommended complementary task: put the bisection log and the current .config
  file aside for the bug report; furthermore tell Git to reset the sources to
  the state before the bisection::

     git bisect log > ~/bisection-log
     cp .config ~/bisection-config-culprit
     git bisect reset

* Recommended optional task: try reverting the culprit on top of the latest
  codebase and check if that fixes your bug; if that is the case, it validates
  the bisection and enables developers to resolve the regression through a
  revert.

  To try this, update your clone and check out latest mainline. Then tell Git
  to revert the change by specifying its commit-id::

     git revert --no-edit cafec0cacaca0

  Git might reject this, for example when the bisection landed on a merge
  commit. In that case, abandon the attempt. Do the same, if Git fails to revert
  the culprit on its own because later changes depend on it -- at least unless
  you bisected a stable or longterm kernel series, in which case you want to
  check out its latest codebase and try a revert there.

  If a revert succeeds, build and test another kernel to check if reverting
  resolved your regression.

With that the process is complete. Now report the regression as described by
Documentation/admin-guide/reporting-issues.rst.

Bisecting linux-next
--------------------

If you face a problem only happening in linux-next, bisect between the
linux-next branches 'stable' and 'master'. The following commands will start
the process for a linux-next tree you added as a remote called 'next'::

  git bisect start
  git bisect good next/stable
  git bisect bad next/master

The 'stable' branch refers to the state of linux-mainline that the current
linux-next release (found in the 'master' branch) is based on -- the former
thus should be free of any problems that show up in -next, but not in Linus'
tree.

This will bisect across a wide range of changes, some of which you might have
used in earlier linux-next releases without problems. Sadly there is no simple
way to avoid checking them: bisecting from one linux-next release to a later
one (say between 'next-20241020' and 'next-20241021') is impossible, as they
share no common history.

Additional reading material
---------------------------

* The `man page for 'git bisect' <https://git-scm.com/docs/git-bisect>`_ and
  `fighting regressions with 'git bisect' <https://git-scm.com/docs/git-bisect-lk2009.html>`_
  in the Git documentation.
* `Working with git bisect <https://nathanchance.dev/posts/working-with-git-bisect/>`_
  from kernel developer Nathan Chancellor.
* `Using Git bisect to figure out when brokenness was introduced <http://webchick.net/node/99>`_.
* `Fully automated bisecting with 'git bisect run' <https://lwn.net/Articles/317154>`_.

..
   end-of-content
..
   This document is maintained by Thorsten Leemhuis <linux@leemhuis.info>. If
   you spot a typo or small mistake, feel free to let him know directly and
   he'll fix it. You are free to do the same in a mostly informal way if you
   want to contribute changes to the text -- but for copyright reasons please CC
   linux-doc@vger.kernel.org and 'sign-off' your contribution as
   Documentation/process/submitting-patches.rst explains in the section 'Sign
   your work - the Developer's Certificate of Origin'.
..
   This text is available under GPL-2.0+ or CC-BY-4.0, as stated at the top
   of the file. If you want to distribute this text under CC-BY-4.0 only,
   please use 'The Linux kernel development community' for author attribution
   and link this as source:
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/admin-guide/bug-bisect.rst

..
   Note: Only the content of this RST file as found in the Linux kernel sources
   is available under CC-BY-4.0, as versions of this text that were processed
   (for example by the kernel's build system) might contain content taken from
   files which use a more restrictive license.

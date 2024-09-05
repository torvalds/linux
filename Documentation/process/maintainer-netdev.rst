.. SPDX-License-Identifier: GPL-2.0

.. _netdev-FAQ:

=============================
Networking subsystem (netdev)
=============================

tl;dr
-----

 - designate your patch to a tree - ``[PATCH net]`` or ``[PATCH net-next]``
 - for fixes the ``Fixes:`` tag is required, regardless of the tree
 - don't post large series (> 15 patches), break them up
 - don't repost your patches within one 24h period
 - reverse xmas tree

netdev
------

netdev is a mailing list for all network-related Linux stuff.  This
includes anything found under net/ (i.e. core code like IPv6) and
drivers/net (i.e. hardware specific drivers) in the Linux source tree.

Note that some subsystems (e.g. wireless drivers) which have a high
volume of traffic have their own specific mailing lists and trees.

Like many other Linux mailing lists, the netdev list is hosted at
kernel.org with archives available at https://lore.kernel.org/netdev/.

Aside from subsystems like those mentioned above, all network-related
Linux development (i.e. RFC, review, comments, etc.) takes place on
netdev.

Development cycle
-----------------

Here is a bit of background information on
the cadence of Linux development.  Each new release starts off with a
two week "merge window" where the main maintainers feed their new stuff
to Linus for merging into the mainline tree.  After the two weeks, the
merge window is closed, and it is called/tagged ``-rc1``.  No new
features get mainlined after this -- only fixes to the rc1 content are
expected.  After roughly a week of collecting fixes to the rc1 content,
rc2 is released.  This repeats on a roughly weekly basis until rc7
(typically; sometimes rc6 if things are quiet, or rc8 if things are in a
state of churn), and a week after the last vX.Y-rcN was done, the
official vX.Y is released.

To find out where we are now in the cycle - load the mainline (Linus)
page here:

  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

and note the top of the "tags" section.  If it is rc1, it is early in
the dev cycle.  If it was tagged rc7 a week ago, then a release is
probably imminent. If the most recent tag is a final release tag
(without an ``-rcN`` suffix) - we are most likely in a merge window
and ``net-next`` is closed.

git trees and patch flow
------------------------

There are two networking trees (git repositories) in play.  Both are
driven by David Miller, the main network maintainer.  There is the
``net`` tree, and the ``net-next`` tree.  As you can probably guess from
the names, the ``net`` tree is for fixes to existing code already in the
mainline tree from Linus, and ``net-next`` is where the new code goes
for the future release.  You can find the trees here:

- https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net.git
- https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net-next.git

Relating that to kernel development: At the beginning of the 2-week
merge window, the ``net-next`` tree will be closed - no new changes/features.
The accumulated new content of the past ~10 weeks will be passed onto
mainline/Linus via a pull request for vX.Y -- at the same time, the
``net`` tree will start accumulating fixes for this pulled content
relating to vX.Y

An announcement indicating when ``net-next`` has been closed is usually
sent to netdev, but knowing the above, you can predict that in advance.

.. warning::
  Do not send new ``net-next`` content to netdev during the
  period during which ``net-next`` tree is closed.

RFC patches sent for review only are obviously welcome at any time
(use ``--subject-prefix='RFC net-next'`` with ``git format-patch``).

Shortly after the two weeks have passed (and vX.Y-rc1 is released), the
tree for ``net-next`` reopens to collect content for the next (vX.Y+1)
release.

If you aren't subscribed to netdev and/or are simply unsure if
``net-next`` has re-opened yet, simply check the ``net-next`` git
repository link above for any new networking-related commits.  You may
also check the following website for the current status:

  https://netdev.bots.linux.dev/net-next.html

The ``net`` tree continues to collect fixes for the vX.Y content, and is
fed back to Linus at regular (~weekly) intervals.  Meaning that the
focus for ``net`` is on stabilization and bug fixes.

Finally, the vX.Y gets released, and the whole cycle starts over.

netdev patch review
-------------------

.. _patch_status:

Patch status
~~~~~~~~~~~~

Status of a patch can be checked by looking at the main patchwork
queue for netdev:

  https://patchwork.kernel.org/project/netdevbpf/list/

The "State" field will tell you exactly where things are at with your
patch:

================== =============================================================
Patch state        Description
================== =============================================================
New, Under review  pending review, patch is in the maintainer’s queue for
                   review; the two states are used interchangeably (depending on
                   the exact co-maintainer handling patchwork at the time)
Accepted           patch was applied to the appropriate networking tree, this is
                   usually set automatically by the pw-bot
Needs ACK          waiting for an ack from an area expert or testing
Changes requested  patch has not passed the review, new revision is expected
                   with appropriate code and commit message changes
Rejected           patch has been rejected and new revision is not expected
Not applicable     patch is expected to be applied outside of the networking
                   subsystem
Awaiting upstream  patch should be reviewed and handled by appropriate
                   sub-maintainer, who will send it on to the networking trees;
                   patches set to ``Awaiting upstream`` in netdev's patchwork
                   will usually remain in this state, whether the sub-maintainer
                   requested changes, accepted or rejected the patch
Deferred           patch needs to be reposted later, usually due to dependency
                   or because it was posted for a closed tree
Superseded         new version of the patch was posted, usually set by the
                   pw-bot
RFC                not to be applied, usually not in maintainer’s review queue,
                   pw-bot can automatically set patches to this state based
                   on subject tags
================== =============================================================

Patches are indexed by the ``Message-ID`` header of the emails
which carried them so if you have trouble finding your patch append
the value of ``Message-ID`` to the URL above.

Updating patch status
~~~~~~~~~~~~~~~~~~~~~

Contributors and reviewers do not have the permissions to update patch
state directly in patchwork. Patchwork doesn't expose much information
about the history of the state of patches, therefore having multiple
people update the state leads to confusion.

Instead of delegating patchwork permissions netdev uses a simple mail
bot which looks for special commands/lines within the emails sent to
the mailing list. For example to mark a series as Changes Requested
one needs to send the following line anywhere in the email thread::

  pw-bot: changes-requested

As a result the bot will set the entire series to Changes Requested.
This may be useful when author discovers a bug in their own series
and wants to prevent it from getting applied.

The use of the bot is entirely optional, if in doubt ignore its existence
completely. Maintainers will classify and update the state of the patches
themselves. No email should ever be sent to the list with the main purpose
of communicating with the bot, the bot commands should be seen as metadata.

The use of the bot is restricted to authors of the patches (the ``From:``
header on patch submission and command must match!), maintainers of
the modified code according to the MAINTAINERS file (again, ``From:``
must match the MAINTAINERS entry) and a handful of senior reviewers.

Bot records its activity here:

  https://netdev.bots.linux.dev/pw-bot.html

Review timelines
~~~~~~~~~~~~~~~~

Generally speaking, the patches get triaged quickly (in less than
48h). But be patient, if your patch is active in patchwork (i.e. it's
listed on the project's patch list) the chances it was missed are close to zero.

The high volume of development on netdev makes reviewers move on
from discussions relatively quickly. New comments and replies
are very unlikely to arrive after a week of silence. If a patch
is no longer active in patchwork and the thread went idle for more
than a week - clarify the next steps and/or post the next version.

For RFC postings specifically, if nobody responded in a week - reviewers
either missed the posting or have no strong opinions. If the code is ready,
repost as a PATCH.

Emails saying just "ping" or "bump" are considered rude. If you can't figure
out the status of the patch from patchwork or where the discussion has
landed - describe your best guess and ask if it's correct. For example::

  I don't understand what the next steps are. Person X seems to be unhappy
  with A, should I do B and repost the patches?

.. _Changes requested:

Changes requested
~~~~~~~~~~~~~~~~~

Patches :ref:`marked<patch_status>` as ``Changes Requested`` need
to be revised. The new version should come with a change log,
preferably including links to previous postings, for example::

  [PATCH net-next v3] net: make cows go moo

  Even users who don't drink milk appreciate hearing the cows go "moo".

  The amount of mooing will depend on packet rate so should match
  the diurnal cycle quite well.

  Signed-off-by: Joe Defarmer <joe@barn.org>
  ---
  v3:
    - add a note about time-of-day mooing fluctuation to the commit message
  v2: https://lore.kernel.org/netdev/123themessageid@barn.org/
    - fix missing argument in kernel doc for netif_is_bovine()
    - fix memory leak in netdev_register_cow()
  v1: https://lore.kernel.org/netdev/456getstheclicks@barn.org/

The commit message should be revised to answer any questions reviewers
had to ask in previous discussions. Occasionally the update of
the commit message will be the only change in the new version.

Partial resends
~~~~~~~~~~~~~~~

Please always resend the entire patch series and make sure you do number your
patches such that it is clear this is the latest and greatest set of patches
that can be applied. Do not try to resend just the patches which changed.

Handling misapplied patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Occasionally a patch series gets applied before receiving critical feedback,
or the wrong version of a series gets applied.

Making the patch disappear once it is pushed out is not possible, the commit
history in netdev trees is immutable.
Please send incremental versions on top of what has been merged in order to fix
the patches the way they would look like if your latest patch series was to be
merged.

In cases where full revert is needed the revert has to be submitted
as a patch to the list with a commit message explaining the technical
problems with the reverted commit. Reverts should be used as a last resort,
when original change is completely wrong; incremental fixes are preferred.

Stable tree
~~~~~~~~~~~

While it used to be the case that netdev submissions were not supposed
to carry explicit ``CC: stable@vger.kernel.org`` tags that is no longer
the case today. Please follow the standard stable rules in
:ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`,
and make sure you include appropriate Fixes tags!

Security fixes
~~~~~~~~~~~~~~

Do not email netdev maintainers directly if you think you discovered
a bug that might have possible security implications.
The current netdev maintainer has consistently requested that
people use the mailing lists and not reach out directly.  If you aren't
OK with that, then perhaps consider mailing security@kernel.org or
reading about http://oss-security.openwall.org/wiki/mailing-lists/distros
as possible alternative mechanisms.


Co-posting changes to user space components
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

User space code exercising kernel features should be posted
alongside kernel patches. This gives reviewers a chance to see
how any new interface is used and how well it works.

When user space tools reside in the kernel repo itself all changes
should generally come as one series. If series becomes too large
or the user space project is not reviewed on netdev include a link
to a public repo where user space patches can be seen.

In case user space tooling lives in a separate repository but is
reviewed on netdev  (e.g. patches to ``iproute2`` tools) kernel and
user space patches should form separate series (threads) when posted
to the mailing list, e.g.::

  [PATCH net-next 0/3] net: some feature cover letter
   └─ [PATCH net-next 1/3] net: some feature prep
   └─ [PATCH net-next 2/3] net: some feature do it
   └─ [PATCH net-next 3/3] selftest: net: some feature

  [PATCH iproute2-next] ip: add support for some feature

Posting as one thread is discouraged because it confuses patchwork
(as of patchwork 2.2.2).

Preparing changes
-----------------

Attention to detail is important.  Re-read your own work as if you were the
reviewer.  You can start with using ``checkpatch.pl``, perhaps even with
the ``--strict`` flag.  But do not be mindlessly robotic in doing so.
If your change is a bug fix, make sure your commit log indicates the
end-user visible symptom, the underlying reason as to why it happens,
and then if necessary, explain why the fix proposed is the best way to
get things done.  Don't mangle whitespace, and as is common, don't
mis-indent function arguments that span multiple lines.  If it is your
first patch, mail it to yourself so you can test apply it to an
unpatched tree to confirm infrastructure didn't mangle it.

Finally, go back and read
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
to be sure you are not repeating some common mistake documented there.

Indicating target tree
~~~~~~~~~~~~~~~~~~~~~~

To help maintainers and CI bots you should explicitly mark which tree
your patch is targeting. Assuming that you use git, use the prefix
flag::

  git format-patch --subject-prefix='PATCH net-next' start..finish

Use ``net`` instead of ``net-next`` (always lower case) in the above for
bug-fix ``net`` content.

Dividing work into patches
~~~~~~~~~~~~~~~~~~~~~~~~~~

Put yourself in the shoes of the reviewer. Each patch is read separately
and therefore should constitute a comprehensible step towards your stated
goal.

Avoid sending series longer than 15 patches. Larger series takes longer
to review as reviewers will defer looking at it until they find a large
chunk of time. A small series can be reviewed in a short time, so Maintainers
just do it. As a result, a sequence of smaller series gets merged quicker and
with better review coverage. Re-posting large series also increases the mailing
list traffic.

Local variable ordering ("reverse xmas tree", "RCS")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Netdev has a convention for ordering local variables in functions.
Order the variable declaration lines longest to shortest, e.g.::

  struct scatterlist *sg;
  struct sk_buff *skb;
  int err, i;

If there are dependencies between the variables preventing the ordering
move the initialization out of line.

Format precedence
~~~~~~~~~~~~~~~~~

When working in existing code which uses nonstandard formatting make
your code follow the most recent guidelines, so that eventually all code
in the domain of netdev is in the preferred format.

Resending after review
~~~~~~~~~~~~~~~~~~~~~~

Allow at least 24 hours to pass between postings. This will ensure reviewers
from all geographical locations have a chance to chime in. Do not wait
too long (weeks) between postings either as it will make it harder for reviewers
to recall all the context.

Make sure you address all the feedback in your new posting. Do not post a new
version of the code if the discussion about the previous version is still
ongoing, unless directly instructed by a reviewer.

The new version of patches should be posted as a separate thread,
not as a reply to the previous posting. Change log should include a link
to the previous posting (see :ref:`Changes requested`).

Testing
-------

Expected level of testing
~~~~~~~~~~~~~~~~~~~~~~~~~

At the very minimum your changes must survive an ``allyesconfig`` and an
``allmodconfig`` build with ``W=1`` set without new warnings or failures.

Ideally you will have done run-time testing specific to your change,
and the patch series contains a set of kernel selftest for
``tools/testing/selftests/net`` or using the KUnit framework.

You are expected to test your changes on top of the relevant networking
tree (``net`` or ``net-next``) and not e.g. a stable tree or ``linux-next``.

patchwork checks
~~~~~~~~~~~~~~~~

Checks in patchwork are mostly simple wrappers around existing kernel
scripts, the sources are available at:

https://github.com/linux-netdev/nipa/tree/master/tests

**Do not** post your patches just to run them through the checks.
You must ensure that your patches are ready by testing them locally
before posting to the mailing list. The patchwork build bot instance
gets overloaded very easily and netdev@vger really doesn't need more
traffic if we can help it.

netdevsim
~~~~~~~~~

``netdevsim`` is a test driver which can be used to exercise driver
configuration APIs without requiring capable hardware.
Mock-ups and tests based on ``netdevsim`` are strongly encouraged when
adding new APIs, but ``netdevsim`` in itself is **not** considered
a use case/user. You must also implement the new APIs in a real driver.

We give no guarantees that ``netdevsim`` won't change in the future
in a way which would break what would normally be considered uAPI.

``netdevsim`` is reserved for use by upstream tests only, so any
new ``netdevsim`` features must be accompanied by selftests under
``tools/testing/selftests/``.

Reviewer guidance
-----------------

Reviewing other people's patches on the list is highly encouraged,
regardless of the level of expertise. For general guidance and
helpful tips please see :ref:`development_advancedtopics_reviews`.

It's safe to assume that netdev maintainers know the community and the level
of expertise of the reviewers. The reviewers should not be concerned about
their comments impeding or derailing the patch flow.

Less experienced reviewers are highly encouraged to do more in-depth
review of submissions and not focus exclusively on trivial or subjective
matters like code formatting, tags etc.

Testimonials / feedback
-----------------------

Some companies use peer feedback in employee performance reviews.
Please feel free to request feedback from netdev maintainers,
especially if you spend significant amount of time reviewing code
and go out of your way to improve shared infrastructure.

The feedback must be requested by you, the contributor, and will always
be shared with you (even if you request for it to be submitted to your
manager).

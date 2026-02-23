.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)
.. See the bottom of this file for additional redistribution information.

Handling regressions
++++++++++++++++++++

*We don't cause regressions* -- this document describes what this "first rule of
Linux kernel development" means in practice for developers. It complements
Documentation/admin-guide/reporting-regressions.rst, which covers the topic from a
user's point of view; if you never read that text, go and at least skim over it
before continuing here.

The important bits (aka "The TL;DR")
====================================

#. Ensure subscribers of the `regression mailing list <https://lore.kernel.org/regressions/>`_
   (regressions@lists.linux.dev) quickly become aware of any new regression
   report:

    * When receiving a mailed report that did not CC the list, bring it into the
      loop by immediately sending at least a brief "Reply-all" with the list
      CCed.

    * Forward or bounce any reports submitted in bug trackers to the list.

#. Make the Linux kernel regression tracking bot "regzbot" track the issue (this
   is optional, but recommended):

    * For mailed reports, check if the reporter included a line like ``#regzbot
      introduced: v5.13..v5.14-rc1``. If not, send a reply (with the regressions
      list in CC) containing a paragraph like the following, which tells regzbot
      when the issue started to happen::

       #regzbot ^introduced: 1f2e3d4c5b6a

    * When forwarding reports from a bug tracker to the regressions list (see
      above), include a paragraph like the following::

       #regzbot introduced: v5.13..v5.14-rc1
       #regzbot from: Some N. Ice Human <some.human@example.com>
       #regzbot monitor: http://some.bugtracker.example.com/ticket?id=123456789

#. When submitting fixes for regressions, add "Closes:" tags to the patch
   description pointing to all places where the issue was reported, as
   mandated by Documentation/process/submitting-patches.rst and
   :ref:`Documentation/process/5.Posting.rst <development_posting>`. If you are
   only fixing part of the issue that caused the regression, you may use
   "Link:" tags instead. regzbot currently makes no distinction between the
   two.

#. Try to fix regressions quickly once the culprit has been identified; fixes
   for most regressions should be merged within two weeks, but some need to be
   resolved within two or three days.


All the details on Linux kernel regressions relevant for developers
===================================================================


The important basics in more detail
-----------------------------------


What to do when receiving regression reports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ensure the Linux kernel's regression tracker and others subscribers of the
`regression mailing list <https://lore.kernel.org/regressions/>`_
(regressions@lists.linux.dev) become aware of any newly reported regression:

 * When you receive a report by mail that did not CC the list, immediately bring
   it into the loop by sending at least a brief "Reply-all" with the list CCed;
   try to ensure it gets CCed again in case you reply to a reply that omitted
   the list.

 * If a report submitted in a bug tracker hits your Inbox, forward or bounce it
   to the list. Consider checking the list archives beforehand, if the reporter
   already forwarded the report as instructed by
   Documentation/admin-guide/reporting-issues.rst.

When doing either, consider making the Linux kernel regression tracking bot
"regzbot" immediately start tracking the issue:

 * For mailed reports, check if the reporter included a "regzbot command" like
   ``#regzbot introduced: 1f2e3d4c5b6a``. If not, send a reply (with the
   regressions list in CC) with a paragraph like the following:::

       #regzbot ^introduced: v5.13..v5.14-rc1

   This tells regzbot the version range in which the issue started to happen;
   you can specify a range using commit-ids as well or state a single commit-id
   in case the reporter bisected the culprit.

   Note the caret (^) before the "introduced": it tells regzbot to treat the
   parent mail (the one you reply to) as the initial report for the regression
   you want to see tracked; that's important, as regzbot will later look out
   for patches with "Closes:" tags pointing to the report in the archives on
   lore.kernel.org.

 * When forwarding a regression reported to a bug tracker, include a paragraph
   with these regzbot commands::

       #regzbot introduced: 1f2e3d4c5b6a
       #regzbot from: Some N. Ice Human <some.human@example.com>
       #regzbot monitor: http://some.bugtracker.example.com/ticket?id=123456789

   Regzbot will then automatically associate patches with the report that
   contain "Closes:" tags pointing to your mail or the mentioned ticket.

What's important when fixing regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You don't need to do anything special when submitting fixes for regression, just
remember to do what Documentation/process/submitting-patches.rst,
:ref:`Documentation/process/5.Posting.rst <development_posting>`, and
Documentation/process/stable-kernel-rules.rst already explain in more detail:

 * Point to all places where the issue was reported using "Closes:" tags::

       Closes: https://lore.kernel.org/r/30th.anniversary.repost@klaava.Helsinki.FI/
       Closes: https://bugzilla.kernel.org/show_bug.cgi?id=1234567890

   If you are only fixing part of the issue, you may use "Link:" instead as
   described in the first document mentioned above. regzbot currently treats
   both of these equivalently and considers the linked reports as resolved.

 * Add a "Fixes:" tag to specify the commit causing the regression.

 * If the culprit was merged in an earlier development cycle, explicitly mark
   the fix for backporting using the ``Cc: stable@vger.kernel.org`` tag.

All this is expected from you and important when it comes to regression, as
these tags are of great value for everyone (you included) that might be looking
into the issue weeks, months, or years later. These tags are also crucial for
tools and scripts used by other kernel developers or Linux distributions; one of
these tools is regzbot, which heavily relies on the "Closes:" tags to associate
reports for regression with changes resolving them.

Expectations and best practices for fixing regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As a Linux kernel developer, you are expected to give your best to prevent
situations where a regression caused by a recent change of yours leaves users
only these options:

 * Run a kernel with a regression that impacts usage.

 * Switch to an older or newer kernel series.

 * Continue running an outdated and thus potentially insecure kernel for more
   than three weeks after the regression's culprit was identified. Ideally it
   should be less than two. And it ought to be just a few days, if the issue is
   severe or affects many users -- either in general or in prevalent
   environments.

How to realize that in practice depends on various factors. Use the following
rules of thumb as a guide.

In general:

 * Prioritize work on regressions over all other Linux kernel work, unless the
   latter concerns a severe issue (e.g. acute security vulnerability, data loss,
   bricked hardware, ...).

 * Expedite fixing mainline regressions that recently made it into a proper
   mainline, stable, or longterm release (either directly or via backport).

 * Do not consider regressions from the current cycle as something that can wait
   till the end of the cycle, as the issue might discourage or prevent users and
   CI systems from testing mainline now or generally.

 * Work with the required care to avoid additional or bigger damage, even if
   resolving an issue then might take longer than outlined below.

On timing once the culprit of a regression is known:

 * Aim to mainline a fix within two or three days, if the issue is severe or
   bothering many users -- either in general or in prevalent conditions like a
   particular hardware environment, distribution, or stable/longterm series.

 * Aim to mainline a fix by Sunday after the next, if the culprit made it
   into a recent mainline, stable, or longterm release (either directly or via
   backport); if the culprit became known early during a week and is simple to
   resolve, try to mainline the fix within the same week.

 * For other regressions, aim to mainline fixes before the hindmost Sunday
   within the next three weeks. One or two Sundays later are acceptable, if the
   regression is something people can live with easily for a while -- like a
   mild performance regression.

 * It's strongly discouraged to delay mainlining regression fixes till the next
   merge window, except when the fix is extraordinarily risky or when the
   culprit was mainlined more than a year ago.

On procedure:

 * Always consider reverting the culprit, as it's often the quickest and least
   dangerous way to fix a regression. Don't worry about mainlining a fixed
   variant later: that should be straight-forward, as most of the code went
   through review once already.

 * Try to resolve any regressions introduced in mainline during the past
   twelve months before the current development cycle ends: Linus wants such
   regressions to be handled like those from the current cycle, unless fixing
   bears unusual risks.

 * Consider CCing Linus on discussions or patch review, if a regression seems
   tangly. Do the same in precarious or urgent cases -- especially if the
   subsystem maintainer might be unavailable. Also CC the stable team, when you
   know such a regression made it into a mainline, stable, or longterm release.

 * For urgent regressions, consider asking Linus to pick up the fix straight
   from the mailing list: he is totally fine with that for uncontroversial
   fixes. Ideally though such requests should happen in accordance with the
   subsystem maintainers or come directly from them.

 * In case you are unsure if a fix is worth the risk applying just days before
   a new mainline release, send Linus a mail with the usual lists and people in
   CC; in it, summarize the situation while asking him to consider picking up
   the fix straight from the list. He then himself can make the call and when
   needed even postpone the release. Such requests again should ideally happen
   in accordance with the subsystem maintainers or come directly from them.

Regarding stable and longterm kernels:

 * You are free to leave regressions to the stable team, if they at no point in
   time occurred with mainline or were fixed there already.

 * If a regression made it into a proper mainline release during the past
   twelve months, ensure to tag the fix with "Cc: stable@vger.kernel.org", as a
   "Fixes:" tag alone does not guarantee a backport. Please add the same tag,
   in case you know the culprit was backported to stable or longterm kernels.

 * When receiving reports about regressions in recent stable or longterm kernel
   series, please evaluate at least briefly if the issue might happen in current
   mainline as well -- and if that seems likely, take hold of the report. If in
   doubt, ask the reporter to check mainline.

 * Whenever you want to swiftly resolve a regression that recently also made it
   into a proper mainline, stable, or longterm release, fix it quickly in
   mainline; when appropriate thus involve Linus to fast-track the fix (see
   above). That's because the stable team normally does neither revert nor fix
   any changes that cause the same problems in mainline.

 * In case of urgent regression fixes you might want to ensure prompt
   backporting by dropping the stable team a note once the fix was mainlined;
   this is especially advisable during merge windows and shortly thereafter, as
   the fix otherwise might land at the end of a huge patch queue.

On patch flow:

 * Developers, when trying to reach the time periods mentioned above, remember
   to account for the time it takes to get fixes tested, reviewed, and merged by
   Linus, ideally with them being in linux-next at least briefly. Hence, if a
   fix is urgent, make it obvious to ensure others handle it appropriately.

 * Reviewers, you are kindly asked to assist developers in reaching the time
   periods mentioned above by reviewing regression fixes in a timely manner.

 * Subsystem maintainers, you likewise are encouraged to expedite the handling
   of regression fixes. Thus evaluate if skipping linux-next is an option for
   the particular fix. Also consider sending git pull requests more often than
   usual when needed. And try to avoid holding onto regression fixes over
   weekends -- especially when the fix is marked for backporting.


More aspects regarding regressions developers should be aware of
----------------------------------------------------------------


How to deal with changes where a risk of regression is known
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Evaluate how big the risk of regressions is, for example by performing a code
search in Linux distributions and Git forges. Also consider asking other
developers or projects likely to be affected to evaluate or even test the
proposed change; if problems surface, maybe some solution acceptable for all
can be found.

If the risk of regressions in the end seems to be relatively small, go ahead
with the change, but let all involved parties know about the risk. Hence, make
sure your patch description makes this aspect obvious. Once the change is
merged, tell the Linux kernel's regression tracker and the regressions mailing
list about the risk, so everyone has the change on the radar in case reports
trickle in. Depending on the risk, you also might want to ask the subsystem
maintainer to mention the issue in his mainline pull request.

What else is there to known about regressions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Check out Documentation/admin-guide/reporting-regressions.rst, it covers a lot
of other aspects you want might want to be aware of:

 * the purpose of the "no regressions" rule

 * what issues actually qualify as regression

 * who's in charge for finding the root cause of a regression

 * how to handle tricky situations, e.g. when a regression is caused by a
   security fix or when fixing a regression might cause another one

Whom to ask for advice when it comes to regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Send a mail to the regressions mailing list (regressions@lists.linux.dev) while
CCing the Linux kernel's regression tracker (regressions@leemhuis.info); if the
issue might better be dealt with in private, feel free to omit the list.


More about regression tracking and regzbot
------------------------------------------


Why the Linux kernel has a regression tracker, and why is regzbot used?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rules like "no regressions" need someone to ensure they are followed, otherwise
they are broken either accidentally or on purpose. History has shown this to be
true for the Linux kernel as well. That's why Thorsten Leemhuis volunteered to
keep an eye on things as the Linux kernel's regression tracker, who's
occasionally helped by other people. Neither of them are paid to do this,
that's why regression tracking is done on a best effort basis.

Earlier attempts to manually track regressions have shown it's an exhausting and
frustrating work, which is why they were abandoned after a while. To prevent
this from happening again, Thorsten developed regzbot to facilitate the work,
with the long term goal to automate regression tracking as much as possible for
everyone involved.

How does regression tracking work with regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bot watches for replies to reports of tracked regressions. Additionally,
it's looking out for posted or committed patches referencing such reports
with "Closes:" tags; replies to such patch postings are tracked as well.
Combined this data provides good insights into the current state of the fixing
process.

Regzbot tries to do its job with as little overhead as possible for both
reporters and developers. In fact, only reporters are burdened with an extra
duty: they need to tell regzbot about the regression report using the ``#regzbot
introduced`` command outlined above; if they don't do that, someone else can
take care of that using ``#regzbot ^introduced``.

For developers there normally is no extra work involved, they just need to make
sure to do something that was expected long before regzbot came to light: add
links to the patch description pointing to all reports about the issue fixed.

Do I have to use regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~

It's in the interest of everyone if you do, as kernel maintainers like Linus
Torvalds partly rely on regzbot's tracking in their work -- for example when
deciding to release a new version or extend the development phase. For this they
need to be aware of all unfixed regression; to do that, Linus is known to look
into the weekly reports sent by regzbot.

Do I have to tell regzbot about every regression I stumble upon?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ideally yes: we are all humans and easily forget problems when something more
important unexpectedly comes up -- for example a bigger problem in the Linux
kernel or something in real life that's keeping us away from keyboards for a
while. Hence, it's best to tell regzbot about every regression, except when you
immediately write a fix and commit it to a tree regularly merged to the affected
kernel series.

How to see which regressions regzbot tracks currently?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Check `regzbot's web-interface <https://linux-regtracking.leemhuis.info/regzbot/>`_
for the latest info; alternatively, `search for the latest regression report
<https://lore.kernel.org/lkml/?q=%22Linux+regressions+report%22+f%3Aregzbot>`_,
which regzbot normally sends out once a week on Sunday evening (UTC), which is a
few hours before Linus usually publishes new (pre-)releases.

What places is regzbot monitoring?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Regzbot is watching the most important Linux mailing lists as well as the git
repositories of linux-next, mainline, and stable/longterm.

What kind of issues are supposed to be tracked by regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bot is meant to track regressions, hence please don't involve regzbot for
regular issues. But it's okay for the Linux kernel's regression tracker if you
use regzbot to track severe issues, like reports about hangs, corrupted data,
or internal errors (Panic, Oops, BUG(), warning, ...).

Can I add regressions found by CI systems to regzbot's tracking?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Feel free to do so, if the particular regression likely has impact on practical
use cases and thus might be noticed by users; hence, please don't involve
regzbot for theoretical regressions unlikely to show themselves in real world
usage.

How to interact with regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By using a 'regzbot command' in a direct or indirect reply to the mail with the
regression report. These commands need to be in their own paragraph (IOW: they
need to be separated from the rest of the mail using blank lines).

One such command is ``#regzbot introduced: <version or commit>``, which makes
regzbot consider your mail as a regressions report added to the tracking, as
already described above; ``#regzbot ^introduced: <version or commit>`` is another
such command, which makes regzbot consider the parent mail as a report for a
regression which it starts to track.

Once one of those two commands has been utilized, other regzbot commands can be
used in direct or indirect replies to the report. You can write them below one
of the `introduced` commands or in replies to the mail that used one of them
or itself is a reply to that mail:

 * Set or update the title::

       #regzbot title: foo

 * Monitor a discussion or bugzilla.kernel.org ticket where additions aspects of
   the issue or a fix are discussed -- for example the posting of a patch fixing
   the regression::

       #regzbot monitor: https://lore.kernel.org/all/30th.anniversary.repost@klaava.Helsinki.FI/

   Monitoring only works for lore.kernel.org and bugzilla.kernel.org; regzbot
   will consider all messages in that thread or ticket as related to the fixing
   process.

 * Point to a place with further details of interest, like a mailing list post
   or a ticket in a bug tracker that are slightly related, but about a different
   topic::

       #regzbot link: https://bugzilla.kernel.org/show_bug.cgi?id=123456789

 * Mark a regression as fixed by a commit that is heading upstream or already
   landed::

       #regzbot fix: 1f2e3d4c5d

 * Mark a regression as a duplicate of another one already tracked by regzbot::

       #regzbot dup-of: https://lore.kernel.org/all/30th.anniversary.repost@klaava.Helsinki.FI/

 * Mark a regression as invalid::

       #regzbot invalid: wasn't a regression, problem has always existed

Is there more to tell about regzbot and its commands?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

More detailed and up-to-date information about the Linux
kernel's regression tracking bot can be found on its
`project page <https://gitlab.com/knurd42/regzbot>`_, which among others
contains a `getting started guide <https://gitlab.com/knurd42/regzbot/-/blob/main/docs/getting_started.md>`_
and `reference documentation <https://gitlab.com/knurd42/regzbot/-/blob/main/docs/reference.md>`_
which both cover more details than the above section.

Quotes from Linus about regression
----------------------------------

The following statements from Linus Torvalds provide some insight into Linux
"no regressions" rule and how he expects regressions to be handled:

On how quickly regressions should be fixed
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-22 <https://lore.kernel.org/all/CAHk-=wheQNiW_WtHGO7bKkT7Uib-p+ai2JP9M+z+FYcZ6CAxYA@mail.gmail.com/>`_::

    But a user complaining should basically result in an immediate fix -
    possibly a "revert and rethink".

  With a later clarification on `2026-01-28 <https://lore.kernel.org/all/CAHk-%3Dwi86AosXs66-yi54%2BmpQjPu0upxB8ZAfG%2BLsMyJmcuMSA@mail.gmail.com/>`_::

    It's also worth noting that "immediate" obviously doesn't mean "right
    this *second* when the problem has been reported".

    But if it's a regression with a known commit that caused it, I think
    the rule of thumb should generally be "within a week", preferably
    before the next rc.

* From `2023-04-21 <https://lore.kernel.org/all/CAHk-=wgD98pmSK3ZyHk_d9kZ2bhgN6DuNZMAJaV0WTtbkf=RDw@mail.gmail.com/>`_::

    Known-broken commits either
     (a) get a timely fix that doesn't have other questions
    or
     (b) get reverted

* From `2021-09-20(2) <https://lore.kernel.org/all/CAHk-=wgOvmtRw1TNbMC1rn5YqyTKyn0hz+sc4k0DGNn++u9aYw@mail.gmail.com/>`_::

    [...] review shouldn't hold up reported regressions of existing code. That's
    just basic _testing_ - either the fix should be applied, or - if the fix is
    too invasive or too ugly - the problematic source of the regression should
    be reverted.

    Review should be about new code, it shouldn't be holding up "there's a
    bug report, here's the obvious fix".

* From `2023-05-08 <https://lore.kernel.org/all/CAHk-=wgzU8_dGn0Yg+DyX7ammTkDUCyEJ4C=NvnHRhxKWC7Wpw@mail.gmail.com/>`_::

    If something doesn't even build, it should damn well be fixed ASAP.

On how fixing regressions with reverts can help prevent maintainer burnout
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-28 <https://lore.kernel.org/all/CAHk-%3Dwi86AosXs66-yi54%2BmpQjPu0upxB8ZAfG%2BLsMyJmcuMSA@mail.gmail.com/>`_::

    > So how can I/we make "immediate fixes" happen more often without
    > contributing to maintainer burnout?

    [...] the "revert and rethink" model [...] often a good idea in general [...]

    Exactly so that maintainers don't get stressed out over having a pending
    problem report that people keep pestering them about.

    I think people are sometimes a bit too bought into whatever changes
    they made, and reverting is seen as "too drastic", but I think it's
    often the quick and easy solution for when there isn't some obvious
    response to a regression report.

On mainlining fixes when the last -rc or a new release is close
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-02-01 <https://lore.kernel.org/all/CAHk-%3DwhXTw1oPsa%2BTLuY1Rc9D1OAiPVOdR_-R2xG45kwDObKdA@mail.gmail.com/>`_::

    So I think I'd rather see them hit rc8 (later today) and have a week
    of testing in my tree and be reverted if they cause problems, than
    have them go in after rc8 and then cause problems in the 6.19 release
    instead.

* From `2023-04-20 <https://lore.kernel.org/all/CAHk-=wis_qQy4oDNynNKi5b7Qhosmxtoj1jxo5wmB6SRUwQUBQ@mail.gmail.com/>`_::

    But something like this, where the regression was in the previous release
    and it's just a clear fix with no semantic subtlety, I consider to be just a
    regular regression that should be expedited - partly to make it into stable,
    and partly to avoid having to put the fix into _another_ stable kernel.

On sending merge requests with just one fix
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2024-04-24 <https://lore.kernel.org/all/CAHk-=wjy_ph9URuFt-pq+2AJ__p7gFDx=yzVSCsx16xAYvNw9g@mail.gmail.com/>`_::

    If the issue is just that there's nothing else happening, I think people
    should just point me to the patch and say "can you apply this single fix?"

* From `2023-04-20 <https://lore.kernel.org/all/CAHk-=wis_qQy4oDNynNKi5b7Qhosmxtoj1jxo5wmB6SRUwQUBQ@mail.gmail.com/>`_::

    I'm always open to direct fixes when there is no controversy about the fix.
    No problem. I still happily deal with individual patches.

On the importance of pointing to bug reports using Link:/Closes: tags
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2025-07-29(1) <https://lore.kernel.org/all/CAHk-=wj2kJRPWx8B09AAtzj+_g+T6UBX11TP0ebs1WJdTtv=WQ@mail.gmail.com/>`_::

    [...] revert like this, it really would be good to link to the problems, so
    that when people try to re-enable it, they have the history for why it
    didn't work the first time.

* From `2022-05-08 <https://lore.kernel.org/all/CAHk-=wjMmSZzMJ3Xnskdg4+GGz=5p5p+GSYyFBTh0f-DgvdBWg@mail.gmail.com/>`_::

    So I have to once more complain [...]

    [...] There's no link to the actual problem the patch fixes.

* From `2022-06-22 <https://lore.kernel.org/all/CAHk-=wjxzafG-=J8oT30s7upn4RhBs6TX-uVFZ5rME+L5_DoJA@mail.gmail.com/>`_::

    See, *that* link [to the report] would have been useful in the commit.

On why the "no regressions" rule exists
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-22 <https://lore.kernel.org/all/CAHk-=wheQNiW_WtHGO7bKkT7Uib-p+ai2JP9M+z+FYcZ6CAxYA@mail.gmail.com/>`_::

    But the basic rule is: be so good about backwards compatibility that
    users never have to worry about upgrading. They should absolutely feel
    confident that any kernel-reported problem will either be solved, or
    have an easy solution that is appropriate for *them* (ie a
    non-technical user shouldn't be expected to be able to do a lot).

    Because the last thing we want is people holding back from trying new
    kernels.

* From `2024-05-28 <https://lore.kernel.org/all/CAHk-=wgtb7y-bEh7tPDvDWru7ZKQ8-KMjZ53Tsk37zsPPdwXbA@mail.gmail.com/>`_::

    I introduced that "no regressions" rule something like two decades
    ago, because people need to be able to update their kernel without
    fear of something they relied on suddenly stopping to work.

* From `2018-08-03 <https://lore.kernel.org/all/CA+55aFwWZX=CXmWDTkDGb36kf12XmTehmQjbiMPCqCRG2hi9kw@mail.gmail.com/>`_::

    The whole point of "we do not regress" is so that people can upgrade
    the kernel and never have to worry about it.

    [...]

    Because the only thing that matters IS THE USER.

* From `2017-10-26(1) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

    If the kernel used to work for you, the rule is that it continues to work
    for you.

    [...]

    People should basically always feel like they can update their kernel
    and simply not have to worry about it.

    I refuse to introduce "you can only update the kernel if you also
    update that other program" kind of limitations. If the kernel used to
    work for you, the rule is that it continues to work for you.

On exceptions to the "no regressions" rule
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-22 <https://lore.kernel.org/all/CAHk-=wheQNiW_WtHGO7bKkT7Uib-p+ai2JP9M+z+FYcZ6CAxYA@mail.gmail.com/>`_::

    There are _very_ few exceptions to that rule, the main one being "the
    problem was a fundamental huge and gaping security issue and we *had* to
    make that change, and we couldn't even make your limited use-case just
    continue to work".

    The other exception is "the problem was reported years after it was
    introduced, and now most people rely on the new behavior".

    [...]

    Now, if it's one or two users and you can just get them to recompile,
    that's one thing. Niche hardware and odd use-cases can sometimes be
    solved that way, and regressions can sometimes be fixed by handholding
    every single reporter if the reporter is willing and able to change
    his or her workflow.

* From `2023-04-20 <https://lore.kernel.org/all/CAHk-=wis_qQy4oDNynNKi5b7Qhosmxtoj1jxo5wmB6SRUwQUBQ@mail.gmail.com/>`_::

    And yes, I do consider "regression in an earlier release" to be a
    regression that needs fixing.

    There's obviously a time limit: if that "regression in an earlier
    release" was a year or more ago, and just took forever for people to
    notice, and it had semantic changes that now mean that fixing the
    regression could cause a _new_ regression, then that can cause me to
    go "Oh, now the new semantics are what we have to live with".

* From `2017-10-26(2) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

    There have been exceptions, but they are few and far between, and they
    generally have some major and fundamental reasons for having happened,
    that were basically entirely unavoidable, and people _tried_hard_ to
    avoid them. Maybe we can't practically support the hardware any more
    after it is decades old and nobody uses it with modern kernels any
    more. Maybe there's a serious security issue with how we did things,
    and people actually depended on that fundamentally broken model. Maybe
    there was some fundamental other breakage that just _had_ to have a
    flag day for very core and fundamental reasons.

On situations where updating something in userspace can resolve regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2018-08-03 <https://lore.kernel.org/all/CA+55aFwWZX=CXmWDTkDGb36kf12XmTehmQjbiMPCqCRG2hi9kw@mail.gmail.com/>`_::

    And dammit, we upgrade the kernel ALL THE TIME without upgrading any
    other programs at all. It is absolutely required, because flag-days
    and dependencies are horribly bad.

    And it is also required simply because I as a kernel developer do not
    upgrade random other tools that I don't even care about as I develop the
    kernel, and I want any of my users to feel safe doing the same time.

* From `2017-10-26(3) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

    But if something actually breaks, then the change must get fixed or
    reverted. And it gets fixed in the *kernel*. Not by saying "well, fix your
    user space then". It was a kernel change that exposed the problem, it needs
    to be the kernel that corrects for it, because we have a "upgrade in place"
    model. We don't have a "upgrade with new user space".

    And I seriously will refuse to take code from people who do not understand
    and honor this very simple rule.

    This rule is also not going to change.

    And yes, I realize that the kernel is "special" in this respect. I'm proud
    of it.

* From `2017-10-26(4) <https://lore.kernel.org/all/CA+55aFwiiQYJ+YoLKCXjN_beDVfu38mg=Ggg5LFOcqHE8Qi7Zw@mail.gmail.com/>`_::

    If you break existing user space setups THAT IS A REGRESSION.

    It's not ok to say "but we'll fix the user space setup".

    Really. NOT OK.

On what qualifies as userspace interface, ABI, API, documented interfaces, etc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-20 <https://lore.kernel.org/all/CAHk-=wga8Qu0-OSE9VZbviq9GuqwhPhLUXeAt-S7_9+fMCLkKg@mail.gmail.com/>`_::

    So I absolutely detest the whole notion of "ABI changes". It's a
    meaningless concept, and I hate it with a passion, [...]

    The Linux rule for regressions is basically based on the philosophical
    question of "If a tree falls in the forest, and nobody is around to
    hear it, does it make a sound?".

    So the only thing that matters is if something breaks user-*conscious*
    behavior.

    And when that happens, the distinction between "bug fix" and "new
    feature" and "ABI change" matters not one whit, and the change needs
    to be done differently.

    [...]

    I just wanted to point out that the argument about whether it's an ABI
    change or not is irrelevant. If it turns out that some program - not a test
    script, but something with relevance to conscious user expectations ~
    depended on the old broken behavior, then it needs to be done some other
    way.

* From `2026-02-13 <https://lore.kernel.org/all/CAHk-=whY-N8kjm8kiFUV5Ei-8AuYw--EPGD-AR3Pd+5GTx2sAQ@mail.gmail.com/>`_::

    > [...] this should not fall under the don't break user space rule [...]

    Note that the rule is about breaking *users*, not breaking user space per
    se. [...]

    If some user setup breaks, things need fixing.

    [...] but I want to make it very clear that there are no excuses about "user
    space applications".

* From `2021-09-20(4) <https://lore.kernel.org/all/CAHk-=wi7DB2SJ-wngVvsJ7Ak2cM556Q8437sOXo4EJt2BWPdEg@mail.gmail.com/>`_::

    [...] a regression is a bit like Schrödinger's cat - if nobody is around
    to notice it and it doesn't actually affect any real workload, then you
    can treat the regression as if it doesn't exist.

* From `2020-05-21 <https://lore.kernel.org/all/CAHk-=wiVi7mSrsMP=fLXQrXK_UimybW=ziLOwSzFTtoXUacWVQ@mail.gmail.com/>`_::

    The rules about regressions have never been about any kind of documented
    behavior, or where the code lives.

    The rules about regressions are always about "breaks user workflow".

    Users are literally the _only_ thing that matters.

* From `2019-09-15 <https://lore.kernel.org/lkml/CAHk-=wiP4K8DRJWsCo=20hn_6054xBamGKF2kPgUzpB5aMaofA@mail.gmail.com/>`_::

    One _particularly_ last-minute revert is the top-most commit (ignoring
    the version change itself) done just before the release, and while
    it's very annoying, it's perhaps also instructive.

    What's instructive about it is that I reverted a commit that wasn't
    actually buggy. In fact, it was doing exactly what it set out to do,
    and did it very well. In fact it did it _so_ well that the much
    improved IO patterns it caused then ended up revealing a user-visible
    regression due to a real bug in a completely unrelated area.

    The actual details of that regression are not the reason I point that
    revert out as instructive, though. It's more that it's an instructive
    example of what counts as a regression, and what the whole "no
    regressions" kernel rule means.

    [...] The reverted commit didn't change any API's, and it didn't introduce
    any new bugs. But it ended up exposing another problem, and as such caused
    a kernel upgrade to fail for a user. So it got reverted.

    The point here being that we revert based on user-reported _behavior_, not
    based on some "it changes the ABI" or "it caused a bug" concept. The problem
    was really pre-existing, and it just didn't happen to trigger before. [...]

    Take-away from the whole thing: it's not about whether you change the
    kernel-userspace ABI, or fix a bug, or about whether the old code
    "should never have worked in the first place". It's about whether
    something breaks existing users' workflow.

* From `2017-11-05 <https://lore.kernel.org/all/CA+55aFzUvbGjD8nQ-+3oiMBx14c_6zOj2n7KLN3UsJ-qsd4Dcw@mail.gmail.com/>`_::

    And our regression rule has never been "behavior doesn't change".
    That would mean that we could never make any changes at all.

* From `2020-05-21 <https://lore.kernel.org/all/CAHk-=wiVi7mSrsMP=fLXQrXK_UimybW=ziLOwSzFTtoXUacWVQ@mail.gmail.com/>`_::

    No amount of "you shouldn't have used this" or "that behavior was
    undefined, it's your own fault your app broke" or "that used to work
    simply because of a kernel bug" is at all relevant.

* From `2021-05-21 <https://lore.kernel.org/all/CAHk-=wiVi7mSrsMP=fLXQrXK_UimybW=ziLOwSzFTtoXUacWVQ@mail.gmail.com/>`_::

    But no, "that was documented to be broken" (whether it's because the code
    was in staging or because the man-page said something else) is irrelevant.
    If staging code is so useful that people end up using it, that means that
    it's basically regular kernel code with a flag saying "please clean this
    up".

    [...]

    The other side of the coin is that people who talk about "API stability" are
    entirely wrong. API's don't matter either. You can make any changes to an
    API you like - as long as nobody notices.

    Again, the regression rule is not about documentation, not about API's, and
    not about the phase of the moon.

* From `2012-07-06 <https://lore.kernel.org/all/CA+55aFwnLJ+0sjx92EGREGTWOx84wwKaraSzpTNJwPVV8edw8g@mail.gmail.com/>`_::

    > Now this got me wondering if Debian _unstable_ actually qualifies as a
    > standard distro userspace.

    Oh, if the kernel breaks some standard user space, that counts. Tons
    of people run Debian unstable

* From `2011-05-06 <https://lore.kernel.org/all/BANLkTi=KVXjKR82sqsz4gwjr+E0vtqCmvA@mail.gmail.com/>`_::

    It's clearly NOT an internal tracepoint. By definition. It's being
    used by powertop.

On regressions noticed by users or test-suites/CIs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-22 <https://lore.kernel.org/all/CAHk-=wheQNiW_WtHGO7bKkT7Uib-p+ai2JP9M+z+FYcZ6CAxYA@mail.gmail.com/>`_::

    Users complaining is the only real line in the end.

    [...] a test-suite complaining is then often a *very* good indication that
    maybe users will hit some problem, and test suite issues should be taken
    very seriously [...]

    But a test-suite error isn't necessarily where you have to draw the
    line - it's a big red flag [...]

* From `2024-29-01 <https://lore.kernel.org/all/CAHk-=wg8BrZEzjJ5kUyZzHPZmFqH6ooMN1gRBCofxxCfucgjaw@mail.gmail.com/>`_::

    The "no regressions" rule is not about made-up "if I do this, behavior
    changes".

    The "no regressions" rule is about *users*.

    If you have an actual user that has been doing insane things, and we
    change something, and now the insane thing no longer works, at that
    point it's a regression, and we'll sigh, and go "Users are insane" and
    have to fix it.

    But if you have some random test that now behaves differently, it's
    not a regression. It's a *warning* sign, sure: tests are useful.

On accepting when a regression occurred
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2026-01-22 <https://lore.kernel.org/all/CAHk-=wheQNiW_WtHGO7bKkT7Uib-p+ai2JP9M+z+FYcZ6CAxYA@mail.gmail.com/>`_::

    But starting to argue about users reporting breaking changes is
    basically the final line for me. I have a couple of people that I have
    in my spam block-list and refuse to have anything to do with, and they
    have generally been about exactly that.

    Note how it's not about making mistakes and _causing_ the regression.
    That's normal. That's development. But then arguing about it is a
    no-no.

* From `2024-06-23 <https://lore.kernel.org/all/CAHk-=wi_KMO_rJ6OCr8mAWBRg-irziM=T9wxGC+J1VVoQb39gw@mail.gmail.com/>`_::

    We don't introduce regressions and then blame others.

    There's a very clear rule in kernel development: things that break
    other things ARE NOT FIXES.

    EVER.

    They get reverted, or the thing they broke gets fixed.

* From `2021-06-05 <https://lore.kernel.org/all/CAHk-=wiUVqHN76YUwhkjZzwTdjMMJf_zN4+u7vEJjmEGh3recw@mail.gmail.com/>`_::

    THERE ARE NO VALID ARGUMENTS FOR REGRESSIONS.

    Honestly, security people need to understand that "not working" is not
    a success case of security. It's a failure case.

    Yes, "not working" may be secure. But security in that case is *pointless*.

* From `2017-10-26(5) <https://lore.kernel.org/lkml/CA+55aFwiiQYJ+YoLKCXjN_beDVfu38mg=Ggg5LFOcqHE8Qi7Zw@mail.gmail.com/>`_::

    [...] when regressions *do* occur, we admit to them and fix them, instead of
    blaming user space.

    The fact that you have apparently been denying the regression now for
    three weeks means that I will revert, and I will stop pulling apparmor
    requests until the people involved understand how kernel development
    is done.

On back-and-forth
~~~~~~~~~~~~~~~~~

* From `2024-05-28 <https://lore.kernel.org/all/CAHk-=wgtb7y-bEh7tPDvDWru7ZKQ8-KMjZ53Tsk37zsPPdwXbA@mail.gmail.com/>`_::

    The "no regressions" rule is that we do not introduce NEW bugs.

    It *literally* came about because we had an endless dance of "fix two
    bugs, introduce one new one", and that then resulted in a system that
    you cannot TRUST.

* From `2021-09-20(1) <https://lore.kernel.org/all/CAHk-=wi7DB2SJ-wngVvsJ7Ak2cM556Q8437sOXo4EJt2BWPdEg@mail.gmail.com/>`_::

    And the thing that makes regressions special is that back when I
    wasn't so strict about these things, we'd end up in endless "seesaw
    situations" where somebody would fix something, it would break
    something else, then that something else would break, and it would
    never actually converge on anything reliable at all.

* From `2015-08-13 <https://lore.kernel.org/all/CA+55aFxk8-BsiKwr_S-c+4G6wihKPQVMLE34H9wOZpeua6W9+Q@mail.gmail.com/>`_::

    The strict policy of no regressions actually originally started mainly wrt
    suspend/resume issues, where the "fix one machine, break another" kind of
    back-and-forth caused endless problems, and meant that we didn't actually
    necessarily make any forward progress, just moving a problem around.

On changes with a risk of causing regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2023-06-02 <https://lore.kernel.org/all/CAHk-=wgyAGUMHmQM-5Eb556z5xiHZB7cF05qjrtUH4F7P-1rSA@mail.gmail.com/>`_::

    So what I think you should do is to fix the bug right, with a clean
    patch, and no crazy hacks. That is something we can then apply and
    test. All the while knowing full well that "uhhuh, this is a visible
    change, we may have to revert it".

    If then some *real* load ends up showing a regression, we may just be
    screwed. Our current behavior may be buggy, but we have the rule that
    once user space depends on kernel bugs, they become features pretty
    much by definition, however much we might dislike it.

On in-kernel workarounds to avoid regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2017-10-26(6) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

    Behavioral changes happen, and maybe we don't even support some
    feature any more. There's a number of fields in /proc/<pid>/stat that
    are printed out as zeroes, simply because they don't even *exist* in
    the kernel any more, or because showing them was a mistake (typically
    an information leak). But the numbers got replaced by zeroes, so that
    the code that used to parse the fields still works. The user might not
    see everything they used to see, and so behavior is clearly different,
    but things still _work_, even if they might no longer show sensitive
    (or no longer relevant) information.

On regressions caused by bugfixes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2018-08-03 <https://lore.kernel.org/all/CA+55aFwWZX=CXmWDTkDGb36kf12XmTehmQjbiMPCqCRG2hi9kw@mail.gmail.com/>`_::

    > Kernel had a bug which has been fixed

    That is *ENTIRELY* immaterial.

    Guys, whether something was buggy or not DOES NOT MATTER.

    [...]

    It's basically saying "I took something that worked, and I broke it,
    but now it's better". Do you not see how f*cking insane that statement
    is?

On internal API changes
~~~~~~~~~~~~~~~~~~~~~~~

* From `2017-10-26(7) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

    We do API breakage _inside_ the kernel all the time. We will fix
    internal problems by saying "you now need to do XYZ", but then it's
    about internal kernel API's, and the people who do that then also
    obviously have to fix up all the in-kernel users of that API. Nobody
    can say "I now broke the API you used, and now _you_ need to fix it
    up". Whoever broke something gets to fix it too.

On regressions only found after a long time
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2024-03-28 <https://lore.kernel.org/all/CAHk-=wgFuoHpMk_Z_R3qMXVDgq0N1592+bABkyGjwwSL4zBtHA@mail.gmail.com/>`_::

    I'm definitely not reverting a patch from almost a decade ago as a
    regression.

    If it took that long to find, it can't be that critical of a regression.

    So yes, let's treat it as a regular bug.

On testing regressions fixes in linux-next
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* On `maintainers summit 2024 <https://lwn.net/Articles/990599/>`_::

   So running fixes though linux-next is just a waste of time.

On a few other aspects related to regressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* From `2025-07-29(2) <https://lore.kernel.org/all/CAHk-=wjj9DvOZtmTkoLtyfHmy5mNKy6q_96d9=4FUEDXre=cww@mail.gmail.com/>`_
  [which `is not quite a regression, but a huge inconvenience <https://lore.kernel.org/all/CAHk-=wgO0Rx2LcYT4f75Xs46orbJ4JxO2jbAFQnVKDYAjV5HeQ@mail.gmail.com/>`_]::

    I no longer have sound.

    I also suspect that it's purely because "make oldconfig" doesn't work,
    and probably turned off my old Intel HDA settings. Or something.

    Renaming config parameters is *bad*. I've harped on the Kconfig phase
    of the kernel build probably being our nastiest point, and a real pain
    point to people getting involved with development simply because
    building your own kernel can be so daunting with hundreds of fairly
    esoteric questions.

..
   end-of-content
..
   This text is available under GPL-2.0+ or CC-BY-4.0, as stated at the top
   of the file. If you want to distribute this text under CC-BY-4.0 only,
   please use "The Linux kernel developers" for author attribution and link
   this as source:
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/process/handling-regressions.rst
..
   Note: Only the content of this RST file as found in the Linux kernel sources
   is available under CC-BY-4.0, as versions of this text that were processed
   (for example by the kernel's build system) might contain content taken from
   files which use a more restrictive license.

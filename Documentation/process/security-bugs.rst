.. _securitybugs:

Security bugs
=============

Linux kernel developers take security very seriously.  As such, we'd
like to know when a security bug is found so that it can be fixed and
disclosed as quickly as possible.

Preparing your report
---------------------

Like with any bug report, a security bug report requires a lot of analysis work
from the developers, so the more information you can share about the issue, the
better.  Please review the procedure outlined in
Documentation/admin-guide/reporting-issues.rst if you are unclear about what
information is helpful.  The following information are absolutely necessary in
**any** security bug report:

  * **affected kernel version range**: with no version indication, your report
    will not be processed.  A significant part of reports are for bugs that
    have already been fixed, so it is extremely important that vulnerabilities
    are verified on recent versions (development tree or latest stable
    version), at least by verifying that the code has not changed since the
    version where it was detected.

  * **description of the problem**: a detailed description of the problem, with
    traces showing its manifestation, and why you consider that the observed
    behavior as a problem in the kernel, is necessary.

  * **reproducer**: developers will need to be able to reproduce the problem to
    consider a fix as effective.  This includes both a way to trigger the issue
    and a way to confirm it happens.  A reproducer with low complexity
    dependencies will be needed (source code, shell script, sequence of
    instructions, file-system image etc).  Binary-only executables are not
    accepted.  Working exploits are extremely helpful and will not be released
    without consent from the reporter, unless they are already public.  By
    definition if an issue cannot be reproduced, it is not exploitable, thus it
    is not a security bug.

  * **conditions**: if the bug depends on certain configuration options,
    sysctls, permissions, timing, code modifications etc, these should be
    indicated.

In addition, the following information are highly desirable:

  * **suspected location of the bug**: the file names and functions where the
    bug is suspected to be present are very important, at least to help forward
    the report to the appropriate maintainers.  When not possible (for example,
    "system freezes each time I run this command"), the security team will help
    identify the source of the bug.

  * **a proposed fix**: bug reporters who have analyzed the cause of a bug in
    the source code almost always have an accurate idea on how to fix it,
    because they spent a long time studying it and its implications.  Proposing
    a tested fix will save maintainers a lot of time, even if the fix ends up
    not being the right one, because it helps understand the bug.  When
    proposing a tested fix, please always format it in a way that can be
    immediately merged (see Documentation/process/submitting-patches.rst).
    This will save some back-and-forth exchanges if it is accepted, and you
    will be credited for finding and fixing this issue.  Note that in this case
    only a ``Signed-off-by:`` tag is needed, without ``Reported-by:`` when the
    reporter and author are the same.

  * **mitigations**: very often during a bug analysis, some ways of mitigating
    the issue appear. It is useful to share them, as they can be helpful to
    keep end users protected during the time it takes them to apply the fix.

Identifying contacts
--------------------

The most effective way to report a security bug is to send it directly to the
affected subsystem's maintainers and Cc: the Linux kernel security team.  Do
not send it to a public list at this stage, unless you have good reasons to
consider the issue as being public or trivial to discover (e.g. result of a
widely available automated vulnerability scanning tool that can be repeated by
anyone).

If you're sending a report for issues affecting multiple parts in the kernel,
even if they're fairly similar issues, please send individual messages (think
that maintainers will not all work on the issues at the same time). The only
exception is when an issue concerns closely related parts maintained by the
exact same subset of maintainers, and these parts are expected to be fixed all
at once by the same commit, then it may be acceptable to report them at once.

One difficulty for most first-time reporters is to figure the right list of
recipients to send a report to.  In the Linux kernel, all official maintainers
are trusted, so the consequences of accidentally including the wrong maintainer
are essentially a bit more noise for that person, i.e. nothing dramatic.  As
such, a suitable method to figure the list of maintainers (which kernel
security officers use) is to rely on the get_maintainer.pl script, tuned to
only report maintainers.  This script, when passed a file name, will look for
its path in the MAINTAINERS file to figure a hierarchical list of relevant
maintainers.  Calling it a first time with the finest level of filtering will
most of the time return a short list of this specific file's maintainers::

  $ ./scripts/get_maintainer.pl --no-l --no-r --pattern-depth 1 \
    drivers/example.c
  Developer One <dev1@example.com> (maintainer:example driver)
  Developer Two <dev2@example.org> (maintainer:example driver)

These two maintainers should then receive the message.  If the command does not
return anything, it means the affected file is part of a wider subsystem, so we
should be less specific::

  $ ./scripts/get_maintainer.pl --no-l --no-r drivers/example.c
  Developer One <dev1@example.com> (maintainer:example subsystem)
  Developer Two <dev2@example.org> (maintainer:example subsystem)
  Developer Three <dev3@example.com> (maintainer:example subsystem [GENERAL])
  Developer Four <dev4@example.org> (maintainer:example subsystem [GENERAL])

Here, picking the first, most specific ones, is sufficient.  When the list is
long, it is possible to produce a comma-delimited e-mail address list on a
single line suitable for use in the To: field of a mailer like this::

  $ ./scripts/get_maintainer.pl --no-tree --no-l --no-r --no-n --m \
    --no-git-fallback --no-substatus --no-rolestats --no-multiline \
    --pattern-depth 1 drivers/example.c
  dev1@example.com, dev2@example.org

or this for the wider list::

  $ ./scripts/get_maintainer.pl --no-tree --no-l --no-r --no-n --m \
    --no-git-fallback --no-substatus --no-rolestats --no-multiline \
    drivers/example.c
  dev1@example.com, dev2@example.org, dev3@example.com, dev4@example.org

If at this point you're still facing difficulties spotting the right
maintainers, **and only in this case**, it's possible to send your report to
the Linux kernel security team only.  Your message will be triaged, and you
will receive instructions about whom to contact, if needed.  Your message may
equally be forwarded as-is to the relevant maintainers.

Sending the report
------------------

Reports are to be sent over e-mail exclusively.  Please use a working e-mail
address, preferably the same that you want to appear in ``Reported-by`` tags
if any.  If unsure, send your report to yourself first.

The security team and maintainers almost always require additional
information beyond what was initially provided in a report and rely on
active and efficient collaboration with the reporter to perform further
testing (e.g., verifying versions, configuration options, mitigations, or
patches). Before contacting the security team, the reporter must ensure
they are available to explain their findings, engage in discussions, and
run additional tests.  Reports where the reporter does not respond promptly
or cannot effectively discuss their findings may be abandoned if the
communication does not quickly improve.

The report must be sent to maintainers, with the security team in ``Cc:``.
The Linux kernel security team can be contacted by email at
<security@kernel.org>.  This is a private list of security officers
who will help verify the bug report and assist developers working on a fix.
It is possible that the security team will bring in extra help from area
maintainers to understand and fix the security vulnerability.

Please send **plain text** emails without attachments where possible.
It is much harder to have a context-quoted discussion about a complex
issue if all the details are hidden away in attachments.  Think of it like a
:doc:`regular patch submission <../process/submitting-patches>`
(even if you don't have a patch yet): describe the problem and impact, list
reproduction steps, and follow it with a proposed fix, all in plain text.
Markdown, HTML and RST formatted reports are particularly frowned upon since
they're quite hard to read for humans and encourage to use dedicated viewers,
sometimes online, which by definition is not acceptable for a confidential
security report. Note that some mailers tend to mangle formatting of plain
text by default, please consult Documentation/process/email-clients.rst for
more info.

Disclosure and embargoed information
------------------------------------

The security list is not a disclosure channel.  For that, see Coordination
below.

Once a robust fix has been developed, the release process starts.  Fixes
for publicly known bugs are released immediately.

Although our preference is to release fixes for publicly undisclosed bugs
as soon as they become available, this may be postponed at the request of
the reporter or an affected party for up to 7 calendar days from the start
of the release process, with an exceptional extension to 14 calendar days
if it is agreed that the criticality of the bug requires more time.  The
only valid reason for deferring the publication of a fix is to accommodate
the logistics of QA and large scale rollouts which require release
coordination.

While embargoed information may be shared with trusted individuals in
order to develop a fix, such information will not be published alongside
the fix or on any other disclosure channel without the permission of the
reporter.  This includes but is not limited to the original bug report
and followup discussions (if any), exploits, CVE information or the
identity of the reporter.

In other words our only interest is in getting bugs fixed.  All other
information submitted to the security list and any followup discussions
of the report are treated confidentially even after the embargo has been
lifted, in perpetuity.

Coordination with other groups
------------------------------

While the kernel security team solely focuses on getting bugs fixed,
other groups focus on fixing issues in distros and coordinating
disclosure between operating system vendors.  Coordination is usually
handled by the "linux-distros" mailing list and disclosure by the
public "oss-security" mailing list, both of which are closely related
and presented in the linux-distros wiki:
<https://oss-security.openwall.org/wiki/mailing-lists/distros>

Please note that the respective policies and rules are different since
the 3 lists pursue different goals.  Coordinating between the kernel
security team and other teams is difficult since for the kernel security
team occasional embargoes (as subject to a maximum allowed number of
days) start from the availability of a fix, while for "linux-distros"
they start from the initial post to the list regardless of the
availability of a fix.

As such, the kernel security team strongly recommends that as a reporter
of a potential security issue you DO NOT contact the "linux-distros"
mailing list UNTIL a fix is accepted by the affected code's maintainers
and you have read the distros wiki page above and you fully understand
the requirements that contacting "linux-distros" will impose on you and
the kernel community.  This also means that in general it doesn't make
sense to Cc: both lists at once, except maybe for coordination if and
while an accepted fix has not yet been merged.  In other words, until a
fix is accepted do not Cc: "linux-distros", and after it's merged do not
Cc: the kernel security team.

CVE assignment
--------------

The security team does not assign CVEs, nor do we require them for
reports or fixes, as this can needlessly complicate the process and may
delay the bug handling.  If a reporter wishes to have a CVE identifier
assigned for a confirmed issue, they can contact the :doc:`kernel CVE
assignment team<../process/cve>` to obtain one.

Non-disclosure agreements
-------------------------

The Linux kernel security team is not a formal body and therefore unable
to enter any non-disclosure agreements.

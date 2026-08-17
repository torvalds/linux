.. SPDX-License-Identifier: GPL-2.0

DAMON Maintainer Entry Profile
==============================

The DAMON subsystem covers the files that are listed in 'DAMON' section of
'MAINTAINERS' file.

The mailing lists for the subsystem are damon@lists.linux.dev and
linux-mm@kvack.org.  Patches should be made against the `mm-new tree
<https://git.kernel.org/akpm/mm/h/mm-new>`_ whenever possible and posted to the
mailing lists.

SCM Trees
---------

There are multiple Linux trees for DAMON development.  Patches under
development or testing are queued in `damon/next
<https://git.kernel.org/sj/h/damon/next>`_ by the DAMON maintainer.
Sufficiently reviewed patches will be queued in `mm-new
<https://git.kernel.org/akpm/mm/h/mm-new>`_ by the memory management subsystem
maintainer.  As more sufficient tests are done, the patches will move to
`mm-unstable <https://git.kernel.org/akpm/mm/h/mm-unstable>`_ and then to
`mm-stable <https://git.kernel.org/akpm/mm/h/mm-stable>`_.  And finally those
will be pull-requested to the mainline by the memory management subsystem
maintainer.

Note again the patches for `mm-new tree
<https://git.kernel.org/akpm/mm/h/mm-new>`_ are queued by the memory management
subsystem maintainer.  If the patches require some patches in `damon/next tree
<https://git.kernel.org/sj/h/damon/next>`_ which have not yet merged in mm-new,
please make sure the requirement is clearly specified.

Submit checklist addendum
-------------------------

When making DAMON changes, you should do below.

- Build changes related outputs including kernel and documents.
- Ensure the builds introduce no new errors or warnings.
- Run and ensure no new failures for DAMON `selftests
  <https://github.com/damonitor/damon-tests/blob/master/corr/run.sh#L49>`_ and
  `kunittests
  <https://github.com/damonitor/damon-tests/blob/master/corr/tests/kunit.sh>`_.

Further doing below and putting the results will be helpful.

- Run `damon-tests/corr
  <https://github.com/damonitor/damon-tests/tree/master/corr>`_ for normal
  changes.
- Measure impacts on benchmarks or real world workloads for performance
  changes.

Key cycle dates
---------------

Patches can be sent anytime.  Key cycle dates of the `mm-new
<https://git.kernel.org/akpm/mm/h/mm-new>`_, `mm-unstable
<https://git.kernel.org/akpm/mm/h/mm-unstable>`_ and `mm-stable
<https://git.kernel.org/akpm/mm/h/mm-stable>`_ trees depend on the memory
management subsystem maintainer.

Review cadence
--------------

The DAMON maintainer usually work in a flexible way, except early morning in PT
(Pacific Time).  The response to patches will occasionally be slow.  Do not
hesitate to send a ping if you have not heard back within a week of sending a
patch.

Mailing tool
------------

Like many other Linux kernel subsystems, DAMON uses the mailing lists
(damon@lists.linux.dev and linux-mm@kvack.org) as the major communication
channel.  There is a simple tool called `HacKerMaiL
<https://github.com/damonitor/hackermail>`_ (``hkml``), which is for people who
are not very familiar with the mailing lists based communication.  The tool
could be particularly helpful for DAMON community members since it is developed
and maintained by DAMON maintainer.  The tool is also officially announced to
support DAMON and general Linux kernel development workflow.

In other words, `hkml <https://github.com/damonitor/hackermail>`_ is a mailing
tool for DAMON community, which DAMON maintainer is committed to support.
Please feel free to try and report issues or feature requests for the tool to
the maintainer.

Community meetup
----------------

DAMON community has a bi-weekly meetup series for members who prefer
synchronous conversations over mails.  It is for discussions on specific topics
between a group of members including the maintainer.  The maintainer shares the
available time slots, and attendees should reserve one of those at least 24
hours before the time slot, by reaching out to the maintainer.

Schedules and reservation status are available at the Google `doc
<https://docs.google.com/document/d/1v43Kcj3ly4CYqmAkMaZzLiM2GEnWfgdGbZAH3mi2vpM/edit?usp=sharing>`_.
There is also a public Google `calendar
<https://calendar.google.com/calendar/u/0?cid=ZDIwOTA4YTMxNjc2MDQ3NTIyMmUzYTM5ZmQyM2U4NDA0ZGIwZjBiYmJlZGQxNDM0MmY4ZTRjOTE0NjdhZDRiY0Bncm91cC5jYWxlbmRhci5nb29nbGUuY29t>`_
that has the events.  Anyone can subscribe to it.  DAMON maintainer will also
provide periodic reminders to the mailing list (damon@lists.linux.dev).

AI Review
---------

For patches that are publicly posted to DAMON mailing list
(damon@lists.linux.dev), AI reviews of the patches will be available at
sashiko.dev.  The reviews could also be sent as mails to the author of the
patch.

Patch authors are encouraged to check the AI reviews and share their opinions.
The sharing could be done as a reply to the mail thread.  Consider reducing the
recipients list for such sharing, since some people are not really interested
in AI reviews.  As a rule of thumb, drop stable@vger.kernel.org and individuals
except DAMON maintainer.

`hkml` also provides a `feature
<https://github.com/sjp38/hackermail/blob/master/USAGE.md#forwarding-sashikodev-statuscomments-to-mailing-list>`_
for such sharing.  Please feel free to use the feature.

It is only an optional recommendation.  DAMON maintainer could also ask any
question about the AI reviews, though.

.. SPDX-License-Identifier: GPL-2.0

==============================
Feature and driver maintainers
==============================

The term "maintainer" spans a very wide range of levels of engagement
from people handling patches and pull requests as almost a full time job
to people responsible for a small feature or a driver.

Unlike most of the chapter, this section is meant for the latter (more
populous) group. It provides tips and describes the expectations and
responsibilities of maintainers of a small(ish) section of the code.

Drivers and alike most often do not have their own mailing lists and
git trees but instead send and review patches on the list of a larger
subsystem.

Responsibilities
================

The amount of maintenance work is usually proportional to the size
and popularity of the code base. Small features and drivers should
require relatively small amount of care and feeding. Nonetheless
when the work does arrive (in form of patches which need review,
user bug reports etc.) it has to be acted upon promptly.
Even when a particular driver only sees one patch a month, or a quarter,
a subsystem could well have a hundred such drivers. Subsystem
maintainers cannot afford to wait a long time to hear from reviewers.

The exact expectations on the response time will vary by subsystem.
The patch review SLA the subsystem had set for itself can sometimes
be found in the subsystem documentation. Failing that as a rule of thumb
reviewers should try to respond quicker than what is the usual patch
review delay of the subsystem maintainer. The resulting expectations
may range from two working days for fast-paced subsystems (e.g. networking)
to as long as a few weeks in slower moving parts of the kernel.

Mailing list participation
--------------------------

Linux kernel uses mailing lists as the primary form of communication.
Maintainers must be subscribed and follow the appropriate subsystem-wide
mailing list. Either by subscribing to the whole list or using more
modern, selective setup like
`lei <https://people.kernel.org/monsieuricon/lore-lei-part-1-getting-started>`_.

Maintainers must know how to communicate on the list (plain text, no invasive
legal footers, no top posting, etc.)

Reviews
-------

Maintainers must review *all* patches touching exclusively their drivers,
no matter how trivial. If the patch is a tree wide change and modifies
multiple drivers - whether to provide a review is left to the maintainer.

When there are multiple maintainers for a piece of code an ``Acked-by``
or ``Reviewed-by`` tag (or review comments) from a single maintainer is
enough to satisfy this requirement.

If the review process or validation for a particular change will take longer
than the expected review timeline for the subsystem, maintainer should
reply to the submission indicating that the work is being done, and when
to expect full results.

Refactoring and core changes
----------------------------

Occasionally core code needs to be changed to improve the maintainability
of the kernel as a whole. Maintainers are expected to be present and
help guide and test changes to their code to fit the new infrastructure.

Bug reports
-----------

Maintainers must ensure severe problems in their code reported to them
are resolved in a timely manner: regressions, kernel crashes, kernel warnings,
compilation errors, lockups, data loss, and other bugs of similar scope.

Maintainers furthermore should respond to reports about other kinds of
bugs as well, if the report is of reasonable quality or indicates a
problem that might be severe -- especially if they have *Supported*
status of the codebase in the MAINTAINERS file.

Open development
----------------

Discussions about user reported issues, and development of new code
should be conducted in a manner typical for the larger subsystem.
It is common for development within a single company to be conducted
behind closed doors. However, development and discussions initiated
by community members must not be redirected from public to closed forums
or to private email conversations. Reasonable exceptions to this guidance
include discussions about security related issues.

Selecting the maintainer
========================

The previous section described the expectations of the maintainer,
this section provides guidance on selecting one and describes common
misconceptions.

The author
----------

Most natural and common choice of a maintainer is the author of the code.
The author is intimately familiar with the code, so it is the best person
to take care of it on an ongoing basis.

That said, being a maintainer is an active role. The MAINTAINERS file
is not a list of credits (in fact a separate CREDITS file exists),
it is a list of those who will actively help with the code.
If the author does not have the time, interest or ability to maintain
the code, a different maintainer must be selected.

Multiple maintainers
--------------------

Modern best practices dictate that there should be at least two maintainers
for any piece of code, no matter how trivial. It spreads the burden, helps
people take vacations and prevents burnout, trains new members of
the community etc. etc. Even when there is clearly one perfect candidate,
another maintainer should be found.

Maintainers must be human, therefore, it is not acceptable to add a mailing
list or a group email as a maintainer. Trust and understanding are the
foundation of kernel maintenance and one cannot build trust with a mailing
list. Having a mailing list *in addition* to humans is perfectly fine.

Corporate structures
--------------------

To an outsider the Linux kernel may resemble a hierarchical organization
with Linus as the CEO. While the code flows in a hierarchical fashion,
the corporate template does not apply here. Linux is an anarchy held
together by (rarely expressed) mutual respect, trust and convenience.

All that is to say that managers almost never make good maintainers.
The maintainer position more closely matches an on-call rotation
than a position of power.

The following characteristics of a person selected as a maintainer
are clear red flags:

 - unknown to the community, never sent an email to the list before
 - did not author any of the code
 - (when development is contracted) works for a company which paid
   for the development rather than the company which did the work

Non compliance
==============

Subsystem maintainers may remove inactive maintainers from the MAINTAINERS
file. If the maintainer was a significant author or played an important
role in the development of the code, they should be moved to the CREDITS file.

Removing an inactive maintainer should not be seen as a punitive action.
Having an inactive maintainer has a real cost as all developers have
to remember to include the maintainers in discussions and subsystem
maintainers spend brain power figuring out how to solicit feedback.

Subsystem maintainers may remove code for lacking maintenance.

Subsystem maintainers may refuse accepting code from companies
which repeatedly neglected their maintainership duties.

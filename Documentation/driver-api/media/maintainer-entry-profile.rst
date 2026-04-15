.. SPDX-License-Identifier: GPL-2.0

Media Subsystem Profile
=======================

Overview
--------

The Linux Media Community (aka: the LinuxTV Community) is formed by
developers working on Linux Kernel Media Subsystem, together with users
who also play an important role in testing the code.

The Media Subsystem has code to support a wide variety of media-related
devices: stream capture, analog and digital TV streams, cameras,
video codecs, video processing (resizers, etc.), radio, remote controllers,
HDMI CEC and media pipeline control.

The Media Subsystem consists of the following directories in the kernel
tree:

  - drivers/media
  - drivers/staging/media
  - include/media
  - Documentation/devicetree/bindings/media/\ [1]_
  - Documentation/admin-guide/media
  - Documentation/driver-api/media
  - Documentation/userspace-api/media

.. [1] Device tree bindings are maintained by the
       OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS maintainers
       (see the MAINTAINERS file). So, changes there must be reviewed
       by them before being merged into the media subsystem's development
       tree.

Both media userspace and Kernel APIs are documented and the documentation
must be kept in sync with the API changes. It means that all patches that
add new features to the subsystem must also bring changes to the
corresponding API documentation.

Media Maintainers
-----------------

Media Maintainers are not just people capable of writing code, but they
are developers who have demonstrated their ability to collaborate with
the team, get the most knowledgeable people to review code, contribute
high-quality code, and follow through to fix issues (in code or tests).

Due to the size and wide scope of the media subsystem, multiple layers of
maintainers are required, each with their own areas of expertise:

- **Media Driver Maintainer**:
    Responsible for one or more drivers within the Media Subsystem. They
    are listed in the MAINTAINERS file as maintainer for those drivers. Media
    Driver Maintainers review patches for those drivers, provide feedback if
    patches do not follow the subsystem rules, or are not using the
    media kernel or userspace APIs correctly, or if they have poor code
    quality.

    If you are the patch author, you work with other Media
    Maintainers to ensure your patches are reviewed.

    Some Media Driver Maintainers have additional responsibilities. They have
    been granted Patchwork access and keep
    `Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_
    up to date, decide when patches are ready for merging, and create Pull
    Requests for the Media Subsystem Maintainers to merge.

- **Media Core Maintainer**:
    Media Driver Maintainers with Patchwork access who are also responsible for
    one or more media core frameworks.

    Core framework changes are done via consensus between the relevant Media
    Core Maintainers. Media Maintainers may include core framework changes in
    their Pull Requests if they are signed off by the relevant Media Core
    Maintainers.

- **Media Subsystem Maintainers**:
    Media Core Maintainers who are also responsible for the subsystem as a
    whole, with access to the entire subsystem. Responsible for merging Pull
    Requests from other Media Maintainers.

    Userspace API/ABI changes are made via consensus among Media Subsystem
    Maintainers\ [2]_. Media Maintainers may include API/ABI changes in
    their Pull Requests if they are signed off by all Media Subsystem
    Maintainers.

All Media Maintainers shall agree with the Kernel development process as
described in Documentation/process/index.rst and with the Kernel development
rules in the Kernel documentation, including its code of conduct.

Media Maintainers are often reachable via the #linux-media IRC channel at OFTC.

.. [2] Everything that would break backward compatibility with existing
       non-kernel code are API/ABI changes. This includes ioctl and sysfs
       interfaces, v4l2 controls, and their behaviors.

Patchwork Access
----------------

All Media Maintainers who have been granted Patchwork access shall ensure that
`Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_
will reflect the current status, e.g. patches shall be delegated to the Media
Maintainer who is handling them and the patch status shall be updated according
to these rules:

- ``Under Review``: Used if the patch requires a second opinion
  or when it is part of a Pull Request;
- ``Superseded``: There is a newer version of the patch posted to the
  mailing list.
- ``Duplicated``: There was another patch doing the same thing from someone
  else that was accepted.
- ``Not Applicable``: Use for patch series that are not merged at media.git
  tree (e.g. drm, dmabuf, upstream merge, etc.) but were cross-posted to the
  linux-media mailing list.
- ``Accepted``: Once a patch is merged in the multi-committer tree. Only Media
  Maintainers with commit rights are allowed to set this state.

If Media Maintainers decide not to accept a patch, they should reply to the
patch authors by e‑mail, explaining why it is not accepted, and
update `Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_
accordingly with one of the following statuses:

- ``Changes Requested``: if a new revision was requested;
- ``Rejected``: if the proposed change is not acceptable at all.

.. Note::

   Patchwork supports a couple of clients to help semi-automate
   status updates via its REST interface:

   https://patchwork.readthedocs.io/en/latest/usage/clients/

For patches that fall within their area of responsibility a Media Maintainer
also decides when those patches are ready for merging, and create Pull Requests
for the Media Subsystem Maintainers to merge.

The most important aspect of becoming a Media Maintainer with Patchwork access
is that you have demonstrated an ability to give good code reviews. We value
your ability to deliver thorough, constructive code reviews.

As such, potential maintainers must earn enough credibility and trust from the
Linux Media Community. To do that, developers shall be familiar with the open
source model and have been active in the Linux Kernel community for some time,
and, in particular, in the media subsystem.

In addition to actually making the code changes, you are basically
demonstrating your:

- commitment to the project;
- ability to collaborate with the team and communicate well;
- understanding of how upstream and the Linux Media Community work
  (policies, processes for testing, code review, ...)
- reasonable knowledge about:

  - the Kernel development process:
    Documentation/process/index.rst

  - the Media development profile:
    Documentation/driver-api/media/maintainer-entry-profile.rst

- understanding of the projects' code base and coding style;
- ability to provide feedback to the patch authors;
- ability to judge when a patch might be ready for review and to submit;
- ability to write good code (last but certainly not least).

Media Driver Maintainers that desire to get Patchwork access are encouraged
to participate at the yearly Linux Media Summit, typically co-located with
a Linux-related conference. These summits are announced on the linux-media
mailing list.

If you are doing such tasks and have become a valued developer, an
existing Media Maintainer can nominate you to the Media Subsystem Maintainers.

The ultimate responsibility for accepting a nominated maintainer is up to
the subsystem's maintainers. The nominated maintainer must have earned a trust
relationship with all Media Subsystem Maintainers, as, by being granted
Patchwork access, you will take over part of their maintenance tasks.

Media Committers
----------------

Experienced and trusted Media Maintainers may be granted commit rights
which allow them to directly push patches to the media development tree instead
of posting a Pull Request for the Media Subsystem Maintainers. This helps
offloading some of the work of the Media Subsystem Maintainers.

More details about Media Committers' roles and responsibilities can be
found here: :ref:`Media Committers`.

Media development sites
-----------------------

The `LinuxTV <https://linuxtv.org/>`_ web site hosts news about the subsystem,
together with:

- `Wiki pages <https://www.linuxtv.org/wiki/index.php/Main_Page>`_;
- `Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_;
- `Linux Media documentation <https://linuxtv.org/docs.php>`_;
- and more.

The main development trees used by the media subsystem are at:

- Stable tree:
  - https://git.linuxtv.org/media.git/

- Media committers tree:
  - https://gitlab.freedesktop.org/linux-media/media-committers.git

    Please note that it can be rebased, although only as a last resort.

- Media development trees, including apps and CI:

  - https://git.linuxtv.org/
  - https://gitlab.freedesktop.org/linux-media/


.. _Media development workflow:

Media development workflow
++++++++++++++++++++++++++

All changes for the media subsystem shall be sent first as e-mails to the
media mailing list, following the process documented at
Documentation/process/index.rst.

It means that patches shall be submitted as plain text only via e-mail to
linux-media@vger.kernel.org (aka: LMML). While subscription is not mandatory,
you can find details about how to subscribe to it and to see its archives at:

  https://subspace.kernel.org/vger.kernel.org.html

Emails with HTML will be automatically rejected by the mail server.

It could be wise to also copy the relevant Media Maintainer(s). You should use
``scripts/get_maintainers.pl`` to identify whom else needs to be copied.
Please always copy driver's authors and maintainers.

To minimize the chance of merge conflicts for your patch series, and make it
easier to backport patches to stable Kernels, we recommend that you use the
following baseline for your patch series:

1. Features for the next mainline release:

   - baseline shall be the ``media-committers.git next`` branch;

2. Bug fixes for the next mainline release:

   - baseline shall be the ``media-committers.git next`` branch. If the
     changes depend on a fix from the ``media-committers.git fixes``
     branch, then you can use that as baseline.

3. Bug fixes for the current mainline release (-rcX):

   - baseline shall be the latest mainline -rcX release or the
     ``media-committers.git fixes`` branch if changes depend on a mainline
     fix that is not yet merged;

.. Note::

   See https://www.kernel.org/category/releases.html for an overview
   about Kernel release types.

Patches with fixes shall have:

- a ``Fixes:`` tag pointing to the first commit that introduced the bug;
- when applicable, a ``Cc: stable@vger.kernel.org``.

Patches that were fixing bugs publicly reported by someone at the
linux-media@vger.kernel.org mailing list shall have:

- a ``Reported-by:`` tag immediately followed by a ``Closes:`` tag.

Patches that change API shall update documentation accordingly at the
same patch series.

See Documentation/process/index.rst for more details about e-mail submission.

Once a patch is submitted, it may follow either one of the following
workflows:

a. Media Maintainers' workflow: Media Maintainers post the Pull Requests,
   which are handled by the Media Subsystem Maintainers::

     +-------+   +------------+   +------+   +-------+   +---------------------+
     |e-mail |-->|picked up by|-->|code  |-->|pull   |-->|Subsystem Maintainers|
     |to LMML|   |Patchwork   |   |review|   |request|   |merge in             |
     |       |   |            |   |      |   |       |   |media-committers.git |
     +-------+   +------------+   +------+   +-------+   +---------------------+

   For this workflow, Pull Requests are generated by Media Maintainers with
   Patchwork access.  If you do not have Patchwork access, then please don't
   submit Pull Requests, as they will not be processed.

b. Media Committers' workflow: patches are handled by Media Maintainers with
   commit rights::

     +-------+   +------------+   +------+   +--------------------------+
     |e-mail |-->|picked up by|-->|code  |-->|Media Committers merge in |
     |to LMML|   |Patchwork   |   |review|   |media-committers.git      |
     +-------+   +------------+   +------+   +--------------------------+

When patches are picked up by
`Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_
and when merged at media-committers, Media CI bots will check for errors and
may provide e-mail feedback about patch problems. When this happens, the patch
submitter must fix them or explain why the errors are false positives.

Patches will only be moved to the next stage in these two workflows if they
pass on Media CI or if there are false-positives in the Media CI reports.

For both workflows, all patches shall be properly reviewed at
linux-media@vger.kernel.org (LMML) before being merged in
``media-committers.git``. Media patches will be reviewed in a timely manner
by the maintainers and reviewers as listed in the MAINTAINERS file.

Media Maintainers shall request reviews from other Media Maintainers and
developers where applicable, i.e. because those developers have more
knowledge about some areas that are changed by a patch.

There shall be no open issues or unresolved or conflicting feedback
from anyone. Clear them up first. Defer to the Media Subsystem
Maintainers if needed.

Failures during e-mail submission
+++++++++++++++++++++++++++++++++

Media's workflow is heavily based on Patchwork, meaning that, once a patch
is submitted, the e-mail will first be accepted by the mailing list
server, and, after a while, it should appear at:

   - https://patchwork.linuxtv.org/project/linux-media/list/

If it doesn't automatically appear there after some time [3]_, then
probably something went wrong on your submission. Please check if the
email is in plain text\ [4]_ only and if your emailer is not mangling
whitespaces before complaining or submitting them again.

To troubleshoot problems, you should first check if the mailing list
server has accepted your patch, by looking at:

   - https://lore.kernel.org/linux-media/

If the patch is there and not at
`Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_,
it is likely that your e-mailer mangled the patch. Patchwork internally
has logic that checks if the received e-mail contains a valid patch.
Any whitespace and new line breakages mangling the patch won't be recognized by
`Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_,
and such a patch will be rejected.

.. [3] It usually takes a few minutes for the patch to arrive, but
       the e-mail server may be busy, so it may take a longer time
       for a patch to be picked by
       `Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_.

.. [4] If your email contains HTML, the mailing list server will simply
       drop it, without any further notice.

.. _media-developers-gpg:

Authentication for pull and merge requests
++++++++++++++++++++++++++++++++++++++++++

The authenticity of developers submitting Pull Requests and merge requests
shall be validated by using the Linux Kernel Web of Trust, with PGP signing
at some moment. See: :ref:`kernel_org_trust_repository`.

With the Pull Request workflow, Pull Requests shall use PGP-signed tags.

With the committers' workflow, this is ensured at the time merge request
rights will be granted to the gitlab instance used by the media-committers.git
tree, after receiving the e-mail documented in
:ref:`media-committer-agreement`.

For more details about PGP signing, please read
Documentation/process/maintainer-pgp-guide.rst.

Maintaining media maintainer status
-----------------------------------

See :ref:`Maintain Media Status`.

List of Media Maintainers
-------------------------

The Media Maintainers listed here all have patchwork access and can
make Pull Requests or have commit rights.

The Media Subsystem Maintainers are:
  - Mauro Carvalho Chehab <mchehab@kernel.org>
  - Hans Verkuil <hverkuil@kernel.org>

The Media Core Maintainers are:
  - Sakari Ailus <sakari.ailus@linux.intel.com>

    - Media controller drivers
    - Core media controller framework
    - ISP
    - sensor drivers
    - v4l2-async and v4l2-fwnode core frameworks
    - v4l2-flash-led-class core framework

  - Mauro Carvalho Chehab <mchehab@kernel.org>

    - DVB

  - Laurent Pinchart <laurent.pinchart@ideasonboard.com>

    - Media controller drivers
    - Core media controller framework
    - ISP

  - Hans Verkuil <hverkuil@kernel.org>

    - V4L2 drivers
    - V4L2 and videobuf2 core frameworks
    - HDMI CEC drivers
    - HDMI CEC core framework

  - Sean Young <sean@mess.org>

    - Remote Controller (infrared) drivers
    - Remote Controller (infrared) core framework

The Media Driver Maintainers responsible for specific areas are:
  - Nicolas Dufresne <nicolas.dufresne@collabora.com>

    - Codec drivers
    - M2M driver not otherwise delegated

  - Bryan O'Donoghue <bryan.odonoghue@linaro.org>

    - Qualcomm drivers

Submit Checklist Addendum
-------------------------

Patches that change the Open Firmware/Device Tree bindings must be
reviewed by the Device Tree maintainers. So, DT maintainers should be
Cc:ed when those are submitted via devicetree@vger.kernel.org mailing
list.

There is a set of compliance tools at https://git.linuxtv.org/v4l-utils.git/
that should be used in order to check if the drivers are properly
implementing the media APIs:

====================	=======================================================
Type			Utility
====================	=======================================================
V4L2 drivers\ [5]_	``v4l2-compliance``
V4L2 virtual drivers	``contrib/test/test-media``
CEC drivers		``cec-compliance``
====================	=======================================================

.. [5] The ``v4l2-compliance`` utility also covers the media controller usage
       inside V4L2 drivers.

Those tests need to pass before the patches go upstream.

Also, please notice that we build the Kernel with::

	make CF=-D__CHECK_ENDIAN__ CONFIG_DEBUG_SECTION_MISMATCH=y C=1 W=1 CHECK=check_script

Where the check script is::

	#!/bin/bash
	/devel/smatch/smatch -p=kernel $@ >&2
	/devel/sparse/sparse $@ >&2

Be sure to not introduce new warnings on your patches without a
very good reason.

Please see `Media development workflow`_ for e-mail submission rules.

Style Cleanup Patches
+++++++++++++++++++++

Style cleanups are welcome when they come together with other changes
at the files where the style changes will affect.

We may accept pure standalone style cleanups, but they should ideally
be one patch for the whole subsystem (if the cleanup is low volume),
or at least be grouped per directory. So, for example, if you're doing a
big cleanup change set at drivers under drivers/media, please send a single
patch for all drivers under drivers/media/pci, another one for
drivers/media/usb and so on.

Coding Style Addendum
+++++++++++++++++++++

Media development uses ``checkpatch.pl`` on strict mode to verify the code
style, e.g.::

	$ ./scripts/checkpatch.pl --strict --max-line-length=80

In principle, patches should follow the coding style rules, but exceptions
are allowed if there are good reasons. On such case, maintainers and reviewers
may question about the rationale for not addressing the ``checkpatch.pl``.

Please notice that the goal here is to improve code readability. On
a few cases, ``checkpatch.pl`` may actually point to something that would
look worse. So, you should use good sense.

Note that addressing one ``checkpatch.pl`` issue (of any kind) alone may lead
to having longer lines than 80 characters per line. While this is not
strictly prohibited, efforts should be made towards staying within 80
characters per line. This could include using re-factoring code that leads
to less indentation, shorter variable or function names and last but not
least, simply wrapping the lines.

In particular, we accept lines with more than 80 columns:

    - on strings, as they shouldn't be broken due to line length limits;
    - when a function or variable name needs to have a long identifier name,
      which makes hard to honor the 80 columns limit;
    - on arithmetic expressions, when breaking lines makes them harder to
      read;
    - when they avoid a line ending with an open parenthesis or an open
      bracket.

Key Cycle Dates
---------------

New submissions can be sent at any time, but if they are intended to hit the
next merge window they should be sent before -rc5, and ideally stabilized
in the linux-media branch by -rc6.

Review Cadence
--------------

Provided that your patch has landed in
`Patchwork <https://patchwork.linuxtv.org/project/linux-media/list/>`_, it
should be sooner or later handled, so you don't need to re-submit a patch.

Except for important bug fixes, we don't usually add new patches to the
development tree between -rc6 and the next -rc1.

Please notice that the media subsystem is a high traffic one, so it
could take a while for us to be able to review your patches. Feel free
to ping if you don't get a feedback in a couple of weeks or to ask
other developers to publicly add ``Reviewed-by:`` and, more importantly,
``Tested-by:`` tags.

Please note that we expect a detailed description for ``Tested-by:``,
identifying what boards were used during the test and what it was tested.

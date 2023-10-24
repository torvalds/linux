.. _stable_kernel_rules:

Everything you ever wanted to know about Linux -stable releases
===============================================================

Rules on what kind of patches are accepted, and which ones are not, into the
"-stable" tree:

 - It or an equivalent fix must already exist in Linus' tree (upstream).
 - It must be obviously correct and tested.
 - It cannot be bigger than 100 lines, with context.
 - It must follow the
   :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
   rules.
 - It must either fix a real bug that bothers people or just add a device ID.
   To elaborate on the former:

   - It fixes a problem like an oops, a hang, data corruption, a real security
     issue, a hardware quirk, a build error (but not for things marked
     CONFIG_BROKEN), or some "oh, that's not good" issue.
   - Serious issues as reported by a user of a distribution kernel may also
     be considered if they fix a notable performance or interactivity issue.
     As these fixes are not as obvious and have a higher risk of a subtle
     regression they should only be submitted by a distribution kernel
     maintainer and include an addendum linking to a bugzilla entry if it
     exists and additional information on the user-visible impact.
   - No "This could be a problem..." type of things like a "theoretical race
     condition", unless an explanation of how the bug can be exploited is also
     provided.
   - No "trivial" fixes without benefit for users (spelling changes, whitespace
     cleanups, etc).


Procedure for submitting patches to the -stable tree
----------------------------------------------------

.. note::

   Security patches should not be handled (solely) by the -stable review
   process but should follow the procedures in
   :ref:`Documentation/process/security-bugs.rst <securitybugs>`.

There are three options to submit a change to -stable trees:

 1. Add a 'stable tag' to the description of a patch you then submit for
    mainline inclusion.
 2. Ask the stable team to pick up a patch already mainlined.
 3. Submit a patch to the stable team that is equivalent to a change already
    mainlined.

The sections below describe each of the options in more detail.

:ref:`option_1` is **strongly** preferred, it is the easiest and most common.
:ref:`option_2` is mainly meant for changes where backporting was not considered
at the time of submission. :ref:`option_3` is an alternative to the two earlier
options for cases where a mainlined patch needs adjustments to apply in older
series (for example due to API changes).

When using option 2 or 3 you can ask for your change to be included in specific
stable series. When doing so, ensure the fix or an equivalent is applicable,
submitted, or already present in all newer stable trees still supported. This is
meant to prevent regressions that users might later encounter on updating, if
e.g. a fix merged for 5.19-rc1 would be backported to 5.10.y, but not to 5.15.y.

.. _option_1:

Option 1
********

To have a patch you submit for mainline inclusion later automatically picked up
for stable trees, add the tag

.. code-block:: none

     Cc: stable@vger.kernel.org

in the sign-off area. Once the patch is mainlined it will be applied to the
stable tree without anything else needing to be done by the author or
subsystem maintainer.

To sent additional instructions to the stable team, use a shell-style inline
comment:

 * To specify any additional patch prerequisites for cherry picking use the
   following format in the sign-off area:

   .. code-block:: none

     Cc: <stable@vger.kernel.org> # 3.3.x: a1f84a3: sched: Check for idle
     Cc: <stable@vger.kernel.org> # 3.3.x: 1b9508f: sched: Rate-limit newidle
     Cc: <stable@vger.kernel.org> # 3.3.x: fd21073: sched: Fix affinity logic
     Cc: <stable@vger.kernel.org> # 3.3.x
     Signed-off-by: Ingo Molnar <mingo@elte.hu>

   The tag sequence has the meaning of:

   .. code-block:: none

     git cherry-pick a1f84a3
     git cherry-pick 1b9508f
     git cherry-pick fd21073
     git cherry-pick <this commit>

 * For patches that may have kernel version prerequisites specify them using
   the following format in the sign-off area:

   .. code-block:: none

     Cc: <stable@vger.kernel.org> # 3.3.x

   The tag has the meaning of:

   .. code-block:: none

     git cherry-pick <this commit>

   For each "-stable" tree starting with the specified version.

   Note, such tagging is unnecessary if the stable team can derive the
   appropriate versions from Fixes: tags.

 * To delay pick up of patches, use the following format:

   .. code-block:: none

     Cc: <stable@vger.kernel.org> # after 4 weeks in mainline

 * For any other requests, just add a note to the stable tag. This for example
   can be used to point out known problems:

   .. code-block:: none

     Cc: <stable@vger.kernel.org> # see patch description, needs adjustments for <= 6.3

.. _option_2:

Option 2
********

If the patch already has been merged to mainline, send an email to
stable@vger.kernel.org containing the subject of the patch, the commit ID,
why you think it should be applied, and what kernel versions you wish it to
be applied to.

.. _option_3:

Option 3
********

Send the patch, after verifying that it follows the above rules, to
stable@vger.kernel.org and mention the kernel versions you wish it to be applied
to. When doing so, you must note the upstream commit ID in the changelog of your
submission with a separate line above the commit text, like this:

.. code-block:: none

    commit <sha1> upstream.

or alternatively:

.. code-block:: none

    [ Upstream commit <sha1> ]

If the submitted patch deviates from the original upstream patch (for example
because it had to be adjusted for the older API), this must be very clearly
documented and justified in the patch description.


Following the submission
------------------------

The sender will receive an ACK when the patch has been accepted into the
queue, or a NAK if the patch is rejected.  This response might take a few
days, according to the schedules of the stable team members.

If accepted, the patch will be added to the -stable queue, for review by other
developers and by the relevant subsystem maintainer.


Review cycle
------------

 - When the -stable maintainers decide for a review cycle, the patches will be
   sent to the review committee, and the maintainer of the affected area of
   the patch (unless the submitter is the maintainer of the area) and CC: to
   the linux-kernel mailing list.
 - The review committee has 48 hours in which to ACK or NAK the patch.
 - If the patch is rejected by a member of the committee, or linux-kernel
   members object to the patch, bringing up issues that the maintainers and
   members did not realize, the patch will be dropped from the queue.
 - The ACKed patches will be posted again as part of release candidate (-rc)
   to be tested by developers and testers.
 - Usually only one -rc release is made, however if there are any outstanding
   issues, some patches may be modified or dropped or additional patches may
   be queued. Additional -rc releases are then released and tested until no
   issues are found.
 - Responding to the -rc releases can be done on the mailing list by sending
   a "Tested-by:" email with any testing information desired. The "Tested-by:"
   tags will be collected and added to the release commit.
 - At the end of the review cycle, the new -stable release will be released
   containing all the queued and tested patches.
 - Security patches will be accepted into the -stable tree directly from the
   security kernel team, and not go through the normal review cycle.
   Contact the kernel security team for more details on this procedure.


Trees
-----

 - The queues of patches, for both completed versions and in progress
   versions can be found at:

	https://git.kernel.org/pub/scm/linux/kernel/git/stable/stable-queue.git

 - The finalized and tagged releases of all stable kernels can be found
   in separate branches per version at:

	https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

 - The release candidate of all stable kernel versions can be found at:

        https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/

   .. warning::
      The -stable-rc tree is a snapshot in time of the stable-queue tree and
      will change frequently, hence will be rebased often. It should only be
      used for testing purposes (e.g. to be consumed by CI systems).


Review committee
----------------

 - This is made up of a number of kernel developers who have volunteered for
   this task, and a few that haven't.

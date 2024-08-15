.. _modifyingpatches:

Modifying Patches
=================

If you are a subsystem or branch maintainer, sometimes you need to slightly
modify patches you receive in order to merge them, because the code is not
exactly the same in your tree and the submitters'. If you stick strictly to
rule (c) of the developers certificate of origin, you should ask the submitter
to rediff, but this is a totally counter-productive waste of time and energy.
Rule (b) allows you to adjust the code, but then it is very impolite to change
one submitters code and make him endorse your bugs. To solve this problem, it
is recommended that you add a line between the last Signed-off-by header and
yours, indicating the nature of your changes. While there is nothing mandatory
about this, it seems like prepending the description with your mail and/or
name, all enclosed in square brackets, is noticeable enough to make it obvious
that you are responsible for last-minute changes. Example::

       Signed-off-by: Random J Developer <random@developer.example.org>
       [lucky@maintainer.example.org: struct foo moved from foo.c to foo.h]
       Signed-off-by: Lucky K Maintainer <lucky@maintainer.example.org>

This practice is particularly helpful if you maintain a stable branch and
want at the same time to credit the author, track changes, merge the fix,
and protect the submitter from complaints. Note that under no circumstances
can you change the author's identity (the From header), as it is the one
which appears in the changelog.

Special note to back-porters: It seems to be a common and useful practice
to insert an indication of the origin of a patch at the top of the commit
message (just after the subject line) to facilitate tracking. For instance,
here's what we see in a 3.x-stable release::

  Date:   Tue Oct 7 07:26:38 2014 -0400

    libata: Un-break ATA blacklist

    commit 1c40279960bcd7d52dbdf1d466b20d24b99176c8 upstream.

And here's what might appear in an older kernel once a patch is backported::

    Date:   Tue May 13 22:12:27 2008 +0200

        wireless, airo: waitbusy() won't delay

        [backport of 2.6 commit b7acbdfbd1f277c1eb23f344f899cfa4cd0bf36a]

Whatever the format, this information provides a valuable help to people
tracking your trees, and to people trying to troubleshoot bugs in your
tree.

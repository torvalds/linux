Background
==========

The upstream Linux kernel maintainers only fix bugs for specific kernel
versions.  Those versions include the current "release candidate" (or -rc)
kernel, any "stable" kernel versions, and any "long term" kernels.

Please see https://www.kernel.org/ for a list of supported kernels.  Any
kernel marked with [EOL] is "end of life" and will not have any fixes
backported to it.

If you've found a bug on a kernel version that isn't listed on kernel.org,
contact your Linux distribution or embedded vendor for support.
Alternatively, you can attempt to run one of the supported stable or -rc
kernels, and see if you can reproduce the bug on that.  It's preferable
to reproduce the bug on the latest -rc kernel.


How to report Linux kernel bugs
===============================


Identify the problematic subsystem
----------------------------------

Identifying which part of the Linux kernel might be causing your issue
increases your chances of getting your bug fixed. Simply posting to the
generic linux-kernel mailing list (LKML) may cause your bug report to be
lost in the noise of a mailing list that gets 1000+ emails a day.

Instead, try to figure out which kernel subsystem is causing the issue,
and email that subsystem's maintainer and mailing list.  If the subsystem
maintainer doesn't answer, then expand your scope to mailing lists like
LKML.


Identify who to notify
----------------------

Once you know the subsystem that is causing the issue, you should send a
bug report.  Some maintainers prefer bugs to be reported via bugzilla
(https://bugzilla.kernel.org), while others prefer that bugs be reported
via the subsystem mailing list.

To find out where to send an emailed bug report, find your subsystem or
device driver in the MAINTAINERS file.  Search in the file for relevant
entries, and send your bug report to the person(s) listed in the "M:"
lines, making sure to Cc the mailing list(s) in the "L:" lines.  When the
maintainer replies to you, make sure to 'Reply-all' in order to keep the
public mailing list(s) in the email thread.

If you know which driver is causing issues, you can pass one of the driver
files to the get_maintainer.pl script:
     perl scripts/get_maintainer.pl -f <filename>

If it is a security bug, please copy the Security Contact listed in the
MAINTAINERS file.  They can help coordinate bugfix and disclosure.  See
Documentation/SecurityBugs for more information.

If you can't figure out which subsystem caused the issue, you should file
a bug in kernel.org bugzilla and send email to
linux-kernel@vger.kernel.org, referencing the bugzilla URL.  (For more
information on the linux-kernel mailing list see
http://www.tux.org/lkml/).


Tips for reporting bugs
-----------------------

If you haven't reported a bug before, please read:

http://www.chiark.greenend.org.uk/~sgtatham/bugs.html
http://www.catb.org/esr/faqs/smart-questions.html

It's REALLY important to report bugs that seem unrelated as separate email
threads or separate bugzilla entries.  If you report several unrelated
bugs at once, it's difficult for maintainers to tease apart the relevant
data.


Gather information
------------------

The most important information in a bug report is how to reproduce the
bug.  This includes system information, and (most importantly)
step-by-step instructions for how a user can trigger the bug.

If the failure includes an "OOPS:", take a picture of the screen, capture
a netconsole trace, or type the message from your screen into the bug
report.  Please read "Documentation/oops-tracing.txt" before posting your
bug report. This explains what you should do with the "Oops" information
to make it useful to the recipient.

This is a suggested format for a bug report sent via email or bugzilla.
Having a standardized bug report form makes it easier for you not to
overlook things, and easier for the developers to find the pieces of
information they're really interested in.  If some information is not
relevant to your bug, feel free to exclude it.

First run the ver_linux script included as scripts/ver_linux, which
reports the version of some important subsystems.  Run this script with
the command "sh scripts/ver_linux".

Use that information to fill in all fields of the bug report form, and
post it to the mailing list with a subject of "PROBLEM: <one line
summary from [1.]>" for easy identification by the developers.

[1.] One line summary of the problem:
[2.] Full description of the problem/report:
[3.] Keywords (i.e., modules, networking, kernel):
[4.] Kernel information
[4.1.] Kernel version (from /proc/version):
[4.2.] Kernel .config file:
[5.] Most recent kernel version which did not have the bug:
[6.] Output of Oops.. message (if applicable) with symbolic information
     resolved (see Documentation/oops-tracing.txt)
[7.] A small shell script or example program which triggers the
     problem (if possible)
[8.] Environment
[8.1.] Software (add the output of the ver_linux script here)
[8.2.] Processor information (from /proc/cpuinfo):
[8.3.] Module information (from /proc/modules):
[8.4.] Loaded driver and hardware information (/proc/ioports, /proc/iomem)
[8.5.] PCI information ('lspci -vvv' as root)
[8.6.] SCSI information (from /proc/scsi/scsi)
[8.7.] Other information that might be relevant to the problem
       (please look in /proc and include all information that you
       think to be relevant):
[X.] Other notes, patches, fixes, workarounds:


Follow up
=========

Expectations for bug reporters
------------------------------

Linux kernel maintainers expect bug reporters to be able to follow up on
bug reports.  That may include running new tests, applying patches,
recompiling your kernel, and/or re-triggering your bug.  The most
frustrating thing for maintainers is for someone to report a bug, and then
never follow up on a request to try out a fix.

That said, it's still useful for a kernel maintainer to know a bug exists
on a supported kernel, even if you can't follow up with retests.  Follow
up reports, such as replying to the email thread with "I tried the latest
kernel and I can't reproduce my bug anymore" are also helpful, because
maintainers have to assume silence means things are still broken.

Expectations for kernel maintainers
-----------------------------------

Linux kernel maintainers are busy, overworked human beings.  Some times
they may not be able to address your bug in a day, a week, or two weeks.
If they don't answer your email, they may be on vacation, or at a Linux
conference.  Check the conference schedule at LWN.net for more info:
	https://lwn.net/Calendar/

In general, kernel maintainers take 1 to 5 business days to respond to
bugs.  The majority of kernel maintainers are employed to work on the
kernel, and they may not work on the weekends.  Maintainers are scattered
around the world, and they may not work in your time zone.  Unless you
have a high priority bug, please wait at least a week after the first bug
report before sending the maintainer a reminder email.

The exceptions to this rule are regressions, kernel crashes, security holes,
or userspace breakage caused by new kernel behavior.  Those bugs should be
addressed by the maintainers ASAP.  If you suspect a maintainer is not
responding to these types of bugs in a timely manner (especially during a
merge window), escalate the bug to LKML and Linus Torvalds.

Thank you!

[Some of this is taken from Frohwalt Egerer's original linux-kernel FAQ]

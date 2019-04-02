.. _reportings:

Reporting s
++++++++++++++

Background
==========

The upstream Linux kernel maintainers only fix s for specific kernel
versions.  Those versions include the current "release candidate" (or -rc)
kernel, any "stable" kernel versions, and any "long term" kernels.

Please see https://www.kernel.org/ for a list of supported kernels.  Any
kernel marked with [EOL] is "end of life" and will not have any fixes
backported to it.

If you've found a  on a kernel version that isn't listed on kernel.org,
contact your Linux distribution or embedded vendor for support.
Alternatively, you can attempt to run one of the supported stable or -rc
kernels, and see if you can reproduce the  on that.  It's preferable
to reproduce the  on the latest -rc kernel.


How to report Linux kernel s
===============================


Identify the problematic subsystem
----------------------------------

Identifying which part of the Linux kernel might be causing your issue
increases your chances of getting your  fixed. Simply posting to the
generic linux-kernel mailing list (LKML) may cause your  report to be
lost in the noise of a mailing list that gets 1000+ emails a day.

Instead, try to figure out which kernel subsystem is causing the issue,
and email that subsystem's maintainer and mailing list.  If the subsystem
maintainer doesn't answer, then expand your scope to mailing lists like
LKML.


Identify who to notify
----------------------

Once you know the subsystem that is causing the issue, you should send a
 report.  Some maintainers prefer s to be reported via zilla
(https://zilla.kernel.org), while others prefer that s be reported
via the subsystem mailing list.

To find out where to send an emailed  report, find your subsystem or
device driver in the MAINTAINERS file.  Search in the file for relevant
entries, and send your  report to the person(s) listed in the "M:"
lines, making sure to Cc the mailing list(s) in the "L:" lines.  When the
maintainer replies to you, make sure to 'Reply-all' in order to keep the
public mailing list(s) in the email thread.

If you know which driver is causing issues, you can pass one of the driver
files to the get_maintainer.pl script::

     perl scripts/get_maintainer.pl -f <filename>

If it is a security , please copy the Security Contact listed in the
MAINTAINERS file.  They can help coordinate fix and disclosure.  See
:ref:`Documentation/admin-guide/security-s.rst <securitys>` for more information.

If you can't figure out which subsystem caused the issue, you should file
a  in kernel.org zilla and send email to
linux-kernel@vger.kernel.org, referencing the zilla URL.  (For more
information on the linux-kernel mailing list see
http://vger.kernel.org/lkml/).


Tips for reporting s
-----------------------

If you haven't reported a  before, please read:

	http://www.chiark.greenend.org.uk/~sgtatham/s.html

	http://www.catb.org/esr/faqs/smart-questions.html

It's REALLY important to report s that seem unrelated as separate email
threads or separate zilla entries.  If you report several unrelated
s at once, it's difficult for maintainers to tease apart the relevant
data.


Gather information
------------------

The most important information in a  report is how to reproduce the
.  This includes system information, and (most importantly)
step-by-step instructions for how a user can trigger the .

If the failure includes an "OOPS:", take a picture of the screen, capture
a netconsole trace, or type the message from your screen into the 
report.  Please read "Documentation/admin-guide/-hunting.rst" before posting your
 report. This explains what you should do with the "Oops" information
to make it useful to the recipient.

This is a suggested format for a  report sent via email or zilla.
Having a standardized  report form makes it easier for you not to
overlook things, and easier for the developers to find the pieces of
information they're really interested in.  If some information is not
relevant to your , feel free to exclude it.

First run the ver_linux script included as scripts/ver_linux, which
reports the version of some important subsystems.  Run this script with
the command ``awk -f scripts/ver_linux``.

Use that information to fill in all fields of the  report form, and
post it to the mailing list with a subject of "PROBLEM: <one line
summary from [1.]>" for easy identification by the developers::

  [1.] One line summary of the problem:
  [2.] Full description of the problem/report:
  [3.] Keywords (i.e., modules, networking, kernel):
  [4.] Kernel information
  [4.1.] Kernel version (from /proc/version):
  [4.2.] Kernel .config file:
  [5.] Most recent kernel version which did not have the :
  [6.] Output of Oops.. message (if applicable) with symbolic information
       resolved (see Documentation/admin-guide/-hunting.rst)
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

Expectations for  reporters
------------------------------

Linux kernel maintainers expect  reporters to be able to follow up on
 reports.  That may include running new tests, applying patches,
recompiling your kernel, and/or re-triggering your .  The most
frustrating thing for maintainers is for someone to report a , and then
never follow up on a request to try out a fix.

That said, it's still useful for a kernel maintainer to know a  exists
on a supported kernel, even if you can't follow up with retests.  Follow
up reports, such as replying to the email thread with "I tried the latest
kernel and I can't reproduce my  anymore" are also helpful, because
maintainers have to assume silence means things are still broken.

Expectations for kernel maintainers
-----------------------------------

Linux kernel maintainers are busy, overworked human beings.  Some times
they may not be able to address your  in a day, a week, or two weeks.
If they don't answer your email, they may be on vacation, or at a Linux
conference.  Check the conference schedule at https://LWN.net for more info:

	https://lwn.net/Calendar/

In general, kernel maintainers take 1 to 5 business days to respond to
s.  The majority of kernel maintainers are employed to work on the
kernel, and they may not work on the weekends.  Maintainers are scattered
around the world, and they may not work in your time zone.  Unless you
have a high priority , please wait at least a week after the first 
report before sending the maintainer a reminder email.

The exceptions to this rule are regressions, kernel crashes, security holes,
or userspace breakage caused by new kernel behavior.  Those s should be
addressed by the maintainers ASAP.  If you suspect a maintainer is not
responding to these types of s in a timely manner (especially during a
merge window), escalate the  to LKML and Linus Torvalds.

Thank you!

[Some of this is taken from Frohwalt Egerer's original linux-kernel FAQ]

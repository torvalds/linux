====
CVEs
====

Common Vulnerabilities and Exposure (CVE®) numbers were developed as an
unambiguous way to identify, define, and catalog publicly disclosed
security vulnerabilities.  Over time, their usefulness has declined with
regards to the kernel project, and CVE numbers were very often assigned
in inappropriate ways and for inappropriate reasons.  Because of this,
the kernel development community has tended to avoid them.  However, the
combination of continuing pressure to assign CVEs and other forms of
security identifiers, and ongoing abuses by individuals and companies
outside of the kernel community has made it clear that the kernel
community should have control over those assignments.

The Linux kernel developer team does have the ability to assign CVEs for
potential Linux kernel security issues.  This assignment is independent
of the :doc:`normal Linux kernel security bug reporting
process<../process/security-bugs>`.

A list of all assigned CVEs for the Linux kernel can be found in the
archives of the linux-cve mailing list, as seen on
https://lore.kernel.org/linux-cve-announce/.  To get notice of the
assigned CVEs, please `subscribe
<https://subspace.kernel.org/subscribing.html>`_ to that mailing list.

Process
=======

As part of the normal stable release process, kernel changes that are
potentially security issues are identified by the developers responsible
for CVE number assignments and have CVE numbers automatically assigned
to them.  These assignments are published on the linux-cve-announce
mailing list as announcements on a frequent basis.

Note, due to the layer at which the Linux kernel is in a system, almost
any bug might be exploitable to compromise the security of the kernel,
but the possibility of exploitation is often not evident when the bug is
fixed.  Because of this, the CVE assignment team is overly cautious and
assign CVE numbers to any bugfix that they identify.  This
explains the seemingly large number of CVEs that are issued by the Linux
kernel team.

If the CVE assignment team misses a specific fix that any user feels
should have a CVE assigned to it, please email them at <cve@kernel.org>
and the team there will work with you on it.  Note that no potential
security issues should be sent to this alias, it is ONLY for assignment
of CVEs for fixes that are already in released kernel trees.  If you
feel you have found an unfixed security issue, please follow the
:doc:`normal Linux kernel security bug reporting
process<../process/security-bugs>`.

No CVEs will be automatically assigned for unfixed security issues in
the Linux kernel; assignment will only automatically happen after a fix
is available and applied to a stable kernel tree, and it will be tracked
that way by the git commit id of the original fix.  If anyone wishes to
have a CVE assigned before an issue is resolved with a commit, please
contact the kernel CVE assignment team at <cve@kernel.org> to get an
identifier assigned from their batch of reserved identifiers.

No CVEs will be assigned for any issue found in a version of the kernel
that is not currently being actively supported by the Stable/LTS kernel
team.  A list of the currently supported kernel branches can be found at
https://kernel.org/releases.html

Disputes of assigned CVEs
=========================

The authority to dispute or modify an assigned CVE for a specific kernel
change lies solely with the maintainers of the relevant subsystem
affected.  This principle ensures a high degree of accuracy and
accountability in vulnerability reporting.  Only those individuals with
deep expertise and intimate knowledge of the subsystem can effectively
assess the validity and scope of a reported vulnerability and determine
its appropriate CVE designation.  Any attempt to modify or dispute a CVE
outside of this designated authority could lead to confusion, inaccurate
reporting, and ultimately, compromised systems.

Invalid CVEs
============

If a security issue is found in a Linux kernel that is only supported by
a Linux distribution due to the changes that have been made by that
distribution, or due to the distribution supporting a kernel version
that is no longer one of the kernel.org supported releases, then a CVE
can not be assigned by the Linux kernel CVE team, and must be asked for
from that Linux distribution itself.

Any CVE that is assigned against the Linux kernel for an actively
supported kernel version, by any group other than the kernel assignment
CVE team should not be treated as a valid CVE.  Please notify the
kernel CVE assignment team at <cve@kernel.org> so that they can work to
invalidate such entries through the CNA remediation process.

Applicability of specific CVEs
==============================

As the Linux kernel can be used in many different ways, with many
different ways of accessing it by external users, or no access at all,
the applicability of any specific CVE is up to the user of Linux to
determine, it is not up to the CVE assignment team.  Please do not
contact us to attempt to determine the applicability of any specific
CVE.

Also, as the source tree is so large, and any one system only uses a
small subset of the source tree, any users of Linux should be aware that
large numbers of assigned CVEs are not relevant for their systems.

In short, we do not know your use case, and we do not know what portions
of the kernel that you use, so there is no way for us to determine if a
specific CVE is relevant for your system.

As always, it is best to take all released kernel changes, as they are
tested together in a unified whole by many community members, and not as
individual cherry-picked changes.  Also note that for many bugs, the
solution to the overall problem is not found in a single change, but by
the sum of many fixes on top of each other.  Ideally CVEs will be
assigned to all fixes for all issues, but sometimes we will fail to
notice fixes, therefore assume that some changes without a CVE assigned
might be relevant to take.


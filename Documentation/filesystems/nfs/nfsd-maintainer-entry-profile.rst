NFSD Maintainer Entry Profile
=============================

A Maintainer Entry Profile supplements the top-level process
documents (found in Documentation/process/) with customs that are
specific to a subsystem and its maintainers. A contributor may use
this document to set their expectations and avoid common mistakes.
A maintainer may use these profiles to look across subsystems for
opportunities to converge on best common practices.

Overview
--------
The Network File System (NFS) is a standardized family of network
protocols that enable access to files across a set of network-
connected peer hosts. Applications on NFS clients access files that
reside on file systems that are shared by NFS servers. A single
network peer can act as both an NFS client and an NFS server.

NFSD refers to the NFS server implementation included in the Linux
kernel. An in-kernel NFS server has fast access to files stored
in file systems local to that server. NFSD can share files stored
on most of the file system types native to Linux, including xfs,
ext4, btrfs, and tmpfs.

Mailing list
------------
The linux-nfs@vger.kernel.org mailing list is a public list. Its
purpose is to enable collaboration among developers working on the
Linux NFS stack, both client and server. It is not a place for
conversations that are not related directly to the Linux NFS stack.

The linux-nfs mailing list is archived on `lore.kernel.org <https://lore.kernel.org/linux-nfs/>`_.

The Linux NFS community does not have any chat room.

Reporting bugs
--------------
If you experience an NFSD-related bug on a distribution-built
kernel, please start by working with your Linux distributor.

Bug reports against upstream Linux code bases are welcome on the
linux-nfs@vger.kernel.org mailing list, where some active triage
can be done. NFSD bugs may also be reported in the Linux kernel
community's bugzilla at:

  https://bugzilla.kernel.org

Please file NFSD-related bugs under the "Filesystems/NFSD"
component. In general, including as much detail as possible is a
good start, including pertinent system log messages from both
the client and server.

User space software related to NFSD, such as mountd or the exportfs
command, is contained in the nfs-utils package. Report problems
with those components to linux-nfs@vger.kernel.org. You might be
directed to move the report to a specific bug tracker.

Contributor's Guide
-------------------

Standards compliance
~~~~~~~~~~~~~~~~~~~~
The priority is for NFSD to interoperate fully with the Linux NFS
client. We also test against other popular NFS client implementa-
tions regularly at NFS bake-a-thon events (also known as plug-
fests). Non-Linux NFS clients are not part of upstream NFSD CI/CD.

The NFSD community strives to provide an NFS server implementation
that interoperates with all standards-compliant NFS client
implementations. This is done by staying as close as is sensible to
the normative mandates in the IETF's published NFS, RPC, and GSS-API
standards.

It is always useful to reference an RFC and section number in a code
comment where behavior deviates from the standard (and even when the
behavior is compliant but the implementation is obfuscatory).

On the rare occasion when a deviation from standard-mandated
behavior is needed, brief documentation of the use case or
deficiencies in the standard is a required part of in-code
documentation.

Care must always be taken to avoid leaking local error codes (ie,
errnos) to clients of NFSD. A proper NFS status code is always
required in NFS protocol replies.

NFSD administrative interfaces
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
NFSD administrative interfaces include:

- an NFSD or SUNRPC module parameter

- export options in /etc/exports

- files under /proc/fs/nfsd/ or /proc/sys/sunrpc/

- the NFSD netlink protocol

Frequently, a request is made to introduce or modify one of NFSD's
traditional administrative interfaces. Certainly it is technically
easy to introduce a new administrative setting. However, there are
good reasons why the NFSD maintainers prefer to leave that as a last
resort:

- As with any API, administrative interfaces are difficult to get
  right.

- Once they are documented and have a legacy of use, administrative
  interfaces become difficult to modify or remove.

- Every new administrative setting multiplies the NFSD test matrix.

- The cost of one administrative interface is incremental, but costs
  add up across all of the existing interfaces.

It is often better for everyone if effort is made up front to
understanding the underlying requirement of the new setting, and
then trying to make it tune itself (or to become otherwise
unnecessary).

If a new setting is indeed necessary, first consider adding it to
the NFSD netlink protocol. Or if it doesn't need to be a reliable
long term user space feature, it can be added to NFSD's menagerie of
experimental settings which reside under /sys/kernel/debug/nfsd/ .

Field observability
~~~~~~~~~~~~~~~~~~~
NFSD employs several different mechanisms for observing operation,
including counters, printks, WARNings, and static trace points. Each
have their strengths and weaknesses. Contributors should select the
most appropriate tool for their task.

- BUG must be avoided if at all possible, as it will frequently
  result in a full system crash.

- WARN is appropriate only when a full stack trace is useful.

- printk can show detailed information. These must not be used
  in code paths where they can be triggered repeatedly by remote
  users.

- dprintk can show detailed information, but can be enabled only
  in pre-set groups. The overhead of emitting output makes dprintk
  inappropriate for frequent operations like I/O.

- Counters are always on, but provide little information about
  individual events other than how frequently they occur.

- static trace points can be enabled individually or in groups
  (via a glob). These are generally low overhead, and thus are
  favored for use in hot paths.

- dynamic tracing, such as kprobes or eBPF, are quite flexible but
  cannot be used in certain environments (eg, full kernel lock-
  down).

Testing
~~~~~~~
The kdevops project

  https://github.com/linux-kdevops/kdevops

contains several NFS-specific workflows, as well as the community
standard fstests suite. These workflows are based on open source
testing tools such as ltp and fio. Contributors are encouraged to
use these tools without kdevops, or contributors should install and
use kdevops themselves to verify their patches before submission.

Coding style
~~~~~~~~~~~~
Follow the coding style preferences described in

  Documentation/process/coding-style.rst

with the following exceptions:

- Add new local variables to a function in reverse Christmas tree
  order

- Use the kdoc comment style for
  + non-static functions
  + static inline functions
  + static functions that are callbacks/virtual functions

- All new function names start with ``nfsd_`` for non-NFS-version-
  specific functions.

- New function names that are specific to NFSv2 or NFSv3, or are
  used by all minor versions of NFSv4, use ``nfsdN_`` where N is
  the version.

- New function names specific to an NFSv4 minor version can be
  named with ``nfsd4M_`` where M is the minor version.

Patch preparation
~~~~~~~~~~~~~~~~~
Read and follow all guidelines in

  Documentation/process/submitting-patches.rst

Use tagging to identify all patch authors. However, reviewers and
testers should be added by replying to the email patch submission.
Email is extensively used in order to publicly archive review and
testing attributions. These tags are automatically inserted into
your patches when they are applied.

The code in the body of the diff already shows /what/ is being
changed. Thus it is not necessary to repeat that in the patch
description. Instead, the description should contain one or more
of:

- A brief problem statement ("what is this patch trying to fix?")
  with a root-cause analysis.

- End-user visible symptoms or items that a support engineer might
  use to search for the patch, like stack traces.

- A brief explanation of why the patch is the best way to address
  the problem.

- Any context that reviewers might need to understand the changes
  made by the patch.

- Any relevant benchmarking results, and/or functional test results.

As detailed in Documentation/process/submitting-patches.rst,
identify the point in history that the issue being addressed was
introduced by using a Fixes: tag.

Mention in the patch description if that point in history cannot be
determined -- that is, no Fixes: tag can be provided. In this case,
please make it clear to maintainers whether an LTS backport is
needed even though there is no Fixes: tag.

The NFSD maintainers prefer to add stable tagging themselves, after
public discussion in response to the patch submission. Contributors
may suggest stable tagging, but be aware that many version
management tools add such stable Cc's when you post your patches.
Don't add "Cc: stable" unless you are absolutely sure the patch
needs to go to stable during the initial submission process.

Patch submission
~~~~~~~~~~~~~~~~
Patches to NFSD are submitted via the kernel's email-based review
process that is common to most other kernel subsystems.

Just before each submission, rebase your patch or series on the
nfsd-testing branch at

  https://git.kernel.org/pub/scm/linux/kernel/git/cel/linux.git

The NFSD subsystem is maintained separately from the Linux in-kernel
NFS client. The NFSD maintainers do not normally take submissions
for client changes, nor can they respond authoritatively to bug
reports or feature requests for NFS client code.

This means that contributors might be asked to resubmit patches if
they were emailed to the incorrect set of maintainers and reviewers.
This is not a rejection, but simply a correction of the submission
process.

When in doubt, consult the NFSD entry in the MAINTAINERS file to
see which files and directories fall under the NFSD subsystem.

The proper set of email addresses for NFSD patches are:

To: the NFSD maintainers and reviewers listed in MAINTAINERS
Cc: linux-nfs@vger.kernel.org and optionally linux-kernel@

If there are other subsystems involved in the patches (for example
MM or RDMA) their primary mailing list address can be included in
the Cc: field. Other contributors and interested parties may be
included there as well.

In general we prefer that contributors use common patch email tools
such as "git send-email" or "stg email format/send", which tend to
get the details right without a lot of fuss.

A series consisting of a single patch is not required to have a
cover letter. However, a cover letter can be included if there is
substantial context that is not appropriate to include in the
patch description.

Please note that, with an e-mail based submission process, series
cover letters are not part of the work that is committed to the
kernel source code base or its commit history. Therefore always try
to keep pertinent information in the patch descriptions.

Design documentation is welcome, but as cover letters are not
preserved, a perhaps better option is to include a patch that adds
such documentation under Documentation/filesystems/nfs/.

Reviewers will ask about test coverage and what use cases the
patches are expected to address. Please be prepared to answer these
questions.

Review comments from maintainers might be politely stated, but in
general, these are not optional to address when they are actionable.
If necessary, the maintainers retain the right to not apply patches
when contributors refuse to address reasonable requests.

Post changes to kernel source code and user space source code as
separate series. You can connect the two series with comments in
your cover letters.

Generally the NFSD maintainers ask for a reposts even for simple
modifications in order to publicly archive the request and the
resulting repost before it is pulled into the NFSD trees. This
also enables us to rebuild a patch series quickly without missing
changes that might have been discussed via email.

Avoid frequently reposting large series with only small changes. As
a rule of thumb, posting substantial changes more than once a week
will result in reviewer overload.

Remember, there are only a handful of subsystem maintainers and
reviewers, but potentially many sources of contributions. The
maintainers and reviewers, therefore, are always the less scalable
resource. Be kind to your friendly neighborhood maintainer.

Patch Acceptance
~~~~~~~~~~~~~~~~
There isn't a formal review process for NFSD, but we like to see
at least two Reviewed-by: notices for patches that are more than
simple clean-ups. Reviews are done in public on
linux-nfs@vger.kernel.org and are archived on lore.kernel.org.

Currently the NFSD patch queues are maintained in branches here:

  https://git.kernel.org/pub/scm/linux/kernel/git/cel/linux.git

The NFSD maintainers apply patches initially to the nfsd-testing
branch, which is always open to new submissions. Patches can be
applied while review is ongoing. nfsd-testing is a topic branch,
so it can change frequently, it will be rebased, and your patch
might get dropped if there is a problem with it.

Generally a script-generated "thank you" email will indicate when
your patch has been added to the nfsd-testing branch. You can track
the progress of your patch using the linux-nfs patchworks instance:

  https://patchwork.kernel.org/project/linux-nfs/list/

While your patch is in nfsd-testing, it is exposed to a variety of
test environments, including community zero-day bots, static
analysis tools, and NFSD continuous integration testing. The soak
period is three to four weeks.

Each patch that survives in nfsd-testing for the soak period without
changes is moved to the nfsd-next branch.

The nfsd-next branch is automatically merged into linux-next and
fs-next on a nightly basis.

Patches that survive in nfsd-next are included in the next NFSD
merge window pull request. These windows typically occur once every
63 days (nine weeks).

When the upstream merge window closes, the nfsd-next branch is
renamed nfsd-fixes, and a new nfsd-next branch is created, based on
the upstream -rc1 tag.

Fixes that are destined for an upstream -rc release also run the
nfsd-testing gauntlet, but are then applied to the nfsd-fixes
branch. That branch is made available for Linus to pull after a
short time. In order to limit the risk of introducing regressions,
we limit such fixes to emergency situations or fixes to breakage
that occurred during the most recent upstream merge.

Please make it clear when submitting an emergency patch that
immediate action (either application to -rc or LTS backport) is
needed.

Sensitive patch submissions and bug reports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
CVEs are generated by specific members of the Linux kernel community
and several external entities. The Linux NFS community does not emit
or assign CVEs. CVEs are assigned after an issue and its fix are
known.

However, the NFSD maintainers sometimes receive sensitive security
reports, and at times these are significant enough to need to be
embargoed. In such rare cases, fixes can be developed and reviewed
out of the public eye.

Please be aware that many version management tools add the stable
Cc's when you post your patches. This is generally a nuisance, but
it can result in outing an embargoed security issue accidentally.
Don't add "Cc: stable" unless you are absolutely sure the patch
needs to go to stable@ during the initial submission process.

Patches that are merged without ever appearing on any list, and
which carry a Reported-by: or Fixes: tag are detected as suspicious
by security-focused people. We encourage that, after any private
review, security-sensitive patches should be posted to linux-nfs@
for the usual public review, archiving, and test period.

LLM-generated submissions
~~~~~~~~~~~~~~~~~~~~~~~~~
The Linux kernel community as a whole is still exploring the new
world of LLM-generated code. The NFSD maintainers will entertain
submission of patches that are partially or wholly generated by
LLM-based development tools. Such submissions are held to the
same standards as submissions created entirely by human authors:

- The human contributor identifies themselves via a Signed-off-by:
  tag. This tag counts as a DoC.

- The human contributor is solely responsible for code provenance
  and any contamination by inadvertently-included code with a
  conflicting license, as usual.

- The human contributor must be able to answer and address review
  questions. A patch description such as "This fixed my problem
  but I don't know why" is not acceptable.

- The contribution is subjected to the same test regimen as all
  other submissions.

- An indication (via a Generated-by: tag or otherwise) that the
  contribution is LLM-generated is not required.

It is easy to address review comments and fix requests in LLM
generated code. So easy, in fact, that it becomes tempting to repost
refreshed code immediately. Please resist that temptation.

As always, please avoid reposting series revisions more than once
every 24 hours.

Clean-up patches
~~~~~~~~~~~~~~~~
The NFSD maintainers discourage patches which perform simple clean-
ups, which are not in the context of other work. For example:

* Addressing ``checkpatch.pl`` warnings after merge
* Addressing :ref:`Local variable ordering<rcs>` issues
* Addressing long-standing whitespace damage

This is because it is felt that the churn that such changes produce
comes at a greater cost than the value of such clean-ups.

Conversely, spelling and grammar fixes are encouraged.

Stable and LTS support
----------------------
Upstream NFSD continuous integration testing runs against LTS trees
whenever they are updated.

Please indicate when a patch containing a fix needs to be considered
for LTS kernels, either via a Fixes: tag or explicit mention.

Feature requests
----------------
There is no one way to make an official feature request, but
discussion about the request should eventually make its way to
the linux-nfs@vger.kernel.org mailing list for public review by
the community.

Subsystem boundaries
~~~~~~~~~~~~~~~~~~~~
NFSD itself is not much more than a protocol engine. This means its
primary responsibility is to translate the NFS protocol into API
calls in the Linux kernel. For example, NFSD is not responsible for
knowing exactly how bytes or file attributes are managed on a block
device. It relies on other kernel subsystems for that.

If the subsystems on which NFSD relies do not implement a particular
feature, even if the standard NFS protocols do support that feature,
that usually means NFSD cannot provide that feature without
substantial development work in other areas of the kernel.

Specificity
~~~~~~~~~~~
Feature requests can come from anywhere, and thus can often be
nebulous. A requester might not understand what a "use case" or
"user story" is. These descriptive paradigms are often used by
developers and architects to understand what is required of a
design, but are terms of art in the software trade, not used in
the everyday world.

In order to prevent contributors and maintainers from becoming
overwhelmed, we won't be afraid of saying "no" politely to
underspecified requests.

Community roles and their authority
-----------------------------------
The purpose of Linux subsystem communities is to provide expertise
and active stewardship of a narrow set of source files in the Linux
kernel. This can include managing user space tooling as well.

To contextualize the structure of the Linux NFS community that
is responsible for stewardship of the NFS server code base, we
define the community roles here.

- **Contributor** : Anyone who submits a code change, bug fix,
  recommendation, documentation fix, and so on. A contributor can
  submit regularly or infrequently.

- **Outside Contributor** : A contributor who is not a regular actor
  in the Linux NFS community. This can mean someone who contributes
  to other parts of the kernel, or someone who just noticed a
  misspelling in a comment and sent a patch.

- **Reviewer** : Someone who is named in the MAINTAINERS file as a
  reviewer is an area expert who can request changes to contributed
  code, and expects that contributors will address the request.

- **External Reviewer** : Someone who is not named in the
  MAINTAINERS file as a reviewer, but who is an area expert.
  Examples include Linux kernel contributors with networking,
  security, or persistent storage expertise, or developers who
  contribute primarily to other NFS implementations.

One or more people will take on the following roles. These people
are often generically referred to as "maintainers", and are
identified in the MAINTAINERS file with the "M:" tag under the NFSD
subsystem.

- **Upstream Release Manager** : This role is responsible for
  curating contributions into a branch, reviewing test results, and
  then sending a pull request during merge windows. There is a
  trust relationship between the release manager and Linus.

- **Bug Triager** : Someone who is a first responder to bug reports
  submitted to the linux-nfs mailing list or bug trackers, and helps
  troubleshoot and identify next steps.

- **Security Lead** : The security lead handles contacts from the
  security community to resolve immediate issues, as well as dealing
  with long-term security issues such as supply chain concerns. For
  upstream, that's usually whether contributions violate licensing
  or other intellectual property agreements.

- **Testing Lead** : The testing lead builds and runs the test
  infrastructure for the subsystem. The testing lead may ask for
  patches to be dropped because of ongoing high defect rates.

- **LTS Maintainer** : The LTS maintainer is responsible for managing
  the Fixes: and Cc: stable annotations on patches, and seeing that
  patches that cannot be automatically applied to LTS kernels get
  proper manual backports as necessary.

- **Community Manager** : This umpire role can be asked to call balls
  and strikes during conflicts, but is also responsible for ensuring
  the health of the relationships within the community and for
  facilitating discussions on long-term topics such as how to manage
  growing technical debt.

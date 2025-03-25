XFS Maintainer Entry Profile
============================

Overview
--------
XFS is a well known high-performance filesystem in the Linux kernel.
The aim of this project is to provide and maintain a robust and
performant filesystem.

Patches are generally merged to the for-next branch of the appropriate
git repository.
After a testing period, the for-next branch is merged to the master
branch.

Kernel code are merged to the xfs-linux tree[0].
Userspace code are merged to the xfsprogs tree[1].
Test cases are merged to the xfstests tree[2].
Ondisk format documentation are merged to the xfs-documentation tree[3].

All patchsets involving XFS *must* be cc'd in their entirety to the mailing
list linux-xfs@vger.kernel.org.

Roles
-----
There are eight key roles in the XFS project.
A person can take on multiple roles, and a role can be filled by
multiple people.
Anyone taking on a role is advised to check in with themselves and
others on a regular basis about burnout.

- **Outside Contributor**: Anyone who sends a patch but is not involved
  in the XFS project on a regular basis.
  These folks are usually people who work on other filesystems or
  elsewhere in the kernel community.

- **Developer**: Someone who is familiar with the XFS codebase enough to
  write new code, documentation, and tests.

  Developers can often be found in the IRC channel mentioned by the ``C:``
  entry in the kernel MAINTAINERS file.

- **Senior Developer**: A developer who is very familiar with at least
  some part of the XFS codebase and/or other subsystems in the kernel.
  These people collectively decide the long term goals of the project
  and nudge the community in that direction.
  They should help prioritize development and review work for each release
  cycle.

  Senior developers tend to be more active participants in the IRC channel.

- **Reviewer**: Someone (most likely also a developer) who reads code
  submissions to decide:

  0. Is the idea behind the contribution sound?
  1. Does the idea fit the goals of the project?
  2. Is the contribution designed correctly?
  3. Is the contribution polished?
  4. Can the contribution be tested effectively?

  Reviewers should identify themselves with an ``R:`` entry in the kernel
  and fstests MAINTAINERS files.

- **Testing Lead**: This person is responsible for setting the test
  coverage goals of the project, negotiating with developers to decide
  on new tests for new features, and making sure that developers and
  release managers execute on the testing.

  The testing lead should identify themselves with an ``M:`` entry in
  the XFS section of the fstests MAINTAINERS file.

- **Bug Triager**: Someone who examines incoming bug reports in just
  enough detail to identify the person to whom the report should be
  forwarded.

  The bug triagers should identify themselves with a ``B:`` entry in
  the kernel MAINTAINERS file.

- **Release Manager**: This person merges reviewed patchsets into an
  integration branch, tests the result locally, pushes the branch to a
  public git repository, and sends pull requests further upstream.
  The release manager is not expected to work on new feature patchsets.
  If a developer and a reviewer fail to reach a resolution on some point,
  the release manager must have the ability to intervene to try to drive a
  resolution.

  The release manager should identify themselves with an ``M:`` entry in
  the kernel MAINTAINERS file.

- **Community Manager**: This person calls and moderates meetings of as many
  XFS participants as they can get when mailing list discussions prove
  insufficient for collective decisionmaking.
  They may also serve as liaison between managers of the organizations
  sponsoring work on any part of XFS.

- **LTS Maintainer**: Someone who backports and tests bug fixes from
  upstream to the LTS kernels.
  There tend to be six separate LTS trees at any given time.

  The maintainer for a given LTS release should identify themselves with an
  ``M:`` entry in the MAINTAINERS file for that LTS tree.
  Unmaintained LTS kernels should be marked with status ``S: Orphan`` in that
  same file.

Submission Checklist Addendum
-----------------------------
Please follow these additional rules when submitting to XFS:

- Patches affecting only the filesystem itself should be based against
  the latest -rc or the for-next branch.
  These patches will be merged back to the for-next branch.

- Authors of patches touching other subsystems need to coordinate with
  the maintainers of XFS and the relevant subsystems to decide how to
  proceed with a merge.

- Any patchset changing XFS should be cc'd in its entirety to linux-xfs.
  Do not send partial patchsets; that makes analysis of the broader
  context of the changes unnecessarily difficult.

- Anyone making kernel changes that have corresponding changes to the
  userspace utilities should send the userspace changes as separate
  patchsets immediately after the kernel patchsets.

- Authors of bug fix patches are expected to use fstests[2] to perform
  an A/B test of the patch to determine that there are no regressions.
  When possible, a new regression test case should be written for
  fstests.

- Authors of new feature patchsets must ensure that fstests will have
  appropriate functional and input corner-case test cases for the new
  feature.

- When implementing a new feature, it is strongly suggested that the
  developers write a design document to answer the following questions:

  * **What** problem is this trying to solve?

  * **Who** will benefit from this solution, and **where** will they
    access it?

  * **How** will this new feature work?  This should touch on major data
    structures and algorithms supporting the solution at a higher level
    than code comments.

  * **What** userspace interfaces are necessary to build off of the new
    features?

  * **How** will this work be tested to ensure that it solves the
    problems laid out in the design document without causing new
    problems?

  The design document should be committed in the kernel documentation
  directory.
  It may be omitted if the feature is already well known to the
  community.

- Patchsets for the new tests should be submitted as separate patchsets
  immediately after the kernel and userspace code patchsets.

- Changes to the on-disk format of XFS must be described in the ondisk
  format document[3] and submitted as a patchset after the fstests
  patchsets.

- Patchsets implementing bug fixes and further code cleanups should put
  the bug fixes at the beginning of the series to ease backporting.

Key Release Cycle Dates
-----------------------
Bug fixes may be sent at any time, though the release manager may decide to
defer a patch when the next merge window is close.

Code submissions targeting the next merge window should be sent between
-rc1 and -rc6.
This gives the community time to review the changes, to suggest other changes,
and for the author to retest those changes.

Code submissions also requiring changes to fs/iomap and targeting the
next merge window should be sent between -rc1 and -rc4.
This allows the broader kernel community adequate time to test the
infrastructure changes.

Review Cadence
--------------
In general, please wait at least one week before pinging for feedback.
To find reviewers, either consult the MAINTAINERS file, or ask
developers that have Reviewed-by tags for XFS changes to take a look and
offer their opinion.

References
----------
| [0] https://git.kernel.org/pub/scm/fs/xfs/xfs-linux.git/
| [1] https://git.kernel.org/pub/scm/fs/xfs/xfsprogs-dev.git/
| [2] https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git/
| [3] https://git.kernel.org/pub/scm/fs/xfs/xfs-documentation.git/

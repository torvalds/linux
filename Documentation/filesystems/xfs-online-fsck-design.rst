.. SPDX-License-Identifier: GPL-2.0
.. _xfs_online_fsck_design:

..
        Mapping of heading styles within this document:
        Heading 1 uses "====" above and below
        Heading 2 uses "===="
        Heading 3 uses "----"
        Heading 4 uses "````"
        Heading 5 uses "^^^^"
        Heading 6 uses "~~~~"
        Heading 7 uses "...."

        Sections are manually numbered because apparently that's what everyone
        does in the kernel.

======================
XFS Online Fsck Design
======================

This document captures the design of the online filesystem check feature for
XFS.
The purpose of this document is threefold:

- To help kernel distributors understand exactly what the XFS online fsck
  feature is, and issues about which they should be aware.

- To help people reading the code to familiarize themselves with the relevant
  concepts and design points before they start digging into the code.

- To help developers maintaining the system by capturing the reasons
  supporting higher level decision making.

As the online fsck code is merged, the links in this document to topic branches
will be replaced with links to code.

This document is licensed under the terms of the GNU Public License, v2.
The primary author is Darrick J. Wong.

This design document is split into seven parts.
Part 1 defines what fsck tools are and the motivations for writing a new one.
Parts 2 and 3 present a high level overview of how online fsck process works
and how it is tested to ensure correct functionality.
Part 4 discusses the user interface and the intended usage modes of the new
program.
Parts 5 and 6 show off the high level components and how they fit together, and
then present case studies of how each repair function actually works.
Part 7 sums up what has been discussed so far and speculates about what else
might be built atop online fsck.

.. contents:: Table of Contents
   :local:

1. What is a Filesystem Check?
==============================

A Unix filesystem has four main responsibilities:

- Provide a hierarchy of names through which application programs can associate
  arbitrary blobs of data for any length of time,

- Virtualize physical storage media across those names, and

- Retrieve the named data blobs at any time.

- Examine resource usage.

Metadata directly supporting these functions (e.g. files, directories, space
mappings) are sometimes called primary metadata.
Secondary metadata (e.g. reverse mapping and directory parent pointers) support
operations internal to the filesystem, such as internal consistency checking
and reorganization.
Summary metadata, as the name implies, condense information contained in
primary metadata for performance reasons.

The filesystem check (fsck) tool examines all the metadata in a filesystem
to look for errors.
In addition to looking for obvious metadata corruptions, fsck also
cross-references different types of metadata records with each other to look
for inconsistencies.
People do not like losing data, so most fsck tools also contains some ability
to correct any problems found.
As a word of caution -- the primary goal of most Linux fsck tools is to restore
the filesystem metadata to a consistent state, not to maximize the data
recovered.
That precedent will not be challenged here.

Filesystems of the 20th century generally lacked any redundancy in the ondisk
format, which means that fsck can only respond to errors by erasing files until
errors are no longer detected.
More recent filesystem designs contain enough redundancy in their metadata that
it is now possible to regenerate data structures when non-catastrophic errors
occur; this capability aids both strategies.

+--------------------------------------------------------------------------+
| **Note**:                                                                |
+--------------------------------------------------------------------------+
| System administrators avoid data loss by increasing the number of        |
| separate storage systems through the creation of backups; and they avoid |
| downtime by increasing the redundancy of each storage system through the |
| creation of RAID arrays.                                                 |
| fsck tools address only the first problem.                               |
+--------------------------------------------------------------------------+

TLDR; Show Me the Code!
-----------------------

Code is posted to the kernel.org git trees as follows:
`kernel changes <https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-symlink>`_,
`userspace changes <https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-media-scan-service>`_, and
`QA test changes <https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=repair-dirs>`_.
Each kernel patchset adding an online repair function will use the same branch
name across the kernel, xfsprogs, and fstests git repos.

Existing Tools
--------------

The online fsck tool described here will be the third tool in the history of
XFS (on Linux) to check and repair filesystems.
Two programs precede it:

The first program, ``xfs_check``, was created as part of the XFS debugger
(``xfs_db``) and can only be used with unmounted filesystems.
It walks all metadata in the filesystem looking for inconsistencies in the
metadata, though it lacks any ability to repair what it finds.
Due to its high memory requirements and inability to repair things, this
program is now deprecated and will not be discussed further.

The second program, ``xfs_repair``, was created to be faster and more robust
than the first program.
Like its predecessor, it can only be used with unmounted filesystems.
It uses extent-based in-memory data structures to reduce memory consumption,
and tries to schedule readahead IO appropriately to reduce I/O waiting time
while it scans the metadata of the entire filesystem.
The most important feature of this tool is its ability to respond to
inconsistencies in file metadata and directory tree by erasing things as needed
to eliminate problems.
Space usage metadata are rebuilt from the observed file metadata.

Problem Statement
-----------------

The current XFS tools leave several problems unsolved:

1. **User programs** suddenly **lose access** to the filesystem when unexpected
   shutdowns occur as a result of silent corruptions in the metadata.
   These occur **unpredictably** and often without warning.

2. **Users** experience a **total loss of service** during the recovery period
   after an **unexpected shutdown** occurs.

3. **Users** experience a **total loss of service** if the filesystem is taken
   offline to **look for problems** proactively.

4. **Data owners** cannot **check the integrity** of their stored data without
   reading all of it.
   This may expose them to substantial billing costs when a linear media scan
   performed by the storage system administrator might suffice.

5. **System administrators** cannot **schedule** a maintenance window to deal
   with corruptions if they **lack the means** to assess filesystem health
   while the filesystem is online.

6. **Fleet monitoring tools** cannot **automate periodic checks** of filesystem
   health when doing so requires **manual intervention** and downtime.

7. **Users** can be tricked into **doing things they do not desire** when
   malicious actors **exploit quirks of Unicode** to place misleading names
   in directories.

Given this definition of the problems to be solved and the actors who would
benefit, the proposed solution is a third fsck tool that acts on a running
filesystem.

This new third program has three components: an in-kernel facility to check
metadata, an in-kernel facility to repair metadata, and a userspace driver
program to drive fsck activity on a live filesystem.
``xfs_scrub`` is the name of the driver program.
The rest of this document presents the goals and use cases of the new fsck
tool, describes its major design points in connection to those goals, and
discusses the similarities and differences with existing tools.

+--------------------------------------------------------------------------+
| **Note**:                                                                |
+--------------------------------------------------------------------------+
| Throughout this document, the existing offline fsck tool can also be     |
| referred to by its current name "``xfs_repair``".                        |
| The userspace driver program for the new online fsck tool can be         |
| referred to as "``xfs_scrub``".                                          |
| The kernel portion of online fsck that validates metadata is called      |
| "online scrub", and portion of the kernel that fixes metadata is called  |
| "online repair".                                                         |
+--------------------------------------------------------------------------+

The naming hierarchy is broken up into objects known as directories and files
and the physical space is split into pieces known as allocation groups.
Sharding enables better performance on highly parallel systems and helps to
contain the damage when corruptions occur.
The division of the filesystem into principal objects (allocation groups and
inodes) means that there are ample opportunities to perform targeted checks and
repairs on a subset of the filesystem.

While this is going on, other parts continue processing IO requests.
Even if a piece of filesystem metadata can only be regenerated by scanning the
entire system, the scan can still be done in the background while other file
operations continue.

In summary, online fsck takes advantage of resource sharding and redundant
metadata to enable targeted checking and repair operations while the system
is running.
This capability will be coupled to automatic system management so that
autonomous self-healing of XFS maximizes service availability.

2. Theory of Operation
======================

Because it is necessary for online fsck to lock and scan live metadata objects,
online fsck consists of three separate code components.
The first is the userspace driver program ``xfs_scrub``, which is responsible
for identifying individual metadata items, scheduling work items for them,
reacting to the outcomes appropriately, and reporting results to the system
administrator.
The second and third are in the kernel, which implements functions to check
and repair each type of online fsck work item.

+------------------------------------------------------------------+
| **Note**:                                                        |
+------------------------------------------------------------------+
| For brevity, this document shortens the phrase "online fsck work |
| item" to "scrub item".                                           |
+------------------------------------------------------------------+

Scrub item types are delineated in a manner consistent with the Unix design
philosophy, which is to say that each item should handle one aspect of a
metadata structure, and handle it well.

Scope
-----

In principle, online fsck should be able to check and to repair everything that
the offline fsck program can handle.
However, online fsck cannot be running 100% of the time, which means that
latent errors may creep in after a scrub completes.
If these errors cause the next mount to fail, offline fsck is the only
solution.
This limitation means that maintenance of the offline fsck tool will continue.
A second limitation of online fsck is that it must follow the same resource
sharing and lock acquisition rules as the regular filesystem.
This means that scrub cannot take *any* shortcuts to save time, because doing
so could lead to concurrency problems.
In other words, online fsck is not a complete replacement for offline fsck, and
a complete run of online fsck may take longer than online fsck.
However, both of these limitations are acceptable tradeoffs to satisfy the
different motivations of online fsck, which are to **minimize system downtime**
and to **increase predictability of operation**.

.. _scrubphases:

Phases of Work
--------------

The userspace driver program ``xfs_scrub`` splits the work of checking and
repairing an entire filesystem into seven phases.
Each phase concentrates on checking specific types of scrub items and depends
on the success of all previous phases.
The seven phases are as follows:

1. Collect geometry information about the mounted filesystem and computer,
   discover the online fsck capabilities of the kernel, and open the
   underlying storage devices.

2. Check allocation group metadata, all realtime volume metadata, and all quota
   files.
   Each metadata structure is scheduled as a separate scrub item.
   If corruption is found in the inode header or inode btree and ``xfs_scrub``
   is permitted to perform repairs, then those scrub items are repaired to
   prepare for phase 3.
   Repairs are implemented by using the information in the scrub item to
   resubmit the kernel scrub call with the repair flag enabled; this is
   discussed in the next section.
   Optimizations and all other repairs are deferred to phase 4.

3. Check all metadata of every file in the filesystem.
   Each metadata structure is also scheduled as a separate scrub item.
   If repairs are needed and ``xfs_scrub`` is permitted to perform repairs,
   and there were no problems detected during phase 2, then those scrub items
   are repaired immediately.
   Optimizations, deferred repairs, and unsuccessful repairs are deferred to
   phase 4.

4. All remaining repairs and scheduled optimizations are performed during this
   phase, if the caller permits them.
   Before starting repairs, the summary counters are checked and any necessary
   repairs are performed so that subsequent repairs will not fail the resource
   reservation step due to wildly incorrect summary counters.
   Unsuccesful repairs are requeued as long as forward progress on repairs is
   made somewhere in the filesystem.
   Free space in the filesystem is trimmed at the end of phase 4 if the
   filesystem is clean.

5. By the start of this phase, all primary and secondary filesystem metadata
   must be correct.
   Summary counters such as the free space counts and quota resource counts
   are checked and corrected.
   Directory entry names and extended attribute names are checked for
   suspicious entries such as control characters or confusing Unicode sequences
   appearing in names.

6. If the caller asks for a media scan, read all allocated and written data
   file extents in the filesystem.
   The ability to use hardware-assisted data file integrity checking is new
   to online fsck; neither of the previous tools have this capability.
   If media errors occur, they will be mapped to the owning files and reported.

7. Re-check the summary counters and presents the caller with a summary of
   space usage and file counts.

Steps for Each Scrub Item
-------------------------

The kernel scrub code uses a three-step strategy for checking and repairing
the one aspect of a metadata object represented by a scrub item:

1. The scrub item of interest is checked for corruptions; opportunities for
   optimization; and for values that are directly controlled by the system
   administrator but look suspicious.
   If the item is not corrupt or does not need optimization, resource are
   released and the positive scan results are returned to userspace.
   If the item is corrupt or could be optimized but the caller does not permit
   this, resources are released and the negative scan results are returned to
   userspace.
   Otherwise, the kernel moves on to the second step.

2. The repair function is called to rebuild the data structure.
   Repair functions generally choose rebuild a structure from other metadata
   rather than try to salvage the existing structure.
   If the repair fails, the scan results from the first step are returned to
   userspace.
   Otherwise, the kernel moves on to the third step.

3. In the third step, the kernel runs the same checks over the new metadata
   item to assess the efficacy of the repairs.
   The results of the reassessment are returned to userspace.

Classification of Metadata
--------------------------

Each type of metadata object (and therefore each type of scrub item) is
classified as follows:

Primary Metadata
````````````````

Metadata structures in this category should be most familiar to filesystem
users either because they are directly created by the user or they index
objects created by the user
Most filesystem objects fall into this class:

- Free space and reference count information

- Inode records and indexes

- Storage mapping information for file data

- Directories

- Extended attributes

- Symbolic links

- Quota limits

Scrub obeys the same rules as regular filesystem accesses for resource and lock
acquisition.

Primary metadata objects are the simplest for scrub to process.
The principal filesystem object (either an allocation group or an inode) that
owns the item being scrubbed is locked to guard against concurrent updates.
The check function examines every record associated with the type for obvious
errors and cross-references healthy records against other metadata to look for
inconsistencies.
Repairs for this class of scrub item are simple, since the repair function
starts by holding all the resources acquired in the previous step.
The repair function scans available metadata as needed to record all the
observations needed to complete the structure.
Next, it stages the observations in a new ondisk structure and commits it
atomically to complete the repair.
Finally, the storage from the old data structure are carefully reaped.

Because ``xfs_scrub`` locks a primary object for the duration of the repair,
this is effectively an offline repair operation performed on a subset of the
filesystem.
This minimizes the complexity of the repair code because it is not necessary to
handle concurrent updates from other threads, nor is it necessary to access
any other part of the filesystem.
As a result, indexed structures can be rebuilt very quickly, and programs
trying to access the damaged structure will be blocked until repairs complete.
The only infrastructure needed by the repair code are the staging area for
observations and a means to write new structures to disk.
Despite these limitations, the advantage that online repair holds is clear:
targeted work on individual shards of the filesystem avoids total loss of
service.

This mechanism is described in section 2.1 ("Off-Line Algorithm") of
V. Srinivasan and M. J. Carey, `"Performance of On-Line Index Construction
Algorithms" <https://minds.wisconsin.edu/bitstream/handle/1793/59524/TR1047.pdf>`_,
*Extending Database Technology*, pp. 293-309, 1992.

Most primary metadata repair functions stage their intermediate results in an
in-memory array prior to formatting the new ondisk structure, which is very
similar to the list-based algorithm discussed in section 2.3 ("List-Based
Algorithms") of Srinivasan.
However, any data structure builder that maintains a resource lock for the
duration of the repair is *always* an offline algorithm.

Secondary Metadata
``````````````````

Metadata structures in this category reflect records found in primary metadata,
but are only needed for online fsck or for reorganization of the filesystem.

Secondary metadata include:

- Reverse mapping information

- Directory parent pointers

This class of metadata is difficult for scrub to process because scrub attaches
to the secondary object but needs to check primary metadata, which runs counter
to the usual order of resource acquisition.
Frequently, this means that full filesystems scans are necessary to rebuild the
metadata.
Check functions can be limited in scope to reduce runtime.
Repairs, however, require a full scan of primary metadata, which can take a
long time to complete.
Under these conditions, ``xfs_scrub`` cannot lock resources for the entire
duration of the repair.

Instead, repair functions set up an in-memory staging structure to store
observations.
Depending on the requirements of the specific repair function, the staging
index will either have the same format as the ondisk structure or a design
specific to that repair function.
The next step is to release all locks and start the filesystem scan.
When the repair scanner needs to record an observation, the staging data are
locked long enough to apply the update.
While the filesystem scan is in progress, the repair function hooks the
filesystem so that it can apply pending filesystem updates to the staging
information.
Once the scan is done, the owning object is re-locked, the live data is used to
write a new ondisk structure, and the repairs are committed atomically.
The hooks are disabled and the staging staging area is freed.
Finally, the storage from the old data structure are carefully reaped.

Introducing concurrency helps online repair avoid various locking problems, but
comes at a high cost to code complexity.
Live filesystem code has to be hooked so that the repair function can observe
updates in progress.
The staging area has to become a fully functional parallel structure so that
updates can be merged from the hooks.
Finally, the hook, the filesystem scan, and the inode locking model must be
sufficiently well integrated that a hook event can decide if a given update
should be applied to the staging structure.

In theory, the scrub implementation could apply these same techniques for
primary metadata, but doing so would make it massively more complex and less
performant.
Programs attempting to access the damaged structures are not blocked from
operation, which may cause application failure or an unplanned filesystem
shutdown.

Inspiration for the secondary metadata repair strategy was drawn from section
2.4 of Srinivasan above, and sections 2 ("NSF: Inded Build Without Side-File")
and 3.1.1 ("Duplicate Key Insert Problem") in C. Mohan, `"Algorithms for
Creating Indexes for Very Large Tables Without Quiescing Updates"
<https://dl.acm.org/doi/10.1145/130283.130337>`_, 1992.

The sidecar index mentioned above bears some resemblance to the side file
method mentioned in Srinivasan and Mohan.
Their method consists of an index builder that extracts relevant record data to
build the new structure as quickly as possible; and an auxiliary structure that
captures all updates that would be committed to the index by other threads were
the new index already online.
After the index building scan finishes, the updates recorded in the side file
are applied to the new index.
To avoid conflicts between the index builder and other writer threads, the
builder maintains a publicly visible cursor that tracks the progress of the
scan through the record space.
To avoid duplication of work between the side file and the index builder, side
file updates are elided when the record ID for the update is greater than the
cursor position within the record ID space.

To minimize changes to the rest of the codebase, XFS online repair keeps the
replacement index hidden until it's completely ready to go.
In other words, there is no attempt to expose the keyspace of the new index
while repair is running.
The complexity of such an approach would be very high and perhaps more
appropriate to building *new* indices.

**Future Work Question**: Can the full scan and live update code used to
facilitate a repair also be used to implement a comprehensive check?

*Answer*: In theory, yes.  Check would be much stronger if each scrub function
employed these live scans to build a shadow copy of the metadata and then
compared the shadow records to the ondisk records.
However, doing that is a fair amount more work than what the checking functions
do now.
The live scans and hooks were developed much later.
That in turn increases the runtime of those scrub functions.

Summary Information
```````````````````

Metadata structures in this last category summarize the contents of primary
metadata records.
These are often used to speed up resource usage queries, and are many times
smaller than the primary metadata which they represent.

Examples of summary information include:

- Summary counts of free space and inodes

- File link counts from directories

- Quota resource usage counts

Check and repair require full filesystem scans, but resource and lock
acquisition follow the same paths as regular filesystem accesses.

The superblock summary counters have special requirements due to the underlying
implementation of the incore counters, and will be treated separately.
Check and repair of the other types of summary counters (quota resource counts
and file link counts) employ the same filesystem scanning and hooking
techniques as outlined above, but because the underlying data are sets of
integer counters, the staging data need not be a fully functional mirror of the
ondisk structure.

Inspiration for quota and file link count repair strategies were drawn from
sections 2.12 ("Online Index Operations") through 2.14 ("Incremental View
Maintenace") of G.  Graefe, `"Concurrent Queries and Updates in Summary Views
and Their Indexes"
<http://www.odbms.org/wp-content/uploads/2014/06/Increment-locks.pdf>`_, 2011.

Since quotas are non-negative integer counts of resource usage, online
quotacheck can use the incremental view deltas described in section 2.14 to
track pending changes to the block and inode usage counts in each transaction,
and commit those changes to a dquot side file when the transaction commits.
Delta tracking is necessary for dquots because the index builder scans inodes,
whereas the data structure being rebuilt is an index of dquots.
Link count checking combines the view deltas and commit step into one because
it sets attributes of the objects being scanned instead of writing them to a
separate data structure.
Each online fsck function will be discussed as case studies later in this
document.

Risk Management
---------------

During the development of online fsck, several risk factors were identified
that may make the feature unsuitable for certain distributors and users.
Steps can be taken to mitigate or eliminate those risks, though at a cost to
functionality.

- **Decreased performance**: Adding metadata indices to the filesystem
  increases the time cost of persisting changes to disk, and the reverse space
  mapping and directory parent pointers are no exception.
  System administrators who require the maximum performance can disable the
  reverse mapping features at format time, though this choice dramatically
  reduces the ability of online fsck to find inconsistencies and repair them.

- **Incorrect repairs**: As with all software, there might be defects in the
  software that result in incorrect repairs being written to the filesystem.
  Systematic fuzz testing (detailed in the next section) is employed by the
  authors to find bugs early, but it might not catch everything.
  The kernel build system provides Kconfig options (``CONFIG_XFS_ONLINE_SCRUB``
  and ``CONFIG_XFS_ONLINE_REPAIR``) to enable distributors to choose not to
  accept this risk.
  The xfsprogs build system has a configure option (``--enable-scrub=no``) that
  disables building of the ``xfs_scrub`` binary, though this is not a risk
  mitigation if the kernel functionality remains enabled.

- **Inability to repair**: Sometimes, a filesystem is too badly damaged to be
  repairable.
  If the keyspaces of several metadata indices overlap in some manner but a
  coherent narrative cannot be formed from records collected, then the repair
  fails.
  To reduce the chance that a repair will fail with a dirty transaction and
  render the filesystem unusable, the online repair functions have been
  designed to stage and validate all new records before committing the new
  structure.

- **Misbehavior**: Online fsck requires many privileges -- raw IO to block
  devices, opening files by handle, ignoring Unix discretionary access control,
  and the ability to perform administrative changes.
  Running this automatically in the background scares people, so the systemd
  background service is configured to run with only the privileges required.
  Obviously, this cannot address certain problems like the kernel crashing or
  deadlocking, but it should be sufficient to prevent the scrub process from
  escaping and reconfiguring the system.
  The cron job does not have this protection.

- **Fuzz Kiddiez**: There are many people now who seem to think that running
  automated fuzz testing of ondisk artifacts to find mischevious behavior and
  spraying exploit code onto the public mailing list for instant zero-day
  disclosure is somehow of some social benefit.
  In the view of this author, the benefit is realized only when the fuzz
  operators help to **fix** the flaws, but this opinion apparently is not
  widely shared among security "researchers".
  The XFS maintainers' continuing ability to manage these events presents an
  ongoing risk to the stability of the development process.
  Automated testing should front-load some of the risk while the feature is
  considered EXPERIMENTAL.

Many of these risks are inherent to software programming.
Despite this, it is hoped that this new functionality will prove useful in
reducing unexpected downtime.

3. Testing Plan
===============

As stated before, fsck tools have three main goals:

1. Detect inconsistencies in the metadata;

2. Eliminate those inconsistencies; and

3. Minimize further loss of data.

Demonstrations of correct operation are necessary to build users' confidence
that the software behaves within expectations.
Unfortunately, it was not really feasible to perform regular exhaustive testing
of every aspect of a fsck tool until the introduction of low-cost virtual
machines with high-IOPS storage.
With ample hardware availability in mind, the testing strategy for the online
fsck project involves differential analysis against the existing fsck tools and
systematic testing of every attribute of every type of metadata object.
Testing can be split into four major categories, as discussed below.

Integrated Testing with fstests
-------------------------------

The primary goal of any free software QA effort is to make testing as
inexpensive and widespread as possible to maximize the scaling advantages of
community.
In other words, testing should maximize the breadth of filesystem configuration
scenarios and hardware setups.
This improves code quality by enabling the authors of online fsck to find and
fix bugs early, and helps developers of new features to find integration
issues earlier in their development effort.

The Linux filesystem community shares a common QA testing suite,
`fstests <https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git/>`_, for
functional and regression testing.
Even before development work began on online fsck, fstests (when run on XFS)
would run both the ``xfs_check`` and ``xfs_repair -n`` commands on the test and
scratch filesystems between each test.
This provides a level of assurance that the kernel and the fsck tools stay in
alignment about what constitutes consistent metadata.
During development of the online checking code, fstests was modified to run
``xfs_scrub -n`` between each test to ensure that the new checking code
produces the same results as the two existing fsck tools.

To start development of online repair, fstests was modified to run
``xfs_repair`` to rebuild the filesystem's metadata indices between tests.
This ensures that offline repair does not crash, leave a corrupt filesystem
after it exists, or trigger complaints from the online check.
This also established a baseline for what can and cannot be repaired offline.
To complete the first phase of development of online repair, fstests was
modified to be able to run ``xfs_scrub`` in a "force rebuild" mode.
This enables a comparison of the effectiveness of online repair as compared to
the existing offline repair tools.

General Fuzz Testing of Metadata Blocks
---------------------------------------

XFS benefits greatly from having a very robust debugging tool, ``xfs_db``.

Before development of online fsck even began, a set of fstests were created
to test the rather common fault that entire metadata blocks get corrupted.
This required the creation of fstests library code that can create a filesystem
containing every possible type of metadata object.
Next, individual test cases were created to create a test filesystem, identify
a single block of a specific type of metadata object, trash it with the
existing ``blocktrash`` command in ``xfs_db``, and test the reaction of a
particular metadata validation strategy.

This earlier test suite enabled XFS developers to test the ability of the
in-kernel validation functions and the ability of the offline fsck tool to
detect and eliminate the inconsistent metadata.
This part of the test suite was extended to cover online fsck in exactly the
same manner.

In other words, for a given fstests filesystem configuration:

* For each metadata object existing on the filesystem:

  * Write garbage to it

  * Test the reactions of:

    1. The kernel verifiers to stop obviously bad metadata
    2. Offline repair (``xfs_repair``) to detect and fix
    3. Online repair (``xfs_scrub``) to detect and fix

Targeted Fuzz Testing of Metadata Records
-----------------------------------------

The testing plan for online fsck includes extending the existing fs testing
infrastructure to provide a much more powerful facility: targeted fuzz testing
of every metadata field of every metadata object in the filesystem.
``xfs_db`` can modify every field of every metadata structure in every
block in the filesystem to simulate the effects of memory corruption and
software bugs.
Given that fstests already contains the ability to create a filesystem
containing every metadata format known to the filesystem, ``xfs_db`` can be
used to perform exhaustive fuzz testing!

For a given fstests filesystem configuration:

* For each metadata object existing on the filesystem...

  * For each record inside that metadata object...

    * For each field inside that record...

      * For each conceivable type of transformation that can be applied to a bit field...

        1. Clear all bits
        2. Set all bits
        3. Toggle the most significant bit
        4. Toggle the middle bit
        5. Toggle the least significant bit
        6. Add a small quantity
        7. Subtract a small quantity
        8. Randomize the contents

        * ...test the reactions of:

          1. The kernel verifiers to stop obviously bad metadata
          2. Offline checking (``xfs_repair -n``)
          3. Offline repair (``xfs_repair``)
          4. Online checking (``xfs_scrub -n``)
          5. Online repair (``xfs_scrub``)
          6. Both repair tools (``xfs_scrub`` and then ``xfs_repair`` if online repair doesn't succeed)

This is quite the combinatoric explosion!

Fortunately, having this much test coverage makes it easy for XFS developers to
check the responses of XFS' fsck tools.
Since the introduction of the fuzz testing framework, these tests have been
used to discover incorrect repair code and missing functionality for entire
classes of metadata objects in ``xfs_repair``.
The enhanced testing was used to finalize the deprecation of ``xfs_check`` by
confirming that ``xfs_repair`` could detect at least as many corruptions as
the older tool.

These tests have been very valuable for ``xfs_scrub`` in the same ways -- they
allow the online fsck developers to compare online fsck against offline fsck,
and they enable XFS developers to find deficiencies in the code base.

Proposed patchsets include
`general fuzzer improvements
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=fuzzer-improvements>`_,
`fuzzing baselines
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=fuzz-baseline>`_,
and `improvements in fuzz testing comprehensiveness
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=more-fuzz-testing>`_.

Stress Testing
--------------

A unique requirement to online fsck is the ability to operate on a filesystem
concurrently with regular workloads.
Although it is of course impossible to run ``xfs_scrub`` with *zero* observable
impact on the running system, the online repair code should never introduce
inconsistencies into the filesystem metadata, and regular workloads should
never notice resource starvation.
To verify that these conditions are being met, fstests has been enhanced in
the following ways:

* For each scrub item type, create a test to exercise checking that item type
  while running ``fsstress``.
* For each scrub item type, create a test to exercise repairing that item type
  while running ``fsstress``.
* Race ``fsstress`` and ``xfs_scrub -n`` to ensure that checking the whole
  filesystem doesn't cause problems.
* Race ``fsstress`` and ``xfs_scrub`` in force-rebuild mode to ensure that
  force-repairing the whole filesystem doesn't cause problems.
* Race ``xfs_scrub`` in check and force-repair mode against ``fsstress`` while
  freezing and thawing the filesystem.
* Race ``xfs_scrub`` in check and force-repair mode against ``fsstress`` while
  remounting the filesystem read-only and read-write.
* The same, but running ``fsx`` instead of ``fsstress``.  (Not done yet?)

Success is defined by the ability to run all of these tests without observing
any unexpected filesystem shutdowns due to corrupted metadata, kernel hang
check warnings, or any other sort of mischief.

Proposed patchsets include `general stress testing
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=race-scrub-and-mount-state-changes>`_
and the `evolution of existing per-function stress testing
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfstests-dev.git/log/?h=refactor-scrub-stress>`_.

4. User Interface
=================

The primary user of online fsck is the system administrator, just like offline
repair.
Online fsck presents two modes of operation to administrators:
A foreground CLI process for online fsck on demand, and a background service
that performs autonomous checking and repair.

Checking on Demand
------------------

For administrators who want the absolute freshest information about the
metadata in a filesystem, ``xfs_scrub`` can be run as a foreground process on
a command line.
The program checks every piece of metadata in the filesystem while the
administrator waits for the results to be reported, just like the existing
``xfs_repair`` tool.
Both tools share a ``-n`` option to perform a read-only scan, and a ``-v``
option to increase the verbosity of the information reported.

A new feature of ``xfs_scrub`` is the ``-x`` option, which employs the error
correction capabilities of the hardware to check data file contents.
The media scan is not enabled by default because it may dramatically increase
program runtime and consume a lot of bandwidth on older storage hardware.

The output of a foreground invocation is captured in the system log.

The ``xfs_scrub_all`` program walks the list of mounted filesystems and
initiates ``xfs_scrub`` for each of them in parallel.
It serializes scans for any filesystems that resolve to the same top level
kernel block device to prevent resource overconsumption.

Background Service
------------------

To reduce the workload of system administrators, the ``xfs_scrub`` package
provides a suite of `systemd <https://systemd.io/>`_ timers and services that
run online fsck automatically on weekends by default.
The background service configures scrub to run with as little privilege as
possible, the lowest CPU and IO priority, and in a CPU-constrained single
threaded mode.
This can be tuned by the systemd administrator at any time to suit the latency
and throughput requirements of customer workloads.

The output of the background service is also captured in the system log.
If desired, reports of failures (either due to inconsistencies or mere runtime
errors) can be emailed automatically by setting the ``EMAIL_ADDR`` environment
variable in the following service files:

* ``xfs_scrub_fail@.service``
* ``xfs_scrub_media_fail@.service``
* ``xfs_scrub_all_fail.service``

The decision to enable the background scan is left to the system administrator.
This can be done by enabling either of the following services:

* ``xfs_scrub_all.timer`` on systemd systems
* ``xfs_scrub_all.cron`` on non-systemd systems

This automatic weekly scan is configured out of the box to perform an
additional media scan of all file data once per month.
This is less foolproof than, say, storing file data block checksums, but much
more performant if application software provides its own integrity checking,
redundancy can be provided elsewhere above the filesystem, or the storage
device's integrity guarantees are deemed sufficient.

The systemd unit file definitions have been subjected to a security audit
(as of systemd 249) to ensure that the xfs_scrub processes have as little
access to the rest of the system as possible.
This was performed via ``systemd-analyze security``, after which privileges
were restricted to the minimum required, sandboxing was set up to the maximal
extent possible with sandboxing and system call filtering; and access to the
filesystem tree was restricted to the minimum needed to start the program and
access the filesystem being scanned.
The service definition files restrict CPU usage to 80% of one CPU core, and
apply as nice of a priority to IO and CPU scheduling as possible.
This measure was taken to minimize delays in the rest of the filesystem.
No such hardening has been performed for the cron job.

Proposed patchset:
`Enabling the xfs_scrub background service
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-media-scan-service>`_.

Health Reporting
----------------

XFS caches a summary of each filesystem's health status in memory.
The information is updated whenever ``xfs_scrub`` is run, or whenever
inconsistencies are detected in the filesystem metadata during regular
operations.
System administrators should use the ``health`` command of ``xfs_spaceman`` to
download this information into a human-readable format.
If problems have been observed, the administrator can schedule a reduced
service window to run the online repair tool to correct the problem.
Failing that, the administrator can decide to schedule a maintenance window to
run the traditional offline repair tool to correct the problem.

**Future Work Question**: Should the health reporting integrate with the new
inotify fs error notification system?
Would it be helpful for sysadmins to have a daemon to listen for corruption
notifications and initiate a repair?

*Answer*: These questions remain unanswered, but should be a part of the
conversation with early adopters and potential downstream users of XFS.

Proposed patchsets include
`wiring up health reports to correction returns
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=corruption-health-reports>`_
and
`preservation of sickness info during memory reclaim
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=indirect-health-reporting>`_.

5. Kernel Algorithms and Data Structures
========================================

This section discusses the key algorithms and data structures of the kernel
code that provide the ability to check and repair metadata while the system
is running.
The first chapters in this section reveal the pieces that provide the
foundation for checking metadata.
The remainder of this section presents the mechanisms through which XFS
regenerates itself.

Self Describing Metadata
------------------------

Starting with XFS version 5 in 2012, XFS updated the format of nearly every
ondisk block header to record a magic number, a checksum, a universally
"unique" identifier (UUID), an owner code, the ondisk address of the block,
and a log sequence number.
When loading a block buffer from disk, the magic number, UUID, owner, and
ondisk address confirm that the retrieved block matches the specific owner of
the current filesystem, and that the information contained in the block is
supposed to be found at the ondisk address.
The first three components enable checking tools to disregard alleged metadata
that doesn't belong to the filesystem, and the fourth component enables the
filesystem to detect lost writes.

Whenever a file system operation modifies a block, the change is submitted
to the log as part of a transaction.
The log then processes these transactions marking them done once they are
safely persisted to storage.
The logging code maintains the checksum and the log sequence number of the last
transactional update.
Checksums are useful for detecting torn writes and other discrepancies that can
be introduced between the computer and its storage devices.
Sequence number tracking enables log recovery to avoid applying out of date
log updates to the filesystem.

These two features improve overall runtime resiliency by providing a means for
the filesystem to detect obvious corruption when reading metadata blocks from
disk, but these buffer verifiers cannot provide any consistency checking
between metadata structures.

For more information, please see the documentation for
Documentation/filesystems/xfs-self-describing-metadata.rst

Reverse Mapping
---------------

The original design of XFS (circa 1993) is an improvement upon 1980s Unix
filesystem design.
In those days, storage density was expensive, CPU time was scarce, and
excessive seek time could kill performance.
For performance reasons, filesystem authors were reluctant to add redundancy to
the filesystem, even at the cost of data integrity.
Filesystems designers in the early 21st century choose different strategies to
increase internal redundancy -- either storing nearly identical copies of
metadata, or more space-efficient encoding techniques.

For XFS, a different redundancy strategy was chosen to modernize the design:
a secondary space usage index that maps allocated disk extents back to their
owners.
By adding a new index, the filesystem retains most of its ability to scale
well to heavily threaded workloads involving large datasets, since the primary
file metadata (the directory tree, the file block map, and the allocation
groups) remain unchanged.
Like any system that improves redundancy, the reverse-mapping feature increases
overhead costs for space mapping activities.
However, it has two critical advantages: first, the reverse index is key to
enabling online fsck and other requested functionality such as free space
defragmentation, better media failure reporting, and filesystem shrinking.
Second, the different ondisk storage format of the reverse mapping btree
defeats device-level deduplication because the filesystem requires real
redundancy.

+--------------------------------------------------------------------------+
| **Sidebar**:                                                             |
+--------------------------------------------------------------------------+
| A criticism of adding the secondary index is that it does nothing to     |
| improve the robustness of user data storage itself.                      |
| This is a valid point, but adding a new index for file data block        |
| checksums increases write amplification by turning data overwrites into  |
| copy-writes, which age the filesystem prematurely.                       |
| In keeping with thirty years of precedent, users who want file data      |
| integrity can supply as powerful a solution as they require.             |
| As for metadata, the complexity of adding a new secondary index of space |
| usage is much less than adding volume management and storage device      |
| mirroring to XFS itself.                                                 |
| Perfection of RAID and volume management are best left to existing       |
| layers in the kernel.                                                    |
+--------------------------------------------------------------------------+

The information captured in a reverse space mapping record is as follows:

.. code-block:: c

	struct xfs_rmap_irec {
	    xfs_agblock_t    rm_startblock;   /* extent start block */
	    xfs_extlen_t     rm_blockcount;   /* extent length */
	    uint64_t         rm_owner;        /* extent owner */
	    uint64_t         rm_offset;       /* offset within the owner */
	    unsigned int     rm_flags;        /* state flags */
	};

The first two fields capture the location and size of the physical space,
in units of filesystem blocks.
The owner field tells scrub which metadata structure or file inode have been
assigned this space.
For space allocated to files, the offset field tells scrub where the space was
mapped within the file fork.
Finally, the flags field provides extra information about the space usage --
is this an attribute fork extent?  A file mapping btree extent?  Or an
unwritten data extent?

Online filesystem checking judges the consistency of each primary metadata
record by comparing its information against all other space indices.
The reverse mapping index plays a key role in the consistency checking process
because it contains a centralized alternate copy of all space allocation
information.
Program runtime and ease of resource acquisition are the only real limits to
what online checking can consult.
For example, a file data extent mapping can be checked against:

* The absence of an entry in the free space information.
* The absence of an entry in the inode index.
* The absence of an entry in the reference count data if the file is not
  marked as having shared extents.
* The correspondence of an entry in the reverse mapping information.

There are several observations to make about reverse mapping indices:

1. Reverse mappings can provide a positive affirmation of correctness if any of
   the above primary metadata are in doubt.
   The checking code for most primary metadata follows a path similar to the
   one outlined above.

2. Proving the consistency of secondary metadata with the primary metadata is
   difficult because that requires a full scan of all primary space metadata,
   which is very time intensive.
   For example, checking a reverse mapping record for a file extent mapping
   btree block requires locking the file and searching the entire btree to
   confirm the block.
   Instead, scrub relies on rigorous cross-referencing during the primary space
   mapping structure checks.

3. Consistency scans must use non-blocking lock acquisition primitives if the
   required locking order is not the same order used by regular filesystem
   operations.
   For example, if the filesystem normally takes a file ILOCK before taking
   the AGF buffer lock but scrub wants to take a file ILOCK while holding
   an AGF buffer lock, scrub cannot block on that second acquisition.
   This means that forward progress during this part of a scan of the reverse
   mapping data cannot be guaranteed if system load is heavy.

In summary, reverse mappings play a key role in reconstruction of primary
metadata.
The details of how these records are staged, written to disk, and committed
into the filesystem are covered in subsequent sections.

Checking and Cross-Referencing
------------------------------

The first step of checking a metadata structure is to examine every record
contained within the structure and its relationship with the rest of the
system.
XFS contains multiple layers of checking to try to prevent inconsistent
metadata from wreaking havoc on the system.
Each of these layers contributes information that helps the kernel to make
three decisions about the health of a metadata structure:

- Is a part of this structure obviously corrupt (``XFS_SCRUB_OFLAG_CORRUPT``) ?
- Is this structure inconsistent with the rest of the system
  (``XFS_SCRUB_OFLAG_XCORRUPT``) ?
- Is there so much damage around the filesystem that cross-referencing is not
  possible (``XFS_SCRUB_OFLAG_XFAIL``) ?
- Can the structure be optimized to improve performance or reduce the size of
  metadata (``XFS_SCRUB_OFLAG_PREEN``) ?
- Does the structure contain data that is not inconsistent but deserves review
  by the system administrator (``XFS_SCRUB_OFLAG_WARNING``) ?

The following sections describe how the metadata scrubbing process works.

Metadata Buffer Verification
````````````````````````````

The lowest layer of metadata protection in XFS are the metadata verifiers built
into the buffer cache.
These functions perform inexpensive internal consistency checking of the block
itself, and answer these questions:

- Does the block belong to this filesystem?

- Does the block belong to the structure that asked for the read?
  This assumes that metadata blocks only have one owner, which is always true
  in XFS.

- Is the type of data stored in the block within a reasonable range of what
  scrub is expecting?

- Does the physical location of the block match the location it was read from?

- Does the block checksum match the data?

The scope of the protections here are very limited -- verifiers can only
establish that the filesystem code is reasonably free of gross corruption bugs
and that the storage system is reasonably competent at retrieval.
Corruption problems observed at runtime cause the generation of health reports,
failed system calls, and in the extreme case, filesystem shutdowns if the
corrupt metadata force the cancellation of a dirty transaction.

Every online fsck scrubbing function is expected to read every ondisk metadata
block of a structure in the course of checking the structure.
Corruption problems observed during a check are immediately reported to
userspace as corruption; during a cross-reference, they are reported as a
failure to cross-reference once the full examination is complete.
Reads satisfied by a buffer already in cache (and hence already verified)
bypass these checks.

Internal Consistency Checks
```````````````````````````

After the buffer cache, the next level of metadata protection is the internal
record verification code built into the filesystem.
These checks are split between the buffer verifiers, the in-filesystem users of
the buffer cache, and the scrub code itself, depending on the amount of higher
level context required.
The scope of checking is still internal to the block.
These higher level checking functions answer these questions:

- Does the type of data stored in the block match what scrub is expecting?

- Does the block belong to the owning structure that asked for the read?

- If the block contains records, do the records fit within the block?

- If the block tracks internal free space information, is it consistent with
  the record areas?

- Are the records contained inside the block free of obvious corruptions?

Record checks in this category are more rigorous and more time-intensive.
For example, block pointers and inumbers are checked to ensure that they point
within the dynamically allocated parts of an allocation group and within
the filesystem.
Names are checked for invalid characters, and flags are checked for invalid
combinations.
Other record attributes are checked for sensible values.
Btree records spanning an interval of the btree keyspace are checked for
correct order and lack of mergeability (except for file fork mappings).
For performance reasons, regular code may skip some of these checks unless
debugging is enabled or a write is about to occur.
Scrub functions, of course, must check all possible problems.

Validation of Userspace-Controlled Record Attributes
````````````````````````````````````````````````````

Various pieces of filesystem metadata are directly controlled by userspace.
Because of this nature, validation work cannot be more precise than checking
that a value is within the possible range.
These fields include:

- Superblock fields controlled by mount options
- Filesystem labels
- File timestamps
- File permissions
- File size
- File flags
- Names present in directory entries, extended attribute keys, and filesystem
  labels
- Extended attribute key namespaces
- Extended attribute values
- File data block contents
- Quota limits
- Quota timer expiration (if resource usage exceeds the soft limit)

Cross-Referencing Space Metadata
````````````````````````````````

After internal block checks, the next higher level of checking is
cross-referencing records between metadata structures.
For regular runtime code, the cost of these checks is considered to be
prohibitively expensive, but as scrub is dedicated to rooting out
inconsistencies, it must pursue all avenues of inquiry.
The exact set of cross-referencing is highly dependent on the context of the
data structure being checked.

The XFS btree code has keyspace scanning functions that online fsck uses to
cross reference one structure with another.
Specifically, scrub can scan the key space of an index to determine if that
keyspace is fully, sparsely, or not at all mapped to records.
For the reverse mapping btree, it is possible to mask parts of the key for the
purposes of performing a keyspace scan so that scrub can decide if the rmap
btree contains records mapping a certain extent of physical space without the
sparsenses of the rest of the rmap keyspace getting in the way.

Btree blocks undergo the following checks before cross-referencing:

- Does the type of data stored in the block match what scrub is expecting?

- Does the block belong to the owning structure that asked for the read?

- Do the records fit within the block?

- Are the records contained inside the block free of obvious corruptions?

- Are the name hashes in the correct order?

- Do node pointers within the btree point to valid block addresses for the type
  of btree?

- Do child pointers point towards the leaves?

- Do sibling pointers point across the same level?

- For each node block record, does the record key accurate reflect the contents
  of the child block?

Space allocation records are cross-referenced as follows:

1. Any space mentioned by any metadata structure are cross-referenced as
   follows:

   - Does the reverse mapping index list only the appropriate owner as the
     owner of each block?

   - Are none of the blocks claimed as free space?

   - If these aren't file data blocks, are none of the blocks claimed as space
     shared by different owners?

2. Btree blocks are cross-referenced as follows:

   - Everything in class 1 above.

   - If there's a parent node block, do the keys listed for this block match the
     keyspace of this block?

   - Do the sibling pointers point to valid blocks?  Of the same level?

   - Do the child pointers point to valid blocks?  Of the next level down?

3. Free space btree records are cross-referenced as follows:

   - Everything in class 1 and 2 above.

   - Does the reverse mapping index list no owners of this space?

   - Is this space not claimed by the inode index for inodes?

   - Is it not mentioned by the reference count index?

   - Is there a matching record in the other free space btree?

4. Inode btree records are cross-referenced as follows:

   - Everything in class 1 and 2 above.

   - Is there a matching record in free inode btree?

   - Do cleared bits in the holemask correspond with inode clusters?

   - Do set bits in the freemask correspond with inode records with zero link
     count?

5. Inode records are cross-referenced as follows:

   - Everything in class 1.

   - Do all the fields that summarize information about the file forks actually
     match those forks?

   - Does each inode with zero link count correspond to a record in the free
     inode btree?

6. File fork space mapping records are cross-referenced as follows:

   - Everything in class 1 and 2 above.

   - Is this space not mentioned by the inode btrees?

   - If this is a CoW fork mapping, does it correspond to a CoW entry in the
     reference count btree?

7. Reference count records are cross-referenced as follows:

   - Everything in class 1 and 2 above.

   - Within the space subkeyspace of the rmap btree (that is to say, all
     records mapped to a particular space extent and ignoring the owner info),
     are there the same number of reverse mapping records for each block as the
     reference count record claims?

Proposed patchsets are the series to find gaps in
`refcount btree
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-detect-refcount-gaps>`_,
`inode btree
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-detect-inobt-gaps>`_, and
`rmap btree
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-detect-rmapbt-gaps>`_ records;
to find
`mergeable records
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-detect-mergeable-records>`_;
and to
`improve cross referencing with rmap
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-strengthen-rmap-checking>`_
before starting a repair.

Checking Extended Attributes
````````````````````````````

Extended attributes implement a key-value store that enable fragments of data
to be attached to any file.
Both the kernel and userspace can access the keys and values, subject to
namespace and privilege restrictions.
Most typically these fragments are metadata about the file -- origins, security
contexts, user-supplied labels, indexing information, etc.

Names can be as long as 255 bytes and can exist in several different
namespaces.
Values can be as large as 64KB.
A file's extended attributes are stored in blocks mapped by the attr fork.
The mappings point to leaf blocks, remote value blocks, or dabtree blocks.
Block 0 in the attribute fork is always the top of the structure, but otherwise
each of the three types of blocks can be found at any offset in the attr fork.
Leaf blocks contain attribute key records that point to the name and the value.
Names are always stored elsewhere in the same leaf block.
Values that are less than 3/4 the size of a filesystem block are also stored
elsewhere in the same leaf block.
Remote value blocks contain values that are too large to fit inside a leaf.
If the leaf information exceeds a single filesystem block, a dabtree (also
rooted at block 0) is created to map hashes of the attribute names to leaf
blocks in the attr fork.

Checking an extended attribute structure is not so straightfoward due to the
lack of separation between attr blocks and index blocks.
Scrub must read each block mapped by the attr fork and ignore the non-leaf
blocks:

1. Walk the dabtree in the attr fork (if present) to ensure that there are no
   irregularities in the blocks or dabtree mappings that do not point to
   attr leaf blocks.

2. Walk the blocks of the attr fork looking for leaf blocks.
   For each entry inside a leaf:

   a. Validate that the name does not contain invalid characters.

   b. Read the attr value.
      This performs a named lookup of the attr name to ensure the correctness
      of the dabtree.
      If the value is stored in a remote block, this also validates the
      integrity of the remote value block.

Checking and Cross-Referencing Directories
``````````````````````````````````````````

The filesystem directory tree is a directed acylic graph structure, with files
constituting the nodes, and directory entries (dirents) constituting the edges.
Directories are a special type of file containing a set of mappings from a
255-byte sequence (name) to an inumber.
These are called directory entries, or dirents for short.
Each directory file must have exactly one directory pointing to the file.
A root directory points to itself.
Directory entries point to files of any type.
Each non-directory file may have multiple directories point to it.

In XFS, directories are implemented as a file containing up to three 32GB
partitions.
The first partition contains directory entry data blocks.
Each data block contains variable-sized records associating a user-provided
name with an inumber and, optionally, a file type.
If the directory entry data grows beyond one block, the second partition (which
exists as post-EOF extents) is populated with a block containing free space
information and an index that maps hashes of the dirent names to directory data
blocks in the first partition.
This makes directory name lookups very fast.
If this second partition grows beyond one block, the third partition is
populated with a linear array of free space information for faster
expansions.
If the free space has been separated and the second partition grows again
beyond one block, then a dabtree is used to map hashes of dirent names to
directory data blocks.

Checking a directory is pretty straightfoward:

1. Walk the dabtree in the second partition (if present) to ensure that there
   are no irregularities in the blocks or dabtree mappings that do not point to
   dirent blocks.

2. Walk the blocks of the first partition looking for directory entries.
   Each dirent is checked as follows:

   a. Does the name contain no invalid characters?

   b. Does the inumber correspond to an actual, allocated inode?

   c. Does the child inode have a nonzero link count?

   d. If a file type is included in the dirent, does it match the type of the
      inode?

   e. If the child is a subdirectory, does the child's dotdot pointer point
      back to the parent?

   f. If the directory has a second partition, perform a named lookup of the
      dirent name to ensure the correctness of the dabtree.

3. Walk the free space list in the third partition (if present) to ensure that
   the free spaces it describes are really unused.

Checking operations involving :ref:`parents <dirparent>` and
:ref:`file link counts <nlinks>` are discussed in more detail in later
sections.

Checking Directory/Attribute Btrees
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As stated in previous sections, the directory/attribute btree (dabtree) index
maps user-provided names to improve lookup times by avoiding linear scans.
Internally, it maps a 32-bit hash of the name to a block offset within the
appropriate file fork.

The internal structure of a dabtree closely resembles the btrees that record
fixed-size metadata records -- each dabtree block contains a magic number, a
checksum, sibling pointers, a UUID, a tree level, and a log sequence number.
The format of leaf and node records are the same -- each entry points to the
next level down in the hierarchy, with dabtree node records pointing to dabtree
leaf blocks, and dabtree leaf records pointing to non-dabtree blocks elsewhere
in the fork.

Checking and cross-referencing the dabtree is very similar to what is done for
space btrees:

- Does the type of data stored in the block match what scrub is expecting?

- Does the block belong to the owning structure that asked for the read?

- Do the records fit within the block?

- Are the records contained inside the block free of obvious corruptions?

- Are the name hashes in the correct order?

- Do node pointers within the dabtree point to valid fork offsets for dabtree
  blocks?

- Do leaf pointers within the dabtree point to valid fork offsets for directory
  or attr leaf blocks?

- Do child pointers point towards the leaves?

- Do sibling pointers point across the same level?

- For each dabtree node record, does the record key accurate reflect the
  contents of the child dabtree block?

- For each dabtree leaf record, does the record key accurate reflect the
  contents of the directory or attr block?

Cross-Referencing Summary Counters
``````````````````````````````````

XFS maintains three classes of summary counters: available resources, quota
resource usage, and file link counts.

In theory, the amount of available resources (data blocks, inodes, realtime
extents) can be found by walking the entire filesystem.
This would make for very slow reporting, so a transactional filesystem can
maintain summaries of this information in the superblock.
Cross-referencing these values against the filesystem metadata should be a
simple matter of walking the free space and inode metadata in each AG and the
realtime bitmap, but there are complications that will be discussed in
:ref:`more detail <fscounters>` later.

:ref:`Quota usage <quotacheck>` and :ref:`file link count <nlinks>`
checking are sufficiently complicated to warrant separate sections.

Post-Repair Reverification
``````````````````````````

After performing a repair, the checking code is run a second time to validate
the new structure, and the results of the health assessment are recorded
internally and returned to the calling process.
This step is critical for enabling system administrator to monitor the status
of the filesystem and the progress of any repairs.
For developers, it is a useful means to judge the efficacy of error detection
and correction in the online and offline checking tools.

Eventual Consistency vs. Online Fsck
------------------------------------

Complex operations can make modifications to multiple per-AG data structures
with a chain of transactions.
These chains, once committed to the log, are restarted during log recovery if
the system crashes while processing the chain.
Because the AG header buffers are unlocked between transactions within a chain,
online checking must coordinate with chained operations that are in progress to
avoid incorrectly detecting inconsistencies due to pending chains.
Furthermore, online repair must not run when operations are pending because
the metadata are temporarily inconsistent with each other, and rebuilding is
not possible.

Only online fsck has this requirement of total consistency of AG metadata, and
should be relatively rare as compared to filesystem change operations.
Online fsck coordinates with transaction chains as follows:

* For each AG, maintain a count of intent items targetting that AG.
  The count should be bumped whenever a new item is added to the chain.
  The count should be dropped when the filesystem has locked the AG header
  buffers and finished the work.

* When online fsck wants to examine an AG, it should lock the AG header
  buffers to quiesce all transaction chains that want to modify that AG.
  If the count is zero, proceed with the checking operation.
  If it is nonzero, cycle the buffer locks to allow the chain to make forward
  progress.

This may lead to online fsck taking a long time to complete, but regular
filesystem updates take precedence over background checking activity.
Details about the discovery of this situation are presented in the
:ref:`next section <chain_coordination>`, and details about the solution
are presented :ref:`after that<intent_drains>`.

.. _chain_coordination:

Discovery of the Problem
````````````````````````

Midway through the development of online scrubbing, the fsstress tests
uncovered a misinteraction between online fsck and compound transaction chains
created by other writer threads that resulted in false reports of metadata
inconsistency.
The root cause of these reports is the eventual consistency model introduced by
the expansion of deferred work items and compound transaction chains when
reverse mapping and reflink were introduced.

Originally, transaction chains were added to XFS to avoid deadlocks when
unmapping space from files.
Deadlock avoidance rules require that AGs only be locked in increasing order,
which makes it impossible (say) to use a single transaction to free a space
extent in AG 7 and then try to free a now superfluous block mapping btree block
in AG 3.
To avoid these kinds of deadlocks, XFS creates Extent Freeing Intent (EFI) log
items to commit to freeing some space in one transaction while deferring the
actual metadata updates to a fresh transaction.
The transaction sequence looks like this:

1. The first transaction contains a physical update to the file's block mapping
   structures to remove the mapping from the btree blocks.
   It then attaches to the in-memory transaction an action item to schedule
   deferred freeing of space.
   Concretely, each transaction maintains a list of ``struct
   xfs_defer_pending`` objects, each of which maintains a list of ``struct
   xfs_extent_free_item`` objects.
   Returning to the example above, the action item tracks the freeing of both
   the unmapped space from AG 7 and the block mapping btree (BMBT) block from
   AG 3.
   Deferred frees recorded in this manner are committed in the log by creating
   an EFI log item from the ``struct xfs_extent_free_item`` object and
   attaching the log item to the transaction.
   When the log is persisted to disk, the EFI item is written into the ondisk
   transaction record.
   EFIs can list up to 16 extents to free, all sorted in AG order.

2. The second transaction contains a physical update to the free space btrees
   of AG 3 to release the former BMBT block and a second physical update to the
   free space btrees of AG 7 to release the unmapped file space.
   Observe that the the physical updates are resequenced in the correct order
   when possible.
   Attached to the transaction is a an extent free done (EFD) log item.
   The EFD contains a pointer to the EFI logged in transaction #1 so that log
   recovery can tell if the EFI needs to be replayed.

If the system goes down after transaction #1 is written back to the filesystem
but before #2 is committed, a scan of the filesystem metadata would show
inconsistent filesystem metadata because there would not appear to be any owner
of the unmapped space.
Happily, log recovery corrects this inconsistency for us -- when recovery finds
an intent log item but does not find a corresponding intent done item, it will
reconstruct the incore state of the intent item and finish it.
In the example above, the log must replay both frees described in the recovered
EFI to complete the recovery phase.

There are subtleties to XFS' transaction chaining strategy to consider:

* Log items must be added to a transaction in the correct order to prevent
  conflicts with principal objects that are not held by the transaction.
  In other words, all per-AG metadata updates for an unmapped block must be
  completed before the last update to free the extent, and extents should not
  be reallocated until that last update commits to the log.

* AG header buffers are released between each transaction in a chain.
  This means that other threads can observe an AG in an intermediate state,
  but as long as the first subtlety is handled, this should not affect the
  correctness of filesystem operations.

* Unmounting the filesystem flushes all pending work to disk, which means that
  offline fsck never sees the temporary inconsistencies caused by deferred
  work item processing.

In this manner, XFS employs a form of eventual consistency to avoid deadlocks
and increase parallelism.

During the design phase of the reverse mapping and reflink features, it was
decided that it was impractical to cram all the reverse mapping updates for a
single filesystem change into a single transaction because a single file
mapping operation can explode into many small updates:

* The block mapping update itself
* A reverse mapping update for the block mapping update
* Fixing the freelist
* A reverse mapping update for the freelist fix

* A shape change to the block mapping btree
* A reverse mapping update for the btree update
* Fixing the freelist (again)
* A reverse mapping update for the freelist fix

* An update to the reference counting information
* A reverse mapping update for the refcount update
* Fixing the freelist (a third time)
* A reverse mapping update for the freelist fix

* Freeing any space that was unmapped and not owned by any other file
* Fixing the freelist (a fourth time)
* A reverse mapping update for the freelist fix

* Freeing the space used by the block mapping btree
* Fixing the freelist (a fifth time)
* A reverse mapping update for the freelist fix

Free list fixups are not usually needed more than once per AG per transaction
chain, but it is theoretically possible if space is very tight.
For copy-on-write updates this is even worse, because this must be done once to
remove the space from a staging area and again to map it into the file!

To deal with this explosion in a calm manner, XFS expands its use of deferred
work items to cover most reverse mapping updates and all refcount updates.
This reduces the worst case size of transaction reservations by breaking the
work into a long chain of small updates, which increases the degree of eventual
consistency in the system.
Again, this generally isn't a problem because XFS orders its deferred work
items carefully to avoid resource reuse conflicts between unsuspecting threads.

However, online fsck changes the rules -- remember that although physical
updates to per-AG structures are coordinated by locking the buffers for AG
headers, buffer locks are dropped between transactions.
Once scrub acquires resources and takes locks for a data structure, it must do
all the validation work without releasing the lock.
If the main lock for a space btree is an AG header buffer lock, scrub may have
interrupted another thread that is midway through finishing a chain.
For example, if a thread performing a copy-on-write has completed a reverse
mapping update but not the corresponding refcount update, the two AG btrees
will appear inconsistent to scrub and an observation of corruption will be
recorded.  This observation will not be correct.
If a repair is attempted in this state, the results will be catastrophic!

Several other solutions to this problem were evaluated upon discovery of this
flaw and rejected:

1. Add a higher level lock to allocation groups and require writer threads to
   acquire the higher level lock in AG order before making any changes.
   This would be very difficult to implement in practice because it is
   difficult to determine which locks need to be obtained, and in what order,
   without simulating the entire operation.
   Performing a dry run of a file operation to discover necessary locks would
   make the filesystem very slow.

2. Make the deferred work coordinator code aware of consecutive intent items
   targeting the same AG and have it hold the AG header buffers locked across
   the transaction roll between updates.
   This would introduce a lot of complexity into the coordinator since it is
   only loosely coupled with the actual deferred work items.
   It would also fail to solve the problem because deferred work items can
   generate new deferred subtasks, but all subtasks must be complete before
   work can start on a new sibling task.

3. Teach online fsck to walk all transactions waiting for whichever lock(s)
   protect the data structure being scrubbed to look for pending operations.
   The checking and repair operations must factor these pending operations into
   the evaluations being performed.
   This solution is a nonstarter because it is *extremely* invasive to the main
   filesystem.

.. _intent_drains:

Intent Drains
`````````````

Online fsck uses an atomic intent item counter and lock cycling to coordinate
with transaction chains.
There are two key properties to the drain mechanism.
First, the counter is incremented when a deferred work item is *queued* to a
transaction, and it is decremented after the associated intent done log item is
*committed* to another transaction.
The second property is that deferred work can be added to a transaction without
holding an AG header lock, but per-AG work items cannot be marked done without
locking that AG header buffer to log the physical updates and the intent done
log item.
The first property enables scrub to yield to running transaction chains, which
is an explicit deprioritization of online fsck to benefit file operations.
The second property of the drain is key to the correct coordination of scrub,
since scrub will always be able to decide if a conflict is possible.

For regular filesystem code, the drain works as follows:

1. Call the appropriate subsystem function to add a deferred work item to a
   transaction.

2. The function calls ``xfs_defer_drain_bump`` to increase the counter.

3. When the deferred item manager wants to finish the deferred work item, it
   calls ``->finish_item`` to complete it.

4. The ``->finish_item`` implementation logs some changes and calls
   ``xfs_defer_drain_drop`` to decrease the sloppy counter and wake up any threads
   waiting on the drain.

5. The subtransaction commits, which unlocks the resource associated with the
   intent item.

For scrub, the drain works as follows:

1. Lock the resource(s) associated with the metadata being scrubbed.
   For example, a scan of the refcount btree would lock the AGI and AGF header
   buffers.

2. If the counter is zero (``xfs_defer_drain_busy`` returns false), there are no
   chains in progress and the operation may proceed.

3. Otherwise, release the resources grabbed in step 1.

4. Wait for the intent counter to reach zero (``xfs_defer_drain_intents``), then go
   back to step 1 unless a signal has been caught.

To avoid polling in step 4, the drain provides a waitqueue for scrub threads to
be woken up whenever the intent count drops to zero.

The proposed patchset is the
`scrub intent drain series
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-drain-intents>`_.

.. _jump_labels:

Static Keys (aka Jump Label Patching)
`````````````````````````````````````

Online fsck for XFS separates the regular filesystem from the checking and
repair code as much as possible.
However, there are a few parts of online fsck (such as the intent drains, and
later, live update hooks) where it is useful for the online fsck code to know
what's going on in the rest of the filesystem.
Since it is not expected that online fsck will be constantly running in the
background, it is very important to minimize the runtime overhead imposed by
these hooks when online fsck is compiled into the kernel but not actively
running on behalf of userspace.
Taking locks in the hot path of a writer thread to access a data structure only
to find that no further action is necessary is expensive -- on the author's
computer, this have an overhead of 40-50ns per access.
Fortunately, the kernel supports dynamic code patching, which enables XFS to
replace a static branch to hook code with ``nop`` sleds when online fsck isn't
running.
This sled has an overhead of however long it takes the instruction decoder to
skip past the sled, which seems to be on the order of less than 1ns and
does not access memory outside of instruction fetching.

When online fsck enables the static key, the sled is replaced with an
unconditional branch to call the hook code.
The switchover is quite expensive (~22000ns) but is paid entirely by the
program that invoked online fsck, and can be amortized if multiple threads
enter online fsck at the same time, or if multiple filesystems are being
checked at the same time.
Changing the branch direction requires taking the CPU hotplug lock, and since
CPU initialization requires memory allocation, online fsck must be careful not
to change a static key while holding any locks or resources that could be
accessed in the memory reclaim paths.
To minimize contention on the CPU hotplug lock, care should be taken not to
enable or disable static keys unnecessarily.

Because static keys are intended to minimize hook overhead for regular
filesystem operations when xfs_scrub is not running, the intended usage
patterns are as follows:

- The hooked part of XFS should declare a static-scoped static key that
  defaults to false.
  The ``DEFINE_STATIC_KEY_FALSE`` macro takes care of this.
  The static key itself should be declared as a ``static`` variable.

- When deciding to invoke code that's only used by scrub, the regular
  filesystem should call the ``static_branch_unlikely`` predicate to avoid the
  scrub-only hook code if the static key is not enabled.

- The regular filesystem should export helper functions that call
  ``static_branch_inc`` to enable and ``static_branch_dec`` to disable the
  static key.
  Wrapper functions make it easy to compile out the relevant code if the kernel
  distributor turns off online fsck at build time.

- Scrub functions wanting to turn on scrub-only XFS functionality should call
  the ``xchk_fsgates_enable`` from the setup function to enable a specific
  hook.
  This must be done before obtaining any resources that are used by memory
  reclaim.
  Callers had better be sure they really need the functionality gated by the
  static key; the ``TRY_HARDER`` flag is useful here.

Online scrub has resource acquisition helpers (e.g. ``xchk_perag_lock``) to
handle locking AGI and AGF buffers for all scrubber functions.
If it detects a conflict between scrub and the running transactions, it will
try to wait for intents to complete.
If the caller of the helper has not enabled the static key, the helper will
return -EDEADLOCK, which should result in the scrub being restarted with the
``TRY_HARDER`` flag set.
The scrub setup function should detect that flag, enable the static key, and
try the scrub again.
Scrub teardown disables all static keys obtained by ``xchk_fsgates_enable``.

For more information, please see the kernel documentation of
Documentation/staging/static-keys.rst.

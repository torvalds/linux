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

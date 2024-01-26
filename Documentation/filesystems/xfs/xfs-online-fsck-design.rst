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
   Unsuccessful repairs are requeued as long as forward progress on repairs is
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

This allocation of responsibilities will be :ref:`revisited <scrubcheck>`
later in this document.

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

.. _secondary_metadata:

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
Maintenance") of G.  Graefe, `"Concurrent Queries and Updates in Summary Views
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
  automated fuzz testing of ondisk artifacts to find mischievous behavior and
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
Documentation/filesystems/xfs/xfs-self-describing-metadata.rst

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

Checking an extended attribute structure is not so straightforward due to the
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

Checking a directory is pretty straightforward:

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

* For each AG, maintain a count of intent items targeting that AG.
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
   Observe that the physical updates are resequenced in the correct order
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

.. _xfile:

Pageable Kernel Memory
----------------------

Some online checking functions work by scanning the filesystem to build a
shadow copy of an ondisk metadata structure in memory and comparing the two
copies.
For online repair to rebuild a metadata structure, it must compute the record
set that will be stored in the new structure before it can persist that new
structure to disk.
Ideally, repairs complete with a single atomic commit that introduces
a new data structure.
To meet these goals, the kernel needs to collect a large amount of information
in a place that doesn't require the correct operation of the filesystem.

Kernel memory isn't suitable because:

* Allocating a contiguous region of memory to create a C array is very
  difficult, especially on 32-bit systems.

* Linked lists of records introduce double pointer overhead which is very high
  and eliminate the possibility of indexed lookups.

* Kernel memory is pinned, which can drive the system into OOM conditions.

* The system might not have sufficient memory to stage all the information.

At any given time, online fsck does not need to keep the entire record set in
memory, which means that individual records can be paged out if necessary.
Continued development of online fsck demonstrated that the ability to perform
indexed data storage would also be very useful.
Fortunately, the Linux kernel already has a facility for byte-addressable and
pageable storage: tmpfs.
In-kernel graphics drivers (most notably i915) take advantage of tmpfs files
to store intermediate data that doesn't need to be in memory at all times, so
that usage precedent is already established.
Hence, the ``xfile`` was born!

+--------------------------------------------------------------------------+
| **Historical Sidebar**:                                                  |
+--------------------------------------------------------------------------+
| The first edition of online repair inserted records into a new btree as  |
| it found them, which failed because filesystem could shut down with a    |
| built data structure, which would be live after recovery finished.       |
|                                                                          |
| The second edition solved the half-rebuilt structure problem by storing  |
| everything in memory, but frequently ran the system out of memory.       |
|                                                                          |
| The third edition solved the OOM problem by using linked lists, but the  |
| memory overhead of the list pointers was extreme.                        |
+--------------------------------------------------------------------------+

xfile Access Models
```````````````````

A survey of the intended uses of xfiles suggested these use cases:

1. Arrays of fixed-sized records (space management btrees, directory and
   extended attribute entries)

2. Sparse arrays of fixed-sized records (quotas and link counts)

3. Large binary objects (BLOBs) of variable sizes (directory and extended
   attribute names and values)

4. Staging btrees in memory (reverse mapping btrees)

5. Arbitrary contents (realtime space management)

To support the first four use cases, high level data structures wrap the xfile
to share functionality between online fsck functions.
The rest of this section discusses the interfaces that the xfile presents to
four of those five higher level data structures.
The fifth use case is discussed in the :ref:`realtime summary <rtsummary>` case
study.

The most general storage interface supported by the xfile enables the reading
and writing of arbitrary quantities of data at arbitrary offsets in the xfile.
This capability is provided by ``xfile_pread`` and ``xfile_pwrite`` functions,
which behave similarly to their userspace counterparts.
XFS is very record-based, which suggests that the ability to load and store
complete records is important.
To support these cases, a pair of ``xfile_obj_load`` and ``xfile_obj_store``
functions are provided to read and persist objects into an xfile.
They are internally the same as pread and pwrite, except that they treat any
error as an out of memory error.
For online repair, squashing error conditions in this manner is an acceptable
behavior because the only reaction is to abort the operation back to userspace.
All five xfile usecases can be serviced by these four functions.

However, no discussion of file access idioms is complete without answering the
question, "But what about mmap?"
It is convenient to access storage directly with pointers, just like userspace
code does with regular memory.
Online fsck must not drive the system into OOM conditions, which means that
xfiles must be responsive to memory reclamation.
tmpfs can only push a pagecache folio to the swap cache if the folio is neither
pinned nor locked, which means the xfile must not pin too many folios.

Short term direct access to xfile contents is done by locking the pagecache
folio and mapping it into kernel address space.
Programmatic access (e.g. pread and pwrite) uses this mechanism.
Folio locks are not supposed to be held for long periods of time, so long
term direct access to xfile contents is done by bumping the folio refcount,
mapping it into kernel address space, and dropping the folio lock.
These long term users *must* be responsive to memory reclaim by hooking into
the shrinker infrastructure to know when to release folios.

The ``xfile_get_page`` and ``xfile_put_page`` functions are provided to
retrieve the (locked) folio that backs part of an xfile and to release it.
The only code to use these folio lease functions are the xfarray
:ref:`sorting<xfarray_sort>` algorithms and the :ref:`in-memory
btrees<xfbtree>`.

xfile Access Coordination
`````````````````````````

For security reasons, xfiles must be owned privately by the kernel.
They are marked ``S_PRIVATE`` to prevent interference from the security system,
must never be mapped into process file descriptor tables, and their pages must
never be mapped into userspace processes.

To avoid locking recursion issues with the VFS, all accesses to the shmfs file
are performed by manipulating the page cache directly.
xfile writers call the ``->write_begin`` and ``->write_end`` functions of the
xfile's address space to grab writable pages, copy the caller's buffer into the
page, and release the pages.
xfile readers call ``shmem_read_mapping_page_gfp`` to grab pages directly
before copying the contents into the caller's buffer.
In other words, xfiles ignore the VFS read and write code paths to avoid
having to create a dummy ``struct kiocb`` and to avoid taking inode and
freeze locks.
tmpfs cannot be frozen, and xfiles must not be exposed to userspace.

If an xfile is shared between threads to stage repairs, the caller must provide
its own locks to coordinate access.
For example, if a scrub function stores scan results in an xfile and needs
other threads to provide updates to the scanned data, the scrub function must
provide a lock for all threads to share.

.. _xfarray:

Arrays of Fixed-Sized Records
`````````````````````````````

In XFS, each type of indexed space metadata (free space, inodes, reference
counts, file fork space, and reverse mappings) consists of a set of fixed-size
records indexed with a classic B+ tree.
Directories have a set of fixed-size dirent records that point to the names,
and extended attributes have a set of fixed-size attribute keys that point to
names and values.
Quota counters and file link counters index records with numbers.
During a repair, scrub needs to stage new records during the gathering step and
retrieve them during the btree building step.

Although this requirement can be satisfied by calling the read and write
methods of the xfile directly, it is simpler for callers for there to be a
higher level abstraction to take care of computing array offsets, to provide
iterator functions, and to deal with sparse records and sorting.
The ``xfarray`` abstraction presents a linear array for fixed-size records atop
the byte-accessible xfile.

.. _xfarray_access_patterns:

Array Access Patterns
^^^^^^^^^^^^^^^^^^^^^

Array access patterns in online fsck tend to fall into three categories.
Iteration of records is assumed to be necessary for all cases and will be
covered in the next section.

The first type of caller handles records that are indexed by position.
Gaps may exist between records, and a record may be updated multiple times
during the collection step.
In other words, these callers want a sparse linearly addressed table file.
The typical use case are quota records or file link count records.
Access to array elements is performed programmatically via ``xfarray_load`` and
``xfarray_store`` functions, which wrap the similarly-named xfile functions to
provide loading and storing of array elements at arbitrary array indices.
Gaps are defined to be null records, and null records are defined to be a
sequence of all zero bytes.
Null records are detected by calling ``xfarray_element_is_null``.
They are created either by calling ``xfarray_unset`` to null out an existing
record or by never storing anything to an array index.

The second type of caller handles records that are not indexed by position
and do not require multiple updates to a record.
The typical use case here is rebuilding space btrees and key/value btrees.
These callers can add records to the array without caring about array indices
via the ``xfarray_append`` function, which stores a record at the end of the
array.
For callers that require records to be presentable in a specific order (e.g.
rebuilding btree data), the ``xfarray_sort`` function can arrange the sorted
records; this function will be covered later.

The third type of caller is a bag, which is useful for counting records.
The typical use case here is constructing space extent reference counts from
reverse mapping information.
Records can be put in the bag in any order, they can be removed from the bag
at any time, and uniqueness of records is left to callers.
The ``xfarray_store_anywhere`` function is used to insert a record in any
null record slot in the bag; and the ``xfarray_unset`` function removes a
record from the bag.

The proposed patchset is the
`big in-memory array
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=big-array>`_.

Iterating Array Elements
^^^^^^^^^^^^^^^^^^^^^^^^

Most users of the xfarray require the ability to iterate the records stored in
the array.
Callers can probe every possible array index with the following:

.. code-block:: c

	xfarray_idx_t i;
	foreach_xfarray_idx(array, i) {
	    xfarray_load(array, i, &rec);

	    /* do something with rec */
	}

All users of this idiom must be prepared to handle null records or must already
know that there aren't any.

For xfarray users that want to iterate a sparse array, the ``xfarray_iter``
function ignores indices in the xfarray that have never been written to by
calling ``xfile_seek_data`` (which internally uses ``SEEK_DATA``) to skip areas
of the array that are not populated with memory pages.
Once it finds a page, it will skip the zeroed areas of the page.

.. code-block:: c

	xfarray_idx_t i = XFARRAY_CURSOR_INIT;
	while ((ret = xfarray_iter(array, &i, &rec)) == 1) {
	    /* do something with rec */
	}

.. _xfarray_sort:

Sorting Array Elements
^^^^^^^^^^^^^^^^^^^^^^

During the fourth demonstration of online repair, a community reviewer remarked
that for performance reasons, online repair ought to load batches of records
into btree record blocks instead of inserting records into a new btree one at a
time.
The btree insertion code in XFS is responsible for maintaining correct ordering
of the records, so naturally the xfarray must also support sorting the record
set prior to bulk loading.

Case Study: Sorting xfarrays
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The sorting algorithm used in the xfarray is actually a combination of adaptive
quicksort and a heapsort subalgorithm in the spirit of
`Sedgewick <https://algs4.cs.princeton.edu/23quicksort/>`_ and
`pdqsort <https://github.com/orlp/pdqsort>`_, with customizations for the Linux
kernel.
To sort records in a reasonably short amount of time, ``xfarray`` takes
advantage of the binary subpartitioning offered by quicksort, but it also uses
heapsort to hedge against performance collapse if the chosen quicksort pivots
are poor.
Both algorithms are (in general) O(n * lg(n)), but there is a wide performance
gulf between the two implementations.

The Linux kernel already contains a reasonably fast implementation of heapsort.
It only operates on regular C arrays, which limits the scope of its usefulness.
There are two key places where the xfarray uses it:

* Sorting any record subset backed by a single xfile page.

* Loading a small number of xfarray records from potentially disparate parts
  of the xfarray into a memory buffer, and sorting the buffer.

In other words, ``xfarray`` uses heapsort to constrain the nested recursion of
quicksort, thereby mitigating quicksort's worst runtime behavior.

Choosing a quicksort pivot is a tricky business.
A good pivot splits the set to sort in half, leading to the divide and conquer
behavior that is crucial to  O(n * lg(n)) performance.
A poor pivot barely splits the subset at all, leading to O(n\ :sup:`2`)
runtime.
The xfarray sort routine tries to avoid picking a bad pivot by sampling nine
records into a memory buffer and using the kernel heapsort to identify the
median of the nine.

Most modern quicksort implementations employ Tukey's "ninther" to select a
pivot from a classic C array.
Typical ninther implementations pick three unique triads of records, sort each
of the triads, and then sort the middle value of each triad to determine the
ninther value.
As stated previously, however, xfile accesses are not entirely cheap.
It turned out to be much more performant to read the nine elements into a
memory buffer, run the kernel's in-memory heapsort on the buffer, and choose
the 4th element of that buffer as the pivot.
Tukey's ninthers are described in J. W. Tukey, `The ninther, a technique for
low-effort robust (resistant) location in large samples`, in *Contributions to
Survey Sampling and Applied Statistics*, edited by H. David, (Academic Press,
1978), pp. 251–257.

The partitioning of quicksort is fairly textbook -- rearrange the record
subset around the pivot, then set up the current and next stack frames to
sort with the larger and the smaller halves of the pivot, respectively.
This keeps the stack space requirements to log2(record count).

As a final performance optimization, the hi and lo scanning phase of quicksort
keeps examined xfile pages mapped in the kernel for as long as possible to
reduce map/unmap cycles.
Surprisingly, this reduces overall sort runtime by nearly half again after
accounting for the application of heapsort directly onto xfile pages.

.. _xfblob:

Blob Storage
````````````

Extended attributes and directories add an additional requirement for staging
records: arbitrary byte sequences of finite length.
Each directory entry record needs to store entry name,
and each extended attribute needs to store both the attribute name and value.
The names, keys, and values can consume a large amount of memory, so the
``xfblob`` abstraction was created to simplify management of these blobs
atop an xfile.

Blob arrays provide ``xfblob_load`` and ``xfblob_store`` functions to retrieve
and persist objects.
The store function returns a magic cookie for every object that it persists.
Later, callers provide this cookie to the ``xblob_load`` to recall the object.
The ``xfblob_free`` function frees a specific blob, and the ``xfblob_truncate``
function frees them all because compaction is not needed.

The details of repairing directories and extended attributes will be discussed
in a subsequent section about atomic extent swapping.
However, it should be noted that these repair functions only use blob storage
to cache a small number of entries before adding them to a temporary ondisk
file, which is why compaction is not required.

The proposed patchset is at the start of the
`extended attribute repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-xattrs>`_ series.

.. _xfbtree:

In-Memory B+Trees
`````````````````

The chapter about :ref:`secondary metadata<secondary_metadata>` mentioned that
checking and repairing of secondary metadata commonly requires coordination
between a live metadata scan of the filesystem and writer threads that are
updating that metadata.
Keeping the scan data up to date requires requires the ability to propagate
metadata updates from the filesystem into the data being collected by the scan.
This *can* be done by appending concurrent updates into a separate log file and
applying them before writing the new metadata to disk, but this leads to
unbounded memory consumption if the rest of the system is very busy.
Another option is to skip the side-log and commit live updates from the
filesystem directly into the scan data, which trades more overhead for a lower
maximum memory requirement.
In both cases, the data structure holding the scan results must support indexed
access to perform well.

Given that indexed lookups of scan data is required for both strategies, online
fsck employs the second strategy of committing live updates directly into
scan data.
Because xfarrays are not indexed and do not enforce record ordering, they
are not suitable for this task.
Conveniently, however, XFS has a library to create and maintain ordered reverse
mapping records: the existing rmap btree code!
If only there was a means to create one in memory.

Recall that the :ref:`xfile <xfile>` abstraction represents memory pages as a
regular file, which means that the kernel can create byte or block addressable
virtual address spaces at will.
The XFS buffer cache specializes in abstracting IO to block-oriented  address
spaces, which means that adaptation of the buffer cache to interface with
xfiles enables reuse of the entire btree library.
Btrees built atop an xfile are collectively known as ``xfbtrees``.
The next few sections describe how they actually work.

The proposed patchset is the
`in-memory btree
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=in-memory-btrees>`_
series.

Using xfiles as a Buffer Cache Target
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Two modifications are necessary to support xfiles as a buffer cache target.
The first is to make it possible for the ``struct xfs_buftarg`` structure to
host the ``struct xfs_buf`` rhashtable, because normally those are held by a
per-AG structure.
The second change is to modify the buffer ``ioapply`` function to "read" cached
pages from the xfile and "write" cached pages back to the xfile.
Multiple access to individual buffers is controlled by the ``xfs_buf`` lock,
since the xfile does not provide any locking on its own.
With this adaptation in place, users of the xfile-backed buffer cache use
exactly the same APIs as users of the disk-backed buffer cache.
The separation between xfile and buffer cache implies higher memory usage since
they do not share pages, but this property could some day enable transactional
updates to an in-memory btree.
Today, however, it simply eliminates the need for new code.

Space Management with an xfbtree
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Space management for an xfile is very simple -- each btree block is one memory
page in size.
These blocks use the same header format as an on-disk btree, but the in-memory
block verifiers ignore the checksums, assuming that xfile memory is no more
corruption-prone than regular DRAM.
Reusing existing code here is more important than absolute memory efficiency.

The very first block of an xfile backing an xfbtree contains a header block.
The header describes the owner, height, and the block number of the root
xfbtree block.

To allocate a btree block, use ``xfile_seek_data`` to find a gap in the file.
If there are no gaps, create one by extending the length of the xfile.
Preallocate space for the block with ``xfile_prealloc``, and hand back the
location.
To free an xfbtree block, use ``xfile_discard`` (which internally uses
``FALLOC_FL_PUNCH_HOLE``) to remove the memory page from the xfile.

Populating an xfbtree
^^^^^^^^^^^^^^^^^^^^^

An online fsck function that wants to create an xfbtree should proceed as
follows:

1. Call ``xfile_create`` to create an xfile.

2. Call ``xfs_alloc_memory_buftarg`` to create a buffer cache target structure
   pointing to the xfile.

3. Pass the buffer cache target, buffer ops, and other information to
   ``xfbtree_create`` to write an initial tree header and root block to the
   xfile.
   Each btree type should define a wrapper that passes necessary arguments to
   the creation function.
   For example, rmap btrees define ``xfs_rmapbt_mem_create`` to take care of
   all the necessary details for callers.
   A ``struct xfbtree`` object will be returned.

4. Pass the xfbtree object to the btree cursor creation function for the
   btree type.
   Following the example above, ``xfs_rmapbt_mem_cursor`` takes care of this
   for callers.

5. Pass the btree cursor to the regular btree functions to make queries against
   and to update the in-memory btree.
   For example, a btree cursor for an rmap xfbtree can be passed to the
   ``xfs_rmap_*`` functions just like any other btree cursor.
   See the :ref:`next section<xfbtree_commit>` for information on dealing with
   xfbtree updates that are logged to a transaction.

6. When finished, delete the btree cursor, destroy the xfbtree object, free the
   buffer target, and the destroy the xfile to release all resources.

.. _xfbtree_commit:

Committing Logged xfbtree Buffers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Although it is a clever hack to reuse the rmap btree code to handle the staging
structure, the ephemeral nature of the in-memory btree block storage presents
some challenges of its own.
The XFS transaction manager must not commit buffer log items for buffers backed
by an xfile because the log format does not understand updates for devices
other than the data device.
An ephemeral xfbtree probably will not exist by the time the AIL checkpoints
log transactions back into the filesystem, and certainly won't exist during
log recovery.
For these reasons, any code updating an xfbtree in transaction context must
remove the buffer log items from the transaction and write the updates into the
backing xfile before committing or cancelling the transaction.

The ``xfbtree_trans_commit`` and ``xfbtree_trans_cancel`` functions implement
this functionality as follows:

1. Find each buffer log item whose buffer targets the xfile.

2. Record the dirty/ordered status of the log item.

3. Detach the log item from the buffer.

4. Queue the buffer to a special delwri list.

5. Clear the transaction dirty flag if the only dirty log items were the ones
   that were detached in step 3.

6. Submit the delwri list to commit the changes to the xfile, if the updates
   are being committed.

After removing xfile logged buffers from the transaction in this manner, the
transaction can be committed or cancelled.

Bulk Loading of Ondisk B+Trees
------------------------------

As mentioned previously, early iterations of online repair built new btree
structures by creating a new btree and adding observations individually.
Loading a btree one record at a time had a slight advantage of not requiring
the incore records to be sorted prior to commit, but was very slow and leaked
blocks if the system went down during a repair.
Loading records one at a time also meant that repair could not control the
loading factor of the blocks in the new btree.

Fortunately, the venerable ``xfs_repair`` tool had a more efficient means for
rebuilding a btree index from a collection of records -- bulk btree loading.
This was implemented rather inefficiently code-wise, since ``xfs_repair``
had separate copy-pasted implementations for each btree type.

To prepare for online fsck, each of the four bulk loaders were studied, notes
were taken, and the four were refactored into a single generic btree bulk
loading mechanism.
Those notes in turn have been refreshed and are presented below.

Geometry Computation
````````````````````

The zeroth step of bulk loading is to assemble the entire record set that will
be stored in the new btree, and sort the records.
Next, call ``xfs_btree_bload_compute_geometry`` to compute the shape of the
btree from the record set, the type of btree, and any load factor preferences.
This information is required for resource reservation.

First, the geometry computation computes the minimum and maximum records that
will fit in a leaf block from the size of a btree block and the size of the
block header.
Roughly speaking, the maximum number of records is::

        maxrecs = (block_size - header_size) / record_size

The XFS design specifies that btree blocks should be merged when possible,
which means the minimum number of records is half of maxrecs::

        minrecs = maxrecs / 2

The next variable to determine is the desired loading factor.
This must be at least minrecs and no more than maxrecs.
Choosing minrecs is undesirable because it wastes half the block.
Choosing maxrecs is also undesirable because adding a single record to each
newly rebuilt leaf block will cause a tree split, which causes a noticeable
drop in performance immediately afterwards.
The default loading factor was chosen to be 75% of maxrecs, which provides a
reasonably compact structure without any immediate split penalties::

        default_load_factor = (maxrecs + minrecs) / 2

If space is tight, the loading factor will be set to maxrecs to try to avoid
running out of space::

        leaf_load_factor = enough space ? default_load_factor : maxrecs

Load factor is computed for btree node blocks using the combined size of the
btree key and pointer as the record size::

        maxrecs = (block_size - header_size) / (key_size + ptr_size)
        minrecs = maxrecs / 2
        node_load_factor = enough space ? default_load_factor : maxrecs

Once that's done, the number of leaf blocks required to store the record set
can be computed as::

        leaf_blocks = ceil(record_count / leaf_load_factor)

The number of node blocks needed to point to the next level down in the tree
is computed as::

        n_blocks = (n == 0 ? leaf_blocks : node_blocks[n])
        node_blocks[n + 1] = ceil(n_blocks / node_load_factor)

The entire computation is performed recursively until the current level only
needs one block.
The resulting geometry is as follows:

- For AG-rooted btrees, this level is the root level, so the height of the new
  tree is ``level + 1`` and the space needed is the summation of the number of
  blocks on each level.

- For inode-rooted btrees where the records in the top level do not fit in the
  inode fork area, the height is ``level + 2``, the space needed is the
  summation of the number of blocks on each level, and the inode fork points to
  the root block.

- For inode-rooted btrees where the records in the top level can be stored in
  the inode fork area, then the root block can be stored in the inode, the
  height is ``level + 1``, and the space needed is one less than the summation
  of the number of blocks on each level.
  This only becomes relevant when non-bmap btrees gain the ability to root in
  an inode, which is a future patchset and only included here for completeness.

.. _newbt:

Reserving New B+Tree Blocks
```````````````````````````

Once repair knows the number of blocks needed for the new btree, it allocates
those blocks using the free space information.
Each reserved extent is tracked separately by the btree builder state data.
To improve crash resilience, the reservation code also logs an Extent Freeing
Intent (EFI) item in the same transaction as each space allocation and attaches
its in-memory ``struct xfs_extent_free_item`` object to the space reservation.
If the system goes down, log recovery will use the unfinished EFIs to free the
unused space, the free space, leaving the filesystem unchanged.

Each time the btree builder claims a block for the btree from a reserved
extent, it updates the in-memory reservation to reflect the claimed space.
Block reservation tries to allocate as much contiguous space as possible to
reduce the number of EFIs in play.

While repair is writing these new btree blocks, the EFIs created for the space
reservations pin the tail of the ondisk log.
It's possible that other parts of the system will remain busy and push the head
of the log towards the pinned tail.
To avoid livelocking the filesystem, the EFIs must not pin the tail of the log
for too long.
To alleviate this problem, the dynamic relogging capability of the deferred ops
mechanism is reused here to commit a transaction at the log head containing an
EFD for the old EFI and new EFI at the head.
This enables the log to release the old EFI to keep the log moving forwards.

EFIs have a role to play during the commit and reaping phases; please see the
next section and the section about :ref:`reaping<reaping>` for more details.

Proposed patchsets are the
`bitmap rework
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-bitmap-rework>`_
and the
`preparation for bulk loading btrees
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-prep-for-bulk-loading>`_.


Writing the New Tree
````````````````````

This part is pretty simple -- the btree builder (``xfs_btree_bulkload``) claims
a block from the reserved list, writes the new btree block header, fills the
rest of the block with records, and adds the new leaf block to a list of
written blocks::

  ┌────┐
  │leaf│
  │RRR │
  └────┘

Sibling pointers are set every time a new block is added to the level::

  ┌────┐ ┌────┐ ┌────┐ ┌────┐
  │leaf│→│leaf│→│leaf│→│leaf│
  │RRR │←│RRR │←│RRR │←│RRR │
  └────┘ └────┘ └────┘ └────┘

When it finishes writing the record leaf blocks, it moves on to the node
blocks
To fill a node block, it walks each block in the next level down in the tree
to compute the relevant keys and write them into the parent node::

      ┌────┐       ┌────┐
      │node│──────→│node│
      │PP  │←──────│PP  │
      └────┘       └────┘
      ↙   ↘         ↙   ↘
  ┌────┐ ┌────┐ ┌────┐ ┌────┐
  │leaf│→│leaf│→│leaf│→│leaf│
  │RRR │←│RRR │←│RRR │←│RRR │
  └────┘ └────┘ └────┘ └────┘

When it reaches the root level, it is ready to commit the new btree!::

          ┌─────────┐
          │  root   │
          │   PP    │
          └─────────┘
          ↙         ↘
      ┌────┐       ┌────┐
      │node│──────→│node│
      │PP  │←──────│PP  │
      └────┘       └────┘
      ↙   ↘         ↙   ↘
  ┌────┐ ┌────┐ ┌────┐ ┌────┐
  │leaf│→│leaf│→│leaf│→│leaf│
  │RRR │←│RRR │←│RRR │←│RRR │
  └────┘ └────┘ └────┘ └────┘

The first step to commit the new btree is to persist the btree blocks to disk
synchronously.
This is a little complicated because a new btree block could have been freed
in the recent past, so the builder must use ``xfs_buf_delwri_queue_here`` to
remove the (stale) buffer from the AIL list before it can write the new blocks
to disk.
Blocks are queued for IO using a delwri list and written in one large batch
with ``xfs_buf_delwri_submit``.

Once the new blocks have been persisted to disk, control returns to the
individual repair function that called the bulk loader.
The repair function must log the location of the new root in a transaction,
clean up the space reservations that were made for the new btree, and reap the
old metadata blocks:

1. Commit the location of the new btree root.

2. For each incore reservation:

   a. Log Extent Freeing Done (EFD) items for all the space that was consumed
      by the btree builder.  The new EFDs must point to the EFIs attached to
      the reservation to prevent log recovery from freeing the new blocks.

   b. For unclaimed portions of incore reservations, create a regular deferred
      extent free work item to be free the unused space later in the
      transaction chain.

   c. The EFDs and EFIs logged in steps 2a and 2b must not overrun the
      reservation of the committing transaction.
      If the btree loading code suspects this might be about to happen, it must
      call ``xrep_defer_finish`` to clear out the deferred work and obtain a
      fresh transaction.

3. Clear out the deferred work a second time to finish the commit and clean
   the repair transaction.

The transaction rolling in steps 2c and 3 represent a weakness in the repair
algorithm, because a log flush and a crash before the end of the reap step can
result in space leaking.
Online repair functions minimize the chances of this occurring by using very
large transactions, which each can accommodate many thousands of block freeing
instructions.
Repair moves on to reaping the old blocks, which will be presented in a
subsequent :ref:`section<reaping>` after a few case studies of bulk loading.

Case Study: Rebuilding the Inode Index
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The high level process to rebuild the inode index btree is:

1. Walk the reverse mapping records to generate ``struct xfs_inobt_rec``
   records from the inode chunk information and a bitmap of the old inode btree
   blocks.

2. Append the records to an xfarray in inode order.

3. Use the ``xfs_btree_bload_compute_geometry`` function to compute the number
   of blocks needed for the inode btree.
   If the free space inode btree is enabled, call it again to estimate the
   geometry of the finobt.

4. Allocate the number of blocks computed in the previous step.

5. Use ``xfs_btree_bload`` to write the xfarray records to btree blocks and
   generate the internal node blocks.
   If the free space inode btree is enabled, call it again to load the finobt.

6. Commit the location of the new btree root block(s) to the AGI.

7. Reap the old btree blocks using the bitmap created in step 1.

Details are as follows.

The inode btree maps inumbers to the ondisk location of the associated
inode records, which means that the inode btrees can be rebuilt from the
reverse mapping information.
Reverse mapping records with an owner of ``XFS_RMAP_OWN_INOBT`` marks the
location of the old inode btree blocks.
Each reverse mapping record with an owner of ``XFS_RMAP_OWN_INODES`` marks the
location of at least one inode cluster buffer.
A cluster is the smallest number of ondisk inodes that can be allocated or
freed in a single transaction; it is never smaller than 1 fs block or 4 inodes.

For the space represented by each inode cluster, ensure that there are no
records in the free space btrees nor any records in the reference count btree.
If there are, the space metadata inconsistencies are reason enough to abort the
operation.
Otherwise, read each cluster buffer to check that its contents appear to be
ondisk inodes and to decide if the file is allocated
(``xfs_dinode.i_mode != 0``) or free (``xfs_dinode.i_mode == 0``).
Accumulate the results of successive inode cluster buffer reads until there is
enough information to fill a single inode chunk record, which is 64 consecutive
numbers in the inumber keyspace.
If the chunk is sparse, the chunk record may include holes.

Once the repair function accumulates one chunk's worth of data, it calls
``xfarray_append`` to add the inode btree record to the xfarray.
This xfarray is walked twice during the btree creation step -- once to populate
the inode btree with all inode chunk records, and a second time to populate the
free inode btree with records for chunks that have free non-sparse inodes.
The number of records for the inode btree is the number of xfarray records,
but the record count for the free inode btree has to be computed as inode chunk
records are stored in the xfarray.

The proposed patchset is the
`AG btree repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-ag-btrees>`_
series.

Case Study: Rebuilding the Space Reference Counts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Reverse mapping records are used to rebuild the reference count information.
Reference counts are required for correct operation of copy on write for shared
file data.
Imagine the reverse mapping entries as rectangles representing extents of
physical blocks, and that the rectangles can be laid down to allow them to
overlap each other.
From the diagram below, it is apparent that a reference count record must start
or end wherever the height of the stack changes.
In other words, the record emission stimulus is level-triggered::

                        █    ███
              ██      █████ ████   ███        ██████
        ██   ████     ███████████ ████     █████████
        ████████████████████████████████ ███████████
        ^ ^  ^^ ^^    ^ ^^ ^^^  ^^^^  ^ ^^ ^  ^     ^
        2 1  23 21    3 43 234  2123  1 01 2  3     0

The ondisk reference count btree does not store the refcount == 0 cases because
the free space btree already records which blocks are free.
Extents being used to stage copy-on-write operations should be the only records
with refcount == 1.
Single-owner file blocks aren't recorded in either the free space or the
reference count btrees.

The high level process to rebuild the reference count btree is:

1. Walk the reverse mapping records to generate ``struct xfs_refcount_irec``
   records for any space having more than one reverse mapping and add them to
   the xfarray.
   Any records owned by ``XFS_RMAP_OWN_COW`` are also added to the xfarray
   because these are extents allocated to stage a copy on write operation and
   are tracked in the refcount btree.

   Use any records owned by ``XFS_RMAP_OWN_REFC`` to create a bitmap of old
   refcount btree blocks.

2. Sort the records in physical extent order, putting the CoW staging extents
   at the end of the xfarray.
   This matches the sorting order of records in the refcount btree.

3. Use the ``xfs_btree_bload_compute_geometry`` function to compute the number
   of blocks needed for the new tree.

4. Allocate the number of blocks computed in the previous step.

5. Use ``xfs_btree_bload`` to write the xfarray records to btree blocks and
   generate the internal node blocks.

6. Commit the location of new btree root block to the AGF.

7. Reap the old btree blocks using the bitmap created in step 1.

Details are as follows; the same algorithm is used by ``xfs_repair`` to
generate refcount information from reverse mapping records.

- Until the reverse mapping btree runs out of records:

  - Retrieve the next record from the btree and put it in a bag.

  - Collect all records with the same starting block from the btree and put
    them in the bag.

  - While the bag isn't empty:

    - Among the mappings in the bag, compute the lowest block number where the
      reference count changes.
      This position will be either the starting block number of the next
      unprocessed reverse mapping or the next block after the shortest mapping
      in the bag.

    - Remove all mappings from the bag that end at this position.

    - Collect all reverse mappings that start at this position from the btree
      and put them in the bag.

    - If the size of the bag changed and is greater than one, create a new
      refcount record associating the block number range that we just walked to
      the size of the bag.

The bag-like structure in this case is a type 2 xfarray as discussed in the
:ref:`xfarray access patterns<xfarray_access_patterns>` section.
Reverse mappings are added to the bag using ``xfarray_store_anywhere`` and
removed via ``xfarray_unset``.
Bag members are examined through ``xfarray_iter`` loops.

The proposed patchset is the
`AG btree repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-ag-btrees>`_
series.

Case Study: Rebuilding File Fork Mapping Indices
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The high level process to rebuild a data/attr fork mapping btree is:

1. Walk the reverse mapping records to generate ``struct xfs_bmbt_rec``
   records from the reverse mapping records for that inode and fork.
   Append these records to an xfarray.
   Compute the bitmap of the old bmap btree blocks from the ``BMBT_BLOCK``
   records.

2. Use the ``xfs_btree_bload_compute_geometry`` function to compute the number
   of blocks needed for the new tree.

3. Sort the records in file offset order.

4. If the extent records would fit in the inode fork immediate area, commit the
   records to that immediate area and skip to step 8.

5. Allocate the number of blocks computed in the previous step.

6. Use ``xfs_btree_bload`` to write the xfarray records to btree blocks and
   generate the internal node blocks.

7. Commit the new btree root block to the inode fork immediate area.

8. Reap the old btree blocks using the bitmap created in step 1.

There are some complications here:
First, it's possible to move the fork offset to adjust the sizes of the
immediate areas if the data and attr forks are not both in BMBT format.
Second, if there are sufficiently few fork mappings, it may be possible to use
EXTENTS format instead of BMBT, which may require a conversion.
Third, the incore extent map must be reloaded carefully to avoid disturbing
any delayed allocation extents.

The proposed patchset is the
`file mapping repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-file-mappings>`_
series.

.. _reaping:

Reaping Old Metadata Blocks
---------------------------

Whenever online fsck builds a new data structure to replace one that is
suspect, there is a question of how to find and dispose of the blocks that
belonged to the old structure.
The laziest method of course is not to deal with them at all, but this slowly
leads to service degradations as space leaks out of the filesystem.
Hopefully, someone will schedule a rebuild of the free space information to
plug all those leaks.
Offline repair rebuilds all space metadata after recording the usage of
the files and directories that it decides not to clear, hence it can build new
structures in the discovered free space and avoid the question of reaping.

As part of a repair, online fsck relies heavily on the reverse mapping records
to find space that is owned by the corresponding rmap owner yet truly free.
Cross referencing rmap records with other rmap records is necessary because
there may be other data structures that also think they own some of those
blocks (e.g. crosslinked trees).
Permitting the block allocator to hand them out again will not push the system
towards consistency.

For space metadata, the process of finding extents to dispose of generally
follows this format:

1. Create a bitmap of space used by data structures that must be preserved.
   The space reservations used to create the new metadata can be used here if
   the same rmap owner code is used to denote all of the objects being rebuilt.

2. Survey the reverse mapping data to create a bitmap of space owned by the
   same ``XFS_RMAP_OWN_*`` number for the metadata that is being preserved.

3. Use the bitmap disunion operator to subtract (1) from (2).
   The remaining set bits represent candidate extents that could be freed.
   The process moves on to step 4 below.

Repairs for file-based metadata such as extended attributes, directories,
symbolic links, quota files and realtime bitmaps are performed by building a
new structure attached to a temporary file and swapping the forks.
Afterward, the mappings in the old file fork are the candidate blocks for
disposal.

The process for disposing of old extents is as follows:

4. For each candidate extent, count the number of reverse mapping records for
   the first block in that extent that do not have the same rmap owner for the
   data structure being repaired.

   - If zero, the block has a single owner and can be freed.

   - If not, the block is part of a crosslinked structure and must not be
     freed.

5. Starting with the next block in the extent, figure out how many more blocks
   have the same zero/nonzero other owner status as that first block.

6. If the region is crosslinked, delete the reverse mapping entry for the
   structure being repaired and move on to the next region.

7. If the region is to be freed, mark any corresponding buffers in the buffer
   cache as stale to prevent log writeback.

8. Free the region and move on.

However, there is one complication to this procedure.
Transactions are of finite size, so the reaping process must be careful to roll
the transactions to avoid overruns.
Overruns come from two sources:

a. EFIs logged on behalf of space that is no longer occupied

b. Log items for buffer invalidations

This is also a window in which a crash during the reaping process can leak
blocks.
As stated earlier, online repair functions use very large transactions to
minimize the chances of this occurring.

The proposed patchset is the
`preparation for bulk loading btrees
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-prep-for-bulk-loading>`_
series.

Case Study: Reaping After a Regular Btree Repair
````````````````````````````````````````````````

Old reference count and inode btrees are the easiest to reap because they have
rmap records with special owner codes: ``XFS_RMAP_OWN_REFC`` for the refcount
btree, and ``XFS_RMAP_OWN_INOBT`` for the inode and free inode btrees.
Creating a list of extents to reap the old btree blocks is quite simple,
conceptually:

1. Lock the relevant AGI/AGF header buffers to prevent allocation and frees.

2. For each reverse mapping record with an rmap owner corresponding to the
   metadata structure being rebuilt, set the corresponding range in a bitmap.

3. Walk the current data structures that have the same rmap owner.
   For each block visited, clear that range in the above bitmap.

4. Each set bit in the bitmap represents a block that could be a block from the
   old data structures and hence is a candidate for reaping.
   In other words, ``(rmap_records_owned_by & ~blocks_reachable_by_walk)``
   are the blocks that might be freeable.

If it is possible to maintain the AGF lock throughout the repair (which is the
common case), then step 2 can be performed at the same time as the reverse
mapping record walk that creates the records for the new btree.

Case Study: Rebuilding the Free Space Indices
`````````````````````````````````````````````

The high level process to rebuild the free space indices is:

1. Walk the reverse mapping records to generate ``struct xfs_alloc_rec_incore``
   records from the gaps in the reverse mapping btree.

2. Append the records to an xfarray.

3. Use the ``xfs_btree_bload_compute_geometry`` function to compute the number
   of blocks needed for each new tree.

4. Allocate the number of blocks computed in the previous step from the free
   space information collected.

5. Use ``xfs_btree_bload`` to write the xfarray records to btree blocks and
   generate the internal node blocks for the free space by length index.
   Call it again for the free space by block number index.

6. Commit the locations of the new btree root blocks to the AGF.

7. Reap the old btree blocks by looking for space that is not recorded by the
   reverse mapping btree, the new free space btrees, or the AGFL.

Repairing the free space btrees has three key complications over a regular
btree repair:

First, free space is not explicitly tracked in the reverse mapping records.
Hence, the new free space records must be inferred from gaps in the physical
space component of the keyspace of the reverse mapping btree.

Second, free space repairs cannot use the common btree reservation code because
new blocks are reserved out of the free space btrees.
This is impossible when repairing the free space btrees themselves.
However, repair holds the AGF buffer lock for the duration of the free space
index reconstruction, so it can use the collected free space information to
supply the blocks for the new free space btrees.
It is not necessary to back each reserved extent with an EFI because the new
free space btrees are constructed in what the ondisk filesystem thinks is
unowned space.
However, if reserving blocks for the new btrees from the collected free space
information changes the number of free space records, repair must re-estimate
the new free space btree geometry with the new record count until the
reservation is sufficient.
As part of committing the new btrees, repair must ensure that reverse mappings
are created for the reserved blocks and that unused reserved blocks are
inserted into the free space btrees.
Deferrred rmap and freeing operations are used to ensure that this transition
is atomic, similar to the other btree repair functions.

Third, finding the blocks to reap after the repair is not overly
straightforward.
Blocks for the free space btrees and the reverse mapping btrees are supplied by
the AGFL.
Blocks put onto the AGFL have reverse mapping records with the owner
``XFS_RMAP_OWN_AG``.
This ownership is retained when blocks move from the AGFL into the free space
btrees or the reverse mapping btrees.
When repair walks reverse mapping records to synthesize free space records, it
creates a bitmap (``ag_owner_bitmap``) of all the space claimed by
``XFS_RMAP_OWN_AG`` records.
The repair context maintains a second bitmap corresponding to the rmap btree
blocks and the AGFL blocks (``rmap_agfl_bitmap``).
When the walk is complete, the bitmap disunion operation ``(ag_owner_bitmap &
~rmap_agfl_bitmap)`` computes the extents that are used by the old free space
btrees.
These blocks can then be reaped using the methods outlined above.

The proposed patchset is the
`AG btree repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-ag-btrees>`_
series.

.. _rmap_reap:

Case Study: Reaping After Repairing Reverse Mapping Btrees
``````````````````````````````````````````````````````````

Old reverse mapping btrees are less difficult to reap after a repair.
As mentioned in the previous section, blocks on the AGFL, the two free space
btree blocks, and the reverse mapping btree blocks all have reverse mapping
records with ``XFS_RMAP_OWN_AG`` as the owner.
The full process of gathering reverse mapping records and building a new btree
are described in the case study of
:ref:`live rebuilds of rmap data <rmap_repair>`, but a crucial point from that
discussion is that the new rmap btree will not contain any records for the old
rmap btree, nor will the old btree blocks be tracked in the free space btrees.
The list of candidate reaping blocks is computed by setting the bits
corresponding to the gaps in the new rmap btree records, and then clearing the
bits corresponding to extents in the free space btrees and the current AGFL
blocks.
The result ``(new_rmapbt_gaps & ~(agfl | bnobt_records))`` are reaped using the
methods outlined above.

The rest of the process of rebuildng the reverse mapping btree is discussed
in a separate :ref:`case study<rmap_repair>`.

The proposed patchset is the
`AG btree repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-ag-btrees>`_
series.

Case Study: Rebuilding the AGFL
```````````````````````````````

The allocation group free block list (AGFL) is repaired as follows:

1. Create a bitmap for all the space that the reverse mapping data claims is
   owned by ``XFS_RMAP_OWN_AG``.

2. Subtract the space used by the two free space btrees and the rmap btree.

3. Subtract any space that the reverse mapping data claims is owned by any
   other owner, to avoid re-adding crosslinked blocks to the AGFL.

4. Once the AGFL is full, reap any blocks leftover.

5. The next operation to fix the freelist will right-size the list.

See `fs/xfs/scrub/agheader_repair.c <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs/xfs/scrub/agheader_repair.c>`_ for more details.

Inode Record Repairs
--------------------

Inode records must be handled carefully, because they have both ondisk records
("dinodes") and an in-memory ("cached") representation.
There is a very high potential for cache coherency issues if online fsck is not
careful to access the ondisk metadata *only* when the ondisk metadata is so
badly damaged that the filesystem cannot load the in-memory representation.
When online fsck wants to open a damaged file for scrubbing, it must use
specialized resource acquisition functions that return either the in-memory
representation *or* a lock on whichever object is necessary to prevent any
update to the ondisk location.

The only repairs that should be made to the ondisk inode buffers are whatever
is necessary to get the in-core structure loaded.
This means fixing whatever is caught by the inode cluster buffer and inode fork
verifiers, and retrying the ``iget`` operation.
If the second ``iget`` fails, the repair has failed.

Once the in-memory representation is loaded, repair can lock the inode and can
subject it to comprehensive checks, repairs, and optimizations.
Most inode attributes are easy to check and constrain, or are user-controlled
arbitrary bit patterns; these are both easy to fix.
Dealing with the data and attr fork extent counts and the file block counts is
more complicated, because computing the correct value requires traversing the
forks, or if that fails, leaving the fields invalid and waiting for the fork
fsck functions to run.

The proposed patchset is the
`inode
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-inodes>`_
repair series.

Quota Record Repairs
--------------------

Similar to inodes, quota records ("dquots") also have both ondisk records and
an in-memory representation, and hence are subject to the same cache coherency
issues.
Somewhat confusingly, both are known as dquots in the XFS codebase.

The only repairs that should be made to the ondisk quota record buffers are
whatever is necessary to get the in-core structure loaded.
Once the in-memory representation is loaded, the only attributes needing
checking are obviously bad limits and timer values.

Quota usage counters are checked, repaired, and discussed separately in the
section about :ref:`live quotacheck <quotacheck>`.

The proposed patchset is the
`quota
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-quota>`_
repair series.

.. _fscounters:

Freezing to Fix Summary Counters
--------------------------------

Filesystem summary counters track availability of filesystem resources such
as free blocks, free inodes, and allocated inodes.
This information could be compiled by walking the free space and inode indexes,
but this is a slow process, so XFS maintains a copy in the ondisk superblock
that should reflect the ondisk metadata, at least when the filesystem has been
unmounted cleanly.
For performance reasons, XFS also maintains incore copies of those counters,
which are key to enabling resource reservations for active transactions.
Writer threads reserve the worst-case quantities of resources from the
incore counter and give back whatever they don't use at commit time.
It is therefore only necessary to serialize on the superblock when the
superblock is being committed to disk.

The lazy superblock counter feature introduced in XFS v5 took this even further
by training log recovery to recompute the summary counters from the AG headers,
which eliminated the need for most transactions even to touch the superblock.
The only time XFS commits the summary counters is at filesystem unmount.
To reduce contention even further, the incore counter is implemented as a
percpu counter, which means that each CPU is allocated a batch of blocks from a
global incore counter and can satisfy small allocations from the local batch.

The high-performance nature of the summary counters makes it difficult for
online fsck to check them, since there is no way to quiesce a percpu counter
while the system is running.
Although online fsck can read the filesystem metadata to compute the correct
values of the summary counters, there's no way to hold the value of a percpu
counter stable, so it's quite possible that the counter will be out of date by
the time the walk is complete.
Earlier versions of online scrub would return to userspace with an incomplete
scan flag, but this is not a satisfying outcome for a system administrator.
For repairs, the in-memory counters must be stabilized while walking the
filesystem metadata to get an accurate reading and install it in the percpu
counter.

To satisfy this requirement, online fsck must prevent other programs in the
system from initiating new writes to the filesystem, it must disable background
garbage collection threads, and it must wait for existing writer programs to
exit the kernel.
Once that has been established, scrub can walk the AG free space indexes, the
inode btrees, and the realtime bitmap to compute the correct value of all
four summary counters.
This is very similar to a filesystem freeze, though not all of the pieces are
necessary:

- The final freeze state is set one higher than ``SB_FREEZE_COMPLETE`` to
  prevent other threads from thawing the filesystem, or other scrub threads
  from initiating another fscounters freeze.

- It does not quiesce the log.

With this code in place, it is now possible to pause the filesystem for just
long enough to check and correct the summary counters.

+--------------------------------------------------------------------------+
| **Historical Sidebar**:                                                  |
+--------------------------------------------------------------------------+
| The initial implementation used the actual VFS filesystem freeze         |
| mechanism to quiesce filesystem activity.                                |
| With the filesystem frozen, it is possible to resolve the counter values |
| with exact precision, but there are many problems with calling the VFS   |
| methods directly:                                                        |
|                                                                          |
| - Other programs can unfreeze the filesystem without our knowledge.      |
|   This leads to incorrect scan results and incorrect repairs.            |
|                                                                          |
| - Adding an extra lock to prevent others from thawing the filesystem     |
|   required the addition of a ``->freeze_super`` function to wrap         |
|   ``freeze_fs()``.                                                       |
|   This in turn caused other subtle problems because it turns out that    |
|   the VFS ``freeze_super`` and ``thaw_super`` functions can drop the     |
|   last reference to the VFS superblock, and any subsequent access        |
|   becomes a UAF bug!                                                     |
|   This can happen if the filesystem is unmounted while the underlying    |
|   block device has frozen the filesystem.                                |
|   This problem could be solved by grabbing extra references to the       |
|   superblock, but it felt suboptimal given the other inadequacies of     |
|   this approach.                                                         |
|                                                                          |
| - The log need not be quiesced to check the summary counters, but a VFS  |
|   freeze initiates one anyway.                                           |
|   This adds unnecessary runtime to live fscounter fsck operations.       |
|                                                                          |
| - Quiescing the log means that XFS flushes the (possibly incorrect)      |
|   counters to disk as part of cleaning the log.                          |
|                                                                          |
| - A bug in the VFS meant that freeze could complete even when            |
|   sync_filesystem fails to flush the filesystem and returns an error.    |
|   This bug was fixed in Linux 5.17.                                      |
+--------------------------------------------------------------------------+

The proposed patchset is the
`summary counter cleanup
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-fscounters>`_
series.

Full Filesystem Scans
---------------------

Certain types of metadata can only be checked by walking every file in the
entire filesystem to record observations and comparing the observations against
what's recorded on disk.
Like every other type of online repair, repairs are made by writing those
observations to disk in a replacement structure and committing it atomically.
However, it is not practical to shut down the entire filesystem to examine
hundreds of billions of files because the downtime would be excessive.
Therefore, online fsck must build the infrastructure to manage a live scan of
all the files in the filesystem.
There are two questions that need to be solved to perform a live walk:

- How does scrub manage the scan while it is collecting data?

- How does the scan keep abreast of changes being made to the system by other
  threads?

.. _iscan:

Coordinated Inode Scans
```````````````````````

In the original Unix filesystems of the 1970s, each directory entry contained
an index number (*inumber*) which was used as an index into on ondisk array
(*itable*) of fixed-size records (*inodes*) describing a file's attributes and
its data block mapping.
This system is described by J. Lions, `"inode (5659)"
<http://www.lemis.com/grog/Documentation/Lions/>`_ in *Lions' Commentary on
UNIX, 6th Edition*, (Dept. of Computer Science, the University of New South
Wales, November 1977), pp. 18-2; and later by D. Ritchie and K. Thompson,
`"Implementation of the File System"
<https://archive.org/details/bstj57-6-1905/page/n8/mode/1up>`_, from *The UNIX
Time-Sharing System*, (The Bell System Technical Journal, July 1978), pp.
1913-4.

XFS retains most of this design, except now inumbers are search keys over all
the space in the data section filesystem.
They form a continuous keyspace that can be expressed as a 64-bit integer,
though the inodes themselves are sparsely distributed within the keyspace.
Scans proceed in a linear fashion across the inumber keyspace, starting from
``0x0`` and ending at ``0xFFFFFFFFFFFFFFFF``.
Naturally, a scan through a keyspace requires a scan cursor object to track the
scan progress.
Because this keyspace is sparse, this cursor contains two parts.
The first part of this scan cursor object tracks the inode that will be
examined next; call this the examination cursor.
Somewhat less obviously, the scan cursor object must also track which parts of
the keyspace have already been visited, which is critical for deciding if a
concurrent filesystem update needs to be incorporated into the scan data.
Call this the visited inode cursor.

Advancing the scan cursor is a multi-step process encapsulated in
``xchk_iscan_iter``:

1. Lock the AGI buffer of the AG containing the inode pointed to by the visited
   inode cursor.
   This guarantee that inodes in this AG cannot be allocated or freed while
   advancing the cursor.

2. Use the per-AG inode btree to look up the next inumber after the one that
   was just visited, since it may not be keyspace adjacent.

3. If there are no more inodes left in this AG:

   a. Move the examination cursor to the point of the inumber keyspace that
      corresponds to the start of the next AG.

   b. Adjust the visited inode cursor to indicate that it has "visited" the
      last possible inode in the current AG's inode keyspace.
      XFS inumbers are segmented, so the cursor needs to be marked as having
      visited the entire keyspace up to just before the start of the next AG's
      inode keyspace.

   c. Unlock the AGI and return to step 1 if there are unexamined AGs in the
      filesystem.

   d. If there are no more AGs to examine, set both cursors to the end of the
      inumber keyspace.
      The scan is now complete.

4. Otherwise, there is at least one more inode to scan in this AG:

   a. Move the examination cursor ahead to the next inode marked as allocated
      by the inode btree.

   b. Adjust the visited inode cursor to point to the inode just prior to where
      the examination cursor is now.
      Because the scanner holds the AGI buffer lock, no inodes could have been
      created in the part of the inode keyspace that the visited inode cursor
      just advanced.

5. Get the incore inode for the inumber of the examination cursor.
   By maintaining the AGI buffer lock until this point, the scanner knows that
   it was safe to advance the examination cursor across the entire keyspace,
   and that it has stabilized this next inode so that it cannot disappear from
   the filesystem until the scan releases the incore inode.

6. Drop the AGI lock and return the incore inode to the caller.

Online fsck functions scan all files in the filesystem as follows:

1. Start a scan by calling ``xchk_iscan_start``.

2. Advance the scan cursor (``xchk_iscan_iter``) to get the next inode.
   If one is provided:

   a. Lock the inode to prevent updates during the scan.

   b. Scan the inode.

   c. While still holding the inode lock, adjust the visited inode cursor
      (``xchk_iscan_mark_visited``) to point to this inode.

   d. Unlock and release the inode.

8. Call ``xchk_iscan_teardown`` to complete the scan.

There are subtleties with the inode cache that complicate grabbing the incore
inode for the caller.
Obviously, it is an absolute requirement that the inode metadata be consistent
enough to load it into the inode cache.
Second, if the incore inode is stuck in some intermediate state, the scan
coordinator must release the AGI and push the main filesystem to get the inode
back into a loadable state.

The proposed patches are the
`inode scanner
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-iscan>`_
series.
The first user of the new functionality is the
`online quotacheck
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-quotacheck>`_
series.

Inode Management
````````````````

In regular filesystem code, references to allocated XFS incore inodes are
always obtained (``xfs_iget``) outside of transaction context because the
creation of the incore context for an existing file does not require metadata
updates.
However, it is important to note that references to incore inodes obtained as
part of file creation must be performed in transaction context because the
filesystem must ensure the atomicity of the ondisk inode btree index updates
and the initialization of the actual ondisk inode.

References to incore inodes are always released (``xfs_irele``) outside of
transaction context because there are a handful of activities that might
require ondisk updates:

- The VFS may decide to kick off writeback as part of a ``DONTCACHE`` inode
  release.

- Speculative preallocations need to be unreserved.

- An unlinked file may have lost its last reference, in which case the entire
  file must be inactivated, which involves releasing all of its resources in
  the ondisk metadata and freeing the inode.

These activities are collectively called inode inactivation.
Inactivation has two parts -- the VFS part, which initiates writeback on all
dirty file pages, and the XFS part, which cleans up XFS-specific information
and frees the inode if it was unlinked.
If the inode is unlinked (or unconnected after a file handle operation), the
kernel drops the inode into the inactivation machinery immediately.

During normal operation, resource acquisition for an update follows this order
to avoid deadlocks:

1. Inode reference (``iget``).

2. Filesystem freeze protection, if repairing (``mnt_want_write_file``).

3. Inode ``IOLOCK`` (VFS ``i_rwsem``) lock to control file IO.

4. Inode ``MMAPLOCK`` (page cache ``invalidate_lock``) lock for operations that
   can update page cache mappings.

5. Log feature enablement.

6. Transaction log space grant.

7. Space on the data and realtime devices for the transaction.

8. Incore dquot references, if a file is being repaired.
   Note that they are not locked, merely acquired.

9. Inode ``ILOCK`` for file metadata updates.

10. AG header buffer locks / Realtime metadata inode ILOCK.

11. Realtime metadata buffer locks, if applicable.

12. Extent mapping btree blocks, if applicable.

Resources are often released in the reverse order, though this is not required.
However, online fsck differs from regular XFS operations because it may examine
an object that normally is acquired in a later stage of the locking order, and
then decide to cross-reference the object with an object that is acquired
earlier in the order.
The next few sections detail the specific ways in which online fsck takes care
to avoid deadlocks.

iget and irele During a Scrub
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An inode scan performed on behalf of a scrub operation runs in transaction
context, and possibly with resources already locked and bound to it.
This isn't much of a problem for ``iget`` since it can operate in the context
of an existing transaction, as long as all of the bound resources are acquired
before the inode reference in the regular filesystem.

When the VFS ``iput`` function is given a linked inode with no other
references, it normally puts the inode on an LRU list in the hope that it can
save time if another process re-opens the file before the system runs out
of memory and frees it.
Filesystem callers can short-circuit the LRU process by setting a ``DONTCACHE``
flag on the inode to cause the kernel to try to drop the inode into the
inactivation machinery immediately.

In the past, inactivation was always done from the process that dropped the
inode, which was a problem for scrub because scrub may already hold a
transaction, and XFS does not support nesting transactions.
On the other hand, if there is no scrub transaction, it is desirable to drop
otherwise unused inodes immediately to avoid polluting caches.
To capture these nuances, the online fsck code has a separate ``xchk_irele``
function to set or clear the ``DONTCACHE`` flag to get the required release
behavior.

Proposed patchsets include fixing
`scrub iget usage
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-iget-fixes>`_ and
`dir iget usage
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-dir-iget-fixes>`_.

.. _ilocking:

Locking Inodes
^^^^^^^^^^^^^^

In regular filesystem code, the VFS and XFS will acquire multiple IOLOCK locks
in a well-known order: parent → child when updating the directory tree, and
in numerical order of the addresses of their ``struct inode`` object otherwise.
For regular files, the MMAPLOCK can be acquired after the IOLOCK to stop page
faults.
If two MMAPLOCKs must be acquired, they are acquired in numerical order of
the addresses of their ``struct address_space`` objects.
Due to the structure of existing filesystem code, IOLOCKs and MMAPLOCKs must be
acquired before transactions are allocated.
If two ILOCKs must be acquired, they are acquired in inumber order.

Inode lock acquisition must be done carefully during a coordinated inode scan.
Online fsck cannot abide these conventions, because for a directory tree
scanner, the scrub process holds the IOLOCK of the file being scanned and it
needs to take the IOLOCK of the file at the other end of the directory link.
If the directory tree is corrupt because it contains a cycle, ``xfs_scrub``
cannot use the regular inode locking functions and avoid becoming trapped in an
ABBA deadlock.

Solving both of these problems is straightforward -- any time online fsck
needs to take a second lock of the same class, it uses trylock to avoid an ABBA
deadlock.
If the trylock fails, scrub drops all inode locks and use trylock loops to
(re)acquire all necessary resources.
Trylock loops enable scrub to check for pending fatal signals, which is how
scrub avoids deadlocking the filesystem or becoming an unresponsive process.
However, trylock loops means that online fsck must be prepared to measure the
resource being scrubbed before and after the lock cycle to detect changes and
react accordingly.

.. _dirparent:

Case Study: Finding a Directory Parent
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Consider the directory parent pointer repair code as an example.
Online fsck must verify that the dotdot dirent of a directory points up to a
parent directory, and that the parent directory contains exactly one dirent
pointing down to the child directory.
Fully validating this relationship (and repairing it if possible) requires a
walk of every directory on the filesystem while holding the child locked, and
while updates to the directory tree are being made.
The coordinated inode scan provides a way to walk the filesystem without the
possibility of missing an inode.
The child directory is kept locked to prevent updates to the dotdot dirent, but
if the scanner fails to lock a parent, it can drop and relock both the child
and the prospective parent.
If the dotdot entry changes while the directory is unlocked, then a move or
rename operation must have changed the child's parentage, and the scan can
exit early.

The proposed patchset is the
`directory repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-dirs>`_
series.

.. _fshooks:

Filesystem Hooks
`````````````````

The second piece of support that online fsck functions need during a full
filesystem scan is the ability to stay informed about updates being made by
other threads in the filesystem, since comparisons against the past are useless
in a dynamic environment.
Two pieces of Linux kernel infrastructure enable online fsck to monitor regular
filesystem operations: filesystem hooks and :ref:`static keys<jump_labels>`.

Filesystem hooks convey information about an ongoing filesystem operation to
a downstream consumer.
In this case, the downstream consumer is always an online fsck function.
Because multiple fsck functions can run in parallel, online fsck uses the Linux
notifier call chain facility to dispatch updates to any number of interested
fsck processes.
Call chains are a dynamic list, which means that they can be configured at
run time.
Because these hooks are private to the XFS module, the information passed along
contains exactly what the checking function needs to update its observations.

The current implementation of XFS hooks uses SRCU notifier chains to reduce the
impact to highly threaded workloads.
Regular blocking notifier chains use a rwsem and seem to have a much lower
overhead for single-threaded applications.
However, it may turn out that the combination of blocking chains and static
keys are a more performant combination; more study is needed here.

The following pieces are necessary to hook a certain point in the filesystem:

- A ``struct xfs_hooks`` object must be embedded in a convenient place such as
  a well-known incore filesystem object.

- Each hook must define an action code and a structure containing more context
  about the action.

- Hook providers should provide appropriate wrapper functions and structs
  around the ``xfs_hooks`` and ``xfs_hook`` objects to take advantage of type
  checking to ensure correct usage.

- A callsite in the regular filesystem code must be chosen to call
  ``xfs_hooks_call`` with the action code and data structure.
  This place should be adjacent to (and not earlier than) the place where
  the filesystem update is committed to the transaction.
  In general, when the filesystem calls a hook chain, it should be able to
  handle sleeping and should not be vulnerable to memory reclaim or locking
  recursion.
  However, the exact requirements are very dependent on the context of the hook
  caller and the callee.

- The online fsck function should define a structure to hold scan data, a lock
  to coordinate access to the scan data, and a ``struct xfs_hook`` object.
  The scanner function and the regular filesystem code must acquire resources
  in the same order; see the next section for details.

- The online fsck code must contain a C function to catch the hook action code
  and data structure.
  If the object being updated has already been visited by the scan, then the
  hook information must be applied to the scan data.

- Prior to unlocking inodes to start the scan, online fsck must call
  ``xfs_hooks_setup`` to initialize the ``struct xfs_hook``, and
  ``xfs_hooks_add`` to enable the hook.

- Online fsck must call ``xfs_hooks_del`` to disable the hook once the scan is
  complete.

The number of hooks should be kept to a minimum to reduce complexity.
Static keys are used to reduce the overhead of filesystem hooks to nearly
zero when online fsck is not running.

.. _liveupdate:

Live Updates During a Scan
``````````````````````````

The code paths of the online fsck scanning code and the :ref:`hooked<fshooks>`
filesystem code look like this::

            other program
                  ↓
            inode lock ←────────────────────┐
                  ↓                         │
            AG header lock                  │
                  ↓                         │
            filesystem function             │
                  ↓                         │
            notifier call chain             │    same
                  ↓                         ├─── inode
            scrub hook function             │    lock
                  ↓                         │
            scan data mutex ←──┐    same    │
                  ↓            ├─── scan    │
            update scan data   │    lock    │
                  ↑            │            │
            scan data mutex ←──┘            │
                  ↑                         │
            inode lock ←────────────────────┘
                  ↑
            scrub function
                  ↑
            inode scanner
                  ↑
            xfs_scrub

These rules must be followed to ensure correct interactions between the
checking code and the code making an update to the filesystem:

- Prior to invoking the notifier call chain, the filesystem function being
  hooked must acquire the same lock that the scrub scanning function acquires
  to scan the inode.

- The scanning function and the scrub hook function must coordinate access to
  the scan data by acquiring a lock on the scan data.

- Scrub hook function must not add the live update information to the scan
  observations unless the inode being updated has already been scanned.
  The scan coordinator has a helper predicate (``xchk_iscan_want_live_update``)
  for this.

- Scrub hook functions must not change the caller's state, including the
  transaction that it is running.
  They must not acquire any resources that might conflict with the filesystem
  function being hooked.

- The hook function can abort the inode scan to avoid breaking the other rules.

The inode scan APIs are pretty simple:

- ``xchk_iscan_start`` starts a scan

- ``xchk_iscan_iter`` grabs a reference to the next inode in the scan or
  returns zero if there is nothing left to scan

- ``xchk_iscan_want_live_update`` to decide if an inode has already been
  visited in the scan.
  This is critical for hook functions to decide if they need to update the
  in-memory scan information.

- ``xchk_iscan_mark_visited`` to mark an inode as having been visited in the
  scan

- ``xchk_iscan_teardown`` to finish the scan

This functionality is also a part of the
`inode scanner
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-iscan>`_
series.

.. _quotacheck:

Case Study: Quota Counter Checking
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is useful to compare the mount time quotacheck code to the online repair
quotacheck code.
Mount time quotacheck does not have to contend with concurrent operations, so
it does the following:

1. Make sure the ondisk dquots are in good enough shape that all the incore
   dquots will actually load, and zero the resource usage counters in the
   ondisk buffer.

2. Walk every inode in the filesystem.
   Add each file's resource usage to the incore dquot.

3. Walk each incore dquot.
   If the incore dquot is not being flushed, add the ondisk buffer backing the
   incore dquot to a delayed write (delwri) list.

4. Write the buffer list to disk.

Like most online fsck functions, online quotacheck can't write to regular
filesystem objects until the newly collected metadata reflect all filesystem
state.
Therefore, online quotacheck records file resource usage to a shadow dquot
index implemented with a sparse ``xfarray``, and only writes to the real dquots
once the scan is complete.
Handling transactional updates is tricky because quota resource usage updates
are handled in phases to minimize contention on dquots:

1. The inodes involved are joined and locked to a transaction.

2. For each dquot attached to the file:

   a. The dquot is locked.

   b. A quota reservation is added to the dquot's resource usage.
      The reservation is recorded in the transaction.

   c. The dquot is unlocked.

3. Changes in actual quota usage are tracked in the transaction.

4. At transaction commit time, each dquot is examined again:

   a. The dquot is locked again.

   b. Quota usage changes are logged and unused reservation is given back to
      the dquot.

   c. The dquot is unlocked.

For online quotacheck, hooks are placed in steps 2 and 4.
The step 2 hook creates a shadow version of the transaction dquot context
(``dqtrx``) that operates in a similar manner to the regular code.
The step 4 hook commits the shadow ``dqtrx`` changes to the shadow dquots.
Notice that both hooks are called with the inode locked, which is how the
live update coordinates with the inode scanner.

The quotacheck scan looks like this:

1. Set up a coordinated inode scan.

2. For each inode returned by the inode scan iterator:

   a. Grab and lock the inode.

   b. Determine that inode's resource usage (data blocks, inode counts,
      realtime blocks) and add that to the shadow dquots for the user, group,
      and project ids associated with the inode.

   c. Unlock and release the inode.

3. For each dquot in the system:

   a. Grab and lock the dquot.

   b. Check the dquot against the shadow dquots created by the scan and updated
      by the live hooks.

Live updates are key to being able to walk every quota record without
needing to hold any locks for a long duration.
If repairs are desired, the real and shadow dquots are locked and their
resource counts are set to the values in the shadow dquot.

The proposed patchset is the
`online quotacheck
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-quotacheck>`_
series.

.. _nlinks:

Case Study: File Link Count Checking
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

File link count checking also uses live update hooks.
The coordinated inode scanner is used to visit all directories on the
filesystem, and per-file link count records are stored in a sparse ``xfarray``
indexed by inumber.
During the scanning phase, each entry in a directory generates observation
data as follows:

1. If the entry is a dotdot (``'..'``) entry of the root directory, the
   directory's parent link count is bumped because the root directory's dotdot
   entry is self referential.

2. If the entry is a dotdot entry of a subdirectory, the parent's backref
   count is bumped.

3. If the entry is neither a dot nor a dotdot entry, the target file's parent
   count is bumped.

4. If the target is a subdirectory, the parent's child link count is bumped.

A crucial point to understand about how the link count inode scanner interacts
with the live update hooks is that the scan cursor tracks which *parent*
directories have been scanned.
In other words, the live updates ignore any update about ``A → B`` when A has
not been scanned, even if B has been scanned.
Furthermore, a subdirectory A with a dotdot entry pointing back to B is
accounted as a backref counter in the shadow data for A, since child dotdot
entries affect the parent's link count.
Live update hooks are carefully placed in all parts of the filesystem that
create, change, or remove directory entries, since those operations involve
bumplink and droplink.

For any file, the correct link count is the number of parents plus the number
of child subdirectories.
Non-directories never have children of any kind.
The backref information is used to detect inconsistencies in the number of
links pointing to child subdirectories and the number of dotdot entries
pointing back.

After the scan completes, the link count of each file can be checked by locking
both the inode and the shadow data, and comparing the link counts.
A second coordinated inode scan cursor is used for comparisons.
Live updates are key to being able to walk every inode without needing to hold
any locks between inodes.
If repairs are desired, the inode's link count is set to the value in the
shadow information.
If no parents are found, the file must be :ref:`reparented <orphanage>` to the
orphanage to prevent the file from being lost forever.

The proposed patchset is the
`file link count repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=scrub-nlinks>`_
series.

.. _rmap_repair:

Case Study: Rebuilding Reverse Mapping Records
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Most repair functions follow the same pattern: lock filesystem resources,
walk the surviving ondisk metadata looking for replacement metadata records,
and use an :ref:`in-memory array <xfarray>` to store the gathered observations.
The primary advantage of this approach is the simplicity and modularity of the
repair code -- code and data are entirely contained within the scrub module,
do not require hooks in the main filesystem, and are usually the most efficient
in memory use.
A secondary advantage of this repair approach is atomicity -- once the kernel
decides a structure is corrupt, no other threads can access the metadata until
the kernel finishes repairing and revalidating the metadata.

For repairs going on within a shard of the filesystem, these advantages
outweigh the delays inherent in locking the shard while repairing parts of the
shard.
Unfortunately, repairs to the reverse mapping btree cannot use the "standard"
btree repair strategy because it must scan every space mapping of every fork of
every file in the filesystem, and the filesystem cannot stop.
Therefore, rmap repair foregoes atomicity between scrub and repair.
It combines a :ref:`coordinated inode scanner <iscan>`, :ref:`live update hooks
<liveupdate>`, and an :ref:`in-memory rmap btree <xfbtree>` to complete the
scan for reverse mapping records.

1. Set up an xfbtree to stage rmap records.

2. While holding the locks on the AGI and AGF buffers acquired during the
   scrub, generate reverse mappings for all AG metadata: inodes, btrees, CoW
   staging extents, and the internal log.

3. Set up an inode scanner.

4. Hook into rmap updates for the AG being repaired so that the live scan data
   can receive updates to the rmap btree from the rest of the filesystem during
   the file scan.

5. For each space mapping found in either fork of each file scanned,
   decide if the mapping matches the AG of interest.
   If so:

   a. Create a btree cursor for the in-memory btree.

   b. Use the rmap code to add the record to the in-memory btree.

   c. Use the :ref:`special commit function <xfbtree_commit>` to write the
      xfbtree changes to the xfile.

6. For each live update received via the hook, decide if the owner has already
   been scanned.
   If so, apply the live update into the scan data:

   a. Create a btree cursor for the in-memory btree.

   b. Replay the operation into the in-memory btree.

   c. Use the :ref:`special commit function <xfbtree_commit>` to write the
      xfbtree changes to the xfile.
      This is performed with an empty transaction to avoid changing the
      caller's state.

7. When the inode scan finishes, create a new scrub transaction and relock the
   two AG headers.

8. Compute the new btree geometry using the number of rmap records in the
   shadow btree, like all other btree rebuilding functions.

9. Allocate the number of blocks computed in the previous step.

10. Perform the usual btree bulk loading and commit to install the new rmap
    btree.

11. Reap the old rmap btree blocks as discussed in the case study about how
    to :ref:`reap after rmap btree repair <rmap_reap>`.

12. Free the xfbtree now that it not needed.

The proposed patchset is the
`rmap repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-rmap-btree>`_
series.

Staging Repairs with Temporary Files on Disk
--------------------------------------------

XFS stores a substantial amount of metadata in file forks: directories,
extended attributes, symbolic link targets, free space bitmaps and summary
information for the realtime volume, and quota records.
File forks map 64-bit logical file fork space extents to physical storage space
extents, similar to how a memory management unit maps 64-bit virtual addresses
to physical memory addresses.
Therefore, file-based tree structures (such as directories and extended
attributes) use blocks mapped in the file fork offset address space that point
to other blocks mapped within that same address space, and file-based linear
structures (such as bitmaps and quota records) compute array element offsets in
the file fork offset address space.

Because file forks can consume as much space as the entire filesystem, repairs
cannot be staged in memory, even when a paging scheme is available.
Therefore, online repair of file-based metadata createas a temporary file in
the XFS filesystem, writes a new structure at the correct offsets into the
temporary file, and atomically swaps the fork mappings (and hence the fork
contents) to commit the repair.
Once the repair is complete, the old fork can be reaped as necessary; if the
system goes down during the reap, the iunlink code will delete the blocks
during log recovery.

**Note**: All space usage and inode indices in the filesystem *must* be
consistent to use a temporary file safely!
This dependency is the reason why online repair can only use pageable kernel
memory to stage ondisk space usage information.

Swapping metadata extents with a temporary file requires the owner field of the
block headers to match the file being repaired and not the temporary file.  The
directory, extended attribute, and symbolic link functions were all modified to
allow callers to specify owner numbers explicitly.

There is a downside to the reaping process -- if the system crashes during the
reap phase and the fork extents are crosslinked, the iunlink processing will
fail because freeing space will find the extra reverse mappings and abort.

Temporary files created for repair are similar to ``O_TMPFILE`` files created
by userspace.
They are not linked into a directory and the entire file will be reaped when
the last reference to the file is lost.
The key differences are that these files must have no access permission outside
the kernel at all, they must be specially marked to prevent them from being
opened by handle, and they must never be linked into the directory tree.

+--------------------------------------------------------------------------+
| **Historical Sidebar**:                                                  |
+--------------------------------------------------------------------------+
| In the initial iteration of file metadata repair, the damaged metadata   |
| blocks would be scanned for salvageable data; the extents in the file    |
| fork would be reaped; and then a new structure would be built in its     |
| place.                                                                   |
| This strategy did not survive the introduction of the atomic repair      |
| requirement expressed earlier in this document.                          |
|                                                                          |
| The second iteration explored building a second structure at a high      |
| offset in the fork from the salvage data, reaping the old extents, and   |
| using a ``COLLAPSE_RANGE`` operation to slide the new extents into       |
| place.                                                                   |
|                                                                          |
| This had many drawbacks:                                                 |
|                                                                          |
| - Array structures are linearly addressed, and the regular filesystem    |
|   codebase does not have the concept of a linear offset that could be    |
|   applied to the record offset computation to build an alternate copy.   |
|                                                                          |
| - Extended attributes are allowed to use the entire attr fork offset     |
|   address space.                                                         |
|                                                                          |
| - Even if repair could build an alternate copy of a data structure in a  |
|   different part of the fork address space, the atomic repair commit     |
|   requirement means that online repair would have to be able to perform  |
|   a log assisted ``COLLAPSE_RANGE`` operation to ensure that the old     |
|   structure was completely replaced.                                     |
|                                                                          |
| - A crash after construction of the secondary tree but before the range  |
|   collapse would leave unreachable blocks in the file fork.              |
|   This would likely confuse things further.                              |
|                                                                          |
| - Reaping blocks after a repair is not a simple operation, and           |
|   initiating a reap operation from a restarted range collapse operation  |
|   during log recovery is daunting.                                       |
|                                                                          |
| - Directory entry blocks and quota records record the file fork offset   |
|   in the header area of each block.                                      |
|   An atomic range collapse operation would have to rewrite this part of  |
|   each block header.                                                     |
|   Rewriting a single field in block headers is not a huge problem, but   |
|   it's something to be aware of.                                         |
|                                                                          |
| - Each block in a directory or extended attributes btree index contains  |
|   sibling and child block pointers.                                      |
|   Were the atomic commit to use a range collapse operation, each block   |
|   would have to be rewritten very carefully to preserve the graph        |
|   structure.                                                             |
|   Doing this as part of a range collapse means rewriting a large number  |
|   of blocks repeatedly, which is not conducive to quick repairs.         |
|                                                                          |
| This lead to the introduction of temporary file staging.                 |
+--------------------------------------------------------------------------+

Using a Temporary File
``````````````````````

Online repair code should use the ``xrep_tempfile_create`` function to create a
temporary file inside the filesystem.
This allocates an inode, marks the in-core inode private, and attaches it to
the scrub context.
These files are hidden from userspace, may not be added to the directory tree,
and must be kept private.

Temporary files only use two inode locks: the IOLOCK and the ILOCK.
The MMAPLOCK is not needed here, because there must not be page faults from
userspace for data fork blocks.
The usage patterns of these two locks are the same as for any other XFS file --
access to file data are controlled via the IOLOCK, and access to file metadata
are controlled via the ILOCK.
Locking helpers are provided so that the temporary file and its lock state can
be cleaned up by the scrub context.
To comply with the nested locking strategy laid out in the :ref:`inode
locking<ilocking>` section, it is recommended that scrub functions use the
xrep_tempfile_ilock*_nowait lock helpers.

Data can be written to a temporary file by two means:

1. ``xrep_tempfile_copyin`` can be used to set the contents of a regular
   temporary file from an xfile.

2. The regular directory, symbolic link, and extended attribute functions can
   be used to write to the temporary file.

Once a good copy of a data file has been constructed in a temporary file, it
must be conveyed to the file being repaired, which is the topic of the next
section.

The proposed patches are in the
`repair temporary files
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-tempfiles>`_
series.

Atomic Extent Swapping
----------------------

Once repair builds a temporary file with a new data structure written into
it, it must commit the new changes into the existing file.
It is not possible to swap the inumbers of two files, so instead the new
metadata must replace the old.
This suggests the need for the ability to swap extents, but the existing extent
swapping code used by the file defragmenting tool ``xfs_fsr`` is not sufficient
for online repair because:

a. When the reverse-mapping btree is enabled, the swap code must keep the
   reverse mapping information up to date with every exchange of mappings.
   Therefore, it can only exchange one mapping per transaction, and each
   transaction is independent.

b. Reverse-mapping is critical for the operation of online fsck, so the old
   defragmentation code (which swapped entire extent forks in a single
   operation) is not useful here.

c. Defragmentation is assumed to occur between two files with identical
   contents.
   For this use case, an incomplete exchange will not result in a user-visible
   change in file contents, even if the operation is interrupted.

d. Online repair needs to swap the contents of two files that are by definition
   *not* identical.
   For directory and xattr repairs, the user-visible contents might be the
   same, but the contents of individual blocks may be very different.

e. Old blocks in the file may be cross-linked with another structure and must
   not reappear if the system goes down mid-repair.

These problems are overcome by creating a new deferred operation and a new type
of log intent item to track the progress of an operation to exchange two file
ranges.
The new deferred operation type chains together the same transactions used by
the reverse-mapping extent swap code.
The new log item records the progress of the exchange to ensure that once an
exchange begins, it will always run to completion, even there are
interruptions.
The new ``XFS_SB_FEAT_INCOMPAT_LOG_ATOMIC_SWAP`` log-incompatible feature flag
in the superblock protects these new log item records from being replayed on
old kernels.

The proposed patchset is the
`atomic extent swap
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=atomic-file-updates>`_
series.

+--------------------------------------------------------------------------+
| **Sidebar: Using Log-Incompatible Feature Flags**                        |
+--------------------------------------------------------------------------+
| Starting with XFS v5, the superblock contains a                          |
| ``sb_features_log_incompat`` field to indicate that the log contains     |
| records that might not readable by all kernels that could mount this     |
| filesystem.                                                              |
| In short, log incompat features protect the log contents against kernels |
| that will not understand the contents.                                   |
| Unlike the other superblock feature bits, log incompat bits are          |
| ephemeral because an empty (clean) log does not need protection.         |
| The log cleans itself after its contents have been committed into the    |
| filesystem, either as part of an unmount or because the system is        |
| otherwise idle.                                                          |
| Because upper level code can be working on a transaction at the same     |
| time that the log cleans itself, it is necessary for upper level code to |
| communicate to the log when it is going to use a log incompatible        |
| feature.                                                                 |
|                                                                          |
| The log coordinates access to incompatible features through the use of   |
| one ``struct rw_semaphore`` for each feature.                            |
| The log cleaning code tries to take this rwsem in exclusive mode to      |
| clear the bit; if the lock attempt fails, the feature bit remains set.   |
| Filesystem code signals its intention to use a log incompat feature in a |
| transaction by calling ``xlog_use_incompat_feat``, which takes the rwsem |
| in shared mode.                                                          |
| The code supporting a log incompat feature should create wrapper         |
| functions to obtain the log feature and call                             |
| ``xfs_add_incompat_log_feature`` to set the feature bits in the primary  |
| superblock.                                                              |
| The superblock update is performed transactionally, so the wrapper to    |
| obtain log assistance must be called just prior to the creation of the   |
| transaction that uses the functionality.                                 |
| For a file operation, this step must happen after taking the IOLOCK      |
| and the MMAPLOCK, but before allocating the transaction.                 |
| When the transaction is complete, the ``xlog_drop_incompat_feat``        |
| function is called to release the feature.                               |
| The feature bit will not be cleared from the superblock until the log    |
| becomes clean.                                                           |
|                                                                          |
| Log-assisted extended attribute updates and atomic extent swaps both use |
| log incompat features and provide convenience wrappers around the        |
| functionality.                                                           |
+--------------------------------------------------------------------------+

Mechanics of an Atomic Extent Swap
``````````````````````````````````

Swapping entire file forks is a complex task.
The goal is to exchange all file fork mappings between two file fork offset
ranges.
There are likely to be many extent mappings in each fork, and the edges of
the mappings aren't necessarily aligned.
Furthermore, there may be other updates that need to happen after the swap,
such as exchanging file sizes, inode flags, or conversion of fork data to local
format.
This is roughly the format of the new deferred extent swap work item:

.. code-block:: c

	struct xfs_swapext_intent {
	    /* Inodes participating in the operation. */
	    struct xfs_inode    *sxi_ip1;
	    struct xfs_inode    *sxi_ip2;

	    /* File offset range information. */
	    xfs_fileoff_t       sxi_startoff1;
	    xfs_fileoff_t       sxi_startoff2;
	    xfs_filblks_t       sxi_blockcount;

	    /* Set these file sizes after the operation, unless negative. */
	    xfs_fsize_t         sxi_isize1;
	    xfs_fsize_t         sxi_isize2;

	    /* XFS_SWAP_EXT_* log operation flags */
	    uint64_t            sxi_flags;
	};

The new log intent item contains enough information to track two logical fork
offset ranges: ``(inode1, startoff1, blockcount)`` and ``(inode2, startoff2,
blockcount)``.
Each step of a swap operation exchanges the largest file range mapping possible
from one file to the other.
After each step in the swap operation, the two startoff fields are incremented
and the blockcount field is decremented to reflect the progress made.
The flags field captures behavioral parameters such as swapping the attr fork
instead of the data fork and other work to be done after the extent swap.
The two isize fields are used to swap the file size at the end of the operation
if the file data fork is the target of the swap operation.

When the extent swap is initiated, the sequence of operations is as follows:

1. Create a deferred work item for the extent swap.
   At the start, it should contain the entirety of the file ranges to be
   swapped.

2. Call ``xfs_defer_finish`` to process the exchange.
   This is encapsulated in ``xrep_tempswap_contents`` for scrub operations.
   This will log an extent swap intent item to the transaction for the deferred
   extent swap work item.

3. Until ``sxi_blockcount`` of the deferred extent swap work item is zero,

   a. Read the block maps of both file ranges starting at ``sxi_startoff1`` and
      ``sxi_startoff2``, respectively, and compute the longest extent that can
      be swapped in a single step.
      This is the minimum of the two ``br_blockcount`` s in the mappings.
      Keep advancing through the file forks until at least one of the mappings
      contains written blocks.
      Mutual holes, unwritten extents, and extent mappings to the same physical
      space are not exchanged.

      For the next few steps, this document will refer to the mapping that came
      from file 1 as "map1", and the mapping that came from file 2 as "map2".

   b. Create a deferred block mapping update to unmap map1 from file 1.

   c. Create a deferred block mapping update to unmap map2 from file 2.

   d. Create a deferred block mapping update to map map1 into file 2.

   e. Create a deferred block mapping update to map map2 into file 1.

   f. Log the block, quota, and extent count updates for both files.

   g. Extend the ondisk size of either file if necessary.

   h. Log an extent swap done log item for the extent swap intent log item
      that was read at the start of step 3.

   i. Compute the amount of file range that has just been covered.
      This quantity is ``(map1.br_startoff + map1.br_blockcount -
      sxi_startoff1)``, because step 3a could have skipped holes.

   j. Increase the starting offsets of ``sxi_startoff1`` and ``sxi_startoff2``
      by the number of blocks computed in the previous step, and decrease
      ``sxi_blockcount`` by the same quantity.
      This advances the cursor.

   k. Log a new extent swap intent log item reflecting the advanced state of
      the work item.

   l. Return the proper error code (EAGAIN) to the deferred operation manager
      to inform it that there is more work to be done.
      The operation manager completes the deferred work in steps 3b-3e before
      moving back to the start of step 3.

4. Perform any post-processing.
   This will be discussed in more detail in subsequent sections.

If the filesystem goes down in the middle of an operation, log recovery will
find the most recent unfinished extent swap log intent item and restart from
there.
This is how extent swapping guarantees that an outside observer will either see
the old broken structure or the new one, and never a mismash of both.

Preparation for Extent Swapping
```````````````````````````````

There are a few things that need to be taken care of before initiating an
atomic extent swap operation.
First, regular files require the page cache to be flushed to disk before the
operation begins, and directio writes to be quiesced.
Like any filesystem operation, extent swapping must determine the maximum
amount of disk space and quota that can be consumed on behalf of both files in
the operation, and reserve that quantity of resources to avoid an unrecoverable
out of space failure once it starts dirtying metadata.
The preparation step scans the ranges of both files to estimate:

- Data device blocks needed to handle the repeated updates to the fork
  mappings.
- Change in data and realtime block counts for both files.
- Increase in quota usage for both files, if the two files do not share the
  same set of quota ids.
- The number of extent mappings that will be added to each file.
- Whether or not there are partially written realtime extents.
  User programs must never be able to access a realtime file extent that maps
  to different extents on the realtime volume, which could happen if the
  operation fails to run to completion.

The need for precise estimation increases the run time of the swap operation,
but it is very important to maintain correct accounting.
The filesystem must not run completely out of free space, nor can the extent
swap ever add more extent mappings to a fork than it can support.
Regular users are required to abide the quota limits, though metadata repairs
may exceed quota to resolve inconsistent metadata elsewhere.

Special Features for Swapping Metadata File Extents
```````````````````````````````````````````````````

Extended attributes, symbolic links, and directories can set the fork format to
"local" and treat the fork as a literal area for data storage.
Metadata repairs must take extra steps to support these cases:

- If both forks are in local format and the fork areas are large enough, the
  swap is performed by copying the incore fork contents, logging both forks,
  and committing.
  The atomic extent swap mechanism is not necessary, since this can be done
  with a single transaction.

- If both forks map blocks, then the regular atomic extent swap is used.

- Otherwise, only one fork is in local format.
  The contents of the local format fork are converted to a block to perform the
  swap.
  The conversion to block format must be done in the same transaction that
  logs the initial extent swap intent log item.
  The regular atomic extent swap is used to exchange the mappings.
  Special flags are set on the swap operation so that the transaction can be
  rolled one more time to convert the second file's fork back to local format
  so that the second file will be ready to go as soon as the ILOCK is dropped.

Extended attributes and directories stamp the owning inode into every block,
but the buffer verifiers do not actually check the inode number!
Although there is no verification, it is still important to maintain
referential integrity, so prior to performing the extent swap, online repair
builds every block in the new data structure with the owner field of the file
being repaired.

After a successful swap operation, the repair operation must reap the old fork
blocks by processing each fork mapping through the standard :ref:`file extent
reaping <reaping>` mechanism that is done post-repair.
If the filesystem should go down during the reap part of the repair, the
iunlink processing at the end of recovery will free both the temporary file and
whatever blocks were not reaped.
However, this iunlink processing omits the cross-link detection of online
repair, and is not completely foolproof.

Swapping Temporary File Extents
```````````````````````````````

To repair a metadata file, online repair proceeds as follows:

1. Create a temporary repair file.

2. Use the staging data to write out new contents into the temporary repair
   file.
   The same fork must be written to as is being repaired.

3. Commit the scrub transaction, since the swap estimation step must be
   completed before transaction reservations are made.

4. Call ``xrep_tempswap_trans_alloc`` to allocate a new scrub transaction with
   the appropriate resource reservations, locks, and fill out a ``struct
   xfs_swapext_req`` with the details of the swap operation.

5. Call ``xrep_tempswap_contents`` to swap the contents.

6. Commit the transaction to complete the repair.

.. _rtsummary:

Case Study: Repairing the Realtime Summary File
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In the "realtime" section of an XFS filesystem, free space is tracked via a
bitmap, similar to Unix FFS.
Each bit in the bitmap represents one realtime extent, which is a multiple of
the filesystem block size between 4KiB and 1GiB in size.
The realtime summary file indexes the number of free extents of a given size to
the offset of the block within the realtime free space bitmap where those free
extents begin.
In other words, the summary file helps the allocator find free extents by
length, similar to what the free space by count (cntbt) btree does for the data
section.

The summary file itself is a flat file (with no block headers or checksums!)
partitioned into ``log2(total rt extents)`` sections containing enough 32-bit
counters to match the number of blocks in the rt bitmap.
Each counter records the number of free extents that start in that bitmap block
and can satisfy a power-of-two allocation request.

To check the summary file against the bitmap:

1. Take the ILOCK of both the realtime bitmap and summary files.

2. For each free space extent recorded in the bitmap:

   a. Compute the position in the summary file that contains a counter that
      represents this free extent.

   b. Read the counter from the xfile.

   c. Increment it, and write it back to the xfile.

3. Compare the contents of the xfile against the ondisk file.

To repair the summary file, write the xfile contents into the temporary file
and use atomic extent swap to commit the new contents.
The temporary file is then reaped.

The proposed patchset is the
`realtime summary repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-rtsummary>`_
series.

Case Study: Salvaging Extended Attributes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In XFS, extended attributes are implemented as a namespaced name-value store.
Values are limited in size to 64KiB, but there is no limit in the number of
names.
The attribute fork is unpartitioned, which means that the root of the attribute
structure is always in logical block zero, but attribute leaf blocks, dabtree
index blocks, and remote value blocks are intermixed.
Attribute leaf blocks contain variable-sized records that associate
user-provided names with the user-provided values.
Values larger than a block are allocated separate extents and written there.
If the leaf information expands beyond a single block, a directory/attribute
btree (``dabtree``) is created to map hashes of attribute names to entries
for fast lookup.

Salvaging extended attributes is done as follows:

1. Walk the attr fork mappings of the file being repaired to find the attribute
   leaf blocks.
   When one is found,

   a. Walk the attr leaf block to find candidate keys.
      When one is found,

      1. Check the name for problems, and ignore the name if there are.

      2. Retrieve the value.
         If that succeeds, add the name and value to the staging xfarray and
         xfblob.

2. If the memory usage of the xfarray and xfblob exceed a certain amount of
   memory or there are no more attr fork blocks to examine, unlock the file and
   add the staged extended attributes to the temporary file.

3. Use atomic extent swapping to exchange the new and old extended attribute
   structures.
   The old attribute blocks are now attached to the temporary file.

4. Reap the temporary file.

The proposed patchset is the
`extended attribute repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-xattrs>`_
series.

Fixing Directories
------------------

Fixing directories is difficult with currently available filesystem features,
since directory entries are not redundant.
The offline repair tool scans all inodes to find files with nonzero link count,
and then it scans all directories to establish parentage of those linked files.
Damaged files and directories are zapped, and files with no parent are
moved to the ``/lost+found`` directory.
It does not try to salvage anything.

The best that online repair can do at this time is to read directory data
blocks and salvage any dirents that look plausible, correct link counts, and
move orphans back into the directory tree.
The salvage process is discussed in the case study at the end of this section.
The :ref:`file link count fsck <nlinks>` code takes care of fixing link counts
and moving orphans to the ``/lost+found`` directory.

Case Study: Salvaging Directories
`````````````````````````````````

Unlike extended attributes, directory blocks are all the same size, so
salvaging directories is straightforward:

1. Find the parent of the directory.
   If the dotdot entry is not unreadable, try to confirm that the alleged
   parent has a child entry pointing back to the directory being repaired.
   Otherwise, walk the filesystem to find it.

2. Walk the first partition of data fork of the directory to find the directory
   entry data blocks.
   When one is found,

   a. Walk the directory data block to find candidate entries.
      When an entry is found:

      i. Check the name for problems, and ignore the name if there are.

      ii. Retrieve the inumber and grab the inode.
          If that succeeds, add the name, inode number, and file type to the
          staging xfarray and xblob.

3. If the memory usage of the xfarray and xfblob exceed a certain amount of
   memory or there are no more directory data blocks to examine, unlock the
   directory and add the staged dirents into the temporary directory.
   Truncate the staging files.

4. Use atomic extent swapping to exchange the new and old directory structures.
   The old directory blocks are now attached to the temporary file.

5. Reap the temporary file.

**Future Work Question**: Should repair revalidate the dentry cache when
rebuilding a directory?

*Answer*: Yes, it should.

In theory it is necessary to scan all dentry cache entries for a directory to
ensure that one of the following apply:

1. The cached dentry reflects an ondisk dirent in the new directory.

2. The cached dentry no longer has a corresponding ondisk dirent in the new
   directory and the dentry can be purged from the cache.

3. The cached dentry no longer has an ondisk dirent but the dentry cannot be
   purged.
   This is the problem case.

Unfortunately, the current dentry cache design doesn't provide a means to walk
every child dentry of a specific directory, which makes this a hard problem.
There is no known solution.

The proposed patchset is the
`directory repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-dirs>`_
series.

Parent Pointers
```````````````

A parent pointer is a piece of file metadata that enables a user to locate the
file's parent directory without having to traverse the directory tree from the
root.
Without them, reconstruction of directory trees is hindered in much the same
way that the historic lack of reverse space mapping information once hindered
reconstruction of filesystem space metadata.
The parent pointer feature, however, makes total directory reconstruction
possible.

XFS parent pointers include the dirent name and location of the entry within
the parent directory.
In other words, child files use extended attributes to store pointers to
parents in the form ``(parent_inum, parent_gen, dirent_pos) → (dirent_name)``.
The directory checking process can be strengthened to ensure that the target of
each dirent also contains a parent pointer pointing back to the dirent.
Likewise, each parent pointer can be checked by ensuring that the target of
each parent pointer is a directory and that it contains a dirent matching
the parent pointer.
Both online and offline repair can use this strategy.

**Note**: The ondisk format of parent pointers is not yet finalized.

+--------------------------------------------------------------------------+
| **Historical Sidebar**:                                                  |
+--------------------------------------------------------------------------+
| Directory parent pointers were first proposed as an XFS feature more     |
| than a decade ago by SGI.                                                |
| Each link from a parent directory to a child file is mirrored with an    |
| extended attribute in the child that could be used to identify the       |
| parent directory.                                                        |
| Unfortunately, this early implementation had major shortcomings and was  |
| never merged into Linux XFS:                                             |
|                                                                          |
| 1. The XFS codebase of the late 2000s did not have the infrastructure to |
|    enforce strong referential integrity in the directory tree.           |
|    It did not guarantee that a change in a forward link would always be  |
|    followed up with the corresponding change to the reverse links.       |
|                                                                          |
| 2. Referential integrity was not integrated into offline repair.         |
|    Checking and repairs were performed on mounted filesystems without    |
|    taking any kernel or inode locks to coordinate access.                |
|    It is not clear how this actually worked properly.                    |
|                                                                          |
| 3. The extended attribute did not record the name of the directory entry |
|    in the parent, so the SGI parent pointer implementation cannot be     |
|    used to reconnect the directory tree.                                 |
|                                                                          |
| 4. Extended attribute forks only support 65,536 extents, which means     |
|    that parent pointer attribute creation is likely to fail at some      |
|    point before the maximum file link count is achieved.                 |
|                                                                          |
| The original parent pointer design was too unstable for something like   |
| a file system repair to depend on.                                       |
| Allison Henderson, Chandan Babu, and Catherine Hoang are working on a    |
| second implementation that solves all shortcomings of the first.         |
| During 2022, Allison introduced log intent items to track physical       |
| manipulations of the extended attribute structures.                      |
| This solves the referential integrity problem by making it possible to   |
| commit a dirent update and a parent pointer update in the same           |
| transaction.                                                             |
| Chandan increased the maximum extent counts of both data and attribute   |
| forks, thereby ensuring that the extended attribute structure can grow   |
| to handle the maximum hardlink count of any file.                        |
+--------------------------------------------------------------------------+

Case Study: Repairing Directories with Parent Pointers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Directory rebuilding uses a :ref:`coordinated inode scan <iscan>` and
a :ref:`directory entry live update hook <liveupdate>` as follows:

1. Set up a temporary directory for generating the new directory structure,
   an xfblob for storing entry names, and an xfarray for stashing directory
   updates.

2. Set up an inode scanner and hook into the directory entry code to receive
   updates on directory operations.

3. For each parent pointer found in each file scanned, decide if the parent
   pointer references the directory of interest.
   If so:

   a. Stash an addname entry for this dirent in the xfarray for later.

   b. When finished scanning that file, flush the stashed updates to the
      temporary directory.

4. For each live directory update received via the hook, decide if the child
   has already been scanned.
   If so:

   a. Stash an addname or removename entry for this dirent update in the
      xfarray for later.
      We cannot write directly to the temporary directory because hook
      functions are not allowed to modify filesystem metadata.
      Instead, we stash updates in the xfarray and rely on the scanner thread
      to apply the stashed updates to the temporary directory.

5. When the scan is complete, atomically swap the contents of the temporary
   directory and the directory being repaired.
   The temporary directory now contains the damaged directory structure.

6. Reap the temporary directory.

7. Update the dirent position field of parent pointers as necessary.
   This may require the queuing of a substantial number of xattr log intent
   items.

The proposed patchset is the
`parent pointers directory repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=pptrs-online-dir-repair>`_
series.

**Unresolved Question**: How will repair ensure that the ``dirent_pos`` fields
match in the reconstructed directory?

*Answer*: There are a few ways to solve this problem:

1. The field could be designated advisory, since the other three values are
   sufficient to find the entry in the parent.
   However, this makes indexed key lookup impossible while repairs are ongoing.

2. We could allow creating directory entries at specified offsets, which solves
   the referential integrity problem but runs the risk that dirent creation
   will fail due to conflicts with the free space in the directory.

   These conflicts could be resolved by appending the directory entry and
   amending the xattr code to support updating an xattr key and reindexing the
   dabtree, though this would have to be performed with the parent directory
   still locked.

3. Same as above, but remove the old parent pointer entry and add a new one
   atomically.

4. Change the ondisk xattr format to ``(parent_inum, name) → (parent_gen)``,
   which would provide the attr name uniqueness that we require, without
   forcing repair code to update the dirent position.
   Unfortunately, this requires changes to the xattr code to support attr
   names as long as 263 bytes.

5. Change the ondisk xattr format to ``(parent_inum, hash(name)) →
   (name, parent_gen)``.
   If the hash is sufficiently resistant to collisions (e.g. sha256) then
   this should provide the attr name uniqueness that we require.
   Names shorter than 247 bytes could be stored directly.

Discussion is ongoing under the `parent pointers patch deluge
<https://www.spinics.net/lists/linux-xfs/msg69397.html>`_.

Case Study: Repairing Parent Pointers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Online reconstruction of a file's parent pointer information works similarly to
directory reconstruction:

1. Set up a temporary file for generating a new extended attribute structure,
   an `xfblob<xfblob>` for storing parent pointer names, and an xfarray for
   stashing parent pointer updates.

2. Set up an inode scanner and hook into the directory entry code to receive
   updates on directory operations.

3. For each directory entry found in each directory scanned, decide if the
   dirent references the file of interest.
   If so:

   a. Stash an addpptr entry for this parent pointer in the xfblob and xfarray
      for later.

   b. When finished scanning the directory, flush the stashed updates to the
      temporary directory.

4. For each live directory update received via the hook, decide if the parent
   has already been scanned.
   If so:

   a. Stash an addpptr or removepptr entry for this dirent update in the
      xfarray for later.
      We cannot write parent pointers directly to the temporary file because
      hook functions are not allowed to modify filesystem metadata.
      Instead, we stash updates in the xfarray and rely on the scanner thread
      to apply the stashed parent pointer updates to the temporary file.

5. Copy all non-parent pointer extended attributes to the temporary file.

6. When the scan is complete, atomically swap the attribute fork of the
   temporary file and the file being repaired.
   The temporary file now contains the damaged extended attribute structure.

7. Reap the temporary file.

The proposed patchset is the
`parent pointers repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=pptrs-online-parent-repair>`_
series.

Digression: Offline Checking of Parent Pointers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Examining parent pointers in offline repair works differently because corrupt
files are erased long before directory tree connectivity checks are performed.
Parent pointer checks are therefore a second pass to be added to the existing
connectivity checks:

1. After the set of surviving files has been established (i.e. phase 6),
   walk the surviving directories of each AG in the filesystem.
   This is already performed as part of the connectivity checks.

2. For each directory entry found, record the name in an xfblob, and store
   ``(child_ag_inum, parent_inum, parent_gen, dirent_pos)`` tuples in a
   per-AG in-memory slab.

3. For each AG in the filesystem,

   a. Sort the per-AG tuples in order of child_ag_inum, parent_inum, and
      dirent_pos.

   b. For each inode in the AG,

      1. Scan the inode for parent pointers.
         Record the names in a per-file xfblob, and store ``(parent_inum,
         parent_gen, dirent_pos)`` tuples in a per-file slab.

      2. Sort the per-file tuples in order of parent_inum, and dirent_pos.

      3. Position one slab cursor at the start of the inode's records in the
         per-AG tuple slab.
         This should be trivial since the per-AG tuples are in child inumber
         order.

      4. Position a second slab cursor at the start of the per-file tuple slab.

      5. Iterate the two cursors in lockstep, comparing the parent_ino and
         dirent_pos fields of the records under each cursor.

         a. Tuples in the per-AG list but not the per-file list are missing and
            need to be written to the inode.

         b. Tuples in the per-file list but not the per-AG list are dangling
            and need to be removed from the inode.

         c. For tuples in both lists, update the parent_gen and name components
            of the parent pointer if necessary.

4. Move on to examining link counts, as we do today.

The proposed patchset is the
`offline parent pointers repair
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=pptrs-repair>`_
series.

Rebuilding directories from parent pointers in offline repair is very
challenging because it currently uses a single-pass scan of the filesystem
during phase 3 to decide which files are corrupt enough to be zapped.
This scan would have to be converted into a multi-pass scan:

1. The first pass of the scan zaps corrupt inodes, forks, and attributes
   much as it does now.
   Corrupt directories are noted but not zapped.

2. The next pass records parent pointers pointing to the directories noted
   as being corrupt in the first pass.
   This second pass may have to happen after the phase 4 scan for duplicate
   blocks, if phase 4 is also capable of zapping directories.

3. The third pass resets corrupt directories to an empty shortform directory.
   Free space metadata has not been ensured yet, so repair cannot yet use the
   directory building code in libxfs.

4. At the start of phase 6, space metadata have been rebuilt.
   Use the parent pointer information recorded during step 2 to reconstruct
   the dirents and add them to the now-empty directories.

This code has not yet been constructed.

.. _orphanage:

The Orphanage
-------------

Filesystems present files as a directed, and hopefully acyclic, graph.
In other words, a tree.
The root of the filesystem is a directory, and each entry in a directory points
downwards either to more subdirectories or to non-directory files.
Unfortunately, a disruption in the directory graph pointers result in a
disconnected graph, which makes files impossible to access via regular path
resolution.

Without parent pointers, the directory parent pointer online scrub code can
detect a dotdot entry pointing to a parent directory that doesn't have a link
back to the child directory and the file link count checker can detect a file
that isn't pointed to by any directory in the filesystem.
If such a file has a positive link count, the file is an orphan.

With parent pointers, directories can be rebuilt by scanning parent pointers
and parent pointers can be rebuilt by scanning directories.
This should reduce the incidence of files ending up in ``/lost+found``.

When orphans are found, they should be reconnected to the directory tree.
Offline fsck solves the problem by creating a directory ``/lost+found`` to
serve as an orphanage, and linking orphan files into the orphanage by using the
inumber as the name.
Reparenting a file to the orphanage does not reset any of its permissions or
ACLs.

This process is more involved in the kernel than it is in userspace.
The directory and file link count repair setup functions must use the regular
VFS mechanisms to create the orphanage directory with all the necessary
security attributes and dentry cache entries, just like a regular directory
tree modification.

Orphaned files are adopted by the orphanage as follows:

1. Call ``xrep_orphanage_try_create`` at the start of the scrub setup function
   to try to ensure that the lost and found directory actually exists.
   This also attaches the orphanage directory to the scrub context.

2. If the decision is made to reconnect a file, take the IOLOCK of both the
   orphanage and the file being reattached.
   The ``xrep_orphanage_iolock_two`` function follows the inode locking
   strategy discussed earlier.

3. Call ``xrep_orphanage_compute_blkres`` and ``xrep_orphanage_compute_name``
   to compute the new name in the orphanage and the block reservation required.

4. Use ``xrep_orphanage_adoption_prep`` to reserve resources to the repair
   transaction.

5. Call ``xrep_orphanage_adopt`` to reparent the orphaned file into the lost
   and found, and update the kernel dentry cache.

The proposed patches are in the
`orphanage adoption
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=repair-orphanage>`_
series.

6. Userspace Algorithms and Data Structures
===========================================

This section discusses the key algorithms and data structures of the userspace
program, ``xfs_scrub``, that provide the ability to drive metadata checks and
repairs in the kernel, verify file data, and look for other potential problems.

.. _scrubcheck:

Checking Metadata
-----------------

Recall the :ref:`phases of fsck work<scrubphases>` outlined earlier.
That structure follows naturally from the data dependencies designed into the
filesystem from its beginnings in 1993.
In XFS, there are several groups of metadata dependencies:

a. Filesystem summary counts depend on consistency within the inode indices,
   the allocation group space btrees, and the realtime volume space
   information.

b. Quota resource counts depend on consistency within the quota file data
   forks, inode indices, inode records, and the forks of every file on the
   system.

c. The naming hierarchy depends on consistency within the directory and
   extended attribute structures.
   This includes file link counts.

d. Directories, extended attributes, and file data depend on consistency within
   the file forks that map directory and extended attribute data to physical
   storage media.

e. The file forks depends on consistency within inode records and the space
   metadata indices of the allocation groups and the realtime volume.
   This includes quota and realtime metadata files.

f. Inode records depends on consistency within the inode metadata indices.

g. Realtime space metadata depend on the inode records and data forks of the
   realtime metadata inodes.

h. The allocation group metadata indices (free space, inodes, reference count,
   and reverse mapping btrees) depend on consistency within the AG headers and
   between all the AG metadata btrees.

i. ``xfs_scrub`` depends on the filesystem being mounted and kernel support
   for online fsck functionality.

Therefore, a metadata dependency graph is a convenient way to schedule checking
operations in the ``xfs_scrub`` program:

- Phase 1 checks that the provided path maps to an XFS filesystem and detect
  the kernel's scrubbing abilities, which validates group (i).

- Phase 2 scrubs groups (g) and (h) in parallel using a threaded workqueue.

- Phase 3 scans inodes in parallel.
  For each inode, groups (f), (e), and (d) are checked, in that order.

- Phase 4 repairs everything in groups (i) through (d) so that phases 5 and 6
  may run reliably.

- Phase 5 starts by checking groups (b) and (c) in parallel before moving on
  to checking names.

- Phase 6 depends on groups (i) through (b) to find file data blocks to verify,
  to read them, and to report which blocks of which files are affected.

- Phase 7 checks group (a), having validated everything else.

Notice that the data dependencies between groups are enforced by the structure
of the program flow.

Parallel Inode Scans
--------------------

An XFS filesystem can easily contain hundreds of millions of inodes.
Given that XFS targets installations with large high-performance storage,
it is desirable to scrub inodes in parallel to minimize runtime, particularly
if the program has been invoked manually from a command line.
This requires careful scheduling to keep the threads as evenly loaded as
possible.

Early iterations of the ``xfs_scrub`` inode scanner naïvely created a single
workqueue and scheduled a single workqueue item per AG.
Each workqueue item walked the inode btree (with ``XFS_IOC_INUMBERS``) to find
inode chunks and then called bulkstat (``XFS_IOC_BULKSTAT``) to gather enough
information to construct file handles.
The file handle was then passed to a function to generate scrub items for each
metadata object of each inode.
This simple algorithm leads to thread balancing problems in phase 3 if the
filesystem contains one AG with a few large sparse files and the rest of the
AGs contain many smaller files.
The inode scan dispatch function was not sufficiently granular; it should have
been dispatching at the level of individual inodes, or, to constrain memory
consumption, inode btree records.

Thanks to Dave Chinner, bounded workqueues in userspace enable ``xfs_scrub`` to
avoid this problem with ease by adding a second workqueue.
Just like before, the first workqueue is seeded with one workqueue item per AG,
and it uses INUMBERS to find inode btree chunks.
The second workqueue, however, is configured with an upper bound on the number
of items that can be waiting to be run.
Each inode btree chunk found by the first workqueue's workers are queued to the
second workqueue, and it is this second workqueue that queries BULKSTAT,
creates a file handle, and passes it to a function to generate scrub items for
each metadata object of each inode.
If the second workqueue is too full, the workqueue add function blocks the
first workqueue's workers until the backlog eases.
This doesn't completely solve the balancing problem, but reduces it enough to
move on to more pressing issues.

The proposed patchsets are the scrub
`performance tweaks
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-performance-tweaks>`_
and the
`inode scan rebalance
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-iscan-rebalance>`_
series.

.. _scrubrepair:

Scheduling Repairs
------------------

During phase 2, corruptions and inconsistencies reported in any AGI header or
inode btree are repaired immediately, because phase 3 relies on proper
functioning of the inode indices to find inodes to scan.
Failed repairs are rescheduled to phase 4.
Problems reported in any other space metadata are deferred to phase 4.
Optimization opportunities are always deferred to phase 4, no matter their
origin.

During phase 3, corruptions and inconsistencies reported in any part of a
file's metadata are repaired immediately if all space metadata were validated
during phase 2.
Repairs that fail or cannot be repaired immediately are scheduled for phase 4.

In the original design of ``xfs_scrub``, it was thought that repairs would be
so infrequent that the ``struct xfs_scrub_metadata`` objects used to
communicate with the kernel could also be used as the primary object to
schedule repairs.
With recent increases in the number of optimizations possible for a given
filesystem object, it became much more memory-efficient to track all eligible
repairs for a given filesystem object with a single repair item.
Each repair item represents a single lockable object -- AGs, metadata files,
individual inodes, or a class of summary information.

Phase 4 is responsible for scheduling a lot of repair work in as quick a
manner as is practical.
The :ref:`data dependencies <scrubcheck>` outlined earlier still apply, which
means that ``xfs_scrub`` must try to complete the repair work scheduled by
phase 2 before trying repair work scheduled by phase 3.
The repair process is as follows:

1. Start a round of repair with a workqueue and enough workers to keep the CPUs
   as busy as the user desires.

   a. For each repair item queued by phase 2,

      i.   Ask the kernel to repair everything listed in the repair item for a
           given filesystem object.

      ii.  Make a note if the kernel made any progress in reducing the number
           of repairs needed for this object.

      iii. If the object no longer requires repairs, revalidate all metadata
           associated with this object.
           If the revalidation succeeds, drop the repair item.
           If not, requeue the item for more repairs.

   b. If any repairs were made, jump back to 1a to retry all the phase 2 items.

   c. For each repair item queued by phase 3,

      i.   Ask the kernel to repair everything listed in the repair item for a
           given filesystem object.

      ii.  Make a note if the kernel made any progress in reducing the number
           of repairs needed for this object.

      iii. If the object no longer requires repairs, revalidate all metadata
           associated with this object.
           If the revalidation succeeds, drop the repair item.
           If not, requeue the item for more repairs.

   d. If any repairs were made, jump back to 1c to retry all the phase 3 items.

2. If step 1 made any repair progress of any kind, jump back to step 1 to start
   another round of repair.

3. If there are items left to repair, run them all serially one more time.
   Complain if the repairs were not successful, since this is the last chance
   to repair anything.

Corruptions and inconsistencies encountered during phases 5 and 7 are repaired
immediately.
Corrupt file data blocks reported by phase 6 cannot be recovered by the
filesystem.

The proposed patchsets are the
`repair warning improvements
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-better-repair-warnings>`_,
refactoring of the
`repair data dependency
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-repair-data-deps>`_
and
`object tracking
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-object-tracking>`_,
and the
`repair scheduling
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=scrub-repair-scheduling>`_
improvement series.

Checking Names for Confusable Unicode Sequences
-----------------------------------------------

If ``xfs_scrub`` succeeds in validating the filesystem metadata by the end of
phase 4, it moves on to phase 5, which checks for suspicious looking names in
the filesystem.
These names consist of the filesystem label, names in directory entries, and
the names of extended attributes.
Like most Unix filesystems, XFS imposes the sparest of constraints on the
contents of a name:

- Slashes and null bytes are not allowed in directory entries.

- Null bytes are not allowed in userspace-visible extended attributes.

- Null bytes are not allowed in the filesystem label.

Directory entries and attribute keys store the length of the name explicitly
ondisk, which means that nulls are not name terminators.
For this section, the term "naming domain" refers to any place where names are
presented together -- all the names in a directory, or all the attributes of a
file.

Although the Unix naming constraints are very permissive, the reality of most
modern-day Linux systems is that programs work with Unicode character code
points to support international languages.
These programs typically encode those code points in UTF-8 when interfacing
with the C library because the kernel expects null-terminated names.
In the common case, therefore, names found in an XFS filesystem are actually
UTF-8 encoded Unicode data.

To maximize its expressiveness, the Unicode standard defines separate control
points for various characters that render similarly or identically in writing
systems around the world.
For example, the character "Cyrillic Small Letter A" U+0430 "а" often renders
identically to "Latin Small Letter A" U+0061 "a".

The standard also permits characters to be constructed in multiple ways --
either by using a defined code point, or by combining one code point with
various combining marks.
For example, the character "Angstrom Sign U+212B "Å" can also be expressed
as "Latin Capital Letter A" U+0041 "A" followed by "Combining Ring Above"
U+030A "◌̊".
Both sequences render identically.

Like the standards that preceded it, Unicode also defines various control
characters to alter the presentation of text.
For example, the character "Right-to-Left Override" U+202E can trick some
programs into rendering "moo\\xe2\\x80\\xaegnp.txt" as "mootxt.png".
A second category of rendering problems involves whitespace characters.
If the character "Zero Width Space" U+200B is encountered in a file name, the
name will render identically to a name that does not have the zero width
space.

If two names within a naming domain have different byte sequences but render
identically, a user may be confused by it.
The kernel, in its indifference to upper level encoding schemes, permits this.
Most filesystem drivers persist the byte sequence names that are given to them
by the VFS.

Techniques for detecting confusable names are explained in great detail in
sections 4 and 5 of the
`Unicode Security Mechanisms <https://unicode.org/reports/tr39/>`_
document.
When ``xfs_scrub`` detects UTF-8 encoding in use on a system, it uses the
Unicode normalization form NFD in conjunction with the confusable name
detection component of
`libicu <https://github.com/unicode-org/icu>`_
to identify names with a directory or within a file's extended attributes that
could be confused for each other.
Names are also checked for control characters, non-rendering characters, and
mixing of bidirectional characters.
All of these potential issues are reported to the system administrator during
phase 5.

Media Verification of File Data Extents
---------------------------------------

The system administrator can elect to initiate a media scan of all file data
blocks.
This scan after validation of all filesystem metadata (except for the summary
counters) as phase 6.
The scan starts by calling ``FS_IOC_GETFSMAP`` to scan the filesystem space map
to find areas that are allocated to file data fork extents.
Gaps between data fork extents that are smaller than 64k are treated as if
they were data fork extents to reduce the command setup overhead.
When the space map scan accumulates a region larger than 32MB, a media
verification request is sent to the disk as a directio read of the raw block
device.

If the verification read fails, ``xfs_scrub`` retries with single-block reads
to narrow down the failure to the specific region of the media and recorded.
When it has finished issuing verification requests, it again uses the space
mapping ioctl to map the recorded media errors back to metadata structures
and report what has been lost.
For media errors in blocks owned by files, parent pointers can be used to
construct file paths from inode numbers for user-friendly reporting.

7. Conclusion and Future Work
=============================

It is hoped that the reader of this document has followed the designs laid out
in this document and now has some familiarity with how XFS performs online
rebuilding of its metadata indices, and how filesystem users can interact with
that functionality.
Although the scope of this work is daunting, it is hoped that this guide will
make it easier for code readers to understand what has been built, for whom it
has been built, and why.
Please feel free to contact the XFS mailing list with questions.

FIEXCHANGE_RANGE
----------------

As discussed earlier, a second frontend to the atomic extent swap mechanism is
a new ioctl call that userspace programs can use to commit updates to files
atomically.
This frontend has been out for review for several years now, though the
necessary refinements to online repair and lack of customer demand mean that
the proposal has not been pushed very hard.

Extent Swapping with Regular User Files
```````````````````````````````````````

As mentioned earlier, XFS has long had the ability to swap extents between
files, which is used almost exclusively by ``xfs_fsr`` to defragment files.
The earliest form of this was the fork swap mechanism, where the entire
contents of data forks could be exchanged between two files by exchanging the
raw bytes in each inode fork's immediate area.
When XFS v5 came along with self-describing metadata, this old mechanism grew
some log support to continue rewriting the owner fields of BMBT blocks during
log recovery.
When the reverse mapping btree was later added to XFS, the only way to maintain
the consistency of the fork mappings with the reverse mapping index was to
develop an iterative mechanism that used deferred bmap and rmap operations to
swap mappings one at a time.
This mechanism is identical to steps 2-3 from the procedure above except for
the new tracking items, because the atomic extent swap mechanism is an
iteration of an existing mechanism and not something totally novel.
For the narrow case of file defragmentation, the file contents must be
identical, so the recovery guarantees are not much of a gain.

Atomic extent swapping is much more flexible than the existing swapext
implementations because it can guarantee that the caller never sees a mix of
old and new contents even after a crash, and it can operate on two arbitrary
file fork ranges.
The extra flexibility enables several new use cases:

- **Atomic commit of file writes**: A userspace process opens a file that it
  wants to update.
  Next, it opens a temporary file and calls the file clone operation to reflink
  the first file's contents into the temporary file.
  Writes to the original file should instead be written to the temporary file.
  Finally, the process calls the atomic extent swap system call
  (``FIEXCHANGE_RANGE``) to exchange the file contents, thereby committing all
  of the updates to the original file, or none of them.

.. _swapext_if_unchanged:

- **Transactional file updates**: The same mechanism as above, but the caller
  only wants the commit to occur if the original file's contents have not
  changed.
  To make this happen, the calling process snapshots the file modification and
  change timestamps of the original file before reflinking its data to the
  temporary file.
  When the program is ready to commit the changes, it passes the timestamps
  into the kernel as arguments to the atomic extent swap system call.
  The kernel only commits the changes if the provided timestamps match the
  original file.

- **Emulation of atomic block device writes**: Export a block device with a
  logical sector size matching the filesystem block size to force all writes
  to be aligned to the filesystem block size.
  Stage all writes to a temporary file, and when that is complete, call the
  atomic extent swap system call with a flag to indicate that holes in the
  temporary file should be ignored.
  This emulates an atomic device write in software, and can support arbitrary
  scattered writes.

Vectorized Scrub
----------------

As it turns out, the :ref:`refactoring <scrubrepair>` of repair items mentioned
earlier was a catalyst for enabling a vectorized scrub system call.
Since 2018, the cost of making a kernel call has increased considerably on some
systems to mitigate the effects of speculative execution attacks.
This incentivizes program authors to make as few system calls as possible to
reduce the number of times an execution path crosses a security boundary.

With vectorized scrub, userspace pushes to the kernel the identity of a
filesystem object, a list of scrub types to run against that object, and a
simple representation of the data dependencies between the selected scrub
types.
The kernel executes as much of the caller's plan as it can until it hits a
dependency that cannot be satisfied due to a corruption, and tells userspace
how much was accomplished.
It is hoped that ``io_uring`` will pick up enough of this functionality that
online fsck can use that instead of adding a separate vectored scrub system
call to XFS.

The relevant patchsets are the
`kernel vectorized scrub
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=vectorized-scrub>`_
and
`userspace vectorized scrub
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=vectorized-scrub>`_
series.

Quality of Service Targets for Scrub
------------------------------------

One serious shortcoming of the online fsck code is that the amount of time that
it can spend in the kernel holding resource locks is basically unbounded.
Userspace is allowed to send a fatal signal to the process which will cause
``xfs_scrub`` to exit when it reaches a good stopping point, but there's no way
for userspace to provide a time budget to the kernel.
Given that the scrub codebase has helpers to detect fatal signals, it shouldn't
be too much work to allow userspace to specify a timeout for a scrub/repair
operation and abort the operation if it exceeds budget.
However, most repair functions have the property that once they begin to touch
ondisk metadata, the operation cannot be cancelled cleanly, after which a QoS
timeout is no longer useful.

Defragmenting Free Space
------------------------

Over the years, many XFS users have requested the creation of a program to
clear a portion of the physical storage underlying a filesystem so that it
becomes a contiguous chunk of free space.
Call this free space defragmenter ``clearspace`` for short.

The first piece the ``clearspace`` program needs is the ability to read the
reverse mapping index from userspace.
This already exists in the form of the ``FS_IOC_GETFSMAP`` ioctl.
The second piece it needs is a new fallocate mode
(``FALLOC_FL_MAP_FREE_SPACE``) that allocates the free space in a region and
maps it to a file.
Call this file the "space collector" file.
The third piece is the ability to force an online repair.

To clear all the metadata out of a portion of physical storage, clearspace
uses the new fallocate map-freespace call to map any free space in that region
to the space collector file.
Next, clearspace finds all metadata blocks in that region by way of
``GETFSMAP`` and issues forced repair requests on the data structure.
This often results in the metadata being rebuilt somewhere that is not being
cleared.
After each relocation, clearspace calls the "map free space" function again to
collect any newly freed space in the region being cleared.

To clear all the file data out of a portion of the physical storage, clearspace
uses the FSMAP information to find relevant file data blocks.
Having identified a good target, it uses the ``FICLONERANGE`` call on that part
of the file to try to share the physical space with a dummy file.
Cloning the extent means that the original owners cannot overwrite the
contents; any changes will be written somewhere else via copy-on-write.
Clearspace makes its own copy of the frozen extent in an area that is not being
cleared, and uses ``FIEDEUPRANGE`` (or the :ref:`atomic extent swap
<swapext_if_unchanged>` feature) to change the target file's data extent
mapping away from the area being cleared.
When all other mappings have been moved, clearspace reflinks the space into the
space collector file so that it becomes unavailable.

There are further optimizations that could apply to the above algorithm.
To clear a piece of physical storage that has a high sharing factor, it is
strongly desirable to retain this sharing factor.
In fact, these extents should be moved first to maximize sharing factor after
the operation completes.
To make this work smoothly, clearspace needs a new ioctl
(``FS_IOC_GETREFCOUNTS``) to report reference count information to userspace.
With the refcount information exposed, clearspace can quickly find the longest,
most shared data extents in the filesystem, and target them first.

**Future Work Question**: How might the filesystem move inode chunks?

*Answer*: To move inode chunks, Dave Chinner constructed a prototype program
that creates a new file with the old contents and then locklessly runs around
the filesystem updating directory entries.
The operation cannot complete if the filesystem goes down.
That problem isn't totally insurmountable: create an inode remapping table
hidden behind a jump label, and a log item that tracks the kernel walking the
filesystem to update directory entries.
The trouble is, the kernel can't do anything about open files, since it cannot
revoke them.

**Future Work Question**: Can static keys be used to minimize the cost of
supporting ``revoke()`` on XFS files?

*Answer*: Yes.
Until the first revocation, the bailout code need not be in the call path at
all.

The relevant patchsets are the
`kernel freespace defrag
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfs-linux.git/log/?h=defrag-freespace>`_
and
`userspace freespace defrag
<https://git.kernel.org/pub/scm/linux/kernel/git/djwong/xfsprogs-dev.git/log/?h=defrag-freespace>`_
series.

Shrinking Filesystems
---------------------

Removing the end of the filesystem ought to be a simple matter of evacuating
the data and metadata at the end of the filesystem, and handing the freed space
to the shrink code.
That requires an evacuation of the space at end of the filesystem, which is a
use of free space defragmentation!

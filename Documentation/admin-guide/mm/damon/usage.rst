.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for different users.

- *DAMON user space tool.*
  This is for privileged people such as system administrators who want a
  just-working human-friendly interface.  Using this, users can use the DAMON’s
  major features in a human-friendly way.  It may not be highly tuned for
  special cases, though.  It supports both virtual and physical address spaces
  monitoring.
- *debugfs interface.*
  This is for privileged user space programmers who want more optimized use of
  DAMON.  Using this, users can use DAMON’s major features by reading
  from and writing to special debugfs files.  Therefore, you can write and use
  your personalized DAMON debugfs wrapper programs that reads/writes the
  debugfs files instead of you.  The DAMON user space tool is also a reference
  implementation of such programs.  It supports both virtual and physical
  address spaces monitoring.
- *Kernel Space Programming Interface.*
  This is for kernel space programmers.  Using this, users can utilize every
  feature of DAMON most flexibly and efficiently by writing kernel space
  DAMON application programs for you.  You can even extend DAMON for various
  address spaces.

Nevertheless, you could write your own user space tool using the debugfs
interface.  A reference implementation is available at
https://github.com/awslabs/damo.  If you are a kernel programmer, you could
refer to :doc:`/vm/damon/api` for the kernel space programming interface.  For
the reason, this document describes only the debugfs interface

debugfs Interface
=================

DAMON exports five files, ``attrs``, ``target_ids``, ``init_regions``,
``schemes`` and ``monitor_on`` under its debugfs directory,
``<debugfs>/damon/``.


Attributes
----------

Users can get and set the ``sampling interval``, ``aggregation interval``,
``regions update interval``, and min/max number of monitoring target regions by
reading from and writing to the ``attrs`` file.  To know about the monitoring
attributes in detail, please refer to the :doc:`/vm/damon/design`.  For
example, below commands set those values to 5 ms, 100 ms, 1,000 ms, 10 and
1000, and then check it again::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 > attrs
    # cat attrs
    5000 100000 1000000 10 1000


Target IDs
----------

Some types of address spaces supports multiple monitoring target.  For example,
the virtual memory address spaces monitoring can have multiple processes as the
monitoring targets.  Users can set the targets by writing relevant id values of
the targets to, and get the ids of the current targets by reading from the
``target_ids`` file.  In case of the virtual address spaces monitoring, the
values should be pids of the monitoring target processes.  For example, below
commands set processes having pids 42 and 4242 as the monitoring targets and
check it again::

    # cd <debugfs>/damon
    # echo 42 4242 > target_ids
    # cat target_ids
    42 4242

Users can also monitor the physical memory address space of the system by
writing a special keyword, "``paddr\n``" to the file.  Because physical address
space monitoring doesn't support multiple targets, reading the file will show a
fake value, ``42``, as below::

    # cd <debugfs>/damon
    # echo paddr > target_ids
    # cat target_ids
    42

Note that setting the target ids doesn't start the monitoring.


Initial Monitoring Target Regions
---------------------------------

In case of the virtual address space monitoring, DAMON automatically sets and
updates the monitoring target regions so that entire memory mappings of target
processes can be covered.  However, users can want to limit the monitoring
region to specific address ranges, such as the heap, the stack, or specific
file-mapped area.  Or, some users can know the initial access pattern of their
workloads and therefore want to set optimal initial regions for the 'adaptive
regions adjustment'.

In contrast, DAMON do not automatically sets and updates the monitoring target
regions in case of physical memory monitoring.  Therefore, users should set the
monitoring target regions by themselves.

In such cases, users can explicitly set the initial monitoring target regions
as they want, by writing proper values to the ``init_regions`` file.  Each line
of the input should represent one region in below form.::

    <target id> <start address> <end address>

The ``target id`` should already in ``target_ids`` file, and the regions should
be passed in address order.  For example, below commands will set a couple of
address ranges, ``1-100`` and ``100-200`` as the initial monitoring target
region of process 42, and another couple of address ranges, ``20-40`` and
``50-100`` as that of process 4242.::

    # cd <debugfs>/damon
    # echo "42   1       100
            42   100     200
            4242 20      40
            4242 50      100" > init_regions

Note that this sets the initial monitoring target regions only.  In case of
virtual memory monitoring, DAMON will automatically updates the boundary of the
regions after one ``regions update interval``.  Therefore, users should set the
``regions update interval`` large enough in this case, if they don't want the
update.


Schemes
-------

For usual DAMON-based data access aware memory management optimizations, users
would simply want the system to apply a memory management action to a memory
region of a specific size having a specific access frequency for a specific
time.  DAMON receives such formalized operation schemes from the user and
applies those to the target processes.  It also counts the total number and
size of regions that each scheme is applied.  This statistics can be used for
online analysis or tuning of the schemes.

Users can get and set the schemes by reading from and writing to ``schemes``
debugfs file.  Reading the file also shows the statistics of each scheme.  To
the file, each of the schemes should be represented in each line in below form:

    min-size max-size min-acc max-acc min-age max-age action

Note that the ranges are closed interval.  Bytes for the size of regions
(``min-size`` and ``max-size``), number of monitored accesses per aggregate
interval for access frequency (``min-acc`` and ``max-acc``), number of
aggregate intervals for the age of regions (``min-age`` and ``max-age``), and a
predefined integer for memory management actions should be used.  The supported
numbers and their meanings are as below.

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``
 - 5: Do nothing but count the statistics

You can disable schemes by simply writing an empty string to the file.  For
example, below commands applies a scheme saying "If a memory region of size in
[4KiB, 8KiB] is showing accesses per aggregate interval in [0, 5] for aggregate
interval in [10, 20], page out the region", check the entered scheme again, and
finally remove the scheme. ::

    # cd <debugfs>/damon
    # echo "4096 8192    0 5    10 20    2" > schemes
    # cat schemes
    4096 8192 0 5 10 20 2 0 0
    # echo > schemes

The last two integers in the 4th line of above example is the total number and
the total size of the regions that the scheme is applied.


Turning On/Off
--------------

Setting the files as described above doesn't incur effect unless you explicitly
start the monitoring.  You can start, stop, and check the current status of the
monitoring by writing to and reading from the ``monitor_on`` file.  Writing
``on`` to the file starts the monitoring of the targets with the attributes.
Writing ``off`` to the file stops those.  DAMON also stops if every target
process is terminated.  Below example commands turn on, off, and check the
status of DAMON::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that you cannot write to the above-mentioned debugfs files while
the monitoring is turned on.  If you write to the files while DAMON is running,
an error code such as ``-EBUSY`` will be returned.


Tracepoint for Monitoring Results
=================================

DAMON provides the monitoring results via a tracepoint,
``damon:damon_aggregated``.  While the monitoring is turned on, you could
record the tracepoint events and show results using tracepoint supporting tools
like ``perf``.  For example::

    # echo on > monitor_on
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on
    # perf script

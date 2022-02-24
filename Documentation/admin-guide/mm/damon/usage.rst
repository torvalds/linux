.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for different users.

- *DAMON user space tool.*
  `This <https://github.com/awslabs/damo>`_ is for privileged people such as
  system administrators who want a just-working human-friendly interface.
  Using this, users can use the DAMON’s major features in a human-friendly way.
  It may not be highly tuned for special cases, though.  It supports both
  virtual and physical address spaces monitoring.  For more detail, please
  refer to its `usage document
  <https://github.com/awslabs/damo/blob/next/USAGE.md>`_.
- *debugfs interface.*
  :ref:`This <debugfs_interface>` is for privileged user space programmers who
  want more optimized use of DAMON.  Using this, users can use DAMON’s major
  features by reading from and writing to special debugfs files.  Therefore,
  you can write and use your personalized DAMON debugfs wrapper programs that
  reads/writes the debugfs files instead of you.  The `DAMON user space tool
  <https://github.com/awslabs/damo>`_ is one example of such programs.  It
  supports both virtual and physical address spaces monitoring.  Note that this
  interface provides only simple :ref:`statistics <damos_stats>` for the
  monitoring results.  For detailed monitoring results, DAMON provides a
  :ref:`tracepoint <tracepoint>`.
- *Kernel Space Programming Interface.*
  :doc:`This </vm/damon/api>` is for kernel space programmers.  Using this,
  users can utilize every feature of DAMON most flexibly and efficiently by
  writing kernel space DAMON application programs for you.  You can even extend
  DAMON for various address spaces.  For detail, please refer to the interface
  :doc:`document </vm/damon/api>`.


.. _debugfs_interface:

debugfs Interface
=================

DAMON exports eight files, ``attrs``, ``target_ids``, ``init_regions``,
``schemes``, ``monitor_on``, ``kdamond_pid``, ``mk_contexts`` and
``rm_contexts`` under its debugfs directory, ``<debugfs>/damon/``.


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
region of a specific access pattern.  DAMON receives such formalized operation
schemes from the user and applies those to the target processes.

Users can get and set the schemes by reading from and writing to ``schemes``
debugfs file.  Reading the file also shows the statistics of each scheme.  To
the file, each of the schemes should be represented in each line in below
form::

    <target access pattern> <action> <quota> <watermarks>

You can disable schemes by simply writing an empty string to the file.

Target Access Pattern
~~~~~~~~~~~~~~~~~~~~~

The ``<target access pattern>`` is constructed with three ranges in below
form::

    min-size max-size min-acc max-acc min-age max-age

Specifically, bytes for the size of regions (``min-size`` and ``max-size``),
number of monitored accesses per aggregate interval for access frequency
(``min-acc`` and ``max-acc``), number of aggregate intervals for the age of
regions (``min-age`` and ``max-age``) are specified.  Note that the ranges are
closed interval.

Action
~~~~~~

The ``<action>`` is a predefined integer for memory management actions, which
DAMON will apply to the regions having the target access pattern.  The
supported numbers and their meanings are as below.

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``
 - 5: Do nothing but count the statistics

Quota
~~~~~

Optimal ``target access pattern`` for each ``action`` is workload dependent, so
not easy to find.  Worse yet, setting a scheme of some action too aggressive
can cause severe overhead.  To avoid such overhead, users can limit time and
size quota for the scheme via the ``<quota>`` in below form::

    <ms> <sz> <reset interval> <priority weights>

This makes DAMON to try to use only up to ``<ms>`` milliseconds for applying
the action to memory regions of the ``target access pattern`` within the
``<reset interval>`` milliseconds, and to apply the action to only up to
``<sz>`` bytes of memory regions within the ``<reset interval>``.  Setting both
``<ms>`` and ``<sz>`` zero disables the quota limits.

When the quota limit is expected to be exceeded, DAMON prioritizes found memory
regions of the ``target access pattern`` based on their size, access frequency,
and age.  For personalized prioritization, users can set the weights for the
three properties in ``<priority weights>`` in below form::

    <size weight> <access frequency weight> <age weight>

Watermarks
~~~~~~~~~~

Some schemes would need to run based on current value of the system's specific
metrics like free memory ratio.  For such cases, users can specify watermarks
for the condition.::

    <metric> <check interval> <high mark> <middle mark> <low mark>

``<metric>`` is a predefined integer for the metric to be checked.  The
supported numbers and their meanings are as below.

 - 0: Ignore the watermarks
 - 1: System's free memory rate (per thousand)

The value of the metric is checked every ``<check interval>`` microseconds.

If the value is higher than ``<high mark>`` or lower than ``<low mark>``, the
scheme is deactivated.  If the value is lower than ``<mid mark>``, the scheme
is activated.

.. _damos_stats:

Statistics
~~~~~~~~~~

It also counts the total number and bytes of regions that each scheme is tried
to be applied, the two numbers for the regions that each scheme is successfully
applied, and the total number of the quota limit exceeds.  This statistics can
be used for online analysis or tuning of the schemes.

The statistics can be shown by reading the ``schemes`` file.  Reading the file
will show each scheme you entered in each line, and the five numbers for the
statistics will be added at the end of each line.

Example
~~~~~~~

Below commands applies a scheme saying "If a memory region of size in [4KiB,
8KiB] is showing accesses per aggregate interval in [0, 5] for aggregate
interval in [10, 20], page out the region.  For the paging out, use only up to
10ms per second, and also don't page out more than 1GiB per second.  Under the
limitation, page out memory regions having longer age first.  Also, check the
free memory rate of the system every 5 seconds, start the monitoring and paging
out when the free memory rate becomes lower than 50%, but stop it if the free
memory rate becomes larger than 60%, or lower than 30%".::

    # cd <debugfs>/damon
    # scheme="4096 8192  0 5    10 20    2"  # target access pattern and action
    # scheme+=" 10 $((1024*1024*1024)) 1000" # quotas
    # scheme+=" 0 0 100"                     # prioritization weights
    # scheme+=" 1 5000000 600 500 300"       # watermarks
    # echo "$scheme" > schemes


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


Monitoring Thread PID
---------------------

DAMON does requested monitoring with a kernel thread called ``kdamond``.  You
can get the pid of the thread by reading the ``kdamond_pid`` file.  When the
monitoring is turned off, reading the file returns ``none``. ::

    # cd <debugfs>/damon
    # cat monitor_on
    off
    # cat kdamond_pid
    none
    # echo on > monitor_on
    # cat kdamond_pid
    18594


Using Multiple Monitoring Threads
---------------------------------

One ``kdamond`` thread is created for each monitoring context.  You can create
and remove monitoring contexts for multiple ``kdamond`` required use case using
the ``mk_contexts`` and ``rm_contexts`` files.

Writing the name of the new context to the ``mk_contexts`` file creates a
directory of the name on the DAMON debugfs directory.  The directory will have
DAMON debugfs files for the context. ::

    # cd <debugfs>/damon
    # ls foo
    # ls: cannot access 'foo': No such file or directory
    # echo foo > mk_contexts
    # ls foo
    # attrs  init_regions  kdamond_pid  schemes  target_ids

If the context is not needed anymore, you can remove it and the corresponding
directory by putting the name of the context to the ``rm_contexts`` file. ::

    # echo foo > rm_contexts
    # ls foo
    # ls: cannot access 'foo': No such file or directory

Note that ``mk_contexts``, ``rm_contexts``, and ``monitor_on`` files are in the
root directory only.


.. _tracepoint:

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

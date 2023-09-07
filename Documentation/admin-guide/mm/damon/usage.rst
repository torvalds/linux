.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below interfaces for different users.

- *DAMON user space tool.*
  `This <https://github.com/awslabs/damo>`_ is for privileged people such as
  system administrators who want a just-working human-friendly interface.
  Using this, users can use the DAMON’s major features in a human-friendly way.
  It may not be highly tuned for special cases, though.  For more detail,
  please refer to its `usage document
  <https://github.com/awslabs/damo/blob/next/USAGE.md>`_.
- *sysfs interface.*
  :ref:`This <sysfs_interface>` is for privileged user space programmers who
  want more optimized use of DAMON.  Using this, users can use DAMON’s major
  features by reading from and writing to special sysfs files.  Therefore,
  you can write and use your personalized DAMON sysfs wrapper programs that
  reads/writes the sysfs files instead of you.  The `DAMON user space tool
  <https://github.com/awslabs/damo>`_ is one example of such programs.
- *Kernel Space Programming Interface.*
  :doc:`This </mm/damon/api>` is for kernel space programmers.  Using this,
  users can utilize every feature of DAMON most flexibly and efficiently by
  writing kernel space DAMON application programs for you.  You can even extend
  DAMON for various address spaces.  For detail, please refer to the interface
  :doc:`document </mm/damon/api>`.
- *debugfs interface. (DEPRECATED!)*
  :ref:`This <debugfs_interface>` is almost identical to :ref:`sysfs interface
  <sysfs_interface>`.  This is deprecated, so users should move to the
  :ref:`sysfs interface <sysfs_interface>`.  If you depend on this and cannot
  move, please report your usecase to damon@lists.linux.dev and
  linux-mm@kvack.org.

.. _sysfs_interface:

sysfs Interface
===============

DAMON sysfs interface is built when ``CONFIG_DAMON_SYSFS`` is defined.  It
creates multiple directories and files under its sysfs directory,
``<sysfs>/kernel/mm/damon/``.  You can control DAMON by writing to and reading
from the files under the directory.

For a short example, users can monitor the virtual address space of a given
workload as below. ::

    # cd /sys/kernel/mm/damon/admin/
    # echo 1 > kdamonds/nr_kdamonds && echo 1 > kdamonds/0/contexts/nr_contexts
    # echo vaddr > kdamonds/0/contexts/0/operations
    # echo 1 > kdamonds/0/contexts/0/targets/nr_targets
    # echo $(pidof <workload>) > kdamonds/0/contexts/0/targets/0/pid_target
    # echo on > kdamonds/0/state

Files Hierarchy
---------------

The files hierarchy of DAMON sysfs interface is shown below.  In the below
figure, parents-children relations are represented with indentations, each
directory is having ``/`` suffix, and files in each directory are separated by
comma (","). ::

    /sys/kernel/mm/damon/admin
    │ kdamonds/nr_kdamonds
    │ │ 0/state,pid
    │ │ │ contexts/nr_contexts
    │ │ │ │ 0/avail_operations,operations
    │ │ │ │ │ monitoring_attrs/
    │ │ │ │ │ │ intervals/sample_us,aggr_us,update_us
    │ │ │ │ │ │ nr_regions/min,max
    │ │ │ │ │ targets/nr_targets
    │ │ │ │ │ │ 0/pid_target
    │ │ │ │ │ │ │ regions/nr_regions
    │ │ │ │ │ │ │ │ 0/start,end
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ │ schemes/nr_schemes
    │ │ │ │ │ │ 0/action
    │ │ │ │ │ │ │ access_pattern/
    │ │ │ │ │ │ │ │ sz/min,max
    │ │ │ │ │ │ │ │ nr_accesses/min,max
    │ │ │ │ │ │ │ │ age/min,max
    │ │ │ │ │ │ │ quotas/ms,bytes,reset_interval_ms
    │ │ │ │ │ │ │ │ weights/sz_permil,nr_accesses_permil,age_permil
    │ │ │ │ │ │ │ watermarks/metric,interval_us,high,mid,low
    │ │ │ │ │ │ │ filters/nr_filters
    │ │ │ │ │ │ │ │ 0/type,matching,memcg_id
    │ │ │ │ │ │ │ stats/nr_tried,sz_tried,nr_applied,sz_applied,qt_exceeds
    │ │ │ │ │ │ │ tried_regions/total_bytes
    │ │ │ │ │ │ │ │ 0/start,end,nr_accesses,age
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ ...
    │ │ ...

Root
----

The root of the DAMON sysfs interface is ``<sysfs>/kernel/mm/damon/``, and it
has one directory named ``admin``.  The directory contains the files for
privileged user space programs' control of DAMON.  User space tools or daemons
having the root permission could use this directory.

kdamonds/
---------

Under the ``admin`` directory, one directory, ``kdamonds``, which has files for
controlling the kdamonds (refer to
:ref:`design <damon_design_execution_model_and_data_structures>` for more
details) exists.  In the beginning, this directory has only one file,
``nr_kdamonds``.  Writing a number (``N``) to the file creates the number of
child directories named ``0`` to ``N-1``.  Each directory represents each
kdamond.

kdamonds/<N>/
-------------

In each kdamond directory, two files (``state`` and ``pid``) and one directory
(``contexts``) exist.

Reading ``state`` returns ``on`` if the kdamond is currently running, or
``off`` if it is not running.  Writing ``on`` or ``off`` makes the kdamond be
in the state.  Writing ``commit`` to the ``state`` file makes kdamond reads the
user inputs in the sysfs files except ``state`` file again.  Writing
``update_schemes_stats`` to ``state`` file updates the contents of stats files
for each DAMON-based operation scheme of the kdamond.  For details of the
stats, please refer to :ref:`stats section <sysfs_schemes_stats>`.

Writing ``update_schemes_tried_regions`` to ``state`` file updates the
DAMON-based operation scheme action tried regions directory for each
DAMON-based operation scheme of the kdamond.  Writing
``update_schemes_tried_bytes`` to ``state`` file updates only
``.../tried_regions/total_bytes`` files.  Writing
``clear_schemes_tried_regions`` to ``state`` file clears the DAMON-based
operating scheme action tried regions directory for each DAMON-based operation
scheme of the kdamond.  For details of the DAMON-based operation scheme action
tried regions directory, please refer to :ref:`tried_regions section
<sysfs_schemes_tried_regions>`.

If the state is ``on``, reading ``pid`` shows the pid of the kdamond thread.

``contexts`` directory contains files for controlling the monitoring contexts
that this kdamond will execute.

kdamonds/<N>/contexts/
----------------------

In the beginning, this directory has only one file, ``nr_contexts``.  Writing a
number (``N``) to the file creates the number of child directories named as
``0`` to ``N-1``.  Each directory represents each monitoring context (refer to
:ref:`design <damon_design_execution_model_and_data_structures>` for more
details).  At the moment, only one context per kdamond is supported, so only
``0`` or ``1`` can be written to the file.

.. _sysfs_contexts:

contexts/<N>/
-------------

In each context directory, two files (``avail_operations`` and ``operations``)
and three directories (``monitoring_attrs``, ``targets``, and ``schemes``)
exist.

DAMON supports multiple types of monitoring operations, including those for
virtual address space and the physical address space.  You can get the list of
available monitoring operations set on the currently running kernel by reading
``avail_operations`` file.  Based on the kernel configuration, the file will
list some or all of below keywords.

 - vaddr: Monitor virtual address spaces of specific processes
 - fvaddr: Monitor fixed virtual address ranges
 - paddr: Monitor the physical address space of the system

Please refer to :ref:`regions sysfs directory <sysfs_regions>` for detailed
differences between the operations sets in terms of the monitoring target
regions.

You can set and get what type of monitoring operations DAMON will use for the
context by writing one of the keywords listed in ``avail_operations`` file and
reading from the ``operations`` file.

.. _sysfs_monitoring_attrs:

contexts/<N>/monitoring_attrs/
------------------------------

Files for specifying attributes of the monitoring including required quality
and efficiency of the monitoring are in ``monitoring_attrs`` directory.
Specifically, two directories, ``intervals`` and ``nr_regions`` exist in this
directory.

Under ``intervals`` directory, three files for DAMON's sampling interval
(``sample_us``), aggregation interval (``aggr_us``), and update interval
(``update_us``) exist.  You can set and get the values in micro-seconds by
writing to and reading from the files.

Under ``nr_regions`` directory, two files for the lower-bound and upper-bound
of DAMON's monitoring regions (``min`` and ``max``, respectively), which
controls the monitoring overhead, exist.  You can set and get the values by
writing to and rading from the files.

For more details about the intervals and monitoring regions range, please refer
to the Design document (:doc:`/mm/damon/design`).

contexts/<N>/targets/
---------------------

In the beginning, this directory has only one file, ``nr_targets``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each monitoring target.

targets/<N>/
------------

In each target directory, one file (``pid_target``) and one directory
(``regions``) exist.

If you wrote ``vaddr`` to the ``contexts/<N>/operations``, each target should
be a process.  You can specify the process to DAMON by writing the pid of the
process to the ``pid_target`` file.

.. _sysfs_regions:

targets/<N>/regions
-------------------

When ``vaddr`` monitoring operations set is being used (``vaddr`` is written to
the ``contexts/<N>/operations`` file), DAMON automatically sets and updates the
monitoring target regions so that entire memory mappings of target processes
can be covered.  However, users could want to set the initial monitoring region
to specific address ranges.

In contrast, DAMON do not automatically sets and updates the monitoring target
regions when ``fvaddr`` or ``paddr`` monitoring operations sets are being used
(``fvaddr`` or ``paddr`` have written to the ``contexts/<N>/operations``).
Therefore, users should set the monitoring target regions by themselves in the
cases.

For such cases, users can explicitly set the initial monitoring target regions
as they want, by writing proper values to the files under this directory.

In the beginning, this directory has only one file, ``nr_regions``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each initial monitoring target region.

regions/<N>/
------------

In each region directory, you will find two files (``start`` and ``end``).  You
can set and get the start and end addresses of the initial monitoring target
region by writing to and reading from the files, respectively.

Each region should not overlap with others.  ``end`` of directory ``N`` should
be equal or smaller than ``start`` of directory ``N+1``.

contexts/<N>/schemes/
---------------------

The directory for DAMON-based Operation Schemes (:ref:`DAMOS
<damon_design_damos>`).  Users can get and set the schemes by reading from and
writing to files under this directory.

In the beginning, this directory has only one file, ``nr_schemes``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each DAMON-based operation scheme.

schemes/<N>/
------------

In each scheme directory, five directories (``access_pattern``, ``quotas``,
``watermarks``, ``filters``, ``stats``, and ``tried_regions``) and one file
(``action``) exist.

The ``action`` file is for setting and getting the scheme's :ref:`action
<damon_design_damos_action>`.  The keywords that can be written to and read
from the file and their meaning are as below.

Note that support of each action depends on the running DAMON operations set
:ref:`implementation <sysfs_contexts>`.

 - ``willneed``: Call ``madvise()`` for the region with ``MADV_WILLNEED``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``cold``: Call ``madvise()`` for the region with ``MADV_COLD``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``pageout``: Call ``madvise()`` for the region with ``MADV_PAGEOUT``.
   Supported by ``vaddr``, ``fvaddr`` and ``paddr`` operations set.
 - ``hugepage``: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``nohugepage``: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``lru_prio``: Prioritize the region on its LRU lists.
   Supported by ``paddr`` operations set.
 - ``lru_deprio``: Deprioritize the region on its LRU lists.
   Supported by ``paddr`` operations set.
 - ``stat``: Do nothing but count the statistics.
   Supported by all operations sets.

schemes/<N>/access_pattern/
---------------------------

The directory for the target access :ref:`pattern
<damon_design_damos_access_pattern>` of the given DAMON-based operation scheme.

Under the ``access_pattern`` directory, three directories (``sz``,
``nr_accesses``, and ``age``) each having two files (``min`` and ``max``)
exist.  You can set and get the access pattern for the given scheme by writing
to and reading from the ``min`` and ``max`` files under ``sz``,
``nr_accesses``, and ``age`` directories, respectively.  Note that the ``min``
and the ``max`` form a closed interval.

schemes/<N>/quotas/
-------------------

The directory for the :ref:`quotas <damon_design_damos_quotas>` of the given
DAMON-based operation scheme.

Under ``quotas`` directory, three files (``ms``, ``bytes``,
``reset_interval_ms``) and one directory (``weights``) having three files
(``sz_permil``, ``nr_accesses_permil``, and ``age_permil``) in it exist.

You can set the ``time quota`` in milliseconds, ``size quota`` in bytes, and
``reset interval`` in milliseconds by writing the values to the three files,
respectively.  Then, DAMON tries to use only up to ``time quota`` milliseconds
for applying the ``action`` to memory regions of the ``access_pattern``, and to
apply the action to only up to ``bytes`` bytes of memory regions within the
``reset_interval_ms``.  Setting both ``ms`` and ``bytes`` zero disables the
quota limits.

You can also set the :ref:`prioritization weights
<damon_design_damos_quotas_prioritization>` for size, access frequency, and age
in per-thousand unit by writing the values to the three files under the
``weights`` directory.

schemes/<N>/watermarks/
-----------------------

The directory for the :ref:`watermarks <damon_design_damos_watermarks>` of the
given DAMON-based operation scheme.

Under the watermarks directory, five files (``metric``, ``interval_us``,
``high``, ``mid``, and ``low``) for setting the metric, the time interval
between check of the metric, and the three watermarks exist.  You can set and
get the five values by writing to the files, respectively.

Keywords and meanings of those that can be written to the ``metric`` file are
as below.

 - none: Ignore the watermarks
 - free_mem_rate: System's free memory rate (per thousand)

The ``interval`` should written in microseconds unit.

schemes/<N>/filters/
--------------------

The directory for the :ref:`filters <damon_design_damos_filters>` of the given
DAMON-based operation scheme.

In the beginning, this directory has only one file, ``nr_filters``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each filter.  The filters are evaluated
in the numeric order.

Each filter directory contains six files, namely ``type``, ``matcing``,
``memcg_path``, ``addr_start``, ``addr_end``, and ``target_idx``.  To ``type``
file, you can write one of four special keywords: ``anon`` for anonymous pages,
``memcg`` for specific memory cgroup, ``addr`` for specific address range (an
open-ended interval), or ``target`` for specific DAMON monitoring target
filtering.  In case of the memory cgroup filtering, you can specify the memory
cgroup of the interest by writing the path of the memory cgroup from the
cgroups mount point to ``memcg_path`` file.  In case of the address range
filtering, you can specify the start and end address of the range to
``addr_start`` and ``addr_end`` files, respectively.  For the DAMON monitoring
target filtering, you can specify the index of the target between the list of
the DAMON context's monitoring targets list to ``target_idx`` file.  You can
write ``Y`` or ``N`` to ``matching`` file to filter out pages that does or does
not match to the type, respectively.  Then, the scheme's action will not be
applied to the pages that specified to be filtered out.

For example, below restricts a DAMOS action to be applied to only non-anonymous
pages of all memory cgroups except ``/having_care_already``.::

    # echo 2 > nr_filters
    # # filter out anonymous pages
    echo anon > 0/type
    echo Y > 0/matching
    # # further filter out all cgroups except one at '/having_care_already'
    echo memcg > 1/type
    echo /having_care_already > 1/memcg_path
    echo N > 1/matching

Note that ``anon`` and ``memcg`` filters are currently supported only when
``paddr`` :ref:`implementation <sysfs_contexts>` is being used.

Also, memory regions that are filtered out by ``addr`` or ``target`` filters
are not counted as the scheme has tried to those, while regions that filtered
out by other type filters are counted as the scheme has tried to.  The
difference is applied to :ref:`stats <damos_stats>` and
:ref:`tried regions <sysfs_schemes_tried_regions>`.

.. _sysfs_schemes_stats:

schemes/<N>/stats/
------------------

DAMON counts the total number and bytes of regions that each scheme is tried to
be applied, the two numbers for the regions that each scheme is successfully
applied, and the total number of the quota limit exceeds.  This statistics can
be used for online analysis or tuning of the schemes.

The statistics can be retrieved by reading the files under ``stats`` directory
(``nr_tried``, ``sz_tried``, ``nr_applied``, ``sz_applied``, and
``qt_exceeds``), respectively.  The files are not updated in real time, so you
should ask DAMON sysfs interface to update the content of the files for the
stats by writing a special keyword, ``update_schemes_stats`` to the relevant
``kdamonds/<N>/state`` file.

.. _sysfs_schemes_tried_regions:

schemes/<N>/tried_regions/
--------------------------

This directory initially has one file, ``total_bytes``.

When a special keyword, ``update_schemes_tried_regions``, is written to the
relevant ``kdamonds/<N>/state`` file, DAMON updates the ``total_bytes`` file so
that reading it returns the total size of the scheme tried regions, and creates
directories named integer starting from ``0`` under this directory.  Each
directory contains files exposing detailed information about each of the memory
region that the corresponding scheme's ``action`` has tried to be applied under
this directory, during next :ref:`aggregation interval
<sysfs_monitoring_attrs>`.  The information includes address range,
``nr_accesses``, and ``age`` of the region.

Writing ``update_schemes_tried_bytes`` to the relevant ``kdamonds/<N>/state``
file will only update the ``total_bytes`` file, and will not create the
subdirectories.

The directories will be removed when another special keyword,
``clear_schemes_tried_regions``, is written to the relevant
``kdamonds/<N>/state`` file.

The expected usage of this directory is investigations of schemes' behaviors,
and query-like efficient data access monitoring results retrievals.  For the
latter use case, in particular, users can set the ``action`` as ``stat`` and
set the ``access pattern`` as their interested pattern that they want to query.

tried_regions/<N>/
------------------

In each region directory, you will find four files (``start``, ``end``,
``nr_accesses``, and ``age``).  Reading the files will show the start and end
addresses, ``nr_accesses``, and ``age`` of the region that corresponding
DAMON-based operation scheme ``action`` has tried to be applied.

Example
~~~~~~~

Below commands applies a scheme saying "If a memory region of size in [4KiB,
8KiB] is showing accesses per aggregate interval in [0, 5] for aggregate
interval in [10, 20], page out the region.  For the paging out, use only up to
10ms per second, and also don't page out more than 1GiB per second.  Under the
limitation, page out memory regions having longer age first.  Also, check the
free memory rate of the system every 5 seconds, start the monitoring and paging
out when the free memory rate becomes lower than 50%, but stop it if the free
memory rate becomes larger than 60%, or lower than 30%". ::

    # cd <sysfs>/kernel/mm/damon/admin
    # # populate directories
    # echo 1 > kdamonds/nr_kdamonds; echo 1 > kdamonds/0/contexts/nr_contexts;
    # echo 1 > kdamonds/0/contexts/0/schemes/nr_schemes
    # cd kdamonds/0/contexts/0/schemes/0
    # # set the basic access pattern and the action
    # echo 4096 > access_pattern/sz/min
    # echo 8192 > access_pattern/sz/max
    # echo 0 > access_pattern/nr_accesses/min
    # echo 5 > access_pattern/nr_accesses/max
    # echo 10 > access_pattern/age/min
    # echo 20 > access_pattern/age/max
    # echo pageout > action
    # # set quotas
    # echo 10 > quotas/ms
    # echo $((1024*1024*1024)) > quotas/bytes
    # echo 1000 > quotas/reset_interval_ms
    # # set watermark
    # echo free_mem_rate > watermarks/metric
    # echo 5000000 > watermarks/interval_us
    # echo 600 > watermarks/high
    # echo 500 > watermarks/mid
    # echo 300 > watermarks/low

Please note that it's highly recommended to use user space tools like `damo
<https://github.com/awslabs/damo>`_ rather than manually reading and writing
the files as above.  Above is only for an example.

.. _tracepoint:

Tracepoint for Monitoring Results
=================================

Users can get the monitoring results via the :ref:`tried_regions
<sysfs_schemes_tried_regions>` or a tracepoint, ``damon:damon_aggregated``.
While the tried regions directory is useful for getting a snapshot, the
tracepoint is useful for getting a full record of the results.  While the
monitoring is turned on, you could record the tracepoint events and show
results using tracepoint supporting tools like ``perf``.  For example::

    # echo on > monitor_on
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on
    # perf script
    kdamond.0 46568 [027] 79357.842179: damon:damon_aggregated: target_id=0 nr_regions=11 122509119488-135708762112: 0 864
    [...]

Each line of the perf script output represents each monitoring region.  The
first five fields are as usual other tracepoint outputs.  The sixth field
(``target_id=X``) shows the ide of the monitoring target of the region.  The
seventh field (``nr_regions=X``) shows the total number of monitoring regions
for the target.  The eighth field (``X-Y:``) shows the start (``X``) and end
(``Y``) addresses of the region in bytes.  The ninth field (``X``) shows the
``nr_accesses`` of the region (refer to
:ref:`design <damon_design_region_based_sampling>` for more details of the
counter).  Finally the tenth field (``X``) shows the ``age`` of the region
(refer to :ref:`design <damon_design_age_tracking>` for more details of the
counter).

.. _debugfs_interface:

debugfs Interface (DEPRECATED!)
===============================

.. note::

  THIS IS DEPRECATED!

  DAMON debugfs interface is deprecated, so users should move to the
  :ref:`sysfs interface <sysfs_interface>`.  If you depend on this and cannot
  move, please report your usecase to damon@lists.linux.dev and
  linux-mm@kvack.org.

DAMON exports eight files, ``attrs``, ``target_ids``, ``init_regions``,
``schemes``, ``monitor_on``, ``kdamond_pid``, ``mk_contexts`` and
``rm_contexts`` under its debugfs directory, ``<debugfs>/damon/``.


Attributes
----------

Users can get and set the ``sampling interval``, ``aggregation interval``,
``update interval``, and min/max number of monitoring target regions by
reading from and writing to the ``attrs`` file.  To know about the monitoring
attributes in detail, please refer to the :doc:`/mm/damon/design`.  For
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
as they want, by writing proper values to the ``init_regions`` file.  The input
should be a sequence of three integers separated by white spaces that represent
one region in below form.::

    <target idx> <start address> <end address>

The ``target idx`` should be the index of the target in ``target_ids`` file,
starting from ``0``, and the regions should be passed in address order.  For
example, below commands will set a couple of address ranges, ``1-100`` and
``100-200`` as the initial monitoring target region of pid 42, which is the
first one (index ``0``) in ``target_ids``, and another couple of address
ranges, ``20-40`` and ``50-100`` as that of pid 4242, which is the second one
(index ``1``) in ``target_ids``.::

    # cd <debugfs>/damon
    # cat target_ids
    42 4242
    # echo "0   1       100 \
            0   100     200 \
            1   20      40  \
            1   50      100" > init_regions

Note that this sets the initial monitoring target regions only.  In case of
virtual memory monitoring, DAMON will automatically updates the boundary of the
regions after one ``update interval``.  Therefore, users should set the
``update interval`` large enough in this case, if they don't want the
update.


Schemes
-------

Users can get and set the DAMON-based operation :ref:`schemes
<damon_design_damos>` by reading from and writing to ``schemes`` debugfs file.
Reading the file also shows the statistics of each scheme.  To the file, each
of the schemes should be represented in each line in below form::

    <target access pattern> <action> <quota> <watermarks>

You can disable schemes by simply writing an empty string to the file.

Target Access Pattern
~~~~~~~~~~~~~~~~~~~~~

The target access :ref:`pattern <damon_design_damos_access_pattern>` of the
scheme.  The ``<target access pattern>`` is constructed with three ranges in
below form::

    min-size max-size min-acc max-acc min-age max-age

Specifically, bytes for the size of regions (``min-size`` and ``max-size``),
number of monitored accesses per aggregate interval for access frequency
(``min-acc`` and ``max-acc``), number of aggregate intervals for the age of
regions (``min-age`` and ``max-age``) are specified.  Note that the ranges are
closed interval.

Action
~~~~~~

The ``<action>`` is a predefined integer for memory management :ref:`actions
<damon_design_damos_action>`.  The supported numbers and their meanings are as
below.

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``.  Ignored if
   ``target`` is ``paddr``.
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``.  Ignored if
   ``target`` is ``paddr``.
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``.
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``.  Ignored if
   ``target`` is ``paddr``.
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``.  Ignored if
   ``target`` is ``paddr``.
 - 5: Do nothing but count the statistics

Quota
~~~~~

Users can set the :ref:`quotas <damon_design_damos_quotas>` of the given scheme
via the ``<quota>`` in below form::

    <ms> <sz> <reset interval> <priority weights>

This makes DAMON to try to use only up to ``<ms>`` milliseconds for applying
the action to memory regions of the ``target access pattern`` within the
``<reset interval>`` milliseconds, and to apply the action to only up to
``<sz>`` bytes of memory regions within the ``<reset interval>``.  Setting both
``<ms>`` and ``<sz>`` zero disables the quota limits.

For the :ref:`prioritization <damon_design_damos_quotas_prioritization>`, users
can set the weights for the three properties in ``<priority weights>`` in below
form::

    <size weight> <access frequency weight> <age weight>

Watermarks
~~~~~~~~~~

Users can specify :ref:`watermarks <damon_design_damos_watermarks>` of the
given scheme via ``<watermarks>`` in below form::

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

.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below interfaces for different users.

- *DAMON user space tool.*
  `This <https://github.com/damonitor/damo>`_ is for privileged people such as
  system administrators who want a just-working human-friendly interface.
  Using this, users can use the DAMON’s major features in a human-friendly way.
  It may not be highly tuned for special cases, though.  For more detail,
  please refer to its `usage document
  <https://github.com/damonitor/damo/blob/next/USAGE.md>`_.
- *sysfs interface.*
  :ref:`This <sysfs_interface>` is for privileged user space programmers who
  want more optimized use of DAMON.  Using this, users can use DAMON’s major
  features by reading from and writing to special sysfs files.  Therefore,
  you can write and use your personalized DAMON sysfs wrapper programs that
  reads/writes the sysfs files instead of you.  The `DAMON user space tool
  <https://github.com/damonitor/damo>`_ is one example of such programs.
- *Kernel Space Programming Interface.*
  :doc:`This </mm/damon/api>` is for kernel space programmers.  Using this,
  users can utilize every feature of DAMON most flexibly and efficiently by
  writing kernel space DAMON application programs for you.  You can even extend
  DAMON for various address spaces.  For detail, please refer to the interface
  :doc:`document </mm/damon/api>`.

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
comma (",").

.. parsed-literal::

    :ref:`/sys/kernel/mm/damon <sysfs_root>`/admin
    │ :ref:`kdamonds <sysfs_kdamonds>`/nr_kdamonds
    │ │ :ref:`0 <sysfs_kdamond>`/state,pid
    │ │ │ :ref:`contexts <sysfs_contexts>`/nr_contexts
    │ │ │ │ :ref:`0 <sysfs_context>`/avail_operations,operations
    │ │ │ │ │ :ref:`monitoring_attrs <sysfs_monitoring_attrs>`/
    │ │ │ │ │ │ intervals/sample_us,aggr_us,update_us
    │ │ │ │ │ │ nr_regions/min,max
    │ │ │ │ │ :ref:`targets <sysfs_targets>`/nr_targets
    │ │ │ │ │ │ :ref:`0 <sysfs_target>`/pid_target
    │ │ │ │ │ │ │ :ref:`regions <sysfs_regions>`/nr_regions
    │ │ │ │ │ │ │ │ :ref:`0 <sysfs_region>`/start,end
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ │ :ref:`schemes <sysfs_schemes>`/nr_schemes
    │ │ │ │ │ │ :ref:`0 <sysfs_scheme>`/action,target_nid,apply_interval_us
    │ │ │ │ │ │ │ :ref:`access_pattern <sysfs_access_pattern>`/
    │ │ │ │ │ │ │ │ sz/min,max
    │ │ │ │ │ │ │ │ nr_accesses/min,max
    │ │ │ │ │ │ │ │ age/min,max
    │ │ │ │ │ │ │ :ref:`quotas <sysfs_quotas>`/ms,bytes,reset_interval_ms,effective_bytes
    │ │ │ │ │ │ │ │ weights/sz_permil,nr_accesses_permil,age_permil
    │ │ │ │ │ │ │ │ :ref:`goals <sysfs_schemes_quota_goals>`/nr_goals
    │ │ │ │ │ │ │ │ │ 0/target_metric,target_value,current_value
    │ │ │ │ │ │ │ :ref:`watermarks <sysfs_watermarks>`/metric,interval_us,high,mid,low
    │ │ │ │ │ │ │ :ref:`filters <sysfs_filters>`/nr_filters
    │ │ │ │ │ │ │ │ 0/type,matching,allow,memcg_path,addr_start,addr_end,target_idx,min,max
    │ │ │ │ │ │ │ :ref:`stats <sysfs_schemes_stats>`/nr_tried,sz_tried,nr_applied,sz_applied,sz_ops_filter_passed,qt_exceeds
    │ │ │ │ │ │ │ :ref:`tried_regions <sysfs_schemes_tried_regions>`/total_bytes
    │ │ │ │ │ │ │ │ 0/start,end,nr_accesses,age,sz_filter_passed
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ ...
    │ │ ...

.. _sysfs_root:

Root
----

The root of the DAMON sysfs interface is ``<sysfs>/kernel/mm/damon/``, and it
has one directory named ``admin``.  The directory contains the files for
privileged user space programs' control of DAMON.  User space tools or daemons
having the root permission could use this directory.

.. _sysfs_kdamonds:

kdamonds/
---------

Under the ``admin`` directory, one directory, ``kdamonds``, which has files for
controlling the kdamonds (refer to
:ref:`design <damon_design_execution_model_and_data_structures>` for more
details) exists.  In the beginning, this directory has only one file,
``nr_kdamonds``.  Writing a number (``N``) to the file creates the number of
child directories named ``0`` to ``N-1``.  Each directory represents each
kdamond.

.. _sysfs_kdamond:

kdamonds/<N>/
-------------

In each kdamond directory, two files (``state`` and ``pid``) and one directory
(``contexts``) exist.

Reading ``state`` returns ``on`` if the kdamond is currently running, or
``off`` if it is not running.

Users can write below commands for the kdamond to the ``state`` file.

- ``on``: Start running.
- ``off``: Stop running.
- ``commit``: Read the user inputs in the sysfs files except ``state`` file
  again.
- ``commit_schemes_quota_goals``: Read the DAMON-based operation schemes'
  :ref:`quota goals <sysfs_schemes_quota_goals>`.
- ``update_schemes_stats``: Update the contents of stats files for each
  DAMON-based operation scheme of the kdamond.  For details of the stats,
  please refer to :ref:`stats section <sysfs_schemes_stats>`.
- ``update_schemes_tried_regions``: Update the DAMON-based operation scheme
  action tried regions directory for each DAMON-based operation scheme of the
  kdamond.  For details of the DAMON-based operation scheme action tried
  regions directory, please refer to
  :ref:`tried_regions section <sysfs_schemes_tried_regions>`.
- ``update_schemes_tried_bytes``: Update only ``.../tried_regions/total_bytes``
  files.
- ``clear_schemes_tried_regions``: Clear the DAMON-based operating scheme
  action tried regions directory for each DAMON-based operation scheme of the
  kdamond.
- ``update_schemes_effective_quotas``: Update the contents of
  ``effective_bytes`` files for each DAMON-based operation scheme of the
  kdamond.  For more details, refer to :ref:`quotas directory <sysfs_quotas>`.

If the state is ``on``, reading ``pid`` shows the pid of the kdamond thread.

``contexts`` directory contains files for controlling the monitoring contexts
that this kdamond will execute.

.. _sysfs_contexts:

kdamonds/<N>/contexts/
----------------------

In the beginning, this directory has only one file, ``nr_contexts``.  Writing a
number (``N``) to the file creates the number of child directories named as
``0`` to ``N-1``.  Each directory represents each monitoring context (refer to
:ref:`design <damon_design_execution_model_and_data_structures>` for more
details).  At the moment, only one context per kdamond is supported, so only
``0`` or ``1`` can be written to the file.

.. _sysfs_context:

contexts/<N>/
-------------

In each context directory, two files (``avail_operations`` and ``operations``)
and three directories (``monitoring_attrs``, ``targets``, and ``schemes``)
exist.

DAMON supports multiple types of :ref:`monitoring operations
<damon_design_configurable_operations_set>`, including those for virtual address
space and the physical address space.  You can get the list of available
monitoring operations set on the currently running kernel by reading
``avail_operations`` file.  Based on the kernel configuration, the file will
list different available operation sets.  Please refer to the :ref:`design
<damon_operations_set>` for the list of all available operation sets and their
brief explanations.

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

.. _sysfs_targets:

contexts/<N>/targets/
---------------------

In the beginning, this directory has only one file, ``nr_targets``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each monitoring target.

.. _sysfs_target:

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

In case of ``fvaddr`` or ``paddr`` monitoring operations sets, users are
required to set the monitoring target address ranges.  In case of ``vaddr``
operations set, it is not mandatory, but users can optionally set the initial
monitoring region to specific address ranges.  Please refer to the :ref:`design
<damon_design_vaddr_target_regions_construction>` for more details.

For such cases, users can explicitly set the initial monitoring target regions
as they want, by writing proper values to the files under this directory.

In the beginning, this directory has only one file, ``nr_regions``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each initial monitoring target region.

.. _sysfs_region:

regions/<N>/
------------

In each region directory, you will find two files (``start`` and ``end``).  You
can set and get the start and end addresses of the initial monitoring target
region by writing to and reading from the files, respectively.

Each region should not overlap with others.  ``end`` of directory ``N`` should
be equal or smaller than ``start`` of directory ``N+1``.

.. _sysfs_schemes:

contexts/<N>/schemes/
---------------------

The directory for DAMON-based Operation Schemes (:ref:`DAMOS
<damon_design_damos>`).  Users can get and set the schemes by reading from and
writing to files under this directory.

In the beginning, this directory has only one file, ``nr_schemes``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each DAMON-based operation scheme.

.. _sysfs_scheme:

schemes/<N>/
------------

In each scheme directory, five directories (``access_pattern``, ``quotas``,
``watermarks``, ``filters``, ``stats``, and ``tried_regions``) and three files
(``action``, ``target_nid`` and ``apply_interval``) exist.

The ``action`` file is for setting and getting the scheme's :ref:`action
<damon_design_damos_action>`.  The keywords that can be written to and read
from the file and their meaning are same to those of the list on
:ref:`design doc <damon_design_damos_action>`.

The ``target_nid`` file is for setting the migration target node, which is
only meaningful when the ``action`` is either ``migrate_hot`` or
``migrate_cold``.

The ``apply_interval_us`` file is for setting and getting the scheme's
:ref:`apply_interval <damon_design_damos>` in microseconds.

.. _sysfs_access_pattern:

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

.. _sysfs_quotas:

schemes/<N>/quotas/
-------------------

The directory for the :ref:`quotas <damon_design_damos_quotas>` of the given
DAMON-based operation scheme.

Under ``quotas`` directory, four files (``ms``, ``bytes``,
``reset_interval_ms``, ``effective_bytes``) and two directores (``weights`` and
``goals``) exist.

You can set the ``time quota`` in milliseconds, ``size quota`` in bytes, and
``reset interval`` in milliseconds by writing the values to the three files,
respectively.  Then, DAMON tries to use only up to ``time quota`` milliseconds
for applying the ``action`` to memory regions of the ``access_pattern``, and to
apply the action to only up to ``bytes`` bytes of memory regions within the
``reset_interval_ms``.  Setting both ``ms`` and ``bytes`` zero disables the
quota limits unless at least one :ref:`goal <sysfs_schemes_quota_goals>` is
set.

The time quota is internally transformed to a size quota.  Between the
transformed size quota and user-specified size quota, smaller one is applied.
Based on the user-specified :ref:`goal <sysfs_schemes_quota_goals>`, the
effective size quota is further adjusted.  Reading ``effective_bytes`` returns
the current effective size quota.  The file is not updated in real time, so
users should ask DAMON sysfs interface to update the content of the file for
the stats by writing a special keyword, ``update_schemes_effective_quotas`` to
the relevant ``kdamonds/<N>/state`` file.

Under ``weights`` directory, three files (``sz_permil``,
``nr_accesses_permil``, and ``age_permil``) exist.
You can set the :ref:`prioritization weights
<damon_design_damos_quotas_prioritization>` for size, access frequency, and age
in per-thousand unit by writing the values to the three files under the
``weights`` directory.

.. _sysfs_schemes_quota_goals:

schemes/<N>/quotas/goals/
-------------------------

The directory for the :ref:`automatic quota tuning goals
<damon_design_damos_quotas_auto_tuning>` of the given DAMON-based operation
scheme.

In the beginning, this directory has only one file, ``nr_goals``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each goal and current achievement.
Among the multiple feedback, the best one is used.

Each goal directory contains three files, namely ``target_metric``,
``target_value`` and ``current_value``.  Users can set and get the three
parameters for the quota auto-tuning goals that specified on the :ref:`design
doc <damon_design_damos_quotas_auto_tuning>` by writing to and reading from each
of the files.  Note that users should further write
``commit_schemes_quota_goals`` to the ``state`` file of the :ref:`kdamond
directory <sysfs_kdamond>` to pass the feedback to DAMON.

.. _sysfs_watermarks:

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

.. _sysfs_filters:

schemes/<N>/filters/
--------------------

The directory for the :ref:`filters <damon_design_damos_filters>` of the given
DAMON-based operation scheme.

In the beginning, this directory has only one file, ``nr_filters``.  Writing a
number (``N``) to the file creates the number of child directories named ``0``
to ``N-1``.  Each directory represents each filter.  The filters are evaluated
in the numeric order.

Each filter directory contains nine files, namely ``type``, ``matching``,
``allow``, ``memcg_path``, ``addr_start``, ``addr_end``, ``min``, ``max``
and ``target_idx``.  To ``type`` file, you can write one of six special
keywords: ``anon`` for anonymous pages, ``memcg`` for specific memory cgroup,
``young`` for young pages, ``addr`` for specific address range (an open-ended
interval), ``hugepage_size`` for large folios of a specific size range [``min``,
``max``] or ``target`` for specific DAMON monitoring target filtering.  Meaning
of the types are same to the description on the :ref:`design doc
<damon_design_damos_filters>`.

In case of the memory cgroup filtering, you can specify the memory cgroup of
the interest by writing the path of the memory cgroup from the cgroups mount
point to ``memcg_path`` file.  In case of the address range filtering, you can
specify the start and end address of the range to ``addr_start`` and
``addr_end`` files, respectively.  For the DAMON monitoring target filtering,
you can specify the index of the target between the list of the DAMON context's
monitoring targets list to ``target_idx`` file.

You can write ``Y`` or ``N`` to ``matching`` file to specify whether the filter
is for memory that matches the ``type``.  You can write ``Y`` or ``N`` to
``allow`` file to specify if applying the action to the memory that satisfies
the ``type`` and ``matching`` should be allowed or not.

For example, below restricts a DAMOS action to be applied to only non-anonymous
pages of all memory cgroups except ``/having_care_already``.::

    # echo 2 > nr_filters
    # # disallow anonymous pages
    echo anon > 0/type
    echo Y > 0/matching
    echo N > 0/allow
    # # further filter out all cgroups except one at '/having_care_already'
    echo memcg > 1/type
    echo /having_care_already > 1/memcg_path
    echo Y > 1/matching
    echo N > 1/allow

Refer to the :ref:`DAMOS filters design documentation
<damon_design_damos_filters>` for more details including how multiple filters
of different ``allow`` works, when each of the filters are supported, and
differences on stats.

.. _sysfs_schemes_stats:

schemes/<N>/stats/
------------------

DAMON counts statistics for each scheme.  This statistics can be used for
online analysis or tuning of the schemes.  Refer to :ref:`design doc
<damon_design_damos_stat>` for more details about the stats.

The statistics can be retrieved by reading the files under ``stats`` directory
(``nr_tried``, ``sz_tried``, ``nr_applied``, ``sz_applied``,
``sz_ops_filter_passed``, and ``qt_exceeds``), respectively.  The files are not
updated in real time, so you should ask DAMON sysfs interface to update the
content of the files for the stats by writing a special keyword,
``update_schemes_stats`` to the relevant ``kdamonds/<N>/state`` file.

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
this directory, during next :ref:`apply interval <damon_design_damos>` of the
corresponding scheme.  The information includes address range, ``nr_accesses``,
and ``age`` of the region.

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

.. _sysfs_schemes_tried_region:

tried_regions/<N>/
------------------

In each region directory, you will find five files (``start``, ``end``,
``nr_accesses``, ``age``, and ``sz_filter_passed``).  Reading the files will
show the properties of the region that corresponding DAMON-based operation
scheme ``action`` has tried to be applied.

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
<https://github.com/damonitor/damo>`_ rather than manually reading and writing
the files as above.  Above is only for an example.

.. _tracepoint:

Tracepoints for Monitoring Results
==================================

Users can get the monitoring results via the :ref:`tried_regions
<sysfs_schemes_tried_regions>`.  The interface is useful for getting a
snapshot, but it could be inefficient for fully recording all the monitoring
results.  For the purpose, two trace points, namely ``damon:damon_aggregated``
and ``damon:damos_before_apply``, are provided.  ``damon:damon_aggregated``
provides the whole monitoring results, while ``damon:damos_before_apply``
provides the monitoring results for regions that each DAMON-based Operation
Scheme (:ref:`DAMOS <damon_design_damos>`) is gonna be applied.  Hence,
``damon:damos_before_apply`` is more useful for recording internal behavior of
DAMOS, or DAMOS target access
:ref:`pattern <damon_design_damos_access_pattern>` based query-like efficient
monitoring results recording.

While the monitoring is turned on, you could record the tracepoint events and
show results using tracepoint supporting tools like ``perf``.  For example::

    # echo on > kdamonds/0/state
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > kdamonds/0/state
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

If the event was ``damon:damos_beofre_apply``, the ``perf script`` output would
be somewhat like below::

    kdamond.0 47293 [000] 80801.060214: damon:damos_before_apply: ctx_idx=0 scheme_idx=0 target_idx=0 nr_regions=11 121932607488-135128711168: 0 136
    [...]

Each line of the output represents each monitoring region that each DAMON-based
Operation Scheme was about to be applied at the traced time.  The first five
fields are as usual.  It shows the index of the DAMON context (``ctx_idx=X``)
of the scheme in the list of the contexts of the context's kdamond, the index
of the scheme (``scheme_idx=X``) in the list of the schemes of the context, in
addition to the output of ``damon_aggregated`` tracepoint.

.. SPDX-License-Identifier: GPL-2.0

======
Design
======


.. _damon_design_execution_model_and_data_structures:

Execution Model and Data Structures
===================================

The monitoring-related information including the monitoring request
specification and DAMON-based operation schemes are stored in a data structure
called DAMON ``context``.  DAMON executes each context with a kernel thread
called ``kdamond``.  Multiple kdamonds could run in parallel, for different
types of monitoring.

To know how user-space can do the configurations and start/stop DAMON, refer to
:ref:`DAMON sysfs interface <sysfs_interface>` documentation.


Overall Architecture
====================

DAMON subsystem is configured with three layers including

- :ref:`Operations Set <damon_operations_set>`: Implements fundamental
  operations for DAMON that depends on the given monitoring target
  address-space and available set of software/hardware primitives,
- :ref:`Core <damon_core_logic>`: Implements core logics including monitoring
  overhead/accuracy control and access-aware system operations on top of the
  operations set layer, and
- :ref:`Modules <damon_modules>`: Implements kernel modules for various
  purposes that provides interfaces for the user space, on top of the core
  layer.


.. _damon_operations_set:

Operations Set Layer
====================

.. _damon_design_configurable_operations_set:

For data access monitoring and additional low level work, DAMON needs a set of
implementations for specific operations that are dependent on and optimized for
the given target address space.  For example, below two operations for access
monitoring are address-space dependent.

1. Identification of the monitoring target address range for the address space.
2. Access check of specific address range in the target space.

DAMON consolidates these implementations in a layer called DAMON Operations
Set, and defines the interface between it and the upper layer.  The upper layer
is dedicated for DAMON's core logics including the mechanism for control of the
monitoring accruracy and the overhead.

Hence, DAMON can easily be extended for any address space and/or available
hardware features by configuring the core logic to use the appropriate
operations set.  If there is no available operations set for a given purpose, a
new operations set can be implemented following the interface between the
layers.

For example, physical memory, virtual memory, swap space, those for specific
processes, NUMA nodes, files, and backing memory devices would be supportable.
Also, if some architectures or devices support special optimized access check
features, those will be easily configurable.

DAMON currently provides below three operation sets.  Below two subsections
describe how those work.

 - vaddr: Monitor virtual address spaces of specific processes
 - fvaddr: Monitor fixed virtual address ranges
 - paddr: Monitor the physical address space of the system

To know how user-space can do the configuration via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`operations <sysfs_context>` file part of the
documentation.


 .. _damon_design_vaddr_target_regions_construction:

VMA-based Target Address Range Construction
-------------------------------------------

A mechanism of ``vaddr`` DAMON operations set that automatically initializes
and updates the monitoring target address regions so that entire memory
mappings of the target processes can be covered.

This mechanism is only for the ``vaddr`` operations set.  In cases of
``fvaddr`` and ``paddr`` operation sets, users are asked to manually set the
monitoring target address ranges.

Only small parts in the super-huge virtual address space of the processes are
mapped to the physical memory and accessed.  Thus, tracking the unmapped
address regions is just wasteful.  However, because DAMON can deal with some
level of noise using the adaptive regions adjustment mechanism, tracking every
mapping is not strictly required but could even incur a high overhead in some
cases.  That said, too huge unmapped areas inside the monitoring target should
be removed to not take the time for the adaptive mechanism.

For the reason, this implementation converts the complex mappings to three
distinct regions that cover every mapped area of the address space.  The two
gaps between the three regions are the two biggest unmapped areas in the given
address space.  The two biggest unmapped areas would be the gap between the
heap and the uppermost mmap()-ed region, and the gap between the lowermost
mmap()-ed region and the stack in most of the cases.  Because these gaps are
exceptionally huge in usual address spaces, excluding these will be sufficient
to make a reasonable trade-off.  Below shows this in detail::

    <heap>
    <BIG UNMAPPED REGION 1>
    <uppermost mmap()-ed region>
    (small mmap()-ed regions and munmap()-ed regions)
    <lowermost mmap()-ed region>
    <BIG UNMAPPED REGION 2>
    <stack>


PTE Accessed-bit Based Access Check
-----------------------------------

Both of the implementations for physical and virtual address spaces use PTE
Accessed-bit for basic access checks.  Only one difference is the way of
finding the relevant PTE Accessed bit(s) from the address.  While the
implementation for the virtual address walks the page table for the target task
of the address, the implementation for the physical address walks every page
table having a mapping to the address.  In this way, the implementations find
and clear the bit(s) for next sampling target address and checks whether the
bit(s) set again after one sampling period.  This could disturb other kernel
subsystems using the Accessed bits, namely Idle page tracking and the reclaim
logic.  DAMON does nothing to avoid disturbing Idle page tracking, so handling
the interference is the responsibility of sysadmins.  However, it solves the
conflict with the reclaim logic using ``PG_idle`` and ``PG_young`` page flags,
as Idle page tracking does.


.. _damon_core_logic:

Core Logics
===========

.. _damon_design_monitoring:

Monitoring
----------

Below four sections describe each of the DAMON core mechanisms and the five
monitoring attributes, ``sampling interval``, ``aggregation interval``,
``update interval``, ``minimum number of regions``, and ``maximum number of
regions``.

To know how user-space can set the attributes via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`monitoring_attrs <sysfs_monitoring_attrs>`
part of the documentation.


Access Frequency Monitoring
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The output of DAMON says what pages are how frequently accessed for a given
duration.  The resolution of the access frequency is controlled by setting
``sampling interval`` and ``aggregation interval``.  In detail, DAMON checks
access to each page per ``sampling interval`` and aggregates the results.  In
other words, counts the number of the accesses to each page.  After each
``aggregation interval`` passes, DAMON calls callback functions that previously
registered by users so that users can read the aggregated results and then
clears the results.  This can be described in below simple pseudo-code::

    while monitoring_on:
        for page in monitoring_target:
            if accessed(page):
                nr_accesses[page] += 1
        if time() % aggregation_interval == 0:
            for callback in user_registered_callbacks:
                callback(monitoring_target, nr_accesses)
            for page in monitoring_target:
                nr_accesses[page] = 0
        sleep(sampling interval)

The monitoring overhead of this mechanism will arbitrarily increase as the
size of the target workload grows.


.. _damon_design_region_based_sampling:

Region Based Sampling
~~~~~~~~~~~~~~~~~~~~~

To avoid the unbounded increase of the overhead, DAMON groups adjacent pages
that assumed to have the same access frequencies into a region.  As long as the
assumption (pages in a region have the same access frequencies) is kept, only
one page in the region is required to be checked.  Thus, for each ``sampling
interval``, DAMON randomly picks one page in each region, waits for one
``sampling interval``, checks whether the page is accessed meanwhile, and
increases the access frequency counter of the region if so.  The counter is
called ``nr_accesses`` of the region.  Therefore, the monitoring overhead is
controllable by setting the number of regions.  DAMON allows users to set the
minimum and the maximum number of regions for the trade-off.

This scheme, however, cannot preserve the quality of the output if the
assumption is not guaranteed.


.. _damon_design_adaptive_regions_adjustment:

Adaptive Regions Adjustment
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Even somehow the initial monitoring target regions are well constructed to
fulfill the assumption (pages in same region have similar access frequencies),
the data access pattern can be dynamically changed.  This will result in low
monitoring quality.  To keep the assumption as much as possible, DAMON
adaptively merges and splits each region based on their access frequency.

For each ``aggregation interval``, it compares the access frequencies
(``nr_accesses``) of adjacent regions.  If the difference is small, and if the
sum of the two regions' sizes is smaller than the size of total regions divided
by the ``minimum number of regions``, DAMON merges the two regions.  If the
resulting number of total regions is still higher than ``maximum number of
regions``, it repeats the merging with increasing access frequenceis difference
threshold until the upper-limit of the number of regions is met, or the
threshold becomes higher than possible maximum value (``aggregation interval``
divided by ``sampling interval``).   Then, after it reports and clears the
aggregated access frequency of each region, it splits each region into two or
three regions if the total number of regions will not exceed the user-specified
maximum number of regions after the split.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


.. _damon_design_age_tracking:

Age Tracking
~~~~~~~~~~~~

By analyzing the monitoring results, users can also find how long the current
access pattern of a region has maintained.  That could be used for good
understanding of the access pattern.  For example, page placement algorithm
utilizing both the frequency and the recency could be implemented using that.
To make such access pattern maintained period analysis easier, DAMON maintains
yet another counter called ``age`` in each region.  For each ``aggregation
interval``, DAMON checks if the region's size and access frequency
(``nr_accesses``) has significantly changed.  If so, the counter is reset to
zero.  Otherwise, the counter is increased.


Dynamic Target Space Updates Handling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The monitoring target address range could dynamically changed.  For example,
virtual memory could be dynamically mapped and unmapped.  Physical memory could
be hot-plugged.

As the changes could be quite frequent in some cases, DAMON allows the
monitoring operations to check dynamic changes including memory mapping changes
and applies it to monitoring operations-related data structures such as the
abstracted monitoring target memory area only for each of a user-specified time
interval (``update interval``).

User-space can get the monitoring results via DAMON sysfs interface and/or
tracepoints.  For more details, please refer to the documentations for
:ref:`DAMOS tried regions <sysfs_schemes_tried_regions>` and :ref:`tracepoint`,
respectively.


.. _damon_design_monitoring_params_tuning_guide:

Monitoring Parameters Tuning Guide
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In short, set ``aggregation interval`` to capture meaningful amount of accesses
for the purpose.  The amount of accesses can be measured using ``nr_accesses``
and ``age`` of regions in the aggregated monitoring results snapshot.  The
default value of the interval, ``100ms``, turns out to be too short in many
cases.  Set ``sampling interval`` proportional to ``aggregation interval``.  By
default, ``1/20`` is recommended as the ratio.

``Aggregation interval`` should be set as the time interval that the workload
can make an amount of accesses for the monitoring purpose, within the interval.
If the interval is too short, only small number of accesses are captured.  As a
result, the monitoring results look everything is samely accessed only rarely.
For many purposes, that would be useless.  If it is too long, however, the time
to converge regions with the :ref:`regions adjustment mechanism
<damon_design_adaptive_regions_adjustment>` can be too long, depending on the
time scale of the given purpose.  This could happen if the workload is actually
making only rare accesses but the user thinks the amount of accesses for the
monitoring purpose too high.  For such cases, the target amount of access to
capture per ``aggregation interval`` should carefully reconsidered.  Also, note
that the captured amount of accesses is represented with not only
``nr_accesses``, but also ``age``.  For example, even if every region on the
monitoring results show zero ``nr_accesses``, regions could still be
distinguished using ``age`` values as the recency information.

Hence the optimum value of ``aggregation interval`` depends on the access
intensiveness of the workload.  The user should tune the interval based on the
amount of access that captured on each aggregated snapshot of the monitoring
results.

Note that the default value of the interval is 100 milliseconds, which is too
short in many cases, especially on large systems.

``Sampling interval`` defines the resolution of each aggregation.  If it is set
too large, monitoring results will look like every region was samely rarely
accessed, or samely frequently accessed.  That is, regions become
undistinguishable based on access pattern, and therefore the results will be
useless in many use cases.  If ``sampling interval`` is too small, it will not
degrade the resolution, but will increase the monitoring overhead.  If it is
appropriate enough to provide a resolution of the monitoring results that
sufficient for the given purpose, it shouldn't be unnecessarily further
lowered.  It is recommended to be set proportional to ``aggregation interval``.
By default, the ratio is set as ``1/20``, and it is still recommended.

Refer to below documents for an example tuning based on the above guide.

.. toctree::
   :maxdepth: 1

   monitoring_intervals_tuning_example


.. _damon_design_damos:

Operation Schemes
-----------------

One common purpose of data access monitoring is access-aware system efficiency
optimizations.  For example,

    paging out memory regions that are not accessed for more than two minutes

or

    using THP for memory regions that are larger than 2 MiB and showing a high
    access frequency for more than one minute.

One straightforward approach for such schemes would be profile-guided
optimizations.  That is, getting data access monitoring results of the
workloads or the system using DAMON, finding memory regions of special
characteristics by profiling the monitoring results, and making system
operation changes for the regions.  The changes could be made by modifying or
providing advice to the software (the application and/or the kernel), or
reconfiguring the hardware.  Both offline and online approaches could be
available.

Among those, providing advice to the kernel at runtime would be flexible and
effective, and therefore widely be used.   However, implementing such schemes
could impose unnecessary redundancy and inefficiency.  The profiling could be
redundant if the type of interest is common.  Exchanging the information
including monitoring results and operation advice between kernel and user
spaces could be inefficient.

To allow users to reduce such redundancy and inefficiencies by offloading the
works, DAMON provides a feature called Data Access Monitoring-based Operation
Schemes (DAMOS).  It lets users specify their desired schemes at a high
level.  For such specifications, DAMON starts monitoring, finds regions having
the access pattern of interest, and applies the user-desired operation actions
to the regions, for every user-specified time interval called
``apply_interval``.

To know how user-space can set ``apply_interval`` via :ref:`DAMON sysfs
interface <sysfs_interface>`, refer to :ref:`apply_interval_us <sysfs_scheme>`
part of the documentation.


.. _damon_design_damos_action:

Operation Action
~~~~~~~~~~~~~~~~

The management action that the users desire to apply to the regions of their
interest.  For example, paging out, prioritizing for next reclamation victim
selection, advising ``khugepaged`` to collapse or split, or doing nothing but
collecting statistics of the regions.

The list of supported actions is defined in DAMOS, but the implementation of
each action is in the DAMON operations set layer because the implementation
normally depends on the monitoring target address space.  For example, the code
for paging specific virtual address ranges out would be different from that for
physical address ranges.  And the monitoring operations implementation sets are
not mandated to support all actions of the list.  Hence, the availability of
specific DAMOS action depends on what operations set is selected to be used
together.

The list of the supported actions, their meaning, and DAMON operations sets
that supports each action are as below.

 - ``willneed``: Call ``madvise()`` for the region with ``MADV_WILLNEED``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``cold``: Call ``madvise()`` for the region with ``MADV_COLD``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``pageout``: Reclaim the region.
   Supported by ``vaddr``, ``fvaddr`` and ``paddr`` operations set.
 - ``hugepage``: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``nohugepage``: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``.
   Supported by ``vaddr`` and ``fvaddr`` operations set.
 - ``lru_prio``: Prioritize the region on its LRU lists.
   Supported by ``paddr`` operations set.
 - ``lru_deprio``: Deprioritize the region on its LRU lists.
   Supported by ``paddr`` operations set.
 - ``migrate_hot``: Migrate the regions prioritizing warmer regions.
   Supported by ``paddr`` operations set.
 - ``migrate_cold``: Migrate the regions prioritizing colder regions.
   Supported by ``paddr`` operations set.
 - ``stat``: Do nothing but count the statistics.
   Supported by all operations sets.

Applying the actions except ``stat`` to a region is considered as changing the
region's characteristics.  Hence, DAMOS resets the age of regions when any such
actions are applied to those.

To know how user-space can set the action via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`action <sysfs_scheme>` part of the
documentation.


.. _damon_design_damos_access_pattern:

Target Access Pattern
~~~~~~~~~~~~~~~~~~~~~

The access pattern of the schemes' interest.  The patterns are constructed with
the properties that DAMON's monitoring results provide, specifically the size,
the access frequency, and the age.  Users can describe their access pattern of
interest by setting minimum and maximum values of the three properties.  If a
region's three properties are in the ranges, DAMOS classifies it as one of the
regions that the scheme is having an interest in.

To know how user-space can set the access pattern via :ref:`DAMON sysfs
interface <sysfs_interface>`, refer to :ref:`access_pattern
<sysfs_access_pattern>` part of the documentation.


.. _damon_design_damos_quotas:

Quotas
~~~~~~

DAMOS upper-bound overhead control feature.  DAMOS could incur high overhead if
the target access pattern is not properly tuned.  For example, if a huge memory
region having the access pattern of interest is found, applying the scheme's
action to all pages of the huge region could consume unacceptably large system
resources.  Preventing such issues by tuning the access pattern could be
challenging, especially if the access patterns of the workloads are highly
dynamic.

To mitigate that situation, DAMOS provides an upper-bound overhead control
feature called quotas.  It lets users specify an upper limit of time that DAMOS
can use for applying the action, and/or a maximum bytes of memory regions that
the action can be applied within a user-specified time duration.

To know how user-space can set the basic quotas via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`quotas <sysfs_quotas>` part of the
documentation.


.. _damon_design_damos_quotas_prioritization:

Prioritization
^^^^^^^^^^^^^^

A mechanism for making a good decision under the quotas.  When the action
cannot be applied to all regions of interest due to the quotas, DAMOS
prioritizes regions and applies the action to only regions having high enough
priorities so that it will not exceed the quotas.

The prioritization mechanism should be different for each action.  For example,
rarely accessed (colder) memory regions would be prioritized for page-out
scheme action.  In contrast, the colder regions would be deprioritized for huge
page collapse scheme action.  Hence, the prioritization mechanisms for each
action are implemented in each DAMON operations set, together with the actions.

Though the implementation is up to the DAMON operations set, it would be common
to calculate the priority using the access pattern properties of the regions.
Some users would want the mechanisms to be personalized for their specific
case.  For example, some users would want the mechanism to weigh the recency
(``age``) more than the access frequency (``nr_accesses``).  DAMOS allows users
to specify the weight of each access pattern property and passes the
information to the underlying mechanism.  Nevertheless, how and even whether
the weight will be respected are up to the underlying prioritization mechanism
implementation.

To know how user-space can set the prioritization weights via :ref:`DAMON sysfs
interface <sysfs_interface>`, refer to :ref:`weights <sysfs_quotas>` part of
the documentation.


.. _damon_design_damos_quotas_auto_tuning:

Aim-oriented Feedback-driven Auto-tuning
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Automatic feedback-driven quota tuning.  Instead of setting the absolute quota
value, users can specify the metric of their interest, and what target value
they want the metric value to be.  DAMOS then automatically tunes the
aggressiveness (the quota) of the corresponding scheme.  For example, if DAMOS
is under achieving the goal, DAMOS automatically increases the quota.  If DAMOS
is over achieving the goal, it decreases the quota.

The goal can be specified with three parameters, namely ``target_metric``,
``target_value``, and ``current_value``.  The auto-tuning mechanism tries to
make ``current_value`` of ``target_metric`` be same to ``target_value``.
Currently, two ``target_metric`` are provided.

- ``user_input``: User-provided value.  Users could use any metric that they
  has interest in for the value.  Use space main workload's latency or
  throughput, system metrics like free memory ratio or memory pressure stall
  time (PSI) could be examples.  Note that users should explicitly set
  ``current_value`` on their own in this case.  In other words, users should
  repeatedly provide the feedback.
- ``some_mem_psi_us``: System-wide ``some`` memory pressure stall information
  in microseconds that measured from last quota reset to next quota reset.
  DAMOS does the measurement on its own, so only ``target_value`` need to be
  set by users at the initial time.  In other words, DAMOS does self-feedback.

To know how user-space can set the tuning goal metric, the target value, and/or
the current value via :ref:`DAMON sysfs interface <sysfs_interface>`, refer to
:ref:`quota goals <sysfs_schemes_quota_goals>` part of the documentation.


.. _damon_design_damos_watermarks:

Watermarks
~~~~~~~~~~

Conditional DAMOS (de)activation automation.  Users might want DAMOS to run
only under certain situations.  For example, when a sufficient amount of free
memory is guaranteed, running a scheme for proactive reclamation would only
consume unnecessary system resources.  To avoid such consumption, the user would
need to manually monitor some metrics such as free memory ratio, and turn
DAMON/DAMOS on or off.

DAMOS allows users to offload such works using three watermarks.  It allows the
users to configure the metric of their interest, and three watermark values,
namely high, middle, and low.  If the value of the metric becomes above the
high watermark or below the low watermark, the scheme is deactivated.  If the
metric becomes below the mid watermark but above the low watermark, the scheme
is activated.  If all schemes are deactivated by the watermarks, the monitoring
is also deactivated.  In this case, the DAMON worker thread only periodically
checks the watermarks and therefore incurs nearly zero overhead.

To know how user-space can set the watermarks via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`watermarks <sysfs_watermarks>` part of the
documentation.


.. _damon_design_damos_filters:

Filters
~~~~~~~

Non-access pattern-based target memory regions filtering.  If users run
self-written programs or have good profiling tools, they could know something
more than the kernel, such as future access patterns or some special
requirements for specific types of memory. For example, some users may know
only anonymous pages can impact their program's performance.  They can also
have a list of latency-critical processes.

To let users optimize DAMOS schemes with such special knowledge, DAMOS provides
a feature called DAMOS filters.  The feature allows users to set an arbitrary
number of filters for each scheme.  Each filter specifies

- a type of memory (``type``),
- whether it is for the memory of the type or all except the type
  (``matching``), and
- whether it is to allow (include) or reject (exclude) applying
  the scheme's action to the memory (``allow``).

For efficient handling of filters, some types of filters are handled by the
core layer, while others are handled by operations set.  In the latter case,
hence, support of the filter types depends on the DAMON operations set.  In
case of the core layer-handled filters, the memory regions that excluded by the
filter are not counted as the scheme has tried to the region.  In contrast, if
a memory regions is filtered by an operations set layer-handled filter, it is
counted as the scheme has tried.  This difference affects the statistics.

When multiple filters are installed, the group of filters that handled by the
core layer are evaluated first.  After that, the group of filters that handled
by the operations layer are evaluated.  Filters in each of the groups are
evaluated in the installed order.  If a part of memory is matched to one of the
filter, next filters are ignored.  If the memory passes through the filters
evaluation stage because it is not matched to any of the filters, applying the
scheme's action to it is allowed, same to the behavior when no filter exists.

For example, let's assume 1) a filter for allowing anonymous pages and 2)
another filter for rejecting young pages are installed in the order.  If a page
of a region that eligible to apply the scheme's action is an anonymous page,
the scheme's action will be applied to the page regardless of whether it is
young or not, since it matches with the first allow-filter.  If the page is
not anonymous but young, the scheme's action will not be applied, since the
second reject-filter blocks it.  If the page is neither anonymous nor young,
the page will pass through the filters evaluation stage since there is no
matching filter, and the action will be applied to the page.

Note that the action can equally be applied to memory that either explicitly
filter-allowed or filters evaluation stage passed.  It means that installing
allow-filters at the end of the list makes no practical change but only
filters-checking overhead.

Below ``type`` of filters are currently supported.

- Core layer handled
    - addr
        - Applied to pages that belonging to a given address range.
    - target
        - Applied to pages that belonging to a given DAMON monitoring target.
- Operations layer handled, supported by only ``paddr`` operations set.
    - anon
        - Applied to pages that containing data that not stored in files.
    - memcg
        - Applied to pages that belonging to a given cgroup.
    - young
        - Applied to pages that are accessed after the last access check from the
          scheme.
    - hugepage_size
        - Applied to pages that managed in a given size range.
    - unmapped
        - Applied to pages that unmapped.

To know how user-space can set the filters via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:`filters <sysfs_filters>` part of the
documentation.

.. _damon_design_damos_stat:

Statistics
~~~~~~~~~~

The statistics of DAMOS behaviors that designed to help monitoring, tuning and
debugging of DAMOS.

DAMOS accounts below statistics for each scheme, from the beginning of the
scheme's execution.

- ``nr_tried``: Total number of regions that the scheme is tried to be applied.
- ``sz_trtied``: Total size of regions that the scheme is tried to be applied.
- ``sz_ops_filter_passed``: Total bytes that passed operations set
  layer-handled DAMOS filters.
- ``nr_applied``: Total number of regions that the scheme is applied.
- ``sz_applied``: Total size of regions that the scheme is applied.
- ``qt_exceeds``: Total number of times the quota of the scheme has exceeded.

"A scheme is tried to be applied to a region" means DAMOS core logic determined
the region is eligible to apply the scheme's :ref:`action
<damon_design_damos_action>`.  The :ref:`access pattern
<damon_design_damos_access_pattern>`, :ref:`quotas
<damon_design_damos_quotas>`, :ref:`watermarks
<damon_design_damos_watermarks>`, and :ref:`filters
<damon_design_damos_filters>` that handled on core logic could affect this.
The core logic will only ask the underlying :ref:`operation set
<damon_operations_set>` to do apply the action to the region, so whether the
action is really applied or not is unclear.  That's why it is called "tried".

"A scheme is applied to a region" means the :ref:`operation set
<damon_operations_set>` has applied the action to at least a part of the
region.  The :ref:`filters <damon_design_damos_filters>` that handled by the
operation set, and the types of the :ref:`action <damon_design_damos_action>`
and the pages of the region can affect this.  For example, if a filter is set
to exclude anonymous pages and the region has only anonymous pages, or if the
action is ``pageout`` while all pages of the region are unreclaimable, applying
the action to the region will fail.

To know how user-space can read the stats via :ref:`DAMON sysfs interface
<sysfs_interface>`, refer to :ref:s`stats <sysfs_stats>` part of the
documentation.

Regions Walking
~~~~~~~~~~~~~~~

DAMOS feature allowing users access each region that a DAMOS action has just
applied.  Using this feature, DAMON :ref:`API <damon_design_api>` allows users
access full properties of the regions including the access monitoring results
and amount of the region's internal memory that passed the DAMOS filters.
:ref:`DAMON sysfs interface <sysfs_interface>` also allows users read the data
via special :ref:`files <sysfs_schemes_tried_regions>`.

.. _damon_design_api:

Application Programming Interface
---------------------------------

The programming interface for kernel space data access-aware applications.
DAMON is a framework, so it does nothing by itself.  Instead, it only helps
other kernel components such as subsystems and modules building their data
access-aware applications using DAMON's core features.  For this, DAMON exposes
its all features to other kernel components via its application programming
interface, namely ``include/linux/damon.h``.  Please refer to the API
:doc:`document </mm/damon/api>` for details of the interface.


.. _damon_modules:

Modules
=======

Because the core of DAMON is a framework for kernel components, it doesn't
provide any direct interface for the user space.  Such interfaces should be
implemented by each DAMON API user kernel components, instead.  DAMON subsystem
itself implements such DAMON API user modules, which are supposed to be used
for general purpose DAMON control and special purpose data access-aware system
operations, and provides stable application binary interfaces (ABI) for the
user space.  The user space can build their efficient data access-aware
applications using the interfaces.


General Purpose User Interface Modules
--------------------------------------

DAMON modules that provide user space ABIs for general purpose DAMON usage in
runtime.

Like many other ABIs, the modules create files on pseudo file systems like
'sysfs', allow users to specify their requests to and get the answers from
DAMON by writing to and reading from the files.  As a response to such I/O,
DAMON user interface modules control DAMON and retrieve the results as user
requested via the DAMON API, and return the results to the user-space.

The ABIs are designed to be used for user space applications development,
rather than human beings' fingers.  Human users are recommended to use such
user space tools.  One such Python-written user space tool is available at
Github (https://github.com/damonitor/damo), Pypi
(https://pypistats.org/packages/damo), and Fedora
(https://packages.fedoraproject.org/pkgs/python-damo/damo/).

Currently, one module for this type, namely 'DAMON sysfs interface' is
available.  Please refer to the ABI :ref:`doc <sysfs_interface>` for details of
the interfaces.


Special-Purpose Access-aware Kernel Modules
-------------------------------------------

DAMON modules that provide user space ABI for specific purpose DAMON usage.

DAMON user interface modules are for full control of all DAMON features in
runtime.  For each special-purpose system-wide data access-aware system
operations such as proactive reclamation or LRU lists balancing, the interfaces
could be simplified by removing unnecessary knobs for the specific purpose, and
extended for boot-time and even compile time control.  Default values of DAMON
control parameters for the usage would also need to be optimized for the
purpose.

To support such cases, yet more DAMON API user kernel modules that provide more
simple and optimized user space interfaces are available.  Currently, two
modules for proactive reclamation and LRU lists manipulation are provided.  For
more detail, please read the usage documents for those
(:doc:`/admin-guide/mm/damon/reclaim` and
:doc:`/admin-guide/mm/damon/lru_sort`).

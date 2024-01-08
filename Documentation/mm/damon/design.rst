.. SPDX-License-Identifier: GPL-2.0

======
Design
======


Overall Architecture
====================

DAMON subsystem is configured with three layers including

- Operations Set: Implements fundamental operations for DAMON that depends on
  the given monitoring target address-space and available set of
  software/hardware primitives,
- Core: Implements core logics including monitoring overhead/accurach control
  and access-aware system operations on top of the operations set layer, and
- Modules: Implements kernel modules for various purposes that provides
  interfaces for the user space, on top of the core layer.


Configurable Operations Set
---------------------------

For data access monitoring and additional low level work, DAMON needs a set of
implementations for specific operations that are dependent on and optimized for
the given target address space.  On the other hand, the accuracy and overhead
tradeoff mechanism, which is the core logic of DAMON, is in the pure logic
space.  DAMON separates the two parts in different layers, namely DAMON
Operations Set and DAMON Core Logics Layers, respectively.  It further defines
the interface between the layers to allow various operations sets to be
configured with the core logic.

Due to this design, users can extend DAMON for any address space by configuring
the core logic to use the appropriate operations set.  If any appropriate set
is unavailable, users can implement one on their own.

For example, physical memory, virtual memory, swap space, those for specific
processes, NUMA nodes, files, and backing memory devices would be supportable.
Also, if some architectures or devices supporting special optimized access
check primitives, those will be easily configurable.


Programmable Modules
--------------------

Core layer of DAMON is implemented as a framework, and exposes its application
programming interface to all kernel space components such as subsystems and
modules.  For common use cases of DAMON, DAMON subsystem provides kernel
modules that built on top of the core layer using the API, which can be easily
used by the user space end users.


Operations Set Layer
====================

The monitoring operations are defined in two parts:

1. Identification of the monitoring target address range for the address space.
2. Access check of specific address range in the target space.

DAMON currently provides the implementations of the operations for the physical
and virtual address spaces. Below two subsections describe how those work.


VMA-based Target Address Range Construction
-------------------------------------------

This is only for the virtual address space monitoring operations
implementation.  That for the physical address space simply asks users to
manually set the monitoring target address ranges.

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


Core Logics
===========


Monitoring
----------

Below four sections describe each of the DAMON core mechanisms and the five
monitoring attributes, ``sampling interval``, ``aggregation interval``,
``update interval``, ``minimum number of regions``, and ``maximum number of
regions``.


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
called ``nr_regions`` of the region.  Therefore, the monitoring overhead is
controllable by setting the number of regions.  DAMON allows users to set the
minimum and the maximum number of regions for the trade-off.

This scheme, however, cannot preserve the quality of the output if the
assumption is not guaranteed.


Adaptive Regions Adjustment
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Even somehow the initial monitoring target regions are well constructed to
fulfill the assumption (pages in same region have similar access frequencies),
the data access pattern can be dynamically changed.  This will result in low
monitoring quality.  To keep the assumption as much as possible, DAMON
adaptively merges and splits each region based on their access frequency.

For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges those if the frequency difference is small.  Then,
after it reports and clears the aggregated access frequency of each region, it
splits each region into two or three regions if the total number of regions
will not exceed the user-specified maximum number of regions after the split.

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

Applying an action to a region is considered as changing the region's
characteristics.  Hence, DAMOS resets the age of regions when an action is
applied to those.


.. _damon_design_damos_access_pattern:

Target Access Pattern
~~~~~~~~~~~~~~~~~~~~~

The access pattern of the schemes' interest.  The patterns are constructed with
the properties that DAMON's monitoring results provide, specifically the size,
the access frequency, and the age.  Users can describe their access pattern of
interest by setting minimum and maximum values of the three properties.  If a
region's three properties are in the ranges, DAMOS classifies it as one of the
regions that the scheme is having an interest in.


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
number of filters for each scheme.  Each filter specifies the type of target
memory, and whether it should exclude the memory of the type (filter-out), or
all except the memory of the type (filter-in).

Currently, anonymous page, memory cgroup, address range, and DAMON monitoring
target type filters are supported by the feature.  Some filter target types
require additional arguments.  The memory cgroup filter type asks users to
specify the file path of the memory cgroup for the filter.  The address range
type asks the start and end addresses of the range.  The DAMON monitoring
target type asks the index of the target from the context's monitoring targets
list.  Hence, users can apply specific schemes to only anonymous pages,
non-anonymous pages, pages of specific cgroups, all pages excluding those of
specific cgroups, pages in specific address range, pages in specific DAMON
monitoring targets, and any combination of those.

To handle filters efficiently, the address range and DAMON monitoring target
type filters are handled by the core layer, while others are handled by
operations set.  If a memory region is filtered by a core layer-handled filter,
it is not counted as the scheme has tried to the region.  In contrast, if a
memory regions is filtered by an operations set layer-handled filter, it is
counted as the scheme has tried.  The difference in accounting leads to changes
in the statistics.


Application Programming Interface
---------------------------------

The programming interface for kernel space data access-aware applications.
DAMON is a framework, so it does nothing by itself.  Instead, it only helps
other kernel components such as subsystems and modules building their data
access-aware applications using DAMON's core features.  For this, DAMON exposes
its all features to other kernel components via its application programming
interface, namely ``include/linux/damon.h``.  Please refer to the API
:doc:`document </mm/damon/api>` for details of the interface.


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

DAMON user interface modules, namely 'DAMON sysfs interface' and 'DAMON debugfs
interface' are DAMON API user kernel modules that provide ABIs to the
user-space.  Please note that DAMON debugfs interface is currently deprecated.

Like many other ABIs, the modules create files on sysfs and debugfs, allow
users to specify their requests to and get the answers from DAMON by writing to
and reading from the files.  As a response to such I/O, DAMON user interface
modules control DAMON and retrieve the results as user requested via the DAMON
API, and return the results to the user-space.

The ABIs are designed to be used for user space applications development,
rather than human beings' fingers.  Human users are recommended to use such
user space tools.  One such Python-written user space tool is available at
Github (https://github.com/awslabs/damo), Pypi
(https://pypistats.org/packages/damo), and Fedora
(https://packages.fedoraproject.org/pkgs/python-damo/damo/).

Please refer to the ABI :doc:`document </admin-guide/mm/damon/usage>` for
details of the interfaces.


Special-Purpose Access-aware Kernel Modules
-------------------------------------------

DAMON modules that provide user space ABI for specific purpose DAMON usage.

DAMON sysfs/debugfs user interfaces are for full control of all DAMON features
in runtime.  For each special-purpose system-wide data access-aware system
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


.. _damon_design_execution_model_and_data_structures:

Execution Model and Data Structures
===================================

The monitoring-related information including the monitoring request
specification and DAMON-based operation schemes are stored in a data structure
called DAMON ``context``.  DAMON executes each context with a kernel thread
called ``kdamond``.  Multiple kdamonds could run in parallel, for different
types of monitoring.

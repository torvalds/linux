====================================================================
Marvell Odyssey LLC-TAD Performance Monitoring Unit (PMU UNCORE)
====================================================================

Each TAD provides eight 64-bit counters for monitoring
cache behavior.The driver always configures the same counter for
all the TADs. The user would end up effectively reserving one of
eight counters in every TAD to look across all TADs.
The occurrences of events are aggregated and presented to the user
at the end of running the workload. The driver does not provide a
way for the user to partition TADs so that different TADs are used for
different applications.

The performance events reflect various internal or interface activities.
By combining the values from multiple performance counters, cache
performance can be measured in terms such as: cache miss rate, cache
allocations, interface retry rate, internal resource occupancy, etc.

The PMU driver exposes the available events and format options under sysfs::

        /sys/bus/event_source/devices/tad/events/
        /sys/bus/event_source/devices/tad/format/

Examples::

   $ perf list | grep tad
        tad/tad_alloc_any/                                 [Kernel PMU event]
        tad/tad_alloc_dtg/                                 [Kernel PMU event]
        tad/tad_alloc_ltg/                                 [Kernel PMU event]
        tad/tad_hit_any/                                   [Kernel PMU event]
        tad/tad_hit_dtg/                                   [Kernel PMU event]
        tad/tad_hit_ltg/                                   [Kernel PMU event]
        tad/tad_req_msh_in_exlmn/                          [Kernel PMU event]
        tad/tad_tag_rd/                                    [Kernel PMU event]
        tad/tad_tot_cycle/                                 [Kernel PMU event]

   $ perf stat -e tad_alloc_dtg,tad_alloc_ltg,tad_alloc_any,tad_hit_dtg,tad_hit_ltg,tad_hit_any,tad_tag_rd <workload>

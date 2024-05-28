================================================
StarFive StarLink Performance Monitor Unit (PMU)
================================================

StarFive StarLink Performance Monitor Unit (PMU) exists within the
StarLink Coherent Network on Chip (CNoC) that connects multiple CPU
clusters with an L3 memory system.

The uncore PMU supports overflow interrupt, up to 16 programmable 64bit
event counters, and an independent 64bit cycle counter.
The PMU can only be accessed via Memory Mapped I/O and are common to the
cores connected to the same PMU.

Driver exposes supported PMU events in sysfs "events" directory under::

  /sys/bus/event_source/devices/starfive_starlink_pmu/events/

Driver exposes cpu used to handle PMU events in sysfs "cpumask" directory
under::

  /sys/bus/event_source/devices/starfive_starlink_pmu/cpumask/

Driver describes the format of config (event ID) in sysfs "format" directory
under::

  /sys/bus/event_source/devices/starfive_starlink_pmu/format/

Example of perf usage::

	$ perf list

	starfive_starlink_pmu/cycles/                      [Kernel PMU event]
	starfive_starlink_pmu/read_hit/                    [Kernel PMU event]
	starfive_starlink_pmu/read_miss/                   [Kernel PMU event]
	starfive_starlink_pmu/read_request/                [Kernel PMU event]
	starfive_starlink_pmu/release_request/             [Kernel PMU event]
	starfive_starlink_pmu/write_hit/                   [Kernel PMU event]
	starfive_starlink_pmu/write_miss/                  [Kernel PMU event]
	starfive_starlink_pmu/write_request/               [Kernel PMU event]
	starfive_starlink_pmu/writeback/                   [Kernel PMU event]


	$ perf stat -a -e /starfive_starlink_pmu/cycles/ sleep 1

Sampling is not supported. As a result, "perf record" is not supported.
Attaching to a task is not supported, only system-wide counting is supported.

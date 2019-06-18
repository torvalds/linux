======================================================
HiSilicon SoC uncore Performance Monitoring Unit (PMU)
======================================================

The HiSilicon SoC chip includes various independent system device PMUs
such as L3 cache (L3C), Hydra Home Agent (HHA) and DDRC. These PMUs are
independent and have hardware logic to gather statistics and performance
information.

The HiSilicon SoC encapsulates multiple CPU and IO dies. Each CPU cluster
(CCL) is made up of 4 cpu cores sharing one L3 cache; each CPU die is
called Super CPU cluster (SCCL) and is made up of 6 CCLs. Each SCCL has
two HHAs (0 - 1) and four DDRCs (0 - 3), respectively.

HiSilicon SoC uncore PMU driver
-------------------------------

Each device PMU has separate registers for event counting, control and
interrupt, and the PMU driver shall register perf PMU drivers like L3C,
HHA and DDRC etc. The available events and configuration options shall
be described in the sysfs, see:

/sys/devices/hisi_sccl{X}_<l3c{Y}/hha{Y}/ddrc{Y}>/, or
/sys/bus/event_source/devices/hisi_sccl{X}_<l3c{Y}/hha{Y}/ddrc{Y}>.
The "perf list" command shall list the available events from sysfs.

Each L3C, HHA and DDRC is registered as a separate PMU with perf. The PMU
name will appear in event listing as hisi_sccl<sccl-id>_module<index-id>.
where "sccl-id" is the identifier of the SCCL and "index-id" is the index of
module.

e.g. hisi_sccl3_l3c0/rd_hit_cpipe is READ_HIT_CPIPE event of L3C index #0 in
SCCL ID #3.

e.g. hisi_sccl1_hha0/rx_operations is RX_OPERATIONS event of HHA index #0 in
SCCL ID #1.

The driver also provides a "cpumask" sysfs attribute, which shows the CPU core
ID used to count the uncore PMU event.

Example usage of perf::

  $# perf list
  hisi_sccl3_l3c0/rd_hit_cpipe/ [kernel PMU event]
  ------------------------------------------
  hisi_sccl3_l3c0/wr_hit_cpipe/ [kernel PMU event]
  ------------------------------------------
  hisi_sccl1_l3c0/rd_hit_cpipe/ [kernel PMU event]
  ------------------------------------------
  hisi_sccl1_l3c0/wr_hit_cpipe/ [kernel PMU event]
  ------------------------------------------

  $# perf stat -a -e hisi_sccl3_l3c0/rd_hit_cpipe/ sleep 5
  $# perf stat -a -e hisi_sccl3_l3c0/config=0x02/ sleep 5

The current driver does not support sampling. So "perf record" is unsupported.
Also attach to a task is unsupported as the events are all uncore.

Note: Please contact the maintainer for a complete list of events supported for
the PMU devices in the SoC and its information if needed.

=================================================================
Marvell Odyssey PEM Performance Monitoring Unit (PMU UNCORE)
=================================================================

The PCI Express Interface Units(PEM) are associated with a corresponding
monitoring unit. This includes performance counters to track various
characteristics of the data that is transmitted over the PCIe link.

The counters track inbound and outbound transactions which
includes separate counters for posted/non-posted/completion TLPs.
Also, inbound and outbound memory read requests along with their
latencies can also be monitored. Address Translation Services(ATS)events
such as ATS Translation, ATS Page Request, ATS Invalidation along with
their corresponding latencies are also tracked.

There are separate 64 bit counters to measure posted/non-posted/completion
tlps in inbound and outbound transactions. ATS events are measured by
different counters.

The PMU driver exposes the available events and format options under sysfs,
/sys/bus/event_source/devices/mrvl_pcie_rc_pmu_<>/events/
/sys/bus/event_source/devices/mrvl_pcie_rc_pmu_<>/format/

Examples::

  # perf list | grep mrvl_pcie_rc_pmu
  mrvl_pcie_rc_pmu_<>/ats_inv/             [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ats_inv_latency/     [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ats_pri/             [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ats_pri_latency/     [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ats_trans/           [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ats_trans_latency/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_inflight/         [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_reads/            [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_req_no_ro_ebus/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_req_no_ro_ncb/    [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_cpl_partid/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_dwords_cpl_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_dwords_npr/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_dwords_pr/    [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_npr/          [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ib_tlp_pr/           [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_inflight_partid/  [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_merges_cpl_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_merges_npr_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_merges_pr_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_reads_partid/     [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_cpl_partid/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_dwords_cpl_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_dwords_npr_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_dwords_pr_partid/ [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_npr_partid/   [Kernel PMU event]
  mrvl_pcie_rc_pmu_<>/ob_tlp_pr_partid/    [Kernel PMU event]


  # perf stat -e ib_inflight,ib_reads,ib_req_no_ro_ebus,ib_req_no_ro_ncb <workload>

=====================================================================
NVIDIA Tegra410 SoC Uncore Performance Monitoring Unit (PMU)
=====================================================================

The NVIDIA Tegra410 SoC includes various system PMUs to measure key performance
metrics like memory bandwidth, latency, and utilization:

* Unified Coherence Fabric (UCF)

PMU Driver
----------

The PMU driver describes the available events and configuration of each PMU in
sysfs. Please see the sections below to get the sysfs path of each PMU. Like
other uncore PMU drivers, the driver provides "cpumask" sysfs attribute to show
the CPU id used to handle the PMU event. There is also "associated_cpus"
sysfs attribute, which contains a list of CPUs associated with the PMU instance.

UCF PMU
-------

The Unified Coherence Fabric (UCF) in the NVIDIA Tegra410 SoC serves as a
distributed cache, last level for CPU Memory and CXL Memory, and cache coherent
interconnect that supports hardware coherence across multiple coherently caching
agents, including:

  * CPU clusters
  * GPU
  * PCIe Ordering Controller Unit (OCU)
  * Other IO-coherent requesters

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_source/devices/nvidia_ucf_pmu_<socket-id>.

Some of the events available in this PMU can be used to measure bandwidth and
utilization:

  * slc_access_rd: count the number of read requests to SLC.
  * slc_access_wr: count the number of write requests to SLC.
  * slc_bytes_rd: count the number of bytes transferred by slc_access_rd.
  * slc_bytes_wr: count the number of bytes transferred by slc_access_wr.
  * mem_access_rd: count the number of read requests to local or remote memory.
  * mem_access_wr: count the number of write requests to local or remote memory.
  * mem_bytes_rd: count the number of bytes transferred by mem_access_rd.
  * mem_bytes_wr: count the number of bytes transferred by mem_access_wr.
  * cycles: counts the UCF cycles.

The average bandwidth is calculated as::

   AVG_SLC_READ_BANDWIDTH_IN_GBPS = SLC_BYTES_RD / ELAPSED_TIME_IN_NS
   AVG_SLC_WRITE_BANDWIDTH_IN_GBPS = SLC_BYTES_WR / ELAPSED_TIME_IN_NS
   AVG_MEM_READ_BANDWIDTH_IN_GBPS = MEM_BYTES_RD / ELAPSED_TIME_IN_NS
   AVG_MEM_WRITE_BANDWIDTH_IN_GBPS = MEM_BYTES_WR / ELAPSED_TIME_IN_NS

The average request rate is calculated as::

   AVG_SLC_READ_REQUEST_RATE = SLC_ACCESS_RD / CYCLES
   AVG_SLC_WRITE_REQUEST_RATE = SLC_ACCESS_WR / CYCLES
   AVG_MEM_READ_REQUEST_RATE = MEM_ACCESS_RD / CYCLES
   AVG_MEM_WRITE_REQUEST_RATE = MEM_ACCESS_WR / CYCLES

More details about what other events are available can be found in Tegra410 SoC
technical reference manual.

The events can be filtered based on source or destination. The source filter
indicates the traffic initiator to the SLC, e.g local CPU, non-CPU device, or
remote socket. The destination filter specifies the destination memory type,
e.g. local system memory (CMEM), local GPU memory (GMEM), or remote memory. The
local/remote classification of the destination filter is based on the home
socket of the address, not where the data actually resides. The available
filters are described in
/sys/bus/event_source/devices/nvidia_ucf_pmu_<socket-id>/format/.

The list of UCF PMU event filters:

* Source filter:

  * src_loc_cpu: if set, count events from local CPU
  * src_loc_noncpu: if set, count events from local non-CPU device
  * src_rem: if set, count events from CPU, GPU, PCIE devices of remote socket

* Destination filter:

  * dst_loc_cmem: if set, count events to local system memory (CMEM) address
  * dst_loc_gmem: if set, count events to local GPU memory (GMEM) address
  * dst_loc_other: if set, count events to local CXL memory address
  * dst_rem: if set, count events to CPU, GPU, and CXL memory address of remote socket

If the source is not specified, the PMU will count events from all sources. If
the destination is not specified, the PMU will count events to all destinations.

Example usage:

* Count event id 0x0 in socket 0 from all sources and to all destinations::

    perf stat -a -e nvidia_ucf_pmu_0/event=0x0/

* Count event id 0x0 in socket 0 with source filter = local CPU and destination
  filter = local system memory (CMEM)::

    perf stat -a -e nvidia_ucf_pmu_0/event=0x0,src_loc_cpu=0x1,dst_loc_cmem=0x1/

* Count event id 0x0 in socket 1 with source filter = local non-CPU device and
  destination filter = remote memory::

    perf stat -a -e nvidia_ucf_pmu_1/event=0x0,src_loc_noncpu=0x1,dst_rem=0x1/

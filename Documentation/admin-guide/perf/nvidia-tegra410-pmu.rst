=====================================================================
NVIDIA Tegra410 SoC Uncore Performance Monitoring Unit (PMU)
=====================================================================

The NVIDIA Tegra410 SoC includes various system PMUs to measure key performance
metrics like memory bandwidth, latency, and utilization:

* Unified Coherence Fabric (UCF)
* PCIE
* PCIE-TGT
* CPU Memory (CMEM) Latency

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

PCIE PMU
--------

This PMU is located in the SOC fabric connecting the PCIE root complex (RC) and
the memory subsystem. It monitors all read/write traffic from the root port(s)
or a particular BDF in a PCIE RC to local or remote memory. There is one PMU per
PCIE RC in the SoC. Each RC can have up to 16 lanes that can be bifurcated into
up to 8 root ports. The traffic from each root port can be filtered using RP or
BDF filter. For example, specifying "src_rp_mask=0xFF" means the PMU counter will
capture traffic from all RPs. Please see below for more details.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_source/devices/nvidia_pcie_pmu_<socket-id>_rc_<pcie-rc-id>.

The events in this PMU can be used to measure bandwidth, utilization, and
latency:

  * rd_req: count the number of read requests by PCIE device.
  * wr_req: count the number of write requests by PCIE device.
  * rd_bytes: count the number of bytes transferred by rd_req.
  * wr_bytes: count the number of bytes transferred by wr_req.
  * rd_cum_outs: count outstanding rd_req each cycle.
  * cycles: count the clock cycles of SOC fabric connected to the PCIE interface.

The average bandwidth is calculated as::

   AVG_RD_BANDWIDTH_IN_GBPS = RD_BYTES / ELAPSED_TIME_IN_NS
   AVG_WR_BANDWIDTH_IN_GBPS = WR_BYTES / ELAPSED_TIME_IN_NS

The average request rate is calculated as::

   AVG_RD_REQUEST_RATE = RD_REQ / CYCLES
   AVG_WR_REQUEST_RATE = WR_REQ / CYCLES


The average latency is calculated as::

   FREQ_IN_GHZ = CYCLES / ELAPSED_TIME_IN_NS
   AVG_LATENCY_IN_CYCLES = RD_CUM_OUTS / RD_REQ
   AVERAGE_LATENCY_IN_NS = AVG_LATENCY_IN_CYCLES / FREQ_IN_GHZ

The PMU events can be filtered based on the traffic source and destination.
The source filter indicates the PCIE devices that will be monitored. The
destination filter specifies the destination memory type, e.g. local system
memory (CMEM), local GPU memory (GMEM), or remote memory. The local/remote
classification of the destination filter is based on the home socket of the
address, not where the data actually resides. These filters can be found in
/sys/bus/event_source/devices/nvidia_pcie_pmu_<socket-id>_rc_<pcie-rc-id>/format/.

The list of event filters:

* Source filter:

  * src_rp_mask: bitmask of root ports that will be monitored. Each bit in this
    bitmask represents the RP index in the RC. If the bit is set, all devices under
    the associated RP will be monitored. E.g "src_rp_mask=0xF" will monitor
    devices in root port 0 to 3.
  * src_bdf: the BDF that will be monitored. This is a 16-bit value that
    follows formula: (bus << 8) + (device << 3) + (function). For example, the
    value of BDF 27:01.1 is 0x2781.
  * src_bdf_en: enable the BDF filter. If this is set, the BDF filter value in
    "src_bdf" is used to filter the traffic.

  Note that Root-Port and BDF filters are mutually exclusive and the PMU in
  each RC can only have one BDF filter for the whole counters. If BDF filter
  is enabled, the BDF filter value will be applied to all events.

* Destination filter:

  * dst_loc_cmem: if set, count events to local system memory (CMEM) address
  * dst_loc_gmem: if set, count events to local GPU memory (GMEM) address
  * dst_loc_pcie_p2p: if set, count events to local PCIE peer address
  * dst_loc_pcie_cxl: if set, count events to local CXL memory address
  * dst_rem: if set, count events to remote memory address

If the source filter is not specified, the PMU will count events from all root
ports. If the destination filter is not specified, the PMU will count events
to all destinations.

Example usage:

* Count event id 0x0 from root port 0 of PCIE RC-0 on socket 0 targeting all
  destinations::

    perf stat -a -e nvidia_pcie_pmu_0_rc_0/event=0x0,src_rp_mask=0x1/

* Count event id 0x1 from root port 0 and 1 of PCIE RC-1 on socket 0 and
  targeting just local CMEM of socket 0::

    perf stat -a -e nvidia_pcie_pmu_0_rc_1/event=0x1,src_rp_mask=0x3,dst_loc_cmem=0x1/

* Count event id 0x2 from root port 0 of PCIE RC-2 on socket 1 targeting all
  destinations::

    perf stat -a -e nvidia_pcie_pmu_1_rc_2/event=0x2,src_rp_mask=0x1/

* Count event id 0x3 from root port 0 and 1 of PCIE RC-3 on socket 1 and
  targeting just local CMEM of socket 1::

    perf stat -a -e nvidia_pcie_pmu_1_rc_3/event=0x3,src_rp_mask=0x3,dst_loc_cmem=0x1/

* Count event id 0x4 from BDF 01:01.0 of PCIE RC-4 on socket 0 targeting all
  destinations::

    perf stat -a -e nvidia_pcie_pmu_0_rc_4/event=0x4,src_bdf=0x0180,src_bdf_en=0x1/

.. _NVIDIA_T410_PCIE_PMU_RC_Mapping_Section:

Mapping the RC# to lspci segment number
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mapping the RC# to lspci segment number can be non-trivial; hence a new NVIDIA
Designated Vendor Specific Capability (DVSEC) register is added into the PCIE config space
for each RP. This DVSEC has vendor id "10de" and DVSEC id of "0x4". The DVSEC register
contains the following information to map PCIE devices under the RP back to its RC# :

  - Bus# (byte 0xc) : bus number as reported by the lspci output
  - Segment# (byte 0xd) : segment number as reported by the lspci output
  - RP# (byte 0xe) : port number as reported by LnkCap attribute from lspci for a device with Root Port capability
  - RC# (byte 0xf): root complex number associated with the RP
  - Socket# (byte 0x10): socket number associated with the RP

Example script for mapping lspci BDF to RC# and socket#::

  #!/bin/bash
  while read bdf rest; do
    dvsec4_reg=$(lspci -vv -s $bdf | awk '
      /Designated Vendor-Specific: Vendor=10de ID=0004/ {
        match($0, /\[([0-9a-fA-F]+)/, arr);
        print "0x" arr[1];
        exit
      }
    ')
    if [ -n "$dvsec4_reg" ]; then
      bus=$(setpci -s $bdf $(printf '0x%x' $((${dvsec4_reg} + 0xc))).b)
      segment=$(setpci -s $bdf $(printf '0x%x' $((${dvsec4_reg} + 0xd))).b)
      rp=$(setpci -s $bdf $(printf '0x%x' $((${dvsec4_reg} + 0xe))).b)
      rc=$(setpci -s $bdf $(printf '0x%x' $((${dvsec4_reg} + 0xf))).b)
      socket=$(setpci -s $bdf $(printf '0x%x' $((${dvsec4_reg} + 0x10))).b)
      echo "$bdf: Bus=$bus, Segment=$segment, RP=$rp, RC=$rc, Socket=$socket"
    fi
  done < <(lspci -d 10de:)

Example output::

  0001:00:00.0: Bus=00, Segment=01, RP=00, RC=00, Socket=00
  0002:80:00.0: Bus=80, Segment=02, RP=01, RC=01, Socket=00
  0002:a0:00.0: Bus=a0, Segment=02, RP=02, RC=01, Socket=00
  0002:c0:00.0: Bus=c0, Segment=02, RP=03, RC=01, Socket=00
  0002:e0:00.0: Bus=e0, Segment=02, RP=04, RC=01, Socket=00
  0003:00:00.0: Bus=00, Segment=03, RP=00, RC=02, Socket=00
  0004:00:00.0: Bus=00, Segment=04, RP=00, RC=03, Socket=00
  0005:00:00.0: Bus=00, Segment=05, RP=00, RC=04, Socket=00
  0005:40:00.0: Bus=40, Segment=05, RP=01, RC=04, Socket=00
  0005:c0:00.0: Bus=c0, Segment=05, RP=02, RC=04, Socket=00
  0006:00:00.0: Bus=00, Segment=06, RP=00, RC=05, Socket=00
  0009:00:00.0: Bus=00, Segment=09, RP=00, RC=00, Socket=01
  000a:80:00.0: Bus=80, Segment=0a, RP=01, RC=01, Socket=01
  000a:a0:00.0: Bus=a0, Segment=0a, RP=02, RC=01, Socket=01
  000a:e0:00.0: Bus=e0, Segment=0a, RP=03, RC=01, Socket=01
  000b:00:00.0: Bus=00, Segment=0b, RP=00, RC=02, Socket=01
  000c:00:00.0: Bus=00, Segment=0c, RP=00, RC=03, Socket=01
  000d:00:00.0: Bus=00, Segment=0d, RP=00, RC=04, Socket=01
  000d:40:00.0: Bus=40, Segment=0d, RP=01, RC=04, Socket=01
  000d:c0:00.0: Bus=c0, Segment=0d, RP=02, RC=04, Socket=01
  000e:00:00.0: Bus=00, Segment=0e, RP=00, RC=05, Socket=01

PCIE-TGT PMU
------------

This PMU is located in the SOC fabric connecting the PCIE root complex (RC) and
the memory subsystem. It monitors traffic targeting PCIE BAR and CXL HDM ranges.
There is one PCIE-TGT PMU per PCIE RC in the SoC. Each RC in Tegra410 SoC can
have up to 16 lanes that can be bifurcated into up to 8 root ports (RP). The PMU
provides RP filter to count PCIE BAR traffic to each RP and address filter to
count access to PCIE BAR or CXL HDM ranges. The details of the filters are
described in the following sections.

Mapping the RC# to lspci segment number is similar to the PCIE PMU. Please see
:ref:`NVIDIA_T410_PCIE_PMU_RC_Mapping_Section` for more info.

The events and configuration options of this PMU device are available in sysfs,
see /sys/bus/event_source/devices/nvidia_pcie_tgt_pmu_<socket-id>_rc_<pcie-rc-id>.

The events in this PMU can be used to measure bandwidth and utilization:

  * rd_req: count the number of read requests to PCIE.
  * wr_req: count the number of write requests to PCIE.
  * rd_bytes: count the number of bytes transferred by rd_req.
  * wr_bytes: count the number of bytes transferred by wr_req.
  * cycles: count the clock cycles of SOC fabric connected to the PCIE interface.

The average bandwidth is calculated as::

   AVG_RD_BANDWIDTH_IN_GBPS = RD_BYTES / ELAPSED_TIME_IN_NS
   AVG_WR_BANDWIDTH_IN_GBPS = WR_BYTES / ELAPSED_TIME_IN_NS

The average request rate is calculated as::

   AVG_RD_REQUEST_RATE = RD_REQ / CYCLES
   AVG_WR_REQUEST_RATE = WR_REQ / CYCLES

The PMU events can be filtered based on the destination root port or target
address range. Filtering based on RP is only available for PCIE BAR traffic.
Address filter works for both PCIE BAR and CXL HDM ranges. These filters can be
found in sysfs, see
/sys/bus/event_source/devices/nvidia_pcie_tgt_pmu_<socket-id>_rc_<pcie-rc-id>/format/.

Destination filter settings:

* dst_rp_mask: bitmask to select the root port(s) to monitor. E.g. "dst_rp_mask=0xFF"
  corresponds to all root ports (from 0 to 7) in the PCIE RC. Note that this filter is
  only available for PCIE BAR traffic.
* dst_addr_base: BAR or CXL HDM filter base address.
* dst_addr_mask: BAR or CXL HDM filter address mask.
* dst_addr_en: enable BAR or CXL HDM address range filter. If this is set, the
  address range specified by "dst_addr_base" and "dst_addr_mask" will be used to filter
  the PCIE BAR and CXL HDM traffic address. The PMU uses the following comparison
  to determine if the traffic destination address falls within the filter range::

    (txn's addr & dst_addr_mask) == (dst_addr_base & dst_addr_mask)

  If the comparison succeeds, then the event will be counted.

If the destination filter is not specified, the RP filter will be configured by default
to count PCIE BAR traffic to all root ports.

Example usage:

* Count event id 0x0 to root port 0 and 1 of PCIE RC-0 on socket 0::

    perf stat -a -e nvidia_pcie_tgt_pmu_0_rc_0/event=0x0,dst_rp_mask=0x3/

* Count event id 0x1 for accesses to PCIE BAR or CXL HDM address range
  0x10000 to 0x100FF on socket 0's PCIE RC-1::

    perf stat -a -e nvidia_pcie_tgt_pmu_0_rc_1/event=0x1,dst_addr_base=0x10000,dst_addr_mask=0xFFF00,dst_addr_en=0x1/

CPU Memory (CMEM) Latency PMU
-----------------------------

This PMU monitors latency events of memory read requests from the edge of the
Unified Coherence Fabric (UCF) to local CPU DRAM:

  * RD_REQ counters: count read requests (32B per request).
  * RD_CUM_OUTS counters: accumulated outstanding request counter, which track
    how many cycles the read requests are in flight.
  * CYCLES counter: counts the number of elapsed cycles.

The average latency is calculated as::

   FREQ_IN_GHZ = CYCLES / ELAPSED_TIME_IN_NS
   AVG_LATENCY_IN_CYCLES = RD_CUM_OUTS / RD_REQ
   AVERAGE_LATENCY_IN_NS = AVG_LATENCY_IN_CYCLES / FREQ_IN_GHZ

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_source/devices/nvidia_cmem_latency_pmu_<socket-id>.

Example usage::

  perf stat -a -e '{nvidia_cmem_latency_pmu_0/rd_req/,nvidia_cmem_latency_pmu_0/rd_cum_outs/,nvidia_cmem_latency_pmu_0/cycles/}'

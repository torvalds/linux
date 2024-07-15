=============================================================
Cavium ThunderX2 SoC Performance Monitoring Unit (PMU UNCORE)
=============================================================

The ThunderX2 SoC PMU consists of independent, system-wide, per-socket
PMUs such as the Level 3 Cache (L3C), DDR4 Memory Controller (DMC) and
Cavium Coherent Processor Interconnect (CCPI2).

The DMC has 8 interleaved channels and the L3C has 16 interleaved tiles.
Events are counted for the default channel (i.e. channel 0) and prorated
to the total number of channels/tiles.

The DMC and L3C support up to 4 counters, while the CCPI2 supports up to 8
counters. Counters are independently programmable to different events and
can be started and stopped individually. None of the counters support an
overflow interrupt. DMC and L3C counters are 32-bit and read every 2 seconds.
The CCPI2 counters are 64-bit and assumed not to overflow in normal operation.

PMU UNCORE (perf) driver:

The thunderx2_pmu driver registers per-socket perf PMUs for the DMC and
L3C devices.  Each PMU can be used to count up to 4 (DMC/L3C) or up to 8
(CCPI2) events simultaneously. The PMUs provide a description of their
available events and configuration options under sysfs, see
/sys/bus/event_source/devices/uncore_<l3c_S/dmc_S/ccpi2_S/>; S is the socket id.

The driver does not support sampling, therefore "perf record" will not
work. Per-task perf sessions are also not supported.

Examples::

  # perf stat -a -e uncore_dmc_0/cnt_cycles/ sleep 1

  # perf stat -a -e \
  uncore_dmc_0/cnt_cycles/,\
  uncore_dmc_0/data_transfers/,\
  uncore_dmc_0/read_txns/,\
  uncore_dmc_0/write_txns/ sleep 1

  # perf stat -a -e \
  uncore_l3c_0/read_request/,\
  uncore_l3c_0/read_hit/,\
  uncore_l3c_0/inv_request/,\
  uncore_l3c_0/inv_hit/ sleep 1

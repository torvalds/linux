=============================================================
Alibaba's T-Head SoC Uncore Performance Monitoring Unit (PMU)
=============================================================

The Yitian 710, custom-built by Alibaba Group's chip development business,
T-Head, implements uncore PMU for performance and functional debugging to
facilitate system maintenance.

DDR Sub-System Driveway (DRW) PMU Driver
=========================================

Yitian 710 employs eight DDR5/4 channels, four on each die. Each DDR5 channel
is independent of others to service system memory requests. And one DDR5
channel is split into two independent sub-channels. The DDR Sub-System Driveway
implements separate PMUs for each sub-channel to monitor various performance
metrics.

The Driveway PMU devices are named as ali_drw_<sys_base_addr> with perf.
For example, ali_drw_21000 and ali_drw_21080 are two PMU devices for two
sub-channels of the same channel in die 0. And the PMU device of die 1 is
prefixed with ali_drw_400XXXXX, e.g. ali_drw_40021000.

Each sub-channel has 36 PMU counters in total, which is classified into
four groups:

- Group 0: PMU Cycle Counter. This group has one pair of counters
  pmu_cycle_cnt_low and pmu_cycle_cnt_high, that is used as the cycle count
  based on DDRC core clock.

- Group 1: PMU Bandwidth Counters. This group has 8 counters that are used
  to count the total access number of either the eight bank groups in a
  selected rank, or four ranks separately in the first 4 counters. The base
  transfer unit is 64B.

- Group 2: PMU Retry Counters. This group has 10 counters, that intend to
  count the total retry number of each type of uncorrectable error.

- Group 3: PMU Common Counters. This group has 16 counters, that are used
  to count the common events.

For now, the Driveway PMU driver only uses counters in group 0 and group 3.

The DDR Controller (DDRCTL) and DDR PHY combine to create a complete solution
for connecting an SoC application bus to DDR memory devices. The DDRCTL
receives transactions Host Interface (HIF) which is custom-defined by Synopsys.
These transactions are queued internally and scheduled for access while
satisfying the SDRAM protocol timing requirements, transaction priorities, and
dependencies between the transactions. The DDRCTL in turn issues commands on
the DDR PHY Interface (DFI) to the PHY module, which launches and captures data
to and from the SDRAM. The driveway PMUs have hardware logic to gather
statistics and performance logging signals on HIF, DFI, etc.

By counting the READ, WRITE and RMW commands sent to the DDRC through the HIF
interface, we could calculate the bandwidth. Example usage of counting memory
data bandwidth::

  perf stat \
    -e ali_drw_21000/hif_wr/ \
    -e ali_drw_21000/hif_rd/ \
    -e ali_drw_21000/hif_rmw/ \
    -e ali_drw_21000/cycle/ \
    -e ali_drw_21080/hif_wr/ \
    -e ali_drw_21080/hif_rd/ \
    -e ali_drw_21080/hif_rmw/ \
    -e ali_drw_21080/cycle/ \
    -e ali_drw_23000/hif_wr/ \
    -e ali_drw_23000/hif_rd/ \
    -e ali_drw_23000/hif_rmw/ \
    -e ali_drw_23000/cycle/ \
    -e ali_drw_23080/hif_wr/ \
    -e ali_drw_23080/hif_rd/ \
    -e ali_drw_23080/hif_rmw/ \
    -e ali_drw_23080/cycle/ \
    -e ali_drw_25000/hif_wr/ \
    -e ali_drw_25000/hif_rd/ \
    -e ali_drw_25000/hif_rmw/ \
    -e ali_drw_25000/cycle/ \
    -e ali_drw_25080/hif_wr/ \
    -e ali_drw_25080/hif_rd/ \
    -e ali_drw_25080/hif_rmw/ \
    -e ali_drw_25080/cycle/ \
    -e ali_drw_27000/hif_wr/ \
    -e ali_drw_27000/hif_rd/ \
    -e ali_drw_27000/hif_rmw/ \
    -e ali_drw_27000/cycle/ \
    -e ali_drw_27080/hif_wr/ \
    -e ali_drw_27080/hif_rd/ \
    -e ali_drw_27080/hif_rmw/ \
    -e ali_drw_27080/cycle/ -- sleep 10

The average DRAM bandwidth can be calculated as follows:

- Read Bandwidth =  perf_hif_rd * DDRC_WIDTH * DDRC_Freq / DDRC_Cycle
- Write Bandwidth = (perf_hif_wr + perf_hif_rmw) * DDRC_WIDTH * DDRC_Freq / DDRC_Cycle

Here, DDRC_WIDTH = 64 bytes.

The current driver does not support sampling. So "perf record" is
unsupported.  Also attach to a task is unsupported as the events are all
uncore.

======================================
HNS3 Performance Monitoring Unit (PMU)
======================================

HNS3(HiSilicon network system 3) Performance Monitoring Unit (PMU) is an
End Point device to collect performance statistics of HiSilicon SoC NIC.
On Hip09, each SICL(Super I/O cluster) has one PMU device.

HNS3 PMU supports collection of performance statistics such as bandwidth,
latency, packet rate and interrupt rate.

Each HNS3 PMU supports 8 hardware events.

HNS3 PMU driver
===============

The HNS3 PMU driver registers a perf PMU with the name of its sicl id.::

  /sys/devices/hns3_pmu_sicl_<sicl_id>

PMU driver provides description of available events, filter modes, format,
identifier and cpumask in sysfs.

The "events" directory describes the event code of all supported events
shown in perf list.

The "filtermode" directory describes the supported filter modes of each
event.

The "format" directory describes all formats of the config (events) and
config1 (filter options) fields of the perf_event_attr structure.

The "identifier" file shows version of PMU hardware device.

The "bdf_min" and "bdf_max" files show the supported bdf range of each
pmu device.

The "hw_clk_freq" file shows the hardware clock frequency of each pmu
device.

Example usage of checking event code and subevent code::

  $# cat /sys/devices/hns3_pmu_sicl_0/events/dly_tx_normal_to_mac_time
  config=0x00204
  $# cat /sys/devices/hns3_pmu_sicl_0/events/dly_tx_normal_to_mac_packet_num
  config=0x10204

Each performance statistic has a pair of events to get two values to
calculate real performance data in userspace.

The bits 0~15 of config (here 0x0204) are the true hardware event code. If
two events have same value of bits 0~15 of config, that means they are
event pair. And the bit 16 of config indicates getting counter 0 or
counter 1 of hardware event.

After getting two values of event pair in userspace, the formula of
computation to calculate real performance data is:::

  counter 0 / counter 1

Example usage of checking supported filter mode::

  $# cat /sys/devices/hns3_pmu_sicl_0/filtermode/bw_ssu_rpu_byte_num
  filter mode supported: global/port/port-tc/func/func-queue/

Example usage of perf::

  $# perf list
  hns3_pmu_sicl_0/bw_ssu_rpu_byte_num/ [kernel PMU event]
  hns3_pmu_sicl_0/bw_ssu_rpu_time/     [kernel PMU event]
  ------------------------------------------

  $# perf stat -g -e hns3_pmu_sicl_0/bw_ssu_rpu_byte_num,global=1/ -e hns3_pmu_sicl_0/bw_ssu_rpu_time,global=1/ -I 1000
  or
  $# perf stat -g -e hns3_pmu_sicl_0/config=0x00002,global=1/ -e hns3_pmu_sicl_0/config=0x10002,global=1/ -I 1000


Filter modes
--------------

1. global mode
PMU collect performance statistics for all HNS3 PCIe functions of IO DIE.
Set the "global" filter option to 1 will enable this mode.
Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x1020F,global=1/ -I 1000

2. port mode
PMU collect performance statistic of one whole physical port. The port id
is same as mac id. The "tc" filter option must be set to 0xF in this mode,
here tc stands for traffic class.

Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x1020F,port=0,tc=0xF/ -I 1000

3. port-tc mode
PMU collect performance statistic of one tc of physical port. The port id
is same as mac id. The "tc" filter option must be set to 0 ~ 7 in this
mode.
Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x1020F,port=0,tc=0/ -I 1000

4. func mode
PMU collect performance statistic of one PF/VF. The function id is BDF of
PF/VF, its conversion formula::

  func = (bus << 8) + (device << 3) + (function)

for example:
  BDF         func
  35:00.0    0x3500
  35:00.1    0x3501
  35:01.0    0x3508

In this mode, the "queue" filter option must be set to 0xFFFF.
Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x1020F,bdf=0x3500,queue=0xFFFF/ -I 1000

5. func-queue mode
PMU collect performance statistic of one queue of PF/VF. The function id
is BDF of PF/VF, the "queue" filter option must be set to the exact queue
id of function.
Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x1020F,bdf=0x3500,queue=0/ -I 1000

6. func-intr mode
PMU collect performance statistic of one interrupt of PF/VF. The function
id is BDF of PF/VF, the "intr" filter option must be set to the exact
interrupt id of function.
Example usage of perf::

  $# perf stat -a -e hns3_pmu_sicl_0/config=0x00301,bdf=0x3500,intr=0/ -I 1000

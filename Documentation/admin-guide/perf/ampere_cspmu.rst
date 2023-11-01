.. SPDX-License-Identifier: GPL-2.0

============================================
Ampere SoC Performance Monitoring Unit (PMU)
============================================

Ampere SoC PMU is a generic PMU IP that follows Arm CoreSight PMU architecture.
Therefore, the driver is implemented as a submodule of arm_cspmu driver. At the
first phase it's used for counting MCU events on AmpereOne.


MCU PMU events
--------------

The PMU driver supports setting filters for "rank", "bank", and "threshold".
Note, that the filters are per PMU instance rather than per event.


Example for perf tool use::

  / # perf list ampere

    ampere_mcu_pmu_0/act_sent/                         [Kernel PMU event]
    <...>
    ampere_mcu_pmu_1/rd_sent/                          [Kernel PMU event]
    <...>

  / # perf stat -a -e ampere_mcu_pmu_0/act_sent,bank=5,rank=3,threshold=2/,ampere_mcu_pmu_1/rd_sent/ \
        sleep 1

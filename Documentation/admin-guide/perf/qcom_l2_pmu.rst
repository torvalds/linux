=====================================================================
Qualcomm Technologies Level-2 Cache Performance Monitoring Unit (PMU)
=====================================================================

This driver supports the L2 cache clusters found in Qualcomm Technologies
Centriq SoCs. There are multiple physical L2 cache clusters, each with their
own PMU. Each cluster has one or more CPUs associated with it.

There is one logical L2 PMU exposed, which aggregates the results from
the physical PMUs.

The driver provides a description of its available events and configuration
options in sysfs, see /sys/devices/l2cache_0.

The "format" directory describes the format of the events.

Events can be envisioned as a 2-dimensional array. Each column represents
a group of events. There are 8 groups. Only one entry from each
group can be in use at a time. If multiple events from the same group
are specified, the conflicting events cannot be counted at the same time.

Events are specified as 0xCCG, where CC is 2 hex digits specifying
the code (array row) and G specifies the group (column) 0-7.

In addition there is a cycle counter event specified by the value 0xFE
which is outside the above scheme.

The driver provides a "cpumask" sysfs attribute which contains a mask
consisting of one CPU per cluster which will be used to handle all the PMU
events on that cluster.

Examples for use with perf::

  perf stat -e l2cache_0/config=0x001/,l2cache_0/config=0x042/ -a sleep 1

  perf stat -e l2cache_0/config=0xfe/ -C 2 sleep 1

The driver does not support sampling, therefore "perf record" will
not work. Per-task perf sessions are not supported.

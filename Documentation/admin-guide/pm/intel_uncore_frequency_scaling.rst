.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

==============================
Intel Uncore Frequency Scaling
==============================

:Copyright: |copy| 2022-2023 Intel Corporation

:Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>

Introduction
------------

The uncore can consume significant amount of power in Intel's Xeon servers based
on the workload characteristics. To optimize the total power and improve overall
performance, SoCs have internal algorithms for scaling uncore frequency. These
algorithms monitor workload usage of uncore and set a desirable frequency.

It is possible that users have different expectations of uncore performance and
want to have control over it. The objective is similar to allowing users to set
the scaling min/max frequencies via cpufreq sysfs to improve CPU performance.
Users may have some latency sensitive workloads where they do not want any
change to uncore frequency. Also, users may have workloads which require
different core and uncore performance at distinct phases and they may want to
use both cpufreq and the uncore scaling interface to distribute power and
improve overall performance.

Sysfs Interface
---------------

To control uncore frequency, a sysfs interface is provided in the directory:
`/sys/devices/system/cpu/intel_uncore_frequency/`.

There is one directory for each package and die combination as the scope of
uncore scaling control is per die in multiple die/package SoCs or per
package for single die per package SoCs. The name represents the
scope of control. For example: 'package_00_die_00' is for package id 0 and
die 0.

Each package_*_die_* contains the following attributes:

``initial_max_freq_khz``
	Out of reset, this attribute represent the maximum possible frequency.
	This is a read-only attribute. If users adjust max_freq_khz,
	they can always go back to maximum using the value from this attribute.

``initial_min_freq_khz``
	Out of reset, this attribute represent the minimum possible frequency.
	This is a read-only attribute. If users adjust min_freq_khz,
	they can always go back to minimum using the value from this attribute.

``max_freq_khz``
	This attribute is used to set the maximum uncore frequency.

``min_freq_khz``
	This attribute is used to set the minimum uncore frequency.

``current_freq_khz``
	This attribute is used to get the current uncore frequency.

SoCs with TPMI (Topology Aware Register and PM Capsule Interface)
-----------------------------------------------------------------

An SoC can contain multiple power domains with individual or collection
of mesh partitions. This partition is called fabric cluster.

Certain type of meshes will need to run at the same frequency, they will
be placed in the same fabric cluster. Benefit of fabric cluster is that it
offers a scalable mechanism to deal with partitioned fabrics in a SoC.

The current sysfs interface supports controls at package and die level.
This interface is not enough to support more granular control at
fabric cluster level.

SoCs with the support of TPMI (Topology Aware Register and PM Capsule
Interface), can have multiple power domains. Each power domain can
contain one or more fabric clusters.

To represent controls at fabric cluster level in addition to the
controls at package and die level (like systems without TPMI
support), sysfs is enhanced. This granular interface is presented in the
sysfs with directories names prefixed with "uncore". For example:
uncore00, uncore01 etc.

The scope of control is specified by attributes "package_id", "domain_id"
and "fabric_cluster_id" in the directory.

Attributes in each directory:

``domain_id``
	This attribute is used to get the power domain id of this instance.

``fabric_cluster_id``
	This attribute is used to get the fabric cluster id of this instance.

``package_id``
	This attribute is used to get the package id of this instance.

The other attributes are same as presented at package_*_die_* level.

In most of current use cases, the "max_freq_khz" and "min_freq_khz"
is updated at "package_*_die_*" level. This model will be still supported
with the following approach:

When user uses controls at "package_*_die_*" level, then every fabric
cluster is affected in that package and die. For example: user changes
"max_freq_khz" in the package_00_die_00, then "max_freq_khz" for uncore*
directory with the same package id will be updated. In this case user can
still update "max_freq_khz" at each uncore* level, which is more restrictive.
Similarly, user can update "min_freq_khz" at "package_*_die_*" level
to apply at each uncore* level.

Support for "current_freq_khz" is available only at each fabric cluster
level (i.e., in uncore* directory).

Efficiency vs. Latency Tradeoff
-------------------------------

The Efficiency Latency Control (ELC) feature improves performance
per watt. With this feature hardware power management algorithms
optimize trade-off between latency and power consumption. For some
latency sensitive workloads further tuning can be done by SW to
get desired performance.

The hardware monitors the average CPU utilization across all cores
in a power domain at regular intervals and decides an uncore frequency.
While this may result in the best performance per watt, workload may be
expecting higher performance at the expense of power. Consider an
application that intermittently wakes up to perform memory reads on an
otherwise idle system. In such cases, if hardware lowers uncore
frequency, then there may be delay in ramp up of frequency to meet
target performance.

The ELC control defines some parameters which can be changed from SW.
If the average CPU utilization is below a user-defined threshold
(elc_low_threshold_percent attribute below), the user-defined uncore
floor frequency will be used (elc_floor_freq_khz attribute below)
instead of hardware calculated minimum.

Similarly in high load scenario where the CPU utilization goes above
the high threshold value (elc_high_threshold_percent attribute below)
instead of jumping to maximum uncore frequency, frequency is increased
in 100MHz steps. This avoids consuming unnecessarily high power
immediately with CPU utilization spikes.

Attributes for efficiency latency control:

``elc_floor_freq_khz``
	This attribute is used to get/set the efficiency latency floor frequency.
	If this variable is lower than the 'min_freq_khz', it is ignored by
	the firmware.

``elc_low_threshold_percent``
	This attribute is used to get/set the efficiency latency control low
	threshold. This attribute is in percentages of CPU utilization.

``elc_high_threshold_percent``
	This attribute is used to get/set the efficiency latency control high
	threshold. This attribute is in percentages of CPU utilization.

``elc_high_threshold_enable``
	This attribute is used to enable/disable the efficiency latency control
	high threshold. Write '1' to enable, '0' to disable.

Example system configuration below, which does following:
  * when CPU utilization is less than 10%: sets uncore frequency to 800MHz
  * when CPU utilization is higher than 95%: increases uncore frequency in
    100MHz steps, until power limit is reached

  elc_floor_freq_khz:800000
  elc_high_threshold_percent:95
  elc_high_threshold_enable:1
  elc_low_threshold_percent:10

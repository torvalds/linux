.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/cputopology.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

==========================
如何通过sysfs将CPU拓扑导出
==========================

CPU拓扑信息通过sysfs导出。显示的项（属性）和某些架构的/proc/cpuinfo输出相似。它们位于
/sys/devices/system/cpu/cpuX/topology/。请阅读ABI文件：
Documentation/ABI/stable/sysfs-devices-system-cpu。

drivers/base/topology.c是体系结构中性的，它导出了这些属性。然而，die、cluster、book、
draw这些层次结构相关的文件仅在体系结构提供了下文描述的宏的条件下被创建。

对于支持这个特性的体系结构，它必须在include/asm-XXX/topology.h中定义这些宏中的一部分::

	#define topology_physical_package_id(cpu)
	#define topology_die_id(cpu)
	#define topology_cluster_id(cpu)
	#define topology_core_id(cpu)
	#define topology_book_id(cpu)
	#define topology_drawer_id(cpu)
	#define topology_sibling_cpumask(cpu)
	#define topology_core_cpumask(cpu)
	#define topology_cluster_cpumask(cpu)
	#define topology_die_cpumask(cpu)
	#define topology_book_cpumask(cpu)
	#define topology_drawer_cpumask(cpu)

``**_id macros`` 的类型是int。
``**_cpumask macros`` 的类型是 ``(const) struct cpumask *`` 。后者和恰当的
``**_siblings`` sysfs属性对应（除了topology_sibling_cpumask()，它和thread_siblings
对应）。

为了在所有体系结构上保持一致，include/linux/topology.h提供了上述所有宏的默认定义，以防
它们未在include/asm-XXX/topology.h中定义:

1) topology_physical_package_id: -1
2) topology_die_id: -1
3) topology_cluster_id: -1
4) topology_core_id: 0
5) topology_book_id: -1
6) topology_drawer_id: -1
7) topology_sibling_cpumask: 仅入参CPU
8) topology_core_cpumask: 仅入参CPU
9) topology_cluster_cpumask: 仅入参CPU
10) topology_die_cpumask: 仅入参CPU
11) topology_book_cpumask:  仅入参CPU
12) topology_drawer_cpumask: 仅入参CPU

此外，CPU拓扑信息由/sys/devices/system/cpu提供，包含下述文件。输出对应的内部数据源放在
方括号（"[]"）中。

    =========== ==================================================================
    kernel_max: 内核配置允许的最大CPU下标值。[NR_CPUS-1]

    offline:    由于热插拔移除或者超过内核允许的CPU上限（上文描述的kernel_max）
                导致未上线的CPU。[~cpu_online_mask + cpus >= NR_CPUS]

    online:     在线的CPU，可供调度使用。[cpu_online_mask]

    possible:   已被分配资源的CPU，如果它们CPU实际存在，可以上线。
                [cpu_possible_mask]

    present:    被系统识别实际存在的CPU。[cpu_present_mask]
    =========== ==================================================================

上述输出的格式和cpulist_parse()兼容[参见 <linux/cpumask.h>]。下面给些例子。

在本例中，系统中有64个CPU，但是CPU 32-63超过了kernel_max值，因为NR_CPUS配置项是32，
取值范围被限制为0..31。此外注意CPU2和4-31未上线，但是可以上线，因为它们同时存在于
present和possible::

     kernel_max: 31
        offline: 2,4-31,32-63
         online: 0-1,3
       possible: 0-31
        present: 0-31

在本例中，NR_CPUS配置项是128，但内核启动时设置possible_cpus=144。系统中有4个CPU，
CPU2被手动设置下线（也是唯一一个可以上线的CPU）::

     kernel_max: 127
        offline: 2,4-127,128-143
         online: 0-1,3
       possible: 0-127
        present: 0-3

阅读Documentation/core-api/cpu_hotplug.rst可了解开机参数possible_cpus=NUM，同时还
可以了解各种cpumask的信息。

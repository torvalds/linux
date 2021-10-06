.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/cpu-freq/cpufreq-stats.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_cpufreq-stats.rst:

==========================================
sysfs CPUFreq Stats的一般说明
==========================================

用户信息


作者: Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>

.. Contents

   1. 简介
   2. 提供的统计数据(举例说明)
   3. 配置cpufreq-stats


1. 简介
===============

cpufreq-stats是一个为每个CPU提供CPU频率统计的驱动。
这些统计数据在/sysfs中以一堆只读接口的形式提供。这个接口（在配置好后）将出现在
/sysfs（<sysfs root>/devices/system/cpu/cpuX/cpufreq/stats/）中cpufreq下的一个单
独的目录中，提供给每个CPU。
各种统计数据将在此目录下形成只读文件。

此驱动是独立于任何可能运行在你所用CPU上的特定cpufreq_driver而设计的。因此，它将与所有
cpufreq_driver一起工作。


2. 提供的统计数据(举例说明)
=====================================

cpufreq stats提供了以下统计数据（在下面详细解释）。

-  time_in_state
-  total_trans
-  trans_table

所有的统计数据将从统计驱动被载入的时间（或统计被重置的时间）开始，到某一统计数据被读取的时间为止。
显然，统计驱动不会有任何关于统计驱动载入之前的频率转换信息。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # ls -l
    total 0
    drwxr-xr-x  2 root root    0 May 14 16:06 .
    drwxr-xr-x  3 root root    0 May 14 15:58 ..
    --w-------  1 root root 4096 May 14 16:06 reset
    -r--r--r--  1 root root 4096 May 14 16:06 time_in_state
    -r--r--r--  1 root root 4096 May 14 16:06 total_trans
    -r--r--r--  1 root root 4096 May 14 16:06 trans_table

- **reset**

只写属性，可用于重置统计计数器。这对于评估不同调节器下的系统行为非常有用，且无需重启。


- **time_in_state**

此项给出了这个CPU所支持的每个频率所花费的时间。cat输出的每一行都会有"<frequency>
<time>"对，表示这个CPU在<frequency>上花费了<time>个usertime单位的时间。这里的
usertime单位是10mS（类似于/proc中输出的其他时间）。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat time_in_state
    3600000 2089
    3400000 136
    3200000 34
    3000000 67
    2800000 172488


- **total_trans**

给出了这个CPU上频率转换的总次数。cat的输出将有一个单一的计数，这就是频率转换的总数。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat total_trans
    20

- **trans_table**

这将提供所有CPU频率转换的细粒度信息。这里的cat输出是一个二维矩阵，其中一个条目<i, j>（第
i行，第j列）代表从Freq_i到Freq_j的转换次数。Freq_i行和Freq_j列遵循驱动最初提供给cpufreq
核的频率表的排序顺序，因此可以排序（升序或降序）或不排序。 这里的输出也包含了每行每列的实际
频率值，以便更好地阅读。

如果转换表大于PAGE_SIZE，读取时将返回一个-EFBIG错误。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat trans_table
    From  :    To
	    :   3600000   3400000   3200000   3000000   2800000
    3600000:         0         5         0         0         0
    3400000:         4         0         2         0         0
    3200000:         0         1         0         2         0
    3000000:         0         0         1         0         3
    2800000:         0         0         0         2         0

3. 配置cpufreq-stats
============================

要在你的内核中配置cpufreq-stats::

	Config Main Menu
		Power management options (ACPI, APM)  --->
			CPU Frequency scaling  --->
				[*] CPU Frequency scaling
				[*]   CPU frequency translation statistics


"CPU Frequency scaling" (CONFIG_CPU_FREQ) 应该被启用以配置cpufreq-stats。

"CPU frequency translation statistics" (CONFIG_CPU_FREQ_STAT)提供了包括
time_in_state、total_trans和trans_table的统计数据。

一旦启用了这个选项，并且你的CPU支持cpufrequency，你就可以在/sysfs中看到CPU频率统计。

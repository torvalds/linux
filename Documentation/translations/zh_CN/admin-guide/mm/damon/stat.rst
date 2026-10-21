.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/mm/damon/stat.rst

:翻译:

 Doehyun Baek <doehyunbaek@gmail.com>

====================
数据访问监测结果统计
====================

数据访问监测结果统计（DAMON_STAT）是一个静态内核模块，旨在用于简单的
访问模式监测。它使用 DAMON 监测系统整个物理内存上的访问，并提供简化的
访问监测结果统计，即空闲时间百分位数和估计的内存带宽。

.. _damon_stat_monitoring_accuracy_overhead_zh_CN:

监测精度和开销
==============

DAMON_STAT 使用监测间隔
:ref:`自动调优 <damon_design_monitoring_intervals_autotuning_zh_CN>` 来提高
精度并最小化开销。它会自动调优间隔，目标是在每个快照中捕获 4 % 的
可观测访问事件，同时将得到的采样间隔限制在最小 5 毫秒、最大 10 秒。
在少数生产服务器系统上，它的结果是只消耗 0.x % 的单个 CPU 时间，同时
捕获质量合理的访问模式。调优得到的间隔可以通过
``aggr_interval_us`` :ref:`参数 <damon_stat_aggr_interval_us_zh_CN>` 获取。

接口：模块参数
==============

要使用这个功能，首先应确保你的系统运行在构建时启用了
``CONFIG_DAMON_STAT=y`` 的内核上。通过将
``CONFIG_DAMON_STAT_ENABLED_DEFAULT`` 设置为 true，可以在构建时默认
启用该功能。

为了让系统管理员在启动时和/或运行时启用或禁用它，并读取监测结果，
DAMON_STAT 提供了模块参数。下面的章节描述这些参数。

enabled
-------

启用或禁用 DAMON_STAT。

你可以将该参数的值设置为 ``Y`` 来启用 DAMON_STAT。设置为 ``N`` 会
禁用 DAMON_STAT。默认值由 ``CONFIG_DAMON_STAT_ENABLED_DEFAULT`` 构建
配置选项设置。

请注意，该模块（damon_stat）不能与其他基于 DAMON 的专用模块同时运行。
更多细节请参考 :ref:`DAMON 设计文档的专用模块互斥性 <damon_design_special_purpose_modules_exclusivity_zh_CN>`。

.. _damon_stat_aggr_interval_us_zh_CN:

aggr_interval_us
----------------

自动调优后的聚集时间间隔，单位是微秒。

用户可以读取 DAMON_STAT 使用的 DAMON 实例的聚集间隔。它会被
:ref:`自动调优 <damon_stat_monitoring_accuracy_overhead_zh_CN>`，因此该值
会动态变化。

estimated_memory_bandwidth
--------------------------

系统的估计内存带宽消耗（字节/秒）。

DAMON_STAT 读取当前 DAMON 结果快照上的观测访问事件，并将其转换为以
字节/秒为单位的内存带宽消耗估计。得到的指标通过这个只读参数向用户
公开。由于 DAMON 使用采样，所以这只是访问强度的估计，而不是精确的
内存带宽。

memory_idle_ms_percentiles
--------------------------

系统的逐字节空闲时间（毫秒）百分位数。

DAMON_STAT 基于当前 DAMON 结果快照，计算内存中每个字节到现在为止未被
访问的时间（空闲时间）。对于访问频率（nr_accesses）大于零的区域，当前
访问频率水平保持的时间乘以 ``-1``，就是该区域每个字节的空闲时间。如果
某个区域的访问频率（nr_accesses）为零，则该区域保持零访问频率的时间
（age）就是该区域每个字节的空闲时间。然后，DAMON_STAT 通过这个只读参数
公开空闲时间值的百分位数。读取该参数会返回 101 个以毫秒为单位、用逗号
分隔的空闲时间值。每个值分别表示第 0、第 1、第 2、第 3、……、第 99 和
第 100 百分位的空闲时间。

kdamond_pid
-----------

DAMON 线程的 PID。

如果 DAMON_STAT 已启用，这将成为工作线程的 PID。否则为 -1。

.. SPDX-License-Identifier: GPL-2.0

:Original: Documentation/mm/damon/design.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


====
设计
====


.. _damon_design_execution_model_and_data_structures_zh_CN:

执行模型和数据结构
==================

与监测相关的信息，包括监测请求规格和基于 DAMON 的操作方案，都存储在名为
DAMON ``context`` 的数据结构中。DAMON 使用名为 ``kdamond`` 的内核线程执行
每个上下文。多个 kdamond 可以并行运行，用于不同类型的监测。

要了解用户空间如何进行配置并启动或停止 DAMON，请参考 :ref:`DAMON sysfs
接口 <sysfs_interface_zh_CN>` 文档。

用户还可以请求暂停和恢复每个上下文的执行。当上下文被暂停时，kdamond 除了应用
在线参数更新外不做任何事情。

要了解用户空间如何暂停或恢复每个上下文，请参考 :ref:`DAMON sysfs 上下文
<sysfs_context_zh_CN>` 使用文档。

总体架构
========

DAMON 子系统由三层构成，包括

- :ref:`操作集 <damon_operations_set_zh_CN>`：实现依赖于给定监测目标地址空间
  和可用软硬件基元集合的 DAMON 基本操作，
- :ref:`核心 <damon_core_logic_zh_CN>`：在操作集层之上，实现包括监测开销/准确性
  控制和访问感知系统操作在内的核心逻辑，以及
- :ref:`模块 <damon_modules_zh_CN>`：在核心层之上，实现用于各种目的、并向用户空间
  提供接口的内核模块。


.. _damon_operations_set_zh_CN:

操作集层
========

.. _damon_design_configurable_operations_set_zh_CN:

为了进行数据访问监测和其他低层工作，DAMON 需要一组针对给定目标地址空间、
并依赖于和优化于该地址空间的特定操作实现。例如，下面两个用于访问监测的操作
依赖于地址空间。

1. 确定该地址空间的监测目标地址范围。
2. 检查目标空间中特定地址范围的访问。

DAMON 将这些实现整合到称为 DAMON 操作集的层中，并定义它和上层之间的接口。
上层专用于 DAMON 的核心逻辑，包括控制监测准确性和开销的机制。

因此，DAMON 可以通过配置核心逻辑使用适当的操作集，轻松扩展到任意地址空间
和/或可用硬件功能。如果给定目的没有可用操作集，也可以按照层间接口实现新的
操作集。

例如，物理内存、虚拟内存、交换空间、特定进程、NUMA 节点、文件和后端内存设备
都可以被支持。另外，如果某些架构或设备支持特殊的优化访问检查功能，也可以很容易
进行配置。

DAMON 当前提供以下三个操作集。下面三个小节描述它们如何工作。

 - vaddr：监测特定进程的虚拟地址空间
 - fvaddr：监测固定虚拟地址范围
 - paddr：监测系统的物理地址空间

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 进行配置，
请参考文档的 :ref:`operations <sysfs_context_zh_CN>` 文件部分。


.. _damon_design_vaddr_target_regions_construction_zh_CN:

基于VMA的目标地址范围构造
-------------------------

``vaddr`` DAMON 操作集的一种机制，会自动初始化并更新监测目标地址区域，
以覆盖目标进程的整个内存映射。

该机制仅用于 ``vaddr`` 操作集。对于 ``fvaddr`` 和 ``paddr`` 操作集，
用户需要手动设置监测目标地址范围。

在进程的超级巨大的虚拟地址空间中，只有小部分被映射到物理内存并被访问。因此，跟踪未映射的地
址区域只是一种浪费。然而，由于DAMON可以使用自适应区域调整机制来处理一定程度的噪声，所以严
格来说，跟踪每一个映射并不是必须的，但在某些情况下甚至会产生很高的开销。也就是说，监测目标
内部过于巨大的未映射区域应该被移除，以不占用自适应机制的时间。

出于这个原因，这个实现将复杂的映射转换为三个不同的区域，覆盖地址空间的每个映射区域。这三个
区域之间的两个空隙是给定地址空间中两个最大的未映射区域。这两个最大的未映射区域是堆和最上面
的mmap()区域之间的间隙，以及在大多数情况下最下面的mmap()区域和栈之间的间隙。因为这些间隙
在通常的地址空间中是异常巨大的，排除这些间隙就足以做出合理的权衡。下面详细说明了这一点::

    <heap>
    <BIG UNMAPPED REGION 1>
    <uppermost mmap()-ed region>
    (small mmap()-ed regions and munmap()-ed regions)
    <lowermost mmap()-ed region>
    <BIG UNMAPPED REGION 2>
    <stack>


基于PTE访问位的访问检查
-----------------------

物理和虚拟地址空间的实现都使用PTE Accessed-bit进行基本访问检查。唯一的区别在于从地址中
找到相关的PTE访问位的方式。虚拟地址的实现是为该地址的目标任务查找页表，而物理地址的实现则
是查找与该地址有映射关系的每一个页表。通过这种方式，实现者找到并清除下一个采样目标地址的位，
并检查该位是否在一个采样周期后再次设置。这可能会干扰其他使用访问位的内核子系统，即空闲页跟
踪和回收逻辑。DAMON 不做任何事情来避免干扰空闲页跟踪，因此处理这种干扰是系统管理员的责任。
不过，它会像空闲页跟踪一样，使用 ``PG_idle`` 和 ``PG_young`` 页面标志解决与回收逻辑的冲突。


.. _damon_design_addr_unit_zh_CN:

地址单位
--------

DAMON 核心层使用 ``unsigned long`` 类型表示监测目标地址范围。在某些情况下，
给定操作集的地址空间可能过大，无法用该类型处理。带有大物理地址扩展的
ARM（32 位）就是一个例子。对于这类情况，提供了一个称为 ``address unit`` 的
逐操作集参数。它表示一个缩放因子，需要乘以核心层地址，才能计算给定地址
空间中的真实地址。是否支持 ``address unit`` 参数取决于每个操作集实现。
``paddr`` 是唯一支持该参数的操作集实现。

如果该值小于 ``PAGE_SIZE``，则只能使用 2 的幂。


.. _damon_core_logic_zh_CN:

核心逻辑
========

.. _damon_design_monitoring_zh_CN:

监测
----

下面四个部分分别描述了DAMON的核心机制和五个监测属性，即 ``采样间隔`` 、 ``聚集间隔`` 、
``更新间隔`` 、 ``最小区域数`` 和 ``最大区域数`` 。

请注意，``最小区域数`` 必须为 3 或更高。这是因为虚拟地址空间监测被设计为至少处理三个区域，
以容纳普通虚拟地址空间中常见的两个大型未映射区域。虽然对 ``paddr`` 这类其他操作集来说，
这一限制可能并非严格必要，但为了保持一致性，目前会对所有 DAMON 操作强制执行。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置这些属性，
请参考文档的 :ref:`monitoring_attrs <sysfs_monitoring_attrs_zh_CN>` 部分。


访问频率监测
~~~~~~~~~~~~

DAMON的输出显示了在给定的时间内哪些页面的访问频率是多少。访问频率的分辨率是通过设置
``采样间隔`` 和 ``聚集间隔`` 来控制的。详细地说，DAMON检查每个 ``采样间隔`` 对每
个页面的访问，并将结果汇总。换句话说，计算每个页面的访问次数。在每个 ``聚集间隔`` 过
去后，DAMON调用先前由用户注册的回调函数，以便用户可以阅读聚合的结果，然后再清除这些结
果。这可以用以下简单的伪代码来描述::

    while monitoring_on:
        for page in monitoring_target:
            if accessed(page):
                nr_accesses[page] += 1
        if time() % aggregation_interval == 0:
            for callback in user_registered_callbacks:
                callback(monitoring_target, nr_accesses)
            for page in monitoring_target:
                nr_accesses[page] = 0
        sleep(sampling interval)

这种机制的监测开销将随着目标工作负载规模的增长而任意增加。


.. _damon_design_region_based_sampling_zh_CN:

基于区域的抽样调查
~~~~~~~~~~~~~~~~~~

为了避免开销的无限制增加，DAMON将假定具有相同访问频率的相邻页面归入一个区域。只要保持
这个假设（一个区域内的页面具有相同的访问频率），该区域内就只需要检查一个页面。因此，对
于每个 ``采样间隔`` ，DAMON在每个区域中随机挑选一个页面，等待一个 ``采样间隔`` ，检
查该页面是否同时被访问，如果被访问则增加该区域的访问频率计数器。该计数器称为区域的
``nr_accesses``。因此，监测开销是可以通过设置区域的数量来控制的。DAMON允许用户设置最小
和最大的区域数量来进行权衡。

然而，如果假设没有得到保证，这个方案就不能保持输出的质量。


.. _damon_design_adaptive_regions_adjustment_zh_CN:

适应性区域调整
~~~~~~~~~~~~~~

即使最初的监测目标区域被很好地构建以满足假设（同一区域内的页面具有相似的访问频率），数
据访问模式也会被动态地改变。这将导致监测质量下降。为了尽可能地保持假设，DAMON根据每个
区域的访问频率自适应地进行合并和拆分。

对于每个 ``聚集间隔`` ，它比较相邻区域的访问频率（``nr_accesses``）。如果差异较小，
并且两个区域的大小之和小于总区域大小除以 ``最小区域数``，DAMON 就会合并这两个区域。如果
合并后的总区域数仍然高于 ``最大区域数``，它会增大访问频率差异阈值并重复合并，直到满足区域
数上限，或者阈值高于可能的最大值（``聚集间隔`` 除以 ``采样间隔``）。然后，在它报告并清除
每个区域的聚合访问频率后，如果拆分后的区域总数不超过用户指定的最大区域数，它会把每个区域
拆分为两个或三个区域。

通过这种方式，DAMON提供了其最佳的质量和最小的开销，同时保持了用户为其权衡设定的界限。

.. _damon_design_age_tracking_zh_CN:

年龄跟踪
~~~~~~~~

通过分析监测结果，用户还可以发现某个区域当前的访问模式已经保持了多长时间。这可用于更好地理解
访问模式。例如，可以利用访问频率和时近性实现页面放置算法。为了让这种访问模式保持时间分析更容
易，DAMON 在每个区域中维护另一个名为 ``age`` 的计数器。对于每个 ``聚集间隔``，DAMON 检查该
区域的大小和访问频率（``nr_accesses``）是否发生了显著变化。如果发生了变化，该计数器会被重置为
零。否则，该计数器会增加。


.. _damon_design_data_attrs_monitoring_zh_CN:

数据属性监测
~~~~~~~~~~~~

数据访问模式只是数据属性的一种类型。在某些用例中，用户需要了解更多数据属性
信息。例如，用户可能需要知道给定热或冷内存区域中有多少由匿名页支持，或者属于
某个特定 cgroup。为此，提供了数据属性监测功能。

使用该功能时，用户可以把感兴趣的数据属性注册到 DAMON :ref:`上下文
<damon_design_execution_model_and_data_structures_zh_CN>` 中。注册通过为每个属性
指定一个探针完成。每个探针指定一个规则，用于判断给定内存区域是否具有相关
属性。该规则由多个过滤器构成。除支持的过滤器类型不同外，这些过滤器的工作方式
与 :ref:`DAMOS 过滤器 <damon_design_damos_filters_zh_CN>` 相同。目前，数据属性
监测只支持 ``anon`` 和 ``memcg`` 过滤器类型。

如果注册了这类探针，DAMON 在进行访问 :ref:`采样
<damon_design_region_based_sampling_zh_CN>` 时，会对每个区域的采样内存执行这些
探针。每个 :ref:`聚集间隔 <damon_design_monitoring_zh_CN>` 中被识别为具有数据
属性（命中探针）的样本数量，会计入每区域、每探针的计数器。因此，用户可以在
每个聚集间隔后读取每区域、每探针的探针命中计数器，从而了解给定 DAMON 区域
有多少具有特定数据属性。

这是基于采样的机制。因此，它很轻量，但输出可能包含一些测量误差。用户应在
充分理解统计意义的基础上使用输出。

另一种更高精度的方式是使用 ``stat`` :ref:`动作
<damon_design_damos_action_zh_CN>` 的 :ref:`DAMOS 过滤器
<damon_design_damos_filters_zh_CN>`，并使用 ``sz_ops_filter_passed`` :ref:`统计
<damon_design_damos_stat_zh_CN>`。这种方式以页级别提供数据属性信息。不过，由于
它在页级别操作，开销与内存大小成正比。

动态目标空间更新处理
~~~~~~~~~~~~~~~~~~~~

监测目标地址范围可以动态改变。例如，虚拟内存可以动态地被映射和解映射。物理内存可以被
热插拔。

由于在某些情况下变化可能相当频繁，DAMON允许监控操作检查动态变化，包括内存映射变化，
并仅在用户指定的时间间隔（ ``更新间隔`` ）中的每个时间段，将其应用于监控操作相关的
数据结构，如抽象的监控目标内存区。

用户空间可以通过 DAMON sysfs 接口和/或跟踪点获取监测结果。更多细节请分别参考
:ref:`DAMOS 尝试区域 <sysfs_schemes_tried_regions_zh_CN>` 和 :ref:`跟踪点 <tracepoint_zh_CN>`
文档。


.. _damon_design_monitoring_params_tuning_guide_zh_CN:

监测参数调优指南
~~~~~~~~~~~~~~~~

简而言之，应设置 ``聚集间隔``，使其能够为使用目的捕获有意义数量的访问。访问数量可以用聚集后
监测结果快照中各区域的 ``nr_accesses`` 和 ``age`` 来衡量。该间隔的默认值 ``100ms`` 在许多情况
下被证明过短。应按 ``聚集间隔`` 的比例设置 ``采样间隔``。默认推荐比例为 ``1/20``。

``聚集间隔`` 应设置为工作负载可在该间隔内为监测目的产生一定数量访问的时间间隔。如果该间隔过短，
只能捕获少量访问。结果是，监测结果会看起来像所有内容都同样只是很少被访问。对许多目的而言，这
将毫无用处。不过，如果该间隔过长，根据给定目的的时间尺度，区域通过 :ref:`区域调整机制
<damon_design_adaptive_regions_adjustment_zh_CN>` 收敛所需的时间可能过长。如果工作负载实际只产生
很少访问，而用户却认为监测目的所需的访问数量很高，就可能发生这种情况。对于这种情况，应仔细重
新考虑每个 ``聚集间隔`` 要捕获的目标访问数量。还要注意，捕获的访问数量不仅用 ``nr_accesses``
表示，也用 ``age`` 表示。例如，即使监测结果中的每个区域都显示 ``nr_accesses`` 为零，仍然可以
用 ``age`` 值作为时近性信息来区分区域。

因此，``聚集间隔`` 的最佳值取决于工作负载的访问密集程度。用户应根据每个聚集后的监测结果快照
中捕获的访问数量来调优该间隔。

请注意，该间隔的默认值是 100 毫秒，在许多情况下都太短，尤其是在大型系统上。

``采样间隔`` 定义每次聚集的分辨率。如果它设置得过大，监测结果会看起来像每个区域都同样很少被
访问，或者同样频繁地被访问。也就是说，区域将无法根据访问模式区分，因此结果在许多用例中都会无
用。如果 ``采样间隔`` 过小，它不会降低分辨率，但会增加监测开销。如果它已经足以为给定目的提供
足够的监测结果分辨率，就不应再不必要地降低。建议将它按 ``聚集间隔`` 的比例设置。默认比例设为
``1/20``，并且仍然推荐该比例。

基于手动调优指南，DAMON 提供了更直观的、基于调节项的间隔自动调优机制。更多细节请参考
:ref:`该功能的设计文档 <damon_design_monitoring_intervals_autotuning_zh_CN>`。

基于上述指南的示例调优，请参考英文文档 Documentation/mm/damon/monitoring_intervals_tuning_example.rst。


.. _damon_design_monitoring_intervals_autotuning_zh_CN:

监测间隔自动调优
~~~~~~~~~~~~~~~~

DAMON 基于 :ref:`调优指南的思路 <damon_design_monitoring_params_tuning_guide_zh_CN>`，提供了
对 ``采样间隔`` 和 ``聚集间隔`` 的自动调优机制。该调优机制允许用户设置希望 DAMON 在给定时间间隔
内观测到的访问事件数量目标。用户可以把该目标指定为 DAMON 观测到的访问事件数量与理论最大事件数
量之间的比例（``access_bp``），该比例在给定数量的聚集中测量（``aggrs``）。

DAMON 观测到的访问事件基于 DAMON :ref:`区域假设 <damon_design_region_based_sampling_zh_CN>`
按字节粒度计算。例如，如果发现大小为 ``X`` 字节、``nr_accesses`` 为 ``Y`` 的区域，就意味着
DAMON 观测到了 ``X * Y`` 个访问事件。该区域的理论最大访问事件也以相同方式计算，但会把 ``Y``
替换为理论最大 ``nr_accesses``，即 ``聚集间隔 / 采样间隔``。

该机制会计算 ``aggrs`` 次聚集期间的访问事件比例。如果观测到的访问比例低于或高于目标值，就按
相同比例增大或减小 ``采样间隔`` 和 ``聚集间隔``。间隔变化比例按当前采样比例与目标比例之间的
距离决定。

用户还可以通过两个参数（``min_sample_us`` 和 ``max_sample_us``）进一步设置调优机制可设置的
最小和最大 ``采样间隔``。由于调优机制总是以相同比例改变 ``采样间隔`` 和 ``聚集间隔``，所以
每次调优变化后的最小和最大 ``聚集间隔`` 也可以自动一起设置。

该调优默认关闭，需要由用户显式设置。根据经验法则和帕累托原则，推荐使用 4% 的访问样本比例目标。
请注意，这里应用了两次帕累托原则（80/20 规则）。也就是说，假设以 4%（20% 的 20%）的 DAMON
观测访问事件比例（来源），捕获 64%（80% 乘以 80%）的真实访问事件（结果）。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 使用该功能，请参考文档
的 :ref:`intervals_goal <damon_usage_sysfs_monitoring_intervals_goal_zh_CN>` 部分。


.. _damon_design_damos_zh_CN:

操作方案
--------

数据访问监测的一个常见目的，是实现访问感知的系统效率优化。例如：

    换出超过两分钟未被访问的内存区域

或者：

    对大于 2 MiB 且显示高访问频率超过一分钟的内存区域使用 THP。

这类方案的一种直接做法是基于剖析的优化。也就是说，使用 DAMON 获取工作负载
或系统的数据访问监测结果，通过分析监测结果找到具有特殊特征的内存区域，并针对
这些区域进行系统操作变更。这些变更可以通过修改软件（应用程序和/或内核）或向
其提供建议来完成，也可以通过重新配置硬件来完成。离线和在线两种方式都可以使用。

其中，在运行时向内核提供建议会比较灵活且有效，因此被广泛使用。不过，实现这类
方案可能引入不必要的冗余和低效。如果关注的类型很常见，剖析可能是冗余的。在
内核和用户空间之间交换包括监测结果和操作建议在内的信息，也可能效率较低。

为了让用户通过移交这些工作来减少这种冗余和低效，DAMON 提供了一个名为基于
数据访问监测的操作方案（DAMOS）的功能。它允许用户以高层次指定期望的方案。
对于这类规格，DAMON 会开始监测，找到具有感兴趣访问模式的区域，并在每个用户
指定的时间间隔（称为 ``apply_interval``）把用户期望的操作动作应用于这些区域。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置
``apply_interval``，请参考文档的 :ref:`apply_interval_us <sysfs_scheme_zh_CN>`
部分。


.. _damon_design_damos_action_zh_CN:

操作动作
~~~~~~~~

用户希望应用到其感兴趣区域的管理动作。例如，换出、为下一次回收受害者选择提高
优先级、建议 ``khugepaged`` 折叠或拆分，或者什么也不做而只收集区域统计信息。

支持的动作列表在 DAMOS 中定义，但每个动作的实现位于 DAMON 操作集层，因为实现
通常依赖于监测目标地址空间。例如，换出特定虚拟地址范围的代码会不同于换出物理
地址范围的代码。监测操作实现集也不被要求支持列表中的所有动作。因此，特定 DAMOS
动作是否可用取决于选择一起使用的操作集。

支持的动作列表、含义以及支持每个动作的 DAMON 操作集如下。

 - ``willneed``：对带有 ``MADV_WILLNEED`` 的区域调用 ``madvise()``。
   ``vaddr`` 和 ``fvaddr`` 操作集支持。
 - ``cold``：对带有 ``MADV_COLD`` 的区域调用 ``madvise()``。
   ``vaddr`` 和 ``fvaddr`` 操作集支持。
 - ``pageout``：回收该区域。
   ``vaddr``、``fvaddr`` 和 ``paddr`` 操作集支持。
 - ``hugepage``：对带有 ``MADV_HUGEPAGE`` 的区域调用 ``madvise()``。
   ``vaddr`` 和 ``fvaddr`` 操作集支持。当禁用 TRANSPARENT_HUGEPAGE 时，
   应用该动作只会失败。
 - ``nohugepage``：对带有 ``MADV_NOHUGEPAGE`` 的区域调用 ``madvise()``。
   ``vaddr`` 和 ``fvaddr`` 操作集支持。当禁用 TRANSPARENT_HUGEPAGE 时，
   应用该动作只会失败。
 - ``collapse``：对带有 ``MADV_COLLAPSE`` 的区域调用 ``madvise()``。
   ``vaddr`` 和 ``fvaddr`` 操作集支持。当禁用 TRANSPARENT_HUGEPAGE 时，
   应用该动作只会失败。
 - ``lru_prio``：提高该区域在其 LRU 链表上的优先级。
   ``paddr`` 操作集支持。
 - ``lru_deprio``：降低该区域在其 LRU 链表上的优先级。
   ``paddr`` 操作集支持。
 - ``migrate_hot``：迁移区域，并优先迁移更热的区域。
   ``vaddr``、``fvaddr`` 和 ``paddr`` 操作集支持。
 - ``migrate_cold``：迁移区域，并优先迁移更冷的区域。
   ``vaddr``、``fvaddr`` 和 ``paddr`` 操作集支持。
 - ``stat``：什么也不做，只统计信息。
   所有操作集都支持。

把除 ``stat`` 外的动作应用到某个区域，会被认为改变了该区域的特征。因此，
当任何这类动作被应用到区域时，DAMOS 会重置这些区域的 age。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置
动作，请参考文档的 :ref:`action <sysfs_scheme_zh_CN>` 部分。


.. _damon_design_damos_access_pattern_zh_CN:

目标访问模式
~~~~~~~~~~~~

方案感兴趣的访问模式。这些模式由 DAMON 监测结果提供的属性构成，具体包括
大小、访问频率和年龄。用户可以通过设置这三个属性的最小值和最大值，描述
自己感兴趣的访问模式。如果某个区域的这三个属性都在范围内，DAMOS 就把它
分类为该方案感兴趣的区域之一。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置
访问模式，请参考文档的 :ref:`access_pattern <sysfs_access_pattern_zh_CN>`
部分。


.. _damon_design_damos_quotas_zh_CN:

配额
~~~~

DAMOS 的开销上界控制功能。如果目标访问模式没有正确调优，DAMOS 可能带来高开销。
例如，如果发现一个具有感兴趣访问模式的巨大内存区域，把方案动作应用到该巨大区域
的所有页面可能消耗不可接受的大量系统资源。通过调优访问模式来防止这类问题可能
很有挑战，特别是在工作负载的访问模式高度动态时。

为缓解这种情况，DAMOS 提供了一个称为配额的开销上界控制功能。它允许用户指定
DAMOS 可用于应用动作的时间上限，和/或在用户指定的时间段内可应用动作的最大内存
区域字节数。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置基本
配额，请参考文档的 :ref:`quotas <sysfs_quotas_zh_CN>` 部分。


.. _damon_design_damos_quotas_prioritization_zh_CN:

优先级
^^^^^^

一种在配额限制下作出良好决策的机制。当由于配额限制而无法把动作应用到所有感兴趣
区域时，DAMOS 会对区域排序，并只把动作应用到优先级足够高、且不会超过配额的区域。

每个动作的优先级机制应该不同。例如，很少被访问（更冷）的内存区域应在 page-out
方案动作中被优先处理。相反，对于大页折叠方案动作，更冷的区域应被降低优先级。
因此，每个动作的优先级机制与动作一起，在各个 DAMON 操作集中实现。

虽然实现取决于 DAMON 操作集，但通常会使用区域的访问模式属性来计算优先级。有些
用户可能希望针对自己的特定场景个性化这些机制。例如，有些用户可能希望该机制更重视
时近性（``age``）而不是访问频率（``nr_accesses``）。DAMOS 允许用户指定每个访问模式
属性的权重，并把这些信息传递给底层机制。不过，权重如何被尊重，甚至是否被尊重，
都取决于底层优先级机制实现。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置优先级
权重，请参考文档的 :ref:`weights <sysfs_quotas_zh_CN>` 部分。


.. _damon_design_damos_quotas_failed_memory_charging_ratio_zh_CN:

动作失败内存计费比率
^^^^^^^^^^^^^^^^^^^^

对给定区域执行 DAMOS 动作时，区域中某些内存子集可能失败。例如，如果动作是
``pageout``，而该区域包含一些不可回收页，则把该动作应用到这些页会失败。
这类失败动作应用消耗的系统资源量通常不同于成功动作应用。对于这类情况，用户可以
为失败内存设置不同的计费比率。该比率可以用 ``fail_charge_num`` 和
``fail_charge_denom`` 参数指定。这两个参数分别表示比率的分子和分母。只有当
``fail_charge_denom`` 不为零时，该功能才启用。

例如，假设某个 DAMOS 动作被应用到大小为 1,000 MiB 的区域。该动作只成功应用到
该区域的 700 MiB。``fail_charge_num`` 和 ``fail_charge_denom`` 分别设置为 ``1``
和 ``1024``。那么只会计费 700 MiB 和 300 KiB 的大小（``700 MiB + 300 MiB * 1 /
1024``）。


.. _damon_design_damos_quotas_auto_tuning_zh_CN:

面向目标的反馈驱动自动调优
^^^^^^^^^^^^^^^^^^^^^^^^^^

自动反馈驱动的配额调优。用户可以不设置绝对配额值，而是指定自己感兴趣的指标，
以及希望该指标达到的目标值。随后，DAMOS 会自动调优对应方案的激进程度（配额）。
例如，如果 DAMOS 未达到目标，DAMOS 会自动增加配额。如果 DAMOS 超过目标，则会
减少配额。

用户可以按需要选择两种这样的调优算法。

- ``consist``：基于比例反馈环的算法。尝试找到一个应持续保持的最佳配额，以便持续
  达成目标。适用于动态和长时间运行环境中的内核内部操作。这是默认选择。如果不确定，
  请使用它。
- ``temporal``：更直接的算法。尝试尽快达成目标，使用允许的最大配额，但只持续短暂
  时间。当配额未达成时，该算法持续把配额调优到允许的最大值。一旦配额[超过]达成，
  它就把配额设置为零。适用于需要确定性控制的环境。

目标可以用五个参数指定，即 ``target_metric``、``target_value``、``current_value``、
``nid`` 和 ``path``。自动调优机制尝试使 ``target_metric`` 的 ``current_value``
等于 ``target_value``。

- ``user_input``：用户提供的值。用户可以使用任何自己感兴趣的指标作为该值。用户空间
  主工作负载的延迟或吞吐量、空闲内存率或内存压力停滞时间（PSI）等系统指标都可以
  作为示例。请注意，在这种情况下，用户应自己显式设置 ``current_value``。换言之，
  用户应反复提供反馈。
- ``some_mem_psi_us``：系统范围的 ``some`` 内存压力停滞信息，单位为微秒，从上一次
  配额重置到下一次配额重置之间测量。DAMOS 自行进行测量，所以用户只需要在初始时
  设置 ``target_value``。换言之，DAMOS 会自反馈。
- ``node_mem_used_bp``：特定 NUMA 节点的已用内存比率，单位为 bp（1/10,000）。
- ``node_mem_free_bp``：特定 NUMA 节点的空闲内存比率，单位为 bp（1/10,000）。
- ``node_memcg_used_bp``：特定 cgroup 在特定 NUMA 节点上的节点已用内存比率，
  单位为 bp（1/10,000）。
- ``node_memcg_free_bp``：特定 cgroup 在特定 NUMA 节点上的节点未用内存比率，
  单位为 bp（1/10,000）。
- ``active_mem_bp``：活跃内存相对活跃 + 非活跃（LRU）内存大小的比率，单位为 bp
  （1/10,000）。
- ``inactive_mem_bp``：非活跃内存相对活跃 + 非活跃（LRU）内存大小的比率，单位为 bp
  （1/10,000）。
- ``node_eligible_mem_bp``：节点中符合方案目标访问模式条件的内存比率，单位为 bp
  （1/10,000）。

``nid`` 仅在使用 ``node_mem_used_bp``、``node_mem_free_bp``、
``node_memcg_used_bp``、``node_memcg_free_bp`` 和 ``node_eligible_mem_bp`` 指标时
必须提供，用于指定具体 NUMA 节点。

``path`` 仅在使用 ``node_memcg_used_bp`` 和 ``node_memcg_free_bp`` 指标时必须提供，
用于指定 cgroup 路径。该值应为从 cgroup 挂载点起算的内存 cgroup 路径。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置调优
目标指标、目标值和/或当前值，请参考文档的 :ref:`quota goals
<sysfs_schemes_quota_goals_zh_CN>` 部分。


.. _damon_design_damos_watermarks_zh_CN:

水位
~~~~

条件式 DAMOS（去）激活自动化。用户可能希望 DAMOS 只在特定情况下运行。例如，
当保证有足够空闲内存时，运行主动回收方案只会消耗不必要的系统资源。为避免这类
消耗，用户需要手动监测一些指标（例如空闲内存率），并打开或关闭 DAMON/DAMOS。

DAMOS 允许用户使用三个水位移交这类工作。它允许用户配置自己感兴趣的指标，以及
三个水位值，即高、中和低。如果指标值高于高水位或低于低水位，该方案会被停用。
如果指标值低于中水位但高于低水位，该方案会被激活。如果所有方案都被水位停用，
监测也会被停用。在这种情况下，DAMON 工作线程只会周期性地检查水位，因此几乎
不会产生开销。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置水位，
请参考文档的 :ref:`watermarks <sysfs_watermarks_zh_CN>` 部分。


.. _damon_design_damos_filters_zh_CN:

过滤器
~~~~~~

非访问模式的目标内存区域过滤。如果用户运行自己编写的程序或拥有好的剖析工具，
他们可能比内核知道更多信息，例如未来的访问模式或针对特定内存类型的一些特殊需求。
例如，一些用户可能知道只有匿名页会影响其程序性能。他们也可以拥有一组延迟关键进程
列表。

为了让用户用这类特殊知识优化 DAMOS 方案，DAMOS 提供了一个称为 DAMOS 过滤器的功能。
该功能允许用户为每个方案设置任意数量的过滤器。每个过滤器指定：

- 一种内存类型（``type``），
- 它是针对该类型的内存，还是针对该类型以外的所有内存（``matching``），以及
- 是否允许（包含）或拒绝（排除）把方案动作应用到该内存（``allow``）。

为了高效处理过滤器，一些类型的过滤器由核心层处理，而其他类型由操作集处理。因此，
在后一种情况下，是否支持过滤器类型取决于 DAMON 操作集。对于核心层处理的过滤器，
被过滤器排除的内存区域不会计入方案已尝试应用的区域。相反，如果内存区域被操作集层
处理的过滤器过滤，它会计入方案已尝试。这种差异会影响统计信息。

安装多个过滤器时，由核心层处理的一组过滤器首先求值。之后，由操作层处理的一组过滤器
求值。每组过滤器内部按安装顺序求值。如果某部分内存匹配某个过滤器，后续过滤器会被
忽略。如果该部分因为没有匹配任何过滤器而通过过滤器求值阶段，则是否对其应用方案动作
取决于最后一个过滤器的 allow 类型。如果最后一个过滤器是允许型，该部分内存会被拒绝，
反之亦然。

例如，假设按顺序安装了 1）一个允许匿名页的过滤器，和 2）另一个拒绝年轻页的过滤器。
如果某个符合方案动作应用条件的区域中的页面是匿名页，不论它是否年轻，方案动作都会应用
到该页，因为它匹配第一个允许过滤器。如果该页不是匿名页但年轻，则方案动作不会应用，
因为第二个拒绝过滤器阻止了它。如果该页既不是匿名页也不年轻，由于没有匹配过滤器，
该页会通过过滤器求值阶段，并且动作会应用到该页。

当前支持以下 ``type`` 的过滤器。

- 核心层处理
    - addr
        - 应用于属于给定地址范围的页面。
    - target
        - 应用于属于给定 DAMON 监测目标的页面。
- 操作层处理，只有 ``paddr`` 操作集支持。
    - anon
        - 应用于包含未存储在文件中的数据的页面。
    - active
        - 应用于活跃页。
    - memcg
        - 应用于属于给定 cgroup 的页面。
    - young
        - 应用于自方案上次访问检查以来被访问过的页面。
    - hugepage_size
        - 应用于以给定大小范围管理的页面。
    - unmapped
        - 应用于未映射的页面。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 设置过滤器，
请参考文档的 :ref:`filters <sysfs_filters_zh_CN>` 部分。

.. _damon_design_damos_stat_zh_CN:

统计信息
~~~~~~~~

DAMOS 行为的统计信息，用于帮助监测、调优和调试 DAMOS。

DAMOS 从方案执行开始起，为每个方案统计以下信息。

- ``nr_tried``：方案尝试应用的区域总数。
- ``sz_tried``：方案尝试应用的区域总大小。
- ``sz_ops_filter_passed``：通过操作集层处理的 DAMOS 过滤器的总字节数。
- ``nr_applied``：方案已应用的区域总数。
- ``sz_applied``：方案已应用的区域总大小。
- ``qt_exceeds``：方案配额被超过的总次数。
- ``nr_snapshots``：方案尝试应用的 DAMON 快照总数。
- ``max_nr_snapshots``：``nr_snapshots`` 的上限。

“方案尝试应用到某个区域”表示 DAMOS 核心逻辑判断该区域符合应用方案
:ref:`动作 <damon_design_damos_action_zh_CN>` 的条件。核心逻辑处理的
:ref:`访问模式 <damon_design_damos_access_pattern_zh_CN>`、:ref:`配额
<damon_design_damos_quotas_zh_CN>`、:ref:`水位 <damon_design_damos_watermarks_zh_CN>`
和 :ref:`过滤器 <damon_design_damos_filters_zh_CN>` 可能影响这一判断。核心逻辑
只会请求底层 :ref:`操作集 <damon_operations_set_zh_CN>` 对该区域应用动作，因此
该动作是否真正应用并不明确。这就是它被称为“尝试”的原因。

“方案应用到某个区域”表示 :ref:`操作集 <damon_operations_set_zh_CN>` 已经把动作
应用到该区域的至少一部分。操作集处理的 :ref:`过滤器
<damon_design_damos_filters_zh_CN>`、:ref:`动作 <damon_design_damos_action_zh_CN>`
类型以及该区域中的页面类型都可能影响这一点。例如，如果过滤器设置为排除匿名页且
该区域只有匿名页，或者动作是 ``pageout`` 而该区域的所有页面都不可回收，则把动作
应用到该区域会失败。

不同于普通统计信息，``max_nr_snapshots`` 由用户设置。如果它设置为非零，且
``nr_snapshots`` 等于或大于 ``max_nr_snapshots``，该方案会被停用。

要了解用户空间如何通过 :ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 读取统计
信息，请参考文档的 :ref:`stats <sysfs_schemes_stats_zh_CN>` 部分。


区域遍历
~~~~~~~~

DAMOS 功能允许用户访问刚刚应用了 DAMOS 动作的每个区域。使用该功能，DAMON
:ref:`API <damon_design_api_zh_CN>` 允许用户访问这些区域的完整属性，包括访问监测结果以及
区域内部通过 DAMOS 过滤器的内存量。:ref:`DAMON sysfs 接口 <sysfs_interface_zh_CN>` 也允许
用户通过特殊 :ref:`文件 <sysfs_schemes_tried_regions_zh_CN>` 读取这些数据。

.. _damon_design_api_zh_CN:

应用编程接口
------------

用于内核空间数据访问感知应用的编程接口。DAMON 是一个框架，所以它本身什么也不做。相反，
它只帮助子系统和模块等其他内核组件使用 DAMON 的核心功能构建它们的数据访问感知应用。为此，
DAMON 通过其应用编程接口，即 ``include/linux/damon.h``，向其他内核组件暴露所有功能。接口
细节请参考 API :doc:`文档 <api>`。


.. _damon_modules_zh_CN:

模块
====

由于 DAMON 的核心是供内核组件使用的框架，它本身不向用户空间提供任何直接接口。相反，这些接口
应由每个使用 DAMON API 的内核组件来实现。DAMON 子系统本身实现了这类 DAMON API 用户模块，
它们分别用于通用目的的 DAMON 控制和特定目的的数据访问感知系统操作，并为用户空间提供稳定的应
用二进制接口（ABI）。用户空间可以使用这些接口构建高效的数据访问感知应用程序。


通用目的用户接口模块
--------------------

DAMON 模块为运行时的通用目的 DAMON 用法提供用户空间 ABI。

与许多其他 ABI 一样，这些模块会在类似 ``sysfs`` 的伪文件系统上创建文件，允许用户通过写入和读
取这些文件来向 DAMON 指定请求并获取回答。作为这类 I/O 的响应，DAMON 用户接口模块会通过 DAMON
API 按用户请求控制 DAMON 并获取结果，然后把结果返回给用户空间。

这些 ABI 是为用户空间应用程序开发而设计的，而不是为人工手动操作而设计的。建议人工用户使用这类
用户空间工具。一个用 Python 编写的此类用户空间工具可在 Github
（https://github.com/damonitor/damo）、Pypi（https://pypistats.org/packages/damo）以及多个发行
版（https://repology.org/project/damo/versions）中获取。

目前，该类型有一个模块可用，即 ``DAMON sysfs interface``。关于接口细节，请参考 ABI
:ref:`文档 <sysfs_interface_zh_CN>`。


.. _damon_modules_special_purpose_zh_CN:

专用访问感知内核模块
--------------------

DAMON 模块为特定目的的 DAMON 用法提供用户空间 ABI。

DAMON 用户接口模块用于在运行时完整控制所有 DAMON 功能。对于每种专用的、系统范围的数据访问感知
系统操作，例如主动回收或 LRU 链表均衡，可以通过移除该特定目的不需要的调节项来简化接口，并扩展
为启动时甚至编译时控制。用于该用途的 DAMON 控制参数默认值也需要针对该目的进行优化。

为支持这些场景，DAMON 还提供了更多使用 DAMON API 的内核模块，它们提供更简单且更优化的用户空间
接口。目前提供了用于访问监测统计、主动回收和 LRU 链表操作的三个模块。更多细节请阅读这些模块的
使用文档（:doc:`../../admin-guide/mm/damon/stat`,
:doc:`../../admin-guide/mm/damon/reclaim` 和
:doc:`../../admin-guide/mm/damon/lru_sort`）。

.. _damon_design_special_purpose_modules_exclusivity_zh_CN:

请注意，这些模块当前以互斥方式运行。如果其中一个模块已经在运行，其他模块在收到启动请求时将返回
``-EBUSY``。

示例 DAMON 模块
----------------

DAMON 模块提供 DAMON 内核 API 用法示例。

内核程序员可以使用 DAMON 内核 API 构建自己的专用或通用目的 DAMON 模块。为了帮助他们容易理解
如何使用 DAMON 内核 API，Linux 源码树的 ``samples/damon/`` 目录下提供了一些示例模块。请注意，
这些模块不是为实际产品使用而开发的，而只是为了展示如何以简单方式使用 DAMON 内核 API。

.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/mm/damon/usage.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

========
详细用法
========

DAMON 为不同的用户提供了下面这些接口。

- *专用 DAMON 模块。*
  :ref:`这 <damon_modules_special_purpose_zh_CN>` 是为那些正在构建、发布和/或管理
  带有专用 DAMON 用法的内核的人准备的。使用它，用户可以在构建、启动或运行时，
  以简单的方式为给定目的使用 DAMON 的主要功能。
- *DAMON用户空间工具。*
  `这 <https://github.com/damonitor/damo>`_ 为有这特权的人， 如系统管理员，希望有一个刚好
  可以工作的人性化界面。
  使用它，用户可以以人性化的方式使用DAMON的主要功能。不过，它可能不会为特殊情况进行高度调整。
  更多细节，请参考它的 `使用文档
  <https://github.com/damonitor/damo/blob/next/USAGE.md>`_。
- *sysfs接口。*
  :ref:`这 <sysfs_interface_zh_CN>` 是为那些希望更高级的使用DAMON的特权用户空间程序员准备的。
  使用它，用户可以通过读取和写入特殊的sysfs文件来使用DAMON的主要功能。因此，你可以编写和使
  用你个性化的DAMON sysfs包装程序，代替你读/写sysfs文件。  `DAMON用户空间工具
  <https://github.com/damonitor/damo>`_ 就是这种程序的一个例子。
- *内核空间编程接口。*
  :doc:`这 <../../../mm/damon/api>` 这是为内核空间程序员准备的。使用它，用户可以通过为你编写内
  核空间的DAMON应用程序，最灵活有效地利用DAMON的每一个功能。你甚至可以为各种地址空间扩展DAMON。
  详细情况请参考接口 :doc:`文件 <../../../mm/damon/api>`。

.. _sysfs_interface_zh_CN:

sysfs接口
=========
DAMON的sysfs接口是在定义 ``CONFIG_DAMON_SYSFS`` 时建立的。它在其sysfs目录下创建多
个目录和文件， ``<sysfs>/kernel/mm/damon/`` 。你可以通过对该目录下的文件进行写入和
读取来控制DAMON。

对于一个简短的例子，用户可以监测一个给定工作负载的虚拟地址空间，如下所示::

    # cd /sys/kernel/mm/damon/admin/
    # echo 1 > kdamonds/nr_kdamonds && echo 1 > kdamonds/0/contexts/nr_contexts
    # echo vaddr > kdamonds/0/contexts/0/operations
    # echo 1 > kdamonds/0/contexts/0/targets/nr_targets
    # echo $(pidof <workload>) > kdamonds/0/contexts/0/targets/0/pid_target
    # echo on > kdamonds/0/state

文件层次结构
------------

DAMON sysfs接口的文件层次结构如下图所示。在下图中，父子关系用缩进表示，每个目录有
``/`` 后缀，每个目录中的文件用逗号（","）分开。

.. parsed-literal::

    :ref:`/sys/kernel/mm/damon <sysfs_root_zh_CN>`/admin
    │ :ref:`kdamonds <sysfs_kdamonds_zh_CN>`/nr_kdamonds
    │ │ :ref:`0 <sysfs_kdamond_zh_CN>`/state,pid,refresh_ms
    │ │ │ :ref:`contexts <sysfs_contexts_zh_CN>`/nr_contexts
    │ │ │ │ :ref:`0 <sysfs_context_zh_CN>`/avail_operations,operations,addr_unit,
    │ │ │ │   pause
    │ │ │ │ │ :ref:`monitoring_attrs <sysfs_monitoring_attrs_zh_CN>`/
    │ │ │ │ │ │ intervals/sample_us,aggr_us,update_us
    │ │ │ │ │ │ │ intervals_goal/access_bp,aggrs,min_sample_us,max_sample_us
    │ │ │ │ │ │ nr_regions/min,max
    │ │ │ │ │ │ :ref:`probes <damon_usage_sysfs_probes_zh_CN>`/nr_probes
    │ │ │ │ │ │ │ 0/filters/nr_filters
    │ │ │ │ │ │ │ │ 0/type,matching,allow,path
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ │ ...
    │ │ │ │ │ :ref:`targets <sysfs_targets_zh_CN>`/nr_targets
    │ │ │ │ │ │ :ref:`0 <sysfs_target_zh_CN>`/pid_target,obsolete_target
    │ │ │ │ │ │ │ :ref:`regions <sysfs_regions_zh_CN>`/nr_regions
    │ │ │ │ │ │ │ │ :ref:`0 <sysfs_region_zh_CN>`/start,end
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ │ :ref:`schemes <sysfs_schemes_zh_CN>`/nr_schemes
    │ │ │ │ │ │ :ref:`0 <sysfs_scheme_zh_CN>`/action,target_nid,apply_interval_us
    │ │ │ │ │ │ │ :ref:`access_pattern <sysfs_access_pattern_zh_CN>`/
    │ │ │ │ │ │ │ │ sz/min,max
    │ │ │ │ │ │ │ │ nr_accesses/min,max
    │ │ │ │ │ │ │ │ age/min,max
    │ │ │ │ │ │ │ :ref:`quotas <sysfs_quotas_zh_CN>`/ms,bytes,reset_interval_ms,
    │ │ │ │ │ │ │     effective_bytes,goal_tuner,
    │ │ │ │ │ │ │     fail_charge_num,fail_charge_denom
    │ │ │ │ │ │ │ │ weights/sz_permil,nr_accesses_permil,age_permil
    │ │ │ │ │ │ │ │ :ref:`goals <sysfs_schemes_quota_goals_zh_CN>`/nr_goals
    │ │ │ │ │ │ │ │ │ 0/target_metric,target_value,current_value,nid,path
    │ │ │ │ │ │ │ :ref:`watermarks <sysfs_watermarks_zh_CN>`/metric,interval_us,high,mid,low
    │ │ │ │ │ │ │ :ref:`{core_,ops_,}filters <sysfs_filters_zh_CN>`/nr_filters
    │ │ │ │ │ │ │ │ 0/type,matching,allow,memcg_path,addr_start,addr_end,damon_target_idx,min,max
    │ │ │ │ │ │ │ :ref:`dests <damon_sysfs_dests_zh_CN>`/nr_dests
    │ │ │ │ │ │ │ │ 0/id,weight
    │ │ │ │ │ │ │ :ref:`stats <sysfs_schemes_stats_zh_CN>`/nr_tried,sz_tried,nr_applied,sz_applied,sz_ops_filter_passed,qt_exceeds,nr_snapshots,max_nr_snapshots
    │ │ │ │ │ │ │ :ref:`tried_regions <sysfs_schemes_tried_regions_zh_CN>`/total_bytes
    │ │ │ │ │ │ │ │ 0/start,end,nr_accesses,age,sz_filter_passed
    │ │ │ │ │ │ │ │ │ probes
    │ │ │ │ │ │ │ │ │ │ 0/hits
    │ │ │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ ...
    │ │ ...

.. _sysfs_root_zh_CN:

根
--

DAMON sysfs接口的根是 ``<sysfs>/kernel/mm/damon/`` ，它有一个名为 ``admin`` 的
目录。该目录包含特权用户空间程序控制DAMON的文件。拥有根权限的用户空间工具或deamons可以
使用这个目录。

.. _sysfs_kdamonds_zh_CN:

kdamonds/
---------

在 ``admin`` 目录下，有一个目录，即``kdamonds``，它有控制kdamonds的文件存在（更多
细节请参考 :ref:`设计 <damon_design_execution_model_and_data_structures_zh_CN>`）。在开始
时，这个目录只有一个文件，``nr_kdamonds``。向该文件写入一个数字（``N``），就会创建名为
``0`` 到 ``N-1`` 的子目录数量。每个目录代表每个kdamond。

.. _sysfs_kdamond_zh_CN:

kdamonds/<N>/
-------------

在每个kdamond目录中，存在三个文件（``state`` 、 ``pid`` 和 ``refresh_ms`` ）和一个目录( ``contexts`` )。

读取 ``state`` 时，如果kdamond当前正在运行，则返回 ``on`` ，如果没有运行则返回 ``off`` 。

用户可以向 ``state`` 文件写入以下kdamond命令。

- ``on``：开始运行。
- ``off``：停止运行。
- ``commit``：再次读取除 ``state`` 文件之外的 sysfs 文件中的用户输入。
  如果没有指定目标区域，也会忽略监测 :ref:`目标区域 <sysfs_regions_zh_CN>`
  输入。
- ``update_tuned_intervals``：用自动调优后的 ``sampling interval`` 和
  ``aggregation interval`` 更新该 kdamond 的 ``sample_us`` 和
  ``aggr_us`` 文件内容。更多细节请参考 :ref:`intervals_goal 章节
  <damon_usage_sysfs_monitoring_intervals_goal_zh_CN>`。
- ``commit_schemes_quota_goals``：读取基于 DAMON 的操作方案的
  :ref:`配额目标 <sysfs_schemes_quota_goals_zh_CN>`。
- ``update_schemes_stats``：更新该 kdamond 的每个基于 DAMON 的操作方案的
  统计文件内容。关于统计信息的细节，请参考 :ref:`stats 章节
  <sysfs_schemes_stats_zh_CN>`。
- ``update_schemes_tried_regions``：为该 kdamond 的每个基于 DAMON 的
  操作方案，更新基于 DAMON 的操作方案动作尝试区域目录。关于基于 DAMON
  的操作方案动作尝试区域目录的细节，请参考 :ref:`tried_regions 章节
  <sysfs_schemes_tried_regions_zh_CN>`。
- ``update_schemes_tried_bytes``：只更新 ``.../tried_regions/total_bytes``
  文件。
- ``clear_schemes_tried_regions``：为该 kdamond 的每个基于 DAMON 的
  操作方案，清除基于 DAMON 的操作方案动作尝试区域目录。
- ``update_schemes_effective_quotas``：更新该 kdamond 的每个基于 DAMON
  的操作方案的 ``effective_bytes`` 文件内容。更多细节请参考
  :ref:`quotas 目录 <sysfs_quotas_zh_CN>`。

如果状态为 ``on``，读取 ``pid`` 显示kdamond线程的pid。

用户可以要求内核周期性地更新显示自动调优参数和 DAMOS 统计信息的文件，
而不是手动向 ``state`` 文件写入 ``update_tuned_intervals`` 这类关键字。
为此，用户应将所需的更新时间间隔（毫秒）写入 ``refresh_ms`` 文件。如果
该间隔为零，则禁用周期性更新。读取该文件会显示当前设置的时间间隔。

``contexts`` 目录包含控制这个kdamond要执行的监测上下文的文件。

.. _sysfs_contexts_zh_CN:

kdamonds/<N>/contexts/
----------------------

在开始时，这个目录只有一个文件，即 ``nr_contexts`` 。向该文件写入一个数字( ``N`` )，就会创
建名为``0`` 到 ``N-1`` 的子目录数量。每个目录代表每个监测上下文（更多细节请参考
:ref:`设计 <damon_design_execution_model_and_data_structures_zh_CN>`）。目前，每个kdamond只支持
一个上下文，所以只有 ``0`` 或 ``1`` 可以被写入文件。

.. _sysfs_context_zh_CN:

contexts/<N>/
-------------

在每个上下文目录中，存在四个文件(``avail_operations``、``operations``、
``addr_unit`` 和 ``pause``)和三个目录(``monitoring_attrs``,
``targets``, 和 ``schemes``)。

DAMON支持多种 :ref:`监测操作 <damon_design_configurable_operations_set_zh_CN>` 类型，
包括对虚拟地址空间和物理地址空间的监测。你可以通过读取 ``avail_operations`` 文件，
获取当前运行内核上可用的监测操作集列表。根据内核配置，该文件会列出不同的可用
操作集。所有可用操作集及其简要说明请参考 :ref:`设计 <damon_operations_set_zh_CN>`。

你可以通过向 ``operations`` 文件写入 ``avail_operations`` 文件中列出的一个关键词，
并从 ``operations`` 文件中读取，来设置和获取DAMON将为上下文使用何种类型的监测操作。

``addr_unit`` 文件用于设置和获取该操作集的 :ref:`地址单位 <damon_design_addr_unit_zh_CN>` 参数。

``pause`` 文件用于设置和获取该上下文的 :ref:`暂停请求 <damon_design_execution_model_and_data_structures_zh_CN>` 参数。

.. _sysfs_monitoring_attrs_zh_CN:

contexts/<N>/monitoring_attrs/
------------------------------

用于指定监测属性的文件，包括所需的监测质量和效率，都在 ``monitoring_attrs`` 目录中。
具体来说，这个目录下有三个目录，即 ``intervals``、``nr_regions`` 和 ``probes``。

在 ``intervals`` 目录下，存在DAMON的采样间隔(``sample_us``)、聚集间隔(``aggr_us``)
和更新间隔(``update_us``)三个文件。你可以通过写入和读出这些文件来设置和获取微秒级的值。

在 ``nr_regions`` 目录下，有两个文件分别用于DAMON监测区域的下限和上限（``min`` 和 ``max`` ），
这两个文件控制着监测的开销。你可以通过向这些文件的写入和读出来设置和获取这些值。

关于间隔和监测区域范围的更多细节，请参考设计文件 (:ref:`设计 <damon_design_monitoring_zh_CN>`)。

.. _damon_usage_sysfs_monitoring_intervals_goal_zh_CN:

contexts/<N>/monitoring_attrs/intervals/intervals_goal/
-------------------------------------------------------

在 ``intervals`` 目录下，还存在一个用于自动调优 ``sample_us`` 和
``aggr_us`` 的目录，即 ``intervals_goal`` 目录。在该目录下，存在四个
用于自动调优控制的文件，即 ``access_bp``、``aggrs``、``min_sample_us``
和 ``max_sample_us``。关于调优机制的内部实现，请参考该功能的
:ref:`设计文档 <damon_design_monitoring_intervals_autotuning_zh_CN>`。读取和写入
``intervals_goal`` 目录下的四个文件，会显示并更新 :ref:`设计文档 <damon_design_monitoring_intervals_autotuning_zh_CN>` 中描述的同名调优参数。调优
从用户设置的 ``sample_us`` 和 ``aggr_us`` 开始。向 ``state`` 文件写入
``update_tuned_intervals`` 后，可以从 ``sample_us`` 和 ``aggr_us`` 文件
读取应用调优后的两个间隔的当前值。

.. _damon_usage_sysfs_probes_zh_CN:

contexts/<N>/monitoring_attrs/probes/
-------------------------------------

用于注册 :ref:`数据属性监测 <damon_design_data_attrs_monitoring_zh_CN>` 探针的目录。

开始时，该目录只有一个文件 ``nr_probes``。向该文件写入一个数字
（``N``）会创建数量为该数字的子目录，命名为 ``0`` 到 ``N-1``。每个
目录表示一个监测探针。

在每个探针目录中，存在一个目录 ``filters``。该目录包含为探针安装过滤器
的文件，该过滤器用于确定探针的数据属性。

开始时，``filters`` 目录只有一个文件 ``nr_filters``。向该文件写入一个
数字（``N``）会创建数量为该数字的子目录，命名为 ``0`` 到 ``N-1``。每个
目录表示一个过滤器，其工作方式类似于 :ref:`DAMOS 过滤器
<sysfs_filters_zh_CN>`。当过滤器 ``type`` 为 ``memcg`` 时，``path`` 文件
作为 :ref:`DAMOS 过滤器 <sysfs_filters_zh_CN>` 的 ``memcg_path`` 使用。

.. _sysfs_targets_zh_CN:

contexts/<N>/targets/
---------------------

在开始时，这个目录只有一个文件 ``nr_targets`` 。向该文件写入一个数字(``N``)，就可以创建
名为 ``0`` 到 ``N-1`` 的子目录的数量。每个目录代表每个监测目标。

.. _sysfs_target_zh_CN:

targets/<N>/
------------

在每个目标目录中，存在两个文件（``pid_target`` 和 ``obsolete_target``）和一个目录(``regions``)。

如果你把 ``vaddr`` 写到 ``contexts/<N>/operations`` 中，每个目标应该是一个进程。你
可以通过将进程的pid写到 ``pid_target`` 文件中来指定DAMON的进程。

用户可以通过向 ``obsolete_target`` 文件写入非零值并提交它（向 ``state``
文件写入 ``commit``），有选择地移除目标数组中间的目标。DAMON 会从内部
目标数组中移除匹配的目标。用户负责重新构建目标目录，使其正确表示变更后的
内部目标数组。


.. _sysfs_regions_zh_CN:

targets/<N>/regions
-------------------

在使用 ``fvaddr`` 或 ``paddr`` 监测操作集时，用户需要设置监测目标地址
范围。在使用 ``vaddr`` 操作集时，这不是必须的，但用户可以选择将初始
监测区域设置为特定地址范围。更多细节请参考 :ref:`设计 <damon_design_vaddr_target_regions_construction_zh_CN>`。

对于这类情况，用户可以按照自己的需要，向该目录下的文件写入适当的值，
显式设置初始监测目标区域。

开始时，这个目录只有一个文件， ``nr_regions`` 。向该文件写入一个数字(``N``)，就可以创
建名为 ``0`` 到  ``N-1`` 的子目录。每个目录代表每个初始监测目标区域。

如果在线提交新的 DAMON 参数时（向 :ref:`kdamond <sysfs_kdamond_zh_CN>` 的
``state`` 文件写入 ``commit``）``nr_regions`` 为零，则提交逻辑会忽略
目标区域。换言之，该目标当前的监测结果会被保留。

.. _sysfs_region_zh_CN:

regions/<N>/
------------

在每个区域目录中，你会发现两个文件（ ``start``  和  ``end`` ）。你可以通过向文件写入
和从文件中读出，分别设置和获得初始监测目标区域的起始和结束地址。

每个区域不应该与其他区域重叠。 目录“N”的“结束”应等于或小于目录“N+1”的“开始”。

.. _sysfs_schemes_zh_CN:

contexts/<N>/schemes/
---------------------

对于一版的基于DAMON的数据访问感知的内存管理优化，用户通常希望系统对特定访问模式的内存区
域应用内存管理操作。DAMON从用户那里接收这种形式化的操作方案，并将这些方案应用于目标内存
区域。用户可以通过读取和写入这个目录下的文件来获得和设置这些方案。

在开始时，这个目录只有一个文件，``nr_schemes``。向该文件写入一个数字(``N``)，就可以
创建名为``0``到``N-1``的子目录的数量。每个目录代表每个基于DAMON的操作方案。

.. _sysfs_scheme_zh_CN:

schemes/<N>/
------------

在每个方案目录中，存在九个目录（``access_pattern``、``quotas``、
``watermarks``、``core_filters``、``ops_filters``、``filters``、
``dests``、``stats`` 和 ``tried_regions``）以及三个文件（``action``、
``target_nid`` 和 ``apply_interval_us``）。

``action`` 文件用于设置和获取方案的 :ref:`动作 <damon_design_damos_action_zh_CN>`。可写入和读取该文件的关键字及其含义，与
:ref:`设计文档 <damon_design_damos_action_zh_CN>` 中列表的内容相同。

``target_nid`` 文件用于设置迁移目标节点，只有当 ``action`` 为
``migrate_hot`` 或 ``migrate_cold`` 时才有意义。

``apply_interval_us`` 文件用于以微秒为单位设置和获取方案的
:ref:`apply_interval <damon_design_damos_zh_CN>`。

.. _sysfs_access_pattern_zh_CN:

schemes/<N>/access_pattern/
---------------------------

用于给定基于 DAMON 的操作方案的目标访问 :ref:`模式
<damon_design_damos_access_pattern_zh_CN>` 的目录。

在 ``access_pattern`` 目录下，存在三个目录（ ``sz``, ``nr_accesses``, 和 ``age`` ），
每个目录有两个文件（``min`` 和 ``max`` ）。你可以通过向  ``sz``, ``nr_accesses``, 和
``age``  目录下的 ``min`` 和 ``max`` 文件分别写入和读取来设置和获取给定方案的访问模式。
请注意，``min`` 和 ``max`` 形成闭区间。

.. _sysfs_quotas_zh_CN:

schemes/<N>/quotas/
-------------------

用于给定基于 DAMON 的操作方案的 :ref:`配额 <damon_design_damos_quotas_zh_CN>` 的目录。

在 ``quotas`` 目录下，存在七个文件（``ms``、``bytes``、
``reset_interval_ms``、``effective_bytes``、``goal_tuner``、
``fail_charge_num`` 和 ``fail_charge_denom``）以及两个目录（``weights``
和 ``goals``）。

你可以分别向这三个文件写入值，设置以毫秒为单位的 ``time quota``、以字节
为单位的 ``size quota`` 以及以毫秒为单位的 ``reset interval``。随后，
DAMON 会尝试在 ``reset_interval_ms`` 内，只使用最多 ``time quota``
毫秒把 ``action`` 应用于符合 ``access_pattern`` 的内存区域，并且只将该
动作应用于最多 ``bytes`` 字节的内存区域。将 ``ms`` 和 ``bytes`` 都设置
为零会禁用配额限制，除非至少设置了一个 :ref:`目标
<sysfs_schemes_quota_goals_zh_CN>`。

你可以向 ``goal_tuner`` 文件写入算法名称，设置要使用的基于目标的有效
配额自动调优算法。读取该文件会返回当前选择的调优算法。关于该功能的背景
设计以及可选择算法的名称，请参考 :ref:`自动配额调优目标 <damon_design_damos_quotas_auto_tuning_zh_CN>` 的设计文档。关于目标设置，请参考
:ref:`goals 目录 <sysfs_schemes_quota_goals_zh_CN>`。

你可以分别向 ``fail_charge_num`` 和 ``fail_charge_denom`` 文件写入该比率的
分子和分母，设置动作失败内存配额计费比率。读取这些文件会返回当前设置的值。
关于该比率功能的更多细节，请参考 :ref:`设计 <damon_design_damos_quotas_failed_memory_charging_ratio_zh_CN>`。

时间配额会在内部转换为大小配额。在转换后的大小配额和用户指定的大小配额之间，
会应用较小者。根据用户指定的 :ref:`目标
<sysfs_schemes_quota_goals_zh_CN>`，有效大小配额会被进一步调整。读取
``effective_bytes`` 会返回当前有效大小配额。该文件不会实时更新，所以用户
应通过向相关 ``kdamonds/<N>/state`` 文件写入特殊关键字
``update_schemes_effective_quotas``，要求 DAMON sysfs 接口为统计信息更新该
文件内容。

在 ``weights`` 目录下，存在三个文件（``sz_permil``、
``nr_accesses_permil`` 和 ``age_permil``）。你可以向 ``weights`` 目录下的
三个文件写入值，设置大小、访问频率和年龄的 :ref:`优先级权重 <damon_design_damos_quotas_prioritization_zh_CN>`，单位为千分之一。

.. _sysfs_schemes_quota_goals_zh_CN:

schemes/<N>/quotas/goals/
-------------------------

用于给定基于 DAMON 的操作方案的 :ref:`自动配额调优目标 <damon_design_damos_quotas_auto_tuning_zh_CN>` 的目录。

开始时，该目录只有一个文件 ``nr_goals``。向该文件写入一个数字（``N``）
会创建数量为该数字的子目录，命名为 ``0`` 到 ``N-1``。每个目录表示一个
目标和当前达成情况。在多个反馈中，会使用最好的一个。

每个目标目录包含五个文件，即 ``target_metric``、``target_value``、
``current_value``、``nid`` 和 ``path``。用户可以通过写入和读取每个文件，
设置和获取 :ref:`设计文档 <damon_design_damos_quotas_auto_tuning_zh_CN>` 中指定的
配额自动调优目标的五个参数。因为内核不会更新 ``current_value``，所以只有
当 ``target_metric`` 为 ``user_input`` 时，读取它才有意义。请注意，用户还应
向 :ref:`kdamond 目录 <sysfs_kdamond_zh_CN>` 的 ``state`` 文件写入
``commit_schemes_quota_goals``，以便将反馈传递给 DAMON。

.. _sysfs_watermarks_zh_CN:

schemes/<N>/watermarks/
-----------------------

用于给定基于 DAMON 的操作方案的 :ref:`水位 <damon_design_damos_watermarks_zh_CN>` 的目录。

在 watermarks 目录下，存在五个文件（``metric``、``interval_us``、``high``、
``mid`` 和 ``low``），用于设置度量指标、度量指标检查之间的时间间隔以及三个
水位。你可以分别通过写入和读取这些文件来设置和获取这五个值。

可写入 ``metric`` 文件的关键字及其含义如下。

 - none：忽略水位
 - free_mem_rate：系统空闲内存率（千分之一）

``interval_us`` 应以微秒为单位写入。

.. _sysfs_filters_zh_CN:

schemes/<N>/{core\_,ops\_,}filters/
-----------------------------------

用于给定基于 DAMON 的操作方案的 :ref:`过滤器 <damon_design_damos_filters_zh_CN>` 的目录。

``core_filters`` 和 ``ops_filters`` 目录分别用于由 DAMON 核心层和操作集层
处理的过滤器。``filters`` 目录可用于安装过滤器，而不管它们由哪一层处理。
``core_filters`` 和 ``ops_filters`` 请求的过滤器会先于 ``filters`` 中的
过滤器安装。这三个目录具有相同的文件。

使用 ``filters`` 目录会使过滤器求值顺序难以预期。因此，``filters`` 目录
已被弃用。它仍然可以工作，但计划在不久的将来移除。用户应改用
``core_filters`` 和 ``ops_filters`` 目录。

开始时，该目录只有一个文件 ``nr_filters``。向该文件写入一个数字（``N``）
会创建数量为该数字的子目录，命名为 ``0`` 到 ``N-1``。每个目录表示一个
过滤器。过滤器按数字顺序求值。

每个过滤器目录包含九个文件，即 ``type``、``matching``、``allow``、
``memcg_path``、``addr_start``、``addr_end``、``min``、``max`` 和
``damon_target_idx``。你可以向 ``type`` 文件写入过滤器类型。关于可用
类型名称、它们的含义以及它们由哪一层处理，请参考 :ref:`设计文档 <damon_design_damos_filters_zh_CN>`。

对于 ``memcg`` 类型，你可以向 ``memcg_path`` 文件写入从 cgroups 挂载点
开始的内存 cgroup 路径，指定感兴趣的内存 cgroup。对于 ``addr`` 类型，
你可以分别向 ``addr_start`` 和 ``addr_end`` 文件写入范围（左闭右开区间）
的起始和结束地址。对于 ``hugepage_size`` 类型，你可以分别向 ``min`` 和
``max`` 文件写入范围（闭区间）的最小和最大大小。对于 ``target`` 类型，
你可以向 ``damon_target_idx`` 文件写入 DAMON 上下文的监测目标列表中目标的
索引。

你可以向 ``matching`` 文件写入 ``Y`` 或 ``N``，指定过滤器是否用于匹配
``type`` 的内存。你可以向 ``allow`` 文件写入 ``Y`` 或 ``N``，指定是否允许
将动作应用于满足 ``type`` 和 ``matching`` 的内存。

例如，下面的操作将 DAMOS 动作限制为只应用于除 ``/having_care_already`` 之外
所有内存 cgroup 的非匿名页。::

    # cd ops_filters/0/
    # echo 2 > nr_filters
    # # disallow anonymous pages
    echo anon > 0/type
    echo Y > 0/matching
    echo N > 0/allow
    # # further filter out all cgroups except one at '/having_care_already'
    echo memcg > 1/type
    echo /having_care_already > 1/memcg_path
    echo Y > 1/matching
    echo N > 1/allow

更多细节请参考 :ref:`DAMOS 过滤器设计文档 <damon_design_damos_filters_zh_CN>`，
其中包括不同 ``allow`` 的多个过滤器如何工作、每个过滤器何时受支持，以及
统计信息上的差异。

.. _damon_sysfs_dests_zh_CN:

schemes/<N>/dests/
------------------

用于指定给定基于 DAMON 的操作方案动作的目的地的目录。如果给定方案的动作
不支持多个目的地，则该目录会被忽略。只有 ``DAMOS_MIGRATE_{HOT,COLD}`` 动作
支持多个目的地。

开始时，该目录只有一个文件 ``nr_dests``。向该文件写入一个数字（``N``）
会创建数量为该数字的子目录，命名为 ``0`` 到 ``N-1``。每个目录表示一个
动作目的地。

每个目的地目录包含两个文件，即 ``id`` 和 ``weight``。用户可以向 ``id``
文件写入目的地标识符，并从中读取它。对于 ``DAMOS_MIGRATE_{HOT,COLD}`` 动作，
应将迁移目的节点的节点 id 写入 ``id`` 文件。用户可以向 ``weight`` 文件写入
该目的地在给定目的地中的权重，并从中读取它。权重可以是任意整数。当 DAMOS
将动作应用于内存区域的每个实体时，它会根据目的地的相对权重选择该动作的
目的地。

.. _sysfs_schemes_stats_zh_CN:

schemes/<N>/stats/
------------------

DAMON 为每个方案统计信息。这些统计信息可用于方案的在线分析或调优。关于
这些统计信息的更多细节，请参考 :ref:`设计文档 <damon_design_damos_stat_zh_CN>`。

可以分别通过读取 ``stats`` 目录下的文件（``nr_tried``、``sz_tried``、
``nr_applied``、``sz_applied``、``sz_ops_filter_passed``、``qt_exceeds``、
``nr_snapshots`` 和 ``max_nr_snapshots``）来获取这些统计信息。

默认情况下，这些文件不会实时更新。用户应要求 DAMON sysfs 接口使用
``refresh_ms`` 周期性地更新这些文件，或者向相关 ``kdamonds/<N>/state`` 文件
写入特殊关键字 ``update_schemes_stats`` 来进行一次性更新。更多细节请参考
:ref:`kdamond 目录 <sysfs_kdamond_zh_CN>`。

.. _sysfs_schemes_tried_regions_zh_CN:

schemes/<N>/tried_regions/
--------------------------

该目录初始时有一个文件 ``total_bytes``。

当特殊关键字 ``update_schemes_tried_regions`` 被写入相关
``kdamonds/<N>/state`` 文件时，DAMON 会更新 ``total_bytes`` 文件，使读取
它时返回该方案尝试区域的总大小，并在该目录下创建从 ``0`` 开始以整数命名的
目录。每个目录包含一些文件，暴露该目录下对应方案的 ``action`` 在对应方案的
下一个 :ref:`应用间隔 <damon_design_damos_zh_CN>` 内尝试应用的每个内存区域的详细
信息。这些信息包括该区域的地址范围、``nr_accesses`` 和 ``age``。

向相关 ``kdamonds/<N>/state`` 文件写入 ``update_schemes_tried_bytes`` 只会
更新 ``total_bytes`` 文件，而不会创建子目录。

当另一个特殊关键字 ``clear_schemes_tried_regions`` 被写入相关
``kdamonds/<N>/state`` 文件时，这些目录会被删除。

该目录的预期用途是调查方案行为，以及类似查询的高效数据访问监测结果获取。
特别是对于后一种用例，用户可以将 ``action`` 设置为 ``stat``，并将
``access pattern`` 设置为他们感兴趣、想要查询的模式。

.. _sysfs_schemes_tried_region_zh_CN:

tried_regions/<N>/
------------------

在每个区域目录中，你会发现五个文件（``start``、``end``、``nr_accesses``、
``age`` 和 ``sz_filter_passed``）。读取这些文件将显示相应的基于DAMON的操作方案
``action`` 试图应用的区域属性。

tried_regions/<N>/probes/
-------------------------

在每个区域目录中，还存在一个目录（``probes``）。在该目录中，存在命名为
``0`` 到 ``N-1`` 的子目录。``N`` 是已安装探针的数量。在每个数字命名的
目录中，存在一个文件（``hits``）。读取该文件会显示该区域的数据属性监测
探针命中正样本的数量。

用例
~~~~

下面的命令应用了一个方案：”如果一个大小为[4KiB, 8KiB]的内存区域在[10, 20]的聚合时间间隔内
显示出每一个聚合时间间隔[0, 5]的访问量，请分页该区域。对于分页，每秒最多只能使用10ms，而且每
秒分页不能超过1GiB。在这一限制下，首先分页出具有较长年龄的内存区域。另外，每5秒钟检查一次系统
的可用内存率，当可用内存率低于50%时开始监测和分页，但如果可用内存率大于60%，或低于30%，则停
止监测。“ ::

    # cd <sysfs>/kernel/mm/damon/admin
    # # populate directories
    # echo 1 > kdamonds/nr_kdamonds; echo 1 > kdamonds/0/contexts/nr_contexts;
    # echo 1 > kdamonds/0/contexts/0/schemes/nr_schemes
    # cd kdamonds/0/contexts/0/schemes/0
    # # set the basic access pattern and the action
    # echo 4096 > access_pattern/sz/min
    # echo 8192 > access_pattern/sz/max
    # echo 0 > access_pattern/nr_accesses/min
    # echo 5 > access_pattern/nr_accesses/max
    # echo 10 > access_pattern/age/min
    # echo 20 > access_pattern/age/max
    # echo pageout > action
    # # set quotas
    # echo 10 > quotas/ms
    # echo $((1024*1024*1024)) > quotas/bytes
    # echo 1000 > quotas/reset_interval_ms
    # # set watermark
    # echo free_mem_rate > watermarks/metric
    # echo 5000000 > watermarks/interval_us
    # echo 600 > watermarks/high
    # echo 500 > watermarks/mid
    # echo 300 > watermarks/low

请注意，我们强烈建议使用用户空间的工具，如 `damo <https://github.com/damonitor/damo>`_ ，
而不是像上面那样手动读写文件。以上只是一个例子。

.. _tracepoint_zh_CN:

监测结果的跟踪点
================

用户可以通过 :ref:`tried_regions <sysfs_schemes_tried_regions_zh_CN>` 获取
监测结果。该接口适合获取快照，但用于完整记录所有监测结果时可能效率不高。
为此，提供了两个跟踪点，即 ``damon:damon_aggregated`` 和
``damon:damos_before_apply``。``damon:damon_aggregated`` 提供完整的监测
结果，而 ``damon:damos_before_apply`` 提供每个基于 DAMON 的操作方案
（:ref:`DAMOS <damon_design_damos_zh_CN>`）将要应用到的区域的监测结果。因此，
``damon:damos_before_apply`` 更适合记录 DAMOS 的内部行为，或者基于 DAMOS
目标访问 :ref:`模式 <damon_design_damos_access_pattern_zh_CN>` 的类似查询的高效
监测结果记录。

监测开启时，你可以记录跟踪点事件，并使用跟踪点支持工具如 ``perf`` 显示结果。比如说::

    # echo on > kdamonds/0/state
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > kdamonds/0/state
    # perf script
    kdamond.0 46568 [027] 79357.842179: damon:damon_aggregated: target_id=0 nr_regions=11 122509119488-135708762112: 0 864
    [...]

``perf script`` 输出的每一行表示一个监测区域。前五个字段与其他跟踪点输出
一样。第六个字段 ``target_id=X`` 显示该区域的监测目标 id。第七个字段
``nr_regions=X`` 显示该目标的监测区域总数。第八个字段 ``X-Y:`` 显示
该区域的起始地址 ``X`` 和结束地址 ``Y``，单位为字节。第九个字段 ``X``
显示该区域的 ``nr_accesses`` （关于该计数器的更多细节请参考 :ref:`设计 <damon_design_region_based_sampling_zh_CN>`）。最后，第十个字段 ``X`` 显示该区域
的 ``age`` （关于该计数器的更多细节请参考 :ref:`设计 <damon_design_age_tracking_zh_CN>`）。

如果事件是 ``damon:damos_before_apply``，``perf script`` 输出大致如下::

    kdamond.0 47293 [000] 80801.060214: damon:damos_before_apply: ctx_idx=0 scheme_idx=0 target_idx=0 nr_regions=11 121932607488-135128711168: 0 136
    [...]

输出的每一行表示在被跟踪的时间点，每个基于 DAMON 的操作方案即将应用到的
一个监测区域。前五个字段与通常一样。除了 ``damon_aggregated`` 跟踪点的
输出之外，它还显示方案所属 DAMON 上下文在该上下文的 kdamond 的上下文列表
中的索引（``ctx_idx=X``），以及该方案在该上下文的方案列表中的索引
（``scheme_idx=X``）。

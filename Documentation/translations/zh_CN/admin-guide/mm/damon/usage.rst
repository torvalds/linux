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

- *DAMON用户空间工具。*
  `这 <https://github.com/damonitor/damo>`_ 为有这特权的人， 如系统管理员，希望有一个刚好
  可以工作的人性化界面。
  使用它，用户可以以人性化的方式使用DAMON的主要功能。不过，它可能不会为特殊情况进行高度调整。
  它同时支持虚拟和物理地址空间的监测。更多细节，请参考它的 `使用文档
  <https://github.com/damonitor/damo/blob/next/USAGE.md>`_。
- *sysfs接口。*
  :ref:`这 <sysfs_interface>` 是为那些希望更高级的使用DAMON的特权用户空间程序员准备的。
  使用它，用户可以通过读取和写入特殊的sysfs文件来使用DAMON的主要功能。因此，你可以编写和使
  用你个性化的DAMON sysfs包装程序，代替你读/写sysfs文件。  `DAMON用户空间工具
  <https://github.com/damonitor/damo>`_ 就是这种程序的一个例子  它同时支持虚拟和物理地址
  空间的监测。注意，这个界面只提供简单的监测结果 :ref:`统计 <damos_stats>`。对于详细的监测
  结果，DAMON提供了一个:ref:`跟踪点 <tracepoint>`。
- *debugfs interface.*
  :ref:`这 <debugfs_interface>` 几乎与:ref:`sysfs interface <sysfs_interface>` 接
  口相同。这将在下一个LTS内核发布后被移除，所以用户应该转移到
  :ref:`sysfs interface <sysfs_interface>`。
- *内核空间编程接口。*
  :doc:`这 </mm/damon/api>` 这是为内核空间程序员准备的。使用它，用户可以通过为你编写内
  核空间的DAMON应用程序，最灵活有效地利用DAMON的每一个功能。你甚至可以为各种地址空间扩展DAMON。
  详细情况请参考接口 :doc:`文件 </mm/damon/api>`。

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
``/`` 后缀，每个目录中的文件用逗号（","）分开。 ::

    /sys/kernel/mm/damon/admin
    │ kdamonds/nr_kdamonds
    │ │ 0/state,pid
    │ │ │ contexts/nr_contexts
    │ │ │ │ 0/operations
    │ │ │ │ │ monitoring_attrs/
    │ │ │ │ │ │ intervals/sample_us,aggr_us,update_us
    │ │ │ │ │ │ nr_regions/min,max
    │ │ │ │ │ targets/nr_targets
    │ │ │ │ │ │ 0/pid_target
    │ │ │ │ │ │ │ regions/nr_regions
    │ │ │ │ │ │ │ │ 0/start,end
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ │ schemes/nr_schemes
    │ │ │ │ │ │ 0/action
    │ │ │ │ │ │ │ access_pattern/
    │ │ │ │ │ │ │ │ sz/min,max
    │ │ │ │ │ │ │ │ nr_accesses/min,max
    │ │ │ │ │ │ │ │ age/min,max
    │ │ │ │ │ │ │ quotas/ms,bytes,reset_interval_ms
    │ │ │ │ │ │ │ │ weights/sz_permil,nr_accesses_permil,age_permil
    │ │ │ │ │ │ │ watermarks/metric,interval_us,high,mid,low
    │ │ │ │ │ │ │ stats/nr_tried,sz_tried,nr_applied,sz_applied,qt_exceeds
    │ │ │ │ │ │ │ tried_regions/
    │ │ │ │ │ │ │ │ 0/start,end,nr_accesses,age
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ ...
    │ │ ...

根
--

DAMON sysfs接口的根是 ``<sysfs>/kernel/mm/damon/`` ，它有一个名为 ``admin`` 的
目录。该目录包含特权用户空间程序控制DAMON的文件。拥有根权限的用户空间工具或deamons可以
使用这个目录。

kdamonds/
---------

与监测相关的信息包括请求规格和结果被称为DAMON上下文。DAMON用一个叫做kdamond的内核线程
执行每个上下文，多个kdamonds可以并行运行。

在 ``admin`` 目录下，有一个目录，即``kdamonds``，它有控制kdamonds的文件存在。在开始
时，这个目录只有一个文件，``nr_kdamonds``。向该文件写入一个数字（``N``），就会创建名为
``0`` 到 ``N-1`` 的子目录数量。每个目录代表每个kdamond。

kdamonds/<N>/
-------------

在每个kdamond目录中，存在两个文件（``state`` 和 ``pid`` ）和一个目录( ``contexts`` )。

读取 ``state`` 时，如果kdamond当前正在运行，则返回 ``on`` ，如果没有运行则返回 ``off`` 。
写入 ``on`` 或 ``off`` 使kdamond处于状态。向 ``state`` 文件写 ``update_schemes_stats`` ，
更新kdamond的每个基于DAMON的操作方案的统计文件的内容。关于统计信息的细节，请参考
:ref:`stats section <sysfs_schemes_stats>`. 将 ``update_schemes_tried_regions`` 写到
``state`` 文件，为kdamond的每个基于DAMON的操作方案，更新基于DAMON的操作方案动作的尝试区域目录。
将`clear_schemes_tried_regions`写入`state`文件，清除kdamond的每个基于DAMON的操作方案的动作
尝试区域目录。 关于基于DAMON的操作方案动作尝试区域目录的细节，请参考:ref:tried_regions 部分
<sysfs_schemes_tried_regions>`。

如果状态为 ``on``，读取 ``pid`` 显示kdamond线程的pid。

``contexts`` 目录包含控制这个kdamond要执行的监测上下文的文件。

kdamonds/<N>/contexts/
----------------------

在开始时，这个目录只有一个文件，即 ``nr_contexts`` 。向该文件写入一个数字( ``N`` )，就会创
建名为``0`` 到 ``N-1`` 的子目录数量。每个目录代表每个监测背景。目前，每个kdamond只支持
一个上下文，所以只有 ``0`` 或 ``1`` 可以被写入文件。

contexts/<N>/
-------------

在每个上下文目录中，存在一个文件(``operations``)和三个目录(``monitoring_attrs``,
``targets``, 和 ``schemes``)。

DAMON支持多种类型的监测操作，包括对虚拟地址空间和物理地址空间的监测。你可以通过向文件
中写入以下关键词之一，并从文件中读取，来设置和获取DAMON将为上下文使用何种类型的监测操作。

 - vaddr: 监测特定进程的虚拟地址空间
 - paddr: 监视系统的物理地址空间

contexts/<N>/monitoring_attrs/
------------------------------

用于指定监测属性的文件，包括所需的监测质量和效率，都在 ``monitoring_attrs`` 目录中。
具体来说，这个目录下有两个目录，即 ``intervals`` 和 ``nr_regions`` 。

在 ``intervals`` 目录下，存在DAMON的采样间隔(``sample_us``)、聚集间隔(``aggr_us``)
和更新间隔(``update_us``)三个文件。你可以通过写入和读出这些文件来设置和获取微秒级的值。

在 ``nr_regions`` 目录下，有两个文件分别用于DAMON监测区域的下限和上限（``min`` 和 ``max`` ），
这两个文件控制着监测的开销。你可以通过向这些文件的写入和读出来设置和获取这些值。

关于间隔和监测区域范围的更多细节，请参考设计文件 (:doc:`/mm/damon/design`)。

contexts/<N>/targets/
---------------------

在开始时，这个目录只有一个文件 ``nr_targets`` 。向该文件写入一个数字(``N``)，就可以创建
名为 ``0`` 到 ``N-1`` 的子目录的数量。每个目录代表每个监测目标。

targets/<N>/
------------

在每个目标目录中，存在一个文件(``pid_target``)和一个目录(``regions``)。

如果你把 ``vaddr`` 写到 ``contexts/<N>/operations`` 中，每个目标应该是一个进程。你
可以通过将进程的pid写到 ``pid_target`` 文件中来指定DAMON的进程。

targets/<N>/regions
-------------------

当使用 ``vaddr`` 监测操作集时（ ``vaddr`` 被写入 ``contexts/<N>/operations`` 文
件），DAMON自动设置和更新监测目标区域，这样就可以覆盖目标进程的整个内存映射。然而，用户可
能希望将初始监测区域设置为特定的地址范围。

相反，当使用 ``paddr`` 监测操作集时，DAMON不会自动设置和更新监测目标区域（ ``paddr``
被写入 ``contexts/<N>/operations`` 中）。因此，在这种情况下，用户应该自己设置监测目标
区域。

在这种情况下，用户可以按照自己的意愿明确设置初始监测目标区域，将适当的值写入该目录下的文件。

开始时，这个目录只有一个文件， ``nr_regions`` 。向该文件写入一个数字(``N``)，就可以创
建名为 ``0`` 到  ``N-1`` 的子目录。每个目录代表每个初始监测目标区域。

regions/<N>/
------------

在每个区域目录中，你会发现两个文件（ ``start``  和  ``end`` ）。你可以通过向文件写入
和从文件中读出，分别设置和获得初始监测目标区域的起始和结束地址。

每个区域不应该与其他区域重叠。 目录“N”的“结束”应等于或小于目录“N+1”的“开始”。

contexts/<N>/schemes/
---------------------

对于一版的基于DAMON的数据访问感知的内存管理优化，用户通常希望系统对特定访问模式的内存区
域应用内存管理操作。DAMON从用户那里接收这种形式化的操作方案，并将这些方案应用于目标内存
区域。用户可以通过读取和写入这个目录下的文件来获得和设置这些方案。

在开始时，这个目录只有一个文件，``nr_schemes``。向该文件写入一个数字(``N``)，就可以
创建名为``0``到``N-1``的子目录的数量。每个目录代表每个基于DAMON的操作方案。

schemes/<N>/
------------

在每个方案目录中，存在五个目录(``access_pattern``、``quotas``、``watermarks``、
``stats`` 和 ``tried_regions``)和一个文件(``action``)。

``action`` 文件用于设置和获取你想应用于具有特定访问模式的内存区域的动作。可以写入文件
和从文件中读取的关键词及其含义如下。

 - ``willneed``: 对有 ``MADV_WILLNEED`` 的区域调用 ``madvise()`` 。
 - ``cold``: 对具有 ``MADV_COLD`` 的区域调用 ``madvise()`` 。
 - ``pageout``: 为具有 ``MADV_PAGEOUT`` 的区域调用 ``madvise()`` 。
 - ``hugepage``: 为带有 ``MADV_HUGEPAGE`` 的区域调用 ``madvise()`` 。
 - ``nohugepage``: 为带有 ``MADV_NOHUGEPAGE`` 的区域调用 ``madvise()``。
 - ``lru_prio``: 在其LRU列表上对区域进行优先排序。
 - ``lru_deprio``: 对区域的LRU列表进行降低优先处理。
 - ``stat``: 什么都不做，只计算统计数据

schemes/<N>/access_pattern/
---------------------------

每个基于DAMON的操作方案的目标访问模式由三个范围构成，包括以字节为单位的区域大小、每个
聚合区间的监测访问次数和区域年龄的聚合区间数。

在 ``access_pattern`` 目录下，存在三个目录（ ``sz``, ``nr_accesses``, 和 ``age`` ），
每个目录有两个文件（``min`` 和 ``max`` ）。你可以通过向  ``sz``, ``nr_accesses``, 和
``age``  目录下的 ``min`` 和 ``max`` 文件分别写入和读取来设置和获取给定方案的访问模式。

schemes/<N>/quotas/
-------------------

每个 ``动作`` 的最佳 ``目标访问模式`` 取决于工作负载，所以不容易找到。更糟糕的是，将某些动作
的方案设置得过于激进会造成严重的开销。为了避免这种开销，用户可以为每个方案限制时间和大小配额。
具体来说，用户可以要求DAMON尽量只使用特定的时间（``时间配额``）来应用动作，并且在给定的时间间
隔（``重置间隔``）内，只对具有目标访问模式的内存区域应用动作，而不使用特定数量（``大小配额``）。

当预计超过配额限制时，DAMON会根据 ``目标访问模式`` 的大小、访问频率和年龄，对找到的内存区域
进行优先排序。为了进行个性化的优先排序，用户可以为这三个属性设置权重。

在 ``quotas`` 目录下，存在三个文件（``ms``, ``bytes``, ``reset_interval_ms``）和一个
目录(``weights``)，其中有三个文件(``sz_permil``, ``nr_accesses_permil``, 和
``age_permil``)。

你可以设置以毫秒为单位的 ``时间配额`` ，以字节为单位的 ``大小配额`` ，以及以毫秒为单位的 ``重
置间隔`` ，分别向这三个文件写入数值。你还可以通过向 ``weights`` 目录下的三个文件写入数值来设
置大小、访问频率和年龄的优先权，单位为千分之一。

schemes/<N>/watermarks/
-----------------------

为了便于根据系统状态激活和停用每个方案，DAMON提供了一个称为水位的功能。该功能接收五个值，称为
``度量`` 、``间隔`` 、``高`` 、``中`` 、``低`` 。``度量值`` 是指可以测量的系统度量值，如
自由内存比率。如果系统的度量值 ``高`` 于memoent的高值或 ``低`` 于低值，则该方案被停用。如果
该值低于 ``中`` ，则该方案被激活。

在水位目录下，存在五个文件(``metric``, ``interval_us``,``high``, ``mid``, and ``low``)
用于设置每个值。你可以通过向这些文件的写入来分别设置和获取这五个值。

可以写入 ``metric`` 文件的关键词和含义如下。

 - none: 忽略水位
 - free_mem_rate: 系统的自由内存率（千分比）。

``interval`` 应以微秒为单位写入。

schemes/<N>/stats/
------------------

DAMON统计每个方案被尝试应用的区域的总数量和字节数，每个方案被成功应用的区域的两个数字，以及
超过配额限制的总数量。这些统计数据可用于在线分析或调整方案。

可以通过读取 ``stats`` 目录下的文件(``nr_tried``, ``sz_tried``, ``nr_applied``,
``sz_applied``, 和 ``qt_exceeds``)）分别检索这些统计数据。这些文件不是实时更新的，所以
你应该要求DAMON sysfs接口通过在相关的 ``kdamonds/<N>/state`` 文件中写入一个特殊的关键字
``update_schemes_stats`` 来更新统计信息的文件内容。

schemes/<N>/tried_regions/
--------------------------

当一个特殊的关键字 ``update_schemes_tried_regions`` 被写入相关的 ``kdamonds/<N>/state``
文件时，DAMON会在这个目录下创建从 ``0`` 开始命名的整数目录。每个目录包含的文件暴露了关于每个
内存区域的详细信息，在下一个 :ref:`聚集区间 <sysfs_monitoring_attrs>`，相应的方案的 ``动作``
已经尝试在这个目录下应用。这些信息包括地址范围、``nr_accesses`` 以及区域的 ``年龄`` 。

当另一个特殊的关键字 ``clear_schemes_tried_regions`` 被写入相关的 ``kdamonds/<N>/state``
文件时，这些目录将被删除。

tried_regions/<N>/
------------------

在每个区域目录中，你会发现四个文件(``start``, ``end``, ``nr_accesses``, and ``age``)。
读取这些文件将显示相应的基于DAMON的操作方案 ``动作`` 试图应用的区域的开始和结束地址、``nr_accesses``
和 ``年龄`` 。

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

debugfs接口
===========

.. note::

  DAMON debugfs接口将在下一个LTS内核发布后被移除，所以用户应该转移到
  :ref:`sysfs接口<sysfs_interface>`。

DAMON导出了八个文件, ``attrs``, ``target_ids``, ``init_regions``,
``schemes``, ``monitor_on_DEPRECATED``, ``kdamond_pid``, ``mk_contexts`` 和
``rm_contexts`` under its debugfs directory, ``<debugfs>/damon/``.


属性
----

用户可以通过读取和写入 ``attrs`` 文件获得和设置 ``采样间隔`` 、 ``聚集间隔`` 、 ``更新间隔``
以及监测目标区域的最小/最大数量。要详细了解监测属性，请参考 `:doc:/mm/damon/design` 。例如，
下面的命令将这些值设置为5ms、100ms、1000ms、10和1000，然后再次检查::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 > attrs
    # cat attrs
    5000 100000 1000000 10 1000


目标ID
------

一些类型的地址空间支持多个监测目标。例如，虚拟内存地址空间的监测可以有多个进程作为监测目标。用户
可以通过写入目标的相关id值来设置目标，并通过读取 ``target_ids`` 文件来获得当前目标的id。在监
测虚拟地址空间的情况下，这些值应该是监测目标进程的pid。例如，下面的命令将pid为42和4242的进程设
为监测目标，并再次检查::

    # cd <debugfs>/damon
    # echo 42 4242 > target_ids
    # cat target_ids
    42 4242

用户还可以通过在文件中写入一个特殊的关键字 "paddr\n" 来监测系统的物理内存地址空间。因为物理地
址空间监测不支持多个目标，读取文件会显示一个假值，即 ``42`` ，如下图所示::

    # cd <debugfs>/damon
    # echo paddr > target_ids
    # cat target_ids
    42

请注意，设置目标ID并不启动监测。


初始监测目标区域
----------------

在虚拟地址空间监测的情况下，DAMON自动设置和更新监测的目标区域，这样就可以覆盖目标进程的整个
内存映射。然而，用户可能希望将监测区域限制在特定的地址范围内，如堆、栈或特定的文件映射区域。
或者，一些用户可以知道他们工作负载的初始访问模式，因此希望为“自适应区域调整”设置最佳初始区域。

相比之下，DAMON在物理内存监测的情况下不会自动设置和更新监测目标区域。因此，用户应该自己设置
监测目标区域。

在这种情况下，用户可以通过在 ``init_regions`` 文件中写入适当的值，明确地设置他们想要的初
始监测目标区域。输入应该是一个由三个整数组成的队列，用空格隔开，代表一个区域的形式如下::

    <target idx> <start address> <end address>

目标idx应该是 ``target_ids`` 文件中目标的索引，从 ``0`` 开始，区域应该按照地址顺序传递。
例如，下面的命令将设置几个地址范围， ``1-100`` 和 ``100-200`` 作为pid 42的初始监测目标
区域，这是 ``target_ids`` 中的第一个（索引 ``0`` ），另外几个地址范围， ``20-40`` 和
``50-100`` 作为pid 4242的地址，这是 ``target_ids`` 中的第二个（索引 ``1`` ）::

    # cd <debugfs>/damon
    # cat target_ids
    42 4242
    # echo "0   1       100 \
            0   100     200 \
            1   20      40  \
            1   50      100" > init_regions

请注意，这只是设置了初始的监测目标区域。在虚拟内存监测的情况下，DAMON会在一个 ``更新间隔``
后自动更新区域的边界。因此，在这种情况下，如果用户不希望更新的话，应该把 ``更新间隔`` 设
置得足够大。


方案
----

对于通常的基于DAMON的数据访问感知的内存管理优化，用户只是希望系统对特定访问模式的内存区域应用内
存管理操作。DAMON从用户那里接收这种形式化的操作方案，并将这些方案应用到目标进程中。

用户可以通过读取和写入 ``scheme`` debugfs文件来获得和设置这些方案。读取该文件还可以显示每个
方案的统计数据。在文件中，每一个方案都应该在每一行中以下列形式表示出来::

    <target access pattern> <action> <quota> <watermarks>

你可以通过简单地在文件中写入一个空字符串来禁用方案。

目标访问模式
~~~~~~~~~~~~

``<目标访问模式>`` 是由三个范围构成的，形式如下::

    min-size max-size min-acc max-acc min-age max-age

具体来说，区域大小的字节数（ `min-size` 和 `max-size` ），访问频率的每聚合区间的监测访问次
数（ `min-acc` 和 `max-acc` ），区域年龄的聚合区间数（ `min-age` 和 `max-age` ）都被指定。
请注意，这些范围是封闭区间。

动作
~~~~

``<action>`` 是一个预定义的内存管理动作的整数，DAMON将应用于具有目标访问模式的区域。支持
的数字和它们的含义如下::

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``
 - 5: Do nothing but count the statistics

配额
~~~~

每个 ``动作`` 的最佳 ``目标访问模式`` 取决于工作负载，所以不容易找到。更糟糕的是，将某个
动作的方案设置得过于激进会导致严重的开销。为了避免这种开销，用户可以通过下面表格中的 ``<quota>``
来限制方案的时间和大小配额::

    <ms> <sz> <reset interval> <priority weights>

这使得DAMON在 ``<reset interval>`` 毫秒内，尽量只用 ``<ms>`` 毫秒的时间对 ``目标访
问模式`` 的内存区域应用动作，并在 ``<reset interval>`` 内只对最多<sz>字节的内存区域应
用动作。将 ``<ms>`` 和 ``<sz>`` 都设置为零，可以禁用配额限制。

当预计超过配额限制时，DAMON会根据 ``目标访问模式`` 的大小、访问频率和年龄，对发现的内存
区域进行优先排序。为了实现个性化的优先级，用户可以在 ``<优先级权重>`` 中设置这三个属性的
权重，具体形式如下::

    <size weight> <access frequency weight> <age weight>

水位
~~~~

有些方案需要根据系统特定指标的当前值来运行，如自由内存比率。对于这种情况，用户可以为该条
件指定水位。::

    <metric> <check interval> <high mark> <middle mark> <low mark>

``<metric>`` 是一个预定义的整数，用于要检查的度量。支持的数字和它们的含义如下。

 - 0: 忽视水位
 - 1: 系统空闲内存率 (千分比)

每隔 ``<检查间隔>`` 微秒检查一次公制的值。

如果该值高于 ``<高标>`` 或低于 ``<低标>`` ，该方案被停用。如果该值低于 ``<中标>`` ，
该方案将被激活。

统计数据
~~~~~~~~

它还统计每个方案被尝试应用的区域的总数量和字节数，每个方案被成功应用的区域的两个数量，以
及超过配额限制的总数量。这些统计数据可用于在线分析或调整方案。

统计数据可以通过读取方案文件来显示。读取该文件将显示你在每一行中输入的每个 ``方案`` ，
统计的五个数字将被加在每一行的末尾。

例子
~~~~

下面的命令应用了一个方案：”如果一个大小为[4KiB, 8KiB]的内存区域在[10, 20]的聚合时间
间隔内显示出每一个聚合时间间隔[0, 5]的访问量，请分页出该区域。对于分页，每秒最多只能使
用10ms，而且每秒分页不能超过1GiB。在这一限制下，首先分页出具有较长年龄的内存区域。另外，
每5秒钟检查一次系统的可用内存率，当可用内存率低于50%时开始监测和分页，但如果可用内存率
大于60%，或低于30%，则停止监测“::

    # cd <debugfs>/damon
    # scheme="4096 8192  0 5    10 20    2"  # target access pattern and action
    # scheme+=" 10 $((1024*1024*1024)) 1000" # quotas
    # scheme+=" 0 0 100"                     # prioritization weights
    # scheme+=" 1 5000000 600 500 300"       # watermarks
    # echo "$scheme" > schemes


开关
----

除非你明确地启动监测，否则如上所述的文件设置不会产生效果。你可以通过写入和读取 ``monitor_on_DEPRECATED``
文件来启动、停止和检查监测的当前状态。写入 ``on`` 该文件可以启动对有属性的目标的监测。写入
``off`` 该文件则停止这些目标。如果每个目标进程被终止，DAMON也会停止。下面的示例命令开启、关
闭和检查DAMON的状态::

    # cd <debugfs>/damon
    # echo on > monitor_on_DEPRECATED
    # echo off > monitor_on_DEPRECATED
    # cat monitor_on_DEPRECATED
    off

请注意，当监测开启时，你不能写到上述的debugfs文件。如果你在DAMON运行时写到这些文件，将会返
回一个错误代码，如 ``-EBUSY`` 。


监测线程PID
-----------

DAMON通过一个叫做kdamond的内核线程来进行请求监测。你可以通过读取 ``kdamond_pid`` 文件获
得该线程的 ``pid`` 。当监测被 ``关闭`` 时，读取该文件不会返回任何信息::

    # cd <debugfs>/damon
    # cat monitor_on_DEPRECATED
    off
    # cat kdamond_pid
    none
    # echo on > monitor_on_DEPRECATED
    # cat kdamond_pid
    18594


使用多个监测线程
----------------

每个监测上下文都会创建一个 ``kdamond`` 线程。你可以使用 ``mk_contexts`` 和 ``rm_contexts``
文件为多个 ``kdamond`` 需要的用例创建和删除监测上下文。

将新上下文的名称写入 ``mk_contexts`` 文件，在 ``DAMON debugfs`` 目录上创建一个该名称的目录。
该目录将有该上下文的 ``DAMON debugfs`` 文件::

    # cd <debugfs>/damon
    # ls foo
    # ls: cannot access 'foo': No such file or directory
    # echo foo > mk_contexts
    # ls foo
    # attrs  init_regions  kdamond_pid  schemes  target_ids

如果不再需要上下文，你可以通过把上下文的名字放到 ``rm_contexts`` 文件中来删除它和相应的目录::

    # echo foo > rm_contexts
    # ls foo
    # ls: cannot access 'foo': No such file or directory

注意， ``mk_contexts`` 、 ``rm_contexts`` 和 ``monitor_on_DEPRECATED`` 文件只在根目录下。


监测结果的监测点
================

DAMON通过一个tracepoint ``damon:damon_aggregated`` 提供监测结果.  当监测开启时，你可
以记录追踪点事件，并使用追踪点支持工具如perf显示结果。比如说::

    # echo on > monitor_on_DEPRECATED
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on_DEPRECATED
    # perf script

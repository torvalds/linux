.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/mm/damon/usage.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

========
详细用法
========

DAMON 为不同的用户提供了下面三种接口。

- *DAMON用户空间工具。*
  `这 <https://github.com/awslabs/damo>`_ 为有这特权的人， 如系统管理员，希望有一个刚好
  可以工作的人性化界面。
  使用它，用户可以以人性化的方式使用DAMON的主要功能。不过，它可能不会为特殊情况进行高度调整。
  它同时支持虚拟和物理地址空间的监测。更多细节，请参考它的 `使用文档
  <https://github.com/awslabs/damo/blob/next/USAGE.md>`_。
- *debugfs接口。*
  :ref:`这 <debugfs_interface>` 是为那些希望更高级的使用DAMON的特权用户空间程序员准备的。
  使用它，用户可以通过读取和写入特殊的debugfs文件来使用DAMON的主要功能。因此，你可以编写和使
  用你个性化的DAMON debugfs包装程序，代替你读/写debugfs文件。  `DAMON用户空间工具
  <https://github.com/awslabs/damo>`_ 就是这种程序的一个例子  它同时支持虚拟和物理地址
  空间的监测。注意，这个界面只提供简单的监测结果 :ref:`统计 <damos_stats>`。对于详细的监测
  结果，DAMON提供了一个:ref:`跟踪点 <tracepoint>`。

- *内核空间编程接口。*
  :doc:`This </vm/damon/api>` 这是为内核空间程序员准备的。使用它，用户可以通过为你编写内
  核空间的DAMON应用程序，最灵活有效地利用DAMON的每一个功能。你甚至可以为各种地址空间扩展DAMON。
  详细情况请参考接口 :doc:`文件 </vm/damon/api>`。


debugfs接口
===========

DAMON导出了八个文件, ``attrs``, ``target_ids``, ``init_regions``,
``schemes``, ``monitor_on``, ``kdamond_pid``, ``mk_contexts`` 和
``rm_contexts`` under its debugfs directory, ``<debugfs>/damon/``.


属性
----

用户可以通过读取和写入 ``attrs`` 文件获得和设置 ``采样间隔`` 、 ``聚集间隔`` 、 ``区域更新间隔``
以及监测目标区域的最小/最大数量。要详细了解监测属性，请参考 `:doc:/vm/damon/design` 。例如，
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
始监测目标区域。输入的每一行应代表一个区域，形式如下::

    <target idx> <start address> <end address>

目标idx应该是 ``target_ids`` 文件中目标的索引，从 ``0`` 开始，区域应该按照地址顺序传递。
例如，下面的命令将设置几个地址范围， ``1-100`` 和 ``100-200`` 作为pid 42的初始监测目标
区域，这是 ``target_ids`` 中的第一个（索引 ``0`` ），另外几个地址范围， ``20-40`` 和
``50-100`` 作为pid 4242的地址，这是 ``target_ids`` 中的第二个（索引 ``1`` ）::

    # cd <debugfs>/damon
    # cat target_ids
    42 4242
    # echo "0   1       100
            0   100     200
            1   20      40
            1   50      100" > init_regions

请注意，这只是设置了初始的监测目标区域。在虚拟内存监测的情况下，DAMON会在一个 ``区域更新间隔``
后自动更新区域的边界。因此，在这种情况下，如果用户不希望更新的话，应该把 ``区域的更新间隔`` 设
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

除非你明确地启动监测，否则如上所述的文件设置不会产生效果。你可以通过写入和读取 ``monitor_on``
文件来启动、停止和检查监测的当前状态。写入 ``on`` 该文件可以启动对有属性的目标的监测。写入
``off`` 该文件则停止这些目标。如果每个目标进程被终止，DAMON也会停止。下面的示例命令开启、关
闭和检查DAMON的状态::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

请注意，当监测开启时，你不能写到上述的debugfs文件。如果你在DAMON运行时写到这些文件，将会返
回一个错误代码，如 ``-EBUSY`` 。


监测线程PID
-----------

DAMON通过一个叫做kdamond的内核线程来进行请求监测。你可以通过读取 ``kdamond_pid`` 文件获
得该线程的 ``pid`` 。当监测被 ``关闭`` 时，读取该文件不会返回任何信息::

    # cd <debugfs>/damon
    # cat monitor_on
    off
    # cat kdamond_pid
    none
    # echo on > monitor_on
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

注意， ``mk_contexts`` 、 ``rm_contexts`` 和 ``monitor_on`` 文件只在根目录下。


监测结果的监测点
================

DAMON通过一个tracepoint ``damon:damon_aggregated`` 提供监测结果.  当监测开启时，你可
以记录追踪点事件，并使用追踪点支持工具如perf显示结果。比如说::

    # echo on > monitor_on
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on
    # perf script

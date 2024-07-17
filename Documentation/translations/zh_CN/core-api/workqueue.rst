.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/workqueue.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>
 周彬彬 Binbin Zhou <zhoubinbin@loongson.cn>
 陈兴友 Xingyou Chen <rockrush@rockwork.org>

.. _cn_workqueue.rst:

========
工作队列
========

:日期: September, 2010
:作者: Tejun Heo <tj@kernel.org>
:作者: Florian Mickler <florian@mickler.org>


简介
====

在很多情况下，需要一个异步的程序执行环境，工作队列（wq）API是这种情况下
最常用的机制。

当需要这样一个异步执行上下文时，一个描述将要执行的函数的工作项（work，
即一个待执行的任务）被放在队列中。一个独立的线程作为异步执行环境。该队
列被称为workqueue，线程被称为工作者（worker，即执行这一队列的线程）。

当工作队列上有工作项时，工作者会一个接一个地执行与工作项相关的函数。当
工作队列中没有任何工作项时，工作者就会变得空闲。当一个新的工作项被排入
队列时，工作者又开始执行。


为什么要有并发管理工作队列?
===========================

在最初的wq实现中，多线程（MT）wq在每个CPU上有一个工作者线程，而单线程
（ST）wq在全系统有一个工作者线程。一个MT wq需要保持与CPU数量相同的工
作者数量。这些年来，内核增加了很多MT wq的用户，随着CPU核心数量的不断
增加，一些系统刚启动就达到了默认的32k PID的饱和空间。

尽管MT wq浪费了大量的资源，但所提供的并发性水平却不能令人满意。这个限
制在ST和MT wq中都有，只是在MT中没有那么严重。每个wq都保持着自己独立的
工作者池。一个MT wq只能为每个CPU提供一个执行环境，而一个ST wq则为整个
系统提供一个。工作项必须竞争这些非常有限的执行上下文，从而导致各种问题，
包括在单一执行上下文周围容易发生死锁。

(MT wq)所提供的并发性水平和资源使用之间的矛盾也迫使其用户做出不必要的权衡，比
如libata选择使用ST wq来轮询PIO，并接受一个不必要的限制，即没有两个轮
询PIO可以同时进行。由于MT wq并没有提供更好的并发性，需要更高层次的并
发性的用户，如async或fscache，不得不实现他们自己的线程池。

并发管理工作队列（cmwq）是对wq的重新实现，重点是以下目标。

* 保持与原始工作队列API的兼容性。

* 使用由所有wq共享的每CPU统一的工作者池，在不浪费大量资源的情况下按
* 需提供灵活的并发水平。

* 自动调节工作者池和并发水平，使API用户不需要担心这些细节。


设计
====

为了简化函数的异步执行，引入了一个新的抽象概念，即工作项。

一个工作项是一个简单的结构，它持有一个指向将被异步执行的函数的指针。
每当一个驱动程序或子系统希望一个函数被异步执行时，它必须建立一个指
向该函数的工作项，并在工作队列中排队等待该工作项。（就是挂到workqueue
队列里面去）

工作项可以在线程或BH(软中断)上下文中执行。

对于由线程执行的工作队列，被称为（内核）工作者（[k]worker）的特殊
线程会依次执行其中的函数。如果没有工作项排队，工作者线程就会闲置。
这些工作者线程被管理在所谓的工作者池中。

cmwq设计区分了面向用户的工作队列，子系统和驱动程序在上面排队工作，
以及管理工作者池和处理排队工作项的后端机制。

每个可能的CPU都有两个工作者池，一个用于正常的工作项，另一个用于高
优先级的工作项，还有一些额外的工作者池，用于服务未绑定工作队列的工
作项目——这些后备池的数量是动态的。

BH工作队列使用相同的结构。然而，由于同一时间只可能有一个执行上下文，
不需要担心并发问题。每个CPU上的BH工作者池只包含一个用于表示BH执行
上下文的虚拟工作者。BH工作队列可以被看作软中断的便捷接口。

当他们认为合适的时候，子系统和驱动程序可以通过特殊的
``workqueue API`` 函数创建和排队工作项。他们可以通过在工作队列上
设置标志来影响工作项执行方式的某些方面，他们把工作项放在那里。这些
标志包括诸如CPU定位、并发限制、优先级等等。要获得详细的概述，请参
考下面的 ``alloc_workqueue()`` 的 API 描述。

当一个工作项被排入一个工作队列时，目标工作池将根据队列参数和工作队
列属性确定，并被附加到工作池的共享工作列表上。例如，除非特别重写，
否则一个绑定的工作队列的工作项将被排在与发起线程运行的CPU相关的普
通或高级工作工作者池的工作项列表中。

对于任何线程池的实施，管理并发水平（有多少执行上下文处于活动状
态）是一个重要问题。cmwq试图将并发保持在一个尽可能低且充足的
水平。最低水平是为了节省资源，而充足是为了使系统能被充分使用。

每个与实际CPU绑定的worker-pool通过钩住调度器来实现并发管理。每当
一个活动的工作者被唤醒或睡眠时，工作者池就会得到通知，并跟踪当前可
运行的工作者的数量。一般来说，工作项不会占用CPU并消耗很多周期。这
意味着保持足够的并发性以防止工作处理停滞应该是最优的。只要CPU上有
一个或多个可运行的工作者，工作者池就不会开始执行新的工作，但是，当
最后一个运行的工作者进入睡眠状态时，它会立即安排一个新的工作者，这
样CPU就不会在有待处理的工作项目时闲置。这允许在不损失执行带宽的情
况下使用最少的工作者。

除了kthreads的内存空间外，保留空闲的工作者并没有其他成本，所以cmwq
在杀死它们之前会保留一段时间的空闲。

对于非绑定的工作队列，后备池的数量是动态的。可以使用
``apply_workqueue_attrs()`` 为非绑定工作队列分配自定义属性，
workqueue将自动创建与属性相匹配的后备工作者池。调节并发水平的责任在
用户身上。也有一个标志可以将绑定的wq标记为忽略并发管理。
详情请参考API部分。

前进进度的保证依赖于当需要更多的执行上下文时可以创建工作者，这也是
通过使用救援工作者来保证的。所有可能在处理内存回收的代码路径上使用
的工作项都需要在wq上排队，wq上保留了一个救援工作者，以便在内存有压
力的情况下下执行。否则，工作者池就有可能出现死锁，等待执行上下文释
放出来。


应用程序编程接口 (API)
======================

``alloc_workqueue()`` 分配了一个wq。原来的 ``create_*workqueue()``
函数已被废弃，并计划删除。 ``alloc_workqueue()`` 需要三个
参数 - ``@name`` , ``@flags`` 和 ``@max_active`` 。
``@name`` 是wq的名称，如果有的话，也用作救援线程的名称。

一个wq不再管理执行资源，而是作为前进进度保证、刷新(flush)和
工作项属性的域。 ``@flags`` 和 ``@max_active`` 控制着工作
项如何被分配执行资源、安排和执行。


``flags``
---------

``WQ_BH``
  BH工作队列可以被看作软中断的便捷接口。它总是每个CPU一份，
  其中的各个工作项也会按在队列中的顺序，被所属CPU在软中断
  上下文中执行。

  BH工作队列的 ``max_active`` 值必须为0，且只能单独或和
  ``WQ_HIGHPRI`` 标志组合使用。

  BH工作项不可以睡眠。像延迟排队、冲洗、取消等所有其他特性
  都是支持的。

``WQ_UNBOUND``
  排队到非绑定wq的工作项由特殊的工作者池提供服务，这些工作者不
  绑定在任何特定的CPU上。这使得wq表现得像一个简单的执行环境提
  供者，没有并发管理。非绑定工作者池试图尽快开始执行工作项。非
  绑定的wq牺牲了局部性，但在以下情况下是有用的。

  * 预计并发水平要求会有很大的波动，使用绑定的wq最终可能会在不
    同的CPU上产生大量大部分未使用的工作者，因为发起线程在不同
    的CPU上跳转。

  * 长期运行的CPU密集型工作负载，可以由系统调度器更好地管理。

``WQ_FREEZABLE``
  一个可冻结的wq参与了系统暂停操作的冻结阶段。wq上的工作项被
  排空，在解冻之前没有新的工作项开始执行。

``WQ_MEM_RECLAIM``
  所有可能在内存回收路径中使用的wq都必须设置这个标志。无论内
  存压力如何，wq都能保证至少有一个执行上下文。

``WQ_HIGHPRI``
  高优先级wq的工作项目被排到目标cpu的高优先级工作者池中。高
  优先级的工作者池由具有较高级别的工作者线程提供服务。

  请注意，普通工作者池和高优先级工作者池之间并不相互影响。他
  们各自维护其独立的工作者池，并在其工作者之间实现并发管理。

``WQ_CPU_INTENSIVE``
  CPU密集型wq的工作项对并发水平没有贡献。换句话说，可运行的
  CPU密集型工作项不会阻止同一工作者池中的其他工作项开始执行。
  这对于那些预计会占用CPU周期的绑定工作项很有用，这样它们的
  执行就会受到系统调度器的监管。

  尽管CPU密集型工作项不会对并发水平做出贡献，但它们的执行开
  始仍然受到并发管理的管制，可运行的非CPU密集型工作项会延迟
  CPU密集型工作项的执行。

  这个标志对于未绑定的wq来说是没有意义的。


``max_active``
--------------

``@max_active`` 决定了每个CPU可以分配给wq的工作项的最大执行上
下文数量。例如，如果 ``@max_active`` 为16 ，每个CPU最多可以同
时执行16个wq的工作项。它总是每CPU属性，即便对于未绑定 wq。

``@max_active`` 的最大限制是512，当指定为0时使用的默认值是256。
这些值被选得足够高，所以它们不是限制性因素，同时会在失控情况下提供
保护。

一个wq的活动工作项的数量通常由wq的用户来调节，更具体地说，是由用
户在同一时间可以排列多少个工作项来调节。除非有特定的需求来控制活动
工作项的数量，否则建议指定 为"0"。

一些用户依赖于任意时刻最多只有一个工作项被执行，且各工作项被按队列中
顺序处理带来的严格执行顺序。``@max_active`` 为1和 ``WQ_UNBOUND``
的组合曾被用来实现这种行为，现在不用了。请使用
``alloc_ordered_workqueue()`` 。


执行场景示例
============

下面的示例执行场景试图说明cmwq在不同配置下的行为。

 工作项w0、w1、w2被排到同一个CPU上的一个绑定的wq q0上。w0
 消耗CPU 5ms，然后睡眠10ms，然后在完成之前再次消耗CPU 5ms。

忽略所有其他的任务、工作和处理开销，并假设简单的FIFO调度，
下面是一个高度简化的原始wq的可能事件序列的版本。::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 starts and burns CPU
 25		w1 sleeps
 35		w1 wakes up and finishes
 35		w2 starts and burns CPU
 40		w2 sleeps
 50		w2 wakes up and finishes

And with cmwq with ``@max_active`` >= 3, ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 10		w2 starts and burns CPU
 15		w2 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 25		w2 wakes up and finishes

如果 ``@max_active`` == 2, ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 20		w2 starts and burns CPU
 25		w2 sleeps
 35		w2 wakes up and finishes

现在，我们假设w1和w2被排到了不同的wq q1上，这个wq q1
有 ``WQ_CPU_INTENSIVE`` 设置::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 and w2 start and burn CPU
 10		w1 sleeps
 15		w2 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 25		w2 wakes up and finishes


指南
====

* 如果一个wq可能处理在内存回收期间使用的工作项目，请不
  要忘记使用 ``WQ_MEM_RECLAIM`` 。每个设置了
  ``WQ_MEM_RECLAIM`` 的wq都有一个为其保留的执行环境。
  如果在内存回收过程中使用的多个工作项之间存在依赖关系，
  它们应该被排在不同的wq中，每个wq都有 ``WQ_MEM_RECLAIM`` 。

* 除非需要严格排序，否则没有必要使用ST wq。

* 除非有特殊需要，建议使用0作为@max_active。在大多数使用情
  况下，并发水平通常保持在默认限制之下。

* 一个wq作为前进进度保证，``WQ_MEM_RECLAIM`` ，冲洗（flush）和工
  作项属性的域。不涉及内存回收的工作项，不需要作为工作项组的一
  部分被刷新，也不需要任何特殊属性，可以使用系统中的一个wq。使
  用专用wq和系统wq在执行特性上没有区别。

* 除非工作项预计会消耗大量的CPU周期，否则使用绑定的wq通常是有
  益的，因为wq操作和工作项执行中的定位水平提高了。


亲和性作用域
============

一个非绑定工作队列根据其亲和性作用域来对CPU进行分组以提高缓存
局部性。比如如果一个工作队列使用默认的“cache”亲和性作用域，
它将根据最后一级缓存的边界来分组处理器。这个工作队列上的工作项
将被分配给一个与发起CPU共用最后级缓存的处理器上的工作者。根据
``affinity_strict`` 的设置，工作者在启动后可能被允许移出
所在作用域，也可能不被允许。

工作队列目前支持以下亲和性作用域。

``default``
  使用模块参数 ``workqueue.default_affinity_scope`` 指定
  的作用域，该参数总是会被设为以下作用域中的一个。

``cpu``
  CPU不被分组。一个CPU上发起的工作项会被同一CPU上的工作者执行。
  这使非绑定工作队列表现得像是不含并发管理的每CPU工作队列。

``smt``
  CPU被按SMT边界分组。这通常意味着每个物理CPU核上的各逻辑CPU会
  被分进同一组。

``cache``
  CPU被按缓存边界分组。采用哪个缓存边界由架构代码决定。很多情况
  下会使用L3。这是默认的亲和性作用域。

``numa``
  CPU被按NUMA边界分组。

``system``
  所有CPU被放在同一组。工作队列不尝试在临近发起CPU的CPU上运行
  工作项。

默认的亲和性作用域可以被模块参数 ``workqueue.default_affinity_scope``
修改，特定工作队列的亲和性作用域可以通过 ``apply_workqueue_attrs()``
被更改。

如果设置了 ``WQ_SYSFS`` ，工作队列会在它的 ``/sys/devices/virtual/workqueue/WQ_NAME/``
目录中有以下亲和性作用域相关的接口文件。

``affinity_scope``
  读操作以查看当前的亲和性作用域。写操作用于更改设置。

  当前作用域是默认值时，当前生效的作用域也可以被从这个文件中
  读到（小括号内），例如 ``default (cache)`` 。

``affinity_strict``
  默认值0表明亲和性作用域不是严格的。当一个工作项开始执行时，
  工作队列尽量尝试使工作者处于亲和性作用域内，称为遣返。启动后，
  调度器可以自由地将工作者调度到系统中任意它认为合适的地方去。
  这使得在保留使用其他CPU（如果必需且有可用）能力的同时，
  还能从作用域局部性上获益。

  如果设置为1，作用域内的所有工作者将被保证总是处于作用域内。
  这在跨亲和性作用域会导致如功耗、负载隔离等方面的潜在影响时
  会有用。严格的NUMA作用域也可用于和旧版内核中工作队列的行为
  保持一致。


亲和性作用域与性能
==================

如果非绑定工作队列的行为对绝大多数使用场景来说都是最优的，
不需要更多调节，就完美了。很不幸，在当前内核中，重度使用
工作队列时，需要在局部性和利用率间显式地作一个明显的权衡。

更高的局部性带来更高效率，也就是相同数量的CPU周期内可以做
更多工作。然而，如果发起者没能将工作项充分地分散在亲和性
作用域间，更高的局部性也可能带来更低的整体系统利用率。以下
dm-crypt 的性能测试清楚地阐明了这一取舍。

测试运行在一个12核24线程、4个L3缓存的处理器（AMD Ryzen
9 3900x）上。为保持一致性，关闭CPU超频。 ``/dev/dm-0``
是NVME SSD（三星 990 PRO）上创建，用 ``cryptsetup``
以默认配置打开的一个 dm-crypt 设备。


场景 1: 机器上遍布着有充足的发起者和工作量
------------------------------------------

使用命令：::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k --ioengine=libaio \
    --iodepth=64 --runtime=60 --numjobs=24 --time_based --group_reporting \
    --name=iops-test-job --verify=sha512

这里有24个发起者，每个同时发起64个IO。 ``--verify=sha512``
使得 ``fio`` 每次生成和读回内容受发起者和 ``kcryptd``
间的执行局部性影响。下面是基于不同 ``kcryptd`` 的亲和性
作用域设置，各经过五次测试得到的读取带宽和CPU利用率数据。

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - 亲和性
     - 带宽 (MiBps)
     - CPU利用率（%）

   * - system
     - 1159.40 ±1.34
     - 99.31 ±0.02

   * - cache
     - 1166.40 ±0.89
     - 99.34 ±0.01

   * - cache (strict)
     - 1166.00 ±0.71
     - 99.35 ±0.01

在系统中分布着足够多发起者的情况下，不论严格与否，“cache”
没有表现得更差。三种配置均使整个机器达到饱和，但由于提高了
局部性，缓存相关的两种有0.6%的（带宽）提升。


场景 2: 更少发起者，足以达到饱和的工作量
----------------------------------------

使用命令：::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k \
    --ioengine=libaio --iodepth=64 --runtime=60 --numjobs=8 \
    --time_based --group_reporting --name=iops-test-job --verify=sha512

与上一个场景唯一的区别是 ``--numjobs=8``。 发起者数量
减少为三分之一，但仍然有足以使系统达到饱和的工作总量。

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - 亲和性
     - 带宽 (MiBps)
     - CPU利用率（%）

   * - system
     - 1155.40 ±0.89
     - 97.41 ±0.05

   * - cache
     - 1154.40 ±1.14
     - 96.15 ±0.09

   * - cache (strict)
     - 1112.00 ±4.64
     - 93.26 ±0.35

这里有超过使系统达到饱和所需的工作量。“system”和“cache”
都接近但并未使机器完全饱和。“cache”消耗更少的CPU但更高的
效率使其得到和“system”相同的带宽。

八个发起者盘桓在四个L3缓存作用域间仍然允许“cache (strict)”
几乎使机器饱和，但缺少对工作的保持（不移到空闲处理器上）
开始带来3.7%的带宽损失。


场景 3: 更少发起者，不充足的工作量
----------------------------------

使用命令：::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k \
    --ioengine=libaio --iodepth=64 --runtime=60 --numjobs=4 \
    --time_based --group_reporting --name=iops-test-job --verify=sha512

再次，唯一的区别是 ``--numjobs=4``。由于发起者减少到四个，
现在没有足以使系统饱和的工作量，带宽变得依赖于完成时延。

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - 亲和性
     - 带宽 (MiBps)
     - CPU利用率（%）

   * - system
     - 993.60 ±1.82
     - 75.49 ±0.06

   * - cache
     - 973.40 ±1.52
     - 74.90 ±0.07

   * - cache (strict)
     - 828.20 ±4.49
     - 66.84 ±0.29

现在，局部性和利用率间的权衡更清晰了。“cache”展示出相比
“system”2%的带宽损失，而“cache (strict)”跌到20%。


结论和建议
----------

在以上试验中，虽然一致并且也明显，但“cache”亲和性作用域
相比“system”的性能优势并不大。然而，这影响是依赖于作用域
间距离的，在更复杂的处理器拓扑下可能有更明显的影响。

虽然这些情形下缺少工作保持是有坏处的，但比“cache (strict)”
好多了，而且最大化工作队列利用率的需求也并不常见。因此，
“cache”是非绑定池的默认亲和性作用域。

* 由于不存在一个适用于大多数场景的选择，对于可能需要消耗
  大量CPU的工作队列，建议通过 ``apply_workqueue_attrs()``
  进行（专门）配置，并考虑是否启用 ``WQ_SYSFS``。

* 设置了严格“cpu”亲和性作用域的非绑定工作队列，它的行为与
  ``WQ_CPU_INTENSIVE`` 每CPU工作队列一样。后者没有真正
  优势，而前者提供了大幅度的灵活性。

* 亲和性作用域是从Linux v6.5起引入的。为了模拟旧版行为，
  可以使用严格的“numa”亲和性作用域。

* 不严格的亲和性作用域中，缺少工作保持大概缘于调度器。内核
  为什么没能维护好大多数场景下的工作保持，把事情作对，还没有
  理论上的解释。因此，未来调度器的改进可能会使我们不再需要
  这些调节项。


检查配置
========

使用 tools/workqueue/wq_dump.py（drgn脚本） 来检查未
绑定CPU的亲和性配置，工作者池，以及工作队列如何映射到池上: ::

  $ tools/workqueue/wq_dump.py
  Affinity Scopes
  ===============
  wq_unbound_cpumask=0000000f

  CPU
    nr_pods  4
    pod_cpus [0]=00000001 [1]=00000002 [2]=00000004 [3]=00000008
    pod_node [0]=0 [1]=0 [2]=1 [3]=1
    cpu_pod  [0]=0 [1]=1 [2]=2 [3]=3

  SMT
    nr_pods  4
    pod_cpus [0]=00000001 [1]=00000002 [2]=00000004 [3]=00000008
    pod_node [0]=0 [1]=0 [2]=1 [3]=1
    cpu_pod  [0]=0 [1]=1 [2]=2 [3]=3

  CACHE (default)
    nr_pods  2
    pod_cpus [0]=00000003 [1]=0000000c
    pod_node [0]=0 [1]=1
    cpu_pod  [0]=0 [1]=0 [2]=1 [3]=1

  NUMA
    nr_pods  2
    pod_cpus [0]=00000003 [1]=0000000c
    pod_node [0]=0 [1]=1
    cpu_pod  [0]=0 [1]=0 [2]=1 [3]=1

  SYSTEM
    nr_pods  1
    pod_cpus [0]=0000000f
    pod_node [0]=-1
    cpu_pod  [0]=0 [1]=0 [2]=0 [3]=0

  Worker Pools
  ============
  pool[00] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  0
  pool[01] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  0
  pool[02] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  1
  pool[03] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  1
  pool[04] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  2
  pool[05] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  2
  pool[06] ref= 1 nice=  0 idle/workers=  3/  3 cpu=  3
  pool[07] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  3
  pool[08] ref=42 nice=  0 idle/workers=  6/  6 cpus=0000000f
  pool[09] ref=28 nice=  0 idle/workers=  3/  3 cpus=00000003
  pool[10] ref=28 nice=  0 idle/workers= 17/ 17 cpus=0000000c
  pool[11] ref= 1 nice=-20 idle/workers=  1/  1 cpus=0000000f
  pool[12] ref= 2 nice=-20 idle/workers=  1/  1 cpus=00000003
  pool[13] ref= 2 nice=-20 idle/workers=  1/  1 cpus=0000000c

  Workqueue CPU -> pool
  =====================
  [    workqueue \ CPU              0  1  2  3 dfl]
  events                   percpu   0  2  4  6
  events_highpri           percpu   1  3  5  7
  events_long              percpu   0  2  4  6
  events_unbound           unbound  9  9 10 10  8
  events_freezable         percpu   0  2  4  6
  events_power_efficient   percpu   0  2  4  6
  events_freezable_power_  percpu   0  2  4  6
  rcu_gp                   percpu   0  2  4  6
  rcu_par_gp               percpu   0  2  4  6
  slub_flushwq             percpu   0  2  4  6
  netns                    ordered  8  8  8  8  8
  ...

参见命令的帮助消息以获取更多信息。


监视
====

使用 tools/workqueue/wq_monitor.py 来监视工作队列的运行： ::

  $ tools/workqueue/wq_monitor.py events
                              total  infl  CPUtime  CPUhog CMW/RPR  mayday rescued
  events                      18545     0      6.1       0       5       -       -
  events_highpri                  8     0      0.0       0       0       -       -
  events_long                     3     0      0.0       0       0       -       -
  events_unbound              38306     0      0.1       -       7       -       -
  events_freezable                0     0      0.0       0       0       -       -
  events_power_efficient      29598     0      0.2       0       0       -       -
  events_freezable_power_        10     0      0.0       0       0       -       -
  sock_diag_events                0     0      0.0       0       0       -       -

                              total  infl  CPUtime  CPUhog CMW/RPR  mayday rescued
  events                      18548     0      6.1       0       5       -       -
  events_highpri                  8     0      0.0       0       0       -       -
  events_long                     3     0      0.0       0       0       -       -
  events_unbound              38322     0      0.1       -       7       -       -
  events_freezable                0     0      0.0       0       0       -       -
  events_power_efficient      29603     0      0.2       0       0       -       -
  events_freezable_power_        10     0      0.0       0       0       -       -
  sock_diag_events                0     0      0.0       0       0       -       -

  ...

参见命令的帮助消息以获取更多信息。


调试
====

因为工作函数是由通用的工作者线程执行的，所以需要一些手段来揭示一些行为不端的工作队列用户。

工作者线程在进程列表中显示为: ::

  root      5671  0.0  0.0      0     0 ?        S    12:07   0:00 [kworker/0:1]
  root      5672  0.0  0.0      0     0 ?        S    12:07   0:00 [kworker/1:2]
  root      5673  0.0  0.0      0     0 ?        S    12:12   0:00 [kworker/0:0]
  root      5674  0.0  0.0      0     0 ?        S    12:13   0:00 [kworker/1:0]

如果kworkers失控了（使用了太多的cpu），有两类可能的问题:

	1. 正在迅速调度的事情
	2. 一个消耗大量cpu周期的工作项。

第一个可以用追踪的方式进行跟踪: ::

	$ echo workqueue:workqueue_queue_work > /sys/kernel/tracing/set_event
	$ cat /sys/kernel/tracing/trace_pipe > out.txt
	(wait a few secs)

如果有什么东西在工作队列上忙着做循环，它就会主导输出，可以用工作项函数确定违规者。

对于第二类问题，应该可以只检查违规工作者线程的堆栈跟踪。 ::

	$ cat /proc/THE_OFFENDING_KWORKER/stack

工作项函数在堆栈追踪中应该是微不足道的。

不可重入条件
============

工作队列保证，如果在工作项排队后满足以下条件，则工作项不能重入：

        1. 工作函数没有被改变。
        2. 没有人将该工作项排到另一个工作队列中。
        3. 该工作项尚未被重新启动。

换言之，如果上述条件成立，则保证在任何给定时间最多由一个系统范围内的工作程序执行
该工作项。

请注意，在self函数中将工作项重新排队（到同一队列）不会破坏这些条件，因此可以安全
地执行此操作。否则在破坏工作函数内部的条件时需要小心。


内核内联文档参考
================

该API在以下内核代码中:

include/linux/workqueue.h

kernel/workqueue.c

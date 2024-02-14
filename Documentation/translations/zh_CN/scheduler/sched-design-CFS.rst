.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scheduler/sched-design-CFS.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

===============
完全公平调度器
===============


1. 概述
=======

CFS表示“完全公平调度器”，它是为桌面新设计的进程调度器，由Ingo Molnar实现并合入Linux
2.6.23。它替代了之前原始调度器中SCHED_OTHER策略的交互式代码。

CFS 80%的设计可以总结为一句话：CFS在真实硬件上建模了一个“理想的，精确的多任务CPU”。

“理想的多任务CPU”是一种（不存在的 :-)）具有100%物理算力的CPU，它能让每个任务精确地以
相同的速度并行运行，速度均为1/nr_running。举例来说，如果有两个任务正在运行，那么每个
任务获得50%物理算力。 --- 也就是说，真正的并行。

在真实的硬件上，一次只能运行一个任务，所以我们需要介绍“虚拟运行时间”的概念。任务的虚拟
运行时间表明，它的下一个时间片将在上文描述的理想多任务CPU上开始执行。在实践中，任务的
虚拟运行时间由它的真实运行时间相较正在运行的任务总数归一化计算得到。



2. 一些实现细节
===============

在CFS中，虚拟运行时间由每个任务的p->se.vruntime（单位为纳秒）的值表达和跟踪。因此，
精确地计时和测量一个任务应得的“预期的CPU时间”是可能的。

  一些细节：在“理想的”硬件上，所有的任务在任何时刻都应该具有一样的p->se.vruntime值，
  --- 也就是说，任务应当同时执行，没有任务会在“理想的”CPU分时中变得“不平衡”。

CFS的任务选择逻辑基于p->se.vruntime的值，因此非常简单：总是试图选择p->se.vruntime值
最小的任务运行（也就是说，至今执行时间最少的任务）。CFS总是尽可能尝试按“理想多任务硬件”
那样将CPU时间在可运行任务中均分。

CFS剩下的其它设计，一般脱离了这个简单的概念，附加的设计包括nice级别，多处理，以及各种
用来识别已睡眠任务的算法变体。



3. 红黑树
=========

CFS的设计非常激进：它不使用运行队列的旧数据结构，而是使用按时间排序的红黑树，构建出
任务未来执行的“时间线”。因此没有任何“数组切换”的旧包袱（之前的原始调度器和RSDL/SD都
被它影响）。

CFS同样维护了rq->cfs.min_vruntime值，它是单调递增的，跟踪运行队列中的所有任务的最小
虚拟运行时间值。系统做的全部工作是：使用min_vruntime跟踪，然后用它的值将新激活的调度
实体尽可能地放在红黑树的左侧。

运行队列中正在运行的任务的总数由rq->cfs.load计数，它是运行队列中的任务的权值之和。

CFS维护了一个按时间排序的红黑树，所有可运行任务以p->se.vruntime为键值排序。CFS从这颗
树上选择“最左侧”的任务并运行。系统继续运行，被执行过的任务越来越被放到树的右侧 --- 缓慢，
但很明确每个任务都有成为“最左侧任务”的机会，因此任务将确定性地获得一定量CPU时间。

总结一下，CFS工作方式像这样：它运行一个任务一会儿，当任务发生调度（或者调度器时钟滴答
tick产生），就会考虑任务的CPU使用率：任务刚刚花在物理CPU上的（少量）时间被加到
p->se.vruntime。一旦p->se.vruntime变得足够大，其它的任务将成为按时间排序的红黑树的
“最左侧任务”（相较最左侧的任务，还要加上一个很小的“粒度”量，使得我们不会对任务过度调度，
导致缓存颠簸），然后新的最左侧任务将被选中，当前任务被抢占。




4. CFS的一些特征
================

CFS使用纳秒粒度的计时，不依赖于任何jiffies或HZ的细节。因此CFS并不像之前的调度器那样
有“时间片”的概念，也没有任何启发式的设计。唯一可调的参数（你需要打开CONFIG_SCHED_DEBUG）是：

   /sys/kernel/debug/sched/min_granularity_ns

它可以用来将调度器从“桌面”模式（也就是低时延）调节为“服务器”（也就是高批处理）模式。
它的默认设置是适合桌面的工作负载。SCHED_BATCH也被CFS调度器模块处理。

CFS的设计不易受到当前存在的任何针对stock调度器的“攻击”的影响，包括fiftyp.c，thud.c，
chew.c，ring-test.c，massive_intr.c，它们都能很好地运行，不会影响交互性，将产生
符合预期的行为。

CFS调度器处理nice级别和SCHED_BATCH的能力比之前的原始调度器更强：两种类型的工作负载
都被更激进地隔离了。

SMP负载均衡被重做/清理过：遍历运行队列的假设已经从负载均衡的代码中移除，使用调度模块
的迭代器。结果是，负载均衡代码变得简单不少。



5. 调度策略
===========

CFS实现了三种调度策略：

  - SCHED_NORMAL：（传统被称为SCHED_OTHER）：该调度策略用于普通任务。

  - SCHED_BATCH：抢占不像普通任务那样频繁，因此允许任务运行更长时间，更好地利用缓存，
    不过要以交互性为代价。它很适合批处理工作。

  - SCHED_IDLE：它比nice 19更弱，不过它不是真正的idle定时器调度器，因为要避免给机器
    带来死锁的优先级反转问题。

SCHED_FIFO/_RR被实现在sched/rt.c中，它们由POSIX具体说明。

util-linux-ng 2.13.1.1中的chrt命令可以设置以上所有策略，除了SCHED_IDLE。



6. 调度类
=========

新的CFS调度器被设计成支持“调度类”，一种调度模块的可扩展层次结构。这些模块封装了调度策略
细节，由调度器核心代码处理，且无需对它们做太多假设。

sched/fair.c 实现了上文描述的CFS调度器。

sched/rt.c 实现了SCHED_FIFO和SCHED_RR语义，且比之前的原始调度器更简洁。它使用了100个
运行队列（总共100个实时优先级，替代了之前调度器的140个），且不需要过期数组（expired
array）。

调度类由sched_class结构体实现，它包括一些函数钩子，当感兴趣的事件发生时，钩子被调用。

这是（部分）钩子的列表：

 - enqueue_task(...)

   当任务进入可运行状态时，被调用。它将调度实体（任务）放到红黑树中，增加nr_running变量
   的值。

 - dequeue_task(...)

   当任务不再可运行时，这个函数被调用，对应的调度实体被移出红黑树。它减少nr_running变量
   的值。

 - yield_task(...)

   这个函数的行为基本上是出队，紧接着入队，除非compat_yield sysctl被开启。在那种情况下，
   它将调度实体放在红黑树的最右端。

 - check_preempt_curr(...)

   这个函数检查进入可运行状态的任务能否抢占当前正在运行的任务。

 - pick_next_task(...)

   这个函数选择接下来最适合运行的任务。

 - set_curr_task(...)

   这个函数在任务改变调度类或改变任务组时被调用。

 - task_tick(...)

   这个函数最常被时间滴答函数调用，它可能导致进程切换。这驱动了运行时抢占。




7. CFS的组调度扩展
==================

通常，调度器操作粒度为任务，努力为每个任务提供公平的CPU时间。有时可能希望将任务编组，
并为每个组提供公平的CPU时间。举例来说，可能首先希望为系统中的每个用户提供公平的CPU
时间，接下来才是某个用户的每个任务。

CONFIG_CGROUP_SCHED 力求实现它。它将任务编组，并为这些组公平地分配CPU时间。

CONFIG_RT_GROUP_SCHED 允许将实时（也就是说，SCHED_FIFO和SCHED_RR）任务编组。

CONFIG_FAIR_GROUP_SCHED 允许将CFS（也就是说，SCHED_NORMAL和SCHED_BATCH）任务编组。

   这些编译选项要求CONFIG_CGROUPS被定义，然后管理员能使用cgroup伪文件系统任意创建任务组。
   关于该文件系统的更多信息，参见Documentation/admin-guide/cgroup-v1/cgroups.rst

当CONFIG_FAIR_GROUP_SCHED被定义后，通过伪文件系统，每个组被创建一个“cpu.shares”文件。
参见下面的例子来创建任务组，并通过“cgroup”伪文件系统修改它们的CPU份额::

	# mount -t tmpfs cgroup_root /sys/fs/cgroup
	# mkdir /sys/fs/cgroup/cpu
	# mount -t cgroup -ocpu none /sys/fs/cgroup/cpu
	# cd /sys/fs/cgroup/cpu

	# mkdir multimedia	# 创建 "multimedia" 任务组
	# mkdir browser		# 创建 "browser" 任务组

	# #配置multimedia组，令其获得browser组两倍CPU带宽

	# echo 2048 > multimedia/cpu.shares
	# echo 1024 > browser/cpu.shares

	# firefox &	# 启动firefox并把它移到 "browser" 组
	# echo <firefox_pid> > browser/tasks

	# #启动gmplayer（或者你最喜欢的电影播放器）
	# echo <movie_player_pid> > multimedia/tasks

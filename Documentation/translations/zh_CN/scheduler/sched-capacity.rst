.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scheduler/sched-capacity.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

:校译:

  时奎亮 Alex Shi <alexs@kernel.org>

=============
算力感知调度
=============

1. CPU算力
==========

1.1 简介
--------

一般来说，同构的SMP平台由完全相同的CPU构成。异构的平台则由性能特征不同的CPU构成，在这样的
平台中，CPU不能被认为是相同的。

我们引入CPU算力（capacity）的概念来测量每个CPU能达到的性能，它的值相对系统中性能最强的CPU
做过归一化处理。异构系统也被称为非对称CPU算力系统，因为它们由不同算力的CPU组成。

最大可达性能（换言之，最大CPU算力）的差异有两个主要来源:

- 不是所有CPU的微架构都相同。
- 在动态电压频率升降（Dynamic Voltage and Frequency Scaling，DVFS）框架中，不是所有的CPU都
  能达到一样高的操作性能值（Operating Performance Points，OPP。译注，也就是“频率-电压”对）。

Arm大小核（big.LITTLE）系统是同时具有两种差异的一个例子。相较小核，大核面向性能（拥有更多的
流水线层级，更大的缓存，更智能的分支预测器等），通常可以达到更高的操作性能值。

CPU性能通常由每秒百万指令（Millions of Instructions Per Second，MIPS）表示，也可表示为
per Hz能执行的指令数，故::

  capacity(cpu) = work_per_hz(cpu) * max_freq(cpu)

1.2 调度器术语
--------------

调度器使用了两种不同的算力值。CPU的 ``capacity_orig`` 是它的最大可达算力，即最大可达性能等级。
CPU的 ``capacity`` 是 ``capacity_orig`` 扣除了一些性能损失（比如处理中断的耗时）的值。

注意CPU的 ``capacity`` 仅仅被设计用于CFS调度类，而 ``capacity_orig`` 是不感知调度类的。为
简洁起见，本文档的剩余部分将不加区分的使用术语 ``capacity`` 和 ``capacity_orig`` 。

1.3 平台示例
------------

1.3.1 操作性能值相同
~~~~~~~~~~~~~~~~~~~~

考虑一个假想的双核非对称CPU算力系统，其中

- work_per_hz(CPU0) = W
- work_per_hz(CPU1) = W/2
- 所有CPU以相同的固定频率运行

根据上文对算力的定义:

- capacity(CPU0) = C
- capacity(CPU1) = C/2

若这是Arm大小核系统，那么CPU0是大核，而CPU1是小核。

考虑一种周期性产生固定工作量的工作负载，你将会得到类似下图的执行轨迹::

 CPU0 work ^
           |     ____                ____                ____
           |    |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

 CPU1 work ^
           |     _________           _________           ____
           |    |         |         |         |         |
           +----+----+----+----+----+----+----+----+----+----+-> time

CPU0在系统中具有最高算力（C），它使用T个单位时间完成固定工作量W。另一方面，CPU1只有CPU0一半
算力，因此在T个单位时间内仅完成工作量W/2。

1.3.2 最大操作性能值不同
~~~~~~~~~~~~~~~~~~~~~~~~

具有不同算力值的CPU，通常来说最大操作性能值也不同。考虑上一小节提到的CPU（也就是说，
work_per_hz()相同）:

- max_freq(CPU0) = F
- max_freq(CPU1) = 2/3 * F

这将推出：

- capacity(CPU0) = C
- capacity(CPU1) = C/3

执行1.3.1节描述的工作负载，每个CPU按最大频率运行，结果为::

 CPU0 work ^
           |     ____                ____                ____
           |    |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

                            workload on CPU1
 CPU1 work ^
           |     ______________      ______________      ____
           |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

1.4 关于计算方式的注意事项
--------------------------

需要注意的是，使用单一值来表示CPU性能的差异是有些争议的。两个不同的微架构的相对性能差异应该
描述为：X%整数运算差异，Y%浮点数运算差异，Z%分支跳转差异，等等。尽管如此，使用简单计算方式
的结果目前还是令人满意的。

2. 任务使用率
=============

2.1 简介
--------

算力感知调度要求描述任务需求，描述方式要和CPU算力相关。每个调度类可以用不同的方式描述它。
任务使用率是CFS独有的描述方式，不过在这里介绍它有助于引入更多一般性的概念。

任务使用率是一种用百分比来描述任务吞吐率需求的方式。一个简单的近似是任务的占空比，也就是说::

  task_util(p) = duty_cycle(p)

在频率固定的SMP系统中，100%的利用率意味着任务是忙等待循环。反之，10%的利用率暗示这是一个
小周期任务，它在睡眠上花费的时间比执行更多。

2.2 频率不变性
--------------

一个需要考虑的议题是，工作负载的占空比受CPU正在运行的操作性能值直接影响。考虑以给定的频率F
执行周期性工作负载::

  CPU work ^
           |     ____                ____                ____
           |    |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

可以算出 duty_cycle(p) == 25%。

现在，考虑以给定频率F/2执行 *同一个* 工作负载::

  CPU work ^
           |     _________           _________           ____
           |    |         |         |         |         |
           +----+----+----+----+----+----+----+----+----+----+-> time

可以算出 duty_cycle(p) == 50%，尽管两次执行中，任务的行为完全一致（也就是说，执行的工作量
相同）。

任务利用率信号可按下面公式处理成频率不变的（译注：这里的术语用到了信号与系统的概念）::

  task_util_freq_inv(p) = duty_cycle(p) * (curr_frequency(cpu) / max_frequency(cpu))

对上面两个例子运用该公式，可以算出频率不变的任务利用率均为25%。

2.3 CPU不变性
-------------

CPU算力与任务利用率具有类型的效应，在算力不同的CPU上执行完全相同的工作负载，将算出不同的
占空比。

考虑1.3.2节提到的系统，也就是说::

- capacity(CPU0) = C
- capacity(CPU1) = C/3

每个CPU按最大频率执行指定周期性工作负载，结果为::

 CPU0 work ^
           |     ____                ____                ____
           |    |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

 CPU1 work ^
           |     ______________      ______________      ____
           |    |              |    |              |    |
           +----+----+----+----+----+----+----+----+----+----+-> time

也就是说，

- duty_cycle(p) == 25%，如果任务p在CPU0上按最大频率运行。
- duty_cycle(p) == 75%，如果任务p在CPU1上按最大频率运行。

任务利用率信号可按下面公式处理成CPU算力不变的::

  task_util_cpu_inv(p) = duty_cycle(p) * (capacity(cpu) / max_capacity)

其中 ``max_capacity`` 是系统中最高的CPU算力。对上面的例子运用该公式，可以算出CPU算力不变
的任务利用率均为25%。

2.4 任务利用率不变量
--------------------

频率和CPU算力不变性都需要被应用到任务利用率的计算中，以便求出真正的不变信号。
任务利用率的伪计算公式是同时具备CPU和频率不变性的，也就是说，对于指定任务p::

                                     curr_frequency(cpu)   capacity(cpu)
  task_util_inv(p) = duty_cycle(p) * ------------------- * -------------
                                     max_frequency(cpu)    max_capacity

也就是说，任务利用率不变量假定任务在系统中最高算力CPU上以最高频率运行，以此描述任务的行为。

在接下来的章节中提到的任何任务利用率，均是不变量的形式。

2.5 利用率估算
--------------

由于预测未来的水晶球不存在，当任务第一次变成可运行时，任务的行为和任务利用率均不能被准确预测。
CFS调度类基于实体负载跟踪机制（Per-Entity Load Tracking, PELT）维护了少量CPU和任务信号，
其中之一可以算出平均利用率（与瞬时相反）。

这意味着，尽管运用“真实的”任务利用率（凭借水晶球）写出算力感知调度的准则，但是它的实现将只能
用任务利用率的估算值。

3. 算力感知调度的需求
=====================

3.1 CPU算力
-----------

当前，Linux无法凭自身算出CPU算力，因此必须要有把这个信息传递给Linux的方式。每个架构必须为此
定义arch_scale_cpu_capacity()函数。

arm和arm64架构直接把这个信息映射到arch_topology驱动的CPU scaling数据中（译注：参考
arch_topology.h的percpu变量cpu_scale），它是从capacity-dmips-mhz CPU binding中衍生计算
出来的。参见Documentation/devicetree/bindings/cpu/cpu-capacity.txt。

3.2 频率不变性
--------------

如2.2节所述，算力感知调度需要频率不变的任务利用率。每个架构必须为此定义
arch_scale_freq_capacity(cpu)函数。

实现该函数要求计算出每个CPU当前以什么频率在运行。实现它的一种方式是利用硬件计数器（x86的
APERF/MPERF，arm64的AMU），它能按CPU当前频率动态可扩展地升降递增计数器的速率。另一种方式是
在cpufreq频率变化时直接使用钩子函数，内核此时感知到将要被切换的频率（也被arm/arm64实现了）。

4. 调度器拓扑结构
=================

在构建调度域时，调度器将会发现系统是否表现为非对称CPU算力。如果是，那么：

- sched_asym_cpucapacity静态键（static key）将使能。
- SD_ASYM_CPUCAPACITY_FULL标志位将在尽量最低调度域层级中被设置，同时要满足条件：调度域恰好
  完整包含某个CPU算力值的全部CPU。
- SD_ASYM_CPUCAPACITY标志将在所有包含非对称CPU的调度域中被设置。

sched_asym_cpucapacity静态键的设计意图是，保护为非对称CPU算力系统所准备的代码。不过要注意的
是，这个键是系统范围可见的。想象下面使用了cpuset的步骤::

  capacity    C/2          C
            ________    ________
           /        \  /        \
  CPUs     0  1  2  3  4  5  6  7
           \__/  \______________/
  cpusets   cs0         cs1

可以通过下面的方式创建：

.. code-block:: sh

  mkdir /sys/fs/cgroup/cpuset/cs0
  echo 0-1 > /sys/fs/cgroup/cpuset/cs0/cpuset.cpus
  echo 0 > /sys/fs/cgroup/cpuset/cs0/cpuset.mems

  mkdir /sys/fs/cgroup/cpuset/cs1
  echo 2-7 > /sys/fs/cgroup/cpuset/cs1/cpuset.cpus
  echo 0 > /sys/fs/cgroup/cpuset/cs1/cpuset.mems

  echo 0 > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance

由于“这是”非对称CPU算力系统，sched_asym_cpucapacity静态键将使能。然而，CPU 0--1对应的
调度域层级，算力值仅有一个，该层级中SD_ASYM_CPUCAPACITY未被设置，它描述的是一个SMP区域，也
应该被以此处理。

因此，“典型的”保护非对称CPU算力代码路径的代码模式是：

- 检查sched_asym_cpucapacity静态键
- 如果它被使能，接着检查调度域层级中SD_ASYM_CPUCAPACITY标志位是否出现

5. 算力感知调度的实现
=====================

5.1 CFS
-------

5.1.1 算力适应性（fitness）
~~~~~~~~~~~~~~~~~~~~~~~~~~~

CFS最主要的算力调度准则是::

  task_util(p) < capacity(task_cpu(p))

它通常被称为算力适应性准则。也就是说，CFS必须保证任务“适合”在某个CPU上运行。如果准则被违反，
任务将要更长地消耗该CPU，任务是CPU受限的（CPU-bound）。

此外，uclamp允许用户空间指定任务的最小和最大利用率，要么以sched_setattr()的方式，要么以
cgroup接口的方式（参阅Documentation/admin-guide/cgroup-v2.rst）。如其名字所暗示，uclamp
可以被用在前一条准则中限制task_util()。

5.1.2 被唤醒任务的CPU选择
~~~~~~~~~~~~~~~~~~~~~~~~~

CFS任务唤醒的CPU选择，遵循上面描述的算力适应性准则。在此之上，uclamp被用来限制任务利用率，
这令用户空间对CFS任务的CPU选择有更多的控制。也就是说，CFS被唤醒任务的CPU选择，搜索满足以下
条件的CPU::

  clamp(task_util(p), task_uclamp_min(p), task_uclamp_max(p)) < capacity(cpu)

通过使用uclamp，举例来说，用户空间可以允许忙等待循环（100%使用率）在任意CPU上运行，只要给
它设置低的uclamp.max值。相反，uclamp能强制一个小的周期性任务（比如，10%利用率）在最高性能
的CPU上运行，只要给它设置高的uclamp.min值。

.. note::

  CFS的被唤醒的任务的CPU选择，可被能耗感知调度（Energy Aware Scheduling，EAS）覆盖，在
  Documentation/scheduler/sched-energy.rst中描述。

5.1.3 负载均衡
~~~~~~~~~~~~~~

被唤醒任务的CPU选择的一个病理性的例子是，任务几乎不睡眠，那么也几乎不发生唤醒。考虑::

  w == wakeup event

  capacity(CPU0) = C
  capacity(CPU1) = C / 3

                           workload on CPU0
  CPU work ^
           |     _________           _________           ____
           |    |         |         |         |         |
           +----+----+----+----+----+----+----+----+----+----+-> time
                w                   w                   w

                           workload on CPU1
  CPU work ^
           |     ____________________________________________
           |    |
           +----+----+----+----+----+----+----+----+----+----+->
                w

该工作负载应该在CPU0上运行，不过如果任务满足以下条件之一：

- 一开始发生不合适的调度（不准确的初始利用率估计）
- 一开始调度正确，但突然需要更多的处理器功率

则任务可能变为CPU受限的，也就是说 ``task_util(p) > capacity(task_cpu(p))`` ；CPU算力
调度准则被违反，将不会有任何唤醒事件来修复这个错误的CPU选择。

这种场景下的任务被称为“不合适的”（misfit）任务，处理这个场景的机制同样也以此命名。Misfit
任务迁移借助CFS负载均衡器，更明确的说，是主动负载均衡的部分（用来迁移正在运行的任务）。
当发生负载均衡时，如果一个misfit任务可以被迁移到一个相较当前运行的CPU具有更高算力的CPU上，
那么misfit任务的主动负载均衡将被触发。

5.2 实时调度
------------

5.2.1 被唤醒任务的CPU选择
~~~~~~~~~~~~~~~~~~~~~~~~~

实时任务唤醒时的CPU选择，搜索满足以下条件的CPU::

  task_uclamp_min(p) <= capacity(task_cpu(cpu))

同时仍然允许接着使用常规的优先级限制。如果没有CPU能满足这个算力准则，那么将使用基于严格
优先级的调度，CPU算力将被忽略。

5.3 最后期限调度
----------------

5.3.1 被唤醒任务的CPU选择
~~~~~~~~~~~~~~~~~~~~~~~~~

最后期限任务唤醒时的CPU选择，搜索满足以下条件的CPU::

  task_bandwidth(p) < capacity(task_cpu(p))

同时仍然允许接着使用常规的带宽和截止期限限制。如果没有CPU能满足这个算力准则，那么任务依然
在当前CPU队列中。

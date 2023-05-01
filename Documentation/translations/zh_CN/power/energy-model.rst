.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/power/energy-model.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

============
设备能量模型
============

1. 概述
-------

能量模型（EM）框架是一种驱动程序与内核子系统之间的接口。其中驱动程序了解不同
性能层级的设备所消耗的功率，而内核子系统愿意使用该信息做出能量感知决策。

设备所消耗的功率的信息来源在不同的平台上可能有很大的不同。这些功率成本在某些
情况下可以使用设备树数据来估算。在其它情况下，固件会更清楚。或者，用户空间可能
是最清楚的。以此类推。为了避免每一个客户端子系统对每一种可能的信息源自己重新
实现支持，EM框架作为一个抽象层介入，它在内核中对功率成本表的格式进行标准化，
因此能够避免多余的工作。

功率值可以用微瓦或“抽象刻度”表示。多个子系统可能使用EM，由系统集成商来检查
功率值刻度类型的要求是否满足。可以在能量感知调度器的文档中找到一个例子
Documentation/scheduler/sched-energy.rst。对于一些子系统，比如热能或
powercap，用“抽象刻度”描述功率值可能会导致问题。这些子系统对过去使用的功率的
估算值更感兴趣，因此可能需要真实的微瓦。这些要求的一个例子可以在智能功率分配
Documentation/driver-api/thermal/power_allocator.rst文档中找到。

内核子系统可能（基于EM内部标志位）实现了对EM注册设备是否具有不一致刻度的自动
检查。要记住的重要事情是，当功率值以“抽象刻度”表示时，从中推导以微焦耳为单位
的真实能量消耗是不可能的。

下图描述了一个驱动的例子（这里是针对Arm的，但该方法适用于任何体系结构），它
向EM框架提供了功率成本，感兴趣的客户端可从中读取数据::

       +---------------+  +-----------------+  +---------------+
       | Thermal (IPA) |  | Scheduler (EAS) |  |     Other     |
       +---------------+  +-----------------+  +---------------+
               |                   | em_cpu_energy()   |
               |                   | em_cpu_get()      |
               +---------+         |         +---------+
                         |         |         |
                         v         v         v
                        +---------------------+
                        |    Energy Model     |
                        |     Framework       |
                        +---------------------+
                           ^       ^       ^
                           |       |       | em_dev_register_perf_domain()
                +----------+       |       +---------+
                |                  |                 |
        +---------------+  +---------------+  +--------------+
        |  cpufreq-dt   |  |   arm_scmi    |  |    Other     |
        +---------------+  +---------------+  +--------------+
                ^                  ^                 ^
                |                  |                 |
        +--------------+   +---------------+  +--------------+
        | Device Tree  |   |   Firmware    |  |      ?       |
        +--------------+   +---------------+  +--------------+

对于CPU设备，EM框架管理着系统中每个“性能域”的功率成本表。一个性能域是一组
性能一起伸缩的CPU。性能域通常与CPUFreq策略具有1对1映射。一个性能域中的
所有CPU要求具有相同的微架构。不同性能域中的CPU可以有不同的微架构。


2. 核心API
----------

2.1 配置选项
^^^^^^^^^^^^

必须使能CONFIG_ENERGY_MODEL才能使用EM框架。


2.2 性能域的注册
^^^^^^^^^^^^^^^^

“高级”EM的注册
~~~~~~~~~~~~~~~~

“高级”EM因它允许驱动提供更精确的功率模型而得名。它并不受限于框架中的一些已
实现的数学公式（就像“简单”EM那样）。它可以更好地反映每个性能状态的实际功率
测量。因此，在EM静态功率（漏电流功率）是重要的情况下，应该首选这种注册方式。

驱动程序应通过以下API将性能域注册到EM框架中::

  int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
		struct em_data_callback *cb, cpumask_t *cpus, bool microwatts);

驱动程序必须提供一个回调函数，为每个性能状态返回<频率,功率>元组。驱动程序
提供的回调函数可以自由地从任何相关位置（DT、固件......）以及以任何被认为是
必要的方式获取数据。只有对于CPU设备，驱动程序必须使用cpumask指定性能域的CPU。
对于CPU以外的其他设备，最后一个参数必须被设置为NULL。

最后一个参数“microwatts”（微瓦）设置成正确的值是很重要的，使用EM的内核
子系统可能会依赖这个标志来检查所有的EM设备是否使用相同的刻度。如果有不同的
刻度，这些子系统可能决定返回警告/错误，停止工作或崩溃（panic）。

关于实现这个回调函数的驱动程序的例子，参见第3节。或者在第2.4节阅读这个API
的更多文档。

使用DT的EM注册
==============

EM也可以使用OPP框架和DT "操作点-v2 "中的信息注册。DT中的每个OPP条目都可
以用一个包含微瓦特功率值的属性 "op-microwatt "来扩展。这个OPP DT属性允
许平台注册反映总功率（静态+动态）的EM功率值。这些功率值可能直接来自实验和
测量。

“人工”EM的注册
==============

有一个选项可以为缺少关于每个性能状态的功率值的详细知识的驱动程序提供一个自
定义回调。回调.get_cost()是可选的，它提供EAS使用的“成本”值。这对那些只提
供CPU类型之间相对效率信息的平台很有用，人们可以利用这些信息来创建一个抽象的
功率模型。但是，考虑到输入功率值的大小限制，即使是抽象的功率模型有时也很难装
进去。.get_cost()允许提供反映CPU效率的“成本”值。这将允许提供EAS信息，它
与EM内部计算'成本'值的公式有不同的关系。要为这样的平台注册EM，驱动程序必须
将标志“microwatts”设置为0，提供.get_power()回调和.get_cost()回调。EM
框架会在注册过程中正确处理这样的平台。这种平台会被设置EM_PERF_DOMAIN_ARTIFICIAL
标志。其他使用EM的框架应该特别注意测试和正确对待这个标志。

“简单”EM的注册
~~~~~~~~~~~~~~~~

“简单”EM是用框架的辅助函数cpufreq_register_em_with_opp()注册的。它实现了
一个和以下数学公式紧密相关的功率模型::

	Power = C * V^2 * f

使用这种方法注册的EM可能无法正确反映真实设备的物理特性，例如当静态功率
（漏电流功率）很重要时。


2.3 访问性能域
^^^^^^^^^^^^^^

有两个API函数提供对能量模型的访问。em_cpu_get()以CPU id为参数，em_pd_get()
以设备指针为参数。使用哪个接口取决于子系统，但对于CPU设备来说，这两个函数都返
回相同的性能域。

对CPU的能量模型感兴趣的子系统可以通过em_cpu_get() API检索它。在创建性能域时
分配一次能量模型表，它保存在内存中不被修改。

一个性能域所消耗的能量可以使用em_cpu_energy() API来估算。该估算假定CPU设备
使用的CPUfreq监管器是schedutil。当前该计算不能提供给其它类型的设备。

关于上述API的更多细节可以在 ``<linux/energy_model.h>`` 或第2.4节中找到。


2.4 API的细节描述
^^^^^^^^^^^^^^^^^
参见 include/linux/energy_model.h 和 kernel/power/energy_model.c 的kernel doc。

3. 驱动示例
-----------

CPUFreq框架支持专用的回调函数，用于为指定的CPU（们）注册EM：
cpufreq_driver::register_em()。这个回调必须为每个特定的驱动程序正确实现，
因为框架会在设置过程中适时地调用它。本节提供了一个简单的例子，展示CPUFreq驱动
在能量模型框架中使用（假的）“foo”协议注册性能域。该驱动实现了一个est_power()
函数提供给EM框架::

  -> drivers/cpufreq/foo_cpufreq.c

  01   static int est_power(struct device *dev, unsigned long *mW,
  02                   unsigned long *KHz)
  03	{
  04		long freq, power;
  05
  06		/* 使用“foo”协议设置频率上限 */
  07		freq = foo_get_freq_ceil(dev, *KHz);
  08		if (freq < 0);
  09			return freq;
  10
  11		/* 估算相关频率下设备的功率成本 */
  12		power = foo_estimate_power(dev, freq);
  13		if (power < 0);
  14			return power;
  15
  16		/* 将这些值返回给EM框架 */
  17		*mW = power;
  18		*KHz = freq;
  19
  20		return 0;
  21	}
  22
  23	static void foo_cpufreq_register_em(struct cpufreq_policy *policy)
  24	{
  25		struct em_data_callback em_cb = EM_DATA_CB(est_power);
  26		struct device *cpu_dev;
  27		int nr_opp;
  28
  29		cpu_dev = get_cpu_device(cpumask_first(policy->cpus));
  30
  31     	/* 查找该策略支持的OPP数量 */
  32     	nr_opp = foo_get_nr_opp(policy);
  33
  34     	/* 并注册新的性能域 */
  35     	em_dev_register_perf_domain(cpu_dev, nr_opp, &em_cb, policy->cpus,
  36					    true);
  37	}
  38
  39	static struct cpufreq_driver foo_cpufreq_driver = {
  40		.register_em = foo_cpufreq_register_em,
  41	};

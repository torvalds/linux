.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/power/opp.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

======================
操作性能值（OPP）库
======================

(C) 2009-2010 Nishanth Menon <nm@ti.com>, 德州仪器公司

.. 目录

  1. 简介
  2. OPP链表初始注册
  3. OPP搜索函数
  4. OPP可用性控制函数
  5. OPP数据检索函数
  6. 数据结构

1. 简介
=======

1.1 何为操作性能值（OPP）？
------------------------------

当今复杂的单片系统（SoC）由多个子模块组成，这些子模块会联合工作。在一个执行不同用例
的操作系统中，并不是SoC中的所有模块都需要一直以最高频率工作。为了促成这一点，SoC中
的子模块被分组为不同域，允许一些域以较低的电压和频率运行，而其它域则以较高的“电压/
频率对”运行。

设备按域支持的由频率电压对组成的离散的元组的集合，被称为操作性能值（组），或OPPs。

举例来说：

让我们考虑一个支持下述频率、电压值的内存保护单元（MPU）设备：
{300MHz，最低电压为1V}, {800MHz，最低电压为1.2V}, {1GHz，最低电压为1.3V}

我们能将它们表示为3个OPP，如下述{Hz, uV}元组（译注：频率的单位是赫兹，电压的单位是
微伏）。

- {300000000, 1000000}
- {800000000, 1200000}
- {1000000000, 1300000}

1.2 操作性能值库
----------------

OPP库提供了一组辅助函数来组织和查询OPP信息。该库位于drivers/opp/目录下，其头文件
位于include/linux/pm_opp.h中。OPP库可以通过开启CONFIG_PM_OPP来启用。某些SoC，
如德州仪器的OMAP框架允许在不需要cpufreq的情况下可选地在某一OPP下启动。

OPP库的典型用法如下::

 （用户）        -> 注册一个默认的OPP集合               -> （库）
 （SoC框架）     -> 在必要的情况下，对某些OPP进行修改     -> OPP layer
                -> 搜索/检索信息的查询                 ->

OPP层期望每个域由一个唯一的设备指针来表示。SoC框架在OPP层为每个设备注册了一组初始
OPP。这个链表的长度被期望是一个最优化的小数字，通常每个设备大约5个。初始链表包含了
一个OPP集合，这个集合被期望能在系统中安全使能。

关于OPP可用性的说明
^^^^^^^^^^^^^^^^^^^

随着系统的运行，SoC框架可能会基于各种外部因素选择让某些OPP在每个设备上可用或不可用，
示例：温度管理或其它异常场景中，SoC框架可能会选择禁用一个较高频率的OPP以安全地继续
运行，直到该OPP被重新启用（如果可能）。

OPP库在它的实现中达成了这个概念。以下操作函数只能对可用的OPP使用：
dev_pm_opp_find_freq_{ceil, floor}, dev_pm_opp_get_voltage,
dev_pm_opp_get_freq, dev_pm_opp_get_opp_count。

dev_pm_opp_find_freq_exact是用来查找OPP指针的，该指针可被用在dev_pm_opp_enable/
disable函数，使一个OPP在被需要时变为可用。

警告：如果对一个设备调用dev_pm_opp_enable/disable函数，OPP库的用户应该使用
dev_pm_opp_get_opp_count来刷新OPP的可用性计数。触发这些的具体机制，或者对有依赖的
子系统（比如cpufreq）的通知机制，都是由使用OPP库的SoC特定框架酌情处理的。在这些操作
中，同样需要注意刷新cpufreq表。

2. OPP链表初始注册
==================
SoC的实现会迭代调用dev_pm_opp_add函数来增加每个设备的OPP。预期SoC框架将以最优的
方式注册OPP条目 - 典型的数字范围小于5。通过注册OPP生成的OPP链表，在整个设备运行过程
中由OPP库维护。SoC框架随后可以使用dev_pm_opp_enable / disable函数动态地
控制OPP的可用性。

dev_pm_opp_add
	为设备指针所指向的特定域添加一个新的OPP。OPP是用频率和电压定义的。一旦完成
	添加，OPP被认为是可用的，可以用dev_pm_opp_enable/disable函数来控制其可用性。
	OPP库内部用dev_pm_opp结构体存储并管理这些信息。这个函数可以被SoC框架根据SoC
	的使用环境的需求来定义一个最优链表。

	警告：
		不要在中断上下文使用这个函数。

	示例::

	 soc_pm_init()
	 {
		/* 做一些事情 */
		r = dev_pm_opp_add(mpu_dev, 1000000, 900000);
		if (!r) {
			pr_err("%s: unable to register mpu opp(%d)\n", r);
			goto no_cpufreq;
		}
		/* 做一些和cpufreq相关的事情 */
	 no_cpufreq:
		/* 做剩余的事情 */
	 }

3. OPP搜索函数
==============
cpufreq等高层框架对频率进行操作，为了将频率映射到相应的OPP，OPP库提供了便利的函数
来搜索OPP库内部管理的OPP链表。这些搜索函数如果找到匹配的OPP，将返回指向该OPP的指针，
否则返回错误。这些错误预计由标准的错误检查，如IS_ERR()来处理，并由调用者采取适当的
行动。

这些函数的调用者应在使用完OPP后调用dev_pm_opp_put()。否则，OPP的内存将永远不会
被释放，并导致内存泄露。

dev_pm_opp_find_freq_exact
	根据 *精确的* 频率和可用性来搜索OPP。这个函数对默认不可用的OPP特别有用。
	例子：在SoC框架检测到更高频率可用的情况下，它可以使用这个函数在调用
	dev_pm_opp_enable之前找到OPP::

	 opp = dev_pm_opp_find_freq_exact(dev, 1000000000, false);
	 dev_pm_opp_put(opp);
	 /* 不要操作指针.. 只是做有效性检查.. */
	 if (IS_ERR(opp)) {
		pr_err("frequency not disabled!\n");
		/* 触发合适的操作.. */
	 } else {
		dev_pm_opp_enable(dev,1000000000);
	 }

	注意：
	  这是唯一一个可以搜索不可用OPP的函数。

dev_pm_opp_find_freq_floor
	搜索一个 *最多* 提供指定频率的可用OPP。这个函数在搜索较小的匹配或按频率
	递减的顺序操作OPP信息时很有用。
	例子：要找的一个设备的最高OPP::

	 freq = ULONG_MAX;
	 opp = dev_pm_opp_find_freq_floor(dev, &freq);
	 dev_pm_opp_put(opp);

dev_pm_opp_find_freq_ceil
	搜索一个 *最少* 提供指定频率的可用OPP。这个函数在搜索较大的匹配或按频率
	递增的顺序操作OPP信息时很有用。
	例1：找到一个设备最小的OPP::

	 freq = 0;
	 opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	 dev_pm_opp_put(opp);

	例: 一个SoC的cpufreq_driver->target的简易实现::

	 soc_cpufreq_target(..)
	 {
		/* 做策略检查等操作 */
		/* 找到和请求最接近的频率 */
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		dev_pm_opp_put(opp);
		if (!IS_ERR(opp))
			soc_switch_to_freq_voltage(freq);
		else
			/* 当不能满足请求时，要做的事 */
		/* 做其它事 */
	 }

4. OPP可用性控制函数
====================
在OPP库中注册的默认OPP链表也许无法满足所有可能的场景。OPP库提供了一套函数来修改
OPP链表中的某个OPP的可用性。这使得SoC框架能够精细地动态控制哪一组OPP是可用于操作
的。设计这些函数的目的是在诸如考虑温度时 *暂时地* 删除某个OPP（例如，在温度下降
之前不要使用某OPP）。

警告：
	不要在中断上下文使用这些函数。

dev_pm_opp_enable
	使一个OPP可用于操作。
	例子：假设1GHz的OPP只有在SoC温度低于某个阈值时才可用。SoC框架的实现可能
	会选择做以下事情::

	 if (cur_temp < temp_low_thresh) {
		/* 若1GHz未使能，则使能 */
		opp = dev_pm_opp_find_freq_exact(dev, 1000000000, false);
		dev_pm_opp_put(opp);
		/* 仅仅是错误检查 */
		if (!IS_ERR(opp))
			ret = dev_pm_opp_enable(dev, 1000000000);
		else
			goto try_something_else;
	 }

dev_pm_opp_disable
	使一个OPP不可用于操作。
	例子：假设1GHz的OPP只有在SoC温度高于某个阈值时才可用。SoC框架的实现可能
	会选择做以下事情::

	 if (cur_temp > temp_high_thresh) {
		/* 若1GHz已使能，则关闭 */
		opp = dev_pm_opp_find_freq_exact(dev, 1000000000, true);
		dev_pm_opp_put(opp);
		/* 仅仅是错误检查 */
		if (!IS_ERR(opp))
			ret = dev_pm_opp_disable(dev, 1000000000);
		else
			goto try_something_else;
	 }

5. OPP数据检索函数
==================
由于OPP库对OPP信息进行了抽象化处理，因此需要一组函数来从dev_pm_opp结构体中提取
信息。一旦使用搜索函数检索到一个OPP指针，以下函数就可以被SoC框架用来检索OPP层
内部描述的信息。

dev_pm_opp_get_voltage
	检索OPP指针描述的电压。
	例子: 当cpufreq切换到到不同频率时，SoC框架需要用稳压器框架将OPP描述
	的电压设置到提供电压的电源管理芯片中::

	 soc_switch_to_freq_voltage(freq)
	 {
		/* 做一些事情 */
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		v = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);
		if (v)
			regulator_set_voltage(.., v);
		/* 做其它事 */
	 }

dev_pm_opp_get_freq
	检索OPP指针描述的频率。
	例子：比方说，SoC框架使用了几个辅助函数，通过这些函数，我们可以将OPP
	指针传入，而不是传入额外的参数，用来处理一系列数据参数::

	 soc_cpufreq_target(..)
	 {
		/* 做一些事情.. */
		 max_freq = ULONG_MAX;
		 max_opp = dev_pm_opp_find_freq_floor(dev,&max_freq);
		 requested_opp = dev_pm_opp_find_freq_ceil(dev,&freq);
		 if (!IS_ERR(max_opp) && !IS_ERR(requested_opp))
			r = soc_test_validity(max_opp, requested_opp);
		 dev_pm_opp_put(max_opp);
		 dev_pm_opp_put(requested_opp);
		/* 做其它事 */
	 }
	 soc_test_validity(..)
	 {
		 if(dev_pm_opp_get_voltage(max_opp) < dev_pm_opp_get_voltage(requested_opp))
			 return -EINVAL;
		 if(dev_pm_opp_get_freq(max_opp) < dev_pm_opp_get_freq(requested_opp))
			 return -EINVAL;
		/* 做一些事情.. */
	 }

dev_pm_opp_get_opp_count
	检索某个设备可用的OPP数量。
	例子：假设SoC中的一个协处理器需要知道某个表中的可用频率，主处理器可以
	按如下方式发出通知::

	 soc_notify_coproc_available_frequencies()
	 {
		/* 做一些事情 */
		num_available = dev_pm_opp_get_opp_count(dev);
		speeds = kcalloc(num_available, sizeof(u32), GFP_KERNEL);
		/* 按升序填充表 */
		freq = 0;
		while (!IS_ERR(opp = dev_pm_opp_find_freq_ceil(dev, &freq))) {
			speeds[i] = freq;
			freq++;
			i++;
			dev_pm_opp_put(opp);
		}

		soc_notify_coproc(AVAILABLE_FREQs, speeds, num_available);
		/* 做其它事 */
	 }

6. 数据结构
===========
通常，一个SoC包含多个可变电压域。每个域由一个设备指针描述。和OPP之间的关系可以
按以下方式描述::

  SoC
   |- device 1
   |	|- opp 1 (availability, freq, voltage)
   |	|- opp 2 ..
   ...	...
   |	`- opp n ..
   |- device 2
   ...
   `- device m

OPP库维护着一个内部链表，SoC框架使用上文描述的各个函数来填充和访问。然而，描述
真实OPP和域的结构体是OPP库自身的内部组成，以允许合适的抽象在不同系统中得到复用。

struct dev_pm_opp
	OPP库的内部数据结构，用于表示一个OPP。除了频率、电压、可用性信息外，
	它还包含OPP库运行所需的内部统计信息。指向这个结构体的指针被提供给
	用户（比如SoC框架）使用，在与OPP层的交互中作为OPP的标识符。

	警告：
	  结构体dev_pm_opp的指针不应该由用户解析或修改。一个实例的默认值由
	  dev_pm_opp_add填充，但OPP的可用性由dev_pm_opp_enable/disable函数
	  修改。

struct device
	这用于向OPP层标识一个域。设备的性质和它的实现是由OPP库的用户决定的，
	如SoC框架。

总体来说，以一个简化的视角看，对数据结构的操作可以描述为下面各图::

  初始化 / 修改:
              +-----+        /- dev_pm_opp_enable
  dev_pm_opp_add --> | opp | <-------
    |         +-----+        \- dev_pm_opp_disable
    \-------> domain_info(device)

  搜索函数:
               /-- dev_pm_opp_find_freq_ceil  ---\   +-----+
  domain_info<---- dev_pm_opp_find_freq_exact -----> | opp |
               \-- dev_pm_opp_find_freq_floor ---/   +-----+

  检索函数:
  +-----+     /- dev_pm_opp_get_voltage
  | opp | <---
  +-----+     \- dev_pm_opp_get_freq

  domain_info <- dev_pm_opp_get_opp_count

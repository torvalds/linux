.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../cpu-freq/core`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_core.rst:


====================================
CPUFreq核心和CPUFreq通知器的通用说明
====================================

作者:
	- Dominik Brodowski  <linux@brodo.de>
	- David Kimdon <dwhedon@debian.org>
	- Rafael J. Wysocki <rafael.j.wysocki@intel.com>
	- Viresh Kumar <viresh.kumar@linaro.org>

.. 目录:

   1.  CPUFreq核心和接口
   2.  CPUFreq通知器
   3.  含有Operating Performance Point (OPP)的CPUFreq表的生成

1. CPUFreq核心和接口
======================

cpufreq核心代码位于drivers/cpufreq/cpufreq.c中。这些cpufreq代码为CPUFreq架构的驱
动程序（那些操作硬件切换频率的代码）以及 "通知器 "提供了一个标准化的接口。
这些是设备驱动程序或需要了解策略变化的其它内核部分（如 ACPI 热量管理）或所有频率更改（除
计时代码外），甚至需要强制确定速度限制的通知器（如 ARM 架构上的 LCD 驱动程序）。
此外， 内核 "常数" loops_per_jiffy会根据频率变化而更新。

cpufreq策略的引用计数由 cpufreq_cpu_get 和 cpufreq_cpu_put 来完成，以确保 cpufreq 驱
动程序被正确地注册到核心中，并且驱动程序在 cpufreq_put_cpu 被调用之前不会被卸载。这也保证
了每个CPU核的cpufreq 策略在使用期间不会被释放。

2. CPUFreq 通知器
====================

CPUFreq通知器符合标准的内核通知器接口。
关于通知器的细节请参阅 linux/include/linux/notifier.h。

这里有两个不同的CPUfreq通知器 - 策略通知器和转换通知器。


2.1 CPUFreq策略通知器
----------------------------

当创建或移除策略时，这些都会被通知。

阶段是在通知器的第二个参数中指定的。当第一次创建策略时，阶段是CPUFREQ_CREATE_POLICY，当
策略被移除时，阶段是CPUFREQ_REMOVE_POLICY。

第三个参数 ``void *pointer`` 指向一个结构体cpufreq_policy，其包括min，max(新策略的下限和
上限（单位为kHz）)这几个值。


2.2 CPUFreq转换通知器
--------------------------------

当CPUfreq驱动切换CPU核心频率时，策略中的每个在线CPU都会收到两次通知，这些变化没有任何外部干
预。

第二个参数指定阶段 - CPUFREQ_PRECHANGE or CPUFREQ_POSTCHANGE.

第三个参数是一个包含如下值的结构体cpufreq_freqs：

=====	====================
cpu	受影响cpu的编号
old	旧频率
new	新频率
flags	cpufreq驱动的标志
=====	====================

3. 含有Operating Performance Point (OPP)的CPUFreq表的生成
==================================================================
关于OPP的细节请参阅 Documentation/power/opp.rst

dev_pm_opp_init_cpufreq_table -
	这个功能提供了一个随时可用的转换程序，用来将OPP层关于可用频率的内部信息翻译成一种容易提供给
	cpufreq的格式。

	.. Warning::

		不要在中断上下文中使用此函数。

	例如::

	 soc_pm_init()
	 {
		/* Do things */
		r = dev_pm_opp_init_cpufreq_table(dev, &freq_table);
		if (!r)
			policy->freq_table = freq_table;
		/* Do other things */
	 }

	.. note::

		该函数只有在CONFIG_PM_OPP之外还启用了CONFIG_CPU_FREQ时才可用。

dev_pm_opp_free_cpufreq_table
	释放dev_pm_opp_init_cpufreq_table分配的表。

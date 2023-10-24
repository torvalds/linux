.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../cpu-freq/core`
:Translator: Yanteng Si <siyanteng@loongson.cn>
             Hu Haowen <src.res.211@gmail.com>

.. _tw_core.rst:


====================================
CPUFreq核心和CPUFreq通知器的通用說明
====================================

作者:
	- Dominik Brodowski  <linux@brodo.de>
	- David Kimdon <dwhedon@debian.org>
	- Rafael J. Wysocki <rafael.j.wysocki@intel.com>
	- Viresh Kumar <viresh.kumar@linaro.org>

.. 目錄:

   1.  CPUFreq核心和接口
   2.  CPUFreq通知器
   3.  含有Operating Performance Point (OPP)的CPUFreq表的生成

1. CPUFreq核心和接口
======================

cpufreq核心代碼位於drivers/cpufreq/cpufreq.c中。這些cpufreq代碼爲CPUFreq架構的驅
動程序（那些操作硬體切換頻率的代碼）以及 "通知器 "提供了一個標準化的接口。
這些是設備驅動程序或需要了解策略變化的其它內核部分（如 ACPI 熱量管理）或所有頻率更改（除
計時代碼外），甚至需要強制確定速度限制的通知器（如 ARM 架構上的 LCD 驅動程序）。
此外， 內核 "常數" loops_per_jiffy會根據頻率變化而更新。

cpufreq策略的引用計數由 cpufreq_cpu_get 和 cpufreq_cpu_put 來完成，以確保 cpufreq 驅
動程序被正確地註冊到核心中，並且驅動程序在 cpufreq_put_cpu 被調用之前不會被卸載。這也保證
了每個CPU核的cpufreq 策略在使用期間不會被釋放。

2. CPUFreq 通知器
====================

CPUFreq通知器符合標準的內核通知器接口。
關於通知器的細節請參閱 linux/include/linux/notifier.h。

這裡有兩個不同的CPUfreq通知器 - 策略通知器和轉換通知器。


2.1 CPUFreq策略通知器
----------------------------

當創建或移除策略時，這些都會被通知。

階段是在通知器的第二個參數中指定的。當第一次創建策略時，階段是CPUFREQ_CREATE_POLICY，當
策略被移除時，階段是CPUFREQ_REMOVE_POLICY。

第三個參數 ``void *pointer`` 指向一個結構體cpufreq_policy，其包括min，max(新策略的下限和
上限（單位爲kHz）)這幾個值。


2.2 CPUFreq轉換通知器
--------------------------------

當CPUfreq驅動切換CPU核心頻率時，策略中的每個在線CPU都會收到兩次通知，這些變化沒有任何外部干
預。

第二個參數指定階段 - CPUFREQ_PRECHANGE or CPUFREQ_POSTCHANGE.

第三個參數是一個包含如下值的結構體cpufreq_freqs：

=====	====================
cpu	受影響cpu的編號
old	舊頻率
new	新頻率
flags	cpufreq驅動的標誌
=====	====================

3. 含有Operating Performance Point (OPP)的CPUFreq表的生成
==================================================================
關於OPP的細節請參閱 Documentation/power/opp.rst

dev_pm_opp_init_cpufreq_table -
	這個功能提供了一個隨時可用的轉換程序，用來將OPP層關於可用頻率的內部信息翻譯成一種容易提供給
	cpufreq的格式。

	.. Warning::

		不要在中斷上下文中使用此函數。

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

		該函數只有在CONFIG_PM_OPP之外還啓用了CONFIG_CPU_FREQ時才可用。

dev_pm_opp_free_cpufreq_table
	釋放dev_pm_opp_init_cpufreq_table分配的表。


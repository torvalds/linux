.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../cpu-freq/cpufreq-stats`
:Translator: Yanteng Si <siyanteng@loongson.cn>
             Hu Haowen <src.res.211@gmail.com>

.. _tw_cpufreq-stats.rst:


==========================================
sysfs CPUFreq Stats的一般說明
==========================================

用戶信息


作者: Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>

.. Contents

   1. 簡介
   2. 提供的統計數據(舉例說明)
   3. 配置cpufreq-stats


1. 簡介
===============

cpufreq-stats是一個爲每個CPU提供CPU頻率統計的驅動。
這些統計數據在/sysfs中以一堆只讀接口的形式提供。這個接口（在配置好後）將出現在
/sysfs（<sysfs root>/devices/system/cpu/cpuX/cpufreq/stats/）中cpufreq下的一個單
獨的目錄中，提供給每個CPU。
各種統計數據將在此目錄下形成只讀文件。

此驅動是獨立於任何可能運行在你所用CPU上的特定cpufreq_driver而設計的。因此，它將與所有
cpufreq_driver一起工作。


2. 提供的統計數據(舉例說明)
=====================================

cpufreq stats提供了以下統計數據（在下面詳細解釋）。

-  time_in_state
-  total_trans
-  trans_table

所有的統計數據將從統計驅動被載入的時間（或統計被重置的時間）開始，到某一統計數據被讀取的時間爲止。
顯然，統計驅動不會有任何關於統計驅動載入之前的頻率轉換信息。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # ls -l
    total 0
    drwxr-xr-x  2 root root    0 May 14 16:06 .
    drwxr-xr-x  3 root root    0 May 14 15:58 ..
    --w-------  1 root root 4096 May 14 16:06 reset
    -r--r--r--  1 root root 4096 May 14 16:06 time_in_state
    -r--r--r--  1 root root 4096 May 14 16:06 total_trans
    -r--r--r--  1 root root 4096 May 14 16:06 trans_table

- **reset**

只寫屬性，可用於重置統計計數器。這對於評估不同調節器下的系統行爲非常有用，且無需重啓。


- **time_in_state**

此項給出了這個CPU所支持的每個頻率所花費的時間。cat輸出的每一行都會有"<frequency>
<time>"對，表示這個CPU在<frequency>上花費了<time>個usertime單位的時間。這裡的
usertime單位是10mS（類似於/proc中輸出的其他時間）。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat time_in_state
    3600000 2089
    3400000 136
    3200000 34
    3000000 67
    2800000 172488


- **total_trans**

給出了這個CPU上頻率轉換的總次數。cat的輸出將有一個單一的計數，這就是頻率轉換的總數。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat total_trans
    20

- **trans_table**

這將提供所有CPU頻率轉換的細粒度信息。這裡的cat輸出是一個二維矩陣，其中一個條目<i, j>（第
i行，第j列）代表從Freq_i到Freq_j的轉換次數。Freq_i行和Freq_j列遵循驅動最初提供給cpufreq
核的頻率表的排序順序，因此可以排序（升序或降序）或不排序。 這裡的輸出也包含了每行每列的實際
頻率值，以便更好地閱讀。

如果轉換表大於PAGE_SIZE，讀取時將返回一個-EFBIG錯誤。

::

    <mysystem>:/sys/devices/system/cpu/cpu0/cpufreq/stats # cat trans_table
    From  :    To
	    :   3600000   3400000   3200000   3000000   2800000
    3600000:         0         5         0         0         0
    3400000:         4         0         2         0         0
    3200000:         0         1         0         2         0
    3000000:         0         0         1         0         3
    2800000:         0         0         0         2         0

3. 配置cpufreq-stats
============================

要在你的內核中配置cpufreq-stats::

	Config Main Menu
		Power management options (ACPI, APM)  --->
			CPU Frequency scaling  --->
				[*] CPU Frequency scaling
				[*]   CPU frequency translation statistics


"CPU Frequency scaling" (CONFIG_CPU_FREQ) 應該被啓用以配置cpufreq-stats。

"CPU frequency translation statistics" (CONFIG_CPU_FREQ_STAT)提供了包括
time_in_state、total_trans和trans_table的統計數據。

一旦啓用了這個選項，並且你的CPU支持cpufrequency，你就可以在/sysfs中看到CPU頻率統計。


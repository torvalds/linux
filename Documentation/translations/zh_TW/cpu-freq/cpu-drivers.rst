.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/cpu-freq/cpu-drivers.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

:校譯:

 唐藝舟 Tang Yizhou <tangyeechou@gmail.com>

=======================================
如何實現一個新的CPUFreq處理器驅動程序？
=======================================

作者:


	- Dominik Brodowski  <linux@brodo.de>
	- Rafael J. Wysocki <rafael.j.wysocki@intel.com>
	- Viresh Kumar <viresh.kumar@linaro.org>

.. Contents

   1.   怎麼做？
   1.1  初始化
   1.2  Per-CPU 初始化
   1.3  驗證
   1.4  target/target_index 或 setpolicy?
   1.5  target/target_index
   1.6  setpolicy
   1.7  get_intermediate 與 target_intermediate
   2.   頻率表助手



1. 怎麼做？
===========

如果，你剛剛得到了一個全新的CPU/芯片組及其數據手冊，並希望爲這個CPU/芯片組添加cpufreq
支持？很好，這裏有一些至關重要的提示：


1.1 初始化
----------

首先，在 __initcall level 7 (module_init())或更靠後的函數中檢查這個內核是否
運行在正確的CPU和正確的芯片組上。如果是，則使用cpufreq_register_driver()向
CPUfreq核心層註冊一個cpufreq_driver結構體。

結構體cpufreq_driver應該包含什麼成員?

 .name - 驅動的名字。

 .init - 一個指向per-policy初始化函數的指針。

 .verify - 一個指向"verification"函數的指針。

 .setpolicy 或 .fast_switch 或 .target 或 .target_index - 差異見
 下文。

其它可選成員

 .flags - 給cpufreq核心的提示。

 .driver_data - cpufreq驅動程序的特有數據。

 .get_intermediate 和 target_intermediate - 用於在改變CPU頻率時切換到穩定
 的頻率。

 .get - 返回CPU的當前頻率。

 .bios_limit - 返回HW/BIOS對CPU的最大頻率限制值。

 .exit - 一個指向per-policy清理函數的指針，該函數在CPU熱插拔過程的CPU_POST_DEAD
 階段被調用。

 .suspend - 一個指向per-policy暫停函數的指針，該函數在關中斷且在該策略的調節器停止
 後被調用。

 .resume - 一個指向per-policy恢復函數的指針，該函數在關中斷且在調節器再一次啓動前被
 調用。

 .ready - 一個指向per-policy準備函數的指針，該函數在策略完全初始化之後被調用。

 .attr - 一個指向NULL結尾的"struct freq_attr"列表的指針，該列表允許導出值到
 sysfs。

 .boost_enabled - 如果設置，則啓用提升(boost)頻率。

 .set_boost - 一個指向per-policy函數的指針，該函數用來開啓/關閉提升(boost)頻率功能。


1.2 Per-CPU 初始化
------------------

每當一個新的CPU被註冊到設備模型中，或者當cpufreq驅動註冊自身之後，如果此CPU的cpufreq策
略不存在，則會調用per-policy的初始化函數cpufreq_driver.init。請注意，.init()和.exit()例程
只爲某個策略調用一次，而不是對該策略管理的每個CPU調用一次。它需要一個 ``struct cpufreq_policy
*policy`` 作爲參數。現在該怎麼做呢？

如果有必要，請在你的CPU上激活CPUfreq功能支持。

然後，驅動程序必須填寫以下值:

+-----------------------------------+--------------------------------------+
|policy->cpuinfo.min_freq和         | 該CPU支持的最低和最高頻率（kHz）     |
|policy->cpuinfo.max_freq           |                                      |
|                                   |                                      |
+-----------------------------------+--------------------------------------+
|policy->cpuinfo.transition_latency | CPU在兩個頻率之間切換所需的時間，以  |
|                                   | 納秒爲單位（如不適用，設定爲         |
|                                   | CPUFREQ_ETERNAL）                    |
|                                   |                                      |
+-----------------------------------+--------------------------------------+
|policy->cur                        | 該CPU當前的工作頻率(如適用)          |
|                                   |                                      |
+-----------------------------------+--------------------------------------+
|policy->min,                       | 必須包含該CPU的"默認策略"。稍後      |
|policy->max,                       | 會用這些值調用                       |
|policy->policy and, if necessary,  | cpufreq_driver.verify和下面函數      |
|policy->governor                   | 之一：cpufreq_driver.setpolicy或     |
|                                   | cpufreq_driver.target/target_index   |
|                                   |                                      |
+-----------------------------------+--------------------------------------+
|policy->cpus                       | 該policy通過DVFS框架影響的全部CPU    |
|                                   | (即與本CPU共享"時鐘/電壓"對)構成     |
|                                   | 掩碼(同時包含在線和離線CPU)，用掩碼  |
|                                   | 更新本字段                           |
|                                   |                                      |
+-----------------------------------+--------------------------------------+

對於設置其中的一些值(cpuinfo.min[max]_freq, policy->min[max])，頻率表輔助函數可能會有幫
助。關於它們的更多信息，請參見第2節。


1.3 驗證
--------

當用戶決定設置一個新的策略(由"policy,governor,min,max組成")時，必須對這個策略進行驗證，
以便糾正不兼容的值。爲了驗證這些值，cpufreq_verify_within_limits(``struct cpufreq_policy
*policy``, ``unsigned int min_freq``, ``unsigned int max_freq``)函數可能會有幫助。
關於頻率表輔助函數的詳細內容請參見第2節。

您需要確保至少有一個有效頻率（或工作範圍）在 policy->min 和 policy->max 範圍內。如果有必
要，先增大policy->max，只有在沒有解決方案的情況下，才減小policy->min。


1.4 target 或 target_index 或 setpolicy 或 fast_switch?
-------------------------------------------------------

大多數cpufreq驅動甚至大多數CPU頻率升降算法只允許將CPU頻率設置爲預定義的固定值。對於這些，你
可以使用->target()，->target_index()或->fast_switch()回調。

有些具有硬件調頻能力的處理器可以自行依據某些限制來切換CPU頻率。它們應使用->setpolicy()回調。


1.5. target/target_index
------------------------

target_index調用有兩個參數： ``struct cpufreq_policy * policy`` 和 ``unsigned int``
索引(用於索引頻率表項)。

當調用這裏時，CPUfreq驅動必須設置新的頻率。實際頻率必須由freq_table[index].frequency決定。

在發生錯誤的情況下總是應該恢復到之前的頻率(即policy->restore_freq)，即使我們已經切換到了
中間頻率。

已棄用
----------
target調用有三個參數。``struct cpufreq_policy * policy``, unsigned int target_frequency,
unsigned int relation.

CPUfreq驅動在調用這裏時必須設置新的頻率。實際的頻率必須使用以下規則來確定。

- 儘量貼近"目標頻率"。
- policy->min <= new_freq <= policy->max (這必須是有效的!!!)
- 如果 relation==CPUFREQ_REL_L，嘗試選擇一個高於或等於 target_freq 的 new_freq。("L代表
  最低，但不能低於")
- 如果 relation==CPUFREQ_REL_H，嘗試選擇一個低於或等於 target_freq 的 new_freq。("H代表
  最高，但不能高於")

這裏，頻率表輔助函數可能會幫助你 -- 詳見第2節。

1.6. fast_switch
----------------

這個函數用於從調度器的上下文進行頻率切換。並非所有的驅動都要實現它，因爲不允許在這個回調中睡眠。這
個回調必須經過高度優化，以儘可能快地進行切換。

這個函數有兩個參數： ``struct cpufreq_policy *policy`` 和 ``unsigned int target_frequency``。


1.7 setpolicy
-------------

setpolicy調用只需要一個 ``struct cpufreq_policy * policy`` 作爲參數。需要將處理器內或芯片組內動態頻
率切換的下限設置爲policy->min，上限設置爲policy->max，如果支持的話，當policy->policy爲
CPUFREQ_POLICY_PERFORMANCE時選擇面向性能的設置，爲CPUFREQ_POLICY_POWERSAVE時選擇面向省電的設置。
也可以查看drivers/cpufreq/longrun.c中的參考實現。

1.8 get_intermediate 和 target_intermediate
--------------------------------------------

僅適用於未設置 target_index() 和 CPUFREQ_ASYNC_NOTIFICATION 的驅動。

get_intermediate應該返回一個平臺想要切換到的穩定的中間頻率，target_intermediate()應該將CPU設置爲
該頻率，然後再跳轉到'index'對應的頻率。cpufreq核心會負責發送通知，驅動不必在
target_intermediate()或target_index()中處理它們。

在驅動程序不想爲某個目標頻率切換到中間頻率的情況下，它們可以讓get_intermediate()返回'0'。
在這種情況下，cpufreq核心將直接調用->target_index()。

注意：->target_index()應該在發生失敗的情況下將頻率恢復到policy->restore_freq，
因爲cpufreq核心會爲此發送通知。


2. 頻率表輔助函數
=================

由於大多數支持cpufreq的處理器只允許被設置爲幾個特定的頻率，因此，"頻率表"和一些相關函數可能會輔助處理器驅動
程序的一些工作。這樣的"頻率表"是一個由struct cpufreq_frequency_table的條目構成的數組，"driver_data"成員包
含驅動程序的專用值，"frequency"成員包含了相應的頻率，此外還有標誌成員。在表的最後，需要添加一個
cpufreq_frequency_table條目，頻率設置爲CPUFREQ_TABLE_END。如果想跳過表中的一個條目，則將頻率設置爲
CPUFREQ_ENTRY_INVALID。這些條目不需要按照任何特定的順序排序，如果排序了，cpufreq核心執行DVFS會更快一點，
因爲搜索最佳匹配會更快。

如果在policy->freq_table字段中包含一個有效的頻率表指針，頻率表就會被cpufreq核心自動驗證。

cpufreq_frequency_table_verify()保證至少有一個有效的頻率在policy->min和policy->max範圍內，並且所有其他
準則都被滿足。這對->verify調用很有幫助。

cpufreq_frequency_table_target()是對應於->target階段的頻率表輔助函數。只要把值傳遞給這個函數，這個函數就會返
回包含CPU要設置的頻率的頻率表條目。

以下宏可以作爲cpufreq_frequency_table的迭代器。

cpufreq_for_each_entry(pos, table) - 遍歷頻率表的所有條目。

cpufreq_for_each_valid_entry(pos, table) - 該函數遍歷所有條目，不包括CPUFREQ_ENTRY_INVALID頻率。
使用參數"pos" -- 一個 ``cpufreq_frequency_table *`` 作爲循環指針，使用參數"table" -- 作爲你想迭代
的 ``cpufreq_frequency_table *`` 。

例如::

	struct cpufreq_frequency_table *pos, *driver_freq_table;

	cpufreq_for_each_entry(pos, driver_freq_table) {
		/* Do something with pos */
		pos->frequency = ...
	}

如果你需要在driver_freq_table中處理pos的位置，不要做指針減法，因爲它的代價相當高。作爲替代，使用宏
cpufreq_for_each_entry_idx() 和 cpufreq_for_each_valid_entry_idx() 。


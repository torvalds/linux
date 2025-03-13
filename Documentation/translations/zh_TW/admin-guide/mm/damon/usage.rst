.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/mm/damon/usage.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

:校譯:

========
詳細用法
========

DAMON 爲不同的用戶提供了下面這些接口。

- *DAMON用戶空間工具。*
  `這 <https://github.com/damonitor/damo>`_ 爲有這特權的人， 如系統管理員，希望有一個剛好
  可以工作的人性化界面。
  使用它，用戶可以以人性化的方式使用DAMON的主要功能。不過，它可能不會爲特殊情況進行高度調整。
  它同時支持虛擬和物理地址空間的監測。更多細節，請參考它的 `使用文檔
  <https://github.com/damonitor/damo/blob/next/USAGE.md>`_。
- *sysfs接口。*
  :ref:`這 <sysfs_interface>` 是爲那些希望更高級的使用DAMON的特權用戶空間程序員準備的。
  使用它，用戶可以通過讀取和寫入特殊的sysfs文件來使用DAMON的主要功能。因此，你可以編寫和使
  用你個性化的DAMON sysfs包裝程序，代替你讀/寫sysfs文件。  `DAMON用戶空間工具
  <https://github.com/damonitor/damo>`_ 就是這種程序的一個例子  它同時支持虛擬和物理地址
  空間的監測。
- *內核空間編程接口。*
  :doc:`這 </mm/damon/api>` 這是爲內核空間程序員準備的。使用它，用戶可以通過爲你編寫內
  核空間的DAMON應用程序，最靈活有效地利用DAMON的每一個功能。你甚至可以爲各種地址空間擴展DAMON。
  詳細情況請參考接口 :doc:`文件 </mm/damon/api>`。

sysfs接口
=========
DAMON的sysfs接口是在定義 ``CONFIG_DAMON_SYSFS`` 時建立的。它在其sysfs目錄下創建多
個目錄和文件， ``<sysfs>/kernel/mm/damon/`` 。你可以通過對該目錄下的文件進行寫入和
讀取來控制DAMON。

對於一個簡短的例子，用戶可以監測一個給定工作負載的虛擬地址空間，如下所示::

    # cd /sys/kernel/mm/damon/admin/
    # echo 1 > kdamonds/nr_kdamonds && echo 1 > kdamonds/0/contexts/nr_contexts
    # echo vaddr > kdamonds/0/contexts/0/operations
    # echo 1 > kdamonds/0/contexts/0/targets/nr_targets
    # echo $(pidof <workload>) > kdamonds/0/contexts/0/targets/0/pid_target
    # echo on > kdamonds/0/state

文件層次結構
------------

DAMON sysfs接口的文件層次結構如下圖所示。在下圖中，父子關係用縮進表示，每個目錄有
``/`` 後綴，每個目錄中的文件用逗號（","）分開。 ::

    /sys/kernel/mm/damon/admin
    │ kdamonds/nr_kdamonds
    │ │ 0/state,pid
    │ │ │ contexts/nr_contexts
    │ │ │ │ 0/operations
    │ │ │ │ │ monitoring_attrs/
    │ │ │ │ │ │ intervals/sample_us,aggr_us,update_us
    │ │ │ │ │ │ nr_regions/min,max
    │ │ │ │ │ targets/nr_targets
    │ │ │ │ │ │ 0/pid_target
    │ │ │ │ │ │ │ regions/nr_regions
    │ │ │ │ │ │ │ │ 0/start,end
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ │ schemes/nr_schemes
    │ │ │ │ │ │ 0/action
    │ │ │ │ │ │ │ access_pattern/
    │ │ │ │ │ │ │ │ sz/min,max
    │ │ │ │ │ │ │ │ nr_accesses/min,max
    │ │ │ │ │ │ │ │ age/min,max
    │ │ │ │ │ │ │ quotas/ms,bytes,reset_interval_ms
    │ │ │ │ │ │ │ │ weights/sz_permil,nr_accesses_permil,age_permil
    │ │ │ │ │ │ │ watermarks/metric,interval_us,high,mid,low
    │ │ │ │ │ │ │ stats/nr_tried,sz_tried,nr_applied,sz_applied,qt_exceeds
    │ │ │ │ │ │ │ tried_regions/
    │ │ │ │ │ │ │ │ 0/start,end,nr_accesses,age
    │ │ │ │ │ │ │ │ ...
    │ │ │ │ │ │ ...
    │ │ │ │ ...
    │ │ ...

根
--

DAMON sysfs接口的根是 ``<sysfs>/kernel/mm/damon/`` ，它有一個名爲 ``admin`` 的
目錄。該目錄包含特權用戶空間程序控制DAMON的文件。擁有根權限的用戶空間工具或deamons可以
使用這個目錄。

kdamonds/
---------

與監測相關的信息包括請求規格和結果被稱爲DAMON上下文。DAMON用一個叫做kdamond的內核線程
執行每個上下文，多個kdamonds可以並行運行。

在 ``admin`` 目錄下，有一個目錄，即``kdamonds``，它有控制kdamonds的文件存在。在開始
時，這個目錄只有一個文件，``nr_kdamonds``。向該文件寫入一個數字（``N``），就會創建名爲
``0`` 到 ``N-1`` 的子目錄數量。每個目錄代表每個kdamond。

kdamonds/<N>/
-------------

在每個kdamond目錄中，存在兩個文件（``state`` 和 ``pid`` ）和一個目錄( ``contexts`` )。

讀取 ``state`` 時，如果kdamond當前正在運行，則返回 ``on`` ，如果沒有運行則返回 ``off`` 。
寫入 ``on`` 或 ``off`` 使kdamond處於狀態。向 ``state`` 文件寫 ``update_schemes_stats`` ，
更新kdamond的每個基於DAMON的操作方案的統計文件的內容。關於統計信息的細節，請參考
:ref:`stats section <sysfs_schemes_stats>`. 將 ``update_schemes_tried_regions`` 寫到
``state`` 文件，爲kdamond的每個基於DAMON的操作方案，更新基於DAMON的操作方案動作的嘗試區域目錄。
將`clear_schemes_tried_regions`寫入`state`文件，清除kdamond的每個基於DAMON的操作方案的動作
嘗試區域目錄。 關於基於DAMON的操作方案動作嘗試區域目錄的細節，請參考:ref:tried_regions 部分
<sysfs_schemes_tried_regions>`。

如果狀態爲 ``on``，讀取 ``pid`` 顯示kdamond線程的pid。

``contexts`` 目錄包含控制這個kdamond要執行的監測上下文的文件。

kdamonds/<N>/contexts/
----------------------

在開始時，這個目錄只有一個文件，即 ``nr_contexts`` 。向該文件寫入一個數字( ``N`` )，就會創
建名爲``0`` 到 ``N-1`` 的子目錄數量。每個目錄代表每個監測背景。目前，每個kdamond只支持
一個上下文，所以只有 ``0`` 或 ``1`` 可以被寫入文件。

contexts/<N>/
-------------

在每個上下文目錄中，存在一個文件(``operations``)和三個目錄(``monitoring_attrs``,
``targets``, 和 ``schemes``)。

DAMON支持多種類型的監測操作，包括對虛擬地址空間和物理地址空間的監測。你可以通過向文件
中寫入以下關鍵詞之一，並從文件中讀取，來設置和獲取DAMON將爲上下文使用何種類型的監測操作。

 - vaddr: 監測特定進程的虛擬地址空間
 - paddr: 監視系統的物理地址空間

contexts/<N>/monitoring_attrs/
------------------------------

用於指定監測屬性的文件，包括所需的監測質量和效率，都在 ``monitoring_attrs`` 目錄中。
具體來說，這個目錄下有兩個目錄，即 ``intervals`` 和 ``nr_regions`` 。

在 ``intervals`` 目錄下，存在DAMON的採樣間隔(``sample_us``)、聚集間隔(``aggr_us``)
和更新間隔(``update_us``)三個文件。你可以通過寫入和讀出這些文件來設置和獲取微秒級的值。

在 ``nr_regions`` 目錄下，有兩個文件分別用於DAMON監測區域的下限和上限（``min`` 和 ``max`` ），
這兩個文件控制着監測的開銷。你可以通過向這些文件的寫入和讀出來設置和獲取這些值。

關於間隔和監測區域範圍的更多細節，請參考設計文件 (:doc:`/mm/damon/design`)。

contexts/<N>/targets/
---------------------

在開始時，這個目錄只有一個文件 ``nr_targets`` 。向該文件寫入一個數字(``N``)，就可以創建
名爲 ``0`` 到 ``N-1`` 的子目錄的數量。每個目錄代表每個監測目標。

targets/<N>/
------------

在每個目標目錄中，存在一個文件(``pid_target``)和一個目錄(``regions``)。

如果你把 ``vaddr`` 寫到 ``contexts/<N>/operations`` 中，每個目標應該是一個進程。你
可以通過將進程的pid寫到 ``pid_target`` 文件中來指定DAMON的進程。

targets/<N>/regions
-------------------

當使用 ``vaddr`` 監測操作集時（ ``vaddr`` 被寫入 ``contexts/<N>/operations`` 文
件），DAMON自動設置和更新監測目標區域，這樣就可以覆蓋目標進程的整個內存映射。然而，用戶可
能希望將初始監測區域設置爲特定的地址範圍。

相反，當使用 ``paddr`` 監測操作集時，DAMON不會自動設置和更新監測目標區域（ ``paddr``
被寫入 ``contexts/<N>/operations`` 中）。因此，在這種情況下，用戶應該自己設置監測目標
區域。

在這種情況下，用戶可以按照自己的意願明確設置初始監測目標區域，將適當的值寫入該目錄下的文件。

開始時，這個目錄只有一個文件， ``nr_regions`` 。向該文件寫入一個數字(``N``)，就可以創
建名爲 ``0`` 到  ``N-1`` 的子目錄。每個目錄代表每個初始監測目標區域。

regions/<N>/
------------

在每個區域目錄中，你會發現兩個文件（ ``start``  和  ``end`` ）。你可以通過向文件寫入
和從文件中讀出，分別設置和獲得初始監測目標區域的起始和結束地址。

每個區域不應該與其他區域重疊。 目錄“N”的“結束”應等於或小於目錄“N+1”的“開始”。

contexts/<N>/schemes/
---------------------

對於一版的基於DAMON的數據訪問感知的內存管理優化，用戶通常希望系統對特定訪問模式的內存區
域應用內存管理操作。DAMON從用戶那裏接收這種形式化的操作方案，並將這些方案應用於目標內存
區域。用戶可以通過讀取和寫入這個目錄下的文件來獲得和設置這些方案。

在開始時，這個目錄只有一個文件，``nr_schemes``。向該文件寫入一個數字(``N``)，就可以
創建名爲``0``到``N-1``的子目錄的數量。每個目錄代表每個基於DAMON的操作方案。

schemes/<N>/
------------

在每個方案目錄中，存在五個目錄(``access_pattern``、``quotas``、``watermarks``、
``stats`` 和 ``tried_regions``)和一個文件(``action``)。

``action`` 文件用於設置和獲取你想應用於具有特定訪問模式的內存區域的動作。可以寫入文件
和從文件中讀取的關鍵詞及其含義如下。

 - ``willneed``: 對有 ``MADV_WILLNEED`` 的區域調用 ``madvise()`` 。
 - ``cold``: 對具有 ``MADV_COLD`` 的區域調用 ``madvise()`` 。
 - ``pageout``: 爲具有 ``MADV_PAGEOUT`` 的區域調用 ``madvise()`` 。
 - ``hugepage``: 爲帶有 ``MADV_HUGEPAGE`` 的區域調用 ``madvise()`` 。
 - ``nohugepage``: 爲帶有 ``MADV_NOHUGEPAGE`` 的區域調用 ``madvise()``。
 - ``lru_prio``: 在其LRU列表上對區域進行優先排序。
 - ``lru_deprio``: 對區域的LRU列表進行降低優先處理。
 - ``stat``: 什麼都不做，只計算統計數據

schemes/<N>/access_pattern/
---------------------------

每個基於DAMON的操作方案的目標訪問模式由三個範圍構成，包括以字節爲單位的區域大小、每個
聚合區間的監測訪問次數和區域年齡的聚合區間數。

在 ``access_pattern`` 目錄下，存在三個目錄（ ``sz``, ``nr_accesses``, 和 ``age`` ），
每個目錄有兩個文件（``min`` 和 ``max`` ）。你可以通過向  ``sz``, ``nr_accesses``, 和
``age``  目錄下的 ``min`` 和 ``max`` 文件分別寫入和讀取來設置和獲取給定方案的訪問模式。

schemes/<N>/quotas/
-------------------

每個 ``動作`` 的最佳 ``目標訪問模式`` 取決於工作負載，所以不容易找到。更糟糕的是，將某些動作
的方案設置得過於激進會造成嚴重的開銷。爲了避免這種開銷，用戶可以爲每個方案限制時間和大小配額。
具體來說，用戶可以要求DAMON儘量只使用特定的時間（``時間配額``）來應用動作，並且在給定的時間間
隔（``重置間隔``）內，只對具有目標訪問模式的內存區域應用動作，而不使用特定數量（``大小配額``）。

當預計超過配額限制時，DAMON會根據 ``目標訪問模式`` 的大小、訪問頻率和年齡，對找到的內存區域
進行優先排序。爲了進行個性化的優先排序，用戶可以爲這三個屬性設置權重。

在 ``quotas`` 目錄下，存在三個文件（``ms``, ``bytes``, ``reset_interval_ms``）和一個
目錄(``weights``)，其中有三個文件(``sz_permil``, ``nr_accesses_permil``, 和
``age_permil``)。

你可以設置以毫秒爲單位的 ``時間配額`` ，以字節爲單位的 ``大小配額`` ，以及以毫秒爲單位的 ``重
置間隔`` ，分別向這三個文件寫入數值。你還可以通過向 ``weights`` 目錄下的三個文件寫入數值來設
置大小、訪問頻率和年齡的優先權，單位爲千分之一。

schemes/<N>/watermarks/
-----------------------

爲了便於根據系統狀態激活和停用每個方案，DAMON提供了一個稱爲水位的功能。該功能接收五個值，稱爲
``度量`` 、``間隔`` 、``高`` 、``中`` 、``低`` 。``度量值`` 是指可以測量的系統度量值，如
自由內存比率。如果系統的度量值 ``高`` 於memoent的高值或 ``低`` 於低值，則該方案被停用。如果
該值低於 ``中`` ，則該方案被激活。

在水位目錄下，存在五個文件(``metric``, ``interval_us``,``high``, ``mid``, and ``low``)
用於設置每個值。你可以通過向這些文件的寫入來分別設置和獲取這五個值。

可以寫入 ``metric`` 文件的關鍵詞和含義如下。

 - none: 忽略水位
 - free_mem_rate: 系統的自由內存率（千分比）。

``interval`` 應以微秒爲單位寫入。

schemes/<N>/stats/
------------------

DAMON統計每個方案被嘗試應用的區域的總數量和字節數，每個方案被成功應用的區域的兩個數字，以及
超過配額限制的總數量。這些統計數據可用於在線分析或調整方案。

可以通過讀取 ``stats`` 目錄下的文件(``nr_tried``, ``sz_tried``, ``nr_applied``,
``sz_applied``, 和 ``qt_exceeds``)）分別檢索這些統計數據。這些文件不是實時更新的，所以
你應該要求DAMON sysfs接口通過在相關的 ``kdamonds/<N>/state`` 文件中寫入一個特殊的關鍵字
``update_schemes_stats`` 來更新統計信息的文件內容。

schemes/<N>/tried_regions/
--------------------------

當一個特殊的關鍵字 ``update_schemes_tried_regions`` 被寫入相關的 ``kdamonds/<N>/state``
文件時，DAMON會在這個目錄下創建從 ``0`` 開始命名的整數目錄。每個目錄包含的文件暴露了關於每個
內存區域的詳細信息，在下一個 :ref:`聚集區間 <sysfs_monitoring_attrs>`，相應的方案的 ``動作``
已經嘗試在這個目錄下應用。這些信息包括地址範圍、``nr_accesses`` 以及區域的 ``年齡`` 。

當另一個特殊的關鍵字 ``clear_schemes_tried_regions`` 被寫入相關的 ``kdamonds/<N>/state``
文件時，這些目錄將被刪除。

tried_regions/<N>/
------------------

在每個區域目錄中，你會發現四個文件(``start``, ``end``, ``nr_accesses``, and ``age``)。
讀取這些文件將顯示相應的基於DAMON的操作方案 ``動作`` 試圖應用的區域的開始和結束地址、``nr_accesses``
和 ``年齡`` 。

用例
~~~~

下面的命令應用了一個方案：”如果一個大小爲[4KiB, 8KiB]的內存區域在[10, 20]的聚合時間間隔內
顯示出每一個聚合時間間隔[0, 5]的訪問量，請分頁該區域。對於分頁，每秒最多隻能使用10ms，而且每
秒分頁不能超過1GiB。在這一限制下，首先分頁出具有較長年齡的內存區域。另外，每5秒鐘檢查一次系統
的可用內存率，當可用內存率低於50%時開始監測和分頁，但如果可用內存率大於60%，或低於30%，則停
止監測。“ ::

    # cd <sysfs>/kernel/mm/damon/admin
    # # populate directories
    # echo 1 > kdamonds/nr_kdamonds; echo 1 > kdamonds/0/contexts/nr_contexts;
    # echo 1 > kdamonds/0/contexts/0/schemes/nr_schemes
    # cd kdamonds/0/contexts/0/schemes/0
    # # set the basic access pattern and the action
    # echo 4096 > access_pattern/sz/min
    # echo 8192 > access_pattern/sz/max
    # echo 0 > access_pattern/nr_accesses/min
    # echo 5 > access_pattern/nr_accesses/max
    # echo 10 > access_pattern/age/min
    # echo 20 > access_pattern/age/max
    # echo pageout > action
    # # set quotas
    # echo 10 > quotas/ms
    # echo $((1024*1024*1024)) > quotas/bytes
    # echo 1000 > quotas/reset_interval_ms
    # # set watermark
    # echo free_mem_rate > watermarks/metric
    # echo 5000000 > watermarks/interval_us
    # echo 600 > watermarks/high
    # echo 500 > watermarks/mid
    # echo 300 > watermarks/low

請注意，我們強烈建議使用用戶空間的工具，如 `damo <https://github.com/damonitor/damo>`_ ，
而不是像上面那樣手動讀寫文件。以上只是一個例子。


監測結果的監測點
================

DAMON通過一個tracepoint ``damon:damon_aggregated`` 提供監測結果.  當監測開啓時，你可
以記錄追蹤點事件，並使用追蹤點支持工具如perf顯示結果。比如說::

    # echo on > monitor_on_DEPRECATED
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on_DEPRECATED
    # perf script


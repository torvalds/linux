.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/mm/damon/lru_sort.rst

:翻譯:

 臧雷剛 Leigang Zang <zangleigang@hisilicon.com>

:校譯:

==================
基於DAMON的LRU排序
==================

基於DAMON的LRU排序是一個靜態的內核模塊，旨在用於以主動的、輕量級的數據訪問模型
爲基礎的頁面優先級處理的LRU鏈表上，以使得LRU上的數據訪問模型更爲可信。

哪裏需要主動的LRU排序
=====================

在一個大型系統中，以頁爲粒度的訪問檢測會有比較顯著的開銷，LRU通常不會主動去排序，
而是對部分特殊事件進行部分的、響應式的排序，例如：特殊的用戶請求，系統調用或者
內存壓力。這導致，在有些場景下，LRU不能夠完美的作爲一個可信的數據訪問模型，比如
在內存壓力下對目標內存進行回收。

因爲DAMON能夠儘可能準確的識別數據訪問模型，同時只引起用戶指定範圍的開銷，主動的
執行DAMON_LRU_SORT讓LRU變得更爲可信是有益的，而且這隻需要較少和可控的開銷。

這是如何工作的
==============

DAMON_LRU_SORT使用DAMON尋找熱頁（範圍內的頁面訪問頻率高於用戶指定的閾值）和冷頁
（範圍內的頁面在超過用戶指定的時間無訪問），並提高熱頁和降低冷頁在LRU中的優先級。
爲了避免在排序過程佔用更多的CPU計算資源，可以設置一個CPU佔用時間的約束值。在約
束下，分別提升或者降低更多的熱頁和冷頁。系統管理員也可以配置三個內存水位以控制
在何種條件下自動激活或者停止這種機制。

冷熱閾值和CPU約束的默認值是比較保守的。這意味着，在默認參數下，模塊可以廣泛且無
負作用的使用在常見環境中，同時在只消耗一小部分CPU時間的情況下，給有內存壓力的系
統提供一定水平的冷熱識別。

接口：模塊參數
==============

使用此特性，你首先需要確認你的系統中運行的內核在編譯時啓用了
``CONFIG_DAMON_LRU_SORT=y``.

爲了讓系統管理員打開或者關閉並且調節指定的系統，DAMON_LRU_SORT設計了模塊參數。
這意味着，你可以添加 ``damon_lru_sort.<parameter>=<value>`` 到內核的啓動命令行
參數，或者在 ``/sys/modules/damon_lru_sort/parameters/<parameter>`` 寫入正確的
值。

下邊是每個參數的描述

enabled
-------

打開或者關閉DAMON_LRU_SORT.

你可以通過設置這個參數爲 ``Y`` 來打開DAMON_LRU_SORT。設置爲 ``N`` 關閉
DAMON_LRU_SORT。注意，在基於水位的激活的情況下，DAMON_LRU_SORT有可能不會真正去
監測或者做LRU排序。對這種情況，參考下方關於水位的描述。

commit_inputs
-------------

讓DAMON_LRU_SORT再次讀取輸入參數，除了 ``enabled`` 。

在DAMON_LRU_SORT運行時，新的輸入參數默認不會被應用。一旦這個參數被設置爲 ``Y``
，DAMON_LRU_SORT會再次讀取除了 ``enabled`` 之外的參數。讀取完成後，這個參數會被
設置爲 ``N`` 。如果在讀取時發現有無效參數，DAMON_LRU_SORT會被關閉。

hot_thres_access_freq
---------------------

熱點內存區域的訪問頻率閾值，千分比。

如果一個內存區域的訪問頻率大於等於這個值，DAMON_LRU_SORT把這個區域看作熱區，並
在LRU上把這個區域標記爲已訪問，因些在內存壓力下這部分內存不會被回收。默認爲50%。

cold_min_age
------------

用於識別冷內存區域的時間閾值，單位是微秒。

如果一個內存區域在這個時間內未被訪問過，DAMON_LRU_SORT把這個區域看作冷區，並在
LRU上把這個區域標記爲未訪問，因此在內存壓力下這些內存會首先被回收。默認值爲120
秒。

quota_ms
--------

嘗試LRU鏈表排序的時間限制，單位是毫秒。

DAMON_LRU_SORT在一個時間窗口內（quota_reset_interval_ms）內最多嘗試這麼長時間來
對LRU進行排序。這個可以用來作爲CPU計算資源的約束。如果值爲0，則表示無限制。

默認10毫秒。

quota_reset_interval_ms
-----------------------

配額計時重置週期，毫秒。

配額計時重置週期。即，在quota_reset_interval_ms毫秒內，DAMON_LRU_SORT對LRU進行
排序不會超過quota_ms或者quota_sz。

默認1秒。

wmarks_interval
---------------

水位的檢查週期，單位是微秒。

當DAMON_LRU_SORT使能但是由於水位而不活躍時檢查水位前最小的等待時間。默認值5秒。

wmarks_high
-----------

空閒內存高水位，千分比。

如果空閒內存水位高於這個值，DAMON_LRU_SORT停止工作，不做任何事，除了週期性的檢
查水位。默認200(20%)。

wmarks_mid
----------

空閒內存中間水位，千分比。

如果空閒內存水位在這個值與低水位之間，DAMON_LRU_SORT開始工作，開始檢測並對LRU鏈
表進行排序。默認150(15%)。

wmarks_low
----------

空閒內存低水位，千分比。

如果空閒內存小於這個值，DAMON_LRU_SORT不再工作，不做任何事，除了週期性的檢查水
線。默認50(5%)。

sample_interval
---------------

監測的採樣週期，微秒。

DAMON對冷內存監測的採樣週期。更多細節請參考DAMON文檔 (:doc:`usage`) 。默認5
毫秒。

aggr_interval
-------------

監測的收集週期，微秒。

DAMON對冷內存進行收集的時間週期。更多細節請參考DAMON文檔 (:doc:`usage`) 。默認
100毫秒。

min_nr_regions
--------------

最小監測區域數量。

對冷內存區域監測的最小數量。這個值可以作爲監測質量的下限。不過，這個值設置的過
大會增加開銷。更多細節請參考DAMON文檔 (:doc:`usage`) 。默認值爲10。

max_nr_regions
--------------

最大監測區域數量。

對冷內存區域監測的最大數量。這個值可以作爲監測質量的上限。然而，這個值設置的過
低會導致監測結果變差。更多細節請參考DAMON文檔 (:doc:`usage`) 。默認值爲1000。

monitor_region_start
--------------------

目標內存區域的起始物理地址。

DAMON_LRU_SORT要處理的目標內存區域的起始物理地址。默認，使用系統最大內存。

monitor_region_end
------------------

目標內存區域的結束物理地址。

DAMON_LRU_SORT要處理的目標內存區域的結束物理地址。默認，使用系統最大內存。

kdamond_pid
-----------

DAMON線程的PID。

如果DAMON_LRU_SORT是使能的，這個表示任務線程的PID。其它情況爲-1。

nr_lru_sort_tried_hot_regions
-----------------------------

被嘗試進行LRU排序的熱內存區域的數量。

bytes_lru_sort_tried_hot_regions
--------------------------------

被嘗試進行LRU排序的熱內存區域的大小（字節）。

nr_lru_sorted_hot_regions
-------------------------

成功進行LRU排序的熱內存區域的數量。

bytes_lru_sorted_hot_regions
----------------------------

成功進行LRU排序的熱內存區域的大小（字節）。

nr_hot_quota_exceeds
--------------------

熱區域時間約束超過限制的次數。

nr_lru_sort_tried_cold_regions
------------------------------

被嘗試進行LRU排序的冷內存區域的數量。

bytes_lru_sort_tried_cold_regions
---------------------------------

被嘗試進行LRU排序的冷內存區域的大小（字節）。

nr_lru_sorted_cold_regions
--------------------------

成功進行LRU排序的冷內存區域的數量。

bytes_lru_sorted_cold_regions
-----------------------------

成功進行LRU排序的冷內存區域的大小（字節）。

nr_cold_quota_exceeds
---------------------

冷區域時間約束超過限制的次數。

Example
=======

如下是一個運行時的命令示例，使DAMON_LRU_SORT查找訪問頻率超過50%的區域並對其進行
LRU的優先級的提升，同時降低那些超過120秒無人訪問的內存區域的優先級。優先級的處
理被限制在最多1%的CPU以避免DAMON_LRU_SORT消費過多CPU時間。在系統空閒內存超過50%
時DAMON_LRU_SORT停止工作，並在低於40%時重新開始工作。如果DAMON_RECLAIM沒有取得
進展且空閒內存低於20%，再次讓DAMON_LRU_SORT停止工作，以此回退到以LRU鏈表爲基礎
以頁面爲單位的內存回收上。 ::

    # cd /sys/modules/damon_lru_sort/parameters
    # echo 500 > hot_thres_access_freq
    # echo 120000000 > cold_min_age
    # echo 10 > quota_ms
    # echo 1000 > quota_reset_interval_ms
    # echo 500 > wmarks_high
    # echo 400 > wmarks_mid
    # echo 200 > wmarks_low
    # echo Y > enabled


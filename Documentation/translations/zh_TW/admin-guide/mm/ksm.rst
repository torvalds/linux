.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/mm/ksm.rst

:翻譯:

  徐鑫 xu xin <xu.xin16@zte.com.cn>


============
內核同頁合併
============


概述
====

KSM是一種能節省內存的數據去重功能，由CONFIG_KSM=y啓用，並在2.6.32版本時被添
加到Linux內核。詳見 ``mm/ksm.c`` 的實現，以及http://lwn.net/Articles/306704
和https://lwn.net/Articles/330589

KSM最初目的是爲了與KVM（即著名的內核共享內存）一起使用而開發的，通過共享虛擬機
之間的公共數據，將更多虛擬機放入物理內存。但它對於任何會生成多個相同數據實例的
應用程序都是很有用的。

KSM的守護進程ksmd會定期掃描那些已註冊的用戶內存區域，查找內容相同的頁面，這些
頁面可以被單個寫保護頁面替換（如果進程以後想要更新其內容，將自動複製）。使用：
引用:`sysfs intraface  <ksm_sysfs>` 接口來配置KSM守護程序在單個過程中所掃描的頁
數以及兩個過程之間的間隔時間。

KSM只合並匿名（私有）頁面，從不合並頁緩存（文件）頁面。KSM的合併頁面最初只能被
鎖定在內核內存中，但現在可以就像其他用戶頁面一樣被換出（但當它們被交換回來時共
享會被破壞: ksmd必須重新發現它們的身份並再次合併）。

以madvise控制KSM
================

KSM僅在特定的地址空間區域時運行，即應用程序通過使用如下所示的madvise(2)系統調
用來請求某塊地址成爲可能的合併候選者的地址空間::

    int madvise(addr, length, MADV_MERGEABLE)

應用程序當然也可以通過調用::

    int madvise(addr, length, MADV_UNMERGEABLE)

來取消該請求，並恢復爲非共享頁面：此時KSM將去除合併在該範圍內的任何合併頁。注意：
這個去除合併的調用可能突然需要的內存量超過實際可用的內存量-那麼可能會出現EAGAIN
失敗，但更可能會喚醒OOM killer。

如果KSM未被配置到正在運行的內核中，則madvise MADV_MERGEABLE 和 MADV_UNMERGEABLE
的調用只會以EINVAL 失敗。如果正在運行的內核是用CONFIG_KSM=y方式構建的，那麼這些
調用通常會成功：即使KSM守護程序當前沒有運行，MADV_MERGEABLE 仍然會在KSM守護程序
啓動時註冊範圍，即使該範圍不能包含KSM實際可以合併的任何頁面，即使MADV_UNMERGEABLE
應用於從未標記爲MADV_MERGEABLE的範圍。

如果一塊內存區域必須被拆分爲至少一個新的MADV_MERGEABLE區域或MADV_UNMERGEABLE區域，
當該進程將超過 ``vm.max_map_count`` 的設定，則madvise可能返回ENOMEM。（請參閱文檔
Documentation/admin-guide/sysctl/vm.rst）。

與其他madvise調用一樣，它們在用戶地址空間的映射區域上使用：如果指定的範圍包含未
映射的間隙（儘管在中間的映射區域工作），它們將報告ENOMEM，如果沒有足夠的內存用於
內部結構，則可能會因EAGAIN而失敗。

KSM守護進程sysfs接口
====================

KSM守護進程可以由``/sys/kernel/mm/ksm/`` 中的sysfs文件控制，所有人都可以讀取，但
只能由root用戶寫入。各接口解釋如下：


pages_to_scan
        ksmd進程進入睡眠前要掃描的頁數。
        例如， ``echo 100 > /sys/kernel/mm/ksm/pages_to_scan``

        默認值：100（該值被選擇用於演示目的）

sleep_millisecs
        ksmd在下次掃描前應休眠多少毫秒
        例如， ``echo 20 > /sys/kernel/mm/ksm/sleep_millisecs``

        默認值：20（該值被選擇用於演示目的）

merge_across_nodes
        指定是否可以合併來自不同NUMA節點的頁面。當設置爲0時，ksm僅合併在物理上位
        於同一NUMA節點的內存區域中的頁面。這降低了訪問共享頁面的延遲。在有明顯的
        NUMA距離上，具有更多節點的系統可能受益於設置該值爲0時的更低延遲。而對於
        需要對內存使用量最小化的較小系統來說，設置該值爲1（默認設置）則可能會受
        益於更大共享頁面。在決定使用哪種設置之前，您可能希望比較系統在每種設置下
        的性能。 ``merge_across_nodes`` 僅當系統中沒有ksm共享頁面時，才能被更改設
        置：首先將接口`run` 設置爲2從而對頁進行去合併，然後在修改
        ``merge_across_nodes`` 後再將‘run’又設置爲1，以根據新設置來重新合併。

        默認值：1（如早期的發佈版本一樣合併跨站點）

run
        * 設置爲0可停止ksmd運行，但保留合併頁面，
        * 設置爲1可運行ksmd，例如， ``echo 1 > /sys/kernel/mm/ksm/run`` ，
        * 設置爲2可停止ksmd運行，並且對所有目前已合併的頁進行去合併，但保留可合併
          區域以供下次運行。

        默認值：0（必須設置爲1才能激活KSM，除非禁用了CONFIG_SYSFS）

use_zero_pages
        指定是否應當特殊處理空頁（即那些僅含zero的已分配頁）。當該值設置爲1時，
        空頁與內核零頁合併，而不是像通常情況下那樣空頁自身彼此合併。這可以根據
        工作負載的不同，在具有着色零頁的架構上可以提高性能。啓用此設置時應小心，
        因爲它可能會降低某些工作負載的KSM性能，比如，當待合併的候選頁面的校驗和
        與空頁面的校驗和恰好匹配的時候。此設置可隨時更改，僅對那些更改後再合併
        的頁面有效。

        默認值：0（如同早期版本的KSM正常表現）

max_page_sharing
        單個KSM頁面允許的最大共享站點數。這將強制執行重複數據消除限制，以避免涉
        及遍歷共享KSM頁面的虛擬映射的虛擬內存操作的高延遲。最小值爲2，因爲新創
        建的KSM頁面將至少有兩個共享者。該值越高，KSM合併內存的速度越快，去重
        因子也越高，但是對於任何給定的KSM頁面，虛擬映射的最壞情況遍歷的速度也會
        越慢。減慢了這種遍歷速度就意味着在交換、壓縮、NUMA平衡和頁面遷移期間，
        某些虛擬內存操作將有更高的延遲，從而降低這些虛擬內存操作調用者的響應能力。
        其他任務如果不涉及執行虛擬映射遍歷的VM操作，其任務調度延遲不受此參數的影
        響，因爲這些遍歷本身是調度友好的。

stable_node_chains_prune_millisecs
        指定KSM檢查特定頁面的元數據的頻率（即那些達到過時信息數據去重限制標準的
        頁面）單位是毫秒。較小的毫秒值將以更低的延遲來釋放KSM元數據，但它們將使
        ksmd在掃描期間使用更多CPU。如果還沒有一個KSM頁面達到 ``max_page_sharing``
        標準，那就沒有什麼用。

KSM與MADV_MERGEABLE的工作有效性體現於 ``/sys/kernel/mm/ksm/`` 路徑下的接口：

pages_shared
        表示多少共享頁正在被使用
pages_sharing
        表示還有多少站點正在共享這些共享頁，即節省了多少
pages_unshared
        表示有多少頁是唯一的，但被反覆檢查以進行合併
pages_volatile
        表示有多少頁因變化太快而無法放在tree中
full_scans
        表示所有可合併區域已掃描多少次
stable_node_chains
        達到 ``max_page_sharing`` 限制的KSM頁數
stable_node_dups
        重複的KSM頁數

比值 ``pages_sharing/pages_shared`` 的最大值受限制於 ``max_page_sharing``
的設定。要想增加該比值，則相應地要增加 ``max_page_sharing`` 的值。

監測KSM的收益
=============

KSM可以通過合併相同的頁面來節省內存，但也會消耗額外的內存，因爲它需要生成一些rmap_items
來保存每個掃描頁面的簡要rmap信息。其中有些頁面可能會被合併，但有些頁面在被檢查幾次
後可能無法被合併，這些都是無益的內存消耗。

1) 如何確定KSM在全系統範圍內是節省內存還是消耗內存？這裏有一個簡單的近似計算方法供參考::

       general_profit =~ pages_sharing * sizeof(page) - (all_rmap_items) *
                         sizeof(rmap_item);

   其中all_rmap_items可以通過對 ``pages_sharing`` 、 ``pages_shared`` 、 ``pages_unshared``
   和 ``pages_volatile`` 的求和而輕鬆獲得。

2) 單一進程中KSM的收益也可以通過以下近似的計算得到::

       process_profit =~ ksm_merging_pages * sizeof(page) -
                         ksm_rmap_items * sizeof(rmap_item).

   其中ksm_merging_pages顯示在 ``/proc/<pid>/`` 目錄下，而ksm_rmap_items
   顯示在 ``/proc/<pid>/ksm_stat`` 。

從應用的角度來看， ``ksm_rmap_items`` 和 ``ksm_merging_pages`` 的高比例意
味着不好的madvise-applied策略，所以開發者或管理員必須重新考慮如何改變madvis策
略。舉個例子供參考，一個頁面的大小通常是4K，而rmap_item的大小在32位CPU架構上分
別是32B，在64位CPU架構上是64B。所以如果 ``ksm_rmap_items/ksm_merging_pages``
的比例在64位CPU上超過64，或者在32位CPU上超過128，那麼應用程序的madvise策略應
該被放棄，因爲ksm收益大約爲零或負值。

監控KSM事件
===========

在/proc/vmstat中有一些計數器，可以用來監控KSM事件。KSM可能有助於節省內存，這是
一種權衡，因爲它可能會在KSM COW或複製中的交換上遭受延遲。這些事件可以幫助用戶評估
是否或如何使用KSM。例如，如果cow_ksm增加得太快，用戶可以減少madvise(, , MADV_MERGEABLE)
的範圍。

cow_ksm
        在每次KSM頁面觸發寫時拷貝（COW）時都會被遞增，當用戶試圖寫入KSM頁面時，
        我們必須做一個拷貝。

ksm_swpin_copy
        在換入時，每次KSM頁被複制時都會被遞增。請注意，KSM頁在換入時可能會被複
        制，因爲do_swap_page()不能做所有的鎖，而需要重組一個跨anon_vma的KSM頁。

--
Izik Eidus,
Hugh Dickins, 2009年11月17日。


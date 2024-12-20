.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/dev-tools/kasan.rst
:Translator: 萬家兵 Wan Jiabing <wanjiabing@vivo.com>

內核地址消毒劑(KASAN)
=====================

概述
----

Kernel Address SANitizer(KASAN)是一種動態內存安全錯誤檢測工具，主要功能是
檢查內存越界訪問和使用已釋放內存的問題。

KASAN有三種模式:

1. 通用KASAN
2. 基於軟件標籤的KASAN
3. 基於硬件標籤的KASAN

用CONFIG_KASAN_GENERIC啓用的通用KASAN，是用於調試的模式，類似於用戶空
間的ASan。這種模式在許多CPU架構上都被支持，但它有明顯的性能和內存開銷。

基於軟件標籤的KASAN或SW_TAGS KASAN，通過CONFIG_KASAN_SW_TAGS啓用，
可以用於調試和自我測試，類似於用戶空間HWASan。這種模式只支持arm64，但其
適度的內存開銷允許在內存受限的設備上用真實的工作負載進行測試。

基於硬件標籤的KASAN或HW_TAGS KASAN，用CONFIG_KASAN_HW_TAGS啓用，被
用作現場內存錯誤檢測器或作爲安全緩解的模式。這種模式只在支持MTE（內存標籤
擴展）的arm64 CPU上工作，但它的內存和性能開銷很低，因此可以在生產中使用。

關於每種KASAN模式的內存和性能影響的細節，請參見相應的Kconfig選項的描述。

通用模式和基於軟件標籤的模式通常被稱爲軟件模式。基於軟件標籤的模式和基於
硬件標籤的模式被稱爲基於標籤的模式。

支持
----

體系架構
~~~~~~~~

在x86_64、arm、arm64、powerpc、riscv、s390、xtensa和loongarch上支持通用KASAN，
而基於標籤的KASAN模式只在arm64上支持。

編譯器
~~~~~~

軟件KASAN模式使用編譯時工具在每個內存訪問之前插入有效性檢查，因此需要一個
提供支持的編譯器版本。基於硬件標籤的模式依靠硬件來執行這些檢查，但仍然需要
一個支持內存標籤指令的編譯器版本。

通用KASAN需要GCC 8.3.0版本或更高版本，或者內核支持的任何Clang版本。

基於軟件標籤的KASAN需要GCC 11+或者內核支持的任何Clang版本。

基於硬件標籤的KASAN需要GCC 10+或Clang 12+。

內存類型
~~~~~~~~

通用KASAN支持在所有的slab、page_alloc、vmap、vmalloc、堆棧和全局內存
中查找錯誤。

基於軟件標籤的KASAN支持slab、page_alloc、vmalloc和堆棧內存。

基於硬件標籤的KASAN支持slab、page_alloc和不可執行的vmalloc內存。

對於slab，兩種軟件KASAN模式都支持SLUB和SLAB分配器，而基於硬件標籤的
KASAN只支持SLUB。

用法
----

要啓用KASAN，請使用以下命令配置內核::

	  CONFIG_KASAN=y

同時在 ``CONFIG_KASAN_GENERIC`` (啓用通用KASAN模式)， ``CONFIG_KASAN_SW_TAGS``
(啓用基於硬件標籤的KASAN模式)，和 ``CONFIG_KASAN_HW_TAGS`` (啓用基於硬件標籤
的KASAN模式)之間進行選擇。

對於軟件模式，還可以在 ``CONFIG_KASAN_OUTLINE`` 和 ``CONFIG_KASAN_INLINE``
之間進行選擇。outline和inline是編譯器插樁類型。前者產生較小的二進制文件，
而後者快2倍。

要將受影響的slab對象的alloc和free堆棧跟蹤包含到報告中，請啓用
``CONFIG_STACKTRACE`` 。要包括受影響物理頁面的分配和釋放堆棧跟蹤的話，
請啓用 ``CONFIG_PAGE_OWNER`` 並使用 ``page_owner=on`` 進行引導。

啓動參數
~~~~~~~~

KASAN受到通用 ``panic_on_warn`` 命令行參數的影響。當它被啓用時，KASAN
在打印出錯誤報告後會使內核恐慌。

默認情況下，KASAN只對第一個無效的內存訪問打印錯誤報告。使用
``kasan_multi_shot``，KASAN對每一個無效的訪問都打印一份報告。這會禁用
了KASAN報告的 ``panic_on_warn``。

另外，獨立於 ``panic_on_warn`` 、 ``kasan.fault=`` boot參數可以用
來控制恐慌和報告行爲。

- ``kasan.fault=report`` 或 ``=panic`` 控制是否只打印KASAN report或
  同時使內核恐慌（默認： ``report`` ）。即使 ``kasan_multi_shot`` 被
  啓用，恐慌也會發生。

基於軟件和硬件標籤的KASAN模式（見下面關於各種模式的部分）支持改變堆棧跟
蹤收集行爲：

- ``kasan.stacktrace=off`` 或 ``=on`` 禁用或啓用分配和釋放堆棧痕
  跡的收集（默認： ``on`` ）。

- ``kasan.stack_ring_size=<number of entries>`` 指定堆棧環的條
  目數（默認： ``32768`` ）。

基於硬件標籤的KASAN模式是爲了在生產中作爲一種安全緩解措施使用。因此，它
支持額外的啓動參數，允許完全禁用KASAN或控制其功能。

- ``kasan=off`` 或 ``=on`` 控制KASAN是否被啓用（默認： ``on`` ）。

- ``kasan.mode=sync``, ``=async`` or ``=asymm`` 控制KASAN是否
  被配置爲同步、異步或非對稱的執行模式（默認： ``同步`` ）。
  同步模式：當標籤檢查異常發生時，會立即檢測到不良訪問。
  異步模式：不良訪問的檢測是延遲的。當標籤檢查異常發生時，信息被存儲在硬
  件中（對於arm64來說是在TFSR_EL1寄存器中）。內核週期性地檢查硬件，並\
  且只在這些檢查中報告標籤異常。
  非對稱模式：讀取時同步檢測不良訪問，寫入時異步檢測。

- ``kasan.vmalloc=off`` or ``=on`` 禁用或啓用vmalloc分配的標記（默認： ``on`` ）。

錯誤報告
~~~~~~~~

典型的KASAN報告如下所示::

    ==================================================================
    BUG: KASAN: slab-out-of-bounds in kmalloc_oob_right+0xa8/0xbc [kasan_test]
    Write of size 1 at addr ffff8801f44ec37b by task insmod/2760

    CPU: 1 PID: 2760 Comm: insmod Not tainted 4.19.0-rc3+ #698
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.10.2-1 04/01/2014
    Call Trace:
     dump_stack+0x94/0xd8
     print_address_description+0x73/0x280
     kasan_report+0x144/0x187
     __asan_report_store1_noabort+0x17/0x20
     kmalloc_oob_right+0xa8/0xbc [kasan_test]
     kmalloc_tests_init+0x16/0x700 [kasan_test]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9
    RIP: 0033:0x7f96443109da
    RSP: 002b:00007ffcf0b51b08 EFLAGS: 00000202 ORIG_RAX: 00000000000000af
    RAX: ffffffffffffffda RBX: 000055dc3ee521a0 RCX: 00007f96443109da
    RDX: 00007f96445cff88 RSI: 0000000000057a50 RDI: 00007f9644992000
    RBP: 000055dc3ee510b0 R08: 0000000000000003 R09: 0000000000000000
    R10: 00007f964430cd0a R11: 0000000000000202 R12: 00007f96445cff88
    R13: 000055dc3ee51090 R14: 0000000000000000 R15: 0000000000000000

    Allocated by task 2760:
     save_stack+0x43/0xd0
     kasan_kmalloc+0xa7/0xd0
     kmem_cache_alloc_trace+0xe1/0x1b0
     kmalloc_oob_right+0x56/0xbc [kasan_test]
     kmalloc_tests_init+0x16/0x700 [kasan_test]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    Freed by task 815:
     save_stack+0x43/0xd0
     __kasan_slab_free+0x135/0x190
     kasan_slab_free+0xe/0x10
     kfree+0x93/0x1a0
     umh_complete+0x6a/0xa0
     call_usermodehelper_exec_async+0x4c3/0x640
     ret_from_fork+0x35/0x40

    The buggy address belongs to the object at ffff8801f44ec300
     which belongs to the cache kmalloc-128 of size 128
    The buggy address is located 123 bytes inside of
     128-byte region [ffff8801f44ec300, ffff8801f44ec380)
    The buggy address belongs to the page:
    page:ffffea0007d13b00 count:1 mapcount:0 mapping:ffff8801f7001640 index:0x0
    flags: 0x200000000000100(slab)
    raw: 0200000000000100 ffffea0007d11dc0 0000001a0000001a ffff8801f7001640
    raw: 0000000000000000 0000000080150015 00000001ffffffff 0000000000000000
    page dumped because: kasan: bad access detected

    Memory state around the buggy address:
     ffff8801f44ec200: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec280: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    >ffff8801f44ec300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03
                                                                    ^
     ffff8801f44ec380: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec400: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    ==================================================================

報告標題總結了發生的錯誤類型以及導致該錯誤的訪問類型。緊隨其後的是錯誤訪問的
堆棧跟蹤、所訪問內存分配位置的堆棧跟蹤（對於訪問了slab對象的情況）以及對象
被釋放的位置的堆棧跟蹤（對於訪問已釋放內存的問題報告）。接下來是對訪問的
slab對象的描述以及關於訪問的內存頁的信息。

最後，報告展示了訪問地址周圍的內存狀態。在內部，KASAN單獨跟蹤每個內存顆粒的
內存狀態，根據KASAN模式分爲8或16個對齊字節。報告的內存狀態部分中的每個數字
都顯示了圍繞訪問地址的其中一個內存顆粒的狀態。

對於通用KASAN，每個內存顆粒的大小爲8個字節。每個顆粒的狀態被編碼在一個影子字節
中。這8個字節可以是可訪問的，部分訪問的，已釋放的或成爲Redzone的一部分。KASAN
對每個影子字節使用以下編碼:00表示對應內存區域的所有8個字節都可以訪問；數字N
(1 <= N <= 7)表示前N個字節可訪問，其他(8 - N)個字節不可訪問；任何負值都表示
無法訪問整個8字節。KASAN使用不同的負值來區分不同類型的不可訪問內存，如redzones
或已釋放的內存（參見 mm/kasan/kasan.h）。

在上面的報告中，箭頭指向影子字節 ``03`` ，表示訪問的地址是部分可訪問的。

對於基於標籤的KASAN模式，報告最後的部分顯示了訪問地址周圍的內存標籤
(參考 `實施細則`_ 章節)。

請注意，KASAN錯誤標題（如 ``slab-out-of-bounds`` 或 ``use-after-free`` ）
是儘量接近的:KASAN根據其擁有的有限信息打印出最可能的錯誤類型。錯誤的實際類型
可能會有所不同。

通用KASAN還報告兩個輔助調用堆棧跟蹤。這些堆棧跟蹤指向代碼中與對象交互但不直接
出現在錯誤訪問堆棧跟蹤中的位置。目前，這包括 call_rcu() 和排隊的工作隊列。

實施細則
--------

通用KASAN
~~~~~~~~~

軟件KASAN模式使用影子內存來記錄每個內存字節是否可以安全訪問，並使用編譯時工具
在每次內存訪問之前插入影子內存檢查。

通用KASAN將1/8的內核內存專用於其影子內存（16TB以覆蓋x86_64上的128TB），並使用
具有比例和偏移量的直接映射將內存地址轉換爲其相應的影子地址。

這是將地址轉換爲其相應影子地址的函數::

    static inline void *kasan_mem_to_shadow(const void *addr)
    {
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
    }

在這裏 ``KASAN_SHADOW_SCALE_SHIFT = 3`` 。

編譯時工具用於插入內存訪問檢查。編譯器在每次訪問大小爲1、2、4、8或16的內存之前
插入函數調用( ``__asan_load*(addr)`` , ``__asan_store*(addr)``)。這些函數通過
檢查相應的影子內存來檢查內存訪問是否有效。

使用inline插樁，編譯器不進行函數調用，而是直接插入代碼來檢查影子內存。此選項
顯著地增大了內核體積，但與outline插樁內核相比，它提供了x1.1-x2的性能提升。

通用KASAN是唯一一種通過隔離延遲重新使用已釋放對象的模式
（參見 mm/kasan/quarantine.c 以瞭解實現）。

基於軟件標籤的KASAN模式
~~~~~~~~~~~~~~~~~~~~~~~

基於軟件標籤的KASAN使用軟件內存標籤方法來檢查訪問有效性。目前僅針對arm64架構實現。

基於軟件標籤的KASAN使用arm64 CPU的頂部字節忽略(TBI)特性在內核指針的頂部字節中
存儲一個指針標籤。它使用影子內存來存儲與每個16字節內存單元相關的內存標籤(因此，
它將內核內存的1/16專用於影子內存)。

在每次內存分配時，基於軟件標籤的KASAN都會生成一個隨機標籤，用這個標籤標記分配
的內存，並將相同的標籤嵌入到返回的指針中。

基於軟件標籤的KASAN使用編譯時工具在每次內存訪問之前插入檢查。這些檢查確保正在
訪問的內存的標籤等於用於訪問該內存的指針的標籤。如果標籤不匹配，基於軟件標籤
的KASAN會打印錯誤報告。

基於軟件標籤的KASAN也有兩種插樁模式（outline，發出回調來檢查內存訪問；inline，
執行內聯的影子內存檢查）。使用outline插樁模式，會從執行訪問檢查的函數打印錯誤
報告。使用inline插樁，編譯器會發出 ``brk`` 指令，並使用專用的 ``brk`` 處理程序
來打印錯誤報告。

基於軟件標籤的KASAN使用0xFF作爲匹配所有指針標籤（不檢查通過帶有0xFF指針標籤
的指針進行的訪問）。值0xFE當前保留用於標記已釋放的內存區域。


基於硬件標籤的KASAN模式
~~~~~~~~~~~~~~~~~~~~~~~

基於硬件標籤的KASAN在概念上類似於軟件模式，但它是使用硬件內存標籤作爲支持而
不是編譯器插樁和影子內存。

基於硬件標籤的KASAN目前僅針對arm64架構實現，並且基於ARMv8.5指令集架構中引入
的arm64內存標記擴展(MTE)和最高字節忽略(TBI)。

特殊的arm64指令用於爲每次內存分配指定內存標籤。相同的標籤被指定給指向這些分配
的指針。在每次內存訪問時，硬件確保正在訪問的內存的標籤等於用於訪問該內存的指針
的標籤。如果標籤不匹配，則會生成故障並打印報告。

基於硬件標籤的KASAN使用0xFF作爲匹配所有指針標籤（不檢查通過帶有0xFF指針標籤的
指針進行的訪問）。值0xFE當前保留用於標記已釋放的內存區域。

如果硬件不支持MTE（ARMv8.5之前），則不會啓用基於硬件標籤的KASAN。在這種情況下，
所有KASAN引導參數都將被忽略。

請注意，啓用CONFIG_KASAN_HW_TAGS始終會導致啓用內核中的TBI。即使提供了
``kasan.mode=off`` 或硬件不支持MTE（但支持TBI）。

基於硬件標籤的KASAN只報告第一個發現的錯誤。之後，MTE標籤檢查將被禁用。

影子內存
--------

本節的內容只適用於軟件KASAN模式。

內核將內存映射到地址空間的幾個不同部分。內核虛擬地址的範圍很大：沒有足夠的真實
內存來支持內核可以訪問的每個地址的真實影子區域。因此，KASAN只爲地址空間的某些
部分映射真實的影子。

默認行爲
~~~~~~~~

默認情況下，體系結構僅將實際內存映射到用於線性映射的陰影區域（以及可能的其他
小區域）。對於所有其他區域 —— 例如vmalloc和vmemmap空間 —— 一個只讀頁面被映射
到陰影區域上。這個只讀的影子頁面聲明所有內存訪問都是允許的。

這給模塊帶來了一個問題：它們不存在於線性映射中，而是存在於專用的模塊空間中。
通過連接模塊分配器，KASAN臨時映射真實的影子內存以覆蓋它們。例如，這允許檢測
對模塊全局變量的無效訪問。

這也造成了與 ``VMAP_STACK`` 的不兼容：如果堆棧位於vmalloc空間中，它將被分配
只讀頁面的影子內存，並且內核在嘗試爲堆棧變量設置影子數據時會出錯。

CONFIG_KASAN_VMALLOC
~~~~~~~~~~~~~~~~~~~~

使用 ``CONFIG_KASAN_VMALLOC`` ，KASAN可以以更大的內存使用爲代價覆蓋vmalloc
空間。目前，這在arm64、x86、riscv、s390和powerpc上受支持。

這通過連接到vmalloc和vmap並動態分配真實的影子內存來支持映射。

vmalloc空間中的大多數映射都很小，需要不到一整頁的陰影空間。因此，爲每個映射
分配一個完整的影子頁面將是一種浪費。此外，爲了確保不同的映射使用不同的影子
頁面，映射必須與 ``KASAN_GRANULE_SIZE * PAGE_SIZE`` 對齊。

相反，KASAN跨多個映射共享後備空間。當vmalloc空間中的映射使用影子區域的特定
頁面時，它會分配一個後備頁面。此頁面稍後可以由其他vmalloc映射共享。

KASAN連接到vmap基礎架構以懶清理未使用的影子內存。

爲了避免交換映射的困難，KASAN預測覆蓋vmalloc空間的陰影區域部分將不會被早期
的陰影頁面覆蓋，但是將不會被映射。這將需要更改特定於arch的代碼。

這允許在x86上支持 ``VMAP_STACK`` ，並且可以簡化對沒有固定模塊區域的架構的支持。

對於開發者
----------

忽略訪問
~~~~~~~~

軟件KASAN模式使用編譯器插樁來插入有效性檢查。此類檢測可能與內核的某些部分
不兼容，因此需要禁用。

內核的其他部分可能會訪問已分配對象的元數據。通常，KASAN會檢測並報告此類訪問，
但在某些情況下（例如，在內存分配器中），這些訪問是有效的。

對於軟件KASAN模式，要禁用特定文件或目錄的檢測，請將 ``KASAN_SANITIZE`` 添加
到相應的內核Makefile中:

- 對於單個文件(例如，main.o)::

    KASAN_SANITIZE_main.o := n

- 對於一個目錄下的所有文件::

    KASAN_SANITIZE := n

對於軟件KASAN模式，要在每個函數的基礎上禁用檢測，請使用KASAN特定的
``__no_sanitize_address`` 函數屬性或通用的 ``noinstr`` 。

請注意，禁用編譯器插樁（基於每個文件或每個函數）會使KASAN忽略在軟件KASAN模式
的代碼中直接發生的訪問。當訪問是間接發生的（通過調用檢測函數）或使用沒有編譯器
插樁的基於硬件標籤的模式時，它沒有幫助。

對於軟件KASAN模式，要在當前任務的一部分內核代碼中禁用KASAN報告，請使用
``kasan_disable_current()``/``kasan_enable_current()`` 部分註釋這部分代碼。
這也會禁用通過函數調用發生的間接訪問的報告。

對於基於標籤的KASAN模式，要禁用訪問檢查，請使用 ``kasan_reset_tag()`` 或
``page_kasan_tag_reset()`` 。請注意，通過 ``page_kasan_tag_reset()``
臨時禁用訪問檢查需要通過 ``page_kasan_tag`` / ``page_kasan_tag_set`` 保
存和恢復每頁KASAN標籤。

測試
~~~~

有一些KASAN測試可以驗證KASAN是否正常工作並可以檢測某些類型的內存損壞。

所有 KASAN 測試均與 KUnit 測試框架集成，並且可以啟用
透過 ``CONFIG_KASAN_KUNIT_TEST``。可以運行測試並進行部分驗證
以幾種不同的方式自動進行；請參閱下面的說明。

如果偵測到錯誤，每個 KASAN 測試都會列印多個 KASAN 報告之一。
然後測試列印其編號和狀態。

當測試通過::

        ok 28 - kmalloc_double_kzfree

當由於 ``kmalloc`` 失敗而導致測試失敗時::

        # kmalloc_large_oob_right: ASSERTION FAILED at mm/kasan/kasan_test.c:245
        Expected ptr is not null, but is
        not ok 5 - kmalloc_large_oob_right

當由於缺少KASAN報告而導致測試失敗時::

        # kmalloc_double_kzfree: EXPECTATION FAILED at mm/kasan/kasan_test.c:709
        KASAN failure expected in "kfree_sensitive(ptr)", but none occurred
        not ok 28 - kmalloc_double_kzfree


最後打印所有KASAN測試的累積狀態。成功::

        ok 1 - kasan

或者，如果其中一項測試失敗::

        not ok 1 - kasan

有幾種方法可以執行 KASAN 測試。

1. 可加載模塊

   啟用 ``CONFIG_KUNIT`` 後，測試可以建置為可載入模組
   並且透過使用 ``insmod`` 或 ``modprobe`` 來載入 ``kasan_test.ko`` 來運作。

2. 內置

   透過內建 ``CONFIG_KUNIT``，測試也可以內建。
   測試將在啓動時作爲後期初始化調用運行。

3. 使用kunit_tool

   通過內置 ``CONFIG_KUNIT`` 和 ``CONFIG_KASAN_KUNIT_TEST`` ，還可以使用
   ``kunit_tool`` 以更易讀的方式查看KUnit測試結果。這不會打印通過測試
   的KASAN報告。有關 ``kunit_tool`` 更多最新信息，請參閱
   `KUnit文檔 <https://www.kernel.org/doc/html/latest/dev-tools/kunit/index.html>`_ 。

.. _KUnit: https://www.kernel.org/doc/html/latest/dev-tools/kunit/index.html


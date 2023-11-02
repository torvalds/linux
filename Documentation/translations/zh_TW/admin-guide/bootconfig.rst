.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/admin-guide/bootconfig.rst

:譯者: 吳想成 Wu XiangCheng <bobwxc@email.cn>

========
引導配置
========

:作者: Masami Hiramatsu <mhiramat@kernel.org>

概述
====

引導配置擴展了現有的內核命令行，以一種更有效率的方式在引導內核時進一步支持
鍵值數據。這允許管理員傳遞一份結構化關鍵字的配置文件。

配置文件語法
============

引導配置文件的語法採用非常簡單的鍵值結構。每個關鍵字由點連接的單詞組成，鍵
和值由 ``=`` 連接。值以分號（ ``;`` ）或換行符（ ``\n`` ）結尾。數組值中每
個元素由逗號（ ``,`` ）分隔。::

  KEY[.WORD[...]] = VALUE[, VALUE2[...]][;]

與內核命令行語法不同，逗號和 ``=`` 周圍允許有空格。

關鍵字只允許包含字母、數字、連字符（ ``-`` ）和下劃線（ ``_`` ）。值可包含
可打印字符和空格，但分號（ ``;`` ）、換行符（ ``\n`` ）、逗號（ ``,`` ）、
井號（ ``#`` ）和右大括號（ ``}`` ）等分隔符除外。

如果你需要在值中使用這些分隔符，可以用雙引號（ ``"VALUE"`` ）或單引號
（ ``'VALUE'`` ）括起來。注意，引號無法轉義。

鍵的值可以爲空或不存在。這些鍵用於檢查該鍵是否存在（類似布爾值）。

鍵值語法
--------

引導配置文件語法允許用戶通過大括號合併鍵名部分相同的關鍵字。例如::

 foo.bar.baz = value1
 foo.bar.qux.quux = value2

也可以寫成::

 foo.bar {
    baz = value1
    qux.quux = value2
 }

或者更緊湊一些，寫成::

 foo.bar { baz = value1; qux.quux = value2 }

在這兩種樣式中，引導解析時相同的關鍵字都會自動合併。因此可以追加類似的樹或
鍵值。

相同關鍵字的值
--------------

禁止兩個或多個值或數組共享同一個關鍵字。例如::

 foo = bar, baz
 foo = qux  # !錯誤! 我們不可以重定義相同的關鍵字

如果你想要更新值，必須顯式使用覆蓋操作符 ``:=`` 。例如::

 foo = bar, baz
 foo := qux

這樣 ``foo`` 關鍵字的值就變成了 ``qux`` 。這對於通過添加（部分）自定義引導
配置來覆蓋默認值非常有用，免於解析默認引導配置。

如果你想對現有關鍵字追加值作爲數組成員，可以使用 ``+=`` 操作符。例如::

 foo = bar, baz
 foo += qux

這樣， ``foo`` 關鍵字就同時擁有了 ``bar`` ， ``baz`` 和 ``qux`` 。

此外，父關鍵字下可同時存在值和子關鍵字。
例如，下列配置是可行的。::

 foo = value1
 foo.bar = value2
 foo := value3 # 這會更新foo的值。

注意，裸值不能直接放進結構化關鍵字中，必須在大括號外定義它。例如::

 foo {
     bar = value1
     bar {
         baz = value2
         qux = value3
     }
 }

同時，關鍵字下值節點的順序是固定的。如果值和子關鍵字同時存在，值永遠是該關
鍵字的第一個子節點。因此如果用戶先指定子關鍵字，如::

 foo.bar = value1
 foo = value2

則在程序（和/proc/bootconfig）中，它會按如下顯示::

 foo = value2
 foo.bar = value1

註釋
----

配置語法接受shell腳本風格的註釋。註釋以井號（ ``#`` ）開始，到換行符
（ ``\n`` ）結束。

::

 # comment line
 foo = value # value is set to foo.
 bar = 1, # 1st element
       2, # 2nd element
       3  # 3rd element

會被解析爲::

 foo = value
 bar = 1, 2, 3

注意你不能把註釋放在值和分隔符（ ``,`` 或 ``;`` ）之間。如下配置語法是錯誤的::

 key = 1 # comment
       ,2


/proc/bootconfig
================

/proc/bootconfig是引導配置的用戶空間接口。與/proc/cmdline不同，此文件內容以
鍵值列表樣式顯示。
每個鍵值對一行，樣式如下::

 KEY[.WORDS...] = "[VALUE]"[,"VALUE2"...]


用引導配置引導內核
==================

用引導配置引導內核有兩種方法：將引導配置附加到initrd鏡像或直接嵌入內核中。

*initrd: initial RAM disk，初始內存磁盤*

將引導配置附加到initrd
----------------------

由於默認情況下引導配置文件是用initrd加載的，因此它將被添加到initrd（initramfs）
鏡像文件的末尾，其中包含填充、大小、校驗值和12字節幻數，如下所示::

 [initrd][bootconfig][padding][size(le32)][checksum(le32)][#BOOTCONFIG\n]

大小和校驗值爲小端序存放的32位無符號值。

當引導配置被加到initrd鏡像時，整個文件大小會對齊到4字節。空字符（ ``\0`` ）
會填補對齊空隙。因此 ``size`` 就是引導配置文件的長度+填充的字節。

Linux內核在內存中解碼initrd鏡像的最後部分以獲取引導配置數據。由於這種“揹負式”
的方法，只要引導加載器傳遞了正確的initrd文件大小，就無需更改或更新引導加載器
和內核鏡像本身。如果引導加載器意外傳遞了更長的大小，內核將無法找到引導配置數
據。

Linux內核在tools/bootconfig下提供了 ``bootconfig`` 命令來完成此操作，管理員
可以用它從initrd鏡像中刪除或追加配置文件。你可以用以下命令來構建它::

 # make -C tools/bootconfig

要向initrd鏡像添加你的引導配置文件，請按如下命令操作（舊數據會自動移除）::

 # tools/bootconfig/bootconfig -a your-config /boot/initrd.img-X.Y.Z

要從鏡像中移除配置，可以使用-d選項::

 # tools/bootconfig/bootconfig -d /boot/initrd.img-X.Y.Z

然後在內核命令行上添加 ``bootconfig`` 告訴內核去initrd文件末尾尋找內核配置。

將引導配置嵌入內核
------------------

如果你不能使用initrd，也可以通過Kconfig選項將引導配置文件嵌入內核中。在此情
況下，你需要用以下選項重新編譯內核::

 CONFIG_BOOT_CONFIG_EMBED=y
 CONFIG_BOOT_CONFIG_EMBED_FILE="/引導配置/文件/的/路徑"

``CONFIG_BOOT_CONFIG_EMBED_FILE`` 需要從源碼樹或對象樹開始的引導配置文件的
絕對/相對路徑。內核會將其嵌入作爲默認引導配置。

與將引導配置附加到initrd一樣，你也需要在內核命令行上添加 ``bootconfig`` 告訴
內核去啓用內嵌的引導配置。

注意，即使你已經設置了此選項，仍可用附加到initrd的其他引導配置覆蓋內嵌的引導
配置。

通過引導配置傳遞內核參數
========================

除了內核命令行，引導配置也可以用於傳遞內核參數。所有 ``kernel`` 關鍵字下的鍵
值對都將直接傳遞給內核命令行。此外， ``init`` 下的鍵值對將通過命令行傳遞給
init進程。參數按以下順序與用戶給定的內核命令行字符串相連，因此命令行參數可以
覆蓋引導配置參數（這取決於子系統如何處理參數，但通常前面的參數將被後面的參數
覆蓋）::

 [bootconfig params][cmdline params] -- [bootconfig init params][cmdline init params]

如果引導配置文件給出的kernel/init參數是::

 kernel {
   root = 01234567-89ab-cdef-0123-456789abcd
 }
 init {
  splash
 }

這將被複制到內核命令行字符串中，如下所示::

 root="01234567-89ab-cdef-0123-456789abcd" -- splash

如果用戶給出的其他命令行是::

 ro bootconfig -- quiet

則最後的內核命令行如下::

 root="01234567-89ab-cdef-0123-456789abcd" ro bootconfig -- splash quiet


配置文件的限制
==============

當前最大的配置大小是32KB，關鍵字總數（不是鍵值條目）必須少於1024個節點。
注意：這不是條目數而是節點數，條目必須消耗超過2個節點（一個關鍵字和一個值）。
所以從理論上講最多512個鍵值對。如果關鍵字平均包含3個單詞，則可有256個鍵值對。
在大多數情況下，配置項的數量將少於100個條目，小於8KB，因此這應該足夠了。如果
節點數超過1024，解析器將返回錯誤，即使文件大小小於32KB。（請注意，此最大尺寸
不包括填充的空字符。）
無論如何，因爲 ``bootconfig`` 命令在附加啓動配置到initrd映像時會驗證它，用戶
可以在引導之前注意到它。


引導配置API
===========

用戶可以查詢或遍歷鍵值對，也可以查找（前綴）根關鍵字節點，並在查找該節點下的
鍵值。

如果您有一個關鍵字字符串，則可以直接使用 xbc_find_value() 查詢該鍵的值。如果
你想知道引導配置裏有哪些關鍵字，可以使用 xbc_for_each_key_value() 迭代鍵值對。
請注意，您需要使用 xbc_array_for_each_value() 訪問數組的值，例如::

 vnode = NULL;
 xbc_find_value("key.word", &vnode);
 if (vnode && xbc_node_is_array(vnode))
    xbc_array_for_each_value(vnode, value) {
      printk("%s ", value);
    }

如果您想查找具有前綴字符串的鍵，可以使用 xbc_find_node() 通過前綴字符串查找
節點，然後用 xbc_node_for_each_key_value() 迭代前綴節點下的鍵。

但最典型的用法是獲取前綴下的命名值或前綴下的命名數組，例如::

 root = xbc_find_node("key.prefix");
 value = xbc_node_find_value(root, "option", &vnode);
 ...
 xbc_node_for_each_array_value(root, "array-option", value, anode) {
    ...
 }

這將訪問值“key.prefix.option”的值和“key.prefix.array-option”的數組。

鎖是不需要的，因爲在初始化之後配置只讀。如果需要修改，必須複製所有數據和關鍵字。


函數與結構體
============

相關定義的kernel-doc參見：

 - include/linux/bootconfig.h
 - lib/bootconfig.c


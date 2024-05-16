.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/dev-tools/testing-overview.rst
:Translator: 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

============
內核測試指南
============

有許多不同的工具可以用於測試Linux內核，因此瞭解什麼時候使用它們可能
很困難。本文檔粗略概述了它們之間的區別，並闡釋了它們是怎樣糅合在一起
的。

編寫和運行測試
==============

大多數內核測試都是用kselftest或KUnit框架之一編寫的。它們都讓運行測試
更加簡化，併爲編寫新測試提供幫助。

如果你想驗證內核的行爲——尤其是內核的特定部分——那你就要使用kUnit或
kselftest。

KUnit和kselftest的區別
----------------------

.. note::
     由於本文段中部分術語尚無較好的對應中文釋義，可能導致與原文含義
     存在些許差異，因此建議讀者結合原文
     （Documentation/dev-tools/testing-overview.rst）輔助閱讀。
     如對部分翻譯有異議或有更好的翻譯意見，歡迎聯繫譯者進行修訂。

KUnit（Documentation/dev-tools/kunit/index.rst）是用於“白箱”測
試的一個完整的內核內部系統：因爲測試代碼是內核的一部分，所以它能夠訪
問用戶空間不能訪問到的內部結構和功能。

因此，KUnit測試最好針對內核中較小的、自包含的部分，以便能夠獨立地測
試。“單元”測試的概念亦是如此。

比如，一個KUnit測試可能測試一個單獨的內核功能（甚至通過一個函數測試
一個單一的代碼路徑，例如一個錯誤處理案例），而不是整個地測試一個特性。

這也使得KUnit測試構建和運行非常地快，從而能夠作爲開發流程的一部分被
頻繁地運行。

有關更詳細的介紹，請參閱KUnit測試代碼風格指南
Documentation/dev-tools/kunit/style.rst

kselftest（Documentation/dev-tools/kselftest.rst），相對來說，大量用
於用戶空間，並且通常測試用戶空間的腳本或程序。

這使得編寫複雜的測試，或者需要操作更多全局系統狀態的測試更加容易（諸
如生成進程之類）。然而，從kselftest直接調用內核函數是不行的。這也就
意味着只有通過某種方式（如系統調用、驅動設備、文件系統等）導出到了用
戶空間的內核功能才能使用kselftest來測試。爲此，有些測試包含了一個伴
生的內核模塊用於導出更多的信息和功能。不過，對於基本上或者完全在內核
中運行的測試，KUnit可能是更佳工具。

kselftest也因此非常適合於全部功能的測試，因爲這些功能會將接口暴露到
用戶空間，從而能夠被測試，而不是展現實現細節。“system”測試和
“end-to-end”測試亦是如此。

比如，一個新的系統調用應該伴隨有新的kselftest測試。

代碼覆蓋率工具
==============

支持兩種不同代碼之間的覆蓋率測量工具。它們可以用來驗證一項測試執行的
確切函數或代碼行。這有助於決定內核被測試了多少，或用來查找合適的測試
中沒有覆蓋到的極端情況。

Documentation/translations/zh_CN/dev-tools/gcov.rst 是GCC的覆蓋率測試
工具，能用於獲取內核的全局或每個模塊的覆蓋率。與KCOV不同的是，這個工具
不記錄每個任務的覆蓋率。覆蓋率數據可以通過debugfs讀取，並通過常規的
gcov工具進行解釋。

Documentation/dev-tools/kcov.rst 是能夠構建在內核之中，用於在每個任務
的層面捕捉覆蓋率的一個功能。因此，它對於模糊測試和關於代碼執行期間信
息的其它情況非常有用，比如在一個單一系統調用裏使用它就很有用。

動態分析工具
============

內核也支持許多動態分析工具，用以檢測正在運行的內核中出現的多種類型的
問題。這些工具通常每個去尋找一類不同的缺陷，比如非法內存訪問，數據競
爭等併發問題，或整型溢出等其他未定義行爲。

如下所示：

* kmemleak檢測可能的內存泄漏。參閱
  Documentation/dev-tools/kmemleak.rst
* KASAN檢測非法內存訪問，如數組越界和釋放後重用（UAF）。參閱
  Documentation/dev-tools/kasan.rst
* UBSAN檢測C標準中未定義的行爲，如整型溢出。參閱
  Documentation/dev-tools/ubsan.rst
* KCSAN檢測數據競爭。參閱 Documentation/dev-tools/kcsan.rst
* KFENCE是一個低開銷的內存問題檢測器，比KASAN更快且能被用於批量構建。
  參閱 Documentation/dev-tools/kfence.rst
* lockdep是一個鎖定正確性檢測器。參閱
  Documentation/locking/lockdep-design.rst
* 除此以外，在內核中還有一些其它的調試工具，大多數能在
  lib/Kconfig.debug 中找到。

這些工具傾向於對內核進行整體測試，並且不像kselftest和KUnit一樣“傳遞”。
它們可以通過在啓用這些工具時運行內核測試以與kselftest或KUnit結合起來：
之後你就能確保這些錯誤在測試過程中都不會發生了。

一些工具與KUnit和kselftest集成，並且在檢測到問題時會自動打斷測試。

靜態分析工具
============

除了測試運行中的內核，我們還可以使用**靜態分析**工具直接分析內核的源代
碼（**在編譯時**）。內核中常用的工具允許人們檢查整個源代碼樹或其中的特
定文件。它們使得在開發過程中更容易發現和修復問題。

 Sparse可以通過執行類型檢查、鎖檢查、值範圍檢查來幫助測試內核，此外還
 可以在檢查代碼時報告各種錯誤和警告。關於如何使用它的細節，請參閱
 Documentation/translations/zh_CN/dev-tools/sparse.rst。

 Smatch擴展了Sparse，並提供了對編程邏輯錯誤的額外檢查，如開關語句中
 缺少斷點，錯誤檢查中未使用的返回值，忘記在錯誤路徑的返回中設置錯誤代
 碼等。Smatch也有針對更嚴重問題的測試，如整數溢出、空指針解除引用和內
 存泄漏。見項目頁面http://smatch.sourceforge.net/。

 Coccinelle是我們可以使用的另一個靜態分析器。Coccinelle經常被用來
 幫助源代碼的重構和並行演化，但它也可以幫助避免常見代碼模式中出現的某
 些錯誤。可用的測試類型包括API測試、內核迭代器的正確使用測試、自由操
 作的合理性檢查、鎖定行爲的分析，以及已知的有助於保持內核使用一致性的
 進一步測試。詳情請見Documentation/dev-tools/coccinelle.rst。

 不過要注意的是，靜態分析工具存在**假陽性**的問題。在試圖修復錯誤和警
 告之前，需要仔細評估它們。

何時使用Sparse和Smatch
----------------------

Sparse做類型檢查，例如驗證註釋的變量不會導致無符號的錯誤，檢測
``__user`` 指針使用不當的地方，以及分析符號初始化器的兼容性。

Smatch進行流程分析，如果允許建立函數數據庫，它還會進行跨函數分析。
Smatch試圖回答一些問題，比如這個緩衝區是在哪裏分配的？它有多大？這
個索引可以由用戶控制嗎？這個變量比那個變量大嗎？

一般來說，在Smatch中寫檢查比在Sparse中寫檢查要容易。儘管如此，
Sparse和Smatch的檢查還是有一些重疊的地方。

Smatch和Coccinelle的強項
------------------------

Coccinelle可能是最容易寫檢查的。它在預處理器之前工作，所以用Coccinelle
檢查宏中的錯誤更容易。Coccinelle還能爲你創建補丁，這是其他工具無法做到的。

例如，用Coccinelle你可以從 ``kmalloc_array(x, size, GFP_KERNEL)``
到 ``kmalloc_array(x, size, GFP_KERNEL)`` 進行大規模轉換，這真的很
有用。如果你只是創建一個Smatch警告，並試圖把轉換的工作推給維護者，他們會很
惱火。你將不得不爲每個警告爭論是否真的可以溢出。

Coccinelle不對變量值進行分析，而這正是Smatch的強項。另一方面，Coccinelle
允許你用簡單的方法做簡單的事情。


.. SPDX-License-Identifier: GPL-2.0

.. _tw_volatile_considered_harmful:

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/volatile-considered-harmful.rst
           <volatile_considered_harmful>`

如果想評論或更新本文的內容，請直接聯繫原文檔的維護者。如果你使用英文
交流有困難的話，也可以向中文版維護者求助。如果本翻譯更新不及時或者翻
譯存在問題，請聯繫中文版維護者::

        英文版維護者： Jonathan Corbet <corbet@lwn.net>
        中文版維護者： 伍鵬  Bryan Wu <bryan.wu@analog.com>
        中文版翻譯者： 伍鵬  Bryan Wu <bryan.wu@analog.com>
        中文版校譯者： 張漢輝  Eugene Teo <eugeneteo@kernel.sg>
                       楊瑞  Dave Young <hidave.darkstar@gmail.com>
                       時奎亮 Alex Shi <alex.shi@linux.alibaba.com>
                       胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

爲什麼不應該使用“volatile”類型
==============================

C程序員通常認爲volatile表示某個變量可以在當前執行的線程之外被改變；因此，在內核
中用到共享數據結構時，常常會有C程序員喜歡使用volatile這類變量。換句話說，他們經
常會把volatile類型看成某種簡易的原子變量，當然它們不是。在內核中使用volatile幾
乎總是錯誤的；本文檔將解釋爲什麼這樣。

理解volatile的關鍵是知道它的目的是用來消除優化，實際上很少有人真正需要這樣的應
用。在內核中，程序員必須防止意外的併發訪問破壞共享的數據結構，這其實是一個完全
不同的任務。用來防止意外併發訪問的保護措施，可以更加高效的避免大多數優化相關的
問題。

像volatile一樣，內核提供了很多原語來保證併發訪問時的數據安全（自旋鎖, 互斥量,內
存屏障等等），同樣可以防止意外的優化。如果可以正確使用這些內核原語，那麼就沒有
必要再使用volatile。如果仍然必須使用volatile，那麼幾乎可以肯定在代碼的某處有一
個bug。在正確設計的內核代碼中，volatile能帶來的僅僅是使事情變慢。

思考一下這段典型的內核代碼::

    spin_lock(&the_lock);
    do_something_on(&shared_data);
    do_something_else_with(&shared_data);
    spin_unlock(&the_lock);

如果所有的代碼都遵循加鎖規則，當持有the_lock的時候，不可能意外的改變shared_data的
值。任何可能訪問該數據的其他代碼都會在這個鎖上等待。自旋鎖原語跟內存屏障一樣—— 它
們顯式的用來書寫成這樣 —— 意味着數據訪問不會跨越它們而被優化。所以本來編譯器認爲
它知道在shared_data裏面將有什麼，但是因爲spin_lock()調用跟內存屏障一樣，會強制編
譯器忘記它所知道的一切。那麼在訪問這些數據時不會有優化的問題。

如果shared_data被聲名爲volatile，鎖操作將仍然是必須的。就算我們知道沒有其他人正在
使用它，編譯器也將被阻止優化對臨界區內shared_data的訪問。在鎖有效的同時，
shared_data不是volatile的。在處理共享數據的時候，適當的鎖操作可以不再需要
volatile —— 並且是有潛在危害的。

volatile的存儲類型最初是爲那些內存映射的I/O寄存器而定義。在內核裏，寄存器訪問也應
該被鎖保護，但是人們也不希望編譯器“優化”臨界區內的寄存器訪問。內核裏I/O的內存訪問
是通過訪問函數完成的；不贊成通過指針對I/O內存的直接訪問，並且不是在所有體系架構上
都能工作。那些訪問函數正是爲了防止意外優化而寫的，因此，再說一次，volatile類型不
是必需的。

另一種引起用戶可能使用volatile的情況是當處理器正忙着等待一個變量的值。正確執行一
個忙等待的方法是::

    while (my_variable != what_i_want)
        cpu_relax();

cpu_relax()調用會降低CPU的能量消耗或者讓位於超線程雙處理器；它也作爲內存屏障一樣出
現，所以，再一次，volatile不是必需的。當然，忙等待一開始就是一種反常規的做法。

在內核中，一些稀少的情況下volatile仍然是有意義的：

  - 在一些體系架構的系統上，允許直接的I/0內存訪問，那麼前面提到的訪問函數可以使用
    volatile。基本上，每一個訪問函數調用它自己都是一個小的臨界區域並且保證了按照
    程序員期望的那樣發生訪問操作。

  - 某些會改變內存的內聯彙編代碼雖然沒有什麼其他明顯的附作用，但是有被GCC刪除的可
    能性。在彙編聲明中加上volatile關鍵字可以防止這種刪除操作。

  - Jiffies變量是一種特殊情況，雖然每次引用它的時候都可以有不同的值，但讀jiffies
    變量時不需要任何特殊的加鎖保護。所以jiffies變量可以使用volatile，但是不贊成
    其他跟jiffies相同類型變量使用volatile。Jiffies被認爲是一種“愚蠢的遺留物"
    （Linus的話）因爲解決這個問題比保持現狀要麻煩的多。

  - 由於某些I/0設備可能會修改連續一致的內存,所以有時,指向連續一致內存的數據結構
    的指針需要正確的使用volatile。網絡適配器使用的環狀緩存區正是這類情形的一個例
    子，其中適配器用改變指針來表示哪些描述符已經處理過了。

對於大多代碼，上述幾種可以使用volatile的情況都不適用。所以，使用volatile是一種
bug並且需要對這樣的代碼額外仔細檢查。那些試圖使用volatile的開發人員需要退一步想想
他們真正想實現的是什麼。

非常歡迎刪除volatile變量的補丁 － 只要證明這些補丁完整的考慮了併發問題。

註釋
----

[1] https://lwn.net/Articles/233481/
[2] https://lwn.net/Articles/233482/

致謝
----

最初由Randy Dunlap推動並作初步研究
由Jonathan Corbet撰寫
參考Satyam Sharma，Johannes Stezenbach，Jesper Juhl，Heikki Orsila，
H. Peter Anvin，Philipp Hahn和Stefan Richter的意見改善了本檔。


.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :ref:`Documentation/process/submit-checklist.rst <submitchecklist>`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>
             Hu Haowen <src.res@email.cn>

.. _tw_submitchecklist:

Linux內核補丁提交清單
~~~~~~~~~~~~~~~~~~~~~

如果開發人員希望看到他們的內核補丁提交更快地被接受，那麼他們應該做一些基本
的事情。

這些都是在
:ref:`Documentation/translations/zh_TW/process/submitting-patches.rst <tw_submittingpatches>`
和其他有關提交Linux內核補丁的文檔中提供的。

1) 如果使用工具，則包括定義/聲明該工具的文件。不要依賴於其他頭文件拉入您使用
   的頭文件。

2) 乾淨的編譯：

   a) 使用適用或修改的 ``CONFIG`` 選項 ``=y``、``=m`` 和 ``=n`` 。沒有GCC
      警告/錯誤，沒有連結器警告/錯誤。

   b) 通過allnoconfig、allmodconfig

   c) 使用 ``O=builddir`` 時可以成功編譯

3) 通過使用本地交叉編譯工具或其他一些構建場在多個CPU體系結構上構建。

4) PPC64是一種很好的交叉編譯檢查體系結構，因爲它傾向於對64位的數使用無符號
   長整型。

5) 如下所述 :ref:`Documentation/translations/zh_TW/process/coding-style.rst <tw_codingstyle>`.
   檢查您的補丁是否爲常規樣式。在提交（ ``scripts/check patch.pl`` ）之前，
   使用補丁樣式檢查器檢查是否有輕微的衝突。您應該能夠處理您的補丁中存在的所有
   違規行爲。

6) 任何新的或修改過的 ``CONFIG`` 選項都不會弄髒配置菜單，並默認爲關閉，除非
   它們符合 ``Documentation/kbuild/kconfig-language.rst`` 中記錄的異常條件,
   菜單屬性：默認值.

7) 所有新的 ``kconfig`` 選項都有幫助文本。

8) 已仔細審查了相關的 ``Kconfig`` 組合。這很難用測試來糾正——腦力在這裡是有
   回報的。

9) 用 sparse 檢查乾淨。

10) 使用 ``make checkstack`` 和 ``make namespacecheck`` 並修復他們發現的任何
    問題。

    .. note::

        ``checkstack`` 並沒有明確指出問題，但是任何一個在堆棧上使用超過512
        字節的函數都可以進行更改。

11) 包括 :ref:`kernel-doc <kernel_doc>` 內核文檔以記錄全局內核API。（靜態函數
    不需要，但也可以。）使用 ``make htmldocs`` 或 ``make pdfdocs`` 檢查
    :ref:`kernel-doc <kernel_doc>` 並修復任何問題。

12) 通過以下選項同時啓用的測試 ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
    ``CONFIG_DEBUG_SLAB``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
    ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
    ``CONFIG_PROVE_RCU`` and ``CONFIG_DEBUG_OBJECTS_RCU_HEAD``

13) 已經過構建和運行時測試，包括有或沒有 ``CONFIG_SMP``, ``CONFIG_PREEMPT``.

14) 如果補丁程序影響IO/磁碟等：使用或不使用 ``CONFIG_LBDAF`` 進行測試。

15) 所有代碼路徑都已在啓用所有lockdep功能的情況下運行。

16) 所有新的/proc條目都記錄在 ``Documentation/``

17) 所有新的內核引導參數都記錄在
    Documentation/admin-guide/kernel-parameters.rst 中。

18) 所有新的模塊參數都記錄在 ``MODULE_PARM_DESC()``

19) 所有新的用戶空間接口都記錄在 ``Documentation/ABI/`` 中。有關詳細信息，
    請參閱 ``Documentation/ABI/README`` 。更改用戶空間接口的補丁應該抄送
    linux-api@vger.kernel.org。

20) 已通過至少注入slab和page分配失敗進行檢查。請參閱 ``Documentation/fault-injection/``
    如果新代碼是實質性的，那麼添加子系統特定的故障注入可能是合適的。

21) 新添加的代碼已經用 ``gcc -W`` 編譯（使用 ``make EXTRA-CFLAGS=-W`` ）。這
    將產生大量噪聲，但對於查找諸如「警告：有符號和無符號之間的比較」之類的錯誤
    很有用。

22) 在它被合併到-mm補丁集中之後進行測試，以確保它仍然與所有其他排隊的補丁以
    及VM、VFS和其他子系統中的各種更改一起工作。

23) 所有內存屏障例如 ``barrier()``, ``rmb()``, ``wmb()`` 都需要原始碼中的注
    釋來解釋它們正在執行的操作及其原因的邏輯。

24) 如果補丁添加了任何ioctl，那麼也要更新 ``Documentation/userspace-api/ioctl/ioctl-number.rst``

25) 如果修改後的原始碼依賴或使用與以下 ``Kconfig`` 符號相關的任何內核API或
    功能，則在禁用相關 ``Kconfig`` 符號和/或 ``=m`` （如果該選項可用）的情況
    下測試以下多個構建[並非所有這些都同時存在，只是它們的各種/隨機組合]：

    ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``, ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
    ``CONFIG_NET``, ``CONFIG_INET=n`` (但是後者伴隨 ``CONFIG_NET=y``).


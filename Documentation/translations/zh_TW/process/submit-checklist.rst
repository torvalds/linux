.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: Documentation/process/submit-checklist.rst
:Translator:
 - Alex Shi <alexs@kernel.org>
 - Wu XiangCheng <bobwxc@email.cn>
 - Hu Haowen <2023002089@link.tyut.edu.cn>

.. _tw_submitchecklist:

Linux內核補丁提交檢查單
~~~~~~~~~~~~~~~~~~~~~~~

如果開發人員希望看到他們的內核補丁提交更快地被接受，那麼他們應該做一些基本
的事情。

這些都是在 Documentation/translations/zh_CN/process/submitting-patches.rst
和其他有關提交Linux內核補丁的文檔中提供的。

1) 如果使用工具，則包括定義/聲明該工具的文件。不要依賴其他頭文件來引入您使用
   的頭文件。

2) 乾淨的編譯：

   a) 使用合適的 ``CONFIG`` 選項 ``=y``、``=m`` 和 ``=n`` 。沒有 ``gcc``
      警告/錯誤，沒有鏈接器警告/錯誤。

   b) 通過 ``allnoconfig`` 、 ``allmodconfig``

   c) 使用 ``O=builddir`` 時可以成功編譯

   d) 任何 Documentation/ 下的變更都能成功構建且不引入新警告/錯誤。
      用 ``make htmldocs`` 或 ``make pdfdocs`` 檢驗構建情況並修復問題。

3) 通過使用本地交叉編譯工具或其他一些構建設施在多個CPU體系結構上構建。

4) PPC64是一種很好的交叉編譯檢查體系結構，因爲它傾向於對64位的數使用無符號
   長整型。

5) 按 Documentation/translations/zh_CN/process/coding-style.rst 所述檢查您的
   補丁是否爲常規樣式。在提交之前使用補丁樣式檢查器 ``scripts/checkpatch.pl``
   檢查是否有輕微的衝突。您應該能夠處理您的補丁中存在的所有
   違規行爲。

6) 任何新的或修改過的 ``CONFIG`` 選項都不應搞亂配置菜單，並默認爲關閉，除非
   它們符合 ``Documentation/kbuild/kconfig-language.rst`` 菜單屬性：默認值中
   記錄的例外條件。

7) 所有新的 ``kconfig`` 選項都有幫助文本。

8) 已仔細審查了相關的 ``Kconfig`` 組合。這很難用測試來糾正——腦力在這裏是有
   回報的。

9) 通過 sparse 清查。
   （參見 Documentation/translations/zh_CN/dev-tools/sparse.rst ）

10) 使用 ``make checkstack`` 並修復他們發現的任何問題。

    .. note::

        ``checkstack`` 並不會明確指出問題，但是任何一個在堆棧上使用超過512
        字節的函數都可以進行更改。

11) 包括 :ref:`kernel-doc <kernel_doc_zh>` 內核文檔以記錄全局內核API。（靜態
    函數不需要，但也可以。）使用 ``make htmldocs`` 或 ``make pdfdocs`` 檢查
    :ref:`kernel-doc <kernel_doc_zh>` 並修復任何問題。

12) 通過以下選項同時啓用的測試： ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
    ``CONFIG_DEBUG_SLAB``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
    ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
    ``CONFIG_PROVE_RCU`` 和 ``CONFIG_DEBUG_OBJECTS_RCU_HEAD`` 。

13) 在 ``CONFIG_SMP``, ``CONFIG_PREEMPT`` 開啓和關閉的情況下都進行構建和運行
    時測試。

14) 所有代碼路徑都已在啓用所有死鎖檢測（lockdep）功能的情況下運行。

15) 所有新的 ``/proc`` 條目都記錄在 ``Documentation/``

16) 所有新的內核引導參數都記錄在
    Documentation/admin-guide/kernel-parameters.rst 中。

17) 所有新的模塊參數都記錄在 ``MODULE_PARM_DESC()``

18) 所有新的用戶空間接口都記錄在 ``Documentation/ABI/`` 中。有關詳細信息，
    請參閱 Documentation/admin-guide/abi.rst (或 ``Documentation/ABI/README``)。
    更改用戶空間接口的補丁應該抄送 linux-api@vger.kernel.org\ 。

19) 已通過至少注入slab和page分配失敗進行檢查。請參閱 ``Documentation/fault-injection/`` 。
    如果新代碼是實質性的，那麼添加子系統特定的故障注入可能是合適的。

20) 新添加的代碼已經用 ``gcc -W`` 編譯（使用 ``make EXTRA-CFLAGS=-W`` ）。這
    將產生大量噪聲，但對於查找諸如“警告：有符號和無符號之間的比較”之類的錯誤
    很有用。

21) 在它被合併到-mm補丁集中之後進行測試，以確保它仍然與所有其他排隊的補丁以
    及VM、VFS和其他子系統中的各種更改一起工作。

22) 所有內存屏障（例如 ``barrier()``, ``rmb()``, ``wmb()`` ）都需要源代碼注
    釋來解釋它們正在執行的操作及其原因的邏輯。

23) 如果補丁添加了任何ioctl，那麼也要更新
    ``Documentation/userspace-api/ioctl/ioctl-number.rst`` 。

24) 如果修改後的源代碼依賴或使用與以下 ``Kconfig`` 符號相關的任何內核API或
    功能，則在禁用相關 ``Kconfig`` 符號和/或 ``=m`` （如果該選項可用）的情況
    下測試以下多個構建[並非所有這些都同時存在，只是它們的各種/隨機組合]：

    ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``,
    ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
    ``CONFIG_NET``, ``CONFIG_INET=n`` （但是最後一個需要 ``CONFIG_NET=y`` ）。


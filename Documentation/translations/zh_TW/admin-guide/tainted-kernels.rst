.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../admin-guide/tainted-kernels`

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <src.res.211@gmail.com>

受污染的內核
-------------

當發生一些在稍後調查問題時可能相關的事件時，內核會將自己標記爲“受污染
（tainted）”的。不用太過擔心，大多數情況下運行受污染的內核沒有問題；這些信息
主要在有人想調查某個問題時纔有意義的，因爲問題的真正原因可能是導致內核受污染
的事件。這就是爲什麼來自受污染內核的缺陷報告常常被開發人員忽略，因此請嘗試用
未受污染的內核重現問題。

請注意，即使在您消除導致污染的原因（亦即卸載專有內核模塊）之後，內核仍將保持
污染狀態，以表示內核仍然不可信。這也是爲什麼內核在注意到內部問題（“kernel
bug”）、可恢復錯誤（“kernel oops”）或不可恢復錯誤（“kernel panic”）時會打印
受污染狀態，並將有關此的調試信息寫入日誌 ``dmesg`` 輸出。也可以通過
``/proc/`` 中的文件在運行時檢查受污染的狀態。


BUG、Oops或Panics消息中的污染標誌
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

在頂部以“CPU:”開頭的一行中可以找到受污染的狀態；內核是否受到污染和原因會顯示
在進程ID（“PID:”）和觸發事件命令的縮寫名稱（“Comm:”）之後::

	BUG: unable to handle kernel NULL pointer dereference at 0000000000000000
	Oops: 0002 [#1] SMP PTI
	CPU: 0 PID: 4424 Comm: insmod Tainted: P        W  O      4.20.0-0.rc6.fc30 #1
	Hardware name: Red Hat KVM, BIOS 0.5.1 01/01/2011
	RIP: 0010:my_oops_init+0x13/0x1000 [kpanic]
	[...]

如果內核在事件發生時沒有被污染，您將在那裏看到“Not-tainted:”；如果被污染，那
麼它將是“Tainted:”以及字母或空格。在上面的例子中，它看起來是這樣的::

	Tainted: P        W  O

下表解釋了這些字符的含義。在本例中，由於加載了專有模塊（ ``P`` ），出現了
警告（ ``W`` ），並且加載了外部構建的模塊（ ``O`` ），所以內核早些時候受到
了污染。要解碼其他字符，請使用下表。


解碼運行時的污染狀態
~~~~~~~~~~~~~~~~~~~~~

在運行時，您可以通過讀取 ``cat /proc/sys/kernel/tainted`` 來查詢受污染狀態。
如果返回 ``0`` ，則內核沒有受到污染；任何其他數字都表示受到污染的原因。解碼
這個數字的最簡單方法是使用腳本  ``tools/debugging/kernel-chktaint`` ，您的
發行版可能會將其作爲名爲 ``linux-tools`` 或 ``kernel-tools`` 的包的一部分提
供；如果沒有，您可以從
`git.kernel.org <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/tools/debugging/kernel-chktaint>`_
網站下載此腳本並用 ``sh kernel-chktaint`` 執行，它會在上面引用的日誌中有類似
語句的機器上打印這樣的內容::

	Kernel is Tainted for following reasons:
	 * Proprietary module was loaded (#0)
	 * Kernel issued warning (#9)
	 * Externally-built ('out-of-tree') module was loaded  (#12)
	See Documentation/admin-guide/tainted-kernels.rst in the Linux kernel or
	 https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html for
	 a more details explanation of the various taint flags.
	Raw taint value as int/string: 4609/'P        W  O     '

你也可以試着自己解碼這個數字。如果內核被污染的原因只有一個，那麼這很簡單，
在本例中您可以通過下表找到數字。如果你需要解碼有多個原因的數字，因爲它是一
個位域（bitfield），其中每個位表示一個特定類型的污染的存在或不存在，最好讓
前面提到的腳本來處理。但是如果您需要快速看一下，可以使用這個shell命令來檢查
設置了哪些位::

	$ for i in $(seq 18); do echo $(($i-1)) $(($(cat /proc/sys/kernel/tainted)>>($i-1)&1));done

污染狀態代碼表
~~~~~~~~~~~~~~~

===  =====  ======  ========================================================
 位  日誌     數字  內核被污染的原因
===  =====  ======  ========================================================
  0   G/P        1  已加載專用模塊
  1   _/F        2  模塊被強制加載
  2   _/S        4  內核運行在不合規範的系統上
  3   _/R        8  模塊被強制卸載
  4   _/M       16  處理器報告了機器檢測異常（MCE）
  5   _/B       32  引用了錯誤的頁或某些意外的頁標誌
  6   _/U       64  用戶空間應用程序請求的污染
  7   _/D      128  內核最近死機了，即曾出現OOPS或BUG
  8   _/A      256  ACPI表被用戶覆蓋
  9   _/W      512  內核發出警告
 10   _/C     1024  已加載staging驅動程序
 11   _/I     2048  已應用平臺固件缺陷的解決方案
 12   _/O     4096  已加載外部構建（“樹外”）模塊
 13   _/E     8192  已加載未簽名的模塊
 14   _/L    16384  發生軟鎖定
 15   _/K    32768  內核已實時打補丁
 16   _/X    65536  備用污染，爲發行版定義並使用
 17   _/T   131072  內核是用結構隨機化插件構建的
===  =====  ======  ========================================================

注：字符 ``_`` 表示空白，以便於閱讀表。

污染的更詳細解釋
~~~~~~~~~~~~~~~~~

 0)  ``G`` 加載的所有模塊都有GPL或兼容許可證， ``P`` 加載了任何專有模塊。
     沒有MODULE_LICENSE（模塊許可證）或MODULE_LICENSE未被insmod認可爲GPL
     兼容的模塊被認爲是專有的。


 1)  ``F`` 任何模塊被 ``insmod -f`` 強制加載， ``' '`` 所有模塊正常加載。

 2)  ``S`` 內核運行在不合規範的處理器或系統上：硬件已運行在不受支持的配置中，
     因此無法保證正確執行。內核將被污染，例如：

     - 在x86上：PAE是通過intel CPU（如Pentium M）上的forcepae強制執行的，這些
       CPU不報告PAE，但可能有功能實現，SMP內核在非官方支持的SMP Athlon CPU上
       運行，MSR被暴露到用戶空間中。
     - 在arm上：在某些CPU（如Keystone 2）上運行的內核，沒有啓用某些內核特性。
     - 在arm64上：CPU之間存在不匹配的硬件特性，引導加載程序以不同的模式引導CPU。
     - 某些驅動程序正在被用在不受支持的體系結構上（例如x86_64以外的其他系統
       上的scsi/snic，非x86/x86_64/itanium上的scsi/ips，已經損壞了arm64上
       irqchip/irq-gic的固件設置…）。

 3)  ``R`` 模塊被 ``rmmod -f`` 強制卸載， ``' '`` 所有模塊都正常卸載。

 4)  ``M`` 任何處理器報告了機器檢測異常， ``' '`` 未發生機器檢測異常。

 5)  ``B`` 頁面釋放函數發現錯誤的頁面引用或某些意外的頁面標誌。這表示硬件問題
     或內核錯誤；日誌中應該有其他信息指示發生此污染的原因。

 6)  ``U`` 用戶或用戶應用程序特意請求設置受污染標誌，否則應爲 ``' '`` 。

 7)  ``D`` 內核最近死機了，即出現了OOPS或BUG。

 8)  ``A`` ACPI表被重寫。

 9)  ``W`` 內核之前已發出過警告（儘管有些警告可能會設置更具體的污染標誌）。

 10) ``C`` 已加載staging驅動程序。

 11) ``I`` 內核正在處理平臺固件（BIOS或類似軟件）中的嚴重錯誤。

 12) ``O`` 已加載外部構建（“樹外”）模塊。

 13) ``E`` 在支持模塊簽名的內核中加載了未簽名的模塊。

 14) ``L`` 系統上先前發生過軟鎖定。

 15) ``K`` 內核已經實時打了補丁。

 16) ``X`` 備用污染，由Linux發行版定義和使用。

 17) ``T`` 內核構建時使用了randstruct插件，它可以有意生成非常不尋常的內核結構
     佈局（甚至是性能病態的佈局），這在調試時非常有用。於構建時設置。


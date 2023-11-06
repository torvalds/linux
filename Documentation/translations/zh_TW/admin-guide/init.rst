.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../admin-guide/init`

:譯者:

 吳想成 Wu XiangCheng <bobwxc@email.cn>
 胡皓文 Hu Haowen <2023002089@link.tyut.edu.cn>

解釋“No working init found.”啓動掛起消息
=========================================

:作者:

 Andreas Mohr <andi at lisas period de>

 Cristian Souza <cristianmsbr at gmail period com>

本文檔提供了加載初始化二進制（init binary）失敗的一些高層級原因（大致按執行
順序列出）。

1) **無法掛載根文件系統Unable to mount root FS** ：請設置“debug”內核參數（在
   引導加載程序bootloader配置文件或CONFIG_CMDLINE）以獲取更詳細的內核消息。

2) **初始化二進制不存在於根文件系統上init binary doesn't exist on rootfs** ：
   確保您的根文件系統類型正確（並且 ``root=`` 內核參數指向正確的分區）；擁有
   所需的驅動程序，例如SCSI或USB等存儲硬件；文件系統（ext3、jffs2等）是內建的
   （或者作爲模塊由initrd預加載）。

3) **控制檯設備損壞Broken console device** ： ``console= setup`` 中可能存在
   衝突 --> 初始控制檯不可用（initial console unavailable）。例如，由於串行
   IRQ問題（如缺少基於中斷的配置）導致的某些串行控制檯不可靠。嘗試使用不同的
   ``console= device`` 或像 ``netconsole=`` 。

4) **二進制存在但依賴項不可用Binary exists but dependencies not available** ：
   例如初始化二進制的必需庫依賴項，像 ``/lib/ld-linux.so.2`` 丟失或損壞。使用
   ``readelf -d <INIT>|grep NEEDED`` 找出需要哪些庫。

5) **無法加載二進制Binary cannot be loaded** ：請確保二進制的體系結構與您的
   硬件匹配。例如i386不匹配x86_64，或者嘗試在ARM硬件上加載x86。如果您嘗試在
   此處加載非二進制文件（shell腳本？），您應該確保腳本在其工作頭（shebang
   header）行 ``#!/...`` 中指定能正常工作的解釋器（包括其庫依賴項）。在處理
   腳本之前，最好先測試一個簡單的非腳本二進制文件，比如 ``/bin/sh`` ，並確認
   它能成功執行。要了解更多信息，請將代碼添加到 ``init/main.c`` 以顯示
   kernel_execve()的返回值。

當您發現新的失敗原因時，請擴展本解釋（畢竟加載初始化二進制是一個 **關鍵** 且
艱難的過渡步驟，需要儘可能無痛地進行），然後向LKML提交一個補丁。

待辦事項：

- 通過一個可以存儲 ``kernel_execve()`` 結果值的結構體數組實現各種
  ``run_init_process()`` 調用，並在失敗時通過迭代 **所有** 結果來記錄一切
  （非常重要的可用性修復）。
- 試着使實現本身在一般情況下更有幫助，例如在受影響的地方提供額外的錯誤消息。


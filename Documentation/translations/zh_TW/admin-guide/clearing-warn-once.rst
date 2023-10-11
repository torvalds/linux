.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Translator: 胡皓文 Hu Haowen <src.res.211@gmail.com>

清除 WARN_ONCE
--------------

WARN_ONCE / WARN_ON_ONCE / printk_once 僅僅打印一次消息.

echo 1 > /sys/kernel/debug/clear_warn_once

可以清除這種狀態並且再次允許打印一次告警信息，這對於運行測試集後重現問題
很有用。


.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_TW.rst

:Original: :doc:`../../../admin-guide/index`
:Translator: 胡皓文 Hu Haowen <src.res.211@gmail.com>

Linux 內核用戶和管理員指南
==========================

下面是一組隨時間添加到內核中的面向用戶的文檔的集合。到目前爲止，還沒有一個
整體的順序或組織 - 這些材料不是一個單一的，連貫的文件！幸運的話，情況會隨著
時間的推移而迅速改善。

這個初始部分包含總體信息，包括描述內核的README， 關於內核參數的文檔等。

.. toctree::
   :maxdepth: 1

   README

Todolist:

   kernel-parameters
   devices
   sysctl/index

本節介紹CPU漏洞及其緩解措施。

Todolist:

   hw-vuln/index

下面的一組文檔，針對的是試圖跟蹤問題和bug的用戶。

.. toctree::
   :maxdepth: 1

   reporting-issues
   security-bugs
   bug-hunting
   bug-bisect
   tainted-kernels
   init

Todolist:

   reporting-bugs
   ramoops
   dynamic-debug-howto
   kdump/index
   perf/index

這是應用程式開發人員感興趣的章節的開始。可以在這裡找到涵蓋內核ABI各個
方面的文檔。

Todolist:

   sysfs-rules

本手冊的其餘部分包括各種指南，介紹如何根據您的喜好配置內核的特定行爲。


.. toctree::
   :maxdepth: 1

   clearing-warn-once
   cpu-load
   unicode

Todolist:

   acpi/index
   aoe/index
   auxdisplay/index
   bcache
   binderfs
   binfmt-misc
   blockdev/index
   bootconfig
   braille-console
   btmrvl
   cgroup-v1/index
   cgroup-v2
   cifs/index
   cputopology
   dell_rbu
   device-mapper/index
   edid
   efi-stub
   ext4
   nfs/index
   gpio/index
   highuid
   hw_random
   initrd
   iostats
   java
   jfs
   kernel-per-CPU-kthreads
   laptops/index
   lcd-panel-cgram
   ldm
   lockup-watchdogs
   LSM/index
   md
   media/index
   mm/index
   module-signing
   mono
   namespaces/index
   numastat
   parport
   perf-security
   pm/index
   pnp
   rapidio
   ras
   rtc
   serial-console
   svga
   sysrq
   thunderbolt
   ufs
   vga-softcursor
   video-output
   xfs

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`


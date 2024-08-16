.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../admin-guide/index`
:Translator: Alex Shi <alex.shi@linux.alibaba.com>


Linux 内核用户和管理员指南
==========================

下面是一组随时间添加到内核中的面向用户的文档的集合。到目前为止，还没有一个
整体的顺序或组织 - 这些材料不是一个单一的，连贯的文件！幸运的话，情况会随着
时间的推移而迅速改善。

这个初始部分包含总体信息，包括描述内核的README， 关于内核参数的文档等。

.. toctree::
   :maxdepth: 1

   README

Todolist:

*   kernel-parameters
*   devices
*   sysctl/index

本节介绍CPU漏洞及其缓解措施。

Todolist:

*   hw-vuln/index

下面的一组文档，针对的是试图跟踪问题和bug的用户。

.. toctree::
   :maxdepth: 1

   reporting-issues
   reporting-regressions
   security-bugs
   bug-hunting
   bug-bisect
   tainted-kernels
   init

Todolist:

*   ramoops
*   dynamic-debug-howto
*   kdump/index
*   perf/index

这是应用程序开发人员感兴趣的章节的开始。可以在这里找到涵盖内核ABI各个
方面的文档。

Todolist:

*   sysfs-rules

本手册的其余部分包括各种指南，介绍如何根据您的喜好配置内核的特定行为。


.. toctree::
   :maxdepth: 1

   bootconfig
   clearing-warn-once
   cpu-load
   cputopology
   lockup-watchdogs
   numastat
   unicode
   sysrq
   mm/index

Todolist:

*   acpi/index
*   aoe/index
*   auxdisplay/index
*   bcache
*   binderfs
*   binfmt-misc
*   blockdev/index
*   braille-console
*   btmrvl
*   cgroup-v1/index
*   cgroup-v2
*   cifs/index
*   dell_rbu
*   device-mapper/index
*   edid
*   efi-stub
*   ext4
*   nfs/index
*   gpio/index
*   highuid
*   hw_random
*   initrd
*   iostats
*   java
*   jfs
*   kernel-per-CPU-kthreads
*   laptops/index
*   lcd-panel-cgram
*   ldm
*   LSM/index
*   md
*   media/index
*   module-signing
*   mono
*   namespaces/index
*   parport
*   perf-security
*   pm/index
*   pnp
*   rapidio
*   ras
*   rtc
*   serial-console
*   svga
*   thunderbolt
*   ufs
*   vga-softcursor
*   video-output
*   xfs

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

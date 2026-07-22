.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/index.rst

:翻译:

 时奎亮 Alex Shi <alex.shi@linux.alibaba.com>

 朱岩 Yan Zhu <zhuyan2015@qq.com>


Linux 内核用户和管理员指南
==========================

下面是一组随时间添加到内核中的面向用户的文档的集合。到目前为止，还没有一个
整体的顺序或组织 - 这些材料不是一个单一的，连贯的文件！幸运的话，情况会随着
时间的推移而迅速改善。


内核管理通用指南
----------------

本节包含总体信息，包括描述内核整体的 README 文件、内核参数文档等。

.. toctree::
   :maxdepth: 1

   README

Todolist:

*   devices
*   features

内核管理接口的重要组成部分是 /proc 和 sysfs 虚拟文件系统；这些文档描述了如何
与之交互。

.. toctree::
   :maxdepth: 1

   cputopology

Todolist:

*   sysfs-rules
*   sysctl/index
*   abi

安全相关文档：

.. toctree::
   :maxdepth: 1

Todolist:

*   hw-vuln/index
*   LSM/index
*   perf-security


内核启动
--------

.. toctree::
   :maxdepth: 1

   bootconfig

Todolist:

*   kernel-parameters
*   efi-stub
*   initrd


追踪和识别问题
--------------

以下是一组面向试图追踪特定问题和 bug 的用户的文档。

.. toctree::
   :maxdepth: 1

   reporting-issues
   reporting-regressions
   bug-hunting
   bug-bisect
   init
   clearing-warn-once
   lockup-watchdogs
   sysrq

Todolist:

*   quickly-build-trimmed-linux
*   verify-bugs-and-bisect-regressions
*   tainted-kernels
*   ramoops
*   dynamic-debug-howto
*   kdump/index
*   perf/index
*   pstore-blk
*   kernel-per-CPU-kthreads
*   RAS/index


核心内核子系统
--------------

这些文档描述了核心内核管理接口，这些接口几乎在任何系统上都值得关注。

.. toctree::
   :maxdepth: 1

   cpu-load
   mm/index
   module-signing
   numastat

Todolist:

*   cgroup-v2
*   cgroup-v1/index
*   namespaces/index
*   pm/index
*   syscall-user-dispatch


对非原生二进制格式的支持。请注意，其中一些文档相当 **古老**。

.. toctree::
   :maxdepth: 1

Todolist:

*   binfmt-misc
*   java
*   mono


块设备和文件系统管理
--------------------

.. toctree::
   :maxdepth: 1

Todolist:

*   bcache
*   binderfs
*   blockdev/index
*   cifs/index
*   device-mapper/index
*   ext4
*   filesystem-monitoring
*   nfs/index
*   iostats
*   jfs
*   md
*   ufs
*   xfs


专用设备指南
------------

如何在 Linux 系统中配置硬件。

.. toctree::
   :maxdepth: 1

Todolist:

*   acpi/index
*   aoe/index
*   auxdisplay/index
*   braille-console
*   btmrvl
*   dell_rbu
*   edid
*   gpio/index
*   hw_random
*   laptops/index
*   lcd-panel-cgram
*   media/index
*   nvme-multipath
*   parport
*   pnp
*   rapidio
*   rtc
*   serial-console
*   svga
*   thermal/index
*   thunderbolt
*   vga-softcursor
*   video-output


工作负载分析
------------

这是一个章节的开始，其中包含对从事 Linux 内核安全关键性分析的应用程序开发人员
和系统集成商感兴趣的信息。这里可以找到支持分析内核与应用程序交互以及关键内核
子系统预期的文档。

.. toctree::
   :maxdepth: 1

Todolist:

*   workload-tracing


其他内容
--------

一些难以分类且通常已过时的文档。

.. toctree::
   :maxdepth: 1

Todolist:

*   highuid
*   ldm
*   unicode

.. only::  subproject and html

   索引
   ====

   * :ref:`genindex`

.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../core-api/irq/index`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_core-api_index.rst:


===========
核心API文档
===========

这是核心内核API手册的首页。 非常感谢为本手册转换(和编写!)的文档!

核心实用程序
============

本节包含通用的和“核心中的核心”文档。 第一部分是 docbook 时期遗留下
来的大量 kerneldoc 信息；有朝一日，若有人有动力的话，应当把它们拆分
出来。

Todolist:

   kernel-api
   workqueue
   printk-basics
   printk-formats
   symbol-namespaces

数据结构和低级实用程序
======================

在整个内核中使用的函数库。

Todolist:

   kobject
   kref
   assoc_array
   xarray
   idr
   circular-buffers
   rbtree
   generic-radix-tree
   packing
   bus-virt-phys-mapping
   this_cpu_ops
   timekeeping
   errseq

并发原语
========

Linux如何让一切同时发生。 详情请参阅
:doc:`/locking/index`

.. toctree::
   :maxdepth: 1

   irq/index

Todolist:

   refcount-vs-atomic
   local_ops
   padata
   ../RCU/index

低级硬件管理
============

缓存管理，CPU热插拔管理等。

Todolist:

   cachetlb
   cpu_hotplug
   memory-hotplug
   genericirq
   protection-keys


内存管理
========

如何在内核中分配和使用内存。请注意，在
:doc:`/vm/index` 中有更多的内存管理文档。

Todolist:

   memory-allocation
   unaligned-memory-access
   dma-api
   dma-api-howto
   dma-attributes
   dma-isa-lpc
   mm-api
   genalloc
   pin_user_pages
   boot-time-mm
   gfp_mask-from-fs-io

内核调试的接口
==============

Todolist:

   debug-objects
   tracepoint
   debugging-via-ohci1394

其它文档
========

不适合放在其它地方或尚未归类的文件；

Todolist:

   librs

.. only:: subproject and html

   Indices
   =======

   * :ref:`genindex`

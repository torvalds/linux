.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/boot-time-mm.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮 <alexs@kernel.org>

.. _cn_core-api_boot-time-mm:

================
启动时的内存管理
================

系统初始化早期“正常”的内存管理由于没有设置完毕无法使用。但是内核仍然需要
为各种数据结构分配内存，例如物理页分配器。

一个叫做 ``memblock`` 的专用分配器执行启动时的内存管理。特定架构的初始化
必须在setup_arch()中设置它，并在mem_init()函数中移除它。

一旦早期的内存管理可用，它就为内存分配提供了各种函数和宏。分配请求可以指向
第一个（也可能是唯一的）节点或NUMA系统中的某个特定节点。有一些API变体在分
配失败时panic，也有一些不会panic的。

Memblock还提供了各种控制其自身行为的API。

Memblock概述
============

该API在以下内核代码中:

mm/memblock.c


函数和结构体
============

下面是关于memblock数据结构、函数和宏的描述。其中一些实际上是内部的，但由于
它们被记录下来，漏掉它们是很愚蠢的。此外，阅读内部函数的注释可以帮助理解引
擎盖下真正发生的事情。

该API在以下内核代码中:

include/linux/memblock.h
mm/memblock.c

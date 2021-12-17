.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/mm-api.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮<alexs@kernel.org>

.. _cn_core-api_mm-api:

============
内存管理APIs
============

API（Application Programming Interface，应用程序接口）

用户空间内存访问
================

该API在以下内核代码中:

arch/x86/include/asm/uaccess.h

arch/x86/lib/usercopy_32.c

mm/gup.c

.. _cn_mm-api-gfp-flags:

内存分配控制
============

该API在以下内核代码中:

include/linux/gfp.h

Slab缓存
========

此缓存非cpu片上缓存，请读者自行查阅资料。

该API在以下内核代码中:

include/linux/slab.h

mm/slab.c

mm/slab_common.c

mm/util.c

虚拟连续（内存页）映射
======================

该API在以下内核代码中:

mm/vmalloc.c


文件映射和页面缓存
==================

该API在以下内核代码中:

mm/readahead.c

mm/filemap.c

mm/page-writeback.c

mm/truncate.c

include/linux/pagemap.h

内存池
======

该API在以下内核代码中:

mm/mempool.c

DMA池
=====

DMA(Direct Memory Access，直接存储器访问)

该API在以下内核代码中:

mm/dmapool.c

更多的内存管理函数
==================

该API在以下内核代码中:

mm/memory.c

mm/page_alloc.c

mm/mempolicy.c

include/linux/mm_types.h

include/linux/mm.h

include/linux/mmzone.h

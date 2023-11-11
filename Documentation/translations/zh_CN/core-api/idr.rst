.. SPDX-License-Identifier: GPL-2.0+

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/idr.rst

:翻译:

 周彬彬 Binbin Zhou <zhoubinbin@loongson.cn>

:校译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>
 吴想成 Wu Xiangcheng <bobwxc@email.cn>
 时奎亮 Alex Shi <alexs@kernel.org>

======
ID分配
======

:作者: Matthew Wilcox

概述
====

要解决的一个常见问题是分配标识符（IDs）；它通常是标识事物的数字。比如包括文件描述
符、进程ID、网络协议中的数据包标识符、SCSI标记和设备实例编号。IDR和IDA为这个问题
提供了一个合理的解决方案，以避免每个人都自创。IDR提供将ID映射到指针的能力，而IDA
仅提供ID分配，因此内存效率更高。

IDR接口已经被废弃，请使用 ``XArray`` 。

IDR的用法
=========

首先初始化一个IDR，对于静态分配的IDR使用DEFINE_IDR()，或者对于动态分配的IDR使用
idr_init()。

您可以调用idr_alloc()来分配一个未使用的ID。通过调用idr_find()查询与该ID相关的指针，
并通过调用idr_remove()释放该ID。

如果需要更改与一个ID相关联的指针，可以调用idr_replace()。这样做的一个常见原因是通
过将 ``NULL`` 指针传递给分配函数来保留ID；用保留的ID初始化对象，最后将初始化的对
象插入IDR。

一些用户需要分配大于 ``INT_MAX`` 的ID。到目前为止，所有这些用户都满足 ``UINT_MAX``
的限制，他们使用idr_alloc_u32()。如果您需要超出u32的ID，我们将与您合作以满足您的
需求。

如果需要按顺序分配ID，可以使用idr_alloc_cyclic()。处理较大数量的ID时，IDR的效率会
降低，所以使用这个函数会有一点代价。

要对IDR使用的所有指针进行操作，您可以使用基于回调的idr_for_each()或迭代器样式的
idr_for_each_entry()。您可能需要使用idr_for_each_entry_continue()来继续迭代。如果
迭代器不符合您的需求，您也可以使用idr_get_next()。

当使用完IDR后，您可以调用idr_destroy()来释放IDR占用的内存。这并不会释放IDR指向的
对象；如果您想这样做，请使用其中一个迭代器来执行此操作。

您可以使用idr_is_empty()来查看当前是否分配了任何ID。

如果在从IDR分配一个新ID时需要带锁，您可能需要传递一组限制性的GFP标志，但这可能导
致IDR无法分配内存。为了解决该问题，您可以在获取锁之前调用idr_preload()，然后在分
配之后调用idr_preload_end()。

IDR同步的相关内容请见include/linux/idr.h文件中的“DOC: idr sync”。

IDA的用法
=========

IDA的用法的相关内容请见lib/idr.c文件中的“DOC: IDA description”。

函数和数据结构
==============

该API在以下内核代码中:

include/linux/idr.h

lib/idr.c

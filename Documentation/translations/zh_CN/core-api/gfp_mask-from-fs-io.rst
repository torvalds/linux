.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/gfp_mask-from-fs-io.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮 <alexs@kernel.org>

.. _cn_core-api_gfp_mask-from-fs-io:

============================
从FS/IO上下文中使用的GFP掩码
============================

:日期: 2018年5月
:作者: Michal Hocko <mhocko@kernel.org>

简介
====

文件系统和IO栈中的代码路径在分配内存时必须小心，以防止因直接调用FS或IO路径的内
存回收和阻塞已经持有的资源（例如锁--最常见的是用于事务上下文的锁）而造成递归死
锁。

避免这种死锁问题的传统方法是在调用分配器时，在gfp掩码中清除__GFP_FS和__GFP_IO
（注意后者意味着也要清除第一个）。GFP_NOFS和GFP_NOIO可以作为快捷方式使用。但事
实证明，上述方法导致了滥用，当限制性的gfp掩码被用于“万一”时，没有更深入的考虑，
这导致了问题，因为过度使用GFP_NOFS/GFP_NOIO会导致内存过度回收或其他内存回收的问
题。

新API
=====

从4.12开始，我们为NOFS和NOIO上下文提供了一个通用的作用域API，分别是
``memalloc_nofs_save`` , ``memalloc_nofs_restore`` 和 ``memalloc_noio_save`` ,
``memalloc_noio_restore`` ，允许从文件系统或I/O的角度将一个作用域标记为一个
关键部分。从该作用域的任何分配都将从给定的掩码中删除__GFP_FS和__GFP_IO，所以
没有内存分配可以追溯到FS/IO中。


该API在以下内核代码中:

include/linux/sched/mm.h

然后，FS/IO代码在任何与回收有关的关键部分开始之前简单地调用适当的保存函数
——例如，与回收上下文共享的锁或当事务上下文嵌套可能通过回收进行时。恢复函数
应该在关键部分结束时被调用。所有这一切最好都伴随着解释什么是回收上下文，以
方便维护。

请注意，保存/恢复函数的正确配对允许嵌套，所以从现有的NOIO或NOFS范围分别调
用 ``memalloc_noio_save`` 或 ``memalloc_noio_restore`` 是安全的。

那么__vmalloc(GFP_NOFS)呢？
===========================

vmalloc不支持GFP_NOFS语义，因为在分配器的深处有硬编码的GFP_KERNEL分配，要修
复这些分配是相当不容易的。这意味着用GFP_NOFS/GFP_NOIO调用 ``vmalloc`` 几乎
总是一个错误。好消息是，NOFS/NOIO语义可以通过范围API实现。

在理想的世界中，上层应该已经标记了危险的上下文，因此不需要特别的照顾， ``vmalloc``
的调用应该没有任何问题。有时，如果上下文不是很清楚，或者有叠加的违规行为，那么
推荐的方法是用范围API包装vmalloc，并加上注释来解释问题。

.. SPDX-License-Identifier: GPL-2.0+
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/xarray.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



.. _cn_core-api_xarray:

======
XArray
======

:作者: Matthew Wilcox

概览
====

XArray是一个抽象的数据类型，它的行为就像一个非常大的指针数组。它满足了许多与哈
希或传统可调整大小的数组相同的需求。与哈希不同的是，它允许你以一种高效的缓存方
式合理地转到下一个或上一个条目。与可调整大小的数组相比，不需要复制数据或改变MMU
的映射来增加数组。与双链表相比，它的内存效率更高，可并行，对缓存更友好。它利用
RCU的优势来执行查找而不需要锁定。

当使用的索引是密集聚集的时候，XArray的实现是有效的；而哈希对象并使用哈希作为索引
将不会有好的表现。XArray对小的索引进行了优化，不过对大的索引仍有良好的性能。如果
你的索引可以大于 ``ULONG_MAX`` ，那么XArray就不适合你的数据类型。XArray最重要
的用户是页面高速缓存。

普通指针可以直接存储在XArray中。它们必须是4字节对齐的，这对任何从kmalloc()和
alloc_page()返回的指针来说都是如此。这对任意的用户空间指针和函数指针来说都不是
真的。你可以存储指向静态分配对象的指针，只要这些对象的对齐方式至少是4（字节）。

你也可以在XArray中存储0到 ``LONG_MAX`` 之间的整数。你必须首先使用xa_mk_value()
将其转换为一个条目。当你从XArray中检索一个条目时，你可以通过调用xa_is_value()检
查它是否是一个值条目，并通过调用xa_to_value()将它转换回一个整数。

一些用户希望对他们存储在XArray中的指针进行标记。你可以调用xa_tag_pointer()来创建
一个带有标签的条目，xa_untag_pointer()将一个有标签的条目转回一个无标签的指针，
xa_pointer_tag()来检索一个条目的标签。标签指针使用相同的位，用于区分值条目和普通
指针，所以你必须决定他们是否要在任何特定的XArray中存储值条目或标签指针。

XArray不支持存储IS_ERR()指针，因为有些指针与值条目或内部条目冲突。

XArray的一个不寻常的特点是能够创建占据一系列索引的条目。一旦存储到其中，查询该范围
内的任何索引将返回与查询该范围内任何其他索引相同的条目。存储到任何索引都会存储所有
的索引条目。多索引条目可以明确地分割成更小的条目，或者将其存储 ``NULL`` 到任何条目中
都会使XArray忘记范围。

普通API
=======

首先初始化一个XArray，对于静态分配的XArray可以用DEFINE_XARRAY()，对于动态分配的
XArray可以用xa_init()。一个新初始化的XArray在每个索引处都包含一个 ``NULL`` 指针。

然后你可以用xa_store()来设置条目，用xa_load()来获取条目。xa_store将用新的条目覆盖任
何条目，并返回存储在该索引的上一个条目。你可以使用xa_erase()来代替调用xa_store()的
``NULL`` 条目。一个从未被存储过的条目、一个被擦除的条目和一个最近被存储过 ``NULL`` 的
条目之间没有区别。

你可以通过使用xa_cmpxchg()有条件地替换一个索引中的条目。和cmpxchg()一样，它只有在该索
引的条目有 ‘旧‘ 值时才会成功。它也会返回该索引上的条目；如果它返回与传递的 ‘旧‘ 相同的条
目，那么xa_cmpxchg()就成功了。

如果你只想在某个索引的当前条目为 ``NULL`` 时将一个新条目存储到该索引，你可以使用xa_insert()，
如果该条目不是空的，则返回 ``-EBUSY`` 。

你可以通过调用xa_extract()将条目从XArray中复制到一个普通数组中。或者你可以通过调用
xa_for_each()、xa_for_each_start()或xa_for_each_range()来遍历XArray中的现有条目。你
可能更喜欢使用xa_find()或xa_find_after()来移动到XArray中的下一个当前条目。

调用xa_store_range()可以在一个索引范围内存储同一个条目。如果你这样做，其他的一些操作将以
一种稍微奇怪的方式进行。例如，在一个索引上标记条目可能会导致该条目在一些，但不是所有其他索
引上被标记。储存到一个索引中可能会导致由一些，但不是所有其他索引检索的条目发生变化。

有时你需要确保对xa_store()的后续调用将不需要分配内存。xa_reserve()函数将在指定索引处存储
一个保留条目。普通API的用户将看到这个条目包含 ``NULL`` 。如果你不需要使用保留的条目，你可
以调用xa_release()来删除这个未使用的条目。如果在此期间有其他用户存储到该条目，xa_release()
将不做任何事情；相反，如果你想让该条目变成 ``NULL`` ，你应该使用xa_erase()。在一个保留的条
目上使用xa_insert()将会失败。

如果数组中的所有条目都是 ``NULL`` ，xa_empty()函数将返回 ``true`` 。

最后，你可以通过调用xa_destroy()删除XArray中的所有条目。如果XArray的条目是指针，你可能希望
先释放这些条目。你可以通过使用xa_for_each()迭代器遍历XArray中所有存在的条目来实现这一目的。

搜索标记
--------

数组中的每个条目都有三个与之相关的位，称为标记。每个标记可以独立于其他标记被设置或清除。你可以
通过使用xa_for_each_marked()迭代器来迭代有标记的条目。

你可以通过使用xa_get_mark()来查询某个条目是否设置了标记。如果该条目不是 ``NULL`` ，你可以通过
使用xa_set_mark()来设置一个标记，并通过调用xa_clear_mark()来删除条目上的标记。你可以通过调用
xa_marked()来询问XArray中的任何条目是否有一个特定的标记被设置。从XArray中删除一个条目会导致与
该条目相关的所有标记被清除。

在一个多索引条目的任何索引上设置或清除标记将影响该条目所涵盖的所有索引。查询任何索引上的标记将返
回相同的结果。

没有办法对没有标记的条目进行迭代；数据结构不允许有效地实现这一点。目前没有迭代器来搜索比特的逻辑
组合（例如迭代所有同时设置了 ``XA_MARK_1`` 和 ``XA_MARK_2`` 的条目，或者迭代所有设置了
``XA_MARK_0`` 或 ``XA_MARK_2`` 的条目）。如果有用户需要，可以增加这些内容。

分配XArrays
-----------

如果你使用DEFINE_XARRAY_ALLOC()来定义XArray，或者通过向xa_init_flags()传递 ``XA_FLAGS_ALLOC``
来初始化它，XArray会改变以跟踪条目是否被使用。

你可以调用xa_alloc()将条目存储在XArray中一个未使用的索引上。如果你需要从中断上下文中修改数组，你
可以使用xa_alloc_bh()或xa_alloc_irq()，在分配ID的同时禁用中断。

使用xa_store()、xa_cmpxchg()或xa_insert()也将标记该条目为正在分配。与普通的XArray不同，存储 ``NULL``
将标记该条目为正在使用中，就像xa_reserve()。要释放一个条目，请使用xa_erase()（或者xa_release()，
如果你只想释放一个 ``NULL`` 的条目）。

默认情况下，最低的空闲条目从0开始分配。如果你想从1开始分配条目，使用DEFINE_XARRAY_ALLOC1()或
``XA_FLAGS_ALLOC1`` 会更有效。如果你想分配ID到一个最大值，然后绕回最低的空闲ID，你可以使用
xa_alloc_cyclic()。

你不能在分配的XArray中使用 ``XA_MARK_0`` ，因为这个标记是用来跟踪一个条目是否是空闲的。其他的
标记可以供你使用。

内存分配
--------

xa_store(), xa_cmpxchg(), xa_alloc(), xa_reserve()和xa_insert()函数接受一个gfp_t参数，以
防XArray需要分配内存来存储这个条目。如果该条目被删除，则不需要进行内存分配，指定的GFP标志将被忽
略。

没有内存可供分配是可能的，特别是如果你传递了一组限制性的GFP标志。在这种情况下，这些函数会返回一
个特殊的值，可以用xa_err()把它变成一个错误值。如果你不需要确切地知道哪个错误发生，使用xa_is_err()
会更有效一些。

锁
--

当使用普通API时，你不必担心锁的问题。XArray使用RCU和一个内部自旋锁来同步访问:

不需要锁:
 * xa_empty()
 * xa_marked()

采取RCU读锁:
 * xa_load()
 * xa_for_each()
 * xa_for_each_start()
 * xa_for_each_range()
 * xa_find()
 * xa_find_after()
 * xa_extract()
 * xa_get_mark()

内部使用xa_lock:
 * xa_store()
 * xa_store_bh()
 * xa_store_irq()
 * xa_insert()
 * xa_insert_bh()
 * xa_insert_irq()
 * xa_erase()
 * xa_erase_bh()
 * xa_erase_irq()
 * xa_cmpxchg()
 * xa_cmpxchg_bh()
 * xa_cmpxchg_irq()
 * xa_store_range()
 * xa_alloc()
 * xa_alloc_bh()
 * xa_alloc_irq()
 * xa_reserve()
 * xa_reserve_bh()
 * xa_reserve_irq()
 * xa_destroy()
 * xa_set_mark()
 * xa_clear_mark()

假设进入时持有xa_lock:
 * __xa_store()
 * __xa_insert()
 * __xa_erase()
 * __xa_cmpxchg()
 * __xa_alloc()
 * __xa_set_mark()
 * __xa_clear_mark()

如果你想利用锁来保护你存储在XArray中的数据结构，你可以在调用xa_load()之前调用xa_lock()，然后在
调用xa_unlock()之前对你找到的对象进行一个引用计数。这将防止存储操作在查找对象和增加refcount期间
从数组中删除对象。你也可以使用RCU来避免解除对已释放内存的引用，但对这一点的解释已经超出了本文的范
围。

在修改数组时，XArray不会禁用中断或softirqs。从中断或softirq上下文中读取XArray是安全的，因为RCU锁
提供了足够的保护。

例如，如果你想在进程上下文中存储XArray中的条目，然后在softirq上下文中擦除它们，你可以这样做::

    void foo_init(struct foo *foo)
    {
        xa_init_flags(&foo->array, XA_FLAGS_LOCK_BH);
    }

    int foo_store(struct foo *foo, unsigned long index, void *entry)
    {
        int err;

        xa_lock_bh(&foo->array);
        err = xa_err(__xa_store(&foo->array, index, entry, GFP_KERNEL));
        if (!err)
            foo->count++;
        xa_unlock_bh(&foo->array);
        return err;
    }

    /* foo_erase()只在软中断上下文中调用 */
    void foo_erase(struct foo *foo, unsigned long index)
    {
        xa_lock(&foo->array);
        __xa_erase(&foo->array, index);
        foo->count--;
        xa_unlock(&foo->array);
    }

如果你要从中断或softirq上下文中修改XArray，你需要使用xa_init_flags()初始化数组，传递
``XA_FLAGS_LOCK_IRQ`` 或 ``XA_FLAGS_LOCK_BH`` （参数）。

上面的例子还显示了一个常见的模式，即希望在存储端扩展xa_lock的覆盖范围，以保护与数组相关的一些统计
数据。

与中断上下文共享XArray也是可能的，可以在中断处理程序和进程上下文中都使用xa_lock_irqsave()，或者
在进程上下文中使用xa_lock_irq()，在中断处理程序中使用xa_lock()。一些更常见的模式有一些辅助函数，
如xa_store_bh()、xa_store_irq()、xa_erase_bh()、xa_erase_irq()、xa_cmpxchg_bh() 和xa_cmpxchg_irq()。

有时你需要用一个mutex来保护对XArray的访问，因为这个锁在锁的层次结构中位于另一个mutex之上。这并不
意味着你有权使用像__xa_erase()这样的函数而不占用xa_lock；xa_lock是用来进行lockdep验证的，将来也
会用于其他用途。

__xa_set_mark() 和 __xa_clear_mark() 函数也适用于你查找一个条目并想原子化地设置或清除一个标记的
情况。在这种情况下，使用高级API可能更有效，因为它将使你免于走两次树。

高级API
=======

高级API提供了更多的灵活性和更好的性能，但代价是接口可能更难使用，保障措施更少。高级API没有为你加锁，
你需要在修改数组的时候使用xa_lock。在对数组进行只读操作时，你可以选择使用xa_lock或RCU锁。你可以在
同一个数组上混合使用高级和普通操作；事实上，普通API是以高级API的形式实现的。高级API只对具有GPL兼容
许可证的模块可用。

高级API是基于xa_state的。这是一个不透明的数据结构，你使用XA_STATE()宏在堆栈中声明。这个宏初始化了
xa_state，准备开始在XArray上移动。它被用作一个游标来保持在XArray中的位置，并让你把各种操作组合在一
起，而不必每次都从头开始。

xa_state也被用来存储错误(store errors)。你可以调用xas_error()来检索错误。所有的操作在进行之前都
会检查xa_state是否处于错误状态，所以你没有必要在每次调用之后检查错误；你可以连续进行多次调用，只在
方便的时候检查。目前XArray代码本身产生的错误只有 ``ENOMEM`` 和 ``EINVAL`` ，但它支持任意的错误，
以防你想自己调用xas_set_err()。

如果xa_state持有 ``ENOMEM`` 错误，调用xas_nomem()将尝试使用指定的gfp标志分配更多的内存，并将其缓
存在xa_state中供下一次尝试。这个想法是，你拿着xa_lock，尝试操作，然后放弃锁。该操作试图在持有锁的情
况下分配内存，但它更有可能失败。一旦你放弃了锁，xas_nomem()可以更努力地尝试分配更多内存。如果值得重
试该操作，它将返回 ``true`` （即出现了内存错误，分配了更多的内存）。如果它之前已经分配了内存，并且
该内存没有被使用，也没有错误（或者一些不是 ``ENOMEM`` 的错误），那么它将释放之前分配的内存。

内部条目
--------

XArray为它自己的目的保留了一些条目。这些条目从未通过正常的API暴露出来，但是当使用高级API时，有可能看
到它们。通常，处理它们的最好方法是把它们传递给xas_retry()，如果它返回 ``true`` ，就重试操作。

.. flat-table::
   :widths: 1 1 6

   * - 名称
     - 检测
     - 用途

   * - Node
     - xa_is_node()
     - 一个XArray节点。 在使用多索引xa_state时可能是可见的。

   * - Sibling
     - xa_is_sibling()
     - 一个多索引条目的非典型条目。该值表示该节点中的哪个槽有典型条目。

   * - Retry
     - xa_is_retry()
     - 这个条目目前正在被一个拥有xa_lock的线程修改。在这个RCU周期结束时，包含该条目的节点可能会被释放。
       你应该从数组的头部重新开始查找。

   * - Zero
     - xa_is_zero()
     - Zero条目通过普通API显示为 ``NULL`` ，但在XArray中占有一个条目，可用于保留索引供将来使用。这是
       通过为分配的条目分配XArrays来使用的，这些条目是 ``NULL`` 。

其他内部条目可能会在未来被添加。在可能的情况下，它们将由xas_retry()处理。

附加函数
--------

xas_create_range()函数分配了所有必要的内存来存储一个范围内的每一个条目。如果它不能分配内存，它将在
xa_state中设置ENOMEM。

你可以使用xas_init_marks()将一个条目上的标记重置为默认状态。这通常是清空所有标记，除非XArray被标记
为 ``XA_FLAGS_TRACK_FREE`` ，在这种情况下，标记0被设置，所有其他标记被清空。使用xas_store()将一个
条目替换为另一个条目不会重置该条目上的标记；如果你想重置标记，你应该明确地这样做。

xas_load()会尽可能地将xa_state移动到该条目附近。如果你知道xa_state已经移动到了该条目，并且需要检查
该条目是否有变化，你可以使用xas_reload()来保存一个函数调用。

如果你需要移动到XArray中的不同索引，可以调用xas_set()。这可以将光标重置到树的顶端，这通常会使下一个
操作将光标移动到树中想要的位置。如果你想移动到下一个或上一个索引，调用xas_next()或xas_prev()。设置
索引不会使光标在数组中移动，所以不需要锁，而移动到下一个或上一个索引则需要锁。

你可以使用xas_find()搜索下一个当前条目。这相当于xa_find()和xa_find_after()；如果光标已经移动到了
一个条目，那么它将找到当前引用的条目之后的下一个条目。如果没有，它将返回xa_state索引处的条目。使用
xas_next_entry()而不是xas_find()来移动到下一个当前条目，在大多数情况下会节省一个函数调用，但代价
是发出更多内联代码。

xas_find_marked()函数也是如此。如果xa_state没有被移动过，它将返回xa_state的索引处的条目，如果它
被标记了。否则，它将返回xa_state所引用的条目之后的第一个被标记的条目。xas_next_marked()函数等同
于xas_next_entry()。

当使用xas_for_each()或xas_for_each_marked()在XArray的某个范围内进行迭代时，可能需要暂时停止迭代。
xas_pause()函数的存在就是为了这个目的。在你完成了必要的工作并希望恢复后，xa_state处于适当的状态，在
你最后处理的条目后继续迭代。如果你在迭代时禁用了中断，那么暂停迭代并在每一个 ``XA_CHECK_SCHED`` 条目
中重新启用中断是很好的做法。

xas_get_mark(), xas_set_mark()和xas_clear_mark()函数要求xa_state光标已经被移动到XArray中的适当位
置；如果你在之前调用了xas_pause()或xas_set()，它们将不会有任何作用。

你可以调用xas_set_update()，让XArray每次更新一个节点时都调用一个回调函数。这被页面缓存的workingset
代码用来维护其只包含阴影项的节点列表。

多索引条目
----------

XArray有能力将多个索引联系在一起，因此对一个索引的操作会影响到所有的索引。例如，存储到任何索引将改变
从任何索引检索的条目的值。在任何索引上设置或清除一个标记，都会在每个被绑在一起的索引上设置或清除该标
记。目前的实现只允许将2的整数倍的范围绑在一起；例如指数64-127可以绑在一起，但2-6不能。这可以节省大量
的内存；例如，将512个条目绑在一起可以节省4kB以上的内存。

你可以通过使用XA_STATE_ORDER()或xas_set_order()，然后调用xas_store()来创建一个多索引条目。用一个
多索引的xa_state调用xas_load()会把xa_state移动到树中的正确位置，但是返回值没有意义，有可能是一个内
部条目或 ``NULL`` ，即使在范围内有一个条目存储。调用xas_find_conflict()将返回该范围内的第一个条目，
如果该范围内没有条目，则返回 ``NULL`` 。xas_for_each_conflict()迭代器将遍历每个与指定范围重叠的条目。

如果xas_load()遇到一个多索引条目，xa_state中的xa_index将不会被改变。当遍历一个XArray或者调用xas_find()
时，如果初始索引在一个多索引条目的中间，它将不会被改变。随后的调用或迭代将把索引移到范围内的第一个索引。
每个条目只会被返回一次，不管它占据了多少个索引。

不支持使用xas_next()或xas_prev()来处理一个多索引的xa_state。在一个多索引的条目上使用这两个函数中的任
何一个都会显示出同级的条目；这些条目应该被调用者跳过。

在一个多索引条目的任何一个索引中存储 ``NULL`` ，将把每个索引的条目设置为 ``NULL`` ，并解除绑定。通过调
用xas_split_alloc()，在没有xa_lock的情况下，可以将一个多索引条目分割成占据较小范围的条目，然后再取锁并
调用xas_split()。

函数和结构体
============

该API在以下内核代码中:

include/linux/xarray.h

lib/xarray.c

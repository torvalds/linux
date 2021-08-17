.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/refcount-vs-atomic.rst
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_refcount-vs-atomic:


=======================================
与atomic_t相比，refcount_t的API是这样的
=======================================

.. contents:: :local:

简介
====

refcount_t API的目标是为实现对象的引用计数器提供一个最小的API。虽然来自
lib/refcount.c的独立于架构的通用实现在下面使用了原子操作，但一些 ``refcount_*()``
和 ``atomic_*()`` 函数在内存顺序保证方面有很多不同。本文档概述了这些差异，并
提供了相应的例子，以帮助开发者根据这些内存顺序保证的变化来验证他们的代码。

本文档中使用的术语尽量遵循tools/memory-model/Documentation/explanation.txt
中定义的正式LKMM。

memory-barriers.txt和atomic_t.txt提供了更多关于内存顺序的背景，包括通用的
和针对原子操作的。

内存顺序的相关类型
==================

.. note:: 下面的部分只涵盖了本文使用的与原子操作和引用计数器有关的一些内存顺
   序类型。如果想了解更广泛的情况，请查阅memory-barriers.txt文件。

在没有任何内存顺序保证的情况下（即完全无序），atomics和refcounters只提供原
子性和程序顺序（program order, po）关系（在同一个CPU上）。它保证每个
``atomic_* ()`` 和 ``refcount_*()`` 操作都是原子性的，指令在单个CPU上按程序
顺序执行。这是用READ_ONCE()/WRITE_ONCE()和比较并交换原语实现的。

强（完全）内存顺序保证在同一CPU上的所有较早加载和存储的指令（所有程序顺序较早
[po-earlier]指令）在执行任何程序顺序较后指令（po-later）之前完成。它还保证
同一CPU上储存的程序优先较早的指令和来自其他CPU传播的指令必须在该CPU执行任何
程序顺序较后指令之前传播到其他CPU（A-累积属性）。这是用smp_mb()实现的。

RELEASE内存顺序保证了在同一CPU上所有较早加载和存储的指令（所有程序顺序较早
指令）在此操作前完成。它还保证同一CPU上储存的程序优先较早的指令和来自其他CPU
传播的指令必须在释放（release）操作之前传播到所有其他CPU（A-累积属性）。这是用
smp_store_release()实现的。

ACQUIRE内存顺序保证了同一CPU上的所有后加载和存储的指令（所有程序顺序较后
指令）在获取（acquire）操作之后完成。它还保证在获取操作执行后，同一CPU上
储存的所有程序顺序较后指令必须传播到所有其他CPU。这是用
smp_acquire__after_ctrl_dep()实现的。

对Refcounters的控制依赖（取决于成功）保证了如果一个对象的引用被成功获得（引用计数
器的增量或增加行为发生了，函数返回true），那么进一步的存储是针对这个操作的命令。对存
储的控制依赖没有使用任何明确的屏障来实现，而是依赖于CPU不对存储进行猜测。这只是
一个单一的CPU关系，对其他CPU不提供任何保证。


函数的比较
==========

情况1） - 非 “读/修改/写”（RMW）操作
------------------------------------

函数变化:

 * atomic_set() --> refcount_set()
 * atomic_read() --> refcount_read()

内存顺序保证变化:

 * none (两者都是完全无序的)


情况2） - 基于增量的操作，不返回任何值
--------------------------------------

函数变化:

 * atomic_inc() --> refcount_inc()
 * atomic_add() --> refcount_add()

内存顺序保证变化:

 * none (两者都是完全无序的)

情况3） - 基于递减的RMW操作，没有返回值
---------------------------------------

函数变化:

 * atomic_dec() --> refcount_dec()

内存顺序保证变化:

 * 完全无序的 --> RELEASE顺序


情况4） - 基于增量的RMW操作，返回一个值
---------------------------------------

函数变化:

 * atomic_inc_not_zero() --> refcount_inc_not_zero()
 * 无原子性对应函数 --> refcount_add_not_zero()

内存顺序保证变化:

 * 完全有序的 --> 控制依赖于存储的成功

.. note:: 此处 **假设** 了，必要的顺序是作为获得对象指针的结果而提供的。


情况 5） - 基于Dec/Sub递减的通用RMW操作，返回一个值
---------------------------------------------------

函数变化:

 * atomic_dec_and_test() --> refcount_dec_and_test()
 * atomic_sub_and_test() --> refcount_sub_and_test()

内存顺序保证变化:

 * 完全有序的 --> RELEASE顺序 + 成功后ACQUIRE顺序


情况6）其他基于递减的RMW操作，返回一个值
----------------------------------------

函数变化:

 * 无原子性对应函数 --> refcount_dec_if_one()
 * ``atomic_add_unless(&var, -1, 1)`` --> ``refcount_dec_not_one(&var)``

内存顺序保证变化:

 * 完全有序的 --> RELEASE顺序 + 控制依赖

.. note:: atomic_add_unless()只在执行成功时提供完整的顺序。


情况7）--基于锁的RMW
--------------------

函数变化:

 * atomic_dec_and_lock() --> refcount_dec_and_lock()
 * atomic_dec_and_mutex_lock() --> refcount_dec_and_mutex_lock()

内存顺序保证变化:

 * 完全有序 --> RELEASE顺序 + 控制依赖 + 持有

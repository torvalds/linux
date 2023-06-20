.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/kernel-api.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_kernel-api.rst:

============
Linux内核API
============


列表管理函数
============

该API在以下内核代码中:

include/linux/list.h

基本的C库函数
=============

在编写驱动程序时，一般不能使用C库中的例程。部分函数通常很有用，它们在
下面被列出。这些函数的行为可能会与ANSI定义的略有不同，这些偏差会在文中
注明。

字符串转换
----------

该API在以下内核代码中:

lib/vsprintf.c

include/linux/kernel.h

include/linux/kernel.h

lib/kstrtox.c

lib/string_helpers.c

字符串处理
----------

该API在以下内核代码中:

lib/string.c

include/linux/string.h

mm/util.c

基本的内核库函数
================

Linux内核提供了很多实用的基本函数。

位运算
------

该API在以下内核代码中:

include/asm-generic/bitops/instrumented-atomic.h

include/asm-generic/bitops/instrumented-non-atomic.h

include/asm-generic/bitops/instrumented-lock.h

位图运算
--------

该API在以下内核代码中:

lib/bitmap.c

include/linux/bitmap.h

include/linux/bitmap.h

include/linux/bitmap.h

lib/bitmap.c

lib/bitmap.c

include/linux/bitmap.h

命令行解析
----------

该API在以下内核代码中:

lib/cmdline.c

排序
----

该API在以下内核代码中:

lib/sort.c

lib/list_sort.c

文本检索
--------

该API在以下内核代码中:

lib/textsearch.c

lib/textsearch.c

include/linux/textsearch.h

Linux中的CRC和数学函数
======================


CRC函数
-------

*译注：CRC，Cyclic Redundancy Check，循环冗余校验*

该API在以下内核代码中:

lib/crc4.c

lib/crc7.c

lib/crc8.c

lib/crc16.c

lib/crc32.c

lib/crc-ccitt.c

lib/crc-itu-t.c

基数为2的对数和幂函数
---------------------

该API在以下内核代码中:

include/linux/log2.h

整数幂函数
----------

该API在以下内核代码中:

lib/math/int_pow.c

lib/math/int_sqrt.c

除法函数
--------

该API在以下内核代码中:

include/asm-generic/div64.h

include/linux/math64.h

lib/math/div64.c

lib/math/gcd.c

UUID/GUID
---------

该API在以下内核代码中:

lib/uuid.c

内核IPC设备
===========

IPC实用程序
-----------

该API在以下内核代码中:

ipc/util.c

FIFO 缓冲区
===========

kfifo接口
---------

该API在以下内核代码中:

include/linux/kfifo.h

转发接口支持
============

转发接口支持旨在为工具和设备提供一种有效的机制，将大量数据从内核空间
转发到用户空间。

转发接口
--------

该API在以下内核代码中:

kernel/relay.c

kernel/relay.c

模块支持
========

模块加载
--------

该API在以下内核代码中:

kernel/kmod.c

模块接口支持
------------

更多信息请参阅kernel/module/目录下的文件。

硬件接口
========


该API在以下内核代码中:

kernel/dma.c

资源管理
--------

该API在以下内核代码中:

kernel/resource.c

kernel/resource.c

MTRR处理
--------

该API在以下内核代码中:

arch/x86/kernel/cpu/mtrr/mtrr.c

安全框架
========

该API在以下内核代码中:

security/security.c

security/inode.c

审计接口
========

该API在以下内核代码中:

kernel/audit.c

kernel/auditsc.c

kernel/auditfilter.c

核算框架
========

该API在以下内核代码中:

kernel/acct.c

块设备
======

该API在以下内核代码中:

block/blk-core.c

block/blk-core.c

block/blk-map.c

block/blk-sysfs.c

block/blk-settings.c

block/blk-flush.c

block/blk-lib.c

block/blk-integrity.c

kernel/trace/blktrace.c

block/genhd.c

block/genhd.c

字符设备
========

该API在以下内核代码中:

fs/char_dev.c

时钟框架
========

时钟框架定义了编程接口，以支持系统时钟树的软件管理。该框架广泛用于系统级芯片（SOC）平
台，以支持电源管理和各种可能需要自定义时钟速率的设备。请注意，这些 “时钟”与计时或实
时时钟(RTC)无关，它们都有单独的框架。这些:c:type: `struct clk <clk>` 实例可用于管理
各种时钟信号，例如一个96理例如96MHz的时钟信号，该信号可被用于总线或外设的数据交换，或以
其他方式触发系统硬件中的同步状态机转换。

通过明确的软件时钟门控来支持电源管理：未使用的时钟被禁用，因此系统不会因为改变不在使用
中的晶体管的状态而浪费电源。在某些系统中，这可能是由硬件时钟门控支持的，其中时钟被门控
而不在软件中被禁用。芯片的部分，在供电但没有时钟的情况下，可能会保留其最后的状态。这种
低功耗状态通常被称为*保留模式*。这种模式仍然会产生漏电流，特别是在电路几何结构较细的情
况下，但对于CMOS电路来说，电能主要是随着时钟翻转而被消耗的。

电源感知驱动程序只有在其管理的设备处于活动使用状态时才会启用时钟。此外，系统睡眠状态通
常根据哪些时钟域处于活动状态而有所不同：“待机”状态可能允许从多个活动域中唤醒，而
"mem"（暂停到RAM）状态可能需要更全面地关闭来自高速PLL和振荡器的时钟，从而限制了可能
的唤醒事件源的数量。驱动器的暂停方法可能需要注意目标睡眠状态的系统特定时钟约束。

一些平台支持可编程时钟发生器。这些可以被各种外部芯片使用，如其他CPU、多媒体编解码器以
及对接口时钟有严格要求的设备。

该API在以下内核代码中:

include/linux/clk.h

同步原语
========

读-复制-更新（RCU）
-------------------

该API在以下内核代码中:

include/linux/rcupdate.h

kernel/rcu/tree.c

kernel/rcu/tree_exp.h

kernel/rcu/update.c

include/linux/srcu.h

kernel/rcu/srcutree.c

include/linux/rculist_bl.h

include/linux/rculist.h

include/linux/rculist_nulls.h

include/linux/rcu_sync.h

kernel/rcu/sync.c

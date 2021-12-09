.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/virt/paravirt_ops.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 陈飞杨 Feiyang Chen <chenfeiyang@loongson.cn>
 时奎亮 Alex Shi <alexs@kernel.org>

.. _cn_virt_paravirt_ops:

============
半虚拟化操作
============

Linux提供了对不同管理程序虚拟化技术的支持。历史上，为了支持不同的虚拟机超级管理器
（hypervisor，下文简称超级管理器），需要不同的二进制内核，这个限制已经被pv_ops移
除了。Linux pv_ops是一个虚拟化API，它能够支持不同的管理程序。它允许每个管理程序
优先于关键操作，并允许单一的内核二进制文件在所有支持的执行环境中运行，包括本机——没
有任何管理程序。

pv_ops提供了一组函数指针，代表了与低级关键指令和各领域高级功能相对应的操作。
pv-ops允许在运行时进行优化，在启动时对低级关键操作进行二进制修补。

pv_ops操作被分为三类:

- 简单的间接调用
   这些操作对应于高水平的函数，众所周知，间接调用的开销并不十分重要。

- 间接调用，允许用二进制补丁进行优化
   通常情况下，这些操作对应于低级别的关键指令。它们被频繁地调用，并且是对性能关
   键。开销是非常重要的。

- 一套用于手写汇编代码的宏程序
   手写的汇编代码（.S文件）也需要半虚拟化，因为它们包括敏感指令或其中的一些代
   码路径对性能非常关键。

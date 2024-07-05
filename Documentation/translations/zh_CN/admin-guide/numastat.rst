.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/admin-guide/numastat.rst
:Translator: Tao Zou <wodemia@linux.alibaba.com>


=======================
Numa策略命中/未命中统计
=======================

/sys/devices/system/node/node*/numastat

所有数据的单位都是页面。巨页有独立的计数器。

numa_hit、numa_miss和numa_foreign计数器反映了进程是否能够在他们偏好的节点上分配内存。
如果进程成功在偏好的节点上分配内存则在偏好的节点上增加numa_hit计数，否则在偏好的节点上增
加numa_foreign计数同时在实际内存分配的节点上增加numa_miss计数。

通常，偏好的节点是进程运行所在的CPU的本地节点，但是一些限制可以改变这一行为，比如内存策略，
因此同样有两个基于CPU本地节点的计数器。local_node和numa_hit类似，当在CPU所在的节点上分
配内存时增加local_node计数，other_node和numa_miss类似，当在CPU所在节点之外的其他节点
上成功分配内存时增加other_node计数。需要注意，没有和numa_foreign对应的计数器。

更多细节内容:

=============== ============================================================
numa_hit        一个进程想要从本节点分配内存并且成功。

numa_miss       一个进程想要从其他节点分配内存但是最终在本节点完成内存分配。

numa_foreign    一个进程想要在本节点分配内存但是最终在其他节点完成内存分配。

local_node      一个进程运行在本节点的CPU上并且从本节点上获得了内存。

other_node      一个进程运行在其他节点的CPU上但是在本节点上获得了内存。

interleave_hit  内存交叉分配策略下想要从本节点分配内存并且成功。
=============== ============================================================

你可以使用numactl软件包（http://oss.sgi.com/projects/libnuma/）中的numastat工具
来辅助阅读。需要注意，numastat工具目前只在有少量CPU的机器上运行良好。

需要注意，在包含无内存节点（一个节点有CPUs但是没有内存）的系统中numa_hit、numa_miss和
numa_foreign统计数据会被严重曲解。在当前的内核实现中，如果一个进程偏好一个无内存节点（即
进程正在该节点的一个本地CPU上运行），实际上会从距离最近的有内存节点中挑选一个作为偏好节点。
结果会导致相应的内存分配不会增加无内存节点上的numa_foreign计数器，并且会扭曲最近节点上的
numa_hit、numa_miss和numa_foreign统计数据。

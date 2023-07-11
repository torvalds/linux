.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/arch/arm64/perf.rst <perf_index>`

Translator: Bailu Lin <bailu.lin@vivo.com>

=============
Perf 事件属性
=============

:作者: Andrew Murray <andrew.murray@arm.com>
:日期: 2019-03-06

exclude_user
------------

该属性排除用户空间。

用户空间始终运行在 EL0，因此该属性将排除 EL0。


exclude_kernel
--------------

该属性排除内核空间。

打开 VHE 时内核运行在 EL2，不打开 VHE 时内核运行在 EL1。客户机
内核总是运行在 EL1。

对于宿主机，该属性排除 EL1 和 VHE 上的 EL2。

对于客户机，该属性排除 EL1。请注意客户机从来不会运行在 EL2。


exclude_hv
----------

该属性排除虚拟机监控器。

对于 VHE 宿主机该属性将被忽略，此时我们认为宿主机内核是虚拟机监
控器。

对于 non-VHE 宿主机该属性将排除 EL2，因为虚拟机监控器运行在 EL2
的任何代码主要用于客户机和宿主机的切换。

对于客户机该属性无效。请注意客户机从来不会运行在 EL2。


exclude_host / exclude_guest
----------------------------

这些属性分别排除了 KVM 宿主机和客户机。

KVM 宿主机可能运行在 EL0（用户空间），EL1（non-VHE 内核）和
EL2（VHE 内核 或 non-VHE 虚拟机监控器）。

KVM 客户机可能运行在 EL0（用户空间）和 EL1（内核）。

由于宿主机和客户机之间重叠的异常级别，我们不能仅仅依靠 PMU 的硬件异
常过滤机制-因此我们必须启用/禁用对于客户机进入和退出的计数。而这在
VHE 和 non-VHE 系统上表现不同。

对于 non-VHE 系统的 exclude_host 属性排除 EL2 - 在进入和退出客户
机时，我们会根据 exclude_host 和 exclude_guest 属性在适当的情况下
禁用/启用该事件。

对于 VHE 系统的 exclude_guest 属性排除 EL1，而对其中的 exclude_host
属性同时排除 EL0，EL2。在进入和退出客户机时，我们会适当地根据
exclude_host 和 exclude_guest 属性包括/排除 EL0。

以上声明也适用于在 not-VHE 客户机使用这些属性时，但是请注意客户机从
来不会运行在 EL2。


准确性
------

在 non-VHE 宿主机上，我们在 EL2 进入/退出宿主机/客户机的切换时启用/
关闭计数器 -但是在启用/禁用计数器和进入/退出客户机之间存在一段延时。
对于 exclude_host， 我们可以通过过滤 EL2 消除在客户机进入/退出边界
上用于计数客户机事件的宿主机事件计数器。但是当使用 !exclude_hv 时，
在客户机进入/退出有一个小的停电窗口无法捕获到宿主机的事件。

在 VHE 系统没有停电窗口。

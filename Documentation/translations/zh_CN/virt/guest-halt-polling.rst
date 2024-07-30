.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/virt/guest-halt-polling.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮 Alex Shi <alexs@kernel.org>

.. _cn_virt_guest-halt-polling:

========================================
客户机停机轮询机制（Guest halt polling）
========================================

cpuidle_haltpoll驱动，与haltpoll管理器一起，允许客户机vcpus在停机前轮询
一定的时间。

这为物理机侧的轮询提供了以下好处:

	1) 在执行轮询时，POLL标志被设置，这允许远程vCPU在执行唤醒时避免发送
	   IPI（以及处理IPI的相关成本）。

	2) 可以避免虚拟机退出的成本。

客户机侧轮询的缺点是，即使在物理机中的其他可运行任务中也会进行轮询。

其基本逻辑如下。一个全局值，即guest_halt_poll_ns，是由用户配置的，表示允
许轮询的最大时间量。这个值是固定的。

每个vcpu都有一个可调整的guest_halt_poll_ns（"per-cpu guest_halt_poll_ns"），
它由算法响应事件进行调整（解释如下）。

模块参数
========

haltpoll管理器有5个可调整的模块参数:

1) guest_halt_poll_ns:

轮询停机前执行的最大时间，以纳秒为单位。

默认值: 200000

2) guest_halt_poll_shrink:

当唤醒事件发生在全局的guest_halt_poll_ns之后，用于缩减每个CPU的guest_halt_poll_ns
的划分系数。

默认值: 2

3) guest_halt_poll_grow:

当事件发生在per-cpu guest_halt_poll_ns之后但在global guest_halt_poll_ns之前，
用于增长per-cpu guest_halt_poll_ns的乘法系数。

默认值: 2

4) guest_halt_poll_grow_start:

在系统空闲的情况下，每个cpu guest_halt_poll_ns最终达到零。这个值设置了增长时的
初始每cpu guest_halt_poll_ns。这个值可以从10000开始增加，以避免在最初的增长阶
段出现失误。:

10k, 20k, 40k, ... (例如，假设guest_halt_poll_grow=2).

默认值: 50000

5) guest_halt_poll_allow_shrink:

允许缩减的Bool参数。设置为N以避免它（一旦达到全局的guest_halt_poll_ns值，每CPU的
guest_halt_poll_ns将保持高位）。

默认值: Y

模块参数可以从sysfs文件中设置，在::

	/sys/module/haltpoll/parameters/

进一步说明
==========

- 在设置guest_halt_poll_ns参数时应该小心，因为一个大的值有可能使几乎是完全空闲机
  器上的cpu使用率达到100%。

.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/core-api/irq/irq-affinity.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_irq-affinity.rst:

==============
SMP IRQ 亲和性
==============

变更记录:
	- 作者：最初由Ingo Molnar <mingo@redhat.com>开始撰写
	- 后期更新维护： Max Krasnyansky <maxk@qualcomm.com>


/proc/irq/IRQ#/smp_affinity和/proc/irq/IRQ#/smp_affinity_list指定了哪些CPU能
够关联到一个给定的IRQ源，这两个文件包含了这些指定cpu的cpu位掩码(smp_affinity)和cpu列
表(smp_affinity_list)。它不允许关闭所有CPU， 同时如果IRQ控制器不支持中断请求亲和
(IRQ affinity)，那么所有cpu的默认值将保持不变(即关联到所有CPU).

/proc/irq/default_smp_affinity指明了适用于所有非激活IRQ的默认亲和性掩码。一旦IRQ被
分配/激活，它的亲和位掩码将被设置为默认掩码。然后可以如上所述改变它。默认掩码是0xffffffff。

下面是一个先将IRQ44(eth1)限制在CPU0-3上，然后限制在CPU4-7上的例子(这是一个8CPU的SMP box)

::

	[root@moon 44]# cd /proc/irq/44
	[root@moon 44]# cat smp_affinity
	ffffffff

	[root@moon 44]# echo 0f > smp_affinity
	[root@moon 44]# cat smp_affinity
	0000000f
	[root@moon 44]# ping -f h
	PING hell (195.4.7.3): 56 data bytes
	...
	--- hell ping statistics ---
	6029 packets transmitted, 6027 packets received, 0% packet loss
	round-trip min/avg/max = 0.1/0.1/0.4 ms
	[root@moon 44]# cat /proc/interrupts | grep 'CPU\|44:'
		CPU0       CPU1       CPU2       CPU3      CPU4       CPU5        CPU6       CPU7
	44:       1068       1785       1785       1783         0          0           0         0    IO-APIC-level  eth1

从上面一行可以看出，IRQ44只传递给前四个处理器（0-3）。
现在让我们把这个IRQ限制在CPU(4-7)。

::

	[root@moon 44]# echo f0 > smp_affinity
	[root@moon 44]# cat smp_affinity
	000000f0
	[root@moon 44]# ping -f h
	PING hell (195.4.7.3): 56 data bytes
	..
	--- hell ping statistics ---
	2779 packets transmitted, 2777 packets received, 0% packet loss
	round-trip min/avg/max = 0.1/0.5/585.4 ms
	[root@moon 44]# cat /proc/interrupts |  'CPU\|44:'
		CPU0       CPU1       CPU2       CPU3      CPU4       CPU5        CPU6       CPU7
	44:       1068       1785       1785       1783      1784       1069        1070       1069   IO-APIC-level  eth1

这次IRQ44只传递给最后四个处理器。
即CPU0-3的计数器没有变化。

下面是一个将相同的irq(44)限制在cpus 1024到1031的例子

::

	[root@moon 44]# echo 1024-1031 > smp_affinity_list
	[root@moon 44]# cat smp_affinity_list
	1024-1031

需要注意的是，如果要用位掩码来做这件事，就需要32个为0的位掩码来追踪其相关的一个。

.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/PCI/msi-howto.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



===========
MSI驱动指南
===========

:作者: Tom L Nguyen; Martine Silbermann; Matthew Wilcox

:版权: 2003, 2008 Intel Corporation

关于本指南
==========

本指南介绍了消息标记中断（MSI）的基本知识，使用MSI相对于传统中断机制的优势，如何
改变你的驱动程序以使用MSI或MSI-X，以及在设备不支持MSI时可以尝试的一些基本诊断方法。


什么是MSI?
==========

信息信号中断是指从设备写到一个特殊的地址，导致CPU接收到一个中断。

MSI能力首次在PCI 2.2中规定，后来在PCI 3.0中得到增强，允许对每个中断进行单独屏蔽。
MSI-X功能也随着PCI 3.0被引入。它比MSI支持每个设备更多的中断，并允许独立配置中断。

设备可以同时支持MSI和MSI-X，但一次只能启用一个。


为什么用MSI?
============

有三个原因可以说明为什么使用MSI比传统的基于针脚的中断有优势。

基于针脚的PCI中断通常在几个设备之间共享。为了支持这一点，内核必须调用每个与中断相
关的中断处理程序，这导致了整个系统性能的降低。MSI从不共享，所以这个问题不会出现。

当一个设备将数据写入内存，然后引发一个基于引脚的中断时，有可能在所有的数据到达内存
之前，中断就已经到达了（这在PCI-PCI桥后面的设备中变得更有可能）。为了确保所有的数
据已经到达内存中，中断处理程序必须在引发中断的设备上读取一个寄存器。PCI事务排序规
则要求所有的数据在返回寄存器的值之前到达内存。使用MSI可以避免这个问题，因为中断产
生的写入不能通过数据写入，所以当中断发生时，驱动程序知道所有的数据已经到达内存中。

PCI设备每个功能只能支持一个基于引脚的中断。通常情况下，驱动程序必须查询设备以找出
发生了什么事件，这就减慢了对常见情况的中断处理。有了MSI，设备可以支持更多的中断，
允许每个中断被专门用于不同的目的。一种可能的设计是给不经常发生的情况（如错误）提供
自己的中断，这使得驱动程序可以更有效地处理正常的中断处理路径。其他可能的设计包括给
网卡的每个数据包队列或存储控制器的每个端口提供一个中断。


如何使用MSI
===========

PCI设备被初始化为使用基于引脚的中断。设备驱动程序必须将设备设置为使用MSI或MSI-X。
并非所有的机器都能正确地支持MSI，对于这些机器，下面描述的API将简单地失败，设备将
继续使用基于引脚的中断。

加入内核对MSI的支持
-------------------

为了支持MSI或MSI-X，内核在构建时必须启用CONFIG_PCI_MSI选项。这个选项只在某些架
构上可用，而且它可能取决于其他一些选项的设置。例如，在x86上，你必须同时启用X86_UP_APIC
或SMP，才能看到CONFIG_PCI_MSI选项。

使用MSI
-------

大部分沉重的工作是在PCI层为驱动程序完成的。驱动程序只需要请求PCI层为这个设备设置
MSI功能。

要自动使用MSI或MSI-X中断向量，请使用以下函数::

  int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
		unsigned int max_vecs, unsigned int flags);

它为一个PCI设备分配最多至max_vecs的中断向量。它返回分配的向量数量或一个负的错误。
如果设备对最小数量的向量有要求，驱动程序可以传递一个min_vecs参数，设置为这个限制，
如果PCI核不能满足最小数量的向量，将返回-ENOSPC。

flags参数用来指定设备和驱动程序可以使用哪种类型的中断（PCI_IRQ_LEGACY, PCI_IRQ_MSI,
PCI_IRQ_MSIX）。一个方便的短语（PCI_IRQ_ALL_TYPES）也可以用来要求任何可能的中断类型。
如果PCI_IRQ_AFFINITY标志被设置，pci_alloc_irq_vectors()将把中断分散到可用的CPU上。

要获得传递给require_irq()和free_irq()的Linux IRQ号码和向量，请使用以下函数::

  int pci_irq_vector(struct pci_dev *dev, unsigned int nr);

在删除设备之前，应使用以下功能释放任何已分配的资源::

  void pci_free_irq_vectors(struct pci_dev *dev);

如果一个设备同时支持MSI-X和MSI功能，这个API将优先使用MSI-X，而不是MSI。MSI-X支
持1到2048之间的任何数量的中断。相比之下，MSI被限制为最多32个中断（而且必须是2的幂）。
此外，MSI中断向量必须连续分配，所以系统可能无法为MSI分配像MSI-X那样多的向量。在一
些平台上，MSI中断必须全部针对同一组CPU，而MSI-X中断可以全部针对不同的CPU。

如果一个设备既不支持MSI-X，也不支持MSI，它就会退回到一个传统的IRQ向量。

MSI或MSI-X中断的典型用法是分配尽可能多的向量，可能达到设备支持的极限。如果nvec大于
设备支持的数量，它将自动被限制在支持的限度内，所以没有必要事先查询支持的向量的数量。::

	nvec = pci_alloc_irq_vectors(pdev, 1, nvec, PCI_IRQ_ALL_TYPES)
	if (nvec < 0)
		goto out_err;

如果一个驱动程序不能或不愿意处理可变数量的MSI中断，它可以要求一个特定数量的中断，将该
数量作为“min_vecs“和“max_vecs“参数传递给pci_alloc_irq_vectors()函数。::

	ret = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto out_err;

上述请求类型的最臭名昭著的例子是为一个设备启用单一的MSI模式。它可以通过传递两个1作为
'min_vecs'和'max_vecs'来实现::

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto out_err;

一些设备可能不支持使用传统的线路中断，在这种情况下，驱动程序可以指定只接受MSI或MSI-X。::

	nvec = pci_alloc_irq_vectors(pdev, 1, nvec, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (nvec < 0)
		goto out_err;

传统API
-----------

以下用于启用和禁用MSI或MSI-X中断的旧API不应该在新代码中使用::

  pci_enable_msi()		/* deprecated */
  pci_disable_msi()		/* deprecated */
  pci_enable_msix_range()	/* deprecated */
  pci_enable_msix_exact()	/* deprecated */
  pci_disable_msix()		/* deprecated */

此外，还有一些API来提供支持的MSI或MSI-X向量的数量：pci_msi_vec_count()和
pci_msix_vec_count()。一般来说，应该避免使用这些方法，而是让pci_alloc_irq_vectors()
来限制向量的数量。如果你对向量的数量有合法的特殊用例，我们可能要重新审视这个决定，
并增加一个pci_nr_irq_vectors()助手，透明地处理MSI和MSI-X。

使用MSI时需要考虑的因素
-----------------------

自旋锁
~~~~~~

大多数设备驱动程序都有一个每的自旋锁，在中断处理程序中被占用。对于基于引脚的中断
或单一的MSI，没有必要禁用中断（Linux保证同一中断不会被重新输入）。如果一个设备
使用多个中断，驱动程序必须在锁被持有的时候禁用中断。如果设备发出一个不同的中断，
驱动程序将死锁，试图递归地获取自旋锁。这种死锁可以通过使用spin_lock_irqsave()
或spin_lock_irq()来避免，它们可以禁用本地中断并获取锁（见《不可靠的锁定指南》）。

如何判断一个设备上是否启用了MSI/MSI-X
-------------------------------------

使用“lspci -v“（以root身份）可能会显示一些具有“MSI“、“Message Signalled Interrupts“
或“MSI-X“功能的设备。这些功能中的每一个都有一个“启用“标志，后面是“+“（启用）
或“-“（禁用）。


MSI特性
=======

众所周知，一些PCI芯片组或设备不支持MSI。PCI协议栈提供了三种禁用MSI的方法:

1. 全局的
2. 在一个特定的桥后面的所有设备上
3. 在单一设备上

全局禁用MSI
-----------

一些主控芯片组根本无法正确支持MSI。如果我们幸运的话，制造商知道这一点，并在
ACPI FADT表中指明了它。在这种情况下，Linux会自动禁用MSI。有些板卡在表中没
有包括这一信息，因此我们必须自己检测它们。完整的列表可以在drivers/pci/quirks.c
中的quirk_disable_all_msi()函数附近找到。

如果你有一块有MSI问题的板子，你可以在内核命令行中传递pci=nomsi来禁用所有设
备上的MSI。你最好把问题报告给linux-pci@vger.kernel.org，包括完整的
“lspci -v“，这样我们就可以把这些怪癖添加到内核中。

禁用桥下的MSI
-------------

一些PCI桥接器不能在总线之间正确地路由MSI。在这种情况下，必须在桥接器后面的所
有设备上禁用MSI。

一些桥接器允许你通过改变PCI配置空间的一些位来启用MSI（特别是Hypertransport
芯片组，如nVidia nForce和Serverworks HT2000）。与主机芯片组一样，Linux大
多知道它们，如果可以的话，会自动启用MSI。如果你有一个Linux不知道的网桥，你可以
用你知道的任何方法在配置空间中启用MSI，然后通过以下方式在该网桥上启用MSI::

       echo 1 > /sys/bus/pci/devices/$bridge/msi_bus

其中$bridge是你所启用的桥的PCI地址（例如0000:00:0e.0）。

要禁用MSI，请回显0而不是1。改变这个值应该谨慎进行，因为它可能会破坏这个桥下面所
有设备的中断处理。

同样，请通知 linux-pci@vger.kernel.org 任何需要特殊处理的桥。

在单一设备上关闭MSIs
--------------------

众所周知，有些设备的MSI实现是有问题的。通常情况下，这是在单个设备驱动程序中处理的，
但偶尔也有必要用一个古怪的方法来处理。一些驱动程序有一个选项可以禁用MSI的使用。虽然
这对驱动程序的作者来说是一个方便的变通办法，但这不是一个好的做法，不应该被模仿。

寻找设备上MSI被禁用的原因
-------------------------

从以上三个部分，你可以看到有许多原因导致MSI没有在某个设备上被启用。你的第一步应该是
仔细检查你的dmesg以确定你的机器是否启用了MSI。你还应该检查你的.config以确定你已经
启用了CONFIG_PCI_MSI。

然后，“lspci -t“给出一个设备上面的网列表。读取 ``/sys/bus/pci/devices/*/msi_bus``
将告诉你MSI是否被启用（1）或禁用（0）。如果在任何属于PCI根和设备之间的桥的msi_bus
文件中发现0，说明MSI被禁用。

也需要检查设备驱动程序，看它是否支持MSI。例如，它可能包含对带有PCI_IRQ_MSI或
PCI_IRQ_MSIX标志的pci_alloc_irq_vectors（）的调用。


MSI(-X) APIs设备驱动程序列表
============================

PCI/MSI子系统有一个专门的C文件，用于其导出的设备驱动程序APIs - `drivers/pci/msi/api.c` 。
以下是导出的函数:

该API在以下内核代码中:

drivers/pci/msi/api.c

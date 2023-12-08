.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/PCI/pci.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



.. _cn_PCI_pci.rst:

===================
如何写Linux PCI驱动
===================

:作者: - Martin Mares <mj@ucw.cz>
          - Grant Grundler <grundler@parisc-linux.org>

PCI的世界是巨大的，而且充满了（大多数是不愉快的）惊喜。由于每个CPU架构实现了不同
的芯片组，并且PCI设备有不同的要求（呃，“特性”），结果是Linux内核中的PCI支持并不
像人们希望的那样简单。这篇短文试图向所有潜在的驱动程序作者介绍PCI设备驱动程序的
Linux APIs。

更完整的资源是Jonathan Corbet、Alessandro Rubini和Greg Kroah-Hartman的
《Linux设备驱动程序》第三版。LDD3可以免费获得（在知识共享许可下），网址是：
https://lwn.net/Kernel/LDD3/。



然而，请记住，所有的文档都会受到“维护不及时”的影响。如果事情没有按照这里描述的那
样进行，请参考源代码。

请将有关Linux PCI API的问题/评论/补丁发送到“Linux PCI”
<linux-pci@atrey.karlin.mff.cuni.cz> 邮件列表。


PCI驱动的结构体
===============
PCI驱动通过pci_register_driver()在系统中“发现”PCI设备。实际上，它是反过来的。
当PCI通用代码发现一个新设备时，具有匹配“描述”的驱动程序将被通知。下面是这方面的细
节。

pci_register_driver()将大部分探测设备的工作留给了PCI层，并支持设备的在线插入/移
除[从而在一个驱动中支持可热插拔的PCI、CardBus和Express-Card]。 pci_register_driver()
调用需要传入一个函数指针表，从而决定了驱动的高层结构体。

一旦驱动探测到一个PCI设备并取得了所有权，驱动通常需要执行以下初始化：

  - 启用设备
  - 请求MMIO/IOP资源
  - 设置DMA掩码大小（对于流式和一致的DMA）
  - 分配和初始化共享控制数据（pci_allocate_coherent()）
  - 访问设备配置空间(如果需要)
  - 注册IRQ处理程序(request_irq())
  - 初始化非PCI（即芯片的LAN/SCSI/等部分）
  - 启用DMA/处理引擎

当使用完设备后，也许需要卸载模块，驱动需要采取以下步骤:

  - 禁用设备产生的IRQ
  - 释放IRQ（free_irq()）
  - 停止所有DMA活动
  - 释放DMA缓冲区（包括一致性和数据流式）
  - 从其他子系统（例如scsi或netdev）上取消注册
  - 释放MMIO/IOP资源
  - 禁用设备

这些主题中的大部分都在下面的章节中有所涉及。其余的内容请参考LDD3或<linux/pci.h> 。

如果没有配置PCI子系统（没有设置 ``CONFIG_PCI`` ），下面描述的大多数PCI函数被定
义为内联函数，要么完全为空，要么只是返回一个适当的错误代码，以避免在驱动程序中出现
大量的 ``ifdef`` 。


调用pci_register_driver()
=========================

PCI设备驱动程序在初始化过程中调用 ``pci_register_driver()`` ，并提供一个指向
描述驱动程序的结构体的指针（ ``struct pci_driver`` ）：

该API在以下内核代码中:

include/linux/pci.h
pci_driver

ID表是一个由 ``struct pci_device_id`` 结构体成员组成的数组，以一个全零的成员
结束。一般来说，带有静态常数的定义是首选。

该API在以下内核代码中:

include/linux/mod_devicetable.h
pci_device_id

大多数驱动程序只需要 ``PCI_DEVICE()`` 或 ``PCI_DEVICE_CLASS()`` 来设置一个
pci_device_id表。

新的 ``PCI ID`` 可以在运行时被添加到设备驱动的 ``pci_ids`` 表中，如下所示::

  echo "vendor device subvendor subdevice class class_mask driver_data" > \
  /sys/bus/pci/drivers/{driver}/new_id

所有字段都以十六进制值传递（没有前置0x）。供应商和设备字段是强制性的，其他字段是可
选的。用户只需要传递必要的可选字段：

  - subvendor和subdevice字段默认为PCI_ANY_ID (FFFFFFF)。
  - class和classmask字段默认为0
  - driver_data默认为0UL。
  - override_only字段默认为0。

请注意， ``driver_data`` 必须与驱动程序中定义的任何一个 ``pci_device_id`` 条
目所使用的值相匹配。如果所有的 ``pci_device_id`` 成员都有一个非零的driver_data
值，这使得driver_data字段是强制性的。

一旦添加，驱动程序探测程序将被调用，以探测其（新更新的） ``pci_ids`` 列表中列出的
任何无人认领的PCI设备。

当驱动退出时，它只是调用 ``pci_unregister_driver()`` ，PCI层会自动调用驱动处理
的所有设备的移除钩子。


驱动程序功能/数据的“属性”
-------------------------

请在适当的地方标记初始化和清理函数（相应的宏在<linux/init.h>中定义）：

	======		==============================================
	__init		初始化代码。在驱动程序初始化后被抛弃。
	__exit		退出代码。对于非模块化的驱动程序来说是忽略的。
	======		==============================================

关于何时/何地使用上述属性的提示：

	- module_init()/module_exit()函数（以及所有仅由这些函数调用的初始化函数）应该被标记

	- 为__init/__exit。

	- 不要标记pci_driver结构体。

	- 如果你不确定应该使用哪种标记，请不要标记一个函数。不标记函数比标记错误的函数更好。


如何手动搜索PCI设备
===================

PCI驱动最好有一个非常好的理由不使用 ``pci_register_driver()`` 接口来搜索PCI设备。
PCI设备被多个驱动程序控制的主要原因是一个PCI设备实现了几个不同的HW服务。例如，组合的
串行/并行端口/软盘控制器。

可以使用以下结构体进行手动搜索：

通过供应商和设备ID进行搜索::

	struct pci_dev *dev = NULL;
	while (dev = pci_get_device(VENDOR_ID, DEVICE_ID, dev))
		configure_device(dev);

按类别ID搜索（以类似的方式迭代）::

	pci_get_class(CLASS_ID, dev)

通过供应商/设备和子系统供应商/设备ID进行搜索::

	pci_get_subsys(VENDOR_ID,DEVICE_ID, SUBSYS_VENDOR_ID, SUBSYS_DEVICE_ID, dev).

你可以使用常数 ``PCI_ANY_ID`` 作为 ``VENDOR_ID`` 或 ``DEVICE_ID`` 的通
配符替代。例如，这允许搜索来自一个特定供应商的任何设备。

这些函数是热拔插安全的。它们会增加它们所返回的 ``pci_dev`` 的参考计数。你最终
必须通过调用 ``pci_dev_put()`` 来减少这些设备上的参考计数（可能在模块卸载时）。


设备初始化步骤
==============

正如介绍中所指出的，大多数PCI驱动需要以下步骤进行设备初始化：

  - 启用设备
  - 请求MMIO/IOP资源
  - 设置DMA掩码大小（对于流式和一致的DMA）
  - 分配和初始化共享控制数据（pci_allocate_coherent()）
  - 访问设备配置空间(如果需要)
  - 注册IRQ处理程序（request_irq()）
  - 初始化non-PCI（即芯片的LAN/SCSI/等部分）
  - 启用DMA/处理引擎

驱动程序可以在任何时候访问PCI配置空间寄存器。（嗯，几乎如此。当运行BIST时，配置
空间可以消失......但这只会导致PCI总线主控中止，读取配置将返回垃圾值）。）


启用PCI设备
-----------
在接触任何设备寄存器之前，驱动程序需要通过调用 ``pci_enable_device()`` 启用
PCI设备。这将:

  - 唤醒处于暂停状态的设备。
  - 分配设备的I/O和内存区域（如果BIOS没有这样做）。
  - 分配一个IRQ（如果BIOS没有）。

.. note::
   pci_enable_device() 可能失败，检查返回值。

.. warning::
   OS BUG：在启用这些资源之前，我们没有检查资源分配情况。如果我们在调用
   之前调用pci_request_resources()，这个顺序会更合理。目前，当两个设备被分配
   了相同的范围时，设备驱动无法检测到这个错误。这不是一个常见的问题，不太可能很快
   得到修复。

   这个问题之前已经讨论过了，但从2.6.19开始没有改变：
   https://lore.kernel.org/r/20060302180025.GC28895@flint.arm.linux.org.uk/


pci_set_master()将通过设置PCI_COMMAND寄存器中的总线主控位来启用DMA。
``pci_clear_master()`` 将通过清除总线主控位来禁用DMA，它还修复了延迟计时器的
值，如果它被BIOS设置成假的。

如果PCI设备可以使用 ``PCI Memory-Write-Invalidate`` 事务，请调用 ``pci_set_mwi()`` 。
这将启用 ``Mem-Wr-Inval`` 的 ``PCI_COMMAND`` 位，也确保缓存行大小寄存器被正确设置。检
查 ``pci_set_mwi()`` 的返回值，因为不是所有的架构或芯片组都支持 ``Memory-Write-Invalidate`` 。
另外，如果 ``Mem-Wr-Inval`` 是好的，但不是必须的，可以调用 ``pci_try_set_mwi()`` ，让
系统尽最大努力来启用 ``Mem-Wr-Inval`` 。


请求MMIO/IOP资源
----------------
内存（MMIO）和I/O端口地址不应该直接从PCI设备配置空间中读取。使用 ``pci_dev`` 结构体
中的值，因为PCI “总线地址”可能已经被arch/chip-set特定的内核支持重新映射为“主机物理”
地址。

参见io_mapping函数，了解如何访问设备寄存器或设备内存。

设备驱动需要调用 ``pci_request_region()`` 来确认没有其他设备已经在使用相同的地址
资源。反之，驱动应该在调用 ``pci_disable_device()`` 之后调用 ``pci_release_region()`` 。
这个想法是为了防止两个设备在同一地址范围内发生冲突。

.. tip::
   见上面的操作系统BUG注释。目前(2.6.19)，驱动程序只能在调用pci_enable_device()
   后确定MMIO和IO端口资源的可用性。

``pci_request_region()`` 的通用风格是 ``request_mem_region()`` （用于MMIO
范围）和 ``request_region()`` （用于IO端口范围）。对于那些不被 "正常 "PCI BAR描
述的地址资源，使用这些方法。

也请看下面的 ``pci_request_selected_regions()`` 。


设置DMA掩码大小
---------------
.. note::
   如果下面有什么不明白的地方，请参考使用通用设备的动态DMA映射。本节只是提醒大家，
   驱动程序需要说明设备的DMA功能，并不是DMA接口的权威来源。

虽然所有的驱动程序都应该明确指出PCI总线主控的DMA功能（如32位或64位），但对于流式
数据来说，具有超过32位总线主站功能的设备需要驱动程序通过调用带有适当参数的
``dma_set_mask()`` 来“注册”这种功能。一般来说，在系统RAM高于4G物理地址的情
况下，这允许更有效的DMA。

所有PCI-X和PCIe兼容设备的驱动程序必须调用 ``dma_set_mask()`` ，因为它们
是64位DMA设备。

同样，如果设备可以通过调用 ``dma_set_coherent_mask()`` 直接寻址到
4G物理地址以上的系统RAM中的“一致性内存”，那么驱动程序也必须“注册”这种功能。同
样，这包括所有PCI-X和PCIe兼容设备的驱动程序。许多64位“PCI”设备（在PCI-X之前）
和一些PCI-X设备对有效载荷（“流式”）数据具有64位DMA功能，但对控制（“一致性”）数
据则没有。


设置共享控制数据
----------------
一旦DMA掩码设置完毕，驱动程序就可以分配“一致的”（又称共享的）内存。参见使用通
用设备的动态DMA映射，了解DMA API的完整描述。本节只是提醒大家，需要在设备上启
用DMA之前完成。


初始化设备寄存器
----------------
一些驱动程序需要对特定的“功能”字段进行编程，或对其他“供应商专用”寄存器进行初始
化或重置。例如，清除挂起的中断。


注册IRQ处理函数
---------------
虽然调用 ``request_irq()`` 是这里描述的最后一步，但这往往只是初始化设备的另
一个中间步骤。这一步通常可以推迟到设备被打开使用时进行。

所有IRQ线的中断处理程序都应该用 ``IRQF_SHARED`` 注册，并使用devid将IRQ映射
到设备（记住，所有的PCI IRQ线都可以共享）。

``request_irq()`` 将把一个中断处理程序和设备句柄与一个中断号联系起来。历史上，
中断号码代表从PCI设备到中断控制器的IRQ线。在MSI和MSI-X中（更多内容见下文），中
断号是CPU的一个“向量”。

``request_irq()`` 也启用中断。在注册中断处理程序之前，请确保设备是静止的，并且
没有任何中断等待。

MSI和MSI-X是PCI功能。两者都是“消息信号中断”，通过向本地APIC的DMA写入来向CPU发
送中断。MSI和MSI-X的根本区别在于如何分配多个“向量”。MSI需要连续的向量块，而
MSI-X可以分配几个单独的向量。

在调用 ``request_irq()`` 之前，可以通过调用 ``pci_alloc_irq_vectors()``
的PCI_IRQ_MSI和/或PCI_IRQ_MSIX标志来启用MSI功能。这将导致PCI支持将CPU向量数
据编程到PCI设备功能寄存器中。许多架构、芯片组或BIOS不支持MSI或MSI-X，调用
``pci_alloc_irq_vectors`` 时只使用PCI_IRQ_MSI和PCI_IRQ_MSIX标志会失败，
所以尽量也要指定 ``PCI_IRQ_LEGACY`` 。

对MSI/MSI-X和传统INTx有不同中断处理程序的驱动程序应该在调用
``pci_alloc_irq_vectors`` 后根据 ``pci_dev``结构体中的 ``msi_enabled``
和 ``msix_enabled`` 标志选择正确的处理程序。

使用MSI有（至少）两个真正好的理由：

1) 根据定义，MSI是一个排他性的中断向量。这意味着中断处理程序不需要验证其设备是
   否引起了中断。

2) MSI避免了DMA/IRQ竞争条件。到主机内存的DMA被保证在MSI交付时对主机CPU是可
   见的。这对数据一致性和避

3) 免控制数据过期都很重要。这个保证允许驱动程序省略MMIO读取，以刷新DMA流。

参见drivers/infiniband/hw/mthca/或drivers/net/tg3.c了解MSI/MSI-X的使
用实例。


PCI设备关闭
===========

当一个PCI设备驱动程序被卸载时，需要执行以下大部分步骤:

  - 禁用设备产生的IRQ
  - 释放IRQ（free_irq()）
  - 停止所有DMA活动
  - 释放DMA缓冲区（包括流式和一致的）
  - 从其他子系统（例如scsi或netdev）上取消注册
  - 禁用设备对MMIO/IO端口地址的响应
  - 释放MMIO/IO端口资源


停止设备上的IRQ
---------------
如何做到这一点是针对芯片/设备的。如果不这样做，如果（也只有在）IRQ与另一个设备
共享，就会出现“尖叫中断”的可能性。

当共享的IRQ处理程序被“解钩”时，使用同一IRQ线的其余设备仍然需要启用该IRQ。因此，
如果“脱钩”的设备断言IRQ线，假设它是其余设备中的一个断言IRQ线，系统将作出反应。
由于其他设备都不会处理这个IRQ，系统将“挂起”，直到它决定这个IRQ不会被处理并屏蔽
这个IRQ（100,000次之后）。一旦共享的IRQ被屏蔽，其余设备将停止正常工作。这不是
一个好事情。

这是使用MSI或MSI-X的另一个原因，如果它可用的话。MSI和MSI-X被定义为独占中断，
因此不容易受到“尖叫中断”问题的影响。

释放IRQ
-------
一旦设备被静止（不再有IRQ），就可以调用free_irq()。这个函数将在任何待处理
的IRQ被处理后返回控制，从该IRQ上“解钩”驱动程序的IRQ处理程序，最后如果没有人
使用该IRQ，则释放它。


停止所有DMA活动
---------------
在试图取消分配DMA控制数据之前，停止所有的DMA操作是非常重要的。如果不这样做，
可能会导致内存损坏、挂起，在某些芯片组上还会导致硬崩溃。

在停止IRQ后停止DMA可以避免IRQ处理程序可能重新启动DMA引擎的竞争。

虽然这个步骤听起来很明显，也很琐碎，但过去有几个“成熟”的驱动程序没有做好这个
步骤。


释放DMA缓冲区
-------------
一旦DMA被停止，首先要清理流式DMA。即取消数据缓冲区的映射，如果有的话，将缓
冲区返回给“上游”所有者。

然后清理包含控制数据的“一致的”缓冲区。

关于取消映射接口的细节，请参见Documentation/core-api/dma-api.rst。


从其他子系统取消注册
--------------------
大多数低级别的PCI设备驱动程序支持其他一些子系统，如USB、ALSA、SCSI、NetDev、
Infiniband等。请确保你的驱动程序没有从其他子系统中丢失资源。如果发生这种情况，
典型的症状是当子系统试图调用已经卸载的驱动程序时，会出现Oops（恐慌）。


禁止设备对MMIO/IO端口地址做出响应
---------------------------------
io_unmap() MMIO或IO端口资源，然后调用pci_disable_device()。
这与pci_enable_device()对称相反。
在调用pci_disable_device()后不要访问设备寄存器。


释放MMIO/IO端口资源
-------------------
调用pci_release_region()来标记MMIO或IO端口范围为可用。
如果不这样做，通常会导致无法重新加载驱动程序。




如何访问PCI配置空间
===================

你可以使用 `pci_(read|write)_config_(byte|word|dword)` 来访问由
`struct pci_dev *` 表示的设备的配置空间。所有这些函数在成功时返回0，或者返回一个
错误代码（ `PCIBIOS_...` ），这个错误代码可以通过pcibios_strerror翻译成文本字
符串。大多数驱动程序希望对有效的PCI设备的访问不会失败。

如果你没有可用的pci_dev结构体，你可以调用
`pci_bus_(read|write)_config_(byte|word|dword)` 来访问一个给定的设备和该总
线上的功能。

如果你访问配置头的标准部分的字段，请使用<linux/pci.h>中声明的位置和位的符号名称。

如果你需要访问扩展的PCI功能寄存器，只要为特定的功能调用pci_find_capability()，
它就会为你找到相应的寄存器块。


其它有趣的函数
==============

=============================	=================================================
pci_get_domain_bus_and_slot()   找到与给定的域、总线和槽以及编号相对应的pci_dev。
                                如果找到该设备，它的引用计数就会增加。
pci_set_power_state()           设置PCI电源管理状态（0=D0 ... 3=D3
pci_find_capability()           在设备的功能列表中找到指定的功能
pci_resource_start()            返回一个给定的PCI区域的总线起始地址
pci_resource_end()              返回给定PCI区域的总线末端地址
pci_resource_len()              返回一个PCI区域的字节长度
pci_set_drvdata()               为一个pci_dev设置私有驱动数据指针
pci_get_drvdata()               返回一个pci_dev的私有驱动数据指针
pci_set_mwi()                   启用设备内存写无效
pci_clear_mwi()                 关闭设备内存写无效
=============================	=================================================


杂项提示
========

当向用户显示PCI设备名称时(例如，当驱动程序想告诉用户它找到了什么卡时)，请使
用pci_name(pci_dev)。

始终通过对pci_dev结构体的指针来引用PCI设备。所有的PCI层函数都使用这个标识，
它是唯一合理的标识。除了非常特殊的目的，不要使用总线/插槽/功能号————在有多个
主总线的系统上，它们的语义可能相当复杂。

不要试图在你的驱动程序中开启快速寻址周期写入功能。总线上的所有设备都需要有这样
的功能，所以这需要由平台和通用代码来处理，而不是由单个驱动程序来处理。


供应商和设备标识
================

不要在include/linux/pci_ids.h中添加新的设备或供应商ID，除非它们是在多个驱
动程序中共享。如果有需要的话，你可以在你的驱动程序中添加私有定义，或者直接使用
普通的十六进制常量。

设备ID是任意的十六进制数字（厂商控制），通常只在一个地方使用，即pci_device_id
表。

请务必提交新的供应商/设备ID到https://pci-ids.ucw.cz/。在
https://github.com/pciutils/pciids，有一个pci.ids文件的镜像。


过时的函数
==========

当你试图将一个旧的驱动程序移植到新的PCI接口时，你可能会遇到几个函数。它们不再存
在于内核中，因为它们与热插拔或PCI域或具有健全的锁不兼容。

=================	===================================
pci_find_device()	被pci_get_device()取代
pci_find_subsys()	被pci_get_subsys()取代
pci_find_slot()		被pci_get_domain_bus_and_slot()取代
pci_get_slot()		被pci_get_domain_bus_and_slot()取代
=================	===================================

另一种方法是传统的PCI设备驱动，即走PCI设备列表。这仍然是可能的，但不鼓励这样做。


MMIO空间和“写通知”
==================

将驱动程序从使用I/O端口空间转换为使用MMIO空间，通常需要一些额外的改变。具体来说，
需要处理“写通知”。许多驱动程序（如tg3，acenic，sym53c8xx_2）已经做了这个。I/O
端口空间保证写事务在CPU继续之前到达PCI设备。对MMIO空间的写入允许CPU在事务到达PCI
设备之前继续。HW weenies称这为“写通知”，因为在事务到达目的地之前，写的完成被“通知”
给CPU。

因此，对时间敏感的代码应该添加readl()，CPU在做其他工作之前应该等待。经典的“位脉冲”
序列对I/O端口空间很有效::

       for (i = 8; --i; val >>= 1) {
               outb(val & 1, ioport_reg);      /* 置位 */
               udelay(10);
       }

对MMIO空间来说，同样的顺序应该是::

       for (i = 8; --i; val >>= 1) {
               writeb(val & 1, mmio_reg);      /* 置位 */
               readb(safe_mmio_reg);           /* 刷新写通知 */
               udelay(10);
       }

重要的是， ``safe_mmio_reg`` 不能有任何干扰设备正确操作的副作用。

另一种需要注意的情况是在重置PCI设备时。使用PCI配置空间读数来刷新writeel()。如果预期
PCI设备不响应readl()，这将在所有平台上优雅地处理PCI主控器的中止。大多数x86平台将允许
MMIO读取主控中止（又称“软失败”），并返回垃圾（例如~0）。但许多RISC平台会崩溃（又称“硬失败”）。

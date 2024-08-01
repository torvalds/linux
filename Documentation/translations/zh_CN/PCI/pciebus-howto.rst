.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/PCI/pciebus-howto.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



.. _cn_pciebus-howto:

===========================
PCI Express端口总线驱动指南
===========================

:作者: Tom L Nguyen tom.l.nguyen@intel.com 11/03/2004
:版权: |copy| 2004 Intel Corporation

关于本指南
==========

本指南介绍了PCI Express端口总线驱动程序的基本知识，并提供了如何使服务驱
动程序在PCI Express端口总线驱动程序中注册/取消注册的介绍。


什么是PCI Express端口总线驱动程序
=================================

一个PCI Express端口是一个逻辑的PCI-PCI桥结构。有两种类型的PCI Express端
口：根端口和交换端口。根端口从PCI Express根综合体发起一个PCI Express链接，
交换端口将PCI Express链接连接到内部逻辑PCI总线。交换机端口，其二级总线代表
交换机的内部路由逻辑，被称为交换机的上行端口。交换机的下行端口是从交换机的内部
路由总线桥接到代表来自PCI Express交换机的下游PCI Express链接的总线。

一个PCI Express端口可以提供多达四个不同的功能，在本文中被称为服务，这取决于
其端口类型。PCI Express端口的服务包括本地热拔插支持（HP）、电源管理事件支持（PME）、
高级错误报告支持（AER）和虚拟通道支持（VC）。这些服务可以由一个复杂的驱动程序
处理，也可以单独分布并由相应的服务驱动程序处理。

为什么要使用PCI Express端口总线驱动程序？
=========================================

在现有的Linux内核中，Linux设备驱动模型允许一个物理设备只由一个驱动处理。
PCI Express端口是一个具有多个不同服务的PCI-PCI桥设备。为了保持一个干净和简
单的解决方案，每个服务都可以有自己的软件服务驱动。在这种情况下，几个服务驱动将
竞争一个PCI-PCI桥设备。例如，如果PCI Express根端口的本机热拔插服务驱动程序
首先被加载，它就会要求一个PCI-PCI桥根端口。因此，内核不会为该根端口加载其他服
务驱动。换句话说，使用当前的驱动模型，不可能让多个服务驱动同时加载并运行在
PCI-PCI桥设备上。

为了使多个服务驱动程序同时运行，需要有一个PCI Express端口总线驱动程序，它管
理所有填充的PCI Express端口，并根据需要将所有提供的服务请求分配给相应的服务
驱动程序。下面列出了使用PCI Express端口总线驱动程序的一些关键优势:

  - 允许在一个PCI-PCI桥接端口设备上同时运行多个服务驱动。

  - 允许以独立的分阶段方式实施服务驱动程序。

  - 允许一个服务驱动程序在多个PCI-PCI桥接端口设备上运行。

  - 管理和分配PCI-PCI桥接端口设备的资源给要求的服务驱动程序。

配置PCI Express端口总线驱动程序与服务驱动程序
=============================================

将PCI Express端口总线驱动支持纳入内核
-------------------------------------

包括PCI Express端口总线驱动程序取决于内核配置中是否包含PCI Express支持。当内核
中的PCI Express支持被启用时，内核将自动包含PCI Express端口总线驱动程序作为内核
驱动程序。

启用服务驱动支持
----------------

PCI设备驱动是基于Linux设备驱动模型实现的。所有的服务驱动都是PCI设备驱动。如上所述，
一旦内核加载了PCI Express端口总线驱动程序，就不可能再加载任何服务驱动程序。为了满
足PCI Express端口总线驱动程序模型，需要对现有的服务驱动程序进行一些最小的改变，其
对现有的服务驱动程序的功能没有影响。

服务驱动程序需要使用下面所示的两个API，将其服务注册到PCI Express端口总线驱动程
序中（见第5.2.1和5.2.2节）。在调用这些API之前，服务驱动程序必须初始化头文件
/include/linux/pcieport_if.h中的pcie_port_service_driver数据结构。如果不这
样做，将导致身份不匹配，从而使PCI Express端口总线驱动程序无法加载服务驱动程序。

pcie_port_service_register
~~~~~~~~~~~~~~~~~~~~~~~~~~
::

  int pcie_port_service_register(struct pcie_port_service_driver *new)

这个API取代了Linux驱动模型的 pci_register_driver API。一个服务驱动应该总是在模
块启动时调用 pcie_port_service_register。请注意，在服务驱动被加载后，诸如
pci_enable_device(dev) 和 pci_set_master(dev) 的调用不再需要，因为这些调用由
PCI端口总线驱动执行。

pcie_port_service_unregister
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
::

  void pcie_port_service_unregister(struct pcie_port_service_driver *new)

pcie_port_service_unregister取代了Linux驱动模型的pci_unregister_driver。当一
个模块退出时，它总是被服务驱动调用。

示例代码
~~~~~~~~

下面是服务驱动代码示例，用于初始化端口服务的驱动程序数据结构。
::

  static struct pcie_port_service_id service_id[] = { {
    .vendor = PCI_ANY_ID,
    .device = PCI_ANY_ID,
    .port_type = PCIE_RC_PORT,
    .service_type = PCIE_PORT_SERVICE_AER,
    }, { /* end: all zeroes */ }
  };

  static struct pcie_port_service_driver root_aerdrv = {
    .name		= (char *)device_name,
    .id_table	= service_id,

    .probe		= aerdrv_load,
    .remove		= aerdrv_unload,

    .suspend	= aerdrv_suspend,
    .resume		= aerdrv_resume,
  };

下面是一个注册/取消注册服务驱动的示例代码。
::

  static int __init aerdrv_service_init(void)
  {
    int retval = 0;

    retval = pcie_port_service_register(&root_aerdrv);
    if (!retval) {
      /*
      * FIX ME
      */
    }
    return retval;
  }

  static void __exit aerdrv_service_exit(void)
  {
    pcie_port_service_unregister(&root_aerdrv);
  }

  module_init(aerdrv_service_init);
  module_exit(aerdrv_service_exit);

可能的资源冲突
==============

由于PCI-PCI桥接端口设备的所有服务驱动被允许同时运行，下面列出了一些可能的资源冲突和
建议的解决方案。

MSI 和 MSI-X 向量资源
---------------------

一旦设备上的MSI或MSI-X中断被启用，它就会一直保持这种模式，直到它们再次被禁用。由于同
一个PCI-PCI桥接端口的服务驱动程序共享同一个物理设备，如果一个单独的服务驱动程序启用或
禁用MSI/MSI-X模式，可能会导致不可预知的行为。

为了避免这种情况，所有的服务驱动程序都不允许在其设备上切换中断模式。PCI Express端口
总线驱动程序负责确定中断模式，这对服务驱动程序来说应该是透明的。服务驱动程序只需要知道
分配给结构体pcie_device的字段irq的向量IRQ，当PCI Express端口总线驱动程序探测每
个服务驱动程序时，它被传入。服务驱动应该使用（struct pcie_device*）dev->irq来调用
request_irq/free_irq。此外，中断模式被存储在struct pcie_device的interrupt_mode
字段中。

PCI内存/IO映射的区域
--------------------

PCI Express电源管理（PME）、高级错误报告（AER）、热插拔（HP）和虚拟通道（VC）的服务
驱动程序访问PCI Express端口的PCI配置空间。在所有情况下，访问的寄存器是相互独立的。这
个补丁假定所有的服务驱动程序都会表现良好，不会覆盖其他服务驱动程序的配置设置。

PCI配置寄存器
-------------

每个服务驱动都在自己的功能结构体上运行PCI配置操作，除了PCI Express功能结构体，其中根控制
寄存器和设备控制寄存器是在PME和AER之间共享。这个补丁假定所有的服务驱动都会表现良好，不会
覆盖其他服务驱动的配置设置。

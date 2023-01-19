.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/userspace-api/accelerators/ocxl.rst

:翻译:

 李睿 Rui Li <me@lirui.org>

=====================================
OpenCAPI （开放相干加速器处理器接口）
=====================================

*OpenCAPI: Open Coherent Accelerator Processor Interface*

OpenCAPI是处理器和加速器之间的一个接口，致力于达到低延迟和高带宽。该规范
由 `OpenCAPI Consortium <http://opencapi.org/>`_ 开发。

它允许加速器（可以是FPGA、ASIC等）使用虚拟地址连贯地访问主机内存。一个OpenCAPI
设备也可以托管它自己的内存，并可以由主机访问。

OpenCAPI在Linux中称为“ocxl”，它作为“cxl”（用于powerpc的IBM CAPI接口的驱动）的
开放、处理器无关的演进，这么命名是为了避免与ISDN CAPI子系统相混淆。


高层视角
========

OpenCAPI定义了一个在物理链路层上实现的数据链路层（TL）和传输层（TL）。任何
实现DL和TL的处理器或者设备都可以开始共享内存。

::

  +-----------+                         +-------------+
  |           |                         |             |
  |           |                         | Accelerated |
  | Processor |                         |  Function   |
  |           |  +--------+             |    Unit     |  +--------+
  |           |--| Memory |             |    (AFU)    |--| Memory |
  |           |  +--------+             |             |  +--------+
  +-----------+                         +-------------+
       |                                       |
  +-----------+                         +-------------+
  |    TL     |                         |    TLX      |
  +-----------+                         +-------------+
       |                                       |
  +-----------+                         +-------------+
  |    DL     |                         |    DLX      |
  +-----------+                         +-------------+
       |                                       |
       |                   PHY                 |
       +---------------------------------------+

  Processor：处理器
  Memory：内存
  Accelerated Function Unit：加速函数单元



设备发现
========

OpenCAPI依赖一个在设备上实现的与PCI类似的配置空间。因此主机可以通过查询
配置空间来发现AFU。

OpenCAPI设备在Linux中被当作类PCI设备（有一些注意事项）。固件需要对硬件进行
抽象，就好像它是一个PCI链路。许多已有的PCI架构被重用：在模拟标准PCI时，
设备被扫描并且BAR（基址寄存器）被分配。像“lspci”的命令因此可以被用于查看
哪些设备可用。

配置空间定义了可以在物理适配器上可以被找到的AFU，比如它的名字、支持多少内
存上下文、内存映射IO（MMIO）区域的大小等。



MMIO
====

OpenCAPI为每个AFU定义了两个MMIO区域：

* 全局MMIO区域，保存和整个AFU相关的寄存器。
* 每个进程的MMIO区域，对于每个上下文固定大小。



AFU中断
=======

OpenCAPI拥有AFU向主机进程发送中断的可能性。它通过定义在传输层的“intrp_req”
来完成，指定一个定义中断的64位对象句柄。

驱动允许一个进程分配中断并获取可以传递给AFU的64位对象句柄。



字符设备
========

驱动为每个在物理设备上发现的AFU创建一个字符设备。一个物理设备可能拥有多个
函数，一个函数可以拥有多个AFU。不过编写这篇文档之时，只对导出一个AFU的设备
测试过。

字符设备可以在 /dev/ocxl/ 中被找到，其命名为：
/dev/ocxl/<AFU 名称>.<位置>.<索引>

<AFU 名称> 是一个最长20个字符的名称，和在AFU配置空间中找到的相同。
<位置>由驱动添加，可在系统有不止一个相同的OpenCAPI设备时帮助区分设备。
<索引>也是为了在少见情况下帮助区分AFU，即设备携带多个同样的AFU副本时。



Sysfs类
=======

添加了代表AFU的ocxl类。查看/sys/class/ocxl。布局在
Documentation/ABI/testing/sysfs-class-ocxl 中描述。



用户API
=======

打开
----

基于在配置空间中找到的AFU定义，AFU可能支持在多个内存上下文中工作，这种情况
下相关的字符设备可以被不同进程多次打开。


ioctl
-----

OCXL_IOCTL_ATTACH:

  附加调用进程的内存上下文到AFU，以允许AFU访问其内存。

OCXL_IOCTL_IRQ_ALLOC:

  分配AFU中断，返回标识符。

OCXL_IOCTL_IRQ_FREE:

  释放之前分配的AFU中断。

OCXL_IOCTL_IRQ_SET_FD:

  将一个事件文件描述符和AFU中断关联，因此用户进程可以在AFU发送中断时收到通
  知。

OCXL_IOCTL_GET_METADATA:

  从卡中获取配置信息，比如内存映射IO区域的大小、AFU版本和当前上下文的进程
  地址空间ID（PASID）。

OCXL_IOCTL_ENABLE_P9_WAIT:

  允许AFU唤醒执行“等待”的用户空间进程。返回信息给用户空间，允许其配置AFU。
  注意这只在POWER9上可用。

OCXL_IOCTL_GET_FEATURES:

  报告用户空间可用的影响OpenCAPI的CPU特性。


mmap
----

一个进程可以mmap每个进程的MMIO区域来和AFU交互。

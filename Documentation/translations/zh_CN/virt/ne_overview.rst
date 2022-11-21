.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/virt/ne_overview.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 时奎亮 Alex Shi <alexs@kernel.org>

.. _cn_virt_ne_overview:

==============
Nitro Enclaves
==============

概述
====

Nitro Enclaves（NE）是亚马逊弹性计算云（EC2）的一项新功能，允许客户在EC2实
例中划分出孤立的计算环境[1]。

例如，一个处理敏感数据并在虚拟机中运行的应用程序，可以与在同一虚拟机中运行的
其他应用程序分开。然后，这个应用程序在一个独立于主虚拟机的虚拟机中运行，即
enclave。

一个enclave与催生它的虚拟机一起运行。这种设置符合低延迟应用的需要。为enclave
分配的资源，如内存和CPU，是从主虚拟机中分割出来的。每个enclave都被映射到一
个运行在主虚拟机中的进程，该进程通过一个ioctl接口与NE驱动进行通信。

在这个意义上，有两个组成部分。

1. 一个enclave抽象进程——一个运行在主虚拟机客体中的用户空间进程，它使用NE驱动
提供的ioctl接口来生成一个enclave虚拟机（这就是下面的2）。

有一个NE模拟的PCI设备暴露给主虚拟机。这个新的PCI设备的驱动被包含在NE驱动中。

ioctl逻辑被映射到PCI设备命令，例如，NE_START_ENCLAVE ioctl映射到一个enclave
启动PCI命令。然后，PCI设备命令被翻译成在管理程序方面采取的行动；也就是在运
行主虚拟机的主机上运行的Nitro管理程序。Nitro管理程序是基于KVM核心技术的。

2. enclave本身——一个运行在与催生它的主虚拟机相同的主机上的虚拟机。内存和CPU
从主虚拟机中分割出来，专门用于enclave虚拟机。enclave没有连接持久性存储。

从主虚拟机中分割出来并给enclave的内存区域需要对齐2 MiB/1 GiB物理连续的内存
区域（或这个大小的倍数，如8 MiB）。该内存可以通过使用hugetlbfs从用户空间分
配[2][3]。一个enclave的内存大小需要至少64 MiB。enclave内存和CPU需要来自同
一个NUMA节点。

一个enclave在专用的核心上运行。CPU 0及其同级别的CPU需要保持对主虚拟机的可用
性。CPU池必须由具有管理能力的用户为NE目的进行设置。关于CPU池的格式，请看内核
文档[4]中的cpu list部分。

enclave通过本地通信通道与主虚拟机进行通信，使用virtio-vsock[5]。主虚拟机有
virtio-pci vsock模拟设备，而飞地虚拟机有virtio-mmio vsock模拟设备。vsock
设备使用eventfd作为信令。enclave虚拟机看到通常的接口——本地APIC和IOAPIC——从
virtio-vsock设备获得中断。virtio-mmio设备被放置在典型的4 GiB以下的内存中。

在enclave中运行的应用程序需要和将在enclave虚拟机中运行的操作系统（如内核、
ramdisk、init）一起被打包到enclave镜像中。enclave虚拟机有自己的内核并遵循标
准的Linux启动协议[6]。

内核bzImage、内核命令行、ramdisk（s）是enclave镜像格式（EIF）的一部分；另外
还有一个EIF头，包括元数据，如magic number、eif版本、镜像大小和CRC。

哈希值是为整个enclave镜像（EIF）、内核和ramdisk（s）计算的。例如，这被用来检
查在enclave虚拟机中加载的enclave镜像是否是打算运行的那个。

这些加密测量包括在由Nitro超级管理器成的签名证明文件中，并进一步用来证明enclave
的身份；KMS是NE集成的服务的一个例子，它检查证明文件。

enclave镜像（EIF）被加载到enclave内存中，偏移量为8 MiB。enclave中的初始进程
连接到主虚拟机的vsock CID和一个预定义的端口--9000，以发送一个心跳值--0xb7。这
个机制用于在主虚拟机中检查enclave是否已经启动。主虚拟机的CID是3。

如果enclave虚拟机崩溃或优雅地退出，NE驱动会收到一个中断事件。这个事件会通过轮询
通知机制进一步发送到运行在主虚拟机中的用户空间enclave进程。然后，用户空间enclave
进程就可以退出了。

[1] https://aws.amazon.com/ec2/nitro/nitro-enclaves/
[2] https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html
[3] https://lwn.net/Articles/807108/
[4] https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html
[5] https://man7.org/linux/man-pages/man7/vsock.7.html
[6] https://www.kernel.org/doc/html/latest/x86/boot.html

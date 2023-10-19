.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/filesystems/virtiofs.rst <virtiofs_index>`

译者
::

	中文版维护者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>
	中文版翻译者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>
	中文版校译者： 王文虎 Wang Wenhu <wenhu.wang@vivo.com>

===========================================
virtiofs: virtio-fs 主机<->客机共享文件系统
===========================================

- Copyright (C) 2020 Vivo Communication Technology Co. Ltd.

介绍
====
Linux的virtiofs文件系统实现了一个半虚拟化VIRTIO类型“virtio-fs”设备的驱动，通过该\
类型设备实现客机<->主机文件系统共享。它允许客机挂载一个已经导出到主机的目录。

客机通常需要访问主机或者远程系统上的文件。使用场景包括：在新客机安装时让文件对其\
可见；从主机上的根文件系统启动；对无状态或临时客机提供持久存储和在客机之间共享目录。

尽管在某些任务可能通过使用已有的网络文件系统完成，但是却需要非常难以自动化的配置\
步骤，且将存储网络暴露给客机。而virtio-fs设备通过提供不经过网络的文件系统访问文件\
的设计方式解决了这些问题。

另外，virto-fs设备发挥了主客机共存的优点提高了性能，并且提供了网络文件系统所不具备
的一些语义功能。

用法
====
以``myfs``标签将文件系统挂载到``/mnt``:

.. code-block:: sh

  guest# mount -t virtiofs myfs /mnt

请查阅 https://virtio-fs.gitlab.io/ 了解配置QEMU和virtiofsd守护程序的详细信息。

内幕
====
由于virtio-fs设备将FUSE协议用于文件系统请求，因此Linux的virtiofs文件系统与FUSE文\
件系统客户端紧密集成在一起。客机充当FUSE客户端而主机充当FUSE服务器，内核与用户空\
间之间的/dev/fuse接口由virtio-fs设备接口代替。

FUSE请求被置于虚拟队列中由主机处理。主机填充缓冲区中的响应部分，而客机处理请求的完成部分。

将/dev/fuse映射到虚拟队列需要解决/dev/fuse和虚拟队列之间语义上的差异。每次读取\
/dev/fuse设备时，FUSE客户端都可以选择要传输的请求，从而可以使某些请求优先于其他\
请求。虚拟队列有其队列语义，无法更改已入队请求的顺序。在虚拟队列已满的情况下尤
其关键，因为此时不可能加入高优先级的请求。为了解决此差异，virtio-fs设备采用“hiprio”\
（高优先级）虚拟队列，专门用于有别于普通请求的高优先级请求。


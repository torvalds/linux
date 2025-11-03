.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/filesystems/gfs2.rst

:翻译:

   邵明寅 Shao Mingyin <shao.mingyin@zte.com.cn>

:校译:

   杨涛 yang tao <yang.tao172@zte.com.cn>

=====================================
全局文件系统 2 (Global File System 2)
=====================================

GFS2 是一个集群文件系统。它允许一组计算机同时使用在它们之间共享的块设备（通
过 FC、iSCSI、NBD 等）。GFS2 像本地文件系统一样读写块设备，但也使用一个锁
模块来让计算机协调它们的 I/O 操作，从而维护文件系统的一致性。GFS2 的出色特
性之一是完美一致性——在一台机器上对文件系统所做的更改会立即显示在集群中的所
有其他机器上。

GFS2 使用可互换的节点间锁定机制，当前支持的机制有：

  lock_nolock
    - 允许将 GFS2 用作本地文件系统

  lock_dlm
    - 使用分布式锁管理器 (dlm) 进行节点间锁定。
      该 dlm 位于 linux/fs/dlm/

lock_dlm 依赖于在上述 URL 中找到的用户空间集群管理系统。

若要将 GFS2 用作本地文件系统，则不需要外部集群系统，只需：:

  $ mkfs -t gfs2 -p lock_nolock -j 1 /dev/block_device
  $ mount -t gfs2 /dev/block_device /dir

在所有集群节点上都需要安装 gfs2-utils 软件包；对于 lock_dlm，您还需要按
照文档配置 dlm 和 corosync 用户空间工具。

gfs2-utils 可在 https://pagure.io/gfs2-utils  找到。

GFS2 在磁盘格式上与早期版本的 GFS 不兼容，但它已相当接近。

以下手册页 (man pages) 可在 gfs2-utils 中找到：

  ============          =============================================
  fsck.gfs2		用于修复文件系统
  gfs2_grow		用于在线扩展文件系统
  gfs2_jadd		用于在线向文件系统添加日志
  tunegfs2		用于操作、检查和调优文件系统
  gfs2_convert		用于将 gfs 文件系统原地转换为 GFS2
  mkfs.gfs2		用于创建文件系统
  ============          =============================================

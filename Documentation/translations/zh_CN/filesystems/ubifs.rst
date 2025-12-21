.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/filesystems/ubifs.rst

:翻译:

   邵明寅 Shao Mingyin <shao.mingyin@zte.com.cn>

:校译:

   杨涛 yang tao <yang.tao172@zte.com.cn>

============
UBI 文件系统
============

简介
====

UBIFS 文件系统全称为 UBI 文件系统（UBI File System）。UBI 代表无序块镜
像（Unsorted Block Images）。UBIFS 是一种闪存文件系统，这意味着它专为闪
存设备设计。需要理解的是，UBIFS与 Linux 中任何传统文件系统（如 Ext2、
XFS、JFS 等）完全不同。UBIFS 代表一类特殊的文件系统，它们工作在 MTD 设备
而非块设备上。该类别的另一个 Linux 文件系统是 JFFS2。

为更清晰说明，以下是 MTD 设备与块设备的简要比较：

1. MTD 设备代表闪存设备，由较大尺寸的擦除块组成，通常约 128KiB。块设备由
   小块组成，通常 512 字节。
2. MTD 设备支持 3 种主要操作：在擦除块内偏移位置读取、在擦除块内偏移位置写
   入、以及擦除整个擦除块。块设备支持 2 种主要操作：读取整个块和写入整个块。
3. 整个擦除块必须先擦除才能重写内容。块可直接重写。
4. 擦除块在经历一定次数的擦写周期后会磨损，通常 SLC NAND 和 NOR 闪存为
   100K-1G 次，MLC NAND 闪存为 1K-10K 次。块设备不具备磨损特性。
5. 擦除块可能损坏（仅限 NAND 闪存），软件需处理此问题。硬盘上的块通常不会损
   坏，因为硬件有坏块替换机制（至少现代 LBA 硬盘如此）。

这充分说明了 UBIFS 与传统文件系统的本质差异。

UBIFS 工作在 UBI 层之上。UBI 是一个独立的软件层（位于 drivers/mtd/ubi），
本质上是卷管理和磨损均衡层。它提供称为 UBI 卷的高级抽象，比 MTD 设备更上层。
UBI 设备的编程模型与 MTD 设备非常相似，仍由大容量擦除块组成，支持读/写/擦
除操作，但 UBI 设备消除了磨损和坏块限制（上述列表的第 4 和第 5 项）。

某种意义上，UBIFS 是 JFFS2 文件系统的下一代产品，但它与 JFFS2 差异巨大且
不兼容。主要区别如下：

* JFFS2 工作在 MTD 设备之上，UBIFS 依赖于 UBI 并工作在 UBI 卷之上。
* JFFS2 没有介质索引，需在挂载时构建索引，这要求全介质扫描。UBIFS 在闪存
  介质上维护文件系统索引信息，无需全介质扫描，因此挂载速度远快于 JFFS2。
* JFFS2 是直写（write-through）文件系统，而 UBIFS 支持回写
  （write-back），这使得 UBIFS 写入速度快得多。

与 JFFS2 类似，UBIFS 支持实时压缩，可将大量数据存入闪存。

与 JFFS2 类似，UBIFS 能容忍异常重启和断电。它不需要类似 fsck.ext2 的工
具。UBIFS 会自动重放日志并从崩溃中恢复，确保闪存数据结构的一致性。

UBIFS 具有对数级扩展性（其使用的数据结构多为树形），因此挂载时间和内存消耗不
像 JFFS2 那样线性依赖于闪存容量。这是因为 UBIFS 在闪存介质上维护文件系统
索引。但 UBIFS 依赖于线性扩展的 UBI 层，因此整体 UBI/UBIFS 栈仍是线性扩
展。尽管如此，UBIFS/UBI 的扩展性仍显著优于 JFFS2。

UBIFS 开发者认为，未来可开发同样具备对数级扩展性的 UBI2。UBI2 将支持与
UBI 相同的 API，但二进制不兼容。因此 UBIFS 无需修改即可使用 UBI2。

挂载选项
========

(*) 表示默认选项。

====================    =======================================================
bulk_read               批量读取以利用闪存介质的顺序读取加速特性
no_bulk_read (*)        禁用批量读取
no_chk_data_crc (*)     跳过数据节点的 CRC 校验以提高读取性能。 仅在闪存
                        介质高度可靠时使用此选项。 此选项可能导致文件内容损坏无法被
                        察觉。
chk_data_crc            强制校验数据节点的 CRC
compr=none              覆盖默认压缩器，设置为"none"
compr=lzo               覆盖默认压缩器，设置为"LZO"
compr=zlib              覆盖默认压缩器，设置为"zlib"
auth_key=               指定用于文件系统身份验证的密钥。
                        使用此选项将强制启用身份验证。
                        传入的密钥必须存在于内核密钥环中， 且类型必须是'logon'
auth_hash_name=         用于身份验证的哈希算法。同时用于哈希计算和 HMAC
                        生成。典型值包括"sha256"或"sha512"
====================    =======================================================

快速使用指南
============

挂载的 UBI 卷通过 "ubiX_Y" 或 "ubiX:NAME" 语法指定，其中 "X" 是 UBI
设备编号，"Y" 是 UBI 卷编号，"NAME" 是 UBI 卷名称。

将 UBI 设备 0 的卷 0 挂载到 /mnt/ubifs::

    $ mount -t ubifs ubi0_0 /mnt/ubifs

将 UBI 设备 0 的 "rootfs" 卷挂载到 /mnt/ubifs（"rootfs" 是卷名）::

    $ mount -t ubifs ubi0:rootfs /mnt/ubifs

以下是内核启动参数的示例，用于将 mtd0 附加到 UBI 并挂载 "rootfs" 卷：
ubi.mtd=0 root=ubi0:rootfs rootfstype=ubifs

参考资料
========

UBIFS 文档及常见问题解答/操作指南请访问 MTD 官网：

- http://www.linux-mtd.infradead.org/doc/ubifs.html
- http://www.linux-mtd.infradead.org/faq/ubifs.html

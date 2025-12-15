.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scsi/sd-parameters.rst

:翻译:

 郝栋栋 doubled <doubled@leap-io-kernel.com>

:校译:



============================
Linux SCSI磁盘驱动（sd）参数
============================

缓存类型（读/写）
------------------
启用/禁用驱动器读写缓存。

===========================    =====   =====   =======   =======
 缓存类型字符串                 WCE     RCD     写缓存    读缓存
===========================    =====   =====   =======   =======
 write through                   0       0       关闭      开启
 none                            0       1       关闭      关闭
 write back                      1       0       开启      开启
 write back, no read (daft)      1       1       开启      关闭
===========================    =====   =====   =======   =======

将缓存类型设置为“write back”并将该设置保存到驱动器::

  # echo "write back" > cache_type

如果要修改缓存模式但不使更改持久化，可在缓存类型字符串前
添加“temporary ”。例如::

  # echo "temporary write back" > cache_type

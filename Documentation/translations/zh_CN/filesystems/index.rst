.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/filesystems/index.rst <filesystems_index>`
:Translator: Wang Wenhu <wenhu.wang@vivo.com>

.. _cn_filesystems_index:

========================
Linux Kernel中的文件系统
========================

这份正在开发的手册或许在未来某个辉煌的日子里以易懂的形式将Linux虚拟\
文件系统（VFS）层以及基于其上的各种文件系统如何工作呈现给大家。当前\
可以看到下面的内容。

核心 VFS 文档
=============

有关 VFS 层本身以及其算法工作方式的文档，请参阅这些手册。

.. toctree::
   :maxdepth: 1

   dnotify

文件系统
========

文件系统实现文档。

.. toctree::
   :maxdepth: 2

   virtiofs
   debugfs
   tmpfs
   ubifs
   ubifs-authentication
   gfs2
   gfs2-uevents
   gfs2-glocks
   inotify

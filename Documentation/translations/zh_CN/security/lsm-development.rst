.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/security/lsm-development.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

=================
Linux安全模块开发
=================

基于https://lore.kernel.org/r/20071026073721.618b4778@laptopd505.fenrus.org，
当一种新的LSM的意图（它试图防范什么，以及在哪些情况下人们会期望使用它）在
``Documentation/admin-guide/LSM/`` 中适当记录下来后，就会被接受进入内核。
这使得LSM的代码可以很轻松的与其目标进行对比，从而让最终用户和发行版可以更
明智地决定那些LSM适合他们的需求。

有关可用的 LSM 钩子接口的详细文档，请参阅 ``security/security.c`` 及相关结构。

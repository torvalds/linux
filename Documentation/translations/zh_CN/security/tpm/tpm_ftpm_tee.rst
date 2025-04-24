.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/security/tpm/tpm_ftpm_tee.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

===========
固件TPM驱动
===========

本文档描述了固件可信平台模块（fTPM）设备驱动。

介绍
====

该驱动程序是用于ARM的TrustZone环境中实现的固件的适配器。该驱动
程序允许程序以与硬件TPM相同的方式与TPM进行交互。

设计
====

该驱动程序充当一个薄层，传递命令到固件实现的TPM并接收其响应。驱动
程序本身并不包含太多逻辑，更像是固件与内核/用户空间之间的一个管道。

固件本身基于以下论文：
https://www.microsoft.com/en-us/research/wp-content/uploads/2017/06/ftpm1.pdf

当驱动程序被加载时，它会向用户空间暴露 ``/dev/tpmX`` 字符设备，允许
用户空间通过该设备与固件TPM进行通信。

.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/security/tpm/tpm_event_log.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

===========
TPM事件日志
===========

本文档简要介绍了什么是TPM日志，以及它是如何从预启动固件移交到操作系统的。

介绍
====

预启动固件维护一个事件日志，每当它将某些内容哈希到任何一个PCR寄存器时，该
日志会添加新条目。这些事件按类型分类，并包含哈希后的PCR寄存器值。通常，预
启动固件会哈希那些即将移交执行权或与启动过程相关的组件。

其主要应用是远程认证，而它之所以有用的原因在[1]中第一部分很好地阐述了：

认证用于向挑战者提供有关平台状态的信息。然而，PCR的内容难以解读；因此，当
PCR内容附有测量日志时，认证通常会更有用。尽管测量日志本身并不可信，但它们
包含比PCR内容更为丰富的信息集。PCR内容用于对测量日志进行验证。

UEFI事件日志
============

UEFI提供的事件日志有一些比较奇怪的特性。

在调用ExitBootServices()之前，Linux EFI引导加载程序会将事件日志复制到由
引导加载程序自定义的配置表中。不幸的是，通过ExitBootServices()生成的事件
并不会出现在这个表里。

固件提供了一个所谓的最终事件配置表排序来解决这个问题。事件会在第一次调用
EFI_TCG2_PROTOCOL.GetEventLog()后被镜像到这个表中。

这引出了另一个问题：无法保证它不会在 Linux EFI stub 开始运行之前被调用。
因此，在 stub 运行时，它需要计算并将最终事件表的大小保存到自定义配置表中，
以便TPM驱动程序可以在稍后连接来自自定义配置表和最终事件表的两个事件日志时
跳过这些事件。

参考文献
========

- [1] https://trustedcomputinggroup.org/resource/pc-client-specific-platform-firmware-profile-specification/
- [2] The final concatenation is done in drivers/char/tpm/eventlog/efi.c

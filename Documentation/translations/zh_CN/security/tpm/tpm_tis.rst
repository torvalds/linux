.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/security/tpm/tpm_tis.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

TPM FIFO接口驱动
================

TCG PTP规范定义了两种接口类型：FIFO和CRB。前者基于顺序的读写操作，
后者基于包含完整命令或响应的缓冲区。

FIFO（先进先出）接口被tpm_tis_core依赖的驱动程序使用。最初，Linux只
有一个名为tpm_tis的驱动，覆盖了内存映射（即 MMIO）接口，但后来它被
扩展以支持TCG标准所支持的其他物理接口。

由于历史原因，最初的MMIO驱动被称为tpm_tis，而FIFO驱动的框架被命名为
tpm_tis_core。在tpm_tis中的“tis”后缀来自TPM接口规范，这是针对TPM1.x
芯片的硬件接口规范。

通信基于一个由TPM芯片通过硬件总线或内存映射共享的20KiB 缓冲区，具体
取决于物理接线。该缓冲区进一步分为五个相等大小的4KiB缓冲区，为CPU和
TPM之间的通信提供等效的寄存器集。这些通信端点在TCG术语中称为localities。

当内核想要向TPM芯片发送命令时，它首先通过在TPM_ACCESS寄存器中设置
requestUse位来保留locality0。当访问被授予时，该位由芯片清除。一旦完成
通信，内核会写入TPM_ACCESS.activeLocality位。这告诉芯片该本地性已被
释放。

待处理的本地性由芯片按优先级降序逐个服务，一次一个：

- Locality0优先级最低。
- Locality5优先级最高。

关于localities的更多信息和含义，请参阅TCG PC客户端平台TPM 配置文件规范的第3.2节。

参考文献
========

TCG PC客户端平台TPM配置文件（PTP）规范
https://trustedcomputinggroup.org/resource/pc-client-platform-tpm-profile-ptp-specification/

.. SPDX-License-Identifier: GPL-2.0
.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/security/tpm/tpm_vtpm_proxy.rst

:翻译:
 赵硕 Shuo Zhao <zhaoshuo@cqsoftware.com.cn>

==========================
Linux容器的虚拟TPM代理驱动
==========================

| 作者：
| Stefan Berger <stefanb@linux.vnet.ibm.com>

本文档描述了用于Linux容器的虚拟可信平台模块（vTPM）代理设备驱动。

介绍
====

这项工作的目标是为每个Linux容器提供TPM功能。这使得程序能够像与物理系统
上的TPM交互一样，与容器中的TPM进行交互。每个容器都会获得一个唯一的、模
拟的软件TPM。

设计
====

为了使每个容器都能使用模拟的软件TPM，容器管理栈需要创建一对设备，其中
包括一个客户端TPM字符设备 ``/dev/tpmX`` （X=0,1,2...）和一个‘服务器端’
文件描述符。当文件描述符传被递给TPM模拟器时，前者通过创建具有适当主次
设备号的字符设备被移入容器，然后，容器内的软件可以使用字符设备发送TPM
命令，模拟器将通过文件描述符接收这些命令，并用它来发送响应。

为了支持这一点，虚拟TPM代理驱动程序提供了一个设备 ``/dev/vtpmx`` ，该设备
用于通过ioctl创建设备对。ioctl将其作为配置设备的输入标志，例如这些标志指示
TPM模拟器是否支持TPM1.2或TPM2功能。ioctl的结果是返回‘服务器端’的文件描述符
以及创建的字符设备的主次设备号。此外，还会返回TPM字符设备的编号。例如，如果
创建了 ``/dev/tpm10`` ，则返回编号（ ``dev_num`` ）10。

一旦设备被创建，驱动程序将立即尝试与TPM进行通信。来自驱动程序的所有命令
都可以从ioctl返回的文件描述符中读取。这些命令应该立即得到响应。

UAPI
====

该API在以下内核代码中：

include/uapi/linux/vtpm_proxy.h
drivers/char/tpm/tpm_vtpm_proxy.c

函数：vtpmx_ioc_new_dev

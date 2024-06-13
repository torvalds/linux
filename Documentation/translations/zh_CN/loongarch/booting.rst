.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/loongarch/booting.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

====================
启动 Linux/LoongArch
====================

:作者: 司延腾 <siyanteng@loongson.cn>
:日期: 2022年11月18日

BootLoader传递给内核的信息
==========================

LoongArch支持ACPI和FDT启动，需要传递给内核的信息包括memmap、initrd、cmdline、可
选的ACPI/FDT表等。

内核在 `kernel_entry` 入口处被传递以下参数:

      - a0 = efi_boot: `efi_boot` 是一个标志，表示这个启动环境是否完全符合UEFI
        的要求。

      - a1 = cmdline: `cmdline` 是一个指向内核命令行的指针。

      - a2 = systemtable: `systemtable` 指向EFI的系统表，在这个阶段涉及的所有
        指针都是物理地址。

Linux/LoongArch内核镜像文件头
=============================

内核镜像是EFI镜像。作为PE文件，它们有一个64字节的头部结构体，如下所示::

	u32	MZ_MAGIC                /* "MZ", MS-DOS 头 */
	u32	res0 = 0                /* 保留 */
	u64	kernel_entry            /* 内核入口点 */
	u64	_end - _text            /* 内核镜像有效大小 */
	u64	load_offset             /* 加载内核镜像相对内存起始地址的偏移量 */
	u64	res1 = 0                /* 保留 */
	u64	res2 = 0                /* 保留 */
	u64	res3 = 0                /* 保留 */
	u32	LINUX_PE_MAGIC          /* 魔术数 */
	u32	pe_header - _head       /* 到PE头的偏移量 */

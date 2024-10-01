.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/loongarch/booting.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

====================
啓動 Linux/LoongArch
====================

:作者: 司延騰 <siyanteng@loongson.cn>
:日期: 2022年11月18日

BootLoader傳遞給內核的信息
==========================

LoongArch支持ACPI和FDT啓動，需要傳遞給內核的信息包括memmap、initrd、cmdline、可
選的ACPI/FDT表等。

內核在 `kernel_entry` 入口處被傳遞以下參數:

      - a0 = efi_boot: `efi_boot` 是一個標誌，表示這個啓動環境是否完全符合UEFI
        的要求。

      - a1 = cmdline: `cmdline` 是一個指向內核命令行的指針。

      - a2 = systemtable: `systemtable` 指向EFI的系統表，在這個階段涉及的所有
        指針都是物理地址。

Linux/LoongArch內核鏡像文件頭
=============================

內核鏡像是EFI鏡像。作爲PE文件，它們有一個64字節的頭部結構體，如下所示::

	u32	MZ_MAGIC                /* "MZ", MS-DOS 頭 */
	u32	res0 = 0                /* 保留 */
	u64	kernel_entry            /* 內核入口點 */
	u64	_end - _text            /* 內核鏡像有效大小 */
	u64	load_offset             /* 加載內核鏡像相對內存起始地址的偏移量 */
	u64	res1 = 0                /* 保留 */
	u64	res2 = 0                /* 保留 */
	u64	res3 = 0                /* 保留 */
	u32	LINUX_PE_MAGIC          /* 魔術數 */
	u32	pe_header - _head       /* 到PE頭的偏移量 */


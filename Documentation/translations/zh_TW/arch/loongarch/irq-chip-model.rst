.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/loongarch/irq-chip-model.rst
:Translator: Huacai Chen <chenhuacai@loongson.cn>

==================================
LoongArch的IRQ芯片模型（層級關係）
==================================

目前，基於LoongArch的處理器（如龍芯3A5000）只能與LS7A芯片組配合工作。LoongArch計算機
中的中斷控制器（即IRQ芯片）包括CPUINTC（CPU Core Interrupt Controller）、LIOINTC（
Legacy I/O Interrupt Controller）、EIOINTC（Extended I/O Interrupt Controller）、
HTVECINTC（Hyper-Transport Vector Interrupt Controller）、PCH-PIC（LS7A芯片組的主中
斷控制器）、PCH-LPC（LS7A芯片組的LPC中斷控制器）和PCH-MSI（MSI中斷控制器）。

CPUINTC是一種CPU內部的每個核本地的中斷控制器，LIOINTC/EIOINTC/HTVECINTC是CPU內部的
全局中斷控制器（每個芯片一個，所有核共享），而PCH-PIC/PCH-LPC/PCH-MSI是CPU外部的中
斷控制器（在配套芯片組裏面）。這些中斷控制器（或者說IRQ芯片）以一種層次樹的組織形式
級聯在一起，一共有兩種層級關係模型（傳統IRQ模型和擴展IRQ模型）。

傳統IRQ模型
===========

在這種模型裏面，IPI（Inter-Processor Interrupt）和CPU本地時鐘中斷直接發送到CPUINTC，
CPU串口（UARTs）中斷髮送到LIOINTC，而其他所有設備的中斷則分別發送到所連接的PCH-PIC/
PCH-LPC/PCH-MSI，然後被HTVECINTC統一收集，再發送到LIOINTC，最後到達CPUINTC::

     +-----+     +---------+     +-------+
     | IPI | --> | CPUINTC | <-- | Timer |
     +-----+     +---------+     +-------+
                      ^
                      |
                 +---------+     +-------+
                 | LIOINTC | <-- | UARTs |
                 +---------+     +-------+
                      ^
                      |
                +-----------+
                | HTVECINTC |
                +-----------+
                 ^         ^
                 |         |
           +---------+ +---------+
           | PCH-PIC | | PCH-MSI |
           +---------+ +---------+
             ^     ^           ^
             |     |           |
     +---------+ +---------+ +---------+
     | PCH-LPC | | Devices | | Devices |
     +---------+ +---------+ +---------+
          ^
          |
     +---------+
     | Devices |
     +---------+

擴展IRQ模型
===========

在這種模型裏面，IPI（Inter-Processor Interrupt）和CPU本地時鐘中斷直接發送到CPUINTC，
CPU串口（UARTs）中斷髮送到LIOINTC，而其他所有設備的中斷則分別發送到所連接的PCH-PIC/
PCH-LPC/PCH-MSI，然後被EIOINTC統一收集，再直接到達CPUINTC::

          +-----+     +---------+     +-------+
          | IPI | --> | CPUINTC | <-- | Timer |
          +-----+     +---------+     +-------+
                       ^       ^
                       |       |
                +---------+ +---------+     +-------+
                | EIOINTC | | LIOINTC | <-- | UARTs |
                +---------+ +---------+     +-------+
                 ^       ^
                 |       |
          +---------+ +---------+
          | PCH-PIC | | PCH-MSI |
          +---------+ +---------+
            ^     ^           ^
            |     |           |
    +---------+ +---------+ +---------+
    | PCH-LPC | | Devices | | Devices |
    +---------+ +---------+ +---------+
         ^
         |
    +---------+
    | Devices |
    +---------+

ACPI相關的定義
==============

CPUINTC::

  ACPI_MADT_TYPE_CORE_PIC;
  struct acpi_madt_core_pic;
  enum acpi_madt_core_pic_version;

LIOINTC::

  ACPI_MADT_TYPE_LIO_PIC;
  struct acpi_madt_lio_pic;
  enum acpi_madt_lio_pic_version;

EIOINTC::

  ACPI_MADT_TYPE_EIO_PIC;
  struct acpi_madt_eio_pic;
  enum acpi_madt_eio_pic_version;

HTVECINTC::

  ACPI_MADT_TYPE_HT_PIC;
  struct acpi_madt_ht_pic;
  enum acpi_madt_ht_pic_version;

PCH-PIC::

  ACPI_MADT_TYPE_BIO_PIC;
  struct acpi_madt_bio_pic;
  enum acpi_madt_bio_pic_version;

PCH-MSI::

  ACPI_MADT_TYPE_MSI_PIC;
  struct acpi_madt_msi_pic;
  enum acpi_madt_msi_pic_version;

PCH-LPC::

  ACPI_MADT_TYPE_LPC_PIC;
  struct acpi_madt_lpc_pic;
  enum acpi_madt_lpc_pic_version;

參考文獻
========

龍芯3A5000的文檔：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-CN.pdf (中文版)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-EN.pdf (英文版)

龍芯LS7A芯片組的文檔：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-CN.pdf (中文版)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-EN.pdf (英文版)

.. note::
    - CPUINTC：即《龍芯架構參考手冊卷一》第7.4節所描述的CSR.ECFG/CSR.ESTAT寄存器及其
      中斷控制邏輯；
    - LIOINTC：即《龍芯3A5000處理器使用手冊》第11.1節所描述的“傳統I/O中斷”；
    - EIOINTC：即《龍芯3A5000處理器使用手冊》第11.2節所描述的“擴展I/O中斷”；
    - HTVECINTC：即《龍芯3A5000處理器使用手冊》第14.3節所描述的“HyperTransport中斷”；
    - PCH-PIC/PCH-MSI：即《龍芯7A1000橋片用戶手冊》第5章所描述的“中斷控制器”；
    - PCH-LPC：即《龍芯7A1000橋片用戶手冊》第24.3節所描述的“LPC中斷”。


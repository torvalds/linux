.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/loongarch/irq-chip-model.rst
:Translator: Huacai Chen <chenhuacai@loongson.cn>

==================================
LoongArch的IRQ芯片模型（层级关系）
==================================

目前，基于LoongArch的处理器（如龙芯3A5000）只能与LS7A芯片组配合工作。LoongArch计算机
中的中断控制器（即IRQ芯片）包括CPUINTC（CPU Core Interrupt Controller）、LIOINTC（
Legacy I/O Interrupt Controller）、EIOINTC（Extended I/O Interrupt Controller）、
HTVECINTC（Hyper-Transport Vector Interrupt Controller）、PCH-PIC（LS7A芯片组的主中
断控制器）、PCH-LPC（LS7A芯片组的LPC中断控制器）和PCH-MSI（MSI中断控制器）。

CPUINTC是一种CPU内部的每个核本地的中断控制器，LIOINTC/EIOINTC/HTVECINTC是CPU内部的
全局中断控制器（每个芯片一个，所有核共享），而PCH-PIC/PCH-LPC/PCH-MSI是CPU外部的中
断控制器（在配套芯片组里面）。这些中断控制器（或者说IRQ芯片）以一种层次树的组织形式
级联在一起，一共有两种层级关系模型（传统IRQ模型和扩展IRQ模型）。

传统IRQ模型
===========

在这种模型里面，IPI（Inter-Processor Interrupt）和CPU本地时钟中断直接发送到CPUINTC，
CPU串口（UARTs）中断发送到LIOINTC，而其他所有设备的中断则分别发送到所连接的PCH-PIC/
PCH-LPC/PCH-MSI，然后被HTVECINTC统一收集，再发送到LIOINTC，最后到达CPUINTC::

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

扩展IRQ模型
===========

在这种模型里面，IPI（Inter-Processor Interrupt）和CPU本地时钟中断直接发送到CPUINTC，
CPU串口（UARTs）中断发送到LIOINTC，而其他所有设备的中断则分别发送到所连接的PCH-PIC/
PCH-LPC/PCH-MSI，然后被EIOINTC统一收集，再直接到达CPUINTC::

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

ACPI相关的定义
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

参考文献
========

龙芯3A5000的文档：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-CN.pdf (中文版)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-EN.pdf (英文版)

龙芯LS7A芯片组的文档：

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-CN.pdf (中文版)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-EN.pdf (英文版)

.. note::
    - CPUINTC：即《龙芯架构参考手册卷一》第7.4节所描述的CSR.ECFG/CSR.ESTAT寄存器及其
      中断控制逻辑；
    - LIOINTC：即《龙芯3A5000处理器使用手册》第11.1节所描述的“传统I/O中断”；
    - EIOINTC：即《龙芯3A5000处理器使用手册》第11.2节所描述的“扩展I/O中断”；
    - HTVECINTC：即《龙芯3A5000处理器使用手册》第14.3节所描述的“HyperTransport中断”；
    - PCH-PIC/PCH-MSI：即《龙芯7A1000桥片用户手册》第5章所描述的“中断控制器”；
    - PCH-LPC：即《龙芯7A1000桥片用户手册》第24.3节所描述的“LPC中断”。

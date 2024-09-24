.. SPDX-License-Identifier: GPL-2.0

=======================================
IRQ chip model (hierarchy) of LoongArch
=======================================

Currently, LoongArch based processors (e.g. Loongson-3A5000) can only work together
with LS7A chipsets. The irq chips in LoongArch computers include CPUINTC (CPU Core
Interrupt Controller), LIOINTC (Legacy I/O Interrupt Controller), EIOINTC (Extended
I/O Interrupt Controller), HTVECINTC (Hyper-Transport Vector Interrupt Controller),
PCH-PIC (Main Interrupt Controller in LS7A chipset), PCH-LPC (LPC Interrupt Controller
in LS7A chipset) and PCH-MSI (MSI Interrupt Controller).

CPUINTC is a per-core controller (in CPU), LIOINTC/EIOINTC/HTVECINTC are per-package
controllers (in CPU), while PCH-PIC/PCH-LPC/PCH-MSI are controllers out of CPU (i.e.,
in chipsets). These controllers (in other words, irqchips) are linked in a hierarchy,
and there are two models of hierarchy (legacy model and extended model).

Legacy IRQ model
================

In this model, IPI (Inter-Processor Interrupt) and CPU Local Timer interrupt go
to CPUINTC directly, CPU UARTS interrupts go to LIOINTC, while all other devices
interrupts go to PCH-PIC/PCH-LPC/PCH-MSI and gathered by HTVECINTC, and then go
to LIOINTC, and then CPUINTC::

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

Extended IRQ model
==================

In this model, IPI (Inter-Processor Interrupt) and CPU Local Timer interrupt go
to CPUINTC directly, CPU UARTS interrupts go to LIOINTC, while all other devices
interrupts go to PCH-PIC/PCH-LPC/PCH-MSI and gathered by EIOINTC, and then go to
to CPUINTC directly::

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

Advanced Extended IRQ model
===========================

In this model, IPI (Inter-Processor Interrupt) and CPU Local Timer interrupt go
to CPUINTC directly, CPU UARTS interrupts go to LIOINTC, PCH-MSI interrupts go
to AVECINTC, and then go to CPUINTC directly, while all other devices interrupts
go to PCH-PIC/PCH-LPC and gathered by EIOINTC, and then go to CPUINTC directly::

 +-----+     +-----------------------+     +-------+
 | IPI | --> |        CPUINTC        | <-- | Timer |
 +-----+     +-----------------------+     +-------+
              ^          ^          ^
              |          |          |
       +---------+ +----------+ +---------+     +-------+
       | EIOINTC | | AVECINTC | | LIOINTC | <-- | UARTs |
       +---------+ +----------+ +---------+     +-------+
            ^            ^
            |            |
       +---------+  +---------+
       | PCH-PIC |  | PCH-MSI |
       +---------+  +---------+
         ^     ^           ^
         |     |           |
 +---------+ +---------+ +---------+
 | Devices | | PCH-LPC | | Devices |
 +---------+ +---------+ +---------+
                  ^
                  |
             +---------+
             | Devices |
             +---------+

ACPI-related definitions
========================

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

References
==========

Documentation of Loongson-3A5000:

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-CN.pdf (in Chinese)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-3A5000-usermanual-1.02-EN.pdf (in English)

Documentation of Loongson's LS7A chipset:

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-CN.pdf (in Chinese)

  https://github.com/loongson/LoongArch-Documentation/releases/latest/download/Loongson-7A1000-usermanual-2.00-EN.pdf (in English)

.. Note::
    - CPUINTC is CSR.ECFG/CSR.ESTAT and its interrupt controller described
      in Section 7.4 of "LoongArch Reference Manual, Vol 1";
    - LIOINTC is "Legacy I/OInterrupts" described in Section 11.1 of
      "Loongson 3A5000 Processor Reference Manual";
    - EIOINTC is "Extended I/O Interrupts" described in Section 11.2 of
      "Loongson 3A5000 Processor Reference Manual";
    - HTVECINTC is "HyperTransport Interrupts" described in Section 14.3 of
      "Loongson 3A5000 Processor Reference Manual";
    - PCH-PIC/PCH-MSI is "Interrupt Controller" described in Section 5 of
      "Loongson 7A1000 Bridge User Manual";
    - PCH-LPC is "LPC Interrupts" described in Section 24.3 of
      "Loongson 7A1000 Bridge User Manual".

.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: :doc:`../../../mips/ingenic-tcu`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_ingenic-tcu:

===============================================
君正 JZ47xx SoC定时器/计数器硬件单元
===============================================

君正 JZ47xx SoC中的定时器/计数器单元(TCU)是一个多功能硬件块。它有多达
8个通道，可以用作计数器，计时器，或脉冲宽度调制器。

- JZ4725B, JZ4750, JZ4755 只有６个TCU通道。其它SoC都有８个通道。

- JZ4725B引入了一个独立的通道，称为操作系统计时器(OST)。这是一个32位可
  编程定时器。在JZ4760B及以上型号上，它是64位的。

- 每个TCU通道都有自己的时钟源，可以通过 TCSR 寄存器设置通道的父级时钟
  源（pclk、ext、rtc）、开关以及分频。

    - 看门狗和OST硬件模块在它们的寄存器空间中也有相同形式的TCSR寄存器。
    - 用于关闭/开启的 TCU 寄存器也可以关闭/开启看门狗和 OST 时钟。

- 每个TCU通道在两种模式的其中一种模式下运行：

    - 模式 TCU1：通道无法在睡眠模式下运行，但更易于操作。
    - 模式 TCU2：通道可以在睡眠模式下运行，但操作比 TCU1 通道复杂一些。

- 每个 TCU 通道的模式取决于使用的SoC：

    - 在最老的SoC（高于JZ4740），八个通道都运行在TCU1模式。
    - 在 JZ4725B，通道5运行在TCU2,其它通道则运行在TCU1。
    - 在最新的SoC（JZ4750及之后），通道1-2运行在TCU2，其它通道则运行
      在TCU1。

- 每个通道都可以生成中断。有些通道共享一条中断线，而有些没有，其在SoC型
  号之间的变更：

    - 在很老的SoC（JZ4740及更低），通道0和通道1有它们自己的中断线；通
      道2-7共享最后一条中断线。
    - 在 JZ4725B，通道0有它自己的中断线；通道1-5共享一条中断线；OST
      使用最后一条中断线。
    - 在比较新的SoC（JZ4750及以后），通道5有它自己的中断线；通
      道0-4和（如果是8通道）6-7全部共享一条中断线；OST使用最后一条中
      断线。

实现
====

TCU硬件的功能分布在多个驱动程序：

==============      ===================================
时钟                drivers/clk/ingenic/tcu.c
中断                drivers/irqchip/irq-ingenic-tcu.c
定时器              drivers/clocksource/ingenic-timer.c
OST                 drivers/clocksource/ingenic-ost.c
脉冲宽度调制器      drivers/pwm/pwm-jz4740.c
看门狗              drivers/watchdog/jz4740_wdt.c
==============      ===================================

因为可以从相同的寄存器控制属于不同驱动程序和框架的TCU的各种功能，所以
所有这些驱动程序都通过相同的控制总线通用接口访问它们的寄存器。

有关TCU驱动程序的设备树绑定的更多信息，请参阅:
Documentation/devicetree/bindings/timer/ingenic,tcu.yaml.

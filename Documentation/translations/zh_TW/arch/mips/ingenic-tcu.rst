.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/mips/ingenic-tcu.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

.. _tw_ingenic-tcu:

===============================================
君正 JZ47xx SoC定時器/計數器硬件單元
===============================================

君正 JZ47xx SoC中的定時器/計數器單元(TCU)是一個多功能硬件塊。它有多達
8個通道，可以用作計數器，計時器，或脈衝寬度調製器。

- JZ4725B, JZ4750, JZ4755 只有６個TCU通道。其它SoC都有８個通道。

- JZ4725B引入了一個獨立的通道，稱爲操作系統計時器(OST)。這是一個32位可
  編程定時器。在JZ4760B及以上型號上，它是64位的。

- 每個TCU通道都有自己的時鐘源，可以通過 TCSR 寄存器設置通道的父級時鐘
  源（pclk、ext、rtc）、開關以及分頻。

    - 看門狗和OST硬件模塊在它們的寄存器空間中也有相同形式的TCSR寄存器。
    - 用於關閉/開啓的 TCU 寄存器也可以關閉/開啓看門狗和 OST 時鐘。

- 每個TCU通道在兩種模式的其中一種模式下運行：

    - 模式 TCU1：通道無法在睡眠模式下運行，但更易於操作。
    - 模式 TCU2：通道可以在睡眠模式下運行，但操作比 TCU1 通道複雜一些。

- 每個 TCU 通道的模式取決於使用的SoC：

    - 在最老的SoC（高於JZ4740），八個通道都運行在TCU1模式。
    - 在 JZ4725B，通道5運行在TCU2,其它通道則運行在TCU1。
    - 在最新的SoC（JZ4750及之後），通道1-2運行在TCU2，其它通道則運行
      在TCU1。

- 每個通道都可以生成中斷。有些通道共享一條中斷線，而有些沒有，其在SoC型
  號之間的變更：

    - 在很老的SoC（JZ4740及更低），通道0和通道1有它們自己的中斷線；通
      道2-7共享最後一條中斷線。
    - 在 JZ4725B，通道0有它自己的中斷線；通道1-5共享一條中斷線；OST
      使用最後一條中斷線。
    - 在比較新的SoC（JZ4750及以後），通道5有它自己的中斷線；通
      道0-4和（如果是8通道）6-7全部共享一條中斷線；OST使用最後一條中
      斷線。

實現
====

TCU硬件的功能分佈在多個驅動程序：

==============      ===================================
時鐘                drivers/clk/ingenic/tcu.c
中斷                drivers/irqchip/irq-ingenic-tcu.c
定時器              drivers/clocksource/ingenic-timer.c
OST                 drivers/clocksource/ingenic-ost.c
脈衝寬度調製器      drivers/pwm/pwm-jz4740.c
看門狗              drivers/watchdog/jz4740_wdt.c
==============      ===================================

因爲可以從相同的寄存器控制屬於不同驅動程序和框架的TCU的各種功能，所以
所有這些驅動程序都通過相同的控制總線通用接口訪問它們的寄存器。

有關TCU驅動程序的設備樹綁定的更多信息，請參閱:
Documentation/devicetree/bindings/timer/ingenic,tcu.yaml.


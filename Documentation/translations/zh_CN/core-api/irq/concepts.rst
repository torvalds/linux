.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/core-api/irq/concepts.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_concepts.rst:

===========
什么是IRQ？
===========

IRQ (Interrupt ReQuest) 指来自设备的中断请求。
目前，它们可以通过一个引脚或通过一个数据包进入。
多个设备可以连接到同一个引脚，从而共享一个IRQ。

IRQ编号是用来描述硬件中断源的内核标识符。通常它是一个到全局irq_desc数组的索引，
但是除了在linux/interrupt.h中实现的之外，其它细节是体系结构特征相关的。

IRQ编号是对机器上可能的中断源的枚举。通常枚举的是系统中所有中断控制器的输入引脚
编号。在ISA（工业标准体系结构）的情况下所枚举的是两个i8259中断控制器的16个输入引脚。

体系结构可以给IRQ号赋予额外的含义，在涉及到硬件手动配置的情况下，我们鼓励这样做。
ISA IRQ是赋予这种额外含义的一个典型例子。

.. SPDX-License-Identifier: GPL-2.0-only
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/peci/peci.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

====
概述
====

平台环境控制接口（PECI）是英特尔处理器和管理控制器（如底板管理控制器，BMC）
之间的一个通信接口。PECI提供的服务允许管理控制器通过访问各种寄存器来配置、监
控和调试平台。它定义了一个专门的命令协议，管理控制器作为PECI的发起者，处理器
作为PECI的响应者。PECI可以用于基于单处理器和多处理器的系统中。

注意：英特尔PECI规范没有作为专门的文件发布，而是作为英特尔CPU的外部设计规范
（EDS）的一部分。外部设计规范通常是不公开的。

PECI 线
---------

PECI线接口使用单线进行自锁和数据传输。它不需要任何额外的控制线--物理层是一个
自锁的单线总线信号，每一个比特都从接近零伏的空闲状态开始驱动、上升边缘。驱动高
电平信号的持续时间可以确定位值是逻辑 “0” 还是逻辑 “1”。PECI线还包括与每个信
息建立的可变数据速率。

对于PECI线，每个处理器包将在一个定义的范围内利用唯一的、固定的地址，该地址应
该与处理器插座ID有固定的关系--如果其中一个处理器被移除，它不会影响其余处理器
的地址。

PECI子系统代码内嵌文档
------------------------

该API在以下内核代码中:

include/linux/peci.h

drivers/peci/internal.h

drivers/peci/core.c

drivers/peci/request.c

PECI CPU 驱动 API
-------------------

该API在以下内核代码中:

drivers/peci/cpu.c

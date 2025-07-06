.. SPDX-License-Identifier: GPL-2.0

.. include:: ../../disclaimer-zh_CN.rst

:Original: Documentation/driver-api/gpio/index.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

=======================
通用型输入/输出（GPIO）
=======================

.. toctree::
   :caption: 目录
   :maxdepth: 2

Todolist:

*   intro
*   using-gpio
*   driver
*   consumer
*   board
*   drivers-on-gpio
*   bt8xxgpio

核心
====

该API在以下内核代码中:

include/linux/gpio/driver.h

drivers/gpio/gpiolib.c

ACPI支持
========

该API在以下内核代码中:

drivers/gpio/gpiolib-acpi-core.c

设备树支持
==========

该API在以下内核代码中:

drivers/gpio/gpiolib-of.c

设备管理支持
============

该API在以下内核代码中:

drivers/gpio/gpiolib-devres.c

sysfs帮助（函数）
=================

该API在以下内核代码中:

drivers/gpio/gpiolib-sysfs.c

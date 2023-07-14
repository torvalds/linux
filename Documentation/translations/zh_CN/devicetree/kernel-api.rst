.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/devicetree/kernel-api.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:


=================
内核中的设备树API
=================

核心函数
--------

该API在以下内核代码中:

drivers/of/base.c

include/linux/of.h

drivers/of/property.c

include/linux/of_graph.h

drivers/of/address.c

drivers/of/irq.c

drivers/of/fdt.c

驱动模型函数
------------

该API在以下内核代码中:

include/linux/of_device.h

drivers/of/device.c

include/linux/of_platform.h

drivers/of/platform.c

覆盖和动态DT函数
----------------

该API在以下内核代码中:

drivers/of/resolver.c

drivers/of/dynamic.c

drivers/of/overlay.c

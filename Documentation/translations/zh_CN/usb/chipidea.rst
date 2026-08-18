.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/usb/chipidea.rst

:翻译:

 白钶凡 Kefan Bai <baikefan@leap-io-kernel.com>

:校译:


=============================
ChipIdea 高速双角色控制器驱动
=============================

1. 如何测试 OTG FSM（HNP 和 SRP）
---------------------------------

下面以两块 Freescale i.MX6Q Sabre SD 开发板为例，
说明如何通过 sysfs 输入文件演示 OTG 的 HNP 和 SRP 功能。

1.1 如何使能 OTG FSM
--------------------

1.1.1 在 ``menuconfig`` 中选择 ``CONFIG_USB_OTG_FSM``，并重新编译内核
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

重新构建内核镜像和模块。如果想查看 OTG FSM 的
一些内部变量，可以挂载 ``debugfs``；其中有两个文件
可以显示 OTG FSM 变量以及部分控制器寄存器的值::

	cat /sys/kernel/debug/ci_hdrc.0/otg
	cat /sys/kernel/debug/ci_hdrc.0/registers

1.1.2 在控制器节点对应的 ``dts`` 文件中添加以下条目
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

	otg-rev = <0x0200>;
	adp-disable;

1.2 测试步骤
------------

1) 给两块 Freescale i.MX6Q Sabre SD 开发板上电，
   并加载 gadget 类驱动（例如 ``g_mass_storage``）。

2) 用 USB 线连接两块开发板：
   一端是 micro A 插头，另一端是 micro B 插头。

   插入 micro A 插头的一端是 A 设备，它应枚举另一端的 B 设备。

3) 角色切换

   在 B 设备上执行::

	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/b_bus_req

   B 设备应接管主机角色并枚举 A 设备。

4) A 设备切回主机角色

   在 B 设备上执行::

	echo 0 > /sys/bus/platform/devices/ci_hdrc.0/inputs/b_bus_req

   或者，通过引入 HNP 轮询，B 端主机可以知道
   A 端外设希望切换为主机角色，因此这次角色切换
   也可以通过 A 端外设响应 B 端主机的轮询，
   在 A 侧触发。
   这可以通过在 A 设备上执行下面的命令来完成::

	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/a_bus_req

   A 设备应切回主机角色并枚举 B 设备。

5) 拔掉 B 设备（拔掉 micro B 插头），
   并在 10 秒内重新插入；
   A 设备应重新枚举 B 设备。

6) 拔掉 B 设备（拔掉 micro B 插头），
   并在 10 秒后重新插入；
   A 设备不应重新枚举 B 设备。

   如果 A 设备希望使用总线：

   在 A 设备上执行::

	echo 0 > /sys/bus/platform/devices/ci_hdrc.0/inputs/a_bus_drop
	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/a_bus_req

   如果 B 设备希望使用总线：

   在 B 设备上执行::

	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/b_bus_req

7) A 设备关闭总线供电

   在 A 设备上执行::

	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/a_bus_drop

   A 设备应断开与 B 设备的连接，并关闭总线供电。

8) B 设备发出 SRP 数据脉冲

   在 B 设备上执行::

	echo 1 > /sys/bus/platform/devices/ci_hdrc.0/inputs/b_bus_req

   A 设备应恢复 USB 总线并枚举 B 设备。

1.3 参考文档
------------
《On-The-Go and Embedded Host Supplement
to the USB Revision 2.0 Specification
July 27, 2012 Revision 2.0 version 1.1a》

2. 如何将 USB 用作系统唤醒源
----------------------------
下面是在 i.MX6 平台上把 USB 用作系统唤醒源的示例。

2.1 使能核心控制器的唤醒功能::

	echo enabled > /sys/bus/platform/devices/ci_hdrc.0/power/wakeup

2.2 使能 glue 层的唤醒功能::

	echo enabled > /sys/bus/platform/devices/2184000.usb/power/wakeup

2.3 使能 PHY 的唤醒功能（可选）::

	echo enabled > /sys/bus/platform/devices/20c9000.usbphy/power/wakeup

2.4 使能根集线器的唤醒功能::

	echo enabled > /sys/bus/usb/devices/usb1/power/wakeup

2.5 使能相关设备的唤醒功能::

	echo enabled > /sys/bus/usb/devices/1-1/power/wakeup

如果系统只有一个 USB 端口，
而你希望在该端口上启用 USB 唤醒功能，
可以使用下面的脚本::

	for i in $(find /sys -name wakeup | grep usb);do echo enabled > $i;done;

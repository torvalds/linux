.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/ipv6.rst

:翻译:

	陈思为 Siwei Chen <businiaoanka@anka1.top>

====
IPv6
====


ipv6 模块的选项在加载时以参数形式提供。

模块选项可作为 insmod 或 modprobe 命令的命令行参数给出，但通常在
``/etc/modules.d/*.conf`` 配置文件中，或在发行版特定的配置文件中指定。

可用的 ipv6 模块参数如下所列。若未指定某参数，则使用其默认值。

参数如下：

disable

	指定是否在加载 IPv6 模块的同时禁用其全部功能。当另一模块依赖于 IPv6
	模块已加载，但又不需要任何 IPv6 地址或操作时，可使用此选项。

	可选值及其效果如下：

	0
		启用 IPv6。

		这是默认值。

	1
		禁用 IPv6。

		接口不会添加 IPv6 地址，也无法打开 IPv6 套接字。

		需要重启才能启用 IPv6。

autoconf

	指定是否在所有接口上启用 IPv6 地址自动配置。当不希望根据路由器通告
	（Router Advertisement）中收到的前缀自动生成地址时，可使用此选项。

	可选值及其效果如下：

	0
		所有接口均禁用 IPv6 地址自动配置。

		仅添加 IPv6 回环地址（::1）与链路本地地址。

	1
		所有接口均启用 IPv6 地址自动配置。

		这是默认值。

disable_ipv6

	指定是否在所有接口上禁用 IPv6。
	当不需要任何 IPv6 地址时，可使用此选项。

	可选值及其效果如下：

	0
		在所有接口上启用 IPv6。

		这是默认值。

	1
		在所有接口上禁用 IPv6。

		接口不会添加 IPv6 地址。

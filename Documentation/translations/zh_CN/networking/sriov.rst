.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/sriov.rst

:翻译:

	陈思为 Siwei Chen <businiaoanka@anka1.top>

===============
网卡 SR-IOV API
===============

强烈建议现代网卡专注于实现 ``switchdev`` 模型
（参见 `以太网交换机设备驱动模型（switchdev）`_）
来配置 SR-IOV 功能的转发与安全。

传统 API
========

旧的 SR-IOV API 在 ``rtnetlink`` Netlink 协议族中实现，是 ``RTM_GETLINK`` 和
``RTM_SETLINK`` 命令的一部分。在驱动侧，它由若干 ``ndo_set_vf_*`` 和
``ndo_get_vf_*`` 回调组成。

由于传统 API 与协议栈其余部分集成不佳，该 API 被视为已冻结；不再接受任何新功能或扩展。
新驱动不应实现那些不常用的回调；
即以下回调禁止使用：

 - ``ndo_get_vf_port``
 - ``ndo_set_vf_port``
 - ``ndo_set_vf_rss_query_en``

.. _`以太网交换机设备驱动模型（switchdev）`: https://docs.kernel.org/networking/switchdev.html#switchdev

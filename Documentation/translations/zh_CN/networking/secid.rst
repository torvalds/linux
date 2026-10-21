.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/secid.rst

:翻译:

	陈思为 Siwei Chen <businiaoanka@anka1.top>

=================
LSM/SeLinux secid
=================

flowi 结构体：

flowi 结构体中的 secid 成员在 LSM（如 SELinux）中用于表示该流的标签。
该流的标签目前用于选择相匹配的带标签 xfrm。

若为出向流，标签派生自套接字（若有）；如果该流是为响应某个入站报文而生成的，
则标签派生自相应的入站报文（例如 TCP 复位、timewait ACK 等）。
在特殊情况下，也可酌情让标签派生自其他来源，如进程上下文、设备等。

若为入向流，标签派生自报文所使用的 IPSec 安全关联（若存在）。

.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/vxlan.rst

:翻译:

 范雨 Fan Yu <fan.yu9@zte.com.cn>

:校译:

 - 邱禹潭 Qiu Yutan <qiu.yutan@zte.com.cn>
 - 徐鑫 xu xin <xu.xin16@zte.com.cn>

==========================
虚拟扩展本地局域网协议文档
==========================

VXLAN 协议是一种隧道协议，旨在解决 IEEE 802.1q 中 VLAN ID（4096）有限的问题。
VXLAN 将标识符的大小扩展到 24 位（16777216）。

VXLAN 在 IETF RFC 7348 中进行了描述，并已由多家供应商设计实现。
该协议通过 UDP 协议运行，并使用特定目的端口。
本文档介绍了 Linux 内核隧道设备，Openvswitch 也有单独的 VXLAN 实现。

与大多数隧道不同，VXLAN 是 1 对 N 的网络，而不仅仅是点对点网络。
VXLAN 设备可以通过类似于学习桥接器的方式动态学习另一端点的 IP 地址，也可以利用静态配置的转发条目。

VXLAN 的管理方式与它的两个近邻 GRE 和 VLAN 相似。
配置 VXLAN 需要 iproute2 的版本与 VXLAN 首次向上游合并的内核版本相匹配。

1. 创建 vxlan 设备::

	# ip link add vxlan0 type vxlan id 42 group 239.1.1.1 dev eth1 dstport 4789

这将创建一个名为 vxlan0 的网络设备，该设备通过 eth1 使用组播组 239.1.1.1 处理转发表中没有对应条目的流量。
目标端口号设置为 IANA 分配的值 4789，VXLAN 的 Linux 实现早于 IANA 选择标准目的端口号的时间。
因此默认使用 Linux 选择的值，以保持向后兼容性。

2. 删除 vxlan 设备::

	# ip link delete vxlan0

3. 查看 vxlan 设备信息::

	# ip -d link show vxlan0

使用新的 bridge 命令可以创建、销毁和显示 vxlan 转发表。

1. 创建vxlan转发表项::

	# bridge fdb add to 00:17:42:8a:b4:05 dst 192.19.0.2 dev vxlan0

2. 删除vxlan转发表项::

	# bridge fdb delete 00:17:42:8a:b4:05 dev vxlan0

3. 显示vxlan转发表项::

	# bridge fdb show dev vxlan0

以下网络接口控制器特性可能表明对 UDP 隧道相关的卸载支持（最常见的是 VXLAN 功能，
但是对特定封装协议的支持取决于网络接口控制器）：

 - `tx-udp_tnl-segmentation`
 - `tx-udp_tnl-csum-segmentation`
    对 UDP 封装帧执行 TCP 分段卸载的能力

 - `rx-udp_tunnel-port-offload`
    在接收端解析 UDP 封装帧，使网络接口控制器能够执行协议感知卸载，
    例如内部帧的校验和验证卸载（只有不带协议感知卸载的网络接口控制器才需要）

对于支持 `rx-udp_tunnel-port-offload` 的设备，可使用 `ethtool` 查询当前卸载端口的列表::

  $ ethtool --show-tunnels eth0
  Tunnel information for eth0:
    UDP port table 0:
      Size: 4
      Types: vxlan
      No entries
    UDP port table 1:
      Size: 4
      Types: geneve, vxlan-gpe
      Entries (1):
          port 1230, vxlan-gpe

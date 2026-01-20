.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/networking/generic-hdlc.rst

:翻译:

   孙渔喜 Sun yuxi <sun.yuxi@zte.com.cn>

==========
通用HDLC层
==========

Krzysztof Halasa <khc@pm.waw.pl>


通用HDLC层当前支持以下协议:

1. 帧中继（支持ANSI、CCITT、Cisco及无LMI模式）

   - 常规（路由）接口和以太网桥接（以太网设备仿真）接口
     可共享同一条PVC。
   - 支持ARP（内核暂不支持InARP，但可通过实验性用户空间守护程序实现，
     下载地址：http://www.kernel.org/pub/linux/utils/net/hdlc/）。

2. 原始HDLC —— 支持IP（IPv4）接口或以太网设备仿真
3. Cisco HDLC
4. PPP
5. X.25（使用X.25协议栈）

通用HDLC仅作为协议驱动 - 必须配合具体硬件的底层驱动
才能运行。

以太网设备仿真（使用HDLC或帧中继PVC）兼容IEEE 802.1Q（VLAN）和
802.1D（以太网桥接）。


请确保已加载 hdlc.o 和硬件驱动程序。系统将为每个WAN端口创建一个
"hdlc"网络设备（如hdlc0等）。您需要使用"sethdlc"工具，可从以下
地址获取：

	http://www.kernel.org/pub/linux/utils/net/hdlc/

编译 sethdlc.c 工具::

	gcc -O2 -Wall -o sethdlc sethdlc.c

请确保使用与您内核版本匹配的 sethdlc 工具。

使用 sethdlc 工具设置物理接口、时钟频率、HDLC 模式，
若使用帧中继还需添加所需的 PVC。
通常您需要执行类似以下命令::

	sethdlc hdlc0 clock int rate 128000
	sethdlc hdlc0 cisco interval 10 timeout 25

或::

	sethdlc hdlc0 rs232 clock ext
	sethdlc hdlc0 fr lmi ansi
	sethdlc hdlc0 create 99
	ifconfig hdlc0 up
	ifconfig pvc0 localIP pointopoint remoteIP

在帧中继模式下，请先启用主hdlc设备（不分配IP地址），再
使用pvc设备。


接口设置选项：

* v35 | rs232 | x21 | t1 | e1
    - 当网卡支持软件可选接口时，可为指定端口设置物理接口
  loopback
    - 启用硬件环回（仅用于测试）
* clock ext
    - RX与TX时钟均使用外部时钟源
* clock int
    - RX与TX时钟均使用内部时钟源
* clock txint
    - RX时钟使用外部时钟源，TX时钟使用内部时钟源
* clock txfromrx
    - RX时钟使用外部时钟源，TX时钟从RX时钟派生
* rate
    - 设置时钟速率（仅适用于"int"或"txint"时钟模式）


设置协议选项：

* hdlc - 设置原始HDLC模式（仅支持IP协议）

  nrz / nrzi / fm-mark / fm-space / manchester - 传输编码选项

  no-parity / crc16 / crc16-pr0 (预设零值的CRC16) / crc32-itu

  crc16-itu (使用ITU-T多项式的CRC16) / crc16-itu-pr0 - 校验方式选项

* hdlc-eth - 使用HDLC进行以太网设备仿真. 校验和编码方式同上
  as above.

* cisco - 设置Cisco HDLC模式（支持IP、IPv6和IPX协议）

  interval - 保活数据包发送间隔（秒）

  timeout - 未收到保活数据包的超时时间（秒），超过此时长将判定
	    链路断开

* ppp - 设置同步PPP模式

* x25 - 设置X.25模式

* fr - 帧中继模式

  lmi ansi / ccitt / cisco / none - LMI(链路管理)类型

  dce - 将帧中继设置为DCE（网络侧）LMI模式（默认为DTE用户侧）。

  此设置与时钟无关！

  - t391 - 链路完整性验证轮询定时器（秒）- 用户侧
  - t392 - 轮询验证定时器（秒）- 网络侧
  - n391 - 全状态轮询计数器 - 用户侧
  - n392 - 错误阈值 - 用户侧和网络侧共用
  - n393 - 监控事件计数 - 用户侧和网络侧共用

帧中继专用命令:

* create n | delete n - 添加/删除DLCI编号为n的PVC接口。
  新创建的接口将命名为pvc0、pvc1等。

* create ether n | delete ether n - 添加/删除用于以太网
  桥接帧的设备设备将命名为pvceth0、pvceth1等。




板卡特定问题
------------

n2.o 和 c101.o 驱动模块需要参数才能工作::

	insmod n2 hw=io,irq,ram,ports[:io,irq,...]

示例::

	insmod n2 hw=0x300,10,0xD0000,01

或::

	insmod c101 hw=irq,ram[:irq,...]

示例::

	insmod c101 hw=9,0xdc000

若直接编译进内核，这些驱动需要通过内核(命令行)参数配置::

	n2.hw=io,irq,ram,ports:...

或::

	c101.hw=irq,ram:...



若您的N2、C101或PLX200SYN板卡出现问题，可通过"private"
命令查看端口数据包描述符环（显示在内核日志中）

	sethdlc hdlc0 private

硬件驱动需使用#define DEBUG_RINGS编译选项构建。
在提交错误报告时附上这些信息将很有帮助。如在使用过程中遇
到任何问题，请随时告知。

获取补丁和其他信息，请访问：
<http://www.kernel.org/pub/linux/utils/net/hdlc/>.
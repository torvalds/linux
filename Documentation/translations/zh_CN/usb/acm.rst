.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/usb/acm.rst

:翻译:

 白钶凡 Kefan Bai <baikefan@leap-io-kernel.com>

:校译:


====================
Linux ACM 驱动 v0.16
====================

版权所有 (c) 1999 Vojtech Pavlik <vojtech@suse.cz>

由 SuSE 赞助

0. 免责声明
~~~~~~~~~~~
本程序是自由软件；你可以在自由软件基金会发布的
GNU 通用公共许可证第 2 版，或者（按你的选择）
任何后续版本的条款下重新发布和/或修改它。

发布本程序是希望它能发挥作用，但它不附带任何担保；
甚至不包括对适销性或特定用途适用性的默示担保。
详情见 GNU 通用公共许可证。

你应该已经随本程序收到了 GNU 通用公共许可证的副本；
如果没有，请致信：Free Software Foundation, Inc., 59
Temple Place, Suite 330, Boston, MA 02111-1307 USA。

如需联系作者，可发送电子邮件至 vojtech@suse.cz，
或邮寄至：
Vojtech Pavlik, Ucitelska 1576, Prague 8,
182 00, Czech Republic。

为方便起见，软件包中已附带 GNU 通用公共许可证
第 2 版：见 COPYING 文件。

1. 使用方法
~~~~~~~~~~~
``drivers/usb/class/cdc-acm.c`` 驱动可用于符合 USB
通信设备类抽象控制模型（USB CDC ACM）规范的
USB 调制解调器和 USB ISDN 终端适配器。

许多调制解调器支持此驱动，以下是我所知道的一些型号：

	- 3Com OfficeConnect 56k
	- 3Com Voice FaxModem Pro
	- 3Com Sportster
	- MultiTech MultiModem 56k
	- Zoom 2986L FaxModem
	- Compaq 56k FaxModem
	- ELSA Microlink 56k

我知道有一款 ISDN 终端适配器可以与 ACM 驱动一起使用：

	- 3Com USR ISDN Pro TA

一些手机也可以通过 USB 连接。
我知道以下机型可以正常工作：

	- SonyEricsson K800i

遗憾的是，许多调制解调器和大多数 ISDN TA
都使用专有接口，因此无法与此驱动配合工作。
购买前请先确认设备是否符合 ACM 规范。

要使用这些调制解调器，需要加载以下模块::

	usbcore.ko
	uhci-hcd.ko ohci-hcd.ko or ehci-hcd.ko
	cdc-acm.ko

之后就应该可以访问这些调制解调器了。
应当可以使用 ``minicom``、``ppp`` 和 ``mgetty``
与它们通信。

2. 验证驱动是否正常工作
~~~~~~~~~~~~~~~~~~~~~~~

第一步是检查 ``/sys/kernel/debug/usb/devices``，
其内容应该类似如下::

  T:  Bus=01 Lev=00 Prnt=00 Port=00 Cnt=00 Dev#=  1 Spd=12  MxCh= 2
  B:  Alloc=  0/900 us ( 0%), #Int=  0, #Iso=  0
  D:  Ver= 1.00 Cls=09(hub  ) Sub=00 Prot=00 MxPS= 8 #Cfgs=  1
  P:  Vendor=0000 ProdID=0000 Rev= 0.00
  S:  Product=USB UHCI Root Hub
  S:  SerialNumber=6800
  C:* #Ifs= 1 Cfg#= 1 Atr=40 MxPwr=  0mA
  I:  If#= 0 Alt= 0 #EPs= 1 Cls=09(hub  ) Sub=00 Prot=00 Driver=hub
  E:  Ad=81(I) Atr=03(Int.) MxPS=   8 Ivl=255ms
  T:  Bus=01 Lev=01 Prnt=01 Port=01 Cnt=01 Dev#=  2 Spd=12  MxCh= 0
  D:  Ver= 1.00 Cls=02(comm.) Sub=00 Prot=00 MxPS= 8 #Cfgs=  2
  P:  Vendor=04c1 ProdID=008f Rev= 2.07
  S:  Manufacturer=3Com Inc.
  S:  Product=3Com U.S. Robotics Pro ISDN TA
  S:  SerialNumber=UFT53A49BVT7
  C:  #Ifs= 1 Cfg#= 1 Atr=60 MxPwr=  0mA
  I:  If#= 0 Alt= 0 #EPs= 3 Cls=ff(vend.) Sub=ff Prot=ff Driver=acm
  E:  Ad=85(I) Atr=02(Bulk) MxPS=  64 Ivl=  0ms
  E:  Ad=04(O) Atr=02(Bulk) MxPS=  64 Ivl=  0ms
  E:  Ad=81(I) Atr=03(Int.) MxPS=  16 Ivl=128ms
  C:* #Ifs= 2 Cfg#= 2 Atr=60 MxPwr=  0mA
  I:  If#= 0 Alt= 0 #EPs= 1 Cls=02(comm.) Sub=02 Prot=01 Driver=acm
  E:  Ad=81(I) Atr=03(Int.) MxPS=  16 Ivl=128ms
  I:  If#= 1 Alt= 0 #EPs= 2 Cls=0a(data ) Sub=00 Prot=00 Driver=acm
  E:  Ad=85(I) Atr=02(Bulk) MxPS=  64 Ivl=  0ms
  E:  Ad=04(O) Atr=02(Bulk) MxPS=  64 Ivl=  0ms

这三行的存在很关键（以及 ``Cls=`` 字段里出现的
``comm`` 和 ``data`` 类）；它说明这是一个 ACM
设备。``Driver=acm`` 表示该设备正在使用 acm 驱动。
如果只看到 ``Cls=ff(vend.)``，那就无能为力了：
这说明你手上的设备使用的是厂商专有接口::

    D:  Ver= 1.00 Cls=02(comm.) Sub=00 Prot=00 MxPS= 8 #Cfgs=  2
    I:  If#= 0 Alt= 0 #EPs= 1 Cls=02(comm.) Sub=02 Prot=01 Driver=acm
    I:  If#= 1 Alt= 0 #EPs= 2 Cls=0a(data ) Sub=00 Prot=00 Driver=acm

在系统日志中应该可以看到::

  usb.c: USB new device connect, assigned device number 2
  usb.c: kmalloc IF c7691fa0, numif 1
  usb.c: kmalloc IF c7b5f3e0, numif 2
  usb.c: skipped 4 class/vendor specific interface descriptors
  usb.c: new device strings: Mfr=1, Product=2, SerialNumber=3
  usb.c: USB device number 2 default language ID 0x409
  Manufacturer: 3Com Inc.
  Product: 3Com U.S. Robotics Pro ISDN TA
  SerialNumber: UFT53A49BVT7
  acm.c: probing config 1
  acm.c: probing config 2
  ttyACM0: USB ACM device
  acm.c: acm_control_msg: rq: 0x22 val: 0x0 len: 0x0 result: 0
  acm.c: acm_control_msg: rq: 0x20 val: 0x0 len: 0x7 result: 7
  usb.c: acm driver claimed interface c7b5f3e0
  usb.c: acm driver claimed interface c7b5f3f8
  usb.c: acm driver claimed interface c7691fa0

如果以上都正常，请启动 ``minicom``，
把它配置为连接 ``ttyACM`` 设备，然后
尝试输入 ``at``。如果返回 ``OK``，说明一切工作正常。

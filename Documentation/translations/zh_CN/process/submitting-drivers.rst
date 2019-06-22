.. _cn_submittingdrivers:

.. include:: ../disclaimer-zh_CN.rst

:Original: :ref:`Documentation/process/submitting-drivers.rst
           <submittingdrivers>`

如果想评论或更新本文的内容，请直接联系原文档的维护者。如果你使用英文
交流有困难的话，也可以向中文版维护者求助。如果本翻译更新不及时或者翻
译存在问题，请联系中文版维护者::

        中文版维护者： 李阳  Li Yang <leoyang.li@nxp.com>
        中文版翻译者： 李阳  Li Yang <leoyang.li@nxp.com>
        中文版校译者： 陈琦 Maggie Chen <chenqi@beyondsoft.com>
                       王聪 Wang Cong <xiyou.wangcong@gmail.com>
                       张巍 Zhang Wei <wezhang@outlook.com>

如何向 Linux 内核提交驱动程序
=============================

这篇文档将会解释如何向不同的内核源码树提交设备驱动程序。请注意，如果你感
兴趣的是显卡驱动程序，你也许应该访问 XFree86 项目(http://www.xfree86.org/)
和／或 X.org 项目 (http://x.org)。

另请参阅 Documentation/translations/zh_CN/process/submitting-patches.rst 文档。


分配设备号
----------

块设备和字符设备的主设备号与从设备号是由 Linux 命名编号分配权威 LANANA（
现在是 Torben Mathiasen）负责分配。申请的网址是 http://www.lanana.org/。
即使不准备提交到主流内核的设备驱动也需要在这里分配设备号。有关详细信息，
请参阅 Documentation/admin-guide/devices.rst。

如果你使用的不是已经分配的设备号，那么当你提交设备驱动的时候，它将会被强
制分配一个新的设备号，即便这个设备号和你之前发给客户的截然不同。

设备驱动的提交对象
------------------

Linux 2.0:
	此内核源码树不接受新的驱动程序。

Linux 2.2:
	此内核源码树不接受新的驱动程序。

Linux 2.4:
	如果所属的代码领域在内核的 MAINTAINERS 文件中列有一个总维护者，
	那么请将驱动程序提交给他。如果此维护者没有回应或者你找不到恰当的
	维护者，那么请联系 Willy Tarreau <w@1wt.eu>。

Linux 2.6:
	除了遵循和 2.4 版内核同样的规则外，你还需要在 linux-kernel 邮件
	列表上跟踪最新的 API 变化。向 Linux 2.6 内核提交驱动的顶级联系人
	是 Andrew Morton <akpm@linux-foundation.org>。

决定设备驱动能否被接受的条件
----------------------------

许可：		代码必须使用 GNU 通用公开许可证 (GPL) 提交给 Linux，但是
		我们并不要求 GPL 是唯一的许可。你或许会希望同时使用多种
		许可证发布，如果希望驱动程序可以被其他开源社区（比如BSD）
		使用。请参考 include/linux/module.h 文件中所列出的可被
		接受共存的许可。

版权：		版权所有者必须同意使用 GPL 许可。最好提交者和版权所有者
		是相同个人或实体。否则，必需列出授权使用 GPL 的版权所有
		人或实体，以备验证之需。

接口：		如果你的驱动程序使用现成的接口并且和其他同类的驱动程序行
		为相似，而不是去发明无谓的新接口，那么它将会更容易被接受。
		如果你需要一个 Linux 和 NT 的通用驱动接口，那么请在用
		户空间实现它。

代码：		请使用 Documentation/process/coding-style.rst 中所描述的 Linux 代码风
		格。如果你的某些代码段（例如那些与 Windows 驱动程序包共
		享的代码段）需要使用其他格式，而你却只希望维护一份代码，
		那么请将它们很好地区分出来，并且注明原因。

可移植性：	请注意，指针并不永远是 32 位的，不是所有的计算机都使用小
		尾模式 (little endian) 存储数据，不是所有的人都拥有浮点
		单元，不要随便在你的驱动程序里嵌入 x86 汇编指令。只能在
		x86 上运行的驱动程序一般是不受欢迎的。虽然你可能只有 x86
		硬件，很难测试驱动程序在其他平台上是否可用，但是确保代码
		可以被轻松地移植却是很简单的。

清晰度：	做到所有人都能修补这个驱动程序将会很有好处，因为这样你将
		会直接收到修复的补丁而不是 bug 报告。如果你提交一个试图
		隐藏硬件工作机理的驱动程序，那么它将会被扔进废纸篓。

电源管理：	因为 Linux 正在被很多移动设备和桌面系统使用，所以你的驱
		动程序也很有可能被使用在这些设备上。它应该支持最基本的电
		源管理，即在需要的情况下实现系统级休眠和唤醒要用到的
		.suspend 和 .resume 函数。你应该检查你的驱动程序是否能正
		确地处理休眠与唤醒，如果实在无法确认，请至少把 .suspend
		函数定义成返回 -ENOSYS（功能未实现）错误。你还应该尝试确
		保你的驱动在什么都不干的情况下将耗电降到最低。要获得驱动
		程序测试的指导，请参阅
		Documentation/power/drivers-testing.txt。有关驱动程序电
		源管理问题相对全面的概述，请参阅
		Documentation/driver-api/pm/devices.rst。

管理：		如果一个驱动程序的作者还在进行有效的维护，那么通常除了那
		些明显正确且不需要任何检查的补丁以外，其他所有的补丁都会
		被转发给作者。如果你希望成为驱动程序的联系人和更新者，最
		好在代码注释中写明并且在 MAINTAINERS 文件中加入这个驱动
		程序的条目。

不影响设备驱动能否被接受的条件
------------------------------

供应商：	由硬件供应商来维护驱动程序通常是一件好事。不过，如果源码
		树里已经有其他人提供了可稳定工作的驱动程序，那么请不要期
		望“我是供应商”会成为内核改用你的驱动程序的理由。理想的情
		况是：供应商与现有驱动程序的作者合作，构建一个统一完美的
		驱动程序。

作者：		驱动程序是由大的 Linux 公司研发还是由你个人编写，并不影
		响其是否能被内核接受。没有人对内核源码树享有特权。只要你
		充分了解内核社区，你就会发现这一点。


资源列表
--------

Linux 内核主源码树：
	ftp.??.kernel.org:/pub/linux/kernel/...
	?? == 你的国家代码，例如 "cn"、"us"、"uk"、"fr" 等等

Linux 内核邮件列表：
	linux-kernel@vger.kernel.org
	[可通过向majordomo@vger.kernel.org发邮件来订阅]

Linux 设备驱动程序，第三版（探讨 2.6.10 版内核）：
	http://lwn.net/Kernel/LDD3/ （免费版）

LWN.net:
	每周内核开发活动摘要 - http://lwn.net/

	2.6 版中 API 的变更：

		http://lwn.net/Articles/2.6-kernel-api/

	将旧版内核的驱动程序移植到 2.6 版：

		http://lwn.net/Articles/driver-porting/

内核新手(KernelNewbies):
	为新的内核开发者提供文档和帮助
	http://kernelnewbies.org/

Linux USB项目：
	http://www.linux-usb.org/

写内核驱动的“不要”（Arjan van de Ven著）:
	http://www.fenrus.org/how-to-not-write-a-device-driver-paper.pdf

内核清洁工 (Kernel Janitor):
	http://kernelnewbies.org/KernelJanitors

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/openrisc/openrisc_port.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_openrisc_port:

==============
OpenRISC Linux
==============

这是Linux对OpenRISC类微处理器的移植；具体来说，最早移植目标是32位
OpenRISC 1000系列（或1k）。

关于OpenRISC处理器和正在进行中的开发的信息:

	=======		=============================
	网站		https://openrisc.io
	邮箱		openrisc@lists.librecores.org
	=======		=============================

---------------------------------------------------------------------

OpenRISC工具链和Linux的构建指南
===============================

为了构建和运行Linux for OpenRISC，你至少需要一个基本的工具链，或许
还需要架构模拟器。 这里概述了准备就位这些部分的步骤。

1) 工具链

工具链二进制文件可以从openrisc.io或我们的github发布页面获得。不同
工具链的构建指南可以在openrisc.io或Stafford的工具链构建和发布脚本
中找到。

	======      =================================================
	二进制      https://github.com/openrisc/or1k-gcc/releases
	工具链      https://openrisc.io/software
	构建        https://github.com/stffrdhrn/or1k-toolchain-build
	======      =================================================

2) 构建

像往常一样构建Linux内核::

	make ARCH=openrisc CROSS_COMPILE="or1k-linux-" defconfig
	make ARCH=openrisc CROSS_COMPILE="or1k-linux-"

3) 在FPGA上运行（可选)

OpenRISC社区通常使用FuseSoC来管理构建和编程SoC到FPGA中。 下面是用
OpenRISC SoC对De0 Nano开发板进行编程的一个例子。 在构建过程中，
FPGA RTL是从FuseSoC IP核库中下载的代码，并使用FPGA供应商工具构建。
二进制文件用openocd加载到电路板上。

::

	git clone https://github.com/olofk/fusesoc
	cd fusesoc
	sudo pip install -e .

	fusesoc init
	fusesoc build de0_nano
	fusesoc pgm de0_nano

	openocd -f interface/altera-usb-blaster.cfg \
		-f board/or1k_generic.cfg

	telnet localhost 4444
	> init
	> halt; load_image vmlinux ; reset

4) 在模拟器上运行（可选）

QEMU是一个处理器仿真器，我们推荐它来模拟OpenRISC平台。 请按照QEMU网
站上的OpenRISC说明，让Linux在QEMU上运行。 你可以自己构建QEMU，但你的
Linux发行版可能提供了支持OpenRISC的二进制包。

	=============	======================================================
	qemu openrisc	https://wiki.qemu.org/Documentation/Platforms/OpenRISC
	=============	======================================================

---------------------------------------------------------------------

术语表
======

代码中使用了以下符号约定以将范围限制在几个特定处理器实现上：

========= =======================
openrisc: OpenRISC类型处理器
or1k:     OpenRISC 1000系列处理器
or1200:   OpenRISC 1200处理器
========= =======================

---------------------------------------------------------------------

历史
====

2003-11-18	Matjaz Breskvar (phoenix@bsemi.com)
   将linux初步移植到OpenRISC或32架构。
       所有的核心功能都实现了，并且可以使用。

2003-12-08	Matjaz Breskvar (phoenix@bsemi.com)
   彻底改变TLB失误处理。
   重写异常处理。
   在默认的initrd中实现了sash-3.6的所有功能。
   大幅改进的版本。

2004-04-10	Matjaz Breskvar (phoenix@bsemi.com)
   大量的bug修复。
   支持以太网，http和telnet服务器功能。
   可以运行许多标准的linux应用程序。

2004-06-26	Matjaz Breskvar (phoenix@bsemi.com)
   移植到2.6.x。

2004-11-30	Matjaz Breskvar (phoenix@bsemi.com)
   大量的bug修复和增强功能。
   增加了opencores framebuffer驱动。

2010-10-09    Jonas Bonn (jonas@southpole.se)
   重大重写，使其与上游的Linux 2.6.36看齐。

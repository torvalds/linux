.. include:: ../../disclaimer-zh_TW.rst

:Original: Documentation/arch/openrisc/openrisc_port.rst

:翻譯:

 司延騰 Yanteng Si <siyanteng@loongson.cn>

.. _tw_openrisc_port:

==============
OpenRISC Linux
==============

這是Linux對OpenRISC類微處理器的移植；具體來說，最早移植目標是32位
OpenRISC 1000系列（或1k）。

關於OpenRISC處理器和正在進行中的開發的信息:

	=======		=============================
	網站		https://openrisc.io
	郵箱		openrisc@lists.librecores.org
	=======		=============================

---------------------------------------------------------------------

OpenRISC工具鏈和Linux的構建指南
===============================

爲了構建和運行Linux for OpenRISC，你至少需要一個基本的工具鏈，或許
還需要架構模擬器。 這裏概述了準備就位這些部分的步驟。

1) 工具鏈

工具鏈二進制文件可以從openrisc.io或我們的github發佈頁面獲得。不同
工具鏈的構建指南可以在openrisc.io或Stafford的工具鏈構建和發佈腳本
中找到。

	======      =================================================
	二進制      https://github.com/openrisc/or1k-gcc/releases
	工具鏈      https://openrisc.io/software
	構建        https://github.com/stffrdhrn/or1k-toolchain-build
	======      =================================================

2) 構建

像往常一樣構建Linux內核::

	make ARCH=openrisc CROSS_COMPILE="or1k-linux-" defconfig
	make ARCH=openrisc CROSS_COMPILE="or1k-linux-"

3) 在FPGA上運行（可選)

OpenRISC社區通常使用FuseSoC來管理構建和編程SoC到FPGA中。 下面是用
OpenRISC SoC對De0 Nano開發板進行編程的一個例子。 在構建過程中，
FPGA RTL是從FuseSoC IP核庫中下載的代碼，並使用FPGA供應商工具構建。
二進制文件用openocd加載到電路板上。

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

4) 在模擬器上運行（可選）

QEMU是一個處理器仿真器，我們推薦它來模擬OpenRISC平臺。 請按照QEMU網
站上的OpenRISC說明，讓Linux在QEMU上運行。 你可以自己構建QEMU，但你的
Linux發行版可能提供了支持OpenRISC的二進制包。

	=============	======================================================
	qemu openrisc	https://wiki.qemu.org/Documentation/Platforms/OpenRISC
	=============	======================================================

---------------------------------------------------------------------

術語表
======

代碼中使用了以下符號約定以將範圍限制在幾個特定處理器實現上：

========= =======================
openrisc: OpenRISC類型處理器
or1k:     OpenRISC 1000系列處理器
or1200:   OpenRISC 1200處理器
========= =======================

---------------------------------------------------------------------

歷史
====

2003-11-18	Matjaz Breskvar (phoenix@bsemi.com)
   將linux初步移植到OpenRISC或32架構。
       所有的核心功能都實現了，並且可以使用。

2003-12-08	Matjaz Breskvar (phoenix@bsemi.com)
   徹底改變TLB失誤處理。
   重寫異常處理。
   在默認的initrd中實現了sash-3.6的所有功能。
   大幅改進的版本。

2004-04-10	Matjaz Breskvar (phoenix@bsemi.com)
   大量的bug修復。
   支持以太網，http和telnet服務器功能。
   可以運行許多標準的linux應用程序。

2004-06-26	Matjaz Breskvar (phoenix@bsemi.com)
   移植到2.6.x。

2004-11-30	Matjaz Breskvar (phoenix@bsemi.com)
   大量的bug修復和增強功能。
   增加了opencores framebuffer驅動。

2010-10-09    Jonas Bonn (jonas@southpole.se)
   重大重寫，使其與上游的Linux 2.6.36看齊。

